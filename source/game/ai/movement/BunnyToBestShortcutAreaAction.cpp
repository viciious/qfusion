#include "BunnyToBestShortcutAreaAction.h"
#include "MovementLocal.h"

BunnyToBestShortcutAreaAction::BunnyToBestShortcutAreaAction( Bot *bot_ )
	: BotBunnyTestingMultipleLookDirsAction( bot_, NAME, COLOR_RGB( 255, 64, 0 ) ) {
	supportsObstacleAvoidance = false;
	// The constructor cannot be defined in the header due to this bot member access
	suggestedAction = &bot_->walkOrSlideInterpolatingReachChainAction;
}

void BunnyToBestShortcutAreaAction::SaveSuggestedLookDirs( Context *context ) {
	Assert( suggestedLookDirs.empty() );
	Assert( context->NavTargetAasAreaNum() );

	int startTravelTime = FindActualStartTravelTime( context );
	if( !startTravelTime ) {
		return;
	}

	AreaAndScore candidates[MAX_BBOX_AREAS];
	AreaAndScore *candidatesEnd = SelectCandidateAreas( context, candidates, startTravelTime );
	SaveCandidateAreaDirs( context, candidates, candidatesEnd );
}

inline int BunnyToBestShortcutAreaAction::FindActualStartTravelTime( Context *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const auto *aasRouteCache = self->ai->botRef->routeCache;
	const int travelFlags = self->ai->botRef->PreferredTravelFlags();
	const int navTargetAreaNum = context->NavTargetAasAreaNum();

	int startAreaNums[2] = { entityPhysicsState.DroppedToFloorAasAreaNum(), entityPhysicsState.CurrAasAreaNum() };
	int startTravelTimes[2];

	int j = 0;
	for( int i = 0, end = ( startAreaNums[0] != startAreaNums[1] ) ? 2 : 1; i < end; ++i ) {
		if( int travelTime = aasRouteCache->TravelTimeToGoalArea( startAreaNums[i], navTargetAreaNum, travelFlags ) ) {
			startTravelTimes[j++] = travelTime;
		}
	}

	switch( j ) {
		case 2:
			return std::min( startTravelTimes[0], startTravelTimes[1] );
		case 1:
			return startTravelTimes[0];
		default:
			return 0;
	}
}

inline int BunnyToBestShortcutAreaAction::FindBBoxAreas( Context *context, int *areaNums, int maxAreas ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	// Do not make it speed-depended, it leads to looping/jitter!
	const float side = 256.0f + 256.0f * self->ai->botRef->Skill();

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
	const auto *aasRouteCache = self->ai->botRef->routeCache;
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const auto *aasAreaStairsClusterNums = aasWorld->AreaStairsClusterNums();

	const int navTargetAreaNum = context->NavTargetAasAreaNum();
	const int travelFlags = self->ai->botRef->PreferredTravelFlags();
	const int currAreaNum = entityPhysicsState.CurrAasAreaNum();
	const int droppedToFloorAreaNum = entityPhysicsState.DroppedToFloorAasAreaNum();

	const auto &prevTestedAction = self->ai->botRef->bunnyStraighteningReachChainAction;
	Assert( prevTestedAction.suggestedAction == this );
	const auto &prevTestedAreas = prevTestedAction.dirsBaseAreas;

	const float speed = entityPhysicsState.Speed();
	Vec3 velocityDir( entityPhysicsState.Velocity() );
	velocityDir *= 1.0f / speed;

	int minTravelTimeSave = 0;
	AreaAndScore *candidatesPtr = candidatesBegin;

	Vec3 traceStartPoint( entityPhysicsState.Origin() );
	traceStartPoint.Z() += playerbox_stand_viewheight;

	const auto *dangerToEvade = self->ai->botRef->perceptionManager.PrimaryDanger();
	// Reduce branching in the loop below
	if( self->ai->botRef->ShouldRushHeadless() || ( dangerToEvade && !dangerToEvade->SupportsImpactTests() ) ) {
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
		// Skip areas higher than the bot (to allow moving on a stairs chain, we test distance/height ratio)
		if( area.mins[2] > entityPhysicsState.Origin()[2] ) {
			float distance = areaPoint.FastDistance2DTo( entityPhysicsState.Origin() );
			if( area.mins[2] - entityPhysicsState.Origin()[2] > M_SQRT1_2 * distance ) {
				continue;
			}
		}

		Vec3 toAreaDir( areaPoint );
		toAreaDir -= entityPhysicsState.Origin();
		float distanceToArea = toAreaDir.LengthFast();

		// Reject areas that are very close to the bot.
		// This for example helps to skip some junk areas in stair-like environment.
		if( distanceToArea < 96 ) {
			continue;
		}

		toAreaDir *= 1.0f / distanceToArea;
		// Reject areas behind/not in front depending on speed
		float speedDotFactor = -1.0f + 2 * 0.99f * BoundedFraction( speed, 900 );
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

		const int prevMinTravelTimeSave = minTravelTimeSave;
		// Do not test lower score areas if there is already enough tested candidates
		if( travelTimeSave > minTravelTimeSave ) {
			minTravelTimeSave = travelTimeSave;
		} else if( candidatesPtr - candidatesBegin >= (ptrdiff_t)maxSuggestedLookDirs ) {
			continue;
		}

		// Q: Why an optimization that tests walkability in a floor cluster is not applied?
		// A: Gaps are allowed between the current and target areas, but the walkability test rejects these kinds of areas
		if( !TraceArcInSolidWorld( entityPhysicsState, traceStartPoint.Data(), areaPoint.Data() ) ) {
			// Restore minTravelTimeSave (it might has been set to the value of the rejected area on this loop step)
			minTravelTimeSave = prevMinTravelTimeSave;
			continue;
		}

		// We DO not check whether traveling to the best nearby area takes less time
		// than time traveling from best area to nav target saves.
		// Otherwise only areas in the reachability chain conform to the condition if the routing algorithm works properly.
		// We hope for shortcuts the routing algorithm is not aware of.

		new( candidatesPtr++ )AreaAndScore( areaNum, travelTimeSave );
	}

	return candidatesPtr;
}
