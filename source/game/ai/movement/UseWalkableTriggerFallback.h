#ifndef QFUSION_USEWALKABLETRIGGERFALLBACK_H
#define QFUSION_USEWALKABLETRIGGERFALLBACK_H

#include "GenericGroundMovementFallback.h"

class UseWalkableTriggerFallback: public GenericGroundMovementFallback
{
	const edict_t *trigger;

	void GetSteeringTarget( vec3_t target ) override;
public:
	explicit UseWalkableTriggerFallback( const Bot *bot_, BotMovementModule *module_ )
		: GenericGroundMovementFallback( bot_, module_, COLOR_RGB( 192, 0, 192 ) ), trigger( nullptr ) {}

	void Activate( const edict_t *trigger_ ) {
		this->trigger = trigger_;
		GenericGroundMovementFallback::Activate();
	}

	bool TryDeactivate( MovementPredictionContext *context = nullptr ) override;
};

#endif
