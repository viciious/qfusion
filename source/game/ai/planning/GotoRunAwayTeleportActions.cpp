#include "PlanningLocal.h"
#include "../bot.h"

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
	plannerNode.WorldState().NavTargetOriginVar().SetSatisfyOp( WorldState::SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
	plannerNode.WorldState().NavTargetOriginVar().SetIgnore( false );
	// Set pending origin to the teleport destination
	plannerNode.WorldState().PendingOriginVar().SetValue( worldState.RunAwayTeleportOriginVar().Value2() );
	plannerNode.WorldState().PendingOriginVar().SetSatisfyOp( WorldState::SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
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
	plannerNode.WorldState().BotOriginVar().SetSatisfyOp( WorldState::SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
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