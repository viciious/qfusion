#ifndef QFUSION_BUNNYSTRAIGHTENINGREACHCHAINACTION_H
#define QFUSION_BUNNYSTRAIGHTENINGREACHCHAINACTION_H

#include "BunnyTestingMultipleLookDirsAction.h"

class BunnyStraighteningReachChainAction : public BotBunnyTestingMultipleLookDirsAction
{
	friend class BunnyToBestShortcutAreaAction;
	static constexpr const char *NAME = "BunnyStraighteningReachChainAction";
	void SaveSuggestedLookDirs( MovementPredictionContext *context ) override;
	// Returns candidates end iterator
	AreaAndScore *SelectCandidateAreas( MovementPredictionContext *context,
										AreaAndScore *candidatesBegin,
										unsigned lastValidReachIndex );

public:
	explicit BunnyStraighteningReachChainAction( class Bot *bot_ );

	void BeforePlanning() override {
		BotBunnyTestingMultipleLookDirsAction::BeforePlanning();
		// Reset to the action default value every frame
		maxSuggestedLookDirs = 2;
	}
};

#endif
