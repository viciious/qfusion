#include "MovementState.h"
#include "MovementLocal.h"

bool BotAerialMovementState::ShouldDeactivate( const edict_t *self, const Context *context ) const {
	const edict_t *groundEntity;
	if( context ) {
		groundEntity = context->movementState->entityPhysicsState.GroundEntity();
	} else {
		groundEntity = self->groundentity;
	}

	if( !groundEntity ) {
		return false;
	}

	// TODO: Discover why general solid checks fail for world entity
	if( groundEntity == world ) {
		return true;
	}

	if( groundEntity->s.solid == SOLID_YES || groundEntity->s.solid == SOLID_BMODEL ) {
		return true;
	}

	return false;
}

bool BotFlyUntilLandingMovementState::CheckForLanding( const Context *context ) {
	if( isLanding ) {
		return true;
	}

	const float *botOrigin = context->movementState->entityPhysicsState.Origin();

	// Put the likely case first
	if( !this->usesDistanceThreshold ) {
		if( target[2] < botOrigin[2] ) {
			isLanding = true;
			return true;
		}
		return false;
	}

	const float threshold = this->landingDistanceThreshold;
	Vec3 unpackedTarget( GetUnpacked4uVec( target ) );
	if( unpackedTarget.SquareDistanceTo( botOrigin ) > threshold * threshold ) {
		return false;
	}

	isLanding = true;
	return true;
}

void BotWeaponJumpMovementState::TryDeactivate( const edict_t *self, const Context *context ) {
	// If a bot has activated a trigger, give its movement state a priority
	if( level.time - self->ai->botRef->LastTriggerTouchTime() < 64 ) {
		Deactivate();
	}

	if( !( hasTriggeredWeaponJump && hasCorrectedWeaponJump ) ) {
		return;
	}

	if( context ) {
		if( context->movementState->entityPhysicsState.GroundEntity() ) {
			Deactivate();
		}
		return;
	}

	if( self->groundentity ) {
		Deactivate();
	}
}

void BotCampingSpotState::TryDeactivate( const edict_t *self, const Context *context ) {
	const float *botOrigin = context ? context->movementState->entityPhysicsState.Origin() : self->s.origin;
	const float distanceThreshold = 1.5f * campingSpot.Radius();
	if( this->Origin().SquareDistance2DTo( botOrigin ) > distanceThreshold * distanceThreshold ) {
		Deactivate();
	}
}

AiPendingLookAtPoint BotCampingSpotState::GetOrUpdateRandomLookAtPoint() const {
	float turnSpeedMultiplier = 0.75f + 1.0f * campingSpot.Alertness();
	if( campingSpot.hasLookAtPoint ) {
		return AiPendingLookAtPoint( campingSpot.LookAtPoint(), turnSpeedMultiplier );
	}
	if( lookAtPointTimeLeft ) {
		return AiPendingLookAtPoint( campingSpot.LookAtPoint(), turnSpeedMultiplier );
	}

	Vec3 lookAtPoint( random(), random(), random() );
	lookAtPoint.NormalizeFast();
	lookAtPoint += campingSpot.Origin();
	campingSpot.SetLookAtPoint( lookAtPoint );
	this->lookAtPointTimeLeft = ( decltype( this->lookAtPointTimeLeft ) )LookAtPointTimeout();
	return AiPendingLookAtPoint( lookAtPoint, turnSpeedMultiplier );
}

#define CHECK_STATE_FLAG( state, bit )                                                                 \
	if( ( expectedStatesMask & ( 1 << bit ) ) != ( ( (unsigned)state.IsActive() ) << ( 1 << bit ) ) )  \
	{                                                                                                  \
		result = false;                                                                                \
		if( logFunc ) {                                                                                \
			const edict_t *owner = game.edicts + bot->EntNum();                                        \
			logFunc( format, Nick( owner ), #state ".IsActive()", (unsigned)state.IsActive() );        \
		}     																						   \
	}

bool BotMovementState::TestActualStatesForExpectedMask( unsigned expectedStatesMask, const Bot *bot ) const {
	// Might be set to null if verbose logging is not needed
#ifdef ENABLE_MOVEMENT_DEBUG_OUTPUT
	void ( *logFunc )( const char *format, ... ) = G_Printf;
#elif defined( CHECK_INFINITE_NEXT_STEP_LOOPS )
	void ( *logFunc )( const char *format, ... );
	// Suppress output if the iterations counter is within a feasible range
	if( ::nextStepIterationsCounter < NEXT_STEP_INFINITE_LOOP_THRESHOLD ) {
		logFunc = nullptr;
	} else {
		logFunc = G_Printf;
	}
#else
	void ( *logFunc )( const char *format, ... ) = nullptr;
#endif
	constexpr const char *format = "BotMovementState(%s): %s %d has mismatch with the mask value\n";

	bool result = true;
	CHECK_STATE_FLAG( jumppadMovementState, 0 );
	CHECK_STATE_FLAG( weaponJumpMovementState, 1 );
	CHECK_STATE_FLAG( pendingLookAtPointState, 2 );
	CHECK_STATE_FLAG( campingSpotState, 3 );
	// Skip keyMoveDirsState.
	// It either should not affect movement at all if regular movement is chosen,
	// or should be handled solely by the combat movement code.
	CHECK_STATE_FLAG( flyUntilLandingMovementState, 4 );
	return result;
}