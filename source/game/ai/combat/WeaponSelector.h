#ifndef QFUSION_BOT_WEAPON_SELECTOR_H
#define QFUSION_BOT_WEAPON_SELECTOR_H

#include "../awareness/BaseEnemyPool.h"

class WorldState;

class GenericFireDef
{
	// Allow SelectedWeapons to use the default constructor
	friend class SelectedWeapons;

	float projectileSpeed;
	float splashRadius;
	ai_weapon_aim_type aimType;
	short weaponNum;
	bool isBuiltin;

	GenericFireDef()
		: projectileSpeed( 0 ),
		splashRadius( 0 ),
		aimType( AI_WEAPON_AIM_TYPE_INSTANT_HIT ),
		weaponNum( -1 ),
		isBuiltin( false ) {}

public:
	GenericFireDef( int weaponNum_, const firedef_t *builtinFireDef ) {
		this->projectileSpeed = builtinFireDef->speed;
		this->splashRadius = builtinFireDef->splash_radius;
		this->aimType = BuiltinWeaponAimType( weaponNum_, builtinFireDef->fire_mode );
		this->weaponNum = (short)weaponNum_;
		this->isBuiltin = true;
	}

	GenericFireDef( int weaponNum_, const AiScriptWeaponDef *scriptWeaponDef ) {
		this->projectileSpeed = scriptWeaponDef->projectileSpeed;
		this->splashRadius = scriptWeaponDef->splashRadius;
		this->aimType = scriptWeaponDef->aimType;
		this->weaponNum = (short)weaponNum_;
		this->isBuiltin = false;
	}

	inline int WeaponNum() const { return weaponNum; }
	inline bool IsBuiltin() const { return isBuiltin; }

	inline ai_weapon_aim_type AimType() const { return aimType; }
	inline float ProjectileSpeed() const { return projectileSpeed; }
	inline float SplashRadius() const { return splashRadius; }
	inline bool IsContinuousFire() const { return isBuiltin; }
};

class SelectedWeapons
{
	friend class BotWeaponsUsageModule;
	friend class BotWeaponSelector;
	friend class Bot;

	GenericFireDef builtinFireDef;
	GenericFireDef scriptFireDef;

	int64_t timeoutAt;
	unsigned instanceId;

	bool preferBuiltinWeapon;
	bool hasSelectedBuiltinWeapon;
	bool hasSelectedScriptWeapon;

	SelectedWeapons()
		: timeoutAt( 0 ),
		instanceId( 0 ),
		preferBuiltinWeapon( true ),
		hasSelectedBuiltinWeapon( false ),
		hasSelectedScriptWeapon( false ) {}

public:
	inline const GenericFireDef *BuiltinFireDef() const {
		return hasSelectedBuiltinWeapon ? &builtinFireDef : nullptr;
	}
	inline const GenericFireDef *ScriptFireDef() const {
		return hasSelectedScriptWeapon ? &scriptFireDef : nullptr;
	}
	inline int BuiltinWeaponNum() const {
		return hasSelectedBuiltinWeapon ? builtinFireDef.WeaponNum() : -1;
	}
	inline int ScriptWeaponNum() const {
		return hasSelectedScriptWeapon ? scriptFireDef.WeaponNum() : -1;
	}
	inline unsigned InstanceId() const { return instanceId; }
	inline bool AreValid() const { return timeoutAt > level.time; }
	inline void Invalidate() { timeoutAt = level.time; }
	inline int64_t TimeoutAt() const { return timeoutAt; }
	inline bool PreferBuiltinWeapon() const { return preferBuiltinWeapon; }
};

class SelectedEnemies
{
	friend class Bot;
	friend class BotThreatTracker;

	const edict_t *self;

	const Enemy *primaryEnemy;

	static const auto MAX_ACTIVE_ENEMIES = AiBaseEnemyPool::MAX_ACTIVE_ENEMIES;

	StaticVector<const Enemy *, MAX_ACTIVE_ENEMIES> activeEnemies;
	mutable int64_t threatFactorsComputedAt[MAX_ACTIVE_ENEMIES];
	mutable int64_t canEnemyHitComputedAt[MAX_ACTIVE_ENEMIES];
	mutable int64_t maxThreatFactorComputedAt;
	mutable int64_t canEnemiesHitComputedAt;
	mutable int64_t arePotentiallyHittableComputedAt;
	mutable float threatFactors[MAX_ACTIVE_ENEMIES];
	mutable bool canEnemyHit[MAX_ACTIVE_ENEMIES];
	mutable bool canEnemiesHit;
	mutable bool arePotentiallyHittable;

	int64_t timeoutAt;
	unsigned instanceId;
	mutable float maxThreatFactor;

	inline void CheckValid( const char *function ) const {
#ifdef _DEBUG
		if( !AreValid() ) {
			AI_FailWith( "SelectedEnemies", "::%s(): Selected enemies are invalid\n", function );
		}
#endif
	}

	explicit SelectedEnemies( const edict_t *self_ )
		: self( self_ ),
		primaryEnemy( nullptr ),
		maxThreatFactorComputedAt( 0 ),
		canEnemiesHitComputedAt( 0 ),
		arePotentiallyHittableComputedAt( 0 ),
		canEnemiesHit( false ),
		arePotentiallyHittable( false ),
		timeoutAt( 0 ),
		instanceId( 0 ),
		maxThreatFactor( 0.0f ) {
		memset( threatFactorsComputedAt, 0, sizeof( threatFactorsComputedAt ) );
		memset( threatFactors, 0, sizeof( threatFactors ) );
		memset( canEnemyHitComputedAt, 0, sizeof( canEnemyHitComputedAt ) );
		memset( canEnemyHit, 0, sizeof( canEnemyHit ) );
	}

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

	void Set( const Enemy *primaryEnemy_,
			  unsigned timeoutPeriod,
			  const Enemy **activeEnemiesBegin,
			  const Enemy **activeEnemiesEnd );

	void Set( const Enemy *primaryEnemy_,
			  unsigned timeoutPeriod,
			  const Enemy *firstActiveEnemy );

	inline unsigned InstanceId() const { return instanceId; }

	bool IsPrimaryEnemy( const edict_t *ent ) const {
		return primaryEnemy && primaryEnemy->ent == ent;
	}

	bool IsPrimaryEnemy( const Enemy *enemy ) const {
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

	typedef Enemy::SnapshotsQueue SnapshotsQueue;
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
	bool Contain( const Enemy *enemy ) const;
	float TotalInflictedDamage() const;

	float MaxDotProductOfBotViewAndDirToEnemy() const;
	float MaxDotProductOfEnemyViewAndDirToBot() const;

	// Checks whether a bot can potentially hit enemies from its origin if it adjusts view angles properly
	bool ArePotentiallyHittable() const;

	typedef const Enemy **EnemiesIterator;
	inline EnemiesIterator begin() const { return (EnemiesIterator)activeEnemies.cbegin(); }
	inline EnemiesIterator end() const { return (EnemiesIterator)activeEnemies.cend(); }

	bool CanHit() const;
	bool GetCanHit( int enemyNum ) const;
	bool TestCanHit( const edict_t *enemy ) const;

	bool HaveGoodSniperRangeWeapons() const;
	bool HaveGoodFarRangeWeapons() const;
	bool HaveGoodMiddleRangeWeapons() const;
	bool HaveGoodCloseRangeWeapons() const;

	bool AreThreatening() const {
		CheckValid( __FUNCTION__ );
		return MaxThreatFactor() > 0.9f;
	}

	float MaxThreatFactor() const;
	float GetThreatFactor( int enemyNum ) const;
	float ComputeThreatFactor( int enemyNum ) const;
	float ComputeThreatFactor( const edict_t *ent, int enemyNum = -1 ) const;
};

class Bot;

class BotWeaponSelector
{
	Bot *const bot;

	float weaponChoiceRandom;
	int64_t weaponChoiceRandomTimeoutAt;

	int64_t nextFastWeaponSwitchActionCheckAt;
	const unsigned weaponChoicePeriod;

public:
	BotWeaponSelector( Bot *bot_, unsigned weaponChoicePeriod_ )
		: bot( bot_ )
		, weaponChoiceRandom( 0.5f )
		, weaponChoiceRandomTimeoutAt( 0 )
		, nextFastWeaponSwitchActionCheckAt( 0 )
		, weaponChoicePeriod( weaponChoicePeriod_ ) {
		// Shut an analyzer up
		memset( &targetEnvironment, 0, sizeof( TargetEnvironment ) );
	}

	void Frame( const WorldState &cachedWorldState );
	void Think( const WorldState &cachedWorldState );

private:
#ifndef _MSC_VER
	inline void Debug( const char *format, ... ) const __attribute__( ( format( printf, 2, 3 ) ) );
#else
	inline void Debug( _Printf_format_string_ const char *format, ... ) const;
#endif

	// All these method cannot be defined in this header as there is a cyclic dependency with bot.h

	inline bool BotHasQuad() const;
	inline bool BotHasShell() const;
	inline bool BotHasPowerups() const;
	inline bool BotIsCarrier() const;

	inline float DamageToKill( const edict_t *client ) const {
		return ::DamageToKill( client, g_armor_protection->value, g_armor_degradation->value );
	}

	inline const int *Inventory() const;

	template <int Weapon> inline int AmmoReadyToFireCount() const;

	inline int BlastsReadyToFireCount() const;
	inline int ShellsReadyToFireCount() const;
	inline int GrenadesReadyToFireCount() const;
	inline int RocketsReadyToFireCount() const;
	inline int PlasmasReadyToFireCount() const;
	inline int BulletsReadyToFireCount() const;
	inline int LasersReadyToFireCount() const;
	inline int WavesReadyToFireCount() const;
	inline int BoltsReadyToFireCount() const;

	bool CheckFastWeaponSwitchAction( const WorldState &worldState );

	void SuggestAimWeapon( const WorldState &worldState );
	void SuggestSniperRangeWeapon( const WorldState &worldState );
	void SuggestFarRangeWeapon( const WorldState &worldState );
	void SuggestMiddleRangeWeapon( const WorldState &worldState );
	void SuggestCloseRangeWeapon( const WorldState &worldState );

	int SuggestInstagibWeapon( const WorldState &worldState );
	int SuggestFinishWeapon( const WorldState &worldState );

	const AiScriptWeaponDef *SuggestScriptWeapon( const WorldState &worldState, int *effectiveTier );
	bool IsEnemyEscaping( const WorldState &worldState, bool *botMovesFast, bool *enemyMovesFast );

	int SuggestHitEscapingEnemyWeapon( const WorldState &worldState, bool botMovesFast, bool enemyMovesFast );

	bool CheckForShotOfDespair( const WorldState &worldState );
	int SuggestShotOfDespairWeapon( const WorldState &worldState );
	int SuggestQuadBearerWeapon( const WorldState &worldState );

	int ChooseWeaponByScores( struct WeaponAndScore *begin, struct WeaponAndScore *end );

	struct TargetEnvironment {
		// Sides are relative to direction from bot origin to target origin
		// Order: top, bottom, front, back, left, right
		trace_t sideTraces[6];

		enum Side { TOP, BOTTOM, FRONT, BACK, LEFT, RIGHT };

		float factor;
		static const float TRACE_DEPTH;
	};

	TargetEnvironment targetEnvironment;
	void TestTargetEnvironment( const Vec3 &botOrigin, const Vec3 &targetOrigin, const edict_t *traceKey );

	void SetSelectedWeapons( int builtinWeapon, int scriptWeapon, bool preferBuiltinWeapon, unsigned timeoutPeriod );
	void SetSelectedWeapons( int builtinWeapon, unsigned timeoutPeriod ) {
		SetSelectedWeapons( builtinWeapon, -1, true, timeoutPeriod );
	}
};

#endif
