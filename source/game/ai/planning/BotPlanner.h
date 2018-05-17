#ifndef QFUSION_BOT_BRAIN_H
#define QFUSION_BOT_BRAIN_H

#include <stdarg.h>
#include "../ai_base_ai.h"
#include "BasePlanner.h"
#include "../ai_base_enemy_pool.h"
#include "ItemsSelector.h"
#include "../combat/WeaponSelector.h"
#include "Actions.h"
#include "Goals.h"

class BotPlanner : public BasePlanner
{
	friend class Bot;
	friend class BotItemsSelector;
	friend class BotBaseGoal;
	friend class BotGutsActionsAccessor;

	StaticVector<BotScriptGoal, MAX_GOALS> scriptGoals;
	StaticVector<BotScriptAction, MAX_ACTIONS> scriptActions;

	BotBaseGoal *GetGoalByName( const char *name );
	BotBaseAction *GetActionByName( const char *name );

	inline BotScriptGoal *AllocScriptGoal() { return scriptGoals.unsafe_grow_back(); }
	inline BotScriptAction *AllocScriptAction() { return scriptActions.unsafe_grow_back(); }

	inline const int *Inventory() const { return self->r.client->ps.inventory; }

	template <int Weapon>
	inline int AmmoReadyToFireCount() const {
		if( !Inventory()[Weapon] ) {
			return 0;
		}
		return Inventory()[WeaponAmmo < Weapon > ::strongAmmoTag] + Inventory()[WeaponAmmo < Weapon > ::weakAmmoTag];
	}

	inline int ShellsReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_RIOTGUN>(); }
	inline int GrenadesReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_GRENADELAUNCHER>(); }
	inline int RocketsReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_ROCKETLAUNCHER>(); }
	inline int PlasmasReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_PLASMAGUN>(); }
	inline int BulletsReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_MACHINEGUN>(); }
	inline int LasersReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_LASERGUN>(); }
	inline int BoltsReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_ELECTROBOLT>(); }
	inline int WavesReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_SHOCKWAVE>(); }
	inline int InstasReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_INSTAGUN>(); }

	bool FindDodgeDangerSpot( const Danger &danger, vec3_t spotOrigin );

	void PrepareCurrWorldState( WorldState *worldState ) override;

	bool ShouldSkipPlanning() const override;

	void BeforePlanning() override;

public:
	BotPlanner() = delete;
	// Disable copying and moving
	BotPlanner( BotPlanner &&that ) = delete;

	// A WorldState cached from the moment of last world state update
	WorldState cachedWorldState;

	// Note: saving references to Bot members is the only valid access kind to Bot in this call
	BotPlanner( class Bot *bot, float skillLevel_ );
};

#endif
