#include "BunnyToBestShortcutAreaAction.h"
#include "MovementLocal.h"

BunnyToBestShortcutAreaAction::BunnyToBestShortcutAreaAction( BotMovementModule *module_ )
	: BunnyTestingMultipleLookDirsAction( module_, NAME, COLOR_RGB( 255, 64, 0 ) ) {
	supportsObstacleAvoidance = false;
	// The constructor cannot be defined in the header due to this bot member access
	suggestedAction = &module->walkOrSlideInterpolatingReachChainAction;
}

void BunnyToBestShortcutAreaAction::SaveSuggestedLookDirs( Context *context ) {
	Assert( suggestedLookDirs.empty() );
	Assert( context->NavTargetAasAreaNum() );

	const int startTravelTime = context->TravelTimeToNavTarget();
	if( !startTravelTime ) {
		return;
	}

	AreaAndScore candidates[MAX_BBOX_AREAS];
	AreaAndScore *candidatesEnd = SelectCandidateAreas( context, candidates, startTravelTime );
	SaveCandidateAreaDirs( context, candidates, candidatesEnd );
}

inline int BunnyToBestShortcutAreaAction::FindBBoxAreas( Context *context, int *areaNums, int maxAreas ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	// Do not make it speed-depended, it leads to looping/jitter!
	const float side = 256.0f + 256.0f * bot->Skill();

	Vec3 boxMins( -side, -side, -0.33f * side );
	Vec3 boxMaxs( +side, +side, 0 );
	boxMins += entityPhysicsState.Origin();
	boxMaxs += entityPhysicsState.Origin();

	return AiAasWorld::Instance()->BBoxAreas( boxMins, boxMaxs, areaNums, maxAreas );
}

AreaAndScore *BunnyToBestShortcutAreaAction::SelectCandidateAreas( Context *context,
																   AreaAndScore *candidatesBegin,
																   int startTravelTime ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasRouteCache = bot->RouteCache();
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const auto *aasAreaStairsClusterNums = aasWorld->AreaStairsClusterNums();

	const int navTargetAreaNum = context->NavTargetAasAreaNum();
	const int travelFlags = bot->PreferredTravelFlags();
	const int currAreaNum = entityPhysicsState.CurrAasAreaNum();
	const int droppedToFloorAreaNum = entityPhysicsState.DroppedToFloorAasAreaNum();

	const auto &prevTestedAction = module->bunnyStraighteningReachChainAction;
	Assert( prevTestedAction.suggestedAction == this );
	const auto &prevTestedAreas = prevTestedAction.dirsBaseAreas;

	const float speed = entityPhysicsState.Speed();
	Vec3 velocityDir( entityPhysicsState.Velocity() );
	velocityDir *= 1.0f / speed;

	AreaAndScore *candidatesPtr = candidatesBegin;

	Vec3 traceStartPoint( entityPhysicsState.Origin() );
	traceStartPoint.Z() += playerbox_stand_viewheight;

	const auto *dangerToEvade = bot->PrimaryHazard();
	// Reduce branching in the loop below
	if( bot->ShouldRushHeadless() || ( dangerToEvade && !dangerToEvade->SupportsImpactTests() ) ) {
		dangerToEvade = nullptr;
	}

	int metStairsClusterNum = 0;

	int bboxAreaNums[MAX_BBOX_AREAS];
	int numBBoxAreas = FindBBoxAreas( context, bboxAreaNums, MAX_BBOX_AREAS );
	for( int i = 0; i < numBBoxAreas; ++i ) {
		int areaNum = bboxAreaNums[i];
		if( areaNum == droppedToFloorAreaNum || areaNum == currAreaNum ) {
			continue;
		}

		if( const int stairsClusterNum = aasAreaStairsClusterNums[areaNum] ) {
			// If a stairs cluster has not been met yet
			// (its currently limited to a single cluster but that's satisfactory)
			if( !metStairsClusterNum ) {
				if( const auto *exitAreaNum = TryFindBestStairsExitArea( context, stairsClusterNum ) ) {
					// Do further tests for exit area num instead of the stairs cluster area
					areaNum = *exitAreaNum;
				}
				metStairsClusterNum = stairsClusterNum;
			} else {
				// Skip the stairs area. A test for the exit area of the cluster has been already done.
				continue;
			}
		}

		// Skip areas that have lead to the previous action failure
		// This condition has been lifted to the beginning of the loop
		// to avoid computing twice cluster exit area if a cluster has been met.
		if( prevTestedAction.disabledForApplicationFrameIndex == context->topOfStackIndex ) {
			if( std::find( prevTestedAreas.begin(), prevTestedAreas.end(), areaNum ) != prevTestedAreas.end() ) {
				continue;
			}
		}

		const auto &areaSettings = aasAreaSettings[areaNum];
		int areaFlags = areaSettings.areaflags;
		if( !( areaFlags & AREA_GROUNDED ) ) {
			continue;
		}
		if( areaFlags & ( AREA_JUNK | AREA_DISABLED ) ) {
			continue;
		}
		if( areaSettings.contents & ( AREACONTENTS_WATER | AREACONTENTS_LAVA | AREACONTENTS_SLIME | AREACONTENTS_DONOTENTER ) ) {
			continue;
		}

		const auto &area = aasAreas[areaNum];
		Vec3 areaPoint( area.center[0], area.center[1], area.mins[2] - playerbox_stand_mins[2] );

		Vec3 toAreaDir( areaPoint );
		toAreaDir -= entityPhysicsState.Origin();
		const float squareDistanceToArea = toAreaDir.SquaredLength();

		// Reject areas that are very close to the bot.
		// This for example helps to skip some junk areas in stair-like environment.
		if( squareDistanceToArea < SQUARE( 72 ) ) {
			continue;
		}

		toAreaDir *= 1.0f / sqrtf( squareDistanceToArea );
		// Reject areas behind/not in front depending on speed
		float speedDotFactor = -1.0f + 1.0f * BoundedFraction( speed, 700 );
		if( velocityDir.Dot( toAreaDir ) < speedDotFactor ) {
			continue;
		}

		if( dangerToEvade && dangerToEvade->HasImpactOnPoint( areaPoint ) ) {
			continue;
		}

		int areaToTargetAreaTravelTime = aasRouteCache->TravelTimeToGoalArea( areaNum, navTargetAreaNum, travelFlags );
		if( !areaToTargetAreaTravelTime ) {
			continue;
		}

		// Time saved on traveling to goal
		const int travelTimeSave = startTravelTime - areaToTargetAreaTravelTime;
		// Try to reject non-feasible areas to cut off expensive trace computation
		if( travelTimeSave <= 0 ) {
			continue;
		}

		// Q: Why an optimization that tests walkability in a floor cluster is not applied?
		// A: Gaps are allowed between the current and target areas, but the walkability test rejects these kinds of areas
		if( !TraceArcInSolidWorld( entityPhysicsState, traceStartPoint.Data(), areaPoint.Data() ) ) {
			continue;
		}

		// We DO not check whether traveling to the best nearby area takes less time
		// than time traveling from best area to nav target saves.
		// Otherwise only areas in the reachability chain conform to the condition if the routing algorithm works properly.
		// We hope for shortcuts the routing algorithm is not aware of.
		if( candidatesPtr - candidatesBegin == maxSuggestedLookDirs ) {
			// Evict the worst element (with the lowest score and with the last order by the operator < in the max-heap)
			std::pop_heap( candidatesBegin, candidatesPtr );
			candidatesPtr--;
		}

		new ( candidatesPtr++ )AreaAndScore( areaNum, travelTimeSave );
		std::push_heap( candidatesBegin, candidatesPtr );
	}

	return candidatesPtr;
}
