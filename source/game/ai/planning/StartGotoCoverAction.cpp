#include "PlanningLocal.h"
#include "../bot.h"

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
	plannerNode.WorldState().NavTargetOriginVar().SetSatisfyOp( WorldState::SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
	plannerNode.WorldState().NavTargetOriginVar().SetIgnore( false );
	// Set pending origin to the tactical spot
	plannerNode.WorldState().PendingOriginVar().SetValue( spotOrigin );
	plannerNode.WorldState().PendingOriginVar().SetSatisfyOp( WorldState::SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
	plannerNode.WorldState().PendingOriginVar().SetIgnore( false );

	unsigned similarWorldStateInstanceId = self->ai->botRef->NextSimilarWorldStateInstanceId();
	plannerNode.WorldState().SimilarWorldStateInstanceIdVar().SetValue( similarWorldStateInstanceId ).SetIgnore( false );

	return plannerNode.PrepareActionResult();
}
