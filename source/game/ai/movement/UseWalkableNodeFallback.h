#ifndef QFUSION_USEWALKABLENODEFALLBACK_H
#define QFUSION_USEWALKABLENODEFALLBACK_H

#include "GenericGroundMovementFallback.h"

class UseWalkableNodeFallback: public GenericGroundMovementFallback
{
protected:
	vec3_t nodeOrigin;
	int nodeAasAreaNum;
	float reachRadius;
	unsigned timeout;

	void GetSteeringTarget( vec3_t target ) override {
		VectorCopy( nodeOrigin, target );
	}
public:
	explicit UseWalkableNodeFallback( const Bot *bot_, BotMovementModule *module_ )
		: GenericGroundMovementFallback( bot_, module_, COLOR_RGB( 0, 192, 0 ) ) {}

	const vec3_t &NodeOrigin() const { return nodeOrigin; }
	int NodeAreaNum() const { return nodeAasAreaNum; }

	void Activate( const vec3_t nodeOrigin_, float reachRadius_, int nodeAasAreaNum_ = 0, unsigned timeout_ = 750 );


	bool TryDeactivate( MovementPredictionContext *context = nullptr ) override;
};

#endif
