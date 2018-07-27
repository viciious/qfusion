#ifndef QFUSION_BOT_PERCEPTION_MANAGER_H
#define QFUSION_BOT_PERCEPTION_MANAGER_H

#include "../planning/BasePlanner.h"
#include "../static_deque.h"
#include "AwarenessLocal.h"

class EventsTracker: public AiFrameAwareUpdatable {
	friend class BotAwarenessModule;
	friend class HazardsDetector;
	friend class JumppadUsersTracker;

	edict_t *const self;
	float viewDirDotTeammateDir[MAX_CLIENTS];
	float distancesToTeammates[MAX_CLIENTS];
	uint8_t testedTeammatePlayerNums[MAX_CLIENTS];
	int8_t teammatesVisStatus[MAX_CLIENTS];
	unsigned numTestedTeamMates { 0 };
	bool hasComputedTeammatesVisData { false };
	bool areAllTeammatesInFov { false };

	struct DetectedEvent {
		vec3_t origin;
		int enemyEntNum;
		DetectedEvent( const vec3_t origin_, int enemyEntNum_ ) {
			VectorCopy( origin_, this->origin );
			this->enemyEntNum = enemyEntNum_;
		}
	};

	StaticDeque<DetectedEvent, 16> eventsQueue;

	// The failure chance is specified mainly to throttle excessive plasma spam
	void TryGuessingBeamOwnersOrigins( const EntNumsVector &dangerousEntsNums, float failureChance = 0.0f );
	void TryGuessingProjectileOwnersOrigins( const EntNumsVector &dangerousEntNums, float failureChance = 0.0f );

	void ResetTeammatesVisData();
	void ComputeTeammatesVisData( const vec3_t forwardDir, float fovDotFactor );

	// We introduce a common wrapper superclass for either edict_t or vec3_t
	// to avoid excessive branching in the call below that that leads to unmaintainable code.
	// Virtual calls are not so expensive as one might think (they are predicted on a sane arch).
	struct GuessedEnemy {
		vec3_t origin;
		GuessedEnemy( const vec3_t origin_ ) {
			VectorCopy( origin_, this->origin );
		}
		virtual bool AreInPvsWith( const edict_t *botEnt ) const = 0;
	};

	struct GuessedEnemyEnt final: public GuessedEnemy {
		const edict_t *const ent;
		GuessedEnemyEnt( const edict_t *ent_ ) : GuessedEnemy( ent_->s.origin ), ent( ent_ ) {}
		bool AreInPvsWith( const edict_t *botEnt ) const override;
	};

	struct GuessedEnemyOrigin final: public EventsTracker::GuessedEnemy {
		mutable int leafNums[4], numLeafs;
		GuessedEnemyOrigin( const vec3_t origin_ ) : GuessedEnemy( origin_ ), numLeafs( 0 ) {}
		bool AreInPvsWith( const edict_t *botEnt ) const override;
	};

	bool CanDistinguishEnemyShotsFromTeammates( const edict_t *enemy ) {
		return CanDistinguishEnemyShotsFromTeammates( GuessedEnemyEnt( enemy ));
	}

	bool CanDistinguishEnemyShotsFromTeammates( const vec3_t specifiedOrigin ) {
		return CanDistinguishEnemyShotsFromTeammates( GuessedEnemyOrigin( specifiedOrigin ) );
	}

	bool CanDistinguishEnemyShotsFromTeammates( const GuessedEnemy &guessedEnemy );

	void PushEnemyEventOrigin( const edict_t *enemy, const vec3_t origin ) {
		if( eventsQueue.size() == eventsQueue.capacity() ) {
			eventsQueue.pop_back();
		}
		eventsQueue.emplace_front( DetectedEvent( origin, ENTNUM( enemy ) ) );
	}

	bool CanPlayerBeHeardAsEnemy( const edict_t *ent, float distanceThreshold ) {
		if( self->s.team != ent->s.team || self->s.team == TEAM_PLAYERS ) {
			if( DistanceSquared( self->s.origin, ent->s.origin ) < distanceThreshold * distanceThreshold ) {
				return true;
			}
		}
		return false;
	}

	bool CanEntityBeHeardAsEnemy( const edict_t *ent, float distanceThreshold ) {
		const edict_t *owner = game.edicts + ent->s.ownerNum;
		if( self->s.team != owner->s.team || self->s.team == TEAM_PLAYERS ) {
			if( DistanceSquared( self->s.origin, ent->s.origin ) < distanceThreshold * distanceThreshold ) {
				return true;
			}
		}
		return false;
	}

	void HandleGenericPlayerEntityEvent( const edict_t *player, float distanceThreshold );
	void HandleGenericEventAtPlayerOrigin( const edict_t *event, float distanceThreshold );
	void HandleGenericImpactEvent( const edict_t *event, float visibleDistanceThreshold );
	void HandleDummyEvent( const edict_t *, float ) {}
	void HandleJumppadEvent( const edict_t *player, float );
	void HandlePlayerTeleportOutEvent( const edict_t *player, float );

	// We are not sure what code a switch statement produces.
	// Event handling code is quite performance-sensitive since its is called for each bot for each event.
	// So we set up a lookup table manually.
	typedef void ( EventsTracker::*EventHandler )( const edict_t *, float );
	EventHandler eventHandlers[MAX_EVENTS];
	float eventHandlingParams[MAX_EVENTS];

	void SetupEventHandlers();
	void SetEventHandler( int event, EventHandler handler, float param = 0.0f ) {
		eventHandlers[event] = handler;
		eventHandlingParams[event] = param;
	}

	class JumppadUsersTracker: public AiFrameAwareUpdatable {
		friend class EventsTracker;
		EventsTracker *eventsTracker;
		// An i-th element corresponds to an i-th client
		bool isTrackedUser[MAX_CLIENTS];
	public:
		explicit JumppadUsersTracker( EventsTracker *eventsTracker_ ) {
			this->eventsTracker = eventsTracker_;
			memset( isTrackedUser, 0, sizeof( isTrackedUser ) );
		}

		void Register( const edict_t *ent ) {
			assert( ent->r.client );
			isTrackedUser[PLAYERNUM( ent )] = true;
		}

		void Think() override;
		void Frame() override;
	};

	JumppadUsersTracker jumppadUsersTracker;
public:
	explicit EventsTracker( edict_t *self_ ): self( self_ ), jumppadUsersTracker( this ) {
		SetupEventHandlers();
	}

	void RegisterEvent( const edict_t *ent, int event, int parm );

	void Frame() override {
		AiFrameAwareUpdatable::Frame();
		// Always calls Frame() and calls Think() if needed
		jumppadUsersTracker.Update();
	}

	void Think() override;

	void SetFrameAffinity( unsigned modulo, unsigned offset ) override {
		AiFrameAwareUpdatable::SetFrameAffinity( modulo, offset );
		jumppadUsersTracker.SetFrameAffinity( modulo, offset );
	}
};

#endif
