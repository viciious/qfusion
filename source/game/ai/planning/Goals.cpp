#include "Goals.h"
#include "../bot.h"

inline const SelectedNavEntity &BotBaseGoal::SelectedNavEntity() const {
	return self->ai->botRef->GetSelectedNavEntity();
}

inline const SelectedEnemies &BotBaseGoal::SelectedEnemies() const {
	return self->ai->botRef->GetSelectedEnemies();
}

inline const BotWeightConfig &BotBaseGoal::WeightConfig() const {
	return self->ai->botRef->WeightConfig();
}

inline PlannerNode *BotBaseGoal::ApplyExtraActions( PlannerNode *firstTransition, const WorldState &worldState ) {
	for( AiBaseAction *action: extraApplicableActions ) {
		if( PlannerNode *currTransition = action->TryApply( worldState ) ) {
			currTransition->nextTransition = firstTransition;
			firstTransition = currTransition;
		}
	}
	return firstTransition;
}

void BotGrabItemGoal::UpdateWeight( const WorldState &currWorldState ) {
	this->weight = 0.0f;

	if( !SelectedNavEntity().IsValid() ) {
		return;
	}
	if( SelectedNavEntity().IsEmpty() ) {
		return;
	}

	const auto &configGroup = WeightConfig().nativeGoals.grabItem;
	// SelectedNavEntity().PickupGoalWeight() still might need some (minor) tweaking.
	this->weight = configGroup.baseWeight + configGroup.selectedGoalWeightScale * SelectedNavEntity().PickupGoalWeight();
}

void BotGrabItemGoal::GetDesiredWorldState( WorldState *worldState ) {
	worldState->SetIgnoreAll( true );

	worldState->HasJustPickedGoalItemVar().SetValue( true ).SetIgnore( false );
}

#define TRY_APPLY_ACTION( actionName )                                                     \
	do                                                                                       \
	{                                                                                        \
		if( PlannerNode *currTransition = self->ai->botRef->actionName.TryApply( worldState ) ) \
		{                                                                                    \
			currTransition->nextTransition = firstTransition;                                \
			firstTransition = currTransition;                                                \
		}                                                                                    \
	} while( 0 )

PlannerNode *BotGrabItemGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	PlannerNode *firstTransition = nullptr;

	TRY_APPLY_ACTION( genericRunToItemAction );
	TRY_APPLY_ACTION( pickupItemAction );
	TRY_APPLY_ACTION( waitForItemAction );

	return ApplyExtraActions( firstTransition, worldState );
}

void BotKillEnemyGoal::UpdateWeight( const WorldState &currWorldState ) {
	this->weight = 0.0f;

	if( !SelectedEnemies().AreValid() ) {
		return;
	}

	const auto &configGroup = WeightConfig().nativeGoals.killEnemy;

	this->weight = configGroup.baseWeight;
	this->weight += configGroup.offCoeff * self->ai->botRef->GetEffectiveOffensiveness();
	if( currWorldState.HasThreateningEnemyVar() ) {
		this->weight *= configGroup.nmyThreatCoeff;
	} else {
		float maxBotViewDot = SelectedEnemies().MaxDotProductOfBotViewAndDirToEnemy();
		float maxEnemyViewDot = SelectedEnemies().MaxDotProductOfEnemyViewAndDirToBot();
		// Do not lower the goal weight if the enemy is looking on the bot straighter than the bot does
		if( maxEnemyViewDot > 0 && maxEnemyViewDot > maxBotViewDot ) {
			return;
		}

		// Convert to [0, 1] range
		clamp_low( maxBotViewDot, 0.0f );
		// [0, 1]
		float offFrac = configGroup.offCoeff / ( configGroup.offCoeff.MaxValue() - configGroup.offCoeff.MinValue() );
		if( maxBotViewDot < offFrac ) {
			this->weight = 0.001f + this->weight * ( maxBotViewDot / offFrac );
		}
	}
}

void BotKillEnemyGoal::GetDesiredWorldState( WorldState *worldState ) {
	worldState->SetIgnoreAll( true );

	worldState->HasJustKilledEnemyVar().SetValue( true ).SetIgnore( false );
}

PlannerNode *BotKillEnemyGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	PlannerNode *firstTransition = nullptr;

	TRY_APPLY_ACTION( advanceToGoodPositionAction );
	TRY_APPLY_ACTION( retreatToGoodPositionAction );
	TRY_APPLY_ACTION( steadyCombatAction );
	TRY_APPLY_ACTION( gotoAvailableGoodPositionAction );
	TRY_APPLY_ACTION( attackFromCurrentPositionAction );

	TRY_APPLY_ACTION( killEnemyAction );

	return ApplyExtraActions( firstTransition, worldState );
}

void BotRunAwayGoal::UpdateWeight( const WorldState &currWorldState ) {
	this->weight = 0.0f;

	if( !SelectedEnemies().AreValid() ) {
		return;
	}
	if( !SelectedEnemies().AreThreatening() ) {
		return;
	}

	const auto &configGroup = WeightConfig().nativeGoals.runAway;

	this->weight = configGroup.baseWeight;
	this->weight = configGroup.offCoeff * ( 1.0f - self->ai->botRef->GetEffectiveOffensiveness() );
	if( currWorldState.HasThreateningEnemyVar() ) {
		this->weight *= configGroup.nmyThreatCoeff;
	} else {
		float maxBotViewDot = SelectedEnemies().MaxDotProductOfBotViewAndDirToEnemy();
		float maxEnemyViewDot = SelectedEnemies().MaxDotProductOfEnemyViewAndDirToBot();
		// Do not lower the goal weight if the enemy is looking on the bot straighter than the bot does
		if( maxEnemyViewDot > 0 && maxEnemyViewDot > maxBotViewDot ) {
			return;
		}

		// Convert to [0, 1] range
		clamp_low( maxBotViewDot, 0.0f );
		// [0, 1]
		float offFrac = configGroup.offCoeff / ( configGroup.offCoeff.MaxValue() - configGroup.offCoeff.MinValue() );
		if( maxBotViewDot < offFrac ) {
			this->weight = 0.001f + this->weight * ( maxBotViewDot / offFrac );
		}
	}
}

void BotRunAwayGoal::GetDesiredWorldState( WorldState *worldState ) {
	worldState->SetIgnoreAll( true );

	worldState->HasRunAwayVar().SetValue( true ).SetIgnore( false );
}

PlannerNode *BotRunAwayGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	PlannerNode *firstTransition = nullptr;

	TRY_APPLY_ACTION( genericRunAvoidingCombatAction );

	TRY_APPLY_ACTION( startGotoCoverAction );
	TRY_APPLY_ACTION( takeCoverAction );

	TRY_APPLY_ACTION( startGotoRunAwayTeleportAction );
	TRY_APPLY_ACTION( doRunAwayViaTeleportAction );

	TRY_APPLY_ACTION( startGotoRunAwayJumppadAction );
	TRY_APPLY_ACTION( doRunAwayViaJumppadAction );

	TRY_APPLY_ACTION( startGotoRunAwayElevatorAction );
	TRY_APPLY_ACTION( doRunAwayViaElevatorAction );

	TRY_APPLY_ACTION( stopRunningAwayAction );

	return ApplyExtraActions( firstTransition, worldState );
}

void BotAttackOutOfDespairGoal::UpdateWeight( const WorldState &currWorldState ) {
	this->weight = 0.0f;

	if( !SelectedEnemies().AreValid() ) {
		return;
	}

	const auto &configGroup = WeightConfig().nativeGoals.attackOutOfDespair;

	if( SelectedEnemies().FireDelay() > configGroup.nmyFireDelayThreshold ) {
		return;
	}

	// The bot already has the maximal offensiveness, changing it would have the same effect as using duplicated search.
	if( self->ai->botRef->GetEffectiveOffensiveness() == 1.0f ) {
		return;
	}

	this->weight = configGroup.baseWeight;
	if( currWorldState.HasThreateningEnemyVar() ) {
		this->weight += configGroup.nmyThreatExtraWeight;
	}
	float damageWeightPart = BoundedFraction( SelectedEnemies().TotalInflictedDamage(), configGroup.dmgUpperBound );
	this->weight += configGroup.dmgFracCoeff * damageWeightPart;
}

void BotAttackOutOfDespairGoal::GetDesiredWorldState( WorldState *worldState ) {
	worldState->SetIgnoreAll( true );

	worldState->HasJustKilledEnemyVar().SetValue( true ).SetIgnore( false );
}

void BotAttackOutOfDespairGoal::OnPlanBuildingStarted() {
	// Hack: save the bot's base offensiveness and enrage the bot
	this->oldOffensiveness = self->ai->botRef->GetBaseOffensiveness();
	self->ai->botRef->SetBaseOffensiveness( 1.0f );
}

void BotAttackOutOfDespairGoal::OnPlanBuildingCompleted( const AiBaseActionRecord *planHead ) {
	// Hack: restore the bot's base offensiveness
	self->ai->botRef->SetBaseOffensiveness( this->oldOffensiveness );
}

PlannerNode *BotAttackOutOfDespairGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	PlannerNode *firstTransition = nullptr;

	TRY_APPLY_ACTION( advanceToGoodPositionAction );
	TRY_APPLY_ACTION( retreatToGoodPositionAction );
	TRY_APPLY_ACTION( steadyCombatAction );
	TRY_APPLY_ACTION( gotoAvailableGoodPositionAction );
	TRY_APPLY_ACTION( attackFromCurrentPositionAction );

	TRY_APPLY_ACTION( killEnemyAction );

	return ApplyExtraActions( firstTransition, worldState );
}

void BotReactToDangerGoal::GetDesiredWorldState( WorldState *worldState ) {
	worldState->SetIgnoreAll( true );

	worldState->HasReactedToDangerVar().SetIgnore( false ).SetValue( true );
}

void BotReactToDangerGoal::UpdateWeight( const WorldState &currWorldState ) {
	this->weight = 0.0f;

	if( currWorldState.PotentialDangerDamageVar().Ignore() ) {
		return;
	}

	const auto &configGroup = WeightConfig().nativeGoals.reactToDanger;

	float damageFraction = currWorldState.PotentialDangerDamageVar() / currWorldState.DamageToBeKilled();
	float weight_ = configGroup.baseWeight + configGroup.dmgFracCoeff * damageFraction;
	weight_ = BoundedFraction( weight_, configGroup.weightBound );
	weight_ = configGroup.weightBound / Q_RSqrt( weight_ );

	this->weight = weight_;
}

PlannerNode *BotReactToDangerGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	PlannerNode *firstTransition = nullptr;

	TRY_APPLY_ACTION( dodgeToSpotAction );

	return ApplyExtraActions( firstTransition, worldState );
}

void BotReactToThreatGoal::UpdateWeight( const WorldState &currWorldState ) {
	this->weight = 0.0f;

	if( currWorldState.ThreatPossibleOriginVar().Ignore() ) {
		return;
	}

	const auto &configGroup = WeightConfig().nativeGoals.reactToThreat;
	float damageRatio = currWorldState.ThreatInflictedDamageVar() / currWorldState.DamageToBeKilled();
	float weight_ = configGroup.baseWeight + configGroup.dmgFracCoeff * damageRatio;
	float offensiveness = self->ai->botRef->GetEffectiveOffensiveness();
	if( offensiveness >= 0.5f ) {
		weight_ *= ( 1.0f + configGroup.offCoeff * ( offensiveness - 0.5f ) );
	}
	weight_ = BoundedFraction( weight_, configGroup.weightBound );
	weight_ = configGroup.weightBound / Q_RSqrt( weight_ );

	this->weight = weight_;
}

void BotReactToThreatGoal::GetDesiredWorldState( WorldState *worldState ) {
	worldState->SetIgnoreAll( true );

	worldState->HasReactedToThreatVar().SetIgnore( false ).SetValue( true );
}

PlannerNode *BotReactToThreatGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	PlannerNode *firstTransition = nullptr;

	TRY_APPLY_ACTION( turnToThreatOriginAction );

	return ApplyExtraActions( firstTransition, worldState );
}

void BotReactToEnemyLostGoal::UpdateWeight( const WorldState &currWorldState ) {
	this->weight = 0.0f;

	if( currWorldState.LostEnemyLastSeenOriginVar().Ignore() ) {
		return;
	}

	const auto &configGroup = WeightConfig().nativeGoals.reactToEnemyLost;
	this->weight = configGroup.baseWeight + configGroup.offCoeff * self->ai->botRef->GetEffectiveOffensiveness();
}

void BotReactToEnemyLostGoal::GetDesiredWorldState( WorldState *worldState ) {
	worldState->SetIgnoreAll( true );

	worldState->HasReactedToEnemyLostVar().SetIgnore( false ).SetValue( true );
}

PlannerNode *BotReactToEnemyLostGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	PlannerNode *firstTransition = nullptr;

	TRY_APPLY_ACTION( turnToLostEnemyAction );
	TRY_APPLY_ACTION( startLostEnemyPursuitAction );
	TRY_APPLY_ACTION( genericRunAvoidingCombatAction );
	TRY_APPLY_ACTION( stopLostEnemyPursuitAction );

	return ApplyExtraActions( firstTransition, worldState );
}

void BotRoamGoal::UpdateWeight( const WorldState &currWorldState ) {
	// This goal is a fallback goal. Set the lowest feasible weight if it should be positive.
	if( self->ai->botRef->ShouldUseRoamSpotAsNavTarget() ) {
		this->weight = 0.000001f;
		return;
	}

	this->weight = 0.0f;
}

void BotRoamGoal::GetDesiredWorldState( WorldState *worldState ) {
	worldState->SetIgnoreAll( true );

	const Vec3 &spotOrigin = self->ai->botRef->roamingManager.GetCachedRoamingSpot();
	worldState->BotOriginVar().SetValue( spotOrigin );
	worldState->BotOriginVar().SetSatisfyOp( WorldState::SatisfyOp::EQ, 32.0f );
	worldState->BotOriginVar().SetIgnore( false );
}

PlannerNode *BotRoamGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	PlannerNode *firstTransition = nullptr;

	TRY_APPLY_ACTION( genericRunAvoidingCombatAction );

	return ApplyExtraActions( firstTransition, worldState );
}

void BotScriptGoal::UpdateWeight( const WorldState &currWorldState ) {
	this->weight = GENERIC_asGetScriptGoalWeight( scriptObject, currWorldState );
}

void BotScriptGoal::GetDesiredWorldState( WorldState *worldState ) {
	GENERIC_asGetScriptGoalDesiredWorldState( scriptObject, worldState );
}

PlannerNode *BotScriptGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	return ApplyExtraActions( nullptr, worldState );
}

void BotScriptGoal::OnPlanBuildingStarted() {
	GENERIC_asOnScriptGoalPlanBuildingStarted( scriptObject );
}

void BotScriptGoal::OnPlanBuildingCompleted( const AiBaseActionRecord *planHead ) {
	GENERIC_asOnScriptGoalPlanBuildingCompleted( scriptObject, planHead != nullptr );
}