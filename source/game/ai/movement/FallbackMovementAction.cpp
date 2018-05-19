#include "FallbackMovementAction.h"
#include "MovementFallback.h"
#include "MovementLocal.h"
#include "BestJumpableSpotDetector.h"
#include "../combat/TacticalSpotsRegistry.h"
#include "../ai_manager.h"
#include "../ai_trajectory_predictor.h"

TriggerAreaNumsCache triggerAreaNumsCache;

int TriggerAreaNumsCache::GetAreaNum( int entNum ) const {
	int *const areaNumRef = &areaNums[entNum];
	// Put the likely case first
	if( *areaNumRef ) {
		return *areaNumRef;
	}

	// Find an area that has suitable flags matching the trigger type
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const auto *aiManager = AiManager::Instance();

	int desiredAreaContents = ~0;
	const edict_t *ent = game.edicts + entNum;
	if( ent->classname ) {
		if( !Q_stricmp( ent->classname, "trigger_push" ) ) {
			desiredAreaContents = AREACONTENTS_JUMPPAD;
		} else if( !Q_stricmp( ent->classname, "trigger_teleport" ) ) {
			desiredAreaContents = AREACONTENTS_TELEPORTER;
		}
	}

	int boxAreaNums[32];
	int numBoxAreas = aasWorld->BBoxAreas( ent->r.absmin, ent->r.absmax, boxAreaNums, 32 );
	for( int i = 0; i < numBoxAreas; ++i ) {
		int areaNum = boxAreaNums[i];
		if( aasAreaSettings[areaNum].contents & desiredAreaContents ) {
			if( aiManager->IsAreaReachableFromHubAreas( areaNum ) ) {
				*areaNumRef = areaNum;
				break;
			}
		}
	}

	return *areaNumRef;
}

void FallbackMovementAction::PlanPredictionStep( Context *context ) {
	bool handledSpecialMovement = false;
	if( auto *fallback = self->ai->botRef->activeMovementFallback ) {
		fallback->SetupMovement( context );
		handledSpecialMovement = true;
	} else if( context->IsInNavTargetArea() ) {
		SetupNavTargetAreaMovement( context );
		handledSpecialMovement = true;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( entityPhysicsState.GroundEntity() && entityPhysicsState.GetGroundNormalZ() < 0.999f ) {
		if( int groundedAreaNum = context->CurrGroundedAasAreaNum() ) {
			if( AiAasWorld::Instance()->AreaSettings()[groundedAreaNum].areaflags & AREA_INCLINED_FLOOR ) {
				if( TrySetupInclinedFloorMovement( context, groundedAreaNum ) ) {
					handledSpecialMovement = true;
				}
			}
		}
	}

	auto *botInput = &context->record->botInput;
	if( handledSpecialMovement ) {
		botInput->SetAllowedRotationMask( BotInputRotation::NONE );
	} else {
		if( !entityPhysicsState.GroundEntity() ) {
			// Fallback path movement is the last hope action, wait for landing
			SetupLostNavTargetMovement( context );
		} else if( auto *fallback = TryFindMovementFallback( context ) ) {
			self->ai->botRef->activeMovementFallback = fallback;
			fallback->SetupMovement( context );
			handledSpecialMovement = true;
			botInput->SetAllowedRotationMask( BotInputRotation::NONE );
		} else {
			// This often leads to bot blocking and suicide. TODO: Invesigate what else can be done.
			botInput->Clear();
			if( self->ai->botRef->keptInFovPoint.IsActive() ) {
				Vec3 intendedLookVec( self->ai->botRef->keptInFovPoint.Origin() );
				intendedLookVec -= entityPhysicsState.Origin();
				botInput->SetIntendedLookDir( intendedLookVec, false );
			}
		}
	}

	botInput->isUcmdSet = true;
	botInput->canOverrideUcmd = !handledSpecialMovement;
	botInput->canOverrideLookVec = !handledSpecialMovement;
	Debug( "Planning is complete: the action should never be predicted ahead\n" );
	context->isCompleted = true;
}

void FallbackMovementAction::SetupNavTargetAreaMovement( Context *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	auto *botInput = &context->record->botInput;

	Vec3 intendedLookDir( context->NavTargetOrigin() );
	intendedLookDir -= entityPhysicsState.Origin();
	intendedLookDir.NormalizeFast();
	botInput->SetIntendedLookDir( intendedLookDir, true );

	if( entityPhysicsState.GroundEntity() ) {
		botInput->SetForwardMovement( true );
		if( self->ai->botRef->ShouldMoveCarefully() ) {
			botInput->SetWalkButton( true );
		} else if( context->IsCloseToNavTarget() ) {
			botInput->SetWalkButton( true );
		}
	} else {
		// Try applying QW-like aircontrol
		float dotForward = intendedLookDir.Dot( entityPhysicsState.ForwardDir() );
		if( dotForward > 0 ) {
			float dotRight = intendedLookDir.Dot( entityPhysicsState.RightDir() );
			if( dotRight > 0.3f ) {
				botInput->SetRightMovement( +1 );
			} else if( dotRight < -0.3f ) {
				botInput->SetRightMovement( -1 );
			}
		}
	}
}

void FallbackMovementAction::SetupLostNavTargetMovement( Context *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	auto *botInput = &context->record->botInput;

	// Looks like the nav target is lost due to being high above the ground

	// If there is a substantial 2D speed, looks like the bot is jumping over a gap
	if( entityPhysicsState.Speed2D() > context->GetRunSpeed() - 50 ) {
		// Keep looking in the velocity direction
		botInput->SetIntendedLookDir( entityPhysicsState.Velocity(), false );
		return;
	}

	if( !entityPhysicsState.IsHighAboveGround() ) {
		// Keep looking in the velocity direction
		if( entityPhysicsState.SquareSpeed() > 1 ) {
			botInput->SetIntendedLookDir( entityPhysicsState.Velocity(), false );
			return;
		}
	}

	// Keep looking in the current direction
	botInput->SetIntendedLookDir( entityPhysicsState.ForwardDir(), true );
}

MovementFallback *FallbackMovementAction::TryFindMovementFallback( Context *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	// First check for being in lava
	// TODO: Inspect why waterType does not work as intended
	if( entityPhysicsState.waterLevel >= 1 ) {
		const auto *aasAreaSettings = AiAasWorld::Instance()->AreaSettings();
		int currAreaNums[2] = { 0, 0 };
		if( int numCurrAreas = entityPhysicsState.PrepareRoutingStartAreas( currAreaNums ) ) {
			int i = 0;
			// Try check whether there is really lava here
			for( ; i < numCurrAreas; ++i ) {
				if( aasAreaSettings[currAreaNums[i]].contents & ( AREACONTENTS_LAVA | AREACONTENTS_SLIME ) ) {
					break;
				}
			}
			// Start checking for jumping fallback only after that (do not fail with double computations!)
			if( i != numCurrAreas ) {
				if( auto *fallback = TryFindJumpFromLavaFallback( context ) ) {
					return fallback;
				}
			}
		}
	}

	// All the following checks require a valid nav target
	if( !context->NavTargetAasAreaNum() ) {
		if( self->ai->botRef->MillisInBlockedState() > 500 ) {
			if( auto *fallback = TryFindLostNavTargetFallback( context ) ) {
				return fallback;
			}
		}
		return nullptr;
	}

	// Check if the bot is standing on a ramp
	if( entityPhysicsState.GroundEntity() && entityPhysicsState.GetGroundNormalZ() < 0.999f ) {
		if( int groundedAreaNum = context->CurrGroundedAasAreaNum() ) {
			if( AiAasWorld::Instance()->AreaSettings()[groundedAreaNum].areaflags & AREA_INCLINED_FLOOR ) {
				if( auto *fallback = TryFindRampFallback( context, groundedAreaNum ) ) {
					return fallback;
				}
			}
		}
	}

	if( auto *fallback = TryFindAasBasedFallback( context ) ) {
		return fallback;
	}

	// Check for stairs
	if( auto *fallback = TryFindStairsFallback( context ) ) {
		return fallback;
	}

	// It is not unusual to see tiny ramp-like areas to the both sides of stairs.
	// Try using these ramp areas as directions for fallback movement.
	if( auto *fallback = TryFindNearbyRampAreasFallback( context ) ) {
		return fallback;
	}

	if( auto *fallback = TryFindWalkableTriggerFallback( context ) ) {
		return fallback;
	}

	if( auto *fallback = TryNodeBasedFallbacksLeft( context ) ) {
		// Check whether its really a node based fallback
		auto *const nodeBasedFallback = &self->ai->botRef->useWalkableNodeFallback;
		if( fallback == nodeBasedFallback ) {
			const vec3_t &origin = nodeBasedFallback->NodeOrigin();
			const int areaNum = nodeBasedFallback->NodeAreaNum();
			if( auto *jumpFallback = TryShortcutOtherFallbackByJumping( context, origin, areaNum ) ) {
				return jumpFallback;
			}
		}
		return fallback;
	}

	if( auto *fallback = TryFindJumpAdvancingToTargetFallback( context ) ) {
		return fallback;
	}

	return nullptr;
}

MovementFallback *FallbackMovementAction::TryNodeBasedFallbacksLeft( Context *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	const unsigned millisInBlockedState = self->ai->botRef->MillisInBlockedState();
	const bool isBotEasy = self->ai->botRef->Skill() < 0.33f;
	// This is a very conservative condition that however should prevent looping these node-based fallbacks are prone to.
	// Prefer jumping fallbacks that are almost seamless to the bunnying movement.
	// Note: threshold values are significanly lower for easy bots since they almost never use bunnying movement.
	if( millisInBlockedState < ( isBotEasy ? 500 : 1500 ) ) {
		return nullptr;
	}

	// Try using the nav target as a fallback movement target
	Assert( context->NavTargetAasAreaNum() );
	auto *nodeFallback = &self->ai->botRef->useWalkableNodeFallback;
	if( context->NavTargetOrigin().SquareDistanceTo( entityPhysicsState.Origin() ) < SQUARE( 384.0f ) ) {
		Vec3 target( context->NavTargetOrigin() );
		target.Z() += -playerbox_stand_mins[2];
		nodeFallback->Activate( target.Data(), 32.0f, context->NavTargetAasAreaNum() );
		nodeFallback->TryDeactivate( context );
		if( nodeFallback->IsActive() ) {
			return nodeFallback;
		}
	}

	if( millisInBlockedState > ( isBotEasy ? 700 : 2000 ) ) {
		vec3_t areaPoint;
		int areaNum;
		if( context->sameFloorClusterAreasCache.GetClosestToTargetPoint( context, areaPoint, &areaNum ) ) {
			nodeFallback->Activate( areaPoint, 48.0f, areaNum );
			return nodeFallback;
		}

		if( context->navMeshQueryCache.GetClosestToTargetPoint( context, areaPoint ) ) {
			float squareDistance = Distance2DSquared( context->movementState->entityPhysicsState.Origin(), areaPoint );
			if( squareDistance > SQUARE( 8 ) ) {
				areaNum = AiAasWorld::Instance()->FindAreaNum( areaPoint );
				float reachRadius = std::min( 64.0f, SQRTFAST( squareDistance ) );
				nodeFallback->Activate( areaPoint, reachRadius, areaNum );
				return nodeFallback;
			}
		}

		if( millisInBlockedState > ( isBotEasy ? 1250 : 2500 ) ) {
			// Notify the nav target selection code
			self->ai->botRef->OnMovementToNavTargetBlocked();
		}
	}

	return nullptr;
}

MovementFallback *FallbackMovementAction::TryFindAasBasedFallback( Context *context ) {
	const int nextReachNum = context->NextReachNum();
	if( !nextReachNum ) {
		return nullptr;
	}

	const auto &nextReach = AiAasWorld::Instance()->Reachabilities()[nextReachNum];
	const int traveltype = nextReach.traveltype & TRAVELTYPE_MASK;

	if( traveltype == TRAVEL_WALK ) {
		return TryFindWalkReachFallback( context, nextReach );
	}

	if( traveltype == TRAVEL_JUMPPAD || traveltype == TRAVEL_TELEPORT || traveltype == TRAVEL_ELEVATOR ) {
		// Always follow these reachabilities
		auto *fallback = &self->ai->botRef->useWalkableNodeFallback;
		// Note: We have to add several units to the target Z, otherwise a collision test
		// on next frame is very likely to immediately deactivate it
		fallback->Activate( ( Vec3( 0, 0, -playerbox_stand_mins[2] ) + nextReach.start ).Data(), 16.0f );
		return fallback;
	}

	if( traveltype == TRAVEL_WALKOFFLEDGE ) {
		return TryFindWalkOffLedgeReachFallback( context, nextReach );
	}

	if( traveltype == TRAVEL_JUMP || traveltype == TRAVEL_STRAFEJUMP ) {
		return TryFindJumpLikeReachFallback( context, nextReach );
	}

	// The only possible fallback left
	auto *fallback = &self->ai->botRef->jumpOverBarrierFallback;
	if( traveltype == TRAVEL_BARRIERJUMP || traveltype == TRAVEL_WATERJUMP ) {
		fallback->Activate( nextReach.start, nextReach.end );
		return fallback;
	}

	// Disallow WJ attempts for TRAVEL_DOUBLEJUMP reachabilities
	if( traveltype == TRAVEL_DOUBLEJUMP ) {
		fallback->Activate( nextReach.start, nextReach.end, false );
		return fallback;
	}

	return nullptr;
}

bool FallbackMovementAction::TrySetupInclinedFloorMovement( Context *context, int rampAreaNum ) {
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasAreas = aasWorld->Areas();
	const auto *bestAreaNum = TryFindBestInclinedFloorExitArea( context, rampAreaNum );
	if( !bestAreaNum ) {
		return false;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	Vec3 intendedLookDir( aasAreas[*bestAreaNum].center );
	intendedLookDir -= entityPhysicsState.Origin();
	intendedLookDir.NormalizeFast();

	context->record->botInput.SetIntendedLookDir( intendedLookDir, true );
	float dot = intendedLookDir.Dot( entityPhysicsState.ForwardDir() );
	if( dot > 0.5f ) {
		context->record->botInput.SetForwardMovement( 1 );
		if( dot > 0.9f && entityPhysicsState.Speed2D() < context->GetDashSpeed() - 10 ) {
			const auto *stats = context->currPlayerState->pmove.stats;
			if( ( stats[PM_STAT_FEATURES] & PMFEAT_DASH ) && !stats[PM_STAT_DASHTIME] ) {
				context->record->botInput.SetSpecialButton( true );
			}
		}
	}

	return true;
}



