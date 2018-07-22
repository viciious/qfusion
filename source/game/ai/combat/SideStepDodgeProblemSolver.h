#ifndef QFUSION_SIDESTEPDODGEPROBLEMSOLVER_H
#define QFUSION_SIDESTEPDODGEPROBLEMSOLVER_H

#include "TacticalSpotsProblemSolver.h"

class SideStepDodgeProblemSolver: public TacticalSpotsProblemSolver {
public:
	class ProblemParams: public BaseProblemParams {
		friend class SideStepDodgeProblemSolver;
		vec3_t keepVisibleOrigin;
	public:
		explicit ProblemParams( const vec3_t keepVisibleOrigin_ ) {
			VectorCopy( keepVisibleOrigin_, this->keepVisibleOrigin );
		}
	};
private:
	ProblemParams &problemParams;

	int FindMany( vec3_t *, int ) override {
		AI_FailWith( "SideStepDodgeProblemSolver::FindMany()", "Should not be called" );
	}
public:
	SideStepDodgeProblemSolver( OriginParams &originParams_, ProblemParams &problemParams_ )
		: TacticalSpotsProblemSolver( originParams_, problemParams_ ), problemParams( problemParams_ ) {}

	bool FindSingle( vec3_t spot ) override;
};

#endif