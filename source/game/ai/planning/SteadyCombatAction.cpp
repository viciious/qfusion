#include "PlanningLocal.h"
#include "../bot.h"

void BotSteadyCombatActionRecord::Activate() {
	BotBaseActionRecord::Activate();
	self->ai->botRef->GetMiscTactics().PreferAttackRatherThanRun();
	self->ai->botRef->SetNavTarget( &navTarget );
}

void BotSteadyCombatActionRecord::Deactivate() {
	BotBaseActionRecord::Deactivate();
	self->ai->botRef->ResetNavTarget();
}

AiBaseActionRecord::Status BotSteadyCombatActionRecord::CheckStatus( const WorldState &currWorldState ) const {
	if( !CheckCommonCombatConditions( currWorldState ) ) {
		return INVALID;
	}

	// Never return COMPLETED value (otherwise the dummy following BotKillEnemyAction may be activated leading to a crash).
	// This action often gets actually deactivated on replanning.

	// Bot often moves out of TACTICAL_SPOT_RADIUS during "camping a spot" movement
	constexpr float invalidationRadius = 1.5f * TACTICAL_SPOT_RADIUS;
	if( ( navTarget.Origin() - self->s.origin ).SquaredLength() > invalidationRadius * invalidationRadius ) {
		return INVALID;
	}

	return VALID;
}

PlannerNode *BotSteadyCombatAction::TryApply( const WorldState &worldState ) {
	if( worldState.EnemyOriginVar().Ignore() ) {
		Debug( "Enemy is ignored in the given world state\n" );
		return nullptr;
	}
	if( !worldState.HasPositionalAdvantageVar().Ignore() && worldState.HasPositionalAdvantageVar() ) {
		Debug( "Bot already has a positional advantage in the given world state\n" );
		return nullptr;
	}
	if( worldState.HealthVar().Ignore() || worldState.ArmorVar().Ignore() ) {
		Debug( "Health or armor are ignored in the given world state\n" );
		return nullptr;
	}

	Vec3 spotOrigin( 0, 0, 0 );
	float actionPenalty = 1.0f;
	const float offensiveness = self->ai->botRef->GetEffectiveOffensiveness();
	if( worldState.EnemyIsOnSniperRange() ) {
		if( !worldState.HasGoodSniperRangeWeaponsVar() ) {
			if( offensiveness < 0.5f || !worldState.HasGoodFarRangeWeaponsVar() ) {
				return nullptr;
			}
			actionPenalty += 0.5f;
		}

		if( worldState.EnemyHasGoodSniperRangeWeaponsVar() ) {
			if( !worldState.EnemyHasGoodSniperRangeWeaponsVar() && worldState.HasGoodFarRangeWeaponsVar() ) {
				actionPenalty += 0.5f * offensiveness;
			}
			if( !worldState.EnemyHasGoodMiddleRangeWeaponsVar() && worldState.HasGoodMiddleRangeWeaponsVar() ) {
				actionPenalty += 0.5f * offensiveness;
			}
			if( !worldState.EnemyHasGoodCloseRangeWeaponsVar() && worldState.HasGoodCloseRangeWeaponsVar() ) {
				actionPenalty += 0.5f * offensiveness;
			}
		}

		// Put this condition last to avoid forcing tactical spot to be computed.
		// Test cheap conditions first for early action rejection.
		if( worldState.SniperRangeTacticalSpotVar().IgnoreOrAbsent() ) {
			return nullptr;
		}

		if( worldState.DistanceToSniperRangeTacticalSpot() > 64.0f ) {
			return nullptr;
		}

		spotOrigin = worldState.SniperRangeTacticalSpotVar().Value();
	} else if( worldState.EnemyIsOnFarRange() ) {
		if( !worldState.HasGoodFarRangeWeaponsVar() ) {
			return nullptr;
		}

		if( worldState.EnemyHasGoodFarRangeWeaponsVar() ) {
			if( !worldState.EnemyHasGoodSniperRangeWeaponsVar() && worldState.HasGoodMiddleRangeWeaponsVar() ) {
				actionPenalty += 0.5f * offensiveness;
			}
			if( !worldState.EnemyHasGoodCloseRangeWeaponsVar() && worldState.HasGoodCloseRangeWeaponsVar() ) {
				actionPenalty += 0.5f * offensiveness;
			}
		}

		// Put this condition last to avoid forcing tactical spot to be computed.
		// Test cheap conditions first for early action rejection.
		if( worldState.FarRangeTacticalSpotVar().IgnoreOrAbsent() ) {
			return nullptr;
		}

		if( worldState.DistanceToFarRangeTacticalSpot() > 64.0f ) {
			return nullptr;
		}

		spotOrigin = worldState.FarRangeTacticalSpotVar().Value();
	} else if( worldState.EnemyIsOnMiddleRange() ) {
		if( !worldState.HasGoodMiddleRangeWeaponsVar() ) {
			return nullptr;
		}

		if( worldState.EnemyHasGoodMiddleRangeWeaponsVar() ) {
			if( !worldState.EnemyHasGoodMiddleRangeWeaponsVar() && worldState.HasGoodCloseRangeWeaponsVar() ) {
				actionPenalty += 1.0f * offensiveness;
			}
		}

		// Put this condition last to avoid forcing tactical spot to be computed.
		// Test cheap conditions first for early action rejection.
		if( worldState.MiddleRangeTacticalSpotVar().IgnoreOrAbsent() ) {
			return nullptr;
		}

		if( worldState.DistanceToMiddleRangeTacticalSpot() > 64.0f ) {
			return nullptr;
		}

		spotOrigin = worldState.MiddleRangeTacticalSpotVar().Value();
	} else {
		if( !worldState.HasGoodCloseRangeWeaponsVar() ) {
			Debug( "Bot does not have good close range weapons\n" );
			return nullptr;
		}

		// Put this condition last to avoid forcing tactical spot to be computed.
		// Test cheap conditions first for early action rejection.
		if( worldState.CloseRangeTacticalSpotVar().IgnoreOrAbsent() ) {
			Debug( "Close range tactical spot is ignored or is absent\n" );
			return nullptr;
		}
		if( worldState.DistanceToCloseRangeTacticalSpot() > 48.0f ) {
			Debug( "Bot is already on the close range tactical spot\n" );
			return nullptr;
		}

		spotOrigin = worldState.CloseRangeTacticalSpotVar().Value();
	}

	// It is faster to check this apriori before spot assignation but the code becomes unmaintainable
	if( worldState.HasThreateningEnemyVar() && offensiveness != 1.0f ) {
		if( worldState.BotOriginVar().Value().DistanceTo( spotOrigin ) > LgRange() ) {
			if( offensiveness <= 0.5f ) {
				if( worldState.DamageToBeKilled() < 80.0f && worldState.KillToBeKilledDamageRatio() > 2.0f ) {
					return nullptr;
				}
			}
		} else {
			if( worldState.KillToBeKilledDamageRatio() > 2.0f * offensiveness ) {
				return nullptr;
			}
		}
	}

	unsigned selectedEnemiesInstanceId = self->ai->botRef->GetSelectedEnemies().InstanceId();
	PlannerNodePtr plannerNode = NewNodeForRecord( pool.New( self, spotOrigin, selectedEnemiesInstanceId ) );
	if( !plannerNode ) {
		return nullptr;
	}

	// Cost is measured in milliseconds, so we have to convert our estimations to this unit, assuming dps is 50
	float secondsToKill = worldState.DamageToKill() / 50.0f;
	plannerNode.Cost() = actionPenalty * (1000 * secondsToKill);

	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().BotOriginVar().SetValue( spotOrigin );
	plannerNode.WorldState().BotOriginVar().SetSatisfyOp( WorldState::SatisfyOp::EQ, TACTICAL_SPOT_RADIUS );
	// These vars should be lazily recomputed for the modified bot origin
	plannerNode.WorldState().ResetTacticalSpots();

	// Satisfy conditions for BotKillEnemyGoal
	plannerNode.WorldState().CanHitEnemyVar().SetValue( true ).SetIgnore( false );
	plannerNode.WorldState().HasPositionalAdvantageVar().SetValue( true ).SetIgnore( false );

	return plannerNode.PrepareActionResult();
}