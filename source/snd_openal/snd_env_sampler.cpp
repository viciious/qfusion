#include "snd_env_sampler.h"
#include "snd_local.h"

#include "../gameshared/q_comref.h"

#include <algorithm>
#include <limits>
#include <random>

struct ListenerProps {
	vec3_t origin;
	vec3_t velocity;
	mutable int leafNum;
	bool isInLiquid;

	ListenerProps(): leafNum( -1 ), isInLiquid( false ) {}

	int GetLeafNum() const {
		if( leafNum < 0 ) {
			leafNum = trap_PointLeafNum( origin );
		}
		return leafNum;
	}

	void InvalidateCachedUpdateState() {
		leafNum = -1;
	}
};

static ListenerProps listenerProps;

static bool isEaxReverbAvailable = false;

class alignas( 8 )EffectsAllocator {
	static_assert( sizeof( EaxReverbEffect ) >= sizeof( StandardReverbEffect ), "" );
	static_assert( sizeof( EaxReverbEffect ) >= sizeof( UnderwaterFlangerEffect ), "" );
	static_assert( sizeof( EaxReverbEffect ) >= sizeof( ChorusEffect ), "" );
	static_assert( sizeof( EaxReverbEffect ) >= sizeof( DistortionEffect ), "" );
	static_assert( sizeof( EaxReverbEffect ) >= sizeof( EchoEffect ), "" );

	static constexpr auto MAX_EFFECT_SIZE = sizeof( EaxReverbEffect );

	static constexpr auto ENTRY_SIZE =
		MAX_EFFECT_SIZE % 8 ? MAX_EFFECT_SIZE + ( 8 - MAX_EFFECT_SIZE % 8 ) : MAX_EFFECT_SIZE;

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
		if( ::isEaxReverbAvailable ) {
			return new( AllocEntry( src, AL_EFFECT_EAXREVERB ) )EaxReverbEffect();
		}
		return new( AllocEntry( src, AL_EFFECT_REVERB ) )StandardReverbEffect();
	}

	ChorusEffect *NewChorusEffect( const src_t *src ) {
		return new( AllocEntry( src, AL_EFFECT_CHORUS ) )ChorusEffect();
	}

	DistortionEffect *NewDistortionEffect( const src_t *src ) {
		return new( AllocEntry( src, AL_EFFECT_DISTORTION ) )DistortionEffect();
	}

	EchoEffect *NewEchoEffect( const src_t *src ) {
		return new( AllocEntry( src, AL_EFFECT_ECHO ) )EchoEffect();
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
	if( *effectTypeRef <= AL_EFFECT_NULL || *effectTypeRef > AL_EFFECT_EAXREVERB ) {
		const char *format = "EffectsAllocator::FreeEntry(): An effect for source #%d and slot %d is not in use\n";
		trap_Error( va( format, sourceIndex, interleavedSlotIndex ) );
	}
#endif

	// Set zero effect type for the entry
	*effectTypeRef = 0;
}

#define MAX_DIRECT_OBSTRUCTION_SAMPLES ( 8 )
#define MAX_REVERB_PRIMARY_RAY_SAMPLES ( 48 )

static_assert( PanningUpdateState::MAX_POINTS == MAX_REVERB_PRIMARY_RAY_SAMPLES, "" );

static vec3_t randomDirectObstructionOffsets[(1 << 8)];

#ifndef M_2_PI
#define M_2_PI ( 2.0 * ( M_PI ) )
#endif

#ifndef ARRAYSIZE
#define ARRAYSIZE( x ) ( sizeof( x ) / sizeof( x[0] ) )
#endif

// We want sampling results to be reproducible especially for leaf sampling and thus use this local implementation
static std::minstd_rand0 samplingRandom;

static inline float SamplingRandom() {
	typedef decltype( samplingRandom ) R;
	return ( samplingRandom() - R::min() ) / (float)( R::max() - R::min() );
}

static void ENV_InitRandomTables() {
	unsigned i, j;

	for( i = 0; i < ARRAYSIZE( randomDirectObstructionOffsets ); i++ ) {
		for( j = 0; j < 3; ++j ) {
			randomDirectObstructionOffsets[i][j] = -20.0f + 40.0f * SamplingRandom();
		}
	}
}

static constexpr float REVERB_ENV_DISTANCE_THRESHOLD = 3072.0f;

struct alignas( 4 )LeafProps {
	uint8_t roomSizeFactor;
	uint8_t skyFactor;
	uint8_t waterFactor;
	uint8_t metalFactor;

	static float PackValue( float value ) { return (uint8_t)( value * 255 ); }
	static float UnpackValue( uint8_t packed ) { return packed / 255.0f; }

#define MK_ACCESSORS( accessorName, fieldName )                                             \
	float accessorName() const { return UnpackValue( fieldName ); }                         \
	void Set##accessorName( float fieldName##_ ) { fieldName = PackValue( fieldName##_ ); }

	MK_ACCESSORS( RoomSizeFactor, roomSizeFactor );
	MK_ACCESSORS( SkyFactor, skyFactor );
	MK_ACCESSORS( WaterFactor, waterFactor );
	MK_ACCESSORS( MetalFactor, metalFactor );

#undef MK_ACCESSORS
};

struct LeafPropsBuilder {
	float roomSizeFactor;
	float skyFactor;
	float waterFactor;
	float metalFactor;
	int numProps;

	LeafPropsBuilder()
		: roomSizeFactor( 0.0f )
		, skyFactor( 0.0f )
		, waterFactor( 0.0f )
		, metalFactor( 0.0f )
		, numProps( 0 ) {}

	void operator+=( const LeafProps &propsToAdd ) {
		roomSizeFactor += propsToAdd.RoomSizeFactor();
		skyFactor += propsToAdd.SkyFactor();
		waterFactor += propsToAdd.WaterFactor();
		metalFactor += propsToAdd.MetalFactor();
		++numProps;
	}

	LeafProps Result() {
		LeafProps result;
		if( numProps ) {
			float scale = 1.0f / numProps;
			roomSizeFactor *= scale;
			skyFactor *= scale;
			waterFactor *= scale;
			metalFactor *= scale;
		}
		result.SetRoomSizeFactor( roomSizeFactor );
		result.SetSkyFactor( skyFactor );
		result.SetWaterFactor( waterFactor );
		result.SetMetalFactor( metalFactor );
		return result;
	}
};

static void ENV_SetupSamplingRayDirs( vec3_t *rayDirs, unsigned numRays );

class GenericRaycastSampler {
	vec3_t *primaryRayDirs;
	vec3_t *primaryHitPoints;
	mutable float *primaryHitDistances;

protected:
	unsigned numRaysHitSky;
	unsigned numRaysHitWater;
	unsigned numRaysHitMetal;
	unsigned numPrimaryRays;
	unsigned numPrimaryHits;
	float averageDistance;

	vec3_t emissionOrigin;

	virtual float GetEmissionRadius() const {
		return 999999.9f;
	}

	float ComputePrimaryHitDistanceStdDev() const;

	void EmitPrimaryRays();

	inline float ComputeGenericHitFactor( float value ) const {
		assert( value <= numPrimaryRays );
		return sqrtf( value / (float)numPrimaryRays );
	}

	inline float ComputeMetalFactor() const { return ComputeGenericHitFactor( numRaysHitMetal ); }
	inline float ComputeWaterFactor() const { return ComputeGenericHitFactor( numRaysHitWater ); }
	inline float ComputeSkyFactor() const { return ComputeGenericHitFactor( numRaysHitSky ); }
	float ComputeRoomSizeFactor() const;

	void ResetMutableState( vec3_t *primaryRayDirs_,
							vec3_t *primaryHitPoints_,
							float *primaryHitDistances_,
							const vec3_t emissionOrigin_ ) {
		numRaysHitSky = 0;
		numRaysHitWater = 0;
		numRaysHitMetal = 0;
		numPrimaryHits = 0;
		averageDistance = 0.0f;

		this->primaryRayDirs = primaryRayDirs_;
		this->primaryHitPoints = primaryHitPoints_;
		this->primaryHitDistances = primaryHitDistances_;

		VectorCopy( emissionOrigin_, this->emissionOrigin );
	}
};

class LeafPropsSampler;
class LeafPropsReader;

class LeafPropsCache {
	char mapName[MAX_CONFIGSTRING_CHARS];
	char mapChecksum[MAX_CONFIGSTRING_CHARS];
	mutable int numLeafs;
	LeafProps *leafProps;

	LeafProps ComputeLeafProps( int leafNum, LeafPropsSampler *sampler );

	bool TryReadFromFile( LeafPropsReader *reader, int actualLeafsNum );
	void SaveToFile( const char *actualMap, const char *actualChecksum, int actualLeafsNum );
public:
	LeafPropsCache(): numLeafs( -1 ), leafProps( nullptr ) {
		mapName[0] = mapChecksum[0] = '\0';
	}

	~LeafPropsCache() {
		if( leafProps ) {
			S_Free( leafProps );
		}
	}

	void EnsureValid();

	const LeafProps &GetPropsForLeaf( int leafNum ) const {
		return leafProps[leafNum];
	}
};

static ATTRIBUTE_ALIGNED( 8 ) uint8_t leafPropsCacheStorage[sizeof( LeafPropsCache )];
static LeafPropsCache *leafPropsCache = nullptr;

class LeafPropsSampler: public GenericRaycastSampler {
	static constexpr unsigned NUM_RAYS = 1024;

	// Inline buffers for algorithm intermediates.
	// Note that instances of this class should be allocated dynamically, so do not bother about arrays size.
	vec3_t dirs[NUM_RAYS];
	float distances[NUM_RAYS];
public:
	LeafPropsSampler() {
		ENV_SetupSamplingRayDirs( dirs, NUM_RAYS );
	}

	bool ComputeLeafProps( const vec3_t origin, LeafProps *props );
};

class LeafPropsIOHelper {
protected:
	char fileName[MAX_STRING_CHARS];
	const char *const map;
	const char *const checksum;
	int fd;
	int fsResult;

public:
	LeafPropsIOHelper( const char *map_, const char *checksum_, int fileFlags )
		: map( map_ ), checksum( checksum_ ) {
		Q_snprintfz( fileName, sizeof( fileName ), "sounds/%s", map );
		COM_StripExtension( fileName );
		Q_strncatz( fileName, ".envcache", sizeof( fileName ) );
		fsResult = trap_FS_FOpenFile( fileName, &fd, fileFlags );
	}

	virtual ~LeafPropsIOHelper() {
		if( fd >= 0 ) {
			trap_FS_FCloseFile( fd );
		}
	}
};

class LeafPropsReader: public LeafPropsIOHelper {
	char *fileData;
	char *dataPtr;
	int fileSize;
	int leafCounter;

	void ReadFileData();

	inline void SkipWhiteSpace() {
		size_t skippedLen = strspn( dataPtr, "\t \n");
		dataPtr += skippedLen;
	}

	bool ExpectString( const char *string );
public:
	LeafPropsReader( const char *map_, const char *checksum_, int fileFlags )
		: LeafPropsIOHelper( map_, checksum_, fileFlags )
		, fileData( nullptr )
		, dataPtr( nullptr )
		, fileSize( 0 )
		, leafCounter( 0 ) {
		if( fsResult >= 0 ) {
			fileSize = fsResult;
			ReadFileData();
		}
	}

	~LeafPropsReader() {
		if( fileData ) {
			S_Free( ( void * )fileData );
		}
	}

	enum Status {
		OK,
		DONE,
		ERROR
	};

	Status ReadNextProps( LeafProps *props );
};


class LeafPropsWriter: public LeafPropsIOHelper {
	int leafCounter;

	bool WriteString( const char *string );
public:
	LeafPropsWriter( const char *map_, const char *checksum_ );

	bool WriteProps( const LeafProps &props );
};

void LeafPropsReader::ReadFileData() {
	assert( fsResult >= 0 && fileSize >= 0 );

	fileData = (char *)S_Malloc( fileSize + 1 );
	if( !fileData ) {
		return;
	}

	int bytesRead = trap_FS_Read( fileData, (size_t)fileSize, fd );
	if( bytesRead != fileSize ) {
		fsResult = -1;
		return;
	}

	dataPtr = fileData;

	// Simplify further parsing
	for( int i = 0; i < fileSize; ++i ) {
		if( fileData[i] == '\r' ) {
			fileData[i] = '\n';
		}
	}
	fileData[fileSize] = '\0';

	if( !ExpectString( map ) ) {
		goto parsingError;
	}
	if( !ExpectString( checksum ) ) {
		goto parsingError;
	}

	return;
parsingError:
	if( fileData ) {
		S_Free( fileData );
	}
	fileData = nullptr;
}

bool LeafPropsReader::ExpectString( const char *string ) {
	SkipWhiteSpace();
	char *nextLine = strchr( dataPtr, '\n' );
	if( !nextLine ) {
		return false;
	}

	*nextLine = '\0';
	if( strcmp( string, dataPtr ) ) {
		return false;
	}

	dataPtr = nextLine + 1;
	return true;
}

LeafPropsReader::Status LeafPropsReader::ReadNextProps( LeafProps *props ) {
	if( fsResult < 0 || !fileData ) {
		return ERROR;
	}

	SkipWhiteSpace();
	if( !*dataPtr ) {
		return DONE;
	}

	char *nextLine = strchr( dataPtr, '\n' );
	if( nextLine ) {
		*nextLine = '\0';
	}

	int num;
	float values[4];
	if ( sscanf( dataPtr, "%d %f %f %f %f", &num, values, values + 1, values + 2, values + 3 ) != 5 ) {
		// Prevent further calls
		dataPtr = nullptr;
		return ERROR;
	}

	if( num != leafCounter ) {
		dataPtr = nullptr;
		return ERROR;
	}

	for( int i = 0; i < 4; ++i ) {
		if( values[i] < 0.0f || values[i] > 1.0f ) {
			dataPtr = nullptr;
			return ERROR;
		}
	}

	if( nextLine ) {
		dataPtr = nextLine + 1;
	} else {
		fileData[0] = '\0';
		dataPtr = fileData;
	}

	leafCounter++;
	props->SetRoomSizeFactor( values[0] );
	props->SetSkyFactor( values[1] );
	props->SetWaterFactor( values[2] );
	props->SetMetalFactor( values[3] );
	return OK;
}

bool LeafPropsWriter::WriteString( const char *string ) {
	char buffer[MAX_STRING_CHARS];
	unsigned charsPrinted = (unsigned)Q_snprintfz( buffer, sizeof( buffer ), "%s\r\n", string );
	return charsPrinted == trap_FS_Write( buffer, charsPrinted, fd );
}

LeafPropsWriter::LeafPropsWriter( const char *map_, const char *checksum_ )
	: LeafPropsIOHelper( map_, checksum_, FS_WRITE | FS_CACHE ), leafCounter( 0 ) {
	if( fsResult < 0 ) {
		return;
	}
	if( !WriteString( map_ ) || !WriteString( checksum_ ) ) {
		fsResult = -1;
	}
}

bool LeafPropsWriter::WriteProps( const LeafProps &props ) {
	if( fsResult < 0 ) {
		return false;
	}

	char buffer[MAX_STRING_CHARS];
	int charsPrinted = Q_snprintfz( buffer, sizeof( buffer ),
									"%d %.2f %.2f %.2f %2.f\r\n", leafCounter,
									props.RoomSizeFactor(), props.SkyFactor(),
									props.WaterFactor(), props.MetalFactor() );

	int charsWritten = trap_FS_Write( buffer, (size_t)charsPrinted, fd );
	if( charsWritten != charsPrinted ) {
		fsResult = -1;
		return false;
	}

	leafCounter++;
	return true;
}

void LeafPropsCache::EnsureValid() {
	const char *actualMap = trap_GetConfigString( CS_WORLDMODEL );
	const int actualNumLeafs = trap_NumLeafs();
	// Should not be used for empty maps
	if( !*actualMap || !numLeafs ) {
		return;
	}

	const char *actualChecksum = trap_GetConfigString( CS_MAPCHECKSUM );
	if( actualNumLeafs == numLeafs && !strcmp( actualMap, mapName ) && !strcmp( actualChecksum, mapChecksum ) ) {
		return;
	}

	if( leafProps ) {
		S_Free( leafProps );
	}

	leafProps = (LeafProps *)S_Malloc( sizeof( LeafProps ) * actualNumLeafs );

	LeafPropsSampler *sampler = nullptr;

	// Try reading from the base game filesystem, then from the cache.
	// We should supply precomputed environment props for maps.
	for( int flags : { FS_READ, ( FS_READ | FS_CACHE ) } ) {
		LeafPropsReader reader( actualMap, actualChecksum, flags );
		if( TryReadFromFile( &reader, actualNumLeafs ) ) {
			goto done;
		}
	}

	sampler = new( S_Malloc( sizeof( LeafPropsSampler ) ) )LeafPropsSampler;

	leafProps[0] = LeafProps();
	for( int i = 1; i < actualNumLeafs; ++i ) {
		leafProps[i] = ComputeLeafProps( i, sampler );
	}

	sampler->~LeafPropsSampler();
	S_Free( sampler );

	// Always saves to cache
	SaveToFile( actualMap, actualChecksum, actualNumLeafs );
done:
	numLeafs = actualNumLeafs;
	Q_strncpyz( mapName, actualMap, MAX_CONFIGSTRING_CHARS );
	Q_strncpyz( mapChecksum, actualChecksum, MAX_CONFIGSTRING_CHARS );
}

bool LeafPropsCache::TryReadFromFile( LeafPropsReader *reader, int actualLeafsNum ) {
	int numReadProps = 0;
	for(;; ) {
		LeafProps props;
		switch( reader->ReadNextProps( &props ) ) {
			case LeafPropsReader::OK:
				if( numReadProps + 1 > actualLeafsNum ) {
					return false;
				}
				this->leafProps[numReadProps] = props;
				numReadProps++;
				break;
			case LeafPropsReader::DONE:
				return numReadProps == actualLeafsNum;
			default:
				return false;
		}
	}
}

void LeafPropsCache::SaveToFile( const char *actualMap, const char *actualChecksum, int actualLeafsNum ) {
	LeafPropsWriter writer( actualMap, actualChecksum );
	for( int i = 0; i < actualLeafsNum; ++i ) {
		if( !writer.WriteProps( this->leafProps[i] ) ) {
			Com_Printf( S_COLOR_YELLOW "LeafPropsCache: Can't save a cache to file\n" );
			break;
		}
	}
}

bool LeafPropsSampler::ComputeLeafProps( const vec3_t origin, LeafProps *props ) {
	ResetMutableState( dirs, nullptr, distances, origin );
	this->numPrimaryRays = NUM_RAYS;

	EmitPrimaryRays();
	// Happens mostly if rays outgoing from origin start in solid
	if( !numPrimaryHits ) {
		return false;
	}

	props->SetRoomSizeFactor( ComputeRoomSizeFactor() );
	props->SetWaterFactor( ComputeWaterFactor() );
	props->SetSkyFactor( ComputeSkyFactor() );
	props->SetMetalFactor( ComputeMetalFactor() );
	return true;
}

LeafProps LeafPropsCache::ComputeLeafProps( int leafNum, LeafPropsSampler *sampler ) {
	const vec3_t *leafBounds = trap_GetLeafBounds( leafNum );
	const float *leafMins = leafBounds[0];
	const float *leafMaxs = leafBounds[1];

	vec3_t extent;
	VectorSubtract( leafMaxs, leafMins, extent );
	const float squareExtent = VectorLengthSquared( extent );

	LeafProps tmpProps;
	LeafPropsBuilder propsBuilder;

	// Let maxSamples grow quadratic depending linearly of square extent value.
	// Cubic growth is more "natural" but leads to way too expensive computations.
	const int maxSamples = 2 + (int)( squareExtent / ( 256.0f * 256.0f ) );
	int numSamples = 0;
	int numAttempts = 0;

	// Always start at the leaf center
	vec3_t point;
	VectorMA( leafMins, 0.5f, extent, point );
	bool hasValidPoint = true;

	while( numSamples < maxSamples ) {
		numAttempts++;
		// Attempts may fail for 2 reasons: can't pick a valid point and a point sampling has failed.
		// Use the shared loop termination condition for these cases.
		if( numAttempts > 7 + 2 * maxSamples ) {
			return propsBuilder.Result();
		}

		if( !hasValidPoint ) {
			for( int i = 0; i < 3; ++i ) {
				point[i] = leafMins[i] + ( 0.1f + 0.9f * SamplingRandom() ) * extent[i];
			}

			// Check whether the point is really in leaf (the leaf is not a box but is inscribed in the bounds box)
			// Some bogus leafs (that are probably degenerate planes) lead to infinite looping otherwise
			if( trap_PointLeafNum( point ) != leafNum ) {
				continue;
			}

			hasValidPoint = true;
		}

		// Might fail if the rays outgoing from the point start in solid
		if( sampler->ComputeLeafProps( point, &tmpProps ) ) {
			propsBuilder += tmpProps;
			numSamples++;
			// Invalidate previous point used for sampling
			hasValidPoint = false;
			continue;
		}
	}

	return propsBuilder.Result();
}

void ENV_Init() {
	if( !s_environment_effects->integer ) {
		return;
	}

	listenerProps.InvalidateCachedUpdateState();

	ENV_InitRandomTables();

	isEaxReverbAvailable = qalGetEnumValue( "AL_EFFECT_EAXREVERB" ) != 0;

	::leafPropsCache = new( leafPropsCacheStorage )LeafPropsCache;
	::leafPropsCache->EnsureValid();
}

void ENV_Shutdown() {
	if( !s_environment_effects->integer ) {
		return;
	}

	::leafPropsCache->~LeafPropsCache();
	::leafPropsCache = nullptr;

	listenerProps.InvalidateCachedUpdateState();

	isEaxReverbAvailable = false;
}

void ENV_EndRegistration() {
	if( s_environment_effects->integer ) {
		::leafPropsCache->EnsureValid();
	}
}

void ENV_RegisterSource( src_t *src ) {
	// Invalidate last update when reusing the source
	// (otherwise it might be misused for props interpolation)
	src->envUpdateState.lastEnvUpdateAt = 0;
	// Force an immediate update
	src->envUpdateState.nextEnvUpdateAt = 0;
	// Reset sampling patterns by setting an illegal quality value
	src->envUpdateState.directObstructionSamplingProps.quality = -1.0f;
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
	if( src->volumeVar ) {
		qalSourcef( src->source, AL_GAIN, src->fvol * src->volumeVar->value );
	} else {
		qalSourcef( src->source, AL_GAIN, src->fvol * s_volume->value );
	}
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
		bool wasInLiquid = updateState->isInLiquid;
		updateState->isInLiquid = ( contents & ( CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_WATER ) ) != 0;
		if( updateState->isInLiquid ^ wasInLiquid ) {
			priorityQueue.AddSource( src, 2.0f );
			continue;
		}

		// Don't update lingering sources environment
		if( src->isLingering ) {
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

	listenerProps.InvalidateCachedUpdateState();

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

static void ENV_UpdatePanning( int64_t millisNow, const vec3_t origin, const mat3_t axes ) {
	for( src_t *src = srclist, *end = srclist + src_count; src != end; ++src ) {
		if( !src->isActive ) {
			continue;
		}
		Effect *effect = src->envUpdateState.effect;
		if( !effect ) {
			continue;
		}
		if( src->panningUpdateState.timeoutAt > millisNow ) {
			continue;
		}
		effect->UpdatePanning( src, origin, axes );
		src->panningUpdateState.timeoutAt = millisNow + 66;
	}
}

void ENV_UpdateListener( const vec3_t origin, const vec3_t velocity, const mat3_t axes ) {
	vec3_t testedOrigin;
	bool needsForcedUpdate = false;
	bool isListenerInLiquid;

	if( !s_environment_effects->integer ) {
		return;
	}

	ENV_UpdateRelativeSoundsSpatialization( origin, velocity );

	// Check whether we have teleported or entered/left a liquid.
	// Run a forced major update in this case.

	if( DistanceSquared( origin, listenerProps.origin ) > 100.0f * 100.0f ) {
		needsForcedUpdate = true;
	} else if( DistanceSquared( velocity, listenerProps.velocity ) > 200.0f * 200.0f ) {
		needsForcedUpdate = true;
	}

	// Check the "head" contents. We assume the regular player viewheight.
	VectorCopy( origin, testedOrigin );
	testedOrigin[2] += 18;
	int contents = trap_PointContents( testedOrigin );

	isListenerInLiquid = ( contents & ( CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_WATER ) ) != 0;
	if( listenerProps.isInLiquid != isListenerInLiquid ) {
		needsForcedUpdate = true;
	}

	VectorCopy( origin, listenerProps.origin );
	VectorCopy( velocity, listenerProps.velocity );
	listenerProps.isInLiquid = isListenerInLiquid;

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

	// Panning info is dependent of environment one, make sure it is executed last
	ENV_UpdatePanning( trap_Milliseconds(), testedOrigin, axes );
}

void UnderwaterFlangerEffect::InterpolateProps( const Effect *oldOne, int timeDelta ) {
	const auto *that = Cast<UnderwaterFlangerEffect *>( oldOne );
	if( !that ) {
		return;
	}

	const Interpolator interpolator( timeDelta );
	directObstruction = interpolator( directObstruction, that->directObstruction, 0.0f, 1.0f );
}

void ReverbEffect::InterpolateCommonReverbProps( const Interpolator &interpolator, const ReverbEffect *that ) {
	directObstruction = interpolator( directObstruction, that->directObstruction, 0.0f, 1.0f );
	density = interpolator( density, that->density, 0.0f, 1.0f );
	diffusion = interpolator( diffusion, that->diffusion, 0.0f, 1.0f );
	gain = interpolator( gain, that->gain, 0.0f, 1.0f );
	gainHf = interpolator( gain, that->gainHf, 0.0f, 1.0f );
	decayTime = interpolator( decayTime, that->decayTime, 0.1f, 20.0f );
	reflectionsGain = interpolator( reflectionsGain, that->reflectionsGain, 0.0f, 3.16f );
	reflectionsDelay = interpolator( reflectionsDelay, that->reflectionsDelay, 0.0f, 0.3f );
	lateReverbGain = interpolator( lateReverbGain, that->lateReverbGain, 0.0f, 10.0f );
	lateReverbDelay = interpolator( lateReverbDelay, that->lateReverbDelay, 0.0f, 0.1f );
	secondaryRaysObstruction = interpolator( secondaryRaysObstruction, that->secondaryRaysObstruction, 0.0f, 1.0f );
}

void StandardReverbEffect::InterpolateProps( const Effect *oldOne, int timeDelta ) {
	if( const auto *that = Cast<ReverbEffect *>( oldOne ) ) {
		InterpolateCommonReverbProps( Interpolator( timeDelta ), that );
	}
}

void EaxReverbEffect::InterpolateProps( const Effect *oldOne, int timeDelta ) {
	const auto *that = Cast<EaxReverbEffect *>( oldOne );
	if( !that ) {
		return;
	}

	Interpolator interpolator( timeDelta );
	InterpolateCommonReverbProps( interpolator, that );
	echoTime = interpolator( echoTime, that->echoTime, 0.075f, 0.25f );
	echoDepth = interpolator( echoDepth, that->echoDepth, 0.0f, 1.0f );
}

void EaxReverbEffect::CopyReverbProps( const ReverbEffect *effect ) {
	ReverbEffect::CopyReverbProps( effect );
	if( const auto *that = Cast<EaxReverbEffect *>( effect ) ) {
		this->echoTime = that->echoTime;
		this->echoDepth = that->echoDepth;
	} else {
		this->echoTime = 0.25f;
		this->echoDepth = 0.0f;
	}
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

class EffectSampler {
public:
	virtual Effect *TryApply( const ListenerProps &listenerProps, src_t *src, const src_t *tryReusePropsSrc ) = 0;
};

class ObstructedEffectSampler: public virtual EffectSampler {
protected:
	float ComputeDirectObstruction( const ListenerProps &listenerProps, src_t *src );
};

class UnderwaterFlangerEffectSampler final: public ObstructedEffectSampler {
public:
	Effect *TryApply( const ListenerProps &listenerProps, src_t *src, const src_t *tryReusePropsSrc ) override;
};

static UnderwaterFlangerEffectSampler underwaterFlangerEffectSampler;

class ReverbEffectSampler final: public ObstructedEffectSampler, private GenericRaycastSampler {
private:
	vec3_t primaryRayDirs[MAX_REVERB_PRIMARY_RAY_SAMPLES];
	vec3_t reflectionPoints[MAX_REVERB_PRIMARY_RAY_SAMPLES];
	float primaryHitDistances[MAX_REVERB_PRIMARY_RAY_SAMPLES];
	vec3_t testedListenerOrigin;

	const ListenerProps *listenerProps;
	src_t * src;
	ReverbEffect * effect;

	void ComputeReverberation( const ListenerProps &listenerProps_, src_t *src_, ReverbEffect *effect_ );

	void ResetMutableState( const ListenerProps &listenerProps_, src_t *src_, ReverbEffect *effect_ ) {
		this->listenerProps = &listenerProps_;
		this->src = src_;
		this->effect = effect_;

		GenericRaycastSampler::ResetMutableState( primaryRayDirs, reflectionPoints, primaryHitDistances, src->origin );

		VectorCopy( listenerProps_.origin, testedListenerOrigin );
		testedListenerOrigin[2] += 18.0f;
	}

	float GetEmissionRadius() const override;
	void SetupPrimaryRayDirs();
	void ProcessPrimaryEmissionResults();
	void EmitSecondaryRays();
	void SetMinimalReverbProps();

public:
	Effect *TryApply( const ListenerProps &listenerProps, src_t *src, const src_t *tryReusePropsSrc ) override;
};

static ReverbEffectSampler reverbEffectSampler;

// Tries to apply samplers starting from highest priority ones
static Effect *ENV_ApplyEffectSamplers( src_t *src, const src_t *tryReusePropsSrc ) {
	if( Effect *effect = underwaterFlangerEffectSampler.TryApply( listenerProps, src, tryReusePropsSrc ) ) {
		return effect;
	}
	if( Effect *effect = reverbEffectSampler.TryApply( listenerProps, src, tryReusePropsSrc ) ) {
		return effect;
	}

	trap_Error( "Can't find an applicable effect sampler\n" );
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
	updateState->needsInterpolation = true;

	// Get the leaf num before the update as it is important for all present tests
	updateState->leafNum = trap_PointLeafNum( src->origin );

	updateState->effect = ENV_ApplyEffectSamplers( src, tryReusePropsSrc );

	updateState->effect->distanceAtLastUpdate = sqrtf( DistanceSquared( src->origin, listenerProps.origin ) );
	updateState->effect->lastUpdateAt = millisNow;

	if( updateState->needsInterpolation ) {
		ENV_InterpolateEnvironmentProps( src, millisNow );
	}

	// Recycle the old effect
	effectsAllocator.DeleteEffect( updateState->oldEffect );
	updateState->oldEffect = nullptr;

	updateState->effect->BindOrUpdate( src );

	// Prevent reusing an outdated leaf num
	updateState->leafNum = -1;
}

Effect *UnderwaterFlangerEffectSampler::TryApply( const ListenerProps &listenerProps, src_t *src, const src_t * ) {
	if( !listenerProps.isInLiquid && !src->envUpdateState.isInLiquid ) {
		return nullptr;
	}

	float directObstruction = 0.9f;
	if( src->envUpdateState.isInLiquid && listenerProps.isInLiquid ) {
		directObstruction = ComputeDirectObstruction( listenerProps, src );
	}

	auto *effect = effectsAllocator.NewFlangerEffect( src );
	effect->directObstruction = directObstruction;
	effect->hasMediumTransition = src->envUpdateState.isInLiquid ^ listenerProps.isInLiquid;
	return effect;
}

Effect *ReverbEffectSampler::TryApply( const ListenerProps &listenerProps, src_t *src, const src_t *tryReusePropsSrc ) {
	ReverbEffect *effect = effectsAllocator.NewReverbEffect( src );
	effect->directObstruction = ComputeDirectObstruction( listenerProps, src );
	// We try reuse props only for reverberation effects
	// since reverberation effects sampling is extremely expensive.
	// Moreover, direct obstruction reuse is just not valid,
	// since even a small origin difference completely changes it.
	if( ENV_TryReuseSourceReverbProps( src, tryReusePropsSrc, effect ) ) {
		src->envUpdateState.needsInterpolation = false;
	} else {
		ComputeReverberation( listenerProps, src, effect );
	}
	return effect;
}

static inline unsigned ENV_GetNumSamplesForCurrentQuality( unsigned minSamples, unsigned maxSamples ) {
	float quality = s_environment_sampling_quality->value;

	assert( quality >= 0.0f && quality <= 1.0f );
	assert( minSamples < maxSamples );

	unsigned numSamples = (unsigned)( minSamples + ( maxSamples - minSamples ) * quality );
	assert( numSamples && numSamples <= maxSamples );
	return numSamples;
}

static void ENV_SetupDirectObstructionSamplingProps( src_t *src, unsigned minSamples, unsigned maxSamples ) {
	float quality = s_environment_sampling_quality->value;
	samplingProps_t *props = &src->envUpdateState.directObstructionSamplingProps;

	// If the quality is valid and has not been modified since the pattern has been set
	if( props->quality == quality ) {
		return;
	}

	unsigned numSamples = ENV_GetNumSamplesForCurrentQuality( minSamples, maxSamples );

	props->quality = quality;
	props->numSamples = numSamples;
	props->valueIndex = (uint16_t)( random() * std::numeric_limits<uint16_t>::max() );
}

float ObstructedEffectSampler::ComputeDirectObstruction( const ListenerProps &listenerProps, src_t *src ) {
	trace_t trace;
	envUpdateState_t *updateState;
	float *originOffset;
	vec3_t testedListenerOrigin;
	vec3_t testedSourceOrigin;
	float squareDistance;
	unsigned numTestedRays, numPassedRays;
	unsigned i, valueIndex;

	updateState = &src->envUpdateState;

	VectorCopy( listenerProps.origin, testedListenerOrigin );
	// TODO: We assume standard view height
	testedListenerOrigin[2] += 18.0f;

	squareDistance = DistanceSquared( testedListenerOrigin, src->origin );
	// Shortcut for sounds relative to the player
	if( squareDistance < 32.0f * 32.0f ) {
		return 0.0f;
	}

	if( !trap_LeafsInPVS( listenerProps.GetLeafNum(), trap_PointLeafNum( src->origin ) ) ) {
		return 1.0f;
	}

	trap_Trace( &trace, testedListenerOrigin, src->origin, vec3_origin, vec3_origin, MASK_SOLID );
	if( trace.fraction == 1.0f && !trace.startsolid ) {
		// Consider zero obstruction in this case
		return 0.0f;
	}

	ENV_SetupDirectObstructionSamplingProps( src, 3, MAX_DIRECT_OBSTRUCTION_SAMPLES );

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

float ReverbEffectSampler::GetEmissionRadius() const {
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

void ReverbEffectSampler::ComputeReverberation( const ListenerProps &listenerProps_,
												src_t *src_,
												ReverbEffect *effect_ ) {
	ResetMutableState( listenerProps_, src_, effect_ );

	numPrimaryRays = ENV_GetNumSamplesForCurrentQuality( 16, MAX_REVERB_PRIMARY_RAY_SAMPLES );

	SetupPrimaryRayDirs();

	EmitPrimaryRays();

	if( !numPrimaryHits ) {
		SetMinimalReverbProps();
		return;
	}

	ProcessPrimaryEmissionResults();
	EmitSecondaryRays();
}

void ReverbEffectSampler::SetupPrimaryRayDirs() {
	assert( numPrimaryRays );

	ENV_SetupSamplingRayDirs( primaryRayDirs, numPrimaryRays );
}

static void ENV_SetupSamplingRayDirs( vec3_t *rayDirs, unsigned numRays ) {
	// algorithm source https://stackoverflow.com/a/26127012
	for( unsigned i = 0; i < numRays; i++ ) {
		float offset = 2.0f / (float)numRays;
		float increment = ( (float)M_PI ) * ( 3.0f - sqrtf( 5.0f ) );

		float y = ( i * offset ) - 1 + ( offset / 2.0f );
		float r = sqrtf( 1 - y*y );
		float phi = i * increment;
		float x = cosf( phi ) * r;
		float z = sinf( phi ) * r;

		rayDirs[i][0] = x;
		rayDirs[i][1] = y;
		rayDirs[i][2] = z;
	}
}

void GenericRaycastSampler::EmitPrimaryRays() {
	const float primaryEmissionRadius = GetEmissionRadius();

	// These values must be reset at this stage
	assert( !averageDistance );
	assert( !numRaysHitSky );
	assert( !numRaysHitMetal );
	assert( !numRaysHitWater );
	assert( !numPrimaryHits );

	// Check whether sampling params have been set
	assert( numPrimaryRays );
	assert( primaryRayDirs );
	assert( primaryHitDistances );

	trace_t trace;
	for( unsigned i = 0; i < numPrimaryRays; ++i ) {
		float *sampleDir, *hitPoint;
		sampleDir = primaryRayDirs[i];

		vec3_t testedRayPoint;
		VectorScale( sampleDir, primaryEmissionRadius, testedRayPoint );
		VectorAdd( testedRayPoint, emissionOrigin, testedRayPoint );
		trap_Trace( &trace, emissionOrigin, testedRayPoint, vec3_origin, vec3_origin, MASK_SOLID | MASK_WATER );

		if( trace.startsolid || trace.allsolid ) {
			continue;
		}

		if( trace.fraction == 1.0f ) {
			continue;
		}

		// Check it before surf flags, otherwise a water gets cut off in almost all cases
		if( trace.contents & CONTENTS_WATER ) {
			numRaysHitWater++;
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

		if( surfFlags & SURF_METALSTEPS ) {
			numRaysHitMetal++;
		}

		if( DistanceSquared( emissionOrigin, trace.endpos ) < 2 * 2 ) {
			continue;
		}

		// Check whether recording hit points is requested (usually for secondary emission)
		if( primaryHitPoints ) {
			// Do not use the trace.endpos exactly as a source of a reflected wave.
			// (a following trace call would probably fail for this start origin).
			// Add -sampleDir offset to the trace.endpos
			hitPoint = primaryHitPoints[numPrimaryHits];
			VectorCopy( sampleDir, hitPoint );
			VectorNegate( hitPoint, hitPoint );
			VectorAdd( trace.endpos, hitPoint, hitPoint );
		}

		float squareDistance = DistanceSquared( emissionOrigin, trace.endpos );
		if( squareDistance < REVERB_ENV_DISTANCE_THRESHOLD * REVERB_ENV_DISTANCE_THRESHOLD ) {
			primaryHitDistances[numPrimaryHits] = sqrtf( squareDistance );
		} else {
			primaryHitDistances[numPrimaryHits] = REVERB_ENV_DISTANCE_THRESHOLD;
		}

		assert( primaryHitDistances[numPrimaryHits] >= 0.0f );
		assert( primaryHitDistances[numPrimaryHits] <= REVERB_ENV_DISTANCE_THRESHOLD );
		averageDistance += primaryHitDistances[numPrimaryHits];

		numPrimaryHits++;
	}

	if( numPrimaryHits ) {
		averageDistance /= (float)numPrimaryHits;
	}
}

float GenericRaycastSampler::ComputePrimaryHitDistanceStdDev() const {
	assert( numPrimaryHits );

	float variance = 0.0f;
	for( unsigned i = 0; i < numPrimaryHits; ++i ) {
		float delta = primaryHitDistances[i] - averageDistance;
		variance += delta * delta;
	}

	variance /= numPrimaryHits;
	return sqrtf( variance );
}

float GenericRaycastSampler::ComputeRoomSizeFactor() const {
	assert( numPrimaryHits );

	// Get the standard deviation of primary rays hit distances.
	const float sigma = ComputePrimaryHitDistanceStdDev();

	// Note: The hit distance distribution is not Gaussian and is not symmetrical!
	// It heavily depends of the real environment,
	// and might be close to Gaussian if a player is in a center of a huge box.
	// Getting the farthest point that is within 3 sigma does not feel good.

	// Select distances that >= the average distance
	float *contestedDistances = primaryHitDistances;
	int numContestedDistances = 0;
	for( unsigned i = 0; i < numPrimaryHits; ++i ) {
		if( primaryHitDistances[i] >= averageDistance ) {
			contestedDistances[numContestedDistances++] = primaryHitDistances[i];
		}
	}

	// We are sure there is at least 1 distance >= the average distance
	assert( numContestedDistances );

	// Sort hit distances so the nearest one is the first
	std::sort( contestedDistances, contestedDistances + numContestedDistances );

	const float connectivityDistance = std::max( 256.0f, sigma );

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

	// Prevent reusing primaryHitDistances as they were used as a scratchpad.
	// We do not complicate interface by adding another buffer to supply.
	primaryHitDistances = nullptr;

	return prevDistance / REVERB_ENV_DISTANCE_THRESHOLD;
}

void ReverbEffectSampler::ProcessPrimaryEmissionResults() {
	// Instead of trying to compute these factors every sampling call,
	// reuse pre-computed properties of CM map leafs that briefly resemble rooms/convex volumes.
	assert( src->envUpdateState.leafNum >= 0 );
	const LeafProps &leafProps = ::leafPropsCache->GetPropsForLeaf( src->envUpdateState.leafNum );

	const float roomSizeFactor = leafProps.RoomSizeFactor();
	const float metalFactor = leafProps.MetalFactor();
	const float skyFactor = leafProps.SkyFactor();

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
	effect->decayTime = 0.75f + 4.0f * roomSizeFactor + skyFactor;

	// Compute "gain factors" that are dependent of room size
	const float lateReverbGainFactor = ( 1.0f - roomSizeFactor ) * ( 1.0f - roomSizeFactor );
	const float reflectionsGainFactor = lateReverbGainFactor * ( 1.0f - roomSizeFactor );
	assert( lateReverbGainFactor >= 0.0f && lateReverbGainFactor <= 1.0f );
	assert( reflectionsGainFactor >= 0.0f && reflectionsGainFactor <= 1.0f );

	effect->reflectionsGain = 0.05f + 0.25f * reflectionsGainFactor * ( 1.0f - skyFactor );
	effect->lateReverbGain = 0.15f + 0.5f * lateReverbGainFactor * ( 1.0f - skyFactor );

	// Force effects strength "indoor"
	if( !skyFactor ) {
		effect->reflectionsGain += 0.25f * reflectionsGainFactor;
		effect->lateReverbGain += 0.5f * lateReverbGainFactor;
		// Hack: try to detect sewers/caves
		if( leafProps.WaterFactor() ) {
			effect->reflectionsGain += 0.5f * reflectionsGainFactor;
			effect->lateReverbGain += 0.75f * lateReverbGainFactor;
		}
	}

	effect->reflectionsDelay = 0.007f + 0.100f * roomSizeFactor;
	effect->lateReverbDelay = 0.011f + 0.055f * roomSizeFactor;

	if( auto *eaxEffect = Effect::Cast<EaxReverbEffect *>( effect ) ) {
		if( skyFactor ) {
			eaxEffect->echoTime = 0.075f + 0.125f * roomSizeFactor;
			// Raise echo depth until sky factor reaches 0.5f, then lower it.
			// So echo depth is within [0.25f, 0.5f] bounds and reaches its maximum at skyFactor = 0.5f
			if( skyFactor < 0.5f ) {
				eaxEffect->echoDepth = 0.25f + 0.5f * 2.0f * skyFactor;
			} else {
				eaxEffect->echoDepth = 0.75f - 0.3f * 2.0f * ( skyFactor - 0.5f );
			}
		} else {
			eaxEffect->echoTime = 0.25f;
			eaxEffect->echoDepth = 0.0f;
		}
	}
}

void ReverbEffectSampler::SetMinimalReverbProps() {
	effect->gain = 0.1f;
	effect->density = 1.0f;
	effect->diffusion = 1.0f;
	effect->decayTime = 0.75f;
	effect->reflectionsDelay = 0.007f;
	effect->lateReverbDelay = 0.011f;
	effect->gainHf = 0.1f;
	if( auto *eaxEffect = Effect::Cast<EaxReverbEffect *>( effect ) ) {
		eaxEffect->echoTime = 0.25f;
		eaxEffect->echoDepth = 0.0f;
	}
}

void ReverbEffectSampler::EmitSecondaryRays() {
	int listenerLeafNum = listenerProps->GetLeafNum();

	auto *const eaxEffect = Effect::Cast<EaxReverbEffect *>( effect );
	auto *const panningUpdateState = &src->panningUpdateState;

	trace_t trace;

	unsigned numPassedSecondaryRays = 0;
	if( eaxEffect ) {
		panningUpdateState->numReflectionPoints = 0;
		for( unsigned i = 0; i < numPrimaryHits; i++ ) {
			// Cut off by PVS system early, we are not interested in actual ray hit points contrary to the primary emission.
			if( !trap_LeafsInPVS( listenerLeafNum, trap_PointLeafNum( reflectionPoints[i] ) ) ) {
				continue;
			}

			trap_Trace( &trace, reflectionPoints[i], testedListenerOrigin, vec3_origin, vec3_origin, MASK_SOLID );
			if( trace.fraction == 1.0f && !trace.startsolid ) {
				numPassedSecondaryRays++;
				float *savedPoint = panningUpdateState->reflectionPoints[panningUpdateState->numReflectionPoints++];
				VectorCopy( reflectionPoints[i], savedPoint );
			}
		}
	} else {
		for( unsigned i = 0; i < numPrimaryHits; i++ ) {
			if( !trap_LeafsInPVS( listenerLeafNum, trap_PointLeafNum( reflectionPoints[i] ) ) ) {
				continue;
			}

			trap_Trace( &trace, reflectionPoints[i], testedListenerOrigin, vec3_origin, vec3_origin, MASK_SOLID );
			if( trace.fraction == 1.0f && !trace.startsolid ) {
				numPassedSecondaryRays++;
			}
		}
	}

	if( numPrimaryHits ) {
		float frac = numPassedSecondaryRays / (float)numPrimaryHits;
		// The secondary rays obstruction is complement to the `frac`
		effect->secondaryRaysObstruction = 1.0f - frac;
		// A absence of a HF attenuation sounds poor, metallic/reflective environments should be the only exception.
		const LeafProps &leafProps = ::leafPropsCache->GetPropsForLeaf( src->envUpdateState.leafNum );
		effect->gainHf = 0.1f + ( 0.4f + 0.4f * leafProps.MetalFactor() ) * frac;
		// We also modify effect gain by a fraction of secondary rays passed to listener.
		// This is not right in theory, but is inevitable in the current game sound model
		// where you can hear across the level through solid walls
		// in order to avoid messy echoes coming from everywhere.
		effect->gain *= 0.75f + 0.25f * frac;
	} else {
		// Set minimal feasible values
		effect->secondaryRaysObstruction = 1.0f;
		effect->gainHf = 0.1f;
		effect->gain *= 0.75f;
	}
}

void EaxReverbEffect::UpdatePanning( src_s *src, const vec3_t listenerOrigin, const mat3_t listenerAxes ) {
	const auto *updateState = &src->panningUpdateState;

	vec3_t avgDir = { 0, 0, 0 };
	unsigned numDirs = 0;
	for( unsigned i = 0; i < updateState->numReflectionPoints; ++i ) {
		vec3_t dir;
		VectorSubtract( listenerOrigin, src->panningUpdateState.reflectionPoints[i], dir );
		float squareDistance = VectorLengthSquared( dir );
		// Do not even take into account directions that have very short segments
		if( squareDistance < 72.0f * 72.0f ) {
			continue;
		}

		numDirs++;
		float distance = sqrtf( squareDistance );
		VectorScale( dir, 1.0f / distance, dir );
		VectorAdd( avgDir, dir, avgDir );
	}

	if( numDirs > 1 ) {
		VectorScale( avgDir, 1.0f / numDirs, avgDir );
	}

	vec3_t pan;
	// Convert to "speakers" coordinate system
	pan[0] = -DotProduct( avgDir, &listenerAxes[AXIS_RIGHT] );
	pan[1] = DotProduct( avgDir, &listenerAxes[AXIS_UP] );
	pan[2] = -DotProduct( avgDir, &listenerAxes[AXIS_FORWARD] );

	// Lower the vector magnitude, otherwise panning is way too strong and feels weird.
	VectorScale( pan, 0.7f, pan );

	qalEffectfv( src->effect, AL_EAXREVERB_REFLECTIONS_PAN, pan );
	qalEffectfv( src->effect, AL_EAXREVERB_LATE_REVERB_PAN, pan );
}
