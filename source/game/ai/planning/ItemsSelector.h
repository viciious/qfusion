#ifndef QFUSION_BOT_ITEMS_SELECTOR_H
#define QFUSION_BOT_ITEMS_SELECTOR_H

#include "../ai_local.h"
#include "GoalEntities.h"
#include "../static_vector.h"

class SelectedNavEntity
{
	friend class Bot;
	friend class BotItemsSelector;
	friend class AiSquad;

	const NavEntity *navEntity;
	float cost;
	float pickupGoalWeight;
	int64_t selectedAt;
	int64_t timeoutAt;

	inline SelectedNavEntity( const NavEntity *navEntity_,
							  float cost_,
							  float pickupGoalWeight_,
							  int64_t timeoutAt_ )
		: navEntity( navEntity_ ),
		cost( cost_ ),
		pickupGoalWeight( pickupGoalWeight_ ),
		selectedAt( level.time ),
		timeoutAt( timeoutAt_ ) {}

	inline void CheckValid( const char *message = nullptr ) const {
		if( !IsValid() ) {
			if( message ) {
				AI_FailWith( "SelectedNavEntity::CheckValid()", "%s\n", message );
			} else {
				AI_FailWith( "SelectedNavEntity::CheckValid()", "A check has failed\n" );
			}
		}
	}

public:
	inline bool IsEmpty() const { return navEntity == nullptr; }
	// Empty one is considered valid (until it times out)
	inline bool IsValid() const { return timeoutAt > level.time; }
	inline void InvalidateNextFrame() {
		timeoutAt = level.time + 1;
	}
	// Avoid class/method name clash by using Get prefix
	inline const NavEntity *GetNavEntity() const {
		CheckValid();
		return navEntity;
	}
	inline float GetCost() const {
		CheckValid();
		return cost;
	}
	inline float PickupGoalWeight() const {
		CheckValid();
		return pickupGoalWeight;
	}
};

class BotItemsSelector
{
	edict_t *self;

	int64_t disabledForSelectionUntil[MAX_EDICTS];

	float internalEntityWeights[MAX_EDICTS];
	float overriddenEntityWeights[MAX_EDICTS];

	// For each item contains a goal weight that would a corresponding AI pickup goal have.
	float internalPickupGoalWeights[MAX_EDICTS];

	inline float GetEntityWeight( int entNum ) {
		float overriddenEntityWeight = overriddenEntityWeights[entNum];
		if( overriddenEntityWeight != 0 ) {
			return overriddenEntityWeight;
		}
		return internalEntityWeights[entNum];
	}

	inline float GetGoalWeight( int entNum ) {
		float overriddenEntityWeight = overriddenEntityWeights[entNum];
		// Make goal weight based on overridden entity weight
		if( overriddenEntityWeight != 0 ) {
			float goalWeight = BoundedFraction( overriddenEntityWeight, 10.0f );
			if( goalWeight > 0 ) {
				goalWeight = 1.0f / Q_RSqrt( goalWeight );
			}
			// High weight items would have 2.0f goal weight
			goalWeight *= 2.0f;
			return goalWeight;
		}
		return internalPickupGoalWeights[entNum];
	}

	inline const int *Inventory() const { return self->r.client->ps.inventory; }

	void UpdateInternalItemAndGoalWeights();

	const edict_t *GetSpotEntityAndWeight( float *weight ) const;

	struct ItemAndGoalWeights {
		float itemWeight;
		float goalWeight;

		ItemAndGoalWeights( float itemWeight_, float goalWeight_ )
			: itemWeight( itemWeight_ ), goalWeight( goalWeight_ ) {}
	};

	ItemAndGoalWeights ComputeItemWeights( const gsitem_t *item, bool onlyGotGB ) const;
	ItemAndGoalWeights ComputeWeaponWeights( const gsitem_t *item, bool onlyGotGB ) const;
	ItemAndGoalWeights ComputeAmmoWeights( const gsitem_t *item ) const;
	ItemAndGoalWeights ComputeArmorWeights( const gsitem_t *item ) const;
	ItemAndGoalWeights ComputeHealthWeights( const gsitem_t *item ) const;
	ItemAndGoalWeights ComputePowerupWeights( const gsitem_t *item ) const;

	inline void Debug( const char *format, ... ) {
		va_list va;
		va_start( va, format );
		AI_Debugv( self->r.client->netname, format, va );
		va_end( va );
	}

	bool IsShortRangeReachable( const NavEntity *navEntity, const int *fromAreaNums, int numFromAreas ) const;
public:
	inline BotItemsSelector( edict_t *self_ ) : self( self_ ) {
		// We zero only this array as its content does not get cleared in SuggestGoalEntity() calls
		memset( disabledForSelectionUntil, 0, sizeof( disabledForSelectionUntil ) );
	}

	inline void ClearOverriddenEntityWeights() {
		memset( overriddenEntityWeights, 0, sizeof( overriddenEntityWeights ) );
	}

	// This weight overrides internal one computed by this brain itself.
	inline void OverrideEntityWeight( const edict_t *ent, float weight ) {
		overriddenEntityWeights[ENTNUM( const_cast<edict_t*>( ent ) )] = weight;
	}

	inline void MarkAsDisabled( const NavEntity &navEntity, unsigned millis ) {
		disabledForSelectionUntil[navEntity.Id()] = level.time + millis;
	}

	SelectedNavEntity SuggestGoalNavEntity( const SelectedNavEntity &currSelectedNavEntity );
};

#endif
