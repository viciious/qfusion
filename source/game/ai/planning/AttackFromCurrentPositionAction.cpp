#include "PlanningLocal.h"
#include "../bot.h"
#include "../combat/TacticalSpotsRegistry.h"

void BotAttackFromCurrentPositionActionRecord::Activate() {
	BotBaseActionRecord::Activate();
	self->ai->botRef->GetMiscTactics().PreferAttackRatherThanRun();
	self->ai->botRef->SetNavTarget( &navTarget );
}

void BotAttackFromCurrentPositionActionRecord::Deactivate() {
	BotBaseActionRecord::Deactivate();
	self->ai->botRef->ResetNavTarget();
}

AiBaseActionRecord::Status BotAttackFromCurrentPositionActionRecord::CheckStatus( const WorldState &currWorldState ) const {
	if( !CheckCommonCombatConditions( currWorldState ) ) {
		return INVALID;
	}

	if( navTarget.Origin().SquareDistance2DTo( self->s.origin ) < 16 * 16 ) {
		vec3_t spotOrigin;
		TacticalSpotsRegistry::OriginParams originParams( self, 128.0f, AiAasRouteCache::Shared() );
		const float *keepVisOrigin = self->ai->botRef->GetSelectedEnemies().LastSeenOrigin().Data();
		if( TacticalSpotsRegistry::Instance()->FindShortSideStepDodgeSpot( originParams, keepVisOrigin, spotOrigin ) ) {
			self->ai->botRef->SetNavTarget( Vec3( spotOrigin ), 16.0f );
		}
	}

	// This action is likely to be deactivate on goal search/reevaluation, do not do extra tests.
	return VALID;
}

PlannerNode *BotAttackFromCurrentPositionAction::TryApply( const WorldState &worldState ) {
	// Use almost the same criteria as for BotSteadyCombatAction
	// with the exception that tactical spots must be absent for low offensiveness.
	// Allow attacking from current position on high offensiveness even if a tactical spot exist
	// (attacking from tactical spots has more restrictive conditions on kill/be killed damage ratio).

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

	float offensiveness = self->ai->botRef->GetEffectiveOffensiveness();
	if( offensiveness <= 0.5f && !worldState.HasThreateningEnemyVar() ) {
		return nullptr;
	}

	if( offensiveness != 1.0f ) {
		if( worldState.EnemyIsOnSniperRange() ) {
			if( !worldState.HasGoodSniperRangeWeaponsVar() ) {
				if( offensiveness <= 0.5f && !worldState.HasGoodFarRangeWeaponsVar() ) {
					return nullptr;
				}
			}
		} else if( worldState.EnemyIsOnFarRange()) {
			if( offensiveness <= 0.5f ) {
				if( !worldState.HasGoodFarRangeWeaponsVar() && !worldState.HasGoodSniperRangeWeaponsVar()) {
					return nullptr;
				}
			}
		} else if( worldState.EnemyIsOnMiddleRange()) {
			if( !worldState.HasGoodMiddleRangeWeaponsVar()) {
				if( offensiveness <= 0.5f && !worldState.HasGoodFarRangeWeaponsVar() ) {
					return nullptr;
				}
			}
		} else if( worldState.EnemyIsOnCloseRange()) {
			if( !worldState.HasGoodCloseRangeWeaponsVar()) {
				if( offensiveness <= 0.5f ) {
					if( !worldState.HasGoodMiddleRangeWeaponsVar() && !worldState.HasGoodFarRangeWeaponsVar() ) {
						return nullptr;
					}
				}
			}
		}

		// It is faster to check this apriori before spot assignation but the code becomes unmaintainable
		if( worldState.HasThreateningEnemyVar() && offensiveness != 1.0f ) {
			if( worldState.EnemyIsOnFarRange() || worldState.EnemyIsOnFarRange() ) {
				if( offensiveness <= 0.5f ) {
					if( worldState.DamageToBeKilled() < 80.0f && worldState.KillToBeKilledDamageRatio() > 2.0f ) {
						return nullptr;
					}
				}
			} else {
				if( worldState.KillToBeKilledDamageRatio() > 1.3f * offensiveness ) {
					return nullptr;
				}
			}
		}
	}

	Vec3 navTargetOrigin( worldState.BotOriginVar().Value() );
	unsigned selectedEnemiesInstanceId = self->ai->botRef->GetSelectedEnemies().InstanceId();
	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( self, navTargetOrigin, selectedEnemiesInstanceId ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	// Set a huge penalty from attacking from a current position and not from a found tactical spot
	plannerNode.Cost() = 999999.0f;

	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().BotOriginVar().SetValue( navTargetOrigin )
		.SetSatisfyOp( WorldState::SatisfyOp::EQ, TACTICAL_SPOT_RADIUS );
	// Setting this is required to satisfy the BotKillEnemyAction preconditions
	// (even they are not really met from human point of view).
	plannerNode.WorldState().HasPositionalAdvantageVar().SetValue( true ).SetIgnore( false );

	return plannerNode.PrepareActionResult();
}