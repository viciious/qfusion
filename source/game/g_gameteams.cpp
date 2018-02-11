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


//==========================================================
//					Teams
//==========================================================

cvar_t *g_teams_maxplayers;
cvar_t *g_teams_allow_uneven;

/*
* G_Teams_Init
*/
void G_Teams_Init( void ) {
	edict_t *ent;

	// set the team names with default ones
	trap_ConfigString( CS_TEAM_SPECTATOR_NAME, GS_DefaultTeamName( TEAM_SPECTATOR ) );
	trap_ConfigString( CS_TEAM_PLAYERS_NAME, GS_DefaultTeamName( TEAM_PLAYERS ) );
	trap_ConfigString( CS_TEAM_ALPHA_NAME, GS_DefaultTeamName( TEAM_ALPHA ) );
	trap_ConfigString( CS_TEAM_BETA_NAME, GS_DefaultTeamName( TEAM_BETA ) );

	g_teams_maxplayers = trap_Cvar_Get( "g_teams_maxplayers", "0", CVAR_ARCHIVE );
	g_teams_allow_uneven = trap_Cvar_Get( "g_teams_allow_uneven", "1", CVAR_ARCHIVE );

	for( int i = 0, end = sizeof( teamlist ) / sizeof( *teamlist ); i < end; ++i ) {
		teamlist[i].Clear();
	}

	for( ent = game.edicts + 1; PLAYERNUM( ent ) < gs.maxclients; ent++ ) {
		if( ent->r.inuse ) {
			ent->r.client->teamstate.Reset();
			ent->r.client->resp.Reset();
			ent->s.team = ent->r.client->team = TEAM_SPECTATOR;
			G_GhostClient( ent );
			ent->movetype = MOVETYPE_NOCLIP; // allow freefly
			ent->r.client->teamstate.timeStamp = level.time;
			ent->r.client->resp.timeStamp = level.time;
			trap_GameCmd( ent, va( "qm %s", ent->r.client->level.quickMenuItems ) );
		}
	}

}

static int G_Teams_CompareMembers( const void *a, const void *b ) {
	edict_t *edict_a = game.edicts + *(int *)a;
	edict_t *edict_b = game.edicts + *(int *)b;
	int score_a = edict_a->r.client->level.stats.score;
	int score_b = edict_b->r.client->level.stats.score;
	int result = ( level.gametype.inverseScore ? -1 : 1 ) * ( score_b - score_a );
	if( !result ) {
		result = Q_stricmp( edict_a->r.client->netname, edict_b->r.client->netname );
	}
	if( !result ) {
		result = ENTNUM( edict_a ) - ENTNUM( edict_b );
	}
	return result;
}

/*
* G_Teams_UpdateMembersList
* It's better to count the list in detail once per fame, than
* creating a quick list each time we need it.
*/
void G_Teams_UpdateMembersList( void ) {
	edict_t *ent;
	int i, team;

	for( team = TEAM_SPECTATOR; team < GS_MAX_TEAMS; team++ ) {
		teamlist[team].numplayers = 0;
		teamlist[team].ping = 0;
		teamlist[team].has_coach = false;

		//create a temp list with the clients inside this team
		for( i = 0, ent = game.edicts + 1; i < gs.maxclients; i++, ent++ ) {
			if( !ent->r.client || ( trap_GetClientState( PLAYERNUM( ent ) ) < CS_CONNECTED ) ) {
				continue;
			}

			if( ent->s.team == team ) {
				teamlist[team].playerIndices[teamlist[team].numplayers++] = ENTNUM( ent );

				if( ent->r.client->teamstate.is_coach ) {
					teamlist[team].has_coach = true;
				}
			}
		}

		qsort( teamlist[team].playerIndices, teamlist[team].numplayers, sizeof( teamlist[team].playerIndices[0] ), G_Teams_CompareMembers );

		if( teamlist[team].numplayers ) {
			for( i = 0; i < teamlist[team].numplayers; i++ )
				teamlist[team].ping += game.edicts[teamlist[team].playerIndices[i]].r.client->r.ping;
			teamlist[team].ping /= teamlist[team].numplayers;
		}
	}
}

/*
* G_Teams_TeamIsLocked
*/
bool G_Teams_TeamIsLocked( int team ) {
	if( team > TEAM_SPECTATOR && team < GS_MAX_TEAMS ) {
		return teamlist[team].locked;
	} else {
		return false;
	}
}

/*
* G_Teams_LockTeam
*/
bool G_Teams_LockTeam( int team ) {
	if( team <= TEAM_SPECTATOR || team >= GS_MAX_TEAMS ) {
		return false;
	}

	if( !level.teamlock || teamlist[team].locked ) {
		return false;
	}

	teamlist[team].locked = true;
	return true;
}

/*
* G_Teams_UnLockTeam
*/
bool G_Teams_UnLockTeam( int team ) {
	if( team <= TEAM_SPECTATOR || team >= GS_MAX_TEAMS ) {
		return false;
	}

	if( !teamlist[team].locked ) {
		return false;
	}

	teamlist[team].locked = false;
	return true;
}

/*
* G_Teams_PlayerIsInvited
*/
static bool G_Teams_PlayerIsInvited( int team, edict_t *ent ) {
	int i;

	if( team < TEAM_PLAYERS || team >= GS_MAX_TEAMS ) {
		return false;
	}

	if( !ent->r.inuse || !ent->r.client ) {
		return false;
	}

	for( i = 0; i < MAX_CLIENTS && teamlist[team].invited[i]; i++ ) {
		if( teamlist[team].invited[i] == ENTNUM( ent ) ) {
			return true;
		}
	}

	return false;
}

/*
* G_Teams_InvitePlayer
*/
static void G_Teams_InvitePlayer( int team, edict_t *ent ) {
	int i;

	if( team < TEAM_PLAYERS || team >= GS_MAX_TEAMS ) {
		return;
	}

	if( !ent->r.inuse || !ent->r.client ) {
		return;
	}

	for( i = 0; i < MAX_CLIENTS && teamlist[team].invited[i]; i++ ) {
		if( teamlist[team].invited[i] == ENTNUM( ent ) ) {
			return;
		}
	}
	
	if( i == MAX_CLIENTS ) {
		return;
	}

	teamlist[team].invited[i] = ENTNUM( ent );
}

/*
* G_Teams_UnInvitePlayer
*/
void G_Teams_UnInvitePlayer( int team, edict_t *ent ) {
	int i;

	if( team < TEAM_PLAYERS || team >= GS_MAX_TEAMS ) {
		return;
	}

	if( !ent->r.inuse || !ent->r.client ) {
		return;
	}

	for( i = 0; i < MAX_CLIENTS && teamlist[team].invited[i]; i++ ) {
		if( teamlist[team].invited[i] == ENTNUM( ent ) ) {
			break;
		}
	}
	while( i + 1 < MAX_CLIENTS && teamlist[team].invited[i] ) {
		teamlist[team].invited[i] = teamlist[team].invited[i + 1];
		i++;
	}
	teamlist[team].invited[MAX_CLIENTS - 1] = 0;
}

/*
* G_Teams_RemoveInvites
* Removes all invites from all teams
*/
void G_Teams_RemoveInvites( void ) {
	int team;

	for( team = TEAM_PLAYERS; team < GS_MAX_TEAMS; team++ )
		teamlist[team].invited[0] = 0;
}

/*
* G_Teams_Invite_f
*/
void G_Teams_Invite_f( edict_t *ent ) {
	char *text;
	edict_t *toinvite;
	int team;

	if( ( !ent->r.inuse || !ent->r.client ) ) {
		return;
	}

	text = trap_Cmd_Argv( 1 );

	if( !text || !strlen( text ) ) {
		int i;
		edict_t *e;
		char msg[1024];

		msg[0] = 0;
		Q_strncatz( msg, "Usage: invite <player>\n", sizeof( msg ) );
		Q_strncatz( msg, "- List of current players:\n", sizeof( msg ) );

		for( i = 0, e = game.edicts + 1; i < gs.maxclients; i++, e++ ) {
			if( !e->r.inuse ) {
				continue;
			}

			Q_strncatz( msg, va( "%3i: %s\n", PLAYERNUM( e ), e->r.client->netname ), sizeof( msg ) );
		}

		G_PrintMsg( ent, "%s", msg );
		return;
	}

	team = ent->s.team;

	if( !G_Teams_TeamIsLocked( team ) ) {
		G_PrintMsg( ent, "Your team is not locked.\n" );
		return;
	}

	toinvite = G_PlayerForText( text );

	if( !toinvite ) {
		G_PrintMsg( ent, "No such player.\n" );
		return;
	}

	if( G_Teams_PlayerIsInvited( team, toinvite ) ) {
		G_PrintMsg( ent, "%s%s is already invited to your team.\n", toinvite->r.client->netname, S_COLOR_WHITE );
		return;
	}

	G_Teams_InvitePlayer( team, toinvite );

	G_PrintMsg( NULL, "%s%s invited %s%s to team %s%s.\n", ent->r.client->netname, S_COLOR_WHITE,
				toinvite->r.client->netname, S_COLOR_WHITE, GS_TeamName( team ), S_COLOR_WHITE );
}

/*
* G_Teams_SetTeam - sets clients team without any checking
*/
void G_Teams_SetTeam( edict_t *ent, int team ) {
	assert( ent && ent->r.inuse && ent->r.client );
	assert( team >= TEAM_SPECTATOR && team < GS_MAX_TEAMS );

	if( ent->r.client->team != TEAM_SPECTATOR && team != TEAM_SPECTATOR ) {
		// keep scores when switching between non-spectating teams
		int64_t timeStamp = ent->r.client->teamstate.timeStamp;
		ent->r.client->teamstate.Reset();
		ent->r.client->teamstate.timeStamp = timeStamp;
	} else {
		// if player was on a team, send partial report to matchmaker
		if( ent->r.client->team != TEAM_SPECTATOR && GS_MatchState() == MATCH_STATE_PLAYTIME ) {
			G_Printf( "Sending teamchange to MM, team %d to team %d\n", ent->r.client->team, team );
			G_AddPlayerReport( ent, false );

			// trap_MR_SendPartialReport();
		}

		// clear scores at changing team
		ent->r.client->level.stats.Clear();
		ent->r.client->teamstate.Reset();
		ent->r.client->teamstate.timeStamp = level.time;
	}

	ent->r.client->team = team;
	G_Teams_UnInvitePlayer( team, ent );

	G_ClientRespawn( ent, true ); // make ghost using G_ClientRespawn so team is updated at ghosting
	G_SpawnQueue_AddClient( ent );

	level.ready[PLAYERNUM( ent )] = false;

	G_Match_CheckReadys();
	G_UpdatePlayerMatchMsg( ent );

	if( ent->ai ) {
		AI_JoinedTeam( ent, team );
	}
}

enum
{
	ER_TEAM_OK,
	ER_TEAM_INVALID,
	ER_TEAM_FULL,
	ER_TEAM_LOCKED,
	ER_TEAM_MATCHSTATE,
	ER_TEAM_CHALLENGERS,
	ER_TEAM_UNEVEN
};

static bool G_Teams_CanKeepEvenTeam( int leaving, int joining ) {
	int max = 0;
	int min = gs.maxclients + 1;
	int numplayers;
	int i;

	for( i = TEAM_ALPHA; i < GS_MAX_TEAMS; i++ ) {
		numplayers = teamlist[i].numplayers;
		if( i == leaving ) {
			numplayers--;
		}
		if( i == joining ) {
			numplayers++;
		}

		if( max < numplayers ) {
			max = numplayers;
		}
		if( min > numplayers ) {
			min = numplayers;
		}
	}

	return teamlist[joining].numplayers + 1 == min || abs( max - min ) <= 1;
}

/*
* G_GameTypes_DenyJoinTeam
*/
static int G_GameTypes_DenyJoinTeam( edict_t *ent, int team ) {
	if( team < 0 || team >= GS_MAX_TEAMS ) {
		G_Printf( "WARNING: 'G_GameTypes_CanJoinTeam' parsing a unrecognized team value\n" );
		return ER_TEAM_INVALID;
	}

	if( team == TEAM_SPECTATOR ) {
		return ER_TEAM_OK;
	}

	if( GS_MatchState() > MATCH_STATE_PLAYTIME ) {
		return ER_TEAM_MATCHSTATE;
	}

	// waiting for chanllengers queue to be executed
	if( GS_HasChallengers() &&
		game.realtime < level.spawnedTimeStamp + (int64_t)( G_CHALLENGERS_MIN_JOINTEAM_MAPTIME + game.snapFrameTime ) ) {
		return ER_TEAM_CHALLENGERS;
	}

	// force eveyone to go through queue so things work on map change
	if( GS_HasChallengers() && !ent->r.client->queueTimeStamp ) {
		return ER_TEAM_CHALLENGERS;
	}

	if( GS_TeamBasedGametype() && ( team >= TEAM_ALPHA && team < GS_MAX_TEAMS ) ) {
		if( ent->r.svflags & SVF_FAKECLIENT && AI_GetType( ent->ai ) == AI_ISBOT ) {
			if( level.gametype.forceTeamBots != TEAM_SPECTATOR ) {
				return team == level.gametype.forceTeamBots ? ER_TEAM_OK : ER_TEAM_INVALID;
			}
		} else {
			if( level.gametype.forceTeamHumans != TEAM_SPECTATOR ) {
				return team == level.gametype.forceTeamHumans ? ER_TEAM_OK : ER_TEAM_INVALID;
			}
		}
	}

	//see if team is locked
	if( G_Teams_TeamIsLocked( team ) && !G_Teams_PlayerIsInvited( team, ent ) ) {
		return ER_TEAM_LOCKED;
	}

	if( GS_TeamBasedGametype() ) {
		if( team >= TEAM_ALPHA && team < GS_MAX_TEAMS ) {
			// see if team is full
			int count = teamlist[team].numplayers;

			if( ( count + 1 > level.gametype.maxPlayersPerTeam &&
				  level.gametype.maxPlayersPerTeam > 0 ) ||
				( count + 1 > g_teams_maxplayers->integer &&
				  g_teams_maxplayers->integer > 0 ) ) {
				return ER_TEAM_FULL;
			}

			if( !g_teams_allow_uneven->integer && !G_Teams_CanKeepEvenTeam( ent->s.team, team ) ) {
				return ER_TEAM_UNEVEN;
			}

			return ER_TEAM_OK;
		} else {
			return ER_TEAM_INVALID;
		}
	} else if( team == TEAM_PLAYERS ) {
		return ER_TEAM_OK;
	}

	return ER_TEAM_INVALID;
}

/*
* G_Teams_JoinTeam - checks that client can join the given team and then joins it
*/
bool G_Teams_JoinTeam( edict_t *ent, int team ) {
	int error;

	G_Teams_UpdateMembersList(); // make sure we have up-to-date data

	if( !ent->r.client ) {
		return false;
	}

	if( ( error = G_GameTypes_DenyJoinTeam( ent, team ) ) ) {
		if( error == ER_TEAM_INVALID ) {
			G_PrintMsg( ent, "Can't join %s in %s\n", GS_TeamName( team ),
						gs.gametypeName );
		} else if( error == ER_TEAM_CHALLENGERS ) {
			G_Teams_JoinChallengersQueue( ent );
		} else if( error == ER_TEAM_FULL ) {
			G_PrintMsg( ent, "Team %s is FULL\n", GS_TeamName( team ) );
			G_Teams_JoinChallengersQueue( ent );
		} else if( error == ER_TEAM_LOCKED ) {
			G_PrintMsg( ent, "Team %s is LOCKED\n", GS_TeamName( team ) );
			G_Teams_JoinChallengersQueue( ent );
		} else if( error == ER_TEAM_MATCHSTATE ) {
			G_PrintMsg( ent, "Can't join %s at this moment\n", GS_TeamName( team ) );
		} else if( error == ER_TEAM_UNEVEN ) {
			G_PrintMsg( ent, "Can't join %s because of uneven teams\n", GS_TeamName( team ) ); // FIXME: need more suitable message :P
			G_Teams_JoinChallengersQueue( ent );
		}
		return false;
	}

	//ok, can join, proceed
	G_Teams_SetTeam( ent, team );
	return true;
}

/*
* G_Teams_JoinAnyTeam - find us a team since we are too lazy to do ourselves
*/
bool G_Teams_JoinAnyTeam( edict_t *ent, bool silent ) {
	int best_numplayers = gs.maxclients + 1, best_score = 999999;
	int i, team = -1;
	bool wasinqueue = ( ent->r.client->queueTimeStamp != 0 );

	G_Teams_UpdateMembersList(); // make sure we have up-to-date data

	//depending on the gametype, of course
	if( !GS_TeamBasedGametype() ) {
		if( ent->s.team == TEAM_PLAYERS ) {
			if( !silent ) {
				G_PrintMsg( ent, "You are already in %s team\n", GS_TeamName( TEAM_PLAYERS ) );
			}
			return false;
		}
		if( G_Teams_JoinTeam( ent, TEAM_PLAYERS ) ) {
			if( !silent ) {
				G_PrintMsg( NULL, "%s%s joined the %s team.\n",
							ent->r.client->netname, S_COLOR_WHITE, GS_TeamName( ent->s.team ) );
			}
		}
		return true;

	} else {   //team based

		//find the available team with smaller player count or worse score
		for( i = TEAM_ALPHA; i < GS_MAX_TEAMS; i++ ) {
			if( G_GameTypes_DenyJoinTeam( ent, i ) ) {
				continue;
			}

			if( team == -1 || teamlist[i].numplayers < best_numplayers
				|| ( teamlist[i].numplayers == best_numplayers && teamlist[i].stats.score < best_score ) ) {
				best_numplayers = teamlist[i].numplayers;
				best_score = teamlist[i].stats.score;
				team = i;
			}
		}

		if( team == ent->s.team ) { // he is at the right team
			if( !silent ) {
				G_PrintMsg( ent, "%sCouldn't find a better team than team %s.\n",
							S_COLOR_WHITE, GS_TeamName( ent->s.team ) );
			}
			return false;
		}

		if( team != -1 ) {
			if( G_Teams_JoinTeam( ent, team ) ) {
				if( !silent ) {
					G_PrintMsg( NULL, "%s%s joined the %s team.\n",
								ent->r.client->netname, S_COLOR_WHITE, GS_TeamName( ent->s.team ) );
				}
				return true;
			}
		}
		if( GS_MatchState() <= MATCH_STATE_PLAYTIME && !silent ) {
			G_Teams_JoinChallengersQueue( ent );
		}
	}

	// don't print message if we joined the queue
	if( !silent && ( !GS_HasChallengers() || wasinqueue || !ent->r.client->queueTimeStamp ) ) {
		G_PrintMsg( ent, "You can't join the game now\n" );
	}

	return false;
}

/*
* G_Teams_Join_Cmd
*/
void G_Teams_Join_Cmd( edict_t *ent ) {
	char *t;
	int team;

	if( !ent->r.client || trap_GetClientState( PLAYERNUM( ent ) ) < CS_SPAWNED ) {
		return;
	}

	t = trap_Cmd_Argv( 1 );
	if( !t || *t == 0 ) {
		G_Teams_JoinAnyTeam( ent, false );
		return;
	}

	team = GS_Teams_TeamFromName( t );
	if( team != -1 ) {
		if( team == TEAM_SPECTATOR ) { // special handling for spectator team
			Cmd_Spec_f( ent );
			return;
		}
		if( team == ent->s.team ) {
			G_PrintMsg( ent, "You are already in %s team\n", GS_TeamName( team ) );
			return;
		}
		if( G_Teams_JoinTeam( ent, team ) ) {
			G_PrintMsg( NULL, "%s%s joined the %s%s team.\n", ent->r.client->netname, S_COLOR_WHITE,
						GS_TeamName( ent->s.team ), S_COLOR_WHITE );
			return;
		}
	} else {
		G_PrintMsg( ent, "No such team.\n" );
		return;
	}
}

//======================================================================
//
//TEAM TAB
//
//======================================================================

/*
* G_Teams_TDM_UpdateTeamInfoMessages
*/
void G_Teams_UpdateTeamInfoMessages( void ) {
	static int nexttime = 0;
	static char teammessage[MAX_STRING_CHARS];
	edict_t *ent, *e;
	size_t len;
	int i, j, team;
	char entry[MAX_TOKEN_CHARS];
	int locationTag;

	nexttime -= game.snapFrameTime;
	if( nexttime > 0 ) {
		return;
	}

	while( nexttime <= 0 )
		nexttime += 2000;

	// time for a new update

	for( team = TEAM_ALPHA; team < GS_MAX_TEAMS; team++ ) {
		*teammessage = 0;
		Q_snprintfz( teammessage, sizeof( teammessage ), "ti \"" );
		len = strlen( teammessage );

		// add our team info to the string
		for( i = 0; i < teamlist[team].numplayers; i++ ) {
			ent = game.edicts + teamlist[team].playerIndices[i];

			if( G_IsDead( ent ) ) { // don't show dead players
				continue;
			}

			if( ent->r.client->teamstate.is_coach ) { // don't show coachs
				continue;
			}

			// get location name
			locationTag = G_MapLocationTAGForOrigin( ent->s.origin );
			if( locationTag == -1 ) {
				continue;
			}

			*entry = 0;

			Q_snprintfz( entry, sizeof( entry ), "%i %i %i %i ", PLAYERNUM( ent ), locationTag, HEALTH_TO_INT( ent->health ), ARMOR_TO_INT( ent->r.client->resp.armor ) );

			if( MAX_STRING_CHARS - len > strlen( entry ) ) {
				Q_strncatz( teammessage, entry, sizeof( teammessage ) );
				len = strlen( teammessage );
			}
		}

		// add closing quote
		*entry = 0;
		Q_snprintfz( entry, sizeof( entry ), "\"" );
		if( MAX_STRING_CHARS - len > strlen( entry ) ) {
			Q_strncatz( teammessage, entry, sizeof( teammessage ) );
			len = strlen( teammessage );
		}

		for( i = 0; i < teamlist[team].numplayers; i++ ) {
			ent = game.edicts + teamlist[team].playerIndices[i];

			if( !ent->r.inuse || !ent->r.client ) {
				continue;
			}

			trap_GameCmd( ent, teammessage );

			// see if there are spectators chasing this player and send them the layout too
			for( j = 0; j < teamlist[TEAM_SPECTATOR].numplayers; j++ ) {
				e = game.edicts + teamlist[TEAM_SPECTATOR].playerIndices[j];

				if( !e->r.inuse || !e->r.client ) {
					continue;
				}

				if( e->r.client->resp.chase.active && e->r.client->resp.chase.target == ENTNUM( ent ) ) {
					trap_GameCmd( e, teammessage );
				}
			}
		}
	}
}


//======================================================================
//
// CHALLENGERS QUEUE
//
//======================================================================

static int G_Teams_ChallengersQueueCmp( const edict_t **pe1, const edict_t **pe2 ) {
	const edict_t *e1 = *pe1, *e2 = *pe2;

	if( e1->r.client->queueTimeStamp > e2->r.client->queueTimeStamp ) {
		return 1;
	}
	if( e2->r.client->queueTimeStamp > e1->r.client->queueTimeStamp ) {
		return -1;
	}
	return rand() & 1 ? -1 : 1;
}

/*
* G_Teams_ChallengersQueue
*
* Returns a NULL-terminated list of challengers or NULL if
* there are no challengers.
*/
edict_t **G_Teams_ChallengersQueue( void ) {
	int num_challengers = 0;
	static edict_t *challengers[MAX_CLIENTS + 1];
	edict_t *e;
	gclient_t *cl;

	// fill the challengers into array, then sort
	for( e = game.edicts + 1; PLAYERNUM( e ) < gs.maxclients; e++ ) {
		if( !e->r.inuse || !e->r.client || e->s.team != TEAM_SPECTATOR ) {
			continue;
		}
		if( trap_GetClientState( PLAYERNUM( e ) ) < CS_SPAWNED ) {
			continue;
		}

		cl = e->r.client;
		if( cl->connecting || !cl->queueTimeStamp ) {
			continue;
		}

		challengers[num_challengers++] = e;
	}

	if( !num_challengers ) {
		return NULL;
	}

	// NULL terminator
	challengers[num_challengers] = NULL;

	// sort challengers by the queue time in ascending order
	if( num_challengers > 1 ) {
		qsort( challengers, num_challengers, sizeof( *challengers ), ( int ( * )( const void *, const void * ) )G_Teams_ChallengersQueueCmp );
	}

	return challengers;
}

/*
* G_Teams_ExecuteChallengersQueue
*/
void G_Teams_ExecuteChallengersQueue( void ) {
	edict_t *ent;
	edict_t **challengers;
	bool restartmatch = false;

	// Medar fixme: this is only really makes sense, if playerlimit per team is one
	if( GS_MatchState() == MATCH_STATE_PLAYTIME ) {
		return;
	}

	if( !GS_HasChallengers() ) {
		return;
	}

	if( game.realtime < level.spawnedTimeStamp + G_CHALLENGERS_MIN_JOINTEAM_MAPTIME ) {
		static int time, lasttime;
		time = (int)( ( G_CHALLENGERS_MIN_JOINTEAM_MAPTIME - ( game.realtime - level.spawnedTimeStamp ) ) * 0.001 );
		if( lasttime && time == lasttime ) {
			return;
		}
		lasttime = time;
		if( lasttime ) {
			G_CenterPrintFormatMsg( NULL, 1, "Waiting... %s", va( "%i", lasttime ) );
		}
		return;
	}

	// pick players in join order and try to put them in the
	// game until we get the first refused one.
	challengers = G_Teams_ChallengersQueue();
	if( challengers ) {
		int i;

		for( i = 0; challengers[i]; i++ ) {
			ent = challengers[i];
			if( !G_Teams_JoinAnyTeam( ent, true ) ) {
				break;
			}

			// if we successfully execute the challengers queue during the countdown, revert to warmup
			if( GS_MatchState() == MATCH_STATE_COUNTDOWN ) {
				restartmatch = true;
			}
		}
	}

	if( restartmatch == true ) {
		G_Match_Autorecord_Cancel();
		G_Match_LaunchState( MATCH_STATE_WARMUP );
	}
}

/*
*
* G_Teams_BestScoreBelow
*/
static edict_t *G_Teams_BestScoreBelow( int maxscore ) {
	int team, i;
	edict_t *e, *best = NULL;
	int bestScore = -9999999;

	if( GS_TeamBasedGametype() ) {
		for( team = TEAM_ALPHA; team < GS_MAX_TEAMS; team++ ) {
			for( i = 0; i < teamlist[team].numplayers; i++ ) {
				e = game.edicts + teamlist[team].playerIndices[i];
				if( e->r.client->level.stats.score > bestScore &&
					e->r.client->level.stats.score <= maxscore
					&& !e->r.client->queueTimeStamp ) {
					bestScore = e->r.client->level.stats.score;
					best = e;
				}
			}
		}
	} else {
		for( i = 0; i < teamlist[TEAM_PLAYERS].numplayers; i++ ) {
			e = game.edicts + teamlist[TEAM_PLAYERS].playerIndices[i];
			if( e->r.client->level.stats.score > bestScore &&
				e->r.client->level.stats.score <= maxscore
				&& !e->r.client->queueTimeStamp ) {
				bestScore = e->r.client->level.stats.score;
				best = e;
			}
		}
	}

	return best;
}

/*
* G_Teams_AdvanceChallengersQueue
*/
void G_Teams_AdvanceChallengersQueue( void ) {
	int i, team, loserscount, winnerscount, playerscount = 0;
	int maxscore = 999999;
	edict_t *won, *e;
	int START_TEAM = TEAM_PLAYERS, END_TEAM = TEAM_PLAYERS + 1;

	if( !GS_HasChallengers() ) {
		return;
	}

	G_Teams_UpdateMembersList();

	if( GS_TeamBasedGametype() ) {
		START_TEAM = TEAM_ALPHA;
		END_TEAM = GS_MAX_TEAMS;
	}

	// assign new timestamps to all the players inside teams
	for( team = START_TEAM; team < END_TEAM; team++ ) {
		playerscount += teamlist[team].numplayers;
	}

	if( !playerscount ) {
		return;
	}

	loserscount = 0;
	if( playerscount > 1 ) {
		loserscount = (int)( playerscount / 2 );
	}
	winnerscount = playerscount - loserscount;

	// put everyone who just played out of the challengers queue
	for( team = START_TEAM; team < END_TEAM; team++ ) {
		for( i = 0; i < teamlist[team].numplayers; i++ ) {
			e = game.edicts + teamlist[team].playerIndices[i];
			e->r.client->queueTimeStamp = 0;
		}
	}

	if( !level.gametype.hasChallengersRoulette ) {
		// put (back) the best scoring players in first positions of challengers queue
		for( i = 0; i < winnerscount; i++ ) {
			won = G_Teams_BestScoreBelow( maxscore );
			if( won ) {
				maxscore = won->r.client->level.stats.score;
				won->r.client->queueTimeStamp = 1 + ( winnerscount - i ); // never have 2 players with the same timestamp
			}
		}
	}
}

/*
* G_Teams_LeaveChallengersQueue
*/
void G_Teams_LeaveChallengersQueue( edict_t *ent ) {
	if( !GS_HasChallengers() ) {
		ent->r.client->queueTimeStamp = 0;
		return;
	}

	if( ent->s.team != TEAM_SPECTATOR ) {
		return;
	}

	// exit the challengers queue
	if( ent->r.client->queueTimeStamp ) {
		ent->r.client->queueTimeStamp = 0;
		G_PrintMsg( ent, "%sYou left the challengers queue\n", S_COLOR_CYAN );
		G_UpdatePlayerMatchMsg( ent );
	}
}

/*
* G_Teams_JoinChallengersQueue
*/
void G_Teams_JoinChallengersQueue( edict_t *ent ) {
	int pos = 0;
	edict_t *e;

	if( !GS_HasChallengers() ) {
		ent->r.client->queueTimeStamp = 0;
		return;
	}

	if( ent->s.team != TEAM_SPECTATOR ) {
		return;
	}

	// enter the challengers queue
	if( !ent->r.client->queueTimeStamp ) {  // enter the line
		ent->r.client->queueTimeStamp = game.realtime;
		for( e = game.edicts + 1; PLAYERNUM( e ) < gs.maxclients; e++ ) {
			if( !e->r.inuse || !e->r.client || trap_GetClientState( PLAYERNUM( e ) ) < CS_SPAWNED ) {
				continue;
			}
			if( !e->r.client->queueTimeStamp || e->s.team != TEAM_SPECTATOR ) {
				continue;
			}

			// if there are other players with the same timestamp, increase ours
			if( e->r.client->queueTimeStamp >= ent->r.client->queueTimeStamp ) {
				ent->r.client->queueTimeStamp = e->r.client->queueTimeStamp + 1;
			}
			if( e->r.client->queueTimeStamp < ent->r.client->queueTimeStamp ) {
				pos++;
			}
		}

		G_PrintMsg( ent, "%sYou entered the challengers queue in position %i\n", S_COLOR_CYAN, pos + 1 );
		G_UpdatePlayerMatchMsg( ent );
	}
}

void G_InitChallengersQueue( void ) {
	int i;

	for( i = 0; i < gs.maxclients; i++ )
		game.clients[i].queueTimeStamp = 0;
}

//======================================================================
//
//TEAM COMMUNICATIONS
//
//======================================================================

static edict_t *point;
static vec3_t point_location;

static void UpdatePoint( edict_t *who ) {
	vec3_t angles, forward, diff;
	trace_t trace;
	edict_t *ent, *ent_best = NULL;
	int i, j;
	float value, value_best = 0.35f; // if nothing better is found, print nothing
	gclient_t *client = who->r.client;
	vec3_t boxpoints[8], viewpoint;

	AngleVectors( client->ps.viewangles, forward, NULL, NULL );
	VectorCopy( who->s.origin, viewpoint );
	viewpoint[2] += who->viewheight;

	for( i = 0; i < game.numentities; i++ ) {
		ent = game.edicts + i;

		if( !ent->r.inuse || !ent->s.modelindex || ent == who ) {
			continue;
		}
		if( G_ISGHOSTING( ent ) ) {
			continue;
		}
		if( ent->s.type != ET_PLAYER && ent->s.type != ET_ITEM ) {
			continue;
		}

		VectorSubtract( ent->s.origin, viewpoint, angles );
		VectorNormalize( angles );
		VectorSubtract( forward, angles, diff );
		for( j = 0; j < 3; j++ ) if( diff[j] < 0 ) {
				diff[j] = -diff[j];
			}
		value = VectorLengthFast( diff );

		if( value < value_best ) {
			BuildBoxPoints( boxpoints, ent->s.origin, ent->r.mins, ent->r.maxs );
			for( j = 0; j < 8; j++ ) {
				G_Trace( &trace, viewpoint, vec3_origin, vec3_origin, boxpoints[j], who, MASK_OPAQUE );
				if( trace.fraction == 1 ) {
					value_best = value;
					ent_best = ent;
					break;
				}
			}
		}
	}

	if( ent_best != NULL ) {
		point = ent_best;
		VectorCopy( ent_best->s.origin, point_location );
	} else {
		vec3_t dest;

		VectorMA( viewpoint, 8192, forward, dest );
		G_Trace( &trace, viewpoint, vec3_origin, vec3_origin, dest, who, MASK_OPAQUE );

		point = NULL;
		VectorCopy( trace.endpos, point_location );
	}
}

static void Say_Team_Location( edict_t *who, char *buf, int buflen, const char *current_color ) {
	G_MapLocationNameForTAG( G_MapLocationTAGForOrigin( who->s.origin ), buf, buflen );
	Q_strncatz( buf, current_color, buflen );
}

static void Say_Team_Armor( edict_t *who, char *buf, int buflen, const char *current_color ) {
	if( GS_Armor_TagForCount( who->r.client->resp.armor ) != ARMOR_NONE ) {
		Q_snprintfz( buf, buflen, "%s%i%s", GS_FindItemByTag( GS_Armor_TagForCount( who->r.client->resp.armor ) )->color,
					 ARMOR_TO_INT( who->r.client->resp.armor ), current_color );
	} else {
		Q_snprintfz( buf, buflen, "%s0%s", S_COLOR_GREEN, current_color );
	}
}

static void Say_Team_Health( edict_t *who, char *buf, int buflen, const char *current_color ) {
	int health = HEALTH_TO_INT( who->health );

	if( health <= 0 ) {
		Q_snprintfz( buf, buflen, "%s0%s", S_COLOR_RED, current_color );
	} else if( health <= 50 ) {
		Q_snprintfz( buf, buflen, "%s%i%s", S_COLOR_YELLOW, health, current_color );
	} else if( health <= 100 ) {
		Q_snprintfz( buf, buflen, "%s%i%s", S_COLOR_WHITE, health, current_color );
	} else {
		Q_snprintfz( buf, buflen, "%s%i%s", S_COLOR_GREEN, health, current_color );
	}
}

static void WeaponString( edict_t *who, int weapon, char *buf, int buflen, const char *current_color ) {
	int strong_ammo, weak_ammo;
	gs_weapon_definition_t *weapondef = GS_GetWeaponDef( weapon );

	Q_snprintfz( buf, buflen, "%s%s%s", ( GS_FindItemByTag( weapon )->color ? GS_FindItemByTag( weapon )->color : "" ),
				 GS_FindItemByTag( weapon )->shortname, current_color );

	strong_ammo = who->r.client->ps.inventory[weapondef->firedef.ammo_id];
	weak_ammo = who->r.client->ps.inventory[weapondef->firedef_weak.ammo_id];
	if( weapon == WEAP_GUNBLADE ) {
		Q_strncatz( buf, va( ":%i", strong_ammo ), buflen );
	} else if( strong_ammo > 0 ) {
		Q_strncatz( buf, va( ":%i/%i", strong_ammo, weak_ammo ), buflen );
	} else {
		Q_strncatz( buf, va( ":%i", weak_ammo ), buflen );
	}
}

static bool HasItem( edict_t *who, int item ) {
	return ( who->r.client && who->r.client->ps.inventory[item] );
}

static void Say_Team_Best_Weapons( edict_t *who, char *buf, int buflen, const char *current_color ) {
	char weapon_strings[2][20];
	int weap, printed = 0;

	for( weap = WEAP_TOTAL - 1; weap > WEAP_GUNBLADE; weap-- ) {
		// evil hack to make RL more important than PG
		if( weap == WEAP_PLASMAGUN ) {
			weap = WEAP_ROCKETLAUNCHER;
		} else if( weap == WEAP_ROCKETLAUNCHER ) {
			weap = WEAP_PLASMAGUN;
		}

		if( HasItem( who, weap ) ) {
			WeaponString( who, weap, weapon_strings[printed], sizeof( weapon_strings[printed] ), current_color );
			if( ++printed == 2 ) {
				break;
			}
		}

		if( weap == WEAP_PLASMAGUN ) {
			weap = WEAP_ROCKETLAUNCHER;
		} else if( weap == WEAP_ROCKETLAUNCHER ) {
			weap = WEAP_PLASMAGUN;
		}
	}

	if( printed == 2 ) {
		Q_snprintfz( buf, buflen, "%s%s %s%s", weapon_strings[1], current_color, weapon_strings[0], current_color );
	} else if( printed == 1 ) {
		Q_snprintfz( buf, buflen, "%s%s", weapon_strings[0], current_color );
	} else {
		WeaponString( who, WEAP_GUNBLADE, buf, buflen, current_color );
		Q_strncatz( buf, current_color, buflen );
	}
}

static void Say_Team_Current_Weapon( edict_t *who, char *buf, int buflen, const char *current_color ) {
	if( !who->s.weapon ) {
		buf[0] = 0;
		return;
	}

	WeaponString( who, who->s.weapon, buf, buflen, current_color );
	Q_strncatz( buf, current_color, buflen );
}

static void Say_Team_Point( edict_t *who, char *buf, int buflen, const char *current_color ) {
	if( !point ) {
		Q_snprintfz( buf, buflen, "nothing" );
		return;
	}

	if( point->s.type == ET_ITEM ) {
		gsitem_t *item = GS_FindItemByClassname( point->classname );
		if( item ) {
			Q_snprintfz( buf, buflen, "%s%s%s", ( item->color ? item->color : "" ), item->shortname, current_color );
		} else {
			Q_snprintfz( buf, buflen, "%s", point->classname );
		}
	} else {
		Q_snprintfz( buf, buflen, "%s%s", point->classname, current_color );
	}
}

static void Say_Team_Point_Location( edict_t *who, char *buf, int buflen, const char *current_color ) {
	G_MapLocationNameForTAG( G_MapLocationTAGForOrigin( point_location ), buf, buflen );
	Q_strncatz( buf, current_color, buflen );
}

static void Say_Team_Pickup( edict_t *who, char *buf, int buflen, const char *current_color ) {
	if( !who->r.client->teamstate.last_pickup ) {
		buf[0] = 0;
	} else {
		gsitem_t *item = GS_FindItemByClassname( who->r.client->teamstate.last_pickup->classname );
		if( item ) {
			Q_snprintfz( buf, buflen, "%s%s%s", ( item->color ? item->color : "" ), item->shortname, current_color );
		} else {
			buf[0] = 0;
		}
	}
}

static void Say_Team_Pickup_Location( edict_t *who, char *buf, int buflen, const char *current_color ) {
	if( !who->r.client->teamstate.last_pickup ) {
		buf[0] = 0;
	} else {
		G_MapLocationNameForTAG( G_MapLocationTAGForOrigin( point_location ), buf, buflen );
		Q_strncatz( buf, current_color, buflen );
	}
}

static void Say_Team_Drop( edict_t *who, char *buf, int buflen, const char *current_color ) {
	const gsitem_t *item = who->r.client->teamstate.last_drop_item;

	if( !item ) {
		buf[0] = 0;
	} else {
		Q_snprintfz( buf, buflen, "%s%s%s", ( item->color ? item->color : "" ), item->shortname, current_color );
	}
}

static void Say_Team_Drop_Location( edict_t *who, char *buf, int buflen, const char *current_color ) {
	if( !who->r.client->teamstate.last_drop_item ) {
		buf[0] = 0;
	} else {
		G_MapLocationNameForTAG( G_MapLocationTAGForOrigin( who->r.client->teamstate.last_drop_location ), buf, buflen );
		Q_strncatz( buf, current_color, buflen );
	}
}

void G_Say_Team( edict_t *who, const char *inmsg, bool checkflood ) {
	char *msg;
	char msgbuf[256];
	char outmsg[256];
	char *p;
	char current_color[3];

	if( who->s.team != TEAM_SPECTATOR && ( !GS_TeamBasedGametype() || GS_InvidualGameType() ) ) {
		Cmd_Say_f( who, false, true );
		return;
	}

	if( checkflood ) {
		if( CheckFlood( who, true ) ) {
			return;
		}
	}

	Q_strncpyz( msgbuf, inmsg, sizeof( msgbuf ) );

	msg = msgbuf;
	if( *msg == '\"' ) {
		msg[strlen( msg ) - 1] = 0;
		msg++;
	}

	if( who->s.team == TEAM_SPECTATOR ) {
		// if speccing, also check for non-team flood
		if( checkflood ) {
			if( CheckFlood( who, false ) ) {
				return;
			}
		}

		G_ChatMsg( NULL, who, true, "%s", msg );
		return;
	}

#ifdef AUTHED_SAY
	if( sv_mm_enable->integer && who->r.client && who->r.client->mm_session <= 0 ) {
		// unauthed players are only allowed to chat to public at non play-time
		// they are allowed to team-chat at any time
		if( GS_MatchState() == MATCH_STATE_PLAYTIME ) {
			G_PrintMsg( who, "%s", S_COLOR_YELLOW "You must authenticate to be able to chat with other players during the match.\n" );
			return;
		}
	}
#endif

	Q_strncpyz( current_color, S_COLOR_WHITE, sizeof( current_color ) );

	memset( outmsg, 0, sizeof( outmsg ) );

	UpdatePoint( who );

	for( p = outmsg; *msg && (size_t)( p - outmsg ) < sizeof( outmsg ) - 3; msg++ ) {
		if( *msg == '%' ) {
			char buf[256];
			buf[0] = 0;
			switch( *++msg ) {
				case 'l':
					Say_Team_Location( who, buf, sizeof( buf ), current_color );
					break;
				case 'a':
					Say_Team_Armor( who, buf, sizeof( buf ), current_color );
					break;
				case 'h':
					Say_Team_Health( who, buf, sizeof( buf ), current_color );
					break;
				case 'b':
					Say_Team_Best_Weapons( who, buf, sizeof( buf ), current_color );
					break;
				case 'w':
					Say_Team_Current_Weapon( who, buf, sizeof( buf ), current_color );
					break;
				case 'x':
					Say_Team_Point( who, buf, sizeof( buf ), current_color );
					break;
				case 'y':
					Say_Team_Point_Location( who, buf, sizeof( buf ), current_color );
					break;
				case 'X':
					Say_Team_Pickup( who, buf, sizeof( buf ), current_color );
					break;
				case 'Y':
					Say_Team_Pickup_Location( who, buf, sizeof( buf ), current_color );
					break;
				case 'd':
					Say_Team_Drop( who, buf, sizeof( buf ), current_color );
					break;
				case 'D':
					Say_Team_Drop_Location( who, buf, sizeof( buf ), current_color );
					break;
				case '%':
					*p++ = *msg;
					break;
				default:

					// Maybe add a warning here?
					*p++ = '%';
					*p++ = *msg;
					break;
			}
			if( strlen( buf ) + ( p - outmsg ) < sizeof( outmsg ) - 3 ) {
				Q_strncatz( outmsg, buf, sizeof( outmsg ) );
				p += strlen( buf );
			}
		} else if( *msg == '^' ) {
			*p++ = *msg++;
			*p++ = *msg;
			Q_strncpyz( current_color, p - 2, sizeof( current_color ) );
		} else {
			*p++ = *msg;
		}
	}
	*p = 0;

	G_ChatMsg( NULL, who, true, "%s", outmsg );
}

// coach

void G_Teams_Coach( edict_t *ent ) {
	if( GS_TeamBasedGametype() && !GS_InvidualGameType() && ent->s.team != TEAM_SPECTATOR ) {
		if( !teamlist[ent->s.team].has_coach ) {
			if( GS_MatchState() > MATCH_STATE_WARMUP && !GS_MatchPaused() ) {
				G_PrintMsg( ent, "Can't set coach mode with the match in progress\n" );
			} else {
				// move to coach mode
				ent->r.client->teamstate.is_coach = true;
				G_GhostClient( ent );
				ent->health = ent->max_health;
				ent->deadflag = DEAD_NO;

				G_ChasePlayer( ent, NULL, true, 0 );

				//clear up his scores
				G_Match_Ready( ent ); // set ready and check readys
				ent->r.client->level.stats.Clear();

				teamlist[ent->s.team].has_coach = true;
				G_PrintMsg( NULL, "%s%s is now team %s coach \n", ent->r.client->netname,
							S_COLOR_WHITE, GS_TeamName( ent->s.team ) );
			}
		} else if( ent->r.client->teamstate.is_coach ) {   // if you are this team coach, resign
			ent->r.client->teamstate.is_coach = false;
			G_PrintMsg( NULL, "%s%s is no longer team %s coach \n", ent->r.client->netname,
						S_COLOR_WHITE, GS_TeamName( ent->s.team ) );

			G_Teams_SetTeam( ent, ent->s.team );
		} else {
			G_PrintMsg( ent, "Your team already has a coach.\n" );
		}
	} else {
		G_PrintMsg( ent, "Coaching only valid while on a team in Team based Gametypes.\n" );
	}
}

void G_Teams_CoachLockTeam( edict_t *ent ) {
	if( ent->r.client->teamstate.is_coach ) {
		if( !G_Teams_TeamIsLocked( ent->s.team ) ) {
			G_Teams_LockTeam( ent->s.team );
			G_PrintMsg( NULL, "%s%s locked the %s team.\n", ent->r.client->netname,
						S_COLOR_WHITE, GS_TeamName( ent->s.team ) );
		}
	}
}

void G_Teams_CoachUnLockTeam( edict_t *ent ) {
	if( ent->r.client->teamstate.is_coach ) {
		if( G_Teams_TeamIsLocked( ent->s.team ) ) {
			G_Teams_UnLockTeam( ent->s.team );
			G_PrintMsg( NULL, "%s%s unlocked the %s team.\n", ent->r.client->netname,
						S_COLOR_WHITE, GS_TeamName( ent->s.team ) );
		}
	}
}
