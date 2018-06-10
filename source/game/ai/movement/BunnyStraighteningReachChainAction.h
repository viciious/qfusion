#ifndef QFUSION_BUNNYSTRAIGHTENINGREACHCHAINACTION_H
#define QFUSION_BUNNYSTRAIGHTENINGREACHCHAINACTION_H

#include "BunnyTestingMultipleLookDirsAction.h"

class BunnyStraighteningReachChainAction : public BunnyTestingMultipleLookDirsAction
{
	friend class BunnyToBestShortcutAreaAction;
	static constexpr const char *NAME = "BunnyStraighteningReachChainAction";
	void SaveSuggestedLookDirs( MovementPredictionContext *context ) override;
	// Returns candidates end iterator
	AreaAndScore *SelectCandidateAreas( MovementPredictionContext *context,
										AreaAndScore *candidatesBegin,
										unsigned lastValidReachIndex );
public:
	explicit BunnyStraighteningReachChainAction( BotMovementModule *module_ );

	void OnApplicationSequenceStarted( MovementPredictionContext *context ) override {
		BunnyTestingMultipleLookDirsAction::OnApplicationSequenceStarted( context );
		if( currSuggestedLookDirNum < suggestedLookDirs.size() ) {
			checkStopAtAreaNums.push_back( dirsBaseAreas[currSuggestedLookDirNum] );
		}
	}

	void BeforePlanning() override {
		BunnyTestingMultipleLookDirsAction::BeforePlanning();
		// Reset to the action default value every frame
		maxSuggestedLookDirs = 2;
	}
};

#endif
