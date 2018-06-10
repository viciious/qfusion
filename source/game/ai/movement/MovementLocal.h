#ifndef QFUSION_MOVEMENTLOCAL_H
#define QFUSION_MOVEMENTLOCAL_H

#include "../bot.h"

#include "MovementPredictionContext.h"
#include "EnvironmentTraceCache.h"

#ifndef PUBLIC_BUILD
#define CHECK_ACTION_SUGGESTION_LOOPS
#define ENABLE_MOVEMENT_ASSERTIONS
#define CHECK_INFINITE_NEXT_STEP_LOOPS
extern int nextStepIterationsCounter;
static constexpr int NEXT_STEP_INFINITE_LOOP_THRESHOLD = 10000;
#endif

// Useful for debugging but freezes even Release version
#if 0
#define ENABLE_MOVEMENT_DEBUG_OUTPUT
#endif

// Should be applied to a view vector Z to avoid bending (but does not suit all cases).
constexpr float Z_NO_BEND_SCALE = 0.5f;
// A threshold of dot product of velocity dir and intended view dir
constexpr float STRAIGHT_MOVEMENT_DOT_THRESHOLD = 0.8f;

inline float GetPMoveStatValue( const player_state_t *playerState, int statIndex, float defaultValue ) {
	float value = playerState->pmove.stats[statIndex];
	// Put likely case (the value is not specified) first
	return value < 0 ? defaultValue : value;
}

inline float MovementPredictionContext::GetJumpSpeed() const {
	return GetPMoveStatValue( this->currPlayerState, PM_STAT_JUMPSPEED, DEFAULT_JUMPSPEED * GRAVITY_COMPENSATE );
}

inline float MovementPredictionContext::GetDashSpeed() const {
	return GetPMoveStatValue( this->currPlayerState, PM_STAT_DASHSPEED, DEFAULT_DASHSPEED );
}

inline float MovementPredictionContext::GetRunSpeed() const {
	return GetPMoveStatValue( this->currPlayerState, PM_STAT_MAXSPEED, DEFAULT_PLAYERSPEED );
}

inline Vec3 MovementPredictionContext::NavTargetOrigin() const {
	return bot->NavTargetOrigin();
}

inline float MovementPredictionContext::NavTargetRadius() const {
	return bot->NavTargetRadius();
}

inline bool MovementPredictionContext::IsCloseToNavTarget() const {
	float distance = NavTargetRadius() + 32.0f;
	return NavTargetOrigin().SquareDistanceTo( movementState->entityPhysicsState.Origin() ) < distance * distance;
}

inline int MovementPredictionContext::CurrAasAreaNum() const {
	if( int currAasAreaNum = movementState->entityPhysicsState.CurrAasAreaNum() ) {
		return currAasAreaNum;
	}

	return movementState->entityPhysicsState.DroppedToFloorAasAreaNum();
}

inline int MovementPredictionContext::CurrGroundedAasAreaNum() const {
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

inline int MovementPredictionContext::NavTargetAasAreaNum() const {
	return bot->NavTargetAasAreaNum();
}

inline bool MovementPredictionContext::IsInNavTargetArea() const {
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

inline void MovementPredictionContext::Assert( bool condition, const char *message ) const {
#ifdef ENABLE_MOVEMENT_ASSERTIONS
	if( !condition ) {
		if( message ) {
			AI_FailWith( "MovementPredictionContext::Assert()", "%s\n", message );
		} else {
			AI_FailWith( "MovementPredictionContext::Assert()", "An assertion has failed\n" );
		}
	}
#endif
}

inline void BaseMovementAction::Assert( bool condition, const char *message ) const {
#ifdef ENABLE_MOVEMENT_ASSERTIONS
	if( !condition ) {
		if( message ) {
			AI_FailWith("BaseMovementAction::Assert()", "An assertion has failed: %s\n", message );
		} else {
			AI_FailWith("BaseMovementAction::Assert()", "An assertion has failed\n");
		}
	}
#endif
}

inline const AiAasRouteCache *MovementPredictionContext::RouteCache() const {
	return bot->RouteCache();
}

inline const ArrayRange<int> MovementPredictionContext::TravelFlags() const {
	return bot->TravelFlags();
}

inline EnvironmentTraceCache &MovementPredictionContext::TraceCache() {
	return environmentTestResultsStack.back();
}

inline void MovementPredictionContext::SaveActionOnStack( BaseMovementAction *action ) {
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

inline void MovementPredictionContext::MarkSavepoint( BaseMovementAction *markedBy, unsigned frameIndex ) {
	Assert( !this->cannotApplyAction );
	Assert( !this->shouldRollback );

	Assert( frameIndex == this->topOfStackIndex || frameIndex == this->topOfStackIndex + 1 );
	this->savepointTopOfStackIndex = frameIndex;
	Debug( "%s has marked frame %d as a savepoint\n", markedBy->Name(), frameIndex );
}

inline void MovementPredictionContext::SetPendingRollback() {
	this->cannotApplyAction = true;
	this->shouldRollback = true;
}

inline void MovementPredictionContext::RollbackToSavepoint() {
	Assert( !this->isCompleted );
	Assert( this->shouldRollback );
	Assert( this->cannotApplyAction );

	constexpr const char *format = "Rolling back to savepoint frame %d from ToS frame %d\n";
	Debug( format, this->savepointTopOfStackIndex, this->topOfStackIndex );
	Assert( this->topOfStackIndex >= this->savepointTopOfStackIndex );
	this->topOfStackIndex = this->savepointTopOfStackIndex;
}

inline void MovementPredictionContext::SetPendingWeapon( int weapon ) {
	Assert( weapon >= WEAP_NONE && weapon < WEAP_TOTAL );
	record->pendingWeapon = ( decltype( record->pendingWeapon ) )weapon;
	pendingWeaponsStack.back() = record->pendingWeapon;
}

inline void MovementPredictionContext::SaveSuggestedActionForNextFrame( BaseMovementAction *action ) {
	//Assert(!this->actionSuggestedByAction);
	this->actionSuggestedByAction = action;
}

inline unsigned MovementPredictionContext::MillisAheadForFrameStart( unsigned frameIndex ) const {
	Assert( frameIndex <= topOfStackIndex );
	if( frameIndex < topOfStackIndex ) {
		return (unsigned)( predictedMovementActions[frameIndex].timestamp - level.time );
	}
	return totalMillisAhead;
}

typedef EnvironmentTraceCache::ObstacleAvoidanceResult ObstacleAvoidanceResult;

inline ObstacleAvoidanceResult MovementPredictionContext::TryAvoidFullHeightObstacles( float correctionFraction ) {
	// Make a modifiable copy of the intended look dir
	Vec3 intendedLookVec( this->record->botInput.IntendedLookDir() );
	auto result = EnvironmentTraceCache().TryAvoidFullHeightObstacles( this, &intendedLookVec, correctionFraction );
	if( result == ObstacleAvoidanceResult::CORRECTED ) {
		// Write the modified intended look dir back in this case
		this->record->botInput.SetIntendedLookDir( intendedLookVec );
	}
	return result;
}

inline ObstacleAvoidanceResult MovementPredictionContext::TryAvoidJumpableObstacles( float correctionFraction ) {
	// Make a modifiable copy of the intended look dir
	Vec3 intendedLookVec( this->record->botInput.IntendedLookDir() );
	auto result = EnvironmentTraceCache().TryAvoidJumpableObstacles( this, &intendedLookVec, correctionFraction );
	if( result == ObstacleAvoidanceResult::CORRECTED ) {
		// Write the modified intended look dir back in this case
		this->record->botInput.SetIntendedLookDir( intendedLookVec );
	}
	return result;
}

inline BaseMovementAction &BaseMovementAction::DummyAction() {
	// We have to check the combat action since it might be disabled due to planning stack overflow.
	if( bot->ShouldKeepXhairOnEnemy() && bot->GetSelectedEnemies().AreValid() ) {
		if( !module->combatDodgeSemiRandomlyToTargetAction.IsDisabledForPlanning() ) {
			return module->combatDodgeSemiRandomlyToTargetAction;
		}
	}

	return module->fallbackMovementAction;
}

inline BaseMovementAction &BaseMovementAction::DefaultWalkAction() {
	return module->walkCarefullyAction;
}

inline BaseMovementAction &BaseMovementAction::DefaultBunnyAction() {
	return module->bunnyToBestFloorClusterPointAction;
}

inline BaseMovementAction &BaseMovementAction::FallbackBunnyAction() {
	return module->walkOrSlideInterpolatingReachChainAction;
}

inline FlyUntilLandingAction &BaseMovementAction::FlyUntilLandingAction() {
	return module->flyUntilLandingAction;
}

inline LandOnSavedAreasAction &BaseMovementAction::LandOnSavedAreasAction() {
	return module->landOnSavedAreasAction;
}

inline bool BaseMovementAction::GenericCheckIsActionEnabled( MovementPredictionContext *context,
															 BaseMovementAction *suggestedAction ) const {
	// Put likely case first
	if( !isDisabledForPlanning ) {
		return true;
	}

	context->sequenceStopReason = DISABLED;
	context->cannotApplyAction = true;
	context->actionSuggestedByAction = suggestedAction;
	Debug( "The action has been completely disabled for further planning\n" );
	return false;
}

typedef MovementPredictionContext Context;

inline void BaseMovementAction::DisableWithAlternative( Context *context, BaseMovementAction *suggestedAction ) {
	context->cannotApplyAction = true;
	context->actionSuggestedByAction = suggestedAction;
	this->isDisabledForPlanning = true;
}

inline void BaseMovementAction::SwitchOrStop( Context *context, BaseMovementAction *suggestedAction ) {
	// Few predicted frames are enough if the action cannot be longer applied (but have not caused rollback)
	if( context->topOfStackIndex > 0 ) {
		Debug( "There were enough successfully predicted frames anyway, stopping prediction\n" );
		context->isCompleted = true;
		return;
	}

	DisableWithAlternative( context, suggestedAction );
}

inline void BaseMovementAction::SwitchOrRollback( Context *context, BaseMovementAction *suggestedAction ) {
	if( context->topOfStackIndex > 0 ) {
		Debug( "There were some frames predicted ahead that lead to a failure, should rollback\n" );
		this->isDisabledForPlanning = true;
		context->SetPendingRollback();
		return;
	}

	DisableWithAlternative( context, suggestedAction );
}

inline float Distance2DSquared( const vec3_t a, const vec3_t b ) {
	float dx = a[0] - b[0];
	float dy = a[1] - b[1];
	return dx * dx + dy * dy;
}

#ifndef SQUARE
#define SQUARE( x ) ( ( x ) * ( x ) )
#endif

static inline bool ShouldCrouchSlideNow( MovementPredictionContext *context ) {
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

// Height threshold should be set according to used time step
// (we might miss crouch sliding activation if its low and the time step is large)
inline bool ShouldPrepareForCrouchSliding( MovementPredictionContext *context, float heightThreshold = 12.0f ) {
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

class TriggerAreaNumsCache {
	mutable int areaNums[MAX_EDICTS];
public:
	TriggerAreaNumsCache() {
		memset( areaNums, 0, sizeof( areaNums ) );
	}

	int GetAreaNum( int entNum ) const;
};

extern TriggerAreaNumsCache triggerAreaNumsCache;

int TravelTimeWalkingOrFallingShort( const AiAasRouteCache *routeCache, int fromAreaNum, int toAreaNum );

#endif
