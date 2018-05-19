#ifndef QFUSION_USESTAIRSEXITFALLBACK_H
#define QFUSION_USESTAIRSEXITFALLBACK_H

#include "GenericGroundMovementFallback.h"

class UseStairsExitFallback: public GenericGroundMovementFallback
{
	int stairsClusterNum;
	int exitAreaNum;

	void GetSteeringTarget( vec3_t target ) override {
		GetAreaMidGroundPoint( exitAreaNum, target );
	}
public:
	UseStairsExitFallback( const edict_t *self_ )
		: GenericGroundMovementFallback( self_, COLOR_RGB( 0, 0, 192 ) ), stairsClusterNum( 0 ), exitAreaNum( 0 ) {}

	void Activate( int stairsClusterNum_, int stairsExitAreaNum_ ) {
		this->stairsClusterNum = stairsClusterNum_;
		this->exitAreaNum = stairsExitAreaNum_;
		GenericGroundMovementFallback::Activate();
	}

	bool TryDeactivate( MovementPredictionContext *context = nullptr ) override;
};

const uint16_t *TryFindBestStairsExitArea( MovementPredictionContext *context, int stairsClusterNum );

#endif
