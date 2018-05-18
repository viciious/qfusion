#include "PlanningLocal.h"
#include "../bot.h"

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
	plannerNode.WorldState().BotOriginVar().SetSatisfyOp( WorldState::SatisfyOp::EQ, 12.0f );
	plannerNode.WorldState().ResetTacticalSpots();

	return plannerNode.PrepareActionResult();
}