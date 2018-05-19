#include "BaseMovementAction.h"
#include "MovementLocal.h"

void FlyUntilLandingAction::PlanPredictionStep( Context *context ) {
	if( !GenericCheckIsActionEnabled( context ) ) {
		return;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( entityPhysicsState.GroundEntity() ) {
		context->cannotApplyAction = true;
		context->actionSuggestedByAction = &DummyAction();
		Debug( "A bot has landed on a ground in the given context state\n" );
		return;
	}

	auto *flyUntilLandingMovementState = &context->movementState->flyUntilLandingMovementState;
	if( flyUntilLandingMovementState->CheckForLanding( context ) ) {
		context->cannotApplyAction = true;
		context->actionSuggestedByAction = &LandOnSavedAreasAction();
		Debug( "Bot should perform landing in the given context state\n" );
		return;
	}

	auto *botInput = &context->record->botInput;
	// Relax all keys
	botInput->ClearMovementDirections();
	botInput->isUcmdSet = true;
	// Look at the target (do not keep the angles from the flight beginning,
	// in worst case a bot is unable to turn quickly if the landing site is in opposite direction)
	Vec3 intendedLookVec( flyUntilLandingMovementState->Target() );
	intendedLookVec -= entityPhysicsState.Origin();
	botInput->SetIntendedLookDir( intendedLookVec, false );
	botInput->SetTurnSpeedMultiplier( 1.5f );
	botInput->canOverrideLookVec = true;
	Debug( "Planning is completed (the action should never be predicted ahead\n" );
	context->isCompleted = true;
}
