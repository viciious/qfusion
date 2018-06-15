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
#include "g_local.h"

/*
* G_Teleport
*
* Teleports client to specified position
* If client is not spectator teleporting is only done if position is free and teleport effects are drawn.
*/
static bool G_Teleport( edict_t *ent, vec3_t origin, vec3_t angles ) {
	int i;

	if( !ent->r.inuse || !ent->r.client ) {
		return false;
	}

	if( ent->r.client->ps.pmove.pm_type != PM_SPECTATOR ) {
		trace_t tr;

		G_Trace( &tr, origin, ent->r.mins, ent->r.maxs, origin, ent, MASK_PLAYERSOLID );
		if( tr.fraction != 1.0f || tr.startsolid ) {
			return false;
		}

		G_TeleportEffect( ent, false );
	}

	VectorCopy( origin, ent->s.origin );
	VectorCopy( origin, ent->olds.origin );
	ent->s.teleported = true;

	VectorClear( ent->velocity );
	ent->r.client->ps.pmove.pm_time = 1;
	ent->r.client->ps.pmove.pm_flags |= PMF_TIME_TELEPORT;

	if( ent->r.client->ps.pmove.pm_type != PM_SPECTATOR ) {
		G_TeleportEffect( ent, true );
	}

	// set angles
	VectorCopy( angles, ent->s.angles );
	VectorCopy( angles, ent->r.client->ps.viewangles );

	// set the delta angle
	for( i = 0; i < 3; i++ )
		ent->r.client->ps.pmove.delta_angles[i] = ANGLE2SHORT( ent->r.client->ps.viewangles[i] ) - ent->r.client->ucmd.angles[i];

	return true;
}


//=================================================================================

/*
* Cmd_Give_f
*
* Give items to a client
*/
static void Cmd_Give_f( edict_t *ent ) {
	char *name;
	gsitem_t    *it;
	int i;
	bool give_all;

	if( !sv_cheats->integer ) {
		G_PrintMsg( ent, "Cheats are not enabled on this server.\n" );
		return;
	}

	name = trap_Cmd_Args();

	if( !Q_stricmp( name, "all" ) ) {
		give_all = true;
	} else {
		give_all = false;
	}

	if( give_all || !Q_stricmp( trap_Cmd_Argv( 1 ), "health" ) ) {
		if( trap_Cmd_Argc() == 3 ) {
			ent->health = atoi( trap_Cmd_Argv( 2 ) );
		} else {
			ent->health = ent->max_health;
		}
		if( !give_all ) {
			return;
		}
	}

	if( give_all || !Q_stricmp( name, "weapons" ) ) {
		for( i = 0; i < GS_MAX_ITEM_TAGS; i++ ) {
			it = GS_FindItemByTag( i );
			if( !it ) {
				continue;
			}

			if( !( it->flags & ITFLAG_PICKABLE ) ) {
				continue;
			}

			if( !( it->type & IT_WEAPON ) ) {
				continue;
			}

			ent->r.client->ps.inventory[i] += 1;
		}
		if( !give_all ) {
			return;
		}
	}

	if( give_all || !Q_stricmp( name, "ammo" ) ) {
		for( i = 0; i < GS_MAX_ITEM_TAGS; i++ ) {
			it = GS_FindItemByTag( i );
			if( !it ) {
				continue;
			}

			if( !( it->flags & ITFLAG_PICKABLE ) ) {
				continue;
			}

			if( !( it->type & IT_AMMO ) ) {
				continue;
			}

			Add_Ammo( ent->r.client, it, 1000, true );
		}
		if( !give_all ) {
			return;
		}
	}

	if( give_all || !Q_stricmp( name, "armor" ) ) {
		ent->r.client->resp.armor = GS_Armor_MaxCountForTag( ARMOR_RA );
		if( !give_all ) {
			return;
		}
	}

	if( give_all ) {
		for( i = 0; i < GS_MAX_ITEM_TAGS; i++ ) {
			it = GS_FindItemByTag( i );
			if( !it ) {
				continue;
			}

			if( !( it->flags & ITFLAG_PICKABLE ) ) {
				continue;
			}

			if( it->type & ( IT_ARMOR | IT_WEAPON | IT_AMMO ) ) {
				continue;
			}

			ent->r.client->ps.inventory[i] = 1;
		}
		return;
	}

	it = GS_FindItemByName( name );
	if( !it ) {
		name = trap_Cmd_Argv( 1 );
		it = GS_FindItemByName( name );
		if( !it ) {
			G_PrintMsg( ent, "unknown item\n" );
			return;
		}
	}

	if( !( it->flags & ITFLAG_PICKABLE ) ) {
		G_PrintMsg( ent, "non-pickup (givable) item\n" );
		return;
	}

	if( it->type & IT_AMMO ) {
		if( trap_Cmd_Argc() == 3 ) {
			ent->r.client->ps.inventory[it->tag] = atoi( trap_Cmd_Argv( 2 ) );
		} else {
			ent->r.client->ps.inventory[it->tag] += it->quantity;
		}
	} else {
		if( it->tag && ( it->tag > 0 ) && ( it->tag < GS_MAX_ITEM_TAGS ) ) {
			if( GS_FindItemByTag( it->tag ) != NULL ) {
				ent->r.client->ps.inventory[it->tag]++;
			}
		} else {
			G_PrintMsg( ent, "non-pickup (givable) item\n" );
		}
	}
}

/*
* Cmd_God_f
* Sets client to godmode
* argv(0) god
*/
static void Cmd_God_f( edict_t *ent ) {
	const char *msg;

	if( !sv_cheats->integer ) {
		G_PrintMsg( ent, "Cheats are not enabled on this server.\n" );
		return;
	}

	ent->flags ^= FL_GODMODE;
	if( !( ent->flags & FL_GODMODE ) ) {
		msg = "godmode OFF\n";
	} else {
		msg = "godmode ON\n";
	}

	G_PrintMsg( ent, "%s", msg );
}

/*
* Cmd_Noclip_f
*
* argv(0) noclip
*/
static void Cmd_Noclip_f( edict_t *ent ) {
	const char *msg;

	if( !sv_cheats->integer ) {
		G_PrintMsg( ent, "Cheats are not enabled on this server.\n" );
		return;
	}

	if( ent->movetype == MOVETYPE_NOCLIP ) {
		ent->movetype = MOVETYPE_PLAYER;
		msg = "noclip OFF\n";
	} else {
		ent->movetype = MOVETYPE_NOCLIP;
		msg = "noclip ON\n";
	}

	G_PrintMsg( ent, "%s", msg );
}

/*
* Cmd_GameOperator_f
*/
static void Cmd_GameOperator_f( edict_t *ent ) {
	if( !g_operator_password->string[0] ) {
		G_PrintMsg( ent, "Operator is disabled in this server\n" );
		return;
	}

	if( trap_Cmd_Argc() < 2 ) {
		G_PrintMsg( ent, "Usage: 'operator <password>' or 'op <password>'\n" );
		return;
	}

	if( !Q_stricmp( trap_Cmd_Argv( 1 ), g_operator_password->string ) ) {
		if( !ent->r.client->isoperator ) {
			G_PrintMsg( NULL, "%s" S_COLOR_WHITE " is now a game operator\n", ent->r.client->netname );
		}

		ent->r.client->isoperator = true;
		return;
	}

	G_PrintMsg( ent, "Incorrect operator password.\n" );
}

/*
* Cmd_Use_f
* Use an inventory item
*/
static void Cmd_Use_f( edict_t *ent ) {
	gsitem_t    *it;

	assert( ent && ent->r.client );

	it = GS_Cmd_UseItem( &ent->r.client->ps, trap_Cmd_Args(), 0 );
	if( !it ) {
		return;
	}

	G_UseItem( ent, it );
}

/*
* Cmd_Kill_f
*/
static void Cmd_Kill_f( edict_t *ent ) {
	if( ent->r.solid == SOLID_NOT ) {
		return;
	}

	// can suicide after 5 seconds
	if( level.time < ent->r.client->resp.timeStamp + ( GS_RaceGametype() ? 1000 : 5000 ) ) {
		return;
	}

	ent->flags &= ~FL_GODMODE;
	ent->health = 0;
	meansOfDeath = MOD_SUICIDE;

	// wsw : pb : fix /kill command
	G_Killed( ent, ent, ent, 100000, vec3_origin, MOD_SUICIDE );
}

void Cmd_ChaseNext_f( edict_t *ent ) {
	G_ChaseStep( ent, 1 );
}

void Cmd_ChasePrev_f( edict_t *ent ) {
	G_ChaseStep( ent, -1 );
}

/*
* Cmd_PutAway_f
*/
static void Cmd_PutAway_f( edict_t *ent ) {
	ent->r.client->level.showscores = false;
}

/*
* Cmd_Score_f
*/
static void Cmd_Score_f( edict_t *ent ) {
	bool newvalue;

	if( trap_Cmd_Argc() == 2 ) {
		newvalue = ( atoi( trap_Cmd_Argv( 1 ) ) != 0 ) ? true : false;
	} else {
		newvalue = !ent->r.client->level.showscores ? true : false;
	}

	ent->r.client->level.showscores = newvalue;
}

/*
* Cmd_CvarInfo_f - Contains a cvar name and string provided by the client
*/
static void Cmd_CvarInfo_f( edict_t *ent ) {
	if( trap_Cmd_Argc() < 2 ) {
		G_PrintMsg( ent, "Cmd_CvarInfo_f: invalid argument count\n" );
		return;
	}

	// see if the gametype script is requesting this info
	if( !GT_asCallGameCommand( ent->r.client, "cvarinfo", trap_Cmd_Args(), trap_Cmd_Argc() - 1 ) ) {
		// if the gametype script wasn't interested in this command, print the output to console
		G_Printf( "%s%s's cvar '%s' is '%s%s'\n", ent->r.client->netname, S_COLOR_WHITE, trap_Cmd_Argv( 1 ), trap_Cmd_Argv( 2 ), S_COLOR_WHITE );
	}
}

/*
* Cmd_Position_f
*/
static void Cmd_Position_f( edict_t *ent ) {
	char *action;

	if( !sv_cheats->integer && GS_MatchState() > MATCH_STATE_WARMUP &&
		ent->r.client->ps.pmove.pm_type != PM_SPECTATOR ) {
		G_PrintMsg( ent, "Position command is only available in warmup and in spectator mode.\n" );
		return;
	}

	// flood protect
	if( ent->r.client->teamstate.position_lastcmd + 500 > game.realtime ) {
		return;
	}
	ent->r.client->teamstate.position_lastcmd = game.realtime;

	action = trap_Cmd_Argv( 1 );

	if( !Q_stricmp( action, "save" ) ) {
		ent->r.client->teamstate.position_saved = true;
		VectorCopy( ent->s.origin, ent->r.client->teamstate.position_origin );
		VectorCopy( ent->s.angles, ent->r.client->teamstate.position_angles );
		G_PrintMsg( ent, "Position saved.\n" );
	} else if( !Q_stricmp( action, "load" ) ) {
		if( !ent->r.client->teamstate.position_saved ) {
			G_PrintMsg( ent, "No position saved.\n" );
		} else {
			if( ent->r.client->resp.chase.active ) {
				G_SpectatorMode( ent );
			}

			if( G_Teleport( ent, ent->r.client->teamstate.position_origin, ent->r.client->teamstate.position_angles ) ) {
				G_PrintMsg( ent, "Position loaded.\n" );
			} else {
				G_PrintMsg( ent, "Position not available.\n" );
			}
		}
	} else if( !Q_stricmp( action, "set" ) && trap_Cmd_Argc() == 7 ) {
		vec3_t origin, angles;
		int i, argnumber = 2;

		for( i = 0; i < 3; i++ )
			origin[i] = atof( trap_Cmd_Argv( argnumber++ ) );
		for( i = 0; i < 2; i++ )
			angles[i] = atof( trap_Cmd_Argv( argnumber++ ) );
		angles[2] = 0;

		if( ent->r.client->resp.chase.active ) {
			G_SpectatorMode( ent );
		}

		if( G_Teleport( ent, origin, angles ) ) {
			G_PrintMsg( ent, "Position not available.\n" );
		} else {
			G_PrintMsg( ent, "Position set.\n" );
		}
	} else {
		char msg[MAX_STRING_CHARS];

		msg[0] = 0;
		Q_strncatz( msg, "Usage:\nposition save - Save current position\n", sizeof( msg ) );
		Q_strncatz( msg, "position load - Teleport to saved position\n", sizeof( msg ) );
		Q_strncatz( msg, "position set <x> <y> <z> <pitch> <yaw> - Teleport to specified position\n", sizeof( msg ) );
		Q_strncatz( msg, va( "Current position: %.4f %.4f %.4f %.4f %.4f\n", ent->s.origin[0], ent->s.origin[1],
							 ent->s.origin[2], ent->s.angles[0], ent->s.angles[1] ), sizeof( msg ) );
		G_PrintMsg( ent, "%s", msg );
	}
}

/*
* Cmd_PlayersExt_f
*/
static void Cmd_PlayersExt_f( edict_t *ent, bool onlyspecs ) {
	int i;
	int count = 0;
	int start = 0;
	char line[64];
	char msg[1024];

	if( trap_Cmd_Argc() > 1 ) {
		start = atoi( trap_Cmd_Argv( 1 ) );
	}
	clamp( start, 0, gs.maxclients - 1 );

	// print information
	msg[0] = 0;

	for( i = start; i < gs.maxclients; i++ ) {
		if( trap_GetClientState( i ) >= CS_SPAWNED ) {
			edict_t *clientEnt = &game.edicts[i + 1];
			gclient_t *cl;
			const char *login;

			if( onlyspecs && clientEnt->s.team != TEAM_SPECTATOR ) {
				continue;
			}

			cl = clientEnt->r.client;

			login = NULL;
			if( cl->mm_session.IsValidSessionId() ) {
				login = Info_ValueForKey( cl->userinfo, "cl_mm_login" );
			}
			if( !login ) {
				login = "";
			}

			Q_snprintfz( line, sizeof( line ), "%3i %s" S_COLOR_WHITE "%s%s%s%s\n", i, cl->netname,
						 login[0] ? "(" S_COLOR_YELLOW : "", login, login[0] ? S_COLOR_WHITE ")" : "",
						 cl->isoperator ? " op" : "" );

			if( strlen( line ) + strlen( msg ) > sizeof( msg ) - 100 ) {
				// can't print all of them in one packet
				Q_strncatz( msg, "...\n", sizeof( msg ) );
				break;
			}

			if( count == 0 ) {
				Q_strncatz( msg, "num name\n", sizeof( msg ) );
				Q_strncatz( msg, "--- ------------------------------\n", sizeof( msg ) );
			}

			Q_strncatz( msg, line, sizeof( msg ) );
			count++;
		}
	}

	if( count ) {
		Q_strncatz( msg, "--- ------------------------------\n", sizeof( msg ) );
	}
	Q_strncatz( msg, va( "%3i %s\n", count, trap_Cmd_Argv( 0 ) ), sizeof( msg ) );
	G_PrintMsg( ent, "%s", msg );

	if( i < gs.maxclients ) {
		G_PrintMsg( ent, "Type '%s %i' for more %s\n", trap_Cmd_Argv( 0 ), i, trap_Cmd_Argv( 0 ) );
	}
}

/*
* Cmd_Players_f
*/
static void Cmd_Players_f( edict_t *ent ) {
	Cmd_PlayersExt_f( ent, false );
}

/*
* Cmd_Spectators_f
*/
static void Cmd_Spectators_f( edict_t *ent ) {
	Cmd_PlayersExt_f( ent, true );
}

bool CheckFlood( edict_t *ent, bool teamonly ) {
	int i;
	gclient_t *client;

	assert( ent != NULL );

	client = ent->r.client;
	assert( client != NULL );

	if( g_floodprotection_messages->modified ) {
		if( g_floodprotection_messages->integer < 0 ) {
			trap_Cvar_Set( "g_floodprotection_messages", "0" );
		}
		if( g_floodprotection_messages->integer > MAX_FLOOD_MESSAGES ) {
			trap_Cvar_Set( "g_floodprotection_messages", va( "%i", MAX_FLOOD_MESSAGES ) );
		}
		g_floodprotection_messages->modified = false;
	}

	if( g_floodprotection_team->modified ) {
		if( g_floodprotection_team->integer < 0 ) {
			trap_Cvar_Set( "g_floodprotection_team", "0" );
		}
		if( g_floodprotection_team->integer > MAX_FLOOD_MESSAGES ) {
			trap_Cvar_Set( "g_floodprotection_team", va( "%i", MAX_FLOOD_MESSAGES ) );
		}
		g_floodprotection_team->modified = false;
	}

	if( g_floodprotection_seconds->modified ) {
		if( g_floodprotection_seconds->value <= 0 ) {
			trap_Cvar_Set( "g_floodprotection_seconds", "4" );
		}
		g_floodprotection_seconds->modified = false;
	}

	if( g_floodprotection_penalty->modified ) {
		if( g_floodprotection_penalty->value < 0 ) {
			trap_Cvar_Set( "g_floodprotection_penalty", "10" );
		}
		g_floodprotection_penalty->modified = false;
	}

	// old protection still active
	if( !teamonly || g_floodprotection_team->integer ) {
		if( game.realtime < client->level.flood_locktill ) {
			G_PrintMsg( ent, "You can't talk for %d more seconds\n",
						(int)( ( client->level.flood_locktill - game.realtime ) / 1000.0f ) + 1 );
			return true;
		}
	}


	if( teamonly ) {
		if( g_floodprotection_team->integer && g_floodprotection_penalty->value > 0 ) {
			i = client->level.flood_team_whenhead - g_floodprotection_team->integer + 1;
			if( i < 0 ) {
				i = MAX_FLOOD_MESSAGES + i;
			}

			if( client->level.flood_team_when[i] && client->level.flood_team_when[i] <= game.realtime &&
				( game.realtime < client->level.flood_team_when[i] + g_floodprotection_seconds->integer * 1000 ) ) {
				client->level.flood_locktill = game.realtime + g_floodprotection_penalty->value * 1000;
				G_PrintMsg( ent, "Flood protection: You can't talk for %d seconds.\n", g_floodprotection_penalty->integer );
				return true;
			}
		}

		client->level.flood_team_whenhead = ( client->level.flood_team_whenhead + 1 ) % MAX_FLOOD_MESSAGES;
		client->level.flood_team_when[client->level.flood_team_whenhead] = game.realtime;
	} else {
		if( g_floodprotection_messages->integer && g_floodprotection_penalty->value > 0 ) {
			i = client->level.flood_whenhead - g_floodprotection_messages->integer + 1;
			if( i < 0 ) {
				i = MAX_FLOOD_MESSAGES + i;
			}

			if( client->level.flood_when[i] && client->level.flood_when[i] <= game.realtime &&
				( game.realtime < client->level.flood_when[i] + g_floodprotection_seconds->integer * 1000 ) ) {
				client->level.flood_locktill = game.realtime + g_floodprotection_penalty->value * 1000;
				G_PrintMsg( ent, "Flood protection: You can't talk for %d seconds.\n", g_floodprotection_penalty->integer );
				return true;
			}
		}

		client->level.flood_whenhead = ( client->level.flood_whenhead + 1 ) % MAX_FLOOD_MESSAGES;
		client->level.flood_when[client->level.flood_whenhead] = game.realtime;
	}

	return false;
}

static void Cmd_CoinToss_f( edict_t *ent ) {
	bool qtails;
	char *s;
	char upper[MAX_STRING_CHARS];

	if( GS_MatchState() > MATCH_STATE_WARMUP && !GS_MatchPaused() ) {
		G_PrintMsg( ent, "You can only toss coins during warmup or timeouts\n" );
		return;
	}
	if( CheckFlood( ent, false ) ) {
		return;
	}

	if( trap_Cmd_Argc() < 2 || ( Q_stricmp( "heads", trap_Cmd_Argv( 1 ) ) && Q_stricmp( "tails", trap_Cmd_Argv( 1 ) ) ) ) {
		//it isn't a valid token
		G_PrintMsg( ent, "You have to choose heads or tails when tossing a coin\n" );
		return;
	}

	Q_strncpyz( upper, trap_Cmd_Argv( 1 ), sizeof( upper ) );
	s = upper;
	while( *s ) {
		*s = toupper( *s );
		s++;
	}

	qtails = ( Q_stricmp( "heads", trap_Cmd_Argv( 1 ) ) != 0 ) ? true : false;
	if( qtails == ( rand() & 1 ) ) {
		G_PrintMsg( NULL, S_COLOR_YELLOW "COINTOSS %s: " S_COLOR_WHITE "It was %s! %s " S_COLOR_WHITE "tossed a coin and " S_COLOR_GREEN "won!\n", upper, trap_Cmd_Argv( 1 ), ent->r.client->netname );
		return;
	}

	G_PrintMsg( NULL, S_COLOR_YELLOW "COINTOSS %s: " S_COLOR_WHITE "It was %s! %s " S_COLOR_WHITE "tossed a coin and " S_COLOR_RED "lost!\n", upper, qtails ? "heads" : "tails", ent->r.client->netname );
}

/*
* Cmd_Say_f
*/
void Cmd_Say_f( edict_t *ent, bool arg0, bool checkflood ) {
	char *p;
	char text[2048];
	size_t arg0len = 0;

#ifdef AUTHED_SAY
	if( sv_mm_enable->integer && ent->r.client && ent->r.client->mm_session <= 0 ) {
		// unauthed players are only allowed to chat to public at non play-time
		if( GS_MatchState() == MATCH_STATE_PLAYTIME ) {
			G_PrintMsg( ent, "%s", S_COLOR_YELLOW "You must authenticate to be able to communicate to other players during the match.\n" );
			return;
		}
	}
#endif

	if( checkflood ) {
		if( CheckFlood( ent, false ) ) {
			return;
		}
	}

	if( ent->r.client && ( ent->r.client->muted & 1 ) ) {
		return;
	}

	if( trap_Cmd_Argc() < 2 && !arg0 ) {
		return;
	}

	text[0] = 0;

	if( arg0 ) {
		Q_strncatz( text, trap_Cmd_Argv( 0 ), sizeof( text ) );
		Q_strncatz( text, " ", sizeof( text ) );
		arg0len = strlen( text );
		Q_strncatz( text, trap_Cmd_Args(), sizeof( text ) );
	} else {
		p = trap_Cmd_Args();

		if( *p == '"' ) {
			if( p[strlen( p ) - 1] == '"' ) {
				p[strlen( p ) - 1] = 0;
			}
			p++;
		}
		Q_strncatz( text, p, sizeof( text ) );
	}

	// don't let text be too long for malicious reasons
	text[arg0len + ( MAX_CHAT_BYTES - 1 )] = 0;

	G_ChatMsg( NULL, ent, false, "%s", text );
}

/*
* Cmd_SayCmd_f
*/
static void Cmd_SayCmd_f( edict_t *ent ) {
	Cmd_Say_f( ent, false, true );
}

/*
* Cmd_SayTeam_f
*/
static void Cmd_SayTeam_f( edict_t *ent ) {
	G_Say_Team( ent, trap_Cmd_Args(), true );
}

/*
* Cmd_Join_f
*/
static void Cmd_Join_f( edict_t *ent ) {
	if( CheckFlood( ent, false ) ) {
		return;
	}

	G_Teams_Join_Cmd( ent );
}

/*
* Cmd_Timeout_f
*/
static void Cmd_Timeout_f( edict_t *ent ) {
	int num;

	if( ent->s.team == TEAM_SPECTATOR || GS_MatchState() != MATCH_STATE_PLAYTIME ) {
		return;
	}

	if( GS_TeamBasedGametype() ) {
		num = ent->s.team;
	} else {
		num = ENTNUM( ent ) - 1;
	}

	if( GS_MatchPaused() && ( level.timeout.endtime - level.timeout.time ) >= 2 * TIMEIN_TIME ) {
		G_PrintMsg( ent, "Timeout already in progress\n" );
		return;
	}

	if( g_maxtimeouts->integer != -1 && level.timeout.used[num] >= g_maxtimeouts->integer ) {
		if( g_maxtimeouts->integer == 0 ) {
			G_PrintMsg( ent, "Timeouts are not allowed on this server\n" );
		} else if( GS_TeamBasedGametype() ) {
			G_PrintMsg( ent, "Your team doesn't have any timeouts left\n" );
		} else {
			G_PrintMsg( ent, "You don't have any timeouts left\n" );
		}
		return;
	}

	G_PrintMsg( NULL, "%s%s called a timeout\n", ent->r.client->netname, S_COLOR_WHITE );

	if( !GS_MatchPaused() ) {
		G_AnnouncerSound( NULL, trap_SoundIndex( va( S_ANNOUNCER_TIMEOUT_TIMEOUT_1_to_2, ( rand() & 1 ) + 1 ) ), GS_MAX_TEAMS, true, NULL );
	}

	level.timeout.used[num]++;
	GS_GamestatSetFlag( GAMESTAT_FLAG_PAUSED, true );
	level.timeout.caller = num;
	level.timeout.endtime = level.timeout.time + TIMEOUT_TIME + FRAMETIME;
}

/*
* Cmd_Timeout_f
*/
static void Cmd_Timein_f( edict_t *ent ) {
	int num;

	if( ent->s.team == TEAM_SPECTATOR ) {
		return;
	}

	if( !GS_MatchPaused() ) {
		G_PrintMsg( ent, "No timeout in progress.\n" );
		return;
	}

	if( level.timeout.endtime - level.timeout.time <= 2 * TIMEIN_TIME ) {
		G_PrintMsg( ent, "The timeout is about to end already.\n" );
		return;
	}

	if( GS_TeamBasedGametype() ) {
		num = ent->s.team;
	} else {
		num = ENTNUM( ent ) - 1;
	}

	if( level.timeout.caller != num ) {
		if( GS_TeamBasedGametype() ) {
			G_PrintMsg( ent, "Your team didn't call this timeout.\n" );
		} else {
			G_PrintMsg( ent, "You didn't call this timeout.\n" );
		}
		return;
	}

	level.timeout.endtime = level.timeout.time + TIMEIN_TIME + FRAMETIME;

	G_AnnouncerSound( NULL, trap_SoundIndex( va( S_ANNOUNCER_TIMEOUT_TIMEIN_1_to_2, ( rand() & 1 ) + 1 ) ), GS_MAX_TEAMS, true, NULL );

	G_PrintMsg( NULL, "%s%s called a timein\n", ent->r.client->netname, S_COLOR_WHITE );
}

/*
* Cmd_Awards_f
*/
static void Cmd_Awards_f( edict_t *ent ) {
	gclient_t *client;
	gameaward_t *ga;
	int i, size;
	static char entry[MAX_TOKEN_CHARS];

	assert( ent && ent->r.client );
	client = ent->r.client;

	Q_snprintfz( entry, sizeof( entry ), "Awards for %s\n", client->netname );

	if( client->level.stats.awardAllocator ) {
		size = LA_Size( client->level.stats.awardAllocator );
		for( i = 0; i < size; i++ ) {
			ga = ( gameaward_t * )LA_Pointer( client->level.stats.awardAllocator, i );
			Q_strncatz( entry, va( "\t%dx %s\n", ga->count, ga->name ), sizeof( entry ) );
		}
		G_PrintMsg( ent, "%s", entry );
	}
}

/*
* G_StatsMessage
*
* Generates stats message for the entity
* The returned string must be freed by the caller using G_Free
* Note: This string must never contain " characters
*/
char *G_StatsMessage( edict_t *ent ) {
	gclient_t *client;
	gsitem_t *item;
	int i, shot_weak, hit_weak, shot_strong, hit_strong, shot_total, hit_total;
	static char entry[MAX_TOKEN_CHARS];

	assert( ent && ent->r.client );
	client = ent->r.client;

	// message header
	Q_snprintfz( entry, sizeof( entry ), "%d", PLAYERNUM( ent ) );

	for( i = WEAP_GUNBLADE; i < WEAP_TOTAL; i++ ) {
		item = GS_FindItemByTag( i );
		assert( item );

		hit_weak = hit_strong = 0;
		shot_weak = shot_strong = 0;

		if( item->weakammo_tag != AMMO_NONE ) {
			hit_weak = client->level.stats.accuracy_hits[item->weakammo_tag - AMMO_GUNBLADE];
			shot_weak = client->level.stats.accuracy_shots[item->weakammo_tag - AMMO_GUNBLADE];
		}

		if( item->ammo_tag != AMMO_NONE ) {
			hit_strong = client->level.stats.accuracy_hits[item->ammo_tag - AMMO_GUNBLADE];
			shot_strong = client->level.stats.accuracy_shots[item->ammo_tag - AMMO_GUNBLADE];
		}

		hit_total = hit_weak + hit_strong;
		shot_total = shot_weak + shot_strong;

		Q_strncatz( entry, va( " %d", shot_total ), sizeof( entry ) );
		if( shot_total < 1 ) {
			continue;
		}
		Q_strncatz( entry, va( " %d", hit_total ), sizeof( entry ) );

		// strong
		Q_strncatz( entry, va( " %d", shot_strong ), sizeof( entry ) );
		if( shot_strong != shot_total ) {
			Q_strncatz( entry, va( " %d", hit_strong ), sizeof( entry ) );
		}
	}

	Q_strncatz( entry, va( " %d %d", (int)client->level.stats.GetEntry( "dmg_given" ), (int)client->level.stats.GetEntry( "dmg_taken" ) ), sizeof( entry ) );
	Q_strncatz( entry, va( " %d %d", (int)client->level.stats.GetEntry( "health_taken" ), (int)client->level.stats.GetEntry( "armor_taken" ) ), sizeof( entry ) );

	// add enclosing quote
	Q_strncatz( entry, "\"", sizeof( entry ) );

	return entry;
}

/*
* Cmd_ShowStats_f
*/
static void Cmd_ShowStats_f( edict_t *ent ) {
	edict_t *target;

	if( trap_Cmd_Argc() > 2 ) {
		G_PrintMsg( ent, "Usage: stats [player]\n" );
		return;
	}

	if( trap_Cmd_Argc() == 2 ) {
		target = G_PlayerForText( trap_Cmd_Argv( 1 ) );
		if( target == NULL ) {
			G_PrintMsg( ent, "No such player\n" );
			return;
		}
	} else {
		if( ent->r.client->resp.chase.active && game.edicts[ent->r.client->resp.chase.target].r.client ) {
			target = &game.edicts[ent->r.client->resp.chase.target];
		} else {
			target = ent;
		}
	}

	if( target->s.team == TEAM_SPECTATOR ) {
		G_PrintMsg( ent, "No stats for spectators\n" );
		return;
	}

	trap_GameCmd( ent, va( "plstats 1 \"%s\"", G_StatsMessage( target ) ) );
}

/*
* Cmd_Whois_f
*/
static void Cmd_Whois_f( edict_t *ent ) {
	edict_t *target;
	gclient_t *cl;
	const char *login;

	if( trap_Cmd_Argc() > 2 ) {
		G_PrintMsg( ent, "Usage: whois [player]\n" );
		return;
	}

	if( trap_Cmd_Argc() == 2 ) {
		target = G_PlayerForText( trap_Cmd_Argv( 1 ) );
		if( target == NULL ) {
			G_PrintMsg( ent, "No such player\n" );
			return;
		}
	} else {
		if( ent->r.client->resp.chase.active && game.edicts[ent->r.client->resp.chase.target].r.client ) {
			target = &game.edicts[ent->r.client->resp.chase.target];
		} else {
			target = ent;
		}
	}

	cl = target->r.client;

	if( !cl->mm_session.IsValidSessionId() ) {
		G_PrintMsg( ent, "Unregistered player\n" );
		return;
	}

	login = Info_ValueForKey( cl->userinfo, "cl_mm_login" );

	G_PrintMsg( ent, "%s%s is %s\n", cl->netname, S_COLOR_WHITE, login ? login : "unknown" );
}

/*
* Cmd_TVConnect_f
*
* Sends cmd to connect to a non-full TV server with round robin balancing
*/
static void Cmd_TVConnect_f( edict_t *ent ) {
	G_MoveClientToTV( ent );
}

/*
* Cmd_Upstate_f
*
* Update client on the state of things
*/
static void Cmd_Upstate_f( edict_t *ent ) {
	G_UpdatePlayerMatchMsg( ent, true );
	G_SetPlayerHelpMessage( ent, ent->r.client->level.helpmessage, true );
	trap_GameCmd( ent, va( "qm %s", ent->r.client->level.quickMenuItems ) );
}

//===========================================================
//	client commands
//===========================================================

typedef void ( *gamecommandfunc_t )( edict_t * );

typedef struct
{
	char name[MAX_QPATH];
	gamecommandfunc_t func;
} g_gamecommands_t;

g_gamecommands_t g_Commands[MAX_GAMECOMMANDS];

// FIXME
void Cmd_ShowPLinks_f( edict_t *ent );
void Cmd_deleteClosestNode_f( edict_t *ent );

/*
* G_PrecacheGameCommands
*/
void G_PrecacheGameCommands( void ) {
	int i;

	for( i = 0; i < MAX_GAMECOMMANDS; i++ )
		trap_ConfigString( CS_GAMECOMMANDS + i, g_Commands[i].name );
}

/*
* G_AddCommand
*/
void G_AddCommand( const char *name, gamecommandfunc_t callback ) {
	int i;
	char temp[MAX_QPATH];
	static const char *blacklist[] = { "callvotevalidate", "callvotepassed", NULL };

	Q_strncpyz( temp, name, sizeof( temp ) );

	for( i = 0; blacklist[i] != NULL; i++ ) {
		if( !Q_stricmp( blacklist[i], temp ) ) {
			G_Printf( "WARNING: G_AddCommand: command name '%s' is write protected\n", temp );
			return;
		}
	}

	// see if we already had it in game side
	for( i = 0; i < MAX_GAMECOMMANDS; i++ ) {
		if( !g_Commands[i].name[0] ) {
			break;
		}
		if( !Q_stricmp( g_Commands[i].name, temp ) ) {
			// update func if different
			if( g_Commands[i].func != callback ) {
				g_Commands[i].func = ( gamecommandfunc_t )callback;
			}
			return;
		}
	}

	if( i == MAX_GAMECOMMANDS ) {
		G_Error( "G_AddCommand: Couldn't find a free g_Commands spot for the new command. (increase MAX_GAMECOMMANDS)\n" );
		return;
	}

	// we don't have it, add it
	g_Commands[i].func = ( gamecommandfunc_t )callback;
	Q_strncpyz( g_Commands[i].name, temp, sizeof( g_Commands[i].name ) );

	// add the configstring if the precache process was already done
	if( level.canSpawnEntities ) {
		trap_ConfigString( CS_GAMECOMMANDS + i, g_Commands[i].name );
	}
}

/*
* G_InitGameCommands
*/
void G_InitGameCommands( void ) {
	int i;

	for( i = 0; i < MAX_GAMECOMMANDS; i++ ) {
		g_Commands[i].func = NULL;
		g_Commands[i].name[0] = 0;
	}

	G_AddCommand( "cvarinfo", Cmd_CvarInfo_f );
	G_AddCommand( "position", Cmd_Position_f );
	G_AddCommand( "players", Cmd_Players_f );
	G_AddCommand( "spectators", Cmd_Spectators_f );
	G_AddCommand( "stats", Cmd_ShowStats_f );
	G_AddCommand( "say", Cmd_SayCmd_f );
	G_AddCommand( "say_team", Cmd_SayTeam_f );
	G_AddCommand( "svscore", Cmd_Score_f );
	G_AddCommand( "god", Cmd_God_f );
	G_AddCommand( "noclip", Cmd_Noclip_f );
	G_AddCommand( "use", Cmd_Use_f );
	G_AddCommand( "give", Cmd_Give_f );
	G_AddCommand( "kill", Cmd_Kill_f );
	G_AddCommand( "putaway", Cmd_PutAway_f );
	G_AddCommand( "chase", Cmd_ChaseCam_f );
	G_AddCommand( "chasenext", Cmd_ChaseNext_f );
	G_AddCommand( "chaseprev", Cmd_ChasePrev_f );
	G_AddCommand( "spec", Cmd_Spec_f );
	G_AddCommand( "enterqueue", G_Teams_JoinChallengersQueue );
	G_AddCommand( "leavequeue", G_Teams_LeaveChallengersQueue );
	G_AddCommand( "camswitch", Cmd_SwitchChaseCamMode_f );
	G_AddCommand( "timeout", Cmd_Timeout_f );
	G_AddCommand( "timein", Cmd_Timein_f );
	G_AddCommand( "cointoss", Cmd_CoinToss_f );
	G_AddCommand( "whois", Cmd_Whois_f );

	// callvotes commands
	G_AddCommand( "callvote", G_CallVote_Cmd );
	G_AddCommand( "vote", G_CallVotes_CmdVote );

	G_AddCommand( "opcall", G_OperatorVote_Cmd );
	G_AddCommand( "operator", Cmd_GameOperator_f );
	G_AddCommand( "op", Cmd_GameOperator_f );

	// teams commands
	G_AddCommand( "ready", G_Match_Ready );
	G_AddCommand( "unready", G_Match_NotReady );
	G_AddCommand( "notready", G_Match_NotReady );
	G_AddCommand( "toggleready", G_Match_ToggleReady );
	G_AddCommand( "join", Cmd_Join_f );

	// coach commands
	G_AddCommand( "coach", G_Teams_Coach );
	G_AddCommand( "lockteam", G_Teams_CoachLockTeam );
	G_AddCommand( "unlockteam", G_Teams_CoachUnLockTeam );
	G_AddCommand( "invite", G_Teams_Invite_f );

	// bot commands
	G_AddCommand( "botnotarget", AI_Cheat_NoTarget );

	// ch : added awards
	G_AddCommand( "awards", Cmd_Awards_f );

	// TV
	G_AddCommand( "tvconnect", Cmd_TVConnect_f );

	// misc
	G_AddCommand( "upstate", Cmd_Upstate_f );
}

/*
* ClientCommand
*/
void ClientCommand( edict_t *ent ) {
	char *cmd;
	int i;

	if( !ent->r.client || trap_GetClientState( PLAYERNUM( ent ) ) < CS_SPAWNED ) {
		return; // not fully in game yet

	}
	cmd = trap_Cmd_Argv( 0 );

	if( Q_stricmp( cmd, "cvarinfo" ) ) { // skip cvarinfo cmds because they are automatic responses
		G_Client_UpdateActivity( ent->r.client ); // activity detected

	}
	for( i = 0; i < MAX_GAMECOMMANDS; i++ ) {
		if( !g_Commands[i].name[0] ) {
			break;
		}

		if( !Q_stricmp( g_Commands[i].name, cmd ) ) {
			if( g_Commands[i].func ) {
				g_Commands[i].func( ent );
			} else {
				GT_asCallGameCommand( ent->r.client, cmd, trap_Cmd_Args(), trap_Cmd_Argc() - 1 );
			}
			return;
		}
	}

	G_PrintMsg( ent, "Bad user command: %s\n", cmd );
}
