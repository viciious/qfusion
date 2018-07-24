#ifndef QFUSION_BOT_WEIGHT_CONFIG_H
#define QFUSION_BOT_WEIGHT_CONFIG_H

#include "ai_weight_config.h"

// Var names might seem to be way too shortened, but its silly to replace each minor numeric constant by a 30 chars name

class BotWeightConfig : public AiWeightConfig
{
public:
	class NativeGoalsGroup : public AiNativeWeightConfigVarGroup
	{
public:
		struct GrabItemGroup : public AiNativeWeightConfigVarGroup {
			AiNativeWeightConfigVar baseWeight;
			AiNativeWeightConfigVar selectedGoalWeightScale;

			GrabItemGroup( AiBaseWeightConfigVarGroup *parent )
				: AiNativeWeightConfigVarGroup( parent, "GrabItem" ),
				baseWeight( this, "BaseWeight", 0.00f, 0.50f, 0.00f ),
				selectedGoalWeightScale( this, "SelectedGoalWeightScale", 0.75f, 1.25f, 1.00f ) {}
		} grabItem;

		struct KillEnemyGroup : public AiNativeWeightConfigVarGroup {
			AiNativeWeightConfigVar baseWeight;
			AiNativeWeightConfigVar offCoeff;
			AiNativeWeightConfigVar nmyThreatCoeff;

			KillEnemyGroup( AiBaseWeightConfigVarGroup *parent )
				: AiNativeWeightConfigVarGroup( parent, "KillEnemy" ),
				baseWeight( this, "BaseWeight", 0.00f, 0.50f, 0.00f ),
				offCoeff( this, "OffCoeff", 0.50f, 3.00f, 1.75f ),
				nmyThreatCoeff( this, "NmyThreatCoeff", 1.10f, 2.00f, 1.25f ) {}
		} killEnemy;

		struct RunAwayGroup : public AiNativeWeightConfigVarGroup {
			AiNativeWeightConfigVar baseWeight;
			AiNativeWeightConfigVar offCoeff;
			AiNativeWeightConfigVar nmyThreatCoeff;

			RunAwayGroup( AiBaseWeightConfigVarGroup *parent )
				: AiNativeWeightConfigVarGroup( parent, "RunAway" ),
				baseWeight( this, "BaseWeight", 0.00f, 0.50f, 0.00f ),
				offCoeff( this, "OffCoeff", 1.00f, 3.00f, 1.75f ),
				nmyThreatCoeff( this, "NmyThreatCoeff", 1.10f, 3.00f, 1.50f ) {}
		} runAway;

		struct ReactToDangerGroup : public AiNativeWeightConfigVarGroup {
			AiNativeWeightConfigVar baseWeight;
			AiNativeWeightConfigVar dmgFracCoeff;
			AiNativeWeightConfigVar weightBound;

			ReactToDangerGroup( AiBaseWeightConfigVarGroup *parent )
				: AiNativeWeightConfigVarGroup( parent, "ReactToDanger" ),
				baseWeight( this, "BaseWeight", 0.50f, 1.00f, 0.75f ),
				dmgFracCoeff( this, "DmgFracCoeff", 0.50f, 5.00f, 2.00f ),
				weightBound( this, "WeightBound", 1.00f, 5.00f, 2.00f ) {}
		} reactToDanger;

		struct ReactToThreatGroup : public AiNativeWeightConfigVarGroup {
			AiNativeWeightConfigVar baseWeight;
			AiNativeWeightConfigVar dmgFracCoeff;
			AiNativeWeightConfigVar weightBound;
			AiNativeWeightConfigVar offCoeff;

			ReactToThreatGroup( AiBaseWeightConfigVarGroup *parent )
				: AiNativeWeightConfigVarGroup( parent, "ReactToThreat" ),
				baseWeight( this, "BaseWeight", 0.50f, 1.00f, 0.50f ),
				dmgFracCoeff( this, "DmgFracCoeff", 1.00f, 5.00f, 3.00f ),
				weightBound( this, "WeightBound", 1.00f, 3.00f, 1.75f ),
				offCoeff( this, "OffCoeff", 0.00f, 2.00f, 1.00f ) {}
		} reactToThreat;

		struct ReactToEnemyLostGroup : public AiNativeWeightConfigVarGroup {
			AiNativeWeightConfigVar baseWeight;
			AiNativeWeightConfigVar offCoeff;

			ReactToEnemyLostGroup( AiBaseWeightConfigVarGroup *parent )
				: AiNativeWeightConfigVarGroup( parent, "ReactToEnemyLost" ),
				baseWeight( this, "BaseWeight", 0.50f, 1.00f, 0.50f ),
				offCoeff( this, "OffCoeff", 0.50f, 6.00f, 2.00f ) {}
		} reactToEnemyLost;

		struct AttackOutOfDespairGroup : public AiNativeWeightConfigVarGroup {
			AiNativeWeightConfigVar nmyFireDelayThreshold;
			AiNativeWeightConfigVar baseWeight;
			AiNativeWeightConfigVar nmyThreatExtraWeight;
			AiNativeWeightConfigVar dmgFracCoeff;
			AiNativeWeightConfigVar dmgUpperBound;

			AttackOutOfDespairGroup( AiBaseWeightConfigVarGroup *parent )
				: AiNativeWeightConfigVarGroup( parent, "AttackOutOfDespair" ),
				nmyFireDelayThreshold( this, "NmyFireDelayThreshold", 200, 800, 600 ),
				baseWeight( this, "BaseWeight", 0.25f, 1.00f, 0.50f ),
				nmyThreatExtraWeight( this, "NmyThreatExtraWeight", 0.10f, 1.00f, 0.50f ),
				dmgFracCoeff( this, "DmgFracCoeff", 0.50f, 1.75f, 1.25f ),
				dmgUpperBound( this, "DmgUpperBound", 50.0f, 200.0f, 100.0f ) {}
		} attackOutOfDespair;

		NativeGoalsGroup( AiBaseWeightConfigVarGroup *parent )
			: AiNativeWeightConfigVarGroup( parent, "NativeGoals" ),
			grabItem( this ),
			killEnemy( this ),
			runAway( this ),
			reactToDanger( this ),
			reactToThreat( this ),
			reactToEnemyLost( this ),
			attackOutOfDespair( this ) {}

	} nativeGoals;

	// We have decided to remove "native actions group" as weights tweaking on this level
	// just adds more jitter and produces fruitless combinations of weights
	// making convergence to a good solution impossible.

	// There are systems though, like "enemies tracker", "weapons selector", etc
	// that might benefit from this kind of weights optimization.
	// There should be an option to freeze goal weights and interbreed only weights for the suggested groups.
	// This is doable via using scripted evolution manager.

	BotWeightConfig( const edict_t *owner )
		: AiWeightConfig( owner ),
		nativeGoals( Root() ) {
		RegisterInScript();
	}
};

#endif
