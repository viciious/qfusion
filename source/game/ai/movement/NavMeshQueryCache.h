#ifndef QFUSION_NAVMESHQUERYCACHE_H
#define QFUSION_NAVMESHQUERYCACHE_H

#include "../ai_local.h"

class MovementPredictionContext;

class BotNavMeshQueryCache {
	edict_t *const self;
	const class AiAasWorld *const aasWorld;
	mutable int64_t computedAt;
	mutable vec3_t computedForOrigin;
	mutable vec3_t computedResultPoint;
	mutable Vec3 startOrigin;

	vec3_t walkabilityTraceMins;
	vec3_t walkabilityTraceMaxs;

	static constexpr auto MAX_TESTED_REACH = 16;
	static constexpr auto MAX_PATH_POLYS = 32;
	uint32_t paths[MAX_TESTED_REACH][MAX_PATH_POLYS];
	int pathLengths[MAX_TESTED_REACH];

	bool FindClosestToTargetPoint( MovementPredictionContext *context, float *resultPoint ) const;

	bool TryNavMeshWalkabilityTests( MovementPredictionContext *context, int lastReachIndex, float *resultPoint );
	bool TryTraceAndAasWalkabilityTests( MovementPredictionContext *context, int lastReachIndex, float *resultPoint );
	bool InspectAasWorldTraceToPoly( const vec3_t polyOrigin );
public:
	BotNavMeshQueryCache( edict_t *self_ );

	bool GetClosestToTargetPoint( MovementPredictionContext *context, float *resultPoint ) const;
};

#endif
