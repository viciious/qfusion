#ifndef QFUSION_REACHCHAININTERPOLATOR_H
#define QFUSION_REACHCHAININTERPOLATOR_H

#include "../ai_local.h"

class MovementPredictionContext;

struct ReachChainInterpolator {
	Vec3 intendedLookDir;
	// Continue interpolating while a next reach has these travel types
	const int *compatibleReachTypes;
	int numCompatibleReachTypes;
	// Stop interpolating on these reach types but include a reach start in interpolation
	const int *allowedEndReachTypes;
	int numAllowedEndReachTypes;
	// Note: Ignored when there is only a single far reach.
	float stopAtDistance;

	inline ReachChainInterpolator()
		: intendedLookDir( 0, 0, 0 )
		, compatibleReachTypes( nullptr )
		, numCompatibleReachTypes( 0 )
		, allowedEndReachTypes( nullptr )
		, numAllowedEndReachTypes( 0 )
		, stopAtDistance( 256 )
	{}

	inline void SetCompatibleReachTypes( const int *reachTravelTypes, int numTravelTypes ) {
		this->compatibleReachTypes = reachTravelTypes;
		this->numCompatibleReachTypes = numTravelTypes;
	}

	inline void SetAllowedEndReachTypes( const int *reachTravelTypes, int numTravelTypes ) {
		this->allowedEndReachTypes = reachTravelTypes;
		this->numAllowedEndReachTypes = numTravelTypes;
	}

	inline bool IsCompatibleReachType( int reachTravelType ) const {
		assert( ( reachTravelType & TRAVELTYPE_MASK ) == reachTravelType );
		const int *end = compatibleReachTypes + numCompatibleReachTypes;
		return std::find( compatibleReachTypes, end, reachTravelType ) != end;
	}

	inline bool IsAllowedEndReachType( int reachTravelType ) const {
		assert( ( reachTravelType & TRAVELTYPE_MASK ) == reachTravelType );
		const int *end = allowedEndReachTypes + numAllowedEndReachTypes;
		return std::find( allowedEndReachTypes, end, reachTravelType ) != end;
	}

	bool TrySetDirToRegionExitArea( MovementPredictionContext *context,
									const aas_area_t &area,
									float distanceThreshold = 64.0f );

	bool Exec( MovementPredictionContext *context );

	inline const Vec3 &Result() const { return intendedLookDir; }
};

#endif //QFUSION_REACHCHAININTERPOLATOR_H
