#ifndef QFUSION_BUNNYINTERPOLATINGREACHCHAINACTION_H
#define QFUSION_BUNNYINTERPOLATINGREACHCHAINACTION_H

#include "GenericBunnyingAction.h"

class BunnyInterpolatingReachChainAction : public GenericRunBunnyingAction
{
public:
	DECLARE_BUNNYING_MOVEMENT_ACTION_CONSTRUCTOR( BunnyInterpolatingReachChainAction, COLOR_RGB( 32, 0, 255 ) )
	{
		supportsObstacleAvoidance = false;
	}
	void PlanPredictionStep( MovementPredictionContext *context ) override;
};

#endif
