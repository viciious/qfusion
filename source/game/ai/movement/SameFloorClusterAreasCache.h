#ifndef QFUSION_SAMEFLOORCLUSTERAREASCACHE_H
#define QFUSION_SAMEFLOORCLUSTERAREASCACHE_H

#include "../ai_local.h"
#include "../static_vector.h"

class MovementPredictionContext;

class BotSameFloorClusterAreasCache
{
	typedef StaticVector<AreaAndScore, 48> CandidateAreasHeap;

	// If an area is closer to a bot, it should never be selected
	static constexpr float SELECTION_THRESHOLD = 192.0f;
	// If an area is closer to a bot, it should be considered reached
	static constexpr float REACHABILITY_RADIUS = 128.0f;

	// If a bot remains in the same area, candidates computation might be skipped,
	// and only straight-line walkability tests are to be performed.
	mutable CandidateAreasHeap oldCandidatesHeap;

	edict_t *const self;
	const AiAasWorld *aasWorld;
	mutable int64_t computedAt;
	mutable Vec3 computedTargetAreaPoint;
	mutable int computedForAreaNum;
	mutable int computedTargetAreaNum;
	mutable int computedTravelTime;

	void BuildCandidateAreasHeap( MovementPredictionContext *context,
								  const uint16_t *clusterAreaNums,
								  int numClusterAreas,
								  CandidateAreasHeap &result ) const;

	int FindClosestToTargetPoint( MovementPredictionContext *context, int *areaNum ) const;

	bool NeedsToComputed( MovementPredictionContext *context ) const;
	bool AreaPassesCollisionTest( MovementPredictionContext *context, int areaNum ) const;
	bool AreaPassesCollisionTest( const Vec3 &start, int areaNum, const vec3_t mins, const vec3_t maxs ) const;
public:
	explicit BotSameFloorClusterAreasCache( edict_t *self_ )
		: self( self_ )
		, aasWorld( AiAasWorld::Instance() )
		, computedAt( 0 )
		, computedTargetAreaPoint( 0, 0, 0 )
		, computedForAreaNum( 0 )
		, computedTargetAreaNum( 0 )
		, computedTravelTime( 0 ) {}

	// Returns travel time from the target point
	int GetClosestToTargetPoint( MovementPredictionContext *context,
								 float *resultPoint, int *areaNum = nullptr ) const;
};

bool IsAreaWalkableInFloorCluster( int startAreaNum, int targetAreaNum );

#endif
