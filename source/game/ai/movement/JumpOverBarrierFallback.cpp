#include "JumpOverBarrierFallback.h"
#include "MovementLocal.h"

bool JumpOverBarrierFallback::TryDeactivate( Context *context ) {
	assert( status == PENDING );

	if( level.time - activatedAt > 750 ) {
		status = INVALID;
		return true;
	}

	// TODO: Eliminate this boilerplate
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

	return entityPhysicsState->Origin()[2] >= top[2];
}

void JumpOverBarrierFallback::SetupMovement( Context *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	auto *botInput = &context->record->botInput;

	// View Z really matters a lot in this case, don't use the entity origin as-is.
	// Don't forget to negate the vector after target subtraction.
	Vec3 intendedLookDir( entityPhysicsState.Origin() );
	intendedLookDir.Z() += game.edicts[bot->EntNum()].viewheight;

	if( !hasReachedStart ) {
		float squareDistance = Distance2DSquared( start, entityPhysicsState.Origin() );
		if( squareDistance > SQUARE( 16.0f ) ) {
			intendedLookDir -= start;
			intendedLookDir *= -1.0f / intendedLookDir.LengthFast();

			botInput->SetIntendedLookDir( intendedLookDir, true );

			const float viewDot = intendedLookDir.Dot( entityPhysicsState.ForwardDir() );
			if( viewDot < 0.9f ) {
				botInput->SetTurnSpeedMultiplier( viewDot < 0 ? 10.0f : 5.0f );
				return;
			}

			botInput->SetForwardMovement( 1 );

			// Try dashing in case when the distance is significant (this should be a rare case)
			if( bot->ShouldMoveCarefully() || bot->ShouldBeSilent() ) {
				return;
			}
			// Note that the distance threshold is lower than usual for fallbacks,
			// since we're going to be stopped by a barrier anyway and shouldn't miss it
			if( !entityPhysicsState.GroundEntity() || squareDistance < SQUARE( 48.0f ) ) {
				return;
			}

			const auto *pmStats = context->currPlayerState->pmove.stats;
			if( ( pmStats[PM_STAT_FEATURES] & PMFEAT_DASH ) && !pmStats[PM_STAT_DASHTIME] ) {
				botInput->SetSpecialButton( true );
			}

			return;
		}
		hasReachedStart = true;
	}

	intendedLookDir -= top;
	intendedLookDir *= -1.0f / intendedLookDir.LengthFast();

	botInput->SetIntendedLookDir( intendedLookDir, true );

	const float viewDot = intendedLookDir.Dot( entityPhysicsState.ForwardDir() );
	if( viewDot < 0.9f ) {
		botInput->SetTurnSpeedMultiplier( viewDot < 0 ? 10.0f : 5.0f );
		return;
	}

	botInput->SetForwardMovement( 1 );
	botInput->SetUpMovement( 1 );

	if( !allowWalljumping ) {
		return;
	}

	// Try WJ having reached the peak point
	const auto *pmStats = context->currPlayerState->pmove.stats;
	if( !( pmStats[PM_STAT_FEATURES] & PMFEAT_WALLJUMP ) ) {
		return;
	}

	if( pmStats[PM_STAT_WJTIME] || pmStats[PM_STAT_STUN] ) {
		return;
	}

	if( !entityPhysicsState.GroundEntity() && fabsf( entityPhysicsState.Velocity()[2] ) < 50 ) {
		botInput->SetSpecialButton( true );
	}
}