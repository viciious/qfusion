#include "BunnyStraighteningReachChainAction.h"
#include "MovementLocal.h"
#include "SameFloorClusterAreasCache.h"
#include "../ai_manager.h"

BunnyStraighteningReachChainAction::BunnyStraighteningReachChainAction( Bot *bot_ )
	: BotBunnyTestingMultipleLookDirsAction( bot_, NAME, COLOR_RGB( 0, 192, 0 ) ) {
	supportsObstacleAvoidance = true;
	// The constructor cannot be defined in the header due to this bot member access
	suggestedAction = &bot_->bunnyToBestShortcutAreaAction;
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
	Assert( suggestedAction == &self->ai->botRef->bunnyToBestShortcutAreaAction );
	// * Quotas are allowed to request only once per frame,
	// and subsequent calls for the same client fail (return with false).
	// Set suggested look dirs for both actions, otherwise the second one (almost) never gets a quota.
	// * Never try to acquire a quota here if a bot is really blocked.
	// These predicted actions are very likely to fail again,
	// and fallbacks do not get their quotas leading to blocking to bot suicide.
	if( self->ai->botRef->MillisInBlockedState() < 100 && AiManager::Instance()->TryGetExpensiveComputationQuota( self ) ) {
		this->maxSuggestedLookDirs = 11;
		self->ai->botRef->bunnyToBestShortcutAreaAction.maxSuggestedLookDirs = 5;
	} else {
		this->maxSuggestedLookDirs = 2;
		self->ai->botRef->bunnyToBestShortcutAreaAction.maxSuggestedLookDirs = 2;
	}

	const AiAasWorld *aasWorld = AiAasWorld::Instance();
	const aas_reachability_t *aasReachabilities = aasWorld->Reachabilities();

	int lastValidReachIndex = -1;
	constexpr unsigned MAX_TESTED_REACHABILITIES = 16U;
	const unsigned maxTestedReachabilities = std::min( MAX_TESTED_REACHABILITIES, nextReachChain.size() );
	const aas_reachability_t *reachStoppedAt = nullptr;
	for( unsigned i = 0; i < maxTestedReachabilities; ++i ) {
		const auto &reach = aasReachabilities[nextReachChain[i].ReachNum()];
		if( reach.traveltype != TRAVEL_WALK && reach.traveltype != TRAVEL_WALKOFFLEDGE ) {
			if( reach.traveltype != TRAVEL_JUMP && reach.traveltype != TRAVEL_STRAFEJUMP ) {
				reachStoppedAt = &reach;
				break;
			}
		}

		lastValidReachIndex++;
	}

	if( lastValidReachIndex < 0 || lastValidReachIndex >= (int)maxTestedReachabilities ) {
		Debug( "There were no supported for bunnying reachabilities\n" );
		return;
	}
	Assert( lastValidReachIndex < (int)maxTestedReachabilities );

	AreaAndScore candidates[MAX_TESTED_REACHABILITIES];
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
	const auto *aasReachabilities = aasWorld->Reachabilities();
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const auto *aasAreaFloorClusterNums = aasWorld->AreaFloorClusterNums();
	const auto *aasAreaStairsClusterNums = aasWorld->AreaStairsClusterNums();
	const int navTargetAasAreaNum = context->NavTargetAasAreaNum();

	const auto *dangerToEvade = self->ai->botRef->perceptionManager.PrimaryDanger();
	// Reduce branching in the loop below
	if( self->ai->botRef->ShouldRushHeadless() || ( dangerToEvade && !dangerToEvade->SupportsImpactTests() ) ) {
		dangerToEvade = nullptr;
	}

	int metStairsClusterNum = 0;

	int currAreaNum = context->CurrAasAreaNum();
	int floorClusterNum = 0;
	if( int groundedAreaNum = context->CurrGroundedAasAreaNum() ) {
		floorClusterNum = aasAreaFloorClusterNums[groundedAreaNum];
	}

	// Do not make it speed-depended, it leads to looping/jitter!
	const float distanceThreshold = 256.0f + 512.0f * self->ai->botRef->Skill();

	AreaAndScore *candidatesPtr = candidatesBegin;
	float minScore = 0.0f;

	Vec3 traceStartPoint( entityPhysicsState.Origin() );
	traceStartPoint.Z() += playerbox_stand_viewheight;
	for( int i = lastValidReachIndex; i >= 0; --i ) {
		const int reachNum = nextReachChain[i].ReachNum();
		const auto &reachability = aasReachabilities[reachNum];
		int areaNum = reachability.areanum;
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

		Vec3 areaPoint( area.center[0], area.center[1], area.mins[2] + 4.0f );
		// Skip areas higher than the bot (to allow moving on a stairs chain, we test distance/height ratio)
		if( area.mins[2] > entityPhysicsState.Origin()[2] ) {
			float distance = areaPoint.FastDistance2DTo( entityPhysicsState.Origin() );
			if( area.mins[2] - entityPhysicsState.Origin()[2] > M_SQRT1_2 * distance ) {
				continue;
			}
		}

		const float squareDistanceToArea = DistanceSquared( area.center, entityPhysicsState.Origin() );
		// Skip way too close areas (otherwise the bot might fall into endless looping)
		if( squareDistanceToArea < SQUARE( 0.4f * distanceThreshold ) ) {
			continue;
		}

		// Skip way too far areas (this is mainly an optimization for the following SolidWorldTrace() call)
		if( squareDistanceToArea > SQUARE( distanceThreshold ) ) {
			continue;
		}

		if( dangerToEvade && dangerToEvade->HasImpactOnPoint( areaPoint ) ) {
			continue;
		}

		// Compute score first to cut off expensive tracing
		const float prevMinScore = minScore;
		// Give far areas greater initial score
		float score = 999999.0f;
		if( areaNum != navTargetAasAreaNum ) {
			// Avoid a division by zero by shifting both nominator and denominator by 1.
			// Note that it slightly shifts score too but there are no upper bounds for the score.
			score = 0.5f + 0.5f * ( (float)( i + 1 ) / (float)( lastValidReachIndex + 1 ) );
			// Try skip "junk" areas (sometimes these areas cannot be avoided in the shortest path)
			if( areaFlags & AREA_JUNK ) {
				score *= 0.1f;
			}
			// Give ledge areas a bit smaller score (sometimes these areas cannot be avoided in the shortest path)
			if( areaFlags & AREA_LEDGE ) {
				score *= 0.7f;
			}
			// Prefer not bounded by walls areas to avoid bumping into walls
			if( !( areaFlags & AREA_WALL ) ) {
				score *= 1.6f;
			}

			// Do not test lower score areas if there is already enough tested candidates
			if( score > minScore ) {
				minScore = score;
			} else if( candidatesPtr - candidatesBegin >= (ptrdiff_t)maxSuggestedLookDirs ) {
				continue;
			}
		}

		// Make sure the bot can see the ground
		// On failure, restore minScore (it might have been set to the value of the rejected area score on this loop step)
		if( floorClusterNum && floorClusterNum == aasAreaFloorClusterNums[areaNum] ) {
			if( !IsAreaWalkableInFloorCluster( currAreaNum, areaNum ) ) {
				minScore = prevMinScore;
				continue;
			}
		} else {
			if( !TraceArcInSolidWorld( entityPhysicsState, traceStartPoint.Data(), areaPoint.Data() ) ) {
				minScore = prevMinScore;
				continue;
			}
		}

		new ( candidatesPtr++ )AreaAndScore( areaNum, score );
	}

	return candidatesPtr;
}