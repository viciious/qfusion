#include "AdvantageProblemSolver.h"
#include "SpotsProblemSolversLocal.h"

int AdvantageProblemSolver::FindMany( vec3_t *spots, int maxSpots ) {
	uint16_t insideSpotNum;
	const SpotsQueryVector &spotsFromQuery = tacticalSpotsRegistry->FindSpotsInRadius( originParams, &insideSpotNum );
	SpotsAndScoreVector &candidateSpots = SelectCandidateSpots( spotsFromQuery );
	SpotsAndScoreVector &reachCheckedSpots = CheckSpotsReach( candidateSpots, insideSpotNum );
	SpotsAndScoreVector &visCheckedSpots = CheckOriginVisibility( reachCheckedSpots, maxSpots );
	SortByVisAndOtherFactors( visCheckedSpots );
	return CleanupAndCopyResults( visCheckedSpots, spots, maxSpots );
}

SpotsAndScoreVector &AdvantageProblemSolver::SelectCandidateSpots( const SpotsQueryVector &spotsFromQuery ) {
	const float minHeightAdvantageOverOrigin = problemParams.minHeightAdvantageOverOrigin;
	const float minHeightAdvantageOverEntity = problemParams.minHeightAdvantageOverEntity;
	const float heightOverOriginInfluence = problemParams.heightOverOriginInfluence;
	const float heightOverEntityInfluence = problemParams.heightOverEntityInfluence;
	const float minSquareDistanceToEntity = problemParams.minSpotDistanceToEntity * problemParams.minSpotDistanceToEntity;
	const float maxSquareDistanceToEntity = problemParams.maxSpotDistanceToEntity * problemParams.maxSpotDistanceToEntity;
	const float searchRadius = originParams.searchRadius;
	const float originZ = originParams.origin[2];
	const float entityZ = problemParams.keepVisibleOrigin[2];
	// Copy to stack for faster access
	Vec3 origin( originParams.origin );
	Vec3 entityOrigin( problemParams.keepVisibleOrigin );

	const auto *const spots = tacticalSpotsRegistry->spots;
	SpotsAndScoreVector &result = tacticalSpotsRegistry->temporariesAllocator.GetNextCleanSpotsAndScoreVector();
	for( auto spotNum: spotsFromQuery ) {
		const TacticalSpot &spot = spots[spotNum];

		float heightOverOrigin = spot.absMins[2] - originZ;
		if( heightOverOrigin < minHeightAdvantageOverOrigin ) {
			continue;
		}

		float heightOverEntity = spot.absMins[2] - entityZ;
		if( heightOverEntity < minHeightAdvantageOverEntity ) {
			continue;
		}

		float squareDistanceToOrigin = DistanceSquared( origin.Data(), spot.origin );
		if( squareDistanceToOrigin > searchRadius * searchRadius ) {
			continue;
		}

		float squareDistanceToEntity = DistanceSquared( entityOrigin.Data(), spot.origin );
		if( squareDistanceToEntity < minSquareDistanceToEntity ) {
			continue;
		}
		if( squareDistanceToEntity > maxSquareDistanceToEntity ) {
			continue;
		}

		float score = 1.0f;
		float factor;
		factor = BoundedFraction( heightOverOrigin - minHeightAdvantageOverOrigin, searchRadius );
		score = ApplyFactor( score, factor, heightOverOriginInfluence );
		factor = BoundedFraction( heightOverEntity - minHeightAdvantageOverEntity, searchRadius );
		score = ApplyFactor( score, factor, heightOverEntityInfluence );

		result.push_back( SpotAndScore( spotNum, score ) );
	}

	// Sort result so best score areas are first
	std::sort( result.begin(), result.end() );
	return result;
}

SpotsAndScoreVector &AdvantageProblemSolver::CheckOriginVisibility( SpotsAndScoreVector &reachCheckedSpots,
																	int maxSpots ) {
	edict_t *passent = const_cast<edict_t*>( originParams.originEntity );
	edict_t *keepVisibleEntity = const_cast<edict_t *>( problemParams.keepVisibleEntity );
	Vec3 entityOrigin( problemParams.keepVisibleOrigin );
	// If not only origin but an entity too is supplied
	if( keepVisibleEntity ) {
		// Its a good idea to add some offset from the ground
		entityOrigin.Z() += 0.66f * keepVisibleEntity->r.maxs[2];
	}

	// Copy to locals for faster access
	const edict_t *gameEdicts = game.edicts;
	const auto *const spots = tacticalSpotsRegistry->spots;
	const auto *aasWorld = AiAasWorld::Instance();

	SpotsAndScoreVector &result = tacticalSpotsRegistry->temporariesAllocator.GetNextCleanSpotsAndScoreVector();

	int originLeafNums[4], topNode;
	Vec3 mins( originParams.origin );
	Vec3 maxs( originParams.origin );
	// We need some small and feasible box for BoxLeafNums() call,
	// player bounds are not obligatory but suit this purpose well
	mins += playerbox_stand_mins;
	maxs += playerbox_stand_maxs;
	const int numOriginLeafs = trap_CM_BoxLeafnums( mins.Data(), maxs.Data(), originLeafNums, 4, &topNode );

	trace_t trace;
	for( const SpotAndScore &spotAndScore : reachCheckedSpots ) {
		const auto &spot = spots[spotAndScore.spotNum];
		// Use area leaf nums for PVS tests
		for( int i = 0; i < numOriginLeafs; ++i ) {
			auto *areaLeafNums = aasWorld->AreaMapLeafsList( spot.aasAreaNum ) + 1;
			for( int j = 0; j < areaLeafNums[-1]; ++j ) {
				if( trap_CM_LeafsInPVS( originLeafNums[i], areaLeafNums[j] ) ) {
					goto pvsTestPassed;
				}
			}
		}

		continue;
pvsTestPassed:

		//.Spot origins are dropped to floor (only few units above)
		// Check whether we can hit standing on this spot (having the gun at viewheight)
		Vec3 from( spots[spotAndScore.spotNum].origin );
		from.Z() += -playerbox_stand_mins[2] + playerbox_stand_viewheight;
		G_Trace( &trace, from.Data(), nullptr, nullptr, entityOrigin.Data(), passent, MASK_AISOLID );
		if( trace.fraction != 1.0f && gameEdicts + trace.ent != keepVisibleEntity ) {
			continue;
		}

		result.push_back( spotAndScore );
		// Stop immediately after we have reached the needed spots count
		if( result.size() == maxSpots ) {
			break;
		}
	}

	return result;
}

void AdvantageProblemSolver::SortByVisAndOtherFactors( SpotsAndScoreVector &result ) {
	const unsigned resultSpotsSize = result.size();
	if( resultSpotsSize <= 1 ) {
		return;
	}

	const Vec3 origin( originParams.origin );
	const Vec3 entityOrigin( problemParams.keepVisibleOrigin );
	const float originZ = originParams.origin[2];
	const float entityZ = problemParams.keepVisibleOrigin[2];
	const float searchRadius = originParams.searchRadius;
	const float minHeightAdvantageOverOrigin = problemParams.minHeightAdvantageOverOrigin;
	const float heightOverOriginInfluence = problemParams.heightOverOriginInfluence;
	const float minHeightAdvantageOverEntity = problemParams.minHeightAdvantageOverEntity;
	const float heightOverEntityInfluence = problemParams.heightOverEntityInfluence;
	const float originWeightFalloffDistanceRatio = problemParams.originWeightFalloffDistanceRatio;
	const float originDistanceInfluence = problemParams.originDistanceInfluence;
	const float entityWeightFalloffDistanceRatio = problemParams.entityWeightFalloffDistanceRatio;
	const float entityDistanceInfluence = problemParams.entityDistanceInfluence;
	const float minSpotDistanceToEntity = problemParams.minSpotDistanceToEntity;
	const float entityDistanceRange = problemParams.maxSpotDistanceToEntity - problemParams.minSpotDistanceToEntity;

	const auto *const spotVisibilityTable = tacticalSpotsRegistry->spotVisibilityTable;
	const auto *const spots = tacticalSpotsRegistry->spots;
	const auto numSpots = tacticalSpotsRegistry->numSpots;

	for( unsigned i = 0; i < resultSpotsSize; ++i ) {
		unsigned visibilitySum = 0;
		unsigned testedSpotNum = result[i].spotNum;
		// Get address of the visibility table row
		const uint8_t *spotVisibilityForSpotNum = spotVisibilityTable + testedSpotNum * numSpots;

		for( unsigned j = 0; j < i; ++j )
			visibilitySum += spotVisibilityForSpotNum[j];

		// Skip i-th index

		for( unsigned j = i + 1; j < resultSpotsSize; ++j )
			visibilitySum += spotVisibilityForSpotNum[j];

		const TacticalSpot &testedSpot = spots[testedSpotNum];
		float score = result[i].score;

		// The maximum possible visibility score for a pair of spots is 255
		float visFactor = visibilitySum / ( ( result.size() - 1 ) * 255.0f );
		visFactor = 1.0f / Q_RSqrt( visFactor );
		score *= visFactor;

		float heightOverOrigin = testedSpot.absMins[2] - originZ - minHeightAdvantageOverOrigin;
		float heightOverOriginFactor = BoundedFraction( heightOverOrigin, searchRadius - minHeightAdvantageOverOrigin );
		score = ApplyFactor( score, heightOverOriginFactor, heightOverOriginInfluence );

		float heightOverEntity = testedSpot.absMins[2] - entityZ - minHeightAdvantageOverEntity;
		float heightOverEntityFactor = BoundedFraction( heightOverEntity, searchRadius - minHeightAdvantageOverEntity );
		score = ApplyFactor( score, heightOverEntityFactor, heightOverEntityInfluence );

		float originDistance = 1.0f / Q_RSqrt( 0.001f + DistanceSquared( testedSpot.origin, origin.Data() ) );
		float originDistanceFactor = ComputeDistanceFactor( originDistance, originWeightFalloffDistanceRatio, searchRadius );
		score = ApplyFactor( score, originDistanceFactor, originDistanceInfluence );

		float entityDistance = 1.0f / Q_RSqrt( 0.001f + DistanceSquared( testedSpot.origin, entityOrigin.Data() ) );
		entityDistance -= minSpotDistanceToEntity;
		float entityDistanceFactor = ComputeDistanceFactor( entityDistance,
															entityWeightFalloffDistanceRatio,
															entityDistanceRange );
		score = ApplyFactor( score, entityDistanceFactor, entityDistanceInfluence );

		result[i].score = score;
	}

	// Sort results so best score spots are first
	std::stable_sort( result.begin(), result.end() );
}