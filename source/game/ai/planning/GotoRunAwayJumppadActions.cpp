#include "PlanningLocal.h"
#include "../bot.h"

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
	plannerNode.WorldState().NavTargetOriginVar().SetSatisfyOp( WorldState::SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
	plannerNode.WorldState().NavTargetOriginVar().SetIgnore( false );
	// Set pending origin to the jumppad destination
	plannerNode.WorldState().PendingOriginVar().SetValue( worldState.RunAwayJumppadOriginVar().Value2() );
	plannerNode.WorldState().PendingOriginVar().SetSatisfyOp( WorldState::SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
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
	plannerNode.WorldState().BotOriginVar().SetSatisfyOp( WorldState::SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
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

