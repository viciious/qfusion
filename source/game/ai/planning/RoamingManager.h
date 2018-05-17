#ifndef QFUSION_BOT_ROAMING_MANAGER_H
#define QFUSION_BOT_ROAMING_MANAGER_H

#include "../ai_local.h"
#include "../static_vector.h"

class BotRoamingManager
{
	// Note: very large values lead to all spots being considered as visited
	// during the first VISITED_SPOT_EXPIRATION_TIME millis of the level time.
	// Even if TryResetAllSpotsDisabledState() fills visitedAt by zero,
	// levelTime - visitedAt[spotNum] is still below this limit.
	static constexpr unsigned VISITED_SPOT_EXPIRATION_TIME = 10 * 1000;

	int64_t *visitedAt;
	edict_t *self;
	Vec3 tmpSpotOrigin;
	Vec3 cachedSpotOrigin;
	int64_t spotSelectedAt;
	int currTacticalSpotNum;
	unsigned numVisitedSpots;
	const class TacticalSpotsRegistry *tacticalSpotsRegistry;
	const class AiAasWorld *aasWorld;

	typedef StaticVector<int, 20> Candidates;

	const Vec3 &GetRoamingSpot();

	inline bool IsTemporarilyDisabled( unsigned spotNum ) const {
		return MillisSinceVisited( spotNum, level.time ) < VISITED_SPOT_EXPIRATION_TIME;
	}

	inline bool IsTemporarilyDisabled( unsigned spotNum, int64_t levelTime ) const {
		return MillisSinceVisited( spotNum, levelTime ) < VISITED_SPOT_EXPIRATION_TIME;
	}

	inline const Vec3 &SetTmpSpotFromArea( int areaNum );
	// Non-negative return values are feasible
	int TrySuggestTacticalSpot();
	// Non-negative return values are feasible
	int TryFindReachableSpot( const Candidates &candidateSpots, int travelFlags, const int *fromAreaNums, int numFromAreas );
	// Positive return values are feasible
	int TryFindReachableArea( const Candidates &candidateAreas, int travelFlags, const int *fromAreaNums, int numFromAreas );
	void TryResetAllSpotsDisabledState();
	// Positive return values are feasible
	int TrySuggestRandomAasArea();
	int TrySuggestNearbyAasArea();
	bool IsFeasibleArea( const aas_area_t &area, const aas_areasettings_t &areaSettings );

	inline void ClearVisitedSpots();
public:
	BotRoamingManager( edict_t *self_ );
	~BotRoamingManager() {
		G_LevelFree( visitedAt );
	}

	inline uint64_t MillisSinceVisited( unsigned spotNum ) const {
		assert( level.time >= visitedAt[spotNum] );
		return (uint64_t)( level.time - visitedAt[spotNum] );
	}

	inline uint64_t MillisSinceVisited( unsigned spotNum, int64_t levelTime ) const {
		assert( levelTime >= visitedAt[spotNum] );
		return (uint64_t)( levelTime - visitedAt[spotNum] );
	}

	inline void DisableSpotsInRadius( const Vec3 &origin, float radius ) {
		DisableSpotsInRadius( origin.Data(), radius );
	}

	void DisableSpotsInRadius( const vec3_t origin, float radius );

	// All calls during a single frame are guaranteed to return the same result
	const Vec3 &GetCachedRoamingSpot();

	inline void CheckSpotsProximity() {
		DisableSpotsInRadius( self->s.origin, 96.0f );
	}
};

#endif
