#ifndef QFUSION_BOT_ACTIONS_H
#define QFUSION_BOT_ACTIONS_H

#include "BasePlanner.h"

constexpr const float GOAL_PICKUP_ACTION_RADIUS = 72.0f;
constexpr const float TACTICAL_SPOT_RADIUS = 40.0f;

class BotBaseActionRecord : public AiBaseActionRecord
{
public:
	BotBaseActionRecord( PoolBase *pool_, edict_t *self_, const char *name_ )
		: AiBaseActionRecord( pool_, self_, name_ ) {}

	void Activate() override;
	void Deactivate() override;
};

class BotBaseAction : public AiBaseAction
{
public:
	BotBaseAction( Ai *ai, const char *name_ )
		: AiBaseAction( ai, name_ ) {}

	inline const class BotWeightConfig &WeightConfig() const;
};

class BotGenericRunToItemActionRecord : public BotBaseActionRecord
{
	NavTarget navTarget;

public:
	BotGenericRunToItemActionRecord( PoolBase *pool_, edict_t *self_, const NavEntity *navEntity_ )
		: BotBaseActionRecord( pool_, self_, "BotGenericRunToItemActionRecord" ), navTarget( NavTarget::Dummy() ) {
		navTarget.SetToNavEntity( navEntity_ );
	}

	void Activate() override;
	void Deactivate() override;
	Status CheckStatus( const WorldState &currWorldState ) const override;
};

#define DECLARE_ACTION( actionName, poolSize )                                                     \
	class actionName : public BotBaseAction                                                           \
	{                                                                                                \
		Pool<actionName ## Record, poolSize> pool;                                                     \
public:                                                                                          \
		actionName( Ai * ai_ ) : BotBaseAction( ai_, #actionName ), pool( "Pool<" #actionName "Record>" ) {} \
		PlannerNode *TryApply( const WorldState &worldState ) override final;                          \
	}

#define DECLARE_INHERITED_ACTION( actionName, baseActionName, poolSize )                            \
	class actionName : public baseActionName                                                           \
	{                                                                                                 \
		Pool<actionName ## Record, poolSize> pool;                                                      \
public:                                                                                           \
		actionName( Ai * ai_ ) : baseActionName( ai_, #actionName ), pool( "Pool<" #actionName "Record>" ) {} \
		PlannerNode *TryApply( const WorldState &worldState ) override final;                           \
	}

DECLARE_ACTION( BotGenericRunToItemAction, 3 );

class BotPickupItemActionRecord : public BotBaseActionRecord
{
	NavTarget navTarget;

public:
	BotPickupItemActionRecord( PoolBase *pool_, edict_t *self_, const NavEntity *navEntity_ )
		: BotBaseActionRecord( pool_, self_, "BotPickupItemActionRecord" ), navTarget( NavTarget::Dummy() ) {
		navTarget.SetToNavEntity( navEntity_ );
	}

	void Activate() override;
	void Deactivate() override;
	Status CheckStatus( const WorldState &currWorldState ) const override;
};

DECLARE_ACTION( BotPickupItemAction, 3 );

class BotWaitForItemActionRecord : public BotBaseActionRecord
{
	NavTarget navTarget;

public:
	BotWaitForItemActionRecord( PoolBase *pool_, edict_t *self_, const NavEntity *navEntity_ )
		: BotBaseActionRecord( pool_, self_, "BotWaitForItemActionRecord" ), navTarget( NavTarget::Dummy() ) {
		navTarget.SetToNavEntity( navEntity_ );
	}

	void Activate() override;
	void Deactivate() override;
	Status CheckStatus( const WorldState &currWorldState ) const override;
};

DECLARE_ACTION( BotWaitForItemAction, 3 );

// A dummy action that always terminates actions chain but should not actually gets reached.
// This action is used to avoid direct world state satisfaction by temporary actions
// (that leads to premature planning termination).
class BotDummyActionRecord : public BotBaseActionRecord
{
public:
	BotDummyActionRecord( PoolBase *pool_, edict_t *self_, const char *name_ )
		: BotBaseActionRecord( pool_, self_, name_ ) {}

	void Activate() override { BotBaseActionRecord::Activate(); }
	void Deactivate() override { BotBaseActionRecord::Deactivate(); }
	Status CheckStatus( const WorldState &currWorldState ) const override {
		Debug( "This is a dummy action, should move to next one or replan\n" );
		return COMPLETED;
	}
};

#define DECLARE_DUMMY_ACTION_RECORD( recordName )              \
	class recordName : public BotDummyActionRecord                \
	{                                                            \
public:                                                      \
		recordName( PoolBase * pool_, edict_t * self_ )              \
			: BotDummyActionRecord( pool_, self_, #recordName ) {} \
	};

DECLARE_DUMMY_ACTION_RECORD( BotKillEnemyActionRecord )
DECLARE_ACTION( BotKillEnemyAction, 5 );

class BotCombatActionRecord : public BotBaseActionRecord
{
protected:
	NavTarget navTarget;
	unsigned selectedEnemiesInstanceId;

	bool CheckCommonCombatConditions( const WorldState &currWorldState ) const;

public:
	BotCombatActionRecord( PoolBase *pool_, edict_t *self_, const char *name_,
						   const Vec3 &tacticalSpotOrigin,
						   unsigned selectedEnemiesInstanceId )
		: BotBaseActionRecord( pool_, self_, name_ ),
		navTarget( NavTarget::Dummy() ),
		selectedEnemiesInstanceId( selectedEnemiesInstanceId ) {
		navTarget.SetToTacticalSpot( tacticalSpotOrigin );
	}
};

#define DECLARE_COMBAT_ACTION_RECORD( recordName )                                                                     \
	class recordName : public BotCombatActionRecord                                                                       \
	{                                                                                                                    \
public:                                                                                                              \
		recordName( PoolBase * pool_, edict_t * self_, const Vec3 &tacticalSpotOrigin, unsigned selectedEnemiesInstanceId_ ) \
			: BotCombatActionRecord( pool_, self_, #recordName, tacticalSpotOrigin, selectedEnemiesInstanceId_ ) {}        \
		void Activate() override;                                                                                        \
		void Deactivate() override;                                                                                      \
		Status CheckStatus( const WorldState &currWorldState ) const override;                                             \
	};

DECLARE_COMBAT_ACTION_RECORD( BotAdvanceToGoodPositionActionRecord );
DECLARE_ACTION( BotAdvanceToGoodPositionAction, 2 );

DECLARE_COMBAT_ACTION_RECORD( BotRetreatToGoodPositionActionRecord );
DECLARE_ACTION( BotRetreatToGoodPositionAction, 2 );

DECLARE_COMBAT_ACTION_RECORD( BotSteadyCombatActionRecord );
DECLARE_ACTION( BotSteadyCombatAction, 2 );

DECLARE_COMBAT_ACTION_RECORD( BotGotoAvailableGoodPositionActionRecord );
DECLARE_ACTION( BotGotoAvailableGoodPositionAction, 2 );

DECLARE_COMBAT_ACTION_RECORD( BotAttackFromCurrentPositionActionRecord );
DECLARE_ACTION( BotAttackFromCurrentPositionAction, 2 );

class BotRunAwayActionRecord : public BotBaseActionRecord
{
protected:
	NavTarget navTarget;
	const unsigned selectedEnemiesInstanceId;

public:
	BotRunAwayActionRecord( PoolBase *pool_,
							edict_t *self_,
							const char *name_,
							const Vec3 &navTargetOrigin,
							unsigned selectedEnemiesInstanceId_ )
		: BotBaseActionRecord( pool_, self_, name_ ),
		navTarget( NavTarget::Dummy() ),
		selectedEnemiesInstanceId( selectedEnemiesInstanceId_ ) {
		navTarget.SetToTacticalSpot( navTargetOrigin );
	}
};

#define DECLARE_RUN_AWAY_ACTION_RECORD( recordName )                                                                   \
	class recordName : public BotRunAwayActionRecord                                                                      \
	{                                                                                                                    \
public:                                                                                                              \
		recordName( PoolBase * pool_, edict_t * self_, const Vec3 &tacticalSpotOrigin, unsigned selectedEnemiesInstanceId_ ) \
			: BotRunAwayActionRecord( pool_, self_, #recordName, tacticalSpotOrigin, selectedEnemiesInstanceId_ ) {}       \
		void Activate() override;                                                                                        \
		void Deactivate() override;                                                                                      \
		Status CheckStatus( const WorldState &currWorldState ) const override;                                             \
	}

class BotRunAwayAction : public BotBaseAction
{
protected:
	bool CheckCommonRunAwayPreconditions( const WorldState &worldState ) const;
	bool CheckMiddleRangeKDDamageRatio( const WorldState &worldState ) const;
	bool CheckCloseRangeKDDamageRatio( const WorldState &worldState ) const;

public:
	BotRunAwayAction( Ai *ai_, const char *name_ ) : BotBaseAction( ai_, name_ ) {}
};

class BotGenericRunAvoidingCombatActionRecord : public BotBaseActionRecord
{
	NavTarget navTarget;

public:
	BotGenericRunAvoidingCombatActionRecord( PoolBase *pool_, edict_t *self_, const Vec3 &destination )
		: BotBaseActionRecord( pool_, self_, "BotGenericRunAvoidingCombatActionRecord" ),
		navTarget( NavTarget::Dummy() ) {
		navTarget.SetToTacticalSpot( destination, GOAL_PICKUP_ACTION_RADIUS );
	}

	void Activate() override;
	void Deactivate() override;
	Status CheckStatus( const WorldState &currWorldState ) const override;
};

DECLARE_ACTION( BotGenericRunAvoidingCombatAction, 5 );

DECLARE_DUMMY_ACTION_RECORD( BotStartGotoCoverActionRecord );
DECLARE_INHERITED_ACTION( BotStartGotoCoverAction, BotRunAwayAction, 5 );

DECLARE_RUN_AWAY_ACTION_RECORD( BotTakeCoverActionRecord );
DECLARE_INHERITED_ACTION( BotTakeCoverAction, BotRunAwayAction, 5 );

DECLARE_DUMMY_ACTION_RECORD( BotStartGotoRunAwayTeleportActionRecord );
DECLARE_INHERITED_ACTION( BotStartGotoRunAwayTeleportAction, BotRunAwayAction, 5 );

DECLARE_RUN_AWAY_ACTION_RECORD( BotDoRunAwayViaTeleportActionRecord );
DECLARE_INHERITED_ACTION( BotDoRunAwayViaTeleportAction, BotRunAwayAction, 5 );

DECLARE_DUMMY_ACTION_RECORD( BotStartGotoRunAwayJumppadActionRecord );
DECLARE_INHERITED_ACTION( BotStartGotoRunAwayJumppadAction, BotRunAwayAction, 5 );

DECLARE_RUN_AWAY_ACTION_RECORD( BotDoRunAwayViaJumppadActionRecord );
DECLARE_INHERITED_ACTION( BotDoRunAwayViaJumppadAction, BotRunAwayAction, 5 );

DECLARE_DUMMY_ACTION_RECORD( BotStartGotoRunAwayElevatorActionRecord );
DECLARE_INHERITED_ACTION( BotStartGotoRunAwayElevatorAction, BotRunAwayAction, 5 );

DECLARE_RUN_AWAY_ACTION_RECORD( BotDoRunAwayViaElevatorActionRecord );
DECLARE_INHERITED_ACTION( BotDoRunAwayViaElevatorAction, BotRunAwayAction, 5 );

DECLARE_DUMMY_ACTION_RECORD( BotStopRunningAwayActionRecord );
DECLARE_INHERITED_ACTION( BotStopRunningAwayAction, BotRunAwayAction, 5 );

#undef DEFINE_ACTION
#undef DEFINE_INHERITED_ACTION
#undef DEFINE_DUMMY_ACTION_RECORD
#undef DEFINE_COMBAT_ACTION_RECORD
#undef DEFINE_RUN_AWAY_ACTION_RECORD

class BotDodgeToSpotActionRecord : public BotBaseActionRecord
{
	NavTarget navTarget;
	int64_t timeoutAt;

public:
	BotDodgeToSpotActionRecord( PoolBase *pool_, edict_t *self_, const Vec3 &spotOrigin )
		: BotBaseActionRecord( pool_, self_, "BotDodgeToSpotActionRecord" ),
		navTarget( NavTarget::Dummy() ) {
		// Shut an analyzer up (this var value gets really set in Activate()).
		timeoutAt = std::numeric_limits<int>::max();
		navTarget.SetToTacticalSpot( spotOrigin );
	}

	void Activate() override;
	void Deactivate() override;
	Status CheckStatus( const WorldState &currWorldState ) const override;
};

DECLARE_ACTION( BotDodgeToSpotAction, 1 );

class BotTurnToThreatOriginActionRecord : public BotBaseActionRecord
{
	Vec3 threatPossibleOrigin;

public:
	BotTurnToThreatOriginActionRecord( PoolBase *pool_, edict_t *self_, const Vec3 &threatPossibleOrigin_ )
		: BotBaseActionRecord( pool_, self_, "BotTurnToThreatOriginActionRecord" ),
		threatPossibleOrigin( threatPossibleOrigin_ ) {}

	void Activate() override;
	void Deactivate() override;
	Status CheckStatus( const WorldState &currWorldState ) const override;
};

DECLARE_ACTION( BotTurnToThreatOriginAction, 1 );

class BotTurnToLostEnemyActionRecord : public BotBaseActionRecord
{
	Vec3 lastSeenEnemyOrigin;

public:
	BotTurnToLostEnemyActionRecord( PoolBase *pool_, edict_t *self_, const Vec3 &lastSeenEnemyOrigin_ )
		: BotBaseActionRecord( pool_, self_, "BotTurnToLostEnemyActionRecord" ),
		lastSeenEnemyOrigin( lastSeenEnemyOrigin_ ) {}

	void Activate() override;
	void Deactivate() override;
	Status CheckStatus( const WorldState &currWorldState ) const override;
};

DECLARE_ACTION( BotTurnToLostEnemyAction, 1 );

class BotStartLostEnemyPursuitActionRecord : public BotDummyActionRecord
{
public:
	BotStartLostEnemyPursuitActionRecord( PoolBase *pool_, edict_t *self_ )
		: BotDummyActionRecord( pool_, self_, "BotStartLostEnemyPursuitActionRecord" ) {}
};

DECLARE_ACTION( BotStartLostEnemyPursuitAction, 1 );

class BotStopLostEnemyPursuitActionRecord : public BotDummyActionRecord
{
public:
	BotStopLostEnemyPursuitActionRecord( PoolBase *pool_, edict_t *self_ )
		: BotDummyActionRecord( pool_, self_, "BotStopLostEnemyPursuitActionRecord" ) {}
};

DECLARE_ACTION( BotStopLostEnemyPursuitAction, 1 );

class BotScriptActionRecord : public BotBaseActionRecord
{
	void *scriptObject;

public:
	BotScriptActionRecord( PoolBase *pool_, edict_t *self_, const char *name_, void *scriptObject_ )
		: BotBaseActionRecord( pool_, self_, name_ ),
		scriptObject( scriptObject_ ) {}

	~BotScriptActionRecord() override;

	inline edict_t *Self() { return self; }
	using BotBaseActionRecord::Debug;

	void Activate() override;
	void Deactivate() override;

	Status CheckStatus( const WorldState &worldState ) const override;
};

class BotScriptAction : public BotBaseAction
{
	Pool<BotScriptActionRecord, 3> pool;
	void *scriptObject;

public:
	BotScriptAction( Ai *ai_, const char *name_, void *scriptObject_ )
		: BotBaseAction( ai_, name_ ),
		pool( name_ ),
		scriptObject( scriptObject_ ) {}

	// Exposed for script API
	inline edict_t *Self() { return self; }
	using BotBaseAction::Debug;

	inline PlannerNode *NewNodeForRecord( void *scriptRecord ) {
		// Reuse the existing method to ensure that logic and messaging is consistent
		PlannerNodePtr plannerNodePtr( AiBaseAction::NewNodeForRecord( pool.New( self, name, scriptRecord ) ) );
		return plannerNodePtr.ReleaseOwnership();
	}

	PlannerNode *TryApply( const WorldState &worldState ) override;
};

#endif
