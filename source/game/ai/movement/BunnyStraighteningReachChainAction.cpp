#include "BunnyStraighteningReachChainAction.h"
#include "MovementLocal.h"
#include "SameFloorClusterAreasCache.h"
#include "../ai_manager.h"

BunnyStraighteningReachChainAction::BunnyStraighteningReachChainAction( BotMovementModule *module_ )
	: BunnyTestingMultipleLookDirsAction( module_, NAME, COLOR_RGB( 0, 192, 0 ) ) {
	supportsObstacleAvoidance = false;
	// The constructor cannot be defined in the header due to this bot member access
	suggestedAction = &module->bunnyToBestShortcutAreaAction;
}

void BunnyStraighteningReachChainAction::SaveSuggestedLookDirs( Context *context ) {
	Assert( suggestedLookDirs.empty() );
	Assert( dirsBaseAreas.empty() );
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const int navTargetAasAreaNum = context->NavTargetAasAreaNum();
	Assert( navTargetAasAreaNum );

	// Do not modify look vec in this case (we assume its set to nav target)
	if( context->IsInNavTargetArea() ) {
		void *mem = suggestedLookDirs.unsafe_grow_back();
		dirsBaseAreas.push_back( navTargetAasAreaNum );
		Vec3 *toTargetDir = new(mem)Vec3( context->NavTargetOrigin() );
		*toTargetDir -= entityPhysicsState.Origin();
		toTargetDir->NormalizeFast();
		return;
	}

	const auto &nextReachChain = context->NextReachChain();
	if( nextReachChain.empty() ) {
		Debug( "Cannot straighten look vec: next reach. chain is empty\n" );
		return;
	}

	// Make sure the action is dependent of this action
	Assert( suggestedAction == &module->bunnyToBestShortcutAreaAction );
	// * Quotas are allowed to request only once per frame,
	// and subsequent calls for the same client fail (return with false).
	// Set suggested look dirs for both actions, otherwise the second one (almost) never gets a quota.
	// * Never try to acquire a quota here if a bot is really blocked.
	// These predicted actions are very likely to fail again,
	// and fallbacks do not get their quotas leading to blocking to bot suicide.
	if( bot->MillisInBlockedState() < 100 && AiManager::Instance()->TryGetExpensiveComputationQuota( bot ) ) {
		this->maxSuggestedLookDirs = 11;
		module->bunnyToBestShortcutAreaAction.maxSuggestedLookDirs = 5;
	} else {
		this->maxSuggestedLookDirs = 2;
		module->bunnyToBestShortcutAreaAction.maxSuggestedLookDirs = 2;
	}

	const AiAasWorld *aasWorld = AiAasWorld::Instance();
	const aas_reachability_t *aasReach = aasWorld->Reachabilities();

	int lastValidReachIndex = -1;
	constexpr unsigned MAX_TESTED_REACH = Ai::ReachChainVector::capacity();
	const unsigned maxTestedReach = std::min( MAX_TESTED_REACH, nextReachChain.size() );
	const aas_reachability_t *reachStoppedAt = nullptr;
	for( unsigned i = 0; i < maxTestedReach; ++i ) {
		const auto &reach = aasReach[nextReachChain[i].ReachNum()];
		// Avoid inclusion of TRAVEL_JUMP and TRAVEL_STRAFEJUMP reach.
		// as they are prone to falling down in this case
		// (Jumping over gaps should be timed precisely)
		if( reach.traveltype != TRAVEL_WALK && reach.traveltype != TRAVEL_WALKOFFLEDGE ) {
			reachStoppedAt = &reach;
			break;
		}

		lastValidReachIndex++;
	}

	if( lastValidReachIndex < 0 || lastValidReachIndex >= (int)maxTestedReach ) {
		Debug( "There were no supported for bunnying reachabilities\n" );
		return;
	}
	Assert( lastValidReachIndex < (int)maxTestedReach );

	AreaAndScore candidates[MAX_TESTED_REACH];
	AreaAndScore *candidatesEnd = SelectCandidateAreas( context, candidates, (unsigned)lastValidReachIndex );

	SaveCandidateAreaDirs( context, candidates, candidatesEnd );
	Assert( suggestedLookDirs.size() <= maxSuggestedLookDirs );

	// If there is a trigger entity in the reach chain, try keep looking at it
	if( reachStoppedAt ) {
		int travelType = reachStoppedAt->traveltype;
		if( travelType == TRAVEL_TELEPORT || travelType == TRAVEL_JUMPPAD || travelType == TRAVEL_ELEVATOR ) {
			Assert( maxSuggestedLookDirs > 0 );
			// Evict the last dir, the trigger should have a priority over it
			if( suggestedLookDirs.size() == maxSuggestedLookDirs ) {
				suggestedLookDirs.pop_back();
			}
			dirsBaseAreas.push_back( 0 );
			void *mem = suggestedLookDirs.unsafe_grow_back();
			// reachStoppedAt->areanum is an area num of reach destination, not the trigger itself.
			// Saving or restoring the trigger area num does not seem worth this minor case.
			Vec3 *toTriggerDir = new(mem)Vec3( reachStoppedAt->start );
			*toTriggerDir -= entityPhysicsState.Origin();
			toTriggerDir->NormalizeFast();
			return;
		}
	}

	if( suggestedLookDirs.size() == 0 ) {
		Debug( "Cannot straighten look vec: cannot find a suitable area in reach. chain to aim for\n" );
	}
}

AreaAndScore *BunnyStraighteningReachChainAction::SelectCandidateAreas( Context *context,
 																		AreaAndScore *candidatesBegin,
																		unsigned lastValidReachIndex ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const auto &nextReachChain = context->NextReachChain();
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *routeCache = context->RouteCache();
	const auto *aasReach = aasWorld->Reachabilities();
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const auto *aasAreaFloorClusterNums = aasWorld->AreaFloorClusterNums();
	const auto *aasAreaStairsClusterNums = aasWorld->AreaStairsClusterNums();
	const int navTargetAasAreaNum = context->NavTargetAasAreaNum();
	const float pointZOffset = -playerbox_stand_mins[2];

	const auto *dangerToEvade = bot->PrimaryHazard();
	// Reduce branching in the loop below
	if( bot->ShouldRushHeadless() || ( dangerToEvade && !dangerToEvade->SupportsImpactTests() ) ) {
		dangerToEvade = nullptr;
	}

	int metStairsClusterNum = 0;

	int currAreaNum = context->CurrAasAreaNum();
	int floorClusterNum = 0;
	const int groundedAreaNum = context->CurrGroundedAasAreaNum();
	if( groundedAreaNum ) {
		floorClusterNum = aasAreaFloorClusterNums[groundedAreaNum];
	}

	AreaAndScore *candidatesPtr = candidatesBegin;
	Vec3 traceStartPoint( entityPhysicsState.Origin() );
	traceStartPoint.Z() += playerbox_stand_viewheight;

	for( int i = lastValidReachIndex; i >= 0; --i ) {
		const int reachNum = nextReachChain[i].ReachNum();
		const auto &reach = aasReach[reachNum];
		int areaNum = reach.areanum;
		const auto &area = aasAreas[areaNum];
		const auto &areaSettings = aasAreaSettings[areaNum];

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

		if( areaSettings.contents & AREACONTENTS_DONOTENTER ) {
			continue;
		}

		int areaFlags = areaSettings.areaflags;
		if( !( areaFlags & AREA_GROUNDED ) ) {
			continue;
		}
		if( areaFlags & AREA_DISABLED ) {
			continue;
		}

		Vec3 areaPoint( area.center[0], area.center[1], area.mins[2] + pointZOffset );

		const float squareDistanceToArea = areaPoint.SquareDistanceTo( entityPhysicsState.Origin() );
		// Skip way too close areas (otherwise the bot might fall into endless looping)
		if( squareDistanceToArea < SQUARE( 96 ) ) {
			continue;
		}

		// Skip way too far areas (this is mainly an optimization for the following SolidWorldTrace() call)
		if( squareDistanceToArea > SQUARE( 1024 + 512 ) ) {
			continue;
		}

		if( dangerToEvade && dangerToEvade->HasImpactOnPoint( areaPoint ) ) {
			continue;
		}

		// Give far areas greater initial score
		float score = 999999.0f;
		if( areaNum != navTargetAasAreaNum ) {
			score = 0.1f + 0.9f * ( (float)( i + 1 ) / (float)( lastValidReachIndex + 1 ) );
		}

		// Make sure the bot can see the ground
		// On failure, restore minScore (it might have been set to the value of the rejected area score on this loop step)
		if( floorClusterNum && floorClusterNum == aasAreaFloorClusterNums[areaNum] ) {
			if( !IsAreaWalkableInFloorCluster( currAreaNum, areaNum ) ) {
				continue;
			}
		} else {
			if( !TraceArcInSolidWorld( entityPhysicsState, traceStartPoint.Data(), areaPoint.Data() ) ) {
				continue;
			}

			// This is very likely to indicate a significant elevation of the area over the bot area.
			// TODO: This test leads to a failure if the target area is direct-reachable via falling
			if( !TravelTimeWalkingOrFallingShort( routeCache, areaNum, groundedAreaNum ) ) {
				continue;
			}
		}

		if( candidatesPtr - candidatesBegin == maxSuggestedLookDirs ) {
			// Evict the worst element (with the lowest score and with the last order by the operator < in the max-heap)
			std::pop_heap( candidatesBegin, candidatesPtr );
			candidatesPtr--;
		}

		new ( candidatesPtr++ )AreaAndScore( areaNum, score );
		std::push_heap( candidatesBegin, candidatesPtr );
	}

	return candidatesPtr;
}