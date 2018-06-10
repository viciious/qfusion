#ifndef QFUSION_BOT_MOVEMENT_H
#define QFUSION_BOT_MOVEMENT_H

// It would be better if we avoid inclusion of the implementation headers
// but we do not want to lose some performance on indirect access.
// Let offsets of all members be known statically.

#include "MovementPredictionContext.h"

#include "LandOnSavedAreasAction.h"
#include "RidePlatformAction.h"
#include "BunnyInterpolatingReachChainAction.h"
#include "BunnyStraighteningReachChainAction.h"
#include "BunnyToBestShortcutAreaAction.h"
#include "BunnyToBestClusterPointAction.h"
#include "CampASpotAction.h"
#include "CombatDodgeToTargetAction.h"
#include "FallbackMovementAction.h"
#include "WalkOrSlideInterpolatingAction.h"
#include "WeaponJumpActions.h"

#include "FallDownFallback.h"
#include "JumpOverBarrierFallback.h"
#include "JumpToSpotFallback.h"
#include "UseWalkableNodeFallback.h"
#include "UseWalkableTriggerFallback.h"
#include "UseStairsExitFallback.h"
#include "UseRampExitFallback.h"

class Bot;

// Roughly based on token buckets algorithm
class alignas( 4 )RateLimiter {
	Int64Align4 refilledAt;
	float refillRatePerMillis;
	unsigned intervalMillis;
	const int size;
	int value;

	int GetNewValue( int64_t millisNow ) const {
		int64_t diff = level.time - refilledAt;
		auto tokensToAdd = (int)(diff * refillRatePerMillis);
		if( tokensToAdd <= 0 ) {
			return value;
		}

		int newValue = value;
		if( value <= 0 ) {
			newValue = tokensToAdd;
			if( newValue > size ) {
				newValue = size;
			}
		} else {
			newValue += tokensToAdd;
			if( newValue > size ) {
				newValue = 0;
			}
		}
		return newValue;
	}

	void Refill( int64_t millisNow ) {
		int newValue = GetNewValue( millisNow );
		if( value != newValue ) {
			value = newValue;
			refilledAt = millisNow - ( millisNow - refilledAt ) % intervalMillis;
		}
	}
public:
	explicit RateLimiter( int actionsPerSecond )
		: refilledAt( 0 )
		, refillRatePerMillis( actionsPerSecond / 1000.0f )
		, intervalMillis( (unsigned)( 1000.0f / actionsPerSecond ) )
		, size( actionsPerSecond )
		, value( 0 ) {}

	bool TryAcquire() {
		Refill( level.time );
		value -= 1;
		return value >= 0;
	}
};

class BotMovementModule {
	friend class Bot;
	friend struct BotMovementState;
	friend class MovementPredictionContext;
	friend class BaseMovementAction;
	friend class FallbackMovementAction;
	friend class HandleTriggeredJumppadAction;
	friend class LandOnSavedAreasAction;
	friend class RidePlatformAction;
	friend class SwimMovementAction;
	friend class FlyUntilLandingAction;
	friend class CampASpotMovementAction;
	friend class WalkCarefullyAction;
	friend class GenericRunBunnyingAction;
	friend class BunnyStraighteningReachChainAction;
	friend class BunnyToBestShortcutAreaAction;
	friend class BunnyToBestFloorClusterPointAction;
	friend class BunnyInterpolatingChainAtStartAction;
	friend class BunnyInterpolatingReachChainAction;
	friend class WalkOrSlideInterpolatingReachChainAction;
	friend class CombatDodgeSemiRandomlyToTargetAction;
	friend class ScheduleWeaponJumpAction;

	friend class GenericGroundMovementFallback;
	friend class UseWalkableNodeFallback;
	friend class UseRampExitFallback;
	friend class UseStairsExitFallback;
	friend class UseWalkableTriggerFallback;
	friend class FallDownFallback;
	friend class JumpOverBarrierFallback;

	Bot *const bot;

	static constexpr unsigned MAX_SAVED_AREAS = MovementPredictionContext::MAX_SAVED_LANDING_AREAS;
	StaticVector<int, MAX_SAVED_AREAS> savedLandingAreas;
	StaticVector<int, MAX_SAVED_AREAS> savedPlatformAreas;

	// Limits weapon jumps attempts per second
	// (consequential attempts are allowed but no more than several frames,
	// otherwise a bot might loop attempts forever)
	RateLimiter weaponJumpAttemptsRateLimiter;

	// Must be initialized before any of movement actions constructors is called
	StaticVector<BaseMovementAction *, 20> movementActions;

	FallbackMovementAction fallbackMovementAction;
	HandleTriggeredJumppadAction handleTriggeredJumppadAction;
	LandOnSavedAreasAction landOnSavedAreasAction;
	RidePlatformAction ridePlatformAction;
	SwimMovementAction swimMovementAction;
	FlyUntilLandingAction flyUntilLandingAction;
	CampASpotMovementAction campASpotMovementAction;
	WalkCarefullyAction walkCarefullyAction;
	BunnyStraighteningReachChainAction bunnyStraighteningReachChainAction;
	BunnyToBestShortcutAreaAction bunnyToBestShortcutAreaAction;
	BunnyToBestFloorClusterPointAction bunnyToBestFloorClusterPointAction;
	BunnyInterpolatingChainAtStartAction bunnyInterpolatingChainAtStartAction;
	BunnyInterpolatingReachChainAction bunnyInterpolatingReachChainAction;
	WalkOrSlideInterpolatingReachChainAction walkOrSlideInterpolatingReachChainAction;
	CombatDodgeSemiRandomlyToTargetAction combatDodgeSemiRandomlyToTargetAction;
	ScheduleWeaponJumpAction scheduleWeaponJumpAction;
	TryTriggerWeaponJumpAction tryTriggerWeaponJumpAction;
	CorrectWeaponJumpAction correctWeaponJumpAction;

	BotMovementState movementState;

	MovementPredictionContext predictionContext;

	UseWalkableNodeFallback useWalkableNodeFallback;
	UseRampExitFallback useRampExitFallback;
	UseStairsExitFallback useStairsExitFallback;
	UseWalkableTriggerFallback useWalkableTriggerFallback;

	JumpToSpotFallback jumpToSpotFallback;
	FallDownFallback fallDownFallback;
	JumpOverBarrierFallback jumpOverBarrierFallback;

	MovementFallback *activeMovementFallback;

	int64_t nextRotateInputAttemptAt;
	int64_t inputRotationBlockingTimer;
	int64_t lastInputRotationFailureAt;

	void CheckGroundPlatform();

	void ApplyPendingTurnToLookAtPoint( BotInput *input, MovementPredictionContext *context = nullptr );
	inline void InvertInput( BotInput *input, MovementPredictionContext *context = nullptr );
	inline void TurnInputToSide( vec3_t sideDir, int sign, BotInput *input, MovementPredictionContext *context = nullptr );
	inline bool TryRotateInput( BotInput *input, MovementPredictionContext *context = nullptr );
	void CheckBlockingDueToInputRotation();
public:
	BotMovementModule( Bot *bot_ );

	bool TestWhetherCanSafelyKeepHighSpeed( MovementPredictionContext *context );

	inline void OnInterceptedPredictedEvent( int ev, int parm ) {
		predictionContext.OnInterceptedPredictedEvent( ev, parm );
	}

	inline void OnInterceptedPMoveTouchTriggers( pmove_t *pm, const vec3_t previousOrigin ) {
		predictionContext.OnInterceptedPMoveTouchTriggers( pm, previousOrigin );
	}

	inline void SetCampingSpot( const AiCampingSpot &campingSpot ) {
		movementState.campingSpotState.Activate( campingSpot );
	}

	inline void ResetCampingSpot() {
		movementState.campingSpotState.Deactivate();
	}

	inline bool HasActiveCampingSpot() const {
		return movementState.campingSpotState.IsActive();
	}

	inline void SetPendingLookAtPoint( const AiPendingLookAtPoint &lookAtPoint, unsigned timeoutPeriod ) {
		movementState.pendingLookAtPointState.Activate( lookAtPoint, timeoutPeriod );
	}

	inline void ResetPendingLookAtPoint() {
		movementState.pendingLookAtPointState.Deactivate();
	}

	inline bool HasPendingLookAtPoint() const {
		return movementState.pendingLookAtPointState.IsActive();
	}

	inline void ActivateJumppadState( const edict_t *jumppadEnt ) {
		movementState.jumppadMovementState.Activate( jumppadEnt );
	}

	inline bool CanChangeWeapons() const {
		auto &weaponJumpState = movementState.weaponJumpMovementState;
		return !weaponJumpState.IsActive() || weaponJumpState.hasTriggeredWeaponJump;
	}

	void Reset() {
		movementState.Reset();
		activeMovementFallback = nullptr;
	}

	bool CanInterruptMovement() const;

	void Frame( BotInput *input );
	void ApplyInput( BotInput *input, MovementPredictionContext *context = nullptr );
};

#endif
