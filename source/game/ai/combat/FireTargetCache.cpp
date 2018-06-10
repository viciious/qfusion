#include "FireTargetCache.h"
#include "../ai_trajectory_predictor.h"
#include "../ai_shutdown_hooks_holder.h"
#include "../bot.h"

class FixedBitVector
{
private:
	unsigned size;   // Count of bits this vector is capable to contain
	uint32_t *words; // Actual bits data. We are limited 32-bit words to work fast on 32-bit processors.

public:
	FixedBitVector( unsigned size_ ) : size( size_ ) {
		words = (uint32_t *)( G_Malloc( size_ / 8 + 4 ) );
		Clear();
	}

	// These following move-related members are mandatory for intended BitVectorHolder behavior

	FixedBitVector( FixedBitVector &&that ) {
		size = that.size;
		words = that.words;
		that.words = nullptr;
	}

	FixedBitVector &operator=( FixedBitVector &&that ) {
		if( words ) {
			G_Free( words );
		}
		size = that.size;
		words = that.words;
		that.words = nullptr;
		return *this;
	}

	~FixedBitVector() {
		// If not moved
		if( words ) {
			G_Free( words );
		}
	}

	void Clear() { memset( words, 0, size / 8 + 4 ); }

	// TODO: Shift by a variable may be an interpreted instruction on some CPUs

	inline bool IsSet( int bitIndex ) const {
		unsigned wordIndex = (unsigned)bitIndex / 32;
		unsigned bitOffset = (unsigned)bitIndex - wordIndex * 32;

		return ( words[wordIndex] & ( 1 << bitOffset ) ) != 0;
	}

	inline void Set( int bitIndex, bool value ) const {
		unsigned wordIndex = (unsigned)bitIndex / 32;
		unsigned bitOffset = (unsigned)bitIndex - wordIndex * 32;
		if( value ) {
			words[wordIndex] |= ( (unsigned)value << bitOffset );
		} else {
			words[wordIndex] &= ~( (unsigned)value << bitOffset );
		}
	}
};

class BitVectorHolder
{
private:
	StaticVector<FixedBitVector, 1> vectorHolder;
	unsigned size;
	bool hasRegisteredShutdownHook;

	BitVectorHolder( BitVectorHolder &&that ) = delete;

public:
	BitVectorHolder() : size( std::numeric_limits<unsigned>::max() ), hasRegisteredShutdownHook( false ) {}

	FixedBitVector &Get( unsigned size_ ) {
		if( this->size != size_ ) {
			vectorHolder.clear();
		}

		this->size = size_;

		if( vectorHolder.empty() ) {
			vectorHolder.emplace_back( FixedBitVector( size_ ) );
		}

		if( !hasRegisteredShutdownHook ) {
			// Clean up the held bit vector on shutdown
			const auto hook = [&]() {
								  this->vectorHolder.clear();
							  };
			AiShutdownHooksHolder::Instance()->RegisterHook( hook );
			hasRegisteredShutdownHook = true;
		}

		return vectorHolder[0];
	}
};

static BitVectorHolder visitedFacesHolder;
static BitVectorHolder visitedAreasHolder;

struct PointAndDistance {
	Vec3 point;
	float distance;
	inline PointAndDistance( const Vec3 &point_, float distance_ ) : point( point_ ), distance( distance_ ) {}
	inline bool operator<( const PointAndDistance &that ) const { return distance < that.distance; }
};

constexpr int MAX_CLOSEST_FACE_POINTS = 8;

static void FindClosestAreasFacesPoints( float splashRadius, const vec3_t target, int startAreaNum,
										 StaticVector<PointAndDistance, MAX_CLOSEST_FACE_POINTS + 1> &closestPoints ) {
	// Retrieve these instances before the loop
	const AiAasWorld *aasWorld = AiAasWorld::Instance();
	FixedBitVector &visitedFaces = visitedFacesHolder.Get( (unsigned)aasWorld->NumFaces() );
	FixedBitVector &visitedAreas = visitedAreasHolder.Get( (unsigned)aasWorld->NumFaces() );

	visitedFaces.Clear();
	visitedAreas.Clear();

	// Actually it is not a limit of a queue capacity but a limit of processed areas number
	constexpr int MAX_FRINGE_AREAS = 16;
	// This is a breadth-first search fringe queue for a BFS through areas
	int areasFringe[MAX_FRINGE_AREAS];

	// Points to a head (front) of the fringe queue.
	int areasFringeHead = 0;
	// Points after a tail (back) of the fringe queue.
	int areasFringeTail = 0;
	// Push the start area to the queue
	areasFringe[areasFringeTail++] = startAreaNum;

	while( areasFringeHead < areasFringeTail ) {
		const int areaNum = areasFringe[areasFringeHead++];
		visitedAreas.Set( areaNum, true );

		const aas_area_t *area = aasWorld->Areas() + areaNum;

		for( int faceIndexNum = area->firstface; faceIndexNum < area->firstface + area->numfaces; ++faceIndexNum ) {
			int faceIndex = aasWorld->FaceIndex()[faceIndexNum];

			// If the face has been already processed, skip it
			if( visitedFaces.IsSet( abs( faceIndex ) ) ) {
				continue;
			}

			// Mark the face as processed
			visitedFaces.Set( abs( faceIndex ), true );

			// Get actual face and area behind it by a sign of the faceIndex
			const aas_face_t *face;
			int areaBehindFace;
			if( faceIndex >= 0 ) {
				face = aasWorld->Faces() + faceIndex;
				areaBehindFace = face->backarea;
			} else {
				face = aasWorld->Faces() - faceIndex;
				areaBehindFace = face->frontarea;
			}

			// Determine a distance from the target to the face
			const aas_plane_t *plane = aasWorld->Planes() + face->planenum;
			const aas_edge_t *anyFaceEdge = aasWorld->Edges() + abs( aasWorld->EdgeIndex()[face->firstedge] );

			Vec3 anyPlanePointToTarget( target );
			anyPlanePointToTarget -= aasWorld->Vertexes()[anyFaceEdge->v[0]];
			const float pointToFaceDistance = anyPlanePointToTarget.Dot( plane->normal );

			// This is the actual loop stop condition.
			// This means that `areaBehindFace` will not be pushed to the fringe queue, and the queue will shrink.
			if( pointToFaceDistance > splashRadius ) {
				continue;
			}

			// If the area borders with a solid
			if( areaBehindFace == 0 ) {
				Vec3 projectedPoint = Vec3( target ) - pointToFaceDistance * Vec3( plane->normal );
				// We are sure we always have a free slot (closestPoints.capacity() == MAX_CLOSEST_FACE_POINTS + 1)
				closestPoints.push_back( PointAndDistance( projectedPoint, pointToFaceDistance ) );
				std::push_heap( closestPoints.begin(), closestPoints.end() );
				// Ensure that we have a free slot by evicting a largest distance point
				// Do this afterward the addition to allow a newly added point win over some old one
				if( closestPoints.size() == closestPoints.capacity() ) {
					std::pop_heap( closestPoints.begin(), closestPoints.end() );
					closestPoints.pop_back();
				}
			}
			// If the area behind face is not checked yet and areas BFS limit is not reached
			else if( !visitedAreas.IsSet( areaBehindFace ) && areasFringeTail != MAX_FRINGE_AREAS ) {
				// Enqueue `areaBehindFace` to the fringe queue
				areasFringe[areasFringeTail++] = areaBehindFace;
			}
		}
	}

	// `closestPoints` is a heap arranged for quick eviction of the largest value.
	// We have to sort it in ascending order
	std::sort( closestPoints.begin(), closestPoints.end() );
}

void BotFireTargetCache::AdjustAimParams( const SelectedEnemies &selectedEnemies, const SelectedWeapons &selectedWeapons,
										  const GenericFireDef &fireDef, AimParams *aimParams ) {
	SetupCoarseFireTarget( selectedEnemies, fireDef, aimParams->fireOrigin, aimParams->fireTarget );

	switch( fireDef.AimType() ) {
		case AI_WEAPON_AIM_TYPE_PREDICTION_EXPLOSIVE:
			AdjustPredictionExplosiveAimTypeParams( selectedEnemies, selectedWeapons, fireDef, aimParams );
			break;
		case AI_WEAPON_AIM_TYPE_PREDICTION:
			AdjustPredictionAimTypeParams( selectedEnemies, selectedWeapons, fireDef, aimParams );
			break;
		case AI_WEAPON_AIM_TYPE_DROP:
			AdjustDropAimTypeParams( selectedEnemies, selectedWeapons, fireDef, aimParams );
			break;
		default:
			AdjustInstantAimTypeParams( selectedEnemies, selectedWeapons, fireDef, aimParams );
			break;
	}
}

bool BotFireTargetCache::AdjustTargetByEnvironmentTracing( const SelectedEnemies &selectedEnemies, float splashRadius,
														   AimParams *aimParams ) {
	trace_t trace;

	float minSqDistance = 999999.0f;
	vec_t nearestPoint[3] = { NAN, NAN, NAN };  // Avoid an "uninitialized" compiler/inspection warning

	edict_t *traceKey = const_cast<edict_t *>( selectedEnemies.TraceKey() );
	float *firePoint = const_cast<float*>( aimParams->fireOrigin );

	if( selectedEnemies.OnGround() ) {
		Vec3 groundPoint( aimParams->fireTarget );
		groundPoint.Z() += playerbox_stand_maxs[2];
		// Check whether shot to this point is not blocked
		edict_t *self = game.edicts + bot->EntNum();
		G_Trace( &trace, firePoint, nullptr, nullptr, groundPoint.Data(), self, MASK_AISOLID );
		if( trace.fraction > 0.999f || selectedEnemies.TraceKey() == game.edicts + trace.ent ) {
			float skill = bot->Skill();
			// For mid-skill bots it may be enough. Do not waste cycles.
			if( skill < 0.66f && random() < ( 1.0f - skill ) ) {
				aimParams->fireTarget[2] += playerbox_stand_mins[2];
				return true;
			}

			VectorCopy( groundPoint.Data(), nearestPoint );
			minSqDistance = playerbox_stand_mins[2] * playerbox_stand_mins[2];
		}
	}

	Vec3 toTargetDir( aimParams->fireTarget );
	toTargetDir -= aimParams->fireOrigin;
	float sqDistanceToTarget = toTargetDir.SquaredLength();
	// Not only prevent division by zero, but keep target as-is when it is colliding with bot
	if( sqDistanceToTarget < 16 * 16 ) {
		return false;
	}

	// Normalize to target dir
	toTargetDir *= Q_RSqrt( sqDistanceToTarget );
	Vec3 traceEnd = Vec3( aimParams->fireTarget ) + splashRadius * toTargetDir;

	// We hope this function will be called rarely only when somebody wants to load a stripped Q3 AAS.
	// Just trace an environment behind the bot, it is better than it used to be anyway.
	G_Trace( &trace, aimParams->fireTarget, nullptr, nullptr, traceEnd.Data(), traceKey, MASK_AISOLID );
	if( trace.fraction != 1.0f ) {
		// First check whether an explosion in the point behind may damage the target to cut a trace quickly
		float sqDistance = DistanceSquared( aimParams->fireTarget, trace.endpos );
		if( sqDistance < minSqDistance ) {
			// trace.endpos will be overwritten
			Vec3 pointBehind( trace.endpos );
			// Check whether shot to this point is not blocked
			edict_t *self = game.edicts + bot->EntNum();
			G_Trace( &trace, firePoint, nullptr, nullptr, pointBehind.Data(), self, MASK_AISOLID );
			if( trace.fraction > 0.999f || selectedEnemies.TraceKey() == game.edicts + trace.ent ) {
				minSqDistance = sqDistance;
				VectorCopy( pointBehind.Data(), nearestPoint );
			}
		}
	}

	// Modify `target` if we have found some close solid point
	if( minSqDistance <= splashRadius ) {
		VectorCopy( nearestPoint, aimParams->fireTarget );
		return true;
	}
	return false;
}

bool BotFireTargetCache::AdjustTargetByEnvironmentWithAAS( const SelectedEnemies &selectedEnemies,
														   float splashRadius, int areaNum, AimParams *aimParams ) {
	// We can't just get a closest point from AAS world, it may be blocked for shooting.
	// Also we can't check each potential point for being blocked, tracing is very expensive.
	// Instead we get MAX_CLOSEST_FACE_POINTS best points and for each check whether it is blocked.
	// We hope at least a single point will not be blocked.

	StaticVector<PointAndDistance, MAX_CLOSEST_FACE_POINTS + 1> closestAreaFacePoints;
	FindClosestAreasFacesPoints( splashRadius, aimParams->fireTarget, areaNum, closestAreaFacePoints );

	trace_t trace;

	// On each step get a best point left and check it for being blocked for shooting
	// We assume that FindClosestAreasFacesPoints() returns a sorted array where closest points are first
	for( const PointAndDistance &pointAndDistance: closestAreaFacePoints ) {
		float *traceEnd = const_cast<float*>( pointAndDistance.point.Data() );
		edict_t *passent = game.edicts + bot->EntNum();
		G_Trace( &trace, aimParams->fireOrigin, nullptr, nullptr, traceEnd, passent, MASK_AISOLID );

		if( trace.fraction > 0.999f || selectedEnemies.TraceKey() == game.edicts + trace.ent ) {
			VectorCopy( traceEnd, aimParams->fireTarget );
			return true;
		}
	}

	return false;
}

constexpr float GENERIC_PROJECTILE_COORD_AIM_ERROR = 75.0f;
constexpr float GENERIC_INSTANTHIT_COORD_AIM_ERROR = 100.0f;

bool BotFireTargetCache::AdjustTargetByEnvironment( const SelectedEnemies &selectedEnemies,
													float splashRaidus, AimParams *aimParams ) {
	int targetAreaNum = 0;
	// Reject AAS worlds that look like stripped
	if( AiAasWorld::Instance()->NumFaces() > 512 ) {
		targetAreaNum = AiAasWorld::Instance()->FindAreaNum( aimParams->fireTarget );
	}

	if( targetAreaNum ) {
		return AdjustTargetByEnvironmentWithAAS( selectedEnemies, splashRaidus, targetAreaNum, aimParams );
	}

	return AdjustTargetByEnvironmentTracing( selectedEnemies, splashRaidus, aimParams );
}

void BotFireTargetCache::AdjustPredictionExplosiveAimTypeParams( const SelectedEnemies &selectedEnemies,
																 const SelectedWeapons &selectedWeapons,
																 const GenericFireDef &fireDef,
																 AimParams *aimParams ) {
	bool wasCached = cachedFireTarget.IsValidFor( selectedEnemies, selectedWeapons );
	GetPredictedTargetOrigin( selectedEnemies, selectedWeapons, fireDef.ProjectileSpeed(), aimParams );
	// If new generic predicted target origin has been computed, adjust it for target environment
	if( !wasCached ) {
		// First, modify temporary `target` value
		AdjustTargetByEnvironment( selectedEnemies, fireDef.SplashRadius(), aimParams );
		// Copy modified `target` value to cached value
		cachedFireTarget.origin = Vec3( aimParams->fireTarget );
	}
	// Accuracy for air rockets is worse anyway (movement prediction in gravity field is approximate)
	aimParams->suggestedBaseCoordError = 1.3f * ( 1.01f - bot->Skill() ) * GENERIC_PROJECTILE_COORD_AIM_ERROR;
}


void BotFireTargetCache::AdjustPredictionAimTypeParams( const SelectedEnemies &selectedEnemies,
														const SelectedWeapons &selectedWeapons,
														const GenericFireDef &fireDef,
														AimParams *aimParams ) {
	aimParams->suggestedBaseCoordError = GENERIC_PROJECTILE_COORD_AIM_ERROR;
	if( fireDef.IsBuiltin() ) {
		switch( fireDef.WeaponNum() ) {
			case WEAP_PLASMAGUN:
				aimParams->suggestedBaseCoordError *= 0.5f * ( 1.0f - bot->Skill() );
				break;
			case WEAP_ELECTROBOLT:
				// Do not apply any error in this case (set it to some very low feasible value)
				aimParams->suggestedBaseCoordError = 1.0f;
				break;
			default:
				// Shut up an analyzer
				break;
		}
	}

	GetPredictedTargetOrigin( selectedEnemies, selectedWeapons, fireDef.ProjectileSpeed(), aimParams );
}

void BotFireTargetCache::AdjustDropAimTypeParams( const SelectedEnemies &selectedEnemies,
												  const SelectedWeapons &selectedWeapons,
												  const GenericFireDef &fireDef,
												  AimParams *aimParams ) {
	bool wasCached = cachedFireTarget.IsValidFor( selectedEnemies, selectedWeapons );
	GetPredictedTargetOrigin( selectedEnemies, selectedWeapons, fireDef.ProjectileSpeed(), aimParams );
	// If new generic predicted target origin has been computed, adjust it for gravity (changes will be cached)
	if( !wasCached ) {
		// It is not very accurate but satisfactory
		Vec3 fireOriginToTarget = Vec3( aimParams->fireTarget ) - aimParams->fireOrigin;
		Vec3 fireOriginToTarget2D( fireOriginToTarget.X(), fireOriginToTarget.Y(), 0 );
		float squareDistance2D = fireOriginToTarget2D.SquaredLength();
		if( squareDistance2D > 0 ) {
			Vec3 velocity2DVec( fireOriginToTarget );
			velocity2DVec.NormalizeFast();
			velocity2DVec *= fireDef.ProjectileSpeed();
			velocity2DVec.Z() = 0;
			float squareVelocity2D = velocity2DVec.SquaredLength();
			if( squareVelocity2D > 0 ) {
				float distance2D = 1.0f / Q_RSqrt( squareDistance2D );
				float velocity2D = 1.0f / Q_RSqrt( squareVelocity2D );
				float time = distance2D / velocity2D;
				float height = std::max( 0.0f, 0.5f * level.gravity * time * time - 32.0f );
				// Modify both cached and temporary values
				cachedFireTarget.origin.Z() += height;
				aimParams->fireTarget[2] += height;
			}
		}
	}

	// This kind of weapons is not precise by its nature, do not add any more noise.
	aimParams->suggestedBaseCoordError = 0.3f * ( 1.01f - bot->Skill() ) * GENERIC_PROJECTILE_COORD_AIM_ERROR;
}

void BotFireTargetCache::AdjustInstantAimTypeParams( const SelectedEnemies &selectedEnemies,
													 const SelectedWeapons &selectedWeapons,
													 const GenericFireDef &fireDef, AimParams *aimParams ) {
	aimParams->suggestedBaseCoordError = GENERIC_INSTANTHIT_COORD_AIM_ERROR;
}

void BotFireTargetCache::SetupCoarseFireTarget( const SelectedEnemies &selectedEnemies,
												const GenericFireDef &fireDef,
												vec_t *fire_origin, vec_t *target ) {
	const float skill = bot->Skill();
	// For hard bots use actual enemy origin
	// (last seen one may be outdated up to 3 frames, and it matter a lot for fast-moving enemies)
	if( skill < 0.66f ) {
		VectorCopy( selectedEnemies.LastSeenOrigin().Data(), target );
	} else {
		VectorCopy( selectedEnemies.ActualOrigin().Data(), target );
	}

	// For hitscan weapons we try to imitate a human-like aiming.
	// We get a weighted last seen enemy origin/velocity and extrapolate origin a bit.
	// Do not add extra aiming error for other aim styles (these aim styles are not precise by their nature).
	if( fireDef.AimType() == AI_WEAPON_AIM_TYPE_INSTANT_HIT ) {
		vec3_t velocity;
		if( skill < 0.66f ) {
			VectorCopy( selectedEnemies.LastSeenVelocity().Data(), velocity );
		} else {
			VectorCopy( selectedEnemies.ActualVelocity().Data(), velocity );
		}

		const auto &snapshots = selectedEnemies.LastSeenSnapshots();
		const int64_t levelTime = level.time;
		// Skilled bots have this value lesser (this means target will be closer to an actual origin)
		const unsigned maxTimeDelta = (unsigned)( 900 - 550 * skill );
		const float weightTimeDeltaScale = 1.0f / maxTimeDelta;
		float weightsSum = 1.0f;
		for( auto iter = snapshots.begin(), end = snapshots.end(); iter != end; ++iter ) {
			const auto &snapshot = *iter;
			unsigned timeDelta = (unsigned)( levelTime - snapshot.Timestamp() );
			// If snapshot is too outdated, stop accumulation
			if( timeDelta > maxTimeDelta ) {
				break;
			}

			// Recent snapshots have greater weight
			float weight = 1.0f - timeDelta * weightTimeDeltaScale;
			// We have to store these temporarily unpacked values in locals
			Vec3 snapshotOrigin( snapshot.Origin() );
			Vec3 snapshotVelocity( snapshot.Velocity() );
			// Accumulate snapshot target origin using the weight
			VectorMA( target, weight, snapshotOrigin.Data(), target );
			// Accumulate snapshot target velocity using the weight
			VectorMA( velocity, weight, snapshotVelocity.Data(), velocity );
			weightsSum += weight;
		}
		float invWeightsSum = 1.0f / weightsSum;
		// Make `target` contain a weighted sum of enemy snapshot origin
		VectorScale( target, invWeightsSum, target );
		// Make `velocity` contain a weighted sum of enemy snapshot velocities
		VectorScale( velocity, invWeightsSum, velocity );

		if( extrapolationRandomTimeoutAt < levelTime ) {
			// Make constant part lesser for higher skill
			extrapolationRandom = ( 0.75f - 0.5f * skill ) * random() + 0.25f + 0.5f * ( 1.0f - skill );
			extrapolationRandomTimeoutAt = levelTime + 150;
		}
		float extrapolationTimeSeconds = 0.0001f * ( 900 - 650 * skill ) * extrapolationRandom;
		// Add some extrapolated target movement
		VectorMA( target, extrapolationTimeSeconds, velocity, target );
	}

	const edict_t *self = game.edicts + bot->EntNum();

	fire_origin[0] = self->s.origin[0];
	fire_origin[1] = self->s.origin[1];
	fire_origin[2] = self->s.origin[2] + self->viewheight;
}

// This is a port of public domain projectile prediction code by Kain Shin
// http://ringofblades.com/Blades/Code/PredictiveAim.cs
// This function assumes that target velocity is constant and gravity is not applied to projectile and target.
bool PredictProjectileNoClip( const Vec3 &fireOrigin, float projectileSpeed, vec3_t target, const Vec3 &targetVelocity ) {
	constexpr float EPSILON = 0.0001f;

	float projectileSpeedSq = projectileSpeed * projectileSpeed;
	float targetSpeedSq = targetVelocity.SquaredLength();
	float targetSpeed = sqrtf( targetSpeedSq );
	Vec3 targetToFire = fireOrigin - target;
	float targetToFireDistSq = targetToFire.SquaredLength();
	float targetToFireDist = sqrtf( targetToFireDistSq );
	Vec3 targetToFireDir( targetToFire );
	targetToFireDir.Normalize();

	Vec3 targetVelocityDir( targetVelocity );
	targetVelocityDir.Normalize();

	float cosTheta = targetToFireDir.Dot( targetVelocityDir );

	float t;
	if( fabsf( projectileSpeedSq - targetSpeedSq ) < EPSILON ) {
		if( cosTheta <= 0 ) {
			return false;
		}

		t = 0.5f * targetToFireDist / ( targetSpeed * cosTheta );
	} else {
		float a = projectileSpeedSq - targetSpeedSq;
		float b = 2.0f * targetToFireDist * targetSpeed * cosTheta;
		float c = -targetToFireDistSq;
		float discriminant = b * b - 4.0f * a * c;

		if( discriminant < 0 ) {
			return false;
		}

		float uglyNumber = sqrtf( discriminant );
		float t0 = 0.5f * ( -b + uglyNumber ) / a;
		float t1 = 0.5f * ( -b - uglyNumber ) / a;
		t = std::min( t0, t1 );
		if( t < EPSILON ) {
			t = std::max( t0, t1 );
		}

		if( t < EPSILON ) {
			return false;
		}
	}

	Vec3 move = targetVelocity * t;
	VectorAdd( target, move.Data(), target );
	return true;
}

void BotFireTargetCache::GetPredictedTargetOrigin( const SelectedEnemies &selectedEnemies,
												   const SelectedWeapons &selectedWeapons,
												   float projectileSpeed, AimParams *aimParams ) {
	if( bot->Skill() < 0.33f || selectedEnemies.IsStaticSpot() ) {
		return;
	}

	// Check whether we are shooting the same enemy and cached predicted origin is not outdated
	if( cachedFireTarget.IsValidFor( selectedEnemies, selectedWeapons ) ) {
		VectorCopy( cachedFireTarget.origin.Data(), aimParams->fireTarget );
	} else {
		PredictProjectileShot( selectedEnemies, projectileSpeed, aimParams, true );
		cachedFireTarget.invalidAt = level.time + 66;
		cachedFireTarget.CacheFor( selectedEnemies, selectedWeapons, aimParams->fireTarget );
	}
}

static inline void TryAimingAtGround( trace_t *trace, AimParams *aimParams, edict_t *traceKey ) {
	// In this case try fallback to the ground below the target.
	Vec3 endPoint( 0, 0, -128 );
	endPoint += aimParams->fireTarget;
	G_Trace( trace, aimParams->fireTarget, nullptr, nullptr, endPoint.Data(), traceKey, MASK_AISOLID );
	if( trace->fraction != 1.0f ) {
		VectorCopy( trace->endpos, aimParams->fireTarget );
		// Add some offset from the ground (enviroment tests probably expect this input).
		aimParams->fireTarget[2] += 1.0f;
	}
}

class HitPointPredictor final: private AiTrajectoryPredictor {
public:

	struct ProblemParams {
		const Vec3 fireOrigin;
		const Vec3 targetVelocity;
		const edict_t *enemyEnt;
		const float projectileSpeed;

		ProblemParams( const vec3_t fireOrigin_, const vec3_t targetVelocity_,
					   const edict_t *enemyEnt_, const float projectileSpeed_ )
			: fireOrigin( fireOrigin_ ), targetVelocity( targetVelocity_ ),
			  enemyEnt( enemyEnt_ ), projectileSpeed( projectileSpeed_ ) {}
	};

private:
	const ProblemParams *problemParams;
	float *fireTarget;

	bool tryHitInAir;
	bool hasFailed;

	bool OnPredictionStep( const Vec3 &segmentStart, const Results *results ) override;
public:
	HitPointPredictor()
		: problemParams( nullptr ),
		  fireTarget( nullptr ),
		  tryHitInAir( true ),
		  hasFailed( false ) {}

	bool Run( const ProblemParams &problemParams_, float skill, vec3_t fireTarget_ );
};

bool HitPointPredictor::Run( const ProblemParams &problemParams_, float skill, vec3_t fireTarget_ ) {
	SetStepMillis( (unsigned)( 200.0f - 100.0f * skill ) );
	SetNumSteps( (unsigned)( 6 + 12 * skill ) );

	tryHitInAir = true;
	hasFailed = false;
	this->problemParams = &problemParams_;
	this->fireTarget = fireTarget_;

	SetEntitiesCollisionProps( true, ENTNUM( problemParams_.enemyEnt ) );
	SetColliderBounds( vec3_origin, playerbox_stand_maxs );
	AddStopEventFlags( HIT_SOLID );
	SetExtrapolateLastStep( true );

	Vec3 startOrigin( fireTarget_ );
	AiTrajectoryPredictor::Results baseResults;
	auto stopEvents = AiTrajectoryPredictor::Run( problemParams->targetVelocity, startOrigin, &baseResults );

	// If a hit point has been found in OnPredictionStep(), a prediction gets interrupted
	return ( stopEvents & INTERRUPTED ) && !hasFailed;
}

bool HitPointPredictor::OnPredictionStep( const Vec3 &segmentStart, const Results *results ) {
	if( results->trace->fraction != 1.0f ) {
		if( ( results->trace->contents & CONTENTS_NODROP ) || ( results->trace->surfFlags & SURF_NOIMPACT ) ) {
			hasFailed = true;
		} else {
			VectorCopy( results->trace->endpos, fireTarget );
			if( ISWALKABLEPLANE( &results->trace->plane ) ) {
				// There are sophisticated algorithms applied on top of this result
				// for determining where to shoot an explosive, don't stick to a ground.
				fireTarget[2] += -playerbox_stand_mins[2];
			}
		}

		// Interrupt base trajectory prediction
		return false;
	}

	// Wait for hitting a solid
	if( !tryHitInAir ) {
		return true;
	}

	// const float zPartAtSegmentStart = 0.001f * ( results->millisAhead - stepMillis ) * level.gravity;
	// const float zPartAtSegmentEnd = 0.001f * results->millisAhead * level.gravity;
	// segmentTargetVelocity.Z() -= 0.5f * ( zPartAtSegmentStart + zPartAtSegmentEnd );
	float newTargetZSpeed = problemParams->targetVelocity.Z();
	newTargetZSpeed -= 0.5f * 0.001f * ( 2.0f * results->millisAhead - stepMillis ) * level.gravity;

	// Wait for a negative target velocity or for hitting a solid.
	// Do not try to hit in-air in this case (it yields poor results).
	if( newTargetZSpeed > 0 ) {
		return true;
	}

	Vec3 segmentTargetVelocity( problemParams->targetVelocity );
	segmentTargetVelocity.Z() = newTargetZSpeed;

	// TODO: Projectile speed used in PredictProjectileNoClip() needs correction
	// We can't offset fire origin since we do not know direction to target yet
	// Instead, increase projectile speed used in calculations according to currTime
	// Exact formula is to be proven yet
	Vec3 predictedTarget( segmentStart );
	if( !PredictProjectileNoClip( problemParams->fireOrigin, problemParams->projectileSpeed,
								  predictedTarget.Data(), segmentTargetVelocity ) ) {
		tryHitInAir = false;
		// Wait for hitting a solid
		return true;
	}

	// Check whether predictedTarget is within [currPoint, nextPoint]
	// where extrapolation that use currTargetVelocity is assumed to be valid.
	Vec3 segmentEnd( results->origin );
	Vec3 segmentVec( segmentEnd );
	segmentVec -= segmentStart;
	Vec3 predictedTargetToEndVec( segmentEnd );
	predictedTargetToEndVec -= predictedTarget;
	Vec3 predictedTargetToStartVec( segmentStart );
	predictedTargetToStartVec -= predictedTarget;

	if( segmentVec.Dot( predictedTargetToEndVec ) >= 0 && segmentVec.Dot( predictedTargetToStartVec ) <= 0 ) {
		// Can hit in air
		predictedTarget.CopyTo( fireTarget );
		// Interrupt the base trajectory prediction
		return false;
	}

	// Continue a base trajectory prediction
	return true;
}

void BotFireTargetCache::PredictProjectileShot( const SelectedEnemies &selectedEnemies, float projectileSpeed,
												AimParams *aimParams, bool applyTargetGravity ) {
	if( projectileSpeed <= 0.0f ) {
		return;
	}

	trace_t trace;
	edict_t *traceKey = const_cast<edict_t*>( selectedEnemies.TraceKey() );

	if( applyTargetGravity ) {
		HitPointPredictor predictor;
		HitPointPredictor::ProblemParams predictionParams( aimParams->fireOrigin,
														   selectedEnemies.LastSeenVelocity().Data(),
														   selectedEnemies.TraceKey(),
														   projectileSpeed );

		if( !predictor.Run( predictionParams, bot->Skill(), aimParams->fireTarget ) ) {
			TryAimingAtGround( &trace, aimParams, traceKey );
		}
		return;
	}

	Vec3 predictedTarget( aimParams->fireTarget );
	if( !PredictProjectileNoClip( Vec3( aimParams->fireOrigin ),
								  projectileSpeed,
								  predictedTarget.Data(),
								  selectedEnemies.LastSeenVelocity() ) ) {
		TryAimingAtGround( &trace, aimParams, traceKey );
		return;
	}

	// Test a segment between predicted target and initial target
	// Aim at the trace hit point if there is an obstacle.
	// Aim at the predicted target otherwise.
	G_Trace( &trace, aimParams->fireTarget, nullptr, nullptr, predictedTarget.Data(), traceKey, MASK_AISOLID );
	if( trace.fraction == 1.0f ) {
		VectorCopy( predictedTarget.Data(), aimParams->fireTarget );
	} else {
		VectorCopy( trace.endpos, aimParams->fireTarget );
		if( ISWALKABLEPLANE( &trace.plane ) ) {
			// There are sophisticated algorithms applied on top of this result
			// for determining where to shoot an explosive, don't stick to a ground.
			aimParams->fireTarget[2] += -playerbox_stand_mins[2];
		}
	}
}