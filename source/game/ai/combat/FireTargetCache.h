#ifndef QFUSION_BOT_FIRE_TARGET_CACHE_H
#define QFUSION_BOT_FIRE_TARGET_CACHE_H

#include "WeaponSelector.h"

struct AimParams {
	vec3_t fireOrigin;
	vec3_t fireTarget;
	float suggestedBaseCoordError;

	inline float EffectiveCoordError( float skill ) const {
		return suggestedBaseCoordError * ( 1.5f - 1.35f * skill );
	}
};

class BotFireTargetCache
{
	struct CachedFireTarget {
		Vec3 origin;
		unsigned selectedEnemiesInstanceId;
		unsigned selectedWeaponsInstanceId;
		int64_t invalidAt;

		CachedFireTarget()
			: origin( 0, 0, 0 ),
			selectedEnemiesInstanceId( 0 ),
			selectedWeaponsInstanceId( 0 ),
			invalidAt( 0 ) {}

		bool IsValidFor( const SelectedEnemies &selectedEnemies, const SelectedWeapons &selectedWeapons ) const {
			return selectedEnemies.InstanceId() == selectedEnemies.InstanceId() &&
				   selectedWeapons.InstanceId() == selectedWeapons.InstanceId() &&
				   invalidAt > level.time;
		}

		void CacheFor( const SelectedEnemies &selectedEnemies, const SelectedWeapons &selectedWeapons, const vec3_t origin_ ) {
			VectorCopy( origin_, this->origin.Data() );
			selectedEnemiesInstanceId = selectedEnemies.InstanceId();
			selectedWeaponsInstanceId = selectedWeapons.InstanceId();
			invalidAt = level.time + 64;
		}

		void CacheFor( const SelectedEnemies &selectedEnemies, const SelectedWeapons &selectedWeapons, const Vec3 &origin_ ) {
			this->origin = origin_;
			selectedEnemiesInstanceId = selectedEnemies.InstanceId();
			selectedWeaponsInstanceId = selectedWeapons.InstanceId();
			invalidAt = level.time + 64;
		}
	};

	CachedFireTarget cachedFireTarget;
	const edict_t *bot;
	// Contains a value in range [0, 1] used for extrapolation of enemy origin for imitation of human-like aiming
	float extrapolationRandom;
	int64_t extrapolationRandomTimeoutAt;

	void SetupCoarseFireTarget( const SelectedEnemies &selectedEnemies,
								const GenericFireDef &fireDef,
								vec3_t fire_origin, vec3_t target );

	void AdjustPredictionExplosiveAimTypeParams( const SelectedEnemies &selectedEnemies,
												 const SelectedWeapons &selectedWeapons,
												 const GenericFireDef &fireDef, AimParams *aimParams );

	void AdjustPredictionAimTypeParams( const SelectedEnemies &selectedEnemies,
										const SelectedWeapons &selectedWeapons,
										const GenericFireDef &fireDef, AimParams *aimParams );

	void AdjustDropAimTypeParams( const SelectedEnemies &selectedEnemies, const SelectedWeapons &selectedWeapons,
								  const GenericFireDef &fireDef, AimParams *aimParams );

	void AdjustInstantAimTypeParams( const SelectedEnemies &selectedEnemies, const SelectedWeapons &selectedWeapons,
									 const GenericFireDef &fireDef, AimParams *aimParams );

	// Returns true if a shootable environment for inflicting a splash damage has been found
	bool AdjustTargetByEnvironment( const SelectedEnemies &selectedEnemies, float splashRadius, AimParams *aimParams );
	bool AdjustTargetByEnvironmentTracing( const SelectedEnemies &selectedEnemies, float splashRadius,
										   AimParams *aimParams );
	bool AdjustTargetByEnvironmentWithAAS( const SelectedEnemies &selectedEnemies, float splashRadius, int areaNum,
										   AimParams *aimParams );

	void GetPredictedTargetOrigin( const SelectedEnemies &selectedEnemies, const SelectedWeapons &selectedWeapons,
								   float projectileSpeed, AimParams *aimParams );
	void PredictProjectileShot( const SelectedEnemies &selectedEnemies, float projectileSpeed, AimParams *aimParams,
								bool applyTargetGravity );

public:
	BotFireTargetCache( const edict_t *bot_ )
		: bot( bot_ ), extrapolationRandom( 0.0f ), extrapolationRandomTimeoutAt( 0 ) {}

	void AdjustAimParams( const SelectedEnemies &selectedEnemies, const SelectedWeapons &selectedWeapons,
						  const GenericFireDef &fireDef, AimParams *aimParams );
};

#endif
