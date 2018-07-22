#ifndef QFUSION_DODGEHAZARDPROBLEMSOLVER_H
#define QFUSION_DODGEHAZARDPROBLEMSOLVER_H

#include "TacticalSpotsProblemSolver.h"

class DodgeHazardProblemSolver: public TacticalSpotsProblemSolver {
public:
	class ProblemParams : public BaseProblemParams {
		friend class DodgeHazardProblemSolver;
		const Vec3 &hazardHitPoint;
		const Vec3 &hazardDirection;
		const bool avoidSplashDamage;
	public:
		ProblemParams( const Vec3 &hazardHitPoint_, const Vec3 &hazardDirection_, bool avoidSplashDamage_ )
			: hazardHitPoint( hazardHitPoint_ )
			, hazardDirection( hazardDirection_ )
			, avoidSplashDamage( avoidSplashDamage_ ) {}
	};
private:
	const ProblemParams &problemParams;

	Vec3 MakeDodgeHazardDir( bool *mightNegateDodgeDir ) const;

	SpotsAndScoreVector &SelectCandidateSpots( const SpotsQueryVector &spotsFromQuery ) override;
	void TryModifyingScoreByVelocityConformance( SpotsAndScoreVector &reachCheckedSpots );
public:
	DodgeHazardProblemSolver( const OriginParams &originParams_, const ProblemParams &problemParams_ )
		: TacticalSpotsProblemSolver( originParams_, problemParams_ ), problemParams( problemParams_ ) {}

	int FindMany( vec3_t *spots, int numSpots ) override;
};

#endif
