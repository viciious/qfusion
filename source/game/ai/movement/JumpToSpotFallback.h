#ifndef QFUSION_JUMPTOSPOTFALLBACK_H
#define QFUSION_JUMPTOSPOTFALLBACK_H

#include "MovementFallback.h"

class JumpToSpotFallback: public MovementFallback {
protected:
	vec3_t targetOrigin;
	vec3_t startOrigin;
	unsigned timeout;
	float reachRadius;
	float startAirAccelFrac;
	float endAirAccelFrac;
	float jumpBoostSpeed;
	bool hasAppliedJumpBoost;
	bool allowCorrection;
public:
	int undesiredAasContents;
	int undesiredAasFlags;
	int desiredAasContents;
	int desiredAasFlags;

	JumpToSpotFallback( const Bot *bot_, BotMovementModule *module_ )
		: MovementFallback( bot_, module_, COLOR_RGB( 255, 0, 128 ) )
		, timeout( 0 )
		, startAirAccelFrac( 0 )
		, endAirAccelFrac( 0 )
		, jumpBoostSpeed( 0 )
		, hasAppliedJumpBoost( false )
		, allowCorrection( false )
		, undesiredAasContents( AREACONTENTS_LAVA | AREACONTENTS_SLIME | AREACONTENTS_DONOTENTER )
		, undesiredAasFlags( AREA_DISABLED )
		, desiredAasContents( 0 )
		, desiredAasFlags( AREA_GROUNDED ) {}

	void Activate( const vec3_t startOrigin_,
				   const vec3_t targetOrigin_,
				   unsigned timeout,
				   float reachRadius_ = 32.0f,
				   float startAirAccelFrac_ = 0.0f,
				   float endAirAccelFrac_ = 0.0f,
				   float jumpBoostSpeed_ = 0.0f );

	bool TryDeactivate( MovementPredictionContext *context = nullptr ) override;

	void SetupMovement( MovementPredictionContext *context ) override;
};

#endif
