#include "PlanningLocal.h"
#include "../bot.h"

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