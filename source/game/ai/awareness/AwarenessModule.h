#ifndef QFUSION_BOT_THREAT_TRACKER_H
#define QFUSION_BOT_THREAT_TRACKER_H

#include "EnemiesTracker.h"
#include "SelectedEnemies.h"
#include "HazardsSelector.h"
#include "EventsTracker.h"

class AiSquad;

class BotAwarenessModule: public AiFrameAwareUpdatable {
	friend class Bot;

	AiEnemiesTracker *activeEnemiesTracker { &ownEnemiesTracker };
	AiSquad *squad { nullptr };
	const edict_t *const self;

	SelectedEnemies &selectedEnemies;
	SelectedEnemies &lostEnemies;

	const unsigned targetChoicePeriod;
	const unsigned reactionTime;

	HazardsDetector hazardsDetector;
	HazardsSelector hazardsSelector;
	EventsTracker eventsTracker;
public:
	struct HurtEvent {
		// Initialize the inflictor by the world entity (it is never valid as one).
		// This helps to avoid extra branching from testing for nullity.
		const edict_t *inflictor { world };
		int64_t lastHitTimestamp { 0 };
		Vec3 possibleOrigin { 0, 0, 0 };
		float totalDamage { 0.0f };

		bool IsValidFor( const edict_t *self ) const;
		void Invalidate() { lastHitTimestamp = 0; }
	};

private:
	mutable HurtEvent hurtEvent;

	class EnemiesTracker : public AiEnemiesTracker {
		edict_t *const self;
		BotAwarenessModule *const threatTracker;
	protected:
		void OnHurtByNewThreat( const edict_t *newThreat ) override {
			threatTracker->OnHurtByNewThreat( newThreat, this );
		}
		bool CheckHasQuad() const override { return ::HasQuad( self ); }
		bool CheckHasShell() const override { return ::HasShell( self ); }
		float ComputeDamageToBeKilled() const override { return DamageToKill( self ); }
		void OnEnemyRemoved( const TrackedEnemy *enemy ) override {
			threatTracker->OnEnemyRemoved( enemy );
		}
		void SetBotRoleWeight( const edict_t *bot_, float weight ) override {}
		float GetAdditionalEnemyWeight( const edict_t *bot_, const edict_t *enemy ) const override { return 0; }
		void OnBotEnemyAssigned( const edict_t *bot_, const TrackedEnemy *enemy ) override {}

	public:
		EnemiesTracker( edict_t *self_, BotAwarenessModule *threatTracker_, float skill_ )
			: AiEnemiesTracker( skill_ ), self( self_ ), threatTracker( threatTracker_ ) {
			SetTag( va( "BotAwarenessModule(%s)::EnemiesTracker", self_->r.client->netname ) );
		}
	};

	EnemiesTracker ownEnemiesTracker;

	Hazard triggeredPlanningHazard { nullptr };

	void Frame() override;
	void Think() override;

	void SetFrameAffinity( unsigned modulo, unsigned offset ) override {
		AiFrameAwareUpdatable::SetFrameAffinity( modulo, offset );
		eventsTracker.SetFrameAffinity( modulo, offset );
		ownEnemiesTracker.SetFrameAffinity( modulo, offset );
	}

	void UpdateSelectedEnemies();
	void UpdateBlockedAreasStatus();
	void TryTriggerPlanningForNewHazard();

	void RegisterVisibleEnemies();

	void CheckForNewHazards();
public:
	// We have to provide both entity and Bot class refs due to initialization order issues
	BotAwarenessModule( edict_t *self_, Bot *bot_, float skill_ );

	void OnAttachedToSquad( AiSquad *squad_ );
	void OnDetachedFromSquad( AiSquad *squad_ );

	void OnHurtByNewThreat( const edict_t *newThreat, const AiFrameAwareUpdatable *threatDetector );
	void OnEnemyRemoved( const TrackedEnemy *enemy );

	void OnEnemyViewed( const edict_t *enemy );
	void OnEnemyOriginGuessed( const edict_t *enemy, unsigned minMillisSinceLastSeen, const float *guessedOrigin = nullptr );

	void RegisterEvent( const edict_t *ent, int event, int parm ) {
		eventsTracker.RegisterEvent( ent, event, parm );
	}

	void OnPain( const edict_t *enemy, float kick, int damage );
	void OnEnemyDamaged( const edict_t *target, int damage );

	const TrackedEnemy *ChooseLostOrHiddenEnemy( unsigned timeout = ~0u );

	const TrackedEnemy *TrackedEnemiesHead() const {
		return ( (const AiEnemiesTracker *)activeEnemiesTracker )->TrackedEnemiesHead();
	}

	const Hazard *PrimaryHazard() const {
		if( const auto *hazard = hazardsSelector.PrimaryHazard() ) {
			// The return value must always be valid if present.
			// Check whether if has not been invalidated since last selection.
			if( hazard->IsValid() ) {
				return hazard;
			}
		}
		return nullptr;
	}

	const HurtEvent *GetValidHurtEvent() const {
		if( !hurtEvent.IsValidFor( self ) ) {
			hurtEvent.Invalidate();
			return nullptr;
		}

		return &hurtEvent;
	}

	// In these calls use not active but bot's own enemy pool
	// (this behaviour is expected by callers, otherwise referring to a squad enemy pool is enough)
	inline int64_t LastAttackedByTime( const edict_t *attacker ) const {
		return ownEnemiesTracker.LastAttackedByTime( attacker );
	}

	inline int64_t LastTargetTime( const edict_t *target ) const {
		return ownEnemiesTracker.LastTargetTime( target );
	}
};

#endif
