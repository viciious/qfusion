#ifndef QFUSION_BUNNYTOSTAIRSORRAMPEXITACTION_H
#define QFUSION_BUNNYTOSTAIRSORRAMPEXITACTION_H

#include "GenericBunnyingAction.h"

class BunnyToStairsOrRampExitAction: public GenericRunBunnyingAction {
	float *intendedLookDir { nullptr };
	Vec3 lookDirStorage { vec3_origin };
	int targetFloorCluster { 0 };

	bool TryFindAndSaveLookDir( MovementPredictionContext *context );
	void TrySaveStairsExitFloorCluster( MovementPredictionContext *context, int exitAreaNum );
public:
	DECLARE_BUNNYING_MOVEMENT_ACTION_CONSTRUCTOR( BunnyToStairsOrRampExitAction, COLOR_RGB( 0, 255, 255 ) ) {}

	void BeforePlanning() override {
		GenericRunBunnyingAction::BeforePlanning();
		targetFloorCluster = 0;
		intendedLookDir = nullptr;
	}

	void PlanPredictionStep( MovementPredictionContext *context ) override;
	void CheckPredictionStepResults( MovementPredictionContext *context ) override;
};

#endif
