#include "UseStairsExitFallback.h"
#include "MovementLocal.h"

bool UseStairsExitFallback::TryDeactivate( Context *context ) {
	// Call the superclass method first
	if( GenericGroundMovementFallback::TryDeactivate( context ) ) {
		return true;
	}

	if( GenericGroundMovementFallback::ShouldSkipTests( context ) ) {
		return false;
	}

	const auto *aasAreaStairsClusterNums = AiAasWorld::Instance()->AreaStairsClusterNums();

	int areaNums[2] = { 0, 0 };
	int numBotAreas = GetCurrBotAreas( areaNums );
	for( int i = 0; i < numBotAreas; ++i ) {
		const int areaNum = areaNums[i];
		// The bot has entered the exit area (make sure this condition is first)
		if( areaNum == this->exitAreaNum ) {
			status = COMPLETED;
			return true;
		}
		// The bot is still in the same stairs cluster
		if( aasAreaStairsClusterNums[areaNum] == stairsClusterNum ) {
			assert( status == PENDING );
			return false;
		}
	}

	// The bot is neither in the same stairs cluster nor in the cluster exit area
	status = INVALID;
	return true;
}

const uint16_t *TryFindBestStairsExitArea( Context *context, int stairsClusterNum ) {
	const int toAreaNum = context->NavTargetAasAreaNum();
	if( !toAreaNum ) {
		return nullptr;
	}

	const int currTravelTimeToTarget = context->TravelTimeToNavTarget();
	if( !currTravelTimeToTarget ) {
		return nullptr;
	}

	const auto *aasWorld = AiAasWorld::Instance();
	const auto *routeCache = context->RouteCache();
	const auto &travelFlags = context->TravelFlags();

	const uint16_t *stairsClusterAreaNums = aasWorld->StairsClusterData( stairsClusterNum ) + 1;
	int numAreasInStairsCluster = stairsClusterAreaNums[-1];

	// TODO: Support curved stairs, here and from StairsClusterBuilder side

	// Determine whether highest or lowest area is closer to the nav target
	const uint16_t *stairsBoundaryAreas[2];
	stairsBoundaryAreas[0] = &stairsClusterAreaNums[0];
	stairsBoundaryAreas[1] = &stairsClusterAreaNums[numAreasInStairsCluster - 1];

	int bestStairsAreaIndex = -1;
	int bestTravelTimeOfStairsAreas = std::numeric_limits<int>::max();
	for( int i = 0; i < 2; ++i ) {
		int bestAreaTravelTime = std::numeric_limits<int>::max();
		for( int flags: travelFlags ) {
			int travelTime = routeCache->TravelTimeToGoalArea( *stairsBoundaryAreas[i], toAreaNum, flags );
			if( travelTime && travelTime < bestAreaTravelTime ) {
				bestAreaTravelTime = travelTime;
			}
		}
		// The stairs boundary area is not reachable
		if( bestAreaTravelTime == std::numeric_limits<int>::max() ) {
			return nullptr;
		}
		// Make sure a stairs area is closer to the nav target than the current one
		if( bestAreaTravelTime < currTravelTimeToTarget ) {
			if( bestAreaTravelTime < bestTravelTimeOfStairsAreas ) {
				bestTravelTimeOfStairsAreas = bestAreaTravelTime;
				bestStairsAreaIndex = i;
			}
		}
	}

	if( bestStairsAreaIndex < 0 ) {
		return nullptr;
	}

	// The value points to the cluster data that is persistent in memory
	// during the entire match, so returning this address is legal.
	return stairsBoundaryAreas[bestStairsAreaIndex];
}

MovementFallback *FallbackMovementAction::TryFindStairsFallback( Context *context ) {
	const auto *aasWorld = AiAasWorld::Instance();

	int currAreaNums[2] = { 0, 0 };
	const int numCurrAreas = context->movementState->entityPhysicsState.PrepareRoutingStartAreas( currAreaNums );

	int stairsClusterNum = 0;
	for( int i = 0; i < numCurrAreas; ++i ) {
		if( ( stairsClusterNum = aasWorld->StairsClusterNum( currAreaNums[i] ) ) ) {
			break;
		}
	}

	if( !stairsClusterNum ) {
		return nullptr;
	}

	const auto *bestAreaNum = TryFindBestStairsExitArea( context, stairsClusterNum );
	if( !bestAreaNum ) {
		return nullptr;
	}

	// Note: Don't try to apply jumping shortcut, results are very poor.

	auto *fallback = &module->useStairsExitFallback;
	fallback->Activate( stairsClusterNum, *bestAreaNum );
	return fallback;
}