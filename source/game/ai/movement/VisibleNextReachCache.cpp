#include "MovementLocal.h"
#include "VisibleNextReachCache.h"

const Ai::ReachChainVector &VisibleNextReachCache::GetVisibleReachVector( MovementPredictionContext *context ) {
#ifndef PUBLIC_BUILD
	if( context->topOfStackIndex ) {
		AI_FailWith( "VisibleNextReachCache::GetVisibleReachVector", "Cached results are valid only for initial frame" );
	}
#endif
	if( computedAt != level.time ) {
		FindVisibleReachChainVector( context );
	}

	return cached;
}

inline void VisibleNextReachCache::PrecacheLeafs( const float *origin ) {
	// We can't/shouldn't reuse self-> leafs/clusters as they are modified during planning
	// and might differ with the actual ones even at 0 prediction frame (if a rollback has occurred)
	Vec3 mins = Vec3( -8, -8, -8 ) + origin;
	Vec3 maxs = Vec3( +8, +8, +8 ) + origin;
	int tmp;
	numOriginLeafs = trap_CM_BoxLeafnums( mins.Data(), maxs.Data(), originLeafNums, (int)sizeof( originLeafNums ), &tmp );
}

inline bool VisibleNextReachCache::IsAreaInPvs( const int *areaLeafsList ) const {
	// Skip the list length
	areaLeafsList++;
	for( int i = 0; i < numOriginLeafs; ++i ) {
		for( int j = 0; j < areaLeafsList[-1]; ++j ) {
			if( trap_CM_LeafsInPVS( originLeafNums[i], areaLeafsList[j] ) ) {
				return true;
			}
		}
	}
	return false;
}

void VisibleNextReachCache::FindVisibleReachChainVector( MovementPredictionContext *context ) {
	cached.clear();

	const float *origin = context->movementState->entityPhysicsState.Origin();
	PrecacheLeafs( origin );

	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasReach = aasWorld->Reachabilities();

	trace_t trace;

	int areaNum = 0;
	for( const auto &reachAndTravelTime: context->NextReachChain() ) {
		const auto &reach = aasReach[reachAndTravelTime.ReachNum()];
		// Check whether the reach. start is in PVS (the area where reach start is is in PVS).
		// We should obviously skip this test for the start (current) area.
		if( areaNum ) {
			if( !IsAreaInPvs( aasWorld->AreaMapLeafsList( areaNum ) ) ) {
				continue;
			}
		}

		SolidWorldTrace( &trace, origin, reach.start );
		if( trace.fraction != 1.0f ) {
			continue;
		}

		cached.push_back( reachAndTravelTime );

		// Stop past the start of an incompatible reach type
		const auto travelType = reach.traveltype & TRAVELTYPE_MASK;
		if( travelType != TRAVEL_WALK ) {
			if( travelType != TRAVEL_WALKOFFLEDGE || DistanceSquared( reach.start, reach.end ) > SQUARE( 40 ) ) {
				break;
			}
		}

		areaNum = reach.areanum;
	}
}
