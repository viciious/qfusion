#include "PlanningLocal.h"
#include "../bot.h"

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
	plannerNode.WorldState().BotOriginVar().SetSatisfyOp( WorldState::SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
	plannerNode.WorldState().ResetTacticalSpots();

	return plannerNode.PrepareActionResult();
}