#include "FallDownFallback.h"
#include "MovementLocal.h"

bool FallDownFallback::TryDeactivate( Context *context ) {
	assert( status == PENDING );

	if( level.time - activatedAt > timeout ) {
		status = INVALID;
		return true;
	}

	const AiEntityPhysicsState *entityPhysicsState;
	if( context ) {
		entityPhysicsState = &context->movementState->entityPhysicsState;
	} else {
		entityPhysicsState = bot->EntityPhysicsState();
	}

	// Wait for touching any ground
	if( !entityPhysicsState->GroundEntity() ) {
		return false;
	}

	if( DistanceSquared( entityPhysicsState->Origin(), targetOrigin ) > reachRadius * reachRadius ) {
		return false;
	}

	return entityPhysicsState->Origin()[2] < targetOrigin[2];
}

void FallDownFallback::SetupMovement( Context *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	auto *botInput = &context->record->botInput;

	// Start Z is rather important, don't use entity origin as-is
	Vec3 intendedLookDir( entityPhysicsState.Origin() );
	intendedLookDir.Z() += game.edicts[bot->EntNum()].viewheight;
	intendedLookDir -= targetOrigin;
	intendedLookDir *= -1.0f;
	intendedLookDir.Normalize();

	botInput->SetIntendedLookDir( intendedLookDir, true );

	const float viewDot = intendedLookDir.Dot( entityPhysicsState.ForwardDir() );
	if( viewDot < 0 ) {
		botInput->SetTurnSpeedMultiplier( 10.0f );
	} else if( viewDot < 0.9f ) {
		if( viewDot < 0.7f ) {
			botInput->SetWalkButton( true );
			botInput->SetTurnSpeedMultiplier( 5.0f );
		} else {
			// Apply air-control being in air, so turn rather slowly.
			// We might consider using CheatingCorrectVelocity()
			// but it currently produces weird results on vertical trajectories.
			if( !entityPhysicsState.GroundEntity() ) {
				botInput->SetForwardMovement( 1 );
			} else {
				botInput->SetTurnSpeedMultiplier( 3.0f );
			}
		}
	} else {
		botInput->SetForwardMovement( 1 );
	}
}

MovementFallback *FallbackMovementAction::TryFindWalkOffLedgeReachFallback( Context *context,
																			const aas_reachability_t &nextReach ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	// If the falling distance is really low, treat is just as walking to a node
	const float squareFallingHeight = DistanceSquared( nextReach.start, nextReach.end );
	if( squareFallingHeight < SQUARE( 40.0f ) ) {
		auto *fallback = &module->useWalkableNodeFallback;
		float squareDistance = DistanceSquared( entityPhysicsState.Origin(), nextReach.start );
		unsigned timeout = 100 + (unsigned)( 1000.0f * sqrtf( squareDistance ) / context->GetRunSpeed() );
		Vec3 target( nextReach.start );
		target.Z() += 1.0f - playerbox_stand_mins[2];
		fallback->Activate( target.Data(), 16.0f, nextReach.areanum, timeout );
		return fallback;
	}

	auto *fallback = &module->fallDownFallback;
	Vec3 targetOrigin( nextReach.end );
	// Setting the proper Z (should be greater than an origin of bot standing at destination) is important!
	targetOrigin.Z() = AiAasWorld::Instance()->Areas()[nextReach.areanum].mins[2] + 4.0f - playerbox_stand_mins[2];
	// Compute the proper timeout
	float distanceToReach = sqrtf( DistanceSquared( entityPhysicsState.Origin(), nextReach.start ) );
	unsigned travelTimeToLedgeMillis = (unsigned)( 1000.0f * distanceToReach / context->GetRunSpeed() );
	unsigned fallingTimeMillis = (unsigned)( 1000.0f * sqrtf( 2.0f * sqrtf( squareFallingHeight ) / level.gravity ) );
	fallback->Activate( targetOrigin.Data(), travelTimeToLedgeMillis + fallingTimeMillis + 250, 24.0f );
	return fallback;
}
