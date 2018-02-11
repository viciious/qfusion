/*
Copyright (C) 2006 Pekka Lampila ("Medar"), Damien Deville ("Pb")
and German Garcia Fernandez ("Jal") for Chasseur de bots association.


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

#include "g_local.h"

extern cvar_t *g_votable_gametypes;
extern cvar_t *g_disable_vote_gametype;

//===================================================================

/*
* G_Timeout_Reset
*/
void G_Timeout_Reset( void ) {
	int i;

	GS_GamestatSetFlag( GAMESTAT_FLAG_PAUSED, false );
	level.timeout.time = 0;
	level.timeout.endtime = 0;
	level.timeout.caller = 0;
	for( i = 0; i < MAX_CLIENTS; i++ )
		level.timeout.used[i] = 0;
}

/*
* G_Timeout_Update
*
* Updates the timeout struct and informs clients about the status of the pause
*/
static void G_Timeout_Update( unsigned int msec ) {
	static int timeout_printtime = 0;
	static int timeout_last_endtime = 0;
	static int countdown_set = 1;

	if( !GS_MatchPaused() ) {
		return;
	}

	game.frametime = 0;

	if( timeout_last_endtime != level.timeout.endtime ) { // force print when endtime is changed
		timeout_printtime = 0;
		timeout_last_endtime = level.timeout.endtime;
	}

	level.timeout.time += msec;
	if( level.timeout.endtime && level.timeout.time >= level.timeout.endtime ) {
		level.timeout.time = 0;
		level.timeout.caller = -1;
		GS_GamestatSetFlag( GAMESTAT_FLAG_PAUSED, false );

		timeout_printtime = 0;
		timeout_last_endtime = -1;

		G_AnnouncerSound( NULL, trap_SoundIndex( va( S_ANNOUNCER_TIMEOUT_MATCH_RESUMED_1_to_2, ( rand() & 1 ) + 1 ) ),
						  GS_MAX_TEAMS, true, NULL );
		G_CenterPrintMsg( NULL, "Match resumed" );
		G_PrintMsg( NULL, "Match resumed\n" );
	} else if( timeout_printtime == 0 || level.timeout.time - timeout_printtime >= 1000 ) {
		if( level.timeout.endtime ) {
			int seconds_left = (int)( ( level.timeout.endtime - level.timeout.time ) / 1000.0 + 0.5 );

			if( seconds_left == ( TIMEIN_TIME * 2 ) / 1000 ) {
				G_AnnouncerSound( NULL, trap_SoundIndex( va( S_ANNOUNCER_COUNTDOWN_READY_1_to_2, ( rand() & 1 ) + 1 ) ),
								  GS_MAX_TEAMS, false, NULL );
				countdown_set = ( rand() & 1 ) + 1;
			} else if( seconds_left >= 1 && seconds_left <= 3 ) {
				G_AnnouncerSound( NULL, trap_SoundIndex( va( S_ANNOUNCER_COUNTDOWN_COUNT_1_to_3_SET_1_to_2, seconds_left,
															 countdown_set ) ), GS_MAX_TEAMS, false, NULL );
			}

			if( seconds_left > 1 ) {
				G_CenterPrintFormatMsg( NULL, 1, "Match will resume in %s seconds", va( "%i", seconds_left ) );
			} else {
				G_CenterPrintMsg( NULL, "Match will resume in 1 second" );
			}
		} else {
			G_CenterPrintMsg( NULL, "Match paused" );
		}

		timeout_printtime = level.timeout.time;
	}
}

/*
* G_UpdateServerInfo
* update the cvars which show the match state at server browsers
*/
static void G_UpdateServerInfo( void ) {
	// g_match_time
	if( GS_MatchState() <= MATCH_STATE_WARMUP ) {
		trap_Cvar_ForceSet( "g_match_time", "Warmup" );
	} else if( GS_MatchState() == MATCH_STATE_COUNTDOWN ) {
		trap_Cvar_ForceSet( "g_match_time", "Countdown" );
	} else if( GS_MatchState() == MATCH_STATE_PLAYTIME ) {
		// partly from G_GetMatchState
		char extra[MAX_INFO_VALUE];
		int clocktime, timelimit, mins, secs;

		if( GS_MatchDuration() ) {
			timelimit = ( ( GS_MatchDuration() ) * 0.001 ) / 60;
		} else {
			timelimit = 0;
		}

		clocktime = (float)( game.serverTime - GS_MatchStartTime() ) * 0.001f;

		if( clocktime <= 0 ) {
			mins = 0;
			secs = 0;
		} else {
			mins = clocktime / 60;
			secs = clocktime - mins * 60;
		}

		extra[0] = 0;
		if( GS_MatchExtended() ) {
			if( timelimit ) {
				Q_strncatz( extra, " overtime", sizeof( extra ) );
			} else {
				Q_strncatz( extra, " suddendeath", sizeof( extra ) );
			}
		}
		if( GS_MatchPaused() ) {
			Q_strncatz( extra, " (in timeout)", sizeof( extra ) );
		}

		if( timelimit ) {
			trap_Cvar_ForceSet( "g_match_time", va( "%02i:%02i / %02i:00%s", mins, secs, timelimit, extra ) );
		} else {
			trap_Cvar_ForceSet( "g_match_time", va( "%02i:%02i%s", mins, secs, extra ) );
		}
	} else {
		trap_Cvar_ForceSet( "g_match_time", "Finished" );
	}

	// g_match_score
	if( GS_MatchState() >= MATCH_STATE_PLAYTIME && GS_TeamBasedGametype() ) {
		char score[MAX_INFO_STRING];

		score[0] = 0;
		Q_strncatz( score, va( " %s: %d", GS_TeamName( TEAM_ALPHA ), teamlist[TEAM_ALPHA].stats.score ), sizeof( score ) );
		Q_strncatz( score, va( " %s: %d", GS_TeamName( TEAM_BETA ), teamlist[TEAM_BETA].stats.score ), sizeof( score ) );

		if( strlen( score ) >= MAX_INFO_VALUE ) {
			// prevent "invalid info cvar value" flooding
			score[0] = '\0';
		}
		trap_Cvar_ForceSet( "g_match_score", score );
	} else {
		trap_Cvar_ForceSet( "g_match_score", "" );
	}

	// g_needpass
	if( password->modified ) {
		if( password->string && strlen( password->string ) ) {
			trap_Cvar_ForceSet( "g_needpass", "1" );
		} else {
			trap_Cvar_ForceSet( "g_needpass", "0" );
		}
		password->modified = false;
	}

	// g_gametypes_available
	if( g_votable_gametypes->modified || g_disable_vote_gametype->modified ) {
		if( g_disable_vote_gametype->integer || !g_votable_gametypes->string || !strlen( g_votable_gametypes->string ) ) {
			trap_Cvar_ForceSet( "g_gametypes_available", "" );
		} else {
			char *votable;
			char *name;
			size_t len;
			int count;

			len = 0;

			for( count = 0; ( name = COM_ListNameForPosition( g_gametypes_list->string, count, CHAR_GAMETYPE_SEPARATOR ) ) != NULL; count++ ) {
				if( G_Gametype_IsVotable( name ) ) {
					len += strlen( name ) + 1;
				}
			}

			len++;
			votable = ( char * )G_Malloc( len );
			votable[0] = 0;

			for( count = 0; ( name = COM_ListNameForPosition( g_gametypes_list->string, count, CHAR_GAMETYPE_SEPARATOR ) ) != NULL; count++ ) {
				if( G_Gametype_IsVotable( name ) ) {
					Q_strncatz( votable, name, len );
					Q_strncatz( votable, " ", len );
				}
			}

			//votable[ strlen( votable )-2 ] = 0; // remove the last space
			trap_Cvar_ForceSet( "g_gametypes_available", votable );
			G_Free( votable );
		}

		g_votable_gametypes->modified = false;
		g_disable_vote_gametype->modified = false;
	}

	if( GS_RaceGametype() ) {
		trap_Cvar_ForceSet( "g_race_gametype", "1" );
	} else {
		trap_Cvar_ForceSet( "g_race_gametype", "0" );
	}
}

/*
* G_CheckCvars
* Check for cvars that have been modified and need the game to be updated
*/
void G_CheckCvars( void ) {
	if( g_antilag_maxtimedelta->modified ) {
		if( g_antilag_maxtimedelta->integer < 0 ) {
			trap_Cvar_SetValue( "g_antilag_maxtimedelta", abs( g_antilag_maxtimedelta->integer ) );
		}
		g_antilag_maxtimedelta->modified = false;
		g_antilag_timenudge->modified = true;
	}

	if( g_antilag_timenudge->modified ) {
		if( g_antilag_timenudge->integer > g_antilag_maxtimedelta->integer ) {
			trap_Cvar_SetValue( "g_antilag_timenudge", g_antilag_maxtimedelta->integer );
		} else if( g_antilag_timenudge->integer < -g_antilag_maxtimedelta->integer ) {
			trap_Cvar_SetValue( "g_antilag_timenudge", -g_antilag_maxtimedelta->integer );
		}
		g_antilag_timenudge->modified = false;
	}

	if( g_warmup_timelimit->modified ) {
		// if we are inside timelimit period, update the endtime
		if( GS_MatchState() == MATCH_STATE_WARMUP ) {
			gs.gameState.stats[GAMESTAT_MATCHDURATION] = (int64_t)fabs( 60.0f * 1000 * g_warmup_timelimit->integer );
		}
		g_warmup_timelimit->modified = false;
	}

	if( g_timelimit->modified ) {
		// if we are inside timelimit period, update the endtime
		if( GS_MatchState() == MATCH_STATE_PLAYTIME &&
			!GS_MatchExtended() ) {
			if( g_timelimit->value ) {
				gs.gameState.stats[GAMESTAT_MATCHDURATION] = (int64_t)fabs( 60.0f * 1000 * g_timelimit->value );
			} else {
				gs.gameState.stats[GAMESTAT_MATCHDURATION] = 0;
			}
		}
		g_timelimit->modified = false;
	}

	if( g_match_extendedtime->modified ) {
		// if we are inside extended_time period, update the endtime
		if( GS_MatchExtended() ) {
			if( g_match_extendedtime->integer ) {
				gs.gameState.stats[GAMESTAT_MATCHDURATION] = (int64_t)fabs( 60 * 1000 * g_match_extendedtime->value );
			}
		}
		g_match_extendedtime->modified = false;
	}

	if( g_allow_falldamage->modified ) {
		g_allow_falldamage->modified = false;
	}

	// update gameshared server settings

	// FIXME: This should be restructured so gameshared settings are the master settings
	GS_GamestatSetFlag( GAMESTAT_FLAG_INSTAGIB, ( g_instagib->integer != 0 ) );
	GS_GamestatSetFlag( GAMESTAT_FLAG_FALLDAMAGE, ( g_allow_falldamage->integer != 0 ) );
	GS_GamestatSetFlag( GAMESTAT_FLAG_SELFDAMAGE, ( g_allow_selfdamage->integer != 0 ) );
	GS_GamestatSetFlag( GAMESTAT_FLAG_HASCHALLENGERS, level.gametype.hasChallengersQueue );

	GS_GamestatSetFlag( GAMESTAT_FLAG_ISTEAMBASED, level.gametype.isTeamBased );
	GS_GamestatSetFlag( GAMESTAT_FLAG_ISRACE, level.gametype.isRace );

	GS_GamestatSetFlag( GAMESTAT_FLAG_COUNTDOWN, level.gametype.countdownEnabled );
	GS_GamestatSetFlag( GAMESTAT_FLAG_INHIBITSHOOTING, level.gametype.shootingDisabled );
	GS_GamestatSetFlag( GAMESTAT_FLAG_INFINITEAMMO, ( level.gametype.infiniteAmmo || GS_Instagib() ) );
	GS_GamestatSetFlag( GAMESTAT_FLAG_CANFORCEMODELS, level.gametype.canForceModels );
	GS_GamestatSetFlag( GAMESTAT_FLAG_CANSHOWMINIMAP, level.gametype.canShowMinimap );
	GS_GamestatSetFlag( GAMESTAT_FLAG_TEAMONLYMINIMAP, level.gametype.teamOnlyMinimap );

	GS_GamestatSetFlag( GAMESTAT_FLAG_MMCOMPATIBLE, level.gametype.mmCompatible );

	GS_GamestatSetFlag( GAMESTAT_FLAG_ISTUTORIAL, level.gametype.isTutorial );
	GS_GamestatSetFlag( GAMESTAT_FLAG_CANDROPWEAPON, ( level.gametype.dropableItemsMask & IT_WEAPON ) != 0 );

	gs.gameState.stats[GAMESTAT_MAXPLAYERSINTEAM] = level.gametype.maxPlayersPerTeam;
	clamp( gs.gameState.stats[GAMESTAT_MAXPLAYERSINTEAM], 0, 255 );

}

//===================================================================
//		SNAP FRAMES
//===================================================================

static bool g_snapStarted = false;

/*
* G_SnapClients
*/
void G_SnapClients( void ) {
	int i;
	edict_t *ent;

	// calc the player views now that all pushing and damage has been added
	for( i = 0; i < gs.maxclients; i++ ) {
		ent = game.edicts + 1 + i;
		if( !ent->r.inuse || !ent->r.client ) {
			continue;
		}

		G_Client_InactivityRemove( ent->r.client );

		G_ClientEndSnapFrame( ent );

		if( ent->s.effects & EF_BUSYICON ) {
			ent->flags |= FL_BUSY;
		} else {
			ent->flags &= ~FL_BUSY;
		}
	}

	G_EndServerFrames_UpdateChaseCam();
}

/*
* G_EdictsAddSnapEffects
* add effects based on accumulated info along the server frame
*/
static void G_SnapEntities( void ) {
	edict_t *ent;
	int i;
	vec3_t dir, origin;

	for( i = 0, ent = &game.edicts[0]; i < game.numentities; i++, ent++ ) {
		if( !ent->r.inuse || ( ent->r.svflags & SVF_NOCLIENT ) ) {
			continue;
		}

		if( ent->s.type == ET_PARTICLES ) { // particles use a special configuration
			ent->s.frame = ent->particlesInfo.speed;
			ent->s.modelindex = ent->particlesInfo.shaderIndex;
			ent->s.modelindex2 = ent->particlesInfo.spread;
			ent->s.counterNum = ent->particlesInfo.time;
			ent->s.weapon = ent->particlesInfo.frequency;

			ent->s.effects = ent->particlesInfo.size & 0xFF;
			if( ent->particlesInfo.spherical ) {
				ent->s.effects |= ( 1 << 8 );
			}
			if( ent->particlesInfo.bounce ) {
				ent->s.effects |= ( 1 << 9 );
			}
			if( ent->particlesInfo.gravity ) {
				ent->s.effects |= ( 1 << 10 );
			}
			if( ent->particlesInfo.expandEffect ) {
				ent->s.effects |= ( 1 << 11 );
			}
			if( ent->particlesInfo.shrinkEffect ) {
				ent->s.effects |= ( 1 << 11 );
			}

			GClip_LinkEntity( ent );

			continue;
		}

		if( ent->s.type == ET_PLAYER || ent->s.type == ET_CORPSE ) {
			// this is pretty hackish
			if( !G_ISGHOSTING( ent ) ) {
				VectorCopy( ent->velocity, ent->s.origin2 );
			}
		}

		if( ISEVENTENTITY( ent ) || G_ISGHOSTING( ent ) || !ent->takedamage ) {
			continue;
		}

		// types which can have accumulated damage effects
		if( ( ent->s.type == ET_GENERIC || ent->s.type == ET_PLAYER || ent->s.type == ET_CORPSE ) ) { // doors don't bleed
			// Until we get a proper damage saved effect, we accumulate both into the blood fx
			// so, at least, we don't send 2 entities where we can send one
			ent->snap.damage_taken += ent->snap.damage_saved;

			//ent->snap.damage_saved = 0;

			//spawn accumulated damage
			if( ent->snap.damage_taken && !( ent->flags & FL_GODMODE ) && HEALTH_TO_INT( ent->health ) > 0 ) {
				edict_t *event;
				float damage = ent->snap.damage_taken;
				if( damage > 120 ) {
					damage = 120;
				}

				VectorCopy( ent->snap.damage_dir, dir );
				VectorNormalize( dir );
				VectorAdd( ent->s.origin, ent->snap.damage_at, origin );

				if( ent->s.type == ET_PLAYER || ent->s.type == ET_CORPSE ) {
					event = G_SpawnEvent( EV_BLOOD, DirToByte( dir ), origin );
					event->s.damage = HEALTH_TO_INT( damage );
					event->s.ownerNum = i; // set owner

					// ET_PLAYERS can also spawn sound events
					if( ent->s.type == ET_PLAYER ) {
						// play an apropriate pain sound
						if( level.time >= ent->pain_debounce_time ) {
							// see if it should pain for a FALL or for damage received
							if( ent->snap.damage_fall ) {
								ent->pain_debounce_time = level.time + 200;
							} else if( !G_IsDead( ent ) ) {
								if( ent->r.client->ps.inventory[POWERUP_SHELL] > 0 ) {
									G_AddEvent( ent, EV_PAIN, PAIN_WARSHELL, true );
								} else if( ent->health <= 20 ) {
									G_AddEvent( ent, EV_PAIN, PAIN_20, true );
								} else if( ent->health <= 35 ) {
									G_AddEvent( ent, EV_PAIN, PAIN_30, true );
								} else if( ent->health <= 60 ) {
									G_AddEvent( ent, EV_PAIN, PAIN_60, true );
								} else {
									G_AddEvent( ent, EV_PAIN, PAIN_100, true );
								}

								ent->pain_debounce_time = level.time + 400;
							}
						}
					}
				} else {
					event = G_SpawnEvent( EV_SPARKS, DirToByte( dir ), origin );
					event->s.damage = HEALTH_TO_INT( damage );
				}
			}
		}
	}
}

/*
* G_StartFrameSnap
* a snap was just sent, set up for new one
*/
static void G_StartFrameSnap( void ) {
	g_snapStarted = true;
}

// backup entitiy sounds in timeout
static int entity_sound_backup[MAX_EDICTS];

/*
* G_ClearSnap
* We just run G_SnapFrame, the server just sent the snap to the clients,
* it's now time to clean up snap specific data to start the next snap from clean.
*/
void G_ClearSnap( void ) {
	edict_t *ent;

	game.realtime = trap_Milliseconds(); // level.time etc. might not be real time

	// clear gametype's clock override
	gs.gameState.stats[GAMESTAT_CLOCKOVERRIDE] = 0;

	// clear all events in the snap
	for( ent = &game.edicts[0]; ENTNUM( ent ) < game.numentities; ent++ ) {
		if( ISEVENTENTITY( &ent->s ) ) { // events do not persist after a snapshot
			G_FreeEdict( ent );
			continue;
		}

		// events only last for a single message
		ent->s.events[0] = ent->s.events[1] = 0;
		ent->s.eventParms[0] = ent->s.eventParms[1] = 0;
		ent->numEvents = 0;
		ent->eventPriority[0] = ent->eventPriority[1] = false;
		ent->s.teleported = false; // remove teleported bit.

		// remove effect bits that are (most likely) added from gametypes
		ent->s.effects = ( ent->s.effects & ( EF_TAKEDAMAGE | EF_CARRIER | EF_FLAG_TRAIL | EF_ROTATE_AND_BOB | EF_STRONG_WEAPON | EF_GHOST ) );
	}

	// recover some info, let players respawn and finally clear the snap structures
	for( ent = &game.edicts[0]; ENTNUM( ent ) < game.numentities; ent++ ) {
		if( !GS_MatchPaused() ) {
			// copy origin to old origin ( this old_origin is for snaps )
			G_CheckClientRespawnClick( ent );
		}

		if( GS_MatchPaused() ) {
			ent->s.sound = entity_sound_backup[ENTNUM( ent )];
		}

		// clear the snap temp info
		memset( &ent->snap, 0, sizeof( ent->snap ) );
		if( ent->r.client && trap_GetClientState( PLAYERNUM( ent ) ) >= CS_SPAWNED ) {
			memset( &ent->r.client->resp.snap, 0, sizeof( ent->r.client->resp.snap ) );

			// set race stats to invisible
			ent->r.client->ps.stats[STAT_TIME_SELF] = STAT_NOTSET;
			ent->r.client->ps.stats[STAT_TIME_BEST] = STAT_NOTSET;
			ent->r.client->ps.stats[STAT_TIME_RECORD] = STAT_NOTSET;
			ent->r.client->ps.stats[STAT_TIME_ALPHA] = STAT_NOTSET;
			ent->r.client->ps.stats[STAT_TIME_BETA] = STAT_NOTSET;
		}
	}

	g_snapStarted = false;
}

/*
* G_SnapFrame
* It's time to send a new snap, so set the world up for sending
*/
void G_SnapFrame( void ) {
	edict_t *ent;
	game.realtime = trap_Milliseconds(); // level.time etc. might not be real time

	//others
	G_UpdateServerInfo();

	// exit level
	if( level.exitNow ) {
		G_ExitLevel();
		return;
	}

	// finish snap
	G_SnapClients(); // build the playerstate_t structures for all players
	G_SnapEntities(); // add effects based on accumulated info along the frame

	// set entity bits (prepare entities for being sent in the snap)
	for( ent = &game.edicts[0]; ENTNUM( ent ) < game.numentities; ent++ ) {
		if( ent->s.number != ENTNUM( ent ) ) {
			if( developer->integer ) {
				G_Printf( "fixing ent->s.number (etype:%i, classname:%s)\n", ent->s.type, ent->classname ? ent->classname : "noclassname" );
			}
			ent->s.number = ENTNUM( ent );
		}

		// temporary filter (Q2 system to ensure reliability)
		// ignore ents without visible models unless they have an effect
		if( !ent->r.inuse ) {
			ent->r.svflags |= SVF_NOCLIENT;
			continue;
		} else if( ent->s.type >= ET_TOTAL_TYPES || ent->s.type < 0 ) {
			if( developer->integer ) {
				G_Printf( "'G_SnapFrame': Inhibiting invalid entity type %i\n", ent->s.type );
			}
			ent->r.svflags |= SVF_NOCLIENT;
			continue;
		} else if( !( ent->r.svflags & SVF_NOCLIENT ) && !ent->s.modelindex && !ent->s.effects
				   && !ent->s.sound && !ISEVENTENTITY( &ent->s ) && !ent->s.light && !ent->r.client ) {
			if( developer->integer ) {
				G_Printf( "'G_SnapFrame': fixing missing SVF_NOCLIENT flag (no effect)\n" );
			}
			ent->r.svflags |= SVF_NOCLIENT;
			continue;
		}

		ent->s.effects &= ~EF_TAKEDAMAGE;
		if( ent->takedamage ) {
			ent->s.effects |= EF_TAKEDAMAGE;
		}

		if( GS_MatchPaused() ) {
			// when in timeout, we don't send entity sounds
			entity_sound_backup[ENTNUM( ent )] = ent->s.sound;
			ent->s.sound = 0;
		}
	}
}

//===================================================================
//		WORLD FRAMES
//===================================================================

/*
* G_UpdateFrameTime
*/
static void G_UpdateFrameTime( unsigned int msec ) {
	game.frametime = msec;
	G_Timeout_Update( msec );
	game.realtime = trap_Milliseconds(); // level.time etc. might not be real time
}

/*
* G_RunEntities
* treat each object in turn
* even the world and clients get a chance to think
*/
static void G_RunEntities( void ) {
	edict_t *ent;

	for( ent = &game.edicts[0]; ENTNUM( ent ) < game.numentities; ent++ ) {
		if( !ent->r.inuse ) {
			continue;
		}
		if( ISEVENTENTITY( &ent->s ) ) {
			continue; // events do not think

		}
		level.current_entity = ent;

		// backup oldstate ( for world frame ).
		ent->olds = ent->s;

		// if the ground entity moved, make sure we are still on it
		if( !ent->r.client ) {
			if( ( ent->groundentity ) && ( ent->groundentity->linkcount != ent->groundentity_linkcount ) ) {
				G_CheckGround( ent );
			}
		}

		G_RunEntity( ent );

		if( ent->takedamage ) {
			ent->s.effects |= EF_TAKEDAMAGE;
		} else {
			ent->s.effects &= ~EF_TAKEDAMAGE;
		}
	}
}

/*
* G_RunClients
*/
static void G_RunClients( void ) {
	int i, step;
	edict_t *ent;

	if( level.framenum & 1 ) {
		i = gs.maxclients - 1;
		step = -1;
	} else {
		i = 0;
		step = 1;
	}

	for( ; i < gs.maxclients && i >= 0; i += step ) {
		ent = game.edicts + 1 + i;
		if( !ent->r.inuse ) {
			continue;
		}

		G_ClientThink( ent );

		if( ent->takedamage ) {
			ent->s.effects |= EF_TAKEDAMAGE;
		} else {
			ent->s.effects &= ~EF_TAKEDAMAGE;
		}
	}
}

/*
* G_GetNextThinkClient
* Cycles between connected clients
*/
static edict_t *G_GetNextThinkClient( edict_t *current ) {
	edict_t *check, *start;
	edict_t *first, *last;

	first = game.edicts + 1;
	last = game.edicts + gs.maxclients + 1;
	start = current ? current + 1 : first;

	for( check = start; ; check++ ) {
		if( check > last ) {
			// wrap
			check = first;
		}
		if( check->r.inuse ) {
			return check;
		}
		if( check == start ) {
			break;
		}
	}
	return NULL;
}

/*
* G_RunFrame
* Advances the world
*/
void G_RunFrame( unsigned int msec, int64_t serverTime ) {
	G_CheckCvars();

	game.localTime = time( NULL );

	unsigned int serverTimeDelta = serverTime - game.serverTime;
	game.serverTime = serverTime;
	G_UpdateFrameTime( msec );

	if( !g_snapStarted ) {
		G_StartFrameSnap();
	}

	G_CallVotes_Think();

	if( GS_MatchPaused() ) {
		// freeze match clock and linear projectiles
		gs.gameState.stats[GAMESTAT_MATCHSTART] += serverTimeDelta;
		for( edict_t *ent = game.edicts + gs.maxclients; ENTNUM( ent ) < game.numentities; ent++ ) {
			if( ent->s.linearMovement ) {
				ent->s.linearMovementTimeStamp += serverTimeDelta;
			}
		}

		G_RunClients();
		G_RunGametype();
		G_LevelGarbageCollect();
		return;
	}

	// reset warmup clock if not enough players
	if( GS_MatchWaiting() ) {
		gs.gameState.stats[GAMESTAT_MATCHSTART] = game.serverTime;
	}

	level.framenum++;
	level.time += msec;
	level.think_client_entity = G_GetNextThinkClient( level.think_client_entity );

	G_SpawnQueue_Think();

	// run the world
	G_asCallMapPreThink();
	AI_CommonFrame();
	G_RunClients();
	G_RunEntities();
	G_RunGametype();
	G_asCallMapPostThink();
	GClip_BackUpCollisionFrame();

	G_LevelGarbageCollect();
}
