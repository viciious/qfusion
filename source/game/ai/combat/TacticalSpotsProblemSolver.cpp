#include "TacticalSpotsProblemSolver.h"
#include "SpotsProblemSolversLocal.h"

SpotsAndScoreVector &TacticalSpotsProblemSolver::SelectCandidateSpots( const SpotsQueryVector &spotsFromQuery ) {
	const float minHeightAdvantageOverOrigin = problemParams.minHeightAdvantageOverOrigin;
	const float heightOverOriginInfluence = problemParams.heightOverOriginInfluence;
	const float searchRadius = originParams.searchRadius;
	const float originZ = originParams.origin[2];
	const auto *spots = tacticalSpotsRegistry->spots;
	// Copy to stack for faster access
	Vec3 origin( originParams.origin );

	SpotsAndScoreVector &result = tacticalSpotsRegistry->temporariesAllocator.GetNextCleanSpotsAndScoreVector();

	for( auto spotNum: spotsFromQuery ) {
		const TacticalSpot &spot = spots[spotNum];

		float heightOverOrigin = spot.absMins[2] - originZ;
		if( heightOverOrigin < minHeightAdvantageOverOrigin ) {
			continue;
		}

		float squareDistanceToOrigin = DistanceSquared( origin.Data(), spot.origin );
		if( squareDistanceToOrigin > searchRadius * searchRadius ) {
			continue;
		}

		float score = 1.0f;
		float factor = BoundedFraction( heightOverOrigin - minHeightAdvantageOverOrigin, searchRadius );
		score = ApplyFactor( score, factor, heightOverOriginInfluence );

		result.push_back( SpotAndScore( spotNum, score ) );
	}

	// Sort result so best score areas are first
	std::sort( result.begin(), result.end() );
	return result;
}

SpotsAndScoreVector &TacticalSpotsProblemSolver::CheckSpotsReachFromOrigin( SpotsAndScoreVector &candidateSpots,
																			uint16_t insideSpotNum ) {
	AiAasRouteCache *routeCache = originParams.routeCache;
	const int originAreaNum = originParams.originAreaNum;
	const float *origin = originParams.origin;
	const float searchRadius = originParams.searchRadius;
	// AAS uses travel time in centiseconds
	const int maxFeasibleTravelTimeCentis = problemParams.maxFeasibleTravelTimeMillis / 10;
	const float weightFalloffDistanceRatio = problemParams.originWeightFalloffDistanceRatio;
	const float distanceInfluence = problemParams.originDistanceInfluence;
	const float travelTimeInfluence = problemParams.travelTimeInfluence;
	const auto travelFlags = Bot::ALLOWED_TRAVEL_FLAGS;
	const auto *const spots = tacticalSpotsRegistry->spots;

	SpotsAndScoreVector &result = tacticalSpotsRegistry->temporariesAllocator.GetNextCleanSpotsAndScoreVector();

	// Do not more than result.capacity() iterations.
	// Some feasible areas in candidateAreas tai that pass test may be skipped,
	// but this is intended to reduce performance drops (do not more than result.capacity() pathfinder calls).
	if( insideSpotNum < MAX_SPOTS ) {
		const auto *travelTimeTable = tacticalSpotsRegistry->spotTravelTimeTable;
		const auto tableRowOffset = insideSpotNum * this->tacticalSpotsRegistry->numSpots;
		for( const SpotAndScore &spotAndScore: candidateSpots ) {
			const TacticalSpot &spot = spots[spotAndScore.spotNum];
			// If zero, the spotNum spot is not reachable from insideSpotNum
			int tableTravelTime = travelTimeTable[tableRowOffset + spotAndScore.spotNum];
			if( !tableTravelTime || tableTravelTime > maxFeasibleTravelTimeCentis ) {
				continue;
			}

			// Get an actual travel time (non-zero table value does not guarantee reachability)
			int travelTime = routeCache->TravelTimeToGoalArea( originAreaNum, spot.aasAreaNum, travelFlags );
			if( !travelTime || travelTime > maxFeasibleTravelTimeCentis ) {
				continue;
			}

			float travelTimeFactor = 1.0f - ComputeTravelTimeFactor( travelTime, maxFeasibleTravelTimeCentis );
			float distanceFactor = ComputeDistanceFactor( spot.origin, origin, weightFalloffDistanceRatio, searchRadius );
			float newScore = spotAndScore.score;
			newScore = ApplyFactor( newScore, distanceFactor, distanceInfluence );
			newScore = ApplyFactor( newScore, travelTimeFactor, travelTimeInfluence );
			result.push_back( SpotAndScore( spotAndScore.spotNum, newScore ) );
		}
	} else {
		for( const SpotAndScore &spotAndScore: candidateSpots ) {
			const TacticalSpot &spot = spots[spotAndScore.spotNum];
			int travelTime = routeCache->TravelTimeToGoalArea( originAreaNum, spot.aasAreaNum, travelFlags );
			if( !travelTime || travelTime > maxFeasibleTravelTimeCentis ) {
				continue;
			}

			float travelTimeFactor = 1.0f - ComputeTravelTimeFactor( travelTime, maxFeasibleTravelTimeCentis );
			float distanceFactor = ComputeDistanceFactor( spot.origin, origin, weightFalloffDistanceRatio, searchRadius );
			float newScore = spotAndScore.score;
			newScore = ApplyFactor( newScore, distanceFactor, distanceInfluence );
			newScore = ApplyFactor( newScore, travelTimeFactor, travelTimeInfluence );
			result.push_back( SpotAndScore( spotAndScore.spotNum, newScore ) );
		}
	}

	// Sort result so best score areas are first
	std::sort( result.begin(), result.end() );
	return result;
}

SpotsAndScoreVector &TacticalSpotsProblemSolver::CheckSpotsReachFromOriginAndBack( SpotsAndScoreVector &candidateSpots,
																				   uint16_t insideSpotNum ) {
	AiAasRouteCache *routeCache = originParams.routeCache;
	const int originAreaNum = originParams.originAreaNum;
	const float *origin = originParams.origin;
	const float searchRadius = originParams.searchRadius;
	// AAS uses time in centiseconds
	const int maxFeasibleTravelTimeCentis = problemParams.maxFeasibleTravelTimeMillis / 10;
	const float weightFalloffDistanceRatio = problemParams.originWeightFalloffDistanceRatio;
	const float distanceInfluence = problemParams.originDistanceInfluence;
	const float travelTimeInfluence = problemParams.travelTimeInfluence;
	const auto travelFlags = Bot::ALLOWED_TRAVEL_FLAGS;
	const auto *const spots = tacticalSpotsRegistry->spots;

	SpotsAndScoreVector &result = tacticalSpotsRegistry->temporariesAllocator.GetNextCleanSpotsAndScoreVector();

	// Do not more than result.capacity() iterations.
	// Some feasible areas in candidateAreas tai that pass test may be skipped,
	// but it is intended to reduce performance drops (do not more than 2 * result.capacity() pathfinder calls).
	if( insideSpotNum < MAX_SPOTS ) {
		const auto *travelTimeTable = tacticalSpotsRegistry->spotTravelTimeTable;
		const auto numSpots_ = tacticalSpotsRegistry->numSpots;
		for( const SpotAndScore &spotAndScore : candidateSpots ) {
			const TacticalSpot &spot = spots[spotAndScore.spotNum];

			// If the table element i * numSpots_ + j is zero, j-th spot is not reachable from i-th one.
			int tableToTravelTime = travelTimeTable[insideSpotNum * numSpots_ + spotAndScore.spotNum];
			if( !tableToTravelTime ) {
				continue;
			}
			int tableBackTravelTime = travelTimeTable[spotAndScore.spotNum * numSpots_ + insideSpotNum];
			if( !tableBackTravelTime ) {
				continue;
			}
			if( tableToTravelTime + tableBackTravelTime > maxFeasibleTravelTimeCentis ) {
				continue;
			}

			// Get an actual travel time (non-zero table values do not guarantee reachability)
			int toTravelTime = routeCache->TravelTimeToGoalArea( originAreaNum, spot.aasAreaNum, travelFlags );
			// If `to` travel time is apriori greater than maximum allowed one (and thus the sum would be), reject early.
			if( !toTravelTime || toTravelTime > maxFeasibleTravelTimeCentis ) {
				continue;
			}
			int backTimeTravelTime = routeCache->TravelTimeToGoalArea( spot.aasAreaNum, originAreaNum, travelFlags );
			if( !backTimeTravelTime || toTravelTime + backTimeTravelTime > maxFeasibleTravelTimeCentis ) {
				continue;
			}

			int totalTravelTimeCentis = toTravelTime + backTimeTravelTime;
			float travelTimeFactor = ComputeTravelTimeFactor( totalTravelTimeCentis, maxFeasibleTravelTimeCentis );
			float distanceFactor = ComputeDistanceFactor( spot.origin, origin, weightFalloffDistanceRatio, searchRadius );
			float newScore = spotAndScore.score;
			newScore = ApplyFactor( newScore, distanceFactor, distanceInfluence );
			newScore = ApplyFactor( newScore, travelTimeFactor, travelTimeInfluence );
			result.push_back( SpotAndScore( spotAndScore.spotNum, newScore ) );
		}
	} else {
		for( const SpotAndScore &spotAndScore : candidateSpots ) {
			const TacticalSpot &spot = spots[spotAndScore.spotNum];
			int toSpotTime = routeCache->TravelTimeToGoalArea( originAreaNum, spot.aasAreaNum, Bot::ALLOWED_TRAVEL_FLAGS );
			if( !toSpotTime ) {
				continue;
			}
			int toEntityTime = routeCache->TravelTimeToGoalArea( spot.aasAreaNum, originAreaNum, Bot::ALLOWED_TRAVEL_FLAGS );
			if( !toEntityTime ) {
				continue;
			}

			int totalTravelTimeCentis = 10 * ( toSpotTime + toEntityTime );
			float travelTimeFactor = ComputeTravelTimeFactor( totalTravelTimeCentis, maxFeasibleTravelTimeCentis );
			float distanceFactor = ComputeDistanceFactor( spot.origin, origin, weightFalloffDistanceRatio, searchRadius );
			float newScore = spotAndScore.score;
			newScore = ApplyFactor( newScore, distanceFactor, distanceInfluence );
			newScore = ApplyFactor( newScore, travelTimeFactor, travelTimeInfluence );
			result.push_back( SpotAndScore( spotAndScore.spotNum, newScore ) );
		}
	}

	// Sort result so best score areas are first
	std::sort( result.begin(), result.end() );
	return result;
}

int TacticalSpotsProblemSolver::CleanupAndCopyResults( const ArrayRange<SpotAndScore> &spotsRange,
													   vec3_t *spotOrigins, int maxSpots ) {
	const auto resultsSize = (unsigned)( spotsRange.begin() - spotsRange.end() );
	if( maxSpots == 0 || resultsSize == 0 ) {
		tacticalSpotsRegistry->temporariesAllocator.Release();
		return 0;
	}

	const auto *const spots = tacticalSpotsRegistry->spots;

	// Its a common case so give it an optimized branch
	if( maxSpots == 1 ) {
		VectorCopy( spots[spotsRange.begin()->spotNum].origin, spotOrigins[0] );
		tacticalSpotsRegistry->temporariesAllocator.Release();
		return 1;
	}

	const float spotProximityThreshold = problemParams.spotProximityThreshold;
	bool *const isSpotExcluded = tacticalSpotsRegistry->temporariesAllocator.GetCleanExcludedSpotsMask();

	int numSpots_ = 0;
	unsigned keptSpotIndex = 0;
	for(;; ) {
		if( keptSpotIndex >= resultsSize ) {
			tacticalSpotsRegistry->temporariesAllocator.Release();
			return numSpots_;
		}
		if( numSpots_ >= maxSpots ) {
			tacticalSpotsRegistry->temporariesAllocator.Release();
			return numSpots_;
		}

		// Spots are sorted by score.
		// So first spot not marked as excluded yet has higher priority and should be kept.

		const TacticalSpot &keptSpot = spots[spotsRange.begin()[keptSpotIndex].spotNum];
		VectorCopy( keptSpot.origin, spotOrigins[numSpots_] );
		++numSpots_;

		// Exclude all next (i.e. lower score) spots that are too close to the kept spot.

		unsigned testedSpotIndex = keptSpotIndex + 1;
		keptSpotIndex = 999999;
		for(; testedSpotIndex < resultsSize; testedSpotIndex++ ) {
			// Skip already excluded areas
			if( isSpotExcluded[testedSpotIndex] ) {
				continue;
			}

			const TacticalSpot &testedSpot = spots[spotsRange.begin()[testedSpotIndex].spotNum];
			if( DistanceSquared( keptSpot.origin, testedSpot.origin ) < spotProximityThreshold * spotProximityThreshold ) {
				isSpotExcluded[testedSpotIndex] = true;
			} else if( keptSpotIndex > testedSpotIndex ) {
				keptSpotIndex = testedSpotIndex;
			}
		}
	}
}