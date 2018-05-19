#include "GenericBunnyingAction.h"
#include "MovementLocal.h"

bool GenericRunBunnyingAction::GenericCheckIsActionEnabled( Context *context, BaseMovementAction *suggestedAction ) {
	if( !BaseMovementAction::GenericCheckIsActionEnabled( context, suggestedAction ) ) {
		return false;
	}

	if( this->disabledForApplicationFrameIndex != context->topOfStackIndex ) {
		return true;
	}

	Debug( "Cannot apply action: the action has been disabled for application on frame %d\n", context->topOfStackIndex );
	context->sequenceStopReason = DISABLED;
	context->cannotApplyAction = true;
	context->actionSuggestedByAction = suggestedAction;
	return false;
}

bool GenericRunBunnyingAction::CheckCommonBunnyingPreconditions( Context *context ) {
	int currAasAreaNum = context->CurrAasAreaNum();
	if( !currAasAreaNum ) {
		Debug( "Cannot apply action: curr AAS area num is undefined\n" );
		context->SetPendingRollback();
		return false;
	}

	int navTargetAasAreaNum = context->NavTargetAasAreaNum();
	if( !navTargetAasAreaNum ) {
		Debug( "Cannot apply action: nav target AAS area num is undefined\n" );
		context->SetPendingRollback();
		return false;
	}

	if( self->ai->botRef->ShouldKeepXhairOnEnemy() ) {
		const auto &selectedEnemies = self->ai->botRef->GetSelectedEnemies();
		if( selectedEnemies.AreValid() && selectedEnemies.ArePotentiallyHittable() ) {
			if( !context->MayHitWhileRunning().CanHit() ) {
				Debug( "Cannot apply action: cannot hit an enemy while keeping the crosshair on it is required\n" );
				context->SetPendingRollback();
				this->isDisabledForPlanning = true;
				return false;
			}
		}
	}

	// Cannot find a next reachability in chain while it should exist
	// (looks like the bot is too high above the ground)
	if( !context->IsInNavTargetArea() && !context->NextReachNum() ) {
		Debug( "Cannot apply action: next reachability is undefined and bot is not in the nav target area\n" );
		context->SetPendingRollback();
		return false;
	}

	if( !( context->currPlayerState->pmove.stats[PM_STAT_FEATURES] & PMFEAT_JUMP ) ) {
		Debug( "Cannot apply action: bot does not have the jump movement feature\n" );
		context->SetPendingRollback();
		this->isDisabledForPlanning = true;
		return false;
	}

	if( self->ai->botRef->ShouldBeSilent() ) {
		Debug( "Cannot apply action: bot should be silent\n" );
		context->SetPendingRollback();
		this->isDisabledForPlanning = true;
		return false;
	}

	return true;
}

void GenericRunBunnyingAction::SetupCommonBunnyingInput( Context *context ) {
	const auto *pmoveStats = context->currPlayerState->pmove.stats;

	auto *botInput = &context->record->botInput;
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	botInput->SetForwardMovement( 1 );
	const auto &hitWhileRunningTestResult = context->MayHitWhileRunning();
	if( self->ai->botRef->ShouldKeepXhairOnEnemy() ) {
		const auto &selectedEnemies = self->ai->botRef->GetSelectedEnemies();
		if( selectedEnemies.AreValid() && selectedEnemies.ArePotentiallyHittable() ) {
			Assert( hitWhileRunningTestResult.CanHit() );
		}
	}

	botInput->canOverrideLookVec = hitWhileRunningTestResult.canHitAsIs;
	botInput->canOverridePitch = true;

	if( ( pmoveStats[PM_STAT_FEATURES] & PMFEAT_DASH ) && !pmoveStats[PM_STAT_DASHTIME] ) {
		bool shouldDash = false;
		if( entityPhysicsState.Speed() < context->GetDashSpeed() && entityPhysicsState.GroundEntity() ) {
			// Prevent dashing into obstacles
			auto &traceCache = context->TraceCache();
			traceCache.TestForResultsMask( context, traceCache.FullHeightMask( traceCache.FRONT ) );
			if( traceCache.FullHeightFrontTrace().trace.fraction == 1.0f ) {
				shouldDash = true;
			}
		}

		if( shouldDash ) {
			botInput->SetSpecialButton( true );
			botInput->SetUpMovement( 0 );
			// Predict dash precisely
			context->predictionStepMillis = 16;
		} else {
			botInput->SetUpMovement( 1 );
		}
	} else {
		if( entityPhysicsState.Speed() < context->GetRunSpeed() ) {
			botInput->SetUpMovement( 0 );
		} else {
			botInput->SetUpMovement( 1 );
		}
	}
}

bool GenericRunBunnyingAction::SetupBunnying( const Vec3 &intendedLookVec, Context *context, float maxAccelDotThreshold ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	auto *botInput = &context->record->botInput;

	Vec3 toTargetDir2D( intendedLookVec );
	toTargetDir2D.Z() = 0;

	Vec3 velocityDir2D( entityPhysicsState.Velocity() );
	velocityDir2D.Z() = 0;

	float squareSpeed2D = entityPhysicsState.SquareSpeed2D();
	float toTargetDir2DSqLen = toTargetDir2D.SquaredLength();

	if( squareSpeed2D > 1.0f ) {
		SetupCommonBunnyingInput( context );

		velocityDir2D *= 1.0f / entityPhysicsState.Speed2D();

		if( toTargetDir2DSqLen > 0.1f ) {
			const auto &oldPMove = context->oldPlayerState->pmove;
			const auto &newPMove = context->currPlayerState->pmove;
			// If not skimming
			if( !( newPMove.skim_time && newPMove.skim_time != oldPMove.skim_time ) ) {
				toTargetDir2D *= Q_RSqrt( toTargetDir2DSqLen );
				float velocityDir2DDotToTargetDir2D = velocityDir2D.Dot( toTargetDir2D );
				if( velocityDir2DDotToTargetDir2D > 0.0f ) {
					// Apply cheating acceleration.
					// maxAccelDotThreshold is usually 1.0f, so the "else" path gets executed.
					// If the maxAccelDotThreshold is lesser than the dot product,
					// a maximal possible acceleration is applied
					// (once the velocity and target dirs match conforming to the specified maxAccelDotThreshold).
					// This allows accelerate even faster if we have an a-priori knowledge that the action is reliable.
					Assert( maxAccelDotThreshold >= 0.0f );
					if( velocityDir2DDotToTargetDir2D >= maxAccelDotThreshold ) {
						context->CheatingAccelerate( 1.0f );
					} else {
						context->CheatingAccelerate( velocityDir2DDotToTargetDir2D );
					}
				}
				if( velocityDir2DDotToTargetDir2D < STRAIGHT_MOVEMENT_DOT_THRESHOLD ) {
					// Correct trajectory using cheating aircontrol
					context->CheatingCorrectVelocity( velocityDir2DDotToTargetDir2D, toTargetDir2D );
				}
			}
		}
	}
	// Looks like the bot is in air falling vertically
	else if( !entityPhysicsState.GroundEntity() ) {
		// Release keys to allow full control over view in air without affecting movement
		if( self->ai->botRef->ShouldAttack() && CanFlyAboveGroundRelaxed( context ) ) {
			botInput->ClearMovementDirections();
			botInput->canOverrideLookVec = true;
		}
		return true;
	} else {
		SetupCommonBunnyingInput( context );
		return true;
	}

	if( self->ai->botRef->ShouldAttack() && CanFlyAboveGroundRelaxed( context ) ) {
		botInput->ClearMovementDirections();
		botInput->canOverrideLookVec = true;
	}

	// Skip dash and WJ near triggers and nav targets to prevent missing a trigger/nav target
	const int nextReachNum = context->NextReachNum();
	if( !nextReachNum ) {
		// Preconditions check must not allow bunnying outside of nav target area having an empty reach. chain
		Assert( context->IsInNavTargetArea() );
		botInput->SetSpecialButton( false );
		botInput->canOverrideLookVec = false;
		botInput->canOverridePitch = false;
		return true;
	}

	switch( AiAasWorld::Instance()->Reachabilities()[nextReachNum].traveltype ) {
		case TRAVEL_TELEPORT:
		case TRAVEL_JUMPPAD:
		case TRAVEL_ELEVATOR:
		case TRAVEL_LADDER:
		case TRAVEL_BARRIERJUMP:
			botInput->SetSpecialButton( false );
			botInput->canOverrideLookVec = false;
			botInput->canOverridePitch = true;
			return true;
		default:
			if( context->IsCloseToNavTarget() ) {
				botInput->SetSpecialButton( false );
				botInput->canOverrideLookVec = false;
				botInput->canOverridePitch = false;
				return true;
			}
	}

	if( ShouldPrepareForCrouchSliding( context, 8.0f ) ) {
		botInput->SetUpMovement( -1 );
		context->predictionStepMillis = 16;
	}

	TrySetWalljump( context );
	return true;
}

bool GenericRunBunnyingAction::CanFlyAboveGroundRelaxed( const Context *context ) const {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( entityPhysicsState.GroundEntity() ) {
		return false;
	}

	float desiredHeightOverGround = 0.3f * AI_JUMPABLE_HEIGHT;
	return entityPhysicsState.HeightOverGround() >= desiredHeightOverGround;
}

void GenericRunBunnyingAction::TrySetWalljump( Context *context ) {
	if( !CanSetWalljump( context ) ) {
		return;
	}

	auto *botInput = &context->record->botInput;
	botInput->ClearMovementDirections();
	botInput->SetSpecialButton( true );
	// Predict a frame precisely for walljumps
	context->predictionStepMillis = 16;
}

#define TEST_TRACE_RESULT_NORMAL( traceResult )                                   \
	do                                                                            \
	{                                                                             \
		if( traceResult.trace.fraction != 1.0f )                                  \
		{                                                                         \
			if( velocity2DDir.Dot( traceResult.trace.plane.normal ) < -0.5f ) {   \
				return false; }                                                   \
			hasGoodWalljumpNormal = true;                                         \
		}                                                                         \
	} while( 0 )

bool GenericRunBunnyingAction::CanSetWalljump( Context *context ) const {
	const short *pmoveStats = context->currPlayerState->pmove.stats;
	if( !( pmoveStats[PM_STAT_FEATURES] & PMFEAT_WALLJUMP ) ) {
		return false;
	}

	if( pmoveStats[PM_STAT_WJTIME] ) {
		return false;
	}

	if( pmoveStats[PM_STAT_STUN] ) {
		return false;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( entityPhysicsState.GroundEntity() ) {
		return false;
	}

	if( entityPhysicsState.HeightOverGround() < 8.0f && entityPhysicsState.Velocity()[2] <= 0 ) {
		return false;
	}

	float speed2D = entityPhysicsState.Speed2D();
	// The 2D speed is too low for walljumping
	if( speed2D < 400 ) {
		return false;
	}

	Vec3 velocity2DDir( entityPhysicsState.Velocity()[0], entityPhysicsState.Velocity()[1], 0 );
	velocity2DDir *= 1.0f / speed2D;

	auto &traceCache = context->TraceCache();
	traceCache.TestForResultsMask( context, traceCache.FullHeightMask( traceCache.FRONT ) );
	const auto &frontResult = traceCache.FullHeightFrontTrace();
	if( velocity2DDir.Dot( frontResult.traceDir ) < 0.7f ) {
		return false;
	}

	bool hasGoodWalljumpNormal = false;
	TEST_TRACE_RESULT_NORMAL( frontResult );

	// Do not force full-height traces for sides to be computed.
	// Walljump height rules are complicated, and full simulation of these rules seems to be excessive.
	// In worst case a potential walljump might be skipped.
	auto sidesMask = traceCache.FULL_SIDES_MASK & ~( traceCache.BACK | traceCache.BACK_LEFT | traceCache.BACK_RIGHT );
	traceCache.TestForResultsMask( context, traceCache.JumpableHeightMask( sidesMask ) );

	TEST_TRACE_RESULT_NORMAL( traceCache.JumpableHeightLeftTrace() );
	TEST_TRACE_RESULT_NORMAL( traceCache.JumpableHeightRightTrace() );
	TEST_TRACE_RESULT_NORMAL( traceCache.JumpableHeightFrontLeftTrace() );
	TEST_TRACE_RESULT_NORMAL( traceCache.JumpableHeightFrontLeftTrace() );

	return hasGoodWalljumpNormal;
}

#undef TEST_TRACE_RESULT_NORMAL

bool GenericRunBunnyingAction::CheckStepSpeedGainOrLoss( Context *context ) {
	const auto *oldPMove = &context->oldPlayerState->pmove;
	const auto *newPMove = &context->currPlayerState->pmove;
	// Make sure this test is skipped along with other ones while skimming
	Assert( !( newPMove->skim_time && newPMove->skim_time != oldPMove->skim_time ) );

	const auto &newEntityPhysicsState = context->movementState->entityPhysicsState;
	const auto &oldEntityPhysicsState = context->PhysicsStateBeforeStep();

	// Test for a huge speed loss in case of hitting of an obstacle
	const float *oldVelocity = oldEntityPhysicsState.Velocity();
	const float *newVelocity = newEntityPhysicsState.Velocity();
	const float oldSquare2DSpeed = oldEntityPhysicsState.SquareSpeed2D();
	const float newSquare2DSpeed = newEntityPhysicsState.SquareSpeed2D();

	if( newSquare2DSpeed < 10 * 10 && oldSquare2DSpeed > 100 * 100 ) {
		// Bumping into walls on high speed in nav target areas is OK:5735
		if( !context->IsInNavTargetArea() && !context->IsCloseToNavTarget() ) {
			Debug( "A prediction step has lead to close to zero 2D speed while it was significant\n" );
			this->shouldTryObstacleAvoidance = true;
			return false;
		}
	}

	// Check for unintended bouncing back (starting from some speed threshold)
	if( oldSquare2DSpeed > 100 * 100 && newSquare2DSpeed > 1 * 1 ) {
		Vec3 oldVelocity2DDir( oldVelocity[0], oldVelocity[1], 0 );
		oldVelocity2DDir *= 1.0f / oldEntityPhysicsState.Speed2D();
		Vec3 newVelocity2DDir( newVelocity[0], newVelocity[1], 0 );
		newVelocity2DDir *= 1.0f / newEntityPhysicsState.Speed2D();
		if( oldVelocity2DDir.Dot( newVelocity2DDir ) < 0.3f ) {
			Debug( "A prediction step has lead to an unintended bouncing back\n" );
			return false;
		}
	}

	// Check for regular speed loss
	const float oldSpeed = oldEntityPhysicsState.Speed();
	const float newSpeed = newEntityPhysicsState.Speed();

	Assert( context->predictionStepMillis );
	float actualSpeedGainPerSecond = ( newSpeed - oldSpeed ) / ( 0.001f * context->predictionStepMillis );
	if( actualSpeedGainPerSecond >= minDesiredSpeedGainPerSecond || context->IsInNavTargetArea() ) {
		// Reset speed loss timer
		currentSpeedLossSequentialMillis = 0;
		return true;
	}

	const char *format = "Actual speed gain per second %.3f is lower than the desired one %.3f\n";
	Debug( "oldSpeed: %.1f, newSpeed: %1.f, speed gain per second: %.1f\n", oldSpeed, newSpeed, actualSpeedGainPerSecond );
	Debug( format, actualSpeedGainPerSecond, minDesiredSpeedGainPerSecond );

	currentSpeedLossSequentialMillis += context->predictionStepMillis;
	if( tolerableSpeedLossSequentialMillis < currentSpeedLossSequentialMillis ) {
		const char *format_ = "A sequential speed loss interval of %d millis exceeds the tolerable one of %d millis\n";
		Debug( format_, currentSpeedLossSequentialMillis, tolerableSpeedLossSequentialMillis );
		this->shouldTryObstacleAvoidance = true;
		return false;
	}

	return true;
}

bool GenericRunBunnyingAction::IsMovingIntoNavEntity( Context *context ) const {
	// Sometimes the camping spot movement action might be not applicable to reach a target.
	// Prevent missing the target when THIS action is likely to be really applied to reach it.
	// We do a coarse prediction of bot path for a second testing its intersection with the target (and no solid world).
	// The following ray-sphere test is very coarse but yields satisfactory results.
	// We might call G_Trace() but it is expensive and would fail if the target is not solid yet.
	const auto &newEntityPhysicsState = context->movementState->entityPhysicsState;
	Vec3 navTargetOrigin( context->NavTargetOrigin() );
	float navTargetRadius = context->NavTargetRadius();
	// If bot is walking on ground, use an ordinary ray-sphere test for a linear movement assumed to the bot
	if( newEntityPhysicsState.GroundEntity() && fabsf( newEntityPhysicsState.Velocity()[2] ) < 1.0f ) {
		Vec3 velocityDir( newEntityPhysicsState.Velocity() );
		velocityDir *= 1.0f / newEntityPhysicsState.Speed();

		Vec3 botToTarget( navTargetOrigin );
		botToTarget -= newEntityPhysicsState.Origin();

		float botToTargetDotVelocityDir = botToTarget.Dot( velocityDir );
		if( botToTarget.SquaredLength() > navTargetRadius * navTargetRadius && botToTargetDotVelocityDir < 0 ) {
			return false;
		}

		// |botToTarget| is the length of the triangle hypotenuse
		// |botToTargetDotVelocityDir| is the length of the adjacent triangle side
		// |distanceVec| is the length of the opposite triangle side
		// (The distanceVec is directed to the target center)

		// distanceVec = botToTarget - botToTargetDotVelocityDir * dir
		Vec3 distanceVec( velocityDir );
		distanceVec *= -botToTargetDotVelocityDir;
		distanceVec += botToTarget;

		return distanceVec.SquaredLength() <= navTargetRadius * navTargetRadius;
	}

	Vec3 botOrigin( newEntityPhysicsState.Origin() );
	Vec3 velocity( newEntityPhysicsState.Velocity() );
	const float gravity = level.gravity;
	constexpr float timeStep = 0.125f;
	for( unsigned stepNum = 0; stepNum < 8; ++stepNum ) {
		velocity.Z() -= gravity * timeStep;
		botOrigin += timeStep * velocity;

		Vec3 botToTarget( navTargetOrigin );
		botToTarget -= botOrigin;

		if( botToTarget.SquaredLength() > navTargetRadius * navTargetRadius ) {
			// The bot has already missed the nav entity (same check as in ray-sphere test)
			if( botToTarget.Dot( velocity ) < 0 ) {
				return false;
			}

			// The bot is still moving in the nav target direction
			continue;
		}

		// The bot is inside the target radius
		return true;
	}

	return false;
}

void GenericRunBunnyingAction::CheckPredictionStepResults( Context *context ) {
	BaseMovementAction::CheckPredictionStepResults( context );
	if( context->cannotApplyAction || context->isCompleted ) {
		return;
	}

	const auto &oldPMove = context->oldPlayerState->pmove;
	const auto &newPMove = context->currPlayerState->pmove;

	// Skip tests while skimming
	if( newPMove.skim_time && newPMove.skim_time != oldPMove.skim_time ) {
		// The only exception is testing covered distance to prevent
		// jumping in front of wall contacting it forever updating skim timer
		if( this->SequenceDuration( context ) > 400 ) {
			if( originAtSequenceStart.SquareDistance2DTo( newPMove.origin ) < SQUARE( 128 ) ) {
				context->SetPendingRollback();
				Debug( "Looks like the bot is stuck and is resetting the skim timer forever by jumping\n" );
				return;
			}
		}

		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	if( !CheckStepSpeedGainOrLoss( context ) ) {
		context->SetPendingRollback();
		return;
	}

	// This entity physics state has been modified after prediction step
	const auto &newEntityPhysicsState = context->movementState->entityPhysicsState;

	bool isInNavTargetArea = context->IsInNavTargetArea();
	if( isInNavTargetArea || context->IsCloseToNavTarget() ) {
		if( !IsMovingIntoNavEntity( context ) ) {
			Debug( "The bot is likely to miss the nav target\n" );
			context->SetPendingRollback();
			return;
		}

		// Skip other checks in this case
		if( isInNavTargetArea ) {
			// Avoid prediction stack overflow in huge areas
			if( newEntityPhysicsState.GroundEntity() && this->SequenceDuration( context ) > 250 ) {
				Debug( "The bot is on ground in the nav target area, moving into the target, there is enough predicted data\n" );
				context->isCompleted = true;
				return;
			}
			// Keep this action as an active bunny action
			context->SaveSuggestedActionForNextFrame( this );
			return;
		}
	}

	int currTravelTimeToNavTarget = context->TravelTimeToNavTarget();
	if( !currTravelTimeToNavTarget ) {
		currentUnreachableTargetSequentialMillis += context->predictionStepMillis;
		// Be very strict in case when bot does another jump after landing.
		// (Prevent falling in a gap immediately after successful landing on a ledge).
		if( currentUnreachableTargetSequentialMillis > tolerableUnreachableTargetSequentialMillis ) {
			context->SetPendingRollback();
			Debug( "A prediction step has lead to undefined travel time to the nav target\n" );
			return;
		}

		context->SaveSuggestedActionForNextFrame( this );
		return;
	}
	// Reset unreachable target timer
	currentUnreachableTargetSequentialMillis = 0;

	// Whether prediction should not be terminated (with a success) on this frame
	bool disallowPredictionTermination = false;

	if( currTravelTimeToNavTarget <= minTravelTimeToNavTargetSoFar ) {
		minTravelTimeToNavTargetSoFar = currTravelTimeToNavTarget;
		minTravelTimeAreaNumSoFar = context->CurrAasAreaNum();
		if( int groundedAreaNum = context->CurrGroundedAasAreaNum() ) {
			minTravelTimeAreaGroundZ = AiAasWorld::Instance()->Areas()[groundedAreaNum].mins[2];
		}
	} else {
		constexpr const char *format = "A prediction step has lead to increased travel time to nav target\n";
		if( currTravelTimeToNavTarget > (int)( minTravelTimeToNavTargetSoFar + tolerableWalkableIncreasedTravelTimeMillis ) ) {
			context->SetPendingRollback();
			Debug( format );
			return;
		}

		if( minTravelTimeAreaGroundZ != std::numeric_limits<float>::max() ) {
			int groundedAreaNum = context->CurrGroundedAasAreaNum();
			if( !groundedAreaNum ) {
				context->SetPendingRollback();
				Debug( format );
				return;
			}
			float currGroundAreaZ = AiAasWorld::Instance()->Areas()[groundedAreaNum].mins[2];
			// Disallow significant difference in height with the min travel time area.
			// If the current area is higher than the min travel time area,
			// use an increased height delta (falling is easier than climbing)
			if( minTravelTimeAreaGroundZ > currGroundAreaZ + 8 || minTravelTimeAreaGroundZ + 48 < currGroundAreaZ ) {
				context->SetPendingRollback();
				Debug( format );
				return;
			}
		}

		if( minTravelTimeAreaNumSoFar ) {
			// Disallow moving into an area if the min travel time area cannot be reached by walking from the area
			int areaNums[2] = { newEntityPhysicsState.CurrAasAreaNum(), newEntityPhysicsState.DroppedToFloorAasAreaNum() };
			bool walkable = false;
			for( int i = 0, end = ( areaNums[0] != areaNums[1] ? 2 : 1 ); i < end; ++i ) {
				int travelFlags = GenericGroundMovementFallback::TRAVEL_FLAGS;
				int toAreaNum = minTravelTimeAreaNumSoFar;
				if( int aasTime = self->ai->botRef->routeCache->TravelTimeToGoalArea( areaNums[i], toAreaNum, travelFlags ) ) {
					// aasTime is in seconds^-2
					if( aasTime * 10 < (int)tolerableWalkableIncreasedTravelTimeMillis ) {
						walkable = true;
						break;
					}
				}
			}
			if( !walkable ) {
				context->SetPendingRollback();
				Debug( format );
				return;
			}
		}

		// Don't terminate on this frame even if other termination conditions match
		// There is a speed loss that is very likely caused by bot bumping into walls/obstacles
		disallowPredictionTermination = true;
	}

	if( originAtSequenceStart.SquareDistanceTo( newEntityPhysicsState.Origin() ) < 64 * 64 ) {
		if( SequenceDuration( context ) < 512 ) {
			context->SaveSuggestedActionForNextFrame( this );
			return;
		}

		// Prevent wasting CPU cycles on further prediction
		Debug( "The bot still has not covered 64 units yet in 512 millis\n" );
		context->SetPendingRollback();
		return;
	}

	if( newEntityPhysicsState.GroundEntity() ) {
		Debug( "The bot has covered 64 units and is on ground, should stop prediction\n" );
		context->isCompleted = true;
		return;
	}

	// Continue prediction in the bot does not move down.
	// Test velocity dir Z, use some negative threshold to avoid wasting CPU cycles
	// on traces that are parallel to the ground.
	if( newEntityPhysicsState.Velocity()[2] / newEntityPhysicsState.Speed() > -0.1f ) {
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	if( newEntityPhysicsState.IsHighAboveGround() ) {
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	if( disallowPredictionTermination ) {
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	Assert( context->NavTargetAasAreaNum() );

	// We might wait for landing, but it produces bad results
	// (rejects many legal moves probably due to prediction stack overflow)
	// The tracing is expensive but we did all possible cutoffs above

	Vec3 predictedOrigin( newEntityPhysicsState.Origin() );
	float predictionSeconds = 0.3f;
	for( int i = 0; i < 3; ++i ) {
		predictedOrigin.Data()[i] += newEntityPhysicsState.Velocity()[i] * predictionSeconds;
	}
	predictedOrigin.Data()[2] -= 0.5f * level.gravity * predictionSeconds * predictionSeconds;

	trace_t trace;
	StaticWorldTrace( &trace, newEntityPhysicsState.Origin(), predictedOrigin.Data(),
					  MASK_WATER | MASK_SOLID, playerbox_stand_mins, playerbox_stand_maxs );
	constexpr auto badContents = CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_NODROP | CONTENTS_DONOTENTER;
	if( trace.fraction == 1.0f || !ISWALKABLEPLANE( &trace.plane ) || ( trace.contents & badContents ) ) {
		// Can't say much. The test is very coarse, continue prediction.
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	Vec3 groundPoint( trace.endpos );
	groundPoint.Z() += 4.0f;
	const auto *aasWorld = AiAasWorld::Instance();
	int groundAreaNum = aasWorld->FindAreaNum( groundPoint );
	if( !groundAreaNum ) {
		// Can't say much. The test is very coarse, continue prediction.
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	const auto &groundAreaSettings = aasWorld->AreaSettings()[groundAreaNum];
	if( !( groundAreaSettings.areaflags & AREA_GROUNDED ) || ( groundAreaSettings.areaflags & ( AREA_DISABLED | AREA_JUNK ) ) ) {
		// Can't say much. The test is very coarse, continue prediction.
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}
	if( ( groundAreaSettings.contents & ( AREACONTENTS_LAVA | AREACONTENTS_SLIME | AREACONTENTS_DONOTENTER ) ) ) {
		// Can't say much. The test is very coarse, continue prediction.
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	const auto *routeCache = self->ai->botRef->routeCache;
	int travelTime = routeCache->PreferredRouteToGoalArea( groundAreaNum, context->NavTargetAasAreaNum() );
	if( travelTime && travelTime <= currTravelTimeToNavTarget ) {
		Debug( "The bot is not very high above the ground and looks like it lands in a \"good\" area\n" );
		context->isCompleted = true;
		return;
	}

	// Can't say much. The test is very coarse, continue prediction
	context->SaveSuggestedActionForNextFrame( this );
	return;
}

void GenericRunBunnyingAction::OnApplicationSequenceStarted( Context *context ) {
	BaseMovementAction::OnApplicationSequenceStarted( context );
	context->MarkSavepoint( this, context->topOfStackIndex );

	minTravelTimeToNavTargetSoFar = std::numeric_limits<int>::max();
	minTravelTimeAreaNumSoFar = 0;
	minTravelTimeAreaGroundZ = std::numeric_limits<float>::max();
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( context->NavTargetAasAreaNum() ) {
		if( int travelTime = context->TravelTimeToNavTarget() ) {
			minTravelTimeToNavTargetSoFar = travelTime;
			if( int groundedAreaNum = context->CurrGroundedAasAreaNum() ) {
				minTravelTimeAreaGroundZ = AiAasWorld::Instance()->Areas()[groundedAreaNum].mins[2];
			}
		}
	}

	originAtSequenceStart.Set( entityPhysicsState.Origin() );

	currentSpeedLossSequentialMillis = 0;
	currentUnreachableTargetSequentialMillis = 0;
}

void GenericRunBunnyingAction::OnApplicationSequenceStopped( Context *context,
															 SequenceStopReason reason,
															 unsigned stoppedAtFrameIndex ) {
	BaseMovementAction::OnApplicationSequenceStopped( context, reason, stoppedAtFrameIndex );

	if( reason != FAILED ) {
		ResetObstacleAvoidanceState();
		if( reason != DISABLED ) {
			this->disabledForApplicationFrameIndex = std::numeric_limits<unsigned>::max();
		}
		return;
	}

	// If the action has been disabled due to prediction stack overflow
	if( this->isDisabledForPlanning ) {
		return;
	}

	if( !supportsObstacleAvoidance ) {
		// However having shouldTryObstacleAvoidance flag is legal (it should be ignored in this case).
		// Make sure THIS method logic (that sets isTryingObstacleAvoidance) works as intended.
		Assert( !isTryingObstacleAvoidance );
		// Disable applying this action after rolling back to the savepoint
		this->disabledForApplicationFrameIndex = context->savepointTopOfStackIndex;
		return;
	}

	if( !isTryingObstacleAvoidance && shouldTryObstacleAvoidance ) {
		// Try using obstacle avoidance after rolling back to the savepoint
		// (We rely on skimming for the first try).
		isTryingObstacleAvoidance = true;
		// Make sure this action will be chosen again after rolling back
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	// Disable applying this action after rolling back to the savepoint
	this->disabledForApplicationFrameIndex = context->savepointTopOfStackIndex;
	this->ResetObstacleAvoidanceState();
}

void GenericRunBunnyingAction::BeforePlanning() {
	BaseMovementAction::BeforePlanning();
	this->disabledForApplicationFrameIndex = std::numeric_limits<unsigned>::max();
	ResetObstacleAvoidanceState();
}