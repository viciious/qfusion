#include "PlanningLocal.h"
#include "../bot.h"

void BotTurnToThreatOriginActionRecord::Activate() {
	BotBaseActionRecord::Activate();
	self->ai->botRef->SetPendingLookAtPoint( AiPendingLookAtPoint( threatPossibleOrigin, 3.0f ), 350 );
	self->ai->botRef->GetMiscTactics().PreferAttackRatherThanRun();
}

void BotTurnToThreatOriginActionRecord::Deactivate() {
	BotBaseActionRecord::Deactivate();
	self->ai->botRef->ResetPendingLookAtPoint();
}

AiBaseActionRecord::Status BotTurnToThreatOriginActionRecord::CheckStatus( const WorldState &currWorldState ) const {
	vec3_t lookDir;
	AngleVectors( self->s.angles, lookDir, nullptr, nullptr );

	Vec3 toThreatDir( threatPossibleOrigin );
	toThreatDir -= self->s.origin;
	toThreatDir.NormalizeFast();

	if( toThreatDir.Dot( lookDir ) > self->ai->botRef->FovDotFactor() ) {
		return COMPLETED;
	}

	return self->ai->botRef->HasPendingLookAtPoint() ? VALID : INVALID;
}

PlannerNode *BotTurnToThreatOriginAction::TryApply( const WorldState &worldState ) {
	if( worldState.ThreatPossibleOriginVar().Ignore() ) {
		Debug( "Threat possible origin is ignored in the given world state\n" );
		return nullptr;
	}
	if( !worldState.HasReactedToThreatVar().Ignore() && worldState.HasReactedToThreatVar() ) {
		Debug( "Bot has already reacted to threat in the given world state\n" );
		return nullptr;
	}

	constexpr float squareDistanceError = WorldState::OriginVar::MAX_ROUNDING_SQUARE_DISTANCE_ERROR;
	if( ( worldState.BotOriginVar().Value() - self->s.origin ).SquaredLength() > squareDistanceError ) {
		Debug( "The action can be applied only to the current bot origin\n" );
		return nullptr;
	}

	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( self, worldState.ThreatPossibleOriginVar().Value() ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode.Cost() = 500;
	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().ThreatPossibleOriginVar().SetIgnore( true );
	// If a bot has reacted to threat, he can't hit current enemy (if any)
	plannerNode.WorldState().CanHitEnemyVar().SetValue( false );
	plannerNode.WorldState().HasReactedToThreatVar().SetValue( true ).SetIgnore( false );

	return plannerNode.PrepareActionResult();
}