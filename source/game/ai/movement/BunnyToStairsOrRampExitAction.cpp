#include "MovementLocal.h"
#include "BunnyToStairsOrRampExitAction.h"

void BunnyToStairsOrRampExitAction::PlanPredictionStep( MovementPredictionContext *context ) {
	// This action is the first applied action as it is specialized
	// and falls back to other bunnying actions if it cannot be applied.
	if( !GenericCheckIsActionEnabled( context, &module->bunnyToBestFloorClusterPointAction ) ) {
		return;
	}

	if( !CheckCommonBunnyingPreconditions( context ) ) {
		return;
	}

	if( !context->topOfStackIndex ) {
		if( !TryFindAndSaveLookDir( context ) ) {
			this->isDisabledForPlanning = true;
			context->SetPendingRollback();
			return;
		}
	}

	Assert( intendedLookDir );
	if( !SetupBunnying( Vec3( intendedLookDir ), context ) ) {
		return;
	}
}

bool BunnyToStairsOrRampExitAction::TryFindAndSaveLookDir( MovementPredictionContext *context ) {
	int groundedAreaNum = context->CurrGroundedAasAreaNum();
	if( !groundedAreaNum ) {
		Debug( "A current grounded area num is not defined\n" );
		return false;
	}

	const auto *aasWorld = AiAasWorld::Instance();
	if( aasWorld->AreaSettings()[ groundedAreaNum ].areaflags & AREA_INCLINED_FLOOR ) {
		const int *exitAreaNum = TryFindBestInclinedFloorExitArea( context, groundedAreaNum, groundedAreaNum );
		if( !exitAreaNum ) {
			Debug( "Can't find an exit area of the current grouned inclined floor area\n" );
			return false;
		}

		Debug( "Found a best exit area of an inclined floor area\n" );
		lookDirStorage.Set( aasWorld->Areas()[*exitAreaNum].center );
		lookDirStorage -= context->movementState->entityPhysicsState.Origin();
		lookDirStorage.Normalize();
		intendedLookDir = lookDirStorage.Data();
		if( int clusterNum = aasWorld->FloorClusterNum( *exitAreaNum ) ) {
			targetFloorCluster = clusterNum;
		}

		return true;
	}

	const int stairsClusterNum = aasWorld->StairsClusterNum( groundedAreaNum );
	if( !stairsClusterNum ) {
		Debug( "The current grounded area is neither an inclined floor area, nor a stairs cluster area\n" );
		return false;
	}

	const auto *exitAreaNum = TryFindBestStairsExitArea( context, stairsClusterNum );
	if( !exitAreaNum ) {
		Debug( "Can't find an exit area of the current stairs cluster\n" );
		return false;
	}

	Debug( "Found a best exit area of an stairs cluster\n" );
	lookDirStorage.Set( aasWorld->Areas()[*exitAreaNum].center );
	lookDirStorage -= context->movementState->entityPhysicsState.Origin();
	lookDirStorage.Normalize();
	intendedLookDir = lookDirStorage.Data();

	// Try find an area that is a boundary area of the exit area and is in a floor cluster
	TrySaveStairsExitFloorCluster( context, *exitAreaNum );

	return true;
}

void BunnyToStairsOrRampExitAction::TrySaveStairsExitFloorCluster( MovementPredictionContext *context, int exitAreaNum ) {
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *routeCache = context->RouteCache();

	const int targetAreaNum = context->NavTargetAasAreaNum();
	const int exitAreaTravelTime = routeCache->PreferredRouteToGoalArea( exitAreaNum, targetAreaNum );

	const auto &area = aasWorld->AreaSettings()[exitAreaNum];
	const int endReachNum = area.firstreachablearea + area.numreachableareas;
	for( int reachNum = area.firstreachablearea; reachNum < endReachNum; ++reachNum ) {
		const auto &reach = aasWorld->Reachabilities()[reachNum];
		if( ( reach.traveltype & TRAVELTYPE_MASK ) != TRAVEL_WALK ) {
			continue;
		}
		int clusterNum = aasWorld->FloorClusterNum( reach.areanum );
		if( !clusterNum ) {
			continue;
		}
		int travelTime = routeCache->PreferredRouteToGoalArea( reach.areanum, targetAreaNum );
		if( !travelTime || travelTime > exitAreaTravelTime ) {
			continue;
		}
		this->targetFloorCluster = clusterNum;
		return;
	}
}

void BunnyToStairsOrRampExitAction::CheckPredictionStepResults( MovementPredictionContext *context ) {
	GenericRunBunnyingAction::CheckPredictionStepResults( context );
	if( context->cannotApplyAction || context->isCompleted ) {
		return;
	}

	// There is no target floor cluster saved
	if( !targetFloorCluster ) {
		return;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	// Make sure we don't stop prediction at start.
	// The distance threshold is low due to troublesome movement in these kinds of areas.
	if( originAtSequenceStart.SquareDistance2DTo( entityPhysicsState.Origin() ) < SQUARE( 20 ) ) {
		return;
	}

	// If the bot has not touched a ground this frame
	if( !entityPhysicsState.GroundEntity() && !context->frameEvents.hasJumped ) {
		return;
	}

	const auto *aasWorld = AiAasWorld::Instance();
	if( int currFloorCluster = aasWorld->FloorClusterNum( context->CurrGroundedAasAreaNum() ) ) {
		if( currFloorCluster == targetFloorCluster ) {
			Debug( "The prediction step has lead to touching a ground in the target floor cluster" );
			context->isCompleted = true;
			return;
		}
	}
}
