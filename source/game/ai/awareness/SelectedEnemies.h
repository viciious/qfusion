#ifndef QFUSION_SELECTEDENEMIES_H
#define QFUSION_SELECTEDENEMIES_H

#include "EnemiesTracker.h"

class SelectedEnemies {
	friend class Bot;
	friend class BotThreatTracker;

	const edict_t *self;

	const TrackedEnemy *primaryEnemy { nullptr };

	static const auto MAX_ACTIVE_ENEMIES = AiEnemiesTracker::MAX_ACTIVE_ENEMIES;

	StaticVector<const TrackedEnemy *, MAX_ACTIVE_ENEMIES> activeEnemies;
	mutable int64_t threatFactorsComputedAt[MAX_ACTIVE_ENEMIES];
	mutable int64_t canEnemyHitComputedAt[MAX_ACTIVE_ENEMIES];
	mutable int64_t maxThreatFactorComputedAt { 0 };
	mutable int64_t canEnemiesHitComputedAt { 0 };
	mutable int64_t botViewDirDotToEnemyDirComputedAt { 0 };
	mutable int64_t enemyViewDirDotToBotDirComputedAt { 0 };
	mutable int64_t aboutToHitEBorIGComputedAt { 0 };
	mutable int64_t aboutToHitLGorPGComputedAt { 0 };
	mutable int64_t aboutToHitRLorSWComputedAt { 0 };
	mutable int64_t arePotentiallyHittableComputedAt { 0 };
	mutable float threatFactors[MAX_ACTIVE_ENEMIES];
	mutable float botViewDirDotToEnemyDir[MAX_ACTIVE_ENEMIES];
	mutable float enemyViewDirDotToBotDir[MAX_ACTIVE_ENEMIES];
	mutable bool canEnemyHit[MAX_ACTIVE_ENEMIES];
	mutable bool canEnemiesHit { false };
	mutable bool aboutToHitEBorIG { false };
	mutable bool aboutToHitLGorPG { false };
	mutable bool aboutToHitRLorSW { false };
	mutable bool arePotentiallyHittable { false };

	int64_t timeoutAt { 0 };
	unsigned instanceId { 0 };
	mutable float maxThreatFactor { 0.0f };

	inline void CheckValid( const char *function ) const {
#ifdef _DEBUG
		if( !AreValid() ) {
			AI_FailWith( "SelectedEnemies", "::%s(): Selected enemies are invalid\n", function );
		}
#endif
	}

	explicit SelectedEnemies( const edict_t *self_ ) : self( self_ ) {
		memset( threatFactorsComputedAt, 0, sizeof( threatFactorsComputedAt ) );
		memset( threatFactors, 0, sizeof( threatFactors ) );
		memset( canEnemyHitComputedAt, 0, sizeof( canEnemyHitComputedAt ) );
		memset( canEnemyHit, 0, sizeof( canEnemyHit ) );
	}

	bool TestAboutToHitEBorIG( int64_t levelTime ) const;
	bool TestAboutToHitLGorPG( int64_t levelTime ) const;
	bool TestAboutToHitRLorSW( int64_t levelTime ) const;

	bool AreAboutToHit( int64_t *computedAt, bool *value, bool ( SelectedEnemies::*testHit )( int64_t ) const ) const {
		auto levelTime = level.time;
		if( levelTime != *computedAt ) {
			*value = ( this->*testHit )( levelTime );
			*computedAt = levelTime;
		}
		return *value;
	}

	const float *GetBotViewDirDotToEnemyDirValues() const;
	const float *GetEnemyViewDirDotToBotDirValues() const;
public:
	bool AreValid() const;

	inline void Invalidate() {
		timeoutAt = 0;
		maxThreatFactorComputedAt = 0;
		canEnemiesHitComputedAt = 0;
		memset( threatFactorsComputedAt, 0, sizeof( threatFactorsComputedAt ) );
		memset( canEnemyHitComputedAt, 0, sizeof( canEnemyHitComputedAt ) );
		primaryEnemy = nullptr;
		activeEnemies.clear();
	}

	void Set( const TrackedEnemy *primaryEnemy_,
			  unsigned timeoutPeriod,
			  const TrackedEnemy **activeEnemiesBegin,
			  const TrackedEnemy **activeEnemiesEnd );

	void Set( const TrackedEnemy *primaryEnemy_,
			  unsigned timeoutPeriod,
			  const TrackedEnemy *firstActiveEnemy );

	inline unsigned InstanceId() const { return instanceId; }

	bool IsPrimaryEnemy( const edict_t *ent ) const {
		return primaryEnemy && primaryEnemy->ent == ent;
	}

	bool IsPrimaryEnemy( const TrackedEnemy *enemy ) const {
		return primaryEnemy && primaryEnemy == enemy;
	}

	Vec3 LastSeenOrigin() const {
		CheckValid( __FUNCTION__ );
		return primaryEnemy->LastSeenOrigin();
	}

	Vec3 ActualOrigin() const {
		CheckValid( __FUNCTION__ );
		return Vec3( primaryEnemy->ent->s.origin );
	}

	Vec3 LastSeenVelocity() const {
		CheckValid( __FUNCTION__ );
		return primaryEnemy->LastSeenVelocity();
	}

	unsigned LastSeenAt() const {
		CheckValid( __FUNCTION__ );
		return primaryEnemy->LastSeenAt();
	}

	Vec3 ClosestEnemyOrigin( const Vec3 &relativelyTo ) const {
		return ClosestEnemyOrigin( relativelyTo.Data() );
	}

	Vec3 ClosestEnemyOrigin( const vec3_t relativelyTo ) const;

	typedef TrackedEnemy::SnapshotsQueue SnapshotsQueue;
	const SnapshotsQueue &LastSeenSnapshots() const {
		CheckValid( __FUNCTION__ );
		return primaryEnemy->lastSeenSnapshots;
	}

	Vec3 ActualVelocity() const {
		CheckValid( __FUNCTION__ );
		return Vec3( primaryEnemy->ent->velocity );
	}

	Vec3 Mins() const {
		CheckValid( __FUNCTION__ );
		return Vec3( primaryEnemy->ent->r.mins );
	}

	Vec3 Maxs() const {
		CheckValid( __FUNCTION__ );
		return Vec3( primaryEnemy->ent->r.maxs );
	}

	Vec3 LookDir() const {
		CheckValid( __FUNCTION__ );
		vec3_t lookDir;
		AngleVectors( primaryEnemy->ent->s.angles, lookDir, nullptr, nullptr );
		return Vec3( lookDir );
	}

	Vec3 EnemyAngles() const {
		CheckValid( __FUNCTION__ );
		return Vec3( primaryEnemy->ent->s.angles );
	}

	float DamageToKill() const;

	int PendingWeapon() const {
		if( primaryEnemy && primaryEnemy->ent && primaryEnemy->ent->r.client ) {
			return primaryEnemy->ent->r.client->ps.stats[STAT_PENDING_WEAPON];
		}
		return -1;
	}

	unsigned FireDelay() const;

	inline bool IsStaticSpot() const {
		return Ent()->r.client == nullptr;
	}

	inline const edict_t *Ent() const {
		CheckValid( __FUNCTION__ );
		return primaryEnemy->ent;
	}

	inline const edict_t *TraceKey() const {
		CheckValid( __FUNCTION__ );
		return primaryEnemy->ent;
	}

	inline const bool OnGround() const {
		CheckValid( __FUNCTION__ );
		return primaryEnemy->ent->groundentity != nullptr;
	}

	bool HaveQuad() const;
	bool HaveCarrier() const;
	bool Contain( const TrackedEnemy *enemy ) const;
	float TotalInflictedDamage() const;

	float MaxDotProductOfBotViewAndDirToEnemy() const {
		auto *dots = GetBotViewDirDotToEnemyDirValues();
		return *std::max_element( dots, dots + activeEnemies.size() );
	}

	float MaxDotProductOfEnemyViewAndDirToBot() const {
		auto *dots = GetEnemyViewDirDotToBotDirValues();
		return *std::max_element( dots, dots + activeEnemies.size() );
	}

	// Checks whether a bot can potentially hit enemies from its origin if it adjusts view angles properly
	bool ArePotentiallyHittable() const;

	typedef const TrackedEnemy **EnemiesIterator;
	inline EnemiesIterator begin() const { return (EnemiesIterator)activeEnemies.cbegin(); }
	inline EnemiesIterator end() const { return (EnemiesIterator)activeEnemies.cend(); }

	bool CanHit() const;
	bool GetCanHit( int enemyNum, float viewDot ) const;
	bool TestCanHit( const edict_t *enemy, float viewDot ) const;

	bool HaveGoodSniperRangeWeapons() const;
	bool HaveGoodFarRangeWeapons() const;
	bool HaveGoodMiddleRangeWeapons() const;
	bool HaveGoodCloseRangeWeapons() const;

	bool AreAboutToHitEBorIG() const {
		return AreAboutToHit( &aboutToHitEBorIGComputedAt, &aboutToHitEBorIG, &SelectedEnemies::TestAboutToHitEBorIG );
	}
	bool AreAboutToHitRLorSW() const {
		return AreAboutToHit( &aboutToHitRLorSWComputedAt, &aboutToHitRLorSW, &SelectedEnemies::TestAboutToHitRLorSW );
	}
	bool AreAboutToHitLGorPG() const {
		return AreAboutToHit( &aboutToHitLGorPGComputedAt, &aboutToHitLGorPG, &SelectedEnemies::TestAboutToHitLGorPG );
	}

	bool AreThreatening() const {
		CheckValid( __FUNCTION__ );
		return MaxThreatFactor() > 0.9f;
	}

	float MaxThreatFactor() const;
	float GetThreatFactor( int enemyNum ) const;
	float ComputeThreatFactor( int enemyNum ) const;
	float ComputeThreatFactor( const edict_t *ent, int enemyNum = -1 ) const;
};

#endif
