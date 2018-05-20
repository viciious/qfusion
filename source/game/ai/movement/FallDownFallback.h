#ifndef QFUSION_FALLDOWNFALLBACK_H
#define QFUSION_FALLDOWNFALLBACK_H

#include "MovementFallback.h"

class FallDownFallback: public MovementFallback
{
	vec3_t targetOrigin;
	unsigned timeout;
	float reachRadius;
public:
	explicit FallDownFallback( const Bot *bot_, BotMovementModule *module_ )
		: MovementFallback( bot_, module_, COLOR_RGB( 128, 0, 0 ) ) {}

	// Note: It is expected that bot origin Z should be <= target origin Z
	// after completion of the fallback, so target Z matters a lot!
	// Timeout is variable and should be set according to estimated sum of traveling to the ledge and falling
	void Activate( const vec3_t origin, unsigned timeout_, float reachRadius_ = 32.0f ) {
		VectorCopy( origin, this->targetOrigin );
		this->timeout = timeout_;
		this->reachRadius = reachRadius_;
		MovementFallback::Activate();
	}

	bool TryDeactivate( MovementPredictionContext *context = nullptr ) override;

	void SetupMovement( MovementPredictionContext *context ) override;
};

#endif
