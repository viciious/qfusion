#ifndef QFUSION_BOT_THREAT_TRACKER_H
#define QFUSION_BOT_THREAT_TRACKER_H

#include "ai_base_enemy_pool.h"
#include "bot_perception_manager.h"
#include "bot_weapon_selector.h"

class AiSquad;

class BotThreatTracker: public AiFrameAwareUpdatable {
	friend class Bot;

	AiBaseEnemyPool *activeEnemyPool;
	AiSquad *squad;
	const edict_t *const self;

	SelectedEnemies &selectedEnemies;
	SelectedEnemies &lostEnemies;

	const unsigned targetChoicePeriod;
	const unsigned reactionTime;

public:
	struct HurtEvent {
		const edict_t *inflictor;
		int64_t lastHitTimestamp;
		Vec3 possibleOrigin;
		float totalDamage;

		// Initialize the inflictor by the world entity (it is never valid as one).
		// This helps to avoid extra branching from testing for nullity.
		HurtEvent() : inflictor( world ), lastHitTimestamp( 0 ), possibleOrigin( NAN, NAN, NAN ), totalDamage( 0.0f ) {}

		bool IsValidFor( const edict_t *self ) const;
		void Invalidate() { lastHitTimestamp = 0; }
	};

private:
	mutable HurtEvent hurtEvent;

	class EnemyPool : public AiBaseEnemyPool
	{
		edict_t *const self;
		BotThreatTracker *const threatTracker;
	protected:
		void OnHurtByNewThreat( const edict_t *newThreat ) override {
			threatTracker->OnHurtByNewThreat( newThreat, this );
		}
		bool CheckHasQuad() const override { return ::HasQuad( self ); }
		bool CheckHasShell() const override { return ::HasShell( self ); }
		float ComputeDamageToBeKilled() const override { return DamageToKill( self ); }
		void OnEnemyRemoved( const Enemy *enemy ) override {
			threatTracker->OnEnemyRemoved( enemy );
		}
		void SetBotRoleWeight( const edict_t *bot_, float weight ) override {}
		float GetAdditionalEnemyWeight( const edict_t *bot_, const edict_t *enemy ) const override { return 0; }
		void OnBotEnemyAssigned( const edict_t *bot_, const Enemy *enemy ) override {}

	public:
		EnemyPool( edict_t *self_, BotThreatTracker *threatTracker_, float skill_ )
			: AiBaseEnemyPool( skill_ ), self( self_ ), threatTracker( threatTracker_ ) {
			SetTag( va( "BotThreatTracker(%s)::EnemyPool", self_->r.client->netname ) );
		}
	};

	EnemyPool ownEnemyPool;

	Danger selectedHazard;
	Danger triggeredPlanningHazard;

	void Frame() override;
	void Think() override;

	void SetFrameAffinity( unsigned modulo, unsigned offset ) override {
		AiFrameAwareUpdatable::SetFrameAffinity( modulo, offset );
		ownEnemyPool.SetFrameAffinity( modulo, offset );
	}

	void UpdateSelectedEnemies();
	void UpdateBlockedAreasStatus();
	void CheckNewActiveHazard();
public:
	// We have to provide both entity and Bot class refs due to initialization order issues
	BotThreatTracker( edict_t *self_, Bot *bot_, float skill_ );

	void OnAttachedToSquad( AiSquad *squad_ );
	void OnDetachedFromSquad( AiSquad *squad_ );

	void OnHurtByNewThreat( const edict_t *newThreat, const AiFrameAwareUpdatable *threatDetector );
	void OnEnemyRemoved( const Enemy *enemy );

	void OnEnemyViewed( const edict_t *enemy );
	void OnEnemyOriginGuessed( const edict_t *enemy, unsigned minMillisSinceLastSeen, const float *guessedOrigin = nullptr );

	void AfterAllEnemiesViewed() {}

	void OnPain( const edict_t *enemy, float kick, int damage );
	void OnEnemyDamaged( const edict_t *target, int damage );

	const Enemy *ChooseLostOrHiddenEnemy( unsigned timeout = ~0u );

	const Enemy *TrackedEnemiesHead() const {
		return ( (const AiBaseEnemyPool *)activeEnemyPool )->TrackedEnemiesHead();
	}

	const Danger *GetValidHazard() const {
		// Might be outdated for few frames, check it on access
		return selectedHazard.IsValid() ? &selectedHazard : nullptr;
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
		return ownEnemyPool.LastAttackedByTime( attacker );
	}

	inline int64_t LastTargetTime( const edict_t *target ) const {
		return ownEnemyPool.LastTargetTime( target );
	}
};

#endif
