#include "bot_roaming_manager.h"
#include "combat/TacticalSpotsRegistry.h"

// Cannot be defined in the header
BotRoamingManager::BotRoamingManager( edict_t *self_ )
	: self( self_ ),
	tmpSpotOrigin( 0, 0, 0 ),
	cachedSpotOrigin( 0, 0, 0 ),
	spotSelectedAt( 0 ),
	currTacticalSpotNum( -1 ),
	numVisitedSpots( 0 ),
	tacticalSpotsRegistry( TacticalSpotsRegistry::Instance() ),
	aasWorld( AiAasWorld::Instance() ) {
	visitedAt = (int64_t *)G_LevelMalloc( sizeof( int64_t ) * tacticalSpotsRegistry->numSpots );
	ClearVisitedSpots();
}

const Vec3 &BotRoamingManager::GetCachedRoamingSpot() {
	if( spotSelectedAt != level.time ) {
		cachedSpotOrigin.Set( GetRoamingSpot() );
		spotSelectedAt = level.time;
	}
	return cachedSpotOrigin;
}

inline void BotRoamingManager::ClearVisitedSpots() {
	if( !tacticalSpotsRegistry->numSpots ) {
		AI_FailWith( "BotRoamingManager::ClearVisitedSpots()", "There is no tactical spots\n" );
	}
	memset( visitedAt, 0, sizeof( int64_t ) * tacticalSpotsRegistry->numSpots );
}

const Vec3 &BotRoamingManager::GetRoamingSpot() {
	const auto *routeCache = self->ai->botRef->routeCache;

	// If the bot is using a tactical spot as a roaming spot
	if( currTacticalSpotNum >= 0 ) {
		// Check whether is has been reached since the last GetRoamingSpot() call
		if( IsTemporarilyDisabled( (unsigned)currTacticalSpotNum ) ) {
			currTacticalSpotNum = -1;
		} else {
			// Check whether it is still reachable
			const int allowedTravelFlags = self->ai->botRef->AllowedTravelFlags();
			const int spotAreaNum = tacticalSpotsRegistry->spots[currTacticalSpotNum].aasAreaNum;
			int fromAreaNums[2];
			const int numFromAreas = self->ai->botRef->EntityPhysicsState()->PrepareRoutingStartAreas( fromAreaNums );
			int travelTime = 0;
			for( int i = 0; i < numFromAreas; ++i ) {
				travelTime = routeCache->TravelTimeToGoalArea( fromAreaNums[i], spotAreaNum, allowedTravelFlags );
				if( travelTime > 0 ) {
					break;
				}
			}
			if( !travelTime ) {
				currTacticalSpotNum = -1;
			}
		}
	}

	if( currTacticalSpotNum >= 0 ) {
		// Keep it as-is
		return tmpSpotOrigin;
	}

	int spotNum = TrySuggestTacticalSpot();
	if( spotNum >= 0 ) {
		tmpSpotOrigin.Set( tacticalSpotsRegistry->spots[spotNum].origin );
		currTacticalSpotNum = spotNum;
		return tmpSpotOrigin;
	}

	if( int areaNum = TrySuggestRandomAasArea() ) {
		return SetTmpSpotFromArea( areaNum );
	}

	if( int areaNum = TrySuggestNearbyAasArea() ) {
		return SetTmpSpotFromArea( areaNum );
	}

	tmpSpotOrigin.Set( -99999, -99999, -99999 );
	return tmpSpotOrigin;
}

inline const Vec3 &BotRoamingManager::SetTmpSpotFromArea( int areaNum ) {
	const auto &area = aasWorld->Areas()[areaNum];
	tmpSpotOrigin.Set( area.center );
	tmpSpotOrigin.Z() = area.mins[2] + 8.0f;
	return tmpSpotOrigin;
}

int BotRoamingManager::TrySuggestTacticalSpot() {
	// Limit the number of tested for reachability spots.
	// It may lead to performance spikes otherwise.
	Candidates candidateSpots;

	const int64_t levelTime = level.time;
	const unsigned numSpots = tacticalSpotsRegistry->numSpots;
	const auto *spots = tacticalSpotsRegistry->spots;
	for( unsigned i = 0; i < candidateSpots.capacity(); ++i ) {
		uint16_t spotNum = (uint16_t)( brandom( 0.0f, numSpots - 0.1f ) );
		if( IsTemporarilyDisabled( spotNum, levelTime ) ) {
			continue;
		}

		const auto &spot = spots[spotNum];
		if( DistanceSquared( self->s.origin, spot.origin ) < 384 * 384 ) {
			continue;
		}

		candidateSpots.push_back( spotNum );
	}

	if( candidateSpots.size() < candidateSpots.capacity() / 2 ) {
		// If spot states get reset, some spot is likely to be selected on next GetRoamingSpot() call
		TryResetAllSpotsDisabledState();
	}

	int fromAreas[2];
	const int numFromAreas = self->ai->botRef->EntityPhysicsState()->PrepareRoutingStartAreas( fromAreas );
	for( int travelFlags: self->ai->botRef->TravelFlags() ) {
		int spotNum = TryFindReachableSpot( candidateSpots, travelFlags, fromAreas, numFromAreas );
		if( spotNum >= 0 ) {
			return spotNum;
		}
	}

	return -1;
}

int BotRoamingManager::TryFindReachableSpot( const Candidates &candidateSpots, int travelFlags,
											 const int *fromAreaNums, int numFromAreas ) {
	const auto *spots = tacticalSpotsRegistry->spots;
	const auto *routeCache = self->ai->botRef->routeCache;

	int travelTime = 0;
	// Add an optimized branch for this common case
	if( numFromAreas == 1 ) {
		for( unsigned i = 0; i < candidateSpots.size(); ++i ) {
			const auto spotNum = candidateSpots[i];
			const int spotAreaNum = spots[spotNum].aasAreaNum;
			if( ( travelTime = routeCache->TravelTimeToGoalArea( fromAreaNums[0], spotAreaNum, travelFlags ) ) ) {
				return spotNum;
			}
		}
	} else {
		for( unsigned i = 0; i < candidateSpots.size(); ++i ) {
			const auto spotNum = candidateSpots[i];
			const int spotAreaNum = spots[spotNum].aasAreaNum;
			for( int j = 0; j < numFromAreas; ++j ) {
				if( ( travelTime = routeCache->TravelTimeToGoalArea( fromAreaNums[j], spotAreaNum, travelFlags ) ) ) {
					return spotNum;
				}
			}
		}
	}

	return -1;
}

int BotRoamingManager::TryFindReachableArea( const Candidates &candidateAreas, int travelFlags,
											 const int *fromAreaNums, int numFromAreas ) {
	const auto *routeCache = self->ai->botRef->routeCache;

	int travelTime = 0;
	if( numFromAreas == 1 ) {
		for( unsigned i = 0; i < candidateAreas.size(); ++i ) {
			const auto areaNum = candidateAreas[i];
			if( ( travelTime = routeCache->TravelTimeToGoalArea( fromAreaNums[0], areaNum, travelFlags ) ) ) {
				return areaNum;
			}
		}
	} else {
		for( unsigned i = 0; i < candidateAreas.size(); ++i ) {
			const auto areaNum = candidateAreas[i];
			for( int j = 0; j < numFromAreas; ++j ) {
				if( ( travelTime = routeCache->TravelTimeToGoalArea( fromAreaNums[j], areaNum, travelFlags ) ) ) {
					return areaNum;
				}
			}
		}
	}

	return 0;
}

int BotRoamingManager::TrySuggestRandomAasArea() {
	const int currAreaNum = self->ai->botRef->movementState.entityPhysicsState.CurrAasAreaNum();
	const int groundedAreaNum = self->ai->botRef->movementState.entityPhysicsState.DroppedToFloorAasAreaNum();

	const int numAreas = aasWorld->NumAreas();
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasAreaSettings = aasWorld->AreaSettings();

	Candidates candidateAreas;

	for( unsigned i = 0; i < candidateAreas.capacity(); ++i ) {
		int areaNum = (int)( brandom( 1.0f, numAreas - 0.1f ) );
		if( areaNum == currAreaNum ) {
			continue;
		}

		if( !IsFeasibleArea( aasAreas[areaNum], aasAreaSettings[areaNum] ) ) {
			continue;
		}

		if( areaNum == groundedAreaNum ) {
			continue;
		}

		candidateAreas.push_back( areaNum );
	}

	int fromAreaNums[] = { currAreaNum, groundedAreaNum };
	int numFromAreas = fromAreaNums[0] == fromAreaNums[1] ? 2 : 1;
	for( int travelFlags: self->ai->botRef->TravelFlags() ) {
		if( int areaNum = TryFindReachableArea( candidateAreas, travelFlags, fromAreaNums, numFromAreas ) ) {
			return areaNum;
		}
	}

	return 0;
}

int BotRoamingManager::TrySuggestNearbyAasArea() {
	Vec3 mins( -192, -192, -128 );
	Vec3 maxs( +192, +192, +128 );
	mins += self->s.origin;
	maxs += self->s.origin;
	int bboxAreaNums[64];
	const int numBBoxAreas = aasWorld->BBoxAreas( mins, maxs, bboxAreaNums, 64 );

	const int currAreaNum = self->ai->botRef->movementState.entityPhysicsState.CurrAasAreaNum();
	const int groundedAreaNum = self->ai->botRef->movementState.entityPhysicsState.DroppedToFloorAasAreaNum();

	const auto *aasAreas = aasWorld->Areas();
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	Candidates candidateAreas;

	if( (int)candidateAreas.capacity() >= numBBoxAreas ) {
		for( int i = 0; i < numBBoxAreas; ++i ) {
			int areaNum = bboxAreaNums[i];
			if( currAreaNum == areaNum ) {
				continue;
			}

			if( IsFeasibleArea( aasAreas[areaNum], aasAreaSettings[areaNum] ) ) {
				continue;
			}

			// Test it last (its unlikely to fail)
			if( groundedAreaNum == areaNum ) {
				continue;
			}

			candidateAreas.push_back( areaNum );
		}
	} else {
		for( int i = 0; i < numBBoxAreas; ++i ) {
			int areaNum = bboxAreaNums[i];
			if( currAreaNum == areaNum ) {
				continue;
			}

			if( IsFeasibleArea( aasAreas[areaNum], aasAreaSettings[areaNum] ) ) {
				continue;
			}

			if( groundedAreaNum == areaNum ) {
				continue;
			}

			candidateAreas.push_back( areaNum );
			if( candidateAreas.size() == candidateAreas.capacity() ) {
				break;
			}
		}
	}

	int fromAreaNums[] = { currAreaNum, groundedAreaNum };
	int numFromAreas = fromAreaNums[0] == fromAreaNums[1] ? 1 : 2;
	for( int travelFlags: self->ai->botRef->TravelFlags() ) {
		if( int areaNum = TryFindReachableArea( candidateAreas, travelFlags, fromAreaNums, numFromAreas ) ) {
			return areaNum;
		}
	}

	return 0;
}

bool BotRoamingManager::IsFeasibleArea( const aas_area_t &area, const aas_areasettings_t &areaSettings ) {
	if( !( areaSettings.areaflags & AREA_GROUNDED ) ) {
		return false;
	}

	if( areaSettings.areaflags & ( AREA_JUNK | AREA_DISABLED ) ) {
		return false;
	}

	if( areaSettings.contents & AREACONTENTS_DONOTENTER ) {
		return false;
	}

	if( area.maxs[0] - area.mins[0] < 24.0f || area.maxs[1] - area.mins[1] < 24.0f ) {
		return false;
	}

	Vec3 areaPoint( area.center );
	areaPoint.Z() = area.mins[2] + 16.0f;
	if( areaPoint.SquareDistanceTo( self->s.origin ) <= 144.0f ) {
		return false;
	}

	return true;
}

void BotRoamingManager::TryResetAllSpotsDisabledState() {
	const int64_t levelTime = level.time;
	// Check whether there are unvisited/expired spots
	unsigned numEnabledSpots = 0;
	for( unsigned i = 0, end = tacticalSpotsRegistry->numSpots; i < end; ++i ) {
		if( !IsTemporarilyDisabled( i, levelTime ) ) {
			numEnabledSpots++;
			if( numEnabledSpots > 3 ) {
				return;
			}
		}
	}

	// If (almost) all spots are visited (and have not expired yet), make all spots available to visit
	ClearVisitedSpots();
}

void BotRoamingManager::DisableSpotsInRadius( const vec3_t origin, float radius ) {
	uint16_t spotNums[TacticalSpotsRegistry::MAX_SPOTS_PER_QUERY];
	TacticalSpotsRegistry::OriginParams originParams( self, radius, self->ai->botRef->routeCache );
	uint16_t insideSpotNum = std::numeric_limits<uint16_t>::max();
	const unsigned numNearbySpots = tacticalSpotsRegistry->FindSpotsInRadius( originParams, spotNums, &insideSpotNum );
	const int64_t levelTime = level.time;

	// Do not count spots behind a wall / an obstacle as visited.
	trace_t trace;
	for( unsigned i = 0; i < numNearbySpots; ++i ) {
		const unsigned spotNum = spotNums[i];
		if( spotNum != insideSpotNum ) {
			const auto &spot = tacticalSpotsRegistry->spots[spotNum];
			SolidWorldTrace( &trace, self->s.origin, spot.origin );
			if( trace.fraction != 1.0f ) {
				continue;
			}
		}
		visitedAt[spotNum] = levelTime;
	}
}
