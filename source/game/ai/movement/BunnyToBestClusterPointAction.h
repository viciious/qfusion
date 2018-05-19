#ifndef QFUSION_BUNNYTOBESTCLUSTERPOINTACTION_H
#define QFUSION_BUNNYTOBESTCLUSTERPOINTACTION_H

#include "GenericBunnyingAction.h"

class BunnyToBestFloorClusterPointAction: public GenericRunBunnyingAction
{
	vec3_t spotOrigin;
	bool hasSpotOrigin;
public:
	DECLARE_BUNNYING_MOVEMENT_ACTION_CONSTRUCTOR( BunnyToBestFloorClusterPointAction, COLOR_RGB( 255, 0, 255 ) ) {
		supportsObstacleAvoidance = false;
		hasSpotOrigin = false;
	}

	void PlanPredictionStep( MovementPredictionContext *context ) override;

	void BeforePlanning() override {
		GenericRunBunnyingAction::BeforePlanning();
		hasSpotOrigin = false;
	}
};

#endif
