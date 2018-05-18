#include "PlanningLocal.h"
#include "../bot.h"

bool BotRunAwayAction::CheckCommonRunAwayPreconditions( const WorldState &worldState ) const {
	if( !worldState.HasRunAwayVar().Ignore() && worldState.HasRunAwayVar() ) {
		Debug( "Bot has already run away in the given world state\n" );
		return false;
	}
	if( !worldState.IsRunningAwayVar().Ignore() && worldState.IsRunningAwayVar() ) {
		Debug( "Bot is already running away in the given world state\n" );
		return false;
	}

	if( worldState.EnemyOriginVar().Ignore() ) {
		Debug( "Enemy is ignored in the given world state\n" );
		return false;
	}
	if( worldState.HealthVar().Ignore() || worldState.ArmorVar().Ignore() ) {
		Debug( "Health or armor are ignored in the given world state\n" );
		return false;
	}

	float offensiveness = self->ai->botRef->GetEffectiveOffensiveness();
	if( offensiveness == 1.0f ) {
		return false;
	}

	if( worldState.EnemyHasQuadVar() && !worldState.HasQuadVar() ) {
		return true;
	}

	if( worldState.HasThreateningEnemyVar() && worldState.DamageToBeKilled() < 25 ) {
		return true;
	}

	if( worldState.EnemyIsOnSniperRange() || worldState.EnemyIsOnSniperRange() ) {
		if( !worldState.EnemyHasGoodSniperRangeWeaponsVar() && !worldState.EnemyHasGoodFarRangeWeaponsVar() ) {
			Debug( "Enemy does not have good sniper range weapons and thus taking cover makes no sense\n" );
			return false;
		}
		if( worldState.DamageToBeKilled() > 80 ) {
			Debug( "Bot can resist more than 80 damage units on sniper range and thus taking cover makes no sense\n" );
			return false;
		}
		return true;
	}

	if( worldState.EnemyIsOnMiddleRange() ) {
		return CheckMiddleRangeKDDamageRatio( worldState );
	}

	return CheckCloseRangeKDDamageRatio( worldState );
}

bool BotRunAwayAction::CheckMiddleRangeKDDamageRatio( const WorldState &worldState ) const {
	float offensiveness = self->ai->botRef->GetEffectiveOffensiveness();
	if( worldState.HasThreateningEnemyVar() ) {
		if( worldState.HasGoodMiddleRangeWeaponsVar() ) {
			if( worldState.KillToBeKilledDamageRatio() < 1.0f + 1.0f * offensiveness ) {
				return false;
			}
		} else {
			if( worldState.KillToBeKilledDamageRatio() < 0.75f + 0.5f * offensiveness ) {
				return false;
			}
		}
		return true;
	}

	if( worldState.HasGoodMiddleRangeWeaponsVar() ) {
		if( worldState.KillToBeKilledDamageRatio() < 1.5f + 3.0f * offensiveness ) {
			return false;
		}
	}

	return worldState.KillToBeKilledDamageRatio() > 1.5f + 1.5f * offensiveness;
}

bool BotRunAwayAction::CheckCloseRangeKDDamageRatio( const WorldState &worldState ) const {
	float offensiveness = self->ai->botRef->GetEffectiveOffensiveness();
	if( worldState.HasThreateningEnemyVar() ) {
		if( worldState.HasGoodCloseRangeWeaponsVar() ) {
			if( worldState.KillToBeKilledDamageRatio() < 1.0f + 1.0f * offensiveness ) {
				return false;
			}
		} else {
			if( worldState.KillToBeKilledDamageRatio() < 0.5f + 0.5f * offensiveness ) {
				return false;
			}
		}
		return true;
	}

	return worldState.KillToBeKilledDamageRatio() > 2.0f + 1.0f * offensiveness;
}