#ifndef QFUSION_AI_SQUAD_BASED_TEAM_BRAIN_H
#define QFUSION_AI_SQUAD_BASED_TEAM_BRAIN_H

#include "ai_base_team.h"
#include "ai_base_enemy_pool.h"
#include "ai_aas_route_cache.h"
#include "ai_aas_world.h"
#include "static_vector.h"
#include <deque>
#include <utility>

class Bot;

class CachedTravelTimesMatrix
{
	int aasTravelTimes[MAX_CLIENTS * MAX_CLIENTS];
	int FindAASTravelTime( const edict_t *fromClient, const edict_t *toClient );

public:
	inline void Clear() {
		// -1 means that a value should be lazily computed on demand
		std::fill( aasTravelTimes, aasTravelTimes + MAX_CLIENTS * MAX_CLIENTS, -1 );
	}
	int GetAASTravelTime( const edict_t *fromClient, const edict_t *toClient );
	int GetAASTravelTime( const Bot *from, const Bot *to );
};

class AiSquad : public AiFrameAwareUpdatable
{
	friend class AiSquadBasedTeam;

public:
	static constexpr unsigned MAX_SIZE = 3;
	typedef StaticVector<Bot*, MAX_SIZE> BotsList;

private:
	bool isValid;
	bool inUse;

	// If bots can see at least a single teammate
	bool canFightTogether;
	// If bots can move in a single group
	bool canMoveTogether;

	// If a connectivity of squad members is violated
	// (bots can't neither fight, nor move together)
	// and not restored to this timestamps, squad should be invalidated.
	int64_t brokenConnectivityTimeoutAt;

	bool botsDetached;

	BotsList bots;

	CachedTravelTimesMatrix &travelTimesMatrix;

	bool CheckCanFightTogether() const;
	bool CheckCanMoveTogether() const;

	int GetBotFloorCluster( Bot *bot ) const;
	bool IsInTheSameFloorCluster( Bot *bot, int givenClusterNum ) const;

	void UpdateBotRoleWeights();

	int64_t lastDroppedByBotTimestamps[MAX_SIZE];
	int64_t lastDroppedForBotTimestamps[MAX_SIZE];

	void CheckMembersInventory();

	// Returns lowest best weapon tier among all squad bots
	int FindBotWeaponsTiers( int maxBotWeaponTiers[MAX_SIZE] ) const;
	int FindLowestBotHealth() const;
	int FindLowestBotArmor() const;
	// Returns true if at least a single bot can and would drop health
	bool FindHealthSuppliers( bool wouldSupplyHealth[MAX_SIZE] ) const;
	// Returns true if at least a single bot can and would drop armor
	bool FindArmorSuppliers( bool wouldSupplyArmor[MAX_SIZE] ) const;

	bool ShouldNotDropItemsNow() const;

	typedef StaticVector<unsigned, AiSquad::MAX_SIZE - 1> Suppliers;
	// maxBotWeaponTiers, wouldSupplyHealth, wouldSupplyArmor are global for all bots.
	// Potential suppliers are selected for a single bot, best (nearest) suppliers first.
	// Potential suppliers should be checked then against global capabilities mentioned above.
	void FindSupplierCandidates( unsigned botNum, Suppliers &result ) const;

	bool RequestWeaponAndAmmoDrop( unsigned botNum, const int *maxBotWeaponTiers, Suppliers &supplierCandidates );
	bool RequestHealthDrop( unsigned botNum, bool wouldSupplyHealth[MAX_SIZE], Suppliers &suppliers );
	bool RequestArmorDrop( unsigned botNum, bool wouldSupplyArmor[MAX_SIZE], Suppliers &suppliers );

	bool RequestDrop( unsigned botNum, bool wouldSupply[MAX_SIZE], Suppliers & suppliers, void ( Bot::*dropFunc )() );

	edict_t *TryDropAmmo( unsigned botNum, unsigned supplierNum, int weapon );
	edict_t *TryDropWeapon( unsigned botNum, unsigned supplierNum, int weapon, const int *maxBotWeaponTiers );

	// Hack! To be able to access bot's private methods, define this entity physics callback as a (static) member
	static void SetDroppedEntityAsBotGoal( edict_t *ent );

	class SquadEnemyPool : public AiBaseEnemyPool
	{
		friend class AiSquad;
		AiSquad *squad;

		float botRoleWeights[AiSquad::MAX_SIZE];
		const Enemy *botEnemies[AiSquad::MAX_SIZE];

		unsigned GetBotSlot( const Bot *bot ) const;
		void CheckSquadValid() const;

protected:
		virtual void OnHurtByNewThreat( const edict_t *newThreat ) override;
		virtual bool CheckHasQuad() const override;
		virtual bool CheckHasShell() const override;
		virtual float ComputeDamageToBeKilled() const override;
		virtual void OnEnemyRemoved( const Enemy *enemy ) override;

		void SetBotRoleWeight( const edict_t *bot, float weight ) override;
		float GetAdditionalEnemyWeight( const edict_t *bot, const edict_t *enemy ) const override;
		void OnBotEnemyAssigned( const edict_t *bot, const Enemy *enemy ) override;

public:
		SquadEnemyPool( AiSquad *squad_, float skill );
		virtual ~SquadEnemyPool() override {}
	};

	// We can't use it as a value member because squads should be copyable or moveable
	SquadEnemyPool *squadEnemyPool;

protected:
	virtual void SetFrameAffinity( unsigned modulo, unsigned offset ) override {
		// Call super method first
		AiFrameAwareUpdatable::SetFrameAffinity( modulo, offset );
		// Allow enemy pool to think
		squadEnemyPool->SetFrameAffinity( modulo, offset );
	}

public:
	AiSquad( CachedTravelTimesMatrix &travelTimesMatrix_ );
	AiSquad( AiSquad &&that );
	virtual ~AiSquad() override;

	// If this is false, squad is not valid and should be
	inline bool IsValid() const { return isValid; }
	inline bool InUse() const { return inUse; }
	inline const BotsList &Bots() const { return bots; };

	inline AiBaseEnemyPool *EnemyPool() { return squadEnemyPool; }
	inline const AiBaseEnemyPool *EnemyPool() const { return squadEnemyPool; }

	void ReleaseBotsTo( StaticVector<Bot *, MAX_CLIENTS> &orphans );

	void PrepareToAddBots();

	void AddBot( Bot *bot );

	// Checks whether a bot may be attached to an existing squad
	bool MayAttachBot( const Bot *bot ) const;
	bool TryAttachBot( Bot *bot );

	void Invalidate();

	void OnBotRemoved( Bot *bot );

	inline void OnBotViewedEnemy( const edict_t *bot, const edict_t *enemy ) {
		squadEnemyPool->OnEnemyViewed( enemy );
	}
	inline void OnBotGuessedEnemyOrigin( const edict_t *bot, const edict_t *enemy,
										 unsigned minMillisSinceLastSeen, const float *specifiedOrigin ) {
		squadEnemyPool->OnEnemyOriginGuessed( enemy, minMillisSinceLastSeen, specifiedOrigin );
	}
	inline void OnBotPain( const edict_t *bot, const edict_t *enemy, float kick, int damage ) {
		squadEnemyPool->OnPain( bot, enemy, kick, damage );
	}
	inline void OnBotDamagedEnemy( const edict_t *bot, const edict_t *target, int damage ) {
		squadEnemyPool->OnEnemyDamaged( bot, target, damage );
	}

	// Assumes the bot is a valid squad member
	bool IsSupporter( const edict_t *bot ) const;

	virtual void Frame() override;
	virtual void Think() override;
};

class AiSquadBasedTeam : public AiBaseTeam
{
	friend class AiBaseTeam;
	StaticVector<AiSquad, MAX_CLIENTS> squads;
	StaticVector<Bot*, MAX_CLIENTS> orphanBots;

	CachedTravelTimesMatrix travelTimesMatrix;

protected:
	virtual void OnBotAdded( Bot *bot ) override;
	virtual void OnBotRemoved( Bot *bot ) override;

	// Should be overridden completely if you want to modify squad clustering logic
	// (this method should not be called in overridden one)
	virtual void SetupSquads();
	unsigned GetFreeSquadSlot();

	static AiSquadBasedTeam *InstantiateTeam( int team );
	static AiSquadBasedTeam *InstantiateTeam( int teamNum, const std::type_info &desiredType );
public:
	AiSquadBasedTeam( int team_ ) : AiBaseTeam( team_ ) {}
	virtual ~AiSquadBasedTeam() override {};

	virtual void Frame() override;
	virtual void Think() override;
};

#endif
