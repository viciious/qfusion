#include "PlanningLocal.h"
#include "../bot.h"

void BotAdvanceToGoodPositionActionRecord::Activate() {
	BotBaseActionRecord::Activate();
	self->ai->botRef->GetMiscTactics().PreferAttackRatherThanRun();
	// Set a hint for weapon selection
	self->ai->botRef->GetMiscTactics().willAdvance = true;
	self->ai->botRef->SetNavTarget( &navTarget );
}

void BotAdvanceToGoodPositionActionRecord::Deactivate() {
	BotBaseActionRecord::Deactivate();
	self->ai->botRef->ResetNavTarget();
}

AiBaseActionRecord::Status BotAdvanceToGoodPositionActionRecord::CheckStatus( const WorldState &currWorldState ) const {
	if( !CheckCommonCombatConditions( currWorldState ) ) {
		return INVALID;
	}

	if( ( navTarget.Origin() - self->s.origin ).SquaredLength() < TACTICAL_SPOT_RADIUS * TACTICAL_SPOT_RADIUS ) {
		return COMPLETED;
	}

	return VALID;
}


PlannerNode *BotAdvanceToGoodPositionAction::TryApply( const WorldState &worldState ) {
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

	const float offensiveness = self->ai->botRef->GetEffectiveOffensiveness();
	float actionPenalty = 1.0f;
	Vec3 spotOrigin( 0, 0, 0 );

	if( worldState.EnemyIsOnSniperRange() ) {
		if( !worldState.HasGoodFarRangeWeaponsVar() ) {
			Debug( "Bot doesn't have good far range weapons\n" );
			return nullptr;
		}

		if( !worldState.HasGoodSniperRangeWeaponsVar() ) {
			if( offensiveness <= 0.5f ) {
				Debug( "Bot doesn't have good sniper range weapons and thus can't advance attacking\n" );
				return nullptr;
			}
			actionPenalty += 0.5f;
		}

		// Put this condition last to avoid forcing tactical spot to be computed.
		// Test cheap conditions first for early action rejection.
		if( worldState.FarRangeTacticalSpotVar().IgnoreOrAbsent() ) {
			if( worldState.HasGoodMiddleRangeWeaponsVar() || worldState.HasGoodCloseRangeWeaponsVar() ) {
				if( offensiveness <= 0.5f ) {
					actionPenalty += 1.0f;
				}
				if( worldState.MiddleRangeTacticalSpotVar().IsPresent() ) {
					spotOrigin = worldState.MiddleRangeTacticalSpotVar().Value();
				} else if( worldState.CloseRangeTacticalSpotVar().IsPresent() ) {
					spotOrigin = worldState.CloseRangeTacticalSpotVar().Value();
				} else {
					Debug( "Far range tactical spot is ignored or absent in the given world state\n" );
					return nullptr;
				}
			}
		} else {
			spotOrigin = worldState.FarRangeTacticalSpotVar().Value();
		}
	} else if( worldState.EnemyIsOnFarRange() ) {
		if( !worldState.HasGoodMiddleRangeWeaponsVar() ) {
			Debug( "Bot doesn't have good middle range weapons\n" );
			return nullptr;
		}

		if( !worldState.HasGoodFarRangeWeaponsVar() ) {
			if( offensiveness <= 0.5f ) {
				Debug( "Bot doesn't have good far range weapons and thus can't advance attacking\n" );
				return nullptr;
			}
		}

		// Put this condition last to avoid forcing tactical spot to be computed.
		// Test cheap conditions first for early action rejection.
		if( worldState.MiddleRangeTacticalSpotVar().IgnoreOrAbsent() ) {
			if( worldState.HasGoodMiddleRangeWeaponsVar() || worldState.HasGoodCloseRangeWeaponsVar() ) {
				if( offensiveness <= 0.5f ) {
					actionPenalty += 0.5f;
				}
				if( worldState.CloseRangeTacticalSpotVar().IsPresent() ) {
					if( worldState.HasGoodCloseRangeWeaponsVar() || worldState.HasGoodMiddleRangeWeaponsVar()) {
						spotOrigin = worldState.CloseRangeTacticalSpotVar().Value();
					} else {
						Debug( "Middle range tactical spot is ignored or absent in the given world state\n" );
						return nullptr;
					}
				}
			}
		} else {
			spotOrigin = worldState.MiddleRangeTacticalSpotVar().Value();
		}
	} else if( worldState.EnemyIsOnMiddleRange() ) {
		if( !worldState.HasGoodCloseRangeWeaponsVar() ) {
			Debug( "Bot doesn't have good close range weapons in the given world state\n" );
			return nullptr;
		}

		if( !worldState.HasGoodMiddleRangeWeaponsVar() ) {
			Debug( "Bot doesn't have good middle range weapons and thus can't advance attacking\n" );
			return nullptr;
		}

		// Put this condition last to avoid forcing tactical spot to be computed.
		// Test cheap conditions first for early action rejection.
		if( worldState.CloseRangeTacticalSpotVar().IgnoreOrAbsent() ) {
			Debug( "Close range tactical spot is ignored or absent in the given world state\n" );
			return nullptr;
		}

		spotOrigin = worldState.CloseRangeTacticalSpotVar().Value();
	} else {
		Debug( "Advancing on a close range does not make sense\n" );
		return nullptr;
	}

	// It is faster to check this apriori before spot assignation but the code becomes unmaintainable
	if( worldState.HasThreateningEnemyVar() && offensiveness != 1.0f ) {
		if( worldState.BotOriginVar().Value().DistanceTo( spotOrigin ) > LgRange() ) {
			if( offensiveness <= 0.5f ) {
				if( worldState.DamageToBeKilled() < 80.0f && worldState.KillToBeKilledDamageRatio() > 1.0f ) {
					return nullptr;
				}
			}
		} else {
			if( worldState.KillToBeKilledDamageRatio() > 0.5f + 2.0f * offensiveness ) {
				return nullptr;
			}
		}
	}

	int travelTimeMillis = self->ai->botRef->CheckTravelTimeMillis( worldState.BotOriginVar().Value(), spotOrigin );
	if( !travelTimeMillis ) {
		Debug( "Warning: can't find travel time from the bot origin to the spot origin in the given world state\n" );
		return nullptr;
	}

	unsigned selectedEnemiesInstanceId = self->ai->botRef->GetSelectedEnemies().InstanceId();
	PlannerNodePtr plannerNode = NewNodeForRecord( pool.New( self, spotOrigin, selectedEnemiesInstanceId ) );
	if( !plannerNode ) {
		return nullptr;
	}

	// Decrease action penalty for high offensiveness
	actionPenalty *= 1.25f - 0.5f * offensiveness;

	plannerNode.Cost() = travelTimeMillis * actionPenalty;
	plannerNode.WorldState() = worldState;

	plannerNode.WorldState().BotOriginVar().SetValue( spotOrigin );
	plannerNode.WorldState().ResetTacticalSpots();

	// Satisfy conditions for BotKillEnemyGoal
	plannerNode.WorldState().CanHitEnemyVar().SetValue( true );
	plannerNode.WorldState().HasPositionalAdvantageVar().SetValue( true );

	return plannerNode.PrepareActionResult();
}