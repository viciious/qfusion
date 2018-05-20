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

#include "FallDownFallback.h"
#include "JumpOverBarrierFallback.h"
#include "JumpToSpotFallback.h"
#include "UseWalkableNodeFallback.h"
#include "UseWalkableTriggerFallback.h"
#include "UseStairsExitFallback.h"
#include "UseRampExitFallback.h"

class Bot;

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
	friend class BunnyInterpolatingReachChainAction;
	friend class WalkOrSlideInterpolatingReachChainAction;
	friend class CombatDodgeSemiRandomlyToTargetAction;

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

	// Must be initialized before any of movement actions constructors is called
	StaticVector<BaseMovementAction *, 16> movementActions;

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
	BunnyInterpolatingReachChainAction bunnyInterpolatingReachChainAction;
	WalkOrSlideInterpolatingReachChainAction walkOrSlideInterpolatingReachChainAction;
	CombatDodgeSemiRandomlyToTargetAction combatDodgeSemiRandomlyToTargetAction;

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
		return !weaponJumpState.IsActive() || weaponJumpState.hasTriggeredRocketJump;
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
