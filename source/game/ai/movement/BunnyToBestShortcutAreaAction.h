#ifndef QFUSION_BUNNYTOBESTSHORTCUTAREAACTION_H
#define QFUSION_BUNNYTOBESTSHORTCUTAREAACTION_H

#include "BunnyTestingMultipleLookDirsAction.h"

class BunnyToBestShortcutAreaAction : public BotBunnyTestingMultipleLookDirsAction
{
	friend class BunnyStraighteningReachChainAction;
	static constexpr const char *NAME = "BunnyToBestShortcutAreaAction";
	static constexpr int MAX_BBOX_AREAS = 32;

	inline int FindActualStartTravelTime( MovementPredictionContext *context );
	void SaveSuggestedLookDirs( MovementPredictionContext *context ) override;
	inline int FindBBoxAreas( MovementPredictionContext *context, int *areaNums, int maxAreas );
	// Returns candidates end iterator
	AreaAndScore *SelectCandidateAreas( MovementPredictionContext *context,
										AreaAndScore *candidatesBegin,
										int startTravelTime );

public:
	explicit BunnyToBestShortcutAreaAction( class Bot *bot_ );

	void BeforePlanning() override {
		BotBunnyTestingMultipleLookDirsAction::BeforePlanning();
		// Reset to the action default value every frame
		maxSuggestedLookDirs = 2;
	}
};

#endif
