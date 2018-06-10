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

	if( bot->ShouldKeepXhairOnEnemy() ) {
		const auto &selectedEnemies = bot->GetSelectedEnemies();
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

	if( bot->ShouldBeSilent() ) {
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
	if( bot->ShouldKeepXhairOnEnemy() ) {
		const auto &selectedEnemies = bot->GetSelectedEnemies();
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
				// Do not apply correction if this dot product is negative (looks like hovering in air and does not help)
				if( velocityDir2DDotToTargetDir2D && velocityDir2DDotToTargetDir2D < STRAIGHT_MOVEMENT_DOT_THRESHOLD ) {
					context->CheatingCorrectVelocity( velocityDir2DDotToTargetDir2D, toTargetDir2D );
				}
			}
		}
	}
	// Looks like the bot is in air falling vertically
	else if( !entityPhysicsState.GroundEntity() ) {
		// Release keys to allow full control over view in air without affecting movement
		if( bot->ShouldAttack() && CanFlyAboveGroundRelaxed( context ) ) {
			botInput->ClearMovementDirections();
			botInput->canOverrideLookVec = true;
		}
		return true;
	} else {
		SetupCommonBunnyingInput( context );
		return true;
	}

	if( bot->ShouldAttack() && CanFlyAboveGroundRelaxed( context ) ) {
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

	// Ignore bumping into a wall/speed loss if it happens far from a marked for path truncation origin
	// as there will be later planning next frames and the path will be for sure corrected
	if ( mayStopAtAreaNum && DistanceSquared( mayStopAtOrigin, newPMove->origin ) > SQUARE( 56 ) ) {
		return true;
	}

	// Avoid bumping into walls
	if( newSquare2DSpeed < 10 * 10 && oldSquare2DSpeed > 100 * 100 ) {
		Debug( "A prediction step has lead to close to zero 2D speed while it was significant\n" );
		this->shouldTryObstacleAvoidance = true;
		return false;
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

inline void GenericRunBunnyingAction::MarkForTruncation( Context *context ) {
	int currGroundedAreaNum = context->CurrGroundedAasAreaNum();
	Assert( currGroundedAreaNum );
	mayStopAtAreaNum = currGroundedAreaNum;

	int travelTimeToTarget = context->TravelTimeToNavTarget();
	Assert( travelTimeToTarget );
	mayStopAtTravelTime = travelTimeToTarget;

	mayStopAtStackFrame = (int)context->topOfStackIndex;
	VectorCopy( context->movementState->entityPhysicsState.Origin(), mayStopAtOrigin );
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

	const bool isInNavTargetArea = context->IsInNavTargetArea();
	if( isInNavTargetArea ) {
		hasEnteredNavTargetArea = true;
		if( HasTouchedNavEntityThisFrame( context ) ) {
			hasTouchedNavTarget = true;
			// If there is no truncation frame set yet, we this frame is feasible to mark as one
			if( !mayStopAtAreaNum ) {
				mayStopAtAreaNum = context->NavTargetAasAreaNum();
				mayStopAtStackFrame = (int)context->topOfStackIndex;
				mayStopAtTravelTime = 1;
			}
		}
		if( !hasTouchedNavTarget ) {
			Vec3 toTargetDir( context->NavTargetOrigin() );
			toTargetDir -= newEntityPhysicsState.Origin();
			toTargetDir.NormalizeFast();
			Vec3 velocityDir( newEntityPhysicsState.Velocity() );
			velocityDir *= 1.0f / newEntityPhysicsState.Speed();
			if( velocityDir.Dot( toTargetDir ) < 0.7f ) {
				Debug( "The bot is very likely going to miss the nav target\n" );
				context->SetPendingRollback();
				return;
			}
		}
	} else {
		if( hasEnteredNavTargetArea ) {
			// The bot has left the nav target area
			if( !hasTouchedNavTarget ) {
				Debug( "The bot has left the nav target area without touching the nav target\n" );
				context->SetPendingRollback();
				return;
			}
			// Otherwise just save the action for next frame.
			// We do not want to fall in a gap after picking a nav target.
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

	const auto *aasWorld = AiAasWorld::Instance();
	const float squareDistanceFromStart = originAtSequenceStart.SquareDistanceTo( newEntityPhysicsState.Origin() );

	const int groundedAreaNum = context->CurrGroundedAasAreaNum();

	if( currTravelTimeToNavTarget <= minTravelTimeToNavTargetSoFar ) {
		minTravelTimeToNavTargetSoFar = currTravelTimeToNavTarget;
		minTravelTimeAreaNumSoFar = context->CurrAasAreaNum();

		if( groundedAreaNum ) {
			// This travel time advantage restriction is very lenient!
			if( travelTimeAtSequenceStart && travelTimeAtSequenceStart > 1 + currTravelTimeToNavTarget ) {
				if( squareDistanceFromStart > SQUARE( 64 ) ) {
					if( newEntityPhysicsState.Velocity()[2] / newEntityPhysicsState.Speed() < -0.1f ) {
						MarkForTruncation( context );
					} else if( newEntityPhysicsState.GroundEntity() || context->frameEvents.hasJumped ) {
						MarkForTruncation( context );
					}
				}
			}
		}
	} else {
		constexpr const char *format = "A prediction step has lead to increased travel time to nav target\n";
		if( currTravelTimeToNavTarget > (int)( minTravelTimeToNavTargetSoFar + tolerableWalkableIncreasedTravelTimeMillis ) ) {
			context->SetPendingRollback();
			Debug( format );
			return;
		}

		if( groundedAreaNum ) {
			if( minTravelTimeAreaNumSoFar ) {
				bool walkable = false;
				if( const int clusterNum = aasWorld->FloorClusterNum( minTravelTimeAreaNumSoFar ) ) {
					if( clusterNum == aasWorld->FloorClusterNum( groundedAreaNum ) ) {
						walkable = true;
					}
				}

				if( !walkable ) {
					// Disallow moving into an area if the min travel time area cannot be reached by walking from the area
					int areaNums[2];
					const int numAreas = newEntityPhysicsState.PrepareRoutingStartAreas( areaNums );
					for( int i = 0; i < numAreas; ++i ) {
						int flags = GenericGroundMovementFallback::TRAVEL_FLAGS;
						int toAreaNum = minTravelTimeAreaNumSoFar;
						if( int aasTime = bot->RouteCache()->TravelTimeToGoalArea( areaNums[i], toAreaNum, flags ) ) {
							// aasTime is in seconds^-2
							if( aasTime * 10 < (int) tolerableWalkableIncreasedTravelTimeMillis ) {
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
			}
		}
	}

	if( squareDistanceFromStart < SQUARE( 80 ) ) {
		if( SequenceDuration( context ) < 512 ) {
			context->SaveSuggestedActionForNextFrame( this );
			return;
		}

		// Prevent wasting CPU cycles on further prediction
		Debug( "The bot still has not covered 80 units yet in 512 millis\n" );
		context->SetPendingRollback();
		return;
	}

	if( groundedAreaNum ) {
		auto iter = std::find( checkStopAtAreaNums.begin(), checkStopAtAreaNums.end(), groundedAreaNum );
		if( iter != checkStopAtAreaNums.end() ) {
			// We can interrupt here as we have reached a desired area that should be feasible by a-priori tests.
			// This is not 100% confident but is producing fairly good results,
			// otherwise an application sequence rarely succeeds at all
			context->isCompleted = true;
			return;
		}
	}

	// Consider that the bot has touched a ground if the bot is on ground
	// or has jumped (again) this frame (its uneasy to catch being on ground here)
	// If the bot has not touched ground this frame...
	if( !newEntityPhysicsState.GroundEntity() && !context->frameEvents.hasJumped ) {
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	// Do not stop prediction if the bot has
	if( !mayStopAtAreaNum && newEntityPhysicsState.Speed2D() < context->GetRunSpeed() ) {
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	// If we're at the best reached position currently
	if( currTravelTimeToNavTarget == minTravelTimeToNavTargetSoFar ) {
		// Chop the last frame to prevent jumping if the predicted path will be fully utilized
		if( context->frameEvents.hasJumped && context->topOfStackIndex ) {
			context->StopTruncatingStackAt( context->topOfStackIndex - 1 );
		} else {
			context->isCompleted = true;
		}
		return;
	}

	// If we have reached here, we are sure we have not:
	// 1) Landed in a "bad" area (BaseMovementAction::CheckPredictionStepResults())
	// 2) Lost a speed significantly, have bumped into wall or bounced back (CheckStepSpeedGainOrLoss())
	// 3) Has deviated significantly from the "best" path/falled down

	// If there were no area (and consequently, frame) marked as suitable for path truncation
	if( !mayStopAtAreaNum ) {
		if( squareDistanceFromStart > SQUARE( 144 ) ) {
			Debug( "The action still have not managed to mark \"may stop\" area, do not waste cycles anymore\n" );
			context->SetPendingRollback();
			return;
		}
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	// Consider an attempt successful if we've landed in the same floor cluster and there is no gap to the best position
	if( const int clusterNum = aasWorld->FloorClusterNum( mayStopAtAreaNum ) ) {
		if( clusterNum == aasWorld->FloorClusterNum( groundedAreaNum ) ) {
			if( IsAreaWalkableInFloorCluster( groundedAreaNum, mayStopAtStackFrame ) ) {
				context->StopTruncatingStackAt( (unsigned)mayStopAtStackFrame );
				return;
			}
		}
	}

	// Note: we have tried all possible cutoffs before this expensive part
	// Do an additional raycast from the best to the current origin.
	trace_t trace;
	SolidWorldTrace( &trace, newEntityPhysicsState.Origin(), mayStopAtOrigin, vec3_origin, playerbox_stand_maxs );
	if( trace.fraction != 1.0f ) {
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	// There still might be a gap between current and best position.
	// Unforturnately there is no cheap way to test it
	context->StopTruncatingStackAt( (unsigned)mayStopAtStackFrame );
}

void GenericRunBunnyingAction::OnApplicationSequenceStarted( Context *context ) {
	BaseMovementAction::OnApplicationSequenceStarted( context );
	context->MarkSavepoint( this, context->topOfStackIndex );

	minTravelTimeToNavTargetSoFar = std::numeric_limits<int>::max();
	minTravelTimeAreaNumSoFar = 0;

	checkStopAtAreaNums.clear();

	mayStopAtAreaNum = 0;
	mayStopAtStackFrame = -1;
	mayStopAtTravelTime = 0;

	if( context->NavTargetAasAreaNum() ) {
		if( int travelTime = context->TravelTimeToNavTarget() ) {
			minTravelTimeToNavTargetSoFar = travelTime;
			travelTimeAtSequenceStart = travelTime;
		}
	}

	originAtSequenceStart.Set( context->movementState->entityPhysicsState.Origin() );

	currentSpeedLossSequentialMillis = 0;
	currentUnreachableTargetSequentialMillis = 0;

	hasEnteredNavTargetArea = false;
	hasTouchedNavTarget = false;
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