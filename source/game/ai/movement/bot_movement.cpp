#include "../bot.h"
#include "bot_movement.h"
#include "MovementLocal.h"
#include "EnvironmentTraceCache.h"
#include "BestJumpableSpotDetector.h"
#include "MovementFallback.h"

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
		: routeCache( nullptr ), navTargetAreaNum( 0 ), startTravelTime( 0 ) {}
};

static KeepHighSpeedMovingToNavTargetPredictor keepHighSpeedMovingToNavTargetPredictor;

bool Bot::TestWhetherCanSafelyKeepHighSpeed( Context *context ) {
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

void Bot::MovementFrame( BotInput *input ) {
	this->movementState.Frame( game.frametime );
	this->movementState.TryDeactivateContainedStates( self, nullptr );

	if( auto *fallback = self->ai->botRef->activeMovementFallback ) {
		if( fallback->TryDeactivate( nullptr ) ) {
			self->ai->botRef->activeMovementFallback = nullptr;
		}
	}

	MovementActionRecord movementActionRecord;
	BaseMovementAction *movementAction = movementPredictionContext.GetActionAndRecordForCurrTime( &movementActionRecord );

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

	self->ai->botRef->ridePlatformAction.TrySaveExitAreas( nullptr, self->groundentity );
}

MovementPredictionContext::HitWhileRunningTestResult MovementPredictionContext::MayHitWhileRunning() {
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