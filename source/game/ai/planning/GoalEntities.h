#ifndef QFUSION_AI_GOAL_ENTITIES_H
#define QFUSION_AI_GOAL_ENTITIES_H

#include "../ai_local.h"
#include "../vec3.h"

enum class NavEntityFlags : unsigned {
	NONE = 0x0,
	REACH_AT_TOUCH = 0x1,
	REACH_AT_RADIUS = 0x2,
	REACH_ON_EVENT = 0x4,
	REACH_IN_GROUP = 0x8,
	DROPPED_ENTITY = 0x10,
	NOTIFY_SCRIPT = 0x20,
	MOVABLE = 0x40
};

inline NavEntityFlags operator|( const NavEntityFlags &lhs, const NavEntityFlags &rhs ) {
	return (NavEntityFlags)( (unsigned)lhs | (unsigned)( rhs ) );
}
inline NavEntityFlags operator&( const NavEntityFlags &lhs, const NavEntityFlags &rhs ) {
	return (NavEntityFlags)( (unsigned)lhs & (unsigned)( rhs ) );
}

enum class NavTargetFlags : unsigned {
	NONE = 0x0,
	REACH_ON_RADIUS = 0x1,
	REACH_ON_EVENT = 0x2,
	REACH_IN_GROUP = 0x4,
	TACTICAL_SPOT = 0x8
};

inline NavTargetFlags operator|( const NavTargetFlags &lhs, const NavTargetFlags &rhs ) {
	return (NavTargetFlags)( (unsigned)lhs | (unsigned)rhs );
}
inline NavTargetFlags operator&( const NavTargetFlags &lhs, const NavTargetFlags &rhs ) {
	return (NavTargetFlags)( (unsigned)lhs & (unsigned)rhs );
}

// A NavEntity is based on some entity (edict_t) plus some attributes.
// All NavEntities are global for all Ai beings.
// A Goal is often based on a NavEntity
class NavEntity
{
	friend class NavEntitiesRegistry;
	friend class BotBrain;

	// Numeric id that matches index of corresponding entity in game edicts (if any)
	int id;
	// Id of area this goal is located in
	int aasAreaNum;
	// A goal origin, set once on goal addition or updated explicitly for movable goals
	// (It is duplicated from entity origin to prevent cheating with revealing
	// an actual origin of movable entities not marked as movable).
	Vec3 origin;
	// Misc. goal flags, mainly defining way this goal should be reached
	NavEntityFlags flags;
	// An entity this goal is based on
	edict_t *ent;
	// Links for registry goals pool
	NavEntity *prev, *next;

	NavEntity(): origin( 0, 0, 0 ) {
		memset( this, 0, sizeof( NavEntity ) );
	}

	static constexpr unsigned MAX_NAME_LEN = 128;
	char name[MAX_NAME_LEN];

	inline bool IsFlagSet( NavEntityFlags flag ) const {
		return NavEntityFlags::NONE != ( this->flags & flag );
	}

public:
	inline NavEntityFlags Flags() const { return flags; }
	inline int Id() const { return id; }
	inline int AasAreaNum() const { return aasAreaNum; }
	// A cost influence defines how base entity weight is affected by cost (move duration and wait time).
	// A cost influence is a positive float number usually in 0.5-1.0 range.
	// Lesser cost influence means that an entity weight is less affected by distance.
	float CostInfluence() const;
	inline Vec3 Origin() const { return origin; }
	inline const gsitem_t *Item() const { return ent ? ent->item : nullptr; }
	inline const char *Classname() const { return ent ? ent->classname : nullptr; }
	inline bool IsEnabled() const { return ent && ent->r.inuse; }
	inline bool IsDisabled() const { return !IsEnabled(); }
	inline bool IsBasedOnEntity( const edict_t *e ) const { return e && this->ent == e; }
	inline bool IsClient() const { return ent->r.client != nullptr; }
	inline bool IsSpawnedAtm() const { return ent->r.solid != SOLID_NOT; }
	inline bool ToBeSpawnedLater() const { return ent->r.solid == SOLID_NOT; }

	inline bool IsDroppedEntity() const { return IsFlagSet( NavEntityFlags::DROPPED_ENTITY ); }

	inline bool MayBeReachedInGroup() const { return IsFlagSet( NavEntityFlags::REACH_IN_GROUP ); }

	uint64_t MaxWaitDuration() const;

	bool IsTopTierItem( const float *overriddenEntityWeights = nullptr ) const;

	const char *Name() const { return name; }

	inline void NotifyTouchedByBot( const edict_t *bot ) const {
		if( ShouldNotifyScript() ) {
			GT_asBotTouchedGoal( bot->ai, ent );
		}
	}

	inline void NotifyBotReachedRadius( const edict_t *bot ) const {
		if( ShouldNotifyScript() ) {
			GT_asBotReachedGoalRadius( bot->ai, ent );
		}
	}

	int64_t Timeout() const;

	inline bool ShouldBeReachedAtTouch() const { return IsFlagSet( NavEntityFlags::REACH_AT_TOUCH ); }
	inline bool ShouldBeReachedAtRadius() const { return IsFlagSet( NavEntityFlags::REACH_AT_RADIUS ); }
	inline bool ShouldBeReachedOnEvent() const { return IsFlagSet( NavEntityFlags::REACH_ON_EVENT ); }

	inline bool ShouldNotifyScript() const { return IsFlagSet( NavEntityFlags::NOTIFY_SCRIPT ); }

	// Returns level.time when the item is already spawned
	// Returns zero if spawn time is unknown
	// Returns spawn time when the item is not spawned and spawn time may be predicted
	int64_t SpawnTime() const;
};

// A NavTarget may be based on a NavEntity (an item with attributes) or may be an "artificial" spot
class NavTarget
{
	Vec3 explicitOrigin;
	NavTargetFlags explicitFlags;
	int64_t explicitSpawnTime;
	int64_t explicitTimeout;
	int explicitAasAreaNum;
	float explicitRadius;

	const NavEntity *navEntity;

	const char *name;

	inline bool IsFlagSet( NavTargetFlags flag ) const {
		return NavTargetFlags::NONE != ( this->explicitFlags & flag );
	}

	NavTarget()
		: explicitOrigin( NAN, NAN, NAN ),
		explicitFlags( NavTargetFlags::NONE ),
		explicitSpawnTime( 0 ),
		explicitTimeout( 0 ),
		explicitAasAreaNum( 0 ),
		explicitRadius( 0 ),
		navEntity( nullptr ),
		name( nullptr ) {}

public:
	static inline NavTarget Dummy() { return NavTarget(); }

	void SetToNavEntity( const NavEntity *navEntity_ ) {
		this->navEntity = navEntity_;
	}

	void SetToTacticalSpot( const Vec3 &origin, float reachRadius = 32.0f ) {
		this->navEntity = nullptr;
		this->explicitOrigin = origin;
		this->explicitFlags = NavTargetFlags::REACH_ON_RADIUS | NavTargetFlags::TACTICAL_SPOT;
		this->explicitSpawnTime = 0;
		this->explicitTimeout = std::numeric_limits<int64_t>::max();
		this->explicitAasAreaNum = AiAasWorld::Instance()->FindAreaNum( origin );
		this->explicitRadius = reachRadius;
	}

	inline int AasAreaNum() const {
		return navEntity ? navEntity->AasAreaNum() : explicitAasAreaNum;
	}

	// A cost influence defines how base goal weight is affected by cost (move duration and wait time).
	// A cost influence is a positive float number usually in 0.5-1.0 range.
	// Lesser cost influence means that a goal weight is less affected by distance.
	inline float CostInfluence() const {
		return navEntity ? navEntity->CostInfluence() : 0.5f;
	}

	inline bool IsBasedOnEntity( const edict_t *ent ) const {
		return navEntity ? navEntity->IsBasedOnEntity( ent ) : false;
	}

	inline bool IsBasedOnNavEntity( const NavEntity *navEntity_ ) const {
		return navEntity_ && this->navEntity == navEntity_;
	}

	inline bool IsBasedOnSomeEntity() const { return navEntity != nullptr; }

	inline bool IsDisabled() const {
		return navEntity && navEntity->IsDisabled();
	}

	inline bool IsDroppedEntity() const {
		return navEntity && navEntity->IsDroppedEntity();
	}

	inline bool IsTacticalSpot() const { return IsFlagSet( NavTargetFlags::TACTICAL_SPOT ); }

	inline bool IsEnabled() const { return !IsDisabled(); }

	inline bool IsTopTierItem( const float *overriddenEntityWeights = nullptr ) const {
		return navEntity && navEntity->IsTopTierItem( overriddenEntityWeights );
	}

	inline uint64_t MaxWaitDuration() const {
		return navEntity ? navEntity->MaxWaitDuration() : 0;
	}

	inline bool MayBeReachedInGroup() const {
		if( navEntity ) {
			return navEntity->MayBeReachedInGroup();
		}
		return IsFlagSet( NavTargetFlags::REACH_IN_GROUP );
	}

	inline const char *Name() const { return name ? name : "???"; }

	inline Vec3 Origin() const {
		return navEntity ? navEntity->Origin() : explicitOrigin;
	}

	inline float RadiusOrDefault( float defaultRadius ) const {
		if( ShouldBeReachedAtRadius() ) {
			return explicitRadius;
		}
		return defaultRadius;
	}

	inline bool ShouldBeReachedAtTouch() const {
		return navEntity ? navEntity->ShouldBeReachedAtTouch() : false;
	}

	inline bool ShouldBeReachedAtRadius() const {
		if( navEntity ) {
			return navEntity->ShouldBeReachedAtRadius();
		}
		return IsFlagSet( NavTargetFlags::REACH_ON_RADIUS );
	}

	inline bool ShouldBeReachedOnEvent() const {
		if( navEntity ) {
			return navEntity->ShouldBeReachedOnEvent();
		}
		return IsFlagSet( NavTargetFlags::REACH_ON_EVENT );
	}

	inline bool ShouldNotifyScript() const {
		return navEntity ? navEntity->ShouldNotifyScript() : false;
	}

	inline void NotifyTouchedByBot( const edict_t *bot ) const {
		if( navEntity ) {
			navEntity->NotifyTouchedByBot( bot );
		}
	}

	inline void NotifyBotReachedRadius( const edict_t *bot ) const {
		if( navEntity ) {
			navEntity->NotifyBotReachedRadius( bot );
		}
	}

	// Returns level.time when the item is already spawned
	// Returns zero if spawn time is unknown
	// Returns spawn time when the item is not spawned and spawn time may be predicted
	inline int64_t SpawnTime() const {
		if( navEntity ) {
			return navEntity->SpawnTime();
		}
		if( explicitSpawnTime > level.time ) {
			return explicitSpawnTime;
		}
		return level.time;
	}

	inline int64_t Timeout() const {
		return navEntity ? navEntity->Timeout() : explicitTimeout;
	}
};

class NavEntitiesRegistry
{
	NavEntity navEntities[MAX_NAVENTS];
	NavEntity *entityToNavEntity[MAX_EDICTS];
	NavEntity *freeNavEntity;
	NavEntity headnode;

	NavEntitiesRegistry() {
		memset( this, 0, sizeof( NavEntitiesRegistry ) );
	}

	static NavEntitiesRegistry instance;

	NavEntity *AllocNavEntity();
	void FreeNavEntity( NavEntity *navEntity );

public:
	void Init();
	void Update();

	NavEntity *AddNavEntity( edict_t *ent, int aasAreaNum, NavEntityFlags flags );
	void RemoveNavEntity( NavEntity *navEntity );

	inline NavEntity *NavEntityForEntity( edict_t *ent ) {
		if( !ent ) {
			return nullptr;
		}
		return entityToNavEntity[ENTNUM( ent )];
	}

	class GoalEntitiesIterator
	{
		friend class NavEntitiesRegistry;
		NavEntity *currEntity;
		inline GoalEntitiesIterator( NavEntity *currEntity_ ) : currEntity( currEntity_ ) {}

public:
		inline NavEntity *operator*() { return currEntity; }
		inline const NavEntity *operator*() const { return currEntity; }
		inline void operator++() { currEntity = currEntity->prev; }
		inline bool operator!=( const GoalEntitiesIterator &that ) const { return currEntity != that.currEntity; }
	};
	inline GoalEntitiesIterator begin() { return GoalEntitiesIterator( headnode.prev ); }
	inline GoalEntitiesIterator end() { return GoalEntitiesIterator( &headnode ); }

	static inline NavEntitiesRegistry *Instance() { return &instance; }
};

#define FOREACH_NAVENT( navEnt ) for( auto *navEnt : *NavEntitiesRegistry::Instance() )

#endif
