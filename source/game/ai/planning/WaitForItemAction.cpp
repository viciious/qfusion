#include "PlanningLocal.h"
#include "../bot.h"

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
	plannerNode.WorldState().BotOriginVar().SetSatisfyOp( WorldState::SatisfyOp::EQ, 12.0f );
	plannerNode.WorldState().ResetTacticalSpots();

	return plannerNode.PrepareActionResult();
}