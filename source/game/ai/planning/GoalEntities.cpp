#include "GoalEntities.h"

float NavEntity::CostInfluence() const {
	// Usually these kinds of nav entities are CTF flags or bomb spots,
	// so these entities should be least affected by cost.
	if( !ent->item ) {
		return 0.5f;
	}

	// Make this local copy to shorten lines.
	// Cast a enum value to int in comparisons to avoid analyzer warnings
	// (different enum types are used for comparison).
	const int itemTag = ent->item->tag;

	// Cost influence is not a weight.
	// Cost influence separates different classes of items, not items itself.
	// For two items of the same cost class costs should match and weights may (and usually) no.
	// We add extra conditions just because MH and a health bubble,
	// RA and an armor shard are not really in the same class.
	switch( ent->item->type ) {
		case IT_POWERUP:
			return 0.6f;
		case IT_ARMOR:
			return ( itemTag != ARMOR_SHARD ) ? 0.7f : 0.9f;
		case IT_HEALTH:
			return ( itemTag == HEALTH_MEGA || itemTag == HEALTH_ULTRA ) ? 0.7f : 0.9f;
		case IT_WEAPON:
			return 0.8f;
		case IT_AMMO:
			return 1.0f;
	}

	// Return a default value for malformed (e.g. in custom GT scripts) item type/tags.
	return 0.5f;
}

bool NavEntity::IsTopTierItem( const float *overriddenEntityWeights ) const {
	if( !ent->r.inuse ) {
		return false;
	}

	if( !ent->item ) {
		return overriddenEntityWeights && overriddenEntityWeights[ENTNUM( ent )] >= 2.0f;
	}

	// Make these local copies to shorten lines.
	const auto itemType = ent->item->type;

	// Cast a enum value to int in comparisons to avoid analyzer warnings
	// (different enum types are used for comparison).
	const int itemTag = ent->item->tag;

	if( itemType == IT_POWERUP ) {
		return true;
	}

	if( itemType == IT_HEALTH ) {
		switch( itemTag ) {
			case HEALTH_MEGA:
			case HEALTH_ULTRA:
				return true;
		}	
	}

	if( itemType == IT_ARMOR ) {
		switch( itemTag ) {
			case ARMOR_RA:
			case ARMOR_YA:
				return true;
		}	
	}

	return false;
}

int64_t NavEntity::SpawnTime() const {
	if( !ent->r.inuse ) {
		return 0;
	}

	if( ent->r.solid == SOLID_TRIGGER ) {
		return level.time;
	}

	// This means the nav entity is spawned by a script
	// (only items are hardcoded nav. entities)
	// Let the script manage the entity, do not prevent any action with the entity
	if( !ent->item ) {
		return level.time;
	}

	if( !ent->classname ) {
		return 0;
	}

	// MH needs special handling
	// If MH owner is sent, exact MH spawn time can't be predicted
	// Otherwise fallback to the generic spawn prediction code below
	// Check owner first to cut off string comparison early in negative case
	if( ent->r.owner && !Q_stricmp( "item_health_mega", ent->classname ) ) {
		return 0;
	}

	return ent->nextThink;
}

uint64_t NavEntity::MaxWaitDuration() const {
	if( !ent->item || ShouldBeReachedOnEvent() ) {
		return std::numeric_limits<uint64_t>::max();
	}

	// Make these local copies to shorten lines.
	const auto itemType = ent->item->type;

	// Cast a enum value to int in comparisons to avoid analyzer warnings
	// (different enum types are used for comparison).
	const int itemTag = ent->item->tag;

	if( itemType == IT_POWERUP ) {
		return 5000;
	}

	if( itemType == IT_HEALTH && ( itemTag == HEALTH_MEGA || itemTag == HEALTH_ULTRA ) ) {
		return 3000;
	}

	if( itemType == IT_ARMOR ) {
		switch( itemTag ) {
			case ARMOR_RA:
				return 3000;
			case ARMOR_YA:
				return 2000;
			case ARMOR_GA:
				return 1500;
		}
	}

	return 1000;
}

int64_t NavEntity::Timeout() const {
	if( IsDroppedEntity() ) {
		return ent->nextThink;
	}
	return std::numeric_limits<int64_t>::max();
}

NavEntitiesRegistry NavEntitiesRegistry::instance;

void NavEntitiesRegistry::Init() {
	unsigned i;

	memset( navEntities, 0, sizeof( navEntities ) );
	memset( entityToNavEntity, 0, sizeof( entityToNavEntity ) );

	freeNavEntity = navEntities;
	headnode.id = -1;
	headnode.ent = game.edicts;
	headnode.aasAreaNum = 0;
	headnode.prev = &headnode;
	headnode.next = &headnode;

	for( i = 0; i < sizeof( navEntities ) / sizeof( navEntities[0] ) - 1; i++ ) {
		navEntities[i].id = i;
		navEntities[i].next = &navEntities[i + 1];
	}
	navEntities[i].id = i;
	navEntities[i].next = NULL;

	for( int clientEnt = 1; clientEnt <= gs.maxclients; ++clientEnt )
		AddNavEntity( game.edicts + clientEnt, 0, NavEntityFlags::REACH_ON_EVENT | NavEntityFlags::MOVABLE );
}

void NavEntitiesRegistry::Update() {
	const auto *aasWorld = AiAasWorld::Instance();
	auto *navEntitiesRegistry = NavEntitiesRegistry::Instance();

	for( auto it = navEntitiesRegistry->begin(), end = navEntitiesRegistry->end(); it != end; ++it ) {
		NavEntity *navEnt = *it;
		if( ( navEnt->flags & NavEntityFlags::MOVABLE ) != NavEntityFlags::NONE ) {
			navEnt->aasAreaNum = aasWorld->FindAreaNum( navEnt->ent->s.origin );
		}
	}
}

NavEntity *NavEntitiesRegistry::AllocNavEntity() {
	if( !freeNavEntity ) {
		return nullptr;
	}

	NavEntity *navEntity = freeNavEntity;
	freeNavEntity = navEntity->next;

	navEntity->prev = &headnode;
	navEntity->next = headnode.next;
	navEntity->next->prev = navEntity;
	navEntity->prev->next = navEntity;

	return navEntity;
}

NavEntity *NavEntitiesRegistry::AddNavEntity( edict_t *ent, int aasAreaNum, NavEntityFlags flags ) {
	NavEntity *navEntity = AllocNavEntity();

	if( navEntity ) {
		int entNum = ENTNUM( ent );
		navEntity->ent = ent;
		navEntity->id = entNum;
		navEntity->aasAreaNum = aasAreaNum;
		navEntity->origin = Vec3( ent->s.origin );
		navEntity->flags = flags;
		Q_snprintfz( navEntity->name, NavEntity::MAX_NAME_LEN, "%s(ent#=%d)", ent->classname, entNum );
		entityToNavEntity[entNum] = navEntity;
		return navEntity;
	}

	constexpr const char *format = S_COLOR_RED "Can't allocate a nav. entity for %s @ %.3f %.3f %.3f\n";
	G_Printf( format, ent->classname, ent->s.origin[0], ent->s.origin[1], ent->s.origin[2] );
	return nullptr;
}

void NavEntitiesRegistry::RemoveNavEntity( NavEntity *navEntity ) {
	edict_t *ent = navEntity->ent;

	FreeNavEntity( navEntity );
	entityToNavEntity[ENTNUM( ent )] = nullptr;
}

void NavEntitiesRegistry::FreeNavEntity( NavEntity *navEntity ) {
	// remove from linked active list
	navEntity->prev->next = navEntity->next;
	navEntity->next->prev = navEntity->prev;

	// insert into linked free list
	navEntity->next = freeNavEntity;
	freeNavEntity = navEntity;
}
