#include "PlanningLocal.h"
#include "../bot.h"

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