#ifndef QFUSION_JUMPOVERBARRIERFALLBACK_H
#define QFUSION_JUMPOVERBARRIERFALLBACK_H

#include "MovementFallback.h"

class JumpOverBarrierFallback: public MovementFallback
{
	vec3_t start;
	vec3_t top;
	bool hasReachedStart;
	bool allowWalljumping;
public:
	explicit JumpOverBarrierFallback( const edict_t *self_ )
		: MovementFallback( self_, COLOR_RGB( 128, 0, 128 ) )
		, hasReachedStart( false )
		, allowWalljumping( false ) {}

	void Activate( const vec3_t start_, const vec3_t top_, bool allowWalljumping_ = true ) {
		VectorCopy( start_, start );
		VectorCopy( top_, top );
		hasReachedStart = false;
		allowWalljumping = allowWalljumping_;
		MovementFallback::Activate();
	}

	bool TryDeactivate( MovementPredictionContext *context = nullptr ) override;

	void SetupMovement( MovementPredictionContext *context ) override;
};

#endif
