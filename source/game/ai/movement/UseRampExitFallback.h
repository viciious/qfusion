#ifndef QFUSION_USERAMPEXITFALLBACK_H
#define QFUSION_USERAMPEXITFALLBACK_H

#include "GenericGroundMovementFallback.h"

class UseRampExitFallback: public GenericGroundMovementFallback
{
	int rampAreaNum;
	int exitAreaNum;

	void GetSteeringTarget( vec3_t target ) override {
		return GetAreaMidGroundPoint( exitAreaNum, target );
	}
public:
	UseRampExitFallback( const Bot *bot_, BotMovementModule *module_ )
		: GenericGroundMovementFallback( bot_, module_, COLOR_RGB( 192, 0, 0 ) ), rampAreaNum( 0 ), exitAreaNum( 0 ) {}

	void Activate( int rampAreaNum_, int exitAreaNum_ ) {
		this->rampAreaNum = rampAreaNum_;
		this->exitAreaNum = exitAreaNum_;
		GenericGroundMovementFallback::Activate();
	}

	bool TryDeactivate( MovementPredictionContext *context = nullptr ) override;
};

const int *TryFindBestInclinedFloorExitArea( MovementPredictionContext *context,
											 int rampAreaNum,
											 int forbiddenAreaNum = 0 );

#endif
