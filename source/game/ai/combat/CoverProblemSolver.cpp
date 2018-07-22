#include "CoverProblemSolver.h"
#include "SpotsProblemSolversLocal.h"

int CoverProblemSolver::FindMany( vec3_t *spots, int maxSpots ) {
	uint16_t insideSpotNum;
	const SpotsQueryVector &spotsFromQuery = tacticalSpotsRegistry->FindSpotsInRadius( originParams, &insideSpotNum );
	SpotsAndScoreVector &candidateSpots = SelectCandidateSpots( spotsFromQuery );
	SpotsAndScoreVector &reachCheckedSpots = CheckSpotsReach( candidateSpots, insideSpotNum );
	SpotsAndScoreVector &coverSpots = SelectCoverSpots( reachCheckedSpots, maxSpots );
	return CleanupAndCopyResults( coverSpots, spots, maxSpots );
}

SpotsAndScoreVector &CoverProblemSolver::SelectCoverSpots( const SpotsAndScoreVector &reachCheckedSpots, int maxSpots ) {
	auto &result = tacticalSpotsRegistry->temporariesAllocator.GetNextCleanSpotsAndScoreVector();
	for( const SpotAndScore &spotAndScore: reachCheckedSpots ) {
		if( !LooksLikeACoverSpot( spotAndScore.spotNum ) ) {
			continue;
		}
		result.push_back( spotAndScore );
		if( result.size() == maxSpots ) {
			break;
		}
	}
	return result;
}

bool CoverProblemSolver::LooksLikeACoverSpot( uint16_t spotNum ) const {
	const TacticalSpot &spot = tacticalSpotsRegistry->spots[spotNum];

	edict_t *ignore = const_cast<edict_t *>( problemParams.attackerEntity );
	float *attackerOrigin = const_cast<float *>( problemParams.attackerOrigin );
	float *spotOrigin = const_cast<float *>( spot.origin );
	const edict_t *doNotHitEntity = originParams.originEntity;

	trace_t trace;
	G_Trace( &trace, attackerOrigin, nullptr, nullptr, spotOrigin, ignore, MASK_AISOLID );
	if( trace.fraction == 1.0f ) {
		return false;
	}

	float harmfulRayThickness = problemParams.harmfulRayThickness;

	vec3_t bounds[2] = {
		{ -harmfulRayThickness, -harmfulRayThickness, -harmfulRayThickness },
		{ +harmfulRayThickness, +harmfulRayThickness, +harmfulRayThickness }
	};

	// Convert bounds from relative to absolute
	VectorAdd( bounds[0], spot.origin, bounds[0] );
	VectorAdd( bounds[1], spot.origin, bounds[1] );

	for( int i = 0; i < 8; ++i ) {
		vec3_t traceEnd = {
			bounds[( i >> 2 ) & 1][0],
			bounds[( i >> 1 ) & 1][1],
			bounds[( i >> 0 ) & 1][2]
		};
		G_Trace( &trace, attackerOrigin, nullptr, nullptr, traceEnd, ignore, MASK_AISOLID );
		if( trace.fraction == 1.0f || game.edicts + trace.ent == doNotHitEntity ) {
			return false;
		}
	}

	return true;
}
