#ifndef QFUSION_GENERICGROUNDMOVEMENTFALLBACK_H
#define QFUSION_GENERICGROUNDMOVEMENTFALLBACK_H

#include "MovementFallback.h"
#include "../navigation/AasRouteCache.h"

class GenericGroundMovementFallback: public MovementFallback
{
protected:
	float runDistanceToTargetThreshold;
	float runDotProductToTargetThreshold;
	float dashDistanceToTargetThreshold;
	float dashDotProductToTargetThreshold;
	float airAccelDistanceToTargetThreshold;
	float airAccelDotProductToTargetThreshold;
	bool allowRunning;
	bool allowDashing;
	bool allowAirAccel;
	bool allowCrouchSliding;

	virtual void GetSteeringTarget( vec3_t target ) = 0;

	bool ShouldSkipTests( MovementPredictionContext *context = nullptr );

	int GetCurrBotAreas( int *areaNums, MovementPredictionContext *context = nullptr );

	bool TestActualWalkability( int targetAreaNum, const vec3_t targetOrigin,
								MovementPredictionContext *context = nullptr );

	void GetAreaMidGroundPoint( int areaNum, vec3_t target ) {
		const auto &area = AiAasWorld::Instance()->Areas()[areaNum];
		VectorCopy( area.center, target );
		target[2] = area.mins[2] + 1.0f - playerbox_stand_mins[2];
	}
public:
	static constexpr auto TRAVEL_FLAGS = TFL_WALK | TFL_AIR | TFL_WALKOFFLEDGE;

	GenericGroundMovementFallback( const edict_t *self_, int debugColor_ )
		: MovementFallback( self_, debugColor_ )
		, runDistanceToTargetThreshold( 20.0f )
		, runDotProductToTargetThreshold( 0.3f )
		, dashDistanceToTargetThreshold( 72.0f )
		, dashDotProductToTargetThreshold( 0.9f )
		, airAccelDistanceToTargetThreshold( 72.0f )
		, airAccelDotProductToTargetThreshold( 0.9f )
		, allowRunning( true )
		, allowDashing( true )
		, allowAirAccel( true )
		, allowCrouchSliding( true ) {}

	void SetupMovement( MovementPredictionContext *context ) override;

	bool TryDeactivate( MovementPredictionContext *context ) override;
};

#endif
