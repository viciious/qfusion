#include "PlanningLocal.h"
#include "../bot.h"

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
	plannerNode.WorldState().NavTargetOriginVar().SetSatisfyOp( WorldState::SatisfyOp::EQ, distanceThreshold );
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