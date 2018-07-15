#ifndef QFUSION_VISIBLENEXTREACHCACHE_H
#define QFUSION_VISIBLENEXTREACHCACHE_H

#include "../ai_base_ai.h"

class MovementPredictionContext;

class VisibleNextReachCache {
	Ai::ReachChainVector cached;
	int64_t computedAt { 0 };

	int originLeafNums[4];
	int numOriginLeafs;

	inline bool IsAreaInPvs( const int *areaLeavesList ) const;

	inline void PrecacheLeafs( const float *origin );

	void FindVisibleReachChainVector( MovementPredictionContext *context );
public:
	const Ai::ReachChainVector &GetVisibleReachVector( MovementPredictionContext *context );
};

#endif
