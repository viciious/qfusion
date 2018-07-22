#ifndef QFUSION_SPOTSPROBLEMSOLVERSLOCAL_H
#define QFUSION_SPOTSPROBLEMSOLVERSLOCAL_H

#include "TacticalSpotsProblemSolver.h"

typedef TacticalSpotsProblemSolver::SpotsAndScoreVector SpotsAndScoreVector;

inline float ComputeDistanceFactor( float distance, float weightFalloffDistanceRatio, float searchRadius ) {
	float weightFalloffRadius = weightFalloffDistanceRatio * searchRadius;
	if( distance < weightFalloffRadius ) {
		return distance / weightFalloffRadius;
	}

	return 1.0f - ( ( distance - weightFalloffRadius ) / ( 0.000001f + searchRadius - weightFalloffRadius ) );
}

inline float ComputeDistanceFactor( const vec3_t v1,
									const vec3_t v2,
									float weightFalloffDistanceRatio,
									float searchRadius ) {
	float squareDistance = DistanceSquared( v1, v2 );
	float distance = 1.0f;
	if( squareDistance >= 1.0f ) {
		distance = 1.0f / Q_RSqrt( squareDistance );
	}

	return ComputeDistanceFactor( distance, weightFalloffDistanceRatio, searchRadius );
}

// Units of travelTime and maxFeasibleTravelTime must match!
inline float ComputeTravelTimeFactor( int travelTime, float maxFeasibleTravelTime ) {
	float factor = 1.0f - BoundedFraction( travelTime, maxFeasibleTravelTime );
	return 1.0f / Q_RSqrt( 0.0001f + factor );
}

inline float ApplyFactor( float value, float factor, float factorInfluence ) {
	float keptPart = value * ( 1.0f - factorInfluence );
	float modifiedPart = value * factor * factorInfluence;
	return keptPart + modifiedPart;
}

#endif
