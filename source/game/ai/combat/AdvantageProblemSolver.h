#ifndef QFUSION_ADVANTAGEPROBLEMSOLVER_H
#define QFUSION_ADVANTAGEPROBLEMSOLVER_H

#include "TacticalSpotsProblemSolver.h"

class AdvantageProblemSolver: public TacticalSpotsProblemSolver {
public:
	class ProblemParams : public BaseProblemParams {
		friend class AdvantageProblemSolver;

		const edict_t *keepVisibleEntity;
		vec3_t keepVisibleOrigin;
		float minSpotDistanceToEntity { 0.0f };
		float maxSpotDistanceToEntity { 999999.0f };
		float entityDistanceInfluence { 0.5f };
		float entityWeightFalloffDistanceRatio { 0.0f };
		float minHeightAdvantageOverEntity { -999999.0f };
		float heightOverEntityInfluence { 0.5f };
	public:
		explicit ProblemParams( const edict_t *keepVisibleEntity_ )
			: keepVisibleEntity( keepVisibleEntity_ ) {
			VectorCopy( keepVisibleEntity->s.origin, this->keepVisibleOrigin );
		}

		explicit ProblemParams( const vec3_t keepVisibleOrigin_ )
			: keepVisibleEntity( nullptr ) {
			VectorCopy( keepVisibleOrigin_, this->keepVisibleOrigin );
		}

		inline void SetMinSpotDistanceToEntity( float distance ) { minSpotDistanceToEntity = distance; }
		inline void SetMaxSpotDistanceToEntity( float distance ) { maxSpotDistanceToEntity = distance; }
		inline void SetEntityDistanceInfluence( float influence ) { entityDistanceInfluence = influence; }

		inline void SetEntityWeightFalloffDistanceRatio( float ratio ) {
			entityWeightFalloffDistanceRatio = Clamp( ratio );
		}

		inline void SetMinHeightAdvantageOverEntity( float height ) { minHeightAdvantageOverEntity = height; }

		inline void SetHeightOverEntityInfluence( float influence ) {
			heightOverEntityInfluence = Clamp( influence );
		}
	};
private:
	const ProblemParams &problemParams;

	SpotsAndScoreVector &CheckOriginVisibility( SpotsAndScoreVector &candidateSpots, int maxSpots );

	void SortByVisAndOtherFactors( SpotsAndScoreVector &spots );

	SpotsAndScoreVector &SelectCandidateSpots( const SpotsQueryVector &spotsFromQuery ) override;
public:
	AdvantageProblemSolver( const OriginParams &originParams_, const ProblemParams &problemParams_ )
		: TacticalSpotsProblemSolver( originParams_, problemParams_ ), problemParams( problemParams_ ) {}

	int FindMany( vec3_t *spots, int maxSpots ) override;
};

#endif
