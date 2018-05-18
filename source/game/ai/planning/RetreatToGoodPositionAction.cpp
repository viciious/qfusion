#include "PlanningLocal.h"
#include "../bot.h"

void BotRetreatToGoodPositionActionRecord::Activate() {
	BotBaseActionRecord::Activate();
	self->ai->botRef->GetMiscTactics().PreferAttackRatherThanRun();
	// Set a hint for weapon selection
	self->ai->botRef->GetMiscTactics().willRetreat = true;
	self->ai->botRef->SetNavTarget( &navTarget );
}

void BotRetreatToGoodPositionActionRecord::Deactivate() {
	BotBaseActionRecord::Deactivate();
	self->ai->botRef->ResetNavTarget();
}

AiBaseActionRecord::Status BotRetreatToGoodPositionActionRecord::CheckStatus( const WorldState &currWorldState ) const {
	if( !CheckCommonCombatConditions( currWorldState ) ) {
		return INVALID;
	}

	if( ( navTarget.Origin() - self->s.origin ).SquaredLength() < TACTICAL_SPOT_RADIUS * TACTICAL_SPOT_RADIUS ) {
		return COMPLETED;
	}

	return VALID;
}

PlannerNode *BotRetreatToGoodPositionAction::TryApply( const WorldState &worldState ) {
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
	if( !worldState.HasThreateningEnemyVar() ) {
		Debug( "There is no threatening enemy, and thus retreating does not make sense\n" );
		return nullptr;
	}

	float actionPenalty = 1.0f;
	const float offensiveness = self->ai->botRef->GetEffectiveOffensiveness();
	Vec3 spotOrigin( 0, 0, 0 );
	if( worldState.EnemyIsOnSniperRange() ) {
		Debug( "Retreating on sniper range does not make sense\n" );
		return nullptr;
	} else if( worldState.EnemyIsOnFarRange() ) {
		if( !worldState.HasGoodSniperRangeWeaponsVar() ) {
			if( offensiveness < 0.5f && worldState.HasGoodFarRangeWeaponsVar() ) {
				actionPenalty += 1.0f;
			} else {
				Debug( "Bot doesn't have good sniper range weapons\n" );
				return nullptr;
			}
		}

		// Put this condition last to avoid forcing tactical spot to be computed.
		// Test cheap conditions first for early action rejection.
		if( worldState.SniperRangeTacticalSpotVar().IgnoreOrAbsent() ) {
			Debug( "Sniper range tactical spot is ignored or absent in the given world state\n" );
			return nullptr;
		}

		spotOrigin = worldState.SniperRangeTacticalSpotVar().Value();
	} else if( worldState.EnemyIsOnMiddleRange() ) {
		if( !worldState.HasGoodFarRangeWeaponsVar() && !worldState.HasGoodSniperRangeWeaponsVar() ) {
			Debug( "Bot doesn't have good far or sniper range weapons\n" );
			return nullptr;
		}

		// Put this condition last to avoid forcing tactical spot to be computed.
		// Test cheap conditions first for early action rejection.
		if( worldState.FarRangeTacticalSpotVar().IgnoreOrAbsent() ) {
			if( worldState.SniperRangeTacticalSpotVar().IgnoreOrAbsent() ) {
				Debug( "Far range tactical spot is ignored or absent in the given world state\n" );
				return nullptr;
			} else {
				actionPenalty += 1.0f * offensiveness;
				spotOrigin = worldState.SniperRangeTacticalSpotVar().Value();
			}
		} else {
			spotOrigin = worldState.FarRangeTacticalSpotVar().Value();
		}
	} else if( worldState.EnemyIsOnCloseRange() ) {
		if( worldState.MiddleRangeTacticalSpotVar().IgnoreOrAbsent() ) {
			if( !worldState.HasGoodFarRangeWeaponsVar() && !worldState.HasGoodSniperRangeWeaponsVar() ) {
				Debug( "Middle range tactical spot is ignored or absent in the given world state\n" );
				return nullptr;
			}

			if( worldState.FarRangeTacticalSpotVar().IgnoreOrAbsent() ) {
				if( worldState.SniperRangeTacticalSpotVar().IgnoreOrAbsent() ) {
					Debug( "Middle range tactical spot is ignored or absent in the given world state\n" );
					return nullptr;
				} else {
					actionPenalty += 2.0f * offensiveness;
					spotOrigin = worldState.SniperRangeTacticalSpotVar().Value();
				}
			} else {
				actionPenalty += 1.0f * offensiveness;
				spotOrigin = worldState.FarRangeTacticalSpotVar().Value();
			}
		} else {
			spotOrigin = worldState.MiddleRangeTacticalSpotVar().Value();
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

	// Increase the action penalty for high offensiveness
	actionPenalty *= 0.75f + 0.5f * offensiveness;

	plannerNode.Cost() = travelTimeMillis * actionPenalty;
	plannerNode.WorldState() = worldState;

	plannerNode.WorldState().BotOriginVar().SetValue( spotOrigin );
	plannerNode.WorldState().ResetTacticalSpots();

	// Satisfy conditions for BotKillEnemyGoal
	plannerNode.WorldState().CanHitEnemyVar().SetValue( true );
	plannerNode.WorldState().HasPositionalAdvantageVar().SetValue( true );

	return plannerNode.PrepareActionResult();
}

