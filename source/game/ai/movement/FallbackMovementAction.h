#ifndef QFUSION_DUMMYMOVEMENTACTION_H
#define QFUSION_DUMMYMOVEMENTACTION_H

#include "BaseMovementAction.h"

class MovementFallback;

class FallbackMovementAction : public BaseMovementAction
{
	struct ClosestTriggerProblemParams {
	private:
		vec3_t origin;
		int fromAreaNums[2];
	public:
		const int numFromAreas;
		const int goalAreaNum;

		ClosestTriggerProblemParams( const vec3_t origin_, const int *fromAreaNums_, int numFromAreas_, int goalAreaNum_ )
			: numFromAreas( numFromAreas_ ), goalAreaNum( goalAreaNum_ ) {
			VectorCopy( origin_, this->origin );
			fromAreaNums[0] = numFromAreas_ > 0 ? fromAreaNums_[0] : 0;
			fromAreaNums[1] = numFromAreas_ > 1 ? fromAreaNums_[1] : 0;
		}

		const float *Origin() const { return origin; }
		const int *FromAreaNums() const { return fromAreaNums; }
	};

	const edict_t *FindClosestToTargetTrigger( MovementPredictionContext *context );

	const edict_t *FindClosestToTargetTrigger( const ClosestTriggerProblemParams &problemParams,
											   const MovementPredictionContext::NearbyTriggersCache &triggersCache );

	const edict_t *FindClosestToTargetTrigger( const ClosestTriggerProblemParams &problemParams,
											   const uint16_t *triggerEntNums,
											   int numTriggerEnts, int *travelTime );

	void SetupNavTargetAreaMovement( MovementPredictionContext *context );
	void SetupLostNavTargetMovement( MovementPredictionContext *context );
	MovementFallback *TryFindMovementFallback( MovementPredictionContext *context );
	MovementFallback *TryFindAasBasedFallback( MovementPredictionContext *context );

	MovementFallback *TryFindWalkReachFallback( MovementPredictionContext *context,
												   const aas_reachability_t &nextReach );

	MovementFallback *TryFindWalkOffLedgeReachFallback( MovementPredictionContext *context,
														   const aas_reachability_t &nextReach );

	MovementFallback *TryFindJumpLikeReachFallback( MovementPredictionContext *context,
													   const aas_reachability_t &nextReach );

	MovementFallback *TryFindStairsFallback( MovementPredictionContext *context );
	bool TrySetupInclinedFloorMovement( MovementPredictionContext *context, int rampAreaNum );
	MovementFallback *TryFindRampFallback( MovementPredictionContext *context, int rampAreaNum, int forbiddenAreaNum = 0 );
	MovementFallback *TryFindLostNavTargetFallback( MovementPredictionContext *context );
	MovementFallback *TryFindNearbyRampAreasFallback( MovementPredictionContext *context );
	MovementFallback *TryFindWalkableTriggerFallback( MovementPredictionContext *context );

	MovementFallback *TryFindJumpFromLavaFallback( MovementPredictionContext *context ) {
		return TryFindJumpToSpotFallback( context, false );
	}

	MovementFallback *TryFindJumpAdvancingToTargetFallback( MovementPredictionContext *context );

	MovementFallback *TryFindJumpToSpotFallback( MovementPredictionContext *context, bool testTravelTime );

	MovementFallback *TryNodeBasedFallbacksLeft( MovementPredictionContext *context );

	MovementFallback *TryShortcutOtherFallbackByJumping( MovementPredictionContext *context,
															const vec3_t initialTarget,
															int initialTargetAreaNum = 0 );

	MovementFallback *TryShortcutOtherFallbackByJumping( MovementPredictionContext *context,
															int initialTargetAreaNum );
public:
	DECLARE_MOVEMENT_ACTION_CONSTRUCTOR( FallbackMovementAction, COLOR_RGB( 0, 0, 0 ) ) {}
	void PlanPredictionStep( MovementPredictionContext *context ) override;
	void CheckPredictionStepResults( MovementPredictionContext *context ) override {
		AI_FailWith( __FUNCTION__, "This method should never get called (PlanMovmementStep() should stop planning)\n" );
	}
};

#endif
