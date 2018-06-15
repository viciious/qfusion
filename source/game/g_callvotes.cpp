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
#include "../qcommon/snap.h"

//===================================================================

int clientVoted[MAX_CLIENTS];
int clientVoteChanges[MAX_CLIENTS];

cvar_t *g_callvote_electpercentage;
cvar_t *g_callvote_electtime;          // in seconds
cvar_t *g_callvote_enabled;
cvar_t *g_callvote_maxchanges;
cvar_t *g_callvote_cooldowntime;

enum
{
	VOTED_NOTHING = 0,
	VOTED_YES,
	VOTED_NO
};

// Data that can be used by the vote specific functions
typedef struct
{
	edict_t *caller;
	bool operatorcall;
	struct callvotetype_s *callvote;
	int argc;
	char *argv[MAX_STRING_TOKENS];
	char *string;               // can be used to overwrite the displayed vote string
	void *data;                 // any data vote wants to carry over multiple calls of validate and to execute
} callvotedata_t;

typedef struct callvotetype_s
{
	char *name;
	int expectedargs;               // -1 = any amount, -2 = any amount except 0
	bool ( *validate )( callvotedata_t *data, bool first );
	void ( *execute )( callvotedata_t *vote );
	const char *( *current )( void );
	void ( *extraHelp )( edict_t *ent );
	http_response_code_t ( *webRequest )( http_query_method_t method, const char *resource,
										  const char *query_string, char **content, size_t *content_length );
	char *argument_format;
	char *help;
	char *argument_type;
	struct callvotetype_s *next;
} callvotetype_t;

// Data that will only be used by the common callvote functions
typedef struct
{
	int64_t timeout;           // time to finish
	callvotedata_t vote;
} callvotestate_t;

static callvotestate_t callvoteState;

static callvotetype_t *callvotesHeadNode = NULL;

//==============================================
//		Vote specifics
//==============================================

static void G_AppendString( char **pdst, const char *src, size_t *pdst_len, size_t *pdst_size ) {
	char *dst = *pdst;
	size_t dst_len = *pdst_len;
	size_t dst_size = *pdst_size;
	size_t src_len;

	assert( src != NULL );

	if( !dst ) {
		dst_size = 0x1000;
		dst_len = 0;
		dst = ( char * )G_Malloc( dst_size );
	}

	src_len = strlen( src );
	if( dst_len + src_len >= dst_size ) {
		char *old_dst = dst;

		dst_size = ( dst_len + src_len ) * 2;
		dst = ( char * )G_Malloc( dst_size );
		memcpy( dst, old_dst, dst_len );
		dst[dst_len] = '\0';

		G_Free( old_dst );
	}

	memcpy( dst + dst_len, src, src_len );
	dst_len += src_len;
	dst[dst_len] = '\0';

	*pdst_len = dst_len;
	*pdst_size = dst_size;
	*pdst = dst;
}

static http_response_code_t G_PlayerlistWebRequest( http_query_method_t method, const char *resource,
													const char *query_string, char **content, size_t *content_length ) {
	int i;
	char *msg = NULL;
	size_t msg_len = 0, msg_size = 0;

	if( method != HTTP_METHOD_GET && method != HTTP_METHOD_HEAD ) {
		return HTTP_RESP_BAD_REQUEST;
	}

	for( i = 0; i < gs.maxclients; i++ ) {
		if( trap_GetClientState( i ) >= CS_SPAWNED ) {
			G_AppendString( &msg, va(
								"{\n"
								"\"value\"" " " "\"%i\"" "\n"
								"\"name\"" " " "\"%s\"" "\n"
								"}\n",
								i,
								game.clients[i].netname
								), &msg_len, &msg_size );
		}
	}

	*content = msg;
	*content_length = msg_len;
	return HTTP_RESP_OK;
}

/*
* shuffle/rebalance
*/
typedef struct
{
	int ent;
	int weight;
} weighted_player_t;

static int G_VoteCompareWeightedPlayers( const void *a, const void *b ) {
	const weighted_player_t *pa = ( const weighted_player_t * )a;
	const weighted_player_t *pb = ( const weighted_player_t * )b;
	return pb->weight - pa->weight;
}

/*
* map
*/

#define MAPLIST_SEPS " ,"

static void G_VoteMapExtraHelp( edict_t *ent ) {
	char *s;
	char buffer[MAX_STRING_CHARS];
	char message[MAX_STRING_CHARS / 4 * 3];    // use buffer to send only one print message
	int nummaps, i, start;
	size_t length, msglength;

	// update the maplist
	trap_ML_Update();

	if( g_enforce_map_pool->integer && strlen( g_map_pool->string ) > 2 ) {
		G_PrintMsg( ent, "Maps available [map pool enforced]:\n %s\n", g_map_pool->string );
		return;
	}

	// don't use Q_strncatz and Q_strncpyz below because we
	// check length of the message string manually

	memset( message, 0, sizeof( message ) );
	strcpy( message, "- Available maps:" );

	for( nummaps = 0; trap_ML_GetMapByNum( nummaps, NULL, 0 ); nummaps++ )
		;

	if( trap_Cmd_Argc() > 2 ) {
		start = atoi( trap_Cmd_Argv( 2 ) ) - 1;
		if( start < 0 ) {
			start = 0;
		}
	} else {
		start = 0;
	}

	i = start;
	msglength = strlen( message );
	while( trap_ML_GetMapByNum( i, buffer, sizeof( buffer ) ) ) {
		i++;
		s = buffer;
		length = strlen( s );
		if( msglength + length + 3 >= sizeof( message ) ) {
			break;
		}

		strcat( message, " " );
		strcat( message, s );

		msglength += length + 1;
	}

	if( i == start ) {
		strcat( message, "\nNone" );
	}

	G_PrintMsg( ent, "%s\n", message );

	if( i < nummaps ) {
		G_PrintMsg( ent, "Type 'callvote map %i' for more maps\n", i + 1 );
	}
}

static bool G_VoteMapValidate( callvotedata_t *data, bool first ) {
	char mapname[MAX_CONFIGSTRING_CHARS];

	if( !first ) { // map can't become invalid while voting
		return true;
	}
	if( Q_isdigit( data->argv[0] ) ) { // FIXME
		return false;
	}

	if( strlen( "maps/" ) + strlen( data->argv[0] ) + strlen( ".bsp" ) >= MAX_CONFIGSTRING_CHARS ) {
		G_PrintMsg( data->caller, "%sToo long map name\n", S_COLOR_RED );
		return false;
	}

	Q_strncpyz( mapname, data->argv[0], sizeof( mapname ) );
	COM_SanitizeFilePath( mapname );

	if( !Q_stricmp( level.mapname, mapname ) ) {
		G_PrintMsg( data->caller, "%sYou are already on that map\n", S_COLOR_RED );
		return false;
	}

	if( !COM_ValidateRelativeFilename( mapname ) || strchr( mapname, '/' ) || strchr( mapname, '.' ) ) {
		G_PrintMsg( data->caller, "%sInvalid map name\n", S_COLOR_RED );
		return false;
	}

	if( trap_ML_FilenameExists( mapname ) ) {
		char msg[MAX_STRING_CHARS];
		char fullname[MAX_TOKEN_CHARS];

		Q_strncpyz( fullname, COM_RemoveColorTokens( trap_ML_GetFullname( mapname ) ), sizeof( fullname ) );
		if( !Q_stricmp( mapname, fullname ) ) {
			fullname[0] = '\0';
		}

		// check if valid map is in map pool when on
		if( g_enforce_map_pool->integer ) {
			char *s, *tok;

			// if map pool is empty, basically turn it off
			if( strlen( g_map_pool->string ) < 2 ) {
				return true;
			}

			s = G_CopyString( g_map_pool->string );
			tok = strtok( s, MAPLIST_SEPS );
			while( tok != NULL ) {
				if( !Q_stricmp( tok, mapname ) ) {
					G_Free( s );
					goto valid_map;
				} else {
					tok = strtok( NULL, MAPLIST_SEPS );
				}
			}
			G_Free( s );
			G_PrintMsg( data->caller, "%sMap is not in map pool.\n", S_COLOR_RED );
			return false;
		}

valid_map:
		if( fullname[0] != '\0' ) {
			Q_snprintfz( msg, sizeof( msg ), "%s (%s)", mapname, fullname );
		} else {
			Q_strncpyz( msg, mapname, sizeof( msg ) );
		}

		if( data->string ) {
			G_Free( data->string );
		}
		data->string = G_CopyString( msg );
		return true;
	}

	G_PrintMsg( data->caller, "%sNo such map available on this server\n", S_COLOR_RED );

	return false;
}

static void G_VoteMapPassed( callvotedata_t *vote ) {
	Q_strncpyz( level.forcemap, Q_strlwr( vote->argv[0] ), sizeof( level.forcemap ) );
	G_EndMatch();
}

static const char *G_VoteMapCurrent( void ) {
	return level.mapname;
}

static http_response_code_t G_VoteMapWebRequest( http_query_method_t method, const char *resource,
												 const char *query_string, char **content, size_t *content_length ) {
	int i;
	char *msg = NULL;
	size_t msg_len = 0, msg_size = 0;
	char buffer[MAX_STRING_CHARS];

	if( method != HTTP_METHOD_GET && method != HTTP_METHOD_HEAD ) {
		return HTTP_RESP_BAD_REQUEST;
	}

	// update the maplist
	trap_ML_Update();

	if( g_enforce_map_pool->integer && strlen( g_map_pool->string ) > 2 ) {
		char *s, *tok;

		s = G_CopyString( g_map_pool->string );
		tok = strtok( s, MAPLIST_SEPS );
		while( tok != NULL ) {
			const char *fullname = trap_ML_GetFullname( tok );

			G_AppendString( &msg, va(
								"{\n"
								"\"value\"" " " "\"%s\"" "\n"
								"\"name\"" " " "\"%s '%s'\"" "\n"
								"}\n",
								tok,
								tok, fullname
								), &msg_len, &msg_size );

			tok = strtok( NULL, MAPLIST_SEPS );
		}

		G_Free( s );
	} else {
		for( i = 0; trap_ML_GetMapByNum( i, buffer, sizeof( buffer ) ); i++ ) {
			G_AppendString( &msg, va(
								"{\n"
								"\"value\"" " " "\"%s\"" "\n"
								"\"name\"" " " "\"%s '%s'\"" "\n"
								"}\n",
								buffer,
								buffer, buffer + strlen( buffer ) + 1
								), &msg_len, &msg_size );
		}
	}

	*content = msg;
	*content_length = msg_len;
	return HTTP_RESP_OK;
}


/*
* restart
*/

static void G_VoteRestartPassed( callvotedata_t *vote ) {
	G_RestartLevel();
}


/*
* nextmap
*/

static void G_VoteNextMapPassed( callvotedata_t *vote ) {
	level.forcemap[0] = 0;
	G_EndMatch();
}


/*
* scorelimit
*/

static bool G_VoteScorelimitValidate( callvotedata_t *vote, bool first ) {
	int scorelimit = atoi( vote->argv[0] );

	if( scorelimit < 0 ) {
		if( first ) {
			G_PrintMsg( vote->caller, "%sCan't set negative scorelimit\n", S_COLOR_RED );
		}
		return false;
	}

	if( scorelimit == g_scorelimit->integer ) {
		if( first ) {
			G_PrintMsg( vote->caller, "%sScorelimit is already set to %i\n", S_COLOR_RED, scorelimit );
		}
		return false;
	}

	return true;
}

static void G_VoteScorelimitPassed( callvotedata_t *vote ) {
	trap_Cvar_Set( "g_scorelimit", va( "%i", atoi( vote->argv[0] ) ) );
}

static const char *G_VoteScorelimitCurrent( void ) {
	return va( "%i", g_scorelimit->integer );
}

/*
* timelimit
*/

static bool G_VoteTimelimitValidate( callvotedata_t *vote, bool first ) {
	int timelimit = atoi( vote->argv[0] );

	if( timelimit < 0 ) {
		if( first ) {
			G_PrintMsg( vote->caller, "%sCan't set negative timelimit\n", S_COLOR_RED );
		}
		return false;
	}

	if( timelimit == g_timelimit->integer ) {
		if( first ) {
			G_PrintMsg( vote->caller, "%sTimelimit is already set to %i\n", S_COLOR_RED, timelimit );
		}
		return false;
	}

	return true;
}

static void G_VoteTimelimitPassed( callvotedata_t *vote ) {
	trap_Cvar_Set( "g_timelimit", va( "%i", atoi( vote->argv[0] ) ) );
}

static const char *G_VoteTimelimitCurrent( void ) {
	return va( "%i", g_timelimit->integer );
}


/*
* gametype
*/

static void G_VoteGametypeExtraHelp( edict_t *ent ) {
	char message[2048], *name; // use buffer to send only one print message
	int count;

	message[0] = 0;

	if( g_gametype->latched_string && g_gametype->latched_string[0] != '\0' &&
		G_Gametype_Exists( g_gametype->latched_string ) ) {
		Q_strncatz( message, "- Will be changed to: ", sizeof( message ) );
		Q_strncatz( message, g_gametype->latched_string, sizeof( message ) );
		Q_strncatz( message, "\n", sizeof( message ) );
	}

	Q_strncatz( message, "- Available gametypes:", sizeof( message ) );

	for( count = 0; ( name = COM_ListNameForPosition( g_gametypes_list->string, count, CHAR_GAMETYPE_SEPARATOR ) ) != NULL;
		 count++ ) {
		if( G_Gametype_IsVotable( name ) ) {
			Q_strncatz( message, " ", sizeof( message ) );
			Q_strncatz( message, name, sizeof( message ) );
		}
	}

	G_PrintMsg( ent, "%s\n", message );
}

static http_response_code_t G_VoteGametypeWebRequest( http_query_method_t method, const char *resource,
													  const char *query_string, char **content, size_t *content_length ) {
	char *name; // use buffer to send only one print message
	int count;
	char *msg = NULL;
	size_t msg_len = 0, msg_size = 0;

	if( method != HTTP_METHOD_GET && method != HTTP_METHOD_HEAD ) {
		return HTTP_RESP_BAD_REQUEST;
	}

	for( count = 0; ( name = COM_ListNameForPosition( g_gametypes_list->string, count, CHAR_GAMETYPE_SEPARATOR ) ) != NULL;
		 count++ ) {
		if( G_Gametype_IsVotable( name ) ) {
			G_AppendString( &msg, va(
								"{\n"
								"\"value\"" " " "\"%s\"" "\n"
								"\"name\"" " " "\"%s\"" "\n"
								"}\n",
								name,
								name
								), &msg_len, &msg_size );
		}
	}

	*content = msg;
	*content_length = msg_len;
	return HTTP_RESP_OK;
}

static bool G_VoteGametypeValidate( callvotedata_t *vote, bool first ) {
	if( !G_Gametype_Exists( vote->argv[0] ) ) {
		if( first ) {
			G_PrintMsg( vote->caller, "%sgametype %s is not available\n", S_COLOR_RED, vote->argv[0] );
		}
		return false;
	}

	if( g_gametype->latched_string && G_Gametype_Exists( g_gametype->latched_string ) ) {
		if( ( GS_MatchState() > MATCH_STATE_PLAYTIME ) &&
			!Q_stricmp( vote->argv[0], g_gametype->latched_string ) ) {
			if( first ) {
				G_PrintMsg( vote->caller, "%s%s is already the next gametype\n", S_COLOR_RED, vote->argv[0] );
			}
			return false;
		}
	}

	if( ( GS_MatchState() <= MATCH_STATE_PLAYTIME || g_gametype->latched_string == NULL )
		&& !Q_stricmp( gs.gametypeName, vote->argv[0] ) ) {
		if( first ) {
			G_PrintMsg( vote->caller, "%s%s is the current gametype\n", S_COLOR_RED, vote->argv[0] );
		}
		return false;
	}

	// if the g_votable_gametypes is empty, allow all gametypes
	if( !G_Gametype_IsVotable( vote->argv[0] ) ) {
		if( first ) {
			G_PrintMsg( vote->caller, "%sVoting gametype %s is not allowed on this server\n",
						S_COLOR_RED, vote->argv[0] );
		}
		return false;
	}

	return true;
}

static void G_VoteGametypePassed( callvotedata_t *vote ) {
	char *gametype_string;
	char next_gametype_string[MAX_STRING_TOKENS];

	gametype_string = vote->argv[0];
	Q_strncpyz( next_gametype_string, gametype_string, sizeof( next_gametype_string ) );

	trap_Cvar_Set( "g_gametype", gametype_string );

	if( GS_MatchState() == MATCH_STATE_COUNTDOWN ||
		GS_MatchState() == MATCH_STATE_PLAYTIME || !G_RespawnLevel() ) {
		// go to scoreboard if in game
		Q_strncpyz( level.forcemap, level.mapname, sizeof( level.forcemap ) );
		G_EndMatch();
	}

	// we can't use gametype_string here, because there's a big chance it has just been freed after G_EndMatch
	G_PrintMsg( NULL, "Gametype changed to %s\n", next_gametype_string );
}

static const char *G_VoteGametypeCurrent( void ) {
	return gs.gametypeName;
}


/*
* warmup_timelimit
*/

static bool G_VoteWarmupTimelimitValidate( callvotedata_t *vote, bool first ) {
	int warmup_timelimit = atoi( vote->argv[0] );

	if( warmup_timelimit < 0 ) {
		if( first ) {
			G_PrintMsg( vote->caller, "%sCan't set negative warmup timelimit\n", S_COLOR_RED );
		}
		return false;
	}

	if( warmup_timelimit == g_warmup_timelimit->integer ) {
		if( first ) {
			G_PrintMsg( vote->caller, "%sWarmup timelimit is already set to %i\n", S_COLOR_RED, warmup_timelimit );
		}
		return false;
	}

	return true;
}

static void G_VoteWarmupTimelimitPassed( callvotedata_t *vote ) {
	trap_Cvar_Set( "g_warmup_timelimit", va( "%i", atoi( vote->argv[0] ) ) );
}

static const char *G_VoteWarmupTimelimitCurrent( void ) {
	return va( "%i", g_warmup_timelimit->integer );
}


/*
* extended_time
*/

static bool G_VoteExtendedTimeValidate( callvotedata_t *vote, bool first ) {
	int extended_time = atoi( vote->argv[0] );

	if( extended_time < 0 ) {
		if( first ) {
			G_PrintMsg( vote->caller, "%sCan't set negative extended time\n", S_COLOR_RED );
		}
		return false;
	}

	if( extended_time == g_match_extendedtime->integer ) {
		if( first ) {
			G_PrintMsg( vote->caller, "%sExtended time is already set to %i\n", S_COLOR_RED, extended_time );
		}
		return false;
	}

	return true;
}

static void G_VoteExtendedTimePassed( callvotedata_t *vote ) {
	trap_Cvar_Set( "g_match_extendedtime", va( "%i", atoi( vote->argv[0] ) ) );
}

static const char *G_VoteExtendedTimeCurrent( void ) {
	return va( "%i", g_match_extendedtime->integer );
}

/*
* allready
*/

static bool G_VoteAllreadyValidate( callvotedata_t *vote, bool first ) {
	int notreadys = 0;
	edict_t *ent;

	if( GS_MatchState() >= MATCH_STATE_COUNTDOWN ) {
		if( first ) {
			G_PrintMsg( vote->caller, "%sThe game is not in warmup mode\n", S_COLOR_RED );
		}
		return false;
	}

	for( ent = game.edicts + 1; PLAYERNUM( ent ) < gs.maxclients; ent++ ) {
		if( trap_GetClientState( PLAYERNUM( ent ) ) < CS_SPAWNED ) {
			continue;
		}

		if( ent->s.team > TEAM_SPECTATOR && !level.ready[PLAYERNUM( ent )] ) {
			notreadys++;
		}
	}

	if( !notreadys ) {
		if( first ) {
			G_PrintMsg( vote->caller, "%sEveryone is already ready\n", S_COLOR_RED );
		}
		return false;
	}

	return true;
}

static void G_VoteAllreadyPassed( callvotedata_t *vote ) {
	edict_t *ent;

	for( ent = game.edicts + 1; PLAYERNUM( ent ) < gs.maxclients; ent++ ) {
		if( trap_GetClientState( PLAYERNUM( ent ) ) < CS_SPAWNED ) {
			continue;
		}

		if( ent->s.team > TEAM_SPECTATOR && !level.ready[PLAYERNUM( ent )] ) {
			level.ready[PLAYERNUM( ent )] = true;
			G_UpdatePlayerMatchMsg( ent );
			G_Match_CheckReadys();
		}
	}
}

/*
* maxteamplayers
*/

static bool G_VoteMaxTeamplayersValidate( callvotedata_t *vote, bool first ) {
	int maxteamplayers = atoi( vote->argv[0] );

	if( maxteamplayers < 1 ) {
		if( first ) {
			G_PrintMsg( vote->caller, "%sThe maximum number of players in team can't be less than 1\n",
						S_COLOR_RED );
		}
		return false;
	}

	if( g_teams_maxplayers->integer == maxteamplayers ) {
		if( first ) {
			G_PrintMsg( vote->caller, "%sMaximum number of players in team is already %i\n",
						S_COLOR_RED, maxteamplayers );
		}
		return false;
	}

	return true;
}

static void G_VoteMaxTeamplayersPassed( callvotedata_t *vote ) {
	trap_Cvar_Set( "g_teams_maxplayers", va( "%i", atoi( vote->argv[0] ) ) );
}

static const char *G_VoteMaxTeamplayersCurrent( void ) {
	return va( "%i", g_teams_maxplayers->integer );
}

/*
* lock
*/

static bool G_VoteLockValidate( callvotedata_t *vote, bool first ) {
	if( GS_MatchState() > MATCH_STATE_PLAYTIME ) {
		if( first ) {
			G_PrintMsg( vote->caller, "%sCan't lock teams after the match\n", S_COLOR_RED );
		}
		return false;
	}

	if( level.teamlock ) {
		if( GS_MatchState() < MATCH_STATE_COUNTDOWN && first ) {
			G_PrintMsg( vote->caller, "%sTeams are already set to be locked on match start\n", S_COLOR_RED );
		} else if( first ) {
			G_PrintMsg( vote->caller, "%sTeams are already locked\n", S_COLOR_RED );
		}
		return false;
	}

	return true;
}

static void G_VoteLockPassed( callvotedata_t *vote ) {
	int team;

	level.teamlock = true;

	// if we are inside a match, update the teams state
	if( GS_MatchState() >= MATCH_STATE_COUNTDOWN && GS_MatchState() <= MATCH_STATE_PLAYTIME ) {
		if( GS_TeamBasedGametype() ) {
			for( team = TEAM_ALPHA; team < GS_MAX_TEAMS; team++ )
				G_Teams_LockTeam( team );
		} else {
			G_Teams_LockTeam( TEAM_PLAYERS );
		}
		G_PrintMsg( NULL, "Teams locked\n" );
	} else {
		G_PrintMsg( NULL, "Teams will be locked when the match starts\n" );
	}
}

/*
* unlock
*/

static bool G_VoteUnlockValidate( callvotedata_t *vote, bool first ) {
	if( GS_MatchState() > MATCH_STATE_PLAYTIME ) {
		if( first ) {
			G_PrintMsg( vote->caller, "%sCan't unlock teams after the match\n", S_COLOR_RED );
		}
		return false;
	}

	if( !level.teamlock ) {
		if( GS_MatchState() < MATCH_STATE_COUNTDOWN && first ) {
			G_PrintMsg( vote->caller, "%sTeams are not set to be locked\n", S_COLOR_RED );
		} else if( first ) {
			G_PrintMsg( vote->caller, "%sTeams are not locked\n", S_COLOR_RED );
		}
		return false;
	}

	return true;
}

static void G_VoteUnlockPassed( callvotedata_t *vote ) {
	int team;

	level.teamlock = false;

	// if we are inside a match, update the teams state
	if( GS_MatchState() >= MATCH_STATE_COUNTDOWN && GS_MatchState() <= MATCH_STATE_PLAYTIME ) {
		if( GS_TeamBasedGametype() ) {
			for( team = TEAM_ALPHA; team < GS_MAX_TEAMS; team++ )
				G_Teams_UnLockTeam( team );
		} else {
			G_Teams_UnLockTeam( TEAM_PLAYERS );
		}
		G_PrintMsg( NULL, "Teams unlocked\n" );
	} else {
		G_PrintMsg( NULL, "Teams will no longer be locked when the match starts\n" );
	}
}

/*
* remove
*/

static void G_VoteRemoveExtraHelp( edict_t *ent ) {
	int i;
	edict_t *e;
	char msg[1024];

	msg[0] = 0;
	Q_strncatz( msg, "- List of players in game:\n", sizeof( msg ) );

	if( GS_TeamBasedGametype() ) {
		int team;

		for( team = TEAM_ALPHA; team < GS_MAX_TEAMS; team++ ) {
			Q_strncatz( msg, va( "%s:\n", GS_TeamName( team ) ), sizeof( msg ) );
			for( i = 0, e = game.edicts + 1; i < gs.maxclients; i++, e++ ) {
				if( !e->r.inuse || e->s.team != team ) {
					continue;
				}

				Q_strncatz( msg, va( "%3i: %s\n", PLAYERNUM( e ), e->r.client->netname ), sizeof( msg ) );
			}
		}
	} else {
		for( i = 0, e = game.edicts + 1; i < gs.maxclients; i++, e++ ) {
			if( !e->r.inuse || e->s.team != TEAM_PLAYERS ) {
				continue;
			}

			Q_strncatz( msg, va( "%3i: %s\n", PLAYERNUM( e ), e->r.client->netname ), sizeof( msg ) );
		}
	}

	G_PrintMsg( ent, "%s", msg );
}

static bool G_VoteRemoveValidate( callvotedata_t *vote, bool first ) {
	int who = -1;

	if( first ) {
		edict_t *tokick = G_PlayerForText( vote->argv[0] );

		if( tokick ) {
			who = PLAYERNUM( tokick );
		} else {
			who = -1;
		}

		if( who == -1 ) {
			G_PrintMsg( vote->caller, "%sNo such player\n", S_COLOR_RED );
			return false;
		} else if( tokick->s.team == TEAM_SPECTATOR ) {
			G_PrintMsg( vote->caller, "Player %s%s%s is already spectator.\n", S_COLOR_WHITE,
						tokick->r.client->netname, S_COLOR_RED );

			return false;
		} else {
			// we save the player id to be removed, so we don't later get confused by new ids or players changing names
			vote->data = G_Malloc( sizeof( int ) );
			memcpy( vote->data, &who, sizeof( int ) );
		}
	} else {
		memcpy( &who, vote->data, sizeof( int ) );
	}

	if( !game.edicts[who + 1].r.inuse || game.edicts[who + 1].s.team == TEAM_SPECTATOR ) {
		return false;
	} else {
		if( !vote->string || Q_stricmp( vote->string, game.edicts[who + 1].r.client->netname ) ) {
			if( vote->string ) {
				G_Free( vote->string );
			}
			vote->string = G_CopyString( game.edicts[who + 1].r.client->netname );
		}

		return true;
	}
}

static edict_t *G_Vote_GetValidDeferredVoteTarget( callvotedata_t *vote ) {
	int who;
	memcpy( &who, vote->data, sizeof( int ) );

	edict_t *ent = game.edicts + who + 1;
	if( !ent->r.inuse || !ent->r.client ) {
		return nullptr;
	}

	return ent;
}

static void G_VoteRemovePassed( callvotedata_t *vote ) {
	edict_t *ent = G_Vote_GetValidDeferredVoteTarget( vote );

	// may have disconnect along the callvote time
	if( !ent || ent->s.team == TEAM_SPECTATOR ) {
		return;
	}

	G_PrintMsg( NULL, "Player %s%s removed from team %s%s.\n", ent->r.client->netname, S_COLOR_WHITE,
				GS_TeamName( ent->s.team ), S_COLOR_WHITE );

	G_Teams_SetTeam( ent, TEAM_SPECTATOR );
	ent->r.client->queueTimeStamp = 0;
}


/*
* kick
*/

static void G_VoteHelp_ShowPlayersList( edict_t *ent ) {
	int i;
	edict_t *e;
	char msg[1024];

	msg[0] = 0;
	Q_strncatz( msg, "- List of current players:\n", sizeof( msg ) );

	for( i = 0, e = game.edicts + 1; i < gs.maxclients; i++, e++ ) {
		if( !e->r.inuse ) {
			continue;
		}

		Q_strncatz( msg, va( "%2d: %s\n", PLAYERNUM( e ), e->r.client->netname ), sizeof( msg ) );
	}

	G_PrintMsg( ent, "%s", msg );
}

static bool G_SetOrValidateKickLikeCmdTarget( callvotedata_t *vote, bool first ) {
	int who = -1;
	if( first ) {
		edict_t *tokick = G_PlayerForText( vote->argv[0] );
		if( tokick ) {
			who = PLAYERNUM( tokick );
		} else {
			who = -1;
		}

		if( who != -1 ) {
			if( game.edicts[who + 1].r.client->isoperator ) {
				G_PrintMsg( vote->caller, S_COLOR_RED "%s is a game operator.\n", game.edicts[who + 1].r.client->netname );
				return false;
			}

			// we save the player id to be kicked, so we don't later get
			//confused by new ids or players changing names

			vote->data = G_Malloc( sizeof( int ) );
			memcpy( vote->data, &who, sizeof( int ) );
		} else {
			G_PrintMsg( vote->caller, S_COLOR_RED "%s: No such player\n", vote->argv[0] );
			return false;
		}
	} else {
		memcpy( &who, vote->data, sizeof( int ) );
	}

	edict_t *ent = game.edicts + who + 1;
	if( ent->r.inuse && ent->r.client ) {
		if( !vote->string || Q_stricmp( vote->string, ent->r.client->netname ) ) {
			if( vote->string ) {
				G_Free( vote->string );
			}
			vote->string = G_CopyString( ent->r.client->netname );
		}
		return true;
	}

	return false;
}

static bool G_VoteKickValidate( callvotedata_t *vote, bool first ) {
	return G_SetOrValidateKickLikeCmdTarget( vote, first );
}

const char *G_GetClientHostForFilter( const edict_t *ent ) {
	if( ent->r.svflags & SVF_FAKECLIENT ) {
		return nullptr;
	}

	if( !Q_stricmp( ent->r.client->ip, "loopback" ) ) {
		return nullptr;
	}

	// We have to strip port from the address since only host part is expected by a caller.
	// We are sure the port is present if we have already cut off special cases above.
	static char hostBuffer[MAX_INFO_VALUE];
	Q_strncpyz( hostBuffer, ent->r.client->ip, sizeof( hostBuffer ) );

	// If it is IPv6 host and port
	if( *hostBuffer == '[' ) {
		// Chop the buffer string at the index of the right bracket
		*( strchr( hostBuffer + 1, ']' ) ) = '\0';
		return hostBuffer + 1;
	}

	// Chop the buffer string at the index of the port separator
	*strchr( hostBuffer, ':' ) = '\0';
	return hostBuffer;
}

static void G_VoteKickPassed( callvotedata_t *vote ) {
	if( edict_t *ent = G_Vote_GetValidDeferredVoteTarget( vote ) ) {
		// If the address can be supplied for the filter
		if( const char *host = G_GetClientHostForFilter( ent ) ) {
			// Ban the player for 1 minute to prevent an instant reconnect
			trap_Cmd_ExecuteText( EXEC_APPEND, va( "addip %s 1", host ) );
		}
		trap_DropClient( ent, DROP_TYPE_NORECONNECT, "Kicked" );
	}
}


static bool G_VoteKickBanValidate( callvotedata_t *vote, bool first ) {
	if( !filterban->integer ) {
		G_PrintMsg( vote->caller, "%sFilterban is disabled on this server\n", S_COLOR_RED );
		return false;
	}

	return G_SetOrValidateKickLikeCmdTarget( vote, first );
}

static void G_VoteKickBanPassed( callvotedata_t *vote ) {
	if( edict_t *ent = G_Vote_GetValidDeferredVoteTarget( vote ) ) {
		// If the address can be supplied for the filter
		if( const char *host = G_GetClientHostForFilter( ent ) ) {
			trap_Cmd_ExecuteText( EXEC_APPEND, va( "addip %s 15\n", host ) );
		}
		trap_DropClient( ent, DROP_TYPE_NORECONNECT, "Kicked" );
	}
}

static bool G_VoteMuteValidate( callvotedata_t *vote, bool first ) {
	return G_SetOrValidateKickLikeCmdTarget( vote, first );
}

// chat mute
static void G_VoteMutePassed( callvotedata_t *vote ) {
	if( edict_t *ent = G_Vote_GetValidDeferredVoteTarget( vote ) ) {
		ent->r.client->muted |= 1;
		ent->r.client->level.stats.AddToEntry( "muted_count", 1 );
	}
}

static bool G_VoteUnmuteValidate( callvotedata_t *vote, bool first ) {
	return G_SetOrValidateKickLikeCmdTarget( vote, first );
}

// chat unmute
static void G_VoteUnmutePassed( callvotedata_t *vote ) {
	if( edict_t *ent = G_Vote_GetValidDeferredVoteTarget( vote ) ) {
		ent->r.client->muted &= ~1;
	}
}

static constexpr auto SNAP_ANTIWALLHACK_FLAGS =
	SNAP_HINT_CULL_SOUND_WITH_PVS | SNAP_HINT_USE_RAYCAST_CULLING | SNAP_HINT_SHADOW_EVENTS_DATA;
static constexpr auto SNAP_ANTIRADAR_FLAGS = SNAP_HINT_USE_VIEW_DIR_CULLING;
static constexpr auto SNAP_ANTICHEAT_FLAGS = SNAP_ANTIWALLHACK_FLAGS | SNAP_ANTIRADAR_FLAGS;

static const char *G_VoteEnableGlobalAntiWallhackCurrent();
static const char *G_VoteEnableGlobalAntiRadarCurrent();

static void G_VoteHelp_ShowPlayersListWithSnapFlags( edict_t *ent ) {
	int i;
	edict_t *e;
	char msg[1024];

	msg[0] = 0;
	const char *globalAntiWH = G_VoteEnableGlobalAntiWallhackCurrent();
	const char *globalAntiRD = G_VoteEnableGlobalAntiRadarCurrent();
	Q_strncatz( msg, va( "Global antiwallhack: %s, antiradar: %s\n", globalAntiWH, globalAntiRD ), sizeof( msg ) );
	Q_strncatz( msg, " # | -WH | -RD | nickname\n", sizeof( msg ) );
	Q_strncatz( msg, "-------------------------\n", sizeof( msg ) );

	for( i = 0, e = game.edicts + 1; i < gs.maxclients; i++, e++ ) {
		if( !e->r.inuse ) {
			continue;
		}

		int clientSnapFlags = e->r.client->r.snapHintFlags;
		const char *noWH = ( clientSnapFlags & SNAP_ANTIWALLHACK_FLAGS ) == SNAP_ANTIWALLHACK_FLAGS ? "x" : " ";
		const char *noRD = ( clientSnapFlags & SNAP_ANTIRADAR_FLAGS ) == SNAP_ANTIRADAR_FLAGS ? "x" : " ";
		Q_strncatz( msg, va( "%2d |  %s  |  %s  | %s\n", PLAYERNUM( e ), noWH, noRD, e->r.client->netname ), sizeof( msg ) );
	}

	G_PrintMsg( ent, "%s", msg );
}



static bool G_VoteSetAntiWallhackValidate( callvotedata_t *vote, bool first ) {
	return G_SetOrValidateKickLikeCmdTarget( vote, first );
}

static void G_VoteSetAntiWallhackPassed( callvotedata_t *vote ) {
	if( edict_t *ent = G_Vote_GetValidDeferredVoteTarget( vote ) ) {
		ent->r.client->r.snapHintFlags |= SNAP_ANTIWALLHACK_FLAGS;
	}
}

static bool G_VoteResetAntiWallhackValidate( callvotedata_t *vote, bool first ) {
	return G_SetOrValidateKickLikeCmdTarget( vote, first );
}

static void G_VoteResetAntiWallhackPassed( callvotedata_t *vote ) {
	if( edict_t *ent = G_Vote_GetValidDeferredVoteTarget( vote ) ) {
		ent->r.client->r.snapHintFlags &= ~SNAP_ANTIWALLHACK_FLAGS;
	}
}

static bool G_VoteSetAntiRadarValidate( callvotedata_t *vote, bool first ) {
	return G_SetOrValidateKickLikeCmdTarget( vote, first );
}

static void G_VoteSetAntiRadarPassed( callvotedata_t *vote ) {
	if( edict_t *ent = G_Vote_GetValidDeferredVoteTarget( vote ) ) {
		ent->r.client->r.snapHintFlags |= SNAP_ANTIRADAR_FLAGS;
	}
}

static bool G_VoteResetAntiRadarValidate( callvotedata_t *vote, bool first ) {
	return G_SetOrValidateKickLikeCmdTarget( vote, first );
}

static void G_VoteResetAntiRadarPassed( callvotedata_t *vote ) {
	if( edict_t *ent = G_Vote_GetValidDeferredVoteTarget( vote ) ) {
		ent->r.client->r.snapHintFlags &= ~SNAP_ANTIRADAR_FLAGS;
	}
}

static bool G_VoteSetAntiCheatValidate( callvotedata_t *vote, bool first ) {
	return G_SetOrValidateKickLikeCmdTarget( vote, first );
}

static void G_VoteSetAntiCheatPassed( callvotedata_t *vote ) {
	if( edict_t *ent = G_Vote_GetValidDeferredVoteTarget( vote ) ) {
		ent->r.client->r.snapHintFlags |= SNAP_ANTICHEAT_FLAGS;
	}
}

static bool G_VoteResetAntiCheatValidate( callvotedata_t *vote, bool first ) {
	return G_SetOrValidateKickLikeCmdTarget( vote, first );
}

static void G_VoteResetAntiCheatPassed( callvotedata_t *vote ) {
	if( edict_t *ent = G_Vote_GetValidDeferredVoteTarget( vote ) ) {
		ent->r.client->r.snapHintFlags &= ~SNAP_ANTICHEAT_FLAGS;
	}
}

// We have preferred to access the vars by name without specifying a storage class

static const char *ANTIWALLHACK_VAR_NAMES[] = {
	SNAP_VAR_CULL_SOUND_WITH_PVS, SNAP_VAR_SHADOW_EVENTS_DATA, nullptr
};

static const char *ANTIRADAR_VAR_NAMES[] = {
	SNAP_VAR_USE_VIEWDIR_CULLING, nullptr
};

static const char *ANTICHEAT_VAR_NAMES[] = {
	SNAP_VAR_CULL_SOUND_WITH_PVS, SNAP_VAR_SHADOW_EVENTS_DATA, SNAP_VAR_USE_VIEWDIR_CULLING, nullptr
};

static const int G_Vote_CurrentFromVars( const char **varNames ) {
	while( *varNames != nullptr ) {
		if( !trap_Cvar_Value( *varNames ) ) {
			return 0;
		}
		varNames++;
	}
	return 1;
}

static bool G_ValidateBooleanSwitchVote( int presentValue, const char *desc, callvotedata_t *vote, bool first );

static bool G_ValidateBooleanSwitchVote( const cvar_t *var, const char *desc, callvotedata_t *vote, bool first ) {
	return G_ValidateBooleanSwitchVote( var->integer, desc, vote, first );
}

static bool G_ValidateBooleanSwitchVote( int presentValue, const char *desc, callvotedata_t *vote, bool first ) {
	int value = atoi( vote->argv[0] );
	if( value != 0 && value != 1 ) {
		return false;
	}

	if( value && presentValue ) {
		if( first ) {
			G_PrintMsg( vote->caller, S_COLOR_RED "%s is already allowed\n", desc );
		}
		return false;
	}

	if( !value && !presentValue ) {
		if( first ) {
			G_PrintMsg( vote->caller, S_COLOR_RED "%s is already disabled\n", desc );
		}
		return false;
	}

	return true;
}

static const char *G_VoteEnableGlobalAntiWallhackCurrent() {
	return G_Vote_CurrentFromVars( ANTIWALLHACK_VAR_NAMES ) ? "1" : "0";
}

static bool G_VoteEnableGlobalAntiWallhackValidate( callvotedata_t *vote, bool first ) {
	int presentValue = G_Vote_CurrentFromVars( ANTIWALLHACK_VAR_NAMES );
	return G_ValidateBooleanSwitchVote( presentValue, "Global antiwallhack", vote, first );
}


static void G_ResetClientSnapFlags( int flagsToReset ) {
	for( int i = 1; i <= gs.maxclients; ++i ) {
		const edict_t *ent = game.edicts + i;
		if( !ent->r.client ) {
			continue;
		}
		ent->r.client->r.snapHintFlags &= ~flagsToReset;
	}
}

static void G_VoteEnableGlobalAntiWallhackPassed( callvotedata_t *vote ) {
	const char *valueToSet = atoi( vote->argv[0] ) ? "1" : "0";
	for( const char *name: ANTIWALLHACK_VAR_NAMES ) {
		if( name ) {
			trap_Cvar_ForceSet( name, valueToSet );
		}
	}

	// Always keep this var set, its very efficient at hacks mitigation and should not break gameplay.
	// Using other anti WH settings without this one makes little sense.
	// If we turn it off as all other vars in case of vote failure,
	// server settings would be compromised since this var is set by default.
	trap_Cvar_ForceSet( SNAP_VAR_USE_RAYCAST_CULLING, "1" );

	if( valueToSet[0] == '0' ) {
		// Reset client-specific flags as well
		// (they're override global ones and it's confusing if they would remain set)
		G_ResetClientSnapFlags( SNAP_ANTIWALLHACK_FLAGS );
	}
}

static const char *G_VoteEnableGlobalAntiRadarCurrent() {
	return G_Vote_CurrentFromVars( ANTIRADAR_VAR_NAMES ) ? "1" : "0";
}

static bool G_VoteEnableGlobalAntiRadarValidate( callvotedata_t *vote, bool first ) {
	int presentValue = G_Vote_CurrentFromVars( ANTIRADAR_VAR_NAMES );
	return G_ValidateBooleanSwitchVote( presentValue, "Global antiradar", vote, first );
}

static void G_VoteEnableGlobalAntiRadarPassed( callvotedata_t *vote ) {
	const char *valueToSet = atoi( vote->argv[0] ) ? "1" : "0";
	for( const char *name: ANTIRADAR_VAR_NAMES ) {
		if( name ) {
			trap_Cvar_ForceSet( name, valueToSet );
		}
	}

	if( valueToSet[0] == '0' ) {
		// Reset client-specific flags as well
		// (they're override global ones and it's confusing if they would remain set)
		G_ResetClientSnapFlags( SNAP_ANTIRADAR_FLAGS );
	}
}

static const char *G_VoteEnableGlobalAntiCheatCurrent() {
	return G_Vote_CurrentFromVars( ANTICHEAT_VAR_NAMES ) ? "1" : "0";
}

static bool G_VoteEnableGlobalAntiCheatValidate( callvotedata_t *vote, bool first ) {
	int presentValue = G_Vote_CurrentFromVars( ANTICHEAT_VAR_NAMES );
	return G_ValidateBooleanSwitchVote( presentValue, "Global anticheat", vote, first );
}

static void G_VoteEnableGlobalAntiCheatPassed( callvotedata_t *vote ) {
	const char *valueToSet = atoi( vote->argv[0] ) ? "1" : "0";
	for( const char *name: ANTICHEAT_VAR_NAMES ) {
		if( name ) {
			trap_Cvar_ForceSet( name, valueToSet );
		}
	}

	// See G_VoteEnableGlobalAntiWallhackPassed() for explanation
	trap_Cvar_ForceSet( SNAP_VAR_USE_RAYCAST_CULLING, "1" );

	if( valueToSet[0] == '0' ) {
		// Reset client-specific flags as well
		// (they're override global ones and it's confusing if they would remain set)
		G_ResetClientSnapFlags( SNAP_ANTICHEAT_FLAGS );
	}
}
/*
* addbots
*/

static bool G_VoteNumBotsValidate( callvotedata_t *vote, bool first ) {
	int numbots = atoi( vote->argv[0] );

	if( g_numbots->integer == numbots ) {
		if( first ) {
			G_PrintMsg( vote->caller, "%sNumber of bots is already %i\n", S_COLOR_RED, numbots );
		}
		return false;
	}

	if( numbots < 0 ) {
		if( first ) {
			G_PrintMsg( vote->caller, "%sNegative number of bots is not allowed\n", S_COLOR_RED );
		}
		return false;
	}

	if( numbots > gs.maxclients ) {
		if( first ) {
			G_PrintMsg( vote->caller, "%sNumber of bots can't be higher than the number of client spots (%i)\n",
						S_COLOR_RED, gs.maxclients );
		}
		return false;
	}

	return true;
}

static void G_VoteNumBotsPassed( callvotedata_t *vote ) {
	trap_Cvar_Set( "g_numbots", vote->argv[0] );
}

static const char *G_VoteNumBotsCurrent( void ) {
	return va( "%i", g_numbots->integer );
}

/*
* allow_teamdamage
*/

static bool G_VoteAllowTeamDamageValidate( callvotedata_t *vote, bool first ) {
	return G_ValidateBooleanSwitchVote( g_allow_teamdamage, "Team damage", vote, first );
}

static void G_VoteAllowTeamDamagePassed( callvotedata_t *vote ) {
	trap_Cvar_Set( "g_allow_teamdamage", va( "%i", atoi( vote->argv[0] ) ) );
}

static const char *G_VoteAllowTeamDamageCurrent( void ) {
	if( g_allow_teamdamage->integer ) {
		return "1";
	} else {
		return "0";
	}
}

/*
* instajump
*/

static bool G_VoteAllowInstajumpValidate( callvotedata_t *vote, bool first ) {
	return G_ValidateBooleanSwitchVote( g_instajump, "Instajump", vote, first );
}

static void G_VoteAllowInstajumpPassed( callvotedata_t *vote ) {
	trap_Cvar_Set( "g_instajump", va( "%i", atoi( vote->argv[0] ) ) );
}

static const char *G_VoteAllowInstajumpCurrent( void ) {
	if( g_instajump->integer ) {
		return "1";
	} else {
		return "0";
	}
}

/*
* instashield
*/

static bool G_VoteAllowInstashieldValidate( callvotedata_t *vote, bool first ) {
	return G_ValidateBooleanSwitchVote( g_instashield, "Instashield", vote, first );
}

static void G_VoteAllowInstashieldPassed( callvotedata_t *vote ) {
	trap_Cvar_Set( "g_instashield", va( "%i", atoi( vote->argv[0] ) ) );

	// remove the shield from all players
	if( !g_instashield->integer ) {
		int i;

		for( i = 0; i < gs.maxclients; i++ ) {
			if( trap_GetClientState( i ) < CS_SPAWNED ) {
				continue;
			}

			game.clients[i].ps.inventory[POWERUP_SHELL] = 0;
		}
	}
}

static const char *G_VoteAllowInstashieldCurrent( void ) {
	if( g_instashield->integer ) {
		return "1";
	} else {
		return "0";
	}
}

/*
* allow_falldamage
*/

static bool G_VoteAllowFallDamageValidate( callvotedata_t *vote, bool first ) {
	return G_ValidateBooleanSwitchVote( (int)GS_FallDamage(), "Fall damage", vote, first );
}

static void G_VoteAllowFallDamagePassed( callvotedata_t *vote ) {
	trap_Cvar_Set( "g_allow_falldamage", va( "%i", atoi( vote->argv[0] ) ) );
}

static const char *G_VoteAllowFallDamageCurrent( void ) {
	if( GS_FallDamage() ) {
		return "1";
	} else {
		return "0";
	}
}

/*
* allow_selfdamage
*/

static bool G_VoteAllowSelfDamageValidate( callvotedata_t *vote, bool first ) {
	return G_ValidateBooleanSwitchVote( (int)GS_SelfDamage(), "Self damage", vote, first );
}

static void G_VoteAllowSelfDamagePassed( callvotedata_t *vote ) {
	trap_Cvar_Set( "g_allow_selfdamage", va( "%i", atoi( vote->argv[0] ) ) );
}

static const char *G_VoteAllowSelfDamageCurrent( void ) {
	if( GS_SelfDamage() ) {
		return "1";
	} else {
		return "0";
	}
}

/*
* timeout
*/
static bool G_VoteTimeoutValidate( callvotedata_t *vote, bool first ) {
	if( GS_MatchPaused() && ( level.timeout.endtime - level.timeout.time ) >= 2 * TIMEIN_TIME ) {
		if( first ) {
			G_PrintMsg( vote->caller, "%sTimeout already in progress\n", S_COLOR_RED );
		}
		return false;
	}

	return true;
}

static void G_VoteTimeoutPassed( callvotedata_t *vote ) {
	if( !GS_MatchPaused() ) {
		G_AnnouncerSound( NULL, trap_SoundIndex( va( S_ANNOUNCER_TIMEOUT_TIMEOUT_1_to_2, ( rand() & 1 ) + 1 ) ), GS_MAX_TEAMS, true, NULL );
	}

	GS_GamestatSetFlag( GAMESTAT_FLAG_PAUSED, true );
	level.timeout.caller = 0;
	level.timeout.endtime = level.timeout.time + TIMEOUT_TIME + FRAMETIME;
}

/*
* timein
*/
static bool G_VoteTimeinValidate( callvotedata_t *vote, bool first ) {
	if( !GS_MatchPaused() ) {
		if( first ) {
			G_PrintMsg( vote->caller, "%sNo timeout in progress\n", S_COLOR_RED );
		}
		return false;
	}

	if( level.timeout.endtime - level.timeout.time <= 2 * TIMEIN_TIME ) {
		if( first ) {
			G_PrintMsg( vote->caller, "%sTimeout is about to end already\n", S_COLOR_RED );
		}
		return false;
	}

	return true;
}

static void G_VoteTimeinPassed( callvotedata_t *vote ) {
	G_AnnouncerSound( NULL, trap_SoundIndex( va( S_ANNOUNCER_TIMEOUT_TIMEIN_1_to_2, ( rand() & 1 ) + 1 ) ), GS_MAX_TEAMS, true, NULL );
	level.timeout.endtime = level.timeout.time + TIMEIN_TIME + FRAMETIME;
}

/*
* allow_uneven
*/
static bool G_VoteAllowUnevenValidate( callvotedata_t *vote, bool first ) {
	int allow_uneven = atoi( vote->argv[0] );

	if( allow_uneven != 0 && allow_uneven != 1 ) {
		return false;
	}

	if( allow_uneven && g_teams_allow_uneven->integer ) {
		if( first ) {
			G_PrintMsg( vote->caller, "%sUneven teams is already allowed.\n", S_COLOR_RED );
		}
		return false;
	}

	if( !allow_uneven && !g_teams_allow_uneven->integer ) {
		if( first ) {
			G_PrintMsg( vote->caller, "%sUneven teams is already disallowed\n", S_COLOR_RED );
		}
		return false;
	}

	return true;
}

static void G_VoteAllowUnevenPassed( callvotedata_t *vote ) {
	trap_Cvar_Set( "g_teams_allow_uneven", va( "%i", atoi( vote->argv[0] ) ) );
}

static const char *G_VoteAllowUnevenCurrent( void ) {
	if( g_teams_allow_uneven->integer ) {
		return "1";
	} else {
		return "0";
	}
}

/*
* Shuffle
*/
static void G_VoteShufflePassed( callvotedata_t *vote ) {
	int i;
	int p1, p2, inc;
	int team;
	int numplayers;
	weighted_player_t players[MAX_CLIENTS];

	numplayers = 0;
	for( team = TEAM_ALPHA; team < GS_MAX_TEAMS; team++ ) {
		if( !teamlist[team].numplayers ) {
			continue;
		}
		for( i = 0; i < teamlist[team].numplayers; i++ ) {
			players[numplayers].ent = teamlist[team].playerIndices[i];
			players[numplayers].weight = rand();
			numplayers++;
		}
	}

	if( !numplayers ) {
		return;
	}

	qsort( players, numplayers, sizeof( weighted_player_t ), ( int ( * )( const void *, const void * ) )G_VoteCompareWeightedPlayers );

	if( rand() & 1 ) {
		p1 = 0;
		p2 = numplayers - 1;
		inc = 1;
	} else {
		p1 = numplayers - 1;
		p2 = 0;
		inc = -1;
	}

	// put players into teams
	team = rand() % numplayers;
	for( i = p1; ; i += inc ) {
		edict_t *e = game.edicts + players[i].ent;
		int newteam = TEAM_ALPHA + team++ % ( GS_MAX_TEAMS - TEAM_ALPHA );

		if( e->s.team != newteam ) {
			G_Teams_SetTeam( e, newteam );
		}

		if( i == p2 ) {
			break;
		}
	}

	G_Gametype_ScoreEvent( NULL, "shuffle", "" );
}

static bool G_VoteShuffleValidate( callvotedata_t *vote, bool first ) {
	if( !GS_TeamBasedGametype() || level.gametype.maxPlayersPerTeam == 1 ) {
		if( first ) {
			G_PrintMsg( vote->caller, S_COLOR_RED "Shuffle only works in team-based game modes\n" );
		}
		return false;
	}

	return true;
}

/*
* Rebalance
*/
static void G_VoteRebalancePassed( callvotedata_t *vote ) {
	int i;
	int team;
	int lowest_team, lowest_score;
	int numplayers;
	weighted_player_t players[MAX_CLIENTS];

	numplayers = 0;
	lowest_team = GS_MAX_TEAMS;
	lowest_score = 999999;
	for( team = TEAM_ALPHA; team < GS_MAX_TEAMS; team++ ) {
		if( !teamlist[team].numplayers ) {
			continue;
		}

		if( teamlist[team].stats.score < lowest_score ) {
			lowest_team = team;
			lowest_score = teamlist[team].stats.score;
		}

		for( i = 0; i < teamlist[team].numplayers; i++ ) {
			int ent = teamlist[team].playerIndices[i];
			players[numplayers].ent = ent;
			players[numplayers].weight = game.edicts[ent].r.client->level.stats.score;
			numplayers++;
		}
	}

	if( !numplayers || lowest_team == GS_MAX_TEAMS ) {
		return;
	}

	qsort( players, numplayers, sizeof( weighted_player_t ), ( int ( * )( const void *, const void * ) )G_VoteCompareWeightedPlayers );

	// put players into teams
	// start with the lowest scoring team
	team = lowest_team - TEAM_ALPHA;
	for( i = 0; i < numplayers; i++ ) {
		edict_t *e = game.edicts + players[i].ent;
		int newteam = TEAM_ALPHA + team % ( GS_MAX_TEAMS - TEAM_ALPHA );

		if( e->s.team != newteam ) {
			G_Teams_SetTeam( e, newteam );
		}

		e->r.client->level.stats.Clear();

		if( i % 2 == 0 ) {
			team++;
		}
	}

	G_Gametype_ScoreEvent( NULL, "rebalance", "" );
}

static bool G_VoteRebalanceValidate( callvotedata_t *vote, bool first ) {
	if( !GS_TeamBasedGametype() || level.gametype.maxPlayersPerTeam == 1 ) {
		if( first ) {
			G_PrintMsg( vote->caller, S_COLOR_RED "Rebalance only works in team-based game modes\n" );
		}
		return false;
	}

	return true;
}

//================================================
//
//================================================


callvotetype_t *G_RegisterCallvote( const char *name ) {
	callvotetype_t *callvote;

	for( callvote = callvotesHeadNode; callvote != NULL; callvote = callvote->next ) {
		if( !Q_stricmp( callvote->name, name ) ) {
			return callvote;
		}
	}

	// create a new callvote
	callvote = ( callvotetype_t * )G_LevelMalloc( sizeof( callvotetype_t ) );
	memset( callvote, 0, sizeof( callvotetype_t ) );
	callvote->next = callvotesHeadNode;
	callvotesHeadNode = callvote;

	callvote->name = G_LevelCopyString( name );
	return callvote;
}

void G_FreeCallvotes( void ) {
	callvotetype_t *callvote;

	while( callvotesHeadNode ) {
		callvote = callvotesHeadNode->next;

		if( callvotesHeadNode->name ) {
			G_LevelFree( callvotesHeadNode->name );
		}
		if( callvotesHeadNode->argument_format ) {
			G_LevelFree( callvotesHeadNode->argument_format );
		}
		if( callvotesHeadNode->help ) {
			G_LevelFree( callvotesHeadNode->help );
		}

		G_LevelFree( callvotesHeadNode );
		callvotesHeadNode = callvote;
	}

	callvotesHeadNode = NULL;
}

//===================================================================
// Common functions
//===================================================================

/*
* G_CallVotes_ResetClient
*/
void G_CallVotes_ResetClient( int n ) {
	clientVoted[n] = VOTED_NOTHING;
	clientVoteChanges[n] = g_callvote_maxchanges->integer;
	if( clientVoteChanges[n] < 1 ) {
		clientVoteChanges[n] = 1;
	}
}

/*
* G_CallVotes_Reset
*/
void G_CallVotes_Reset( void ) {
	int i;

	if( callvoteState.vote.caller && callvoteState.vote.caller->r.client ) {
		callvoteState.vote.caller->r.client->level.callvote_when = game.realtime;
	}

	callvoteState.vote.callvote = NULL;
	for( i = 0; i < gs.maxclients; i++ )
		G_CallVotes_ResetClient( i );
	callvoteState.timeout = 0;

	callvoteState.vote.caller = NULL;
	if( callvoteState.vote.string ) {
		G_Free( callvoteState.vote.string );
	}
	if( callvoteState.vote.data ) {
		G_Free( callvoteState.vote.data );
	}
	for( i = 0; i < callvoteState.vote.argc; i++ ) {
		if( callvoteState.vote.argv[i] ) {
			G_Free( callvoteState.vote.argv[i] );
		}
	}

	trap_ConfigString( CS_ACTIVE_CALLVOTE, "" );
	trap_ConfigString( CS_ACTIVE_CALLVOTE_VOTES, "" );

	memset( &callvoteState, 0, sizeof( callvoteState ) );
}

/*
* G_CallVotes_PrintUsagesToPlayer
*/
static void G_CallVotes_PrintUsagesToPlayer( edict_t *ent ) {
	callvotetype_t *callvote;

	G_PrintMsg( ent, "Available votes:\n" );
	for( callvote = callvotesHeadNode; callvote != NULL; callvote = callvote->next ) {
		if( trap_Cvar_Value( va( "g_disable_vote_%s", callvote->name ) ) ) {
			continue;
		}

		if( callvote->argument_format ) {
			G_PrintMsg( ent, " %s %s\n", callvote->name, callvote->argument_format );
		} else {
			G_PrintMsg( ent, " %s\n", callvote->name );
		}
	}
}

/*
* G_CallVotes_PrintHelpToPlayer
*/
static void G_CallVotes_PrintHelpToPlayer( edict_t *ent, callvotetype_t *callvote ) {

	if( !callvote ) {
		return;
	}

	G_PrintMsg( ent, "Usage: %s %s\n%s%s%s\n", callvote->name,
				( callvote->argument_format ? callvote->argument_format : "" ),
				( callvote->current ? va( "Current: %s\n", callvote->current() ) : "" ),
				( callvote->help ? "- " : "" ), ( callvote->help ? callvote->help : "" ) );
	if( callvote->extraHelp != NULL ) {
		callvote->extraHelp( ent );
	}
}

/*
* G_CallVotes_ArgsToString
*/
static const char *G_CallVotes_ArgsToString( const callvotedata_t *vote ) {
	static char argstring[MAX_STRING_CHARS];
	int i;

	argstring[0] = 0;

	if( vote->argc > 0 ) {
		Q_strncatz( argstring, vote->argv[0], sizeof( argstring ) );
	}
	for( i = 1; i < vote->argc; i++ ) {
		Q_strncatz( argstring, " ", sizeof( argstring ) );
		Q_strncatz( argstring, vote->argv[i], sizeof( argstring ) );
	}

	return argstring;
}

/*
* G_CallVotes_Arguments
*/
static const char *G_CallVotes_Arguments( const callvotedata_t *vote ) {
	const char *arguments;
	if( vote->string ) {
		arguments = vote->string;
	} else {
		arguments = G_CallVotes_ArgsToString( vote );
	}
	return arguments;
}

/*
* G_CallVotes_String
*/
static const char *G_CallVotes_String( const callvotedata_t *vote ) {
	const char *arguments;
	static char string[MAX_CONFIGSTRING_CHARS];

	arguments = G_CallVotes_Arguments( vote );
	if( arguments[0] ) {
		Q_snprintfz( string, sizeof( string ), "%s %s", vote->callvote->name, arguments );
		return string;
	}
	return vote->callvote->name;
}

/*
* G_CallVotes_CheckState
*/
static void G_CallVotes_CheckState( void ) {
	edict_t *ent;
	int needvotes, yeses = 0, voters = 0, noes = 0;
	static int64_t warntimer;

	if( !callvoteState.vote.callvote ) {
		warntimer = 0;
		return;
	}

	if( callvoteState.vote.callvote->validate != NULL &&
		!callvoteState.vote.callvote->validate( &callvoteState.vote, false ) ) {
		// fixme: should be vote cancelled or something
		G_AnnouncerSound( NULL, trap_SoundIndex( va( S_ANNOUNCER_CALLVOTE_FAILED_1_to_2, ( rand() & 1 ) + 1 ) ), GS_MAX_TEAMS, true, NULL );
		G_PrintMsg( NULL, "Vote is no longer valid\nVote %s%s%s canceled\n", S_COLOR_YELLOW,
					G_CallVotes_String( &callvoteState.vote ), S_COLOR_WHITE );
		G_CallVotes_Reset();
		return;
	}

	//analize votation state
	for( ent = game.edicts + 1; PLAYERNUM( ent ) < gs.maxclients; ent++ ) {
		gclient_t *client = ent->r.client;

		if( !ent->r.inuse || trap_GetClientState( PLAYERNUM( ent ) ) < CS_SPAWNED ) {
			continue;
		}

		if( ( ent->r.svflags & SVF_FAKECLIENT ) || client->isTV ) {
			continue;
		}

		// ignore inactive players unless they have voted
		if( client->level.last_activity &&
			client->level.last_activity + ( g_inactivity_maxtime->value * 1000 ) < level.time &&
			clientVoted[PLAYERNUM( ent )] == VOTED_NOTHING ) {
			continue;
		}

		voters++;
		if( clientVoted[PLAYERNUM( ent )] == VOTED_YES ) {
			yeses++;
		} else if( clientVoted[PLAYERNUM( ent )] == VOTED_NO ) {
			noes++;
		}
	}

	// passed?
	needvotes = (int)( ( voters * g_callvote_electpercentage->value ) / 100 );
	if( yeses > needvotes || callvoteState.vote.operatorcall ) {
		G_AnnouncerSound( NULL, trap_SoundIndex( va( S_ANNOUNCER_CALLVOTE_PASSED_1_to_2, ( rand() & 1 ) + 1 ) ), GS_MAX_TEAMS, true, NULL );
		G_PrintMsg( NULL, "Vote %s%s%s passed\n", S_COLOR_YELLOW,
					G_CallVotes_String( &callvoteState.vote ), S_COLOR_WHITE );
		if( callvoteState.vote.callvote->execute != NULL ) {
			callvoteState.vote.callvote->execute( &callvoteState.vote );
		}
		G_CallVotes_Reset();
		return;
	}

	// failed?
	if( game.realtime > callvoteState.timeout || voters - noes <= needvotes ) { // no change to pass anymore
		G_AnnouncerSound( NULL, trap_SoundIndex( va( S_ANNOUNCER_CALLVOTE_FAILED_1_to_2, ( rand() & 1 ) + 1 ) ), GS_MAX_TEAMS, true, NULL );
		G_PrintMsg( NULL, "Vote %s%s%s failed\n", S_COLOR_YELLOW,
					G_CallVotes_String( &callvoteState.vote ), S_COLOR_WHITE );
		G_CallVotes_Reset();
		return;
	}

	if( warntimer < game.realtime ) {
		if( callvoteState.timeout - game.realtime <= 7500 && callvoteState.timeout - game.realtime > 2500 ) {
			G_AnnouncerSound( NULL, trap_SoundIndex( S_ANNOUNCER_CALLVOTE_VOTE_NOW ), GS_MAX_TEAMS, true, NULL );
		}
		G_PrintMsg( NULL, "Vote in progress: %s%s%s, %i voted yes, %i voted no. %i required\n", S_COLOR_YELLOW,
					G_CallVotes_String( &callvoteState.vote ), S_COLOR_WHITE, yeses, noes,
					needvotes + 1 );
		warntimer = game.realtime + 5 * 1000;
	}
}

/*
* G_CallVotes_CmdVote
*/
void G_CallVotes_CmdVote( edict_t *ent ) {
	const char *vote;
	int vote_id;

	if( !ent->r.client ) {
		return;
	}
	if( ( ent->r.svflags & SVF_FAKECLIENT ) || ent->r.client->isTV ) {
		return;
	}

	if( !callvoteState.vote.callvote ) {
		G_PrintMsg( ent, "%sThere's no vote in progress\n", S_COLOR_RED );
		return;
	}

	vote = trap_Cmd_Argv( 1 );
	if( !Q_stricmp( vote, "yes" ) ) {
		vote_id = VOTED_YES;
	} else if( !Q_stricmp( vote, "no" ) ) {
		vote_id = VOTED_NO;
	} else {
		G_PrintMsg( ent, "%sInvalid vote: %s%s%s. Use yes or no\n", S_COLOR_RED,
					S_COLOR_YELLOW, vote, S_COLOR_RED );
		return;
	}

	if( clientVoted[PLAYERNUM( ent )] == vote_id ) {
		G_PrintMsg( ent, "%sYou have already voted %s\n", S_COLOR_RED, vote );
		return;
	}

	if( clientVoteChanges[PLAYERNUM( ent )] == 0 ) {
		G_PrintMsg( ent, "%sYou cannot change your vote anymore\n", S_COLOR_RED );
		return;
	}

	clientVoted[PLAYERNUM( ent )] = vote_id;
	clientVoteChanges[PLAYERNUM( ent )]--;
	G_CallVotes_CheckState();
}

/*
* G_CallVotes_UpdateVotesConfigString
*
* For clients that have already votes, sets and encodes
* appropriate bits in the configstring.
*/
static void G_CallVotes_UpdateVotesConfigString( void ) {
#define NUM_VOTEINTS ( ( MAX_CLIENTS + 31 ) / 32 )
	int i, n;
	int votebits[NUM_VOTEINTS];
	char cs[MAX_CONFIGSTRING_CHARS + 1];

	memset( votebits, 0, sizeof( votebits ) );
	for( i = 0; i < gs.maxclients; i++ ) {
		votebits[i >> 5] |= clientVoteChanges[i] == 0 ? ( 1 << ( i & 31 ) ) : 0;
	}

	// find the last bitvector that isn't 0
	for( n = NUM_VOTEINTS; n > 0 && !votebits[n - 1]; n-- ) ;

	cs[0] = cs[1] = '\0';
	for( i = 0; i < n; i++ ) {
		Q_strncatz( cs, va( " %x", votebits[i] ), sizeof( cs ) );
	}
	cs[MAX_CONFIGSTRING_CHARS] = '\0';

	trap_ConfigString( CS_ACTIVE_CALLVOTE_VOTES, cs + 1 );
}

/*
* G_CallVotes_Think
*/
void G_CallVotes_Think( void ) {
	static int64_t callvotethinktimer = 0;

	if( !callvoteState.vote.callvote ) {
		callvotethinktimer = 0;
		return;
	}

	if( callvotethinktimer < game.realtime ) {
		G_CallVotes_UpdateVotesConfigString();

		G_CallVotes_CheckState();
		callvotethinktimer = game.realtime + 1000;
	}
}

/*
* G_CallVote
*/
static void G_CallVote( edict_t *ent, bool isopcall ) {
	int i;
	const char *votename;
	callvotetype_t *callvote;

	if( !isopcall && ent->s.team == TEAM_SPECTATOR && GS_InvidualGameType()
		&& GS_MatchState() == MATCH_STATE_PLAYTIME && !GS_MatchPaused() ) {
		int team, count;
		edict_t *e;

		for( count = 0, team = TEAM_ALPHA; team < GS_MAX_TEAMS; team++ ) {
			if( !teamlist[team].numplayers ) {
				continue;
			}

			for( i = 0; i < teamlist[team].numplayers; i++ ) {
				e = game.edicts + teamlist[team].playerIndices[i];
				if( e->r.inuse && e->r.svflags & SVF_FAKECLIENT ) {
					count++;
				}
			}
		}

		if( !count ) {
			G_PrintMsg( ent, "%sSpectators cannot start a vote while a match is in progress\n", S_COLOR_RED );
			return;
		}
	}

	if( !g_callvote_enabled->integer ) {
		G_PrintMsg( ent, "%sCallvoting is disabled on this server\n", S_COLOR_RED );
		return;
	}

	if( callvoteState.vote.callvote ) {
		G_PrintMsg( ent, "%sA vote is already in progress\n", S_COLOR_RED );
		return;
	}

	votename = trap_Cmd_Argv( 1 );
	if( !votename || !votename[0] ) {
		G_CallVotes_PrintUsagesToPlayer( ent );
		return;
	}

	if( strlen( votename ) > MAX_QPATH ) {
		G_PrintMsg( ent, "%sInvalid vote\n", S_COLOR_RED );
		G_CallVotes_PrintUsagesToPlayer( ent );
		return;
	}

	//find the actual callvote command
	for( callvote = callvotesHeadNode; callvote != NULL; callvote = callvote->next ) {
		if( callvote->name && !Q_stricmp( callvote->name, votename ) ) {
			break;
		}
	}

	//unrecognized callvote type
	if( callvote == NULL ) {
		G_PrintMsg( ent, "%sUnrecognized vote: %s\n", S_COLOR_RED, votename );
		G_CallVotes_PrintUsagesToPlayer( ent );
		return;
	}

	// wsw : pb : server admin can now disable a specific vote command (g_disable_vote_<vote name>)
	// check if vote is disabled
	if( !isopcall && trap_Cvar_Value( va( "g_disable_vote_%s", callvote->name ) ) ) {
		G_PrintMsg( ent, "%sCallvote %s is disabled on this server\n", S_COLOR_RED, callvote->name );
		return;
	}

	// allow a second cvar specific for opcall
	if( isopcall && trap_Cvar_Value( va( "g_disable_opcall_%s", callvote->name ) ) ) {
		G_PrintMsg( ent, "%sOpcall %s is disabled on this server\n", S_COLOR_RED, callvote->name );
		return;
	}

	if( !isopcall && ent->r.client->level.callvote_when &&
		( ent->r.client->level.callvote_when + g_callvote_cooldowntime->integer * 1000 > game.realtime ) ) {
		G_PrintMsg( ent, "%sYou can not call a vote right now\n", S_COLOR_RED );
		return;
	}

	//we got a valid type. Get the parameters if any
	if( callvote->expectedargs != trap_Cmd_Argc() - 2 ) {
		if( callvote->expectedargs != -1 &&
			( callvote->expectedargs != -2 || trap_Cmd_Argc() - 2 > 0 ) ) {
			// wrong number of parametres
			G_CallVotes_PrintHelpToPlayer( ent, callvote );
			return;
		}
	}

	callvoteState.vote.argc = trap_Cmd_Argc() - 2;
	for( i = 0; i < callvoteState.vote.argc; i++ )
		callvoteState.vote.argv[i] = G_CopyString( trap_Cmd_Argv( i + 2 ) );

	callvoteState.vote.callvote = callvote;
	callvoteState.vote.caller = ent;
	callvoteState.vote.operatorcall = isopcall;

	//validate if there's a validation func
	if( callvote->validate != NULL && !callvote->validate( &callvoteState.vote, true ) ) {
		G_CallVotes_PrintHelpToPlayer( ent, callvote );
		G_CallVotes_Reset(); // free the args
		return;
	}

	//we're done. Proceed launching the election
	for( i = 0; i < gs.maxclients; i++ )
		G_CallVotes_ResetClient( i );
	callvoteState.timeout = game.realtime + ( g_callvote_electtime->integer * 1000 );

	//caller is assumed to vote YES
	clientVoted[PLAYERNUM( ent )] = VOTED_YES;
	clientVoteChanges[PLAYERNUM( ent )]--;

	ent->r.client->level.callvote_when = callvoteState.timeout;

	trap_ConfigString( CS_ACTIVE_CALLVOTE, G_CallVotes_String( &callvoteState.vote ) );

	G_AnnouncerSound( NULL, trap_SoundIndex( va( S_ANNOUNCER_CALLVOTE_CALLED_1_to_2, ( rand() & 1 ) + 1 ) ), GS_MAX_TEAMS, true, NULL );

	G_PrintMsg( NULL, "%s" S_COLOR_WHITE " requested to vote " S_COLOR_YELLOW "%s\n",
				ent->r.client->netname, G_CallVotes_String( &callvoteState.vote ) );

	G_PrintMsg( NULL, "Press " S_COLOR_YELLOW "F1" S_COLOR_WHITE " to " S_COLOR_YELLOW "vote yes"
				S_COLOR_WHITE " or " S_COLOR_YELLOW "F2" S_COLOR_WHITE " to " S_COLOR_YELLOW "vote no"
				S_COLOR_WHITE ", or cast your vote using the " S_COLOR_YELLOW "in-game menu\n" );

	G_CallVotes_Think(); // make the first think
}

/*
* G_CallVote_Cmd
*/
void G_CallVote_Cmd( edict_t *ent ) {
	if( ( ent->r.svflags & SVF_FAKECLIENT ) || ent->r.client->isTV ) {
		return;
	}
	G_CallVote( ent, false );
}

/*
* G_OperatorVote_Cmd
*/
void G_OperatorVote_Cmd( edict_t *ent ) {
	edict_t *other;
	int forceVote;

	if( !ent->r.client ) {
		return;
	}
	if( ( ent->r.svflags & SVF_FAKECLIENT ) || ent->r.client->isTV ) {
		return;
	}

	if( !ent->r.client->isoperator ) {
		G_PrintMsg( ent, "You are not a game operator\n" );
		return;
	}

	if( !Q_stricmp( trap_Cmd_Argv( 1 ), "help" ) ) {
		G_PrintMsg( ent, "Opcall can be used with all callvotes and the following commands:\n" );
		G_PrintMsg( ent, "-help\n - passvote\n- cancelvote\n- putteam\n" );
		return;
	}

	if( !Q_stricmp( trap_Cmd_Argv( 1 ), "cancelvote" ) ) {
		forceVote = VOTED_NO;
	} else if( !Q_stricmp( trap_Cmd_Argv( 1 ), "passvote" ) ) {
		forceVote = VOTED_YES;
	} else {
		forceVote = VOTED_NOTHING;
	}

	if( forceVote != VOTED_NOTHING ) {
		if( !callvoteState.vote.callvote ) {
			G_PrintMsg( ent, "There's no callvote to cancel.\n" );
			return;
		}

		for( other = game.edicts + 1; PLAYERNUM( other ) < gs.maxclients; other++ ) {
			if( !other->r.inuse || trap_GetClientState( PLAYERNUM( other ) ) < CS_SPAWNED ) {
				continue;
			}
			if( ( other->r.svflags & SVF_FAKECLIENT ) || other->r.client->isTV ) {
				continue;
			}

			clientVoted[PLAYERNUM( other )] = forceVote;
		}

		G_PrintMsg( NULL, "Callvote has been %s by %s\n",
					forceVote == VOTED_NO ? "cancelled" : "passed", ent->r.client->netname );
		return;
	}

	if( !Q_stricmp( trap_Cmd_Argv( 1 ), "putteam" ) ) {
		char *splayer = trap_Cmd_Argv( 2 );
		char *steam = trap_Cmd_Argv( 3 );
		edict_t *playerEnt;
		int newTeam;

		if( !steam || !steam[0] || !splayer || !splayer[0] ) {
			G_PrintMsg( ent, "Usage 'putteam <player id > <team name>'.\n" );
			return;
		}

		if( ( newTeam = GS_Teams_TeamFromName( steam ) ) < 0 ) {
			G_PrintMsg( ent, "The team '%s' doesn't exist.\n", steam );
			return;
		}

		if( ( playerEnt = G_PlayerForText( splayer ) ) == NULL ) {
			G_PrintMsg( ent, "The player '%s' couldn't be found.\n", splayer );
			return;
		}

		if( playerEnt->s.team == newTeam ) {
			G_PrintMsg( ent, "The player '%s' is already in team '%s'.\n", playerEnt->r.client->netname, GS_TeamName( newTeam ) );
			return;
		}

		G_Teams_SetTeam( playerEnt, newTeam );
		G_PrintMsg( NULL, "%s was moved to team %s by %s.\n", playerEnt->r.client->netname, GS_TeamName( newTeam ), ent->r.client->netname );

		return;
	}

	if( !Q_stricmp( trap_Cmd_Argv( 1 ), "specstotv" ) ) {
		for( other = game.edicts + 1; PLAYERNUM( other ) < gs.maxclients; other++ ) {
			if( !other->r.inuse || trap_GetClientState( PLAYERNUM( other ) ) < CS_SPAWNED ) {
				continue;
			}
			if( other->r.client->isoperator ) {
				continue;
			}
			if( other->s.team != TEAM_SPECTATOR ) {
				continue;
			}
			G_MoveClientToTV( other );
		}
		return;
	}

	G_CallVote( ent, true );
}

/*
* G_VoteFromScriptValidate
*/
static bool G_VoteFromScriptValidate( callvotedata_t *vote, bool first ) {
	char argsString[MAX_STRING_CHARS];
	int i;

	if( !vote || !vote->callvote || !vote->caller ) {
		return false;
	}

	Q_snprintfz( argsString, MAX_STRING_CHARS, "\"%s\"", vote->callvote->name );
	for( i = 0; i < vote->argc; i++ ) {
		Q_strncatz( argsString, " ", MAX_STRING_CHARS );
		Q_strncatz( argsString, va( " \"%s\"", vote->argv[i] ), MAX_STRING_CHARS );
	}

	return GT_asCallGameCommand( vote->caller->r.client, "callvotevalidate", argsString, vote->argc + 1 );
}

/*
* G_VoteFromScriptPassed
*/
static void G_VoteFromScriptPassed( callvotedata_t *vote ) {
	char argsString[MAX_STRING_CHARS];
	int i;

	if( !vote || !vote->callvote || !vote->caller ) {
		return;
	}

	Q_snprintfz( argsString, MAX_STRING_CHARS, "\"%s\"", vote->callvote->name );
	for( i = 0; i < vote->argc; i++ ) {
		Q_strncatz( argsString, " ", MAX_STRING_CHARS );
		Q_strncatz( argsString, va( " \"%s\"", vote->argv[i] ), MAX_STRING_CHARS );
	}

	GT_asCallGameCommand( vote->caller->r.client, "callvotepassed", argsString, vote->argc + 1 );
}

/*
* G_RegisterGametypeScriptCallvote
*/
void G_RegisterGametypeScriptCallvote( const char *name, const char *usage, const char *type, const char *help ) {
	callvotetype_t *vote;

	if( !name ) {
		return;
	}

	vote = G_RegisterCallvote( name );
	vote->expectedargs = -1;
	vote->validate = G_VoteFromScriptValidate;
	vote->execute = G_VoteFromScriptPassed;
	vote->current = NULL;
	vote->extraHelp = NULL;
	vote->argument_format = usage ? G_LevelCopyString( usage ) : NULL;
	vote->argument_type = type ? G_LevelCopyString( type ) : NULL;
	vote->help = help ? G_LevelCopyString( va( "%s", help ) ) : NULL;
}

/*
* G_CallVotes_Init
*/
void G_CallVotes_Init( void ) {
	callvotetype_t *callvote;

	g_callvote_electpercentage =    trap_Cvar_Get( "g_vote_percent", "55", CVAR_ARCHIVE );
	g_callvote_electtime =      trap_Cvar_Get( "g_vote_electtime", "40", CVAR_ARCHIVE );
	g_callvote_enabled =        trap_Cvar_Get( "g_vote_allowed", "1", CVAR_ARCHIVE );
	g_callvote_maxchanges =     trap_Cvar_Get( "g_vote_maxchanges", "3", CVAR_ARCHIVE );
	g_callvote_cooldowntime =   trap_Cvar_Get( "g_vote_cooldowntime", "5", CVAR_ARCHIVE );

	// register all callvotes

	callvote = G_RegisterCallvote( "map" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteMapValidate;
	callvote->execute = G_VoteMapPassed;
	callvote->current = G_VoteMapCurrent;
	callvote->extraHelp = G_VoteMapExtraHelp;
	callvote->argument_format = G_LevelCopyString( "<name>" );
	callvote->argument_type = G_LevelCopyString( "option" );
	callvote->webRequest = G_VoteMapWebRequest;
	callvote->help = G_LevelCopyString( "Changes map" );

	callvote = G_RegisterCallvote( "restart" );
	callvote->expectedargs = 0;
	callvote->validate = NULL;
	callvote->execute = G_VoteRestartPassed;
	callvote->current = NULL;
	callvote->extraHelp = NULL;
	callvote->argument_format = NULL;
	callvote->argument_type = NULL;
	callvote->help = G_LevelCopyString( "Restarts current map" );

	callvote = G_RegisterCallvote( "nextmap" );
	callvote->expectedargs = 0;
	callvote->validate = NULL;
	callvote->execute = G_VoteNextMapPassed;
	callvote->current = NULL;
	callvote->extraHelp = NULL;
	callvote->argument_format = NULL;
	callvote->argument_type = NULL;
	callvote->help = G_LevelCopyString( "Jumps to the next map" );

	callvote = G_RegisterCallvote( "scorelimit" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteScorelimitValidate;
	callvote->execute = G_VoteScorelimitPassed;
	callvote->current = G_VoteScorelimitCurrent;
	callvote->extraHelp = NULL;
	callvote->argument_format = G_LevelCopyString( "<number>" );
	callvote->argument_type = G_LevelCopyString( "integer" );
	callvote->help = G_LevelCopyString( "Sets the number of frags or caps needed to win the match\nSpecify 0 to disable" );

	callvote = G_RegisterCallvote( "timelimit" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteTimelimitValidate;
	callvote->execute = G_VoteTimelimitPassed;
	callvote->current = G_VoteTimelimitCurrent;
	callvote->extraHelp = NULL;
	callvote->argument_format = G_LevelCopyString( "<minutes>" );
	callvote->argument_type = G_LevelCopyString( "integer" );
	callvote->help = G_LevelCopyString( "Sets number of minutes after which the match ends\nSpecify 0 to disable" );

	callvote = G_RegisterCallvote( "gametype" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteGametypeValidate;
	callvote->execute = G_VoteGametypePassed;
	callvote->current = G_VoteGametypeCurrent;
	callvote->extraHelp = G_VoteGametypeExtraHelp;
	callvote->argument_format = G_LevelCopyString( "<name>" );
	callvote->argument_type = G_LevelCopyString( "option" );
	callvote->webRequest = G_VoteGametypeWebRequest;
	callvote->help = G_LevelCopyString( "Changes the gametype" );

	callvote = G_RegisterCallvote( "warmup_timelimit" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteWarmupTimelimitValidate;
	callvote->execute = G_VoteWarmupTimelimitPassed;
	callvote->current = G_VoteWarmupTimelimitCurrent;
	callvote->extraHelp = NULL;
	callvote->argument_format = G_LevelCopyString( "<minutes>" );
	callvote->argument_type = G_LevelCopyString( "integer" );
	callvote->help = G_LevelCopyString( "Sets the number of minutes after which the warmup ends\nSpecify 0 to disable" );

	callvote = G_RegisterCallvote( "extended_time" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteExtendedTimeValidate;
	callvote->execute = G_VoteExtendedTimePassed;
	callvote->current = G_VoteExtendedTimeCurrent;
	callvote->extraHelp = NULL;
	callvote->argument_format = G_LevelCopyString( "<minutes>" );
	callvote->argument_type = G_LevelCopyString( "integer" );
	callvote->help = G_LevelCopyString( "Sets the length of the overtime\nSpecify 0 to enable sudden death mode" );

	callvote = G_RegisterCallvote( "maxteamplayers" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteMaxTeamplayersValidate;
	callvote->execute = G_VoteMaxTeamplayersPassed;
	callvote->current = G_VoteMaxTeamplayersCurrent;
	callvote->extraHelp = NULL;
	callvote->argument_format = G_LevelCopyString( "<number>" );
	callvote->argument_type = G_LevelCopyString( "integer" );
	callvote->help = G_LevelCopyString( "Sets the maximum number of players in one team" );

	callvote = G_RegisterCallvote( "lock" );
	callvote->expectedargs = 0;
	callvote->validate = G_VoteLockValidate;
	callvote->execute = G_VoteLockPassed;
	callvote->current = NULL;
	callvote->extraHelp = NULL;
	callvote->argument_format = NULL;
	callvote->argument_type = NULL;
	callvote->help = G_LevelCopyString( "Locks teams to disallow players joining in mid-game" );

	callvote = G_RegisterCallvote( "unlock" );
	callvote->expectedargs = 0;
	callvote->validate = G_VoteUnlockValidate;
	callvote->execute = G_VoteUnlockPassed;
	callvote->current = NULL;
	callvote->extraHelp = NULL;
	callvote->argument_format = NULL;
	callvote->argument_type = NULL;
	callvote->help = G_LevelCopyString( "Unlocks teams to allow players joining in mid-game" );

	callvote = G_RegisterCallvote( "allready" );
	callvote->expectedargs = 0;
	callvote->validate = G_VoteAllreadyValidate;
	callvote->execute = G_VoteAllreadyPassed;
	callvote->current = NULL;
	callvote->extraHelp = NULL;
	callvote->argument_format = NULL;
	callvote->argument_type = NULL;
	callvote->help = G_LevelCopyString( "Sets all players as ready so the match can start" );

	callvote = G_RegisterCallvote( "remove" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteRemoveValidate;
	callvote->execute = G_VoteRemovePassed;
	callvote->current = NULL;
	callvote->extraHelp = G_VoteRemoveExtraHelp;
	callvote->argument_format = G_LevelCopyString( "<player>" );
	callvote->argument_type = G_LevelCopyString( "option" );
	callvote->webRequest = G_PlayerlistWebRequest;
	callvote->help = G_LevelCopyString( "Forces player back to spectator mode" );

	callvote = G_RegisterCallvote( "kick" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteKickValidate;
	callvote->execute = G_VoteKickPassed;
	callvote->current = NULL;
	callvote->extraHelp = G_VoteHelp_ShowPlayersList;
	callvote->argument_format = G_LevelCopyString( "<player>" );
	callvote->argument_type = G_LevelCopyString( "option" );
	callvote->webRequest = G_PlayerlistWebRequest;
	callvote->help = G_LevelCopyString( "Removes player from the server" );

	callvote = G_RegisterCallvote( "kickban" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteKickBanValidate;
	callvote->execute = G_VoteKickBanPassed;
	callvote->current = NULL;
	callvote->extraHelp = G_VoteHelp_ShowPlayersList;
	callvote->argument_format = G_LevelCopyString( "<player>" );
	callvote->argument_type = G_LevelCopyString( "option" );
	callvote->webRequest = G_PlayerlistWebRequest;
	callvote->help = G_LevelCopyString( "Removes player from the server and bans his IP-address for 15 minutes" );

	callvote = G_RegisterCallvote( "mute" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteMuteValidate;
	callvote->execute = G_VoteMutePassed;
	callvote->current = NULL;
	callvote->extraHelp = G_VoteHelp_ShowPlayersList;
	callvote->argument_format = G_LevelCopyString( "<player>" );
	callvote->argument_type = G_LevelCopyString( "option" );
	callvote->webRequest = G_PlayerlistWebRequest;
	callvote->help = G_LevelCopyString( "Disallows chat messages from the muted player" );

	callvote = G_RegisterCallvote( "unmute" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteUnmuteValidate;
	callvote->execute = G_VoteUnmutePassed;
	callvote->current = NULL;
	callvote->extraHelp = G_VoteHelp_ShowPlayersList;
	callvote->argument_format = G_LevelCopyString( "<player>" );
	callvote->argument_type = G_LevelCopyString( "option" );
	callvote->webRequest = G_PlayerlistWebRequest;
	callvote->help = G_LevelCopyString( "Reallows chat messages from the unmuted player" );

	callvote = G_RegisterCallvote( "set_antiwallhack_for" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteSetAntiWallhackValidate;
	callvote->execute = G_VoteSetAntiWallhackPassed;
	callvote->current = NULL;
	callvote->extraHelp = G_VoteHelp_ShowPlayersListWithSnapFlags;
	callvote->argument_format = G_LevelCopyString( "<player>" );
	callvote->argument_type = G_LevelCopyString( "option" );
	callvote->webRequest = G_PlayerlistWebRequest;
	callvote->help = G_LevelCopyString( "Sends less information that can be used for a wall hack "
										"(but might be important for gameplay) to the player" );

	callvote = G_RegisterCallvote( "reset_antiwallhack_for" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteResetAntiWallhackValidate;
	callvote->execute = G_VoteResetAntiWallhackPassed;
	callvote->current = NULL;
	callvote->extraHelp = G_VoteHelp_ShowPlayersListWithSnapFlags;
	callvote->argument_format = G_LevelCopyString( "<player>" );
	callvote->argument_type = G_LevelCopyString( "option" );
	callvote->webRequest = G_PlayerlistWebRequest;
	callvote->help = G_LevelCopyString( "Restores default wallhack-prone information sent to a player" );

	callvote = G_RegisterCallvote( "set_antiradar_for" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteSetAntiRadarValidate;
	callvote->execute = G_VoteSetAntiRadarPassed;
	callvote->current = NULL;
	callvote->extraHelp = G_VoteHelp_ShowPlayersListWithSnapFlags;
	callvote->argument_format = G_LevelCopyString( "<player>" );
	callvote->argument_type = G_LevelCopyString( "option" );
	callvote->webRequest = G_PlayerlistWebRequest;
	callvote->help = G_LevelCopyString( "Sends less information that can be used for a radar hack "
										"(but might be important for gameplay) to the player" );

	callvote = G_RegisterCallvote( "reset_antiradar_for" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteResetAntiRadarValidate;
	callvote->execute = G_VoteResetAntiRadarPassed;
	callvote->current = NULL;
	callvote->extraHelp = G_VoteHelp_ShowPlayersListWithSnapFlags;
	callvote->argument_format = G_LevelCopyString( "<player>" );
	callvote->argument_type = G_LevelCopyString( "option" );
	callvote->webRequest = G_PlayerlistWebRequest;
	callvote->help = G_LevelCopyString( "Restores default radar-prone information sent to a player" );

	callvote = G_RegisterCallvote( "set_anticheat_for" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteSetAntiCheatValidate;
	callvote->execute = G_VoteSetAntiCheatPassed;
	callvote->current = NULL;
	callvote->extraHelp = G_VoteHelp_ShowPlayersListWithSnapFlags;
	callvote->argument_format = G_LevelCopyString( "<player>" );
	callvote->argument_type = G_LevelCopyString( "option" );
	callvote->webRequest = G_PlayerlistWebRequest;
	callvote->help = G_LevelCopyString( "Turns ON all currently implemented anticheat methods "
										"(that might affect gameplay) for a player" );

	callvote = G_RegisterCallvote( "reset_anticheat_for" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteResetAntiCheatValidate;
	callvote->execute = G_VoteResetAntiCheatPassed;
	callvote->current = NULL;
	callvote->extraHelp = G_VoteHelp_ShowPlayersListWithSnapFlags;
	callvote->argument_format = G_LevelCopyString( "<player>" );
	callvote->argument_type = G_LevelCopyString( "option" );
	callvote->webRequest = G_PlayerlistWebRequest;
	callvote->help = G_LevelCopyString( "Turns OFF all currently implemented anticheat methods "
										"(that might affect gameplay) for a player" );

	callvote = G_RegisterCallvote( "enable_global_antiwallhack" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteEnableGlobalAntiWallhackValidate;
	callvote->execute = G_VoteEnableGlobalAntiWallhackPassed;
	callvote->current = G_VoteEnableGlobalAntiWallhackCurrent;
	callvote->extraHelp = NULL;
	callvote->argument_format = G_LevelCopyString( "<number>" );
	callvote->argument_type = G_LevelCopyString( "integer" );
	callvote->help = G_LevelCopyString( "Toggles sending less information that can be used for a wall hack "
										"(but might be important for gameplay) for every player" );

	callvote = G_RegisterCallvote( "enable_global_antiradar" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteEnableGlobalAntiRadarValidate;
	callvote->execute = G_VoteEnableGlobalAntiRadarPassed;
	callvote->current = G_VoteEnableGlobalAntiRadarCurrent;
	callvote->extraHelp = NULL;
	callvote->argument_format = G_LevelCopyString( "<number>" );
	callvote->argument_type = G_LevelCopyString( "integer " );
	callvote->help = G_LevelCopyString( "Toggles sending less information that can be used for a radar hack "
										"(but might be important for gameplay) for every player" );

	callvote = G_RegisterCallvote( "enable_global_anticheat" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteEnableGlobalAntiCheatValidate;
	callvote->execute = G_VoteEnableGlobalAntiCheatPassed;
	callvote->current = G_VoteEnableGlobalAntiCheatCurrent;
	callvote->extraHelp = NULL;
	callvote->argument_format = G_LevelCopyString( "<number>" );
	callvote->argument_type = G_LevelCopyString( "integer " );
	callvote->help = G_LevelCopyString( "Toggles using all implemented anticheat methods "
										"(that might affect gameplay) for every player" );

	callvote = G_RegisterCallvote( "numbots" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteNumBotsValidate;
	callvote->execute = G_VoteNumBotsPassed;
	callvote->current = G_VoteNumBotsCurrent;
	callvote->extraHelp = NULL;
	callvote->argument_format = G_LevelCopyString( "<number>" );
	callvote->argument_type = G_LevelCopyString( "integer" );
	callvote->help = G_LevelCopyString( "Sets the number of bots to play on the server" );

	callvote = G_RegisterCallvote( "allow_teamdamage" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteAllowTeamDamageValidate;
	callvote->execute = G_VoteAllowTeamDamagePassed;
	callvote->current = G_VoteAllowTeamDamageCurrent;
	callvote->extraHelp = NULL;
	callvote->argument_format = G_LevelCopyString( "<1 or 0>" );
	callvote->argument_type = G_LevelCopyString( "bool" );
	callvote->help = G_LevelCopyString( "Toggles whether shooting teammates will do damage to them" );

	callvote = G_RegisterCallvote( "instajump" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteAllowInstajumpValidate;
	callvote->execute = G_VoteAllowInstajumpPassed;
	callvote->current = G_VoteAllowInstajumpCurrent;
	callvote->extraHelp = NULL;
	callvote->argument_format = G_LevelCopyString( "<1 or 0>" );
	callvote->argument_type = G_LevelCopyString( "bool" );
	callvote->help = G_LevelCopyString( "Toggles whether instagun can be used for weapon jumping" );

	callvote = G_RegisterCallvote( "instashield" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteAllowInstashieldValidate;
	callvote->execute = G_VoteAllowInstashieldPassed;
	callvote->current = G_VoteAllowInstashieldCurrent;
	callvote->extraHelp = NULL;
	callvote->argument_format = G_LevelCopyString( "<1 or 0>" );
	callvote->argument_type = G_LevelCopyString( "bool" );
	callvote->help = G_LevelCopyString( "Toggles the availability of instashield in instagib" );

	callvote = G_RegisterCallvote( "allow_falldamage" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteAllowFallDamageValidate;
	callvote->execute = G_VoteAllowFallDamagePassed;
	callvote->current = G_VoteAllowFallDamageCurrent;
	callvote->extraHelp = NULL;
	callvote->argument_format = G_LevelCopyString( "<1 or 0>" );
	callvote->argument_type = G_LevelCopyString( "bool" );
	callvote->help = G_LevelCopyString( "Toggles whether falling long distances deals damage" );

	callvote = G_RegisterCallvote( "allow_selfdamage" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteAllowSelfDamageValidate;
	callvote->execute = G_VoteAllowSelfDamagePassed;
	callvote->current = G_VoteAllowSelfDamageCurrent;
	callvote->extraHelp = NULL;
	callvote->argument_format = G_LevelCopyString( "<1 or 0>" );
	callvote->argument_type = G_LevelCopyString( "bool" );
	callvote->help = G_LevelCopyString( "Toggles whether weapon splashes can damage self" );

	callvote = G_RegisterCallvote( "timeout" );
	callvote->expectedargs = 0;
	callvote->validate = G_VoteTimeoutValidate;
	callvote->execute = G_VoteTimeoutPassed;
	callvote->current = NULL;
	callvote->extraHelp = NULL;
	callvote->argument_format = NULL;
	callvote->argument_type = NULL;
	callvote->help = G_LevelCopyString( "Pauses the game" );

	callvote = G_RegisterCallvote( "timein" );
	callvote->expectedargs = 0;
	callvote->validate = G_VoteTimeinValidate;
	callvote->execute = G_VoteTimeinPassed;
	callvote->current = NULL;
	callvote->extraHelp = NULL;
	callvote->argument_format = NULL;
	callvote->argument_type = NULL;
	callvote->help = G_LevelCopyString( "Resumes the game if in timeout" );

	callvote = G_RegisterCallvote( "allow_uneven" );
	callvote->expectedargs = 1;
	callvote->validate = G_VoteAllowUnevenValidate;
	callvote->execute = G_VoteAllowUnevenPassed;
	callvote->current = G_VoteAllowUnevenCurrent;
	callvote->extraHelp = NULL;
	callvote->argument_format = G_LevelCopyString( "<1 or 0>" );
	callvote->argument_type = G_LevelCopyString( "bool" );

	callvote = G_RegisterCallvote( "shuffle" );
	callvote->expectedargs = 0;
	callvote->validate = G_VoteShuffleValidate;
	callvote->execute = G_VoteShufflePassed;
	callvote->current = NULL;
	callvote->extraHelp = NULL;
	callvote->argument_format = NULL;
	callvote->argument_type = NULL;
	callvote->help = G_LevelCopyString( "Shuffles teams" );

	callvote = G_RegisterCallvote( "rebalance" );
	callvote->expectedargs = 0;
	callvote->validate = G_VoteRebalanceValidate;
	callvote->execute = G_VoteRebalancePassed;
	callvote->current = NULL;
	callvote->extraHelp = NULL;
	callvote->argument_format = NULL;
	callvote->argument_type = NULL;
	callvote->help = G_LevelCopyString( "Rebalances teams" );

	// wsw : pb : server admin can now disable a specific callvote command (g_disable_vote_<callvote name>)
	for( callvote = callvotesHeadNode; callvote != NULL; callvote = callvote->next ) {
		trap_Cvar_Get( va( "g_disable_vote_%s", callvote->name ), "0", CVAR_ARCHIVE );
	}

	G_CallVotes_Reset();
}

/*
* G_CallVotes_WebRequest
*/
http_response_code_t G_CallVotes_WebRequest( http_query_method_t method, const char *resource,
											 const char *query_string, char **content, size_t *content_length ) {
	char *msg = NULL;
	size_t msg_len = 0, msg_size = 0;
	callvotetype_t *callvote;

	if( method != HTTP_METHOD_GET && method != HTTP_METHOD_HEAD ) {
		return HTTP_RESP_BAD_REQUEST;
	}

	if( !Q_strnicmp( resource, "callvotes/", 10 ) ) {
		// print the list of callvotes
		for( callvote = callvotesHeadNode; callvote != NULL; callvote = callvote->next ) {
			if( trap_Cvar_Value( va( "g_disable_vote_%s", callvote->name ) ) ) {
				continue;
			}

			G_AppendString( &msg, va( "{\n"
									  "\"name\"" " " "\"%s\"" "\n"
									  "\"expected_args\"" " " "\"%i\"" "\n"
									  "\"argument_format\"" " " "\"%s\"" "\n"
									  "\"argument_type\"" " " "\"%s\"" "\n"
									  "\"help\"" " " "\"%s\"" "\n"
									  "}\n",
									  callvote->name,
									  callvote->expectedargs,
									  callvote->argument_format ? callvote->argument_format : "",
									  callvote->argument_type ? callvote->argument_type : "string",
									  callvote->help ? callvote->help : ""
									  ), &msg_len, &msg_size );
		}

		*content = msg;
		*content_length = msg_len;
		return HTTP_RESP_OK;
	} else if( !Q_strnicmp( resource, "callvote/", 9 ) ) {
		const char *votename = resource + 9;

		// print the list of available arguments
		for( callvote = callvotesHeadNode; callvote != NULL; callvote = callvote->next ) {
			if( Q_stricmp( callvote->name, votename ) ) {
				continue;
			}
			if( callvote->webRequest ) {
				return callvote->webRequest( method, resource, query_string, content, content_length );
			}
			break;
		}
	}
	return HTTP_RESP_NOT_FOUND;
}
