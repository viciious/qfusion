#include "../bot.h"
#include "MovementModule.h"
#include "MovementLocal.h"
#include "EnvironmentTraceCache.h"
#include "BestJumpableSpotDetector.h"
#include "MovementFallback.h"

BotMovementModule::BotMovementModule( Bot *bot_ )
	: bot( bot_ )
	, weaponJumpAttemptsRateLimiter( 2 )
	, fallbackMovementAction( this )
	, handleTriggeredJumppadAction( this )
	, landOnSavedAreasAction( this )
	, ridePlatformAction( this )
	, swimMovementAction( this )
	, flyUntilLandingAction( this )
	, campASpotMovementAction( this )
	, walkCarefullyAction( this )
	, bunnyStraighteningReachChainAction( this )
	, bunnyToBestShortcutAreaAction( this )
	, bunnyToBestFloorClusterPointAction( this )
	, bunnyInterpolatingChainAtStartAction( this )
	, bunnyInterpolatingReachChainAction( this )
	, walkOrSlideInterpolatingReachChainAction( this )
	, combatDodgeSemiRandomlyToTargetAction( this )
	, scheduleWeaponJumpAction( this )
	, tryTriggerWeaponJumpAction( this )
	, correctWeaponJumpAction( this )
	, predictionContext( this )
	, useWalkableNodeFallback( bot_, this )
	, useRampExitFallback( bot_, this )
	, useStairsExitFallback( bot_, this )
	, useWalkableTriggerFallback( bot_, this )
	, jumpToSpotFallback( bot_, this )
	, fallDownFallback( bot_, this )
	, jumpOverBarrierFallback( bot_, this )
	, activeMovementFallback( nullptr )
	, nextRotateInputAttemptAt( 0 )
	, inputRotationBlockingTimer( 0 )
	, lastInputRotationFailureAt( 0 ) {
	movementState.Reset();
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
	const AiAasRouteCache *routeCache;
	int navTargetAreaNum;
	int startTravelTime;

	using Super::Exec;
	using Super::startVelocity;
	using Super::startOrigin;

	KeepHighSpeedMovingToNavTargetPredictor()
		: routeCache( nullptr ), navTargetAreaNum( 0 ), startTravelTime( 0 ) {}
};

static KeepHighSpeedMovingToNavTargetPredictor keepHighSpeedMovingToNavTargetPredictor;

bool BotMovementModule::TestWhetherCanSafelyKeepHighSpeed( Context *context ) {
	const int navTargetAreaNum = context ? context->NavTargetAasAreaNum() : bot->NavTargetAasAreaNum();
	if( !navTargetAreaNum ) {
		auto *predictor = &::keepHighSpeedWithoutNavTargetPredictor;
		if( context ) {
			predictor->startVelocity = context->movementState->entityPhysicsState.Velocity();
			predictor->startOrigin = context->movementState->entityPhysicsState.Origin();
		} else {
			const edict_t *self = game.edicts + bot->EntNum();
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
		entityPhysicsState = bot->EntityPhysicsState();
		int startAreaNums[2] = { 0, 0 };
		int numStartAreas = entityPhysicsState->PrepareRoutingStartAreas( startAreaNums );
		int goalAreaNum = bot->NavTargetAasAreaNum();
		if( !( startTravelTime = bot->RouteCache()->PreferredRouteToGoalArea( startAreaNums, numStartAreas, goalAreaNum ) ) ) {
			return false;
		}
	}

	auto *predictor = &::keepHighSpeedMovingToNavTargetPredictor;
	predictor->startVelocity = entityPhysicsState->Velocity();
	predictor->startOrigin = entityPhysicsState->Origin();
	predictor->navTargetAreaNum = navTargetAreaNum;
	predictor->startTravelTime = startTravelTime;
	predictor->routeCache = bot->RouteCache();

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

bool BotMovementModule::CanInterruptMovement() const {
	if( movementState.jumppadMovementState.IsActive() ) {
		return false;
	}
	if( movementState.flyUntilLandingMovementState.IsActive() ) {
		return false;
	}
	if( movementState.weaponJumpMovementState.IsActive() ) {
		return false;
	}

	const edict_t *self = game.edicts + bot->EntNum();
	// False if the bot is standing on a platform and it has not switched to the TOP state
	return !( self->groundentity && self->groundentity->use == Use_Plat && self->groundentity->moveinfo.state != STATE_TOP );
}

void BotMovementModule::Frame( BotInput *input ) {
	CheckBlockingDueToInputRotation();

	ApplyPendingTurnToLookAtPoint( input );

	movementState.Frame( game.frametime );

	const edict_t *self = game.edicts + bot->EntNum();
	movementState.TryDeactivateContainedStates( self, nullptr );

	if( activeMovementFallback && activeMovementFallback->TryDeactivate( nullptr ) ) {
		activeMovementFallback = nullptr;
	}

	MovementActionRecord movementActionRecord;
	BaseMovementAction *movementAction = predictionContext.GetActionAndRecordForCurrTime( &movementActionRecord );

	movementAction->ExecActionRecord( &movementActionRecord, input, nullptr );

	CheckGroundPlatform();
}

void BotMovementModule::CheckGroundPlatform() {
	const edict_t *self = game.edicts + bot->EntNum();
	if( !self->groundentity ) {
		return;
	}

	// Reset saved platform areas after touching a solid world ground
	if( self->groundentity == world ) {
		savedPlatformAreas.clear();
		return;
	}

	if( self->groundentity->use != Use_Plat ) {
		return;
	}

	if( self->groundentity->moveinfo.state != STATE_BOTTOM ) {
		return;
	}

	ridePlatformAction.TrySaveExitAreas( nullptr, self->groundentity );
}

void BotMovementModule::CheckBlockingDueToInputRotation() {
	if( movementState.campingSpotState.IsActive() ) {
		return;
	}
	if( movementState.inputRotation == BotInputRotation::NONE ) {
		return;
	}

	const edict_t *self = game.edicts + bot->EntNum();

	if( !self->groundentity ) {
		return;
	}

	float threshold = self->r.client->ps.stats[PM_STAT_MAXSPEED] - 30.0f;
	if( threshold < 0 ) {
		threshold = DEFAULT_PLAYERSPEED - 30.0f;
	}

	if( self->velocity[0] * self->velocity[0] + self->velocity[1] * self->velocity[1] > threshold * threshold ) {
		nextRotateInputAttemptAt = 0;
		inputRotationBlockingTimer = 0;
		lastInputRotationFailureAt = 0;
		return;
	}

	inputRotationBlockingTimer += game.frametime;
	if( inputRotationBlockingTimer < 200 ) {
		return;
	}

	int64_t millisSinceLastFailure = level.time - lastInputRotationFailureAt;
	assert( millisSinceLastFailure >= 0 );
	if( millisSinceLastFailure >= 10000 ) {
		nextRotateInputAttemptAt = level.time + 400;
	} else {
		nextRotateInputAttemptAt = level.time + 2000 - 400 * ( millisSinceLastFailure / 2500 );
		assert( nextRotateInputAttemptAt > level.time + 400 );
	}
	lastInputRotationFailureAt = level.time;
}

void BotMovementModule::ApplyPendingTurnToLookAtPoint( BotInput *botInput, MovementPredictionContext *context ) {
	BotPendingLookAtPointState *pendingLookAtPointState;
	AiEntityPhysicsState *entityPhysicsState_;
	unsigned frameTime;
	if( context ) {
		pendingLookAtPointState = &context->movementState->pendingLookAtPointState;
		entityPhysicsState_ = &context->movementState->entityPhysicsState;
		frameTime = context->predictionStepMillis;
	} else {
		pendingLookAtPointState = &movementState.pendingLookAtPointState;
		entityPhysicsState_ = &movementState.entityPhysicsState;
		frameTime = game.frametime;
	}

	if( !pendingLookAtPointState->IsActive() ) {
		return;
	}

	const AiPendingLookAtPoint &pendingLookAtPoint = pendingLookAtPointState->pendingLookAtPoint;
	Vec3 toPointDir( pendingLookAtPoint.Origin() );
	toPointDir -= entityPhysicsState_->Origin();
	toPointDir.NormalizeFast();

	botInput->SetIntendedLookDir( toPointDir, true );
	botInput->isLookDirSet = true;

	float turnSpeedMultiplier = pendingLookAtPoint.TurnSpeedMultiplier();
	Vec3 newAngles = bot->GetNewViewAngles( entityPhysicsState_->Angles().Data(), toPointDir, frameTime, turnSpeedMultiplier );
	botInput->SetAlreadyComputedAngles( newAngles );

	botInput->canOverrideLookVec = false;
	botInput->canOverridePitch = false;
}

void BotMovementModule::ApplyInput( BotInput *input, MovementPredictionContext *context ) {
	// It is legal (there are no enemies and no nav targets in some moments))
	if( !input->isLookDirSet ) {
		//const float *origin = entityPhysicsState ? entityPhysicsState->Origin() : self->s.origin;
		//AITools_DrawColorLine(origin, (Vec3(-32, +32, -32) + origin).Data(), COLOR_RGB(192, 0, 0), 0);
		return;
	}
	if( !input->isUcmdSet ) {
		//const float *origin = entityPhysicsState ? entityPhysicsState->Origin() : self->s.origin;
		//AITools_DrawColorLine(origin, (Vec3(+32, -32, +32) + origin).Data(), COLOR_RGB(192, 0, 192), 0);
		return;
	}

	if( context ) {
		auto *entityPhysicsState_ = &context->movementState->entityPhysicsState;
		if( !input->hasAlreadyComputedAngles ) {
			TryRotateInput( input, context );
			Vec3 newAngles( bot->GetNewViewAngles( entityPhysicsState_->Angles().Data(), input->IntendedLookDir(),
												   context->predictionStepMillis, input->TurnSpeedMultiplier() ) );
			input->SetAlreadyComputedAngles( newAngles );
		}
		entityPhysicsState_->SetAngles( input->AlreadyComputedAngles() );
	} else {
		edict_t *self = game.edicts + bot->EntNum();
		if( !input->hasAlreadyComputedAngles ) {
			TryRotateInput( input, context );
			Vec3 newAngles( bot->GetNewViewAngles( self->s.angles, input->IntendedLookDir(),
												   game.frametime, input->TurnSpeedMultiplier() ) );
			input->SetAlreadyComputedAngles( newAngles );
		}
		input->AlreadyComputedAngles().CopyTo( self->s.angles );
	}
}

bool BotMovementModule::TryRotateInput( BotInput *input, MovementPredictionContext *context ) {

	const float *botOrigin;
	BotInputRotation *prevRotation;

	if( context ) {
		botOrigin = context->movementState->entityPhysicsState.Origin();
		prevRotation = &context->movementState->inputRotation;
	} else {
		botOrigin = bot->Origin();
		prevRotation = &movementState.inputRotation;
	}

	if( !bot->keptInFovPoint.IsActive() || nextRotateInputAttemptAt > level.time ) {
		*prevRotation = BotInputRotation::NONE;
		return false;
	}

	// Cut off an expensive PVS call early
	if( input->IsRotationAllowed( BotInputRotation::ALL_KINDS_MASK ) ) {
		// We do not utilize PVS cache since it might produce different results for predicted and actual bot origin
		if( !trap_inPVS( bot->keptInFovPoint.Origin().Data(), botOrigin ) ) {
			*prevRotation = BotInputRotation::NONE;
			return false;
		}
	}

	Vec3 selfToPoint( bot->keptInFovPoint.Origin() );
	selfToPoint -= botOrigin;
	selfToPoint.NormalizeFast();

	if( input->IsRotationAllowed( BotInputRotation::BACK ) ) {
		float backDotThreshold = ( *prevRotation == BotInputRotation::BACK ) ? -0.3f : -0.5f;
		if( selfToPoint.Dot( input->IntendedLookDir() ) < backDotThreshold ) {
			*prevRotation = BotInputRotation::BACK;
			InvertInput( input, context );
			return true;
		}
	}

	if( input->IsRotationAllowed( BotInputRotation::SIDE_KINDS_MASK ) ) {
		vec3_t intendedRightDir, intendedUpDir;
		MakeNormalVectors( input->IntendedLookDir().Data(), intendedRightDir, intendedUpDir );
		const float dotRight = selfToPoint.Dot( intendedRightDir );

		if( input->IsRotationAllowed( BotInputRotation::RIGHT ) ) {
			const float rightDotThreshold = ( *prevRotation == BotInputRotation::RIGHT ) ? 0.6f : 0.7f;
			if( dotRight > rightDotThreshold ) {
				*prevRotation = BotInputRotation::RIGHT;
				TurnInputToSide( intendedRightDir, +1, input, context );
				return true;
			}
		}

		if( input->IsRotationAllowed( BotInputRotation::LEFT ) ) {
			const float leftDotThreshold = ( *prevRotation == BotInputRotation::LEFT ) ? -0.6f : -0.7f;
			if( dotRight < leftDotThreshold ) {
				*prevRotation = BotInputRotation::LEFT;
				TurnInputToSide( intendedRightDir, -1, input, context );
				return true;
			}
		}
	}

	*prevRotation = BotInputRotation::NONE;
	return false;
}

static inline void SetupInputForTransition( BotInput *input, const edict_t *groundEntity, const vec3_t intendedForwardDir ) {
	// If actual input is not inverted, release keys/clear special button while starting a transition
	float intendedDotForward = input->IntendedLookDir().Dot( intendedForwardDir );
	if( intendedDotForward < 0 ) {
		if( groundEntity ) {
			input->SetSpecialButton( false );
		}
		input->ClearMovementDirections();
		input->SetTurnSpeedMultiplier( 2.0f - 5.0f * intendedDotForward );
	} else if( intendedDotForward < 0.3f ) {
		if( groundEntity ) {
			input->SetSpecialButton( false );
		}
		input->SetTurnSpeedMultiplier( 2.0f );
	}
}

void BotMovementModule::InvertInput( BotInput *input, MovementPredictionContext *context ) {
	input->SetForwardMovement( -input->ForwardMovement() );
	input->SetRightMovement( -input->RightMovement() );

	input->SetIntendedLookDir( -input->IntendedLookDir(), true );

	const edict_t *groundEntity;
	vec3_t forwardDir;
	if( context ) {
		context->movementState->entityPhysicsState.ForwardDir().CopyTo( forwardDir );
		groundEntity = context->movementState->entityPhysicsState.GroundEntity();
	} else {
		movementState.entityPhysicsState.ForwardDir().CopyTo( forwardDir );
		groundEntity = game.edicts[bot->EntNum()].groundentity;
	}

	SetupInputForTransition( input, groundEntity, forwardDir );

	// Prevent doing a forward dash if all direction keys are clear.

	if( !input->IsSpecialButtonSet() || !groundEntity ) {
		return;
	}

	if( input->ForwardMovement() || input->RightMovement() ) {
		return;
	}

	input->SetForwardMovement( -1 );
}

void BotMovementModule::TurnInputToSide( vec3_t sideDir, int sign, BotInput *input, MovementPredictionContext *context ) {
	VectorScale( sideDir, sign, sideDir );

	const edict_t *groundEntity;
	vec3_t forwardDir;
	if( context ) {
		context->movementState->entityPhysicsState.ForwardDir().CopyTo( forwardDir );
		groundEntity = context->movementState->entityPhysicsState.GroundEntity();
	} else {
		movementState.entityPhysicsState.ForwardDir().CopyTo( forwardDir );
		groundEntity = game.edicts[bot->EntNum()].groundentity;
	}

	// Rotate input
	input->SetIntendedLookDir( sideDir, true );

	// If flying, release side keys to prevent unintended aircontrol usage
	if( !groundEntity ) {
		input->SetForwardMovement( 0 );
		input->SetRightMovement( 0 );
	} else {
		int oldForwardMovement = input->ForwardMovement();
		int oldRightMovement = input->RightMovement();
		input->SetForwardMovement( sign * oldRightMovement );
		input->SetRightMovement( sign * oldForwardMovement );
		input->SetSpecialButton( false );
	}

	SetupInputForTransition( input, groundEntity, sideDir );
}

MovementPredictionContext::MovementPredictionContext( BotMovementModule *module_ )
	: bot( module_->bot )
	, module( module_ )
	, sameFloorClusterAreasCache( module->bot )
	, navMeshQueryCache( module->bot )
	, movementState( nullptr )
	, record( nullptr )
	, oldPlayerState( nullptr )
	, currPlayerState( nullptr )
	, actionSuggestedByAction( nullptr )
	, activeAction( nullptr )
	, totalMillisAhead( 0 )
	, predictionStepMillis( 0 )
	, oldStepMillis( 0 )
	, topOfStackIndex( 0 )
	, savepointTopOfStackIndex( 0 )
	, sequenceStopReason( SequenceStopReason::SUCCEEDED )
	, isCompleted( false )
	, cannotApplyAction( false )
	, shouldRollback( false ) {}

MovementPredictionContext::HitWhileRunningTestResult MovementPredictionContext::MayHitWhileRunning() {
	if( const auto *cachedResult = mayHitWhileRunningCachesStack.GetCached() ) {
		return *cachedResult;
	}

	const auto &selectedEnemies = bot->GetSelectedEnemies();
	if( !selectedEnemies.AreValid() ) {
		mayHitWhileRunningCachesStack.SetCachedValue( HitWhileRunningTestResult::Failure() );
		// TODO: What if we use Success()?
		return HitWhileRunningTestResult::Failure();
	}

	const auto &entityPhysicsState = movementState->entityPhysicsState;
	Vec3 botLookDir( entityPhysicsState.ForwardDir() );

	Vec3 botToEnemyDir( selectedEnemies.LastSeenOrigin() );
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

int TravelTimeWalkingOrFallingShort( const AiAasRouteCache *routeCache, int fromAreaNum, int toAreaNum ) {
	const auto *aasReach = AiAasWorld::Instance()->Reachabilities();
	constexpr int travelFlags = TFL_WALK | TFL_AIR | TFL_WALKOFFLEDGE;
	int numHops = 0;
	int travelTime = 0;
	for(;; ) {
		if( fromAreaNum == toAreaNum ) {
			return std::max( 1, travelTime );
		}
		// Limit to prevent looping
		if ( ( numHops++ ) == 32 ) {
			return 0;
		}
		int reachNum = routeCache->ReachabilityToGoalArea( fromAreaNum, toAreaNum, travelFlags );
		if( !reachNum ) {
			return 0;
		}
		// Save the returned travel time once at start.
		// It is not so inefficient as results of the previous call including travel time are cached and the cache is fast.
		if( !travelTime ) {
			travelTime = routeCache->TravelTimeToGoalArea( fromAreaNum, toAreaNum, travelFlags );
		}
		const auto &reach = aasReach[reachNum];
		// Move to this area for the next iteration
		fromAreaNum = reach.areanum;
		// Check whether the travel type fits this function restrictions
		const int travelType = reach.traveltype & TRAVELTYPE_MASK;
		if( travelType == TRAVEL_WALK ) {
			continue;
		}
		if( travelType == TRAVEL_WALKOFFLEDGE ) {
			if( DistanceSquared( reach.start, reach.end ) < SQUARE( 0.8 * AI_JUMPABLE_HEIGHT ) ) {
				continue;
			}
		}
		return 0;
	}
}
