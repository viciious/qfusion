#ifndef QFUSION_LANDONSAVEDAREASACTION_H
#define QFUSION_LANDONSAVEDAREASACTION_H

#include "BaseMovementAction.h"

class LandOnSavedAreasAction : public BaseMovementAction
{
	friend class HandleTriggeredJumppadAction;
	friend class BotTryWeaponJumpShortcutMovementAction;

	StaticVector<int, MAX_SAVED_LANDING_AREAS> savedLandingAreas;
	typedef StaticVector<AreaAndScore, MAX_SAVED_LANDING_AREAS * 2> FilteredAreas;

	int currAreaIndex;
	unsigned totalTestedAreas;

	int FindJumppadAreaNum( const edict_t *jumppadEntity );

	// Returns a Z level when the landing is expected to be started
	float SaveJumppadLandingAreas( const edict_t *jumppadEntity );
	float SaveLandingAreasForJumppadTargetArea( const edict_t *jumppadEntity,
												int navTargetAreaNum,
												int jumppadTargetAreaNum );
	float SaveFilteredCandidateAreas( const edict_t *jumppadEntity,
									  int jumppadTargetAreaNum,
									  const FilteredAreas &filteredAreas );

public:
	DECLARE_MOVEMENT_ACTION_CONSTRUCTOR( LandOnSavedAreasAction, COLOR_RGB( 255, 0, 255 ) ) {
		// Shut an analyzer up
		this->currAreaIndex = 0;
		this->totalTestedAreas = 0;
	}

	bool TryLandingStepOnArea( int areaNum, MovementPredictionContext *context );
	void PlanPredictionStep( MovementPredictionContext *context ) override;
	void CheckPredictionStepResults( MovementPredictionContext *context ) override;
	void BeforePlanning() override;
	void AfterPlanning() override;
};

#endif
