#include "MovementLocal.h"
#include "ReachChainInterpolator.h"
#include "SameFloorClusterAreasCache.h"

bool ReachChainInterpolator::TrySetDirToRegionExitArea( Context *context, const aas_area_t &area, float distanceThreshold ) {
	const float *origin = context->movementState->entityPhysicsState.Origin();

	Vec3 areaPoint( area.center );
	areaPoint.Z() = area.mins[2] + 32.0f;
	if( areaPoint.SquareDistanceTo( origin ) < SQUARE( distanceThreshold ) ) {
		return false;
	}

	intendedLookDir.Set( areaPoint );
	intendedLookDir -= origin;
	intendedLookDir.NormalizeFast();
	return true;
}

bool ReachChainInterpolator::Exec( Context *context ) {
	trace_t trace;
	vec3_t firstReachDir;
	const auto &reachChain = context->NextReachChain();
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasReach = aasWorld->Reachabilities();
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const auto *aasAreaFloorClusterNums = aasWorld->AreaFloorClusterNums();
	const auto *aasAreaStairsClusterNums = aasWorld->AreaStairsClusterNums();
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const float *origin = entityPhysicsState.Origin();
	const aas_reachability_t *singleFarReach = nullptr;
	const float squareDistanceThreshold = SQUARE( stopAtDistance );
	const int navTargetAreaNum = context->NavTargetAasAreaNum();
	unsigned numReachFound = 0;
	int currAreaFloorClusterNum = 0;
	int currAreaNum = context->CurrAasAreaNum();
	bool endsInNavTargetArea = false;

	// Check for quick shortcuts for special cases when a bot is already inside a stairs cluster or a ramp.
	// This should reduce CPU cycles wasting on interpolation attempts inside these kinds of environment.
	// Using this when a bot is not already in the special kind of environemnt is more complicated,
	// the question is what rules should be followed? So it is not implemented.
	if( int currGroundedAreaNum = context->CurrGroundedAasAreaNum() ) {
		currAreaNum = currGroundedAreaNum;
		currAreaFloorClusterNum = aasAreaFloorClusterNums[currGroundedAreaNum];
		// Stairs clusters and inclined floor areas are mutually exclusive
		if( aasAreaSettings[currGroundedAreaNum].areaflags & AREA_INCLINED_FLOOR ) {
			if( const auto *exitAreaNum = TryFindBestInclinedFloorExitArea( context, currGroundedAreaNum ) ) {
				if( TrySetDirToRegionExitArea( context, aasAreas[*exitAreaNum] ) ) {
					return true;
				}
			}
		} else if( int stairsClusterNum = aasAreaStairsClusterNums[currGroundedAreaNum] ) {
			if( const auto *exitAreaNum = TryFindBestStairsExitArea( context, stairsClusterNum ) ) {
				if( TrySetDirToRegionExitArea( context, aasAreas[*exitAreaNum] ) ) {
					return true;
				}
			}
		}
	}

	intendedLookDir.Set( 0, 0, 0 );

	// The area the reachability starts in
	int reachStartArea = context->CurrAasAreaNum();
	for( unsigned i = 0; i < reachChain.size(); ++i ) {
		const auto &reach = aasReach[reachChain[i].ReachNum()];

		int travelType = reach.traveltype & TRAVELTYPE_MASK;
		// If the reach type is not in compatible types, we will have to make an immediate or pending break of the loop
		if( !IsCompatibleReachType( travelType ) ) {
			// If the reach type is not even mentioned in the allowed stop reach types, break immediately
			if( !IsAllowedEndReachType( travelType ) ) {
				break;
			}

			// If the reach type is mentioned in the allowed stop reach types, process the reach but stop at it.
			// This line acts as a pending break after the iteration
			i = reachChain.size() + 1;
		}

		if( DistanceSquared( origin, reach.start ) > squareDistanceThreshold ) {
			assert( !singleFarReach );
			// Check for possible CM trace replacement by much cheaper 2D raycasting in floor cluster
			if( currAreaFloorClusterNum && currAreaFloorClusterNum == aasAreaFloorClusterNums[reachStartArea] ) {
				if( IsAreaWalkableInFloorCluster( currAreaNum, reachStartArea ) ) {
					singleFarReach = &reach;
				}
			} else {
				// The trace segment might be very long, test PVS first
				if( trap_inPVS( origin, reach.start ) ) {
					SolidWorldTrace( &trace, origin, reach.start );
					if( trace.fraction == 1.0f ) {
						singleFarReach = &reach;
					}
				}
			}
			break;
		}

		// Check for possible CM trace replacement by much cheaper 2D raycasting in floor cluster
		if( currAreaFloorClusterNum && currAreaFloorClusterNum == aasAreaFloorClusterNums[reachStartArea] ) {
			if( !IsAreaWalkableInFloorCluster( currAreaNum, reachStartArea ) ) {
				break;
			}
		} else {
			SolidWorldTrace( &trace, origin, reach.start );
			if( trace.fraction != 1.0f ) {
				break;
			}
		}

		if( reach.areanum == navTargetAreaNum ) {
			endsInNavTargetArea = true;
		}

		// The next reachability starts in this area
		reachStartArea = reach.areanum;

		Vec3 reachDir( reach.start );
		reachDir -= origin;
		reachDir.NormalizeFast();
		// Add a reach dir to the dirs list (be optimistic to avoid extra temporaries)
		if( numReachFound ) {
			// Limit dirs angular spread checking against a first found dir as a base dir
			if( reachDir.Dot( firstReachDir ) < 0.5f ) {
				break;
			}
		} else {
			reachDir.CopyTo( firstReachDir );
		}

		intendedLookDir += reachDir;
		numReachFound++;
	}

	if( !numReachFound ) {
		if( !singleFarReach ) {
			if( context->IsInNavTargetArea() ) {
				intendedLookDir.Set( context->NavTargetOrigin() );
				intendedLookDir -= origin;
				intendedLookDir.NormalizeFast();
				return true;
			}
			return false;
		}

		intendedLookDir.Set( singleFarReach->start );
		intendedLookDir -= origin;
		intendedLookDir.NormalizeFast();
		return true;
	}

	if( endsInNavTargetArea ) {
		Vec3 navTargetOrigin( context->NavTargetOrigin() );
		SolidWorldTrace( &trace, origin, navTargetOrigin.Data() );
		if( trace.fraction == 1.0f ) {
			// Add the direction to the nav target to the interpolated dir
			Vec3 toTargetDir( navTargetOrigin );
			toTargetDir -= origin;
			toTargetDir.NormalizeFast();
			if( toTargetDir.Dot( firstReachDir ) > 0.5f ) {
				intendedLookDir += toTargetDir;
				// Count it as an additional interpolated reachability
				numReachFound++;
			}
		}
	}

	// If there were more than a single reach dir added to the look dir, it requires normalization
	if( numReachFound > 1 ) {
		intendedLookDir.NormalizeFast();
	}

	return true;
}