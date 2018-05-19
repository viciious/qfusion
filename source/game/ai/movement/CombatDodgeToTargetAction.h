#ifndef QFUSION_COMBATDODGETOTARGETACTION_H
#define QFUSION_COMBATDODGETOTARGETACTION_H

#include "BaseMovementAction.h"

class CombatDodgeSemiRandomlyToTargetAction : public BaseMovementAction
{
	int minTravelTimeToTarget;
	float totalCovered2DDistance;

	unsigned maxAttempts;
	unsigned attemptNum;

	inline bool ShouldTryRandomness() { return attemptNum < maxAttempts / 2; }
	inline bool ShouldTrySpecialMovement() {
		// HACK for easy bots to disable special movement in combat
		// (maxAttempts is 2 for easy bots and is 4 otherwise)
		// This approach seems more cache friendly than self->ai->botRef... chasing
		// not to mention we cannot access Bot members in this header due to its incomplete definition
		return maxAttempts > 2 && !( attemptNum & 1 );
	}

	void UpdateKeyMoveDirs( MovementPredictionContext *context );

public:
	DECLARE_MOVEMENT_ACTION_CONSTRUCTOR( CombatDodgeSemiRandomlyToTargetAction, COLOR_RGB( 192, 192, 192 ) ) {
		// Shut an analyzer up
		this->minTravelTimeToTarget = std::numeric_limits<int>::max();
		this->totalCovered2DDistance = 0.0f;
		this->maxAttempts = 0;
		this->attemptNum = 0;
	}
	void PlanPredictionStep( MovementPredictionContext *context ) override;
	void CheckPredictionStepResults( MovementPredictionContext *context ) override;
	void OnApplicationSequenceStarted( MovementPredictionContext *context ) override;
	void OnApplicationSequenceStopped( MovementPredictionContext *context,
									   SequenceStopReason stopReason,
									   unsigned stoppedAtFrameIndex ) override;
	void BeforePlanning() override;
};

#endif
