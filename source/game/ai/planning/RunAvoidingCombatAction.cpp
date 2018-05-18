#include "PlanningLocal.h"
#include "../bot.h"

void BotGenericRunAvoidingCombatActionRecord::Activate() {
	BotBaseActionRecord::Activate();
	self->ai->botRef->GetMiscTactics().PreferAttackRatherThanRun();
	self->ai->botRef->SetNavTarget( &navTarget );
}

void BotGenericRunAvoidingCombatActionRecord::Deactivate() {
	BotBaseActionRecord::Deactivate();
	self->ai->botRef->ResetNavTarget();
}

AiBaseActionRecord::Status BotGenericRunAvoidingCombatActionRecord::CheckStatus( const WorldState &currWorldState ) const {
	// It really gets invalidated on goal reevaluation

	if( ( navTarget.Origin() - self->s.origin ).LengthFast() <= GOAL_PICKUP_ACTION_RADIUS ) {
		return COMPLETED;
	}

	return VALID;
}

PlannerNode *BotGenericRunAvoidingCombatAction::TryApply( const WorldState &worldState ) {
	if( worldState.NavTargetOriginVar().Ignore() ) {
		Debug( "Nav target is absent in the given world state\n" );
		return nullptr;
	}
	if( worldState.DistanceToNavTarget() <= GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Bot is too close to the nav target\n" );
		return nullptr;
	}

	const Vec3 navTargetOrigin = worldState.NavTargetOriginVar().Value();
	const SelectedNavEntity &selectedNavEntity = self->ai->botRef->GetSelectedNavEntity();
	if( selectedNavEntity.IsValid() && !selectedNavEntity.IsEmpty() ) {
		const Vec3 navEntityOrigin = selectedNavEntity.GetNavEntity()->Origin();
		constexpr float squareDistanceError = WorldState::OriginVar::MAX_ROUNDING_SQUARE_DISTANCE_ERROR;
		if( ( navEntityOrigin - navTargetOrigin ).SquaredLength() < squareDistanceError ) {
			Debug( "Action is not applicable for goal entities (there are specialized actions for these kinds of nav target\n" );
			return nullptr;
		}
	}

	// As a contrary to combat actions, illegal travel time (when the destination is not reachable for AAS) is allowed.
	// Combat actions require simple kinds of movement to keep crosshair on enemy.
	// Thus tactical spot should be reachable in common way for combat actions.
	// In case of retreating, some other kinds of movement AAS is not aware of might be used.
	int travelTimeMillis = self->ai->botRef->CheckTravelTimeMillis( worldState.BotOriginVar().Value(), navTargetOrigin );
	// If the travel time is 0, set it to maximum allowed AAS travel time
	// (AAS stores time as seconds^-2 in a short value)
	if( !travelTimeMillis ) {
		travelTimeMillis = 10 * std::numeric_limits<short>::max();
	}

	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( self, navTargetOrigin ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode.Cost() = travelTimeMillis;

	plannerNode.WorldState() = worldState;
	// Move bot origin
	plannerNode.WorldState().BotOriginVar().SetValue( navTargetOrigin );
	// Since bot origin has been moved, tactical spots should be recomputed
	plannerNode.WorldState().ResetTacticalSpots();

	return plannerNode.PrepareActionResult();
}