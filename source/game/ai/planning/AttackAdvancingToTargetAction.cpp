#include "PlanningLocal.h"
#include "../bot.h"

void BotAttackAdvancingToTargetActionRecord::Activate() {
	AiBaseActionRecord::Activate();
	this->navTarget.SetToNavEntity( self->ai->botRef->GetSelectedNavEntity().GetNavEntity() );
	self->ai->botRef->SetNavTarget( &this->navTarget );
	self->ai->botRef->GetMiscTactics().Clear();
	self->ai->botRef->GetMiscTactics().PreferAttackRatherThanRun();
	// This flag affects weapons choice. This action is very likely to behave similar to retreating.
	self->ai->botRef->GetMiscTactics().willRetreat = true;
}

void BotAttackAdvancingToTargetActionRecord::Deactivate() {
	self->ai->botRef->GetMiscTactics().Clear();
	self->ai->botRef->ResetNavTarget();
	AiBaseActionRecord::Deactivate();
}

AiBaseActionRecord::Status BotAttackAdvancingToTargetActionRecord::CheckStatus( const WorldState &currWorldState ) const {
	const auto &selectedNavEntity = self->ai->botRef->GetSelectedNavEntity();
	if( !selectedNavEntity.IsValid() || selectedNavEntity.IsEmpty() ) {
		Debug( "The currently selected nav entity is invalid or is empty\n" );
		return INVALID;
	}

	const auto &selectedEnemies = self->ai->botRef->GetSelectedEnemies();
	if( !selectedEnemies.AreValid() || selectedEnemies.InstanceId() != selectedEnemiesInstanceId ) {
		Debug( "The currently selected enemies are not valid or have been updated\n" );
		return INVALID;
	}

	if( currWorldState.HasJustPickedGoalItemVar() ) {
		Debug( "The bot has just picked a goal item, should deactivate\n" );
		return COMPLETED;
	}

	return VALID;
}

PlannerNode *BotAttackAdvancingToTargetAction::TryApply( const WorldState &worldState ) {
	if( worldState.BotOriginVar().Ignore() ) {
		Debug( "Bot origin is ignored in the given world state\n" );
		return nullptr;
	}

	// Prevent excessive fruitless branching
	constexpr float distanceError = WorldState::OriginVar::MAX_ROUNDING_SQUARE_DISTANCE_ERROR;
	if( worldState.BotOriginVar().Value().SquareDistanceTo( self->s.origin ) > distanceError ) {
		Debug( "This action is applicable only for the real bot origin\n" );
		return nullptr;
	}
	if( worldState.EnemyOriginVar().Ignore() ) {
		Debug( "Enemy is ignored in the given world state\n" );
		return nullptr;
	}

	if( worldState.HealthVar().Ignore() || worldState.ArmorVar().Ignore() ) {
		Debug( "Health or armor are ignored in the given world state\n" );
		return nullptr;
	}

	if( worldState.NavTargetOriginVar().Ignore() ) {
		Debug( "Nav target is ignored in the given world state\n" );
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

	const float offensiveness = self->ai->botRef->GetEffectiveOffensiveness();
	float actionPenalty = 0.5f + 1.0f * offensiveness;

	const Vec3 botOrigin( worldState.BotOriginVar().Value() );
	const Vec3 navTargetOrigin( worldState.NavTargetOriginVar().Value() );
	int travelTimeMillis = self->ai->botRef->CheckTravelTimeMillis( botOrigin, navTargetOrigin );
	if( !travelTimeMillis ) {
		Debug( "Can't find a travel time from bot origin to the nav target origin\n" );
		return nullptr;
	}

	PlannerNodePtr plannerNode = NewNodeForRecord( pool.New( self, self->ai->botRef->GetSelectedEnemies().InstanceId() ) );
	if( !plannerNode ) {
		Debug( "Can't allocate planner node\n" );
		return nullptr;
	}

	// TODO: We need much more sophisticated cost calculations/applicability checks for this action!
	plannerNode.Cost() = travelTimeMillis * actionPenalty;

	plannerNode.WorldState() = worldState;
	// It is unlikely that bot is going to really have a positional advantage,
	// but that's what the terminal KillEnemy action expects
	plannerNode.WorldState().HasPositionalAdvantageVar().SetValue( true ).SetIgnore( false );
	unsigned similarWorldStateInstanceId = self->ai->botRef->NextSimilarWorldStateInstanceId();
	plannerNode.WorldState().SimilarWorldStateInstanceIdVar().SetValue( similarWorldStateInstanceId ).SetIgnore( false );
	plannerNode.WorldState().BotOriginVar().SetValue( navTargetOrigin );

	return plannerNode.ReleaseOwnership();
}