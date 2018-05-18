#include "Actions.h"
#include "../bot.h"
#include "../ai_ground_trace_cache.h"
#include "../combat/TacticalSpotsRegistry.h"

typedef WorldState::SatisfyOp SatisfyOp;

// These methods really belong to the bot logic, not the generic AI ones

const short *WorldState::GetSniperRangeTacticalSpot() {
	return self->ai->botRef->tacticalSpotsCache.GetSniperRangeTacticalSpot( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetFarRangeTacticalSpot() {
	return self->ai->botRef->tacticalSpotsCache.GetFarRangeTacticalSpot( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetMiddleRangeTacticalSpot() {
	return self->ai->botRef->tacticalSpotsCache.GetMiddleRangeTacticalSpot( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetCloseRangeTacticalSpot() {
	return self->ai->botRef->tacticalSpotsCache.GetCloseRangeTacticalSpot( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetCoverSpot() {
	return self->ai->botRef->tacticalSpotsCache.GetCoverSpot( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetRunAwayTeleportOrigin() {
	return self->ai->botRef->tacticalSpotsCache.GetRunAwayTeleportOrigin( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetRunAwayJumppadOrigin() {
	return self->ai->botRef->tacticalSpotsCache.GetRunAwayJumppadOrigin( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetRunAwayElevatorOrigin() {
	return self->ai->botRef->tacticalSpotsCache.GetRunAwayElevatorOrigin( BotOriginData(), EnemyOriginData() );
}

inline const BotWeightConfig &BotBaseAction::WeightConfig() const {
	return self->ai->botRef->WeightConfig();
}

void BotBaseActionRecord::Activate() {
	AiBaseActionRecord::Activate();
	self->ai->botRef->GetMiscTactics().Clear();
}

void BotBaseActionRecord::Deactivate() {
	AiBaseActionRecord::Deactivate();
	self->ai->botRef->GetMiscTactics().Clear();
}

void BotGenericRunToItemActionRecord::Activate() {
	BotBaseActionRecord::Activate();
	// Attack if view angles needed for movement fit aiming
	self->ai->botRef->GetMiscTactics().PreferRunRatherThanAttack();
	self->ai->botRef->SetNavTarget( &navTarget );
}

void BotGenericRunToItemActionRecord::Deactivate() {
	BotBaseActionRecord::Deactivate();
	self->ai->botRef->ResetNavTarget();
}

AiBaseActionRecord::Status BotGenericRunToItemActionRecord::CheckStatus( const WorldState &currWorldState ) const {
	const auto &selectedNavEntity = self->ai->botRef->GetSelectedNavEntity();
	if( !navTarget.IsBasedOnNavEntity( selectedNavEntity.GetNavEntity() ) ) {
		Debug( "Nav target does no longer match selected nav entity\n" );
		return INVALID;
	}
	if( navTarget.SpawnTime() == 0 ) {
		Debug( "Illegal nav target spawn time (looks like it has been invalidated)\n" );
		return INVALID;
	}
	if( currWorldState.DistanceToNavTarget() <= GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Nav target pickup distance has been reached\n" );
		return COMPLETED;
	}

	return VALID;
}

PlannerNode *BotGenericRunToItemAction::TryApply( const WorldState &worldState ) {
	if( worldState.GoalItemWaitTimeVar().Ignore() ) {
		Debug( "Goal item is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.NavTargetOriginVar().Ignore() ) {
		Debug( "Nav target is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.DistanceToNavTarget() <= GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Distance to goal item nav target is too low in the given world state\n" );
		return nullptr;
	}

	constexpr float roundingSquareDistanceError = WorldState::OriginVar::MAX_ROUNDING_SQUARE_DISTANCE_ERROR;
	if( ( worldState.BotOriginVar().Value() - self->s.origin ).SquaredLength() > roundingSquareDistanceError ) {
		Debug( "Selected goal item is valid only for current bot origin\n" );
		return nullptr;
	}

	const auto &itemNavEntity = self->ai->botRef->GetSelectedNavEntity();

	PlannerNodePtr plannerNode = NewNodeForRecord( pool.New( self, itemNavEntity.GetNavEntity() ) );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode.Cost() = itemNavEntity.GetCost();

	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().BotOriginVar().SetValue( itemNavEntity.GetNavEntity()->Origin() );
	plannerNode.WorldState().BotOriginVar().SetSatisfyOp( SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
	plannerNode.WorldState().ResetTacticalSpots();

	return plannerNode.PrepareActionResult();
}

void BotPickupItemActionRecord::Activate() {
	BotBaseActionRecord::Activate();
	self->ai->botRef->GetMiscTactics().shouldMoveCarefully = true;
	self->ai->botRef->GetMiscTactics().PreferAttackRatherThanRun();
	self->ai->botRef->SetCampingSpot( AiCampingSpot( navTarget.Origin(), GOAL_PICKUP_ACTION_RADIUS, 0.5f ) );
	self->ai->botRef->SetNavTarget( &navTarget );
}

void BotPickupItemActionRecord::Deactivate() {
	BotBaseActionRecord::Deactivate();
	self->ai->botRef->ResetNavTarget();
	self->ai->botRef->ResetCampingSpot();
}

AiBaseActionRecord::Status BotPickupItemActionRecord::CheckStatus( const WorldState &currWorldState ) const {
	if( currWorldState.HasJustPickedGoalItemVar() ) {
		Debug( "Goal item has been just picked up\n" );
		return COMPLETED;
	}

	const SelectedNavEntity &currSelectedNavEntity = self->ai->botRef->GetSelectedNavEntity();
	if( !navTarget.IsBasedOnNavEntity( currSelectedNavEntity.GetNavEntity() ) ) {
		Debug( "Nav target does no longer match current selected nav entity\n" );
		return INVALID;
	}
	if( !navTarget.SpawnTime() ) {
		Debug( "Illegal nav target spawn time (looks like it has been invalidated)\n" );
		return INVALID;
	}
	if( navTarget.SpawnTime() - level.time > 0 ) {
		Debug( "The nav target requires waiting for it\n" );
		return INVALID;
	}
	if( currWorldState.DistanceToNavTarget() > GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "The nav target is too far from the bot to pickup it\n" );
		return INVALID;
	}
	if( currWorldState.HasThreateningEnemyVar() ) {
		Debug( "The bot has threatening enemy\n" );
		return INVALID;
	}

	return VALID;
}

PlannerNode *BotPickupItemAction::TryApply( const WorldState &worldState ) {
	if( worldState.GoalItemWaitTimeVar().Ignore() ) {
		Debug( "Goal item is ignored in the given world state\n" );
		return nullptr;
	}

	if( worldState.DistanceToNavTarget() > GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Distance to goal item nav target is too large to pick up an item in the given world state\n" );
		return nullptr;
	}

	if( worldState.HasJustPickedGoalItemVar().Ignore() ) {
		Debug( "Has bot picked a goal item is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.HasJustPickedGoalItemVar() ) {
		Debug( "Bot has just picked a goal item in the given world state\n" );
		return nullptr;
	}

	if( worldState.GoalItemWaitTimeVar().Ignore() ) {
		Debug( "Goal item wait time is not specified in the given world state\n" );
		return nullptr;
	}
	if( worldState.GoalItemWaitTimeVar() > 0 ) {
		Debug( "Goal item wait time is non-zero in the given world state\n" );
		return nullptr;
	}

	const auto &itemNavEntity = self->ai->botRef->GetSelectedNavEntity();
	PlannerNodePtr plannerNode = NewNodeForRecord( pool.New( self, itemNavEntity.GetNavEntity() ) );
	if( !plannerNode ) {
		return nullptr;
	}

	// Picking up an item costs almost nothing
	plannerNode.Cost() = 1.0f;

	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().HasJustPickedGoalItemVar().SetValue( true ).SetIgnore( false );
	plannerNode.WorldState().BotOriginVar().SetValue( itemNavEntity.GetNavEntity()->Origin() );
	plannerNode.WorldState().BotOriginVar().SetSatisfyOp( SatisfyOp::EQ, 12.0f );
	plannerNode.WorldState().ResetTacticalSpots();

	return plannerNode.PrepareActionResult();
}

void BotWaitForItemActionRecord::Activate() {
	BotBaseActionRecord::Activate();
	self->ai->botRef->GetMiscTactics().shouldMoveCarefully = true;
	self->ai->botRef->GetMiscTactics().PreferAttackRatherThanRun();
	self->ai->botRef->SetNavTarget( &navTarget );
	self->ai->botRef->SetCampingSpot( AiCampingSpot( navTarget.Origin(), GOAL_PICKUP_ACTION_RADIUS, 0.5f ) );
}

void BotWaitForItemActionRecord::Deactivate() {
	BotBaseActionRecord::Deactivate();
	self->ai->botRef->ResetCampingSpot();
	self->ai->botRef->ResetNavTarget();
}

AiBaseActionRecord::Status BotWaitForItemActionRecord::CheckStatus( const WorldState &currWorldState ) const {
	if( currWorldState.HasJustPickedGoalItemVar() ) {
		Debug( "Goal item has been just picked up\n" );
		return COMPLETED;
	}

	const auto &currSelectedNavEntity = self->ai->botRef->GetSelectedNavEntity();
	if( !navTarget.IsBasedOnNavEntity( currSelectedNavEntity.GetNavEntity() ) ) {
		Debug( "Nav target does no longer match current selected nav entity\n" );
		return INVALID;
	}
	if( !navTarget.SpawnTime() ) {
		Debug( "Illegal nav target spawn time (looks like it has been invalidated)\n" );
		return INVALID;
	}
	// Wait duration is too long (more than it was estimated)
	uint64_t waitDuration = (uint64_t)( navTarget.SpawnTime() - level.time );
	if( waitDuration > navTarget.MaxWaitDuration() ) {
		constexpr auto *format =
			"Wait duration %" PRIu64 " is too long (the maximum allowed value for the nav target is %" PRIu64 ")\n";
		Debug( format, waitDuration, navTarget.MaxWaitDuration() );
		return INVALID;
	}
	if( currWorldState.DistanceToNavTarget() > GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Distance to the item is too large to wait for it\n" );
		return INVALID;
	}
	if( currWorldState.HasThreateningEnemyVar() ) {
		Debug( "The bot has a threatening enemy\n" );
		return INVALID;
	}

	return VALID;
}

PlannerNode *BotWaitForItemAction::TryApply( const WorldState &worldState ) {
	if( worldState.NavTargetOriginVar().Ignore() ) {
		Debug( "Nav target is ignored in the given world state\n" );
		return nullptr;
	}

	if( worldState.DistanceToNavTarget() > GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Distance to goal item nav target is too large to wait for an item in the given world state\n" );
		return nullptr;
	}

	if( worldState.HasJustPickedGoalItemVar().Ignore() ) {
		Debug( "Has bot picked a goal item is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.HasJustPickedGoalItemVar() ) {
		Debug( "Bot has just picked a goal item in the given world state\n" );
		return nullptr;
	}

	if( worldState.GoalItemWaitTimeVar().Ignore() ) {
		Debug( "Goal item wait time is not specified in the given world state\n" );
		return nullptr;
	}
	if( worldState.GoalItemWaitTimeVar() == 0 ) {
		Debug( "Goal item wait time is zero in the given world state\n" );
		return nullptr;
	}

	const auto &itemNavEntity = self->ai->botRef->GetSelectedNavEntity();
	PlannerNodePtr plannerNode = NewNodeForRecord( pool.New( self, itemNavEntity.GetNavEntity() ) );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode.Cost() = worldState.GoalItemWaitTimeVar();

	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().HasJustPickedGoalItemVar().SetValue( true ).SetIgnore( false );
	plannerNode.WorldState().BotOriginVar().SetValue( itemNavEntity.GetNavEntity()->Origin() );
	plannerNode.WorldState().BotOriginVar().SetSatisfyOp( SatisfyOp::EQ, 12.0f );
	plannerNode.WorldState().ResetTacticalSpots();

	return plannerNode.PrepareActionResult();
}

bool BotCombatActionRecord::CheckCommonCombatConditions( const WorldState &currWorldState ) const {
	if( currWorldState.EnemyOriginVar().Ignore() ) {
		Debug( "Enemy is not specified\n" );
		return false;
	}
	if( self->ai->botRef->GetSelectedEnemies().InstanceId() != selectedEnemiesInstanceId ) {
		Debug( "New enemies have been selected\n" );
		return false;
	}
	return true;
}

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


inline bool ShouldRetreatWithThisDamageRatio( const WorldState &worldState, float ratioThreshold ) {
	clamp_low( ratioThreshold, 0.2f );
	return worldState.KillToBeKilledDamageRatio() > ratioThreshold;
}

inline bool ShouldRetreatWithThisDamageToBeKilled( const WorldState &worldState, float damageThreshold ) {
	clamp_low( damageThreshold, 15 );
	return worldState.DamageToBeKilled() < damageThreshold;
}

inline float LgRange() {
	return GS_GetWeaponDef( WEAP_LASERGUN )->firedef.timeout;
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

	plannerNode.WorldState().BotOriginVar().SetValue( spotOrigin ).SetSatisfyOp( SatisfyOp::EQ, TACTICAL_SPOT_RADIUS );
	plannerNode.WorldState().ResetTacticalSpots();

	// Satisfy conditions for BotKillEnemyGoal
	plannerNode.WorldState().CanHitEnemyVar().SetValue( true );
	plannerNode.WorldState().HasPositionalAdvantageVar().SetValue( true );

	return plannerNode.PrepareActionResult();
}

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

	plannerNode.WorldState().BotOriginVar().SetValue( spotOrigin ).SetSatisfyOp( SatisfyOp::EQ, TACTICAL_SPOT_RADIUS );
	plannerNode.WorldState().ResetTacticalSpots();

	// Satisfy conditions for BotKillEnemyGoal
	plannerNode.WorldState().CanHitEnemyVar().SetValue( true );
	plannerNode.WorldState().HasPositionalAdvantageVar().SetValue( true );

	return plannerNode.PrepareActionResult();
}

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
	plannerNode.WorldState().BotOriginVar().SetSatisfyOp( SatisfyOp::EQ, TACTICAL_SPOT_RADIUS );
	plannerNode.WorldState().ResetTacticalSpots();
	// Satisfy conditions for BotKillEnemyGoal
	plannerNode.WorldState().CanHitEnemyVar().SetValue( true ).SetIgnore( false );
	plannerNode.WorldState().HasPositionalAdvantageVar().SetValue( true ).SetIgnore( false );
	// Otherwise an another identical world state might be yield leading to the planner logic violation
	unsigned similarWorldStateInstanceId = self->ai->botRef->NextSimilarWorldStateInstanceId();
	plannerNode.WorldState().SimilarWorldStateInstanceIdVar().SetValue( similarWorldStateInstanceId ).SetIgnore( false );

	return plannerNode.PrepareActionResult();
}

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
	plannerNode.WorldState().NavTargetOriginVar().SetValue( navTargetOrigin );
	plannerNode.WorldState().NavTargetOriginVar().SetSatisfyOp( SatisfyOp::EQ, TACTICAL_SPOT_RADIUS );
	plannerNode.WorldState().NavTargetOriginVar().SetIgnore( false );
	// Setting this is required to satisfy the BotKillEnemyAction preconditions
	// (even they are not really met from human point of view).
	plannerNode.WorldState().HasPositionalAdvantageVar().SetValue( true ).SetIgnore( false );

	return plannerNode.PrepareActionResult();
}

void BotAttackAdvancingToTargetActionRecord::Activate() {
	AiBaseActionRecord::Activate();
	this->navTarget.SetToNavEntity( self->ai->botRef->GetSelectedNavEntity().GetNavEntity() );
	self->ai->botRef->SetNavTarget( &this->navTarget );
	self->ai->botRef->GetMiscTactics().Clear();
	self->ai->botRef->GetMiscTactics().PreferAttackRatherThanRun();
	// This flag affects weapons choice. This action is very likely to behave similar to retreating.
	self->ai->botRef->GetMiscTactics().willRetreat = true;
}

void BotAttackAdvancingToTargetActionRecord::Deactivate() {
	self->ai->botRef->GetMiscTactics().Clear();
	self->ai->botRef->ResetNavTarget();
	AiBaseActionRecord::Deactivate();
}

AiBaseActionRecord::Status BotAttackAdvancingToTargetActionRecord::CheckStatus( const WorldState &currWorldState ) const {
	const auto &selectedNavEntity = self->ai->botRef->GetSelectedNavEntity();
	if( !selectedNavEntity.IsValid() || selectedNavEntity.IsEmpty() ) {
		Debug( "The currently selected nav entity is invalid or is empty\n" );
		return INVALID;
	}

	const auto &selectedEnemies = self->ai->botRef->GetSelectedEnemies();
	if( !selectedEnemies.AreValid() || selectedEnemies.InstanceId() != selectedEnemiesInstanceId ) {
		Debug( "The currently selected enemies are not valid or have been updated\n" );
		return INVALID;
	}

	if( currWorldState.HasJustPickedGoalItemVar() ) {
		Debug( "The bot has just picked a goal item, should deactivate\n" );
		return COMPLETED;
	}

	return VALID;
}

PlannerNode *BotAttackAdvancingToTargetAction::TryApply( const WorldState &worldState ) {
	if( worldState.BotOriginVar().Ignore() ) {
		Debug( "Bot origin is ignored in the given world state\n" );
		return nullptr;
	}

	// Prevent excessive fruitless branching
	constexpr float distanceError = WorldState::OriginVar::MAX_ROUNDING_SQUARE_DISTANCE_ERROR;
	if( worldState.BotOriginVar().Value().SquareDistanceTo( self->s.origin ) > distanceError ) {
		Debug( "This action is applicable only for the real bot origin\n" );
		return nullptr;
	}
	if( worldState.EnemyOriginVar().Ignore() ) {
		Debug( "Enemy is ignored in the given world state\n" );
		return nullptr;
	}

	if( worldState.HealthVar().Ignore() || worldState.ArmorVar().Ignore() ) {
		Debug( "Health or armor are ignored in the given world state\n" );
		return nullptr;
	}

	if( worldState.NavTargetOriginVar().Ignore() ) {
		Debug( "Nav target is ignored in the given world state\n" );
		return nullptr;
	}

	if( worldState.CanHitEnemyVar().Ignore() ) {
		Debug( "Can bot hit enemy is ignored in the given world state\n" );
		return nullptr;
	}
	if( !worldState.CanHitEnemyVar() ) {
		Debug( "Bot can't hit enemy in the given world state\n" );
		return nullptr;
	}

	const float offensiveness = self->ai->botRef->GetEffectiveOffensiveness();
	float actionPenalty = 0.5f + 1.0f * offensiveness;

	const Vec3 botOrigin( worldState.BotOriginVar().Value() );
	const Vec3 navTargetOrigin( worldState.NavTargetOriginVar().Value() );
	int travelTimeMillis = self->ai->botRef->CheckTravelTimeMillis( botOrigin, navTargetOrigin );
	if( !travelTimeMillis ) {
		Debug( "Can't find a travel time from bot origin to the nav target origin\n" );
		return nullptr;
	}

	PlannerNodePtr plannerNode = NewNodeForRecord( pool.New( self, self->ai->botRef->GetSelectedEnemies().InstanceId() ) );
	if( !plannerNode ) {
		Debug( "Can't allocate planner node\n" );
		return nullptr;
	}

	// TODO: We need much more sophisticated cost calculations/applicability checks for this action!
	plannerNode.Cost() = travelTimeMillis * actionPenalty;

	plannerNode.WorldState() = worldState;
	// It is unlikely that bot is going to really have a positional advantage,
	// but that's what the terminal KillEnemy action expects
	plannerNode.WorldState().HasPositionalAdvantageVar().SetValue( true ).SetIgnore( false );
	unsigned similarWorldStateInstanceId = self->ai->botRef->NextSimilarWorldStateInstanceId();
	plannerNode.WorldState().SimilarWorldStateInstanceIdVar().SetValue( similarWorldStateInstanceId ).SetIgnore( false );

	plannerNode.WorldState().BotOriginVar().SetValue( navTargetOrigin ).SetIgnore( false );

	return plannerNode.ReleaseOwnership();
}

PlannerNode *BotKillEnemyAction::TryApply( const WorldState &worldState ) {
	if( worldState.EnemyOriginVar().Ignore() ) {
		Debug( "Enemy is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.HealthVar().Ignore() || worldState.ArmorVar().Ignore() ) {
		Debug( "Health or armor are ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.HasPositionalAdvantageVar().Ignore() ) {
		Debug( "Has bot positional advantage is ignored in the given world state\n" );
		return nullptr;
	}
	if( !worldState.HasPositionalAdvantageVar() ) {
		Debug( "Bot does not have positional advantage in the given world state\n" );
		return nullptr;
	}
	if( worldState.CanHitEnemyVar().Ignore() ) {
		Debug( "Can bot hit enemy is ignored in the given world state\n" );
		return nullptr;
	}
	if( !worldState.CanHitEnemyVar() ) {
		Debug( "Bot can't hit enemy in the given world state\n" );
		return nullptr;
	}

	PlannerNodePtr plannerNode = NewNodeForRecord( pool.New( self ) );
	if( !plannerNode ) {
		Debug( "Can't allocate planner node\n" );
		return nullptr;
	}

	// Set low dummy cost
	plannerNode.Cost() = 1.0f;

	plannerNode.WorldState() = worldState;

	plannerNode.WorldState().HasJustKilledEnemyVar().SetValue( true ).SetIgnore( false );

	unsigned similarWorldStateInstanceId = self->ai->botRef->NextSimilarWorldStateInstanceId();
	plannerNode.WorldState().SimilarWorldStateInstanceIdVar().SetValue( similarWorldStateInstanceId ).SetIgnore( false );

	return plannerNode.PrepareActionResult();
}

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

void BotGenericRunAvoidingCombatActionRecord::Activate() {
	BotBaseActionRecord::Activate();
	self->ai->botRef->GetMiscTactics().PreferAttackRatherThanRun();
	self->ai->botRef->SetNavTarget( &navTarget );
}

void BotGenericRunAvoidingCombatActionRecord::Deactivate() {
	BotBaseActionRecord::Deactivate();
	self->ai->botRef->ResetNavTarget();
}

AiBaseActionRecord::Status BotGenericRunAvoidingCombatActionRecord::CheckStatus( const WorldState &currWorldState ) const {
	// It really gets invalidated on goal reevaluation

	if( ( navTarget.Origin() - self->s.origin ).LengthFast() <= GOAL_PICKUP_ACTION_RADIUS ) {
		return COMPLETED;
	}

	return VALID;
}

PlannerNode *BotGenericRunAvoidingCombatAction::TryApply( const WorldState &worldState ) {
	if( worldState.NavTargetOriginVar().Ignore() ) {
		Debug( "Nav target is absent in the given world state\n" );
		return nullptr;
	}
	if( worldState.DistanceToNavTarget() <= GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Bot is too close to the nav target\n" );
		return nullptr;
	}

	const Vec3 navTargetOrigin = worldState.NavTargetOriginVar().Value();
	const SelectedNavEntity &selectedNavEntity = self->ai->botRef->GetSelectedNavEntity();
	if( selectedNavEntity.IsValid() && !selectedNavEntity.IsEmpty() ) {
		const Vec3 navEntityOrigin = selectedNavEntity.GetNavEntity()->Origin();
		constexpr float squareDistanceError = WorldState::OriginVar::MAX_ROUNDING_SQUARE_DISTANCE_ERROR;
		if( ( navEntityOrigin - navTargetOrigin ).SquaredLength() < squareDistanceError ) {
			Debug( "Action is not applicable for goal entities (there are specialized actions for these kinds of nav target\n" );
			return nullptr;
		}
	}

	// As a contrary to combat actions, illegal travel time (when the destination is not reachable for AAS) is allowed.
	// Combat actions require simple kinds of movement to keep crosshair on enemy.
	// Thus tactical spot should be reachable in common way for combat actions.
	// In case of retreating, some other kinds of movement AAS is not aware of might be used.
	int travelTimeMillis = self->ai->botRef->CheckTravelTimeMillis( worldState.BotOriginVar().Value(), navTargetOrigin );
	// If the travel time is 0, set it to maximum allowed AAS travel time
	// (AAS stores time as seconds^-2 in a short value)
	if( !travelTimeMillis ) {
		travelTimeMillis = 10 * std::numeric_limits<short>::max();
	}

	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( self, navTargetOrigin ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode.Cost() = travelTimeMillis;

	plannerNode.WorldState() = worldState;
	// Move bot origin
	plannerNode.WorldState().BotOriginVar().SetValue( navTargetOrigin );
	plannerNode.WorldState().BotOriginVar().SetSatisfyOp( SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
	plannerNode.WorldState().BotOriginVar().SetIgnore( false );
	// Since bot origin has been moved, tactical spots should be recomputed
	plannerNode.WorldState().ResetTacticalSpots();

	return plannerNode.PrepareActionResult();
}

PlannerNode *BotStartGotoCoverAction::TryApply( const WorldState &worldState ) {
	if( !CheckCommonRunAwayPreconditions( worldState ) ) {
		return nullptr;
	}

	if( !worldState.PendingOriginVar().Ignore() ) {
		Debug( "Pending origin is already present in the given world state\n" );
		return nullptr;
	}
#ifdef _DEBUG
	// Sanity check
	if( !worldState.HasPendingCoverSpotVar().Ignore() && worldState.HasPendingCoverSpotVar() ) {
		worldState.DebugPrint( "Given WS" );
		AI_FailWith( this->name, "Pending cover spot is already present in the given world state\n" );
	}
#endif

	if( worldState.CoverSpotVar().Ignore() ) {
		Debug( "Cover spot is ignored in the given world state\n" );
		return nullptr;
	}
	if( !worldState.CoverSpotVar().IsPresent() ) {
		Debug( "Cover spot is absent in the given world state\n" );
		return nullptr;
	}

	Vec3 spotOrigin = worldState.CoverSpotVar().Value();
	PlannerNodePtr plannerNode = NewNodeForRecord( pool.New( self ) );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode.Cost() = 1.0f;

	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().HasPendingCoverSpotVar().SetValue( true ).SetIgnore( false );
	// Set nav target to the tactical spot
	plannerNode.WorldState().NavTargetOriginVar().SetValue( spotOrigin );
	plannerNode.WorldState().NavTargetOriginVar().SetSatisfyOp( SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
	plannerNode.WorldState().NavTargetOriginVar().SetIgnore( false );
	// Set pending origin to the tactical spot
	plannerNode.WorldState().PendingOriginVar().SetValue( spotOrigin );
	plannerNode.WorldState().PendingOriginVar().SetSatisfyOp( SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
	plannerNode.WorldState().PendingOriginVar().SetIgnore( false );

	unsigned similarWorldStateInstanceId = self->ai->botRef->NextSimilarWorldStateInstanceId();
	plannerNode.WorldState().SimilarWorldStateInstanceIdVar().SetValue( similarWorldStateInstanceId ).SetIgnore( false );

	return plannerNode.PrepareActionResult();
}

void BotTakeCoverActionRecord::Activate() {
	BotBaseActionRecord::Activate();
	// Since bot should be already close to the nav target, give (a defencive) aiming a higher priority
	self->ai->botRef->GetMiscTactics().PreferAttackRatherThanRun();
	self->ai->botRef->SetNavTarget( &navTarget );
}

void BotTakeCoverActionRecord::Deactivate() {
	BotBaseActionRecord::Deactivate();
	self->ai->botRef->ResetNavTarget();
}

AiBaseActionRecord::Status BotTakeCoverActionRecord::CheckStatus( const WorldState &currWorldState ) const {
	static_assert( GOAL_PICKUP_ACTION_RADIUS > TACTICAL_SPOT_RADIUS, "" );

	if( selectedEnemiesInstanceId != self->ai->botRef->GetSelectedEnemies().InstanceId() ) {
		Debug( "New enemies have been selected\n" );
		return INVALID;
	}

	float distanceToActionNavTarget = ( navTarget.Origin() - self->s.origin ).SquaredLength();
	if( distanceToActionNavTarget > GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Bot is too far from nav target\n" );
		return INVALID;
	}

	return ( distanceToActionNavTarget < TACTICAL_SPOT_RADIUS ) ? COMPLETED : VALID;
}

PlannerNode *BotTakeCoverAction::TryApply( const WorldState &worldState ) {
	if( !CheckCommonRunAwayPreconditions( worldState ) ) {
		return nullptr;
	}

	if( worldState.NavTargetOriginVar().Ignore() ) {
		Debug( "Nav target is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.HasPendingCoverSpotVar().Ignore() || !worldState.HasPendingCoverSpotVar() ) {
		Debug( "Has bot pending cover spot is ignored or absent in the given world state\n" );
		return nullptr;
	}

	const Vec3 navTargetOrigin = worldState.NavTargetOriginVar().Value();

#ifdef _DEBUG
	// Sanity check
	if( worldState.PendingOriginVar().Ignore() ) {
		worldState.DebugPrint( "Given WS" );
		AI_FailWith( "BotTakeCoverAction", "PendingOriginVar() is ignored in the given world state\n" );
	}

	constexpr float distanceError = WorldState::OriginVar::MAX_ROUNDING_SQUARE_DISTANCE_ERROR;
	if( ( worldState.PendingOriginVar().Value() - navTargetOrigin ).SquaredLength() > distanceError ) {
		worldState.DebugPrint( "Given WS" );
		AI_FailWith( "BotTakeCoverAction", "PendingOrigin and NavTargetOrigin differ in the given world state\n" );
	}
#endif

	if( worldState.DistanceToNavTarget() > GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Bot is too far from the nav target (pending cover spot)\n" );
		return nullptr;
	}

	unsigned selectedEnemiesInstanceId = self->ai->botRef->GetSelectedEnemies().InstanceId();
	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( self, navTargetOrigin, selectedEnemiesInstanceId ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode.Cost() = 1.0f;

	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().HasPendingCoverSpotVar().SetIgnore( true );
	plannerNode.WorldState().PendingOriginVar().SetIgnore( true );
	// Bot origin var remains the same (it is close to nav target)
	plannerNode.WorldState().IsRunningAwayVar().SetValue( true ).SetIgnore( false );
	plannerNode.WorldState().CanHitEnemyVar().SetValue( false ).SetIgnore( false );
	plannerNode.WorldState().EnemyCanHitVar().SetValue( false ).SetIgnore( false );

	return plannerNode.PrepareActionResult();
}

PlannerNode *BotStartGotoRunAwayTeleportAction::TryApply( const WorldState &worldState ) {
	if( !CheckCommonRunAwayPreconditions( worldState ) ) {
		return nullptr;
	}

	if( worldState.HasJustTeleportedVar().Ignore() ) {
		Debug( "Has bot just teleported is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.HasJustTeleportedVar() ) {
		Debug( "Bot has just teleported in the given world state. Avoid chain teleportations\n" );
		return nullptr;
	}
	if( !worldState.PendingOriginVar().Ignore() ) {
		Debug( "The pending origin is already present in the given world state\n" );
		return nullptr;
	}
	if( worldState.RunAwayTeleportOriginVar().IgnoreOrAbsent() ) {
		Debug( "A teleport for running away is ignored or absent in the given world state\n" );
		return nullptr;
	}

	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( self ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode.Cost() = 1.0f;

	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().HasPendingRunAwayTeleportVar().SetValue( true ).SetIgnore( false );
	// Set nav target to the teleport origin
	plannerNode.WorldState().NavTargetOriginVar().SetValue( worldState.RunAwayTeleportOriginVar().Value() );
	plannerNode.WorldState().NavTargetOriginVar().SetSatisfyOp( SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
	plannerNode.WorldState().NavTargetOriginVar().SetIgnore( false );
	// Set pending origin to the teleport destination
	plannerNode.WorldState().PendingOriginVar().SetValue( worldState.RunAwayTeleportOriginVar().Value2() );
	plannerNode.WorldState().PendingOriginVar().SetSatisfyOp( SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
	plannerNode.WorldState().PendingOriginVar().SetIgnore( false );

	return plannerNode.PrepareActionResult();
}

void BotDoRunAwayViaTeleportActionRecord::Activate() {
	BotBaseActionRecord::Activate();
	self->ai->botRef->SetNavTarget( &navTarget );
}

void BotDoRunAwayViaTeleportActionRecord::Deactivate() {
	BotBaseActionRecord::Deactivate();
	self->ai->botRef->ResetNavTarget();
}

PlannerNode *BotDoRunAwayViaTeleportAction::TryApply( const WorldState &worldState ) {
	if( !CheckCommonRunAwayPreconditions( worldState ) ) {
		return nullptr;
	}

	if( !worldState.HasJustTeleportedVar().Ignore() && worldState.HasJustTeleportedVar() ) {
		Debug( "Bot has just teleported in the given world state. Avoid chain teleportations\n" );
		return nullptr;
	}
	if( worldState.HasPendingRunAwayTeleportVar().Ignore() || !worldState.HasPendingRunAwayTeleportVar() ) {
		Debug( "Has bot a pending teleport for running away is ignored or absent in the given world state\n" );
		return nullptr;
	}

#ifdef _DEBUG
	// Sanity check
	if( worldState.NavTargetOriginVar().Ignore() ) {
		worldState.DebugPrint( "Goal WS" );
		AI_FailWith( this->name, "Nav target origin is ignored in the given world state\n" );
	}
	if( worldState.PendingOriginVar().Ignore() ) {
		worldState.DebugPrint( "Goal WS" );
		AI_FailWith( this->name, "Pending origin is ignored in the given world state\n" );
	}
#endif

	if( worldState.DistanceToNavTarget() > GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Bot is too far from the nav target (teleport origin)\n" );
		return nullptr;
	}

	Vec3 teleportOrigin = worldState.NavTargetOriginVar().Value();
	const auto &selectedEnemies = self->ai->botRef->GetSelectedEnemies();
	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( self, teleportOrigin, selectedEnemies.InstanceId() ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	// Teleportation costs almost nothing
	plannerNode.Cost() = 1;

	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().HasJustTeleportedVar().SetValue( false ).SetIgnore( false );
	// Set bot origin to the teleport destination
	plannerNode.WorldState().BotOriginVar().SetValue( worldState.PendingOriginVar().Value() );
	plannerNode.WorldState().BotOriginVar().SetSatisfyOp( SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
	// Reset pending origin
	plannerNode.WorldState().PendingOriginVar().SetIgnore( true );
	plannerNode.WorldState().HasPendingRunAwayTeleportVar().SetIgnore( true );
	// Tactical spots should be recomputed after teleportation
	plannerNode.WorldState().ResetTacticalSpots();

	plannerNode.WorldState().IsRunningAwayVar().SetValue( true ).SetIgnore( false );
	plannerNode.WorldState().CanHitEnemyVar().SetValue( false ).SetIgnore( false );
	plannerNode.WorldState().EnemyCanHitVar().SetValue( false ).SetIgnore( false );

	return plannerNode.PrepareActionResult();
}

AiBaseActionRecord::Status BotDoRunAwayViaTeleportActionRecord::CheckStatus( const WorldState &currWorldState ) const {
	if( currWorldState.HasJustTeleportedVar().Ignore() ) {
		Debug( "Has bot just teleported is ignored\n" );
		return INVALID;
	}
	if( currWorldState.HasJustTeleportedVar() ) {
		return COMPLETED;
	}

	if( currWorldState.HasThreateningEnemyVar().Ignore() ) {
		Debug( "A threatening enemy is ignored\n" );
		return INVALID;
	}
	if( !currWorldState.HasThreateningEnemyVar() ) {
		Debug( "A threatening enemy is absent\n" );
		return INVALID;
	}
	if( selectedEnemiesInstanceId != self->ai->botRef->GetSelectedEnemies().InstanceId() ) {
		Debug( "New enemies have been selected\n" );
		return INVALID;
	}
	// Use the same radius as for goal items pickups
	// (running actions for picking up an item and running away might be shared)
	if( ( navTarget.Origin() - self->s.origin ).SquaredLength() > GOAL_PICKUP_ACTION_RADIUS * GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Bot is too far from the teleport trigger\n" );
		return INVALID;
	}

	return VALID;
}

PlannerNode *BotStartGotoRunAwayJumppadAction::TryApply( const WorldState &worldState ) {
	if( !CheckCommonRunAwayPreconditions( worldState ) ) {
		return nullptr;
	}

	if( !worldState.HasJustTouchedJumppadVar().Ignore() && worldState.HasJustTouchedJumppadVar() ) {
		Debug( "Bot has just touched the jumppad in the given world state\n" );
		return nullptr;
	}
	if( !worldState.PendingOriginVar().Ignore() ) {
		Debug( "The pending origin is already present in the given world state\n" );
		return nullptr;
	}
	if( worldState.RunAwayJumppadOriginVar().IgnoreOrAbsent() ) {
		Debug( "A jumppad for running away is ignored or absent in the given world state\n" );
		return nullptr;
	}

	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( self ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode.Cost() = 1.0f;

	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().HasPendingRunAwayJumppadVar().SetValue( true ).SetIgnore( false );
	// Set nav target to the jumppad origin
	plannerNode.WorldState().NavTargetOriginVar().SetValue( worldState.RunAwayJumppadOriginVar().Value() );
	plannerNode.WorldState().NavTargetOriginVar().SetSatisfyOp( SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
	plannerNode.WorldState().NavTargetOriginVar().SetIgnore( false );
	// Set pending origin to the jumppad destination
	plannerNode.WorldState().PendingOriginVar().SetValue( worldState.RunAwayJumppadOriginVar().Value2() );
	plannerNode.WorldState().PendingOriginVar().SetSatisfyOp( SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
	plannerNode.WorldState().PendingOriginVar().SetIgnore( false );

	return plannerNode.PrepareActionResult();
}

void BotDoRunAwayViaJumppadActionRecord::Activate() {
	BotBaseActionRecord::Activate();
	self->ai->botRef->SetNavTarget( &navTarget );
}

void BotDoRunAwayViaJumppadActionRecord::Deactivate() {
	BotBaseActionRecord::Deactivate();
	self->ai->botRef->ResetNavTarget();
}

AiBaseActionRecord::Status BotDoRunAwayViaJumppadActionRecord::CheckStatus( const WorldState &currWorldState ) const {
	if( currWorldState.HasJustTouchedJumppadVar().Ignore() ) {
		Debug( "Has just touched jumppad is ignored\n" );
		return INVALID;
	}
	if( currWorldState.HasJustTouchedJumppadVar() ) {
		return COMPLETED;
	}

	if( currWorldState.HasThreateningEnemyVar().Ignore() ) {
		Debug( "A threatening enemy is ignored\n" );
		return INVALID;
	}
	if( !currWorldState.HasThreateningEnemyVar() ) {
		Debug( "A threatening enemy is absent\n" );
		return INVALID;
	}
	if( selectedEnemiesInstanceId != self->ai->botRef->GetSelectedEnemies().InstanceId() ) {
		Debug( "New enemies have been selected\n" );
		return INVALID;
	}
	// Use the same radius as for goal items pickups
	// (running actions for picking up an item and running away might be shared)
	if( ( navTarget.Origin() - self->s.origin ).SquaredLength() > GOAL_PICKUP_ACTION_RADIUS * GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Bot is too far from the jumppad trigger\n" );
		return INVALID;
	}

	return VALID;
}

PlannerNode *BotDoRunAwayViaJumppadAction::TryApply( const WorldState &worldState ) {
	if( !CheckCommonRunAwayPreconditions( worldState ) ) {
		return nullptr;
	}

	if( !worldState.HasJustTouchedJumppadVar().Ignore() && worldState.HasJustTouchedJumppadVar() ) {
		Debug( "Has bot just touched a jumppad is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.HasPendingRunAwayJumppadVar().Ignore() || !worldState.HasPendingRunAwayJumppadVar() ) {
		Debug( "Has bot a pending jumppad for running away is ignored or absent in the given world state\n" );
		return nullptr;
	}

#ifdef _DEBUG
	// Sanity check
	if( worldState.NavTargetOriginVar().Ignore() ) {
		worldState.DebugPrint( "Goal WS" );
		AI_FailWith( this->name, "Nav target origin is ignored in the given world state\n" );
	}
	if( worldState.PendingOriginVar().Ignore() ) {
		worldState.DebugPrint( "Goal WS" );
		AI_FailWith( this->name, "Pending origin is ignored in the given world state\n" );
	}
#endif

	if( worldState.DistanceToNavTarget() > GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Bot is too far from the nav target (jumppad origin)" );
		return nullptr;
	}

	Vec3 jumppadOrigin = worldState.NavTargetOriginVar().Value();
	unsigned selectedEnemiesInstanceId = self->ai->botRef->GetSelectedEnemies().InstanceId();
	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( self, jumppadOrigin, selectedEnemiesInstanceId ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	// Use distance from jumppad origin to target as an estimation for travel time millis
	plannerNode.Cost() = ( jumppadOrigin - worldState.PendingOriginVar().Value() ).LengthFast();

	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().HasJustTouchedJumppadVar().SetValue( true ).SetIgnore( false );
	// Set bot origin to the jumppad destination
	plannerNode.WorldState().BotOriginVar().SetValue( worldState.PendingOriginVar().Value() );
	plannerNode.WorldState().BotOriginVar().SetSatisfyOp( SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
	// Reset pending origin
	plannerNode.WorldState().PendingOriginVar().SetIgnore( true );
	plannerNode.WorldState().HasPendingRunAwayJumppadVar().SetIgnore( true );
	// Tactical spots should be recomputed for the new bot origin
	plannerNode.WorldState().ResetTacticalSpots();

	plannerNode.WorldState().IsRunningAwayVar().SetValue( true ).SetIgnore( false );
	plannerNode.WorldState().CanHitEnemyVar().SetValue( false ).SetIgnore( false );
	plannerNode.WorldState().EnemyCanHitVar().SetValue( false ).SetIgnore( false );

	return plannerNode.PrepareActionResult();
}

PlannerNode *BotStartGotoRunAwayElevatorAction::TryApply( const WorldState &worldState ) {
	if( !CheckCommonRunAwayPreconditions( worldState ) ) {
		return nullptr;
	}

	if( worldState.HasJustEnteredElevatorVar().Ignore() ) {
		Debug( "Has bot just entered an elevator is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.HasJustEnteredElevatorVar() ) {
		Debug( "Bot has just entered an elevator in the given world state\n" );
		return nullptr;
	}
	if( !worldState.PendingOriginVar().Ignore() ) {
		Debug( "Pending origin is already present in the given world state\n" );
		return nullptr;
	}
	if( worldState.RunAwayElevatorOriginVar().IgnoreOrAbsent() ) {
		Debug( "An elevator for running away is ignored or absent in the given world state\n" );
		return nullptr;
	}

	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( self ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode.Cost() = 1.0f;

	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().HasPendingRunAwayElevatorVar().SetValue( true ).SetIgnore( false );
	// Set nav target to the elevator origin
	plannerNode.WorldState().NavTargetOriginVar().SetValue( worldState.RunAwayElevatorOriginVar().Value() );
	plannerNode.WorldState().NavTargetOriginVar().SetSatisfyOp( SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
	plannerNode.WorldState().NavTargetOriginVar().SetIgnore( false );
	// Set pending origin to the elevator destination
	plannerNode.WorldState().PendingOriginVar().SetValue( worldState.RunAwayElevatorOriginVar().Value2() );
	plannerNode.WorldState().PendingOriginVar().SetSatisfyOp( SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
	plannerNode.WorldState().PendingOriginVar().SetIgnore( false );

	return plannerNode.PrepareActionResult();
}

void BotDoRunAwayViaElevatorActionRecord::Activate() {
	BotBaseActionRecord::Activate();
	self->ai->botRef->SetNavTarget( &navTarget );
}

void BotDoRunAwayViaElevatorActionRecord::Deactivate() {
	BotBaseActionRecord::Deactivate();
	self->ai->botRef->ResetNavTarget();
}

AiBaseActionRecord::Status BotDoRunAwayViaElevatorActionRecord::CheckStatus( const WorldState &currWorldState ) const {
	// Checking of this action record differs from other run away action record.
	// We want the bot to stand on a platform until it finishes its movement.

	// We do not want to invalidate an action due to being a bit in air above the platform, don't check self->groundentity
	trace_t selfTrace;
	AiGroundTraceCache::Instance()->GetGroundTrace( self, 64.0f, &selfTrace );

	if( selfTrace.fraction == 1.0f ) {
		Debug( "Bot is too high above the ground (if any)\n" );
		return INVALID;
	}
	if( selfTrace.ent <= gs.maxclients || game.edicts[selfTrace.ent].use != Use_Plat ) {
		Debug( "Bot is not above a platform\n" );
		return INVALID;
	}

	// If there are no valid enemies, just keep standing on the platform
	const auto &selectedEnemies = self->ai->botRef->GetSelectedEnemies();
	if( selectedEnemies.AreValid() ) {
		trace_t enemyTrace;
		AiGroundTraceCache::Instance()->GetGroundTrace( selectedEnemies.Ent(), 128.0f, &enemyTrace );
		if( enemyTrace.fraction != 1.0f && enemyTrace.ent == selfTrace.ent ) {
			Debug( "Enemy is on the same platform!\n" );
			return INVALID;
		}
	}

	if( game.edicts[selfTrace.ent].moveinfo.state == STATE_TOP ) {
		return COMPLETED;
	}

	return VALID;
}

PlannerNode *BotDoRunAwayViaElevatorAction::TryApply( const WorldState &worldState ) {
	if( !CheckCommonRunAwayPreconditions( worldState ) ) {
		return nullptr;
	}

	if( !worldState.HasJustEnteredElevatorVar().Ignore() && worldState.HasJustEnteredElevatorVar() ) {
		Debug( "Bot has just entered elevator in the given world state\n" );
		return nullptr;
	}
	if( worldState.HasPendingRunAwayElevatorVar().Ignore() || !worldState.HasPendingRunAwayElevatorVar() ) {
		Debug( "Has bot a pending elevator for running away is ignored or absent in the given world state\n" );
		return nullptr;
	}

#ifdef _DEBUG
	// Sanity check
	if( worldState.NavTargetOriginVar().Ignore() ) {
		worldState.DebugPrint( "Given WS" );
		AI_FailWith( this->name, "Nav target origin is ignored in the given world state\n" );
	}
	if( worldState.PendingOriginVar().Ignore() ) {
		worldState.DebugPrint( "Given WS" );
		AI_FailWith( this->name, "Pending origin is ignored in the given world state\n" );
	}
#endif

	if( worldState.DistanceToNavTarget() > GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Bot is too far from the nav target (elevator origin)\n" );
		return nullptr;
	}

	Vec3 elevatorOrigin = worldState.NavTargetOriginVar().Value();
	unsigned selectedEnemiesInstanceId = self->ai->botRef->GetSelectedEnemies().InstanceId();
	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( self, elevatorOrigin, selectedEnemiesInstanceId ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	float elevatorDistance = ( elevatorOrigin - worldState.PendingOriginVar().Value() ).LengthFast();
	// Assume that elevator speed is 400 units per second
	float speedInUnitsPerMillis = 400 / 1000.0f;
	plannerNode.Cost() = elevatorDistance / speedInUnitsPerMillis;

	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().HasJustEnteredElevatorVar().SetValue( true ).SetIgnore( false );
	// Set bot origin to the elevator destination
	plannerNode.WorldState().BotOriginVar().SetValue( worldState.PendingOriginVar().Value() );
	plannerNode.WorldState().BotOriginVar().SetSatisfyOp( SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
	// Reset pending origin
	plannerNode.WorldState().PendingOriginVar().SetIgnore( true );
	plannerNode.WorldState().HasPendingRunAwayElevatorVar().SetIgnore( true );
	// Tactical spots should be recomputed for the new bot origin
	plannerNode.WorldState().ResetTacticalSpots();

	plannerNode.WorldState().IsRunningAwayVar().SetValue( true ).SetIgnore( false );
	plannerNode.WorldState().CanHitEnemyVar().SetValue( false ).SetIgnore( false );
	plannerNode.WorldState().EnemyCanHitVar().SetValue( false ).SetIgnore( false );

	return plannerNode.PrepareActionResult();
}

PlannerNode *BotStopRunningAwayAction::TryApply( const WorldState &worldState ) {
	if( worldState.IsRunningAwayVar().Ignore() ) {
		Debug( "Is bot running away is ignored in the given world state\n" );
		return nullptr;
	}
	if( !worldState.IsRunningAwayVar() ) {
		Debug( "Bot is not running away in the given world state\n" );
		return nullptr;
	}
	if( !worldState.HasRunAwayVar().Ignore() && worldState.HasRunAwayVar() ) {
		Debug( "Bot has already run away in the given world state\n" );
		return nullptr;
	}

	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( self ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode.Cost() = 1.0f;

	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().HasJustTeleportedVar().SetIgnore( true );
	plannerNode.WorldState().HasJustTouchedJumppadVar().SetIgnore( true );
	plannerNode.WorldState().HasJustEnteredElevatorVar().SetIgnore( true );

	plannerNode.WorldState().IsRunningAwayVar().SetValue( false ).SetIgnore( false );
	plannerNode.WorldState().HasRunAwayVar().SetValue( true ).SetIgnore( false );

	unsigned similarWorldStateInstanceId = self->ai->botRef->NextSimilarWorldStateInstanceId();
	plannerNode.WorldState().SimilarWorldStateInstanceIdVar().SetValue( similarWorldStateInstanceId ).SetIgnore( false );

	return plannerNode.PrepareActionResult();
}

void BotDodgeToSpotActionRecord::Activate() {
	BotBaseActionRecord::Activate();
	self->ai->botRef->SetNavTarget( &navTarget );
	timeoutAt = level.time + Danger::TIMEOUT;
	self->ai->botRef->GetMiscTactics().PreferAttackRatherThanRun();
}

void BotDodgeToSpotActionRecord::Deactivate() {
	BotBaseActionRecord::Deactivate();
	self->ai->botRef->ResetNavTarget();
}

AiBaseActionRecord::Status BotDodgeToSpotActionRecord::CheckStatus( const WorldState &currWorldState ) const {
	// If the bot has reached the spot, consider the action completed
	// (use a low threshold because dodging is a precise movement)
	if( ( navTarget.Origin() - self->s.origin ).SquaredLength() < 16 * 16 ) {
		return COMPLETED;
	}

	// Return INVALID if has not reached the spot when the action timed out
	return timeoutAt > level.time ? VALID : INVALID;
}

PlannerNode *BotDodgeToSpotAction::TryApply( const WorldState &worldState ) {
	if( worldState.PotentialDangerDamageVar().Ignore() ) {
		Debug( "Potential danger damage is ignored in the given world state\n" );
		return nullptr;
	}

#ifndef _DEBUG
	// Sanity check
	if( worldState.DangerHitPointVar().Ignore() ) {
		AI_FailWith( "BotDodgeToSpotAction::TryApply()", "Danger hit point is ignored in the given world state\n" );
	}
	if( worldState.DangerDirectionVar().Ignore() ) {
		AI_FailWith( "BotDodgeToSpotAction::TryApply()", "Danger direction is ignored in the given world state\n" );
	}
#endif

	constexpr float squareDistanceError = WorldState::OriginVar::MAX_ROUNDING_SQUARE_DISTANCE_ERROR;
	if( ( worldState.BotOriginVar().Value() - self->s.origin ).SquaredLength() > squareDistanceError ) {
		Debug( "The action can be applied only to the current bot origin\n" );
		return nullptr;
	}

	if( worldState.DodgeDangerSpotVar().Ignore() ) {
		Debug( "Spot for dodging a danger is ignored in the given world state, can't dodge\n" );
		return nullptr;
	}

	const Vec3 spotOrigin = worldState.DodgeDangerSpotVar().Value();
	int travelTimeMillis = self->ai->botRef->CheckTravelTimeMillis( worldState.BotOriginVar().Value(), spotOrigin );
	if( !travelTimeMillis ) {
		Debug( "Warning: can't find travel time from the bot origin to the spot origin in the given world state\n" );
		return nullptr;
	}

	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( self, spotOrigin ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode.Cost() = travelTimeMillis;
	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().BotOriginVar().SetValue( spotOrigin );
	plannerNode.WorldState().PotentialDangerDamageVar().SetIgnore( true );
	plannerNode.WorldState().DangerHitPointVar().SetIgnore( true );
	plannerNode.WorldState().DangerDirectionVar().SetIgnore( true );
	plannerNode.WorldState().HasReactedToDangerVar().SetIgnore( false ).SetValue( true );

	return plannerNode.PrepareActionResult();
}

void BotTurnToThreatOriginActionRecord::Activate() {
	BotBaseActionRecord::Activate();
	self->ai->botRef->SetPendingLookAtPoint( AiPendingLookAtPoint( threatPossibleOrigin, 3.0f ), 350 );
	self->ai->botRef->GetMiscTactics().PreferAttackRatherThanRun();
}

void BotTurnToThreatOriginActionRecord::Deactivate() {
	BotBaseActionRecord::Deactivate();
	self->ai->botRef->ResetPendingLookAtPoint();
}

AiBaseActionRecord::Status BotTurnToThreatOriginActionRecord::CheckStatus( const WorldState &currWorldState ) const {
	vec3_t lookDir;
	AngleVectors( self->s.angles, lookDir, nullptr, nullptr );

	Vec3 toThreatDir( threatPossibleOrigin );
	toThreatDir -= self->s.origin;
	toThreatDir.NormalizeFast();

	if( toThreatDir.Dot( lookDir ) > self->ai->botRef->FovDotFactor() ) {
		return COMPLETED;
	}

	return self->ai->botRef->HasPendingLookAtPoint() ? VALID : INVALID;
}

PlannerNode *BotTurnToThreatOriginAction::TryApply( const WorldState &worldState ) {
	if( worldState.ThreatPossibleOriginVar().Ignore() ) {
		Debug( "Threat possible origin is ignored in the given world state\n" );
		return nullptr;
	}
	if( !worldState.HasReactedToThreatVar().Ignore() && worldState.HasReactedToThreatVar() ) {
		Debug( "Bot has already reacted to threat in the given world state\n" );
		return nullptr;
	}

	constexpr float squareDistanceError = WorldState::OriginVar::MAX_ROUNDING_SQUARE_DISTANCE_ERROR;
	if( ( worldState.BotOriginVar().Value() - self->s.origin ).SquaredLength() > squareDistanceError ) {
		Debug( "The action can be applied only to the current bot origin\n" );
		return nullptr;
	}

	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( self, worldState.ThreatPossibleOriginVar().Value() ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode.Cost() = 500;
	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().ThreatPossibleOriginVar().SetIgnore( true );
	// If a bot has reacted to threat, he can't hit current enemy (if any)
	plannerNode.WorldState().CanHitEnemyVar().SetValue( false );
	plannerNode.WorldState().HasReactedToThreatVar().SetValue( true ).SetIgnore( false );

	return plannerNode.PrepareActionResult();
}

void BotTurnToLostEnemyActionRecord::Activate() {
	BotBaseActionRecord::Activate();
	self->ai->botRef->SetPendingLookAtPoint( AiPendingLookAtPoint( lastSeenEnemyOrigin, 3.0f ), 400 );
	self->ai->botRef->GetMiscTactics().PreferRunRatherThanAttack();
}

void BotTurnToLostEnemyActionRecord::Deactivate() {
	BotBaseActionRecord::Deactivate();
	self->ai->botRef->ResetPendingLookAtPoint();
}

AiBaseActionRecord::Status BotTurnToLostEnemyActionRecord::CheckStatus( const WorldState &currWorldState ) const {
	vec3_t lookDir;
	AngleVectors( self->s.angles, lookDir, nullptr, nullptr );

	Vec3 toEnemyDir( lastSeenEnemyOrigin );
	toEnemyDir -= self->s.origin;
	toEnemyDir.NormalizeFast();

	if( toEnemyDir.Dot( lookDir ) >= self->ai->botRef->FovDotFactor() ) {
		return COMPLETED;
	}

	return self->ai->botRef->HasPendingLookAtPoint() ? VALID : INVALID;
}

PlannerNode *BotTurnToLostEnemyAction::TryApply( const WorldState &worldState ) {
	if( worldState.IsReactingToEnemyLostVar().Ignore() ) {
		Debug( "Is bot reacting to enemy lost is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.IsReactingToEnemyLostVar() ) {
		Debug( "Bot is already reacting to enemy lost in the given world state\n" );
		return nullptr;
	}
	if( worldState.HasReactedToEnemyLostVar().Ignore() ) {
		Debug( "Has bot reacted to enemy lost is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.HasReactedToEnemyLostVar() ) {
		Debug( "Bot has already reacted to enemy lost in the given world state\n" );
		return nullptr;
	}
	if( worldState.LostEnemyLastSeenOriginVar().Ignore() ) {
		Debug( "Lost enemy origin is ignored in the given world state\n" );
		return nullptr;
	}

	constexpr float squareDistanceError = WorldState::OriginVar::MAX_ROUNDING_SQUARE_DISTANCE_ERROR;
	if( ( worldState.BotOriginVar().Value() - self->s.origin ).SquaredLength() > squareDistanceError ) {
		Debug( "The action can be applied only to the current bot origin\n" );
		return nullptr;
	}
	if( worldState.MightSeeLostEnemyAfterTurnVar().Ignore() ) {
		Debug( "Might bot see lost enemy after turn is ignored in the given world state\n" );
		return nullptr;
	}
	if( !worldState.MightSeeLostEnemyAfterTurnVar() ) {
		Debug( "Bot cannot see lost enemy after turn in the given world state\n" );
		return nullptr;
	}

	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( self, worldState.LostEnemyLastSeenOriginVar().Value() ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode.Cost() = 400.0f;
	plannerNode.WorldState() = worldState;
	// Can't hit current enemy (if any) after applying this action
	plannerNode.WorldState().CanHitEnemyVar().SetValue( false );
	plannerNode.WorldState().HasReactedToEnemyLostVar().SetValue( true );

	return plannerNode.PrepareActionResult();
}

PlannerNode *BotStartLostEnemyPursuitAction::TryApply( const WorldState &worldState ) {
	if( worldState.IsReactingToEnemyLostVar().Ignore() ) {
		Debug( "Is bot reacting to enemy lost is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.IsReactingToEnemyLostVar() ) {
		Debug( "Bot is already reacting to enemy lost in the given world state\n" );
		return nullptr;
	}
	if( worldState.HasReactedToEnemyLostVar().Ignore() ) {
		Debug( "Has bot reacted to enemy lost is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.HasReactedToEnemyLostVar() ) {
		Debug( "Bot has already reacted to enemy lost in the given world state\n" );
		return nullptr;
	}
	if( worldState.LostEnemyLastSeenOriginVar().Ignore() ) {
		Debug( "Lost enemy origin is ignored in the given world state\n" );
		return nullptr;
	}
	constexpr float distanceThreshold = 1.5f * GOAL_PICKUP_ACTION_RADIUS;
	if( worldState.BotOriginVar().DistanceTo( worldState.LostEnemyLastSeenOriginVar() ) < distanceThreshold ) {
		Debug( "Bot is already close to the last seen enemy origin\n" );
		return nullptr;
	}

	constexpr float squareDistanceError = WorldState::OriginVar::MAX_ROUNDING_SQUARE_DISTANCE_ERROR;
	if( ( worldState.BotOriginVar().Value() - self->s.origin ).SquaredLength() > squareDistanceError ) {
		Debug( "The action can be applied only to the current bot origin\n" );
		return nullptr;
	}
	if( worldState.MightSeeLostEnemyAfterTurnVar().Ignore() ) {
		Debug( "Might bot see lost enemy after turn is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.MightSeeLostEnemyAfterTurnVar() ) {
		Debug( "Bot might see lost enemy after turn in the given world state. Pursuing makes no sense.\n" );
		return nullptr;
	}

	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( self ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	// (this action is dummy)
	plannerNode.Cost() = 1.0f;
	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().NavTargetOriginVar().SetValue( worldState.LostEnemyLastSeenOriginVar().Value() );
	plannerNode.WorldState().NavTargetOriginVar().SetIgnore( false );
	plannerNode.WorldState().NavTargetOriginVar().SetSatisfyOp( SatisfyOp::EQ, distanceThreshold );
	plannerNode.WorldState().IsReactingToEnemyLostVar().SetValue( true ).SetIgnore( false );

	return plannerNode.PrepareActionResult();
}

PlannerNode *BotStopLostEnemyPursuitAction::TryApply( const WorldState &worldState ) {
	if( worldState.IsReactingToEnemyLostVar().Ignore() ) {
		Debug( "Is bot reacting to enemy lost is ignored in the given world state\n" );
		return nullptr;
	}
	if( !worldState.IsReactingToEnemyLostVar() ) {
		Debug( "Bot is not reacting to enemy lost in the given world state\n" );
		return nullptr;
	}
	if( worldState.HasReactedToEnemyLostVar().Ignore() ) {
		Debug( "Has bot reacted to enemy lost is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.HasReactedToEnemyLostVar() ) {
		Debug( "Bot has already reacted to enemy lost in the given world state\n" );
		return nullptr;
	}
	if( worldState.LostEnemyLastSeenOriginVar().Ignore() ) {
		Debug( "Lost enemy origin is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.NavTargetOriginVar().Ignore() ) {
		Debug( "Nav target origin is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.DistanceToNavTarget() > 1.5f * GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Bot is too far from the nav target\n" );
		return nullptr;
	}

	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( self ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode.Cost() = 1.0f;
	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().NavTargetOriginVar().SetIgnore( true );
	plannerNode.WorldState().HasReactedToEnemyLostVar().SetValue( true );
	plannerNode.WorldState().IsReactingToEnemyLostVar().SetValue( false );

	return plannerNode.PrepareActionResult();
}

BotScriptActionRecord::~BotScriptActionRecord() {
	GENERIC_asDeleteScriptActionRecord( scriptObject );
}

void BotScriptActionRecord::Activate() {
	BotBaseActionRecord::Activate();
	GENERIC_asActivateScriptActionRecord( scriptObject );
}

void BotScriptActionRecord::Deactivate() {
	BotBaseActionRecord::Deactivate();
	GENERIC_asDeactivateScriptActionRecord( scriptObject );
}

AiBaseActionRecord::Status BotScriptActionRecord::CheckStatus( const WorldState &currWorldState ) const {
	return (AiBaseActionRecord::Status)GENERIC_asCheckScriptActionRecordStatus( scriptObject, currWorldState );
}

PlannerNode *BotScriptAction::TryApply( const WorldState &worldState ) {
	return (PlannerNode *)GENERIC_asTryApplyScriptAction( scriptObject, worldState );
}