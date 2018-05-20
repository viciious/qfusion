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
		context->cannotApplyAction = true;
		context->actionSuggestedByAction = suggestedAction;
		Debug( "Cannot apply action: the bot is quite high above the ground\n" );
		return;
	}

	const int currAasAreaNum = context->CurrAasAreaNum();
	if( !currAasAreaNum ) {
		context->cannotApplyAction = true;
		context->actionSuggestedByAction = &DummyAction();
		Debug( "Cannot apply action: current AAS area num is undefined\n" );
		return;
	}

	const int navTargetAasAreaNum = context->NavTargetAasAreaNum();
	if( !navTargetAasAreaNum ) {
		context->cannotApplyAction = true;
		context->actionSuggestedByAction = &DummyAction();
		Debug( "Cannot apply action: nav target AAS area num is undefined\n" );
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

	// First test whether there is a gap in front of the bot
	// (if this test is omitted, bots would use this no-jumping action instead of jumping over gaps and fall down)

	const float zOffset = playerbox_stand_mins[2] - 32.0f - entityPhysicsState.HeightOverGround();
	Vec3 frontTestPoint( entityPhysicsState.ForwardDir() );
	frontTestPoint *= 8.0f;
	frontTestPoint += entityPhysicsState.Origin();
	frontTestPoint.Z() += zOffset;

	trace_t trace;
	StaticWorldTrace( &trace, entityPhysicsState.Origin(), frontTestPoint.Data(), MASK_SOLID | MASK_WATER );
	if( trace.fraction == 1.0f ) {
		context->cannotApplyAction = true;
		context->actionSuggestedByAction = suggestedAction;
		return;
	}

	int hazardContentsMask = CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_DONOTENTER;
	// Prevent touching a water if not bot is not already walking in it
	if( !entityPhysicsState.waterLevel ) {
		hazardContentsMask |= CONTENTS_WATER;
	}

	// An action might be applied if there are gap or hazard from both sides
	// or from a single side and there is non-walkable plane from an other side
	int gapSidesNum = 2;
	int hazardSidesNum = 0;

	const float sideOffset = playerbox_stand_maxs[1] - playerbox_stand_mins[1];
	Vec3 sidePoints[2] = { Vec3( entityPhysicsState.RightDir() ), -Vec3( entityPhysicsState.RightDir() ) };
	for( int i = 0; i < 2; ++i ) {
		sidePoints[i] *= sideOffset;
		sidePoints[i] += entityPhysicsState.Origin();
		sidePoints[i].Z() += zOffset;
		StaticWorldTrace( &trace, entityPhysicsState.Origin(), sidePoints[i].Data(), MASK_SOLID | MASK_WATER );
		// Put likely case first
		if( trace.fraction != 1.0f ) {
			// Put likely case first
			if( !( trace.contents & hazardContentsMask ) ) {
				if( ISWALKABLEPLANE( &trace.plane ) ) {
					context->cannotApplyAction = true;
					context->actionSuggestedByAction = suggestedAction;
					Debug( "Cannot apply action: there is no gap, wall or hazard to the right below\n" );
					return;
				}
			} else {
				hazardSidesNum++;
			}

			gapSidesNum--;
		}
	}

	if( !( hazardSidesNum + gapSidesNum ) ) {
		context->cannotApplyAction = true;
		context->actionSuggestedByAction = suggestedAction;
		Debug( "Cannot apply action: there are just two walls from both sides, no gap or hazard\n" );
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
}