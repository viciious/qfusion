#include "snd_env_sampler.h"
#include "snd_local.h"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <limits>

static vec3_t oldListenerOrigin;
static vec3_t oldListenerVelocity;
static int listenerLeafNum = -1;
static bool wasListenerInLiquid;

class alignas( 8 )EffectsAllocator {
	static_assert( sizeof( ReverbEffect ) > sizeof( UnderwaterFlangerEffect ), "" );

	static constexpr auto ENTRY_SIZE =
		sizeof( ReverbEffect ) % 8 ? sizeof( ReverbEffect ) + ( 8 - ( sizeof( ReverbEffect ) % 8 ) ) : sizeof( ReverbEffect );

	// For every source two effect storage cells are reserved (for current and old effect).
	alignas( 8 ) uint8_t storage[2 * ENTRY_SIZE * MAX_SRC];
	// For every source contains an effect type for the corresponding entry, or zero if an entry is unused.
	ALint effectTypes[MAX_SRC][2];

	inline void *AllocEntry( const src_t *src, ALint effectTypes );
	inline void FreeEntry( const void *entry );

	// We could return a reference to an effect type array cell,
	// but knowledge of these 2 indices is useful for debugging
	inline void GetEntryIndices( const void *entry, int *sourceIndex, int *interleavedSlotIndex );
public:
	EffectsAllocator() {
		memset( storage, 0, sizeof( storage ) );
		memset( effectTypes, 0, sizeof( effectTypes ) );
	}

	UnderwaterFlangerEffect *NewFlangerEffect( const src_t *src ) {
		return new( AllocEntry( src, AL_EFFECT_FLANGER ) )UnderwaterFlangerEffect();
	}

	ReverbEffect *NewReverbEffect( const src_t *src ) {
		return new( AllocEntry( src, AL_EFFECT_REVERB ) )ReverbEffect();
	}

	inline void DeleteEffect( Effect *effect );
};

static EffectsAllocator effectsAllocator;

inline void *EffectsAllocator::AllocEntry( const src_t *src, ALint forType ) {
	static_assert( AL_EFFECT_FLANGER * AL_EFFECT_REVERB != 0, "0 effect type should mark an unused effect slot" );
	int sourceIndex = (int)( src - srclist );
	for( int i = 0; i < 2; ++i ) {
		// If the slot is free
		if( !effectTypes[sourceIndex][i] ) {
			// Mark is as used for the effect type
			effectTypes[sourceIndex][i] = forType;
			// There are 2 slots per a source, that's why entry index is 2 * source index + slot index
			// The entry offset is in entries and not bytes,
			// so multiply it by ENTRY_SIZE when getting an offset in a byte array.
			return &storage[(sourceIndex * 2 + i) * ENTRY_SIZE];
		}
	}

	trap_Error( "EffectsAllocator::AllocEntry(): There are no free slots for an effect\n" );
}

inline void EffectsAllocator::DeleteEffect( Effect *effect ) {
	if( effect ) {
		FreeEntry( effect );
		effect->~Effect();
	}
}

inline void EffectsAllocator::GetEntryIndices( const void *entry, int *sourceIndex, int *interleavedSlotIndex ) {
	assert( entry );

#ifndef PUBLIC_BUILD
	if( (uint8_t *)entry < storage || (uint8_t *)entry >= storage + sizeof( storage ) ) {
		const char *format = "EffectsAllocator::FreeEntry(): An entry %p is out of storage bounds [%p, %p)\n";
		trap_Error( va( format, entry, storage, storage + sizeof( storage ) ) );
	}
#endif

	// An index of the entry cell in the entire storage
	int entryIndex = (int)( ( (uint8_t *)entry - storage ) / ENTRY_SIZE );
	// An index of the corresponding source
	*sourceIndex = entryIndex / 2;
	// An index of the effect, 0 or 1
	*interleavedSlotIndex = entryIndex - ( *sourceIndex * 2 );
}

inline void EffectsAllocator::FreeEntry( const void *entry ) {
	int sourceIndex, interleavedSlotIndex;
	GetEntryIndices( entry, &sourceIndex, &interleavedSlotIndex );

	// Get a reference to the types array cell for the entry
	ALint *const effectTypeRef = &effectTypes[sourceIndex][interleavedSlotIndex];

#ifndef PUBLIC_BUILD
	if( *effectTypeRef != AL_EFFECT_REVERB && *effectTypeRef != AL_EFFECT_FLANGER ) {
		const char *format = "EffectsAllocator::FreeEntry(): An effect for source #%d and slot %d is not in use\n";
		trap_Error( va( format, sourceIndex, interleavedSlotIndex ) );
	}
#endif

	// Set zero effect type for the entry
	*effectTypeRef = 0;
}

#define MAX_DIRECT_OBSTRUCTION_SAMPLES ( 8 )
#define MAX_REVERB_PRIMARY_RAY_SAMPLES ( 48 )

static vec3_t randomDirectObstructionOffsets[(1 << 8)];
static vec3_t randomReverbPrimaryRayDirs[(1 << 16)];

#ifndef M_2_PI
#define M_2_PI ( 2.0 * ( M_PI ) )
#endif

static inline void MakeRandomDirection( vec3_t dir ) {
	float theta = ( float )( M_2_PI * 0.999999 * random() );
	float phi = (float)( M_PI * random() );
	float sinTheta = sinf( theta );
	float cosTheta = cosf( theta );
	float sinPhi = sinf( phi );
	float cosPhi = cosf( phi );

	dir[0] = sinTheta * cosPhi;
	dir[1] = sinTheta * sinPhi;
	dir[2] = cosTheta;
}

#ifndef ARRAYSIZE
#define ARRAYSIZE( x ) ( sizeof( x ) / sizeof( x[0] ) )
#endif

static void ENV_InitRandomTables() {
	unsigned i, j;

	for( i = 0; i < ARRAYSIZE( randomDirectObstructionOffsets ); i++ ) {
		for( j = 0; j < 3; ++j ) {
			randomDirectObstructionOffsets[i][j] = -20.0f + 40.0f * random();
		}
	}

	for( i = 0; i < ARRAYSIZE( randomReverbPrimaryRayDirs ); i++ ) {
		MakeRandomDirection( randomReverbPrimaryRayDirs[i] );
	}
}

void ENV_Init() {
	if( !s_environment_effects->integer ) {
		return;
	}

	VectorClear( oldListenerOrigin );
	VectorClear( oldListenerVelocity );
	wasListenerInLiquid = false;

	ENV_InitRandomTables();

	listenerLeafNum = -1;
}

void ENV_Shutdown() {
	listenerLeafNum = -1;
}

void ENV_RegisterSource( src_t *src ) {
	// Invalidate last update when reusing the source
	// (otherwise it might be misused for props interpolation)
	src->envUpdateState.lastEnvUpdateAt = 0;
	// Force an immediate update
	src->envUpdateState.nextEnvUpdateAt = 0;
	// Reset sampling patterns by setting an illegal quality value
	src->envUpdateState.directObstructionSamplingProps.quality = -1.0f;
	src->envUpdateState.reverbPrimaryRaysSamplingProps.quality = -1.0f;
}

void ENV_UnregisterSource( src_t *src ) {
	if( !s_environment_effects->integer ) {
		return;
	}

	// Prevent later occasional updates
	src->envUpdateState.nextEnvUpdateAt = std::numeric_limits<int64_t>::max();

	effectsAllocator.DeleteEffect( src->envUpdateState.oldEffect );
	src->envUpdateState.oldEffect = nullptr;
	effectsAllocator.DeleteEffect( src->envUpdateState.effect );
	src->envUpdateState.effect = nullptr;

	// Detach the slot from the source
	qalSource3i( src->source, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL );
	// Detach the effect from the slot
	qalAuxiliaryEffectSloti( src->effectSlot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL );
	// Detach the direct filter
	qalSourcei( src->source, AL_DIRECT_FILTER, AL_FILTER_NULL );
	// Restore the original source gain
	qalSourcef( src->source, AL_GAIN, src->fvol * s_volume->value );
}

class SourcesUpdatePriorityQueue {
	struct ComparableSource {
		src_t *src;
		ComparableSource(): src( nullptr ) {}
		ComparableSource( src_t *src_ ): src( src_ ) {}

		bool operator<( const ComparableSource &that ) const {
			// We use a max-heap, so the natural comparison order for priorities is the right one
			return src->envUpdateState.priorityInQueue < that.src->envUpdateState.priorityInQueue;
		}
	};

	ComparableSource heap[MAX_SRC];
	int numSourcesInHeap;
public:
	SourcesUpdatePriorityQueue() {
		Clear();
	}

	void Clear() { numSourcesInHeap = 0; }

	void AddSource( src_t *src, float urgencyScale );
	src_t *PopSource();
};

static SourcesUpdatePriorityQueue sourcesUpdatePriorityQueue;

static void ENV_ProcessUpdatesPriorityQueue();

static void ENV_UpdateSourceEnvironment( src_t *src, int64_t millisNow, const src_t *tryReusePropsSrc );
static float ENV_ComputeDirectObstruction( src_t *src );
static void ENV_ComputeReverberation( src_t *src, ReverbEffect *effect );

static inline void ENV_CollectForcedEnvironmentUpdates() {
	src_t *src, *end;

	auto &priorityQueue = ::sourcesUpdatePriorityQueue;

	for( src = srclist, end = srclist + src_count; src != end; ++src ) {
		if( !src->isActive ) {
			continue;
		}

		if( src->priority != SRCPRI_LOCAL ) {
			priorityQueue.AddSource( src, 1.0f );
			continue;
		}

		if( !src->envUpdateState.nextEnvUpdateAt ) {
			priorityQueue.AddSource( src, 1.0f );
			continue;
		}
	}
}

static void ENV_CollectRegularEnvironmentUpdates() {
	src_t *src, *end;
	envUpdateState_t *updateState;
	int64_t millisNow;
	int contents;
	bool wasInLiquid, isInLiquid;

	millisNow = trap_Milliseconds();

	auto &priorityQueue = ::sourcesUpdatePriorityQueue;

	for( src = srclist, end = srclist + src_count; src != end; ++src ) {
		if( !src->isActive ) {
			continue;
		}

		updateState = &src->envUpdateState;
		if( src->priority == SRCPRI_LOCAL ) {
			// If this source has never been updated, add it to the queue, otherwise skip further updates.
			if( !updateState->nextEnvUpdateAt ) {
				priorityQueue.AddSource( src, 5.0f );
			}
			continue;
		}

		contents = trap_PointContents( src->origin );
		isInLiquid = ( contents & ( CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_WATER ) ) != 0;
		wasInLiquid = updateState->wasInLiquid;
		updateState->wasInLiquid = isInLiquid;
		if( isInLiquid ^ wasInLiquid ) {
			priorityQueue.AddSource( src, 2.0f );
			continue;
		}

		if( updateState->nextEnvUpdateAt <= millisNow ) {
			// If the playback has been just added
			if( !updateState->nextEnvUpdateAt ) {
				priorityQueue.AddSource( src, 5.0f );
			} else {
				priorityQueue.AddSource( src, 1.0f );
			}
			continue;
		}

		// If the sound is not fixed
		if( updateState->entNum >= 0 ) {
			// If the sound origin has been significantly modified
			if( DistanceSquared( src->origin, updateState->lastUpdateOrigin ) > 128 * 128 ) {
				// Hack! Prevent fast-moving entities (that are very likely PG projectiles)
				// to consume the entire updates throughput
				if( VectorLengthSquared( src->velocity ) < 700 * 700 ) {
					priorityQueue.AddSource( src, 1.5f );
				}
				continue;
			}

			// If the entity velocity has been significantly modified
			if( DistanceSquared( src->velocity, updateState->lastUpdateVelocity ) > 200 * 200 ) {
				priorityQueue.AddSource( src, 1.5f );
				continue;
			}
		}
	}
}

void SourcesUpdatePriorityQueue::AddSource( src_t *src, float urgencyScale ) {
	float attenuationScale;

	assert( urgencyScale >= 0.0f );

	attenuationScale = src->attenuation / 20.0f;
	clamp_high( attenuationScale, 1.0f );
	attenuationScale = sqrtf( attenuationScale );
	assert( attenuationScale >= 0.0f && attenuationScale <= 1.0f );

	src->envUpdateState.priorityInQueue = urgencyScale;
	src->envUpdateState.priorityInQueue *= 1.0f - 0.7f * attenuationScale;

	// Construct a ComparableSource at the end of the heap array
	void *mem = heap + numSourcesInHeap++;
	new( mem )ComparableSource( src );
	// Update the heap
	std::push_heap( heap, heap + numSourcesInHeap );
}

src_t *SourcesUpdatePriorityQueue::PopSource() {
	if( !numSourcesInHeap ) {
		return nullptr;
	}

	// Pop the max element from the heap
	std::pop_heap( heap, heap + numSourcesInHeap );
	// Chop last heap array element (it does not belong to the heap anymore)
	numSourcesInHeap--;
	// Return the just truncated element
	return heap[numSourcesInHeap].src;
}

static void ENV_ProcessUpdatesPriorityQueue() {
	const uint64_t micros = trap_Microseconds();
	const int64_t millis = (int64_t)( micros / 1000 );
	src_t *src;

	// Reset the listener leaf num for the new update session.
	// This prevents reusing outdated leaf nums,
	// especially after a map change when it might even lead to crash,
	// and allows caching the leaf for this update session,
	// since we know that a valid value of the leaf is always up-to-date.
	// The leaf will be computed on demand if its necessary.
	listenerLeafNum = -1;

	const sfx_t *lastProcessedSfx = nullptr;
	const src_t *lastProcessedSrc = nullptr;
	float lastProcessedPriority = std::numeric_limits<float>::max();
	// Always do at least a single update
	for( ;; ) {
		if( !( src = sourcesUpdatePriorityQueue.PopSource() ) ) {
			break;
		}

		const src_t *tryReusePropsSrc = nullptr;
		if( src->sfx == lastProcessedSfx ) {
			tryReusePropsSrc = lastProcessedSrc;
		}

		assert( lastProcessedPriority >= src->envUpdateState.priorityInQueue );
		lastProcessedPriority = src->envUpdateState.priorityInQueue;
		lastProcessedSfx = src->sfx;
		lastProcessedSrc = src;

		ENV_UpdateSourceEnvironment( src, millis, tryReusePropsSrc );
		// Stop updates if the time quota has been exceeded immediately.
		// Do not block the commands queue processing.
		// The priority queue will be rebuilt next ENV_UpdateListenerCall().
		if( trap_Microseconds() - micros > 2000 ) {
			break;
		}
	}
}

void ENV_UpdateRelativeSoundsSpatialization( const vec3_t origin, const vec3_t velocity ) {
	src_t *src, *end;

	for( src = srclist, end = srclist + src_count; src != end; ++src ) {
		if( !src->isActive ) {
			continue;
		}
		if( src->attenuation ) {
			continue;
		}
		VectorCopy( origin, src->origin );
		VectorCopy( velocity, src->velocity );
	}
}

void ENV_UpdateListener( const vec3_t origin, const vec3_t velocity ) {
	vec3_t testedOrigin;
	bool needsForcedUpdate = false;
	bool isListenerInLiquid;

	if( !s_environment_effects->integer ) {
		return;
	}

	ENV_UpdateRelativeSoundsSpatialization( origin, velocity );

	// Check whether we have teleported or entered/left a liquid.
	// Run a forced major update in this case.

	if( DistanceSquared( origin, oldListenerOrigin ) > 100.0f * 100.0f ) {
		needsForcedUpdate = true;
	} else if( DistanceSquared( velocity, oldListenerVelocity ) > 200.0f * 200.0f ) {
		needsForcedUpdate = true;
	}

	// Check the "head" contents. We assume the regular player viewheight.
	VectorCopy( origin, testedOrigin );
	testedOrigin[2] += 18;
	int contents = trap_PointContents( testedOrigin );

	isListenerInLiquid = ( contents & ( CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_WATER ) ) != 0;
	if( wasListenerInLiquid != isListenerInLiquid ) {
		needsForcedUpdate = true;
	}

	VectorCopy( origin, oldListenerOrigin );
	VectorCopy( velocity, oldListenerVelocity );
	wasListenerInLiquid = isListenerInLiquid;

	// Sanitize the possibly modified cvar before the environment update
	if( s_environment_sampling_quality->value < 0.0f || s_environment_sampling_quality->value > 1.0f ) {
		trap_Cvar_ForceSet( s_environment_sampling_quality->name, "0.5" );
	}

	sourcesUpdatePriorityQueue.Clear();

	if( needsForcedUpdate ) {
		ENV_CollectForcedEnvironmentUpdates();
	} else {
		ENV_CollectRegularEnvironmentUpdates();
	}

	ENV_ProcessUpdatesPriorityQueue();
}

void UnderwaterFlangerEffect::InterpolateProps( const Effect *oldOne, int timeDelta ) {
	const auto *that = Cast<UnderwaterFlangerEffect *>( oldOne );
	if( !that ) {
		return;
	}

	const Interpolator interpolator( timeDelta );
	directObstruction = interpolator( directObstruction, that->directObstruction, 0.0f, 1.0f );
}

void ReverbEffect::InterpolateProps( const Effect *oldOne, int timeDelta ) {
	const auto *that = Cast<ReverbEffect *>( oldOne );
	if( !that ) {
		return;
	}

	const Interpolator interpolator( timeDelta );
	directObstruction = interpolator( directObstruction, that->directObstruction, 0.0f, 1.0f );
	density = interpolator( density, that->density, 0.0f, 1.0f );
	diffusion = interpolator( diffusion, that->diffusion, 0.0f, 1.0f );
	gain = interpolator( gain, that->gain, 0.0f, 1.0f );
	gainHf = interpolator( gain, that->gainHf, 0.0f, 1.0f );
	decayTime = interpolator( decayTime, that->decayTime, 0.1f, 20.0f );
	reflectionsGain = interpolator( reflectionsGain, that->reflectionsGain, 0.0f, 3.16f );
	reflectionsDelay = interpolator( reflectionsDelay, that->reflectionsDelay, 0.0f, 0.3f );
	lateReverbDelay = interpolator( lateReverbDelay, that->lateReverbDelay, 0.0f, 0.1f );
}

static void ENV_InterpolateEnvironmentProps( src_t *src, int64_t millisNow ) {
	auto *updateState = &src->envUpdateState;
	if( !updateState->effect ) {
		return;
	}

	int timeDelta = (int)( millisNow - updateState->lastEnvUpdateAt );
	updateState->effect->InterpolateProps( updateState->oldEffect, timeDelta );
	updateState->lastEnvUpdateAt = millisNow;
}

static bool ENV_TryReuseSourceReverbProps( src_t *src, const src_t *tryReusePropsSrc, ReverbEffect *newEffect ) {
	if( !tryReusePropsSrc ) {
		return false;
	}

	const ReverbEffect *reuseEffect = Effect::Cast<const ReverbEffect *>( tryReusePropsSrc->envUpdateState.effect );
	if( !reuseEffect ) {
		return false;
	}

	// We are already sure that both sources are in the same contents kind (non-liquid).
	// Check distance between sources.
	const float squareDistance = DistanceSquared( tryReusePropsSrc->origin, src->origin );
	// If they are way too far for reusing
	if( squareDistance > 96 * 96 ) {
		return false;
	}

	// If they are very close, feel free to just copy props
	if( squareDistance < 4.0f * 4.0f ) {
		newEffect->CopyReverbProps( reuseEffect );
		return true;
	}

	// Do a coarse raycast test between these two sources
	vec3_t start, end, dir;
	VectorSubtract( tryReusePropsSrc->origin, src->origin, dir );
	const float invDistance = 1.0f / sqrtf( squareDistance );
	VectorScale( dir, invDistance, dir );
	// Offset start and end by a dir unit.
	// Ensure start and end are in "air" and not on a brush plane
	VectorAdd( src->origin, dir, start );
	VectorSubtract( tryReusePropsSrc->origin, dir, end );

	trace_t trace;
	trap_Trace( &trace, start, end, vec3_origin, vec3_origin, MASK_SOLID );
	if( trace.fraction != 1.0f ) {
		return false;
	}

	newEffect->CopyReverbProps( reuseEffect );
	return true;
}

static void ENV_UpdateSourceEnvironment( src_t *src, int64_t millisNow, const src_t *tryReusePropsSrc ) {
	envUpdateState_t *updateState = &src->envUpdateState;

	if( src->priority == SRCPRI_LOCAL ) {
		// Check whether the source has never been updated for this local sound.
		assert( !updateState->nextEnvUpdateAt );
		ENV_UnregisterSource( src );
		return;
	}

	// Randomize the update period a bit.
	// Otherwise there will be another updates spike
	// an update period ahead after a forced/initial update.
	updateState->nextEnvUpdateAt = (int64_t)( millisNow + 108 + 32 * random() );

	VectorCopy( src->origin, updateState->lastUpdateOrigin );
	VectorCopy( src->velocity, updateState->lastUpdateVelocity );

	updateState->oldEffect = updateState->effect;
	bool needsInterpolation = true;
	if( updateState->wasInLiquid || wasListenerInLiquid ) {
		float directObstruction = 0.9f;
		if( updateState->wasInLiquid && wasListenerInLiquid ) {
			directObstruction = ENV_ComputeDirectObstruction( src );
		}
		auto *newEffect = effectsAllocator.NewFlangerEffect( src );
		newEffect->directObstruction = directObstruction;
		updateState->effect = newEffect;
	} else {
		auto *newEffect = effectsAllocator.NewReverbEffect( src );
		newEffect->directObstruction = ENV_ComputeDirectObstruction( src );
		// We try reuse props only for reverberation effects
		// since reverberation effects sampling is extremely expensive.
		// Moreover, direct obstruction reuse is just not valid,
		// since even a small origin difference completely changes it.
		if( ENV_TryReuseSourceReverbProps( src, tryReusePropsSrc, newEffect ) ) {
			needsInterpolation = false;
		} else {
			ENV_ComputeReverberation( src, newEffect );
		}
		updateState->effect = newEffect;
	}

	if( needsInterpolation ) {
		ENV_InterpolateEnvironmentProps( src, millisNow );
	}

	// Recycle the old effect
	effectsAllocator.DeleteEffect( updateState->oldEffect );
	updateState->oldEffect = nullptr;

	updateState->effect->BindOrUpdate( src );
}

static void ENV_SetupSamplingProps( samplingProps_t *props, unsigned minSamples, unsigned maxSamples ) {
	unsigned numSamples;
	float quality = s_environment_sampling_quality->value;

	// If the quality is valid and has not been modified since the pattern has been set
	if( props->quality == quality ) {
		return;
	}

	assert( quality >= 0.0f && quality <= 1.0f );
	assert( minSamples < maxSamples );

	numSamples = (unsigned)( minSamples + ( maxSamples - minSamples ) * quality );
	assert( numSamples && numSamples <= maxSamples );

	props->quality = quality;
	props->numSamples = numSamples;
	props->valueIndex = (uint16_t)( random() * std::numeric_limits<uint16_t>::max() );
}

static float ENV_ComputeDirectObstruction( src_t *src ) {
	trace_t trace;
	envUpdateState_t *updateState;
	float *originOffset;
	vec3_t testedListenerOrigin;
	vec3_t testedSourceOrigin;
	float squareDistance;
	unsigned numTestedRays, numPassedRays;
	unsigned i, valueIndex;

	updateState = &src->envUpdateState;

	VectorCopy( oldListenerOrigin, testedListenerOrigin );
	// TODO: We assume standard view height
	testedListenerOrigin[2] += 18.0f;

	squareDistance = DistanceSquared( testedListenerOrigin, src->origin );
	// Shortcut for sounds relative to the player
	if( squareDistance < 32.0f * 32.0f ) {
		return 0.0f;
	}

	// Compute the leaf num if needed
	if( listenerLeafNum < 0 ) {
		listenerLeafNum = trap_PointLeafNum( testedListenerOrigin );
	}

	if( !trap_LeafsInPVS( listenerLeafNum, trap_PointLeafNum( src->origin ) ) ) {
		return 1.0f;
	}

	trap_Trace( &trace, testedListenerOrigin, src->origin, vec3_origin, vec3_origin, MASK_SOLID );
	if( trace.fraction == 1.0f && !trace.startsolid ) {
		// Consider zero obstruction in this case
		return 1.0f;
	}

	ENV_SetupSamplingProps( &updateState->directObstructionSamplingProps, 3, MAX_DIRECT_OBSTRUCTION_SAMPLES );

	numPassedRays = 0;
	numTestedRays = updateState->directObstructionSamplingProps.numSamples;
	valueIndex = updateState->directObstructionSamplingProps.valueIndex;
	for( i = 0; i < numTestedRays; i++ ) {
		valueIndex = ( valueIndex + 1 ) % ARRAYSIZE( randomDirectObstructionOffsets );
		originOffset = randomDirectObstructionOffsets[ valueIndex ];

		VectorAdd( src->origin, originOffset, testedSourceOrigin );
		trap_Trace( &trace, testedListenerOrigin, testedSourceOrigin, vec3_origin, vec3_origin, MASK_SOLID );
		if( trace.fraction == 1.0f && !trace.startsolid ) {
			numPassedRays++;
		}
	}

	return 1.0f - 0.9f * ( numPassedRays / (float)numTestedRays );
}

// Actually just a mutable context for sampling algorithm intermediates
class ReverbSampler {
	static constexpr float REVERB_ENV_DISTANCE_THRESHOLD = 2048.0f;

	vec3_t testedListenerOrigin;
	float primaryHitDistances[MAX_REVERB_PRIMARY_RAY_SAMPLES];
	vec3_t reflectionPoints[MAX_REVERB_PRIMARY_RAY_SAMPLES];

	unsigned numPrimaryRays;
	unsigned numRaysHitSky;
	unsigned numRaysHitMetal;
	unsigned numReflectionPoints;
	float averageDistance;

	src_t *const src;
	ReverbEffect *const effect;

	inline float GetEmissionRadius() const;
	void EmitPrimaryRays();
	void ProcessPrimaryEmissionResults();
	void EmitSecondaryRays();
	float ComputePrimaryHitDistanceStdDev() const;
	float ComputeRoomSizeFactor() const;
	void SetMinimalReverbProps();
public:
	ReverbSampler( src_t *src_, ReverbEffect *effect_ )
		: src( src_ ), effect( effect_ ) {
		numPrimaryRays = 0;
		numRaysHitSky = 0;
		numRaysHitMetal = 0;
		numReflectionPoints = 0;
		averageDistance = 0.0f;
	}

	void Exec();
};

static void ENV_ComputeReverberation( src_t *src, ReverbEffect *effect ) {
	ReverbSampler( src, effect ).Exec();
}

inline float ReverbSampler::GetEmissionRadius() const {
	// Do not even bother casting rays 999999 units ahead for very attenuated sources.
	// However, clamp/normalize the hit distance using the same defined threshold
	float attenuation = src->attenuation;

	if( attenuation <= 1.0f ) {
		return 999999.9f;
	}

	clamp_high( attenuation, 10.0f );
	float distance = 4.0f * REVERB_ENV_DISTANCE_THRESHOLD;
	distance -= 3.5f * SQRTFAST( attenuation / 10.0f ) * REVERB_ENV_DISTANCE_THRESHOLD;
	return distance;
}

void ReverbSampler::Exec() {
	VectorCopy( oldListenerOrigin, testedListenerOrigin );
	testedListenerOrigin[2] += 18.0f;

	ENV_SetupSamplingProps( &src->envUpdateState.reverbPrimaryRaysSamplingProps, 16, MAX_REVERB_PRIMARY_RAY_SAMPLES );

	EmitPrimaryRays();

	if( !numReflectionPoints ) {
		SetMinimalReverbProps();
		return;
	}

	ProcessPrimaryEmissionResults();
	EmitSecondaryRays();
}

void ReverbSampler::EmitPrimaryRays() {
	const float primaryEmissionRadius = GetEmissionRadius();

	averageDistance = 0.0f;
	numRaysHitSky = 0;
	numRaysHitMetal = 0;
	numReflectionPoints = 0;

	const auto *updateState = &src->envUpdateState;
	numPrimaryRays = updateState->reverbPrimaryRaysSamplingProps.numSamples;
	int valueIndex = updateState->reverbPrimaryRaysSamplingProps.valueIndex;

	trace_t trace;
	for( unsigned i = 0; i < numPrimaryRays; ++i ) {
		float *sampleDir, *reflectionPoint;
		valueIndex = ( valueIndex + 1 ) % ARRAYSIZE( randomReverbPrimaryRayDirs );
		sampleDir = randomReverbPrimaryRayDirs[valueIndex];

		vec3_t testedRayPoint;
		VectorScale( sampleDir, primaryEmissionRadius, testedRayPoint );
		VectorAdd( testedRayPoint, src->origin, testedRayPoint );
		trap_Trace( &trace, src->origin, testedRayPoint, vec3_origin, vec3_origin, MASK_SOLID );

		if( trace.fraction == 1.0f || trace.startsolid ) {
			continue;
		}

		// Skip surfaces non-reflective for sounds
		int surfFlags = trace.surfFlags;
		if( surfFlags & ( SURF_SKY | SURF_NOIMPACT | SURF_NOMARKS | SURF_FLESH | SURF_NOSTEPS ) ) {
			// Go even further for sky. Simulate an "absorption" of sound by the void.
			if( surfFlags & SURF_SKY ) {
				numRaysHitSky++;
			}
			continue;
		}

		// Hack... let glass contribute to "metal" feeling
		if( ( surfFlags & SURF_METALSTEPS ) || ( trace.contents & CONTENTS_TRANSLUCENT ) ) {
			numRaysHitMetal++;
		}

		if( DistanceSquared( src->origin, trace.endpos ) < 2 * 2 ) {
			continue;
		}

		// Do not use the trace.endpos exactly as a source of a reflected wave.
		// (a following trace call would probably fail for this start origin).
		// Add -sampleDir offset to the trace.endpos
		reflectionPoint = reflectionPoints[numReflectionPoints];
		VectorCopy( sampleDir, reflectionPoint );
		VectorNegate( reflectionPoint, reflectionPoint );
		VectorAdd( trace.endpos, reflectionPoint, reflectionPoint );

		float squareDistance = DistanceSquared( src->origin, trace.endpos );
		if( squareDistance < REVERB_ENV_DISTANCE_THRESHOLD * REVERB_ENV_DISTANCE_THRESHOLD ) {
			primaryHitDistances[numReflectionPoints] = sqrtf( squareDistance );
		} else {
			primaryHitDistances[numReflectionPoints] = REVERB_ENV_DISTANCE_THRESHOLD;
		}
		assert( primaryHitDistances[numReflectionPoints] >= 0.0f );
		assert( primaryHitDistances[numReflectionPoints] <= REVERB_ENV_DISTANCE_THRESHOLD );
		averageDistance += primaryHitDistances[numReflectionPoints];

		numReflectionPoints++;
	}
}

float ReverbSampler::ComputePrimaryHitDistanceStdDev() const {
	assert( numReflectionPoints );

	float variance = 0.0f;
	for( unsigned i = 0; i < numReflectionPoints; ++i ) {
		float delta = primaryHitDistances[i] - averageDistance;
		variance += delta * delta;
	}

	variance /= numReflectionPoints;
	return sqrtf( variance );
}

float ReverbSampler::ComputeRoomSizeFactor() const {
	assert( numReflectionPoints );

	// Get the standard deviation of primary rays hit distances.
	const float sigma = ComputePrimaryHitDistanceStdDev();

	// Note: The hit distance distribution is not Gaussian and is not symmetrical!
	// It heavily depends of the real environment,
	// and might be close to Gaussian if a player is in a center of a huge box.
	// Getting the farthest point that is within 3 sigma does not feel good.

	// Select distances that >= the average distance
	float contestedDistances[MAX_REVERB_PRIMARY_RAY_SAMPLES];
	int numContestedDistances = 0;
	for( unsigned i = 0; i < numReflectionPoints; ++i ) {
		if( primaryHitDistances[i] >= averageDistance ) {
			contestedDistances[numContestedDistances++] = primaryHitDistances[i];
		}
	}

	// We are sure there is at least 1 distance >= the average distance
	assert( numContestedDistances );

	// Sort hit distances so the nearest one is the first
	std::sort( contestedDistances, contestedDistances + numContestedDistances );

	const float connectivityDistance = std::max( 384.0f, sigma );

	float prevDistance = contestedDistances[0];
	// Stop on first distance that violates "connectivity".
	// This way distance threshold could propagate along a hallway or tube.
	for( int i = 1; i < numContestedDistances; ++i ) {
		float currDistance = contestedDistances[i];
		assert( currDistance >= prevDistance );
		if( currDistance - prevDistance > connectivityDistance ) {
			break;
		}
		prevDistance = currDistance;
	}

	return prevDistance / REVERB_ENV_DISTANCE_THRESHOLD;
}

void ReverbSampler::ProcessPrimaryEmissionResults() {
	assert( numReflectionPoints );
	averageDistance /= numReflectionPoints;

	const float roomSizeFactor = ComputeRoomSizeFactor();
	assert( roomSizeFactor >= 0.0f && roomSizeFactor <= 1.0f );

	const float metalFactor = sqrtf( numRaysHitMetal / (float)numPrimaryRays );
	assert( metalFactor >= 0.0f && metalFactor <= 1.0f );

	const float skyFactor = sqrtf( numRaysHitSky / (float)numPrimaryRays );
	assert( skyFactor >= 0.0f && skyFactor <= 1.0f );

	// It should be default.
	// Secondary rays obstruction is the only modulation we apply.
	// See EmitSecondaryRays()
	effect->gain = 0.32f;

	effect->density = 1.0f - 0.7f * metalFactor;

	// Let diffusion be lower for huge open spaces
	effect->diffusion = 1.0f - 0.75f * skyFactor - 0.5f * roomSizeFactor;
	clamp_low( effect->diffusion, 0.0f );

	// Let decay time depend of:
	// * mainly of effects scale
	// * room size
	// * diffusion (that's an extra hack to hear long "colorated" echoes in open spaces)
	// Since diffusion depends itself of the room size factor, add only sky factor component
	effect->decayTime = 0.75f + 3.0f * roomSizeFactor + skyFactor;

	// Let it grow in small rooms but be absorbed by sky
	effect->reflectionsGain = 0.05f + 0.75f * powf( 1.0f - roomSizeFactor, 4.0f ) * ( 1.0f - skyFactor );

	effect->reflectionsDelay = 0.007f + 0.100f * roomSizeFactor;
	effect->lateReverbDelay = 0.011f + 0.055f * roomSizeFactor;
}

void ReverbSampler::SetMinimalReverbProps() {
	effect->gain = 0.1f;
	effect->density = 1.0f;
	effect->diffusion = 1.0f;
	effect->decayTime = 0.75f;
	effect->reflectionsDelay = 0.007f;
	effect->lateReverbDelay = 0.011f;
	effect->gainHf = 0.1f;
}

void ReverbSampler::EmitSecondaryRays() {
	// Compute the leaf num if needed
	if( listenerLeafNum < 0 ) {
		listenerLeafNum = trap_PointLeafNum( testedListenerOrigin );
	}

	trace_t trace;

	unsigned numPassedSecondaryRays = 0;
	for( unsigned i = 0; i < numReflectionPoints; i++ ) {
		// Cut off by PVS system early, we are not interested in actual ray hit points contrary to the primary emission.
		if( !trap_LeafsInPVS( listenerLeafNum, trap_PointLeafNum( reflectionPoints[i] ) ) ) {
			continue;
		}

		trap_Trace( &trace, reflectionPoints[i], testedListenerOrigin, vec3_origin, vec3_origin, MASK_SOLID );
		if( trace.fraction == 1.0f && !trace.startsolid ) {
			numPassedSecondaryRays++;
		}
	}

	if( numReflectionPoints ) {
		float frac = numPassedSecondaryRays / (float)numReflectionPoints;
		// A absence of a HF attenuation sounds poor, metallic/reflective environments should be the only exception.
		effect->gainHf = 0.1f + ( 0.4f + 0.4f * numRaysHitMetal / (float)numReflectionPoints ) * frac;
		// We also modify effect gain by a fraction of secondary rays passed to listener.
		// This is not right in theory, but is inevitable in the current game sound model
		// where you can hear across the level through solid walls
		// in order to avoid messy echoes coming from everywhere.
		effect->gain *= 0.75f + 0.25f * frac;
	} else {
		// Set minimal feasible values
		effect->gainHf = 0.1f;
		effect->gain *= 0.75f;
	}
}
