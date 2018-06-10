#include "BaseMovementAction.h"
#include "MovementLocal.h"

void WalkCarefullyAction::PlanPredictionStep( Context *context ) {
	// Do not even try using (accelerated) bunnying for easy bots.
	// They however will still still perform various jumps,
	// even during regular roaming on plain surfaces (thats what movement fallbacks do).
	// Ramp/stairs areas and areas not in floor clusters are exceptions
	// (these kinds of areas are still troublesome for bot movement).
	BaseMovementAction *suggestedAction = &DefaultBunnyAction();
	if( bot->Skill() < 0.33f ) {
		const auto *aasWorld = AiAasWorld::Instance();
		const int currGroundedAreaNum = context->CurrGroundedAasAreaNum();
		// If the current area is not a ramp-like area
		if( !( aasWorld->AreaSettings()[currGroundedAreaNum].areaflags & AREA_INCLINED_FLOOR ) ) {
			// If the current area is not in a stairs cluster
			if( !( aasWorld->AreaStairsClusterNums()[currGroundedAreaNum] ) ) {
				// If the current area is in a floor cluster
				if( aasWorld->AreaFloorClusterNums()[currGroundedAreaNum ] ) {
					suggestedAction = &DummyAction();
				}
			}
		}
	}

	if( !GenericCheckIsActionEnabled( context, suggestedAction ) ) {
		return;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( !entityPhysicsState.GroundEntity() && entityPhysicsState.HeightOverGround() > 4.0f ) {
		Debug( "Cannot apply action: the bot is quite high above the ground\n" );
		// Prevent falling as a result of following this action application sequence
		SwitchOrRollback( context, suggestedAction );
		return;
	}

	const int currAasAreaNum = context->CurrAasAreaNum();
	if( !currAasAreaNum ) {
		Debug( "Cannot apply action: current AAS area num is undefined\n" );
		// Prevent moving to an undefined area as a result of following this action application sequence
		SwitchOrRollback( context, suggestedAction );
		return;
	}

	const int navTargetAasAreaNum = context->NavTargetAasAreaNum();
	if( !navTargetAasAreaNum ) {
		Debug( "Cannot apply action: nav target AAS area num is undefined\n" );
		// We're sure there were no predicted frames in this action application sequence
		DisableWithAlternative( context, suggestedAction );
		return;
	}

	if( bot->ShouldMoveCarefully() || bot->ShouldBeSilent() ) {
		context->SetDefaultBotInput();
		context->record->botInput.ClearMovementDirections();
		context->record->botInput.SetForwardMovement( 1 );
		if( bot->ShouldMoveCarefully() || context->IsInNavTargetArea() ) {
			context->record->botInput.SetWalkButton( true );
		}
		return;
	}

	int hazardContentsMask = CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_DONOTENTER;
	if( entityPhysicsState.waterLevel ) {
		if( entityPhysicsState.waterType & hazardContentsMask ) {
			Debug( "Cannot apply action: the bot is already in hazard contents\n" );
			SwitchOrRollback( context, suggestedAction );
		}
	} else {
		// Prevent touching a water if not bot is not already walking in it
		hazardContentsMask |= CONTENTS_WATER;
	}

	// First test whether there is a gap in front of the bot
	// (if this test is omitted, bots would use this no-jumping action instead of jumping over gaps and fall down)

	const float zOffset = playerbox_stand_mins[2] - 32.0f - entityPhysicsState.HeightOverGround();
	Vec3 frontTestPoint( entityPhysicsState.ForwardDir() );
	frontTestPoint *= 8.0f;
	frontTestPoint += entityPhysicsState.Origin();
	frontTestPoint.Z() += zOffset;

	trace_t trace;
	StaticWorldTrace( &trace, entityPhysicsState.Origin(), frontTestPoint.Data(), MASK_SOLID | MASK_WATER );
	if( trace.fraction != 1.0f && ( trace.contents & hazardContentsMask ) ) {
		Debug( "There's a hazard in front of a bot\n" );
		SwitchOrStop( context, suggestedAction );
		return;
	}

	// An action might be applied if there are gap or hazard from both sides
	// or from a single side and there is non-walkable plane from an other side
	int gapSidesNum = 2;
	int hazardSidesNum = 0;

	const auto *aasWorld = AiAasWorld::Instance();
	const auto *routeCache = bot->RouteCache();
	const int currTravelTimeToNavTarget = context->TravelTimeToNavTarget();

	const float sideOffset = playerbox_stand_maxs[1] - playerbox_stand_mins[1];
	Vec3 sidePoints[2] = { Vec3( entityPhysicsState.RightDir() ), -Vec3( entityPhysicsState.RightDir() ) };
	const char *sideNames[] = { "right", "left" };
	edict_t *const ignore = game.edicts + bot->EntNum();
	float *const sideRayStart = const_cast<float *>( entityPhysicsState.Origin() );

	for( int i = 0; i < 2; ++i ) {
		sidePoints[i] *= sideOffset;
		sidePoints[i] += entityPhysicsState.Origin();
		sidePoints[i].Z() += zOffset;

		// We should use G_Trace() and not StaticWorldTrace() as this action
		// requires fine environment sampling and a long application sequence is rare
		G_Trace( &trace, sideRayStart, nullptr, nullptr, sidePoints[i].Data(), ignore, MASK_SOLID | MASK_WATER );
		// Put likely case first
		if( trace.fraction != 1.0f ) {
			// Put likely case first
			if( !( trace.contents & hazardContentsMask ) ) {
				if( ISWALKABLEPLANE( &trace.plane ) ) {
					Debug( "Cannot apply action: there is no gap, wall or hazard to the %s below\n", sideNames[i] );
					// If this block condition held a frame ago, save predicted results
					SwitchOrStop( context, suggestedAction );
					return;
				}
			} else {
				hazardSidesNum++;
			}

			gapSidesNum--;
		} else {
			// Check whether it is really a gap,
			// otherwise bots prefer to walk on fences while they could fall down a bit and move much faster to target
			Vec3 start( sidePoints[i].Data() );
			Vec3 end( start );
			end.Z() -= 64.0f;
			StaticWorldTrace( &trace, start.Data(), end.Data(), MASK_SOLID | MASK_WATER );
			if( trace.fraction == 1.0f ) {
				continue;
			}
			if( trace.contents & hazardContentsMask ) {
				continue;
			}
			if( !ISWALKABLEPLANE( &trace.plane ) ) {
				continue;
			}
			// Check what area is below
			Vec3 pointBelow( trace.plane.normal );
			pointBelow *= 4.0f;
			pointBelow += trace.endpos;
			const int belowAreaNum = aasWorld->FindAreaNum( pointBelow );
			// A "good" area should usually belong to a floor cluster
			if( !aasWorld->FloorClusterNum( belowAreaNum ) ) {
				continue;
			}
			// Check whether a travel time from the area below is not greater than the current travel time
			int travelTimeFromAreaBelow = routeCache->PreferredRouteToGoalArea( belowAreaNum, navTargetAasAreaNum );
			if( travelTimeFromAreaBelow <= currTravelTimeToNavTarget ) {
				gapSidesNum--;
			}
		}
	}

	if( !( hazardSidesNum + gapSidesNum ) ) {
		context->cannotApplyAction = true;
		context->actionSuggestedByAction = suggestedAction;
		Debug( "Cannot apply action: there are just two walls from both sides, no gap or hazard\n" );
		// If this block condition held a frame ago, save predicted results
		SwitchOrStop( context, suggestedAction );
		return;
	}

	context->SetDefaultBotInput();
	context->record->botInput.ClearMovementDirections();
	context->record->botInput.SetForwardMovement( 1 );
	// Be especially careful when there is a nearby hazard area
	if( hazardSidesNum ) {
		context->record->botInput.SetWalkButton( true );
	}
}

void WalkCarefullyAction::CheckPredictionStepResults( Context *context ) {
	BaseMovementAction::CheckPredictionStepResults( context );
	if( context->isCompleted ) {
		return;
	}

	if( context->cannotApplyAction && context->shouldRollback ) {
		Debug( "A prediction step has lead to rolling back, the action will be disabled for planning\n" );
		this->isDisabledForPlanning = true;
		return;
	}

	if( this->SequenceDuration( context ) < 256 ) {
		return;
	}

	Debug( "There is enough predicted ahead millis, should stop planning\n" );
	context->isCompleted = true;
}