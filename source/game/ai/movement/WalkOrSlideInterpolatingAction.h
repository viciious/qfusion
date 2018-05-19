#ifndef QFUSION_WALKORSLIDEINTERPOLATINGACTION_H
#define QFUSION_WALKORSLIDEINTERPOLATINGACTION_H

#include "BaseMovementAction.h"

class WalkOrSlideInterpolatingReachChainAction : public BaseMovementAction
{
	int minTravelTimeToTarget;
	int totalNumFrames;
	int numSlideFrames;

	inline bool SetupMovementInTargetArea( MovementPredictionContext *context );
	inline bool TrySetupCrouchSliding( MovementPredictionContext *context, const Vec3 &intendedLookDir );

public:
	DECLARE_MOVEMENT_ACTION_CONSTRUCTOR( WalkOrSlideInterpolatingReachChainAction, COLOR_RGB( 16, 72, 128 ) ) {
		// Shut an analyzer up
		this->minTravelTimeToTarget = std::numeric_limits<int>::max();
		this->totalNumFrames = 0;
		this->numSlideFrames = 0;
	}

	void PlanPredictionStep( MovementPredictionContext *context ) override;
	void CheckPredictionStepResults( MovementPredictionContext *context ) override;
	void OnApplicationSequenceStarted( MovementPredictionContext *context ) override;
	void OnApplicationSequenceStopped( MovementPredictionContext *context,
									   SequenceStopReason stopReason,
									   unsigned stoppedAtFrameIndex ) override;
};

#endif
