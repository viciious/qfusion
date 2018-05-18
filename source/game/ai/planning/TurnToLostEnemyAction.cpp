#include "PlanningLocal.h"
#include "../bot.h"

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
