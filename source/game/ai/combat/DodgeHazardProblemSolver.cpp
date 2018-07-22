#include "DodgeHazardProblemSolver.h"
#include "SpotsProblemSolversLocal.h"

int DodgeHazardProblemSolver::FindMany( vec3_t *spotOrigins, int maxSpots ) {
	uint16_t insideSpotNum;
	const SpotsQueryVector &spotsFromQuery = tacticalSpotsRegistry->FindSpotsInRadius( originParams, &insideSpotNum );
	SpotsAndScoreVector &candidateSpots =  SelectCandidateSpots( spotsFromQuery );
	SpotsAndScoreVector &reachCheckedSpots = CheckSpotsReach( candidateSpots, insideSpotNum );
	TryModifyingScoreByVelocityConformance( reachCheckedSpots );
	return CleanupAndCopyResults( reachCheckedSpots, spotOrigins, maxSpots );
}

void DodgeHazardProblemSolver::TryModifyingScoreByVelocityConformance( SpotsAndScoreVector &reachCheckedSpots ) {
	const edict_t *ent = originParams.originEntity;
	if( !ent ) {
		return;
	}

	// Make sure that the current entity params match problem params
	if( !VectorCompare( ent->s.origin, originParams.origin ) ) {
		return;
	}

	const float squareSpeed = VectorLengthSquared( ent->velocity );
	const float runSpeed = DEFAULT_PLAYERSPEED;
	if( squareSpeed <= runSpeed * runSpeed ) {
		return;
	}

	const float speed = sqrtf( squareSpeed );
	Vec3 velocityDir( ent->velocity );
	velocityDir *= 1.0f / speed;
	const float dashSpeed = DEFAULT_DASHSPEED;
	assert( dashSpeed > runSpeed );

	// Grow influence up to dash speed so the score is purely based
	// on the "velocity dot factor" on speed equal or greater the dash speed.
	const auto *const spots = tacticalSpotsRegistry->spots;
	const float influence = sqrtf( BoundedFraction( speed - runSpeed, dashSpeed - runSpeed ) );
	for( SpotAndScore &spotAndScore: reachCheckedSpots ) {
		Vec3 toSpotDir( spots[spotAndScore.spotNum].origin );
		toSpotDir -= ent->s.origin;
		toSpotDir.NormalizeFast();
		// [0..1]
		float velocityDotFactor = 0.5f * ( 1.0f + velocityDir.Dot( toSpotDir ) );
		spotAndScore.score = spotAndScore.score * ( 1.0f - influence ) + influence * velocityDotFactor;
	}
}

SpotsAndScoreVector &DodgeHazardProblemSolver::SelectCandidateSpots( const SpotsQueryVector &spotsFromQuery ) {
	bool mightNegateDodgeDir = false;
	Vec3 dodgeDir = MakeDodgeHazardDir( &mightNegateDodgeDir );

	const float searchRadius = originParams.searchRadius;
	const float minHeightAdvantageOverOrigin = problemParams.minHeightAdvantageOverOrigin;
	const float heightOverOriginInfluence = problemParams.heightOverOriginInfluence;
	const float originZ = originParams.origin[2];
	// Copy to stack for faster access
	Vec3 origin( originParams.origin );

	const auto *const spots = tacticalSpotsRegistry->spots;

	SpotsAndScoreVector &result = tacticalSpotsRegistry->temporariesAllocator.GetNextCleanSpotsAndScoreVector();

	if( mightNegateDodgeDir ) {
		for( auto spotNum: spotsFromQuery ) {
			const TacticalSpot &spot = spots[spotNum];

			float heightOverOrigin = spot.absMins[2] - originZ;
			if( heightOverOrigin < minHeightAdvantageOverOrigin ) {
				continue;
			}

			Vec3 toSpotDir( spot.origin );
			toSpotDir -= origin;
			float squaredDistanceToSpot = toSpotDir.SquaredLength();
			if( squaredDistanceToSpot < 1 ) {
				continue;
			}

			toSpotDir *= Q_RSqrt( squaredDistanceToSpot );
			float dot = toSpotDir.Dot( dodgeDir );
			if( dot < 0.2f ) {
				continue;
			}

			heightOverOrigin -= minHeightAdvantageOverOrigin;
			float heightOverOriginFactor = BoundedFraction( heightOverOrigin, searchRadius - minHeightAdvantageOverOrigin );
			float score = ApplyFactor( dot, heightOverOriginFactor, heightOverOriginInfluence );

			result.push_back( SpotAndScore( spotNum, score ) );
		}
	} else {
		for( auto spotNum: spotsFromQuery ) {
			const TacticalSpot &spot = spots[spotNum];

			float heightOverOrigin = spot.absMins[2] - originZ;
			if( heightOverOrigin < minHeightAdvantageOverOrigin ) {
				continue;
			}

			Vec3 toSpotDir( spot.origin );
			toSpotDir -= origin;
			float squaredDistanceToSpot = toSpotDir.SquaredLength();
			if( squaredDistanceToSpot < 1 ) {
				continue;
			}

			toSpotDir *= Q_RSqrt( squaredDistanceToSpot );
			float absDot = fabsf( toSpotDir.Dot( dodgeDir ) );
			if( absDot < 0.2f ) {
				continue;
			}

			heightOverOrigin -= minHeightAdvantageOverOrigin;
			float heightOverOriginFactor = BoundedFraction( heightOverOrigin, searchRadius - minHeightAdvantageOverOrigin );
			float score = ApplyFactor( absDot, heightOverOriginFactor, heightOverOriginInfluence );

			result.push_back( SpotAndScore( spotNum, score ) );
		}
	}

	// Sort result so best score areas are first
	std::sort( result.begin(), result.end() );
	return result;
}

Vec3 DodgeHazardProblemSolver::MakeDodgeHazardDir( bool *mightNegateDodgeDir ) const {
	*mightNegateDodgeDir = false;
	if( problemParams.avoidSplashDamage ) {
		Vec3 result( 0, 0, 0 );
		Vec3 originToHitDir = problemParams.hazardHitPoint - originParams.origin;
		float degrees = originParams.originEntity ? -originParams.originEntity->s.angles[YAW] : -90;
		RotatePointAroundVector( result.Data(), &axis_identity[AXIS_UP], originToHitDir.Data(), degrees );
		result.NormalizeFast();

		if( fabs( result.X() ) < 0.3 ) {
			result.X() = 0;
		}
		if( fabs( result.Y() ) < 0.3 ) {
			result.Y() = 0;
		}
		result.Z() = 0;
		result.X() *= -1.0f;
		result.Y() *= -1.0f;
		return result;
	}

	Vec3 selfToHitPoint = problemParams.hazardHitPoint - originParams.origin;
	selfToHitPoint.Z() = 0;
	// If bot is not hit in its center, try pick a direction that is opposite to a vector from bot center to hit point
	if( selfToHitPoint.SquaredLength() > 4 * 4 ) {
		selfToHitPoint.NormalizeFast();
		// Check whether this direction really helps to dodge the danger
		// (the less is the abs. value of the dot product, the closer is the chosen direction to a perpendicular one)
		if( fabsf( selfToHitPoint.Dot( originParams.origin ) ) < 0.5f ) {
			if( fabsf( selfToHitPoint.X() ) < 0.3f ) {
				selfToHitPoint.X() = 0;
			}
			if( fabsf( selfToHitPoint.Y() ) < 0.3f ) {
				selfToHitPoint.Y() = 0;
			}
			return -selfToHitPoint;
		}
	}

	*mightNegateDodgeDir = true;
	// Otherwise just pick a direction that is perpendicular to the danger direction
	float maxCrossSqLen = 0.0f;
	Vec3 result( 0, 1, 0 );
	for( int i = 0; i < 3; ++i ) {
		Vec3 cross = problemParams.hazardDirection.Cross( &axis_identity[i * 3] );
		cross.Z() = 0;
		float crossSqLen = cross.SquaredLength();
		if( crossSqLen > maxCrossSqLen ) {
			maxCrossSqLen = crossSqLen;
			float invLen = Q_RSqrt( crossSqLen );
			result.X() = cross.X() * invLen;
			result.Y() = cross.Y() * invLen;
		}
	}
	return result;
}