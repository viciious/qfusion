#include "Actions.h"
#include "../bot.h"
#include "../ai_ground_trace_cache.h"
#include "../combat/TacticalSpotsRegistry.h"

typedef WorldState::SatisfyOp SatisfyOp;

// These methods really belong to the bot logic, not the generic AI ones

const short *WorldState::GetSniperRangeTacticalSpot() {
	return self->ai->botRef->tacticalSpotsCache.GetSniperRangeTacticalSpot( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetFarRangeTacticalSpot() {
	return self->ai->botRef->tacticalSpotsCache.GetFarRangeTacticalSpot( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetMiddleRangeTacticalSpot() {
	return self->ai->botRef->tacticalSpotsCache.GetMiddleRangeTacticalSpot( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetCloseRangeTacticalSpot() {
	return self->ai->botRef->tacticalSpotsCache.GetCloseRangeTacticalSpot( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetCoverSpot() {
	return self->ai->botRef->tacticalSpotsCache.GetCoverSpot( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetRunAwayTeleportOrigin() {
	return self->ai->botRef->tacticalSpotsCache.GetRunAwayTeleportOrigin( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetRunAwayJumppadOrigin() {
	return self->ai->botRef->tacticalSpotsCache.GetRunAwayJumppadOrigin( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetRunAwayElevatorOrigin() {
	return self->ai->botRef->tacticalSpotsCache.GetRunAwayElevatorOrigin( BotOriginData(), EnemyOriginData() );
}

inline const BotWeightConfig &BotBaseAction::WeightConfig() const {
	return self->ai->botRef->WeightConfig();
}

void BotBaseActionRecord::Activate() {
	AiBaseActionRecord::Activate();
	self->ai->botRef->GetMiscTactics().Clear();
}

void BotBaseActionRecord::Deactivate() {
	AiBaseActionRecord::Deactivate();
	self->ai->botRef->GetMiscTactics().Clear();
}

bool BotCombatActionRecord::CheckCommonCombatConditions( const WorldState &currWorldState ) const {
	if( currWorldState.EnemyOriginVar().Ignore() ) {
		Debug( "Enemy is not specified\n" );
		return false;
	}
	if( self->ai->botRef->GetSelectedEnemies().InstanceId() != selectedEnemiesInstanceId ) {
		Debug( "New enemies have been selected\n" );
		return false;
	}
	return true;
}

BotScriptActionRecord::~BotScriptActionRecord() {
	GENERIC_asDeleteScriptActionRecord( scriptObject );
}

void BotScriptActionRecord::Activate() {
	BotBaseActionRecord::Activate();
	GENERIC_asActivateScriptActionRecord( scriptObject );
}

void BotScriptActionRecord::Deactivate() {
	BotBaseActionRecord::Deactivate();
	GENERIC_asDeactivateScriptActionRecord( scriptObject );
}

AiBaseActionRecord::Status BotScriptActionRecord::CheckStatus( const WorldState &currWorldState ) const {
	return (AiBaseActionRecord::Status)GENERIC_asCheckScriptActionRecordStatus( scriptObject, currWorldState );
}

PlannerNode *BotScriptAction::TryApply( const WorldState &worldState ) {
	return (PlannerNode *)GENERIC_asTryApplyScriptAction( scriptObject, worldState );
}