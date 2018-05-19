#include "BaseMovementAction.h"
#include "MovementLocal.h"

void SwimMovementAction::PlanPredictionStep( Context *context ) {
	if( !GenericCheckIsActionEnabled( context ) ) {
		return;
	}

	int nextReachNum = context->NextReachNum();
	if( !nextReachNum ) {
		context->cannotApplyAction = true;
		Debug( "Cannot apply action: next reachability is undefined in the given context state\n" );
		return;
	}

	context->SetDefaultBotInput();
	context->record->botInput.canOverrideLookVec = true;
	context->record->botInput.SetForwardMovement( 1 );
	context->TryAvoidFullHeightObstacles( 0.3f );

	const auto &nextReach = AiAasWorld::Instance()->Reachabilities()[nextReachNum];
	if( nextReach.traveltype == TRAVEL_SWIM ) {
		return;
	}

	if( DistanceSquared( nextReach.start, context->movementState->entityPhysicsState.Origin() ) > 24 * 24 ) {
		return;
	}

	// Exit water (might it be above a regular next area? this case is handled by the condition)
	if( nextReach.start[2] < nextReach.end[2] ) {
		context->record->botInput.SetUpMovement( 1 );
	} else {
		context->record->botInput.SetUpMovement( -1 );
	}
}

void SwimMovementAction::CheckPredictionStepResults( Context *context ) {
	BaseMovementAction::CheckPredictionStepResults( context );
	if( context->cannotApplyAction || context->isCompleted ) {
		return;
	}

	const auto &oldPhysicsState = context->PhysicsStateBeforeStep();
	const auto &newPhysicsState = context->movementState->entityPhysicsState;

	Assert( oldPhysicsState.waterLevel > 1 );
	if( newPhysicsState.waterLevel < 2 ) {
		context->isCompleted = true;
		Debug( "A movement step has lead to exiting water, should stop planning\n" );
		return;
	}
}