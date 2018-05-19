#ifndef QFUSION_GENERICBUNNYINGACTION_H
#define QFUSION_GENERICBUNNYINGACTION_H

#include "BaseMovementAction.h"

class GenericRunBunnyingAction : public BaseMovementAction
{
protected:
	int minTravelTimeToNavTargetSoFar;
	int minTravelTimeAreaNumSoFar;
	float minTravelTimeAreaGroundZ;

	// A fraction of speed gain per frame time.
	// Might be negative, in this case it limits allowed speed loss
	float minDesiredSpeedGainPerSecond;
	unsigned currentSpeedLossSequentialMillis;
	unsigned tolerableSpeedLossSequentialMillis;

	// When bot bunnies over a gap, its target either becomes unreachable
	// or travel time is calculated from the bottom of the pit.
	// These timers allow to temporarily skip targer reachability/travel time tests.
	unsigned currentUnreachableTargetSequentialMillis;
	unsigned tolerableUnreachableTargetSequentialMillis;

	// Allow increased final travel time if the min travel time area is reachable by walking
	// from the final area and walking travel time is lower than this limit.
	// It allows to follow the reachability chain less strictly while still being close to it.
	unsigned tolerableWalkableIncreasedTravelTimeMillis;

	// There is a mechanism for completely disabling an action for further planning by setting isDisabledForPlanning flag.
	// However we need a more flexible way of disabling an action after an failed application sequence.
	// A sequence started from different frame that the failed one might succeed.
	// An application sequence will not start at the frame indexed by this value.
	unsigned disabledForApplicationFrameIndex;

	bool supportsObstacleAvoidance;
	bool shouldTryObstacleAvoidance;
	bool isTryingObstacleAvoidance;

	inline void ResetObstacleAvoidanceState() {
		shouldTryObstacleAvoidance = false;
		isTryingObstacleAvoidance = false;
	}

	void SetupCommonBunnyingInput( MovementPredictionContext *context );
	// TODO: Mark as virtual in base class and mark as final here to avoid a warning about hiding parent member?
	bool GenericCheckIsActionEnabled( MovementPredictionContext *context, BaseMovementAction *suggestedAction );
	bool CheckCommonBunnyingPreconditions( MovementPredictionContext *context );
	bool SetupBunnying( const Vec3 &intendedLookVec,
						MovementPredictionContext *context,
						float maxAccelDotThreshold = 1.0f );
	bool CanFlyAboveGroundRelaxed( const MovementPredictionContext *context ) const;
	bool CanSetWalljump( MovementPredictionContext *context ) const;
	void TrySetWalljump( MovementPredictionContext *context );

	// Can be overridden for finer control over tests
	virtual bool CheckStepSpeedGainOrLoss( MovementPredictionContext *context );
	bool IsMovingIntoNavEntity( MovementPredictionContext *context ) const;

public:
	GenericRunBunnyingAction( class Bot *bot_, const char *name_, int debugColor_ = 0 )
		: BaseMovementAction( bot_, name_, debugColor_ )
		, minTravelTimeToNavTargetSoFar( 0 )
		, minTravelTimeAreaNumSoFar( 0 )
		, minTravelTimeAreaGroundZ( 0 )
		, minDesiredSpeedGainPerSecond( 0.0f )
		, currentSpeedLossSequentialMillis( 0 )
		, tolerableSpeedLossSequentialMillis( 300 )
		, currentUnreachableTargetSequentialMillis( 0 )
		, tolerableUnreachableTargetSequentialMillis( 700 )
		, tolerableWalkableIncreasedTravelTimeMillis( 2000 )
		, disabledForApplicationFrameIndex( std::numeric_limits<unsigned>::max() )
		, supportsObstacleAvoidance( false ) {
		ResetObstacleAvoidanceState();
	}

	void CheckPredictionStepResults( MovementPredictionContext *context ) override;
	void OnApplicationSequenceStarted( MovementPredictionContext *context ) override;
	void OnApplicationSequenceStopped( MovementPredictionContext *context,
									   SequenceStopReason reason,
									   unsigned stoppedAtFrameIndex ) override;
	void BeforePlanning() override;
};

#define DECLARE_BUNNYING_MOVEMENT_ACTION_CONSTRUCTOR( name, debugColor_ ) \
	name( class Bot *bot_ ) : GenericRunBunnyingAction( bot_, #name, debugColor_ )

#endif
