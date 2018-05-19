#ifndef QFUSION_BUNNYTESTINGMULTIPLELOOKDIRSACTION_H
#define QFUSION_BUNNYTESTINGMULTIPLELOOKDIRSACTION_H

#include "GenericBunnyingAction.h"

class BotBunnyTestingMultipleLookDirsAction : public GenericRunBunnyingAction
{
protected:
	static constexpr auto MAX_SUGGESTED_LOOK_DIRS = 16;

	StaticVector<Vec3, MAX_SUGGESTED_LOOK_DIRS> suggestedLookDirs;
	// Contains areas that were used in dirs construction.
	// Might be useful by skipping areas already tested by other (also an descendant of this class) action.
	// Note that 1-1 correspondence between dirs and areas (and even dirs size and areas size) is not mandatory.
	StaticVector<int, MAX_SUGGESTED_LOOK_DIRS> dirsBaseAreas;

	unsigned maxSuggestedLookDirs;
	unsigned currSuggestedLookDirNum;
	BaseMovementAction *suggestedAction;

	virtual void SaveSuggestedLookDirs( MovementPredictionContext *context ) = 0;

	// A helper method to select best N areas that is optimized for small areas count.
	// Modifies the collection in-place putting best areas at its beginning.
	// Returns the new end iterator for the selected areas range.
	// The begin iterator is assumed to remain the same.
	inline AreaAndScore *TakeBestCandidateAreas( AreaAndScore *inputBegin, AreaAndScore *inputEnd, unsigned maxAreas );

	void SaveCandidateAreaDirs( MovementPredictionContext *context,
								AreaAndScore *candidateAreasBegin,
								AreaAndScore *candidateAreasEnd );

	// Used for candidate spots selection.
	// Tracing a straight line between two points fails in stairs-like environment way too often.
	// This routine uses extremely coarse arc approximation which still should be sufficient
	// to avoid the mentioned failure in some environment kinds.
	bool TraceArcInSolidWorld( const AiEntityPhysicsState &startPhysicsState, const vec3_t from, const vec3_t to );
public:
	BotBunnyTestingMultipleLookDirsAction( class Bot *bot_, const char *name_, int debugColor_ )
		: GenericRunBunnyingAction( bot_, name_, debugColor_ )
		, maxSuggestedLookDirs( MAX_SUGGESTED_LOOK_DIRS )
		, currSuggestedLookDirNum( 0 )
		, suggestedAction( nullptr ) {}

	void BeforePlanning() override;
	void OnApplicationSequenceStarted( MovementPredictionContext *context ) override;
	void OnApplicationSequenceStopped( MovementPredictionContext *context,
									   SequenceStopReason stopReason,
									   unsigned stoppedAtFrameIndex ) override;
	void PlanPredictionStep( MovementPredictionContext *context ) override;
};

#endif
