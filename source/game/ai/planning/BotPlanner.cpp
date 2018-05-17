#include "../bot.h"
#include "../ai_ground_trace_cache.h"
#include "../ai_squad_based_team.h"
#include "BotPlanner.h"
#include "../combat/TacticalSpotsRegistry.h"
#include <algorithm>
#include <limits>
#include <stdarg.h>

BotPlanner::BotPlanner( Bot *bot, float skillLevel_ )
	: BasePlanner( bot->self ), cachedWorldState( bot->self ) {}

BotBaseGoal *BotPlanner::GetGoalByName( const char *name ) {
	for( unsigned i = 0; i < scriptGoals.size(); ++i ) {
		if( !Q_stricmp( name, scriptGoals[i].Name() ) ) {
			return &scriptGoals[i];
		}
	}

	return nullptr;
}

BotBaseAction *BotPlanner::GetActionByName( const char *name ) {
	for( unsigned i = 0; i < scriptActions.size(); ++i ) {
		if( !Q_stricmp( name, scriptActions[i].Name() ) ) {
			return &scriptActions[i];
		}
	}

	return nullptr;
}

bool BotPlanner::FindDodgeDangerSpot( const Danger &danger, vec3_t spotOrigin ) {
	float radius = 128.0f + 192.0f * self->ai->botRef->Skill();
	TacticalSpotsRegistry::OriginParams originParams( self, radius, self->ai->botRef->routeCache );
	TacticalSpotsRegistry::DodgeDangerProblemParams problemParams( danger.hitPoint, danger.direction, danger.IsSplashLike() );
	problemParams.SetCheckToAndBackReachability( false );
	problemParams.SetMinHeightAdvantageOverOrigin( -64.0f );
	// Influence values are quite low because evade direction factor must be primary
	problemParams.SetHeightOverOriginInfluence( 0.2f );
	problemParams.SetMaxFeasibleTravelTimeMillis( 2500 );
	problemParams.SetOriginDistanceInfluence( 0.4f );
	problemParams.SetOriginWeightFalloffDistanceRatio( 0.9f );
	problemParams.SetTravelTimeInfluence( 0.2f );
	return TacticalSpotsRegistry::Instance()->FindEvadeDangerSpots( originParams, problemParams, (vec3_t *)spotOrigin, 1 ) > 0;
}

void BotPlanner::PrepareCurrWorldState( WorldState *worldState ) {
	worldState->SetIgnoreAll( false );

	worldState->BotOriginVar().SetValue( self->s.origin );
	worldState->PendingOriginVar().SetIgnore( true );

	const auto &selectedEnemies = self->ai->botRef->GetSelectedEnemies();

	if( selectedEnemies.AreValid() ) {
		worldState->EnemyOriginVar().SetValue( selectedEnemies.LastSeenOrigin() );
		worldState->HasThreateningEnemyVar().SetValue( selectedEnemies.AreThreatening() );
		worldState->RawDamageToKillVar().SetValue( (short)selectedEnemies.DamageToKill() );
		worldState->EnemyHasQuadVar().SetValue( selectedEnemies.HaveQuad() );
		worldState->EnemyHasGoodSniperRangeWeaponsVar().SetValue( selectedEnemies.HaveGoodSniperRangeWeapons() );
		worldState->EnemyHasGoodFarRangeWeaponsVar().SetValue( selectedEnemies.HaveGoodFarRangeWeapons() );
		worldState->EnemyHasGoodMiddleRangeWeaponsVar().SetValue( selectedEnemies.HaveGoodMiddleRangeWeapons() );
		worldState->EnemyHasGoodCloseRangeWeaponsVar().SetValue( selectedEnemies.HaveGoodCloseRangeWeapons() );
		worldState->EnemyCanHitVar().SetValue( selectedEnemies.CanHit() );
	} else {
		worldState->EnemyOriginVar().SetIgnore( true );
		worldState->HasThreateningEnemyVar().SetIgnore( true );
		worldState->RawDamageToKillVar().SetIgnore( true );
		worldState->EnemyHasQuadVar().SetIgnore( true );
		worldState->EnemyHasGoodSniperRangeWeaponsVar().SetIgnore( true );
		worldState->EnemyHasGoodFarRangeWeaponsVar().SetIgnore( true );
		worldState->EnemyHasGoodMiddleRangeWeaponsVar().SetIgnore( true );
		worldState->EnemyHasGoodCloseRangeWeaponsVar().SetIgnore( true );
		worldState->EnemyCanHitVar().SetIgnore( true );
	}

	auto &lostEnemies = self->ai->botRef->lostEnemies;
	if( lostEnemies.AreValid() ) {
		worldState->IsReactingToEnemyLostVar().SetValue( false );
		worldState->HasReactedToEnemyLostVar().SetValue( false );
		worldState->LostEnemyLastSeenOriginVar().SetValue( lostEnemies.LastSeenOrigin() );
		worldState->MightSeeLostEnemyAfterTurnVar().SetValue( false );
		Vec3 toEnemiesDir( lostEnemies.LastSeenOrigin() );
		toEnemiesDir -= self->s.origin;
		toEnemiesDir.NormalizeFast();
		if( toEnemiesDir.Dot( self->ai->botRef->EntityPhysicsState()->ForwardDir() ) < self->ai->botRef->FovDotFactor() ) {
			if( EntitiesPvsCache::Instance()->AreInPvs( self, lostEnemies.TraceKey() ) ) {
				trace_t trace;
				G_Trace( &trace, self->s.origin, nullptr, nullptr, lostEnemies.LastSeenOrigin().Data(), self, MASK_AISOLID );
				if( trace.fraction == 1.0f || game.edicts + trace.ent == lostEnemies.TraceKey() ) {
					worldState->MightSeeLostEnemyAfterTurnVar().SetValue( true );
				}
			}
		}
	} else {
		worldState->IsReactingToEnemyLostVar().SetIgnore( true );
		worldState->HasReactedToEnemyLostVar().SetIgnore( true );
		worldState->LostEnemyLastSeenOriginVar().SetIgnore( true );
		worldState->MightSeeLostEnemyAfterTurnVar().SetIgnore( true );
	}

	worldState->HealthVar().SetValue( (short)HEALTH_TO_INT( self->health ) );
	worldState->ArmorVar().SetValue( self->r.client->ps.stats[STAT_ARMOR] );

	worldState->HasQuadVar().SetValue( ::HasQuad( self ) );
	worldState->HasShellVar().SetValue( ::HasShell( self ) );

	bool hasGoodSniperRangeWeapons = false;
	bool hasGoodFarRangeWeapons = false;
	bool hasGoodMiddleRangeWeapons = false;
	bool hasGoodCloseRangeWeapons = false;

	if( BoltsReadyToFireCount() || BulletsReadyToFireCount() || InstasReadyToFireCount() ) {
		hasGoodSniperRangeWeapons = true;
	}
	if( BoltsReadyToFireCount() || BulletsReadyToFireCount() || PlasmasReadyToFireCount() || InstasReadyToFireCount() ) {
		hasGoodFarRangeWeapons = true;
	}
	if( RocketsReadyToFireCount() || LasersReadyToFireCount() || PlasmasReadyToFireCount() ||
		BulletsReadyToFireCount() || ShellsReadyToFireCount() || InstasReadyToFireCount() || WavesReadyToFireCount() ) {
		hasGoodMiddleRangeWeapons = true;
	}
	if( RocketsReadyToFireCount() || PlasmasReadyToFireCount() || ShellsReadyToFireCount() || WavesReadyToFireCount() ) {
		hasGoodCloseRangeWeapons = true;
	}

	worldState->HasGoodSniperRangeWeaponsVar().SetValue( hasGoodSniperRangeWeapons );
	worldState->HasGoodFarRangeWeaponsVar().SetValue( hasGoodFarRangeWeapons );
	worldState->HasGoodMiddleRangeWeaponsVar().SetValue( hasGoodMiddleRangeWeapons );
	worldState->HasGoodCloseRangeWeaponsVar().SetValue( hasGoodCloseRangeWeapons );

	worldState->HasQuadVar().SetValue( ::HasQuad( self ) );
	worldState->HasShellVar().SetValue( ::HasShell( self ) );

	const SelectedNavEntity &currSelectedNavEntity = self->ai->botRef->GetOrUpdateSelectedNavEntity();
	if( currSelectedNavEntity.IsEmpty() ) {
		// HACK! If there is no selected nav entity, set the value to the roaming spot origin.
		if( self->ai->botRef->ShouldUseRoamSpotAsNavTarget() ) {
			Vec3 spot( self->ai->botRef->roamingManager.GetCachedRoamingSpot() );
			Debug( "Using a roaming spot @ %.1f %.1f %.1f as a world state nav target var\n", spot.X(), spot.Y(), spot.Z() );
			worldState->NavTargetOriginVar().SetValue( spot );
		} else {
			worldState->NavTargetOriginVar().SetIgnore( true );
		}
		worldState->GoalItemWaitTimeVar().SetIgnore( true );
	} else {
		const NavEntity *navEntity = currSelectedNavEntity.GetNavEntity();
		worldState->NavTargetOriginVar().SetValue( navEntity->Origin() );
		// Find a travel time to the goal itme nav entity in milliseconds
		// We hope this router call gets cached by AAS subsystem
		int areaNums[2] = { 0, 0 };
		int numAreas = self->ai->botRef->EntityPhysicsState()->PrepareRoutingStartAreas( areaNums );
		const auto *routeCache = self->ai->botRef->routeCache;
		unsigned travelTime = 10U * routeCache->PreferredRouteToGoalArea( areaNums, numAreas, navEntity->AasAreaNum() );
		// AAS returns 1 seconds^-2 as a lowest feasible value
		if( travelTime <= 10 ) {
			travelTime = 0;
		}
		int64_t spawnTime = navEntity->SpawnTime();
		// If the goal item spawns before the moment when it gets reached
		if( level.time + travelTime >= spawnTime ) {
			worldState->GoalItemWaitTimeVar().SetValue( 0 );
		} else {
			worldState->GoalItemWaitTimeVar().SetValue( (unsigned)( spawnTime - level.time - travelTime ) );
		}
	}

	worldState->HasJustPickedGoalItemVar().SetValue( self->ai->botRef->HasJustPickedGoalItem() );

	worldState->HasPositionalAdvantageVar().SetValue( false );
	worldState->CanHitEnemyVar().SetValue( true );

	worldState->HasJustKilledEnemyVar().SetValue( false );

	// If methods corresponding to these comparisons are extracted, their names will be confusing
	// (they are useful for filling world state only as not always corresponding to what a human caller expect).
	worldState->HasJustTeleportedVar().SetValue( level.time - self->ai->botRef->lastTouchedTeleportAt < 64 + 1 );
	worldState->HasJustTouchedJumppadVar().SetValue( level.time - self->ai->botRef->lastTouchedJumppadAt < 64 + 1 );
	worldState->HasJustEnteredElevatorVar().SetValue( level.time - self->ai->botRef->lastTouchedElevatorAt < 64 + 1 );

	worldState->HasPendingCoverSpotVar().SetIgnore( true );
	worldState->HasPendingRunAwayTeleportVar().SetIgnore( true );
	worldState->HasPendingRunAwayJumppadVar().SetIgnore( true );
	worldState->HasPendingRunAwayElevatorVar().SetIgnore( true );

	worldState->IsRunningAwayVar().SetIgnore( true );
	worldState->HasRunAwayVar().SetIgnore( true );

	const Danger *activeHazard = self->ai->botRef->PrimaryDanger();
	worldState->HasReactedToDangerVar().SetValue( false );
	if( self->ai->botRef->Skill() > 0.33f && activeHazard ) {
		worldState->PotentialDangerDamageVar().SetValue( (short)activeHazard->damage );
		worldState->DangerHitPointVar().SetValue( activeHazard->hitPoint );
		worldState->DangerDirectionVar().SetValue( activeHazard->direction );
		vec3_t dodgeDangerSpot;
		if( FindDodgeDangerSpot( *activeHazard, dodgeDangerSpot ) ) {
			worldState->DodgeDangerSpotVar().SetValue( dodgeDangerSpot );
		} else {
			worldState->DodgeDangerSpotVar().SetIgnore( true );
		}
	} else {
		worldState->PotentialDangerDamageVar().SetIgnore( true );
		worldState->DangerHitPointVar().SetIgnore( true );
		worldState->DangerDirectionVar().SetIgnore( true );
		worldState->DodgeDangerSpotVar().SetIgnore( true );
	}

	if( const auto *activeThreat = self->ai->botRef->ActiveHurtEvent() ) {
		worldState->ThreatInflictedDamageVar().SetValue( (short)activeThreat->totalDamage );
		worldState->ThreatPossibleOriginVar().SetValue( activeThreat->possibleOrigin );
		worldState->HasReactedToThreatVar().SetValue( false );
	} else {
		worldState->ThreatInflictedDamageVar().SetIgnore( true );
		worldState->ThreatPossibleOriginVar().SetIgnore( true );
		worldState->HasReactedToThreatVar().SetIgnore( true );
	}

	worldState->ResetTacticalSpots();

	worldState->SimilarWorldStateInstanceIdVar().SetIgnore( true );

	worldState->PrepareAttachment();

	cachedWorldState = *worldState;
}

bool BotPlanner::ShouldSkipPlanning() const {
	// Skip planning moving on a jumppad
	if( self->ai->botRef->movementState.jumppadMovementState.IsActive() ) {
		return true;
	}

	// Skip planning while preparing for a weaponjump / landing after it
	if( self->ai->botRef->movementState.weaponJumpMovementState.IsActive() ) {
		return true;
	}

	if( self->ai->botRef->movementState.flyUntilLandingMovementState.IsActive() ) {
		return true;
	}

	// Skip planning moving on an elevator
	if( self->groundentity && self->groundentity->use == Use_Plat && self->groundentity->moveinfo.state != STATE_TOP ) {
		return true;
	}

	return false;
}

void BotPlanner::BeforePlanning() {
	BasePlanner::BeforePlanning();

	self->ai->botRef->tacticalSpotsCache.Clear();
}

float Bot::GetEffectiveOffensiveness() const {
	if( squad ) {
		return squad->IsSupporter( self ) ? 1.0f : 0.0f;
	}
	if( selectedEnemies.AreValid() && selectedEnemies.HaveCarrier() ) {
		return 0.75f;
	}
	return baseOffensiveness;
}
