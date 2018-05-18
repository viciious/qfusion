#ifndef QFUSION_BOT_WEAPONS_USAGE_MODULE_H
#define QFUSION_BOT_WEAPONS_USAGE_MODULE_H

#include "../ai_local.h"
#include "../static_vector.h"
#include "FireTargetCache.h"

class Bot;
class BotInput;

class BotWeaponsUsageModule {
	friend class BotWeaponSelector;

	static constexpr unsigned MAX_SCRIPT_WEAPONS = 3;

	Bot *const bot;
	StaticVector<AiScriptWeaponDef, MAX_SCRIPT_WEAPONS> scriptWeaponDefs;
	StaticVector<int, MAX_SCRIPT_WEAPONS> scriptWeaponCooldown;

	BotFireTargetCache builtinFireTargetCache;
	BotFireTargetCache scriptFireTargetCache;

	BotWeaponSelector weaponSelector;
	SelectedWeapons selectedWeapons;

	// Returns true if current look angle worth pressing attack
	bool CheckShot( const AimParams &aimParams, const BotInput *input, const GenericFireDef &fireDef );

	bool TryTraceShot( trace_t *tr, const Vec3 &newLookDir,
					   const AimParams &aimParams,
					   const GenericFireDef &fireDef );

	bool CheckSplashTeamDamage( const vec3_t hitOrigin, const AimParams &aimParams, const GenericFireDef &fireDef );

	// A helper to determine whether continuous-fire weapons should be fired even if there is an obstacle in-front.
	// Should be called if a TryTraceShot() call has set non-unit fraction.
	bool IsShotBlockedBySolidWall( trace_t *tr,
								   float distanceThreshold,
								   const AimParams &aimParams,
								   const GenericFireDef &fireDef );

	void LookAtEnemy( float coordError, const vec3_t fire_origin, vec3_t target, BotInput *input );
	void PressAttack( const GenericFireDef *fireDef, const GenericFireDef *builtinFireDef,
					  const GenericFireDef *scriptFireDef, BotInput *input );


	class AimingRandomHolder {
		int64_t valuesTimeoutAt[3] = { 0, 0, 0 };
		float values[3] = { 0.5f, 0.5f, 0.5f };

	public:
		inline float GetCoordRandom( int coordNum ) {
			if( valuesTimeoutAt[coordNum] <= level.time ) {
				values[coordNum] = random();
				valuesTimeoutAt[coordNum] = level.time + 128 + From0UpToMax( 256, random() );
			}
			return values[coordNum];
		}
	};

	AimingRandomHolder aimingRandomHolder;

	void SetSelectedWeapons( int builtinWeapon, int scriptWeapon, bool preferBuiltinWeapon, unsigned timeoutPeriod );
public:
	explicit BotWeaponsUsageModule( Bot *bot_ );

	void UpdateScriptWeaponsStatus();
	void Frame( const WorldState &cachedWorldState );
	void Think( const WorldState &cachedWorldState );
	void TryFire( BotInput *input );

	inline const SelectedWeapons &GetSelectedWeapons() const { return selectedWeapons; }
};

#endif
