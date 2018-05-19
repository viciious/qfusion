
#ifndef QFUSION_CAMPASPOTACTION_H
#define QFUSION_CAMPASPOTACTION_H

#include "BaseMovementAction.h"

class CampASpotMovementAction : public BaseMovementAction
{
	unsigned disabledForApplicationFrameIndex;

	bool TryUpdateKeyMoveDirs( MovementPredictionContext *context );
	Vec3 GetUpdatedPendingLookDir( MovementPredictionContext *context );
	bool TryApplyLookAtPoint( MovementPredictionContext *context );
public:
	DECLARE_MOVEMENT_ACTION_CONSTRUCTOR( CampASpotMovementAction, COLOR_RGB( 128, 0, 128 ) ) {
		this->disabledForApplicationFrameIndex = std::numeric_limits<unsigned>::max();
	}
	void PlanPredictionStep( MovementPredictionContext *context ) override;
	void CheckPredictionStepResults( MovementPredictionContext *context ) override;
	void OnApplicationSequenceStopped( MovementPredictionContext *context,
									   SequenceStopReason stopReason,
									   unsigned stoppedAtFrameIndex ) override;
	void BeforePlanning() override {
		BaseMovementAction::BeforePlanning();
		disabledForApplicationFrameIndex = std::numeric_limits<unsigned>::max();
	}
};

#endif
