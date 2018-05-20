#include "LandOnSavedAreasAction.h"
#include "MovementLocal.h"

int LandOnSavedAreasAction::FindJumppadAreaNum( const edict_t *jumppadEntity ) {
	// TODO: This can be precomputed at level start
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasAreaSettings = aasWorld->AreaSettings();

	// Jumppad entity origin is not what one might think...
	Vec3 jumppadOrigin( jumppadEntity->r.absmin );
	jumppadOrigin += jumppadEntity->r.absmax;
	jumppadOrigin *= 0.5f;

	int entAreaNum = aasWorld->FindAreaNum( jumppadOrigin );
	if( entAreaNum ) {
		const auto &areaSettings = aasAreaSettings[entAreaNum];
		const int contents = areaSettings.contents;
		const int areaflags = areaSettings.areaflags;
		if( ( contents & AREACONTENTS_JUMPPAD ) && !( contents & AREACONTENTS_DONOTENTER ) && !( areaflags & AREA_DISABLED ) ) {
			return entAreaNum;
		}
	}

	int areaNums[32];
	Vec3 mins( -64, -64, -64 );
	Vec3 maxs( +64, +64, +64 );
	mins += jumppadOrigin;
	maxs += jumppadOrigin;
	int numAreas = aasWorld->BBoxAreas( mins, maxs, areaNums, 32 );
	for( int i = 0; i < numAreas; ++i ) {
		const int areaNum = areaNums[i];
		const auto &areaSettings = aasAreaSettings[areaNum];
		const int contents = areaSettings.contents;
		if( !( contents & AREACONTENTS_JUMPPAD ) ) {
			continue;
		}
		if( contents & AREACONTENTS_DONOTENTER ) {
			continue;
		}
		if( areaSettings.areaflags & AREA_DISABLED ) {
			continue;
		}

		return areaNum;
	}

	// Ensure the area is always found. Do not hide the bug, a bot would keep jumping on the trigger forever.
	constexpr const char *tag = "LandOnSavedAreasAction::FindJumppadAreaNum()";
	constexpr const char *format = "Can't find an AAS area num for the jumppad @ %.1f %.1f %.1f\n";
	AI_FailWith( tag, format, jumppadOrigin.X(), jumppadOrigin.Y(), jumppadOrigin.Z() );
}

static float PointToSegmentSquareDistance( const vec3_t point, const vec3_t start, const vec3_t end ) {
	Vec3 segmentVec( end );
	segmentVec -= start;
	Vec3 pointToStart( start );
	pointToStart -= point;

	float pointToStartDotVec = pointToStart.Dot( segmentVec );
	if( pointToStartDotVec >= 0.0f ) {
		return DistanceSquared( point, start );
	}

	Vec3 pointToEnd( end );
	pointToEnd -= point;

	if( pointToEnd.Dot( segmentVec ) <= 0.0f ) {
		return DistanceSquared( point, end );
	}

	Vec3 projection( segmentVec );
	projection *= -pointToStartDotVec / segmentVec.SquaredLength();
	projection += pointToStart;
	return projection.SquaredLength();
}

static float PointToAreaSquareDistance( const vec3_t point, const aas_area_t &area ) {
	if( area.mins[2] > point[2] ) {
		return std::numeric_limits<float>::max();
	}

	if( area.mins[0] >= point[0] && area.maxs[0] <= point[0] && area.mins[1] >= point[1] && area.maxs[1] <= point[1] ) {
		return 0.0f;
	}

	float minDistance = std::numeric_limits<float>::max();
	vec3_t sideStart, sideEnd;
	sideStart[2] = sideEnd[2] = area.mins[2];
	const float *bounds[] = { area.mins, area.maxs };
	// For each side
	for( int i = 0; i < 4; ++i ) {
		// Make side segment
		for( int j = 0; j < 2; ++j ) {
			sideStart[j] = bounds[( ( i + 0 ) >> j ) & 1][j];
			sideEnd[j] = bounds[( ( i + 1 ) >> j ) & 1][j];
		}

		float distance = PointToSegmentSquareDistance( point, sideStart, sideEnd );
		if( distance < minDistance ) {
			minDistance = distance;
		}
	}

	return minDistance;
}

float LandOnSavedAreasAction::SaveJumppadLandingAreas( const edict_t *jumppadEntity ) {
	savedLandingAreas.clear();

	int jumppadAreaNum = FindJumppadAreaNum( jumppadEntity );
	if( !jumppadAreaNum ) {
		return -999999.9f;
	}

	const auto *aasWorld = AiAasWorld::Instance();
	const auto *routeCache = bot->RouteCache();
	if( int navTargetAreaNum = bot->NavTargetAasAreaNum() ) {
		int reachNum = 0;
		if( routeCache->PreferredRouteToGoalArea( jumppadAreaNum, navTargetAreaNum, &reachNum ) ) {
			int jumppadTargetAreaNum = aasWorld->Reachabilities()[reachNum].areanum;
			return SaveLandingAreasForJumppadTargetArea( jumppadEntity, navTargetAreaNum, jumppadTargetAreaNum );
		}
	}

	// The nav target is not reachable. Try to find any areas reachable from the jumppad area by using the jumppad
	const auto &jumppadAreaSettings = aasWorld->AreaSettings()[jumppadAreaNum];
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const auto *aasReach = aasWorld->Reachabilities();
	const float *targetOrigin = jumppadEntity->target_ent->s.origin;
	FilteredAreas filteredAreas;
	// Find an area closest to the jumppad target
	for( int i = 0; i < jumppadAreaSettings.numreachableareas; ++i ) {
		const auto &reach = aasReach[i + jumppadAreaSettings.firstreachablearea];
		if( reach.traveltype != TRAVEL_JUMPPAD ) {
			continue;
		}

		const int areaNum = reach.areanum;
		const auto &areaSettings = aasAreaSettings[areaNum];
		if( areaSettings.areaflags & AREA_DISABLED ) {
			continue;
		}
		if( areaSettings.contents & AREACONTENTS_DONOTENTER ) {
			continue;
		}
		const auto &area = aasAreas[areaNum];
		// Skip areas that are higher than the jumppad target entity
		if( area.mins[2] + 16 > jumppadEntity->target_ent->s.origin[2] ) {
			continue;
		}
		// Closer to the jumppad entity target areas get greater score
		float score = 1.0f / ( 1.0f + PointToAreaSquareDistance( targetOrigin, area ) );
		filteredAreas.emplace_back( AreaAndScore( areaNum, score ) );
		if( filteredAreas.size() == filteredAreas.capacity() ) {
			break;
		}
	}

	// Sort areas so best areas are first
	std::sort( filteredAreas.begin(), filteredAreas.end() );

	return SaveFilteredCandidateAreas( jumppadEntity, 0, filteredAreas );
}

float LandOnSavedAreasAction::SaveLandingAreasForJumppadTargetArea( const edict_t *jumppadEntity,
																	int navTargetAreaNum,
																	int jumppadTargetAreaNum ) {
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *routeCache = bot->RouteCache();
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasAreaSettings = aasWorld->AreaSettings();

	// Get areas around the jumppad area
	const auto &jumppadTargetArea = aasAreas[jumppadTargetAreaNum];
	Vec3 mins( -320, -320, -16 );
	Vec3 maxs( +320, +320, +16 );
	// It's better to use the target entity and not the target area center,
	// because the center might be biased and it leads to poor area selection e.g. on major wdm7 jumppad.
	mins += jumppadEntity->target_ent->s.origin;
	maxs += jumppadEntity->target_ent->s.origin;
	int bboxAreas[48];
	const int numBBoxAreas = aasWorld->BBoxAreas( mins, maxs, bboxAreas, 48 );

	const int baseTravelTime = routeCache->PreferredRouteToGoalArea( jumppadTargetAreaNum, navTargetAreaNum );
	// If the target is for some reasons unreachable or the jumppad target area is the nav target area too
	if( baseTravelTime <= 1 ) {
		// Return some default values in hope they are useful
		savedLandingAreas.push_back( jumppadTargetAreaNum );
		return jumppadTargetArea.mins[2];
	}

	// Filter raw nearby areas
	FilteredAreas filteredAreas;
	for( int i = 0; i < numBBoxAreas; ++i ) {
		const int areaNum = bboxAreas[i];
		// Skip tests for the target area
		if( areaNum == jumppadTargetAreaNum ) {
			continue;
		}

		const auto &rawArea = aasAreas[areaNum];
		// Skip areas that are lower than the target area more than 16 units
		if( rawArea.mins[2] + 16 < jumppadTargetArea.mins[2] ) {
			continue;
		}
		// Skip areas that are higher than the jumppad target entity
		if( rawArea.mins[2] + 16 > jumppadEntity->target_ent->s.origin[2] ) {
			continue;
		}

		const auto &areaSettings = aasAreaSettings[areaNum];
		if( !( areaSettings.areaflags & AREA_GROUNDED ) ) {
			continue;
		}
		if( areaSettings.contents & AREACONTENTS_DONOTENTER ) {
			continue;
		}
		if( areaSettings.areaflags & ( AREA_JUNK | AREA_DISABLED ) ) {
			continue;
		}

		const int travelTime = routeCache->PreferredRouteToGoalArea( areaNum, navTargetAreaNum );
		// If the nav target is not reachable from the box area or
		// it leads to a greater travel time than the jumppad target area
		if( !travelTime || travelTime >= baseTravelTime ) {
			continue;
		}

		// The score is greater if it shortens travel time greater
		float score = (float)baseTravelTime / (float)travelTime;
		// Apply penalty for ledge areas (prevent falling just after landing)
		if( areaSettings.areaflags & AREA_LEDGE ) {
			score *= 0.5f;
		}

		filteredAreas.emplace_back( AreaAndScore( areaNum, score ) );
		if( filteredAreas.size() == filteredAreas.capacity() ) {
			break;
		}
	}

	// Sort filtered areas so best areas are first
	std::sort( filteredAreas.begin(), filteredAreas.end() );

	return SaveFilteredCandidateAreas( jumppadEntity, jumppadTargetAreaNum, filteredAreas );
}

float LandOnSavedAreasAction::SaveFilteredCandidateAreas( const edict_t *jumppadEntity,
														  int jumppadTargetAreaNum,
														  const FilteredAreas &filteredAreas ) {
	savedLandingAreas.clear();
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasAreas = aasWorld->Areas();

	for( unsigned i = 0, end = std::min( filteredAreas.size(), savedLandingAreas.capacity() ); i < end; ++i )
		savedLandingAreas.push_back( filteredAreas[i].areaNum );

	// Always add the target area (with the lowest priority)
	if( jumppadTargetAreaNum ) {
		if( savedLandingAreas.size() == savedLandingAreas.capacity() ) {
			savedLandingAreas.pop_back();
		}

		savedLandingAreas.push_back( jumppadTargetAreaNum );
	}

	float maxAreaZ = std::numeric_limits<float>::min();
	for( int areaNum: savedLandingAreas ) {
		maxAreaZ = std::max( maxAreaZ, aasAreas[areaNum].mins[2] );
	}

	return maxAreaZ;
}

void LandOnSavedAreasAction::BeforePlanning() {
	BaseMovementAction::BeforePlanning();
	currAreaIndex = 0;
	totalTestedAreas = 0;

	this->savedLandingAreas.clear();
	for( int areaNum: module->savedLandingAreas )
		this->savedLandingAreas.push_back( areaNum );

	module->savedLandingAreas.clear();
}

void LandOnSavedAreasAction::AfterPlanning() {
	BaseMovementAction::AfterPlanning();
	if( this->isDisabledForPlanning ) {
		return;
	}

	for( int areaNum: this->savedLandingAreas )
		module->savedLandingAreas.push_back( areaNum );
}

bool LandOnSavedAreasAction::TryLandingStepOnArea( int areaNum, Context *context ) {
	auto *botInput = &context->record->botInput;
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const float *origin = entityPhysicsState.Origin();

	const auto &area = AiAasWorld::Instance()->Areas()[areaNum];
	Vec3 areaPoint( area.center );
	// Lower area point to a bottom of area. Area mins/maxs are absolute.
	areaPoint.Z() = area.mins[2];
	// Do not try to "land" on upper areas
	if( areaPoint.Z() > origin[2] ) {
		Debug( "Cannot land on an area that is above the bot origin in the given movement state\n" );
		return false;
	}

	botInput->Clear();
	botInput->isUcmdSet = true;

	Vec3 intendedLookDir( 0, 0, -1 );
	// Prevent flying over the area.
	if( area.mins[0] > origin[0] || area.maxs[0] < origin[0] || area.mins[1] > origin[1] || area.maxs[1] < origin[1] ) {
		// Most likely case (the bot is outside of the area bounds)
		intendedLookDir.Set( areaPoint );
		intendedLookDir -= origin;
		intendedLookDir.NormalizeFast();
	}
	botInput->SetIntendedLookDir( intendedLookDir, true );

	// Apply QW-like air control
	float dotRight = entityPhysicsState.RightDir().Dot( intendedLookDir );
	if( dotRight > 0.7f ) {
		botInput->SetRightMovement( +1 );
	} else if( dotRight < -0.7f ) {
		botInput->SetRightMovement( -1 );
	} else {
		// While we do not use forwardbunny, there is still a little air control from forward key
		float dotForward = entityPhysicsState.ForwardDir().Dot( intendedLookDir );
		if( dotForward > 0.3f ) {
			botInput->SetForwardMovement( +1 );
		} else if( dotForward < -0.3f ) {
			botInput->SetForwardMovement( -1 );
		}
	}

	return true;
}

void LandOnSavedAreasAction::PlanPredictionStep( Context *context ) {
	if( !GenericCheckIsActionEnabled( context, &DummyAction() ) ) {
		return;
	}

	// This list might be empty if all nearby areas have been disabled (e.g. as blocked by enemy).
	if( savedLandingAreas.empty() ) {
		Debug( "Cannot apply action: the saved landing areas list is empty\n" );
		this->isDisabledForPlanning = true;
		context->cannotApplyAction = true;
		context->actionSuggestedByAction = &DummyAction();
		return;
	}

	// If there the current tested area is set
	if( currAreaIndex >= 0 ) {
		Assert( (int)savedLandingAreas.size() > currAreaIndex );
		// Continue testing this area
		if( TryLandingStepOnArea( savedLandingAreas[currAreaIndex], context ) ) {
			context->SaveSuggestedActionForNextFrame( this );
			return;
		}

		// Schedule next saved area for testing
		const char *format = "Landing on area %d/%d has failed, roll back to initial landing state for next area\n";
		Debug( format, currAreaIndex, savedLandingAreas.size() );
		currAreaIndex = -1;
		totalTestedAreas++;
		// Force rolling back to savepoint
		context->SetPendingRollback();
		// (the method execution implicitly will be continued on the code below outside this condition on next call)
		return;
	}

	// There is not current tested area set, try choose one that fit
	for(; totalTestedAreas < savedLandingAreas.size(); totalTestedAreas++ ) {
		// Test each area left using a-priori feasibility of an area
		if( TryLandingStepOnArea( savedLandingAreas[totalTestedAreas], context ) ) {
			// Set the area as current
			currAreaIndex = totalTestedAreas;
			// Create a savepoint
			context->savepointTopOfStackIndex = context->topOfStackIndex;
			// (the method execution will be implicitly continue on the code inside the condition above on next call)
			Debug( "Area %d/%d has been chosen for landing tests\n", currAreaIndex, savedLandingAreas.size() );
			context->SaveSuggestedActionForNextFrame( this );
			return;
		}
	}

	// All areas have been tested, and there is no suitable area for landing
	Debug( "Warning: An area suitable for landing has not been found\n" );

	// Just look at the jumppad target
	const auto &movementState = context->movementState;
	auto *botInput = &context->record->botInput;
	Vec3 toTargetDir( movementState->jumppadMovementState.JumppadEntity()->target_ent->s.origin );
	toTargetDir -= movementState->entityPhysicsState.Origin();
	toTargetDir.NormalizeFast();

	botInput->SetIntendedLookDir( toTargetDir, true );
	// Try apply air control preferring QW-style one
	float dotForward = toTargetDir.Dot( movementState->entityPhysicsState.ForwardDir() );
	if( dotForward < -0.3f ) {
		botInput->SetTurnSpeedMultiplier( 3.0f );
	}

	float dotRight = toTargetDir.Dot( movementState->entityPhysicsState.RightDir() );
	if( dotRight > 0.5f ) {
		botInput->SetRightMovement( +1 );
	} else if( dotRight < -0.5f ) {
		botInput->SetRightMovement( -1 );
	} else if( dotForward > 0.5f ) {
		botInput->SetForwardMovement( +1 );
	}

	// Disallow any input rotation while landing, it relies on a side aircontrol.
	botInput->SetAllowedRotationMask( BotInputRotation::NONE );

	botInput->isUcmdSet = true;

	// Do not predict ahead.
	context->isCompleted = true;
}

void LandOnSavedAreasAction::CheckPredictionStepResults( Context *context ) {
	BaseMovementAction::CheckPredictionStepResults( context );
	// If movement step failed, make sure that the next area (if any) will be tested after rollback
	if( context->cannotApplyAction ) {
		totalTestedAreas++;
		currAreaIndex = -1;
		return;
	}

	if( context->isCompleted ) {
		return;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( !entityPhysicsState.GroundEntity() ) {
		return;
	}

	// Check which area bot has landed in
	Assert( currAreaIndex >= 0 && currAreaIndex == (int)totalTestedAreas && currAreaIndex < (int)savedLandingAreas.size() );
	const int targetAreaNum = savedLandingAreas[currAreaIndex];
	int currAreaNums[2] = { 0, 0 };
	const int numCurrAreas = entityPhysicsState.PrepareRoutingStartAreas( currAreaNums );
	// If the bot is in the target area
	if( currAreaNums[0] == targetAreaNum || currAreaNums[1] == targetAreaNum ) {
		Debug( "A prediction step has lead to touching a ground in the target landing area, should stop planning\n" );
		context->isCompleted = true;
		return;
	}

	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasAreaFloorClusterNums = aasWorld->AreaFloorClusterNums();
	// If the target area is in some floor cluster
	if( int targetFloorClusterNum = aasAreaFloorClusterNums[targetAreaNum] ) {
		int i = 0;
		for(; i < numCurrAreas; ++i ) {
			if( aasAreaFloorClusterNums[currAreaNums[i]] == targetFloorClusterNum ) {
				break;
			}
		}
		// Some of the current areas is in the same cluster
		if( i != numCurrAreas ) {
			Debug( "A prediction step has lead to touching a ground in the floor cluster of the landing area\n" );
			context->isCompleted = true;
			return;
		}
	} else {
		// Check whether the target area is reachable from the current area by walking and seems to be straight-walkable
		int bestTravelTime = std::numeric_limits<int>::max();
		const auto *routeCache = bot->RouteCache();
		for( int i = 0; i < numCurrAreas; ++i ) {
			int travelFlags = TFL_WALK | TFL_WALKOFFLEDGE | TFL_AIR;
			if( int travelTime = routeCache->TravelTimeToGoalArea( currAreaNums[i], targetAreaNum, travelFlags ) ) {
				bestTravelTime = std::min( travelTime, bestTravelTime );
			}
		}
		// If the target area is short-range reachable by walking (in 150 seconds^-2)
		if( bestTravelTime < 150 ) {
			Vec3 testedTargetPoint( aasAreas[targetAreaNum].center );
			// We are sure the target area is grounded
			testedTargetPoint.Z() = aasAreas[targetAreaNum].mins[2] + 1.0f - playerbox_stand_mins[2];
			// Add a unit offset from ground
			Vec3 currPoint( entityPhysicsState.Origin() );
			currPoint.Z() += 1.0f;
			// We have to check against entities in this case
			trace_t trace;
			edict_t *ignore = game.edicts + bot->EntNum();
			float *mins = playerbox_stand_mins;
			float *maxs = playerbox_stand_maxs;
			G_Trace( &trace, currPoint.Data(), mins, maxs, testedTargetPoint.Data(), ignore, MASK_PLAYERSOLID );
			if( trace.fraction == 1.0f ) {
				Debug( "A prediction step has lead to touching a ground in a short-range neighbour area of the target area\n" );
				context->isCompleted = true;
				return;
			}
		}
	}

	Debug( "A prediction step has lead to touching a ground in an unexpected area\n" );
	context->SetPendingRollback();
	// Make sure that the next area (if any) will be tested after rolling back
	totalTestedAreas++;
	currAreaIndex = -1;
}