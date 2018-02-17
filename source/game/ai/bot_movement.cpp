#include "bot.h"
#include "bot_movement.h"
#include "ai_aas_world.h"
#include "ai_manager.h"
#include "tactical_spots_registry.h"
#include "ai_nav_mesh_manager.h"
#include "ai_trajectory_predictor.h"

#ifndef PUBLIC_BUILD
#define CHECK_ACTION_SUGGESTION_LOOPS
#define ENABLE_MOVEMENT_ASSERTIONS
#define CHECK_INFINITE_NEXT_STEP_LOOPS
static int nextStepIterationsCounter = 0;
static constexpr int NEXT_STEP_INFINITE_LOOP_THRESHOLD = 10000;
#endif

// Useful for debugging but freezes even Release version
#if 0
#define ENABLE_MOVEMENT_DEBUG_OUTPUT
#endif

inline float GetPMoveStatValue( const player_state_t *playerState, int statIndex, float defaultValue ) {
	float value = playerState->pmove.stats[statIndex];
	// Put likely case (the value is not specified) first
	return value < 0 ? defaultValue : value;
}

inline float BotMovementPredictionContext::GetJumpSpeed() const {
	return GetPMoveStatValue( this->currPlayerState, PM_STAT_JUMPSPEED, DEFAULT_JUMPSPEED * GRAVITY_COMPENSATE );
}

inline float BotMovementPredictionContext::GetDashSpeed() const {
	return GetPMoveStatValue( this->currPlayerState, PM_STAT_DASHSPEED, DEFAULT_DASHSPEED );
}

inline float BotMovementPredictionContext::GetRunSpeed() const {
	return GetPMoveStatValue( this->currPlayerState, PM_STAT_MAXSPEED, DEFAULT_PLAYERSPEED );
}

inline Vec3 BotMovementPredictionContext::NavTargetOrigin() const {
	return self->ai->botRef->NavTargetOrigin();
}

inline float BotMovementPredictionContext::NavTargetRadius() const {
	return self->ai->botRef->NavTargetRadius();
}

inline bool BotMovementPredictionContext::IsCloseToNavTarget() const {
	float distance = NavTargetRadius() + 32.0f;
	return NavTargetOrigin().SquareDistanceTo( movementState->entityPhysicsState.Origin() ) < distance * distance;
}

inline int BotMovementPredictionContext::CurrAasAreaNum() const {
	if( int currAasAreaNum = movementState->entityPhysicsState.CurrAasAreaNum() ) {
		return currAasAreaNum;
	}

	return movementState->entityPhysicsState.DroppedToFloorAasAreaNum();
}

inline int BotMovementPredictionContext::CurrGroundedAasAreaNum() const {
	const auto *aasWorld = AiAasWorld::Instance();
	const auto &entityPhysicsState = movementState->entityPhysicsState;
	int areaNums[2] = { entityPhysicsState.CurrAasAreaNum(), entityPhysicsState.DroppedToFloorAasAreaNum() };
	for( int i = 0, end = ( areaNums[0] != areaNums[1] ? 2 : 1 ); i < end; ++i ) {
		if( areaNums[i] && aasWorld->AreaGrounded( areaNums[i] ) ) {
			return areaNums[i];
		}
	}
	return 0;
}

inline int BotMovementPredictionContext::NavTargetAasAreaNum() const {
	return self->ai->botRef->NavTargetAasAreaNum();
}

inline bool BotMovementPredictionContext::IsInNavTargetArea() const {
	const int navTargetAreaNum = NavTargetAasAreaNum();
	if( !navTargetAreaNum ) {
		return false;
	}

	const auto &entityPhysicsState = this->movementState->entityPhysicsState;
	if( navTargetAreaNum == entityPhysicsState.CurrAasAreaNum() ) {
		return true;
	}

	if( navTargetAreaNum == entityPhysicsState.DroppedToFloorAasAreaNum() ) {
		return true;
	}

	return false;
}

inline bool IsInsideHugeArea( const float *origin, const aas_area_t &area, float offset ) {
	if( area.mins[0] > origin[0] - offset || area.maxs[0] < origin[0] + offset ) {
		return false;
	}

	if( area.mins[1] > origin[1] - offset || area.maxs[1] < origin[1] + offset ) {
		return false;
	}

	return true;
}

class CanSafelyKeepHighSpeedPredictor: protected AiTrajectoryPredictor {
protected:
	AiAasWorld *aasWorld;
	bool hasFailed;

	bool OnPredictionStep( const Vec3 &segmentStart, const Results *results ) override;

public:
	const float *startVelocity;
	const float *startOrigin;

	bool Exec();

	CanSafelyKeepHighSpeedPredictor()
		: aasWorld( nullptr ), hasFailed( false ), startVelocity( nullptr ), startOrigin( nullptr ) {
		SetStepMillis( 200 );
		SetNumSteps( 4 );
		SetColliderBounds( playerbox_stand_mins, playerbox_stand_maxs );
		AddStopEventFlags( HIT_SOLID | HIT_LIQUID );
	}
};

class KeepHighSpeedWithoutNavTargetPredictor final: protected CanSafelyKeepHighSpeedPredictor {
	typedef CanSafelyKeepHighSpeedPredictor Super;
public:
	// We do not want users to confuse different subtypes of CanSafelyKeepHighSpeedPredictor
	// by occasionally assigning to a supertype pointer losing type info
	// (some fields should be set explicitly for a concrete type and other approaches add way too much clutter)
	// Thats why a protected inheritance is used, and these fields should be exposed manually.
	using Super::Exec;
	using Super::startVelocity;
	using Super::startOrigin;
};

static KeepHighSpeedWithoutNavTargetPredictor keepHighSpeedWithoutNavTargetPredictor;

class KeepHighSpeedMovingToNavTargetPredictor final: protected CanSafelyKeepHighSpeedPredictor {
	typedef CanSafelyKeepHighSpeedPredictor Super;
	bool OnPredictionStep( const Vec3 &segmentStart, const Results *results ) override;
public:
	AiAasRouteCache *routeCache;
	int navTargetAreaNum;
	int startTravelTime;

	using Super::Exec;
	using Super::startVelocity;
	using Super::startOrigin;

	KeepHighSpeedMovingToNavTargetPredictor()
		: routeCache( nullptr ),
		  navTargetAreaNum( 0 ),
		  startTravelTime( 0 ) {}
};

static KeepHighSpeedMovingToNavTargetPredictor keepHighSpeedMovingToNavTargetPredictor;

bool Bot::TestWhetherCanSafelyKeepHighSpeed( BotMovementPredictionContext *context ) {
	const int navTargetAreaNum = context ? context->NavTargetAasAreaNum() : self->ai->botRef->NavTargetAasAreaNum();
	if( !navTargetAreaNum ) {
		auto *predictor = &::keepHighSpeedWithoutNavTargetPredictor;
		if( context ) {
			predictor->startVelocity = context->movementState->entityPhysicsState.Velocity();
			predictor->startOrigin = context->movementState->entityPhysicsState.Origin();
		} else {
			predictor->startVelocity = self->velocity;
			predictor->startOrigin = self->s.origin;
		}
		return predictor->Exec();
	}

	const AiEntityPhysicsState *entityPhysicsState;
	int startTravelTime = std::numeric_limits<int>::max();
	if( context ) {
		entityPhysicsState = &context->movementState->entityPhysicsState;
		startTravelTime = context->TravelTimeToNavTarget();
	} else {
		entityPhysicsState = self->ai->botRef->EntityPhysicsState();
		int startAreaNums[2] = { 0, 0 };
		int numStartAreas = entityPhysicsState->PrepareRoutingStartAreas( startAreaNums );
		int goalAreaNum = self->ai->botRef->NavTargetAasAreaNum();
		if( !( startTravelTime = routeCache->PreferredRouteToGoalArea( startAreaNums, numStartAreas, goalAreaNum ) ) ) {
			return false;
		}
	}

	auto *predictor = &::keepHighSpeedMovingToNavTargetPredictor;
	predictor->startVelocity = entityPhysicsState->Velocity();
	predictor->startOrigin = entityPhysicsState->Origin();
	predictor->navTargetAreaNum = navTargetAreaNum;
	predictor->startTravelTime = startTravelTime;
	predictor->routeCache = self->ai->botRef->routeCache;

	return predictor->Exec();
}

bool CanSafelyKeepHighSpeedPredictor::OnPredictionStep( const Vec3 &segmentStart, const Results *results ) {
	if( results->trace->fraction == 1.0f ) {
		return true;
	}

	// Disallow bumping into walls (it can lead to cycling in tight environments)
	if( !ISWALKABLEPLANE( &results->trace->plane ) ) {
		hasFailed = true;
		return false;
	}

	// Disallow falling or landing that looks like falling
	if( results->trace->endpos[2] + 8 - playerbox_stand_mins[2] < startOrigin[2] ) {
		hasFailed = true;
	}

	// Interrupt the base prediction
	return false;
}

bool CanSafelyKeepHighSpeedPredictor::Exec() {
	this->aasWorld = AiAasWorld::Instance();
	this->hasFailed = false;

	AiTrajectoryPredictor::Results predictionResults;
	auto stopEvents = AiTrajectoryPredictor::Run( startVelocity, startOrigin, &predictionResults );
	return !( stopEvents & HIT_LIQUID ) && ( stopEvents & INTERRUPTED ) && !hasFailed;
}

bool KeepHighSpeedMovingToNavTargetPredictor::OnPredictionStep( const Vec3 &segmentStart, const Results *results ) {
	// Continue the base prediction loop in this case waiting for actually hitting a brush
	if( Super::OnPredictionStep( segmentStart, results ) ) {
		return true;
	}

	// There is no need for further checks in this case
	if( hasFailed ) {
		return false;
	}

	// Find the area num of the trace hit pos
	int areaNum;

	// Try offsetting hit pos from the hit surface before making FindAreaNum() call
	// otherwise its very likely to yield a zero area on the first test in FindAreaNum(),
	// thus leading to repeated BSP traversal attempts in FindAreaNum()
	Vec3 segmentDir( results->origin );
	segmentDir -= segmentStart;
	float squareSegmentLength = segmentDir.SquaredLength();
	if( squareSegmentLength > 2 * 2 ) {
		segmentDir *= 1.0f / sqrtf( squareSegmentLength );
		Vec3 originForAreaNum( results->trace->endpos );
		originForAreaNum -= segmentDir;
		areaNum = aasWorld->FindAreaNum( originForAreaNum );
	} else {
		areaNum = aasWorld->FindAreaNum( results->trace->endpos );
	}

	// Don't check whether area num is zero, it should be extremely rare and handled by the router in that case

	int travelTimeAtLanding = routeCache->PreferredRouteToGoalArea( areaNum, navTargetAreaNum );
	if( !travelTimeAtLanding || travelTimeAtLanding > startTravelTime ) {
		hasFailed = true;
	}

	// Interrupt the prediction
	return false;
}

inline bool BotMovementPredictionContext::CanSafelyKeepHighSpeed() {
	if( const bool *cachedValue = canSafelyKeepHighSpeedCachesStack.GetCached() ) {
		return *cachedValue;
	}

	bool result = self->ai->botRef->TestWhetherCanSafelyKeepHighSpeed( this );
	canSafelyKeepHighSpeedCachesStack.SetCachedValue( result );
	return result;
}

void BotMovementPredictionContext::NextReachNumAndTravelTimeToNavTarget( int *reachNum, int *travelTimeToNavTarget ) {
	*reachNum = 0;
	*travelTimeToNavTarget = 0;

	// Do NOT use cached reachability chain for the frame (if any).
	// It might be invalid after movement step, and the route cache does caching itself pretty well.

	const int navTargetAreaNum = NavTargetAasAreaNum();
	if( !navTargetAreaNum ) {
		return;
	}

	const auto *routeCache = self->ai->botRef->routeCache;

	int fromAreaNums[2];
	int numFromAreas = movementState->entityPhysicsState.PrepareRoutingStartAreas( fromAreaNums );
	if( int travelTime = routeCache->PreferredRouteToGoalArea( fromAreaNums, numFromAreas, navTargetAreaNum, reachNum ) ) {
		*travelTimeToNavTarget = travelTime;
	}
}

inline const AiAasRouteCache *BotMovementPredictionContext::RouteCache() const {
	return self->ai->botRef->routeCache;
}

inline const ArrayRange<int> BotMovementPredictionContext::TravelFlags() const {
	return self->ai->botRef->TravelFlags();
}

#define CHECK_STATE_FLAG( state, bit )                                                    \
	if( ( expectedStatesMask & ( 1 << bit ) ) != ( ( (unsigned)state.IsActive() ) << ( 1 << bit ) ) )  \
	{                                                                                       \
		result = false;                                                                     \
		if( logFunc ) {                                                                        \
			logFunc( format, Nick( owner ), #state ".IsActive()", (unsigned)state.IsActive() ); } \
	}

bool BotMovementState::TestActualStatesForExpectedMask( unsigned expectedStatesMask, const edict_t *owner ) const {
	// Might be set to null if verbose logging is not needed
#ifdef ENABLE_MOVEMENT_DEBUG_OUTPUT
	void ( *logFunc )( const char *format, ... ) = G_Printf;
#elif defined( CHECK_INFINITE_NEXT_STEP_LOOPS )
	void ( *logFunc )( const char *format, ... );
	// Suppress output if the iterations counter is within a feasible range
	if( ::nextStepIterationsCounter < NEXT_STEP_INFINITE_LOOP_THRESHOLD ) {
		logFunc = nullptr;
	} else {
		logFunc = G_Printf;
	}
#else
	void ( logFunc )( const char *format, ... ) = nullptr;
#endif
	constexpr const char *format = "BotMovementState(%s): %s %d has mismatch with the mask value\n";

	bool result = true;
	CHECK_STATE_FLAG( jumppadMovementState, 0 );
	CHECK_STATE_FLAG( weaponJumpMovementState, 1 );
	CHECK_STATE_FLAG( pendingLookAtPointState, 2 );
	CHECK_STATE_FLAG( campingSpotState, 3 );
	// Skip keyMoveDirsState.
	// It either should not affect movement at all if regular movement is chosen,
	// or should be handled solely by the combat movement code.
	CHECK_STATE_FLAG( flyUntilLandingMovementState, 4 );
	return result;
}

BotBaseMovementAction *BotMovementPredictionContext::GetCachedActionAndRecordForCurrTime( BotMovementActionRecord *record_ ) {
	const int64_t levelTime = level.time;
	PredictedMovementAction *prevPredictedAction = nullptr;
	PredictedMovementAction *nextPredictedAction = nullptr;
	for( PredictedMovementAction &predictedAction: predictedMovementActions ) {
		if( predictedAction.timestamp >= levelTime ) {
			nextPredictedAction = &predictedAction;
			break;
		}
		prevPredictedAction = &predictedAction;
	}

	if( !nextPredictedAction ) {
		Debug( "Cannot use predicted movement action: next one (its timestamp is not in the past) cannot be found\n" );
		return nullptr;
	}

	if( !prevPredictedAction ) {
		// If there were no activated actions, the next state must be recently computed for current level time.
		Assert( nextPredictedAction->timestamp == levelTime );
		// These assertions have already spotted a bug
		Assert( VectorCompare( nextPredictedAction->entityPhysicsState.Origin(), self->s.origin ) );
		Assert( VectorCompare( nextPredictedAction->entityPhysicsState.Velocity(), self->velocity ) );
		// If there is a modified velocity, it will be copied with this record and then applied
		*record_ = nextPredictedAction->record;
		Debug( "Using just computed predicted movement action %s\n", nextPredictedAction->action->Name() );
		return nextPredictedAction->action;
	}

	Assert( prevPredictedAction->timestamp + prevPredictedAction->stepMillis == nextPredictedAction->timestamp );

	// Fail prediction if both previous and next predicted movement states mask mismatch the current movement state
	const auto &actualMovementState = self->ai->botRef->movementState;
	if( !actualMovementState.TestActualStatesForExpectedMask( prevPredictedAction->movementStatesMask, self ) ) {
		if( !actualMovementState.TestActualStatesForExpectedMask( nextPredictedAction->movementStatesMask, self ) ) {
			return nullptr;
		}
	}

	// Check whether predicted action is valid for an actual bot entity physics state
	float stateLerpFrac = (float)( levelTime - prevPredictedAction->timestamp );
	stateLerpFrac *= 1.0f / ( nextPredictedAction->timestamp - prevPredictedAction->timestamp );
	Assert( stateLerpFrac > 0 && stateLerpFrac <= 1.0f );
	const char *format =
		"Prev predicted action timestamp is " PRId64 ", next predicted action is " PRId64 ", level.time is %" PRId64 "\n";
	Debug( format, prevPredictedAction->timestamp, nextPredictedAction->timestamp, level.time );
	Debug( "Should interpolate entity physics state using fraction %f\n", stateLerpFrac );

	const auto &prevPhysicsState = prevPredictedAction->entityPhysicsState;
	const auto &nextPhysicsState = nextPredictedAction->entityPhysicsState;

	// Prevent cache invalidation on each frame if bot is being hit by a continuous fire weapon and knocked back.
	// Perform misprediction test only on the 3rd frame after a last knockback timestamp.
	if( level.time - self->ai->botRef->lastKnockbackAt > 32 ) {
		vec3_t expectedOrigin;
		VectorLerp( prevPhysicsState.Origin(), stateLerpFrac, nextPhysicsState.Origin(), expectedOrigin );
		float squaredDistanceMismatch = DistanceSquared( self->s.origin, expectedOrigin );
		if( squaredDistanceMismatch > 3.0f * 3.0f ) {
			float distanceMismatch = SQRTFAST( squaredDistanceMismatch );
			const char *format_ = "Cannot use predicted movement action: distance mismatch %f is too high for lerp frac %f\n";
			Debug( format_, distanceMismatch, stateLerpFrac );
			return nullptr;
		}

		float expectedSpeed = ( 1.0f - stateLerpFrac ) * prevPhysicsState.Speed() + stateLerpFrac * nextPhysicsState.Speed();
		float actualSpeed = self->ai->botRef->entityPhysicsState->Speed();
		float speedMismatch = fabsf( actualSpeed - expectedSpeed );
		if( speedMismatch > 0.005f * expectedSpeed ) {
			Debug( "Expected speed: %.1f, actual speed: %.1f, speed mismatch: %.1f\n", expectedSpeed, actualSpeed,
				   speedMismatch );
			Debug( "Cannot use predicted movement action: speed mismatch is too high\n" );
			return nullptr;
		}

		if( actualSpeed > 30.0f ) {
			vec3_t expectedVelocity;
			VectorLerp( prevPhysicsState.Velocity(), stateLerpFrac, nextPhysicsState.Velocity(), expectedVelocity );
			Vec3 expectedVelocityDir( expectedVelocity );
			expectedVelocityDir *= 1.0f / expectedSpeed;
			Vec3 actualVelocityDir( self->velocity );
			actualVelocityDir *= 1.0f / actualSpeed;
			float cosine = expectedVelocityDir.Dot( actualVelocityDir );
			static const float MIN_COSINE = cosf( (float) DEG2RAD( 5.0f ) );
			if( cosine < MIN_COSINE ) {
				Debug( "An angle between expected and actual velocities is %f degrees\n",(float) RAD2DEG( acosf( cosine ) ) );
				Debug( "Cannot use predicted movement action:  expected and actual velocity directions differ significantly\n" );
				return nullptr;
			}
		}

		if( !nextPredictedAction->record.botInput.canOverrideLookVec ) {
			Vec3 prevStateAngles( prevPhysicsState.Angles() );
			Vec3 nextStateAngles( nextPhysicsState.Angles() );

			vec3_t expectedAngles;
			for( int i : { YAW, ROLL } )
				expectedAngles[i] = LerpAngle( prevStateAngles.Data()[i], nextStateAngles.Data()[i], stateLerpFrac );

			if( !nextPredictedAction->record.botInput.canOverridePitch ) {
				expectedAngles[PITCH] = LerpAngle( prevStateAngles.Data()[PITCH], nextStateAngles.Data()[PITCH],
												   stateLerpFrac );
			} else {
				expectedAngles[PITCH] = self->s.angles[PITCH];
			}

			vec3_t expectedLookDir;
			AngleVectors( expectedAngles, expectedLookDir, nullptr, nullptr );
			float cosine = self->ai->botRef->entityPhysicsState->ForwardDir().Dot( expectedLookDir );
			static const float MIN_COSINE = cosf( (float) DEG2RAD( 5.0f ) );
			if( cosine < MIN_COSINE ) {
				Debug( "An angle between and actual look directions is %f degrees\n", (float) RAD2DEG( acosf( cosine ) ) );
				Debug( "Cannot use predicted movement action: expected and actual look directions differ significantly\n" );
				return nullptr;
			}
		}
	}

	// If next predicted state is likely to be completed next frame, use its input as-is (except the velocity)
	if( nextPredictedAction->timestamp - levelTime <= game.frametime ) {
		*record_ = nextPredictedAction->record;
		// Apply modified velocity only once for an exact timestamp
		if( nextPredictedAction->timestamp != levelTime ) {
			record_->hasModifiedVelocity = false;
		}
		return nextPredictedAction->action;
	}

	float inputLerpFrac = game.frametime / ( (float)( nextPredictedAction->timestamp - levelTime ) );
	Assert( inputLerpFrac > 0 && inputLerpFrac <= 1.0f );
	// If next predicted time is likely to be pending next frame again, interpolate input for a single frame ahead
	*record_ = nextPredictedAction->record;
	// Prevent applying a modified velocity from the new state
	record_->hasModifiedVelocity = false;
	if( !record_->botInput.canOverrideLookVec ) {
		Vec3 actualLookDir( self->ai->botRef->entityPhysicsState->ForwardDir() );
		Vec3 intendedLookVec( record_->botInput.IntendedLookDir() );
		VectorLerp( actualLookDir.Data(), inputLerpFrac, intendedLookVec.Data(), intendedLookVec.Data() );
		record_->botInput.SetIntendedLookDir( intendedLookVec );
	}

	auto prevRotationMask = (unsigned)prevPredictedAction->record.botInput.allowedRotationMask;
	auto nextRotationMask = (unsigned)nextPredictedAction->record.botInput.allowedRotationMask;
	record_->botInput.allowedRotationMask = (BotInputRotation)( prevRotationMask & nextRotationMask );

	return nextPredictedAction->action;
}

BotBaseMovementAction *BotMovementPredictionContext::GetActionAndRecordForCurrTime( BotMovementActionRecord *record_ ) {
	auto *action = GetCachedActionAndRecordForCurrTime( record_ );
	if( !action ) {
		BuildPlan();
		action = GetCachedActionAndRecordForCurrTime( record_ );
	}

	//AITools_DrawColorLine(self->s.origin, (Vec3(0, 0, 48) + self->s.origin).Data(), action->DebugColor(), 0);
	return action;
}

void BotMovementPredictionContext::ShowBuiltPlanPath() const {
	for( unsigned i = 0, j = 1; j < predictedMovementActions.size(); ++i, ++j ) {
		int color;
		switch( i % 3 ) {
			case 0: color = COLOR_RGB( 192, 0, 0 ); break;
			case 1: color = COLOR_RGB( 0, 192, 0 ); break;
			case 2: color = COLOR_RGB( 0, 0, 192 ); break;
		}
		const float *v1 = predictedMovementActions[i].entityPhysicsState.Origin();
		const float *v2 = predictedMovementActions[j].entityPhysicsState.Origin();
		AITools_DrawColorLine( v1, v2, color, 0 );
	}
}

const Ai::ReachChainVector &BotMovementPredictionContext::NextReachChain() {
	if( const auto *cachedReachChain = reachChainsCachesStack.GetCached() ) {
		return *cachedReachChain;
	}

	Ai::ReachChainVector dummy;
	const Ai::ReachChainVector *oldReachChain = &dummy;
	if( const auto *cachedOldReachChain = reachChainsCachesStack.GetCachedValueBelowTopOfStack() ) {
		oldReachChain = cachedOldReachChain;
	}

	auto *newReachChain = new( reachChainsCachesStack.GetUnsafeBufferForCachedValue() ) Ai::ReachChainVector;
	self->ai->botRef->UpdateReachChain( *oldReachChain, newReachChain, movementState->entityPhysicsState );
	return *newReachChain;
};

inline BotEnvironmentTraceCache &BotMovementPredictionContext::EnvironmentTraceCache() {
	return environmentTestResultsStack.back();
}

typedef BotEnvironmentTraceCache::ObstacleAvoidanceResult ObstacleAvoidanceResult;

inline ObstacleAvoidanceResult BotMovementPredictionContext::TryAvoidFullHeightObstacles( float correctionFraction ) {
	// Make a modifiable copy of the intended look dir
	Vec3 intendedLookVec( this->record->botInput.IntendedLookDir() );
	auto result = EnvironmentTraceCache().TryAvoidFullHeightObstacles( this, &intendedLookVec, correctionFraction );
	if( result == ObstacleAvoidanceResult::CORRECTED ) {
		// Write the modified intended look dir back in this case
		this->record->botInput.SetIntendedLookDir( intendedLookVec );
	}
	return result;
}

inline ObstacleAvoidanceResult BotMovementPredictionContext::TryAvoidJumpableObstacles( float correctionFraction ) {
	// Make a modifiable copy of the intended look dir
	Vec3 intendedLookVec( this->record->botInput.IntendedLookDir() );
	auto result = EnvironmentTraceCache().TryAvoidJumpableObstacles( this, &intendedLookVec, correctionFraction );
	if( result == ObstacleAvoidanceResult::CORRECTED ) {
		// Write the modified intended look dir back in this case
		this->record->botInput.SetIntendedLookDir( intendedLookVec );
	}
	return result;
}

static void Intercepted_PredictedEvent( int entNum, int ev, int parm ) {
	game.edicts[entNum].ai->botRef->OnInterceptedPredictedEvent( ev, parm );
}

static void Intercepted_PMoveTouchTriggers( pmove_t *pm, vec3_t previous_origin ) {
	game.edicts[pm->playerState->playerNum + 1].ai->botRef->OnInterceptedPMoveTouchTriggers( pm, previous_origin );
}

static void Intercepted_Trace( trace_t *t, vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end,
							   int ignore, int contentmask, int timeDelta ) {
	trap_CM_TransformedBoxTrace( t, start, end, mins, maxs, nullptr, contentmask, nullptr, nullptr );
}

static int Intercepted_PointContents( vec3_t p, int timeDelta ) {
	return trap_CM_TransformedPointContents( p, nullptr, nullptr, nullptr );
}

void BotMovementPredictionContext::OnInterceptedPredictedEvent( int ev, int parm ) {
	switch( ev ) {
		case EV_JUMP:
			this->frameEvents.hasJumped = true;
			break;
		case EV_DASH:
			this->frameEvents.hasDashed = true;
			break;
		case EV_WALLJUMP:
			this->frameEvents.hasWalljumped = true;
			break;
		case EV_FALL:
			this->frameEvents.hasTakenFallDamage = true;
			break;
		default: // Shut up an analyzer
			break;
	}
}

void BotMovementPredictionContext::OnInterceptedPMoveTouchTriggers( pmove_t *pm, vec3_t const previousOrigin ) {
	edict_t *ent = game.edicts + pm->playerState->POVnum;
	// update the entity with the new position
	VectorCopy( pm->playerState->pmove.origin, ent->s.origin );
	VectorCopy( pm->playerState->pmove.velocity, ent->velocity );
	VectorCopy( pm->playerState->viewangles, ent->s.angles );
	ent->viewheight = (int)pm->playerState->viewheight;
	VectorCopy( pm->mins, ent->r.mins );
	VectorCopy( pm->maxs, ent->r.maxs );

	ent->waterlevel = pm->waterlevel;
	ent->watertype = pm->watertype;
	if( pm->groundentity == -1 ) {
		ent->groundentity = NULL;
	} else {
		ent->groundentity = &game.edicts[pm->groundentity];
		ent->groundentity_linkcount = ent->groundentity->linkcount;
	}

	GClip_LinkEntity( ent );

	// expand the search bounds to include the space between the previous and current origin
	vec3_t mins, maxs;
	for( int i = 0; i < 3; i++ ) {
		if( previousOrigin[i] < pm->playerState->pmove.origin[i] ) {
			mins[i] = previousOrigin[i] + pm->maxs[i];
			if( mins[i] > pm->playerState->pmove.origin[i] + pm->mins[i] ) {
				mins[i] = pm->playerState->pmove.origin[i] + pm->mins[i];
			}
			maxs[i] = pm->playerState->pmove.origin[i] + pm->maxs[i];
		} else {
			mins[i] = pm->playerState->pmove.origin[i] + pm->mins[i];
			maxs[i] = previousOrigin[i] + pm->mins[i];
			if( maxs[i] < pm->playerState->pmove.origin[i] + pm->maxs[i] ) {
				maxs[i] = pm->playerState->pmove.origin[i] + pm->maxs[i];
			}
		}
	}

	// Make a local copy of the reference for faster access (avoid accessing shared library relocation table).
	edict_t *const gameEdicts = game.edicts;

	nearbyTriggersCache.EnsureValidForBounds( mins, maxs );

	for( int i = 0; i < nearbyTriggersCache.numJumppadEnts; ++i ) {
		if( GClip_EntityContact( mins, maxs, gameEdicts + nearbyTriggersCache.jumppadEntNums[i] ) ) {
			frameEvents.hasTouchedJumppad = true;
			break;
		}
	}

	for( int i = 0; i < nearbyTriggersCache.numTeleportEnts; ++i ) {
		if( GClip_EntityContact( mins, maxs, gameEdicts + nearbyTriggersCache.teleportEntNums[i] ) ) {
			frameEvents.hasTouchedTeleporter = true;
			break;
		}
	}

	for( int i = 0; i < nearbyTriggersCache.numPlatformEnts; ++i ) {
		if( GClip_EntityContact( mins, maxs, gameEdicts + nearbyTriggersCache.platformEntNums[i] ) ) {
			frameEvents.hasTouchedPlatform = true;
			break;
		}
	}

	if ( nearbyTriggersCache.numOtherEnts <= FrameEvents::MAX_TOUCHED_OTHER_TRIGGERS ) {
		for( int i = 0; i < nearbyTriggersCache.numOtherEnts; ++i ) {
			uint16_t entNum = nearbyTriggersCache.otherEntNums[i];
			if( GClip_EntityContact( mins, maxs, gameEdicts + entNum ) ) {
				frameEvents.otherTouchedTriggerEnts[frameEvents.numOtherTouchedTriggers++] = entNum;
			}
		}
	} else {
		for( int i = 0; i < nearbyTriggersCache.numOtherEnts; ++i ) {
			uint16_t entNum = nearbyTriggersCache.otherEntNums[i];
			if( GClip_EntityContact( mins, maxs, gameEdicts + entNum ) ) {
				frameEvents.otherTouchedTriggerEnts[frameEvents.numOtherTouchedTriggers++] = entNum;
				if( frameEvents.numOtherTouchedTriggers == FrameEvents::MAX_TOUCHED_OTHER_TRIGGERS ) {
					break;
				}
			}
		}
	}
}

void BotMovementPredictionContext::NearbyTriggersCache::EnsureValidForBounds( const vec3_t absMins,
																			  const vec3_t absMaxs ) {
	int i = 0;
	for( i = 0; i < 3; ++i ) {
		if ( lastComputedForMins[i] + 192.0f > absMins[i] ) {
			break;
		}
		if ( lastComputedForMaxs[i] - 192.0f < absMaxs[i] ) {
			break;
		}
	}

	// If all coords have passed tests
	if ( i == 3 ) {
		return;
	}

	VectorSet( lastComputedForMins, -256, -256, -256 );
	VectorSet( lastComputedForMaxs, +256, +256, +256 );
	VectorAdd( absMins, lastComputedForMins, lastComputedForMins );
	VectorAdd( absMaxs, lastComputedForMaxs, lastComputedForMaxs );

	numTeleportEnts = numJumppadEnts = numPlatformEnts = numOtherEnts = 0;

	constexpr auto maxEnts = 3 * MAX_GROUP_ENTITIES + MAX_OTHER_ENTITIES;
	int entNums[maxEnts];
	const int numEnts = GClip_AreaEdicts( lastComputedForMins, lastComputedForMaxs, entNums, maxEnts, AREA_TRIGGERS, 0 );

	const edict_t *const gameEdicts = game.edicts;
	for( i = 0; i < numEnts; ++i ) {
		const edict_t *ent = gameEdicts + entNums[i];
		if( !ent->r.inuse ) {
			continue;
		}

		const char *classname = ent->classname;
		if( !classname ) {
			continue;
		}

		if( !Q_stricmp( "func_plat", classname ) ) {
			if( numPlatformEnts != MAX_GROUP_ENTITIES ) {
				platformEntNums[numPlatformEnts++] = (uint16_t)entNums[i];
			}
			continue;
		}

		if( !Q_stricmp( "trigger_push", classname ) ) {
			if( numJumppadEnts != MAX_GROUP_ENTITIES ) {
				jumppadEntNums[numJumppadEnts] = (uint16_t)entNums[i];
			}
			continue;
		}

		if( !Q_stricmp( "trigger_teleport", classname ) ) {
			if( numTeleportEnts != MAX_GROUP_ENTITIES ) {
				teleportEntNums[numTeleportEnts++] = (uint16_t)entNums[i];
			}
			continue;
		}

		if( numOtherEnts != MAX_OTHER_ENTITIES ) {
			otherEntNums[numOtherEnts++] = (uint16_t)entNums[i];
		}
	}
}

void BotMovementPredictionContext::SetupStackForStep() {
	PredictedMovementAction *topOfStack;
	if( topOfStackIndex > 0 ) {
		Assert( predictedMovementActions.size() );
		Assert( botMovementStatesStack.size() == predictedMovementActions.size() );
		Assert( playerStatesStack.size() == predictedMovementActions.size() );
		Assert( pendingWeaponsStack.size() == predictedMovementActions.size() );

		Assert( defaultBotInputsCachesStack.Size() == predictedMovementActions.size() );
		Assert( reachChainsCachesStack.Size() == predictedMovementActions.size() );
		Assert( mayHitWhileRunningCachesStack.Size() == predictedMovementActions.size() );
		Assert( canSafelyKeepHighSpeedCachesStack.Size() == predictedMovementActions.size() );
		Assert( environmentTestResultsStack.size() == predictedMovementActions.size() );

		// topOfStackIndex already points to a needed array element in case of rolling back
		const auto &belowTopOfStack = predictedMovementActions[topOfStackIndex - 1];
		// For case of rolling back to savepoint we have to truncate grew stacks to it
		// The only exception is rolling back to the same top of stack.
		if( this->shouldRollback ) {
			Assert( this->topOfStackIndex <= predictedMovementActions.size() );
			predictedMovementActions.truncate( topOfStackIndex );
			botMovementStatesStack.truncate( topOfStackIndex );
			playerStatesStack.truncate( topOfStackIndex );
			pendingWeaponsStack.truncate( topOfStackIndex );

			defaultBotInputsCachesStack.PopToSize( topOfStackIndex );
			reachChainsCachesStack.PopToSize( topOfStackIndex );
			mayHitWhileRunningCachesStack.PopToSize( topOfStackIndex );
			canSafelyKeepHighSpeedCachesStack.PopToSize( topOfStackIndex );
			environmentTestResultsStack.truncate( topOfStackIndex );
		} else {
			// For case of growing stack topOfStackIndex must point at the first
			// 'illegal' yet free element at top of the stack
			Assert( predictedMovementActions.size() == topOfStackIndex );
		}

		topOfStack = new( predictedMovementActions.unsafe_grow_back() )PredictedMovementAction( belowTopOfStack );

		// Push a copy of previous player state onto top of the stack
		oldPlayerState = &playerStatesStack.back();
		playerStatesStack.push_back( *oldPlayerState );
		currPlayerState = &playerStatesStack.back();
		// Push a copy of previous movement state onto top of the stack
		botMovementStatesStack.push_back( botMovementStatesStack.back() );
		pendingWeaponsStack.push_back( belowTopOfStack.record.pendingWeapon );

		oldStepMillis = belowTopOfStack.stepMillis;
		Assert( belowTopOfStack.timestamp >= level.time );
		Assert( belowTopOfStack.stepMillis > 0 );
		totalMillisAhead = (unsigned)( belowTopOfStack.timestamp - level.time ) + belowTopOfStack.stepMillis;
	} else {
		predictedMovementActions.clear();
		botMovementStatesStack.clear();
		playerStatesStack.clear();
		pendingWeaponsStack.clear();

		defaultBotInputsCachesStack.PopToSize( 0 );
		reachChainsCachesStack.PopToSize( 0 );
		mayHitWhileRunningCachesStack.PopToSize( 0 );
		canSafelyKeepHighSpeedCachesStack.PopToSize( 0 );
		environmentTestResultsStack.clear();

		topOfStack = new( predictedMovementActions.unsafe_grow_back() )PredictedMovementAction;
		// Push the actual bot player state onto top of the stack
		oldPlayerState = &self->r.client->ps;
		playerStatesStack.push_back( *oldPlayerState );
		currPlayerState = &playerStatesStack.back();
		// Push the actual bot movement state onto top of the stack
		botMovementStatesStack.push_back( self->ai->botRef->movementState );
		pendingWeaponsStack.push_back( (signed char)oldPlayerState->stats[STAT_PENDING_WEAPON] );

		oldStepMillis = game.frametime;
		totalMillisAhead = 0;
	}
	// Check whether topOfStackIndex really points at the last element of the array
	Assert( predictedMovementActions.size() == topOfStackIndex + 1 );

	movementState = &botMovementStatesStack.back();
	// Provide a predicted movement state for Ai base class
	self->ai->botRef->entityPhysicsState = &movementState->entityPhysicsState;

	// Set the current action record
	this->record = &topOfStack->record;
	this->record->pendingWeapon = pendingWeaponsStack.back();

	Assert( reachChainsCachesStack.Size() + 1 == predictedMovementActions.size() );
	Assert( mayHitWhileRunningCachesStack.Size() + 1 == predictedMovementActions.size() );
	// Check caches size, a cache size must match the stack size after addition of a single placeholder element.
	Assert( defaultBotInputsCachesStack.Size() + 1 == predictedMovementActions.size() );
	// Then put placeholders for non-cached yet values onto top of caches stack
	defaultBotInputsCachesStack.PushDummyNonCachedValue();
	// The different method is used (there is no copy/move constructors for the template type)
	reachChainsCachesStack.UnsafeGrowForNonCachedValue();
	mayHitWhileRunningCachesStack.PushDummyNonCachedValue();
	canSafelyKeepHighSpeedCachesStack.PushDummyNonCachedValue();
	new ( environmentTestResultsStack.unsafe_grow_back() )BotEnvironmentTraceCache;

	this->shouldRollback = false;

	// Save a movement state BEFORE movement step
	topOfStack->entityPhysicsState = this->movementState->entityPhysicsState;
	topOfStack->movementStatesMask = this->movementState->GetContainedStatesMask();
}

inline BotBaseMovementAction *BotMovementPredictionContext::SuggestAnyAction() {
	if( BotBaseMovementAction *action = this->SuggestSuitableAction() ) {
		return action;
	}

	// If no action has been suggested, use a default/dummy one.
	// We have to check the combat action since it might be disabled due to planning stack overflow.
	if( self->ai->botRef->ShouldAttack() && self->ai->botRef->ShouldKeepXhairOnEnemy() ) {
		const auto &selectedEnemies = self->ai->botRef->selectedEnemies;
		if( selectedEnemies.AreValid() && selectedEnemies.ArePotentiallyHittable() ) {
			if( !self->ai->botRef->combatDodgeSemiRandomlyToTargetMovementAction.IsDisabledForPlanning() ) {
				return &self->ai->botRef->combatDodgeSemiRandomlyToTargetMovementAction;
			}
		}
	}

	return &self->ai->botRef->dummyMovementAction;
}

BotBaseMovementAction *BotMovementPredictionContext::SuggestSuitableAction() {
	Assert( !this->actionSuggestedByAction );

	const auto &entityPhysicsState = this->movementState->entityPhysicsState;

	if( entityPhysicsState.waterLevel > 1 ) {
		return &self->ai->botRef->swimMovementAction;
	}

	if( movementState->jumppadMovementState.hasTouchedJumppad ) {
		if( movementState->jumppadMovementState.hasEnteredJumppad ) {
			if( movementState->flyUntilLandingMovementState.IsActive() ) {
				if( movementState->flyUntilLandingMovementState.CheckForLanding( this ) ) {
					return &self->ai->botRef->landOnSavedAreasSetMovementAction;
				}

				return &self->ai->botRef->flyUntilLandingMovementAction;
			}
			// Fly until landing movement state has been deactivate,
			// switch to bunnying (and, implicitly, to a dummy action if it fails)
			return &self->ai->botRef->walkCarefullyMovementAction;
		}
		return &self->ai->botRef->handleTriggeredJumppadMovementAction;
	}

	if( const edict_t *groundEntity = entityPhysicsState.GroundEntity() ) {
		if( groundEntity->use == Use_Plat ) {
			// (prevent blocking if touching platform but not actually triggering it like @ wdm1 GA)
			const auto &mins = groundEntity->r.absmin;
			const auto &maxs = groundEntity->r.absmax;
			if( mins[0] <= entityPhysicsState.Origin()[0] && maxs[0] >= entityPhysicsState.Origin()[0] ) {
				if( mins[1] <= entityPhysicsState.Origin()[1] && maxs[1] >= entityPhysicsState.Origin()[1] ) {
					return &self->ai->botRef->ridePlatformMovementAction;
				}
			}
		}
	}

	if( movementState->campingSpotState.IsActive() ) {
		return &self->ai->botRef->campASpotMovementAction;
	}

	// The dummy movement action handles escaping using the movement fallback
	if( self->ai->botRef->activeMovementFallback ) {
		return &self->ai->botRef->dummyMovementAction;
	}
	return &self->ai->botRef->walkCarefullyMovementAction;
}

inline void BotMovementPredictionContext::SaveActionOnStack( BotBaseMovementAction *action ) {
	auto *topOfStack = &this->predictedMovementActions[this->topOfStackIndex];
	// This was a source of an annoying bug! movement state has been modified during a prediction step!
	// We expect that record state is a saved state BEFORE the step!
	//topOfStack->entityPhysicsState = this->movementState->entityPhysicsState;
	topOfStack->action = action;
	// Make sure the angles can always be modified for input interpolation or aiming
	topOfStack->record.botInput.hasAlreadyComputedAngles = false;
	topOfStack->timestamp = level.time + this->totalMillisAhead;
	// Check the value for sanity, huge values are a product of negative values wrapping in unsigned context
	Assert( this->predictionStepMillis < 100 );
	Assert( this->predictionStepMillis % 16 == 0 );
	topOfStack->stepMillis = this->predictionStepMillis;
	this->topOfStackIndex++;
}

inline void BotMovementPredictionContext::MarkSavepoint( BotBaseMovementAction *markedBy, unsigned frameIndex ) {
	Assert( !this->cannotApplyAction );
	Assert( !this->shouldRollback );

	Assert( frameIndex == this->topOfStackIndex || frameIndex == this->topOfStackIndex + 1 );
	this->savepointTopOfStackIndex = frameIndex;
	Debug( "%s has marked frame %d as a savepoint\n", markedBy->Name(), frameIndex );
}

inline void BotMovementPredictionContext::RollbackToSavepoint() {
	Assert( !this->isCompleted );
	Assert( this->shouldRollback );
	Assert( this->cannotApplyAction );

	constexpr const char *format = "Rolling back to savepoint frame %d from ToS frame %d\n";
	Debug( format, this->savepointTopOfStackIndex, this->topOfStackIndex );
	Assert( this->topOfStackIndex >= this->savepointTopOfStackIndex );
	this->topOfStackIndex = this->savepointTopOfStackIndex;
}

inline void BotMovementPredictionContext::SetPendingWeapon( int weapon ) {
	Assert( weapon >= WEAP_NONE && weapon < WEAP_TOTAL );
	record->pendingWeapon = ( decltype( record->pendingWeapon ) )weapon;
	pendingWeaponsStack.back() = record->pendingWeapon;
}

inline void BotMovementPredictionContext::SaveSuggestedActionForNextFrame( BotBaseMovementAction *action ) {
	//Assert(!this->actionSuggestedByAction);
	this->actionSuggestedByAction = action;
}

inline unsigned BotMovementPredictionContext::MillisAheadForFrameStart( unsigned frameIndex ) const {
	Assert( frameIndex <= topOfStackIndex );
	if( frameIndex < topOfStackIndex ) {
		return (unsigned)( predictedMovementActions[frameIndex].timestamp - level.time );
	}
	return totalMillisAhead;
}

bool BotMovementPredictionContext::NextPredictionStep() {
	SetupStackForStep();

	// Reset prediction step millis time.
	// Actions might set their custom step value (otherwise it will be set to a default one).
	this->predictionStepMillis = 0;
#ifdef CHECK_ACTION_SUGGESTION_LOOPS
	Assert( self->ai->botRef->movementActions.size() < 32 );
	uint32_t testedActionsMask = 0;
	StaticVector<BotBaseMovementAction *, 32> testedActionsList;
#endif

	// Get an initial suggested a-priori action
	BotBaseMovementAction *action;
	if( this->actionSuggestedByAction ) {
		action = this->actionSuggestedByAction;
		this->actionSuggestedByAction = nullptr;
	} else {
		action = this->SuggestSuitableAction();
	}

#ifdef CHECK_ACTION_SUGGESTION_LOOPS
	testedActionsMask |= ( 1 << action->ActionNum() );
	testedActionsList.push_back( action );
#endif

	this->sequenceStopReason = UNSPECIFIED;
	for(;; ) {
		this->cannotApplyAction = false;
		// Prevent reusing record from the switched on the current frame action
		this->record->Clear();
		if( this->activeAction != action ) {
			// If there was an active previous action, stop its application sequence.
			if( this->activeAction ) {
				unsigned stoppedAtFrameIndex = topOfStackIndex;

				// Never pass the UNSPECIFIED reason to the OnApplicationSequenceStopped() call
				if( sequenceStopReason == UNSPECIFIED ) {
					sequenceStopReason = SWITCHED;
				}

				this->activeAction->OnApplicationSequenceStopped( this, sequenceStopReason, stoppedAtFrameIndex );
			}

			sequenceStopReason = UNSPECIFIED;

			this->activeAction = action;
			// Start the action application sequence
			this->activeAction->OnApplicationSequenceStarted( this );
		}

		Debug( "About to call action->PlanPredictionStep() for %s at ToS frame %d\n", action->Name(), topOfStackIndex );
		action->PlanPredictionStep( this );
		// Check for rolling back necessity (an action application chain has lead to an illegal state)
		if( this->shouldRollback ) {
			// Stop an action application sequence manually with a failure.
			this->activeAction->OnApplicationSequenceStopped( this, BotBaseMovementAction::FAILED, (unsigned)-1 );
			// An action can be suggested again after rolling back on the next prediction step.
			// Force calling action->OnApplicationSequenceStarted() on the next prediction step.
			this->activeAction = nullptr;
			Debug( "Prediction step failed after action->PlanPredictionStep() call for %s\n", action->Name() );
			this->RollbackToSavepoint();
			// Continue planning by returning true (the stack will be restored to a savepoint index)
			return true;
		}

		if( this->cannotApplyAction ) {
			// If current action suggested an alternative, use it
			// Otherwise use the generic suggestion algorithm
			if( this->actionSuggestedByAction ) {
				Debug( "Cannot apply %s, but it has suggested %s\n", action->Name(), this->actionSuggestedByAction->Name() );
				action = this->actionSuggestedByAction;
				this->actionSuggestedByAction = nullptr;
			} else {
				auto *rejectedAction = action;
				action = this->SuggestAnyAction();
				Debug( "Cannot apply %s, using %s suggested by SuggestSuitableAction()\n", rejectedAction->Name(), action->Name() );
			}

#ifdef CHECK_ACTION_SUGGESTION_LOOPS
			if( testedActionsMask & ( 1 << action->ActionNum() ) ) {
				Debug( "List of actions suggested (and tested) this frame #%d:\n", this->topOfStackIndex );
				for( unsigned i = 0; i < testedActionsList.size(); ++i ) {
					if( Q_stricmp( testedActionsList[i]->Name(), action->Name() ) ) {
						Debug( "  %02d: %s\n", i, testedActionsList[i]->Name() );
					} else {
						Debug( ">>%02d: %s\n", i, testedActionsList[i]->Name() );
					}
				}

				AI_FailWith( __FUNCTION__, "An infinite action application loop has been detected\n" );
			}
			testedActionsMask |= ( 1 << action->ActionNum() );
			testedActionsList.push_back( action );
#endif
			// Test next action. Action switch will be handled by the logic above before calling action->PlanPredictionStep().
			continue;
		}

		// Movement prediction is completed
		if( this->isCompleted ) {
			constexpr const char *format = "Movement prediction is completed on %s, ToS frame %d, %d millis ahead\n";
			Debug( format, action->Name(), this->topOfStackIndex, this->totalMillisAhead );
			// Stop an action application sequence manually with a success.
			action->OnApplicationSequenceStopped( this, BotBaseMovementAction::SUCCEEDED, this->topOfStackIndex );
			// Save the predicted movement action
			this->SaveActionOnStack( action );
			// Stop planning by returning false
			return false;
		}

		// An action can be applied, stop testing suitable actions
		break;
	}

	Assert( action == this->activeAction );

	// If prediction step millis time has not been set, set it to a default value
	if( !this->predictionStepMillis ) {
		this->predictionStepMillis = 48;
	}

	NextMovementStep();

	action->CheckPredictionStepResults( this );
	// If results check has been passed
	if( !this->shouldRollback ) {
		// If movement planning is completed, there is no need to do a next step
		if( this->isCompleted ) {
			constexpr const char *format = "Movement prediction is completed on %s, ToS frame %d, %d millis ahead\n";
			Debug( format, action->Name(), this->topOfStackIndex, this->totalMillisAhead );
			SaveActionOnStack( action );
			// Stop action application sequence manually with a success.
			// Prevent duplicated OnApplicationSequenceStopped() call
			// (it might have been done in action->CheckPredictionStepResults() for this->activeAction)
			if( this->activeAction ) {
				this->activeAction->OnApplicationSequenceStopped( this, BotBaseMovementAction::SUCCEEDED, topOfStackIndex );
			}
			// Stop planning by returning false
			return false;
		}

		// Check whether next prediction step is possible
		if( this->CanGrowStackForNextStep() ) {
			SaveActionOnStack( action );
			// Continue planning by returning true
			return true;
		}

		// Disable this action for further planning (it has lead to stack overflow)
		action->isDisabledForPlanning = true;
		Debug( "Stack overflow on action %s, this action will be disabled for further planning\n", action->Name() );
		this->SetPendingRollback();
	}

	constexpr const char *format = "Prediction step failed for %s after calling action->CheckPredictionStepResults()\n";
	Debug( format, action->Name() );

	// An active action might have been already reset in action->CheckPredictionStepResults()
	if( this->activeAction ) {
		// Stop action application sequence with a failure manually.
		this->activeAction->OnApplicationSequenceStopped( this, BotBaseMovementAction::FAILED, (unsigned)-1 );
	}
	// An action can be suggested again after rolling back on the next prediction step.
	// Force calling action->OnApplicationSequenceStarted() on the next prediction step.
	this->activeAction = nullptr;

	this->RollbackToSavepoint();
	// Continue planning by returning true
	return true;
}

void BotMovementPredictionContext::BuildPlan() {
	for( auto *movementAction: self->ai->botRef->movementActions )
		movementAction->BeforePlanning();

	// Intercept these calls implicitly performed by PMove()
	const auto general_PMoveTouchTriggers = module_PMoveTouchTriggers;
	const auto general_PredictedEvent = module_PredictedEvent;

	module_PMoveTouchTriggers = &Intercepted_PMoveTouchTriggers;
	module_PredictedEvent = &Intercepted_PredictedEvent;

	// The entity state might be modified by Intercepted_PMoveTouchTriggers(), so we have to save it
	const Vec3 origin( self->s.origin );
	const Vec3 velocity( self->velocity );
	const Vec3 angles( self->s.angles );
	const int viewHeight = self->viewheight;
	const Vec3 mins( self->r.mins );
	const Vec3 maxs( self->r.maxs );
	const int waterLevel = self->waterlevel;
	const int waterType = self->watertype;
	edict_t *const groundEntity = self->groundentity;
	const int groundEntityLinkCount = self->groundentity_linkcount;

	auto savedPlayerState = self->r.client->ps;
	auto savedPMove = self->r.client->old_pmove;

	Assert( self->ai->botRef->entityPhysicsState == &self->ai->botRef->movementState.entityPhysicsState );
	// Save current entity physics state (it will be modified even for a single prediction step)
	const AiEntityPhysicsState currEntityPhysicsState = self->ai->botRef->movementState.entityPhysicsState;

	// Remember to reset these values before each planning session
	this->totalMillisAhead = 0;
	this->savepointTopOfStackIndex = 0;
	this->topOfStackIndex = 0;
	this->activeAction = nullptr;
	this->actionSuggestedByAction = nullptr;
	this->sequenceStopReason = UNSPECIFIED;
	this->isCompleted = false;
	this->shouldRollback = false;

#ifndef CHECK_INFINITE_NEXT_STEP_LOOPS
	for(;; ) {
		if( !NextPredictionStep() ) {
			break;
		}
	}
#else
	::nextStepIterationsCounter = 0;
	for(;; ) {
		if( !NextPredictionStep() ) {
			break;
		}
		++nextStepIterationsCounter;
		if( nextStepIterationsCounter < NEXT_STEP_INFINITE_LOOP_THRESHOLD ) {
			continue;
		}
		// An verbose output has been enabled at this stage
		if( nextStepIterationsCounter < NEXT_STEP_INFINITE_LOOP_THRESHOLD + 200 ) {
			continue;
		}
		constexpr const char *message =
			"BotMovementPredictionContext::BuildPlan(): "
			"an infinite NextPredictionStep() loop has been detected. "
			"Check the server console output of last 200 steps\n";
		G_Error( "%s", message );
	}
#endif

	// The entity might be linked for some predicted state by Intercepted_PMoveTouchTriggers()
	GClip_UnlinkEntity( self );

	// Restore entity state
	origin.CopyTo( self->s.origin );
	velocity.CopyTo( self->velocity );
	angles.CopyTo( self->s.angles );
	self->viewheight = viewHeight;
	mins.CopyTo( self->r.mins );
	maxs.CopyTo( self->r.maxs );
	self->waterlevel = waterLevel;
	self->watertype = waterType;
	self->groundentity = groundEntity;
	self->groundentity_linkcount = groundEntityLinkCount;

	self->r.client->ps = savedPlayerState;
	self->r.client->old_pmove = savedPMove;

	// Set first predicted movement state as the current bot movement state
	self->ai->botRef->movementState = botMovementStatesStack[0];
	// Even the first predicted movement state usually has modified physics state, restore it to a saved value
	self->ai->botRef->movementState.entityPhysicsState = currEntityPhysicsState;
	// Restore the current entity physics state reference in Ai subclass
	self->ai->botRef->entityPhysicsState = &self->ai->botRef->movementState.entityPhysicsState;
	// These assertions helped to find an annoying bug during development
	Assert( VectorCompare( self->s.origin, self->ai->botRef->entityPhysicsState->Origin() ) );
	Assert( VectorCompare( self->velocity, self->ai->botRef->entityPhysicsState->Velocity() ) );

	module_PMoveTouchTriggers = general_PMoveTouchTriggers;
	module_PredictedEvent = general_PredictedEvent;

	for( auto *movementAction: self->ai->botRef->movementActions )
		movementAction->AfterPlanning();
}

void BotMovementPredictionContext::NextMovementStep() {
	auto *botInput = &this->record->botInput;
	auto *entityPhysicsState = &movementState->entityPhysicsState;

	// Make sure we're modify botInput/entityPhysicsState before copying to ucmd

	// Corresponds to Bot::Think();
	self->ai->botRef->ApplyPendingTurnToLookAtPoint( botInput, this );
	// Corresponds to Bot::MovementFrame();
	this->activeAction->ExecActionRecord( this->record, botInput, this );
	// Corresponds to Bot::Think();
	self->ai->botRef->ApplyInput( botInput, this );

	// ExecActionRecord() call in SimulateMockBotFrame() might fail or complete the planning execution early.
	// Do not call PMove() in these cases
	if( this->cannotApplyAction || this->isCompleted ) {
		return;
	}

	// Prepare for PMove()
	currPlayerState->POVnum = (unsigned)ENTNUM( self );
	currPlayerState->playerNum = (unsigned)PLAYERNUM( self );

	VectorCopy( entityPhysicsState->Origin(), currPlayerState->pmove.origin );
	VectorCopy( entityPhysicsState->Velocity(), currPlayerState->pmove.velocity );
	Vec3 angles( entityPhysicsState->Angles() );
	angles.CopyTo( currPlayerState->viewangles );

	currPlayerState->pmove.gravity = (int)level.gravity;
	currPlayerState->pmove.pm_type = PM_NORMAL;

	pmove_t pm;
	// TODO: Eliminate this call?
	memset( &pm, 0, sizeof( pmove_t ) );

	pm.playerState = currPlayerState;
	botInput->CopyToUcmd( &pm.cmd );

	for( int i = 0; i < 3; i++ )
		pm.cmd.angles[i] = (short)ANGLE2SHORT( angles.Data()[i] ) - currPlayerState->pmove.delta_angles[i];

	VectorSet( currPlayerState->pmove.delta_angles, 0, 0, 0 );

	// Check for unsigned value wrapping
	Assert( this->predictionStepMillis && this->predictionStepMillis < 100 );
	Assert( this->predictionStepMillis % 16 == 0 );
	pm.cmd.msec = (uint8_t)this->predictionStepMillis;
	pm.cmd.serverTimeStamp = game.serverTime + this->totalMillisAhead;

	this->frameEvents.Clear();

	// The naive solution of supplying a dummy trace function
	// (that yields a zeroed output with fraction = 1) does not work.
	// An actual logic tied to this flag has to be added in Pmove() for each module_Trace() call.
	pm.skipCollision = EnvironmentTraceCache().CanSkipPMoveCollision( this );

	// We currently test collisions only against a solid world on each movement step and the corresponding PMove() call.
	// Touching trigger entities is handled by Intercepted_PMoveTouchTriggers(), also we use AAS sampling for it.
	// Actions that involve touching trigger entities currently are never predicted ahead.
	// If an action really needs to test against entities, a corresponding prediction step flag
	// should be added and this interception of the module_Trace() should be skipped if the flag is set.

	// Save the G_GS_Trace() pointer
	auto oldModuleTrace = module_Trace;
	module_Trace = Intercepted_Trace;

	// Do not test entities contents for same reasons
	// Save the G_PointContents4D() pointer
	auto oldModulePointContents = module_PointContents;
	module_PointContents = Intercepted_PointContents;

	Pmove( &pm );

	// Restore the G_GS_Trace() pointer
	module_Trace = oldModuleTrace;
	// Restore the G_PointContents4D() pointer
	module_PointContents = oldModulePointContents;

	// Update the entity physics state that is going to be used in the next prediction frame
	entityPhysicsState->UpdateFromPMove( &pm );
	// Update the entire movement state that is going to be used in the next prediction frame
	this->movementState->Frame( this->predictionStepMillis );
	this->movementState->TryDeactivateContainedStates( self, this );
}

void BotMovementPredictionContext::Debug( const char *format, ... ) const {
#if ( defined( ENABLE_MOVEMENT_DEBUG_OUTPUT ) || defined( CHECK_INFINITE_NEXT_STEP_LOOPS ) )
// Check if there is an already detected error in this case and perform output only it the condition passes
#if !defined( ENABLE_MOVEMENT_DEBUG_OUTPUT )
	if( ::nextStepIterationsCounter < NEXT_STEP_INFINITE_LOOP_THRESHOLD ) {
		return;
	}
#endif

	char tag[64];
	Q_snprintfz( tag, 64, "^6MovementPredictionContext(%s)", Nick( self ) );

	va_list va;
	va_start( va, format );
	AI_Debugv( tag, format, va );
	va_end( va );
#endif
}

inline void BotMovementPredictionContext::Assert( bool condition, const char *message ) const {
#ifdef ENABLE_MOVEMENT_ASSERTIONS
	if( !condition ) {
		if( message ) {
			AI_FailWith( "BotMovementPredictionContext::Assert()", "%s\n", message );
		} else {
			AI_FailWith( "BotMovementPredictionContext::Assert()", "An assertion has failed\n" );
		}
	}
#endif
}

constexpr float Z_NO_BEND_SCALE = 0.5f;

void BotMovementPredictionContext::SetDefaultBotInput() {
	// Check for cached value first
	if( const BotInput *cachedDefaultBotInput = defaultBotInputsCachesStack.GetCached() ) {
		this->record->botInput = *cachedDefaultBotInput;
		return;
	}

	const auto &entityPhysicsState = movementState->entityPhysicsState;
	auto *botInput = &this->record->botInput;

	botInput->ClearMovementDirections();
	botInput->SetSpecialButton( false );
	botInput->SetWalkButton( false );
	botInput->isUcmdSet = true;

	// If angles are already set (e.g. by pending look at point), do not try to set angles for following AAS reach. chain
	if( botInput->hasAlreadyComputedAngles ) {
		// Save cached value and return
		defaultBotInputsCachesStack.SetCachedValue( *botInput );
		return;
	}

	botInput->isLookDirSet = true;
	botInput->canOverrideLookVec = false;
	botInput->canOverridePitch = true;
	botInput->canOverrideUcmd = false;

	const int navTargetAasAreaNum = NavTargetAasAreaNum();
	const int currAasAreaNum = CurrAasAreaNum();
	// If a current area and a nav target area are defined
	if( currAasAreaNum && navTargetAasAreaNum ) {
		// If bot is not in nav target area
		if( currAasAreaNum != navTargetAasAreaNum ) {
			const int nextReachNum = this->NextReachNum();
			if( nextReachNum ) {
				const auto &nextReach = AiAasWorld::Instance()->Reachabilities()[nextReachNum];
				if( DistanceSquared( entityPhysicsState.Origin(), nextReach.start ) < 12 * 12 ) {
					Vec3 intendedLookVec( nextReach.end );
					intendedLookVec -= nextReach.start;
					intendedLookVec.NormalizeFast();
					intendedLookVec *= 16.0f;
					intendedLookVec += nextReach.start;
					intendedLookVec -= entityPhysicsState.Origin();
					if( entityPhysicsState.GroundEntity() ) {
						intendedLookVec.Z() = 0;
					}
					botInput->SetIntendedLookDir( intendedLookVec );
				} else {
					Vec3 intendedLookVec3( nextReach.start );
					intendedLookVec3 -= entityPhysicsState.Origin();
					botInput->SetIntendedLookDir( intendedLookVec3 );
				}
				// Save a cached value and return
				defaultBotInputsCachesStack.SetCachedValue( *botInput );
				return;
			}
		} else {
			// Look at the nav target
			Vec3 intendedLookVec( NavTargetOrigin() );
			intendedLookVec -= entityPhysicsState.Origin();
			botInput->SetIntendedLookDir( intendedLookVec );
			// Save a cached value and return
			defaultBotInputsCachesStack.SetCachedValue( *botInput );
			return;
		}
	}

	// A valid reachability chain does not exist, use dummy relaxed movement
	if( entityPhysicsState.Speed() > 1 ) {
		// Follow the existing velocity direction
		botInput->SetIntendedLookDir( entityPhysicsState.Velocity() );
	} else {
		// The existing velocity is too low to extract a direction from it, keep looking in the same direction
		botInput->SetAlreadyComputedAngles( entityPhysicsState.Angles() );
	}

	// Allow overriding look angles for aiming (since they are set to dummy ones)
	botInput->canOverrideLookVec = true;
	// Save a cached value and return
	defaultBotInputsCachesStack.SetCachedValue( *botInput );
}

void BotBaseMovementAction::RegisterSelf() {
	this->self = bot->self;
	this->actionNum = bot->movementActions.size();
	bot->movementActions.push_back( this );
}

inline BotBaseMovementAction &BotBaseMovementAction::DummyAction() {
	// We have to check the combat action since it might be disabled due to planning stack overflow.
	if( self->ai->botRef->ShouldKeepXhairOnEnemy() && self->ai->botRef->GetSelectedEnemies().AreValid() ) {
		if( !self->ai->botRef->combatDodgeSemiRandomlyToTargetMovementAction.IsDisabledForPlanning() ) {
			return self->ai->botRef->combatDodgeSemiRandomlyToTargetMovementAction;
		}
	}

	return self->ai->botRef->dummyMovementAction;
}

inline BotBaseMovementAction &BotBaseMovementAction::DefaultWalkAction() {
	return self->ai->botRef->walkCarefullyMovementAction;
}
inline BotBaseMovementAction &BotBaseMovementAction::DefaultBunnyAction() {
	return self->ai->botRef->bunnyToBestFloorClusterPointMovementAction;
}
inline BotBaseMovementAction &BotBaseMovementAction::FallbackBunnyAction() {
	return self->ai->botRef->walkOrSlideInterpolatingReachChainMovementAction;
}
inline BotFlyUntilLandingMovementAction &BotBaseMovementAction::FlyUntilLandingAction() {
	return self->ai->botRef->flyUntilLandingMovementAction;
}
inline BotLandOnSavedAreasMovementAction &BotBaseMovementAction::LandOnSavedAreasAction() {
	return self->ai->botRef->landOnSavedAreasSetMovementAction;
}

void BotBaseMovementAction::Debug( const char *format, ... ) const {
#if ( defined( ENABLE_MOVEMENT_DEBUG_OUTPUT ) || defined( CHECK_INFINITE_NEXT_STEP_LOOPS ) )
// Check if there is an already detected error in this case and perform output only it the condition passes
#if !defined( ENABLE_MOVEMENT_DEBUG_OUTPUT )
	if( ::nextStepIterationsCounter < NEXT_STEP_INFINITE_LOOP_THRESHOLD ) {
		return;
	}
#endif

	char tag[128];
	Q_snprintfz( tag, 128, "^5%s(%s)", this->Name(), Nick( self ) );

	va_list va;
	va_start( va, format );
	AI_Debugv( tag, format, va );
	va_end( va );
#endif
}

inline void BotBaseMovementAction::Assert( bool condition, const char *message ) const {
#ifdef ENABLE_MOVEMENT_ASSERTIONS
	if( !condition ) {
		if( message ) {
			AI_FailWith("BotBaseMovementAction::Assert()", "An assertion has failed: %s\n", message );
		} else {
			AI_FailWith("BotBaseMovementAction::Assert()", "An assertion has failed\n");
		}
	}
#endif
}

void BotBaseMovementAction::ExecActionRecord( const BotMovementActionRecord *record,
											  BotInput *inputWillBeUsed,
											  BotMovementPredictionContext *context ) {
	Assert( inputWillBeUsed );
	// TODO: Discover why we still need to do that for pending look at points
	// while the pending look at points seemingly gets applied in SimulateMockBotFrame()
	if( inputWillBeUsed->hasAlreadyComputedAngles ) {
		Vec3 angles( inputWillBeUsed->AlreadyComputedAngles() );
		*inputWillBeUsed = record->botInput;
		inputWillBeUsed->SetAlreadyComputedAngles( angles );
	} else {
		*inputWillBeUsed = record->botInput;
	}

	if( context ) {
		if( record->hasModifiedVelocity ) {
			context->movementState->entityPhysicsState.SetVelocity( record->ModifiedVelocity() );
		}

		// Pending weapon must have been set in PlanPredictionStep()
		// (in planning context it is defined by record->pendingWeapon, pendingWeaponsStack.back()).
		if( record->pendingWeapon >= WEAP_NONE ) {
			//Assert(record->pendingWeapon == context->PendingWeapon());
		}
		return;
	}

	if( record->hasModifiedVelocity ) {
		record->ModifiedVelocity().CopyTo( self->velocity );
	}

	if( record->pendingWeapon != -1 ) {
		self->r.client->ps.stats[STAT_PENDING_WEAPON] = record->pendingWeapon;
	}
}

void BotBaseMovementAction::CheckPredictionStepResults( BotMovementPredictionContext *context ) {
	// These flags might be set by ExecActionRecord(). Skip checks in this case.
	if( context->cannotApplyAction || context->isCompleted ) {
		return;
	}

	const auto &newEntityPhysicsState = context->movementState->entityPhysicsState;
	const auto &oldEntityPhysicsState = context->PhysicsStateBeforeStep();

	// This is a default basic test that suits many relatively simple actions
	// Forbid movement from regular contents to "bad" contents
	// (if old contents are "bad" too, a movement step is considered legal)
	// Note: we do not check any points between these two ones,
	// and this can lead to missing "bad contents" for large prediction time step

	constexpr auto badContents = CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_DONOTENTER;
	if( newEntityPhysicsState.waterType & badContents ) {
		if( !( oldEntityPhysicsState.waterType & badContents ) ) {
			if( badContents & CONTENTS_LAVA ) {
				Debug( "A prediction step has lead to entering CONTENTS_LAVA point\n" );
			} else if( badContents & CONTENTS_SLIME ) {
				Debug( "A prediction step has lead to entering CONTENTS_SLIME point\n" );
			} else if( badContents & CONTENTS_DONOTENTER ) {
				Debug( "A prediction step has lead to entering CONTENTS_DONOTENTER point\n" );
			}

			context->SetPendingRollback();
			return;
		}
	}

	if( stopPredictionOnEnteringWater && newEntityPhysicsState.waterLevel > 1 ) {
		Assert( this != &self->ai->botRef->swimMovementAction );
		Debug( "A prediction step has lead to entering water, should stop planning\n" );
		context->isCompleted = true;
		return;
	}

	// Check AAS areas in the same way
	int oldAasAreaNum = oldEntityPhysicsState.CurrAasAreaNum();
	int newAasAreaNum = newEntityPhysicsState.CurrAasAreaNum();
	if( newAasAreaNum != oldAasAreaNum ) {
		const auto *aasAreaSettings = AiAasWorld::Instance()->AreaSettings();
		const auto &currAreaSettings = aasAreaSettings[newAasAreaNum];
		const auto &prevAreaSettings = aasAreaSettings[oldAasAreaNum];

		if( currAreaSettings.areaflags & AREA_DISABLED ) {
			if( !( prevAreaSettings.areaflags & AREA_DISABLED ) ) {
				Debug( "A prediction step has lead to entering an AREA_DISABLED AAS area\n" );
				context->SetPendingRollback();
				return;
			}
		}

		if( currAreaSettings.contents & AREACONTENTS_DONOTENTER ) {
			if( !( prevAreaSettings.contents & AREACONTENTS_DONOTENTER ) ) {
				Debug( "A prediction step has lead to entering an AREACONTENTS_DONOTENTER AAS area\n" );
				context->SetPendingRollback();
				return;
			}
		}
	}

	if( this->stopPredictionOnTouchingJumppad && context->frameEvents.hasTouchedJumppad ) {
		Debug( "A prediction step has lead to touching a jumppad, should stop planning\n" );
		context->isCompleted = true;
		return;
	}
	if( this->stopPredictionOnTouchingTeleporter && context->frameEvents.hasTouchedTeleporter ) {
		Debug( "A prediction step has lead to touching a teleporter, should stop planning\n" );
		context->isCompleted = true;
		return;
	}
	if( this->stopPredictionOnTouchingPlatform && context->frameEvents.hasTouchedPlatform ) {
		Debug( "A prediction step has lead to touching a platform, should stop planning\n" );
		context->isCompleted = true;
		return;
	}

	if( this->stopPredictionOnTouchingNavEntity ) {
		const edict_t *gameEdicts = game.edicts;
		const uint16_t *ents = context->frameEvents.otherTouchedTriggerEnts;
		for( int i = 0, end = context->frameEvents.numOtherTouchedTriggers; i < end; ++i ) {
			const edict_t *ent = gameEdicts + ents[i];
			if( self->ai->botRef->IsNavTargetBasedOnEntity( ent ) ) {
				const char *entName = ent->classname ? ent->classname : "???";
				Debug( "A prediction step has lead to touching a nav entity %s, should stop planning\n", entName );
				context->isCompleted = true;
				return;
			}
		}
	}

	if( self->ai->botRef->ShouldRushHeadless() ) {
		return;
	}

	if( this->failPredictionOnEnteringDangerImpactZone ) {
		if( const auto *danger = self->ai->botRef->perceptionManager.PrimaryDanger() ) {
			if( danger->SupportsImpactTests() ) {
				// Check the new origin condition first to cut off early
				if( danger->HasImpactOnPoint( newEntityPhysicsState.Origin() ) ) {
					if( !danger->HasImpactOnPoint( oldEntityPhysicsState.Origin() ) ) {
						Debug( "A prediction step has lead to entering a danger influence zone, should rollback\n" );
						context->SetPendingRollback();
						return;
					}
				}
			}
		}
	}

	// If misc tactics flag "rush headless" is set, areas occupied by enemies are never excluded from routing
	const auto *routeCache = self->ai->botRef->routeCache;
	// Check the new origin condition first to cut off early
	if( routeCache->AreaTemporarilyDisabled( newAasAreaNum ) ) {
		if( !routeCache->AreaTemporarilyDisabled( oldAasAreaNum ) ) {
			Debug( "A prediction step has lead to entering a temporarily excluded from routing, should rollback\n" );
			return;
		}
	}
}

void BotBaseMovementAction::BeforePlanning() {
	isDisabledForPlanning = false;
	sequenceStartFrameIndex = std::numeric_limits<unsigned>::max();
	sequenceEndFrameIndex = std::numeric_limits<unsigned>::max();
}

void BotBaseMovementAction::OnApplicationSequenceStarted( BotMovementPredictionContext *context ) {
	Debug( "OnApplicationSequenceStarted(context): context->topOfStackIndex=%d\n", context->topOfStackIndex );

	constexpr auto invalidValue = std::numeric_limits<unsigned>::max();
	Assert( sequenceStartFrameIndex == invalidValue );
	sequenceEndFrameIndex = invalidValue;
	sequenceStartFrameIndex = context->topOfStackIndex;
	originAtSequenceStart.Set( context->movementState->entityPhysicsState.Origin() );
}

void BotBaseMovementAction::OnApplicationSequenceStopped( BotMovementPredictionContext *context,
														  SequenceStopReason reason,
														  unsigned stoppedAtFrameIndex ) {
	constexpr auto invalidValue = std::numeric_limits<unsigned>::max();
	Assert( sequenceStartFrameIndex != invalidValue );
	Assert( sequenceEndFrameIndex == invalidValue );
	Assert( sequenceStartFrameIndex <= stoppedAtFrameIndex );
	sequenceStartFrameIndex = invalidValue;
	sequenceEndFrameIndex = stoppedAtFrameIndex;

	const char *format = "OnApplicationSequenceStopped(context, %s, %d): context->topOfStackIndex=%d\n";
	switch( reason ) {
		case UNSPECIFIED:
			// Should not be reached
			Assert( false );
			break;
		case SUCCEEDED:
			Debug( format, "succeeded", stoppedAtFrameIndex, context->topOfStackIndex );
			context->MarkSavepoint( this, stoppedAtFrameIndex + 1 );
			break;
		case SWITCHED:
			Debug( format, "switched", stoppedAtFrameIndex, context->topOfStackIndex );
			context->MarkSavepoint( this, stoppedAtFrameIndex );
			break;
		case DISABLED:
			Debug( format, "disabled", stoppedAtFrameIndex, context->topOfStackIndex );
			context->MarkSavepoint( this, stoppedAtFrameIndex );
			break;
		case FAILED:
			Debug( format, "failed", stoppedAtFrameIndex, context->topOfStackIndex );
			break;
	}
}

inline unsigned BotBaseMovementAction::SequenceDuration( const BotMovementPredictionContext *context ) const {
	unsigned millisAheadAtSequenceStart = context->MillisAheadForFrameStart( sequenceStartFrameIndex );
	// TODO: Ensure that the method gets called only after prediction step in some way
	// (We need a valid and actual prediction step millis)
	Assert( context->predictionStepMillis );
	Assert( context->predictionStepMillis % 16 == 0 );
	Assert( context->totalMillisAhead + context->predictionStepMillis > millisAheadAtSequenceStart );
	return context->totalMillisAhead + context->predictionStepMillis - millisAheadAtSequenceStart;
}

// Height threshold should be set according to used time step
// (we might miss crouch sliding activation if its low and the time step is large)
inline bool ShouldPrepareForCrouchSliding( BotMovementPredictionContext *context, float heightThreshold = 12.0f ) {
	if( !(context->currPlayerState->pmove.stats[PM_STAT_FEATURES ] & PMFEAT_CROUCHSLIDING ) ) {
		return false;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( entityPhysicsState.GroundEntity() ) {
		return false;
	}

	if( entityPhysicsState.Velocity()[2] > 0 ) {
		return false;
	}

	if( entityPhysicsState.HeightOverGround() > heightThreshold ) {
		return false;
	}

	if( entityPhysicsState.Speed() < context->GetRunSpeed() ) {
		return false;
	}

	return true;
}

inline float Distance2DSquared( const vec3_t a, const vec3_t b ) {
	float dx = a[0] - b[0];
	float dy = a[1] - b[1];
	return dx * dx + dy * dy;
}

#ifndef SQUARE
#define SQUARE( x ) ( ( x ) * ( x ) )
#endif

bool BotFallDownMovementFallback::TryDeactivate( BotMovementPredictionContext *context ) {
	assert( status == PENDING );

	if( level.time - activatedAt > timeout ) {
		status = INVALID;
		return true;
	}

	const AiEntityPhysicsState *entityPhysicsState;
	if( context ) {
		entityPhysicsState = &context->movementState->entityPhysicsState;
	} else {
		entityPhysicsState = self->ai->botRef->EntityPhysicsState();
	}

	// Wait for touching any ground
	if( !entityPhysicsState->GroundEntity() ) {
		return false;
	}

	if( DistanceSquared( entityPhysicsState->Origin(), targetOrigin ) > reachRadius * reachRadius ) {
		return false;
	}

	return entityPhysicsState->Origin()[2] < targetOrigin[2];
}

void BotFallDownMovementFallback::SetupMovement( BotMovementPredictionContext *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	auto *botInput = &context->record->botInput;

	// Start Z is rather important, don't use entity origin as-is
	Vec3 intendedLookDir( entityPhysicsState.Origin() );
	intendedLookDir.Z() += self->viewheight;
	intendedLookDir -= targetOrigin;
	intendedLookDir *= -1.0f;
	intendedLookDir.Normalize();

	botInput->SetIntendedLookDir( intendedLookDir, true );

	const float viewDot = intendedLookDir.Dot( entityPhysicsState.ForwardDir() );
	if( viewDot < 0 ) {
		botInput->SetTurnSpeedMultiplier( 10.0f );
	} else if( viewDot < 0.9f ) {
		if( viewDot < 0.7f ) {
			botInput->SetWalkButton( true );
			botInput->SetTurnSpeedMultiplier( 5.0f );
		} else {
			// Apply air-control being in air, so turn rather slowly.
			// We might consider using CheatingCorrectVelocity()
			// but it currently produces weird results on vertical trajectories.
			if( !entityPhysicsState.GroundEntity() ) {
				botInput->SetForwardMovement( 1 );
			} else {
				botInput->SetTurnSpeedMultiplier( 3.0f );
			}
		}
	} else {
		botInput->SetForwardMovement( 1 );
	}
}

bool BotJumpOverBarrierMovementFallback::TryDeactivate( BotMovementPredictionContext *context ) {
	assert( status == PENDING );

	if( level.time - activatedAt > 750 ) {
		status = INVALID;
		return true;
	}

	// TODO: Eliminate this boilerplate
	const AiEntityPhysicsState *entityPhysicsState;
	if( context ) {
		entityPhysicsState = &context->movementState->entityPhysicsState;
	} else {
		entityPhysicsState = self->ai->botRef->EntityPhysicsState();
	}

	// Wait for touching any ground
	if( !entityPhysicsState->GroundEntity() ) {
		return false;
	}

	return entityPhysicsState->Origin()[2] >= top[2];
}

void BotJumpOverBarrierMovementFallback::SetupMovement( BotMovementPredictionContext *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	auto *botInput = &context->record->botInput;

	// View Z really matters a lot in this case, don't use the entity origin as-is.
	// Don't forget to negate the vector after target subtraction.
	Vec3 intendedLookDir( entityPhysicsState.Origin() );
	intendedLookDir.Z() += self->viewheight;

	if( !hasReachedStart ) {
		float squareDistance = Distance2DSquared( start, entityPhysicsState.Origin() );
		if( squareDistance > SQUARE( 16.0f ) ) {
			intendedLookDir -= start;
			intendedLookDir *= -1.0f / intendedLookDir.LengthFast();

			botInput->SetIntendedLookDir( intendedLookDir, true );

			const float viewDot = intendedLookDir.Dot( entityPhysicsState.ForwardDir() );
			if( viewDot < 0.9f ) {
				botInput->SetTurnSpeedMultiplier( viewDot < 0 ? 10.0f : 5.0f );
				return;
			}

			botInput->SetForwardMovement( 1 );

			// Try dashing in case when the distance is significant (this should be a rare case)
			if( self->ai->botRef->ShouldMoveCarefully() || self->ai->botRef->ShouldBeSilent() ) {
				return;
			}
			// Note that the distance threshold is lower than usual for fallbacks,
			// since we're going to be stopped by a barrier anyway and shouldn't miss it
			if( !entityPhysicsState.GroundEntity() || squareDistance < SQUARE( 48.0f ) ) {
				return;
			}

			const auto *pmStats = context->currPlayerState->pmove.stats;
			if( ( pmStats[PM_STAT_FEATURES] & PMFEAT_DASH ) && !pmStats[PM_STAT_DASHTIME] ) {
				botInput->SetSpecialButton( true );
			}

			return;
		}
		hasReachedStart = true;
	}

	intendedLookDir -= top;
	intendedLookDir *= -1.0f / intendedLookDir.LengthFast();

	botInput->SetIntendedLookDir( intendedLookDir, true );

	const float viewDot = intendedLookDir.Dot( entityPhysicsState.ForwardDir() );
	if( viewDot < 0.9f ) {
		botInput->SetTurnSpeedMultiplier( viewDot < 0 ? 10.0f : 5.0f );
		return;
	}

	botInput->SetForwardMovement( 1 );
	botInput->SetUpMovement( 1 );

	if( !allowWalljumping ) {
		return;
	}

	// Try WJ having reached the peak point
	const auto *pmStats = context->currPlayerState->pmove.stats;
	if( !( pmStats[PM_STAT_FEATURES] & PMFEAT_WALLJUMP ) ) {
		return;
	}

	if( pmStats[PM_STAT_WJTIME] || pmStats[PM_STAT_STUN] ) {
		return;
	}

	if( !entityPhysicsState.GroundEntity() && fabsf( entityPhysicsState.Velocity()[2] ) < 50 ) {
		botInput->SetSpecialButton( true );
	}
}

static inline bool ShouldCrouchSlideNow( BotMovementPredictionContext *context ) {
	if( !( context->currPlayerState->pmove.stats[PM_STAT_FEATURES] & PMFEAT_CROUCHSLIDING ) ) {
		return false;
	}

	if( context->currPlayerState->pmove.pm_flags & PMF_CROUCH_SLIDING ) {
		if( context->currPlayerState->pmove.stats[PM_STAT_CROUCHSLIDETIME] > PM_CROUCHSLIDE_FADE ) {
			return true;
		}
	}

	if( context->movementState->entityPhysicsState.Speed2D() > context->GetRunSpeed() * 1.2f ) {
		return true;
	}

	return false;
}

void BotDummyMovementAction::PlanPredictionStep( BotMovementPredictionContext *context ) {
	bool handledSpecialMovement = false;
	if( auto *fallback = self->ai->botRef->activeMovementFallback ) {
		fallback->SetupMovement( context );
		handledSpecialMovement = true;
	} else if( context->IsInNavTargetArea() ) {
		SetupNavTargetAreaMovement( context );
		handledSpecialMovement = true;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( entityPhysicsState.GroundEntity() && entityPhysicsState.GetGroundNormalZ() < 0.999f ) {
		if( int groundedAreaNum = context->CurrGroundedAasAreaNum() ) {
			if( AiAasWorld::Instance()->AreaSettings()[groundedAreaNum].areaflags & AREA_INCLINED_FLOOR ) {
				if( TrySetupInclinedFloorMovement( context, groundedAreaNum ) ) {
					handledSpecialMovement = true;
				}
			}
		}
	}

	auto *botInput = &context->record->botInput;
	if( handledSpecialMovement ) {
		botInput->SetAllowedRotationMask( BotInputRotation::NONE );
	} else {
		if( !entityPhysicsState.GroundEntity() ) {
			// Fallback path movement is the last hope action, wait for landing
			SetupLostNavTargetMovement( context );
		} else if( auto *fallback = TryFindMovementFallback( context ) ) {
			self->ai->botRef->activeMovementFallback = fallback;
			fallback->SetupMovement( context );
			handledSpecialMovement = true;
			botInput->SetAllowedRotationMask( BotInputRotation::NONE );
		} else {
			// This often leads to bot blocking and suicide. TODO: Invesigate what else can be done.
			botInput->Clear();
			if( self->ai->botRef->keptInFovPoint.IsActive() ) {
				Vec3 intendedLookVec( self->ai->botRef->keptInFovPoint.Origin() );
				intendedLookVec -= entityPhysicsState.Origin();
				botInput->SetIntendedLookDir( intendedLookVec, false );
			}
		}
	}

	botInput->isUcmdSet = true;
	botInput->canOverrideUcmd = !handledSpecialMovement;
	botInput->canOverrideLookVec = !handledSpecialMovement;
	Debug( "Planning is complete: the action should never be predicted ahead\n" );
	context->isCompleted = true;
}

void BotDummyMovementAction::SetupNavTargetAreaMovement( BotMovementPredictionContext *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	auto *botInput = &context->record->botInput;

	Vec3 intendedLookDir( context->NavTargetOrigin() );
	intendedLookDir -= entityPhysicsState.Origin();
	intendedLookDir.NormalizeFast();
	botInput->SetIntendedLookDir( intendedLookDir, true );

	if( entityPhysicsState.GroundEntity() ) {
		botInput->SetForwardMovement( true );
		if( self->ai->botRef->ShouldMoveCarefully() ) {
			botInput->SetWalkButton( true );
		} else if( context->IsCloseToNavTarget() ) {
			botInput->SetWalkButton( true );
		}
	} else {
		// Try applying QW-like aircontrol
		float dotForward = intendedLookDir.Dot( entityPhysicsState.ForwardDir() );
		if( dotForward > 0 ) {
			float dotRight = intendedLookDir.Dot( entityPhysicsState.RightDir() );
			if( dotRight > 0.3f ) {
				botInput->SetRightMovement( +1 );
			} else if( dotRight < -0.3f ) {
				botInput->SetRightMovement( -1 );
			}
		}
	}
}

void BotDummyMovementAction::SetupLostNavTargetMovement( BotMovementPredictionContext *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	auto *botInput = &context->record->botInput;

	// Looks like the nav target is lost due to being high above the ground

	// If there is a substantial 2D speed, looks like the bot is jumping over a gap
	if( entityPhysicsState.Speed2D() > context->GetRunSpeed() - 50 ) {
		// Keep looking in the velocity direction
		botInput->SetIntendedLookDir( entityPhysicsState.Velocity(), false );
		return;
	}

	if( !entityPhysicsState.IsHighAboveGround() ) {
		// Keep looking in the velocity direction
		if( entityPhysicsState.SquareSpeed() > 1 ) {
			botInput->SetIntendedLookDir( entityPhysicsState.Velocity(), false );
			return;
		}
	}

	// Keep looking in the current direction
	botInput->SetIntendedLookDir( entityPhysicsState.ForwardDir(), true );
}

BotMovementFallback *BotDummyMovementAction::TryFindWalkableTriggerFallback( BotMovementPredictionContext *context ) {
	if( const edict_t *trigger = FindClosestToTargetTrigger( context ) ) {
		auto *fallback = &self->ai->botRef->useWalkableTriggerMovementFallback;
		fallback->Activate( trigger );
		return fallback;
	}

	return nullptr;
}

template<typename T1, typename T2>
static inline float PerpDot2D( const T1 &v1, const T2 &v2 ) {
	return v1[0] * v2[1] - v1[1] * v2[0];
}

static bool FindSegments2DIntersectionPoint( const vec3_t start1, const vec3_t end1,
											 const vec3_t start2, const vec3_t end2, vec3_t result ) {
	// Copyright 2001 softSurfer, 2012 Dan Sunday
	// This code may be freely used and modified for any purpose
	// providing that this copyright notice is included with it.
	// SoftSurfer makes no warranty for this code, and cannot be held
	// liable for any real or imagined damage resulting from its use.
	// Users of this code must verify correctness for their application.

	// Compute first segment direction vector
	vec3_t u = { end1[0] - start1[0], end1[1] - start1[1], 0 };
	// Compute second segment direction vector
	vec3_t v = { end2[0] - start2[0], end2[1] - start2[1], 0 };
	// Compute a vector from second start point to the first one
	vec3_t w = { start1[0] - start2[0], start1[1] - start2[1], 0 };

	// |u| * |v| * sin( u ^ v ), if parallel than zero, if some of inputs has zero-length than zero
	float d = PerpDot2D( u, v );

	// We treat parallel or degenerate cases as a failure
	if( fabsf( d ) < 0.0001f ) {
		return false;
	}

	// Group computations together aside from branches
	float t1 = PerpDot2D( v, w ) / d;
	float t2 = PerpDot2D( u, w ) / d;

	// If the first segment direction vector is "behind" or "ahead" of start1-to-start2 vector
	if (t1 < 0 || t1 > 1)
		return false;

	// If the second segment direction vector is "behind" or "ahead" of start1-to-start2 vector
	if (t2 < 0 || t2 > 1)
		return false;

	VectorMA( start1, t1, u, result );
	return true;
}

bool IsAreaWalkableInFloorCluster( int startAreaNum, int targetAreaNum ) {
	// Consider matching areas walkable (even if the area does not belong to some cluster).
	if( startAreaNum == targetAreaNum ) {
		return true;
	}

	const auto *aasWorld = AiAasWorld::Instance();
	const auto *areaFloorClusterNums = aasWorld->AreaFloorClusterNums();
	int startFloorClusterNum = areaFloorClusterNums[startAreaNum];
	if( !startFloorClusterNum ) {
		return false;
	}

	const auto *aasAreas = aasWorld->Areas();
	Vec3 testedSegmentEnd( aasAreas[targetAreaNum].center[0], aasAreas[targetAreaNum].center[1], 0.0f );
	Vec3 testedSegmentStart( aasAreas[startAreaNum].center[0], aasAreas[startAreaNum].center[1], 0.0f );

	Vec3 rayDir( testedSegmentEnd );
	rayDir -= testedSegmentStart;
	rayDir.NormalizeFast();

	const auto *aasFaceIndex = aasWorld->FaceIndex();
	const auto *aasFaces = aasWorld->Faces();
	const auto *aasPlanes = aasWorld->Planes();
	const auto *aasVertices = aasWorld->Vertexes();
	const auto *face2DProjVertexNums = aasWorld->Face2DProjVertexNums();

	int currAreaNum = startAreaNum;
	while( currAreaNum != targetAreaNum ) {
		const auto &currArea = aasAreas[currAreaNum];
		// For each area face
		int faceIndexNum = currArea.firstface;
		const int endFaceIndexNum = faceIndexNum + currArea.numfaces;
		for(; faceIndexNum != endFaceIndexNum; ++faceIndexNum) {
			int signedFaceNum = aasFaceIndex[faceIndexNum];
			const auto &face = aasFaces[abs( signedFaceNum )];
			const auto &plane = aasPlanes[face.planenum];
			// Reject non-2D faces
			if( fabsf( plane.normal[2] ) > 0.1f ) {
				continue;
			}
			// We assume we're inside the area.
			// Do not try intersection tests for already "passed" by the ray faces
			int areaBehindFace;
			if( signedFaceNum < 0 ) {
				if( rayDir.Dot( plane.normal ) < 0 ) {
					continue;
				}
				areaBehindFace = face.frontarea;
			} else {
				if( rayDir.Dot( plane.normal ) > 0 ) {
					continue;
				}
				areaBehindFace = face.backarea;
			}

			// If an area behind the face is in another or zero floor cluster
			if( areaFloorClusterNums[areaBehindFace] != startFloorClusterNum ) {
				continue;
			}

			const auto *projVertexNums = face2DProjVertexNums + 2 * abs( signedFaceNum );
			const float *edgePoint1 = aasVertices[projVertexNums[0]];
			const float *edgePoint2 = aasVertices[projVertexNums[1]];
			vec3_t intersectionPoint;
			if( !FindSegments2DIntersectionPoint( testedSegmentStart.Data(), testedSegmentEnd.Data(),
												  edgePoint1, edgePoint2, intersectionPoint ) ) {
				continue;
			}

			testedSegmentStart.Set( intersectionPoint );
			currAreaNum = areaBehindFace;
			goto nextArea;
		}

		// There are no feasible areas behind feasible faces of the current area
		return false;
	nextArea:;
	}

	return true;
}

bool BotSameFloorClusterAreasCache::AreaPassesCollisionTest( BotMovementPredictionContext *context, int areaNum ) const {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	Vec3 start( entityPhysicsState.Origin() );
	if( entityPhysicsState.GroundEntity() ) {
		start.Z() += 1.0f;
	}

	vec3_t mins, maxs;
	TacticalSpotsRegistry::GetSpotsWalkabilityTraceBounds( mins, maxs );
	return AreaPassesCollisionTest( start, areaNum, mins, maxs );
}

bool BotSameFloorClusterAreasCache::AreaPassesCollisionTest( const Vec3 &start, int areaNum,
															 const vec3_t mins, const vec3_t maxs ) const {
	const auto &area = aasWorld->Areas()[areaNum];
	Vec3 areaPoint( area.center );
	areaPoint.Z() = area.mins[2] + 1.0f + ( -playerbox_stand_mins[2] );

	// We deliberately have to check against entities, like the tank on wbomb1 A spot, and not only solid world
	trace_t trace;
	float *start_ = const_cast<float *>( start.Data() );
	float *mins_ = const_cast<float *>( mins );
	float *maxs_ = const_cast<float *>( maxs );
	edict_t *ignore = const_cast<edict_t *>( self );
	G_Trace( &trace, start_, mins_, maxs_, areaPoint.Data(), ignore, MASK_AISOLID );
	return trace.fraction == 1.0f;
}

bool BotSameFloorClusterAreasCache::NeedsToComputed( BotMovementPredictionContext *context ) const {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const auto *floorClusterNums = aasWorld->AreaFloorClusterNums();

	if( !computedTargetAreaNum ) {
		return true;
	}

	if( floorClusterNums[context->CurrGroundedAasAreaNum()] != floorClusterNums[computedTargetAreaNum] ) {
		return true;
	}

	if( computedTargetAreaPoint.SquareDistanceTo( entityPhysicsState.Origin() ) < SQUARE( REACHABILITY_RADIUS ) ) {
		return true;
	}

	// Walkability tests in cluster are cheap but sometimes produce false negatives,
	// so do not check for walkability in the first second to prevent choice jitter
	if( level.time - computedAt > 1000 ) {
		if( !IsAreaWalkableInFloorCluster( context->CurrGroundedAasAreaNum(), computedTargetAreaNum ) ) {
			return true;
		}
	}

	return !AreaPassesCollisionTest( context, computedTargetAreaNum );
}

int BotSameFloorClusterAreasCache::GetClosestToTargetPoint( BotMovementPredictionContext *context,
															float *resultPoint, int *resultAreaNum ) const {
	// We have switched to using a cached value as far as it is feasible
	// avoiding computing an actual point almost every frame
	// (it has proven to cause jitter/looping)

	// Check whether an old value is present and is feasible
	if( NeedsToComputed( context ) ) {
		computedTargetAreaNum = 0;
		computedTargetAreaPoint.Set( 0, 0, 0 );
		if( ( computedTravelTime = FindClosestToTargetPoint( context, &computedTargetAreaNum ) ) ) {
			computedAt = level.time;
			const auto &area = aasWorld->Areas()[computedTargetAreaNum];
			computedTargetAreaPoint.Set( area.center );
			computedTargetAreaPoint.Z() = area.mins[2] + ( -playerbox_stand_mins[2] );
		}
	}

	if( computedTravelTime ) {
		if( resultAreaNum ) {
			*resultAreaNum = computedTargetAreaNum;
		}
		if( resultPoint ) {
			computedTargetAreaPoint.CopyTo( resultPoint );
		}
		return computedTravelTime;
	}

	return 0;
}

int BotSameFloorClusterAreasCache::FindClosestToTargetPoint( BotMovementPredictionContext *context,
															 int *resultAreaNum ) const {
	int currGroundedAreaNum = context->CurrGroundedAasAreaNum();
	if( !currGroundedAreaNum ) {
		return false;
	}

	CandidateAreasHeap candidateAreasHeap;
	if( currGroundedAreaNum != computedForAreaNum || oldCandidatesHeap.empty() ) {
		oldCandidatesHeap.clear();
		int floorClusterNum = aasWorld->FloorClusterNum( currGroundedAreaNum );
		if( !floorClusterNum ) {
			return false;
		}
		computedForAreaNum = currGroundedAreaNum;
		// Build new areas heap for the new flood start area
		const auto *clusterAreaNums = aasWorld->FloorClusterData( floorClusterNum ) + 1;
		// The number of areas in the cluster areas list prepends the first area num
		const auto numClusterAreas = clusterAreaNums[-1];
		BuildCandidateAreasHeap( context, clusterAreaNums, numClusterAreas, candidateAreasHeap );
		// Save the heap
		for( const auto &heapElem: candidateAreasHeap ) {
			oldCandidatesHeap.push_back( heapElem );
		}
	} else {
		// The flood start area has not been changed.
		// We can reuse old areas heap for walkability tests.
		// Populate the current heap (that is going to be modified) by backed heap values
		for( const auto &heapElem: oldCandidatesHeap ) {
			candidateAreasHeap.push_back( heapElem );
		}
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	vec3_t traceMins, traceMaxs;
	TacticalSpotsRegistry::GetSpotsWalkabilityTraceBounds( traceMins, traceMaxs );
	Vec3 start( entityPhysicsState.Origin() );
	if( entityPhysicsState.GroundEntity() ) {
		start.Z() += 1.0f;
	}

	while( !candidateAreasHeap.empty() ) {
		std::pop_heap( candidateAreasHeap.begin(), candidateAreasHeap.end() );
		int areaNum = candidateAreasHeap.back().areaNum;
		int travelTime = (int)( -candidateAreasHeap.back().score );
		candidateAreasHeap.pop_back();

		if( !IsAreaWalkableInFloorCluster( currGroundedAreaNum, areaNum ) ) {
			continue;
		}

		// We hope we have done all possible cutoffs at this moment of execution.
		// We still need this collision test since cutoffs are performed using thin rays.
		// This test is expensive that's why we try to defer it as far at it is possible.
		if( !AreaPassesCollisionTest( start, areaNum, traceMins, traceMaxs ) ) {
			continue;
		}

		// Stop on the first (and best since a heap is used) feasible area
		if( resultAreaNum ) {
			*resultAreaNum = areaNum;
		}
		return travelTime;
	}

	return 0;
}

void BotSameFloorClusterAreasCache::BuildCandidateAreasHeap( BotMovementPredictionContext *context,
															 const uint16_t *clusterAreaNums,
															 int numClusterAreas,
															 CandidateAreasHeap &result ) const {
	result.clear();

	const int currTravelTimeToTarget = context->TravelTimeToNavTarget();
	if( !currTravelTimeToTarget ) {
		return;
	}

	const auto *dangerToEvade = self->ai->botRef->perceptionManager.PrimaryDanger();
	// Reduce branching in the loop below
	if( self->ai->botRef->ShouldRushHeadless() || ( dangerToEvade && !dangerToEvade->SupportsImpactTests() ) ) {
		dangerToEvade = nullptr;
	}

	const auto *aasAreas = aasWorld->Areas();
	const auto *routeCache = self->ai->botRef->routeCache;
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const int toAreaNum = context->NavTargetAasAreaNum();

	for( int i = 0; i < numClusterAreas; ++i ) {
		int areaNum = clusterAreaNums[i];

		const auto &area = aasAreas[areaNum];
		Vec3 areaPoint( area.center );
		areaPoint.Z() = area.mins[2] + 1 + ( -playerbox_stand_mins[2] );

		const float squareDistance = areaPoint.SquareDistanceTo( entityPhysicsState.Origin() );
		if( squareDistance < SQUARE( SELECTION_THRESHOLD ) ) {
			continue;
		}

		// Cut off very far points as it leads to looping in some cases on vast open areas
		if( squareDistance > SQUARE( 4.0f * SELECTION_THRESHOLD ) ) {
			continue;
		}

		if( dangerToEvade && dangerToEvade->HasImpactOnPoint( areaPoint ) ) {
			continue;
		}

		int bestCurrTime = routeCache->FastestRouteToGoalArea( areaNum, toAreaNum );
		if( !bestCurrTime || bestCurrTime >= currTravelTimeToTarget ) {
			continue;
		}

		if( result.size() == result.capacity() ) {
			// Evict worst area
			std::pop_heap( result.begin(), result.end() );
			result.pop_back();
		}

		new( result.unsafe_grow_back())AreaAndScore( areaNum, currTravelTimeToTarget );
	}

	// We have set scores so worst area got evicted first, invert scores now so the best area is retrieved first
	for( auto &areaAndScore: result ) {
		areaAndScore.score = -areaAndScore.score;
	}

	std::make_heap( result.begin(), result.end() );
}

BotNavMeshQueryCache::BotNavMeshQueryCache( edict_t *self_ )
	: self( self_ ),
	  aasWorld( AiAasWorld::Instance() ),
	  computedAt( 0 ),
	  startOrigin( 0, 0, 0 ) {
	TacticalSpotsRegistry::GetSpotsWalkabilityTraceBounds( walkabilityTraceMins, walkabilityTraceMaxs );
}

bool BotNavMeshQueryCache::GetClosestToTargetPoint( BotMovementPredictionContext *context, float *resultPoint ) const {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( computedAt == level.time && VectorCompare( entityPhysicsState.Origin(), computedForOrigin ) ) {
		if( fabsf( computedResultPoint[0] ) < std::numeric_limits<float>::max() ) {
			VectorCopy( computedResultPoint, resultPoint );
			return true;
		}
		return false;
	}

	computedAt = level.time;
	VectorCopy( entityPhysicsState.Origin(), computedForOrigin );
	computedResultPoint[0] = std::numeric_limits<float>::infinity();
	if( FindClosestToTargetPoint( context, computedResultPoint ) ) {
		VectorCopy( computedResultPoint, resultPoint );
		return true;
	}
	return false;
}

bool BotNavMeshQueryCache::FindClosestToTargetPoint( BotMovementPredictionContext *context, float *resultPoint ) const {
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasReach = aasWorld->Reachabilities();

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	this->startOrigin.Set( entityPhysicsState.Origin() );

	int lastReachIndex = -1;
	const auto &reachChain = context->NextReachChain();
	for( unsigned i = 0; i < reachChain.size(); ++i ) {
		const auto &reach = aasReach[reachChain[i].ReachNum()];
		int travelType = reach.traveltype;
		if( travelType != TRAVEL_WALK ) {
			break;
		}
		// Skip far areas, except they are next in the chain
		if( this->startOrigin.SquareDistanceTo( reach.start ) > SQUARE( 768.0f )) {
			if( lastReachIndex > 2 ) {
				break;
			}
		}
		lastReachIndex++;
		// Make sure reach indices are in [0, MAX_TESTED_REACH) range
		if( lastReachIndex + 1 == MAX_TESTED_REACH ) {
			break;
		}
	}

	// There were no reachabilities having the given criteria found
	if( lastReachIndex < 0 ) {
		return false;
	}

	BotNavMeshQueryCache *mutableThis = const_cast<BotNavMeshQueryCache *>( this );
	// Try finding a path to each area on the nav mesh, test and mark path polys using nav mesh raycasting
	if( !mutableThis->TryNavMeshWalkabilityTests( context, lastReachIndex, resultPoint ) ) {
		// Try testing and marking paths polys using collision/aas raycasting
		if( !mutableThis->TryTraceAndAasWalkabilityTests( context, lastReachIndex, resultPoint ) ) {
			return false;
		}
	}

	return true;
}

template<typename T>
class BloomFilterSet {
	// All size parameters are prime numbers
	static constexpr uint16_t BIN_1_SIZE = 5791;
	static constexpr uint16_t BIN_2_SIZE = 5827;
	static constexpr uint16_t BIN_3_SIZE = 5939;
	static constexpr uint16_t BIN_4_SIZE = 5987;

	uint32_t bin1Words[(BIN_1_SIZE / 32) + 1];
	uint32_t bin2Words[(BIN_2_SIZE / 32) + 1];
	uint32_t bin3Words[(BIN_3_SIZE / 32) + 1];
	uint32_t bin4Words[(BIN_4_SIZE / 32) + 1];

	static constexpr auto NUM_BINS = 4;

	static_assert( !std::numeric_limits<T>::is_signed, "The set is not applicable to signed types" );
public:
	BloomFilterSet() {
		Clear();
	}

	void Clear() {
		memset( this, 0, sizeof( *this ) );
	}

	// Checks whether the value is definitely is not in set.
	// If true, adds it to set and returns true.
	// Otherwise, returns false (so there might be false positives).
	bool MarkIfIsDefinitelyNotMarked( T value ) {
		// There could be much trickier bit ops but remember that this bloom filter
		// is used to cut off much more expensive computations, so keep it readable.

		// A modulo of the value by the corresponding bin size
		uint16_t binIndices[NUM_BINS];
		// An iterable array of bin sizes
		const uint16_t binSizes[NUM_BINS] = { BIN_1_SIZE, BIN_2_SIZE, BIN_3_SIZE, BIN_4_SIZE };
		// An iterable array of bin words
		uint32_t *binWordArrays[NUM_BINS] = { bin1Words, bin2Words, bin3Words, bin4Words };

		// An integer division is not cheap and it's better to use
		// a dynamic branch to force 16-bit division in suitable cases
		if( sizeof( T ) > sizeof( uint16_t ) && value <= (T)std::numeric_limits<uint16_t>::max() ) {
			uint16_t uint16Value = (uint16_t)value;
			for( int i = 0; i < NUM_BINS; ++i ) {
				binIndices[i] = uint16Value % binSizes[i];
			}
		} else {
			for( int i = 0; i < NUM_BINS; ++i ) {
				binIndices[i] = value % binSizes[i];
			}
		}

		// Each index corresponds to a pair (word num in a bin, mask for the word at word num)
		uint16_t binWordNums[NUM_BINS];
		uint32_t binBitMasks[NUM_BINS];
		for( int i = 0; i < NUM_BINS; ++i ) {
			binWordNums[i] = binIndices[i] / (uint16_t)32;
			binBitMasks[i] = 1u << ( binIndices[i] % 32u );
		}

		int j = 0;
		for(; j < NUM_BINS; ++j ) {
			// The actual words array for j-th bin
			uint32_t *binWords = binWordArrays[j];
			if( !( binWords[binWordNums[j]] & binBitMasks[j] ) ) {
				break;
			}
		}

		// If all bits corresponding to the value in all bins are set
		if( j == NUM_BINS ) {
			return false;
		}

		// Set bits in all bins
		for( int i = 0; i < NUM_BINS; ++i ) {
			// The actual words array for j-th bin
			uint32_t *binWords = binWordArrays[j];
			binWords[binWordNums[j]] |= binBitMasks[j];
		}

		return true;
	}
};

static BloomFilterSet<uint32_t> polysBloomFilterSet;

bool BotNavMeshQueryCache::TryNavMeshWalkabilityTests( BotMovementPredictionContext *context,
													   int lastReachIndex,
													   float *resultPoint ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const auto &reachChain = context->NextReachChain();

	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasReach = aasWorld->Reachabilities();

	Vec3 startAbsMins( playerbox_stand_mins );
	Vec3 startAbsMaxs( playerbox_stand_maxs );
	startAbsMins += this->startOrigin;
	startAbsMins.Z() -= 0.25f;
	startAbsMaxs += this->startOrigin;
	if( !entityPhysicsState.GroundEntity() ) {
		if( entityPhysicsState.IsHighAboveGround() ) {
			return false;
		}
		float heightOverGround = entityPhysicsState.HeightOverGround();
		startAbsMins.Z() -= heightOverGround;
		startAbsMaxs.Z() -= heightOverGround;
		startOrigin.Z() -= heightOverGround;
	}

	if( !self->ai->botRef->navMeshQuery ) {
		if( !( self->ai->botRef->navMeshQuery = AiNavMeshManager::Instance()->AllocQuery( self->r.client ) ) ) {
			return false;
		}
	}

	auto *query = self->ai->botRef->navMeshQuery;
	const uint32_t startPolyRef = query->FindNearestPoly( startAbsMins.Data(), startAbsMaxs.Data() );
	if( !startPolyRef ) {
		return false;
	}

	const auto *navMeshManager = AiNavMeshManager::Instance();

	auto *polysSet = &::polysBloomFilterSet;
	polysSet->Clear();

	trace_t trace;
	for( int reachChainIndex = lastReachIndex; reachChainIndex >= 0; --reachChainIndex ) {
		uint32_t *const pathPolyRefs = this->paths[reachChainIndex];
		const auto &area = aasAreas[aasReach[reachChain[reachChainIndex].ReachNum()].areanum];
		const uint32_t areaPolyRef = query->FindNearestPoly( area.mins, area.maxs );
		if( !areaPolyRef ) {
			continue;
		}

		const int numPathPolys = query->FindPath( startPolyRef, areaPolyRef, pathPolyRefs, MAX_PATH_POLYS );
		// Check whether a path exists and really ends with the target poly
		if( !numPathPolys || pathPolyRefs[numPathPolys - 1] != areaPolyRef ) {
			// Invalidate the path for further trace tests
			this->pathLengths[reachChainIndex] = 0;
			continue;
		}

		// Mark the path as valid for further trace tests
		this->pathLengths[reachChainIndex] = numPathPolys;

		// Starting from the last poly in the path, find first walkable poly
		int pathPolyIndex = numPathPolys - 1;
		for( ; pathPolyIndex > 0; --pathPolyIndex ) {
			uint32_t polyRef = pathPolyRefs[pathPolyIndex];
			// Skip testing if the poly is likely to have been already tested
			if( !polysSet->MarkIfIsDefinitelyNotMarked( polyRef ) ) {
				continue;
			}

			if( query->TraceWalkability( startPolyRef, startOrigin.Data(), pathPolyRefs[pathPolyIndex] ) ) {
				// We have to check a real trace as well since Detour raycast ignores height
				navMeshManager->GetPolyCenter( pathPolyRefs[pathPolyIndex], resultPoint );
				resultPoint[2] += 1.0f - playerbox_stand_mins[2];
				StaticWorldTrace( &trace, startOrigin.Data(), resultPoint, MASK_SOLID | MASK_WATER,
								  walkabilityTraceMins, walkabilityTraceMaxs );
				if( trace.fraction == 1.0f ) {
					return true;
				}

				// Invalidate poly ref for further trace tests
				pathPolyRefs[pathPolyIndex] = 0;
			}
		}
	}

	return false;
}

bool BotNavMeshQueryCache::TryTraceAndAasWalkabilityTests( BotMovementPredictionContext *context,
														   int lastReachIndex,
														   float *resultPoint ) {
	const auto *aasReach = aasWorld->Reachabilities();
	const auto &reachChain = context->NextReachChain();
	const auto *navMeshManager = AiNavMeshManager::Instance();

	auto *const polysSet = &::polysBloomFilterSet;
	// We use different ways of raycasting now, so previous results are not applicable.
	// Polys cut off by a final trace is the only exception,
	// but it has been already handled differenly by setting zero poly ref in the path.
	polysSet->Clear();

	// At this moment all nav mesh raycasts have failed.
	// Try using a real trace and check areas along the traced segment.
	// We have to do it due to poor nav mesh quality and necessity to provide a feasible path
	// in all cases where it is possible (nav mesh is primarily used for fallback movement).

	trace_t trace;
	for( int reachChainIndex = lastReachIndex; reachChainIndex >= 0; --reachChainIndex ) {
		if( !pathLengths[reachChainIndex] ) {
			continue;
		}

		const auto &reach = aasReach[reachChain[reachChainIndex].ReachNum()];
		// The poly is way too far and tracing through collision world/areas will be too expensive.
		if( startOrigin.SquareDistanceTo( reach.start ) > SQUARE( 384.0f ) ) {
			continue;
		}

		uint32_t *const pathPolyRefs = paths[reachChainIndex];
		for( int pathPolyIndex = pathLengths[reachChainIndex] - 1; pathPolyIndex > 0; --pathPolyIndex ) {
			uint32_t pathPolyRef = pathPolyRefs[pathPolyIndex];
			// If tests above have invalidated this poly
			if( !pathPolyRef ) {
				continue;
			}
			// Skip testing if the poly is likely to have been already tested
			if( !polysSet->MarkIfIsDefinitelyNotMarked( pathPolyRef ) ) {
				continue;
			}

			vec3_t pathPolyOrigin;
			navMeshManager->GetPolyCenter( pathPolyRefs[pathPolyIndex], pathPolyOrigin );
			pathPolyOrigin[2] += 1.0f - playerbox_stand_mins[2];

			if( !InspectAasWorldTraceToPoly( pathPolyOrigin ) ) {
				continue;
			}

			StaticWorldTrace( &trace, startOrigin.Data(), pathPolyOrigin, MASK_SOLID | MASK_WATER,
							  walkabilityTraceMins, walkabilityTraceMaxs );
			if( trace.fraction == 1.0f ) {
				VectorCopy( pathPolyOrigin, resultPoint );
				return true;
			}
		}
	}

	return false;
}

bool BotNavMeshQueryCache::InspectAasWorldTraceToPoly( const vec3_t polyOrigin ) {
	const int polyAreaNum = aasWorld->FindAreaNum( polyOrigin );
	if( !polyAreaNum ) {
		return false;
	}

	int tracedAreaNums[32];
	const int numTracedAreas = aasWorld->TraceAreas( startOrigin.Data(), polyOrigin, tracedAreaNums, 32 );
	// The last area in traced areas must match the destination (poly) area, otherwise we have probably hit an obstacle
	if( !numTracedAreas || tracedAreaNums[numTracedAreas - 1] != polyAreaNum ) {
		return false;
	}

	const auto *aasAreas = aasWorld->Areas();
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const auto *aasAreaStairsClusterNums = aasWorld->AreaStairsClusterNums();
	const auto *aasFaceIndex = aasWorld->FaceIndex();
	const auto *aasPlanes = aasWorld->Planes();
	const auto numAasPlanes = aasWorld->NumPlanes();
	const auto *aasFaces = aasWorld->Faces();

	constexpr auto BAD_CONTENTS = AREACONTENTS_LAVA | AREACONTENTS_SLIME | AREACONTENTS_DONOTENTER;

	// Try checking all areas the trace has passed through for being a pit/an obstacle or just bad
	for( int j = 1; j < numTracedAreas; ++j ) {
		const int areaNum = tracedAreaNums[j];
		const auto &areaSettings = aasAreaSettings[areaNum];
		// If the area is not an ramp-like area
		if( !( areaSettings.areaflags & AREA_INCLINED_FLOOR ) ) {
			// If the area is not in stairs cluster too
			if( !aasAreaStairsClusterNums[areaNum] ) {
				// Check whether area mins is within sane bounds relative to the start origin.
				// This condition should cut off pits/obstacles.
				const auto &currTraceArea = aasAreas[areaNum];
				if( areaSettings.areaflags & AREA_GROUNDED ) {
					float areaMinZ = currTraceArea.mins[2];
					if( startOrigin.Z() > 24.0f + areaMinZ ) {
						return false;
					}
					if( startOrigin.Z() < areaMinZ ) {
						return false;
					}
				} else {
					// Try cut off pits. Make sure there is a grounded area below and its within the bounds
					int faceIndexNum = currTraceArea.firstface;
					int faceIndexNumBound = faceIndexNum + currTraceArea.numfaces;
					for(; faceIndexNum < faceIndexNumBound; ++faceIndexNum ) {
						const auto &face = aasFaces[abs(aasFaceIndex[faceIndexNum])];
						// IIRC some faces have bogus plane nums
						if( abs( face.planenum ) > numAasPlanes ) {
							continue;
						}
						const auto &plane = aasPlanes[abs( face.planenum )];
						if( fabsf( plane.normal[2] ) < 0.9f ) {
							continue;
						}
						const int faceAreaNum = face.frontarea == areaNum ? face.backarea : face.frontarea;
						if( !faceAreaNum ) {
							continue;
						}

						const auto &faceAreaSettings = aasAreaSettings[faceAreaNum];
						if( faceAreaSettings.contents & BAD_CONTENTS ) {
							return false;
						}

						if( faceAreaSettings.areaflags & AREA_GROUNDED ) {
							// Check the ground height condition
							float areaMinZ = aasAreas[faceAreaNum].mins[2];
							if( startOrigin.Z() > 24.0f + areaMinZ ) {
								return false;
							}
							if( startOrigin.Z() < areaMinZ ) {
								return false;
							}
							// We have found a grounded area that has a boundary with the current area on the trace segment
							break;
						}
					}

					// There is no grounded area below
					if( faceIndexNum == faceIndexNumBound ) {
						return false;
					}
				}
			}
		}

		if( areaSettings.contents & BAD_CONTENTS ) {
			return false;
		}
	}

	return true;
}

BotMovementFallback *BotDummyMovementAction::TryFindMovementFallback( BotMovementPredictionContext *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	// First check for being in lava
	// TODO: Inspect why waterType does not work as intended
	if( entityPhysicsState.waterLevel >= 1 ) {
		const auto *aasAreaSettings = AiAasWorld::Instance()->AreaSettings();
		int currAreaNums[2] = { 0, 0 };
		if( int numCurrAreas = entityPhysicsState.PrepareRoutingStartAreas( currAreaNums ) ) {
			int i = 0;
			// Try check whether there is really lava here
			for( ; i < numCurrAreas; ++i ) {
				if( aasAreaSettings[currAreaNums[i]].contents & ( AREACONTENTS_LAVA | AREACONTENTS_SLIME ) ) {
					break;
				}
			}
			// Start checking for jumping fallback only after that (do not fail with double computations!)
			if( i != numCurrAreas ) {
				if( auto *fallback = TryFindJumpFromLavaFallback( context ) ) {
					return fallback;
				}
			}
		}
	}

	// All the following checks require a valid nav target
	if( !context->NavTargetAasAreaNum() ) {
		if( self->ai->botRef->MillisInBlockedState() > 500 ) {
			if( auto *fallback = TryFindLostNavTargetFallback( context ) ) {
				return fallback;
			}
		}
		return nullptr;
	}

	// Check if the bot is standing on a ramp
	if( entityPhysicsState.GroundEntity() && entityPhysicsState.GetGroundNormalZ() < 0.999f ) {
		if( int groundedAreaNum = context->CurrGroundedAasAreaNum() ) {
			if( AiAasWorld::Instance()->AreaSettings()[groundedAreaNum].areaflags & AREA_INCLINED_FLOOR ) {
				if( auto *fallback = TryFindRampFallback( context, groundedAreaNum ) ) {
					return fallback;
				}
			}
		}
	}

	if( auto *fallback = TryFindAasBasedFallback( context ) ) {
		return fallback;
	}

	// Check for stairs
	if( auto *fallback = TryFindStairsFallback( context ) ) {
		return fallback;
	}

	// It is not unusual to see tiny ramp-like areas to the both sides of stairs.
	// Try using these ramp areas as directions for fallback movement.
	if( auto *fallback = TryFindNearbyRampAreasFallback( context ) ) {
		return fallback;
	}

	if( auto *fallback = TryFindWalkableTriggerFallback( context ) ) {
		return fallback;
	}

	if( auto *fallback = TryNodeBasedFallbacksLeft( context ) ) {
		// Check whether its really a node based fallback
		auto *const nodeBasedFallback = &self->ai->botRef->useWalkableNodeMovementFallback;
		if( fallback == nodeBasedFallback ) {
			const vec3_t &origin = nodeBasedFallback->NodeOrigin();
			const int areaNum = nodeBasedFallback->NodeAreaNum();
			if( auto *jumpFallback = TryShortcutOtherFallbackByJumping( context, origin, areaNum ) ) {
				return jumpFallback;
			}
		}
		return fallback;
	}

	if( auto *fallback = TryFindJumpAdvancingToTargetFallback( context ) ) {
		return fallback;
	}

	return nullptr;
}

BotMovementFallback *BotDummyMovementAction::TryNodeBasedFallbacksLeft( BotMovementPredictionContext *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	const unsigned millisInBlockedState = self->ai->botRef->MillisInBlockedState();
	const bool isBotEasy = self->ai->botRef->Skill() < 0.33f;
	// This is a very conservative condition that however should prevent looping these node-based fallbacks are prone to.
	// Prefer jumping fallbacks that are almost seamless to the bunnying movement.
	// Note: threshold values are significanly lower for easy bots since they almost never use bunnying movement.
	if( millisInBlockedState < ( isBotEasy ? 500 : 1500 ) ) {
		return nullptr;
	}

	// Try using the nav target as a fallback movement target
	Assert( context->NavTargetAasAreaNum() );
	auto *nodeFallback = &self->ai->botRef->useWalkableNodeMovementFallback;
	if( context->NavTargetOrigin().SquareDistanceTo( entityPhysicsState.Origin() ) < SQUARE( 384.0f ) ) {
		Vec3 target( context->NavTargetOrigin() );
		target.Z() += -playerbox_stand_mins[2];
		nodeFallback->Activate( target.Data(), 32.0f, context->NavTargetAasAreaNum() );
		nodeFallback->TryDeactivate( context );
		if( nodeFallback->IsActive() ) {
			return nodeFallback;
		}
	}

	if( millisInBlockedState > ( isBotEasy ? 700 : 2000 ) ) {
		vec3_t areaPoint;
		int areaNum;
		if( context->sameFloorClusterAreasCache.GetClosestToTargetPoint( context, areaPoint, &areaNum ) ) {
			nodeFallback->Activate( areaPoint, 48.0f, areaNum );
			return nodeFallback;
		}

		if( context->navMeshQueryCache.GetClosestToTargetPoint( context, areaPoint ) ) {
			float squareDistance = Distance2DSquared( context->movementState->entityPhysicsState.Origin(), areaPoint );
			if( squareDistance > SQUARE( 8 ) ) {
				areaNum = AiAasWorld::Instance()->FindAreaNum( areaPoint );
				float reachRadius = std::min( 64.0f, SQRTFAST( squareDistance ) );
				nodeFallback->Activate( areaPoint, reachRadius, areaNum );
				return nodeFallback;
			}
		}

		if( millisInBlockedState > ( isBotEasy ? 1250 : 2500 ) ) {
			// Notify the nav target selection code
			self->ai->botRef->OnMovementToNavTargetBlocked();
		}
	}

	return nullptr;
}

BotMovementFallback *BotDummyMovementAction::TryFindAasBasedFallback( BotMovementPredictionContext *context ) {
	const int nextReachNum = context->NextReachNum();
	if( !nextReachNum ) {
		return nullptr;
	}

	const auto &nextReach = AiAasWorld::Instance()->Reachabilities()[nextReachNum];
	const int traveltype = nextReach.traveltype & TRAVELTYPE_MASK;

	if( traveltype == TRAVEL_WALK ) {
		return TryFindWalkReachFallback( context, nextReach );
	}

	if( traveltype == TRAVEL_JUMPPAD || traveltype == TRAVEL_TELEPORT || traveltype == TRAVEL_ELEVATOR ) {
		// Always follow these reachabilities
		auto *fallback = &self->ai->botRef->useWalkableNodeMovementFallback;
		// Note: We have to add several units to the target Z, otherwise a collision test
		// on next frame is very likely to immediately deactivate it
		fallback->Activate( ( Vec3( 0, 0, -playerbox_stand_mins[2] ) + nextReach.start ).Data(), 16.0f );
		return fallback;
	}

	if( traveltype == TRAVEL_WALKOFFLEDGE ) {
		return TryFindWalkOffLedgeReachFallback( context, nextReach );
	}

	if( traveltype == TRAVEL_JUMP || traveltype == TRAVEL_STRAFEJUMP ) {
		return TryFindJumpLikeReachFallback( context, nextReach );
	}

	// The only possible fallback left
	auto *fallback = &self->ai->botRef->jumpOverBarrierMovementFallback;
	if( traveltype == TRAVEL_BARRIERJUMP || traveltype == TRAVEL_WATERJUMP ) {
		fallback->Activate( nextReach.start, nextReach.end );
		return fallback;
	}

	// Disallow WJ attempts for TRAVEL_DOUBLEJUMP reachabilities
	if( traveltype == TRAVEL_DOUBLEJUMP ) {
		fallback->Activate( nextReach.start, nextReach.end, false );
		return fallback;
	}

	return nullptr;
}

BotMovementFallback *BotDummyMovementAction::TryFindWalkReachFallback( BotMovementPredictionContext *context,
																	   const aas_reachability_t &nextReach ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	// Allow following WALK reachabilities but make sure
	// they do not lead to junk areas and are fairly far away to prevent looping.
	float squareDistance = DistanceSquared( entityPhysicsState.Origin(), nextReach.end );
	if( squareDistance < SQUARE( 72.0f ) ) {
		return nullptr;
	}

	const auto &areaSettings = AiAasWorld::Instance()->AreaSettings()[nextReach.areanum];
	if( areaSettings.areaflags & AREA_JUNK ) {
		return nullptr;
	}

	if( auto *fallback = TryShortcutOtherFallbackByJumping( context, nextReach.end, nextReach.areanum ) ) {
		return fallback;
	}

	auto *fallback = &self->ai->botRef->useWalkableNodeMovementFallback;
	unsigned timeout = (unsigned)( 1000.0f * sqrtf( squareDistance ) / context->GetRunSpeed() );
	// Note: We have to add several units to the target Z, otherwise a collision test
	// on next frame is very likely to immediately deactivate it
	Vec3 target( nextReach.end );
	target.Z() += -playerbox_stand_mins[2];
	fallback->Activate( target.Data(), 16.0f, AiAasWorld::Instance()->FindAreaNum( target ), timeout );
	return fallback;
}

BotMovementFallback *BotDummyMovementAction::TryShortcutOtherFallbackByJumping( BotMovementPredictionContext *context,
																				int initialTargetAreaNum ) {
	Assert( initialTargetAreaNum );
	const auto &area = AiAasWorld::Instance()->Areas()[initialTargetAreaNum];
	Vec3 areaPoint( area.center );
	areaPoint.Z() = area.mins[2] + 1.0f - playerbox_stand_mins[2];
	return TryShortcutOtherFallbackByJumping( context, areaPoint.Data(), initialTargetAreaNum );
}

BotMovementFallback *BotDummyMovementAction::TryShortcutOtherFallbackByJumping( BotMovementPredictionContext *context,
																				const vec3_t initialTarget,
																				int initialTargetAreaNum ) {
	if( self->ai->botRef->ShouldBeSilent() ) {
		return nullptr;
	}

	if( !( context->currPlayerState->stats[PM_STAT_FEATURES] & PMFEAT_JUMP ) ) {
		return nullptr;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	// Check necessary preconditions first to cut off expensive trajectory prediction

	if( !entityPhysicsState.GroundEntity() ) {
		return nullptr;
	}

	const auto *aasWorld = AiAasWorld::Instance();
	float distanceThreshold = 96.0f;
	const int groundedAreaNum = context->CurrGroundedAasAreaNum();
	// Lower distance threshold for inclined floor/stairs areas where a bot is very likely to get stuck
	const int groundedAreaFlags = aasWorld->AreaSettings()[groundedAreaNum].areaflags;
	if( groundedAreaFlags & AREA_INCLINED_FLOOR ) {
		distanceThreshold = 72.0f;
	}

	if( DistanceSquared( entityPhysicsState.Origin(), initialTarget ) < SQUARE( distanceThreshold ) ) {
		return nullptr;
	}

	// Do not try jumping to a-priori non-reachable by jumping targets
	if( entityPhysicsState.Origin()[2] < initialTarget[2] ) {
		// Mins does not correspond to the real ground level in this case, just reject the fallback
		if( groundedAreaFlags & AREA_INCLINED_FLOOR ) {
			return nullptr;
		}
		// initialTarget might use arbitrary offset from ground, check a real height over ground
		const auto &targetArea = aasWorld->Areas()[initialTargetAreaNum];
		if( entityPhysicsState.Origin()[2] + playerbox_stand_mins[2] < targetArea.mins[2] + AI_JUMPABLE_HEIGHT ) {
			return nullptr;
		}
	}

	// Dont try starting to jump having a high speed, this leads to looping
	if( entityPhysicsState.Speed2D() > context->GetRunSpeed() ) {
		return nullptr;
	}

	if( !initialTargetAreaNum ) {
		if( !( initialTargetAreaNum = AiAasWorld::Instance()->FindAreaNum( initialTarget ) ) ) {
			return nullptr;
		}
	}

	AiTrajectoryPredictor predictor;
	predictor.SetColliderBounds( ( Vec3( 0, 0, 8 ) + playerbox_stand_mins ).Data(), playerbox_stand_maxs );
	predictor.SetStepMillis( 128 );
	predictor.SetNumSteps( 8 );
	predictor.SetEnterAreaProps( 0, AREACONTENTS_LAVA | AREACONTENTS_SLIME | AREACONTENTS_DONOTENTER );
	predictor.AddStopEventFlags( AiTrajectoryPredictor::HIT_SOLID | AiTrajectoryPredictor::ENTER_AREA_CONTENTS );

	Vec3 startVelocity( initialTarget );
	startVelocity -= entityPhysicsState.Origin();
	startVelocity.Z() = 0;
	startVelocity.NormalizeFast();
	startVelocity *= context->GetRunSpeed();
	startVelocity.Z() = context->GetJumpSpeed();

	Vec3 startOrigin( entityPhysicsState.Origin() );
	startOrigin.Z() += 1.0f;

	AiTrajectoryPredictor::Results predictionResults;
	auto stopEvents = predictor.Run( startVelocity, startOrigin, &predictionResults );
	if( !( stopEvents & AiTrajectoryPredictor::HIT_SOLID ) ) {
		return nullptr;
	}
	// If the bot has entered a hazard area
	if( stopEvents & AiTrajectoryPredictor::ENTER_AREA_CONTENTS ) {
		return nullptr;
	}

	int landingAreaNum = predictionResults.lastAreaNum;
	// We have not landed in the target area, check whether we have landed in even better one
	if( landingAreaNum != initialTargetAreaNum ) {
		const auto *routeCache = self->ai->botRef->routeCache;
		const int goalAreaNum = context->NavTargetAasAreaNum();
		const auto travelFlags = Bot::ALLOWED_TRAVEL_FLAGS;
		int nextReachTravelTime = routeCache->TravelTimeToGoalArea( initialTargetAreaNum, goalAreaNum, travelFlags );
		int landingTargetTravelTime = routeCache->TravelTimeToGoalArea( landingAreaNum, goalAreaNum, travelFlags );
		if( !landingTargetTravelTime || landingTargetTravelTime + 10 > nextReachTravelTime ) {
			return nullptr;
		}
	}

	auto *fallback = &self->ai->botRef->jumpToSpotMovementFallback;
	fallback->Activate( entityPhysicsState.Origin(), predictionResults.origin, predictionResults.millisAhead );
	return fallback;
}

BotMovementFallback *BotDummyMovementAction::TryFindWalkOffLedgeReachFallback( BotMovementPredictionContext *context,
																			   const aas_reachability_t &nextReach ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	// If the falling distance is really low, treat is just as walking to a node
	const float squareFallingHeight = DistanceSquared( nextReach.start, nextReach.end );
	if( squareFallingHeight < SQUARE( 40.0f ) ) {
		auto *fallback = &self->ai->botRef->useWalkableNodeMovementFallback;
		float squareDistance = DistanceSquared( entityPhysicsState.Origin(), nextReach.start );
		unsigned timeout = 100 + (unsigned)( 1000.0f * sqrtf( squareDistance ) / context->GetRunSpeed() );
		Vec3 target( nextReach.start );
		target.Z() += 1.0f - playerbox_stand_mins[2];
		fallback->Activate( target.Data(), 16.0f, nextReach.areanum, timeout );
		return fallback;
	}

	auto *fallback = &self->ai->botRef->fallDownMovementFallback;
	Vec3 targetOrigin( nextReach.end );
	// Setting the proper Z (should be greater than an origin of bot standing at destination) is important!
	targetOrigin.Z() = AiAasWorld::Instance()->Areas()[nextReach.areanum].mins[2] + 4.0f - playerbox_stand_mins[2];
	// Compute the proper timeout
	float distanceToReach = sqrtf( Distance( entityPhysicsState.Origin(), nextReach.start ) );
	unsigned travelTimeToLedgeMillis = (unsigned)( 1000.0f * distanceToReach / context->GetRunSpeed() );
	unsigned fallingTimeMillis = (unsigned)( 1000.0f * sqrtf( 2.0f * sqrtf( squareFallingHeight ) / level.gravity ) );
	fallback->Activate( targetOrigin.Data(), travelTimeToLedgeMillis + fallingTimeMillis + 250, 24.0f );
	return fallback;
}

BotMovementFallback *BotDummyMovementAction::TryFindJumpLikeReachFallback( BotMovementPredictionContext *context,
																		   const aas_reachability_t &nextReach ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	// BSPC generates lots of junk jump reachabilities from small steps to small steps, stairs/ramps boundaries, etc
	// Check whether all areas from start and end are grounded and are of the same height,
	// and run instead of jumping in this case

	// Check only if a reachability does not go noticeably upwards
	if( nextReach.end[2] < nextReach.start[2] + 16.0f ) {
		Vec3 reachVec( nextReach.start );
		reachVec -= nextReach.end;
		float squareReachLength = reachVec.SquaredLength();
		// If a reachability is rather short
		if( squareReachLength < SQUARE( 72.0f ) ) {
			// If there is no significant sloppiness
			if( fabsf( reachVec.Z() ) / sqrtf( SQUARE( reachVec.X() ) + SQUARE( reachVec.Y() ) ) < 0.3f ) {
				const auto *aasWorld = AiAasWorld::Instance();
				const auto *aasAreas = aasWorld->Areas();
				const auto *aasAreaSettings = aasWorld->AreaSettings();

				int tracedAreaNums[32];
				tracedAreaNums[0] = 0;
				const int numTracedAreas = aasWorld->TraceAreas( nextReach.start, nextReach.end, tracedAreaNums, 32 );
				const float startAreaZ = aasAreas[tracedAreaNums[0]].mins[2];
				int i = 1;
				for(; i < numTracedAreas; ++i ) {
					const int areaNum = tracedAreaNums[i];
					// Stop on a non-grounded area
					if( !( aasAreaSettings[areaNum].areaflags & AREA_GROUNDED ) ) {
						break;
					}
					// Force jumping over lava/slime/water
					// TODO: not sure about jumppads, if would ever have an intention to pass jumppad without touching it
					constexpr auto undesiredContents = AREACONTENTS_LAVA | AREACONTENTS_SLIME | AREACONTENTS_WATER;
					if( aasAreaSettings[areaNum].contents & undesiredContents ) {
						break;
					}
					// Stop if there is a significant height difference
					if( fabsf( aasAreas[areaNum].mins[2] - startAreaZ ) > 12.0f ) {
						break;
					}
				}

				// All areas pass the walkability test, use walking to a node that seems to be really close
				if( i == numTracedAreas ) {
					auto *fallback = &self->ai->botRef->useWalkableNodeMovementFallback;
					Vec3 target( nextReach.end );
					target.Z() += 1.0f - playerbox_stand_mins[2];
					fallback->Activate( target.Data(), 24.0f, AiAasWorld::Instance()->FindAreaNum( target ), 500u );
					return fallback;
				}
			}
		}
	}

	AiTrajectoryPredictor predictor;
	predictor.SetStepMillis( 128 );
	predictor.SetNumSteps( 8 );
	predictor.SetEnterAreaProps( 0, AREACONTENTS_LAVA | AREACONTENTS_SLIME | AREACONTENTS_DONOTENTER );
	predictor.SetEnterAreaNum( nextReach.areanum );
	predictor.SetColliderBounds( vec3_origin, playerbox_stand_maxs );
	predictor.SetEntitiesCollisionProps( true, ENTNUM( self ) );
	predictor.AddStopEventFlags( AiTrajectoryPredictor::HIT_SOLID | AiTrajectoryPredictor::HIT_ENTITY );

	int numAttempts;
	float startSpeed2D;

	const float *attemptsZBoosts;
	const float *startAirAccelFracs;
	const float *endAirAccelFracs;

	const float jumpAttemptsZBoosts[] = { 0.0f, 10.0f };
	// Not used in prediction but should fit results for assumed start 2D speed
	const float jumpStartAirAccelFracs[] = { 0.0f, 0.5f };
	const float jumpEndAirAccelFracs[] = { 0.0f, 0.0f };

	const float strafejumpAttemptsZBoosts[] = { 0.0f, 15.0f, 40.0f };
	// Not used in prediction but should fit results for assumed start 2D speed
	const float strafejumpStartAirAccelFracs[] = { 0.9f, 1.0f, 1.0f };
	const float strafejumpEndAirAccelFracs[] = { 0.5f, 0.9f, 1.0f };

	if( ( nextReach.traveltype & TRAVELTYPE_MASK ) == TRAVEL_STRAFEJUMP ) {
		// Approximate applied acceleration as having 470 units of starting speed
		// That's what BSPC assumes for generating strafejumping reachabilities
		startSpeed2D = 470.0;
		numAttempts = sizeof( strafejumpAttemptsZBoosts ) / sizeof( *strafejumpAttemptsZBoosts );
		attemptsZBoosts = strafejumpAttemptsZBoosts;
		startAirAccelFracs = strafejumpStartAirAccelFracs;
		endAirAccelFracs = strafejumpEndAirAccelFracs;
	} else {
		startSpeed2D = context->GetRunSpeed();
		numAttempts = sizeof( jumpAttemptsZBoosts ) / sizeof( *jumpAttemptsZBoosts );
		attemptsZBoosts = jumpAttemptsZBoosts;
		startAirAccelFracs = jumpStartAirAccelFracs;
		endAirAccelFracs = jumpEndAirAccelFracs;
	}

	Vec3 startVelocity( nextReach.end );
	startVelocity -= entityPhysicsState.Origin();
	startVelocity.Z() = 0;
	startVelocity.Normalize();
	startVelocity *= startSpeed2D;

	const float defaultJumpSpeed = context->GetJumpSpeed();

	const auto *routeCache = self->ai->botRef->routeCache;
	int navTargetAreaNum = context->NavTargetAasAreaNum();
	// Note: we don't stop on the first feasible travel time here and below
	int travelTimeFromReachArea = routeCache->FastestRouteToGoalArea( nextReach.areanum, navTargetAreaNum );
	if( !travelTimeFromReachArea ) {
		return nullptr;
	}

	AiTrajectoryPredictor::Results predictionResults;

	vec3_t jumpTarget;
	int jumpTargetAreaNum = 0;

	int i = 0;
	for(; i < numAttempts; ++i ) {
		startVelocity.Z() = defaultJumpSpeed + attemptsZBoosts[i];

		if( i ) {
			// Results are cleared by default
			predictionResults.Clear();
		}

		auto stopEvents = predictor.Run( startVelocity.Data(), entityPhysicsState.Origin(), &predictionResults );
		// A trajectory have entered an undesired contents
		if( stopEvents & AiTrajectoryPredictor::ENTER_AREA_CONTENTS ) {
			continue;
		}

		// A trajectory has hit the target area
		if( stopEvents & AiTrajectoryPredictor::ENTER_AREA_NUM ) {
			VectorCopy( nextReach.end, jumpTarget );
			jumpTargetAreaNum = nextReach.areanum;
			break;
		}

		if( !( stopEvents & AiTrajectoryPredictor::HIT_SOLID ) ) {
			continue;
		}

		if( !ISWALKABLEPLANE( &predictionResults.trace->plane ) ) {
			continue;
		}

		if( predictionResults.trace->contents & ( CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_WATER | CONTENTS_NODROP ) ) {
			continue;
		}

		const int landingArea = predictionResults.lastAreaNum;
		int travelTimeFromLandingArea = routeCache->FastestRouteToGoalArea( landingArea, navTargetAreaNum );

		// Note: thats why we are using best travel time among allowed and preferred travel flags
		// (there is a suspicion that many feasible areas might be cut off by the following test otherwise).
		// If the travel time is significantly worse than travel time from reach area
		if( !travelTimeFromLandingArea || travelTimeFromLandingArea - 20 > travelTimeFromReachArea ) {
			continue;
		}

		VectorCopy( predictionResults.origin, jumpTarget );
		jumpTargetAreaNum = landingArea;
		break;
	}

	// All attempts have failed
	if( i == numAttempts ) {
		return nullptr;
	}

	jumpTarget[2] += 1.0f - playerbox_stand_mins[2] + self->viewheight;
	auto *fallback = &self->ai->botRef->jumpToSpotMovementFallback;
	fallback->Activate( entityPhysicsState.Origin(), jumpTarget, predictionResults.millisAhead,
						32.0f, startAirAccelFracs[i], endAirAccelFracs[i], attemptsZBoosts[i] );
	return fallback;
}

static const uint16_t *TryFindBestStairsExitArea( BotMovementPredictionContext *context, int stairsClusterNum ) {
	const int toAreaNum = context->NavTargetAasAreaNum();
	if( !toAreaNum ) {
		return nullptr;
	}

	const int currTravelTimeToTarget = context->TravelTimeToNavTarget();
	if( !currTravelTimeToTarget ) {
		return nullptr;
	}

	const auto *aasWorld = AiAasWorld::Instance();
	const auto *routeCache = context->RouteCache();
	const auto &travelFlags = context->TravelFlags();

	const uint16_t *stairsClusterAreaNums = aasWorld->StairsClusterData( stairsClusterNum ) + 1;
	int numAreasInStairsCluster = stairsClusterAreaNums[-1];

	// TODO: Support curved stairs, here and from StairsClusterBuilder side

	// Determine whether highest or lowest area is closer to the nav target
	const uint16_t *stairsBoundaryAreas[2];
	stairsBoundaryAreas[0] = &stairsClusterAreaNums[0];
	stairsBoundaryAreas[1] = &stairsClusterAreaNums[numAreasInStairsCluster - 1];

	int bestStairsAreaIndex = -1;
	int bestTravelTimeOfStairsAreas = std::numeric_limits<int>::max();
	for( int i = 0; i < 2; ++i ) {
		int bestAreaTravelTime = std::numeric_limits<int>::max();
		for( int flags: travelFlags ) {
			int travelTime = routeCache->TravelTimeToGoalArea( *stairsBoundaryAreas[i], toAreaNum, flags );
			if( travelTime && travelTime < bestAreaTravelTime ) {
				bestAreaTravelTime = travelTime;
			}
		}
		// The stairs boundary area is not reachable
		if( bestAreaTravelTime == std::numeric_limits<int>::max() ) {
			return nullptr;
		}
		// Make sure a stairs area is closer to the nav target than the current one
		if( bestAreaTravelTime < currTravelTimeToTarget ) {
			if( bestAreaTravelTime < bestTravelTimeOfStairsAreas ) {
				bestTravelTimeOfStairsAreas = bestAreaTravelTime;
				bestStairsAreaIndex = i;
			}
		}
	}

	if( bestStairsAreaIndex < 0 ) {
		return nullptr;
	}

	// The value points to the cluster data that is persistent in memory
	// during the entire match, so returning this address is legal.
	return stairsBoundaryAreas[bestStairsAreaIndex];
}

BotMovementFallback *BotDummyMovementAction::TryFindStairsFallback( BotMovementPredictionContext *context ) {
	const auto *aasWorld = AiAasWorld::Instance();

	int currAreaNums[2] = { 0, 0 };
	const int numCurrAreas = context->movementState->entityPhysicsState.PrepareRoutingStartAreas( currAreaNums );

	int stairsClusterNum = 0;
	for( int i = 0; i < numCurrAreas; ++i ) {
		if( ( stairsClusterNum = aasWorld->StairsClusterNum( currAreaNums[i] ))) {
			break;
		}
	}

	if( !stairsClusterNum ) {
		return nullptr;
	}

	const auto *bestAreaNum = TryFindBestStairsExitArea( context, stairsClusterNum );
	if( !bestAreaNum ) {
		return nullptr;
	}

	// Note: Don't try to apply jumping shortcut, results are very poor.

	auto *fallback = &self->ai->botRef->useStairsExitMovementFallback;
	fallback->Activate( stairsClusterNum, *bestAreaNum );
	return fallback;
}

static const int *TryFindBestInclinedFloorExitArea( BotMovementPredictionContext *context,
													int rampAreaNum,
													int forbiddenAreaNum = 0 ) {
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const auto *aasReach = aasWorld->Reachabilities();

	// Find ramp start and end flat grounded areas

	int lowestAreaNum = 0;
	int lowestReachNum = 0;
	float lowestAreaHeight = std::numeric_limits<float>::max();
	int highestAreaNum = 0;
	int highestReachNum = 0;
	float highestAreaHeight = std::numeric_limits<float>::min();

	const auto &rampAreaSettings = aasAreaSettings[rampAreaNum];
	int reachNum = rampAreaSettings.firstreachablearea;
	const int endReachNum = reachNum + rampAreaSettings.numreachableareas;
	for(; reachNum != endReachNum; ++reachNum) {
		const auto &reach = aasReach[reachNum];
		if( reach.traveltype != TRAVEL_WALK ) {
			continue;
		}
		const int reachAreaNum = reach.areanum;
		if( reach.areanum == forbiddenAreaNum ) {
			continue;
		}

		const auto &reachAreaFlags = aasAreaSettings[reachAreaNum].areaflags;
		if( !( reachAreaFlags & AREA_GROUNDED ) ) {
			continue;
		}

		const auto &reachArea = aasAreas[reachAreaNum];
		if( reachArea.mins[2] < lowestAreaHeight ) {
			lowestAreaHeight = reachArea.mins[2];
			lowestAreaNum = reachAreaNum;
			lowestReachNum = reachNum;
		}
		if( reachArea.mins[2] > highestAreaHeight ) {
			highestAreaHeight = reachArea.mins[2];
			highestAreaNum = reachAreaNum;
			highestReachNum = reachNum;
		}
	}

	if( !lowestAreaNum || !highestAreaNum ) {
		return nullptr;
	}

	// Note: The comparison operator has been changed from >= to >
	// since adjacent ramp areas are likely to have the same bounding box height dimensions
	if( lowestAreaHeight > highestAreaHeight ) {
		return nullptr;
	}

	const int travelTimeToTarget = context->TravelTimeToNavTarget();
	if( !travelTimeToTarget ) {
		return nullptr;
	}

	// Find what area is closer to the nav target
	int fromAreaNums[2] = { lowestAreaNum, highestAreaNum };
	int fromReachNums[2] = { lowestReachNum, highestReachNum };
	int toAreaNum = context->NavTargetAasAreaNum();
	int bestIndex = -1;
	int bestTravelTime = std::numeric_limits<int>::max();
	const auto *routeCache = context->RouteCache();
	const auto &travelFlags = context->TravelFlags();
	for( int i = 0; i < 2; ++i ) {
		for( int flags: travelFlags ) {
			int travelTime = routeCache->TravelTimeToGoalArea( fromAreaNums[i], toAreaNum, flags );
			if( travelTime && travelTime < travelTimeToTarget && travelTime < bestTravelTime ) {
				bestIndex = i;
				bestTravelTime = travelTime;
			}
		}
	}

	if( bestIndex < 0 ) {
		return nullptr;
	}

	// Return a pointer to a persistent during the match memory
	return &aasReach[fromReachNums[bestIndex]].areanum;
}

bool BotDummyMovementAction::TrySetupInclinedFloorMovement( BotMovementPredictionContext *context, int rampAreaNum ) {
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasAreas = aasWorld->Areas();
	const auto *bestAreaNum = TryFindBestInclinedFloorExitArea( context, rampAreaNum );
	if( !bestAreaNum ) {
		return false;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	Vec3 intendedLookDir( aasAreas[*bestAreaNum].center );
	intendedLookDir -= entityPhysicsState.Origin();
	intendedLookDir.NormalizeFast();

	context->record->botInput.SetIntendedLookDir( intendedLookDir, true );
	float dot = intendedLookDir.Dot( entityPhysicsState.ForwardDir() );
	if( dot > 0.5f ) {
		context->record->botInput.SetForwardMovement( 1 );
		if( dot > 0.9f && entityPhysicsState.Speed2D() < context->GetDashSpeed() - 10 ) {
			const auto *stats = context->currPlayerState->pmove.stats;
			if( ( stats[PM_STAT_FEATURES] & PMFEAT_DASH ) && !stats[PM_STAT_DASHTIME] ) {
				context->record->botInput.SetSpecialButton( true );
			}
		}
	}

	return true;
}

BotMovementFallback *BotDummyMovementAction::TryFindRampFallback( BotMovementPredictionContext *context,
																  int rampAreaNum, int forbiddenAreaNum ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const int *bestExitAreaNum = TryFindBestInclinedFloorExitArea( context, rampAreaNum, forbiddenAreaNum );
	if( !bestExitAreaNum ) {
		return nullptr;
	}

	const auto *aasWorld = AiAasWorld::Instance();
	const auto &exitArea = aasWorld->Areas()[*bestExitAreaNum];

	Vec3 areaPoint( exitArea.center );
	areaPoint.Z() = exitArea.mins[2] + 1.0f - playerbox_stand_mins[2];

	bool tryJumpShortcut = false;
	// Try jumping below
	if( exitArea.mins[2] < entityPhysicsState.Origin()[2] ) {
		tryJumpShortcut = true;
	} else {
		// Dont try jumping if a bot can slide on an ramp
		// Check whether a current area is actually slidable (and not just has an inclided floor)
		if( aasWorld->AreaSettings()[rampAreaNum].areaflags & AREA_SLIDABLE_RAMP ) {
			// Don't try jumping to a far exit area that is higher than the bot, keep sliding on a ramp.
			if( exitArea.mins[2] > entityPhysicsState.Origin()[2] ) {
				if( areaPoint.SquareDistanceTo( entityPhysicsState.Origin() ) < SQUARE( 96.0f ) ) {
					tryJumpShortcut = true;
				}
			}
		}
	}

	if( tryJumpShortcut ) {
		if( auto *fallback = TryShortcutOtherFallbackByJumping( context, areaPoint.Data(), *bestExitAreaNum ) ) {
			return fallback;
		}
	}

	auto *fallback = &self->ai->botRef->useRampExitMovementFallback;
	fallback->Activate( rampAreaNum, *bestExitAreaNum );
	return fallback;
}

BotMovementFallback *BotDummyMovementAction::TryFindNearbyRampAreasFallback( BotMovementPredictionContext *context ) {
	int currGroundedAreaNum = context->CurrGroundedAasAreaNum();
	if( !currGroundedAreaNum ) {
		return nullptr;
	}

	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const auto *aasReach = aasWorld->Reachabilities();

	const auto &currAreaSettings = aasAreaSettings[currGroundedAreaNum];
	int reachNum = currAreaSettings.firstreachablearea;
	const int endReachNum = reachNum + currAreaSettings.numreachableareas;
	for(; reachNum != endReachNum; reachNum++ ) {
		const auto reach = aasReach[reachNum];
		if( reach.traveltype != TRAVEL_WALK ) {
			continue;
		}
		int reachAreaNum = reach.areanum;
		if( !( aasAreaSettings[reachAreaNum].areaflags & AREA_INCLINED_FLOOR ) ) {
			continue;
		}

		// Set the current grounded area num as a forbidden to avoid looping
		if( const int *areaNum = TryFindBestInclinedFloorExitArea( context, reachAreaNum, currGroundedAreaNum ) ) {
			const auto &bestArea = aasWorld->Areas()[*areaNum];
			Vec3 areaPoint( bestArea.center );
			areaPoint.Z() = bestArea.mins[2] + 1.0f + -playerbox_stand_mins[2];
			auto *fallback = &self->ai->botRef->useWalkableNodeMovementFallback;
			fallback->Activate( areaPoint.Data(), 32.0f, *areaNum );
			return fallback;
		}
	}

	return nullptr;
}

typedef TacticalSpotsRegistry::OriginParams OriginParams;

bool BotGenericGroundMovementFallback::TryDeactivate( BotMovementPredictionContext *context ) {
	// This code is useful for all children and should be called first in all overridden implementations

	// Deactivate the action with success if the bot has touched a trigger
	// (was the bot targeting at it does not matter)

	const auto *bot = self->ai->botRef;
	for( auto touchTime: { bot->lastTouchedTeleportAt, bot->lastTouchedJumppadAt, bot->lastTouchedElevatorAt } ) {
		if( level.time - touchTime < 64 ) {
			// Consider the completion successful
			status = COMPLETED;
			return true;
		}
	}

	if( level.time - bot->lastKnockbackAt < 64 ) {
		// Consider the action failed
		status = INVALID;
		return true;
	}

	return false;
}

inline bool BotGenericGroundMovementFallback::ShouldSkipTests( BotMovementPredictionContext *context ) {
	if( context ) {
		return !context->movementState->entityPhysicsState.GroundEntity();
	}

	return !self->groundentity;
}

inline int BotGenericGroundMovementFallback::GetCurrBotAreas( int *areaNums, BotMovementPredictionContext *context ) {
	if( context ) {
		return context->movementState->entityPhysicsState.PrepareRoutingStartAreas( areaNums );
	}

	return self->ai->botRef->EntityPhysicsState()->PrepareRoutingStartAreas( areaNums );
}

void BotGenericGroundMovementFallback::SetupMovement( BotMovementPredictionContext *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	auto *botInput = &context->record->botInput;
	const auto &miscTactics = self->ai->botRef->GetMiscTactics();
	const auto *pmStats = context->currPlayerState->pmove.stats;

	vec3_t steeringTarget;
	// Call an overridden by a child method
	this->GetSteeringTarget( steeringTarget );
#if 0
	AITools_DrawColorLine( entityPhysicsState.Origin(), steeringTarget, DebugColor(), 0 );
#endif

	Vec3 intendedLookDir( steeringTarget );
	intendedLookDir -= entityPhysicsState.Origin();

	const float squareDistanceToTarget = intendedLookDir.SquaredLength();
	intendedLookDir.Z() *= Z_NO_BEND_SCALE;
	intendedLookDir.Normalize();

	botInput->SetIntendedLookDir( intendedLookDir, true );

	// Set 1.0f as a default value to prevent blocking in some cases
	float intendedDotActual = 1.0f;
	// We should operate on vectors in 2D plane, otherwise we get dot product match rather selfdom.
	Vec3 intendedLookDir2D( intendedLookDir.X(), intendedLookDir.Y(), 0.0f );
	if( intendedLookDir2D.SquaredLength() > 0.001f ) {
		intendedLookDir2D.Normalize();
		Vec3 forward2DDir( entityPhysicsState.ForwardDir() );
		forward2DDir.Z() = 0;
		if( forward2DDir.SquaredLength() > 0.001f ) {
			forward2DDir.Normalize();
			intendedDotActual = intendedLookDir2D.Dot( forward2DDir );
		}
	}

	if( !entityPhysicsState.GroundEntity() ) {
		if( intendedDotActual > airAccelDotProductToTargetThreshold ) {
			if( allowAirAccel && squareDistanceToTarget > SQUARE( airAccelDistanceToTargetThreshold ) ) {
				if( context->CanSafelyKeepHighSpeed() ) {
					context->CheatingAccelerate( 0.5f );
				}
			}
			return;
		} else if( intendedDotActual < 0 ) {
			return;
		}

		if( !( pmStats[PM_STAT_FEATURES] & PMFEAT_AIRCONTROL ) ) {
			return;
		}

		context->CheatingCorrectVelocity( steeringTarget );
		return;
	}

	if( intendedDotActual < 0 ) {
		botInput->SetForwardMovement( 0 );
		botInput->SetTurnSpeedMultiplier( 5.0f );
		return;
	}

	botInput->SetForwardMovement( 1 );
	botInput->SetWalkButton( true );
	if( allowRunning ) {
		if( intendedDotActual > runDotProductToTargetThreshold ) {
			if( squareDistanceToTarget > SQUARE( runDistanceToTargetThreshold ) ) {
				botInput->SetWalkButton( false );
			}
		}
	}

	if( allowCrouchSliding ) {
		if( ShouldCrouchSlideNow( context ) || ShouldPrepareForCrouchSliding( context ) ) {
			botInput->SetUpMovement( -1 );
			return;
		}
	}

	// Try setting a forward dash, the only kind of non-ground movement allowed in this sub-action
	if( miscTactics.shouldBeSilent || miscTactics.shouldMoveCarefully ) {
		return;
	}

	if( !allowDashing ) {
		return;
	}

	if( squareDistanceToTarget < SQUARE( dashDistanceToTargetThreshold ) ) {
		return;
	}

	if( !( pmStats[PM_STAT_FEATURES] & PMFEAT_DASH ) ) {
		return;
	}

	botInput->SetSpecialButton( true );
	return;
}

const edict_t *BotDummyMovementAction::FindClosestToTargetTrigger( BotMovementPredictionContext *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	int fromAreaNums[2];
	int numFromAreas = entityPhysicsState.PrepareRoutingStartAreas( fromAreaNums );
	int goalAreaNum = context->NavTargetAasAreaNum();
	ClosestTriggerProblemParams problemParams( entityPhysicsState.Origin(), fromAreaNums, numFromAreas, goalAreaNum );
	// self->r.abs* values may be shifted having values remaining from a failed prediction step
	Vec3 absMins( problemParams.Origin() );
	Vec3 absMaxs( problemParams.Origin() );
	absMins += playerbox_stand_mins;
	absMaxs += playerbox_stand_maxs;
	context->nearbyTriggersCache.EnsureValidForBounds( absMins.Data(), absMaxs.Data() );
	return FindClosestToTargetTrigger( problemParams, context->nearbyTriggersCache );
}

typedef BotMovementPredictionContext::NearbyTriggersCache NearbyTriggersCache;

const edict_t *BotDummyMovementAction::FindClosestToTargetTrigger( const ClosestTriggerProblemParams &problemParams,
																   const NearbyTriggersCache &triggersCache ) {
	const int allowedTravelFlags = self->ai->botRef->AllowedTravelFlags();

	const auto triggerTravelFlags = &triggersCache.triggerTravelFlags[0];
	const auto triggerEntNums = &triggersCache.triggerEntNums[0];
	const auto triggerNumEnts = &triggersCache.triggerNumEnts[0];

	const edict_t *bestEnt = nullptr;
	int bestTravelTime = std::numeric_limits<int>::max();
	for( int i = 0; i < 3; ++i ) {
		if( allowedTravelFlags & triggerTravelFlags[i] ) {
			int travelTime;
			const edict_t *ent = FindClosestToTargetTrigger( problemParams, triggerEntNums[i], *triggerNumEnts[i], &travelTime );
			if( travelTime && travelTime < bestTravelTime ) {
				bestEnt = ent;
			}
		}
	}

	return bestTravelTime < std::numeric_limits<int>::max() ? bestEnt : nullptr;
}

class TriggerAreaNumsCache {
	mutable int areaNums[MAX_EDICTS];
public:
	TriggerAreaNumsCache() {
		memset( areaNums, 0, sizeof( areaNums ) );
	}

	inline int GetAreaNum( int entNum ) const;
};

inline int TriggerAreaNumsCache::GetAreaNum( int entNum ) const {
	int *const areaNumRef = &areaNums[entNum];
	// Put the likely case first
	if( *areaNumRef ) {
		return *areaNumRef;
	}

	// Find an area that has suitable flags matching the trigger type
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const auto *aiManager = AiManager::Instance();

	int desiredAreaContents = ~0;
	const edict_t *ent = game.edicts + entNum;
	if( ent->classname ) {
		if( !Q_stricmp( ent->classname, "trigger_push" ) ) {
			desiredAreaContents = AREACONTENTS_JUMPPAD;
		} else if( !Q_stricmp( ent->classname, "trigger_teleport" ) ) {
			desiredAreaContents = AREACONTENTS_TELEPORTER;
		}
	}

	int boxAreaNums[32];
	int numBoxAreas = aasWorld->BBoxAreas( ent->r.absmin, ent->r.absmax, boxAreaNums, 32 );
	for( int i = 0; i < numBoxAreas; ++i ) {
		int areaNum = boxAreaNums[i];
		if( aasAreaSettings[areaNum].contents & desiredAreaContents ) {
			if( aiManager->IsAreaReachableFromHubAreas( areaNum ) ) {
				*areaNumRef = areaNum;
				break;
			}
		}
	}

	return *areaNumRef;
}

static TriggerAreaNumsCache triggerAreaNumsCache;

const edict_t *BotDummyMovementAction::FindClosestToTargetTrigger( const ClosestTriggerProblemParams &problemParams,
																   const uint16_t *triggerEntNums,
																   int numTriggerEnts, int *travelTime ) {
	float *origin = const_cast<float *>( problemParams.Origin() );
	const int *fromAreaNums = problemParams.FromAreaNums();
	const int numFromAreas = problemParams.numFromAreas;
	const int toAreaNum = problemParams.goalAreaNum;
	const edict_t *gameEdicts = game.edicts;
	const auto *routeCache = self->ai->botRef->routeCache;

	int bestTravelTimeFromTrigger = std::numeric_limits<int>::max();
	int bestTriggerIndex = -1;
	int bestTriggerAreaNum = 0;

	trace_t trace;
	vec3_t traceMins, traceMaxs;
	TacticalSpotsRegistry::GetSpotsWalkabilityTraceBounds( traceMins, traceMaxs );

	for( int i = 0; i < numTriggerEnts; ++i ) {
		const edict_t *ent = gameEdicts + triggerEntNums[i];

		// Check whether the trigger is reachable by walking in 2 seconds (200 AAS time units)
		const int entAreaNum = triggerAreaNumsCache.GetAreaNum( triggerEntNums[i] );

		int travelTimeToTrigger = 0;
		for( int j = 0; j < numFromAreas; ++j ) {
			const auto travelFlags = BotGenericGroundMovementFallback::TRAVEL_FLAGS;
			travelTimeToTrigger = routeCache->TravelTimeToGoalArea( fromAreaNums[j], entAreaNum, travelFlags );
			if( travelTimeToTrigger && travelTimeToTrigger < 200 ) {
				break;
			}
			travelTimeToTrigger = 0;
		}

		if( !travelTimeToTrigger ) {
			continue;
		}

		// Find a travel time from trigger for regular bot movement

		const int travelTimeFromTrigger = routeCache->FastestRouteToGoalArea( entAreaNum, toAreaNum );
		if( !travelTimeFromTrigger || travelTimeFromTrigger >= bestTravelTimeFromTrigger ) {
			continue;
		}

		// All trigger seem to have absent s.origin, but the precomputed area is valid.
		// AiAasWorld::FindAreaNum() hides this issue by testing entity bounds too and producing a feasible result
		Vec3 entOrigin( ent->r.absmin );
		entOrigin += ent->r.absmax;
		entOrigin *= 0.5f;

		// We have to test against entities and not only solid world
		// since this is a fallback action and any failure is critical
		G_Trace( &trace, origin, traceMins, traceMaxs, entOrigin.Data(), self, MASK_PLAYERSOLID | CONTENTS_TRIGGER );
		// We might hit a solid world aiming at the trigger origin.
		// Check hit distance too, not only fraction for equality to 1.0f.
		if( trace.fraction != 1.0f && trace.ent != triggerEntNums[i] ) {
			continue;
		}

		bestTriggerIndex = i;
		bestTravelTimeFromTrigger = travelTimeFromTrigger;
		bestTriggerAreaNum = entAreaNum;
	}

	if( bestTriggerIndex >= 0 ) {
		*travelTime = bestTravelTimeFromTrigger;
		return gameEdicts + triggerEntNums[bestTriggerIndex];
	}

	return nullptr;
}

void BotRidePlatformMovementAction::PlanPredictionStep( BotMovementPredictionContext *context ) {
	if( !GenericCheckIsActionEnabled( context, &DefaultWalkAction() ) ) {
		return;
	}

	const edict_t *platform = GetPlatform( context );
	if( !platform ) {
		context->cannotApplyAction = true;
		context->actionSuggestedByAction = &DefaultWalkAction();
		Debug( "Cannot apply the action (cannot find a platform below)\n" );
		return;
	}

	if( platform->moveinfo.state == STATE_TOP ) {
		SetupExitPlatformMovement( context, platform );
	} else {
		SetupIdleRidingPlatformMovement( context, platform );
	}
}

void BotRidePlatformMovementAction::CheckPredictionStepResults( BotMovementPredictionContext *context ) {
	BotBaseMovementAction::CheckPredictionStepResults( context );
	if( context->cannotApplyAction || context->isCompleted ) {
		return;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const int targetAreaNum = self->ai->botRef->savedPlatformAreas[currTestedAreaIndex];
	const int currAreaNum = entityPhysicsState.CurrAasAreaNum();
	const int droppedToFloorAreaNum = entityPhysicsState.DroppedToFloorAasAreaNum();
	if( currAreaNum == targetAreaNum || droppedToFloorAreaNum == targetAreaNum ) {
		Debug( "The bot has entered the target exit area, should stop planning\n" );
		context->isCompleted = true;
		return;
	}

	const unsigned sequenceDuration = SequenceDuration( context );
	if( sequenceDuration < 250 ) {
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	if( originAtSequenceStart.SquareDistance2DTo( entityPhysicsState.Origin() ) < 32 * 32 ) {
		Debug( "The bot is likely stuck trying to use area #%d to exit the platform\n", currTestedAreaIndex );
		context->SetPendingRollback();
		return;
	}

	if( sequenceDuration > 550 ) {
		Debug( "The bot still has not reached the target exit area\n" );
		context->SetPendingRollback();
		return;
	}

	context->SaveSuggestedActionForNextFrame( this );
}

void BotRidePlatformMovementAction::OnApplicationSequenceStopped( BotMovementPredictionContext *context,
																  SequenceStopReason stopReason,
																  unsigned stoppedAtFrameIndex ) {
	BotBaseMovementAction::OnApplicationSequenceStopped( context, stopReason, stoppedAtFrameIndex );
	if( stopReason != FAILED && stopReason != DISABLED ) {
		return;
	}

	currTestedAreaIndex++;
}

void BotRidePlatformMovementAction::SetupIdleRidingPlatformMovement( BotMovementPredictionContext *context,
																	 const edict_t *platform ) {
	if( self->ai->botRef->savedPlatformAreas.empty() && context->NavTargetAasAreaNum() ) {
		TrySaveExitAreas( context, platform );
	}

	auto *botInput = &context->record->botInput;
	if( self->ai->botRef->HasEnemy() ) {
		Vec3 toEnemy( self->ai->botRef->EnemyOrigin() );
		toEnemy -= context->movementState->entityPhysicsState.Origin();
		botInput->SetIntendedLookDir( toEnemy, false );
	} else {
		float height = platform->moveinfo.start_origin[2] - platform->moveinfo.end_origin[2];
		float frac = ( platform->s.origin[2] - platform->moveinfo.end_origin[2] ) / height;
		if( frac > 0.5f && !self->ai->botRef->savedPlatformAreas.empty() ) {
			const auto &area = AiAasWorld::Instance()->Areas()[self->ai->botRef->savedPlatformAreas.front()];
			Vec3 lookVec( area.center );
			lookVec -= context->movementState->entityPhysicsState.Origin();
			botInput->SetIntendedLookDir( lookVec, false );
		} else {
			Vec3 lookVec( context->movementState->entityPhysicsState.ForwardDir() );
			lookVec.Z() = 0.5f - 1.0f * frac;
			botInput->SetIntendedLookDir( lookVec, false );
		}
	}

	botInput->isUcmdSet = true;
	botInput->canOverrideUcmd = true;

	Debug( "Stand idle on the platform, do not plan ahead\n" );
	context->isCompleted = true;
}

void BotRidePlatformMovementAction::SetupExitPlatformMovement( BotMovementPredictionContext *context,
															   const edict_t *platform ) {
	const ExitAreasVector &suggestedAreas = SuggestExitAreas( context, platform );
	if( suggestedAreas.empty() ) {
		Debug( "Warning: there is no platform exit areas, do not plan ahead\n" );
		this->isDisabledForPlanning = true;
		context->SetPendingRollback();
		return;
	}

	if( currTestedAreaIndex >= suggestedAreas.size() ) {
		Debug( "All suggested exit area tests have failed\n" );
		this->isDisabledForPlanning = true;
		context->SetPendingRollback();
		return;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	auto *botInput = &context->record->botInput;

	const auto &area = AiAasWorld::Instance()->Areas()[suggestedAreas[currTestedAreaIndex]];
	Vec3 intendedLookDir( area.center );
	intendedLookDir.Z() = area.mins[2] + 32;
	intendedLookDir -= entityPhysicsState.Origin();
	float distance = intendedLookDir.NormalizeFast();
	botInput->SetIntendedLookDir( intendedLookDir, true );

	botInput->isUcmdSet = true;
	float dot = intendedLookDir.Dot( entityPhysicsState.ForwardDir() );
	if( dot < 0.0f ) {
		botInput->SetTurnSpeedMultiplier( 3.0f );
		return;
	}

	if( dot < 0.7f ) {
		botInput->SetWalkButton( true );
		return;
	}

	if( distance > 64.0f ) {
		botInput->SetSpecialButton( true );
	}
}

const edict_t *BotRidePlatformMovementAction::GetPlatform( BotMovementPredictionContext *context ) const {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const edict_t *groundEntity = entityPhysicsState.GroundEntity();
	if( groundEntity ) {
		return groundEntity->use == Use_Plat ? groundEntity : nullptr;
	}

	trace_t trace;
	Vec3 startPoint( entityPhysicsState.Origin() );
	startPoint.Z() += playerbox_stand_mins[2];
	Vec3 endPoint( entityPhysicsState.Origin() );
	endPoint.Z() += playerbox_stand_mins[2];
	endPoint.Z() -= 32.0f;
	G_Trace( &trace, startPoint.Data(), playerbox_stand_mins, playerbox_stand_maxs, endPoint.Data(), self, MASK_ALL );
	if( trace.ent != -1 ) {
		groundEntity = game.edicts + trace.ent;
		if( groundEntity->use == Use_Plat ) {
			return groundEntity;
		}
	}

	return nullptr;
}

void BotRidePlatformMovementAction::TrySaveExitAreas( BotMovementPredictionContext *context, const edict_t *platform ) {
	auto &savedAreas = self->ai->botRef->savedPlatformAreas;
	savedAreas.clear();

	int navTargetAreaNum = self->ai->botRef->NavTargetAasAreaNum();
	Assert( navTargetAreaNum );

	FindExitAreas( context, platform, tmpExitAreas );

	const auto *routeCache = self->ai->botRef->routeCache;
	const int travelFlags = self->ai->botRef->AllowedTravelFlags();

	int areaTravelTimes[MAX_SAVED_AREAS];
	for( unsigned i = 0, end = tmpExitAreas.size(); i < end; ++i ) {
		int travelTime = routeCache->TravelTimeToGoalArea( tmpExitAreas[i], navTargetAreaNum, travelFlags );
		if( !travelTime ) {
			continue;
		}

		savedAreas.push_back( tmpExitAreas[i] );
		areaTravelTimes[i] = travelTime;
	}

	// Sort areas
	for( unsigned i = 1, end = savedAreas.size(); i < end; ++i ) {
		int area = savedAreas[i];
		int travelTime = areaTravelTimes[i];
		int j = i - 1;
		for(; j >= 0 && areaTravelTimes[j] < travelTime; j-- ) {
			savedAreas[j + 1] = savedAreas[j];
			areaTravelTimes[j + 1] = areaTravelTimes[j];
		}

		savedAreas[j + 1] = area;
		areaTravelTimes[j + 1] = travelTime;
	}
}

typedef BotRidePlatformMovementAction::ExitAreasVector ExitAreasVector;

const ExitAreasVector &BotRidePlatformMovementAction::SuggestExitAreas( BotMovementPredictionContext *context,
																		const edict_t *platform ) {
	if( !self->ai->botRef->savedPlatformAreas.empty() ) {
		return self->ai->botRef->savedPlatformAreas;
	}

	FindExitAreas( context, platform, tmpExitAreas );

	// Save found areas to avoid repeated FindExitAreas() calls while testing next area after rollback
	for( int areaNum: tmpExitAreas )
		self->ai->botRef->savedPlatformAreas.push_back( areaNum );

	return tmpExitAreas;
};

void BotRidePlatformMovementAction::FindExitAreas( BotMovementPredictionContext *context,
												   const edict_t *platform,
												   ExitAreasVector &exitAreas ) {
	const auto &aasWorld = AiAasWorld::Instance();
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasAreaSettings = aasWorld->AreaSettings();

	const BotMovementState &movementState = context ? *context->movementState : self->ai->botRef->movementState;

	exitAreas.clear();

	Vec3 mins( platform->r.absmin );
	Vec3 maxs( platform->r.absmax );
	// SP_func_plat(): start is the top position, end is the bottom
	mins.Z() = platform->moveinfo.start_origin[2];
	maxs.Z() = platform->moveinfo.start_origin[2] + 96.0f;
	// We have to extend bounds... check whether wdm9 movement is OK if one changes these values.
	for( int i = 0; i < 2; ++i ) {
		mins.Data()[i] -= 96.0f;
		maxs.Data()[i] += 96.0f;
	}

	Vec3 finalBotOrigin( movementState.entityPhysicsState.Origin() );
	// Add an extra 1 unit offset for tracing purposes
	finalBotOrigin.Z() += platform->moveinfo.start_origin[2] - platform->s.origin[2] + 1.0f;

	trace_t trace;
	int bboxAreaNums[48];
	const int numBBoxAreas = aasWorld->BBoxAreas( mins, maxs, bboxAreaNums, 48 );
	for( int i = 0; i < numBBoxAreas; ++i ) {
		const int areaNum = bboxAreaNums[i];
		const auto &area = aasAreas[areaNum];
		const auto &areaSettings = aasAreaSettings[areaNum];
		if( !( areaSettings.areaflags & AREA_GROUNDED ) ) {
			continue;
		}
		if( areaSettings.areaflags & ( AREA_JUNK | AREA_DISABLED ) ) {
			continue;
		}
		if( areaSettings.contents & ( AREACONTENTS_DONOTENTER | AREACONTENTS_MOVER ) ) {
			continue;
		}

		Vec3 areaPoint( area.center );
		areaPoint.Z() = area.mins[2] + 8.0f;

		// Do not use player box as trace mins/maxs (it leads to blocking when a bot rides a platform near a wall)
		G_Trace( &trace, finalBotOrigin.Data(), nullptr, nullptr, areaPoint.Data(), self, MASK_ALL );
		if( trace.fraction != 1.0f ) {
			continue;
		}

		exitAreas.push_back( areaNum );
		if( exitAreas.size() == exitAreas.capacity() ) {
			break;
		}
	}
}

void BotSwimMovementAction::PlanPredictionStep( BotMovementPredictionContext *context ) {
	if( !GenericCheckIsActionEnabled( context ) ) {
		return;
	}

	int nextReachNum = context->NextReachNum();
	if( !nextReachNum ) {
		context->cannotApplyAction = true;
		Debug( "Cannot apply action: next reachability is undefined in the given context state\n" );
		return;
	}

	context->SetDefaultBotInput();
	context->record->botInput.canOverrideLookVec = true;
	context->record->botInput.SetForwardMovement( 1 );
	context->TryAvoidFullHeightObstacles( 0.3f );

	const auto &nextReach = AiAasWorld::Instance()->Reachabilities()[nextReachNum];
	if( nextReach.traveltype == TRAVEL_SWIM ) {
		return;
	}

	if( DistanceSquared( nextReach.start, context->movementState->entityPhysicsState.Origin() ) > 24 * 24 ) {
		return;
	}

	// Exit water (might it be above a regular next area? this case is handled by the condition)
	if( nextReach.start[2] < nextReach.end[2] ) {
		context->record->botInput.SetUpMovement( 1 );
	} else {
		context->record->botInput.SetUpMovement( -1 );
	}
}

void BotSwimMovementAction::CheckPredictionStepResults( BotMovementPredictionContext *context ) {
	BotBaseMovementAction::CheckPredictionStepResults( context );
	if( context->cannotApplyAction || context->isCompleted ) {
		return;
	}

	const auto &oldPhysicsState = context->PhysicsStateBeforeStep();
	const auto &newPhysicsState = context->movementState->entityPhysicsState;

	Assert( oldPhysicsState.waterLevel > 1 );
	if( newPhysicsState.waterLevel < 2 ) {
		context->isCompleted = true;
		Debug( "A movement step has lead to exiting water, should stop planning\n" );
		return;
	}
}

void BotFlyUntilLandingMovementAction::PlanPredictionStep( BotMovementPredictionContext *context ) {
	if( !GenericCheckIsActionEnabled( context ) ) {
		return;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( entityPhysicsState.GroundEntity() ) {
		context->cannotApplyAction = true;
		context->actionSuggestedByAction = &DummyAction();
		Debug( "A bot has landed on a ground in the given context state\n" );
		return;
	}

	auto *flyUntilLandingMovementState = &context->movementState->flyUntilLandingMovementState;
	if( flyUntilLandingMovementState->CheckForLanding( context ) ) {
		context->cannotApplyAction = true;
		context->actionSuggestedByAction = &LandOnSavedAreasAction();
		Debug( "Bot should perform landing in the given context state\n" );
		return;
	}

	auto *botInput = &context->record->botInput;
	// Relax all keys
	botInput->ClearMovementDirections();
	botInput->isUcmdSet = true;
	// Look at the target (do not keep the angles from the flight beginning,
	// in worst case a bot is unable to turn quickly if the landing site is in opposite direction)
	Vec3 intendedLookVec( flyUntilLandingMovementState->Target() );
	intendedLookVec -= entityPhysicsState.Origin();
	botInput->SetIntendedLookDir( intendedLookVec, false );
	botInput->SetTurnSpeedMultiplier( 1.5f );
	botInput->canOverrideLookVec = true;
	Debug( "Planning is completed (the action should never be predicted ahead\n" );
	context->isCompleted = true;
}

void Bot::MovementFrame( BotInput *input ) {
	this->movementState.Frame( game.frametime );
	this->movementState.TryDeactivateContainedStates( self, nullptr );

	if( auto *fallback = self->ai->botRef->activeMovementFallback ) {
		if( fallback->TryDeactivate( nullptr ) ) {
			self->ai->botRef->activeMovementFallback = nullptr;
		}
	}

	BotMovementActionRecord movementActionRecord;
	BotBaseMovementAction *movementAction = movementPredictionContext.GetActionAndRecordForCurrTime( &movementActionRecord );

	movementAction->ExecActionRecord( &movementActionRecord, input, nullptr );

	roamingManager.CheckSpotsProximity();
	CheckTargetProximity();
	CheckGroundPlatform();
}

void Bot::CheckGroundPlatform() {
	if( !self->groundentity ) {
		return;
	}

	// Reset saved platform areas after touching a solid world ground
	if( self->groundentity == world ) {
		self->ai->botRef->savedPlatformAreas.clear();
		return;
	}

	if( self->groundentity->use != Use_Plat ) {
		return;
	}

	if( self->groundentity->moveinfo.state != STATE_BOTTOM ) {
		return;
	}

	self->ai->botRef->ridePlatformMovementAction.TrySaveExitAreas( nullptr, self->groundentity );
}

constexpr float STRAIGHT_MOVEMENT_DOT_THRESHOLD = 0.8f;

BotMovementPredictionContext::HitWhileRunningTestResult BotMovementPredictionContext::MayHitWhileRunning() {
	if( const auto *cachedResult = mayHitWhileRunningCachesStack.GetCached() ) {
		return *cachedResult;
	}

	if( !self->ai->botRef->HasEnemy() ) {
		mayHitWhileRunningCachesStack.SetCachedValue( HitWhileRunningTestResult::Failure() );
		return HitWhileRunningTestResult::Failure();
	}

	const auto &entityPhysicsState = movementState->entityPhysicsState;
	Vec3 botLookDir( entityPhysicsState.ForwardDir() );

	Vec3 botToEnemyDir( self->ai->botRef->EnemyOrigin() );
	botToEnemyDir -= entityPhysicsState.Origin();
	// We are sure it has non-zero length (enemies collide with the bot)
	botToEnemyDir.NormalizeFast();

	// Check whether the bot may hit while running
	if( botToEnemyDir.Dot( botLookDir ) > STRAIGHT_MOVEMENT_DOT_THRESHOLD ) {
		HitWhileRunningTestResult result;
		result.canHitAsIs = true;
		result.mayHitOverridingPitch = true;
		mayHitWhileRunningCachesStack.SetCachedValue( result );
		return result;
	}

	// Check whether we can change pitch
	botLookDir.Z() = botToEnemyDir.Z();
	// Normalize again
	float lookDirSquareLength = botLookDir.SquaredLength();
	if( lookDirSquareLength < 0.000001f ) {
		mayHitWhileRunningCachesStack.SetCachedValue( HitWhileRunningTestResult::Failure() );
		return HitWhileRunningTestResult::Failure();
	}

	botLookDir *= Q_RSqrt( lookDirSquareLength );
	if( botToEnemyDir.Dot( botLookDir ) > STRAIGHT_MOVEMENT_DOT_THRESHOLD ) {
		HitWhileRunningTestResult result;
		result.canHitAsIs = false;
		result.mayHitOverridingPitch = true;
		mayHitWhileRunningCachesStack.SetCachedValue( result );
		return result;
	}

	mayHitWhileRunningCachesStack.SetCachedValue( HitWhileRunningTestResult::Failure() );
	return HitWhileRunningTestResult::Failure();
}

int BotLandOnSavedAreasMovementAction::FindJumppadAreaNum( const edict_t *jumppadEntity ) {
	// TODO: This can be precomputed at level start
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasAreaSettings = aasWorld->AreaSettings();

	// Jumppad entity origin is not what one might think...
	Vec3 jumppadOrigin( jumppadEntity->r.absmin );
	jumppadOrigin += jumppadEntity->r.absmax;
	jumppadOrigin *= 0.5f;

	int entAreaNum = aasWorld->FindAreaNum( jumppadOrigin );
	if( entAreaNum ) {
		const auto &areaSettings = aasAreaSettings[entAreaNum];
		const int contents = areaSettings.contents;
		const int areaflags = areaSettings.areaflags;
		if( ( contents & AREACONTENTS_JUMPPAD ) && !( contents & AREACONTENTS_DONOTENTER ) && !( areaflags & AREA_DISABLED ) ) {
			return entAreaNum;
		}
	}

	int areaNums[32];
	Vec3 mins( -64, -64, -64 );
	Vec3 maxs( +64, +64, +64 );
	mins += jumppadOrigin;
	maxs += jumppadOrigin;
	int numAreas = aasWorld->BBoxAreas( mins, maxs, areaNums, 32 );
	for( int i = 0; i < numAreas; ++i ) {
		const int areaNum = areaNums[i];
		const auto &areaSettings = aasAreaSettings[areaNum];
		const int contents = areaSettings.contents;
		if( !( contents & AREACONTENTS_JUMPPAD ) ) {
			continue;
		}
		if( contents & AREACONTENTS_DONOTENTER ) {
			continue;
		}
		if( areaSettings.areaflags & AREA_DISABLED ) {
			continue;
		}

		return areaNum;
	}

	// Ensure the area is always found. Do not hide the bug, a bot would keep jumping on the trigger forever.
	constexpr const char *tag = "BotLandOnSavedAreasMovementAction::FindJumppadAreaNum()";
	constexpr const char *format = "Can't find an AAS area num for the jumppad @ %.1f %.1f %.1f\n";
	AI_FailWith( tag, format, jumppadOrigin.X(), jumppadOrigin.Y(), jumppadOrigin.Z() );
}

static float PointToSegmentSquareDistance( const vec3_t point, const vec3_t start, const vec3_t end ) {
	Vec3 segmentVec( end );
	segmentVec -= start;
	Vec3 pointToStart( start );
	pointToStart -= point;

	float pointToStartDotVec = pointToStart.Dot( segmentVec );
	if( pointToStartDotVec >= 0.0f ) {
		return DistanceSquared( point, start );
	}

	Vec3 pointToEnd( end );
	pointToEnd -= point;

	if( pointToEnd.Dot( segmentVec ) <= 0.0f ) {
		return DistanceSquared( point, end );
	}

	Vec3 projection( segmentVec );
	projection *= -pointToStartDotVec / segmentVec.SquaredLength();
	projection += pointToStart;
	return projection.SquaredLength();
}

static float PointToAreaSquareDistance( const vec3_t point, const aas_area_t &area ) {
	if( area.mins[2] > point[2] ) {
		return std::numeric_limits<float>::max();
	}

	if( area.mins[0] >= point[0] && area.maxs[0] <= point[0] && area.mins[1] >= point[1] && area.maxs[1] <= point[1] ) {
		return 0.0f;
	}

	float minDistance = std::numeric_limits<float>::max();
	vec3_t sideStart, sideEnd;
	sideStart[2] = sideEnd[2] = area.mins[2];
	const float *bounds[] = { area.mins, area.maxs };
	// For each side
	for( int i = 0; i < 4; ++i ) {
		// Make side segment
		for( int j = 0; j < 2; ++j ) {
			sideStart[j] = bounds[( ( i + 0 ) >> j ) & 1][j];
			sideEnd[j] = bounds[( ( i + 1 ) >> j ) & 1][j];
		}

		float distance = PointToSegmentSquareDistance( point, sideStart, sideEnd );
		if( distance < minDistance ) {
			minDistance = distance;
		}
	}

	return minDistance;
}

float BotLandOnSavedAreasMovementAction::SaveJumppadLandingAreas( const edict_t *jumppadEntity ) {
	savedLandingAreas.clear();

	int jumppadAreaNum = FindJumppadAreaNum( jumppadEntity );
	if( !jumppadAreaNum ) {
		return -999999.9f;
	}

	const auto *aasWorld = AiAasWorld::Instance();
	const auto *routeCache = self->ai->botRef->routeCache;
	if( int navTargetAreaNum = self->ai->botRef->botBrain.NavTargetAasAreaNum() ) {
		int reachNum = 0;
		if( routeCache->PreferredRouteToGoalArea( jumppadAreaNum, navTargetAreaNum, &reachNum ) ) {
			int jumppadTargetAreaNum = aasWorld->Reachabilities()[reachNum].areanum;
			return SaveLandingAreasForJumppadTargetArea( jumppadEntity, navTargetAreaNum, jumppadTargetAreaNum );
		}
	}

	// The nav target is not reachable. Try to find any areas reachable from the jumppad area by using the jumppad
	const auto &jumppadAreaSettings = aasWorld->AreaSettings()[jumppadAreaNum];
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const auto *aasReach = aasWorld->Reachabilities();
	const float *targetOrigin = jumppadEntity->target_ent->s.origin;
	FilteredAreas filteredAreas;
	// Find an area closest to the jumppad target
	for( int i = 0; i < jumppadAreaSettings.numreachableareas; ++i ) {
		const auto &reach = aasReach[i + jumppadAreaSettings.firstreachablearea];
		if( reach.traveltype != TRAVEL_JUMPPAD ) {
			continue;
		}

		const int areaNum = reach.areanum;
		const auto &areaSettings = aasAreaSettings[areaNum];
		if( areaSettings.areaflags & AREA_DISABLED ) {
			continue;
		}
		if( areaSettings.contents & AREACONTENTS_DONOTENTER ) {
			continue;
		}
		const auto &area = aasAreas[areaNum];
		// Skip areas that are higher than the jumppad target entity
		if( area.mins[2] + 16 > jumppadEntity->target_ent->s.origin[2] ) {
			continue;
		}
		// Closer to the jumppad entity target areas get greater score
		float score = 1.0f / ( 1.0f + PointToAreaSquareDistance( targetOrigin, area ) );
		filteredAreas.emplace_back( AreaAndScore( areaNum, score ) );
		if( filteredAreas.size() == filteredAreas.capacity() ) {
			break;
		}
	}

	// Sort areas so best areas are first
	std::sort( filteredAreas.begin(), filteredAreas.end() );

	return SaveFilteredCandidateAreas( jumppadEntity, 0, filteredAreas );
}

float BotLandOnSavedAreasMovementAction::SaveLandingAreasForJumppadTargetArea( const edict_t *jumppadEntity,
																			   int navTargetAreaNum,
																			   int jumppadTargetAreaNum ) {
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *routeCache = self->ai->botRef->routeCache;
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasAreaSettings = aasWorld->AreaSettings();

	// Get areas around the jumppad area
	const auto &jumppadTargetArea = aasAreas[jumppadTargetAreaNum];
	Vec3 mins( -320, -320, -16 );
	Vec3 maxs( +320, +320, +16 );
	// It's better to use the target entity and not the target area center,
	// because the center might be biased and it leads to poor area selection e.g. on major wdm7 jumppad.
	mins += jumppadEntity->target_ent->s.origin;
	maxs += jumppadEntity->target_ent->s.origin;
	int bboxAreas[48];
	const int numBBoxAreas = aasWorld->BBoxAreas( mins, maxs, bboxAreas, 48 );

	const int baseTravelTime = routeCache->PreferredRouteToGoalArea( jumppadTargetAreaNum, navTargetAreaNum );
	// If the target is for some reasons unreachable or the jumppad target area is the nav target area too
	if( baseTravelTime <= 1 ) {
		// Return some default values in hope they are useful
		savedLandingAreas.push_back( jumppadTargetAreaNum );
		return jumppadTargetArea.mins[2];
	}

	// Filter raw nearby areas
	FilteredAreas filteredAreas;
	for( int i = 0; i < numBBoxAreas; ++i ) {
		const int areaNum = bboxAreas[i];
		// Skip tests for the target area
		if( areaNum == jumppadTargetAreaNum ) {
			continue;
		}

		const auto &rawArea = aasAreas[areaNum];
		// Skip areas that are lower than the target area more than 16 units
		if( rawArea.mins[2] + 16 < jumppadTargetArea.mins[2] ) {
			continue;
		}
		// Skip areas that are higher than the jumppad target entity
		if( rawArea.mins[2] + 16 > jumppadEntity->target_ent->s.origin[2] ) {
			continue;
		}

		const auto &areaSettings = aasAreaSettings[areaNum];
		if( !( areaSettings.areaflags & AREA_GROUNDED ) ) {
			continue;
		}
		if( areaSettings.contents & AREACONTENTS_DONOTENTER ) {
			continue;
		}
		if( areaSettings.areaflags & ( AREA_JUNK | AREA_DISABLED ) ) {
			continue;
		}

		const int travelTime = routeCache->PreferredRouteToGoalArea( areaNum, navTargetAreaNum );
		// If the nav target is not reachable from the box area or
		// it leads to a greater travel time than the jumppad target area
		if( !travelTime || travelTime >= baseTravelTime ) {
			continue;
		}

		// The score is greater if it shortens travel time greater
		float score = (float)baseTravelTime / (float)travelTime;
		// Apply penalty for ledge areas (prevent falling just after landing)
		if( areaSettings.areaflags & AREA_LEDGE ) {
			score *= 0.5f;
		}

		filteredAreas.emplace_back( AreaAndScore( areaNum, score ) );
		if( filteredAreas.size() == filteredAreas.capacity() ) {
			break;
		}
	}

	// Sort filtered areas so best areas are first
	std::sort( filteredAreas.begin(), filteredAreas.end() );

	return SaveFilteredCandidateAreas( jumppadEntity, jumppadTargetAreaNum, filteredAreas );
}

float BotLandOnSavedAreasMovementAction::SaveFilteredCandidateAreas( const edict_t *jumppadEntity,
																	 int jumppadTargetAreaNum,
																	 const FilteredAreas &filteredAreas ) {
	savedLandingAreas.clear();
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasAreas = aasWorld->Areas();

	for( unsigned i = 0, end = std::min( filteredAreas.size(), savedLandingAreas.capacity() ); i < end; ++i )
		savedLandingAreas.push_back( filteredAreas[i].areaNum );

	// Always add the target area (with the lowest priority)
	if( jumppadTargetAreaNum ) {
		if( savedLandingAreas.size() == savedLandingAreas.capacity() ) {
			savedLandingAreas.pop_back();
		}

		savedLandingAreas.push_back( jumppadTargetAreaNum );
	}

	float maxAreaZ = std::numeric_limits<float>::min();
	for( int areaNum: savedLandingAreas ) {
		maxAreaZ = std::max( maxAreaZ, aasAreas[areaNum].mins[2] );
	}

	return maxAreaZ;
}

void BotLandOnSavedAreasMovementAction::BeforePlanning() {
	BotBaseMovementAction::BeforePlanning();
	currAreaIndex = 0;
	totalTestedAreas = 0;

	this->savedLandingAreas.clear();
	auto *botSavedAreas = &self->ai->botRef->savedLandingAreas;
	for( int areaNum: *botSavedAreas )
		this->savedLandingAreas.push_back( areaNum );

	botSavedAreas->clear();
}

void BotLandOnSavedAreasMovementAction::AfterPlanning() {
	BotBaseMovementAction::AfterPlanning();
	if( this->isDisabledForPlanning ) {
		return;
	}

	auto *botSavedAreas = &self->ai->botRef->savedLandingAreas;
	for( int areaNum: this->savedLandingAreas )
		botSavedAreas->push_back( areaNum );
}

void BotHandleTriggeredJumppadMovementAction::PlanPredictionStep( BotMovementPredictionContext *context ) {
	if( !GenericCheckIsActionEnabled( context, &DummyAction() ) ) {
		return;
	}

	auto *jumppadMovementState = &context->movementState->jumppadMovementState;
	Assert( jumppadMovementState->IsActive() );

	if( jumppadMovementState->hasEnteredJumppad ) {
		context->cannotApplyAction = true;
		context->actionSuggestedByAction = &FlyUntilLandingAction();
		Debug( "The bot has already processed jumppad trigger touch in the given context state, fly until landing\n" );
		return;
	}

	jumppadMovementState->hasEnteredJumppad = true;

	auto *botInput = &context->record->botInput;
	botInput->Clear();

	const edict_t *jumppadEntity = jumppadMovementState->JumppadEntity();
	float startLandingAtZ = self->ai->botRef->landOnSavedAreasSetMovementAction.SaveJumppadLandingAreas( jumppadEntity );
	context->movementState->flyUntilLandingMovementState.Activate( startLandingAtZ );
	// Stop prediction (jumppad triggers are not simulated by Exec() code)
	context->isCompleted = true;
}

bool BotLandOnSavedAreasMovementAction::TryLandingStepOnArea( int areaNum, BotMovementPredictionContext *context ) {
	auto *botInput = &context->record->botInput;
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const float *origin = entityPhysicsState.Origin();

	const auto &area = AiAasWorld::Instance()->Areas()[areaNum];
	Vec3 areaPoint( area.center );
	// Lower area point to a bottom of area. Area mins/maxs are absolute.
	areaPoint.Z() = area.mins[2];
	// Do not try to "land" on upper areas
	if( areaPoint.Z() > origin[2] ) {
		Debug( "Cannot land on an area that is above the bot origin in the given movement state\n" );
		return false;
	}

	botInput->Clear();
	botInput->isUcmdSet = true;

	Vec3 intendedLookDir( 0, 0, -1 );
	// Prevent flying over the area.
	if( area.mins[0] > origin[0] || area.maxs[0] < origin[0] || area.mins[1] > origin[1] || area.maxs[1] < origin[1] ) {
		// Most likely case (the bot is outside of the area bounds)
		intendedLookDir.Set( areaPoint );
		intendedLookDir -= origin;
		intendedLookDir.NormalizeFast();
	}
	botInput->SetIntendedLookDir( intendedLookDir, true );

	// Apply QW-like air control
	float dotRight = entityPhysicsState.RightDir().Dot( intendedLookDir );
	if( dotRight > 0.7f ) {
		botInput->SetRightMovement( +1 );
	} else if( dotRight < -0.7f ) {
		botInput->SetRightMovement( -1 );
	} else {
		// While we do not use forwardbunny, there is still a little air control from forward key
		float dotForward = entityPhysicsState.ForwardDir().Dot( intendedLookDir );
		if( dotForward > 0.3f ) {
			botInput->SetForwardMovement( +1 );
		} else if( dotForward < -0.3f ) {
			botInput->SetForwardMovement( -1 );
		}
	}

	return true;
}

void BotLandOnSavedAreasMovementAction::PlanPredictionStep( BotMovementPredictionContext *context ) {
	if( !GenericCheckIsActionEnabled( context, &DummyAction() ) ) {
		return;
	}

	// This list might be empty if all nearby areas have been disabled (e.g. as blocked by enemy).
	if( savedLandingAreas.empty() ) {
		Debug( "Cannot apply action: the saved landing areas list is empty\n" );
		this->isDisabledForPlanning = true;
		context->cannotApplyAction = true;
		context->actionSuggestedByAction = &DummyAction();
		return;
	}

	// If there the current tested area is set
	if( currAreaIndex >= 0 ) {
		Assert( (int)savedLandingAreas.size() > currAreaIndex );
		// Continue testing this area
		if( TryLandingStepOnArea( savedLandingAreas[currAreaIndex], context ) ) {
			context->SaveSuggestedActionForNextFrame( this );
			return;
		}

		// Schedule next saved area for testing
		const char *format = "Landing on area %d/%d has failed, roll back to initial landing state for next area\n";
		Debug( format, currAreaIndex, savedLandingAreas.size() );
		currAreaIndex = -1;
		totalTestedAreas++;
		// Force rolling back to savepoint
		context->SetPendingRollback();
		// (the method execution implicitly will be continued on the code below outside this condition on next call)
		return;
	}

	// There is not current tested area set, try choose one that fit
	for(; totalTestedAreas < savedLandingAreas.size(); totalTestedAreas++ ) {
		// Test each area left using a-priori feasibility of an area
		if( TryLandingStepOnArea( savedLandingAreas[totalTestedAreas], context ) ) {
			// Set the area as current
			currAreaIndex = totalTestedAreas;
			// Create a savepoint
			context->savepointTopOfStackIndex = context->topOfStackIndex;
			// (the method execution will be implicitly continue on the code inside the condition above on next call)
			Debug( "Area %d/%d has been chosen for landing tests\n", currAreaIndex, savedLandingAreas.size() );
			context->SaveSuggestedActionForNextFrame( this );
			return;
		}
	}

	// All areas have been tested, and there is no suitable area for landing
	Debug( "Warning: An area suitable for landing has not been found\n" );

	// Just look at the jumppad target
	const auto &movementState = context->movementState;
	auto *botInput = &context->record->botInput;
	Vec3 toTargetDir( movementState->jumppadMovementState.JumppadEntity()->target_ent->s.origin );
	toTargetDir -= movementState->entityPhysicsState.Origin();
	toTargetDir.NormalizeFast();

	botInput->SetIntendedLookDir( toTargetDir, true );
	// Try apply air control preferring QW-style one
	float dotForward = toTargetDir.Dot( movementState->entityPhysicsState.ForwardDir() );
	if( dotForward < -0.3f ) {
		botInput->SetTurnSpeedMultiplier( 3.0f );
	}

	float dotRight = toTargetDir.Dot( movementState->entityPhysicsState.RightDir() );
	if( dotRight > 0.5f ) {
		botInput->SetRightMovement( +1 );
	} else if( dotRight < -0.5f ) {
		botInput->SetRightMovement( -1 );
	} else if( dotForward > 0.5f ) {
		botInput->SetForwardMovement( +1 );
	}

	// Disallow any input rotation while landing, it relies on a side aircontrol.
	botInput->SetAllowedRotationMask( BotInputRotation::NONE );

	botInput->isUcmdSet = true;

	// Do not predict ahead.
	context->isCompleted = true;
}

void BotLandOnSavedAreasMovementAction::CheckPredictionStepResults( BotMovementPredictionContext *context ) {
	BotBaseMovementAction::CheckPredictionStepResults( context );
	// If movement step failed, make sure that the next area (if any) will be tested after rollback
	if( context->cannotApplyAction ) {
		totalTestedAreas++;
		currAreaIndex = -1;
		return;
	}

	if( context->isCompleted ) {
		return;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( !entityPhysicsState.GroundEntity() ) {
		return;
	}

	// Check which area bot has landed in
	Assert( currAreaIndex >= 0 && currAreaIndex == (int)totalTestedAreas && currAreaIndex < (int)savedLandingAreas.size() );
	const int targetAreaNum = savedLandingAreas[currAreaIndex];
	int currAreaNums[2] = { 0, 0 };
	const int numCurrAreas = entityPhysicsState.PrepareRoutingStartAreas( currAreaNums );
	// If the bot is in the target area
	if( currAreaNums[0] == targetAreaNum || currAreaNums[1] == targetAreaNum ) {
		Debug( "A prediction step has lead to touching a ground in the target landing area, should stop planning\n" );
		context->isCompleted = true;
		return;
	}

	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasAreaFloorClusterNums = aasWorld->AreaFloorClusterNums();
	// If the target area is in some floor cluster
	if( int targetFloorClusterNum = aasAreaFloorClusterNums[targetAreaNum] ) {
		int i = 0;
		for(; i < numCurrAreas; ++i ) {
			if( aasAreaFloorClusterNums[currAreaNums[i]] == targetFloorClusterNum ) {
				break;
			}
		}
		// Some of the current areas is in the same cluster
		if( i != numCurrAreas ) {
			Debug( "A prediction step has lead to touching a ground in the floor cluster of the landing area\n" );
			context->isCompleted = true;
			return;
		}
	} else {
		// Check whether the target area is reachable from the current area by walking and seems to be straight-walkable
		int bestTravelTime = std::numeric_limits<int>::max();
		const auto *routeCache = self->ai->botRef->routeCache;
		for( int i = 0; i < numCurrAreas; ++i ) {
			int travelFlags = TFL_WALK | TFL_WALKOFFLEDGE | TFL_AIR;
			if( int travelTime = routeCache->TravelTimeToGoalArea( currAreaNums[i], targetAreaNum, travelFlags ) ) {
				bestTravelTime = std::min( travelTime, bestTravelTime );
			}
		}
		// If the target area is short-range reachable by walking (in 150 seconds^-2)
		if( bestTravelTime < 150 ) {
			Vec3 testedTargetPoint( aasAreas[targetAreaNum].center );
			// We are sure the target area is grounded
			testedTargetPoint.Z() = aasAreas[targetAreaNum].mins[2] + 1.0f - playerbox_stand_mins[2];
			// Add a unit offset from ground
			Vec3 currPoint( entityPhysicsState.Origin() );
			currPoint.Z() += 1.0f;
			// We have to check against entities in this case
			trace_t trace;
			edict_t *ignore = const_cast<edict_t *>( self );
			float *mins = playerbox_stand_mins;
			float *maxs = playerbox_stand_maxs;
			G_Trace( &trace, currPoint.Data(), mins, maxs, testedTargetPoint.Data(), ignore, MASK_PLAYERSOLID );
			if( trace.fraction == 1.0f ) {
				Debug( "A prediction step has lead to touching a ground in a short-range neighbour area of the target area\n" );
				context->isCompleted = true;
				return;
			}
		}
	}

	Debug( "A prediction step has lead to touching a ground in an unexpected area\n" );
	context->SetPendingRollback();
	// Make sure that the next area (if any) will be tested after rolling back
	totalTestedAreas++;
	currAreaIndex = -1;
}

void DirToKeyInput( const Vec3 &desiredDir, const vec3_t actualForwardDir, const vec3_t actualRightDir, BotInput *input ) {
	input->ClearMovementDirections();

	float dotForward = desiredDir.Dot( actualForwardDir );
	if( dotForward > 0.3 ) {
		input->SetForwardMovement( 1 );
	} else if( dotForward < -0.3 ) {
		input->SetForwardMovement( -1 );
	}

	float dotRight = desiredDir.Dot( actualRightDir );
	if( dotRight > 0.3 ) {
		input->SetRightMovement( 1 );
	} else if( dotRight < -0.3 ) {
		input->SetRightMovement( -1 );
	}

	// Prevent being blocked
	if( !input->ForwardMovement() && !input->RightMovement() ) {
		input->SetForwardMovement( 1 );
	}
}

void BotCampASpotMovementAction::PlanPredictionStep( BotMovementPredictionContext *context ) {
	if( !GenericCheckIsActionEnabled( context, &DefaultWalkAction() ) ) {
		return;
	}

	if( this->disabledForApplicationFrameIndex == context->topOfStackIndex ) {
		context->sequenceStopReason = DISABLED;
		context->cannotApplyAction = true;
		context->actionSuggestedByAction = &DefaultWalkAction();
		return;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	auto *campingSpotState = &context->movementState->campingSpotState;
	auto *botInput = &context->record->botInput;

	context->SetDefaultBotInput();
	context->record->botInput.canOverrideLookVec = true;

	const Vec3 spotOrigin( campingSpotState->Origin() );
	float distance = spotOrigin.Distance2DTo( entityPhysicsState.Origin() );

	AiPendingLookAtPoint lookAtPoint( campingSpotState->GetOrUpdateRandomLookAtPoint() );

	const Vec3 actualLookDir( entityPhysicsState.ForwardDir() );

	// A "pending look at point" and aiming for attacking are mutually exclusive for reasons described below.
	// Check whether we should prefer attacking.
	if( self->ai->botRef->GetSelectedEnemies().AreValid() ) {
		if( self->ai->botRef->ShouldAttack() && self->ai->botRef->ShouldKeepXhairOnEnemy() ) {
			// Disallow setting already computed angles procuded by the pending look at point
			context->movementState->pendingLookAtPointState.Deactivate();
		}
	} else {
		Vec3 expectedLookDir( lookAtPoint.Origin() );
		expectedLookDir -= spotOrigin;
		expectedLookDir.NormalizeFast();
		if( expectedLookDir.Dot( actualLookDir ) < 0.85 ) {
			if( !context->movementState->pendingLookAtPointState.IsActive() ) {
				AiPendingLookAtPoint pendingLookAtPoint( campingSpotState->GetOrUpdateRandomLookAtPoint() );
				context->movementState->pendingLookAtPointState.Activate( pendingLookAtPoint, 300 );
				botInput->ClearMovementDirections();
				botInput->SetWalkButton( true );
				return;
			}
		}
	}


	context->predictionStepMillis = 16;
	// Keep actual look dir as-is, adjust position by keys only
	botInput->SetIntendedLookDir( actualLookDir, true );
	// This means we may strafe randomly
	if( distance / campingSpotState->Radius() < 1.0f ) {
		if( !campingSpotState->AreKeyMoveDirsValid() ) {
			auto &traceCache = context->EnvironmentTraceCache();
			int keyMoves[2];
			Vec3 botToSpotDir( spotOrigin );
			botToSpotDir -= entityPhysicsState.Origin();
			botToSpotDir.NormalizeFast();
			traceCache.MakeRandomizedKeyMovesToTarget( context, botToSpotDir, keyMoves );
			campingSpotState->SetKeyMoveDirs( keyMoves[0], keyMoves[1] );
		} else {
			// Move dirs are kept and the bot is in the spot radius, use lesser prediction precision
			context->predictionStepMillis = 32;
		}
		botInput->SetForwardMovement( campingSpotState->ForwardMove() );
		botInput->SetRightMovement( campingSpotState->RightMove() );
		if( !botInput->ForwardMovement() && !botInput->RightMovement() ) {
			botInput->SetUpMovement( -1 );
		}
	} else {
		Vec3 botToSpotDir( spotOrigin );
		botToSpotDir -= entityPhysicsState.Origin();
		botToSpotDir.NormalizeFast();
		DirToKeyInput( botToSpotDir, actualLookDir.Data(), entityPhysicsState.RightDir().Data(), botInput );
	}

	botInput->SetWalkButton( random() > campingSpotState->Alertness() * 0.75f );
}

void BotCampASpotMovementAction::CheckPredictionStepResults( BotMovementPredictionContext *context ) {
	BotBaseMovementAction::CheckPredictionStepResults( context );
	if( context->cannotApplyAction || context->isCompleted ) {
		return;
	}

	Vec3 origin( context->movementState->entityPhysicsState.Origin() );
	const auto &campingSpotState = context->movementState->campingSpotState;
	if( !campingSpotState.IsActive() ) {
		Debug( "A prediction step has lead to camping spot state deactivation (the bot is too far from its origin)\n" );
		context->SetPendingRollback();
		return;
	}

	const auto &newEntityPhysicsState = context->movementState->entityPhysicsState;
	const auto &oldEntityPhysicsState = context->PhysicsStateBeforeStep();
	const float radius = campingSpotState.Radius();
	Vec3 spotOrigin( campingSpotState.Origin() );

	const float oldSquareDistanceToOrigin = spotOrigin.SquareDistance2DTo( oldEntityPhysicsState.Origin() );
	const float newSquareDistanceToOrigin = spotOrigin.SquareDistance2DTo( newEntityPhysicsState.Origin() );
	if( oldSquareDistanceToOrigin > SQUARE( 1.3f * radius ) ) {
		if( newSquareDistanceToOrigin > oldSquareDistanceToOrigin ) {
			Debug( "A prediction step has lead to even greater distance to the spot origin while bot should return to it\n" );
			context->SetPendingRollback();
			return;
		}
	}

	// Wait for landing
	if( !newEntityPhysicsState.GroundEntity() ) {
		return;
	}

	if( newSquareDistanceToOrigin < SQUARE( radius ) ) {
		const unsigned sequenceDuration = this->SequenceDuration( context );
		const unsigned completionMillisThreshold = (unsigned) ( 512 * ( 1.0f - 0.5f * campingSpotState.Alertness() ) );
		if( sequenceDuration > completionMillisThreshold ) {
			Debug( "Bot is close to the spot origin and there is enough predicted data ahead\n" );
			context->isCompleted = true;
			return;
		}
	}
}

void BotCampASpotMovementAction::OnApplicationSequenceStopped( BotMovementPredictionContext *context,
															   SequenceStopReason stopReason,
															   unsigned stoppedAtFrameIndex ) {
	BotBaseMovementAction::OnApplicationSequenceStopped( context, stopReason, stoppedAtFrameIndex );

	if( stopReason == DISABLED ) {
		return;
	}

	if( stopReason == FAILED ) {
		disabledForApplicationFrameIndex = context->savepointTopOfStackIndex;
		return;
	}

	disabledForApplicationFrameIndex = std::numeric_limits<unsigned>::max();
}

struct ReachChainInterpolator {
	Vec3 intendedLookDir;
	// Continue interpolating while a next reach has these travel types
	const int *compatibleReachTypes;
	int numCompatibleReachTypes;
	// Stop interpolating on these reach types but include a reach start in interpolation
	const int *allowedEndReachTypes;
	int numAllowedEndReachTypes;
	// Note: Ignored when there is only a single far reach.
	float stopAtDistance;

	inline ReachChainInterpolator()
		: intendedLookDir( 0, 0, 0 ),
		compatibleReachTypes( nullptr ),
		numCompatibleReachTypes( 0 ),
		allowedEndReachTypes( nullptr ),
		numAllowedEndReachTypes( 0 ),
		stopAtDistance( 256 )
	{}

	inline void SetCompatibleReachTypes( const int *reachTravelTypes, int numTravelTypes ) {
		this->compatibleReachTypes = reachTravelTypes;
		this->numCompatibleReachTypes = numTravelTypes;
	}

	inline void SetAllowedEndReachTypes( const int *reachTravelTypes, int numTravelTypes ) {
		this->allowedEndReachTypes = reachTravelTypes;
		this->numAllowedEndReachTypes = numTravelTypes;
	}

	inline bool IsCompatibleReachType( int reachTravelType ) const {
		assert( ( reachTravelType & TRAVELTYPE_MASK ) == reachTravelType );
		const int *end = compatibleReachTypes + numCompatibleReachTypes;
		return std::find( compatibleReachTypes, end, reachTravelType ) != end;
	}

	inline bool IsAllowedEndReachType( int reachTravelType ) const {
		assert( ( reachTravelType & TRAVELTYPE_MASK ) == reachTravelType );
		const int *end = allowedEndReachTypes + numAllowedEndReachTypes;
		return std::find( allowedEndReachTypes, end, reachTravelType ) != end;
	}

	bool TrySetDirToRegionExitArea( BotMovementPredictionContext *context,
									const aas_area_t &area,
									float distanceThreshold = 64.0f );

	bool Exec( BotMovementPredictionContext *context );

	inline const Vec3 &Result() const { return intendedLookDir; }
};

bool ReachChainInterpolator::TrySetDirToRegionExitArea( BotMovementPredictionContext *context,
														const aas_area_t &area,
														float distanceThreshold ) {
	const float *origin = context->movementState->entityPhysicsState.Origin();

	Vec3 areaPoint( area.center );
	areaPoint.Z() = area.mins[2] + 32.0f;
	if( areaPoint.SquareDistanceTo( origin ) < SQUARE( distanceThreshold ) ) {
		return false;
	}

	intendedLookDir.Set( areaPoint );
	intendedLookDir -= origin;
	intendedLookDir.NormalizeFast();
	return true;
}

bool ReachChainInterpolator::Exec( BotMovementPredictionContext *context ) {
	trace_t trace;
	vec3_t firstReachDir;
	const auto &reachChain = context->NextReachChain();
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasReach = aasWorld->Reachabilities();
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const auto *aasAreaFloorClusterNums = aasWorld->AreaFloorClusterNums();
	const auto *aasAreaStairsClusterNums = aasWorld->AreaStairsClusterNums();
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const float *origin = entityPhysicsState.Origin();
	const aas_reachability_t *singleFarReach = nullptr;
	const float squareDistanceThreshold = SQUARE( stopAtDistance );
	const int navTargetAreaNum = context->NavTargetAasAreaNum();
	unsigned numReachFound = 0;
	int currAreaFloorClusterNum = 0;
	int currAreaNum = context->CurrAasAreaNum();
	bool endsInNavTargetArea = false;

	// Check for quick shortcuts for special cases when a bot is already inside a stairs cluster or a ramp.
	// This should reduce CPU cycles wasting on interpolation attempts inside these kinds of environment.
	// Using this when a bot is not already in the special kind of environemnt is more complicated,
	// the question is what rules should be followed? So it is not implemented.
	if( int currGroundedAreaNum = context->CurrGroundedAasAreaNum() ) {
		currAreaNum = currGroundedAreaNum;
		currAreaFloorClusterNum = aasAreaFloorClusterNums[currGroundedAreaNum];
		// Stairs clusters and inclined floor areas are mutually exclusive
		if( aasAreaSettings[currGroundedAreaNum].areaflags & AREA_INCLINED_FLOOR ) {
			if( const auto *exitAreaNum = TryFindBestInclinedFloorExitArea( context, currGroundedAreaNum ) ) {
				if( TrySetDirToRegionExitArea( context, aasAreas[*exitAreaNum] ) ) {
					return true;
				}
			}
		} else if( int stairsClusterNum = aasAreaStairsClusterNums[currGroundedAreaNum] ) {
			if( const auto *exitAreaNum = TryFindBestStairsExitArea( context, stairsClusterNum ) ) {
				if( TrySetDirToRegionExitArea( context, aasAreas[*exitAreaNum] ) ) {
					return true;
				}
			}
		}
	}

	intendedLookDir.Set( 0, 0, 0 );

	// The area the reachability starts in
	int reachStartArea = context->CurrAasAreaNum();
	for( unsigned i = 0; i < reachChain.size(); ++i ) {
		const auto &reach = aasReach[reachChain[i].ReachNum()];

		int travelType = reach.traveltype & TRAVELTYPE_MASK;
		// If the reach type is not in compatible types, we will have to make an immediate or pending break of the loop
		if( !IsCompatibleReachType( travelType ) ) {
			// If the reach type is not even mentioned in the allowed stop reach types, break immediately
			if( !IsAllowedEndReachType( travelType ) ) {
				break;
			}

			// If the reach type is mentioned in the allowed stop reach types, process the reach but stop at it.
			// This line acts as a pending break after the iteration
			i = reachChain.size() + 1;
		}

		if( DistanceSquared( origin, reach.start ) > squareDistanceThreshold ) {
			assert( !singleFarReach );
			// Check for possible CM trace replacement by much cheaper 2D raycasting in floor cluster
			if( currAreaFloorClusterNum && currAreaFloorClusterNum == aasAreaFloorClusterNums[reachStartArea] ) {
				if( IsAreaWalkableInFloorCluster( currAreaNum, reachStartArea ) ) {
					singleFarReach = &reach;
				}
			} else {
				// The trace segment might be very long, test PVS first
				if( trap_inPVS( origin, reach.start ) ) {
					SolidWorldTrace( &trace, origin, reach.start );
					if( trace.fraction == 1.0f ) {
						singleFarReach = &reach;
					}
				}
			}
			break;
		}

		// Check for possible CM trace replacement by much cheaper 2D raycasting in floor cluster
		if( currAreaFloorClusterNum && currAreaFloorClusterNum == aasAreaFloorClusterNums[reachStartArea] ) {
			if( !IsAreaWalkableInFloorCluster( currAreaNum, reachStartArea ) ) {
				break;
			}
		} else {
			SolidWorldTrace( &trace, origin, reach.start );
			if( trace.fraction != 1.0f ) {
				break;
			}
		}

		if( reach.areanum == navTargetAreaNum ) {
			endsInNavTargetArea = true;
		}

		// The next reachability starts in this area
		reachStartArea = reach.areanum;

		Vec3 reachDir( reach.start );
		reachDir -= origin;
		reachDir.NormalizeFast();
		// Add a reach dir to the dirs list (be optimistic to avoid extra temporaries)
		if( numReachFound ) {
			// Limit dirs angular spread checking against a first found dir as a base dir
			if( reachDir.Dot( firstReachDir ) < 0.5f ) {
				break;
			}
		} else {
			reachDir.CopyTo( firstReachDir );
		}

		intendedLookDir += reachDir;
		numReachFound++;
	}

	if( !numReachFound ) {
		if( !singleFarReach ) {
			if( context->IsInNavTargetArea() ) {
				intendedLookDir.Set( context->NavTargetOrigin() );
				intendedLookDir -= origin;
				intendedLookDir.NormalizeFast();
				return true;
			}
			return false;
		}

		intendedLookDir.Set( singleFarReach->start );
		intendedLookDir -= origin;
		intendedLookDir.NormalizeFast();
		return true;
	}

	if( endsInNavTargetArea ) {
		Vec3 navTargetOrigin( context->NavTargetOrigin() );
		SolidWorldTrace( &trace, origin, navTargetOrigin.Data() );
		if( trace.fraction == 1.0f ) {
			// Add the direction to the nav target to the interpolated dir
			Vec3 toTargetDir( navTargetOrigin );
			toTargetDir -= origin;
			toTargetDir.NormalizeFast();
			if( toTargetDir.Dot( firstReachDir ) > 0.5f ) {
				intendedLookDir += toTargetDir;
				// Count it as an additional interpolated reachability
				numReachFound++;
			}
		}
	}

	// If there were more than a single reach dir added to the look dir, it requires normalization
	if( numReachFound > 1 ) {
		intendedLookDir.NormalizeFast();
	}

	return true;
}

void BotBunnyInterpolatingReachChainMovementAction::PlanPredictionStep( BotMovementPredictionContext *context ) {
	if( !GenericCheckIsActionEnabled( context, &self->ai->botRef->bunnyStraighteningReachChainMovementAction ) ) {
		return;
	}

	if( !CheckCommonBunnyingActionPreconditions( context ) ) {
		return;
	}

	context->record->botInput.isUcmdSet = true;
	// Continue interpolating while a next reach has these travel types
	const int compatibleReachTypes[4] = { TRAVEL_WALK, TRAVEL_WALKOFFLEDGE, TRAVEL_JUMP, TRAVEL_STRAFEJUMP };
	// Stop interpolating on these reach types but include a reach start in interpolation
	const int allowedEndReachTypes[4] = { TRAVEL_TELEPORT, TRAVEL_JUMPPAD, TRAVEL_ELEVATOR, TRAVEL_LADDER };
	ReachChainInterpolator interpolator;
	interpolator.stopAtDistance = 256;
	interpolator.SetCompatibleReachTypes( compatibleReachTypes, sizeof( compatibleReachTypes ) / sizeof( int ) );
	interpolator.SetAllowedEndReachTypes( allowedEndReachTypes, sizeof( allowedEndReachTypes ) / sizeof( int ) );
	if( !interpolator.Exec( context ) ) {
		context->SetPendingRollback();
		Debug( "Cannot apply action: cannot interpolate reach chain\n" );
		return;
	}

	context->record->botInput.SetIntendedLookDir( interpolator.Result(), true );

	if( !SetupBunnying( context->record->botInput.IntendedLookDir(), context ) ) {
		context->SetPendingRollback();
		return;
	}
}

void BotBunnyToBestFloorClusterPointMovementAction::PlanPredictionStep( BotMovementPredictionContext *context ) {
	if( !GenericCheckIsActionEnabled( context, &self->ai->botRef->bunnyInterpolatingReachChainMovementAction ) ) {
		return;
	}

	if( !CheckCommonBunnyingActionPreconditions( context ) ) {
		return;
	}

	if( !hasSpotOrigin ) {
		if( !context->sameFloorClusterAreasCache.GetClosestToTargetPoint( context, spotOrigin ) ) {
			context->SetPendingRollback();
			return;
		}
	}

	Vec3 intendedLookDir( spotOrigin );
	intendedLookDir -= context->movementState->entityPhysicsState.Origin();
	float distanceToSpot = intendedLookDir.NormalizeFast();

	context->record->botInput.SetIntendedLookDir( intendedLookDir, true );

	// Set initially to a default value
	float maxAccelDotThreshold = 1.0f;
	// If the distance to the spot is large enough (so we are unlikely to miss it without any space for correction)
	if( distanceToSpot > 128.0f ) {
		// Use the most possible allowed acceleration once the velocity and target dirs dot product exceeds this value
		maxAccelDotThreshold = distanceToSpot > 192.0f ? 0.5f : 0.7f;
	}

	if( !SetupBunnying( intendedLookDir, context, maxAccelDotThreshold ) ) {
		context->SetPendingRollback();
		return;
	}
}

bool BotBunnyTestingMultipleLookDirsMovementAction::TraceArcInSolidWorld( const AiEntityPhysicsState &startPhysicsState,
																		  const vec3_t from, const vec3_t to ) {
	trace_t trace;
	auto brushMask = MASK_WATER | MASK_SOLID;

	const float velocityZ = startPhysicsState.Velocity()[2];
	if( !startPhysicsState.GroundEntity() && velocityZ < 50.0f ) {
		StaticWorldTrace( &trace, from, to, brushMask );
		return trace.fraction != 1.0f;
	}

	Vec3 midPoint( to );
	midPoint += from;
	midPoint *= 0.5f;

	// Lets figure out deltaZ making an assumption that all forward momentum is converted to the direction to the point one

	const float squareDistanceToMidPoint = SQUARE( from[0] - midPoint.X() ) + SQUARE( from[1] - midPoint.Y() );
	if( squareDistanceToMidPoint < 10 ) {
		StaticWorldTrace( &trace, from, to, brushMask );
		return trace.fraction != 1.0f;
	}

	const float timeToMidPoint = sqrtf( squareDistanceToMidPoint ) / startPhysicsState.Speed2D();
	const float deltaZ = velocityZ * timeToMidPoint - 0.5f * level.gravity * ( timeToMidPoint * timeToMidPoint );

	// Does not worth making an arc
	// Note that we ignore negative deltaZ since the real trajectory differs anyway
	if( deltaZ < 2.0f ) {
		StaticWorldTrace( &trace, from, to, brushMask );
		return trace.fraction != 1.0f;
	}

	midPoint.Z() += deltaZ;

	StaticWorldTrace( &trace, from, midPoint.Data(), brushMask );
	if( trace.fraction != 1.0f ) {
		return false;
	}

	StaticWorldTrace( &trace, midPoint.Data(), to, brushMask );
	if( trace.fraction != 1.0f ) {
		return false;
	}

	return true;
}

void BotBunnyTestingMultipleLookDirsMovementAction::BeforePlanning() {
	BotGenericRunBunnyingMovementAction::BeforePlanning();
	currSuggestedLookDirNum = 0;
	suggestedLookDirs.clear();
	dirsBaseAreas.clear();

	// Ensure the suggested action has been set in subtype constructor
	Assert( suggestedAction );
}

void BotBunnyTestingMultipleLookDirsMovementAction::OnApplicationSequenceStarted( BotMovementPredictionContext *ctx ) {
	BotGenericRunBunnyingMovementAction::OnApplicationSequenceStarted( ctx );
	// If there is no dirs tested yet
	if( currSuggestedLookDirNum == 0 ) {
		suggestedLookDirs.clear();
		dirsBaseAreas.clear();
		if( ctx->NavTargetAasAreaNum() ) {
			SaveSuggestedLookDirs( ctx );
		}
	}
}

void BotBunnyTestingMultipleLookDirsMovementAction::OnApplicationSequenceStopped( BotMovementPredictionContext *context,
																				  SequenceStopReason stopReason,
																				  unsigned stoppedAtFrameIndex ) {
	BotGenericRunBunnyingMovementAction::OnApplicationSequenceStopped( context, stopReason, stoppedAtFrameIndex );
	// If application sequence succeeded
	if( stopReason != FAILED ) {
		if( stopReason != DISABLED ) {
			currSuggestedLookDirNum = 0;
		}
		return;
	}

	// If the action has been disabled due to prediction stack overflow
	if( this->isDisabledForPlanning ) {
		return;
	}

	// If rolling back is available for the current suggested dir
	if( disabledForApplicationFrameIndex != context->savepointTopOfStackIndex ) {
		return;
	}

	// If another suggested look dir exists
	if( currSuggestedLookDirNum + 1 < suggestedLookDirs.size() ) {
		currSuggestedLookDirNum++;
		// Allow the action application after the context rollback to savepoint
		this->disabledForApplicationFrameIndex = std::numeric_limits<unsigned>::max();
		// Ensure this action will be used after rollback
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}
	// Otherwise use the first dir in a new sequence started on some other frame
	currSuggestedLookDirNum = 0;
}

inline float SuggestObstacleAvoidanceCorrectionFraction( const BotMovementPredictionContext *context ) {
	// Might be negative!
	float speedOverRunSpeed = context->movementState->entityPhysicsState.Speed() - context->GetRunSpeed();
	if( speedOverRunSpeed > 500.0f ) {
		return 0.15f;
	}
	return 0.35f - 0.20f * speedOverRunSpeed / 500.0f;
}

void BotBunnyTestingMultipleLookDirsMovementAction::PlanPredictionStep( BotMovementPredictionContext *context ) {
	if( !GenericCheckIsActionEnabled( context, suggestedAction ) ) {
		return;
	}

	// Do this test after GenericCheckIsActionEnabled(), otherwise disabledForApplicationFrameIndex does not get tested
	if( currSuggestedLookDirNum >= suggestedLookDirs.size() ) {
		Debug( "There is no suggested look dirs yet/left\n" );
		context->SetPendingRollback();
		return;
	}

	if( !CheckCommonBunnyingActionPreconditions( context ) ) {
		return;
	}

	context->record->botInput.SetIntendedLookDir( suggestedLookDirs[currSuggestedLookDirNum], true );

	if( isTryingObstacleAvoidance ) {
		context->TryAvoidJumpableObstacles( SuggestObstacleAvoidanceCorrectionFraction( context ) );
	}

	if( !SetupBunnying( context->record->botInput.IntendedLookDir(), context ) ) {
		context->SetPendingRollback();
		return;
	}
}

inline AreaAndScore *BotBunnyTestingMultipleLookDirsMovementAction::TakeBestCandidateAreas( AreaAndScore *inputBegin,
																							AreaAndScore *inputEnd,
																							unsigned maxAreas ) {
	Assert( inputEnd >= inputBegin );
	const uintptr_t numAreas = inputEnd - inputBegin;
	const uintptr_t numResultAreas = numAreas < maxAreas ? numAreas : maxAreas;

	// Move best area to the array head, repeat it for the array tail
	for( uintptr_t i = 0, end = numResultAreas; i < end; ++i ) {
		// Set the start area as a current best one
		auto &startArea = *( inputBegin + i );
		for( uintptr_t j = i + 1; j < numAreas; ++j ) {
			auto &currArea = *( inputBegin + j );
			// If current area is better (<) than the start one, swap these areas
			if( currArea.score < startArea.score ) {
				std::swap( currArea, startArea );
			}
		}
	}

	return inputBegin + numResultAreas;
}

void BotBunnyTestingMultipleLookDirsMovementAction::SaveCandidateAreaDirs( BotMovementPredictionContext *context,
																		   AreaAndScore *candidateAreasBegin,
																		   AreaAndScore *candidateAreasEnd ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const int navTargetAreaNum = context->NavTargetAasAreaNum();
	const auto *aasAreas = AiAasWorld::Instance()->Areas();

	AreaAndScore *takenAreasBegin = candidateAreasBegin;
	Assert( maxSuggestedLookDirs <= suggestedLookDirs.capacity() );
	unsigned maxAreas = maxSuggestedLookDirs - suggestedLookDirs.size();
	AreaAndScore *takenAreasEnd = TakeBestCandidateAreas( candidateAreasBegin, candidateAreasEnd, maxAreas );

	for( auto iter = takenAreasBegin; iter < takenAreasEnd; ++iter ) {
		int areaNum = ( *iter ).areaNum;
		void *mem = suggestedLookDirs.unsafe_grow_back();
		dirsBaseAreas.push_back( areaNum );
		if( areaNum != navTargetAreaNum ) {
			Vec3 *toAreaDir = new(mem)Vec3( aasAreas[areaNum].center );
			toAreaDir->Z() = aasAreas[areaNum].mins[2] + 32.0f;
			*toAreaDir -= entityPhysicsState.Origin();
			toAreaDir->Z() *= Z_NO_BEND_SCALE;
			toAreaDir->NormalizeFast();
		} else {
			Vec3 *toTargetDir = new(mem)Vec3( context->NavTargetOrigin() );
			*toTargetDir -= entityPhysicsState.Origin();
			toTargetDir->NormalizeFast();
		}
	}
}

BotBunnyStraighteningReachChainMovementAction::BotBunnyStraighteningReachChainMovementAction( Bot *bot_ )
	: BotBunnyTestingMultipleLookDirsMovementAction( bot_, NAME, COLOR_RGB( 0, 192, 0 ) ) {
	supportsObstacleAvoidance = true;
	// The constructor cannot be defined in the header due to this bot member access
	suggestedAction = &bot_->bunnyToBestShortcutAreaMovementAction;
}

void BotBunnyStraighteningReachChainMovementAction::SaveSuggestedLookDirs( BotMovementPredictionContext *context ) {
	Assert( suggestedLookDirs.empty() );
	Assert( dirsBaseAreas.empty() );
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const int navTargetAasAreaNum = context->NavTargetAasAreaNum();
	Assert( navTargetAasAreaNum );

	// Do not modify look vec in this case (we assume its set to nav target)
	if( context->IsInNavTargetArea() ) {
		void *mem = suggestedLookDirs.unsafe_grow_back();
		dirsBaseAreas.push_back( navTargetAasAreaNum );
		Vec3 *toTargetDir = new(mem)Vec3( context->NavTargetOrigin() );
		*toTargetDir -= entityPhysicsState.Origin();
		toTargetDir->NormalizeFast();
		return;
	}

	const auto &nextReachChain = context->NextReachChain();
	if( nextReachChain.empty() ) {
		Debug( "Cannot straighten look vec: next reach. chain is empty\n" );
		return;
	}

	// Make sure the action is dependent of this action
	Assert( suggestedAction == &self->ai->botRef->bunnyToBestShortcutAreaMovementAction );
	// * Quotas are allowed to request only once per frame,
	// and subsequent calls for the same client fail (return with false).
	// Set suggested look dirs for both actions, otherwise the second one (almost) never gets a quota.
	// * Never try to acquire a quota here if a bot is really blocked.
	// These predicted actions are very likely to fail again,
	// and fallbacks do not get their quotas leading to blocking to bot suicide.
	if( self->ai->botRef->MillisInBlockedState() < 100 && AiManager::Instance()->TryGetExpensiveComputationQuota( self ) ) {
		this->maxSuggestedLookDirs = 11;
		self->ai->botRef->bunnyToBestShortcutAreaMovementAction.maxSuggestedLookDirs = 5;
	} else {
		this->maxSuggestedLookDirs = 2;
		self->ai->botRef->bunnyToBestShortcutAreaMovementAction.maxSuggestedLookDirs = 2;
	}

	const AiAasWorld *aasWorld = AiAasWorld::Instance();
	const aas_reachability_t *aasReachabilities = aasWorld->Reachabilities();

	int lastValidReachIndex = -1;
	constexpr unsigned MAX_TESTED_REACHABILITIES = 16U;
	const unsigned maxTestedReachabilities = std::min( MAX_TESTED_REACHABILITIES, nextReachChain.size() );
	const aas_reachability_t *reachStoppedAt = nullptr;
	for( unsigned i = 0; i < maxTestedReachabilities; ++i ) {
		const auto &reach = aasReachabilities[nextReachChain[i].ReachNum()];
		if( reach.traveltype != TRAVEL_WALK && reach.traveltype != TRAVEL_WALKOFFLEDGE ) {
			if( reach.traveltype != TRAVEL_JUMP && reach.traveltype != TRAVEL_STRAFEJUMP ) {
				reachStoppedAt = &reach;
				break;
			}
		}

		lastValidReachIndex++;
	}

	if( lastValidReachIndex < 0 || lastValidReachIndex >= (int)maxTestedReachabilities ) {
		Debug( "There were no supported for bunnying reachabilities\n" );
		return;
	}
	Assert( lastValidReachIndex < (int)maxTestedReachabilities );

	AreaAndScore candidates[MAX_TESTED_REACHABILITIES];
	AreaAndScore *candidatesEnd = SelectCandidateAreas( context, candidates, (unsigned)lastValidReachIndex );

	SaveCandidateAreaDirs( context, candidates, candidatesEnd );
	Assert( suggestedLookDirs.size() <= maxSuggestedLookDirs );

	// If there is a trigger entity in the reach chain, try keep looking at it
	if( reachStoppedAt ) {
		int travelType = reachStoppedAt->traveltype;
		if( travelType == TRAVEL_TELEPORT || travelType == TRAVEL_JUMPPAD || travelType == TRAVEL_ELEVATOR ) {
			Assert( maxSuggestedLookDirs > 0 );
			// Evict the last dir, the trigger should have a priority over it
			if( suggestedLookDirs.size() == maxSuggestedLookDirs ) {
				suggestedLookDirs.pop_back();
			}
			dirsBaseAreas.push_back( 0 );
			void *mem = suggestedLookDirs.unsafe_grow_back();
			// reachStoppedAt->areanum is an area num of reach destination, not the trigger itself.
			// Saving or restoring the trigger area num does not seem worth this minor case.
			Vec3 *toTriggerDir = new(mem)Vec3( reachStoppedAt->start );
			*toTriggerDir -= entityPhysicsState.Origin();
			toTriggerDir->NormalizeFast();
			return;
		}
	}

	if( suggestedLookDirs.size() == 0 ) {
		Debug( "Cannot straighten look vec: cannot find a suitable area in reach. chain to aim for\n" );
	}
}

AreaAndScore *BotBunnyStraighteningReachChainMovementAction::SelectCandidateAreas( BotMovementPredictionContext *context,
																				   AreaAndScore *candidatesBegin,
																				   unsigned lastValidReachIndex ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const auto &nextReachChain = context->NextReachChain();
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasReachabilities = aasWorld->Reachabilities();
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const auto *aasAreaFloorClusterNums = aasWorld->AreaFloorClusterNums();
	const auto *aasAreaStairsClusterNums = aasWorld->AreaStairsClusterNums();
	const int navTargetAasAreaNum = context->NavTargetAasAreaNum();

	const auto *dangerToEvade = self->ai->botRef->perceptionManager.PrimaryDanger();
	// Reduce branching in the loop below
	if( self->ai->botRef->ShouldRushHeadless() || ( dangerToEvade && !dangerToEvade->SupportsImpactTests() ) ) {
		dangerToEvade = nullptr;
	}

	int metStairsClusterNum = 0;

	int currAreaNum = context->CurrAasAreaNum();
	int floorClusterNum = 0;
	if( int groundedAreaNum = context->CurrGroundedAasAreaNum() ) {
		floorClusterNum = aasAreaFloorClusterNums[groundedAreaNum];
	}

	// Do not make it speed-depended, it leads to looping/jitter!
	const float distanceThreshold = 256.0f + 512.0f * self->ai->botRef->Skill();

	AreaAndScore *candidatesPtr = candidatesBegin;
	float minScore = 0.0f;

	Vec3 traceStartPoint( entityPhysicsState.Origin() );
	traceStartPoint.Z() += playerbox_stand_viewheight;
	for( int i = lastValidReachIndex; i >= 0; --i ) {
		const int reachNum = nextReachChain[i].ReachNum();
		const auto &reachability = aasReachabilities[reachNum];
		int areaNum = reachability.areanum;
		const auto &area = aasAreas[areaNum];
		const auto &areaSettings = aasAreaSettings[areaNum];

		if( const int stairsClusterNum = aasAreaStairsClusterNums[areaNum] ) {
			// If a stairs cluster has not been met yet
			// (its currently limited to a single cluster but that's satisfactory)
			if( !metStairsClusterNum ) {
				if( const auto *exitAreaNum = TryFindBestStairsExitArea( context, stairsClusterNum ) ) {
					// Do further tests for exit area num instead of the stairs cluster area
					areaNum = *exitAreaNum;
				}
				metStairsClusterNum = stairsClusterNum;
			} else {
				// Skip the stairs area. A test for the exit area of the cluster has been already done.
				continue;
			}
		}

		if( areaSettings.contents & AREACONTENTS_DONOTENTER ) {
			continue;
		}

		int areaFlags = areaSettings.areaflags;
		if( !( areaFlags & AREA_GROUNDED ) ) {
			continue;
		}
		if( areaFlags & AREA_DISABLED ) {
			continue;
		}

		Vec3 areaPoint( area.center[0], area.center[1], area.mins[2] + 4.0f );
		// Skip areas higher than the bot (to allow moving on a stairs chain, we test distance/height ratio)
		if( area.mins[2] > entityPhysicsState.Origin()[2] ) {
			float distance = areaPoint.FastDistance2DTo( entityPhysicsState.Origin() );
			if( area.mins[2] - entityPhysicsState.Origin()[2] > M_SQRT1_2 * distance ) {
				continue;
			}
		}

		const float squareDistanceToArea = DistanceSquared( area.center, entityPhysicsState.Origin() );
		// Skip way too close areas (otherwise the bot might fall into endless looping)
		if( squareDistanceToArea < SQUARE( 0.4f * distanceThreshold ) ) {
			continue;
		}

		// Skip way too far areas (this is mainly an optimization for the following SolidWorldTrace() call)
		if( squareDistanceToArea > SQUARE( distanceThreshold ) ) {
			continue;
		}

		if( dangerToEvade && dangerToEvade->HasImpactOnPoint( areaPoint ) ) {
			continue;
		}

		// Compute score first to cut off expensive tracing
		const float prevMinScore = minScore;
		// Give far areas greater initial score
		float score = 999999.0f;
		if( areaNum != navTargetAasAreaNum ) {
			// Avoid a division by zero by shifting both nominator and denominator by 1.
			// Note that it slightly shifts score too but there are no upper bounds for the score.
			score = 0.5f + 0.5f * ( (float)( i + 1 ) / (float)( lastValidReachIndex + 1 ) );
			// Try skip "junk" areas (sometimes these areas cannot be avoided in the shortest path)
			if( areaFlags & AREA_JUNK ) {
				score *= 0.1f;
			}
			// Give ledge areas a bit smaller score (sometimes these areas cannot be avoided in the shortest path)
			if( areaFlags & AREA_LEDGE ) {
				score *= 0.7f;
			}
			// Prefer not bounded by walls areas to avoid bumping into walls
			if( !( areaFlags & AREA_WALL ) ) {
				score *= 1.6f;
			}

			// Do not test lower score areas if there is already enough tested candidates
			if( score > minScore ) {
				minScore = score;
			} else if( candidatesPtr - candidatesBegin >= (ptrdiff_t)maxSuggestedLookDirs ) {
				continue;
			}
		}

		// Make sure the bot can see the ground
		// On failure, restore minScore (it might have been set to the value of the rejected area score on this loop step)
		if( floorClusterNum && floorClusterNum == aasAreaFloorClusterNums[areaNum] ) {
			if( !IsAreaWalkableInFloorCluster( currAreaNum, areaNum ) ) {
				minScore = prevMinScore;
				continue;
			}
		} else {
			if( !TraceArcInSolidWorld( entityPhysicsState, traceStartPoint.Data(), areaPoint.Data() ) ) {
				minScore = prevMinScore;
				continue;
			}
		}

		new ( candidatesPtr++ )AreaAndScore( areaNum, score );
	}

	return candidatesPtr;
}

BotBunnyToBestShortcutAreaMovementAction::BotBunnyToBestShortcutAreaMovementAction( Bot *bot_ )
	: BotBunnyTestingMultipleLookDirsMovementAction( bot_, NAME, COLOR_RGB( 255, 64, 0 ) ) {
	supportsObstacleAvoidance = false;
	// The constructor cannot be defined in the header due to this bot member access
	suggestedAction = &bot_->walkOrSlideInterpolatingReachChainMovementAction;
}

void BotBunnyToBestShortcutAreaMovementAction::SaveSuggestedLookDirs( BotMovementPredictionContext *context ) {
	Assert( suggestedLookDirs.empty() );
	Assert( context->NavTargetAasAreaNum() );

	int startTravelTime = FindActualStartTravelTime( context );
	if( !startTravelTime ) {
		return;
	}

	AreaAndScore candidates[MAX_BBOX_AREAS];
	AreaAndScore *candidatesEnd = SelectCandidateAreas( context, candidates, startTravelTime );
	SaveCandidateAreaDirs( context, candidates, candidatesEnd );
}

inline int BotBunnyToBestShortcutAreaMovementAction::FindActualStartTravelTime( BotMovementPredictionContext *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const auto *aasRouteCache = self->ai->botRef->routeCache;
	const int travelFlags = self->ai->botRef->PreferredTravelFlags();
	const int navTargetAreaNum = context->NavTargetAasAreaNum();

	int startAreaNums[2] = { entityPhysicsState.DroppedToFloorAasAreaNum(), entityPhysicsState.CurrAasAreaNum() };
	int startTravelTimes[2];

	int j = 0;
	for( int i = 0, end = ( startAreaNums[0] != startAreaNums[1] ) ? 2 : 1; i < end; ++i ) {
		if( int travelTime = aasRouteCache->TravelTimeToGoalArea( startAreaNums[i], navTargetAreaNum, travelFlags ) ) {
			startTravelTimes[j++] = travelTime;
		}
	}

	switch( j ) {
		case 2:
			return std::min( startTravelTimes[0], startTravelTimes[1] );
		case 1:
			return startTravelTimes[0];
		default:
			return 0;
	}
}

inline int BotBunnyToBestShortcutAreaMovementAction::FindBBoxAreas( BotMovementPredictionContext *context,
																	int *areaNums, int maxAreas ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	// Do not make it speed-depended, it leads to looping/jitter!
	const float side = 256.0f + 256.0f * self->ai->botRef->Skill();

	Vec3 boxMins( -side, -side, -0.33f * side );
	Vec3 boxMaxs( +side, +side, 0 );
	boxMins += entityPhysicsState.Origin();
	boxMaxs += entityPhysicsState.Origin();

	return AiAasWorld::Instance()->BBoxAreas( boxMins, boxMaxs, areaNums, maxAreas );
}

AreaAndScore *BotBunnyToBestShortcutAreaMovementAction::SelectCandidateAreas( BotMovementPredictionContext *context,
																			  AreaAndScore *candidatesBegin,
																			  int startTravelTime ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasRouteCache = self->ai->botRef->routeCache;
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const auto *aasAreaStairsClusterNums = aasWorld->AreaStairsClusterNums();

	const int navTargetAreaNum = context->NavTargetAasAreaNum();
	const int travelFlags = self->ai->botRef->PreferredTravelFlags();
	const int currAreaNum = entityPhysicsState.CurrAasAreaNum();
	const int droppedToFloorAreaNum = entityPhysicsState.DroppedToFloorAasAreaNum();

	const auto &prevTestedAction = self->ai->botRef->bunnyStraighteningReachChainMovementAction;
	Assert( prevTestedAction.suggestedAction == this );
	const auto &prevTestedAreas = prevTestedAction.dirsBaseAreas;

	const float speed = entityPhysicsState.Speed();
	Vec3 velocityDir( entityPhysicsState.Velocity() );
	velocityDir *= 1.0f / speed;

	int minTravelTimeSave = 0;
	AreaAndScore *candidatesPtr = candidatesBegin;

	Vec3 traceStartPoint( entityPhysicsState.Origin() );
	traceStartPoint.Z() += playerbox_stand_viewheight;

	const auto *dangerToEvade = self->ai->botRef->perceptionManager.PrimaryDanger();
	// Reduce branching in the loop below
	if( self->ai->botRef->ShouldRushHeadless() || ( dangerToEvade && !dangerToEvade->SupportsImpactTests() ) ) {
		dangerToEvade = nullptr;
	}

	int metStairsClusterNum = 0;

	int bboxAreaNums[MAX_BBOX_AREAS];
	int numBBoxAreas = FindBBoxAreas( context, bboxAreaNums, MAX_BBOX_AREAS );
	for( int i = 0; i < numBBoxAreas; ++i ) {
		int areaNum = bboxAreaNums[i];
		if( areaNum == droppedToFloorAreaNum || areaNum == currAreaNum ) {
			continue;
		}

		if( const int stairsClusterNum = aasAreaStairsClusterNums[areaNum] ) {
			// If a stairs cluster has not been met yet
			// (its currently limited to a single cluster but that's satisfactory)
			if( !metStairsClusterNum ) {
				if( const auto *exitAreaNum = TryFindBestStairsExitArea( context, stairsClusterNum ) ) {
					// Do further tests for exit area num instead of the stairs cluster area
					areaNum = *exitAreaNum;
				}
				metStairsClusterNum = stairsClusterNum;
			} else {
				// Skip the stairs area. A test for the exit area of the cluster has been already done.
				continue;
			}
		}

		// Skip areas that have lead to the previous action failure
		// This condition has been lifted to the beginning of the loop
		// to avoid computing twice cluster exit area if a cluster has been met.
		if( prevTestedAction.disabledForApplicationFrameIndex == context->topOfStackIndex ) {
			if( std::find( prevTestedAreas.begin(), prevTestedAreas.end(), areaNum ) != prevTestedAreas.end() ) {
				continue;
			}
		}

		const auto &areaSettings = aasAreaSettings[areaNum];
		int areaFlags = areaSettings.areaflags;
		if( !( areaFlags & AREA_GROUNDED ) ) {
			continue;
		}
		if( areaFlags & ( AREA_JUNK | AREA_DISABLED ) ) {
			continue;
		}
		if( areaSettings.contents & ( AREACONTENTS_WATER | AREACONTENTS_LAVA | AREACONTENTS_SLIME | AREACONTENTS_DONOTENTER ) ) {
			continue;
		}

		const auto &area = aasAreas[areaNum];
		Vec3 areaPoint( area.center[0], area.center[1], area.mins[2] - playerbox_stand_mins[2] );
		// Skip areas higher than the bot (to allow moving on a stairs chain, we test distance/height ratio)
		if( area.mins[2] > entityPhysicsState.Origin()[2] ) {
			float distance = areaPoint.FastDistance2DTo( entityPhysicsState.Origin() );
			if( area.mins[2] - entityPhysicsState.Origin()[2] > M_SQRT1_2 * distance ) {
				continue;
			}
		}

		Vec3 toAreaDir( areaPoint );
		toAreaDir -= entityPhysicsState.Origin();
		float distanceToArea = toAreaDir.LengthFast();

		// Reject areas that are very close to the bot.
		// This for example helps to skip some junk areas in stair-like environment.
		if( distanceToArea < 96 ) {
			continue;
		}

		toAreaDir *= 1.0f / distanceToArea;
		// Reject areas behind/not in front depending on speed
		float speedDotFactor = -1.0f + 2 * 0.99f * BoundedFraction( speed, 900 );
		if( velocityDir.Dot( toAreaDir ) < speedDotFactor ) {
			continue;
		}

		if( dangerToEvade && dangerToEvade->HasImpactOnPoint( areaPoint ) ) {
			continue;
		}

		int areaToTargetAreaTravelTime = aasRouteCache->TravelTimeToGoalArea( areaNum, navTargetAreaNum, travelFlags );
		if( !areaToTargetAreaTravelTime ) {
			continue;
		}

		// Time saved on traveling to goal
		const int travelTimeSave = startTravelTime - areaToTargetAreaTravelTime;
		// Try to reject non-feasible areas to cut off expensive trace computation
		if( travelTimeSave <= 0 ) {
			continue;
		}

		const int prevMinTravelTimeSave = minTravelTimeSave;
		// Do not test lower score areas if there is already enough tested candidates
		if( travelTimeSave > minTravelTimeSave ) {
			minTravelTimeSave = travelTimeSave;
		} else if( candidatesPtr - candidatesBegin >= (ptrdiff_t)maxSuggestedLookDirs ) {
			continue;
		}

		// Q: Why an optimization that tests walkability in a floor cluster is not applied?
		// A: Gaps are allowed between the current and target areas, but the walkability test rejects these kinds of areas
		if( !TraceArcInSolidWorld( entityPhysicsState, traceStartPoint.Data(), areaPoint.Data() ) ) {
			// Restore minTravelTimeSave (it might has been set to the value of the rejected area on this loop step)
			minTravelTimeSave = prevMinTravelTimeSave;
			continue;
		}

		// We DO not check whether traveling to the best nearby area takes less time
		// than time traveling from best area to nav target saves.
		// Otherwise only areas in the reachability chain conform to the condition if the routing algorithm works properly.
		// We hope for shortcuts the routing algorithm is not aware of.

		new( candidatesPtr++ )AreaAndScore( areaNum, travelTimeSave );
	}

	return candidatesPtr;
}

void BotWalkCarefullyMovementAction::PlanPredictionStep( BotMovementPredictionContext *context ) {
	// Do not even try using (accelerated) bunnying for easy bots.
	// They however will still still perform various jumps,
	// even during regular roaming on plain surfaces (thats what movement fallbacks do).
	// Ramp/stairs areas and areas not in floor clusters are exceptions
	// (these kinds of areas are still troublesome for bot movement).
	BotBaseMovementAction *suggestedAction = &DefaultBunnyAction();
	if( self->ai->botRef->Skill() < 0.33f ) {
		const auto *aasWorld = AiAasWorld::Instance();
		const int currGroundedAreaNum = context->CurrGroundedAasAreaNum();
		// If the current area is not a ramp-like area
		if( !( aasWorld->AreaSettings()[currGroundedAreaNum].areaflags & AREA_INCLINED_FLOOR ) ) {
			// If the current area is not in a stairs cluster
			if( !( aasWorld->AreaStairsClusterNums()[currGroundedAreaNum] ) ) {
				// If the current area is in a floor cluster
				if( aasWorld->AreaFloorClusterNums()[currGroundedAreaNum ] ) {
					suggestedAction = &DummyAction();
				}
			}
		}
	}

	if( !GenericCheckIsActionEnabled( context, suggestedAction ) ) {
		return;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( !entityPhysicsState.GroundEntity() && entityPhysicsState.HeightOverGround() > 4.0f ) {
		context->cannotApplyAction = true;
		context->actionSuggestedByAction = suggestedAction;
		Debug( "Cannot apply action: the bot is quite high above the ground\n" );
		return;
	}

	const int currAasAreaNum = context->CurrAasAreaNum();
	if( !currAasAreaNum ) {
		context->cannotApplyAction = true;
		context->actionSuggestedByAction = &DummyAction();
		Debug( "Cannot apply action: current AAS area num is undefined\n" );
		return;
	}

	const int navTargetAasAreaNum = context->NavTargetAasAreaNum();
	if( !navTargetAasAreaNum ) {
		context->cannotApplyAction = true;
		context->actionSuggestedByAction = &DummyAction();
		Debug( "Cannot apply action: nav target AAS area num is undefined\n" );
		return;
	}

	if( self->ai->botRef->ShouldMoveCarefully() || self->ai->botRef->ShouldBeSilent() ) {
		context->SetDefaultBotInput();
		context->record->botInput.ClearMovementDirections();
		context->record->botInput.SetForwardMovement( 1 );
		if( self->ai->botRef->ShouldMoveCarefully() || context->IsInNavTargetArea() ) {
			context->record->botInput.SetWalkButton( true );
		}
		return;
	}

	// First test whether there is a gap in front of the bot
	// (if this test is omitted, bots would use this no-jumping action instead of jumping over gaps and fall down)

	const float zOffset = playerbox_stand_mins[2] - 32.0f - entityPhysicsState.HeightOverGround();
	Vec3 frontTestPoint( entityPhysicsState.ForwardDir() );
	frontTestPoint *= 8.0f;
	frontTestPoint += entityPhysicsState.Origin();
	frontTestPoint.Z() += zOffset;

	trace_t trace;
	StaticWorldTrace( &trace, entityPhysicsState.Origin(), frontTestPoint.Data(), MASK_SOLID | MASK_WATER );
	if( trace.fraction == 1.0f ) {
		context->cannotApplyAction = true;
		context->actionSuggestedByAction = suggestedAction;
		return;
	}

	int hazardContentsMask = CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_DONOTENTER;
	// Prevent touching a water if not bot is not already walking in it
	if( !entityPhysicsState.waterLevel ) {
		hazardContentsMask |= CONTENTS_WATER;
	}

	// An action might be applied if there are gap or hazard from both sides
	// or from a single side and there is non-walkable plane from an other side
	int gapSidesNum = 2;
	int hazardSidesNum = 0;

	const float sideOffset = playerbox_stand_maxs[1] - playerbox_stand_mins[1];
	Vec3 sidePoints[2] = { Vec3( entityPhysicsState.RightDir() ), -Vec3( entityPhysicsState.RightDir() ) };
	for( int i = 0; i < 2; ++i ) {
		sidePoints[i] *= sideOffset;
		sidePoints[i] += entityPhysicsState.Origin();
		sidePoints[i].Z() += zOffset;
		StaticWorldTrace( &trace, entityPhysicsState.Origin(), sidePoints[i].Data(), MASK_SOLID | MASK_WATER );
		// Put likely case first
		if( trace.fraction != 1.0f ) {
			// Put likely case first
			if( !( trace.contents & hazardContentsMask ) ) {
				if( ISWALKABLEPLANE( &trace.plane ) ) {
					context->cannotApplyAction = true;
					context->actionSuggestedByAction = suggestedAction;
					Debug( "Cannot apply action: there is no gap, wall or hazard to the right below\n" );
					return;
				}
			} else {
				hazardSidesNum++;
			}

			gapSidesNum--;
		}
	}

	if( !( hazardSidesNum + gapSidesNum ) ) {
		context->cannotApplyAction = true;
		context->actionSuggestedByAction = suggestedAction;
		Debug( "Cannot apply action: there are just two walls from both sides, no gap or hazard\n" );
		return;
	}

	context->SetDefaultBotInput();
	context->record->botInput.ClearMovementDirections();
	context->record->botInput.SetForwardMovement( 1 );
	// Be especially careful when there is a nearby hazard area
	if( hazardSidesNum ) {
		context->record->botInput.SetWalkButton( true );
	}
}

void BotWalkCarefullyMovementAction::CheckPredictionStepResults( BotMovementPredictionContext *context ) {
	BotBaseMovementAction::CheckPredictionStepResults( context );
	if( context->isCompleted ) {
		return;
	}

	if( context->cannotApplyAction && context->shouldRollback ) {
		Debug( "A prediction step has lead to rolling back, the action will be disabled for planning\n" );
		this->isDisabledForPlanning = true;
		return;
	}
}

bool BotGenericRunBunnyingMovementAction::GenericCheckIsActionEnabled( BotMovementPredictionContext *context,
																	   BotBaseMovementAction *suggestedAction ) {
	if( !BotBaseMovementAction::GenericCheckIsActionEnabled( context, suggestedAction ) ) {
		return false;
	}

	if( this->disabledForApplicationFrameIndex != context->topOfStackIndex ) {
		return true;
	}

	Debug( "Cannot apply action: the action has been disabled for application on frame %d\n", context->topOfStackIndex );
	context->sequenceStopReason = DISABLED;
	context->cannotApplyAction = true;
	context->actionSuggestedByAction = suggestedAction;
	return false;
}

bool BotGenericRunBunnyingMovementAction::CheckCommonBunnyingActionPreconditions( BotMovementPredictionContext *context ) {
	int currAasAreaNum = context->CurrAasAreaNum();
	if( !currAasAreaNum ) {
		Debug( "Cannot apply action: curr AAS area num is undefined\n" );
		context->SetPendingRollback();
		return false;
	}

	int navTargetAasAreaNum = context->NavTargetAasAreaNum();
	if( !navTargetAasAreaNum ) {
		Debug( "Cannot apply action: nav target AAS area num is undefined\n" );
		context->SetPendingRollback();
		return false;
	}

	if( self->ai->botRef->ShouldKeepXhairOnEnemy() ) {
		const auto &selectedEnemies = self->ai->botRef->GetSelectedEnemies();
		if( selectedEnemies.AreValid() && selectedEnemies.ArePotentiallyHittable() ) {
			if( !context->MayHitWhileRunning().CanHit() ) {
				Debug( "Cannot apply action: cannot hit an enemy while keeping the crosshair on it is required\n" );
				context->SetPendingRollback();
				this->isDisabledForPlanning = true;
				return false;
			}
		}
	}

	// Cannot find a next reachability in chain while it should exist
	// (looks like the bot is too high above the ground)
	if( !context->IsInNavTargetArea() && !context->NextReachNum() ) {
		Debug( "Cannot apply action: next reachability is undefined and bot is not in the nav target area\n" );
		context->SetPendingRollback();
		return false;
	}

	if( !( context->currPlayerState->pmove.stats[PM_STAT_FEATURES] & PMFEAT_JUMP ) ) {
		Debug( "Cannot apply action: bot does not have the jump movement feature\n" );
		context->SetPendingRollback();
		this->isDisabledForPlanning = true;
		return false;
	}

	if( self->ai->botRef->ShouldBeSilent() ) {
		Debug( "Cannot apply action: bot should be silent\n" );
		context->SetPendingRollback();
		this->isDisabledForPlanning = true;
		return false;
	}

	return true;
}

void BotGenericRunBunnyingMovementAction::SetupCommonBunnyingInput( BotMovementPredictionContext *context ) {
	const auto *pmoveStats = context->currPlayerState->pmove.stats;

	auto *botInput = &context->record->botInput;
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	botInput->SetForwardMovement( 1 );
	const auto &hitWhileRunningTestResult = context->MayHitWhileRunning();
	if( self->ai->botRef->ShouldKeepXhairOnEnemy() ) {
		const auto &selectedEnemies = self->ai->botRef->GetSelectedEnemies();
		if( selectedEnemies.AreValid() && selectedEnemies.ArePotentiallyHittable() ) {
			Assert( hitWhileRunningTestResult.CanHit() );
		}
	}

	botInput->canOverrideLookVec = hitWhileRunningTestResult.canHitAsIs;
	botInput->canOverridePitch = true;

	if( ( pmoveStats[PM_STAT_FEATURES] & PMFEAT_DASH ) && !pmoveStats[PM_STAT_DASHTIME] ) {
		bool shouldDash = false;
		if( entityPhysicsState.Speed() < context->GetDashSpeed() && entityPhysicsState.GroundEntity() ) {
			// Prevent dashing into obstacles
			auto &traceCache = context->EnvironmentTraceCache();
			traceCache.TestForResultsMask( context, traceCache.FullHeightMask( traceCache.FRONT ) );
			if( traceCache.FullHeightFrontTrace().trace.fraction == 1.0f ) {
				shouldDash = true;
			}
		}

		if( shouldDash ) {
			botInput->SetSpecialButton( true );
			botInput->SetUpMovement( 0 );
			// Predict dash precisely
			context->predictionStepMillis = 16;
		} else {
			botInput->SetUpMovement( 1 );
		}
	} else {
		if( entityPhysicsState.Speed() < context->GetRunSpeed() ) {
			botInput->SetUpMovement( 0 );
		} else {
			botInput->SetUpMovement( 1 );
		}
	}
}

bool BotGenericRunBunnyingMovementAction::SetupBunnying( const Vec3 &intendedLookVec,
														 BotMovementPredictionContext *context,
														 float maxAccelDotThreshold ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	auto *botInput = &context->record->botInput;

	Vec3 toTargetDir2D( intendedLookVec );
	toTargetDir2D.Z() = 0;

	Vec3 velocityDir2D( entityPhysicsState.Velocity() );
	velocityDir2D.Z() = 0;

	float squareSpeed2D = entityPhysicsState.SquareSpeed2D();
	float toTargetDir2DSqLen = toTargetDir2D.SquaredLength();

	if( squareSpeed2D > 1.0f ) {
		SetupCommonBunnyingInput( context );

		velocityDir2D *= 1.0f / entityPhysicsState.Speed2D();

		if( toTargetDir2DSqLen > 0.1f ) {
			const auto &oldPMove = context->oldPlayerState->pmove;
			const auto &newPMove = context->currPlayerState->pmove;
			// If not skimming
			if( !( newPMove.skim_time && newPMove.skim_time != oldPMove.skim_time ) ) {
				toTargetDir2D *= Q_RSqrt( toTargetDir2DSqLen );
				float velocityDir2DDotToTargetDir2D = velocityDir2D.Dot( toTargetDir2D );
				if( velocityDir2DDotToTargetDir2D > 0.0f ) {
					// Apply cheating acceleration.
					// maxAccelDotThreshold is usually 1.0f, so the "else" path gets executed.
					// If the maxAccelDotThreshold is lesser than the dot product,
					// a maximal possible acceleration is applied
					// (once the velocity and target dirs match conforming to the specified maxAccelDotThreshold).
					// This allows accelerate even faster if we have an a-priori knowledge that the action is reliable.
					Assert( maxAccelDotThreshold >= 0.0f );
					if( velocityDir2DDotToTargetDir2D >= maxAccelDotThreshold ) {
						context->CheatingAccelerate( 1.0f );
					} else {
						context->CheatingAccelerate( velocityDir2DDotToTargetDir2D );
					}
				}
				if( velocityDir2DDotToTargetDir2D < STRAIGHT_MOVEMENT_DOT_THRESHOLD ) {
					// Correct trajectory using cheating aircontrol
					context->CheatingCorrectVelocity( velocityDir2DDotToTargetDir2D, toTargetDir2D );
				}
			}
		}
	}
	// Looks like the bot is in air falling vertically
	else if( !entityPhysicsState.GroundEntity() ) {
		// Release keys to allow full control over view in air without affecting movement
		if( self->ai->botRef->ShouldAttack() && CanFlyAboveGroundRelaxed( context ) ) {
			botInput->ClearMovementDirections();
			botInput->canOverrideLookVec = true;
		}
		return true;
	} else {
		SetupCommonBunnyingInput( context );
		return true;
	}

	if( self->ai->botRef->ShouldAttack() && CanFlyAboveGroundRelaxed( context ) ) {
		botInput->ClearMovementDirections();
		botInput->canOverrideLookVec = true;
	}

	// Skip dash and WJ near triggers and nav targets to prevent missing a trigger/nav target
	const int nextReachNum = context->NextReachNum();
	if( !nextReachNum ) {
		// Preconditions check must not allow bunnying outside of nav target area having an empty reach. chain
		Assert( context->IsInNavTargetArea() );
		botInput->SetSpecialButton( false );
		botInput->canOverrideLookVec = false;
		botInput->canOverridePitch = false;
		return true;
	}

	switch( AiAasWorld::Instance()->Reachabilities()[nextReachNum].traveltype ) {
		case TRAVEL_TELEPORT:
		case TRAVEL_JUMPPAD:
		case TRAVEL_ELEVATOR:
		case TRAVEL_LADDER:
		case TRAVEL_BARRIERJUMP:
			botInput->SetSpecialButton( false );
			botInput->canOverrideLookVec = false;
			botInput->canOverridePitch = true;
			return true;
		default:
			if( context->IsCloseToNavTarget() ) {
				botInput->SetSpecialButton( false );
				botInput->canOverrideLookVec = false;
				botInput->canOverridePitch = false;
				return true;
			}
	}

	if( ShouldPrepareForCrouchSliding( context, 8.0f ) ) {
		botInput->SetUpMovement( -1 );
		context->predictionStepMillis = 16;
	}

	TrySetWalljump( context );
	return true;
}

bool BotGenericRunBunnyingMovementAction::CanFlyAboveGroundRelaxed( const BotMovementPredictionContext *context ) const {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( entityPhysicsState.GroundEntity() ) {
		return false;
	}

	float desiredHeightOverGround = 0.3f * AI_JUMPABLE_HEIGHT;
	return entityPhysicsState.HeightOverGround() >= desiredHeightOverGround;
}

inline void BotGenericRunBunnyingMovementAction::TrySetWalljump( BotMovementPredictionContext *context ) {
	if( !CanSetWalljump( context ) ) {
		return;
	}

	auto *botInput = &context->record->botInput;
	botInput->ClearMovementDirections();
	botInput->SetSpecialButton( true );
	// Predict a frame precisely for walljumps
	context->predictionStepMillis = 16;
}

#define TEST_TRACE_RESULT_NORMAL( traceResult )                              \
	do                                                                         \
	{                                                                          \
		if( traceResult.trace.fraction != 1.0f )                                \
		{                                                                      \
			if( velocity2DDir.Dot( traceResult.trace.plane.normal ) < -0.5f ) {     \
				return false; }                                                  \
			hasGoodWalljumpNormal = true;                                      \
		}                                                                      \
	} while( 0 )

bool BotGenericRunBunnyingMovementAction::CanSetWalljump( BotMovementPredictionContext *context ) const {
	const short *pmoveStats = context->currPlayerState->pmove.stats;
	if( !( pmoveStats[PM_STAT_FEATURES] & PMFEAT_WALLJUMP ) ) {
		return false;
	}

	if( pmoveStats[PM_STAT_WJTIME] ) {
		return false;
	}

	if( pmoveStats[PM_STAT_STUN] ) {
		return false;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( entityPhysicsState.GroundEntity() ) {
		return false;
	}

	if( entityPhysicsState.HeightOverGround() < 8.0f && entityPhysicsState.Velocity()[2] <= 0 ) {
		return false;
	}

	float speed2D = entityPhysicsState.Speed2D();
	// The 2D speed is too low for walljumping
	if( speed2D < 400 ) {
		return false;
	}

	Vec3 velocity2DDir( entityPhysicsState.Velocity()[0], entityPhysicsState.Velocity()[1], 0 );
	velocity2DDir *= 1.0f / speed2D;

	auto &traceCache = context->EnvironmentTraceCache();
	traceCache.TestForResultsMask( context, traceCache.FullHeightMask( traceCache.FRONT ) );
	const auto &frontResult = traceCache.FullHeightFrontTrace();
	if( velocity2DDir.Dot( frontResult.traceDir ) < 0.7f ) {
		return false;
	}

	bool hasGoodWalljumpNormal = false;
	TEST_TRACE_RESULT_NORMAL( frontResult );

	// Do not force full-height traces for sides to be computed.
	// Walljump height rules are complicated, and full simulation of these rules seems to be excessive.
	// In worst case a potential walljump might be skipped.
	auto sidesMask = traceCache.FULL_SIDES_MASK & ~( traceCache.BACK | traceCache.BACK_LEFT | traceCache.BACK_RIGHT );
	traceCache.TestForResultsMask( context, traceCache.JumpableHeightMask( sidesMask ) );

	TEST_TRACE_RESULT_NORMAL( traceCache.JumpableHeightLeftTrace() );
	TEST_TRACE_RESULT_NORMAL( traceCache.JumpableHeightRightTrace() );
	TEST_TRACE_RESULT_NORMAL( traceCache.JumpableHeightFrontLeftTrace() );
	TEST_TRACE_RESULT_NORMAL( traceCache.JumpableHeightFrontLeftTrace() );

	return hasGoodWalljumpNormal;
}

#undef TEST_TRACE_RESULT_NORMAL

void BotMovementPredictionContext::CheatingAccelerate( float frac ) {
	if( self->ai->botRef->ShouldMoveCarefully() ) {
		return;
	}

	const auto &entityPhysicsState = this->movementState->entityPhysicsState;
	if( entityPhysicsState.GroundEntity() ) {
		return;
	}

	const float speed = entityPhysicsState.Speed();
	const float speedThreshold = this->GetRunSpeed() - 15.0f;
	// Respect player class speed properties
	const float groundSpeedScale = speedThreshold / ( DEFAULT_PLAYERSPEED_STANDARD - 15.0f );

	// Avoid division by zero and logic errors
	if( speed < speedThreshold ) {
		return;
	}

	if( frac <= 0.0f ) {
		return;
	}

	const float maxSpeedGainPerSecond = 250.0f;
	const float minSpeedGainPerSecond = 75.0f;
	float speedGainPerSecond;
	// If speed = pivotSpeed speedGainPerSecond remains the same
	const float pivotSpeed = 550.0f * groundSpeedScale;
	const float constantAccelSpeed = 900.0f;
	if( speed > pivotSpeed ) {
		speedGainPerSecond = minSpeedGainPerSecond;
		// In this case speedGainPerSecond slowly decreases to minSpeedGainPerSecond
		// on speed = constantAccelSpeed or greater
		if( speed <= constantAccelSpeed ) {
			float speedFrac = BoundedFraction( speed - pivotSpeed, constantAccelSpeed - pivotSpeed );
			Assert( speedFrac >= 0.0f && speedFrac <= 1.0f );
			speedGainPerSecond += ( maxSpeedGainPerSecond - minSpeedGainPerSecond ) * ( 1.0f - speedFrac );
		}
	} else {
		// In this case speedGainPerSecond might be 2x as greater than maxSpeedGainPerSecond
		Assert( speedThreshold < pivotSpeed );
		float speedFrac = BoundedFraction( speed - speedThreshold, pivotSpeed - speedThreshold );
		Assert( speedFrac >= 0.0f && speedFrac <= 1.0f );
		speedGainPerSecond = maxSpeedGainPerSecond * ( 1.0f + ( 1.0f - speedFrac ) );
		// Also modify the frac to ensure the bot accelerates fast to the pivotSpeed in all cases
		// (the real applied frac is frac^(1/4))
		frac = SQRTFAST( frac );
	}

	speedGainPerSecond *= groundSpeedScale;
	// (CheatingAccelerate() is called for several kinds of fallback movement that are assumed to be reliable).
	// Do not lower speed gain per second in this case (fallback movement should be reliable).
	// A caller should set an appropriate frac if CheatingAccelerate() is used for other kinds of movement.

	clamp_high( frac, 1.0f );
	frac = SQRTFAST( frac );

	Vec3 newVelocity( entityPhysicsState.Velocity() );
	// Normalize velocity boost direction
	newVelocity *= 1.0f / speed;
	// Make velocity boost vector
	newVelocity *= ( frac * speedGainPerSecond ) * ( 0.001f * this->oldStepMillis );
	// Add velocity boost to the entity velocity in the given physics state
	newVelocity += entityPhysicsState.Velocity();

	record->SetModifiedVelocity( newVelocity );
}

void BotMovementPredictionContext::CheatingCorrectVelocity( const vec3_t target ) {
	const auto &entityPhysicsState = this->movementState->entityPhysicsState;

	Vec3 toTargetDir2D( target );
	toTargetDir2D -= entityPhysicsState.Origin();
	toTargetDir2D.Z() = 0;
	toTargetDir2D.NormalizeFast();

	Vec3 velocity2DDir( entityPhysicsState.Velocity() );
	velocity2DDir.Z() = 0;
	velocity2DDir *= 1.0f / entityPhysicsState.Speed2D();

	CheatingCorrectVelocity( velocity2DDir.Dot( toTargetDir2D ), toTargetDir2D );
}

void BotMovementPredictionContext::CheatingCorrectVelocity( float velocity2DDirDotToTarget2DDir, const Vec3 &toTargetDir2D ) {
	// Respect player class movement limitations
	if( !( this->currPlayerState->pmove.stats[PM_STAT_FEATURES] & PMFEAT_AIRCONTROL ) ) {
		return;
	}

	const auto &entityPhysicsState = this->movementState->entityPhysicsState;

	// Make correction less effective for large angles multiplying it
	// by the dot product to avoid a weird-looking cheating movement
	float controlMultiplier = 0.05f + fabsf( velocity2DDirDotToTarget2DDir ) * 0.05f;
	// Use lots of correction near items
	if( self->ai->botRef->ShouldMoveCarefully() ) {
		controlMultiplier += 0.10f;
	}

	const float speed = entityPhysicsState.Speed();
	if( speed < 100 ) {
		return;
	}

	// Check whether the direction to the target is normalized
	Assert( toTargetDir2D.LengthFast() > 0.99f && toTargetDir2D.LengthFast() < 1.01f );

	Vec3 newVelocity( entityPhysicsState.Velocity() );
	// Normalize current velocity direction
	newVelocity *= 1.0f / speed;
	// Modify velocity direction
	newVelocity += controlMultiplier * toTargetDir2D;
	// Normalize velocity direction again after modification
	newVelocity.Normalize();
	// Restore velocity magnitude
	newVelocity *= speed;

	record->SetModifiedVelocity( newVelocity );
}

bool BotGenericRunBunnyingMovementAction::CheckStepSpeedGainOrLoss( BotMovementPredictionContext *context ) {
	const auto *oldPMove = &context->oldPlayerState->pmove;
	const auto *newPMove = &context->currPlayerState->pmove;
	// Make sure this test is skipped along with other ones while skimming
	Assert( !( newPMove->skim_time && newPMove->skim_time != oldPMove->skim_time ) );

	const auto &newEntityPhysicsState = context->movementState->entityPhysicsState;
	const auto &oldEntityPhysicsState = context->PhysicsStateBeforeStep();

	// Test for a huge speed loss in case of hitting of an obstacle
	const float *oldVelocity = oldEntityPhysicsState.Velocity();
	const float *newVelocity = newEntityPhysicsState.Velocity();
	const float oldSquare2DSpeed = oldEntityPhysicsState.SquareSpeed2D();
	const float newSquare2DSpeed = newEntityPhysicsState.SquareSpeed2D();

	if( newSquare2DSpeed < 10 * 10 && oldSquare2DSpeed > 100 * 100 ) {
		// Bumping into walls on high speed in nav target areas is OK:5735
		if( !context->IsInNavTargetArea() && !context->IsCloseToNavTarget() ) {
			Debug( "A prediction step has lead to close to zero 2D speed while it was significant\n" );
			this->shouldTryObstacleAvoidance = true;
			return false;
		}
	}

	// Check for unintended bouncing back (starting from some speed threshold)
	if( oldSquare2DSpeed > 100 * 100 && newSquare2DSpeed > 1 * 1 ) {
		Vec3 oldVelocity2DDir( oldVelocity[0], oldVelocity[1], 0 );
		oldVelocity2DDir *= 1.0f / oldEntityPhysicsState.Speed2D();
		Vec3 newVelocity2DDir( newVelocity[0], newVelocity[1], 0 );
		newVelocity2DDir *= 1.0f / newEntityPhysicsState.Speed2D();
		if( oldVelocity2DDir.Dot( newVelocity2DDir ) < 0.3f ) {
			Debug( "A prediction step has lead to an unintended bouncing back\n" );
			return false;
		}
	}

	// Check for regular speed loss
	const float oldSpeed = oldEntityPhysicsState.Speed();
	const float newSpeed = newEntityPhysicsState.Speed();

	Assert( context->predictionStepMillis );
	float actualSpeedGainPerSecond = ( newSpeed - oldSpeed ) / ( 0.001f * context->predictionStepMillis );
	if( actualSpeedGainPerSecond >= minDesiredSpeedGainPerSecond || context->IsInNavTargetArea() ) {
		// Reset speed loss timer
		currentSpeedLossSequentialMillis = 0;
		return true;
	}

	const char *format = "Actual speed gain per second %.3f is lower than the desired one %.3f\n";
	Debug( "oldSpeed: %.1f, newSpeed: %1.f, speed gain per second: %.1f\n", oldSpeed, newSpeed, actualSpeedGainPerSecond );
	Debug( format, actualSpeedGainPerSecond, minDesiredSpeedGainPerSecond );

	currentSpeedLossSequentialMillis += context->predictionStepMillis;
	if( tolerableSpeedLossSequentialMillis < currentSpeedLossSequentialMillis ) {
		const char *format_ = "A sequential speed loss interval of %d millis exceeds the tolerable one of %d millis\n";
		Debug( format_, currentSpeedLossSequentialMillis, tolerableSpeedLossSequentialMillis );
		this->shouldTryObstacleAvoidance = true;
		return false;
	}

	return true;
}

bool BotGenericRunBunnyingMovementAction::IsMovingIntoNavEntity( BotMovementPredictionContext *context ) const {
	// Sometimes the camping spot movement action might be not applicable to reach a target.
	// Prevent missing the target when THIS action is likely to be really applied to reach it.
	// We do a coarse prediction of bot path for a second testing its intersection with the target (and no solid world).
	// The following ray-sphere test is very coarse but yields satisfactory results.
	// We might call G_Trace() but it is expensive and would fail if the target is not solid yet.
	const auto &newEntityPhysicsState = context->movementState->entityPhysicsState;
	Vec3 navTargetOrigin( context->NavTargetOrigin() );
	float navTargetRadius = context->NavTargetRadius();
	// If bot is walking on ground, use an ordinary ray-sphere test for a linear movement assumed to the bot
	if( newEntityPhysicsState.GroundEntity() && fabsf( newEntityPhysicsState.Velocity()[2] ) < 1.0f ) {
		Vec3 velocityDir( newEntityPhysicsState.Velocity() );
		velocityDir *= 1.0f / newEntityPhysicsState.Speed();

		Vec3 botToTarget( navTargetOrigin );
		botToTarget -= newEntityPhysicsState.Origin();

		float botToTargetDotVelocityDir = botToTarget.Dot( velocityDir );
		if( botToTarget.SquaredLength() > navTargetRadius * navTargetRadius && botToTargetDotVelocityDir < 0 ) {
			return false;
		}

		// |botToTarget| is the length of the triangle hypotenuse
		// |botToTargetDotVelocityDir| is the length of the adjacent triangle side
		// |distanceVec| is the length of the opposite triangle side
		// (The distanceVec is directed to the target center)

		// distanceVec = botToTarget - botToTargetDotVelocityDir * dir
		Vec3 distanceVec( velocityDir );
		distanceVec *= -botToTargetDotVelocityDir;
		distanceVec += botToTarget;

		return distanceVec.SquaredLength() <= navTargetRadius * navTargetRadius;
	}

	Vec3 botOrigin( newEntityPhysicsState.Origin() );
	Vec3 velocity( newEntityPhysicsState.Velocity() );
	const float gravity = level.gravity;
	constexpr float timeStep = 0.125f;
	for( unsigned stepNum = 0; stepNum < 8; ++stepNum ) {
		velocity.Z() -= gravity * timeStep;
		botOrigin += timeStep * velocity;

		Vec3 botToTarget( navTargetOrigin );
		botToTarget -= botOrigin;

		if( botToTarget.SquaredLength() > navTargetRadius * navTargetRadius ) {
			// The bot has already missed the nav entity (same check as in ray-sphere test)
			if( botToTarget.Dot( velocity ) < 0 ) {
				return false;
			}

			// The bot is still moving in the nav target direction
			continue;
		}

		// The bot is inside the target radius
		return true;
	}

	return false;
}

void BotGenericRunBunnyingMovementAction::CheckPredictionStepResults( BotMovementPredictionContext *context ) {
	BotBaseMovementAction::CheckPredictionStepResults( context );
	if( context->cannotApplyAction || context->isCompleted ) {
		return;
	}

	const auto &oldPMove = context->oldPlayerState->pmove;
	const auto &newPMove = context->currPlayerState->pmove;

	// Skip tests while skimming
	if( newPMove.skim_time && newPMove.skim_time != oldPMove.skim_time ) {
		// The only exception is testing covered distance to prevent
		// jumping in front of wall contacting it forever updating skim timer
		if( this->SequenceDuration( context ) > 400 ) {
			if( originAtSequenceStart.SquareDistance2DTo( newPMove.origin ) < SQUARE( 128 ) ) {
				context->SetPendingRollback();
				Debug( "Looks like the bot is stuck and is resetting the skim timer forever by jumping\n" );
				return;
			}
		}

		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	if( !CheckStepSpeedGainOrLoss( context ) ) {
		context->SetPendingRollback();
		return;
	}

	// This entity physics state has been modified after prediction step
	const auto &newEntityPhysicsState = context->movementState->entityPhysicsState;

	bool isInNavTargetArea = context->IsInNavTargetArea();
	if( isInNavTargetArea || context->IsCloseToNavTarget() ) {
		if( !IsMovingIntoNavEntity( context ) ) {
			Debug( "The bot is likely to miss the nav target\n" );
			context->SetPendingRollback();
			return;
		}

		// Skip other checks in this case
		if( isInNavTargetArea ) {
			// Avoid prediction stack overflow in huge areas
			if( newEntityPhysicsState.GroundEntity() && this->SequenceDuration( context ) > 250 ) {
				Debug( "The bot is on ground in the nav target area, moving into the target, there is enough predicted data\n" );
				context->isCompleted = true;
				return;
			}
			// Keep this action as an active bunny action
			context->SaveSuggestedActionForNextFrame( this );
			return;
		}
	}

	int currTravelTimeToNavTarget = context->TravelTimeToNavTarget();
	if( !currTravelTimeToNavTarget ) {
		currentUnreachableTargetSequentialMillis += context->predictionStepMillis;
		// Be very strict in case when bot does another jump after landing.
		// (Prevent falling in a gap immediately after successful landing on a ledge).
		if( currentUnreachableTargetSequentialMillis > tolerableUnreachableTargetSequentialMillis ) {
			context->SetPendingRollback();
			Debug( "A prediction step has lead to undefined travel time to the nav target\n" );
			return;
		}

		context->SaveSuggestedActionForNextFrame( this );
		return;
	}
	// Reset unreachable target timer
	currentUnreachableTargetSequentialMillis = 0;

	// Whether prediction should not be terminated (with a success) on this frame
	bool disallowPredictionTermination = false;

	if( currTravelTimeToNavTarget <= minTravelTimeToNavTargetSoFar ) {
		minTravelTimeToNavTargetSoFar = currTravelTimeToNavTarget;
		minTravelTimeAreaNumSoFar = context->CurrAasAreaNum();
		if( int groundedAreaNum = context->CurrGroundedAasAreaNum() ) {
			minTravelTimeAreaGroundZ = AiAasWorld::Instance()->Areas()[groundedAreaNum].mins[2];
		}
	} else {
		constexpr const char *format = "A prediction step has lead to increased travel time to nav target\n";
		if( currTravelTimeToNavTarget > (int)( minTravelTimeToNavTargetSoFar + tolerableWalkableIncreasedTravelTimeMillis ) ) {
			context->SetPendingRollback();
			Debug( format );
			return;
		}

		if( minTravelTimeAreaGroundZ != std::numeric_limits<float>::max() ) {
			int groundedAreaNum = context->CurrGroundedAasAreaNum();
			if( !groundedAreaNum ) {
				context->SetPendingRollback();
				Debug( format );
				return;
			}
			float currGroundAreaZ = AiAasWorld::Instance()->Areas()[groundedAreaNum].mins[2];
			// Disallow significant difference in height with the min travel time area.
			// If the current area is higher than the min travel time area,
			// use an increased height delta (falling is easier than climbing)
			if( minTravelTimeAreaGroundZ > currGroundAreaZ + 8 || minTravelTimeAreaGroundZ + 48 < currGroundAreaZ ) {
				context->SetPendingRollback();
				Debug( format );
				return;
			}
		}

		if( minTravelTimeAreaNumSoFar ) {
			// Disallow moving into an area if the min travel time area cannot be reached by walking from the area
			int areaNums[2] = { newEntityPhysicsState.CurrAasAreaNum(), newEntityPhysicsState.DroppedToFloorAasAreaNum() };
			bool walkable = false;
			for( int i = 0, end = ( areaNums[0] != areaNums[1] ? 2 : 1 ); i < end; ++i ) {
				int travelFlags = BotGenericGroundMovementFallback::TRAVEL_FLAGS;
				int toAreaNum = minTravelTimeAreaNumSoFar;
				if( int aasTime = self->ai->botRef->routeCache->TravelTimeToGoalArea( areaNums[i], toAreaNum, travelFlags ) ) {
					// aasTime is in seconds^-2
					if( aasTime * 10 < (int)tolerableWalkableIncreasedTravelTimeMillis ) {
						walkable = true;
						break;
					}
				}
			}
			if( !walkable ) {
				context->SetPendingRollback();
				Debug( format );
				return;
			}
		}

		// Don't terminate on this frame even if other termination conditions match
		// There is a speed loss that is very likely caused by bot bumping into walls/obstacles
		disallowPredictionTermination = true;
	}

	if( originAtSequenceStart.SquareDistanceTo( newEntityPhysicsState.Origin() ) < 64 * 64 ) {
		if( SequenceDuration( context ) < 512 ) {
			context->SaveSuggestedActionForNextFrame( this );
			return;
		}

		// Prevent wasting CPU cycles on further prediction
		Debug( "The bot still has not covered 64 units yet in 512 millis\n" );
		context->SetPendingRollback();
		return;
	}

	if( newEntityPhysicsState.GroundEntity() ) {
		Debug( "The bot has covered 64 units and is on ground, should stop prediction\n" );
		context->isCompleted = true;
		return;
	}

	// Continue prediction in the bot does not move down.
	// Test velocity dir Z, use some negative threshold to avoid wasting CPU cycles
	// on traces that are parallel to the ground.
	if( newEntityPhysicsState.Velocity()[2] / newEntityPhysicsState.Speed() > -0.1f ) {
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	if( newEntityPhysicsState.IsHighAboveGround() ) {
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	if( disallowPredictionTermination ) {
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	Assert( context->NavTargetAasAreaNum() );

	// We might wait for landing, but it produces bad results
	// (rejects many legal moves probably due to prediction stack overflow)
	// The tracing is expensive but we did all possible cutoffs above

	Vec3 predictedOrigin( newEntityPhysicsState.Origin() );
	float predictionSeconds = 0.3f;
	for( int i = 0; i < 3; ++i ) {
		predictedOrigin.Data()[i] += newEntityPhysicsState.Velocity()[i] * predictionSeconds;
	}
	predictedOrigin.Data()[2] -= 0.5f * level.gravity * predictionSeconds * predictionSeconds;

	trace_t trace;
	StaticWorldTrace( &trace, newEntityPhysicsState.Origin(), predictedOrigin.Data(),
					  MASK_WATER | MASK_SOLID, playerbox_stand_mins, playerbox_stand_maxs );
	constexpr auto badContents = CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_NODROP | CONTENTS_DONOTENTER;
	if( trace.fraction == 1.0f || !ISWALKABLEPLANE( &trace.plane ) || ( trace.contents & badContents ) ) {
		// Can't say much. The test is very coarse, continue prediction.
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	Vec3 groundPoint( trace.endpos );
	groundPoint.Z() += 4.0f;
	const auto *aasWorld = AiAasWorld::Instance();
	int groundAreaNum = aasWorld->FindAreaNum( groundPoint );
	if( !groundAreaNum ) {
		// Can't say much. The test is very coarse, continue prediction.
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	const auto &groundAreaSettings = aasWorld->AreaSettings()[groundAreaNum];
	if( !( groundAreaSettings.areaflags & AREA_GROUNDED ) || ( groundAreaSettings.areaflags & ( AREA_DISABLED | AREA_JUNK ) ) ) {
		// Can't say much. The test is very coarse, continue prediction.
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}
	if( ( groundAreaSettings.contents & ( AREACONTENTS_LAVA | AREACONTENTS_SLIME | AREACONTENTS_DONOTENTER ) ) ) {
		// Can't say much. The test is very coarse, continue prediction.
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	const auto *routeCache = self->ai->botRef->routeCache;
	int travelTime = routeCache->PreferredRouteToGoalArea( groundAreaNum, context->NavTargetAasAreaNum() );
	if( travelTime && travelTime <= currTravelTimeToNavTarget ) {
		Debug( "The bot is not very high above the ground and looks like it lands in a \"good\" area\n" );
		context->isCompleted = true;
		return;
	}

	// Can't say much. The test is very coarse, continue prediction
	context->SaveSuggestedActionForNextFrame( this );
	return;
}

void BotGenericRunBunnyingMovementAction::OnApplicationSequenceStarted( BotMovementPredictionContext *context ) {
	BotBaseMovementAction::OnApplicationSequenceStarted( context );
	context->MarkSavepoint( this, context->topOfStackIndex );

	minTravelTimeToNavTargetSoFar = std::numeric_limits<int>::max();
	minTravelTimeAreaNumSoFar = 0;
	minTravelTimeAreaGroundZ = std::numeric_limits<float>::max();
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( context->NavTargetAasAreaNum() ) {
		if( int travelTime = context->TravelTimeToNavTarget() ) {
			minTravelTimeToNavTargetSoFar = travelTime;
			if( int groundedAreaNum = context->CurrGroundedAasAreaNum() ) {
				minTravelTimeAreaGroundZ = AiAasWorld::Instance()->Areas()[groundedAreaNum].mins[2];
			}
		}
	}

	originAtSequenceStart.Set( entityPhysicsState.Origin() );

	currentSpeedLossSequentialMillis = 0;
	currentUnreachableTargetSequentialMillis = 0;
}

void BotGenericRunBunnyingMovementAction::OnApplicationSequenceStopped( BotMovementPredictionContext *context,
																		SequenceStopReason reason,
																		unsigned stoppedAtFrameIndex ) {
	BotBaseMovementAction::OnApplicationSequenceStopped( context, reason, stoppedAtFrameIndex );

	if( reason != FAILED ) {
		ResetObstacleAvoidanceState();
		if( reason != DISABLED ) {
			this->disabledForApplicationFrameIndex = std::numeric_limits<unsigned>::max();
		}
		return;
	}

	// If the action has been disabled due to prediction stack overflow
	if( this->isDisabledForPlanning ) {
		return;
	}

	if( !supportsObstacleAvoidance ) {
		// However having shouldTryObstacleAvoidance flag is legal (it should be ignored in this case).
		// Make sure THIS method logic (that sets isTryingObstacleAvoidance) works as intended.
		Assert( !isTryingObstacleAvoidance );
		// Disable applying this action after rolling back to the savepoint
		this->disabledForApplicationFrameIndex = context->savepointTopOfStackIndex;
		return;
	}

	if( !isTryingObstacleAvoidance && shouldTryObstacleAvoidance ) {
		// Try using obstacle avoidance after rolling back to the savepoint
		// (We rely on skimming for the first try).
		isTryingObstacleAvoidance = true;
		// Make sure this action will be chosen again after rolling back
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	// Disable applying this action after rolling back to the savepoint
	this->disabledForApplicationFrameIndex = context->savepointTopOfStackIndex;
	this->ResetObstacleAvoidanceState();
}

void BotGenericRunBunnyingMovementAction::BeforePlanning() {
	BotBaseMovementAction::BeforePlanning();
	this->disabledForApplicationFrameIndex = std::numeric_limits<unsigned>::max();
	ResetObstacleAvoidanceState();
}

bool BotWalkOrSlideInterpolatingReachChainMovementAction::SetupMovementInTargetArea( BotMovementPredictionContext *context ) {
	Vec3 intendedMoveVec( context->NavTargetOrigin() );
	intendedMoveVec -= context->movementState->entityPhysicsState.Origin();
	intendedMoveVec.NormalizeFast();

	return TrySetupCrouchSliding( context, intendedMoveVec );
}

bool BotWalkOrSlideInterpolatingReachChainMovementAction::TrySetupCrouchSliding( BotMovementPredictionContext *context,
																				 const Vec3 &intendedLookDir ) {
	if( !( context->currPlayerState->pmove.pm_flags & PMF_CROUCH_SLIDING ) ) {
		return false;
	}

	if( context->currPlayerState->pmove.stats[PM_STAT_CROUCHSLIDETIME] <= ( 3 * PM_CROUCHSLIDE_FADE ) / 4 ) {
		return false;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( entityPhysicsState.Speed() < context->GetRunSpeed() ) {
		return false;
	}

	if( self->ai->botRef->HasEnemy() ) {
		return false;
	}

	Vec3 velocityDir( entityPhysicsState.Velocity() );
	velocityDir *= 1.0f / entityPhysicsState.Speed();

	// These directions mismatch is way too high for using crouch slide control
	if( velocityDir.Dot( entityPhysicsState.ForwardDir() ) < 0 ) {
		return false;
	}

	if( velocityDir.Dot( intendedLookDir ) < 0 ) {
		return false;
	}

	auto *botInput = &context->record->botInput;
	botInput->SetIntendedLookDir( intendedLookDir, true );
	botInput->SetUpMovement( -1 );
	botInput->isUcmdSet = true;

	float dotRight = intendedLookDir.Dot( entityPhysicsState.RightDir() );
	if( dotRight > 0.2f ) {
		botInput->SetRightMovement( 1 );
	} else if( dotRight < -0.2f ) {
		botInput->SetRightMovement( -1 );
	}

	botInput->SetAllowedRotationMask( BotInputRotation::NONE );

	return true;
}

void BotWalkOrSlideInterpolatingReachChainMovementAction::PlanPredictionStep( BotMovementPredictionContext *context ) {
	totalNumFrames++;
	if( !GenericCheckIsActionEnabled( context, &DummyAction() ) ) {
		return;
	}

	int navTargetAreaNum = context->NavTargetAasAreaNum();
	if( !navTargetAreaNum ) {
		this->isDisabledForPlanning = true;
		context->SetPendingRollback();
		return;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( !entityPhysicsState.GroundEntity() && entityPhysicsState.HeightOverGround() > 12 ) {
		context->cannotApplyAction = true;
		context->actionSuggestedByAction = &DummyAction();
		Debug( "Cannot apply action: the bot is way too high above the ground\n" );
		return;
	}

	// Try sliding to the nav target in this case
	if( context->IsInNavTargetArea() ) {
		if( !SetupMovementInTargetArea( context ) ) {
			context->SetPendingRollback();
			Debug( "Cannot setup movement in nav target area\n" );
		}
		return;
	}

	// Continue interpolating while a next reach has these travel types
	const int compatibleReachTypes[1] = { TRAVEL_WALK };
	// Stop interpolating on these reach types but include a reach start in interpolation
	const int allowedEndReachTypes[6] =
	{
		TRAVEL_WALKOFFLEDGE, TRAVEL_JUMP, TRAVEL_TELEPORT, TRAVEL_JUMPPAD, TRAVEL_ELEVATOR, TRAVEL_LADDER
	};
	ReachChainInterpolator interpolator;
	interpolator.stopAtDistance = 128.0f;
	interpolator.SetCompatibleReachTypes( compatibleReachTypes, sizeof( compatibleReachTypes ) / sizeof( int ) );
	interpolator.SetAllowedEndReachTypes( allowedEndReachTypes, sizeof( allowedEndReachTypes ) / sizeof( int ) );
	if( !interpolator.Exec( context ) ) {
		this->isDisabledForPlanning = true;
		context->SetPendingRollback();
		Debug( "Cannot apply action: cannot interpolate reach chain\n" );
		return;
	}

	const auto &miscTactics = self->ai->botRef->GetMiscTactics();
	if( !miscTactics.shouldBeSilent && !miscTactics.shouldMoveCarefully ) {
		if( entityPhysicsState.Speed2D() > context->GetRunSpeed() ) {
			// Check whether the bot is moving in a "proper" direction to prevent cycling in a loop around a reachability
			Vec3 velocityDir( entityPhysicsState.Velocity() );
			velocityDir *= 1.0f / ( entityPhysicsState.Speed() + 0.000001f );
			if( velocityDir.Dot( interpolator.Result() ) > 0.7f ) {
				context->cannotApplyAction = true;
				context->actionSuggestedByAction = &DummyAction();
				Debug( "Cannot apply action: do not lose a high speed, this speed is safe in the current environment\n" );
				return;
			}
		}
	}

	if( TrySetupCrouchSliding( context, interpolator.Result() ) ) {
		// Predict crouch sliding precisely
		context->predictionStepMillis = 16;
		numSlideFrames++;
		return;
	}

	int keyMoves[2] = { 0, 0 };
	auto *botInput = &context->record->botInput;
	if( entityPhysicsState.GroundEntity() ) {
		auto &environmentTraceCache = context->EnvironmentTraceCache();
		if( self->ai->botRef->HasEnemy() ) {
			environmentTraceCache.MakeRandomizedKeyMovesToTarget( context, interpolator.Result(), keyMoves );
		} else {
			environmentTraceCache.MakeKeyMovesToTarget( context, interpolator.Result(), keyMoves );
		}
	} else if( ShouldPrepareForCrouchSliding( context ) ) {
		botInput->SetUpMovement( -1 );
		// Predict crouch sliding precisely
		context->predictionStepMillis = 16;
	}

	botInput->SetForwardMovement( keyMoves[0] );
	botInput->SetRightMovement( keyMoves[1] );
	botInput->SetIntendedLookDir( interpolator.Result(), true );
	botInput->isUcmdSet = true;
	botInput->canOverrideLookVec = true;
}

void BotWalkOrSlideInterpolatingReachChainMovementAction::CheckPredictionStepResults( BotMovementPredictionContext *context ) {
	BotBaseMovementAction::CheckPredictionStepResults( context );
	if( context->cannotApplyAction || context->isCompleted ) {
		return;
	}

	const auto &newPMove = context->currPlayerState->pmove;
	const auto &oldPMove = context->oldPlayerState->pmove;
	// Disallow skimming to kick in.
	// A bot is not intended to move fast enough in this action to allow hiding bumping into obstacles by skimming.
	if( newPMove.skim_time && newPMove.skim_time != oldPMove.skim_time ) {
		Debug( "A prediction step has lead to skimming. A skimming is not allowed in this action\n" );
		this->isDisabledForPlanning = true;
		return;
	}

	const auto &newEntityPhysicsState = context->movementState->entityPhysicsState;
	const auto &oldEntityPhysicsState = context->PhysicsStateBeforeStep();
	if( !newEntityPhysicsState.GroundEntity() ) {
		// Allow being in air on steps/stairs
		if( newEntityPhysicsState.HeightOverGround() > 12 && oldEntityPhysicsState.HeightOverGround() < 12 ) {
			Debug( "A prediction step has lead to being way too high above the ground\n" );
			this->isDisabledForPlanning = true;
			context->SetPendingRollback();
			return;
		}
	}

	int currTravelTimeToNavTarget = context->TravelTimeToNavTarget();
	if( !currTravelTimeToNavTarget ) {
		Debug( "A prediction step has lead to an undefined travel time to the nav target\n" );
		this->isDisabledForPlanning = true;
		context->SetPendingRollback();
		return;
	}
	if( currTravelTimeToNavTarget > minTravelTimeToTarget ) {
		Debug( "A prediction step has lead to an increased travel time to the nav target\n" );
		this->isDisabledForPlanning = true;
		context->SetPendingRollback();
		return;
	}
	minTravelTimeToTarget = currTravelTimeToNavTarget;

	if( this->SequenceDuration( context ) < 200 ) {
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	float distanceThreshold = 20.0f + 28.0f * ( numSlideFrames / (float)totalNumFrames );
	if( originAtSequenceStart.SquareDistanceTo( newEntityPhysicsState.Origin() ) < SQUARE( distanceThreshold ) ) {
		Debug( "The bot is likely to be stuck after 200 millis\n" );
		this->isDisabledForPlanning = true;
		context->SetPendingRollback();
		return;
	}

	if( newEntityPhysicsState.Speed() < context->GetRunSpeed() - 10 ) {
		Debug( "The bot speed is still below run speed after 200 millis\n" );
		this->isDisabledForPlanning = true;
		context->SetPendingRollback();
		return;
	}

	// Wait for hitting the ground
	if( !newEntityPhysicsState.GroundEntity() ) {
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	Debug( "There is enough predicted data ahead\n" );
	context->isCompleted = true;
}

void BotWalkOrSlideInterpolatingReachChainMovementAction::OnApplicationSequenceStarted( BotMovementPredictionContext *context ) {
	BotBaseMovementAction::OnApplicationSequenceStarted( context );

	minTravelTimeToTarget = context->TravelTimeToNavTarget();
	totalNumFrames = 0;
	numSlideFrames = 0;
}

inline unsigned BotEnvironmentTraceCache::SelectNonBlockedDirs( BotMovementPredictionContext *context, unsigned *nonBlockedDirIndices ) {
	this->TestForResultsMask( context, this->FullHeightMask( FULL_SIDES_MASK ) );

	unsigned numNonBlockedDirs = 0;
	for( unsigned i = 0; i < 8; ++i ) {
		if( this->FullHeightTraceForSideIndex( i ).IsEmpty() ) {
			nonBlockedDirIndices[numNonBlockedDirs++] = i;
		}
	}
	return numNonBlockedDirs;
}

void BotEnvironmentTraceCache::MakeRandomizedKeyMovesToTarget( BotMovementPredictionContext *context,
															   const Vec3 &intendedMoveDir, int *keyMoves ) {
	unsigned nonBlockedDirIndices[8];
	unsigned numNonBlockedDirs = SelectNonBlockedDirs( context, nonBlockedDirIndices );

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const Vec3 forwardDir( entityPhysicsState.ForwardDir() );
	const Vec3 rightDir( entityPhysicsState.RightDir() );
	assert( ( intendedMoveDir.Length() - 1.0f ) < 0.0001f );

	// Choose randomly from all non-blocked dirs based on scores
	// For each non - blocked area make an interval having a corresponding to the area score length.
	// An interval is defined by lower and upper bounds.
	// Upper bounds are stored in the array.
	// Lower bounds are upper bounds of the previous array memner (if any) or 0 for the first array memeber.
	float dirDistributionUpperBound[8];
	float scoresSum = 0.0f;
	for( unsigned i = 0; i < numNonBlockedDirs; ++i ) {
		vec3_t keyMoveVec;
		const float *dirFractions = sideDirXYFractions[nonBlockedDirIndices[i]];
		VectorScale( forwardDir.Data(), dirFractions[0], keyMoveVec );
		VectorMA( keyMoveVec, dirFractions[1], rightDir.Data(), keyMoveVec );
		scoresSum += 0.55f + 0.45f * intendedMoveDir.Dot( keyMoveVec );
		dirDistributionUpperBound[i] = scoresSum;
	}

	// A uniformly distributed random number in (0, scoresSum)
	const float rn = random() * scoresSum;
	for( unsigned i = 0; i < numNonBlockedDirs; ++i ) {
		if( rn > dirDistributionUpperBound[i] ) {
			continue;
		}

		int dirIndex = nonBlockedDirIndices[i];
		const int *dirMoves = sideDirXYMoves[dirIndex];
		Vector2Copy( dirMoves, keyMoves );
		return;
	}

	Vector2Set( keyMoves, 0, 0 );
}

void BotEnvironmentTraceCache::MakeKeyMovesToTarget( BotMovementPredictionContext *context,
													 const Vec3 &intendedMoveDir, int *keyMoves ) {
	unsigned nonBlockedDirIndices[8];
	unsigned numNonBlockedDirs = SelectNonBlockedDirs( context, nonBlockedDirIndices );

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const Vec3 forwardDir( entityPhysicsState.ForwardDir() );
	const Vec3 rightDir( entityPhysicsState.RightDir() );
	assert( ( intendedMoveDir.Length() - 1.0f ) < 0.0001f );

	float bestScore = 0.0f;
	unsigned bestDirIndex = (unsigned)-1;
	for( unsigned i = 0; i < numNonBlockedDirs; ++i ) {
		vec3_t keyMoveVec;
		unsigned dirIndex = nonBlockedDirIndices[i];
		const float *dirFractions = sideDirXYFractions[dirIndex];
		VectorScale( forwardDir.Data(), dirFractions[0], keyMoveVec );
		VectorMA( keyMoveVec, dirFractions[1], rightDir.Data(), keyMoveVec );
		float score = 0.55f + 0.45f * intendedMoveDir.Dot( keyMoveVec );
		if( score > bestScore ) {
			bestScore = score;
			bestDirIndex = dirIndex;
		}
	}
	if( bestScore > 0 ) {
		const int *dirMoves = sideDirXYMoves[bestDirIndex];
		Vector2Copy( dirMoves, keyMoves );
		return;
	}

	Vector2Set( keyMoves, 0, 0 );
}

void BotEnvironmentTraceCache::MakeRandomKeyMoves( BotMovementPredictionContext *context, int *keyMoves ) {
	unsigned nonBlockedDirIndices[8];
	unsigned numNonBlockedDirs = SelectNonBlockedDirs( context, nonBlockedDirIndices );
	if( numNonBlockedDirs ) {
		int dirIndex = nonBlockedDirIndices[(unsigned)( 0.9999f * numNonBlockedDirs * random() )];
		const int *dirMoves = sideDirXYMoves[dirIndex];
		Vector2Copy( dirMoves, keyMoves );
		return;
	}
	Vector2Set( keyMoves, 0, 0 );
}

void BotCombatDodgeSemiRandomlyToTargetMovementAction::UpdateKeyMoveDirs( BotMovementPredictionContext *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	auto *combatMovementState = &context->movementState->keyMoveDirsState;
	Assert( combatMovementState->IsActive() );

	int keyMoves[2];
	auto &traceCache = context->EnvironmentTraceCache();
	vec3_t closestFloorPoint;
	Vec3 intendedMoveDir( entityPhysicsState.Origin() );
	bool hasDefinedMoveDir = false;
	if( context->sameFloorClusterAreasCache.GetClosestToTargetPoint( context, closestFloorPoint ) ) {
		intendedMoveDir -= closestFloorPoint;
		hasDefinedMoveDir = true;
	} else if( int nextReachNum = context->NextReachNum() ) {
		const auto &nextReach = AiAasWorld::Instance()->Reachabilities()[nextReachNum];
		Vec3 reachAvgDir( nextReach.start );
		reachAvgDir += nextReach.end;
		reachAvgDir *= 0.5f;
		hasDefinedMoveDir = true;
	}

	if( hasDefinedMoveDir ) {
		// We have swapped the difference start and end points for convenient Vec3 initialization
		intendedMoveDir *= -1.0f;
		intendedMoveDir.NormalizeFast();

		if( ShouldTryRandomness() ) {
			traceCache.MakeRandomizedKeyMovesToTarget( context, intendedMoveDir, keyMoves );
		} else {
			traceCache.MakeKeyMovesToTarget( context, intendedMoveDir, keyMoves );
		}
	} else {
		traceCache.MakeRandomKeyMoves( context, keyMoves );
	}

	combatMovementState->Activate( keyMoves[0], keyMoves[1] );
}

void BotCombatDodgeSemiRandomlyToTargetMovementAction::PlanPredictionStep( BotMovementPredictionContext *context ) {
	Assert( self->ai->botRef->ShouldKeepXhairOnEnemy() );
	Assert( self->ai->botRef->GetSelectedEnemies().AreValid() );

	auto *botInput = &context->record->botInput;
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	botInput->canOverrideLookVec = true;
	botInput->shouldOverrideLookVec = true;
	botInput->isUcmdSet = true;

	if( attemptNum == maxAttempts ) {
		Debug( "Attempts count has reached its limit. Should stop planning\n" );
		// There is no fallback action since this action is a default one for combat state.
		botInput->SetForwardMovement( 0 );
		botInput->SetRightMovement( 0 );
		botInput->SetUpMovement( random() > 0.5f ? -1 : +1 );
		context->isCompleted = true;
	}

	Vec3 botToEnemies( self->ai->botRef->GetSelectedEnemies().LastSeenOrigin() );
	botToEnemies -= entityPhysicsState.Origin();

	const short *pmStats = context->currPlayerState->pmove.stats;
	if( entityPhysicsState.GroundEntity() ) {
		if( ShouldTrySpecialMovement() ) {
			if( pmStats[PM_STAT_FEATURES] & PMFEAT_DASH ) {
				const float speedThreshold = context->GetDashSpeed() - 10;
				if( entityPhysicsState.Speed() < speedThreshold ) {
					if( !pmStats[PM_STAT_DASHTIME] ) {
						botInput->SetSpecialButton( true );
						context->predictionStepMillis = 16;
					}
				}
			}
		}
		auto *combatMovementState = &context->movementState->keyMoveDirsState;
		if( combatMovementState->IsActive() ) {
			UpdateKeyMoveDirs( context );
		}

		botInput->SetForwardMovement( combatMovementState->ForwardMove() );
		botInput->SetRightMovement( combatMovementState->RightMove() );
		// Set at least a single key or button while on ground (forward/right move keys might both be zero)
		if( !botInput->ForwardMovement() && !botInput->RightMovement() && !botInput->UpMovement() ) {
			if( !botInput->IsSpecialButtonSet() ) {
				botInput->SetUpMovement( -1 );
			}
		}
	} else {
		if( ShouldTrySpecialMovement() ) {
			if( ( pmStats[PM_STAT_FEATURES] & PMFEAT_WALLJUMP ) && !pmStats[PM_STAT_WJTIME] && !pmStats[PM_STAT_STUN] ) {
				botInput->SetSpecialButton( true );
				context->predictionStepMillis = 16;
			}
		}

		const float skill = self->ai->botRef->Skill();
		if( !botInput->IsSpecialButtonSet() && entityPhysicsState.Speed2D() < 650 ) {
			const auto &oldPMove = context->oldPlayerState->pmove;
			const auto &newPMove = context->currPlayerState->pmove;
			// If not skimming
			if( !( newPMove.skim_time && newPMove.skim_time != oldPMove.skim_time ) ) {
				context->CheatingAccelerate( skill > 0.33f ? skill : 0.5f * skill );
			}
		}
	}
}

void BotCombatDodgeSemiRandomlyToTargetMovementAction::CheckPredictionStepResults( BotMovementPredictionContext *context ) {
	BotBaseMovementAction::CheckPredictionStepResults( context );
	if( context->cannotApplyAction || context->isCompleted ) {
		return;
	}

	// If there is no nav target, skip nav target reachability tests
	if( this->minTravelTimeToTarget ) {
		Assert( context->NavTargetAasAreaNum() );
		int newTravelTimeToTarget = context->TravelTimeToNavTarget();
		if( !newTravelTimeToTarget ) {
			Debug( "A prediction step has lead to an undefined travel time to the nav target\n" );
			context->SetPendingRollback();
			return;
		}

		if( newTravelTimeToTarget < this->minTravelTimeToTarget ) {
			this->minTravelTimeToTarget = newTravelTimeToTarget;
		} else if( newTravelTimeToTarget > this->minTravelTimeToTarget + 50 ) {
			Debug( "A prediction step has lead to a greater travel time to the nav target\n" );
			context->SetPendingRollback();
			return;
		}
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const auto &moveDirsState = context->movementState->keyMoveDirsState;
	// Do not check total distance in case when bot has/had zero move dirs set
	if( !moveDirsState.ForwardMove() && !moveDirsState.RightMove() ) {
		// If the bot is on ground and move dirs set at a sequence start were invalidated
		if( entityPhysicsState.GroundEntity() && !moveDirsState.IsActive() ) {
			context->isCompleted = true;
		} else {
			context->SaveSuggestedActionForNextFrame( this );
		}
		return;
	}

	const float *oldOrigin = context->PhysicsStateBeforeStep().Origin();
	const float *newOrigin = entityPhysicsState.Origin();
	totalCovered2DDistance += SQRTFAST( SQUARE( newOrigin[0] - oldOrigin[0] ) + SQUARE( newOrigin[1] - oldOrigin[1] ) );

	// If the bot is on ground and move dirs set at a sequence start were invalidated
	if( entityPhysicsState.GroundEntity() && !moveDirsState.IsActive() ) {
		// Check for blocking
		if( totalCovered2DDistance < 24 ) {
			Debug( "Total covered distance since sequence start is too low\n" );
			context->SetPendingRollback();
			return;
		}
		context->isCompleted = true;
		return;
	}

	context->SaveSuggestedActionForNextFrame( this );
}

void BotCombatDodgeSemiRandomlyToTargetMovementAction::OnApplicationSequenceStarted( BotMovementPredictionContext *context ) {
	BotBaseMovementAction::OnApplicationSequenceStarted( context );

	this->minTravelTimeToTarget = context->TravelTimeToNavTarget();
	this->totalCovered2DDistance = 0.0f;
	// Always reset combat move dirs state to ensure that the action will be predicted for the entire move dirs lifetime
	context->movementState->keyMoveDirsState.Deactivate();
}

void BotCombatDodgeSemiRandomlyToTargetMovementAction::OnApplicationSequenceStopped( BotMovementPredictionContext *context,
																					 SequenceStopReason stopReason,
																					 unsigned stoppedAtFrameIndex ) {
	BotBaseMovementAction::OnApplicationSequenceStopped( context, stopReason, stoppedAtFrameIndex );
	if( stopReason != FAILED ) {
		attemptNum = 0;
		return;
	}

	attemptNum++;
	Assert( attemptNum <= maxAttempts );
}

void BotCombatDodgeSemiRandomlyToTargetMovementAction::BeforePlanning() {
	BotBaseMovementAction::BeforePlanning();
	attemptNum = 0;
	maxAttempts = self->ai->botRef->Skill() > 0.33f ? 4 : 2;
}

void Bot::CheckTargetProximity() {
	if( !botBrain.HasNavTarget() ) {
		return;
	}

	if( !botBrain.IsCloseToNavTarget( 128.0f ) ) {
		return;
	}

	// Save the origin for the roaming manager to avoid its occasional modification in the code below
	if( !botBrain.TryReachNavTargetByProximity() ) {
		return;
	}

	OnNavTargetTouchHandled();
}

void BotEnvironmentTraceCache::SetFullHeightCachedTracesEmpty( const vec3_t front2DDir, const vec3_t right2DDir ) {
	for( unsigned i = 0; i < 6; ++i ) {
		auto &fullResult = results[i + 0];
		auto &jumpableResult = results[i + 6];
		fullResult.trace.fraction = 1.0f;
		fullResult.trace.fraction = 1.0f;
		// We have to save a legal trace dir
		MakeTraceDir( i, front2DDir, right2DDir, fullResult.traceDir );
		VectorCopy( fullResult.traceDir, jumpableResult.traceDir );
	}
	resultsMask |= ALL_SIDES_MASK;
	hasNoFullHeightObstaclesAround = true;
}

void BotEnvironmentTraceCache::SetJumpableHeightCachedTracesEmpty( const vec3_t front2DDir, const vec3_t right2DDir ) {
	for( unsigned i = 0; i < 6; ++i ) {
		auto &result = results[i + 6];
		result.trace.fraction = 1.0f;
		// We have to save a legal trace dir
		MakeTraceDir( i, front2DDir, right2DDir, result.traceDir );
	}
	resultsMask |= JUMPABLE_SIDES_MASK;
}

inline bool BotEnvironmentTraceCache::CanSkipTracingForAreaHeight( const vec3_t origin,
																   const aas_area_t &area,
																   float minZOffset ) {
	if( area.mins[2] >= origin[2] + minZOffset ) {
		return false;
	}
	if( area.maxs[2] <= origin[2] + playerbox_stand_maxs[2] ) {
		return false;
	}

	return true;
}

const int BotEnvironmentTraceCache::sideDirXYMoves[8][2] =
{
	{ +1, +0 }, // forward
	{ -1, +0 }, // back
	{ +0, -1 }, // left
	{ +0, +1 }, // right
	{ +1, -1 }, // front left
	{ +1, +1 }, // front right
	{ -1, -1 }, // back left
	{ -1, +1 }, // back right
};

const float BotEnvironmentTraceCache::sideDirXYFractions[8][2] =
{
	{ +1.000f, +0.000f }, // front
	{ -1.000f, +0.000f }, // back
	{ +0.000f, -1.000f }, // left
	{ +0.000f, +1.000f }, // right
	{ +0.707f, -0.707f }, // front left
	{ +0.707f, +0.707f }, // front right
	{ -0.707f, -0.707f }, // back left
	{ -0.707f, +0.707f }, // back right
};

inline void BotEnvironmentTraceCache::MakeTraceDir( unsigned dirNum, const vec3_t front2DDir,
													const vec3_t right2DDir, vec3_t traceDir ) {
	const float *dirFractions = sideDirXYFractions[dirNum];
	VectorScale( front2DDir, dirFractions[0], traceDir );
	VectorMA( traceDir, dirFractions[1], right2DDir, traceDir );
	VectorNormalizeFast( traceDir );
}

bool BotEnvironmentTraceCache::TrySkipTracingForCurrOrigin( BotMovementPredictionContext *context,
															const vec3_t front2DDir, const vec3_t right2DDir ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const int areaNum = entityPhysicsState.CurrAasAreaNum();
	if( !areaNum ) {
		return false;
	}

	const auto *aasWorld = AiAasWorld::Instance();
	const auto &area = aasWorld->Areas()[areaNum];
	const auto &areaSettings = aasWorld->AreaSettings()[areaNum];
	const float *origin = entityPhysicsState.Origin();

	// Extend playerbox XY bounds by TRACE_DEPTH
	Vec3 mins( origin[0] - TRACE_DEPTH, origin[1] - TRACE_DEPTH, origin[2] );
	Vec3 maxs( origin[0] + TRACE_DEPTH, origin[1] + TRACE_DEPTH, origin[2] );
	mins += playerbox_stand_mins;
	maxs += playerbox_stand_maxs;

	// We have to add some offset to the area bounds (an area is not always a box)
	const float areaBoundsOffset = ( areaSettings.areaflags & AREA_WALL ) ? 40.0f : 16.0f;

	int sideNum = 0;
	for(; sideNum < 2; ++sideNum ) {
		if( area.mins[sideNum] + areaBoundsOffset >= mins.Data()[sideNum] ) {
			break;
		}
		if( area.maxs[sideNum] + areaBoundsOffset <= maxs.Data()[sideNum] ) {
			break;
		}
	}

	// If the area bounds test has lead to conclusion that there is enough free space in side directions
	if( sideNum == 2 ) {
		if( CanSkipTracingForAreaHeight( origin, area, playerbox_stand_mins[2] + 0.25f ) ) {
			SetFullHeightCachedTracesEmpty( front2DDir, right2DDir );
			return true;
		}

		if( CanSkipTracingForAreaHeight( origin, area, playerbox_stand_maxs[2] + AI_JUMPABLE_HEIGHT - 0.5f ) ) {
			SetJumpableHeightCachedTracesEmpty( front2DDir, right2DDir );
			// We might still need to perform full height traces in TestForResultsMask()
			return false;
		}
	}

	// Test bounds around the origin.
	// Doing these tests can save expensive trace calls for separate directions

	// Convert these bounds to relative for being used as trace args
	mins -= origin;
	maxs -= origin;

	trace_t trace;
	mins.Z() += 0.25f;
	StaticWorldTrace( &trace, origin, origin, MASK_SOLID | MASK_WATER, mins.Data(), maxs.Data() );
	if( trace.fraction == 1.0f ) {
		SetFullHeightCachedTracesEmpty( front2DDir, right2DDir );
		return true;
	}

	mins.Z() += AI_JUMPABLE_HEIGHT - 1.0f;
	StaticWorldTrace( &trace, origin, origin, MASK_SOLID | MASK_WATER, mins.Data(), maxs.Data() );
	if( trace.fraction == 1.0f ) {
		SetJumpableHeightCachedTracesEmpty( front2DDir, right2DDir );
		// We might still need to perform full height traces in TestForResultsMask()
		return false;
	}

	return false;
}

void BotEnvironmentTraceCache::TestForResultsMask( BotMovementPredictionContext *context, unsigned requiredResultsMask ) {
	// There must not be any extra bits
	Assert( ( requiredResultsMask & ~ALL_SIDES_MASK ) == 0 );
	// All required traces have been already cached
	if( ( this->resultsMask & requiredResultsMask ) == requiredResultsMask ) {
		return;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	vec3_t front2DDir, right2DDir, traceEnd;
	Vec3 angles( entityPhysicsState.Angles() );
	angles.Data()[PITCH] = 0.0f;
	AngleVectors( angles.Data(), front2DDir, right2DDir, nullptr );

	if( !this->didAreaTest ) {
		this->didAreaTest = true;
		if( TrySkipTracingForCurrOrigin( context, front2DDir, right2DDir ) ) {
			return;
		}
	}

	const float *origin = entityPhysicsState.Origin();

	// First, test all full side traces.
	// If a full side trace is empty, a corresponding "jumpable" side trace can be set as empty too.

	// Test these bits for a quick computations shortcut
	unsigned actualFullSides = this->resultsMask & FULL_SIDES_MASK;
	unsigned resultFullSides = requiredResultsMask & FULL_SIDES_MASK;
	if( ( actualFullSides & resultFullSides ) != resultFullSides ) {
		const unsigned endMask = FullHeightMask( LAST_SIDE );
		for( unsigned i = 0, mask = FullHeightMask( FIRST_SIDE ); mask <= endMask; ++i, mask *= 2 ) {
			if( !( mask & requiredResultsMask ) || ( mask & this->resultsMask ) ) {
				continue;
			}

			MakeTraceDir( i, front2DDir, right2DDir, traceEnd );
			// Save the trace dir
			auto &fullResult = results[i];
			VectorCopy( traceEnd, fullResult.traceDir );
			// Convert from a direction to the end point
			VectorScale( traceEnd, TRACE_DEPTH, traceEnd );
			VectorAdd( traceEnd, origin, traceEnd );
			StaticWorldTrace( &fullResult.trace, origin, traceEnd, MASK_SOLID | MASK_WATER,
							  playerbox_stand_mins, playerbox_stand_maxs );
			this->resultsMask |= mask;
			// If full trace is empty, we can set partial trace as empty too
			if( fullResult.trace.fraction == 1.0f ) {
				auto &jumpableResult = results[i + 6];
				jumpableResult.trace.fraction = 1.0f;
				VectorCopy( fullResult.traceDir, jumpableResult.traceDir );
				this->resultsMask |= ( mask << 6 );
			}
		}
	}

	unsigned actualJumpableSides = this->resultsMask & JUMPABLE_SIDES_MASK;
	unsigned resultJumpableSides = requiredResultsMask & JUMPABLE_SIDES_MASK;
	if( ( actualJumpableSides & resultJumpableSides ) != resultJumpableSides ) {
		Vec3 mins( playerbox_stand_mins );
		mins.Z() += AI_JUMPABLE_HEIGHT;
		const unsigned endMask = JumpableHeightMask( LAST_SIDE );
		for( unsigned i = 0, mask = JumpableHeightMask( FIRST_SIDE ); mask <= endMask; ++i, mask *= 2 ) {
			if( !( mask & requiredResultsMask ) || ( mask & this->resultsMask ) ) {
				continue;
			}

			MakeTraceDir( i, front2DDir, right2DDir, traceEnd );
			// Save the trace dir
			auto &result = results[i + 6];
			VectorCopy( traceEnd, result.traceDir );
			// Convert from a direction to the end point
			VectorScale( traceEnd, TRACE_DEPTH, traceEnd );
			VectorAdd( traceEnd, origin, traceEnd );
			StaticWorldTrace( &result.trace, origin, traceEnd, MASK_SOLID | MASK_WATER, mins.Data(), playerbox_stand_maxs );
			this->resultsMask |= mask;
		}
	}

	// Check whether all requires side traces have been computed
	Assert( ( this->resultsMask & requiredResultsMask ) == requiredResultsMask );
}

bool BotEnvironmentTraceCache::CanSkipPMoveCollision( BotMovementPredictionContext *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	// We might still need to check steps even if there is no full height obstacles around.
	if( entityPhysicsState.GroundEntity() ) {
		return false;
	}

	// If the bot does not move upwards
	if( entityPhysicsState.HeightOverGround() <= 12.0f && entityPhysicsState.Velocity()[2] <= 10 ) {
		return false;
	}

	const float expectedShift = entityPhysicsState.Speed() * context->predictionStepMillis * 0.001f;
	const int areaFlags = aasAreaSettings[entityPhysicsState.CurrAasAreaNum()].areaflags;
	// All greater shift flags imply this (and other lesser ones flags) flag being set too
	if( areaFlags & AREA_SKIP_COLLISION_16 ) {
		const float precomputedShifts[3] = { 16.0f, 32.0f, 48.0f };
		const int flagsForShifts[3] = { AREA_SKIP_COLLISION_16, AREA_SKIP_COLLISION_32, AREA_SKIP_COLLISION_48 };
		// Start from the minimal shift
		for( int i = 0; i < 3; ++i ) {
			if( ( expectedShift < precomputedShifts[i] ) && ( areaFlags & flagsForShifts[i] ) ) {
				return true;
			}
		}
	}

	// Do not force computations in this case.
	// Otherwise there is no speedup shown according to testing results.
	if( !this->didAreaTest ) {
		return false;
	}

	// Return the already computed result
	return this->hasNoFullHeightObstaclesAround;
}

// Make a type alias to fit into a line length limit
typedef BotEnvironmentTraceCache::ObstacleAvoidanceResult ObstacleAvoidanceResult;

ObstacleAvoidanceResult BotEnvironmentTraceCache::TryAvoidObstacles( BotMovementPredictionContext *context,
																	 Vec3 *intendedLookVec,
																	 float correctionFraction,
																	 unsigned sidesShift ) {
	TestForResultsMask( context, FRONT << sidesShift );
	const TraceResult &frontResult = results[0 + sidesShift];
	if( frontResult.trace.fraction == 1.0f ) {
		return ObstacleAvoidanceResult::NO_OBSTACLES;
	}

	TestForResultsMask( context, ( LEFT | RIGHT | FRONT_LEFT | FRONT_RIGHT ) << sidesShift );

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	Vec3 velocityDir2D( entityPhysicsState.Velocity()[0], entityPhysicsState.Velocity()[1], 0 );
	velocityDir2D *= 1.0f / entityPhysicsState.Speed2D();

	// Make velocity direction dot product affect score stronger for lower correction fraction (for high speed)

	// This weight corresponds to a kept part of a trace fraction
	const float alpha = 0.51f + 0.24f * correctionFraction;
	// This weight corresponds to an added or subtracted part of a trace fraction multiplied by the dot product
	const float beta = 0.49f - 0.24f * correctionFraction;
	// Make sure a score is always positive
	Assert( alpha > beta );
	// Make sure that score is kept as is for the maximal dot product
	Assert( fabsf( alpha + beta - 1.0f ) < 0.0001f );

	float maxScore = frontResult.trace.fraction * ( alpha + beta * velocityDir2D.Dot( results[0 + sidesShift].traceDir ) );
	const TraceResult *bestTraceResult = nullptr;

	for( unsigned i = 2; i <= 5; ++i ) {
		const TraceResult &result = results[i + sidesShift];
		float score = result.trace.fraction;
		// Make sure that non-blocked directions are in another category
		if( score == 1.0f ) {
			score *= 3.0f;
		}

		score *= alpha + beta * velocityDir2D.Dot( result.traceDir );
		if( score <= maxScore ) {
			continue;
		}

		maxScore = score;
		bestTraceResult = &result;
	}

	if( bestTraceResult ) {
		intendedLookVec->NormalizeFast();
		*intendedLookVec *= ( 1.0f - correctionFraction );
		VectorMA( intendedLookVec->Data(), correctionFraction, bestTraceResult->traceDir, intendedLookVec->Data() );
		// There is no need to normalize intendedLookVec (we had to do it for correction fraction application)
		return ObstacleAvoidanceResult::CORRECTED;
	}

	return ObstacleAvoidanceResult::KEPT_AS_IS;
}

bool BotUseStairsExitMovementFallback::TryDeactivate( BotMovementPredictionContext *context ) {
	// Call the superclass method first
	if( BotGenericGroundMovementFallback::TryDeactivate( context ) ) {
		return true;
	}

	if( BotGenericGroundMovementFallback::ShouldSkipTests( context ) ) {
		return false;
	}

	const auto *aasAreaStairsClusterNums = AiAasWorld::Instance()->AreaStairsClusterNums();

	int areaNums[2] = { 0, 0 };
	int numBotAreas = GetCurrBotAreas( areaNums );
	for( int i = 0; i < numBotAreas; ++i ) {
		const int areaNum = areaNums[i];
		// The bot has entered the exit area (make sure this condition is first)
		if( areaNum == this->exitAreaNum ) {
			status = COMPLETED;
			return true;
		}
		// The bot is still in the same stairs cluster
		if( aasAreaStairsClusterNums[areaNum] == stairsClusterNum ) {
			assert( status == PENDING );
			return false;
		}
	}

	// The bot is neither in the same stairs cluster nor in the cluster exit area
	status = INVALID;
	return true;
}

bool BotUseRampExitMovementFallback::TryDeactivate( BotMovementPredictionContext *context ) {
	// Call the superclass method first
	if( BotGenericGroundMovementFallback::TryDeactivate( context ) ) {
		return true;
	}

	if( BotGenericGroundMovementFallback::ShouldSkipTests( context ) ) {
		return false;
	}

	int areaNums[2] = { 0, 0 };
	const int numBotAreas = GetCurrBotAreas( areaNums, context );
	for( int i = 0; i < numBotAreas; ++i ) {
		const int areaNum = areaNums[i];
		// The bot has entered the exit area (make sure this condition is first)
		if( areaNum == exitAreaNum ) {
			status = COMPLETED;
			return true;
		}
		// The bot is still on the same ramp area
		if( areaNum == rampAreaNum ) {
			assert( status == PENDING );
			return false;
		}
	}

	// The bot is neither on the ramp nor in the ramp exit area
	status = INVALID;
	return true;
}

bool BotUseWalkableTriggerMovementFallback::TryDeactivate( BotMovementPredictionContext *context ) {
	if( BotGenericGroundMovementFallback::TryDeactivate( context ) ) {
		return true;
	}

	if( BotGenericGroundMovementFallback::ShouldSkipTests( context ) ) {
		return false;
	}

	if( level.time - activatedAt > 750 ) {
		status = INVALID;
		return true;
	}

	vec3_t targetOrigin;
	GetSteeringTarget( targetOrigin );
	if( !TestActualWalkability( triggerAreaNumsCache.GetAreaNum( ENTNUM( trigger ) ), targetOrigin, context ) ) {
		status = INVALID;
		return true;
	}

	return false;
}

bool BotUseWalkableNodeMovementFallback::TryDeactivate( BotMovementPredictionContext *context ) {
	// Call the superclass method first
	if( BotGenericGroundMovementFallback::TryDeactivate( context ) ) {
		return true;
	}

	// If the spot can be reached by radius
	const float *botOrigin = context ? context->movementState->entityPhysicsState.Origin() : self->s.origin;
	if( Distance2DSquared( botOrigin, nodeOrigin ) < SQUARE( reachRadius ) ) {
		status = COMPLETED;
		return true;
	}

	if( BotGenericGroundMovementFallback::ShouldSkipTests( context )) {
		return false;
	}

	if( level.time - activatedAt > timeout ) {
		status = INVALID;
		return true;
	}

	if( !TestActualWalkability( nodeAasAreaNum, nodeOrigin, context ) ) {
		status = INVALID;
		return true;
	}

	return false;
}

bool BotGenericGroundMovementFallback::TestActualWalkability( int targetAreaNum, const vec3_t targetOrigin,
															  BotMovementPredictionContext *context ) {
	const AiEntityPhysicsState *entityPhysicsState;
	if( context ) {
		entityPhysicsState = &context->movementState->entityPhysicsState;
	} else {
		entityPhysicsState = self->ai->botRef->EntityPhysicsState();
	}

	const auto &routeCache = self->ai->botRef->routeCache;
	int fromAreaNums[2];
	const int numFromAreas = entityPhysicsState->PrepareRoutingStartAreas( fromAreaNums );
	int travelTimeToTarget = 0;
	for( int j = 0; j < numFromAreas; ++j ) {
		travelTimeToTarget = routeCache->TravelTimeToGoalArea( fromAreaNums[j], targetAreaNum, TRAVEL_FLAGS );
		if( travelTimeToTarget && travelTimeToTarget <= 300 ) {
			break;
		}
	}

	// If the target is not reachable or the travel time is way too large now
	if( !travelTimeToTarget || travelTimeToTarget > 300 ) {
		return false;
	}

	// Test whether the spot can be still considered walkable by results of a coarse trace test

	// TODO: Use floor cluster raycasting too (if the target is in the same floor cluster)?
	// TODO: Use nav mesh first to cut off expensive trace computations?
	// There would be problems though:
	// Floor cluster raycasting is limited and still requires a final trace test
	// Nav mesh raycasting is not reliable due to poor quality of produced nav meshes.

	trace_t trace;
	vec3_t traceMins, traceMaxs;
	TacticalSpotsRegistry::GetSpotsWalkabilityTraceBounds( traceMins, traceMaxs );
	float *start = const_cast<float *>( entityPhysicsState->Origin() );
	float *end = const_cast<float *>( targetOrigin );
	edict_t *skip = const_cast<edict_t *>( self );
	// Allow hitting triggers (for "walk to a trigger" fallbacks)
	// Otherwise a trace hits at a solid brush behind a trigger and we witness a false negative.
	G_Trace( &trace, start, traceMins, traceMaxs, end, skip, MASK_PLAYERSOLID | CONTENTS_TRIGGER );
	if( trace.fraction != 1.0f ) {
		if( game.edicts[trace.ent].r.solid != SOLID_TRIGGER ) {
			return false;
		}
	}

	return true;
}

void BotJumpToSpotMovementFallback::Activate( const vec3_t startOrigin_,
											  const vec3_t targetOrigin_,
											  unsigned timeout,
											  float reachRadius_,
											  float startAirAccelFrac_,
											  float endAirAccelFrac_,
											  float jumpBoostSpeed_ ) {
	VectorCopy( targetOrigin_, this->targetOrigin );
	VectorCopy( startOrigin_, this->startOrigin );
	this->timeout = timeout + 150u;
	this->reachRadius = reachRadius_;
	clamp( startAirAccelFrac_, 0.0f, 1.0f );
	clamp( endAirAccelFrac_, 0.0f, 1.0f );
	this->startAirAccelFrac = startAirAccelFrac_;
	this->endAirAccelFrac = endAirAccelFrac_;
	this->jumpBoostSpeed = jumpBoostSpeed_;
	this->hasAppliedJumpBoost = false;

	// Check whether there is no significant difference in start and target height
	// (velocity correction looks weird in that case)
	this->allowCorrection = false;
	Vec3 jumpVec( targetOrigin );
	jumpVec -= startOrigin;
	if( fabsf( jumpVec.Z() ) / sqrtf( jumpVec.X() * jumpVec.X() + jumpVec.Y() * jumpVec.Y() ) < 0.2f ) {
		this->allowCorrection = true;
	}

	BotMovementFallback::Activate();
}

bool BotJumpToSpotMovementFallback::TryDeactivate( BotMovementPredictionContext *context ) {
	assert( status == PENDING );

	// If the fallback is still active, invalidate it
	if ( level.time - activatedAt > std::max( 250u, timeout ) ) {
		return DeactivateWithStatus( INVALID );
	}

	// If the fallback movement has just started, skip tests
	if( level.time - activatedAt < std::min( 250u, timeout ) ) {
		return false;
	}

	const AiEntityPhysicsState *entityPhysicsState;
	if( context ) {
		entityPhysicsState = &context->movementState->entityPhysicsState;
	} else {
		entityPhysicsState = self->ai->botRef->EntityPhysicsState();
	}

	// Wait until the target is reached
	if( DistanceSquared( entityPhysicsState->Origin(), targetOrigin ) < reachRadius * reachRadius ) {
		return false;
	}

	const auto *aasAreaSettings = AiAasWorld::Instance()->AreaSettings();

	int currAreaNums[2] = { 0, 0 };
	const int numCurrAreas = entityPhysicsState->PrepareRoutingStartAreas( currAreaNums );

	// First, check whether we have entered any area with disabled flags
	for( int i = 0; i < numCurrAreas; ++i ) {
		const auto &areaSettings = aasAreaSettings[currAreaNums[i]];
		if( ( areaSettings.contents & undesiredAasContents ) || ( areaSettings.areaflags & undesiredAasFlags ) ) {
			return DeactivateWithStatus( INVALID );
		}
	}

	// Second, check whether we have entered some area with satisfying flags
	for( int i = 0; i < numCurrAreas; ++i ) {
		const auto &areaSettings = aasAreaSettings[currAreaNums[i]];
		if( ( areaSettings.contents & desiredAasContents ) || ( areaSettings.areaflags & desiredAasFlags ) ) {
			if( entityPhysicsState->GroundEntity() ) {
				return DeactivateWithStatus( COMPLETED );
			} else if( context ) {
				// Try reusing this value that is very likely to be cached
				if( context->CanSafelyKeepHighSpeed() ) {
					return DeactivateWithStatus( COMPLETED );
				}
			} else if( self->ai->botRef->TestWhetherCanSafelyKeepHighSpeed( nullptr ) ) {
				return DeactivateWithStatus( COMPLETED );
			}
		}
	}

	return false;
}

void BotJumpToSpotMovementFallback::SetupMovement( BotMovementPredictionContext *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	auto *botInput = &context->record->botInput;

	// Start Z is rather important, don't use entity origin as-is
	Vec3 toTargetDir( entityPhysicsState.Origin() );
	toTargetDir.Z() += self->viewheight;
	toTargetDir -= targetOrigin;
	toTargetDir *= -1.0f;
	toTargetDir.Normalize();

	int forwardMovement = 1;
	float viewDot = toTargetDir.Dot( entityPhysicsState.ForwardDir() );
	if( viewDot < 0 ) {
		toTargetDir *= -1.0f;
		forwardMovement = -1;
	}

	botInput->SetIntendedLookDir( toTargetDir, true );

	// Note: we do not check only 2D dot product but the dot product in all dimensions intentionally
	// (a bot often has to look exactly at the spot that might be above).
	// Setting exact view angles is important
	if( fabsf( viewDot ) < 0.99f ) {
		// This huge value is important, otherwise bot falls down in some cases
		botInput->SetTurnSpeedMultiplier( 15.0f );
		return;
	}

	botInput->SetForwardMovement( forwardMovement );

	if( !entityPhysicsState.GroundEntity() ) {
		if( !hasAppliedJumpBoost ) {
			hasAppliedJumpBoost = true;
			// Avoid weird-looking behavior, only boost jumping in case when bot has just started a jump
			if( jumpBoostSpeed > 0 && entityPhysicsState.Velocity()[2] > context->GetJumpSpeed() - 30 ) {
				Vec3 modifiedVelocity( entityPhysicsState.Velocity() );
				modifiedVelocity.Z() += jumpBoostSpeed;
				context->record->SetModifiedVelocity( modifiedVelocity );
				return;
			}
		}

		float jumpDistance2D = sqrtf( Distance2DSquared( this->startOrigin, this->targetOrigin ) );
		if( jumpDistance2D < 16.0f ) {
			return;
		}

		float distanceToTarget2D = sqrtf( Distance2DSquared( entityPhysicsState.Origin(), this->targetOrigin ) );
		float distanceFrac = distanceToTarget2D / jumpDistance2D;

		float accelFrac = startAirAccelFrac + distanceFrac * ( endAirAccelFrac - startAirAccelFrac );
		if( accelFrac > 0 ) {
			clamp_high( accelFrac, 1.0f );
			context->CheatingAccelerate( accelFrac );
		}

		if( allowCorrection && entityPhysicsState.Speed2D() > 100 ) {
			// If the movement has been inverted
			if( forwardMovement < 0 ) {
				// Restore the real to target dir that has been inverted
				toTargetDir *= -1.0f;
			}

			// Check whether a correction should be really applied
			// Otherwise it looks weird (a bot starts fly ghosting near the target like having a jetpack).
			Vec3 toTargetDir2D( targetOrigin );
			toTargetDir2D -= entityPhysicsState.Origin();
			toTargetDir2D.Z() = 0;
			toTargetDir2D *= 1.0f / distanceToTarget2D;

			Vec3 velocity2D( entityPhysicsState.Velocity() );
			velocity2D.Z() = 0;
			velocity2D *= 1.0f / entityPhysicsState.Speed2D();

			float velocity2DDotToTargetDir2D = velocity2D.Dot( toTargetDir2D );
			if( velocity2DDotToTargetDir2D < 0.9f ) {
				context->CheatingCorrectVelocity( velocity2DDotToTargetDir2D, toTargetDir2D );
			}
		}

		// Crouch in-air to reduce chances of hitting an obstacle
		botInput->SetUpMovement( -1 );
		return;
	}

	// Wait for accelerating on ground, except there is an obstacle or a gap
	if( entityPhysicsState.Speed2D() < context->GetRunSpeed() - 30 ) {
		// First, utilize the obstacles cache used in prediction
		auto *traceCache = &context->EnvironmentTraceCache();
		bool shouldJumpOverObstacle;
		if( forwardMovement > 0 ) {
			traceCache->TestForResultsMask( context, traceCache->FullHeightMask( BotEnvironmentTraceCache::FRONT ) );
			shouldJumpOverObstacle = !traceCache->FullHeightFrontTrace().IsEmpty();
		} else {
			traceCache->TestForResultsMask( context, traceCache->FullHeightMask( BotEnvironmentTraceCache::BACK ) );
			shouldJumpOverObstacle = !traceCache->FullHeightBackTrace().IsEmpty();
		}
		if( shouldJumpOverObstacle ) {
			botInput->SetUpMovement( 1 );
			// Setting it is important too (helps if skimming gets triggered)
			botInput->SetForwardMovement( forwardMovement );
			return;
		}

		// Check whether there is a gap in front of the bot
		trace_t trace;
		Vec3 traceStart( entityPhysicsState.ForwardDir() );
		traceStart *= forwardMovement;
		traceStart *= 24.0f;
		traceStart += entityPhysicsState.Origin();
		Vec3 traceEnd( traceStart );
		traceEnd.Z() -= 40.0f;
		edict_t *ignore = const_cast<edict_t *>( self );
		G_Trace( &trace, traceStart.Data(), nullptr, nullptr, traceEnd.Data(), ignore, MASK_PLAYERSOLID );
		// If there is no gap or hazard in front of the bot, wait for accelerating on ground
		if( trace.fraction != 1.0f && !( trace.contents & ( CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_NODROP ) ) ) {
			return;
		}
	}

	// Try dashing
	if( entityPhysicsState.Origin()[2] >= targetOrigin[2] ) {
		const auto *pmStats = context->currPlayerState->pmove.stats;
		// If a dash can succeed, return
		if( ( pmStats[PM_STAT_FEATURES] & PMFEAT_JUMP ) && !pmStats[PM_STAT_DASHTIME] ) {
			botInput->SetSpecialButton( true );
			return;
		}
	}

	// Jump in all other cases
	botInput->SetUpMovement( 1 );
}

// Generalizes jumpable/weapon-jumpable spots detection
class BestJumpableSpotDetector {
public:
	struct SpotAndScore {
		vec3_t origin;
		float score;

		SpotAndScore( const vec3_t origin_, float score_ ) {
			VectorCopy( origin_, this->origin );
			this->score = score_;
		}

		// Do not negate the comparison result as we usually do, since spots are assumed to be stored in a max-heap
		bool operator<( const SpotAndScore &that ) const { return this->score < that.score; }
	};
protected:
	AiTrajectoryPredictor predictor;
	AiTrajectoryPredictor::Results predictionResults;
	vec3_t startOrigin;

	// A returned range should be a max-heap (best spots be evicted first)
	virtual void GetCandidateSpots( SpotAndScore **begin, SpotAndScore **end ) = 0;

	// Can be overridden in different ways, e.g.:
	// for regular jumping velocity XY is a 2 direction to spot multiplied by run speed, Z is the jump speed
	// for weapon jumping velocity is a direction to spot multiplied by a value derived from knockback and player mass
	virtual void GetVelocityForJumpingToSpot( vec3_t velocity, const vec3_t spot ) = 0;
public:
	BestJumpableSpotDetector() {
		auto badAreaContents = AREACONTENTS_LAVA | AREACONTENTS_SLIME | AREACONTENTS_WATER | AREACONTENTS_DONOTENTER;
		predictor.SetEnterAreaProps( 0, badAreaContents );
		predictor.AddStopEventFlags( AiTrajectoryPredictor::HIT_SOLID );
	}

	virtual ~BestJumpableSpotDetector() {}

	inline void SetStepMillis( unsigned stepMillis ) {
		predictor.SetStepMillis( stepMillis );
	}

	inline void SetNumSteps( unsigned numSteps ) {
		predictor.SetNumSteps( numSteps );
	}

	// Might vary for different purposes, e.g.
	// for rocketjumping a full playerbox should be used, for barrier jumping we can zero mins
	inline void SetColliderBounds( const vec3_t mins, const vec3_t maxs ) {
		predictor.SetColliderBounds( mins, maxs );
	}

	// Returns an estimated jump travel time on success or zero on failure
	virtual unsigned Exec( const vec3_t startOrigin_, vec3_t spotOrigin_ );
};

// We make it static non-memeber function to avoid lifting SpotAndScore to headers
// The function assumes that the range (spotsBegin, spotsEnd) points to a max-heap
unsigned BestJumpableSpotDetector::Exec( const vec3_t startOrigin_, vec3_t spotOrigin_ ) {
	VectorCopy( startOrigin_, this->startOrigin );
	this->startOrigin[2] += 4.0f;

	constexpr auto badCmContents = CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_DONOTENTER;

	// Make sure we can spot missing initialization easily
	SpotAndScore *spotsBegin = nullptr, *spotsEnd = nullptr;
	GetCandidateSpots( &spotsBegin, &spotsEnd );

#ifndef PUBLIC_BUILD
	if( !std::is_heap( spotsBegin, spotsEnd ) ) {
		AI_FailWith( "BestJumpableSpotsDetector::Exec()", "A given spots range must point to a max-heap\n" );
	}
#endif

	while( spotsBegin != spotsEnd ) {
		// Evict the best candidate from the heap
		std::pop_heap( spotsBegin, spotsEnd );
		const SpotAndScore *spotAndScore = spotsEnd - 1;
		spotsEnd--;

		vec3_t velocityForJumping;
		GetVelocityForJumpingToSpot( velocityForJumping, spotAndScore->origin );

		predictionResults.Clear();
		auto predictionStopEvent = predictor.Run( velocityForJumping, startOrigin, &predictionResults );

		// A bot has entered an area of "bad" contents
		if( predictionStopEvent & AiTrajectoryPredictor::ENTER_AREA_CONTENTS ) {
			continue;
		}

		if( predictionStopEvent & ( AiTrajectoryPredictor::HIT_SOLID | AiTrajectoryPredictor::HIT_LIQUID ) ) {
			if( !ISWALKABLEPLANE( &predictionResults.trace->plane ) ) {
				continue;
			}
			if( predictionResults.trace->contents & badCmContents ) {
				continue;
			}
		} else {
			// Do a ground sampling
			trace_t trace;
			Vec3 traceEnd( predictionResults.origin );
			traceEnd.Z() -= 64.0f;
			StaticWorldTrace( &trace, predictionResults.origin, traceEnd.Data(), MASK_SOLID | MASK_WATER );
			if( trace.fraction == 1.0f ) {
				continue;
			}
			if( !ISWALKABLEPLANE( &trace.plane ) ) {
				continue;
			}
			if( trace.contents & badCmContents ) {
				continue;
			}
		}

		VectorCopy( spotAndScore->origin, spotOrigin_ );

#ifndef PUBLIC_BUILD
		// Make sure the returned timeout value is feasible
		if( predictionResults.millisAhead > 9999 ) {
			AI_FailWith( "BestJumpableSpotsDetector::Exec()", "Don't extrapolate last step (we need a valid timeout)\n" );
		}
#endif
		return predictionResults.millisAhead;
	}

	return 0;
}

class BestRegularJumpableSpotDetector: public BestJumpableSpotDetector {
	float run2DSpeed;
	float jumpZSpeed;
protected:
	void GetVelocityForJumpingToSpot( vec3_t velocity, const vec3_t spot ) override final {
		VectorSubtract( spot, startOrigin, velocity );
		velocity[2] = 0;
		float scale2D =  run2DSpeed / sqrtf( SQUARE( velocity[0] ) + SQUARE( velocity[1] ) );
		velocity[0] *= scale2D;
		velocity[1] *= scale2D;
		velocity[2] = jumpZSpeed;
	}

	const AiAasWorld *aasWorld;
	// If set, a travel time to a nav target is used as a criterion instead of distance proximity
	const AiAasRouteCache *routeCache;
	int navTargetAreaNum;
	int startTravelTimeToTarget;
public:
	void SetJumpPhysicsProps( float run2DSpeed_, float jumpZSpeed_ ) {
		this->run2DSpeed = run2DSpeed_;
		this->jumpZSpeed = jumpZSpeed_;
	}

	BestRegularJumpableSpotDetector()
		: run2DSpeed( 0.0f ),
		  jumpZSpeed( 0.0f ),
		  aasWorld( nullptr ),
		  routeCache( nullptr ),
		  navTargetAreaNum( 0 ),
		  startTravelTimeToTarget( 0 ) {}

	inline void AddRoutingParams( const AiAasRouteCache *routeCache_, int navTargetAreaNum_, int startTravelTimeToTarget_ ) {
		this->routeCache = routeCache_;
		this->navTargetAreaNum = navTargetAreaNum_;
		this->startTravelTimeToTarget = startTravelTimeToTarget_;
	}

	unsigned Exec( const vec3_t startOrigin_, vec3_t spotOrigin_ ) override {
#ifndef PUBLIC_BUILD
		if( run2DSpeed <= 0 || jumpZSpeed <= 0 ) {
			AI_FailWith( "BestRegularJumpableSpotsDetector::Exec()", "Illegal jump physics props (have they been set?)\n" );
		}
#endif
		// Cannot be initialized in constructor called for the global instance
		this->aasWorld = AiAasWorld::Instance();
		unsigned result = BestJumpableSpotDetector::Exec( startOrigin_, spotOrigin_ );
		run2DSpeed = 0.0f;
		jumpZSpeed = 0.0f;
		// Nulliffy supplied references to avoid unintended reusing
		this->routeCache = nullptr;
		this->navTargetAreaNum = 0;
		this->startTravelTimeToTarget = 0;
		return result;
	}
};

class BestAreaCenterJumpableSpotDetector: public BestRegularJumpableSpotDetector {
	StaticVector<SpotAndScore, 64> spotsHeap;
	void GetCandidateSpots( SpotAndScore **begin, SpotAndScore **end ) override;
	inline int GetBoxAreas( int *boxAreaNums, int maxAreaNums );
	void FillCandidateSpotsUsingRoutingTest( const int *boxAreaNums, int numBoxAreas );
	void FillCandidateSpotsWithoutRoutingTest( const int *boxAreaNums, int numBoxAreas );
	inline bool TestAreaSettings( const aas_areasettings_t &areaSettings );
};

static BestAreaCenterJumpableSpotDetector bestAreaCenterJumpableSpotDetector;

inline bool BestAreaCenterJumpableSpotDetector::TestAreaSettings( const aas_areasettings_t &areaSettings ) {
	if( !( areaSettings.areaflags & ( AREA_GROUNDED ) ) ) {
		return false;
	}
	if( areaSettings.areaflags & ( AREA_DISABLED | AREA_JUNK ) ) {
		return false;
	}
	if( areaSettings.contents & ( AREACONTENTS_WATER | AREACONTENTS_LAVA | AREACONTENTS_SLIME | AREACONTENTS_DONOTENTER ) ) {
		return false;
	}
	return true;
}

int BestAreaCenterJumpableSpotDetector::GetBoxAreas( int *boxAreaNums, int maxAreaNums ) {
	Vec3 boxMins( -128, -128, -32 );
	Vec3 boxMaxs( +128, +128, +64 );
	boxMins += startOrigin;
	boxMaxs += startOrigin;
	return aasWorld->BBoxAreas( boxMins, boxMaxs, boxAreaNums, maxAreaNums );
}

void BestAreaCenterJumpableSpotDetector::GetCandidateSpots( SpotAndScore **begin, SpotAndScore **end ) {
	spotsHeap.clear();

	int boxAreaNums[64];
	int numBoxAreas = GetBoxAreas( boxAreaNums, 64 );
	if( routeCache ) {
		FillCandidateSpotsUsingRoutingTest( boxAreaNums, numBoxAreas );
	} else {
		FillCandidateSpotsWithoutRoutingTest( boxAreaNums, numBoxAreas );
	}

	std::make_heap( spotsHeap.begin(), spotsHeap.end() );

	*begin = spotsHeap.begin();
	*end = spotsHeap.end();
}

void BestAreaCenterJumpableSpotDetector::FillCandidateSpotsUsingRoutingTest( const int *boxAreaNums, int numBoxAreas ) {
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const float areaPointZOffset = 1.0f - playerbox_stand_mins[2];

	for( int i = 0; i < numBoxAreas; ++i ) {
		const int areaNum = boxAreaNums[i];
		if( !TestAreaSettings( aasAreaSettings[areaNum] ) ) {
			continue;
		}

		const auto &area = aasAreas[areaNum];
		Vec3 areaPoint( area.center );
		areaPoint.Z() = area.mins[2] + areaPointZOffset;
		if( areaPoint.SquareDistanceTo( startOrigin ) < SQUARE( 64.0f ) ) {
			continue;
		}
		int travelTime = routeCache->PreferredRouteToGoalArea( areaNum, navTargetAreaNum );
		if( !travelTime || travelTime > startTravelTimeToTarget ) {
			continue;
		}
		// Otherwise no results are produced
		areaPoint.Z() += playerbox_stand_maxs[2];
		// Use the negated travel time as a score for the max-heap (closest to target spot gets evicted first)
		new( spotsHeap.unsafe_grow_back() )SpotAndScore( areaPoint.Data(), -travelTime );
	}
}

void BestAreaCenterJumpableSpotDetector::FillCandidateSpotsWithoutRoutingTest( const int *boxAreaNums, int numBoxAreas ) {
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const float areaPointZOffset = 1.0f - playerbox_stand_mins[2];

	for( int i = 0; i < numBoxAreas; ++i ) {
		const int areaNum = boxAreaNums[i];
		if( !TestAreaSettings( aasAreaSettings[areaNum] ) ) {
			continue;
		}

		const auto &area = aasAreas[areaNum];
		Vec3 areaPoint( area.center );
		areaPoint.Z() = area.mins[2] + areaPointZOffset;
		float squareDistance = areaPoint.SquareDistanceTo( startOrigin );
		areaPoint.Z() += playerbox_stand_maxs[2];
		// Farthest spots should get evicted first
		new( spotsHeap.unsafe_grow_back() )SpotAndScore( areaPoint.Data(), squareDistance );
	}
}

class BestNavMeshPolyJumpableSpotDetector: public BestRegularJumpableSpotDetector {
	StaticVector<SpotAndScore, 64> spotsHeap;
	const AiNavMeshManager *navMeshManager;

	void GetCandidateSpots( SpotAndScore **begin, SpotAndScore **end ) override;
	inline uint32_t GetStartPolyRef();
	void FillCandidateSpotsUsingRoutingTest( const uint32_t *polyRefs, int numPolyRefs );
	void FillCandidateSpotsWithoutRoutingTest( const uint32_t *polyRefs, int numPolyRefs );
public:
	BestNavMeshPolyJumpableSpotDetector() : navMeshManager( nullptr ), navMeshQuery( nullptr ) {}

	// Should be set for the query owned by the corresponding client
	AiNavMeshQuery *navMeshQuery;

	unsigned Exec( const vec3_t startOrigin_, vec3_t spotOrigin_ ) override {
		navMeshManager = AiNavMeshManager::Instance();
		unsigned result = BestRegularJumpableSpotDetector::Exec( startOrigin_, spotOrigin_ );
		// Nullify the supplied reference to avoid unintended reusing
		navMeshQuery = nullptr;
		return result;
	}
};

static BestNavMeshPolyJumpableSpotDetector bestNavMeshPolyJumpableSpotDetector;

uint32_t BestNavMeshPolyJumpableSpotDetector::GetStartPolyRef() {
	Vec3 polySearchMins( -24, -24, playerbox_stand_mins[2] - 1.0f );
	Vec3 polySearchMaxs( +24, +24, playerbox_stand_maxs[2] );
	polySearchMins += startOrigin;
	polySearchMaxs += startOrigin;
	return navMeshQuery->FindNearestPoly( polySearchMins.Data(), polySearchMaxs.Data() );
}

void BestNavMeshPolyJumpableSpotDetector::GetCandidateSpots( SpotAndScore **begin, SpotAndScore **end ) {
	spotsHeap.clear();

	uint32_t startPolyRef = GetStartPolyRef();
	if( !startPolyRef ) {
		*begin = spotsHeap.begin();
		*end = spotsHeap.end();
		return;
	}

	uint32_t polyRefs[64];
	int numPolyRefs = navMeshQuery->FindPolysInRadius( startPolyRef, 96.0f, polyRefs, 64 );
	if( routeCache ) {
		FillCandidateSpotsUsingRoutingTest( polyRefs, numPolyRefs );
	} else {
		FillCandidateSpotsWithoutRoutingTest( polyRefs, numPolyRefs );
	}

	std::make_heap( spotsHeap.begin(), spotsHeap.end() );

	*begin = spotsHeap.begin();
	*end = spotsHeap.end();
}

void BestNavMeshPolyJumpableSpotDetector::FillCandidateSpotsUsingRoutingTest( const uint32_t *polyRefs, int numPolyRefs ) {
	vec3_t targetOrigin;
	for( int i = 0; i < numPolyRefs; ++i ) {
		navMeshManager->GetPolyCenter( polyRefs[i], targetOrigin );
		// Poly center corresponds to the center of the grounded poly.
		// Add some height above ground
		targetOrigin[2] += -playerbox_stand_mins[2];
		// Dont try jumping to nearby spots
		if( DistanceSquared( startOrigin, targetOrigin ) < SQUARE( 48.0f ) ) {
			continue;
		}
		int areaNum = aasWorld->FindAreaNum( targetOrigin );
		if( !areaNum ) {
			continue;
		}
		int travelTime = routeCache->PreferredRouteToGoalArea( areaNum, navTargetAreaNum );
		if( !travelTime || travelTime > startTravelTimeToTarget ) {
			continue;
		}
		targetOrigin[2] += playerbox_stand_maxs[2];
		// Use the negated travel time as a spot score (closest to target spots should get evicted first)
		new( spotsHeap.unsafe_grow_back() )SpotAndScore( targetOrigin, -travelTime );
	}
}

void BestNavMeshPolyJumpableSpotDetector::FillCandidateSpotsWithoutRoutingTest( const uint32_t *polyRefs, int numPolyRefs ) {
	vec3_t targetOrigin;
	for( int i = 0; i < numPolyRefs; ++i ) {
		navMeshManager->GetPolyCenter( polyRefs[i], targetOrigin );
		// Use greater Z offset in this case
		targetOrigin[2] += -playerbox_stand_mins[2] + playerbox_stand_maxs[2];
		// Dont try cutting off by distance in this case since we're likely to be jumping from lava
		float spotScore = DistanceSquared( startOrigin, targetOrigin );
		targetOrigin[2] += playerbox_stand_maxs[2];
		// Farthest spots should get evicted first
		new( spotsHeap.unsafe_grow_back() )SpotAndScore( targetOrigin, spotScore );
	}
}

// Can't be defined in the header due to accesing a Bot field
BotMovementFallback *BotDummyMovementAction::TryFindJumpAdvancingToTargetFallback( BotMovementPredictionContext *context ) {
	// Let the bot lose its speed first
	if( self->ai->botRef->MillisInBlockedState() < 100 ) {
		return nullptr;
	}

	return TryFindJumpToSpotFallback( context, true );
}

BotMovementFallback *BotDummyMovementAction::TryFindJumpToSpotFallback( BotMovementPredictionContext *context,
																		bool testTravelTime ) {
	// Cut off these extremely expensive computations
	if( !AiManager::Instance()->TryGetExpensiveComputationQuota( self ) ) {
		return nullptr;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	auto *const fallback = &self->ai->botRef->jumpToSpotMovementFallback;

	auto *const areaDetector = &::bestAreaCenterJumpableSpotDetector;
	auto *const polyDetector = &::bestNavMeshPolyJumpableSpotDetector;

	// Prepare auxiliary travel time data
	if( testTravelTime ) {
		int navTargetAreaNum = context->NavTargetAasAreaNum();
		if( !navTargetAreaNum ) {
			return nullptr;
		}
		int startTravelTimeToTarget = context->TravelTimeToNavTarget();
		if( !startTravelTimeToTarget ) {
			return nullptr;
		}
		areaDetector->AddRoutingParams( self->ai->botRef->routeCache, navTargetAreaNum, startTravelTimeToTarget );
		polyDetector->AddRoutingParams( self->ai->botRef->routeCache, navTargetAreaNum, startTravelTimeToTarget );
	}

	vec3_t spotOrigin;
	areaDetector->SetJumpPhysicsProps( context->GetRunSpeed(), context->GetJumpSpeed() );
	if( unsigned jumpTravelTime = areaDetector->Exec( entityPhysicsState.Origin(), spotOrigin ) ) {
		fallback->Activate( entityPhysicsState.Origin(), spotOrigin, jumpTravelTime );
		return fallback;
	}

	// We have found nothing.. Try polys
	if( !self->ai->botRef->navMeshQuery ) {
		self->ai->botRef->navMeshQuery = AiNavMeshManager::Instance()->AllocQuery( self->r.client );
	}

	polyDetector->SetJumpPhysicsProps( context->GetRunSpeed(), context->GetJumpSpeed() );
	polyDetector->navMeshQuery = self->ai->botRef->navMeshQuery;
	if( unsigned jumpTravelTime = polyDetector->Exec( entityPhysicsState.Origin(), spotOrigin ) ) {
		fallback->Activate( entityPhysicsState.Origin(), spotOrigin, jumpTravelTime );
		return fallback;
	}

	return nullptr;
}

class BestConnectedToHubAreasJumpableSpotDetector: public BestRegularJumpableSpotDetector {
	StaticVector<SpotAndScore, 256> spotsHeap;
	void GetCandidateSpots( SpotAndScore **begin, SpotAndScore **end ) override;
public:
	float searchRadius;
	int currAreaNums[2];
	int numCurrAreas;
};

static BestConnectedToHubAreasJumpableSpotDetector bestConnectedToHubAreasJumpableSpotDetector;

BotMovementFallback *BotDummyMovementAction::TryFindLostNavTargetFallback( BotMovementPredictionContext *context ) {
	Assert( !context->NavTargetAasAreaNum());

	// This code is extremely expensive, prevent frametime spikes
	if( AiManager::Instance()->TryGetExpensiveComputationQuota( self ) ) {
		return nullptr;
	}

	vec3_t spotOrigin;
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	auto *detector = &::bestConnectedToHubAreasJumpableSpotDetector;
	detector->searchRadius = 48.0f + 512.0f * BoundedFraction( self->ai->botRef->MillisInBlockedState(), 2000 );
	detector->numCurrAreas = entityPhysicsState.PrepareRoutingStartAreas( detector->currAreaNums );
	detector->SetJumpPhysicsProps( context->GetRunSpeed(), context->GetJumpSpeed() );
	if( detector->Exec( entityPhysicsState.Origin(), spotOrigin ) ) {
		auto *fallback = &self->ai->botRef->jumpToSpotMovementFallback;
		fallback->Activate( entityPhysicsState.Origin(), spotOrigin, 32.0f );
		return fallback;
	}

	return nullptr;
}

void BestConnectedToHubAreasJumpableSpotDetector::GetCandidateSpots( SpotAndScore **begin, SpotAndScore **end ) {
	spotsHeap.clear();

	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const auto *aiManager = AiManager::Instance();

	Vec3 boxMins( -searchRadius, -searchRadius, -32.0f - 0.33f * searchRadius );
	Vec3 boxMaxs( +searchRadius, +searchRadius, +24.0f + 0.15f * searchRadius );
	boxMins += startOrigin;
	boxMaxs += startOrigin;

	int boxAreas[256];
	const int numBoxAreas = AiAasWorld::Instance()->BBoxAreas( boxMins, boxMaxs, boxAreas, 256 );
	for( int i = 0; i < numBoxAreas; ++i ) {
		const int areaNum = boxAreas[i];
		const auto &areaSettings = aasAreaSettings[areaNum];
		if( !( areaSettings.areaflags & AREA_GROUNDED ) ) {
			continue;
		}
		if( areaSettings.areaflags & AREA_DISABLED ) {
			continue;
		}
		if( areaSettings.contents & ( AREACONTENTS_LAVA | AREACONTENTS_SLIME | AREACONTENTS_DONOTENTER ) ) {
			continue;
		}
		if( areaNum == currAreaNums[0] || areaNum == currAreaNums[1] ) {
			continue;
		}

		const auto &area = aasAreas[areaNum];
		Vec3 areaPoint( area.center );
		areaPoint.Z() = area.mins[2] + 4.0f - playerbox_stand_mins[2];
		float squareDistance = areaPoint.SquareDistanceTo( startOrigin );
		if( squareDistance < SQUARE( 48.0f ) ) {
			continue;
		}

		if( !aiManager->IsAreaReachableFromHubAreas( areaNum ) ) {
			continue;
		}

		// Use the distance as a spot score in a max-heap.
		// Farthest spots should get evicted first
		new( spotsHeap.unsafe_grow_back() )SpotAndScore( areaPoint.Data(), squareDistance );
	}

	// FindBestJumpableSpot assumes candidates to be a max-heap
	std::make_heap( spotsHeap.begin(), spotsHeap.end() );
	*begin = spotsHeap.begin();
	*end = spotsHeap.end();
}