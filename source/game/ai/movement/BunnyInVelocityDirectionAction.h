#ifndef QFUSION_BUNNYINVELOCITYDIRECTIONACTION_H
#define QFUSION_BUNNYINVELOCITYDIRECTIONACTION_H

#include "GenericBunnyingAction.h"

class BunnyInVelocityDirectionAction: public GenericRunBunnyingAction {
public:
	DECLARE_BUNNYING_MOVEMENT_ACTION_CONSTRUCTOR( BunnyInVelocityDirectionAction, COLOR_RGB( 0, 0, 255 ) ) {}

	void PlanPredictionStep( MovementPredictionContext *context ) override;
};

#endif
