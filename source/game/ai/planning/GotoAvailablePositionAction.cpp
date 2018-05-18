#include "PlanningLocal.h"
#include "../bot.h"

void BotGotoAvailableGoodPositionActionRecord::Activate() {
	BotBaseActionRecord::Activate();
	// Since the combat movement has a decent quality and this action is often triggered in combat, set flags this way.
	self->ai->botRef->GetMiscTactics().PreferAttackRatherThanRun();
	self->ai->botRef->SetNavTarget( &navTarget );
}

void BotGotoAvailableGoodPositionActionRecord::Deactivate() {
	BotBaseActionRecord::Deactivate();
	self->ai->botRef->ResetNavTarget();
}

AiBaseActionRecord::Status BotGotoAvailableGoodPositionActionRecord::CheckStatus( const WorldState &currWorldState ) const {
	if( !CheckCommonCombatConditions( currWorldState ) ) {
		return INVALID;
	}

	if( ( navTarget.Origin() - self->s.origin ).SquaredLength() < TACTICAL_SPOT_RADIUS * TACTICAL_SPOT_RADIUS ) {
		return COMPLETED;
	}

	return VALID;
}

PlannerNode *BotGotoAvailableGoodPositionAction::TryApply( const WorldState &worldState ) {
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

	// Don't check whether enemy is threatening.
	// (Use any chance to get a good position)

	float actionPenalty = 1.0f;
	Vec3 spotOrigin( 0, 0, 0 );
	if( worldState.EnemyIsOnSniperRange() ) {
		if( !worldState.HasGoodSniperRangeWeaponsVar() ) {
			Debug( "Bot does not have good sniper range weapons\n" );
			return nullptr;
		}
		if( worldState.SniperRangeTacticalSpotVar().IgnoreOrAbsent() ) {
			Debug( "Sniper range tactical spot is ignored or absent in the given world state\n" );
			return nullptr;
		}

		spotOrigin = worldState.SniperRangeTacticalSpotVar().Value();
	} else if( worldState.EnemyIsOnFarRange() ) {
		if( !worldState.HasGoodFarRangeWeaponsVar() && !worldState.HasGoodSniperRangeWeaponsVar() ) {
			Debug( "Bot does not have good far or sniper range weapons\n" );
			return nullptr;
		}
		if( worldState.FarRangeTacticalSpotVar().IgnoreOrAbsent() ) {
			if( worldState.SniperRangeTacticalSpotVar().IgnoreOrAbsent() ) {
				Debug( "Far range tactical spot is ignored or absent in the given world state\n" );
				return nullptr;
			} else {
				actionPenalty += 0.5f;
				spotOrigin = worldState.SniperRangeTacticalSpotVar().Value();
			}
		} else {
			spotOrigin = worldState.FarRangeTacticalSpotVar().Value();
		}
	} else if( worldState.EnemyIsOnMiddleRange() ) {
		if( !worldState.HasGoodMiddleRangeWeaponsVar() ) {
			Debug( "Bot does not have good middle range weapons\n" );
			return nullptr;
		}
		if( worldState.MiddleRangeTacticalSpotVar().IgnoreOrAbsent() ) {
			Debug( "Middle range tactical spot is ignored or absent in the given world state\n" );
			return nullptr;
		}

		spotOrigin = worldState.MiddleRangeTacticalSpotVar().Value();
	} else {
		if( !worldState.HasGoodCloseRangeWeaponsVar() ) {
			Debug( "Bot does not have good close range weapons\n" );
			return nullptr;
		}
		if( worldState.CloseRangeTacticalSpotVar().IgnoreOrAbsent() ) {
			Debug( "Close range tactical spot is ignored or absent in the given world state\n" );
			return nullptr;
		}

		spotOrigin = worldState.CloseRangeTacticalSpotVar().Value();
	}

	float offensiveness = self->ai->botRef->GetEffectiveOffensiveness();
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

	plannerNode.Cost() = travelTimeMillis * actionPenalty;
	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().BotOriginVar().SetValue( spotOrigin );
	plannerNode.WorldState().BotOriginVar().SetSatisfyOp( WorldState::SatisfyOp::EQ, TACTICAL_SPOT_RADIUS );
	plannerNode.WorldState().ResetTacticalSpots();
	// Satisfy conditions for BotKillEnemyGoal
	plannerNode.WorldState().CanHitEnemyVar().SetValue( true ).SetIgnore( false );
	plannerNode.WorldState().HasPositionalAdvantageVar().SetValue( true ).SetIgnore( false );
	// Otherwise an another identical world state might be yield leading to the planner logic violation
	unsigned similarWorldStateInstanceId = self->ai->botRef->NextSimilarWorldStateInstanceId();
	plannerNode.WorldState().SimilarWorldStateInstanceIdVar().SetValue( similarWorldStateInstanceId ).SetIgnore( false );

	return plannerNode.PrepareActionResult();
}