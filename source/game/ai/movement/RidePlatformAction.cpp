#include "RidePlatformAction.h"
#include "MovementLocal.h"

void RidePlatformAction::PlanPredictionStep( Context *context ) {
	if( !GenericCheckIsActionEnabled( context, &DefaultWalkAction() ) ) {
		return;
	}

	const edict_t *platform = GetPlatform( context );
	if( !platform ) {
		context->cannotApplyAction = true;
		context->actionSuggestedByAction = &DefaultWalkAction();
		Debug( "Cannot apply the action (cannot find a platform below)\n" );
		return;
	}

	if( platform->moveinfo.state == STATE_TOP ) {
		SetupExitPlatformMovement( context, platform );
	} else {
		SetupIdleRidingPlatformMovement( context, platform );
	}
}

void RidePlatformAction::CheckPredictionStepResults( Context *context ) {
	BaseMovementAction::CheckPredictionStepResults( context );
	if( context->cannotApplyAction || context->isCompleted ) {
		return;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const int targetAreaNum = module->savedPlatformAreas[currTestedAreaIndex];
	const int currAreaNum = entityPhysicsState.CurrAasAreaNum();
	const int droppedToFloorAreaNum = entityPhysicsState.DroppedToFloorAasAreaNum();
	if( currAreaNum == targetAreaNum || droppedToFloorAreaNum == targetAreaNum ) {
		Debug( "The bot has entered the target exit area, should stop planning\n" );
		context->isCompleted = true;
		return;
	}

	const unsigned sequenceDuration = SequenceDuration( context );
	if( sequenceDuration < 250 ) {
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	if( originAtSequenceStart.SquareDistance2DTo( entityPhysicsState.Origin() ) < 32 * 32 ) {
		Debug( "The bot is likely stuck trying to use area #%d to exit the platform\n", currTestedAreaIndex );
		context->SetPendingRollback();
		return;
	}

	if( sequenceDuration > 550 ) {
		Debug( "The bot still has not reached the target exit area\n" );
		context->SetPendingRollback();
		return;
	}

	context->SaveSuggestedActionForNextFrame( this );
}

void RidePlatformAction::OnApplicationSequenceStopped( Context *context,
													   SequenceStopReason stopReason,
													   unsigned stoppedAtFrameIndex ) {
	BaseMovementAction::OnApplicationSequenceStopped( context, stopReason, stoppedAtFrameIndex );
	if( stopReason != FAILED && stopReason != DISABLED ) {
		return;
	}

	currTestedAreaIndex++;
}

void DirToKeyInput( const Vec3 &desiredDir, const vec3_t actualForwardDir, const vec3_t actualRightDir, BotInput *input );

inline void DirToKeyInput( const Vec3 &desiredDir, const AiEntityPhysicsState &entityPhysicsState, BotInput *input ) {
	DirToKeyInput( desiredDir, entityPhysicsState.ForwardDir().Data(), entityPhysicsState.RightDir().Data(), input );
}

void RidePlatformAction::SetupIdleRidingPlatformMovement( Context *context, const edict_t *platform ) {
	TrySaveExitAreas( context, platform );

	auto *botInput = &context->record->botInput;
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	// Put all this shared clutter at the beginning

	botInput->isUcmdSet = true;
	botInput->canOverrideUcmd = true;
	botInput->canOverrideLookVec = true;

	Debug( "Stand idle on the platform, do not plan ahead\n" );
	context->isCompleted = true;

	// We are sure the platform has not arrived to the top, otherwise this method does not get called
	if( VectorCompare( vec3_origin, platform->velocity ) && bot->MillisInBlockedState() > 500 ) {
		// A rare but possible situation that happens e.g. on wdm1 near the GA
		// A bot stands still and the platform is considered its groundentity but it does not move

		// Look at the trigger in 2D world projection
		Vec3 intendedLookDir( platform->s.origin );
		intendedLookDir -= entityPhysicsState.Origin();
		intendedLookDir.Z() = 0;
		// Denormalization is possible, add a protection
		float squareLength = intendedLookDir.SquaredLength();
		if( squareLength > 1 ) {
			intendedLookDir *= 1.0f / sqrtf( squareLength );
			botInput->SetIntendedLookDir( intendedLookDir, true );
			DirToKeyInput( intendedLookDir, entityPhysicsState, botInput );
		} else {
			// Set a random input that should not lead to blocking
			botInput->SetIntendedLookDir( &axis_identity[AXIS_UP] );
			botInput->SetForwardMovement( 1 );
			botInput->SetRightMovement( 1 );
		}

		// Our aim is firing a trigger and nothing else, walk carefully
		botInput->SetWalkButton( true );
		return;
	}

	// The bot remains staying still on a platform in all other cases

	if( bot->GetSelectedEnemies().AreValid() ) {
		Vec3 toEnemy( bot->GetSelectedEnemies().LastSeenOrigin() );
		toEnemy -= context->movementState->entityPhysicsState.Origin();
		botInput->SetIntendedLookDir( toEnemy, false );
		return;
	}

	float height = platform->moveinfo.start_origin[2] - platform->moveinfo.end_origin[2];
	float frac = ( platform->s.origin[2] - platform->moveinfo.end_origin[2] ) / height;
	// If the bot is fairly close to the destination and there are saved areas, start looking at the first one
	if( frac > 0.5f && !module->savedPlatformAreas.empty() ) {
		const auto &area = AiAasWorld::Instance()->Areas()[module->savedPlatformAreas.front()];
		Vec3 lookVec( area.center );
		lookVec -= context->movementState->entityPhysicsState.Origin();
		botInput->SetIntendedLookDir( lookVec, false );
		return;
	}

	// Keep looking in the current direction but change pitch
	Vec3 lookVec( context->movementState->entityPhysicsState.ForwardDir() );
	lookVec.Z() = 0.5f - 1.0f * frac;
	botInput->SetIntendedLookDir( lookVec, false );
}

void RidePlatformAction::SetupExitPlatformMovement( Context *context, const edict_t *platform ) {
	const ExitAreasVector &suggestedAreas = SuggestExitAreas( context, platform );
	if( suggestedAreas.empty() ) {
		Debug( "Warning: there is no platform exit areas, do not plan ahead\n" );
		this->isDisabledForPlanning = true;
		context->SetPendingRollback();
		return;
	}

	if( currTestedAreaIndex >= suggestedAreas.size() ) {
		Debug( "All suggested exit area tests have failed\n" );
		this->isDisabledForPlanning = true;
		context->SetPendingRollback();
		return;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	auto *botInput = &context->record->botInput;

	const auto &area = AiAasWorld::Instance()->Areas()[suggestedAreas[currTestedAreaIndex]];
	Vec3 intendedLookDir( area.center );
	intendedLookDir.Z() = area.mins[2] + 32;
	intendedLookDir -= entityPhysicsState.Origin();
	float distance = intendedLookDir.NormalizeFast();
	botInput->SetIntendedLookDir( intendedLookDir, true );

	botInput->isUcmdSet = true;
	float dot = intendedLookDir.Dot( entityPhysicsState.ForwardDir() );
	if( dot < 0.0f ) {
		botInput->SetTurnSpeedMultiplier( 3.0f );
		return;
	}

	if( dot < 0.7f ) {
		botInput->SetWalkButton( true );
		return;
	}

	if( distance > 64.0f ) {
		botInput->SetSpecialButton( true );
	}
}

const edict_t *RidePlatformAction::GetPlatform( Context *context ) const {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const edict_t *groundEntity = entityPhysicsState.GroundEntity();
	if( groundEntity ) {
		return groundEntity->use == Use_Plat ? groundEntity : nullptr;
	}

	trace_t trace;
	Vec3 startPoint( entityPhysicsState.Origin() );
	startPoint.Z() += playerbox_stand_mins[2];
	Vec3 endPoint( entityPhysicsState.Origin() );
	endPoint.Z() += playerbox_stand_mins[2];
	endPoint.Z() -= 32.0f;
	edict_t *const ignore = game.edicts + bot->EntNum();
	G_Trace( &trace, startPoint.Data(), playerbox_stand_mins, playerbox_stand_maxs, endPoint.Data(), ignore, MASK_ALL );
	if( trace.ent != -1 ) {
		groundEntity = game.edicts + trace.ent;
		if( groundEntity->use == Use_Plat ) {
			return groundEntity;
		}
	}

	return nullptr;
}

void RidePlatformAction::TrySaveExitAreas( Context *context, const edict_t *platform ) {
	auto &savedAreas = module->savedPlatformAreas;
	// Don't overwrite already present areas
	if( !savedAreas.empty() ) {
		return;
	}

	int navTargetAreaNum = bot->NavTargetAasAreaNum();
	// Skip if there is no nav target.
	// SetupExitAreaMovement() handles the case when there is no exit areas.
	if( !navTargetAreaNum ) {
		return;
	}

	FindExitAreas( context, platform, tmpExitAreas );

	const auto *routeCache = bot->RouteCache();
	const int travelFlags = bot->AllowedTravelFlags();

	int areaTravelTimes[MAX_SAVED_AREAS];
	for( unsigned i = 0, end = tmpExitAreas.size(); i < end; ++i ) {
		int travelTime = routeCache->TravelTimeToGoalArea( tmpExitAreas[i], navTargetAreaNum, travelFlags );
		if( !travelTime ) {
			continue;
		}

		savedAreas.push_back( tmpExitAreas[i] );
		areaTravelTimes[i] = travelTime;
	}

	// Sort areas
	for( unsigned i = 1, end = savedAreas.size(); i < end; ++i ) {
		int area = savedAreas[i];
		int travelTime = areaTravelTimes[i];
		int j = i - 1;
		for(; j >= 0 && areaTravelTimes[j] < travelTime; j-- ) {
			savedAreas[j + 1] = savedAreas[j];
			areaTravelTimes[j + 1] = areaTravelTimes[j];
		}

		savedAreas[j + 1] = area;
		areaTravelTimes[j + 1] = travelTime;
	}
}

typedef RidePlatformAction::ExitAreasVector ExitAreasVector;

const ExitAreasVector &RidePlatformAction::SuggestExitAreas( Context *context, const edict_t *platform ) {
	if( !module->savedPlatformAreas.empty() ) {
		return module->savedPlatformAreas;
	}

	FindExitAreas( context, platform, tmpExitAreas );

	// Save found areas to avoid repeated FindExitAreas() calls while testing next area after rollback
	for( int areaNum: tmpExitAreas )
		module->savedPlatformAreas.push_back( areaNum );

	return tmpExitAreas;
};

void RidePlatformAction::FindExitAreas( Context *context, const edict_t *platform, ExitAreasVector &exitAreas ) {
	const auto &aasWorld = AiAasWorld::Instance();
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasAreaSettings = aasWorld->AreaSettings();

	edict_t *const ignore = game.edicts + bot->EntNum();

	const BotMovementState &movementState = context ? *context->movementState : module->movementState;

	exitAreas.clear();

	Vec3 mins( platform->r.absmin );
	Vec3 maxs( platform->r.absmax );
	// SP_func_plat(): start is the top position, end is the bottom
	mins.Z() = platform->moveinfo.start_origin[2];
	maxs.Z() = platform->moveinfo.start_origin[2] + 96.0f;
	// We have to extend bounds... check whether wdm9 movement is OK if one changes these values.
	for( int i = 0; i < 2; ++i ) {
		mins.Data()[i] -= 96.0f;
		maxs.Data()[i] += 96.0f;
	}

	Vec3 finalBotOrigin( movementState.entityPhysicsState.Origin() );
	// Add an extra 1 unit offset for tracing purposes
	finalBotOrigin.Z() += platform->moveinfo.start_origin[2] - platform->s.origin[2] + 1.0f;

	trace_t trace;
	int bboxAreaNums[48];
	const int numBBoxAreas = aasWorld->BBoxAreas( mins, maxs, bboxAreaNums, 48 );
	for( int i = 0; i < numBBoxAreas; ++i ) {
		const int areaNum = bboxAreaNums[i];
		const auto &area = aasAreas[areaNum];
		const auto &areaSettings = aasAreaSettings[areaNum];
		if( !( areaSettings.areaflags & AREA_GROUNDED ) ) {
			continue;
		}
		if( areaSettings.areaflags & ( AREA_JUNK | AREA_DISABLED ) ) {
			continue;
		}
		if( areaSettings.contents & ( AREACONTENTS_DONOTENTER | AREACONTENTS_MOVER ) ) {
			continue;
		}

		Vec3 areaPoint( area.center );
		areaPoint.Z() = area.mins[2] + 8.0f;

		// Do not use player box as trace mins/maxs (it leads to blocking when a bot rides a platform near a wall)
		G_Trace( &trace, finalBotOrigin.Data(), nullptr, nullptr, areaPoint.Data(), ignore, MASK_ALL );
		if( trace.fraction != 1.0f ) {
			continue;
		}

		exitAreas.push_back( areaNum );
		if( exitAreas.size() == exitAreas.capacity() ) {
			break;
		}
	}
}