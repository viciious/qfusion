#include "EventsTracker.h"
#include "../bot.h"

void EventsTracker::TryGuessingBeamOwnersOrigins( const EntNumsVector &dangerousEntsNums, float failureChance ) {
	const edict_t *const gameEdicts = game.edicts;
	auto *const bot = self->ai->botRef;
	for( auto entNum: dangerousEntsNums ) {
		if( random() < failureChance ) {
			continue;
		}
		const edict_t *owner = &gameEdicts[gameEdicts[entNum].s.ownerNum];
		if( bot->MayNotBeFeasibleEnemy( owner ) ) {
			continue;
		}
		if( CanDistinguishEnemyShotsFromTeammates( owner ) ) {
			bot->OnEnemyOriginGuessed( owner, 128 );
		}
	}
}

void EventsTracker::TryGuessingProjectileOwnersOrigins( const EntNumsVector &dangerousEntNums, float failureChance ) {
	const edict_t *const gameEdicts = game.edicts;
	const int64_t levelTime = level.time;
	auto *const bot = self->ai->botRef;
	for( auto entNum: dangerousEntNums ) {
		if( random() < failureChance ) {
			continue;
		}
		const edict_t *projectile = &gameEdicts[entNum];
		const edict_t *owner = &gameEdicts[projectile->s.ownerNum];
		if( bot->MayNotBeFeasibleEnemy( owner ) ) {
			continue;
		}

		if( projectile->s.linearMovement ) {
			// This test is expensive, do it after cheaper ones have succeeded.
			if( CanDistinguishEnemyShotsFromTeammates( projectile->s.linearMovementBegin ) ) {
				bot->OnEnemyOriginGuessed( owner, 256, projectile->s.linearMovementBegin );
			}
			return;
		}

		// Can't guess in this case
		if( projectile->s.type != ET_GRENADE ) {
			return;
		}

		unsigned timeout = GS_GetWeaponDef( WEAP_GRENADELAUNCHER )->firedef.timeout;
		// Can't guess in this case
		if( projectile->nextThink < levelTime + timeout / 2 ) {
			continue;
		}

		// This test is expensive, do it after cheaper ones have succeeded.
		if( CanDistinguishEnemyShotsFromTeammates( owner ) ) {
			// Use the exact enemy origin as a guessed one
			bot->OnEnemyOriginGuessed( owner, 384 );
		}
	}
}

void EventsTracker::ResetTeammatesVisData() {
	numTestedTeamMates = 0;
	hasComputedTeammatesVisData = false;
	static_assert( sizeof( *teammatesVisStatus ) == 1, "" );
	static_assert( sizeof( teammatesVisStatus ) == MAX_CLIENTS, "" );
	std::fill_n( teammatesVisStatus, MAX_CLIENTS, -1 );
}

void EventsTracker::ComputeTeammatesVisData( const vec3_t forwardDir, float fovDotFactor ) {
	numTestedTeamMates = 0;
	areAllTeammatesInFov = true;
	const int numMates = teamlist[self->s.team].numplayers;
	const int *mateNums = teamlist[self->s.team].playerIndices;
	const auto *gameEdicts = game.edicts;
	for( int i = 0; i < numMates; ++i ) {
		const edict_t *mate = gameEdicts + mateNums[i];
		if( mate == self || G_ISGHOSTING( mate ) ) {
			continue;
		}
		Vec3 dir( mate->s.origin );
		dir -= self->s.origin;
		distancesToTeammates[i] = dir.NormalizeFast();
		float dot = dir.Dot( forwardDir );
		if( dot < fovDotFactor ) {
			areAllTeammatesInFov = false;
		}
		viewDirDotTeammateDir[numTestedTeamMates] = dot;
		testedTeammatePlayerNums[numTestedTeamMates] = (uint8_t)PLAYERNUM( mate );
		numTestedTeamMates++;
	}
	hasComputedTeammatesVisData = true;
}

bool EventsTracker::CanDistinguishEnemyShotsFromTeammates( const GuessedEnemy &guessedEnemy ) {
	if ( !GS_TeamBasedGametype() ) {
		return true;
	}

	trace_t trace;

	Vec3 toEnemyDir( guessedEnemy.origin );
	toEnemyDir -= self->s.origin;
	const float distanceToEnemy = toEnemyDir.NormalizeFast();

	const auto *gameEdicts = game.edicts;
	const Vec3 forwardDir( self->ai->botRef->EntityPhysicsState()->ForwardDir() );
	const float fovDotFactor = self->ai->botRef->FovDotFactor();

	// Compute vis data lazily
	if( !hasComputedTeammatesVisData ) {
		ComputeTeammatesVisData( forwardDir.Data(), fovDotFactor );
		hasComputedTeammatesVisData = true;
	}

	const bool canShowMinimap = GS_CanShowMinimap();
	// If the bot can't see the guessed enemy origin
	if( toEnemyDir.Dot( forwardDir ) < fovDotFactor ) {
		if( areAllTeammatesInFov ) {
			return true;
		}
		// Try using a minimap to make the distinction.
		for( unsigned i = 0; i < numTestedTeamMates; ++i ) {
			if( viewDirDotTeammateDir[i] >= fovDotFactor ) {
				continue;
			}
			const edict_t *mate = gameEdicts + testedTeammatePlayerNums[i] + 1;
			if( !canShowMinimap ) {
				return false;
			}

			if( DistanceSquared( mate->s.origin, guessedEnemy.origin ) > 300 * 300 ) {
				continue;
			}

			// A mate is way too close to the guessed origin.
			// Check whether there is a wall that helps to make the distinction.

			if( guessedEnemy.AreInPvsWith( self ) ) {
				SolidWorldTrace( &trace, mate->s.origin, guessedEnemy.origin );
				if( trace.fraction == 1.0f ) {
					return false;
				}
			}
		}
		return true;
	}

	const auto *pvsCache = EntitiesPvsCache::Instance();
	const float viewDotEnemy = forwardDir.Dot( toEnemyDir );
	for( unsigned i = 0; i < numTestedTeamMates; ++i ) {
		const float viewDotTeammate = viewDirDotTeammateDir[i];
		const edict_t *mate = gameEdicts + testedTeammatePlayerNums[i] + 1;
		// The bot can't see or hear the teammate. Try using a minimap to make the distinction.
		if( viewDotTeammate <= fovDotFactor ) {
			// A mate is way too close to the guessed origin.
			if( DistanceSquared( mate->s.origin, guessedEnemy.origin ) < 300 * 300 ) {
				if( !canShowMinimap ) {
					return false;
				}

				// Check whether there is a wall that helps to make the distinction.
				if( guessedEnemy.AreInPvsWith( self ) ) {
					SolidWorldTrace( &trace, mate->s.origin, guessedEnemy.origin );
					if( trace.fraction == 1.0f ) {
						return false;
					}
				}
			}
			continue;
		}

		// In this case it's guaranteed that the enemy cannot be distinguished from a teammate
		if( fabsf( viewDotEnemy - viewDotTeammate ) < 0.9f ) {
			continue;
		}

		// Test teammate visibility status lazily
		if( teammatesVisStatus[i] < 0 ) {
			teammatesVisStatus[i] = 0;
			if( pvsCache->AreInPvs( self, mate ) ) {
				Vec3 viewPoint( self->s.origin );
				viewPoint.Z() += self->viewheight;
				SolidWorldTrace( &trace, viewPoint.Data(), mate->s.origin );
				if( trace.fraction == 1.0f ) {
					teammatesVisStatus[i] = 1;
				}
			}
		}

		// Can't say much if the teammate is not visible.
		// We can test visibility of the guessed origin but its way too expensive.
		// (there might be redundant computations overlapping with the normal bot vision).
		if( teammatesVisStatus[i] < 0 ) {
			return false;
		}

		if( distanceToEnemy > 2048 ) {
			// A teammate probably blocks enemy on the line of sight
			if( distancesToTeammates[i] < distanceToEnemy ) {
				return false;
			}
		}
	}

	return true;
}

bool EventsTracker::GuessedEnemyEnt::AreInPvsWith( const edict_t *botEnt ) const {
	return EntitiesPvsCache::Instance()->AreInPvs( botEnt, ent );
}

bool EventsTracker::GuessedEnemyOrigin::AreInPvsWith( const edict_t *botEnt ) const {
	if( botEnt->r.num_clusters < 0 ) {
		return trap_inPVS( botEnt->s.origin, this->origin );
	}

	// Compute leafs for the own origin lazily
	if( !numLeafs ) {
		vec3_t mins = { -4, -4, -4 };
		vec3_t maxs = { +4, +4, +4 };
		int topnode;
		numLeafs = trap_CM_BoxLeafnums( mins, maxs, leafNums, 4, &topnode );
		// Filter out solid leafs
		for( int i = 0; i < numLeafs; ) {
			if( trap_CM_LeafCluster( leafNums[i] ) >= 0 ) {
				i++;
			} else {
				numLeafs--;
				leafNums[i] = leafNums[numLeafs];
			}
		}
	}

	const int *botLeafNums = botEnt->r.leafnums;
	for( int i = 0, end = botEnt->r.num_clusters; i < end; ++i ) {
		for( int j = 0; j < this->numLeafs; ++j ) {
			if( trap_CM_LeafsInPVS( botLeafNums[i], this->leafNums[j] ) ) {
				return true;
			}
		}
	}

	return false;
}

void EventsTracker::HandleGenericPlayerEntityEvent( const edict_t *player, float distanceThreshold ) {
	if( CanPlayerBeHeardAsEnemy( player, distanceThreshold ) ) {
		PushEnemyEventOrigin( player, player->s.origin );
	}
}

class CachedEventsToPlayersMap {
	struct alignas ( 4 )Entry {
		Int64Align4 computedAt;
		// If we use lesser types the rest of 4 bytes would be lost for alignment anyway
		int32_t playerEntNum;
	};

	Entry entries[MAX_EDICTS];
public:
	CachedEventsToPlayersMap() {
		memset( entries, 0, sizeof( entries ) );
	}

	int PlayerEntNumForEvent( int eventEntNum );

	const edict_t *PlayerEntForEvent( int eventEntNum ) {
		if( int entNum = PlayerEntNumForEvent( eventEntNum ) ) {
			return game.edicts + entNum;
		}
		return nullptr;
	}

	const edict_t *PlayerEntForEvent( const edict_t *event ) {
		assert( event->s.type == ET_EVENT );
		return PlayerEntForEvent( ENTNUM( event ) );
	}
};

static CachedEventsToPlayersMap eventsToPlayersMap;

int CachedEventsToPlayersMap::PlayerEntNumForEvent( int eventEntNum ) {
	const int64_t levelTime = level.time;
	const edict_t *gameEdicts = game.edicts;
	Entry *const entry = &entries[eventEntNum];
	if( entry->computedAt == levelTime ) {
		return entry->playerEntNum;
	}

	const edict_t *event = &gameEdicts[eventEntNum];
	Vec3 mins( event->s.origin );
	Vec3 maxs( event->s.origin );
	mins += playerbox_stand_mins;
	maxs += playerbox_stand_maxs;

	int entNums[16];
	int numEnts = GClip_FindInRadius( const_cast<float *>( event->s.origin ), 16.0f, entNums, 16 );
	for( int i = 0; i < numEnts; ++i ) {
		const edict_t *ent = gameEdicts + entNums[i];
		if( !ent->r.client || G_ISGHOSTING( ent ) ) {
			continue;
		}

		if( !VectorCompare( ent->s.origin, event->s.origin ) ) {
			continue;
		}

		entry->computedAt = levelTime;
		entry->playerEntNum = entNums[i];
		return entNums[i];
	}

	entry->computedAt = levelTime;
	entry->playerEntNum = 0;
	return 0;
}

void EventsTracker::HandleGenericEventAtPlayerOrigin( const edict_t *ent, float distanceThreshold ) {
	if( DistanceSquared( self->s.origin, ent->s.origin ) > distanceThreshold * distanceThreshold ) {
		return;
	}

	const edict_t *player = eventsToPlayersMap.PlayerEntForEvent( ent );
	if( !player ) {
		return;
	}

	if( self->s.team == player->s.team && !GS_TeamBasedGametype() ) {
		return;
	}

	assert( VectorCompare( ent->s.origin, player->s.origin ) );
	PushEnemyEventOrigin( player, player->s.origin );
}

void EventsTracker::HandleGenericImpactEvent( const edict_t *event, float visibleDistanceThreshold ) {
	if( !CanEntityBeHeardAsEnemy( event, visibleDistanceThreshold ) ) {
		return;
	}

	// Throttle plasma impacts
	if( event->s.events[event->numEvents & 1] == EV_PLASMA_EXPLOSION && random() > 0.3f ) {
		return;
	}

	// TODO: Throttle PVS/trace calls
	if( EntitiesPvsCache::Instance()->AreInPvs( self, event ) ) {
		trace_t trace;
		Vec3 viewPoint( self->s.origin );
		viewPoint.Z() += self->viewheight;
		SolidWorldTrace( &trace, viewPoint.Data(), event->s.origin );
		// Not sure if the event entity is on a solid plane.
		// Do not check whether the fraction equals 1.0f
		if( DistanceSquared( trace.endpos, event->s.origin ) < 1.0f * 1.0f ) {
			// We can consider the explosion visible.
			// In this case cheat a bit, get the actual owner origin.
			const edict_t *owner = game.edicts + event->s.ownerNum;
			PushEnemyEventOrigin( owner, owner->s.origin );
			return;
		}
	}

	// If we can't see the impact, use try hear it
	if( DistanceSquared( self->s.origin, event->s.origin ) < 512.0f * 512.0f ) {
		// Let enemy origin be the explosion origin in this case
		PushEnemyEventOrigin( game.edicts + event->s.ownerNum, event->s.origin );
	}
}

void EventsTracker::HandleJumppadEvent( const edict_t *player, float ) {
	// A trajectory of a jumppad user is predictable in most cases.
	// So we track a user and push updates using its real origin without an obvious cheating.

	// If a player uses a jumppad, a player sound is emitted, so we can distinguish enemies from teammates.
	// Just check whether we can hear the sound.
	if( CanPlayerBeHeardAsEnemy( player, 512.0f * ( 1.0f + 2.0f * self->ai->botRef->Skill() ) ) ) {
		jumppadUsersTracker.Register( player );
	}
}

class TeleportTriggersDestCache {
	uint16_t destEntNums[MAX_EDICTS];
public:
	TeleportTriggersDestCache() {
		memset( destEntNums, 0, sizeof( destEntNums ) );
	}

	const float *GetTeleportDest( int entNum );
};

static TeleportTriggersDestCache teleportTriggersDestCache;

const float *TeleportTriggersDestCache::GetTeleportDest( int entNum ) {
	const edict_t *gameEdicts = game.edicts;
	const edict_t *ent = gameEdicts + entNum;
	assert( ent->classname && !Q_stricmp( ent->classname, "trigger_teleport" ) );

	if ( int destEntNum = destEntNums[entNum] ) {
		return gameEdicts[destEntNum].s.origin;
	}

	if( const edict_t *destEnt = G_Find( nullptr, FOFS( targetname ), ent->target ) ) {
		destEntNums[entNum] = (uint16_t)ENTNUM( destEnt );
		return destEnt->s.origin;
	}

	// We do not cache a result of failure. However these broken triggers should not be met often.
	return nullptr;
}

class PlayerTeleOutEventsCache {
	struct Entry {
		Int64Align4 computedAt;
		uint16_t triggerEntNum;
		uint16_t playerEntNum;
	} entries[MAX_EDICTS];
public:
	PlayerTeleOutEventsCache() {
		memset( entries, 0, sizeof( entries ) );
	}

	const edict_t *GetPlayerAndDestOriginByEvent( const edict_t *teleportOutEvent, const float **origin );
};

static PlayerTeleOutEventsCache playerTeleOutEventsCache;

const edict_t *PlayerTeleOutEventsCache::GetPlayerAndDestOriginByEvent( const edict_t *teleportOutEvent,
																		const float **origin ) {
	assert( teleportOutEvent->s.type == ET_EVENT );

	Entry *const entry = &entries[ENTNUM( teleportOutEvent )];
	const int64_t levelTime = level.time;
	const edict_t *gameEdicts = game.edicts;

	if( entry->computedAt == levelTime ) {
		if( entry->triggerEntNum && entry->playerEntNum ) {
			if( const float *dest = teleportTriggersDestCache.GetTeleportDest( entry->triggerEntNum ) ) {
				*origin = dest;
				return gameEdicts + entry->playerEntNum;
			}
		}
		return nullptr;
	}

	int entNums[16];
	int triggerEntNum = 0, playerEntNum = 0;
	int numEnts = GClip_FindInRadius( const_cast<float *>( teleportOutEvent->s.origin ), 64.0f, entNums, 16 );
	for( int i = 0; i < numEnts; ++i ) {
		const edict_t *ent = gameEdicts + entNums[i];
		if( ent->classname && !Q_stricmp( ent->classname, "trigger_teleport" ) ) {
			triggerEntNum = entNums[i];
		} else if ( ent->r.client && VectorCompare( ent->s.origin, teleportOutEvent->s.origin ) ) {
			playerEntNum = entNums[i];
		}
	}

	if( !( triggerEntNum && playerEntNum ) ) {
		// Set both ent nums to zero even if some is not to speed up further calls on cached data
		entry->triggerEntNum = 0;
		entry->playerEntNum = 0;
		entry->computedAt = levelTime;
		return nullptr;
	}

	entry->triggerEntNum = (uint16_t)triggerEntNum;
	entry->playerEntNum = (uint16_t)playerEntNum;
	entry->computedAt = levelTime;

	if( const float *dest = teleportTriggersDestCache.GetTeleportDest( triggerEntNum ) ) {
		*origin = dest;
		return game.edicts + playerEntNum;
	}

	return nullptr;
}

void EventsTracker::HandlePlayerTeleportOutEvent( const edict_t *ent, float ) {
	float distanceThreshold = 512.0f * ( 2.0f + 1.0f * self->ai->botRef->Skill() );
	if( DistanceSquared( self->s.origin, ent->s.origin ) > distanceThreshold * distanceThreshold ) {
		return;
	}

	// This event is spawned when the player has not been teleported yet,
	// so we do not know an actual origin after teleportation and have to look at the trigger destination.

	const float *teleportDest;
	const edict_t *player = playerTeleOutEventsCache.GetPlayerAndDestOriginByEvent( ent, &teleportDest );
	if( !player ) {
		return;
	}

	if( self->s.team == ent->s.team && GS_TeamBasedGametype() ) {
		return;
	}

	PushEnemyEventOrigin( player, teleportDest );
}

void EventsTracker::JumppadUsersTracker::Think() {
	// Report new origins of tracked players.

	Bot *bot = eventsTracker->self->ai->botRef;
	const edict_t *gameEdicts = game.edicts;
	for( int i = 0, end = gs.maxclients; i < end; ++i ) {
		if( !isTrackedUser[i] ) {
			continue;
		}
		const edict_t *ent = gameEdicts + i + 1;
		// Check whether a player cannot be longer a valid entity
		if( bot->MayNotBeFeasibleEnemy( ent ) ) {
			isTrackedUser[i] = false;
			continue;
		}
		bot->OnEnemyOriginGuessed( ent, 128 );
	}
}

void EventsTracker::JumppadUsersTracker::Frame() {
	// Stop tracking landed players.
	// It should be done each frame since a player might continue bunnying after landing,
	// and we might miss all frames when the player is on ground keeping tracking it forever.

	const edict_t *gameEdicts = game.edicts;
	for( int i = 0, end = gs.maxclients; i < end; ++i ) {
		if( !isTrackedUser[i] ) {
			continue;
		}
		if( gameEdicts[i + 1].groundentity ) {
			isTrackedUser[i] = false;
		}
	}
}

void EventsTracker::RegisterEvent( const edict_t *ent, int event, int parm ) {
	( this->*eventHandlers[event])( ent, this->eventHandlingParams[event] );
}

void EventsTracker::SetupEventHandlers() {
	for( int i = 0; i < MAX_EVENTS; ++i ) {
		SetEventHandler( i, &EventsTracker::HandleDummyEvent );
	}

	// Note: radius values are a bit lower than it is expected for a human perception,
	// but otherwise bots behave way too hectic detecting all minor events

	for( int i : { EV_FIREWEAPON, EV_SMOOTHREFIREWEAPON } ) {
		SetEventHandler( i, &EventsTracker::HandleGenericPlayerEntityEvent, 768.0f );
	}
	SetEventHandler( EV_WEAPONACTIVATE, &EventsTracker::HandleGenericPlayerEntityEvent, 512.0f );
	SetEventHandler( EV_NOAMMOCLICK, &EventsTracker::HandleGenericPlayerEntityEvent, 256.0f );

	// TODO: We currently skip weapon beam-like events.
	// This kind of events is extremely expensive to check for visibility.

	for( int i : { EV_DASH, EV_WALLJUMP, EV_WALLJUMP_FAILED, EV_DOUBLEJUMP, EV_JUMP } ) {
		SetEventHandler( i, &EventsTracker::HandleGenericPlayerEntityEvent, 512.0f );
	}

	SetEventHandler( EV_JUMP_PAD, &EventsTracker::HandleJumppadEvent );

	SetEventHandler( EV_FALL, &EventsTracker::HandleGenericPlayerEntityEvent, 512.0f );

	for( int i : { EV_PLAYER_RESPAWN, EV_PLAYER_TELEPORT_IN } ) {
		SetEventHandler( i, &EventsTracker::HandleGenericEventAtPlayerOrigin, 768.0f );
	}

	SetEventHandler( EV_PLAYER_TELEPORT_OUT, &EventsTracker::HandlePlayerTeleportOutEvent );

	for( int i : { EV_GUNBLADEBLAST_IMPACT, EV_PLASMA_EXPLOSION, EV_BOLT_EXPLOSION, EV_INSTA_EXPLOSION } ) {
		SetEventHandler( i, &EventsTracker::HandleGenericImpactEvent, 1024.0f + 256.0f );
	}

	for( int i : { EV_ROCKET_EXPLOSION, EV_GRENADE_EXPLOSION, EV_GRENADE_BOUNCE } ) {
		SetEventHandler( i, &EventsTracker::HandleGenericImpactEvent, 2048.0f );
	}

	// TODO: Track platform users (almost the same as with jumppads)
	// TODO: React to door activation
}

void EventsTracker::Think() {
	// We have to validate detected enemies since the events queue
	// has been accumulated during the Think() frames cycle
	// and might contain outdated info (e.g. a player has changed its team).
	const auto *gameEdicts = game.edicts;
	Bot *bot = self->ai->botRef;
	for( const auto &detectedEvent: eventsQueue ) {
		const edict_t *ent = gameEdicts + detectedEvent.enemyEntNum;
		if( bot->MayNotBeFeasibleEnemy( ent ) ) {
			continue;
		}
		bot->OnEnemyOriginGuessed( ent, 96, detectedEvent.origin );
	}

	eventsQueue.clear();
}

