/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <new>

#include "g_local.h"

game_locals_t game;
level_locals_t level;
spawn_temp_t st;

struct mempool_s *gamepool;
struct mempool_s *levelpool;

int meansOfDeath;

cvar_t *password;
cvar_t *g_operator_password;
cvar_t *g_select_empty;
cvar_t *dedicated;
cvar_t *developer;

cvar_t *filterban;

cvar_t *g_maxvelocity;
cvar_t *g_gravity;

cvar_t *sv_cheats;
cvar_t *sv_mm_enable;

cvar_t *cm_mapHeader;
cvar_t *cm_mapVersion;

cvar_t *g_maplist;
cvar_t *g_maprotation;

cvar_t *g_enforce_map_pool;
cvar_t *g_map_pool;

cvar_t *g_floodprotection_messages;
cvar_t *g_floodprotection_team;
cvar_t *g_floodprotection_seconds;
cvar_t *g_floodprotection_penalty;

cvar_t *g_inactivity_maxtime;

cvar_t *g_projectile_touch_owner;
cvar_t *g_projectile_prestep;
cvar_t *g_shockwave_fly_stun_scale;
cvar_t *g_shockwave_fly_damage_scale;
cvar_t *g_shockwave_fly_knockback_scale;
cvar_t *g_numbots;
cvar_t *g_maxtimeouts;
cvar_t *g_antilag;
cvar_t *g_antilag_maxtimedelta;
cvar_t *g_antilag_timenudge;
cvar_t *g_autorecord;
cvar_t *g_autorecord_maxdemos;

cvar_t *g_self_knockback;
cvar_t *g_knockback_scale;
cvar_t *g_allow_stun;
cvar_t *g_armor_degradation;
cvar_t *g_armor_protection;
cvar_t *g_allow_falldamage;
cvar_t *g_allow_selfdamage;
cvar_t *g_allow_teamdamage;
cvar_t *g_allow_bunny;

cvar_t *g_respawn_delay_min;
cvar_t *g_respawn_delay_max;
cvar_t *g_deadbody_followkiller;
cvar_t *g_deadbody_autogib_delay;
cvar_t *g_ammo_respawn;
cvar_t *g_weapon_respawn;
cvar_t *g_health_respawn;
cvar_t *g_armor_respawn;

cvar_t *g_instagib;
cvar_t *g_instajump;
cvar_t *g_instashield;

cvar_t *g_disable_vote_gametype;

cvar_t *g_allow_spectator_voting;

cvar_t *g_asGC_stats;
cvar_t *g_asGC_interval;

cvar_t *g_skillRating;

static char *map_rotation_s = NULL;
static char **map_rotation_p = NULL;
static int map_rotation_current = -1;
static int map_rotation_count = 0;

static const char *G_SelectNextMapName( void );

//===================================================================

/*
* G_API
*/
int G_API( void ) {
	return GAME_API_VERSION;
}

/*
* G_Error
*
* Abort the server with a game error
*/
void G_Error( const char *format, ... ) {
	char msg[1024];
	va_list argptr;

	va_start( argptr, format );
	Q_vsnprintfz( msg, sizeof( msg ), format, argptr );
	va_end( argptr );

	trap_Error( msg );
}

/*
* G_Printf
*
* Debug print to server console
*/
void G_Printf( const char *format, ... ) {
	char msg[1024];
	va_list argptr;

	va_start( argptr, format );
	Q_vsnprintfz( msg, sizeof( msg ), format, argptr );
	va_end( argptr );

	trap_Print( msg );
}

/*
* G_GS_Malloc - Used only for gameshared linking
*/
static void *G_GS_Malloc( size_t size ) {
	return G_Malloc( size );
}

/*
* G_GS_Free - Used only for gameshared linking
*/
static void G_GS_Free( void *data ) {
	G_Free( data );
}

/*
* G_GS_Trace - Used only for gameshared linking
*/
static void G_GS_Trace( trace_t *tr, vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, int ignore, int contentmask, int timeDelta ) {
	edict_t *passent = NULL;
	if( ignore >= 0 && ignore < MAX_EDICTS ) {
		passent = &game.edicts[ignore];
	}

	G_Trace4D( tr, start, mins, maxs, end, passent, contentmask, timeDelta );
}

/*
* G_InitGameShared
* give gameshared access to some utilities
*/
static void G_InitGameShared( void ) {
	memset( &gs, 0, sizeof( gs_state_t ) );
	gs.module = GS_MODULE_GAME;

	gs.maxclients = atoi( trap_GetConfigString( CS_MAXCLIENTS ) );
	if( gs.maxclients < 1 || gs.maxclients > MAX_EDICTS ) {
		G_Error( "Invalid maxclients value %i\n", gs.maxclients );
	}

	module_PredictedEvent = G_PredictedEvent;
	module_Error = G_Error;
	module_Printf = G_Printf;
	module_Malloc = G_GS_Malloc;
	module_Free = G_GS_Free;
	module_Trace = G_GS_Trace;
	module_GetEntityState = G_GetEntityStateForDeltaTime;
	module_PointContents = G_PointContents4D;
	module_PMoveTouchTriggers = G_PMoveTouchTriggers;
	module_GetConfigString = trap_GetConfigString;
}

/*
* G_Init
*
* This will be called when the dll is first loaded, which
* only happens when a new game is started or a save game is loaded.
*/
void G_Init( unsigned int seed, unsigned int framemsec, int protocol, const char *demoExtension ) {
	cvar_t *g_maxentities;

	G_Printf( "==== G_Init ====\n" );

	srand( seed );

	G_InitGameShared();

	SV_InitIPList();

	game.snapFrameTime = framemsec;
	game.frametime = game.snapFrameTime;
	game.protocol = protocol;
	Q_strncpyz( game.demoExtension, demoExtension, sizeof( game.demoExtension ) );
	game.levelSpawnCount = 0;

	g_maxvelocity = trap_Cvar_Get( "g_maxvelocity", "16000", 0 );
	if( g_maxvelocity->value < 20 ) {
		trap_Cvar_SetValue( "g_maxvelocity", 20 );
	}

	g_gravity = trap_Cvar_Get( "g_gravity", va( "%i", GRAVITY ), 0 );
	developer = trap_Cvar_Get( "developer", "0", 0 );

	// noset vars
	dedicated = trap_Cvar_Get( "dedicated", "0", CVAR_NOSET );

	// latched vars
	sv_cheats = trap_Cvar_Get( "sv_cheats", "0", CVAR_SERVERINFO | CVAR_LATCH );
	sv_mm_enable = trap_Cvar_Get( "sv_mm_enable", "0", CVAR_ARCHIVE | CVAR_NOSET | CVAR_SERVERINFO );

	// hack in CVAR_SERVERINFO flag
	trap_Cvar_Get( "gamename", trap_Cvar_String( "gamename" ), CVAR_SERVERINFO );
	trap_Cvar_Get( "gamedate", __DATE__, CVAR_SERVERINFO | CVAR_LATCH );

	password = trap_Cvar_Get( "password", "", CVAR_USERINFO );
	password->modified = true; // force an update of g_needpass in G_UpdateServerInfo
	g_operator_password = trap_Cvar_Get( "g_operator_password", "", CVAR_ARCHIVE );
	filterban = trap_Cvar_Get( "filterban", "1", 0 );

	cm_mapHeader = trap_Cvar_Get( "cm_mapHeader", "", 0 );
	cm_mapVersion = trap_Cvar_Get( "cm_mapVersion", "", 0 );

	g_ammo_respawn = trap_Cvar_Get( "g_ammo_respawn", "0", CVAR_ARCHIVE );
	g_weapon_respawn = trap_Cvar_Get( "g_weapon_respawn", "0", CVAR_ARCHIVE );
	g_health_respawn = trap_Cvar_Get( "g_health_respawn", "0", CVAR_ARCHIVE );
	g_armor_respawn = trap_Cvar_Get( "g_armor_respawn", "0", CVAR_ARCHIVE );
	g_select_empty = trap_Cvar_Get( "g_select_empty", "0", CVAR_DEVELOPER );
	g_projectile_touch_owner = trap_Cvar_Get( "g_projectile_touch_owner", "0", CVAR_DEVELOPER );
	g_projectile_prestep = trap_Cvar_Get( "g_projectile_prestep", va( "%i", PROJECTILE_PRESTEP ), CVAR_DEVELOPER );
	g_shockwave_fly_stun_scale = trap_Cvar_Get( "g_shockwave_fly_stun_scale", "1.0", CVAR_DEVELOPER );
	g_shockwave_fly_damage_scale = trap_Cvar_Get( "g_shockwave_fly_damage_scale", "2.0", CVAR_DEVELOPER );
	g_shockwave_fly_knockback_scale = trap_Cvar_Get( "g_shockwave_fly_knockback_scale", "8.0", CVAR_DEVELOPER );
	g_self_knockback = trap_Cvar_Get( "g_self_knockback", "1.18", CVAR_DEVELOPER );
	g_knockback_scale = trap_Cvar_Get( "g_knockback_scale", "1.0", CVAR_ARCHIVE );
	g_allow_stun = trap_Cvar_Get( "g_allow_stun", "1", CVAR_ARCHIVE );
	g_armor_degradation = trap_Cvar_Get( "g_armor_degradation", va( "%.2f", ARMOR_DEGRADATION ), CVAR_DEVELOPER );
	g_armor_protection = trap_Cvar_Get( "g_armor_protection", va( "%.2f", ARMOR_PROTECTION ), CVAR_DEVELOPER );
	g_respawn_delay_min = trap_Cvar_Get( "g_respawn_delay_min", "600", CVAR_DEVELOPER );
	g_respawn_delay_max = trap_Cvar_Get( "g_respawn_delay_max", "6000", CVAR_DEVELOPER );
	g_numbots = trap_Cvar_Get( "g_numbots", "0", CVAR_ARCHIVE );
	g_deadbody_followkiller = trap_Cvar_Get( "g_deadbody_followkiller", "1", CVAR_DEVELOPER );
	g_deadbody_autogib_delay = trap_Cvar_Get( "g_deadbody_autogib_delay", "2000", CVAR_DEVELOPER );
	g_maxtimeouts = trap_Cvar_Get( "g_maxtimeouts", "2", CVAR_ARCHIVE );
	g_antilag = trap_Cvar_Get( "g_antilag", "1", CVAR_SERVERINFO | CVAR_ARCHIVE | CVAR_LATCH );
	g_antilag_maxtimedelta = trap_Cvar_Get( "g_antilag_maxtimedelta", "200", CVAR_ARCHIVE );
	g_antilag_maxtimedelta->modified = true;
	g_antilag_timenudge = trap_Cvar_Get( "g_antilag_timenudge", "0", CVAR_ARCHIVE );
	g_antilag_timenudge->modified = true;

	g_allow_spectator_voting = trap_Cvar_Get( "g_allow_spectator_voting", "1", CVAR_ARCHIVE );

	if( dedicated->integer ) {
		g_autorecord = trap_Cvar_Get( "g_autorecord", "1", CVAR_ARCHIVE );
		g_autorecord_maxdemos = trap_Cvar_Get( "g_autorecord_maxdemos", "200", CVAR_ARCHIVE );
	} else {
		g_autorecord = trap_Cvar_Get( "g_autorecord", "0", CVAR_ARCHIVE );
		g_autorecord_maxdemos = trap_Cvar_Get( "g_autorecord_maxdemos", "0", CVAR_ARCHIVE );
	}

	// flood control
	g_floodprotection_messages = trap_Cvar_Get( "g_floodprotection_messages", "4", 0 );
	g_floodprotection_messages->modified = true;
	g_floodprotection_team = trap_Cvar_Get( "g_floodprotection_team", "0", 0 );
	g_floodprotection_team->modified = true;
	g_floodprotection_seconds = trap_Cvar_Get( "g_floodprotection_seconds", "4", 0 );
	g_floodprotection_seconds->modified = true;
	g_floodprotection_penalty = trap_Cvar_Get( "g_floodprotection_delay", "10", 0 );
	g_floodprotection_penalty->modified = true;

	g_inactivity_maxtime = trap_Cvar_Get( "g_inactivity_maxtime", "90.0", 0 );
	g_inactivity_maxtime->modified = true;

	// map list
	g_maplist = trap_Cvar_Get( "g_maplist", "", CVAR_ARCHIVE );
	g_maprotation = trap_Cvar_Get( "g_maprotation", "1", CVAR_ARCHIVE );

	// map pool
	g_enforce_map_pool = trap_Cvar_Get( "g_enforce_map_pool", "0", CVAR_ARCHIVE );
	g_map_pool = trap_Cvar_Get( "g_map_pool", "", CVAR_ARCHIVE );

	//game switches
	g_instagib = trap_Cvar_Get( "g_instagib", "0", CVAR_SERVERINFO | CVAR_ARCHIVE | CVAR_LATCH );
	g_instajump = trap_Cvar_Get( "g_instajump", "1", CVAR_ARCHIVE );
	g_instashield = trap_Cvar_Get( "g_instashield", "1", CVAR_ARCHIVE );

	// helper cvars to show current status in serverinfo reply
	trap_Cvar_Get( "g_match_time", "", CVAR_SERVERINFO | CVAR_READONLY );
	trap_Cvar_Get( "g_match_score", "", CVAR_SERVERINFO | CVAR_READONLY );
	trap_Cvar_Get( "g_needpass", "", CVAR_SERVERINFO | CVAR_READONLY );
	trap_Cvar_Get( "g_gametypes_available", "", CVAR_SERVERINFO | CVAR_READONLY );
	trap_Cvar_Get( "g_race_gametype", "0", CVAR_SERVERINFO | CVAR_READONLY );

	// define this one here so we can see when it's modified
	g_disable_vote_gametype = trap_Cvar_Get( "g_disable_vote_gametype", "0", CVAR_ARCHIVE );

	g_asGC_stats = trap_Cvar_Get( "g_asGC_stats", "0", CVAR_ARCHIVE );
	g_asGC_interval = trap_Cvar_Get( "g_asGC_interval", "10", CVAR_ARCHIVE );

	g_skillRating = trap_Cvar_Get( "sv_skillRating", va( "%.0f", MM_RATING_DEFAULT ), CVAR_SERVERINFO | CVAR_READONLY );
	// trap_Cvar_ForceSet( "sv_skillRating", va("%d", MM_RATING_DEFAULT) );

	// nextmap
	trap_Cvar_ForceSet( "nextmap", "match \"advance\"" );

	// initialize all entities for this game
	g_maxentities = trap_Cvar_Get( "sv_maxentities", "1024", CVAR_LATCH );
	game.maxentities = g_maxentities->integer;
	game.edicts = ( edict_t * )G_Malloc( game.maxentities * sizeof( game.edicts[0] ) );

	// initialize all clients for this game
	game.clients = ( gclient_t * )G_Malloc( gs.maxclients * sizeof( game.clients[0] ) );
	// call clients constructors since this is no longer a POD type
	for( int i = 0; i < gs.maxclients; ++i ) {
		new( game.clients + i )gclient_s;
	}

	game.quits = NULL;

	game.numentities = gs.maxclients + 1;

	trap_LocateEntities( game.edicts, sizeof( game.edicts[0] ), game.numentities, game.maxentities );

	// server console commands
	G_AddServerCommands();

	G_LoadFiredefsFromDisk();

	// weapon items
	GS_InitWeapons();

	// init AS engine
	G_asInitGameModuleEngine();
}

/*
* G_Shutdown
*/
void G_Shutdown( void ) {
	int i;

	G_Printf( "==== G_Shutdown ====\n" );

	GT_asCallShutdown();
	G_asCallMapExit();

	AI_BeforeLevelLevelScriptShutdown();

	G_asShutdownMapScript();
	GT_asShutdownScript();
	G_asShutdownGameModuleEngine();

	AI_AfterLevelScriptShutdown();

	SV_ShutdownIPList();

	trap_Cvar_ForceSet( "nextmap", va( "map \"%s\"", G_SelectNextMapName() ) );

	AI_Shutdown();

	G_RemoveCommands();

	G_FreeCallvotes();

	G_LevelFreePool();

	for( i = 0; i < game.numentities; i++ ) {
		if( game.edicts[i].r.inuse ) {
			G_FreeEdict( &game.edicts[i] );
		}
	}

	G_Free( game.edicts );

	for( i = 0; i < gs.maxclients; ++i ) {
		game.clients[i].~gclient_t();
	}
	G_Free( game.clients );
}

//======================================================================

/*
* G_AllowDownload
*/
bool G_AllowDownload( edict_t *ent, const char *requestname, const char *uploadname ) {
	return false;
}

//======================================================================

/*
* CreateTargetChangeLevel
*
* Returns the created target changelevel
*/
static edict_t *CreateTargetChangeLevel( const char *map ) {
	edict_t *ent;

	ent = G_Spawn();
	ent->classname = "target_changelevel";
	Q_strncpyz( level.nextmap, map, sizeof( level.nextmap ) );
	ent->map = level.nextmap;
	return ent;
}

/*
* G_UpdateMapRotation
*
* Reads current map rotation into internal list
*/
static void G_UpdateMapRotation( void ) {
	int count, i;
	bool thiswhitespace, lastwhitespace, found;
	char *p, *start;
	static const char *seps = " ,\n\r";

	if( g_maplist->modified || !map_rotation_s || !map_rotation_p ) {
		g_maplist->modified = false;

		// reread the maplist
		if( map_rotation_s ) {
			G_Free( map_rotation_s );
		}
		if( map_rotation_p ) {
			G_Free( map_rotation_p );
		}

		map_rotation_s = G_CopyString( g_maplist->string );
		map_rotation_p = NULL;
		map_rotation_current = -1;  // reset the mapcounter too
		map_rotation_count = 0;

		// count the number of tokens
		p = map_rotation_s;
		count = 0;
		lastwhitespace = true;
		start = NULL;
		found = false;
		while( *p ) {
			thiswhitespace = ( strchr( seps, *p ) != NULL ) ? true : false;
			if( lastwhitespace && !thiswhitespace ) {
				start = p;
				count++;
			} else if( thiswhitespace && !lastwhitespace && !found && start ) {
				found = true;
				for( i = 0; start + i < p; i++ ) {
					if( tolower( start[i] ) != tolower( level.mapname[i] ) ) {
						found = false;
					}
				}
				if( found ) {
					map_rotation_current = count - 1;
				}
			}

			lastwhitespace = thiswhitespace;
			p++;
		}

		if( !count ) {
			return;
		}

		// allocate the array of pointers
		map_rotation_p = ( char ** )G_Malloc( ( count + 1 ) * sizeof( *map_rotation_p ) );

		// now convert the string to tokens by nulling the separators
		p = map_rotation_s;
		count = 0;
		lastwhitespace = true;
		while( *p ) {
			thiswhitespace = ( strchr( seps, *p ) != NULL ) ? true : false;
			if( lastwhitespace && !thiswhitespace ) {
				map_rotation_p[count++] = p;
			}

			if( thiswhitespace ) {
				*p = 0;
			}

			lastwhitespace = thiswhitespace;
			p++;
		}

		// final null pointer to mark the end
		map_rotation_p[count] = NULL;

		map_rotation_count = count;
	}
}

/*
* G_MapRotationNormal
*/
static const char *G_MapRotationNormal( void ) {
	G_UpdateMapRotation();

	if( !map_rotation_count ) {
		return NULL;
	}

	map_rotation_current++;

	if( map_rotation_current >= map_rotation_count || map_rotation_p[map_rotation_current] == NULL ) {
		map_rotation_current = 0;
	}

	return map_rotation_p[map_rotation_current];
}

/*
* G_MapRotationNormal
*/
static const char *G_MapRotationRandom( void ) {
	int seed, selection;

	G_UpdateMapRotation();

	// avoid eternal loop
	if( !map_rotation_count || map_rotation_count == 1 ) {
		return NULL;
	}

	seed = game.realtime;
	do {
		selection = (int)Q_brandom( &seed, 0, map_rotation_count );
	} while( selection == map_rotation_current );

	map_rotation_current = selection;
	return map_rotation_p[map_rotation_current];
}

/*
* G_ChooseNextMap
*/
static edict_t *G_ChooseNextMap( void ) {
	edict_t *ent = NULL;
	const char *next;

	if( *level.forcemap ) {
		return CreateTargetChangeLevel( level.forcemap );
	}

	if( !( *g_maplist->string ) || g_maplist->string[0] == '\0' || g_maprotation->integer == 0 ) {
		// same map again
		return CreateTargetChangeLevel( level.mapname );
	} else if( g_maprotation->integer == 1 ) {
		next = G_MapRotationNormal();

		// not in the list, we go for the first one
		ent = CreateTargetChangeLevel( next ? next : level.mapname );
		return ent;
	} else if( g_maprotation->integer == 2 ) {
		next = G_MapRotationRandom();
		ent = CreateTargetChangeLevel( next ? next : level.mapname );
		return ent;
	}

	if( level.nextmap[0] ) { // go to a specific map
		return CreateTargetChangeLevel( level.nextmap );
	}

	// search for a changelevel
	ent = G_Find( NULL, FOFS( classname ), "target_changelevel" );
	if( !ent ) {
		// the map designer didn't include a changelevel,
		// so create a fake ent that goes back to the same level
		return CreateTargetChangeLevel( level.mapname );
	}
	return ent;
}

/*
* G_SelectNextMapName
*/
static const char *G_SelectNextMapName( void ) {
	edict_t *changelevel;

	changelevel = G_ChooseNextMap();
	return changelevel->map;
}

/*
* G_ExitLevel
*/
void G_ExitLevel( void ) {
	int i;
	edict_t *ent;
	char command[256];
	const char *nextmapname;
	bool loadmap = true;
	int64_t timeLimit;

	level.exitNow = false;

	nextmapname = G_SelectNextMapName();
	timeLimit = g_timelimit->integer > 0 ? max( g_timelimit->integer, 60 ) : 60;
	timeLimit *= 60 * 1000;

	// if it's the same map see if we can restart without loading
	if( !level.hardReset && !Q_stricmp( nextmapname, level.mapname ) && G_RespawnLevel() ) {
		loadmap = false;
	}

	AI_RemoveBots();

	if( loadmap ) {
		Q_snprintfz( command, sizeof( command ), "gamemap \"%s\"\n", nextmapname );
		trap_Cmd_ExecuteText( EXEC_APPEND, command );
	}

	G_SnapClients();

	// clear some things before going to next level
	for( i = 0; i < gs.maxclients; i++ ) {
		ent = game.edicts + 1 + i;
		if( !ent->r.inuse ) {
			continue;
		}

		ent->r.client->level.showscores = false;

		if( ent->health > ent->max_health ) {
			ent->health = ent->max_health;
		}

		// some things are only cleared when there's a new map load
		if( loadmap ) {
			ent->r.client->connecting = true; // set all connected players as "reconnecting"
			ent->s.team = TEAM_SPECTATOR;
		}
	}
}

void G_RestartLevel( void ) {
	Q_strncpyz( level.forcemap, level.mapname, sizeof( level.mapname ) );
	G_EndMatch();
}

//======================================================================

#ifndef GAME_HARD_LINKED
// this is only here so the functions in q_shared.c and q_math.c can link
void Sys_Error( const char *format, ... ) {
	va_list argptr;
	char msg[3072];

	va_start( argptr, format );
	Q_vsnprintfz( msg, sizeof( msg ), format, argptr );
	va_end( argptr );

	G_Error( "%s", msg );
}

void Com_Printf( const char *format, ... ) {
	va_list argptr;
	char msg[3072];

	va_start( argptr, format );
	Q_vsnprintfz( msg, sizeof( msg ), format, argptr );
	va_end( argptr );

	G_Printf( "%s", msg );
}
#endif
