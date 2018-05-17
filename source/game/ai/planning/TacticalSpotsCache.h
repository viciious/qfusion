#ifndef QFUSION_BOT_TACTICAL_SPOTS_CACHE_H
#define QFUSION_BOT_TACTICAL_SPOTS_CACHE_H

#include "../ai_local.h"

class BotTacticalSpotsCache
{
	template <typename SpotData>
	struct CachedSpot {
		short origin[3];
		short enemyOrigin[3];
		SpotData spotData;
		bool succeeded;
	};

	// Use just a plain array for caching spots.
	// High number of distinct (and thus searched for) spots will kill TacticalSpotsRegistry performance first.
	template <typename SpotData>
	struct SpotsCache {
		static constexpr auto MAX_SPOTS = 3;
		CachedSpot<SpotData> spots[MAX_SPOTS];
		unsigned numSpots;

		inline SpotsCache() { Clear(); }

		inline void Clear() {
			numSpots = 0;
			memset( spots, 0, sizeof( spots ) );
		}

		inline CachedSpot<SpotData> *Alloc() {
			if( numSpots == MAX_SPOTS ) {
				return nullptr;
			}
			return &spots[numSpots++];
		}

		bool TryGetCachedSpot( const short *origin, const short *enemyOrigin, short **result ) const {
			for( unsigned i = 0, end = numSpots; i < end; ++i ) {
				const CachedSpot<SpotData> &spot = spots[i];
				if( !VectorCompare( origin, spot.origin ) ) {
					continue;
				}
				if( !VectorCompare( enemyOrigin, spot.enemyOrigin ) ) {
					continue;
				}
				*result = spot.succeeded ? (short *)spot.spotData : nullptr;
				return true;
			}
			return false;
		}
	};

	typedef SpotsCache<short[3]> SingleOriginSpotsCache;
	SingleOriginSpotsCache sniperRangeTacticalSpotsCache;
	SingleOriginSpotsCache farRangeTacticalSpotsCache;
	SingleOriginSpotsCache middleRangeTacticalSpotsCache;
	SingleOriginSpotsCache closeRangeTacticalSpotsCache;
	SingleOriginSpotsCache coverSpotsTacticalSpotsCache;

	typedef SpotsCache<short[6]> DualOriginSpotsCache;
	DualOriginSpotsCache runAwayTeleportOriginsCache;
	DualOriginSpotsCache runAwayJumppadOriginsCache;
	DualOriginSpotsCache runAwayElevatorOriginsCache;

	edict_t *self;

	bool FindSniperRangeTacticalSpot( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result );
	bool FindFarRangeTacticalSpot( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result );
	bool FindMiddleRangeTacticalSpot( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result );
	bool FindCloseRangeTacticalSpot( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result );
	bool FindCoverSpot( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result );

	// We can't(?) refer to a nested class in a forward declaration, so declare the parameter as a template one
	template <typename ProblemParams>
	inline bool FindForOrigin( const ProblemParams &problemParams, const Vec3 &origin, float searchRadius, vec3_t result );

	bool FindRunAwayTeleportOrigin( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result[2] );
	bool FindRunAwayJumppadOrigin( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result[2] );
	bool FindRunAwayElevatorOrigin( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result[2] );

	typedef StaticVector<EntAndScore, 16> ReachableEntities;
	void FindReachableClassEntities( const Vec3 &origin, float radius, const char *classname, ReachableEntities &result );

	// AiAasWorld::FindAreaNum() fails so often for teleports/elevators, etc, so we have to use this method.
	// AiAasWorld is provided as an argument to avoid an implicit retrieval of global instance in a loop.
	int FindMostFeasibleEntityAasArea( const edict_t *ent, const AiAasWorld *aasWorld ) const;

	class NearbyEntitiesCache
	{
public:
		static constexpr unsigned MAX_CACHED_NEARBY_ENTITIES = 32;
		struct NearbyEntitiesCacheEntry {
			int entNums[MAX_CACHED_NEARBY_ENTITIES];
			int numEntities;
			vec3_t botOrigin;
			float radius;

			// Shut an analyzer up
			NearbyEntitiesCacheEntry() : numEntities( 0 ), radius( 0 ) {}
		};

private:
		static constexpr unsigned MAX_ENTITIES_CACHE_ENTRIES = 4;
		NearbyEntitiesCacheEntry entries[MAX_ENTITIES_CACHE_ENTRIES];
		unsigned numEntries;

public:
		inline NearbyEntitiesCache() : numEntries( 0 ) {}
		inline void Clear() { numEntries = 0; }
		inline NearbyEntitiesCacheEntry *Alloc() {
			if( numEntries == MAX_ENTITIES_CACHE_ENTRIES ) {
				return nullptr;
			}
			return &entries[numEntries++];
		}
		const NearbyEntitiesCacheEntry *TryGetCachedEntities( const Vec3 &origin, float radius );
	};

	NearbyEntitiesCache nearbyEntitiesCache;

	int FindNearbyEntities( const Vec3 &origin, float radius, int **entNums );

	// These functions are extracted to be able to mock a bot entity
	// by a player entity easily for testing and tweaking the cache
	inline class AiAasRouteCache *RouteCache();
	inline float Skill() const;
	inline bool BotHasAlmostSameOrigin( const Vec3 &unpackedOrigin ) const;

	typedef bool (BotTacticalSpotsCache::*SingleOriginFindMethod)( const Vec3 &, const Vec3 &, vec3_t );
	const short *GetSingleOriginSpot( SingleOriginSpotsCache *cachedSpots, const short *origin,
									  const short *enemyOrigin, SingleOriginFindMethod findMethod );

	typedef bool (BotTacticalSpotsCache::*DualOriginFindMethod)( const Vec3 &, const Vec3 &, vec3_t[2] );
	const short *GetDualOriginSpot( DualOriginSpotsCache *cachedSpots, const short *origin,
									const short *enemyOrigin, DualOriginFindMethod findMethod );

public:
	inline BotTacticalSpotsCache( edict_t *self_ ) : self( self_ ) {}

	inline void Clear() {
		sniperRangeTacticalSpotsCache.Clear();
		farRangeTacticalSpotsCache.Clear();
		middleRangeTacticalSpotsCache.Clear();
		closeRangeTacticalSpotsCache.Clear();
		coverSpotsTacticalSpotsCache.Clear();

		nearbyEntitiesCache.Clear();

		runAwayTeleportOriginsCache.Clear();
		runAwayJumppadOriginsCache.Clear();
		runAwayElevatorOriginsCache.Clear();
	}

	inline const short *GetSniperRangeTacticalSpot( const short *origin, const short *enemyOrigin ) {
		return GetSingleOriginSpot( &sniperRangeTacticalSpotsCache, origin, enemyOrigin,
									&BotTacticalSpotsCache::FindSniperRangeTacticalSpot );
	}
	inline const short *GetFarRangeTacticalSpot( const short *origin, const short *enemyOrigin ) {
		return GetSingleOriginSpot( &farRangeTacticalSpotsCache, origin, enemyOrigin,
									&BotTacticalSpotsCache::FindFarRangeTacticalSpot );
	}
	inline const short *GetMiddleRangeTacticalSpot( const short *origin, const short *enemyOrigin ) {
		return GetSingleOriginSpot( &middleRangeTacticalSpotsCache, origin, enemyOrigin,
									&BotTacticalSpotsCache::FindMiddleRangeTacticalSpot );
	}
	inline const short *GetCloseRangeTacticalSpot( const short *origin, const short *enemyOrigin ) {
		return GetSingleOriginSpot( &closeRangeTacticalSpotsCache, origin, enemyOrigin,
									&BotTacticalSpotsCache::FindCloseRangeTacticalSpot );
	}
	inline const short *GetCoverSpot( const short *origin, const short *enemyOrigin ) {
		return GetSingleOriginSpot( &coverSpotsTacticalSpotsCache, origin, enemyOrigin,
									&BotTacticalSpotsCache::FindCoverSpot );
	}

	inline const short *GetRunAwayTeleportOrigin( const short *origin, const short *enemyOrigin ) {
		return GetDualOriginSpot( &runAwayTeleportOriginsCache, origin, enemyOrigin,
								  &BotTacticalSpotsCache::FindRunAwayTeleportOrigin );
	}
	inline const short *GetRunAwayJumppadOrigin( const short *origin, const short *enemyOrigin ) {
		return GetDualOriginSpot( &runAwayJumppadOriginsCache, origin, enemyOrigin,
								  &BotTacticalSpotsCache::FindRunAwayJumppadOrigin );
	}
	inline const short *GetRunAwayElevatorOrigin( const short *origin, const short *enemyOrigin ) {
		return GetDualOriginSpot( &runAwayElevatorOriginsCache, origin, enemyOrigin,
								  &BotTacticalSpotsCache::FindRunAwayElevatorOrigin );
	}
};

#endif
