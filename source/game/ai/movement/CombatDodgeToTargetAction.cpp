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

	combatMovementState->Activate( keyMoves[0], keyMoves[1] );
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
		botInput->SetUpMovement( random() > 0.5f ? -1 : +1 );
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
						botInput->SetSpecialButton( true );
						context->predictionStepMillis = 16;
					}
				}
			}
		}
		auto *combatMovementState = &context->movementState->keyMoveDirsState;
		if( combatMovementState->IsActive() ) {
			UpdateKeyMoveDirs( context );
		}

		botInput->SetForwardMovement( combatMovementState->ForwardMove() );
		botInput->SetRightMovement( combatMovementState->RightMove() );
		// Set at least a single key or button while on ground (forward/right move keys might both be zero)
		if( !botInput->ForwardMovement() && !botInput->RightMovement() && !botInput->UpMovement() ) {
			if( !botInput->IsSpecialButtonSet() ) {
				botInput->SetUpMovement( -1 );
			}
		}
	} else {
		if( ShouldTrySpecialMovement() ) {
			if( ( pmStats[PM_STAT_FEATURES] & PMFEAT_WALLJUMP ) && !pmStats[PM_STAT_WJTIME] && !pmStats[PM_STAT_STUN] ) {
				botInput->SetSpecialButton( true );
				context->predictionStepMillis = 16;
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
	if( this->minTravelTimeToTarget ) {
		Assert( context->NavTargetAasAreaNum() );
		int newTravelTimeToTarget = context->TravelTimeToNavTarget();
		if( !newTravelTimeToTarget ) {
			Debug( "A prediction step has lead to an undefined travel time to the nav target\n" );
			context->SetPendingRollback();
			return;
		}

		if( newTravelTimeToTarget < this->minTravelTimeToTarget ) {
			this->minTravelTimeToTarget = newTravelTimeToTarget;
		} else if( newTravelTimeToTarget > this->minTravelTimeToTarget + 50 ) {
			Debug( "A prediction step has lead to a greater travel time to the nav target\n" );
			context->SetPendingRollback();
			return;
		}
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const auto &moveDirsState = context->movementState->keyMoveDirsState;
	// Do not check total distance in case when bot has/had zero move dirs set
	if( !moveDirsState.ForwardMove() && !moveDirsState.RightMove() ) {
		// If the bot is on ground and move dirs set at a sequence start were invalidated
		if( entityPhysicsState.GroundEntity() && !moveDirsState.IsActive() ) {
			context->isCompleted = true;
		} else {
			context->SaveSuggestedActionForNextFrame( this );
		}
		return;
	}

	const float *oldOrigin = context->PhysicsStateBeforeStep().Origin();
	const float *newOrigin = entityPhysicsState.Origin();
	totalCovered2DDistance += SQRTFAST( SQUARE( newOrigin[0] - oldOrigin[0] ) + SQUARE( newOrigin[1] - oldOrigin[1] ) );

	// If the bot is on ground and move dirs set at a sequence start were invalidated
	if( entityPhysicsState.GroundEntity() && !moveDirsState.IsActive() ) {
		// Check for blocking
		if( totalCovered2DDistance < 24 ) {
			Debug( "Total covered distance since sequence start is too low\n" );
			context->SetPendingRollback();
			return;
		}
		context->isCompleted = true;
		return;
	}

	context->SaveSuggestedActionForNextFrame( this );
}

void CombatDodgeSemiRandomlyToTargetAction::OnApplicationSequenceStarted( Context *context ) {
	BaseMovementAction::OnApplicationSequenceStarted( context );

	this->minTravelTimeToTarget = context->TravelTimeToNavTarget();
	this->totalCovered2DDistance = 0.0f;
	// Always reset combat move dirs state to ensure that the action will be predicted for the entire move dirs lifetime
	context->movementState->keyMoveDirsState.Deactivate();
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