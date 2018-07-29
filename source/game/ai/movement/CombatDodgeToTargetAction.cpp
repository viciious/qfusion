#include "CombatDodgeToTargetAction.h"
#include "MovementLocal.h"

void CombatDodgeSemiRandomlyToTargetAction::UpdateKeyMoveDirs( Context *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	auto *combatMovementState = &context->movementState->keyMoveDirsState;
	Assert( combatMovementState->IsActive() );

	int keyMoves[2];
	auto &traceCache = context->TraceCache();
	vec3_t closestFloorPoint;
	Vec3 intendedMoveDir( entityPhysicsState.Origin() );
	bool hasDefinedMoveDir = false;
	if( context->sameFloorClusterAreasCache.GetClosestToTargetPoint( context, closestFloorPoint ) ) {
		intendedMoveDir -= closestFloorPoint;
		hasDefinedMoveDir = true;
	} else if( int nextReachNum = context->NextReachNum() ) {
		const auto &nextReach = AiAasWorld::Instance()->Reachabilities()[nextReachNum];
		Vec3 reachAvgDir( nextReach.start );
		reachAvgDir += nextReach.end;
		reachAvgDir *= 0.5f;
		hasDefinedMoveDir = true;
	}

	if( hasDefinedMoveDir ) {
		// We have swapped the difference start and end points for convenient Vec3 initialization
		intendedMoveDir *= -1.0f;
		intendedMoveDir.NormalizeFast();

		if( ShouldTryRandomness() ) {
			traceCache.MakeRandomizedKeyMovesToTarget( context, intendedMoveDir, keyMoves );
		} else {
			traceCache.MakeKeyMovesToTarget( context, intendedMoveDir, keyMoves );
		}
	} else {
		traceCache.MakeRandomKeyMoves( context, keyMoves );
	}

	unsigned timeout = BotKeyMoveDirsState::TIMEOUT_PERIOD;
	unsigned oneFourth = timeout / 4u;
	// We are assuming that the bot keeps facing the enemy...
	// Less the side component is, lower the timeout should be
	// (so we can switch to an actual side dodge faster).
	// Note: we can't switch directions every frame as it results
	// to average zero spatial shift (ground acceleration is finite).
	if( keyMoves[0] ) {
		timeout -= oneFourth;
	}
	if( !keyMoves[1] ) {
		timeout -= oneFourth;
	}

	combatMovementState->Activate( keyMoves[0], keyMoves[1], timeout );
}

void CombatDodgeSemiRandomlyToTargetAction::PlanPredictionStep( Context *context ) {
	Assert( bot->ShouldKeepXhairOnEnemy() );
	Assert( bot->GetSelectedEnemies().AreValid() );

	auto *botInput = &context->record->botInput;
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	botInput->canOverrideLookVec = true;
	botInput->shouldOverrideLookVec = true;
	botInput->isUcmdSet = true;

	if( attemptNum == maxAttempts ) {
		Debug( "Attempts count has reached its limit. Should stop planning\n" );
		// There is no fallback action since this action is a default one for combat state.
		botInput->SetForwardMovement( 0 );
		botInput->SetRightMovement( 0 );
		botInput->SetUpMovement( bot->IsCombatCrouchingAllowed() ? -1 : +1 );
		context->isCompleted = true;
	}

	Vec3 botToEnemies( bot->GetSelectedEnemies().LastSeenOrigin() );
	botToEnemies -= entityPhysicsState.Origin();

	const short *pmStats = context->currPlayerState->pmove.stats;
	if( entityPhysicsState.GroundEntity() ) {
		if( ShouldTrySpecialMovement() ) {
			if( pmStats[PM_STAT_FEATURES] & PMFEAT_DASH ) {
				const float speedThreshold = context->GetDashSpeed() - 10;
				if( entityPhysicsState.Speed() < speedThreshold ) {
					if( !pmStats[PM_STAT_DASHTIME] ) {
						if( isCombatDashingAllowed ) {
							botInput->SetSpecialButton( true );
							context->predictionStepMillis = context->DefaultFrameTime();
						}
					}
				}
			}
		}
		auto *combatMovementState = &context->movementState->keyMoveDirsState;
		if( !combatMovementState->IsActive() ) {
			UpdateKeyMoveDirs( context );
		}

		botInput->SetForwardMovement( combatMovementState->ForwardMove() );
		botInput->SetRightMovement( combatMovementState->RightMove() );
		// Set at least a single key or button while on ground (forward/right move keys might both be zero)
		if( !botInput->ForwardMovement() && !botInput->RightMovement() && !botInput->UpMovement() ) {
			if( !botInput->IsSpecialButtonSet() ) {
				botInput->SetUpMovement( isCompatCrouchingAllowed ? -1 : +1 );
			}
		}
	} else {
		if( ShouldTrySpecialMovement() ) {
			if( ( pmStats[PM_STAT_FEATURES] & PMFEAT_WALLJUMP ) && !pmStats[PM_STAT_WJTIME] && !pmStats[PM_STAT_STUN] ) {
				botInput->SetSpecialButton( true );
				context->predictionStepMillis = context->DefaultFrameTime();
			}
		}

		const float skill = bot->Skill();
		if( !botInput->IsSpecialButtonSet() && entityPhysicsState.Speed2D() < 650 ) {
			const auto &oldPMove = context->oldPlayerState->pmove;
			const auto &newPMove = context->currPlayerState->pmove;
			// If not skimming
			if( !( newPMove.skim_time && newPMove.skim_time != oldPMove.skim_time ) ) {
				context->CheatingAccelerate( skill > 0.33f ? skill : 0.5f * skill );
			}
		}
	}
}

void CombatDodgeSemiRandomlyToTargetAction::CheckPredictionStepResults( Context *context ) {
	BaseMovementAction::CheckPredictionStepResults( context );
	if( context->cannotApplyAction || context->isCompleted ) {
		return;
	}

	// If there is no nav target, skip nav target reachability tests
	if( this->bestTravelTimeSoFar ) {
		Assert( context->NavTargetAasAreaNum() );
		int newTravelTimeToTarget = context->TravelTimeToNavTarget();
		if( !newTravelTimeToTarget ) {
			Debug( "A prediction step has lead to an undefined travel time to the nav target\n" );
			context->SetPendingRollback();
			return;
		}

		const int currGroundedAreaNum = context->CurrGroundedAasAreaNum();
		if( newTravelTimeToTarget < this->bestTravelTimeSoFar ) {
			this->bestTravelTimeSoFar = newTravelTimeToTarget;
			this->bestFloorClusterSoFar = AiAasWorld::Instance()->FloorClusterNum( currGroundedAreaNum );
		} else if( newTravelTimeToTarget > this->bestTravelTimeSoFar + 50 ) {
			bool rollback = true;
			// If we're still in the best floor cluster, use more lenient increased travel time threshold
			if( AiAasWorld::Instance()->FloorClusterNum( currGroundedAreaNum ) == bestFloorClusterSoFar ) {
				if( newTravelTimeToTarget < this->bestTravelTimeSoFar + 100 ) {
					rollback = false;
				}
			}
			if( rollback ) {
				Debug( "A prediction step has lead to an increased travel time to the nav target\n" );
				context->SetPendingRollback();
				return;
			}
		}
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	// If the bot is on ground and move dirs set at a sequence start were invalidated
	if( entityPhysicsState.GroundEntity() ) {
		// Check for blocking
		if( this->SequenceDuration( context ) > 500 ) {
			if( originAtSequenceStart.SquareDistance2DTo( entityPhysicsState.Origin()) < SQUARE( 24 ) ) {
				Debug( "Total covered distance since sequence start is too low\n" );
				context->SetPendingRollback();
				return;
			}
		}
		context->isCompleted = true;
		return;
	}

	context->SaveSuggestedActionForNextFrame( this );
}

void CombatDodgeSemiRandomlyToTargetAction::OnApplicationSequenceStarted( Context *context ) {
	BaseMovementAction::OnApplicationSequenceStarted( context );

	this->bestTravelTimeSoFar = context->TravelTimeToNavTarget();
	this->bestFloorClusterSoFar = 0;
	if( int clusterNum = AiAasWorld::Instance()->FloorClusterNum( context->CurrGroundedAasAreaNum() ) ) {
		this->bestFloorClusterSoFar = clusterNum;
	}

	this->isCombatDashingAllowed = bot->IsCombatDashingAllowed();
	this->isCompatCrouchingAllowed = bot->IsCombatCrouchingAllowed();
}

void CombatDodgeSemiRandomlyToTargetAction::OnApplicationSequenceStopped( Context *context,
																		  SequenceStopReason stopReason,
																		  unsigned stoppedAtFrameIndex ) {
	BaseMovementAction::OnApplicationSequenceStopped( context, stopReason, stoppedAtFrameIndex );
	if( stopReason != FAILED ) {
		attemptNum = 0;
		return;
	}

	attemptNum++;
	Assert( attemptNum <= maxAttempts );
}

void CombatDodgeSemiRandomlyToTargetAction::BeforePlanning() {
	BaseMovementAction::BeforePlanning();
	attemptNum = 0;
	maxAttempts = bot->Skill() > 0.33f ? 4 : 2;
}