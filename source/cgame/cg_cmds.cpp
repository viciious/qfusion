/*
Copyright (C) 2002-2003 Victor Luchits

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

#include "cg_local.h"

/*
==========================================================================

SERVER COMMANDS

==========================================================================
*/

/*
* CG_SC_Print
*/
static void CG_SC_Print( void ) {
	CG_LocalPrint( "%s", trap_Cmd_Argv( 1 ) );
}

/*
* CG_SC_ChatPrint
*/
static void CG_SC_ChatPrint( void ) {
	const bool teamonly = ( !Q_stricmp( trap_Cmd_Argv( 0 ), "tch" ) ? true : false );
	const int who = atoi( trap_Cmd_Argv( 1 ) );
	const char *name = ( who && who == bound( 1, who, MAX_CLIENTS ) ? cgs.clientInfo[who - 1].name : NULL );
	const char *text = trap_Cmd_Argv( 2 );
	const cvar_t *filter = cg_chatFilter;

	if( filter->integer & ( teamonly ? 2 : 1 ) ) {
		return;
	}

	if( !name ) {
		CG_LocalPrint( S_COLOR_GREEN "console: %s\n", text );
	} else if( teamonly ) {
		CG_LocalPrint( S_COLOR_YELLOW "[%s]" S_COLOR_WHITE "%s" S_COLOR_YELLOW ": %s\n",
					   cg.frame.playerState.stats[STAT_REALTEAM] == TEAM_SPECTATOR ? "SPEC" : "TEAM", name, text );
	} else {
		CG_LocalPrint( "%s" S_COLOR_GREEN ": %s\n", name, text );
	}

	if( cg_chatBeep->integer ) {
		trap_S_StartLocalSound( CG_MediaSfx( cgs.media.sfxChat ), 1.0f );
	}
}

/*
* CG_SC_TVChatPrint
*/
static void CG_SC_TVChatPrint( void ) {
	const char *name = trap_Cmd_Argv( 1 );
	const char *text = trap_Cmd_Argv( 2 );
	const cvar_t *filter = cg_chatFilter;

	if( filter->integer & 4 ) {
		return;
	}

	CG_LocalPrint( S_COLOR_RED "[TV]" S_COLOR_WHITE "%s" S_COLOR_GREEN ": %s", name, text );
	if( cg_chatBeep->integer ) {
		trap_S_StartLocalSound( CG_MediaSfx( cgs.media.sfxChat ), 1.0f );
	}
}

/*
* CG_SC_CenterPrint
*/
static void CG_SC_CenterPrint( void ) {
	CG_CenterPrint( trap_Cmd_Argv( 1 ) );
}

/*
* CG_SC_CenterPrintFormat
*/
static void CG_SC_CenterPrintFormat( void ) {
	if( trap_Cmd_Argc() == 8 ) {
		CG_CenterPrint( va( CG_TranslateString( trap_Cmd_Argv( 1 ) ), trap_Cmd_Argv( 2 ), trap_Cmd_Argv( 3 ),
							trap_Cmd_Argv( 4 ), trap_Cmd_Argv( 5 ), trap_Cmd_Argv( 6 ), trap_Cmd_Argv( 7 ) ) );
	} else if( trap_Cmd_Argc() == 7 ) {
		CG_CenterPrint( va( CG_TranslateString( trap_Cmd_Argv( 1 ) ), trap_Cmd_Argv( 2 ), trap_Cmd_Argv( 3 ),
							trap_Cmd_Argv( 4 ), trap_Cmd_Argv( 5 ), trap_Cmd_Argv( 6 ) ) );
	} else if( trap_Cmd_Argc() == 6 ) {
		CG_CenterPrint( va( CG_TranslateString( trap_Cmd_Argv( 1 ) ), trap_Cmd_Argv( 2 ), trap_Cmd_Argv( 3 ),
							trap_Cmd_Argv( 4 ), trap_Cmd_Argv( 5 ) ) );
	} else if( trap_Cmd_Argc() == 5 ) {
		CG_CenterPrint( va( CG_TranslateString( trap_Cmd_Argv( 1 ) ), trap_Cmd_Argv( 2 ), trap_Cmd_Argv( 3 ),
							trap_Cmd_Argv( 4 ) ) );
	} else if( trap_Cmd_Argc() == 4 ) {
		CG_CenterPrint( va( CG_TranslateString( trap_Cmd_Argv( 1 ) ), trap_Cmd_Argv( 2 ), trap_Cmd_Argv( 3 ) ) );
	} else if( trap_Cmd_Argc() == 3 ) {
		CG_CenterPrint( va( CG_TranslateString( trap_Cmd_Argv( 1 ) ), trap_Cmd_Argv( 2 ) ) );
	} else if( trap_Cmd_Argc() == 2 ) {
		CG_CenterPrint( CG_TranslateString( trap_Cmd_Argv( 1 ) ) ); // theoretically, shouldn't happen
	}
}

/*
* CG_ConfigString
*/
void CG_ConfigString( int i, const char *s ) {
	size_t len;

	// wsw : jal : warn if configstring overflow
	len = strlen( s );
	if( len >= MAX_CONFIGSTRING_CHARS ) {
		CG_Printf( "%sWARNING:%s Configstring %i overflowed\n", S_COLOR_YELLOW, S_COLOR_WHITE, i );
	}

	if( i < 0 || i >= MAX_CONFIGSTRINGS ) {
		CG_Error( "configstring > MAX_CONFIGSTRINGS" );
		return;
	}

	Q_strncpyz( cgs.configStrings[i], s, sizeof( cgs.configStrings[i] ) );

	// do something apropriate
	if( i == CS_MAPNAME ) {
		CG_RegisterLevelMinimap();
	} else if( i == CS_GAMETYPETITLE ) {
	} else if( i == CS_GAMETYPENAME ) {
		GS_SetGametypeName( cgs.configStrings[CS_GAMETYPENAME] );
	} else if( i == CS_AUTORECORDSTATE ) {
		CG_SC_AutoRecordAction( cgs.configStrings[i] );
	} else if( i >= CS_MODELS && i < CS_MODELS + MAX_MODELS ) {
		if( cgs.configStrings[i][0] == '$' ) {  // indexed pmodel
			cgs.pModelsIndex[i - CS_MODELS] = CG_RegisterPlayerModel( cgs.configStrings[i] + 1 );
		} else {
			cgs.modelDraw[i - CS_MODELS] = CG_RegisterModel( cgs.configStrings[i] );
		}
	} else if( i >= CS_SOUNDS && i < CS_SOUNDS + MAX_SOUNDS ) {
		if( cgs.configStrings[i][0] != '*' ) {
			cgs.soundPrecache[i - CS_SOUNDS] = trap_S_RegisterSound( cgs.configStrings[i] );
		}
	} else if( i >= CS_IMAGES && i < CS_IMAGES + MAX_IMAGES ) {
		if( strstr( cgs.configStrings[i], "correction/" ) ) { // HACK HACK HACK -- for color correction LUTs
			cgs.imagePrecache[i - CS_IMAGES] = trap_R_RegisterLinearPic( cgs.configStrings[i] );
		} else {
			cgs.imagePrecache[i - CS_IMAGES] = trap_R_RegisterPic( cgs.configStrings[i] );
		}
	} else if( i >= CS_SKINFILES && i < CS_SKINFILES + MAX_SKINFILES ) {
		cgs.skinPrecache[i - CS_SKINFILES] = trap_R_RegisterSkinFile( cgs.configStrings[i] );
	} else if( i >= CS_LIGHTS && i < CS_LIGHTS + MAX_LIGHTSTYLES ) {
		CG_SetLightStyle( i - CS_LIGHTS );
	} else if( i >= CS_ITEMS && i < CS_ITEMS + MAX_ITEMS ) {
		CG_ValidateItemDef( i - CS_ITEMS, cgs.configStrings[i] );
	} else if( i >= CS_PLAYERINFOS && i < CS_PLAYERINFOS + MAX_CLIENTS ) {
		CG_LoadClientInfo( i - CS_PLAYERINFOS );
	} else if( i >= CS_GAMECOMMANDS && i < CS_GAMECOMMANDS + MAX_GAMECOMMANDS ) {
		if( !cgs.demoPlaying ) {
			trap_Cmd_AddCommand( cgs.configStrings[i], NULL );
			if( !Q_stricmp( cgs.configStrings[i], "gametypemenu" ) ) {
				cgs.hasGametypeMenu = true;
			}
		}
	} else if( i >= CS_WEAPONDEFS && i < CS_WEAPONDEFS + MAX_WEAPONDEFS ) {
		CG_OverrideWeapondef( i - CS_WEAPONDEFS, cgs.configStrings[i] );
	}
}

/*
* CG_SC_Scoreboard
*/
static void CG_SC_Scoreboard( void ) {
	SCR_UpdateScoreboardMessage( trap_Cmd_Argv( 1 ) );
}

/*
* CG_SC_PrintPlayerStats
*/
static void CG_SC_PrintPlayerStats( const char *s, void ( *print )( const char *format, ... ), void ( *printDmg )( const char *format, ... ) ) {
	int playerNum;
	int i, shot_strong, hit_total, shot_total;
	int total_damage_given, total_damage_received, health_taken, armor_taken;
	gsitem_t *item;

	playerNum = CG_ParseValue( &s );
	if( playerNum < 0 || playerNum >= gs.maxclients ) {
		return;
	}

	if( !printDmg ) {
		printDmg = print;
	}

	// print stats to console/file
	printDmg( "Stats for %s" S_COLOR_WHITE ":\r\n", cgs.clientInfo[playerNum].name );
	print( "\r\nWeapon\r\n" );
	print( "    hit/shot percent\r\n" );

	for( i = WEAP_GUNBLADE; i < WEAP_TOTAL; i++ ) {
		item = GS_FindItemByTag( i );
		assert( item );

		shot_total = CG_ParseValue( &s );
		if( shot_total < 1 ) { // only continue with registered shots
			continue;
		}
		hit_total = CG_ParseValue( &s );

		// legacy - parse shot_strong and hit_strong
		shot_strong = CG_ParseValue( &s );
		if( shot_strong != shot_total ) {
			CG_ParseValue( &s );
		}

		// name
		print( "%s%2s" S_COLOR_WHITE ": ", item->color, item->shortname );

#define STATS_PERCENT( hit,total ) ( ( total ) == 0 ? 0 : ( ( hit ) == ( total ) ? 100 : (float)( hit ) * 100.0f / (float)( total ) ) )

		// total
		print( S_COLOR_GREEN "%3i" S_COLOR_WHITE "/" S_COLOR_CYAN "%3i      " S_COLOR_YELLOW "%2.1f",
			   hit_total, shot_total, STATS_PERCENT( hit_total, shot_total ) );

		print( "\r\n" );
	}

	print( "\r\n" );

	total_damage_given = CG_ParseValue( &s );
	total_damage_received = CG_ParseValue( &s );

	printDmg( S_COLOR_YELLOW "Damage given/received: " S_COLOR_WHITE "%i/%i " S_COLOR_YELLOW "ratio: %s%3.2f\r\n",
			  total_damage_given, total_damage_received,
			  ( total_damage_given > total_damage_received ? S_COLOR_GREEN : S_COLOR_RED ),
			  STATS_PERCENT( total_damage_given, total_damage_given + total_damage_received ) );

	health_taken = CG_ParseValue( &s );
	armor_taken = CG_ParseValue( &s );

	printDmg( S_COLOR_YELLOW "Health/Armor taken: " S_COLOR_CYAN "%i" S_COLOR_WHITE "/" S_COLOR_CYAN "%i\r\n",
			  health_taken, armor_taken );

#undef STATS_PERCENT
}

/*
* CG_SC_PrintStatsToFile
*/
static int cg_statsFileHandle;
void CG_SC_PrintStatsToFile( const char *format, ... ) {
	va_list argptr;
	char msg[1024];

	va_start( argptr, format );
	Q_vsnprintfz( msg, sizeof( msg ), format, argptr );
	va_end( argptr );

	trap_FS_Print( cg_statsFileHandle, msg );
}

/*
* CG_SC_DumpPlayerStats
*/
static void CG_SC_DumpPlayerStats( const char *filename, const char *stats ) {
	if( cgs.demoPlaying ) {
		return;
	}

	if( trap_FS_FOpenFile( filename, &cg_statsFileHandle, FS_APPEND ) == -1 ) {
		CG_Printf( "Couldn't write autorecorded stats, error opening file %s\n", filename );
		return;
	}

	CG_SC_PrintPlayerStats( stats, CG_SC_PrintStatsToFile, NULL );

	trap_FS_FCloseFile( cg_statsFileHandle );
}

/*
* CG_SC_PlayerStats
*/
static void CG_SC_PlayerStats( void ) {
	const char *s;
	int print;

	print = atoi( trap_Cmd_Argv( 1 ) );
	s = trap_Cmd_Argv( 2 );

	if( !print ) { // scoreboard message update
		SCR_UpdatePlayerStatsMessage( s );
		return;
	}

	CG_SC_PrintPlayerStats( s, CG_Printf, CG_LocalPrint );

	if( print == 2 ) {
		CG_SC_AutoRecordAction( "stats" );
	}
}

/*
* CG_SC_AutoRecordName
*/
static const char *CG_SC_AutoRecordName( void ) {
	time_t long_time;
	struct tm *newtime;
	static char name[MAX_STRING_CHARS];
	char mapname[MAX_CONFIGSTRING_CHARS];
	const char *cleanplayername, *cleanplayername2;

	// get date from system
	time( &long_time );
	newtime = localtime( &long_time );

	if( cg.view.POVent <= 0 ) {
		cleanplayername2 = "";
	} else {
		// remove color tokens from player names (doh)
		cleanplayername = COM_RemoveColorTokens( cgs.clientInfo[cg.view.POVent - 1].name );

		// remove junk chars from player names for files
		cleanplayername2 = COM_RemoveJunkChars( cleanplayername );
	}

	// lowercase mapname
	Q_strncpyz( mapname, cgs.configStrings[CS_MAPNAME], sizeof( mapname ) );
	Q_strlwr( mapname );

	// make file name
	// duel_year-month-day_hour-min_map_player
	Q_snprintfz( name, sizeof( name ), "%s_%04d-%02d-%02d_%02d-%02d_%s_%s_%04i",
				 gs.gametypeName,
				 newtime->tm_year + 1900, newtime->tm_mon + 1, newtime->tm_mday,
				 newtime->tm_hour, newtime->tm_min,
				 mapname,
				 cleanplayername2,
				 (int)brandom( 0, 9999 )
				 );

	return name;
}

/*
* CG_SC_AutoRecordAction
*/
void CG_SC_AutoRecordAction( const char *action ) {
	static bool autorecording = false;
	const char *name;
	bool spectator;

	if( !action[0] ) {
		return;
	}

	// filter out autorecord commands when playing a demo
	if( cgs.demoPlaying ) {
		return;
	}

	// let configstrings and other stuff arrive before taking any action
	if( !cgs.precacheDone ) {
		return;
	}

	if( cg.frame.playerState.pmove.pm_type == PM_SPECTATOR || cg.frame.playerState.pmove.pm_type == PM_CHASECAM ) {
		spectator = true;
	} else {
		spectator = false;
	}

	name = CG_SC_AutoRecordName();

	if( !Q_stricmp( action, "start" ) ) {
		if( cg_autoaction_demo->integer && ( !spectator || cg_autoaction_spectator->integer ) ) {
			trap_Cmd_ExecuteText( EXEC_NOW, "stop silent" );
			trap_Cmd_ExecuteText( EXEC_NOW, va( "record autorecord/%s/%s silent",
												gs.gametypeName, name ) );
			autorecording = true;
		}
	} else if( !Q_stricmp( action, "altstart" ) ) {
		if( cg_autoaction_demo->integer && ( !spectator || cg_autoaction_spectator->integer ) ) {
			trap_Cmd_ExecuteText( EXEC_NOW, va( "record autorecord/%s/%s silent",
												gs.gametypeName, name ) );
			autorecording = true;
		}
	} else if( !Q_stricmp( action, "stop" ) ) {
		if( autorecording ) {
			trap_Cmd_ExecuteText( EXEC_NOW, "stop silent" );
			autorecording = false;
		}

		if( cg_autoaction_screenshot->integer && ( !spectator || cg_autoaction_spectator->integer ) ) {
			trap_Cmd_ExecuteText( EXEC_NOW, va( "screenshot autorecord/%s/%s silent",
												gs.gametypeName, name ) );
		}
	} else if( !Q_stricmp( action, "cancel" ) ) {
		if( autorecording ) {
			trap_Cmd_ExecuteText( EXEC_NOW, "stop cancel silent" );
			autorecording = false;
		}
	} else if( !Q_stricmp( action, "stats" ) ) {
		if( cg_autoaction_stats->integer && ( !spectator || cg_autoaction_spectator->integer ) ) {
			const char *filename = va( "stats/%s/%s.txt", gs.gametypeName, name );
			CG_SC_DumpPlayerStats( filename, trap_Cmd_Argv( 2 ) );
		}
	} else if( developer->integer ) {
		CG_Printf( "CG_SC_AutoRecordAction: Unknown action: %s\n", action );
	}
}

/*
* CG_SC_ChannelAdd
*/
static void CG_SC_ChannelAdd( void ) {
	char menuparms[MAX_STRING_CHARS];

	Q_snprintfz( menuparms, sizeof( menuparms ), "menu_tvchannel_add %s\n", trap_Cmd_Args() );
	trap_Cmd_ExecuteText( EXEC_NOW, menuparms );
}

/*
* CG_SC_ChannelRemove
*/
static void CG_SC_ChannelRemove( void ) {
	int i, id;

	for( i = 1; i < trap_Cmd_Argc(); i++ ) {
		id = atoi( trap_Cmd_Argv( i ) );
		if( id <= 0 ) {
			continue;
		}
		trap_Cmd_ExecuteText( EXEC_NOW, va( "menu_tvchannel_remove %i\n", id ) );
	}
}

/**
 * Returns the English match state message.
 *
 * @param mm match message ID
 * @return match message text
 */
static const char *CG_MatchMessageString( matchmessage_t mm ) {
	if( ( trap_IN_SupportedDevices() & ( IN_DEVICE_KEYBOARD | IN_DEVICE_MOUSE ) ) != ( IN_DEVICE_KEYBOARD | IN_DEVICE_MOUSE ) ) {
		switch( mm ) {
			case MATCHMESSAGE_CHALLENGERS_QUEUE:
				return "You are inside the challengers queue waiting for your turn to play.\n"
					   "Use the in-game menu to exit the queue.";

			case MATCHMESSAGE_ENTER_CHALLENGERS_QUEUE:
				return "Use the in-game menu to enter the challengers queue.\n"
					   "Only players in the queue will have a turn to play against the last winner.";

			case MATCHMESSAGE_GET_READY:
				return "Set yourself READY using the in-game menu to start the match!";

			case MATCHMESSAGE_WAITING_FOR_PLAYERS:
				return "Waiting for players.";

			default:
				return "";
		}

		return "";
	}

	switch( mm ) {
		case MATCHMESSAGE_CHALLENGERS_QUEUE:
			return "'ESC' for in-game menu or 'ENTER' for in-game chat.\n"
				   "You are inside the challengers queue waiting for your turn to play.\n"
				   "Use the in-game menu to exit the queue.\n"
				   "\nUse the mouse buttons for switching spectator modes.";

		case MATCHMESSAGE_ENTER_CHALLENGERS_QUEUE:
			return "'ESC' for in-game menu or 'ENTER' for in-game chat.\n"
				   "Use the in-game menu or press 'F3' to enter the challengers queue.\n"
				   "Only players in the queue will have a turn to play against the last winner.\n"
				   "\nUse the mouse buttons for switching spectator modes.";

		case MATCHMESSAGE_SPECTATOR_MODES:
			return "'ESC' for in-game menu or 'ENTER' for in-game chat.\n"
				   "Mouse buttons for switching spectator modes.\n"
				   "This message can be hidden by disabling 'help' in player setup menu.";

		case MATCHMESSAGE_GET_READY:
			return "Set yourself READY to start the match!\n"
				   "You can use the in-game menu or simply press 'F4'.\n"
				   "'ESC' for in-game menu or 'ENTER' for in-game chat.";

		case MATCHMESSAGE_WAITING_FOR_PLAYERS:
			return "Waiting for players.\n"
				   "'ESC' for in-game menu.";

		default:
			return "";
	}

	return "";
}

/*
* CG_SC_MatchMessage
*/
static void CG_SC_MatchMessage( void ) {
	matchmessage_t mm;
	const char *matchmessage;

	cg.matchmessage = NULL;

	mm = (matchmessage_t)atoi( trap_Cmd_Argv( 1 ) );
	matchmessage = CG_MatchMessageString( mm );
	if( !matchmessage || !matchmessage[0] ) {
		return;
	}

	cg.matchmessage = CG_TranslateString( matchmessage );
}

/*
* CG_SC_HelpMessage
*/
static void CG_SC_HelpMessage( void ) {
	unsigned index;
	const char *id;
	const char *helpmessage = NULL;
	unsigned outlen = 0;
	int c;

	cg.helpmessage[0] = '\0';

	index = atoi( trap_Cmd_Argv( 1 ) );
	if( !index || index > MAX_HELPMESSAGES ) {
		return;
	}

	id = cgs.configStrings[CS_HELPMESSAGES + index - 1];
	if( !id[0] ) {
		return;
	}

	if( !( trap_IN_SupportedDevices() & IN_DEVICE_KEYBOARD ) ) {
		char nokeyboard[MAX_CONFIGSTRING_CHARS + 16];
		Q_snprintfz( nokeyboard, sizeof( nokeyboard ), "%s_nokeyboard", id );
		helpmessage = trap_L10n_TranslateString( nokeyboard );
	}

	if( !helpmessage ) {
		helpmessage = CG_TranslateString( id );
	}

	while( ( c = helpmessage[0] ) && ( outlen < MAX_HELPMESSAGE_CHARS - 1 ) ) {
		helpmessage++;

		if( c == '{' ) { // template
			int t = *( helpmessage++ );
			switch( t ) {
				case 'B': // key binding
				{
					char cmd[MAX_STRING_CHARS];
					unsigned cmdlen = 0;
					while( ( c = helpmessage[0] ) != '\0' ) {
						helpmessage++;
						if( c == '}' ) {
							break;
						}
						if( cmdlen < MAX_STRING_CHARS - 1 ) {
							cmd[cmdlen++] = c;
						}
					}
					cmd[cmdlen] = '\0';
					CG_GetBoundKeysString( cmd, cg.helpmessage + outlen, MAX_HELPMESSAGE_CHARS - outlen );
					outlen += strlen( cg.helpmessage + outlen );
				}
					continue;
				default:
					helpmessage--;
					break;
			}
		}

		cg.helpmessage[outlen++] = c;
	}
	cg.helpmessage[outlen] = '\0';
	Q_FixTruncatedUtf8( cg.helpmessage );

	cg.helpmessage_time = cg.time;
}

/*
* CG_CS_UpdateTeamInfo
*/
static void CG_CS_UpdateTeamInfo( void ) {
	char *ti;

	ti = trap_Cmd_Argv( 1 );
	if( !ti[0] ) {
		cg.teaminfo_size = 0;
		CG_Free( cg.teaminfo );
		cg.teaminfo = NULL;
		return;
	}

	if( strlen( ti ) + 1 > cg.teaminfo_size ) {
		if( cg.teaminfo ) {
			CG_Free( cg.teaminfo );
		}
		cg.teaminfo_size = strlen( ti ) + 1;
		cg.teaminfo = ( char * )CG_Malloc( cg.teaminfo_size );
	}

	Q_strncpyz( cg.teaminfo, ti, cg.teaminfo_size );
}

/*
* CG_Cmd_DemoGet_f
*/
static bool demo_requested = false;
static void CG_Cmd_DemoGet_f( void ) {
	if( demo_requested ) {
		CG_Printf( "Already requesting a demo\n" );
		return;
	}

	if( trap_Cmd_Argc() != 2 || ( atoi( trap_Cmd_Argv( 1 ) ) <= 0 && trap_Cmd_Argv( 1 )[0] != '.' ) ) {
		CG_Printf( "Usage: demoget <number>\n" );
		CG_Printf( "Downloads a demo from the server\n" );
		CG_Printf( "Use the demolist command to see list of demos on the server\n" );
		return;
	}

	trap_Cmd_ExecuteText( EXEC_NOW, va( "cmd demoget %s", trap_Cmd_Argv( 1 ) ) );

	demo_requested = true;
}

/*
* CG_SC_DemoGet
*/
static void CG_SC_DemoGet( void ) {
	const char *filename, *extension;

	if( cgs.demoPlaying ) {
		// ignore download commands coming from demo files
		return;
	}

	if( !demo_requested ) {
		CG_Printf( "Warning: demoget when not requested, ignored\n" );
		return;
	}

	demo_requested = false;

	if( trap_Cmd_Argc() < 2 ) {
		CG_Printf( "No such demo found\n" );
		return;
	}

	filename = trap_Cmd_Argv( 1 );
	extension = COM_FileExtension( filename );
	if( !COM_ValidateRelativeFilename( filename ) ||
		!extension || Q_stricmp( extension, cgs.demoExtension ) ) {
		CG_Printf( "Warning: demoget: Invalid filename, ignored\n" );
		return;
	}

	trap_DownloadRequest( filename, false );
}

/*
* CG_SC_MOTD
*/
static void CG_SC_MOTD( void ) {
	char *motd;

	if( cg.motd ) {
		CG_Free( cg.motd );
	}
	cg.motd = NULL;

	motd = trap_Cmd_Argv( 2 );
	if( !motd[0] ) {
		return;
	}

	if( !strcmp( trap_Cmd_Argv( 1 ), "1" ) ) {
		cg.motd = CG_CopyString( motd );
		cg.motd_time = cg.time + 50 * strlen( motd );
		if( cg.motd_time < cg.time + 5000 ) {
			cg.motd_time = cg.time + 5000;
		}
	}

	CG_Printf( "\nMessage of the Day:\n%s", motd );
}

/*
* CG_SC_MenuCustom
*/
static void CG_SC_MenuCustom( void ) {
	char request[MAX_STRING_CHARS];
	int i, c;

	if( cgs.demoPlaying ) {
		return;
	}

	if( trap_Cmd_Argc() < 2 ) {
		return;
	}

	Q_strncpyz( request, va( "menu_open custom title \"%s\" ", trap_Cmd_Argv( 1 ) ), sizeof( request ) );

	for( i = 2, c = 1; i < trap_Cmd_Argc() - 1; i += 2, c++ ) {
		const char *label = trap_Cmd_Argv( i );
		const char *cmd = trap_Cmd_Argv( i + 1 );

		Q_strncatz( request, va( "btn%i \"%s\" ", c, label ), sizeof( request ) );
		Q_strncatz( request, va( "cmd%i \"%s%s\" ", c, *cmd ? "cmd " : "", cmd ), sizeof( request ) );
	}

	trap_Cmd_ExecuteText( EXEC_APPEND, va( "%s\n", request ) );
}

/*
* CG_SC_MenuQuick
*/
static void CG_SC_MenuQuick( void ) {
	int i, c;

	if( cgs.demoPlaying ) {
		return;
	}

	cg.quickmenu[0] = '\0';

	if( trap_Cmd_Argc() >= 2 ) {
		for( i = 1, c = 1; i < trap_Cmd_Argc() - 1; i += 2, c++ ) {
			const char *label = trap_Cmd_Argv( i );
			const char *cmd = trap_Cmd_Argv( i + 1 );

			Q_strncatz( cg.quickmenu, va( "btn%i \"%s\" ", c, label ), sizeof( cg.quickmenu ) );
			Q_strncatz( cg.quickmenu, va( "cmd%i \"%s%s\" ", c, *cmd ? "cmd " : "", cmd ), sizeof( cg.quickmenu ) );
		}
	}

	CG_RefreshQuickMenu();
}

/*
* CG_SC_MenuOpen
*/
static void CG_SC_MenuOpen_( bool modal ) {
	char request[MAX_STRING_CHARS];
	int i, c;

	if( cgs.demoPlaying ) {
		return;
	}

	if( trap_Cmd_Argc() < 2 ) {
		return;
	}

	Q_strncpyz( request, va( "%s \"%s\"", modal ? "menu_modal" : "menu_open", trap_Cmd_Argv( 1 ) ), sizeof( request ) );
	for( i = 2, c = 1; i < trap_Cmd_Argc(); i++, c++ )
		Q_strncatz( request, va( " param%i \"%s\"", c, trap_Cmd_Argv( i ) ), sizeof( request ) );

	trap_Cmd_ExecuteText( EXEC_APPEND, va( "%s\n", request ) );
}

/*
* CG_SC_MenuOpen
*/
static void CG_SC_MenuOpen( void ) {
	CG_SC_MenuOpen_( false );
}

/*
* CG_SC_MenuModal
*/
static void CG_SC_MenuModal( void ) {
	CG_SC_MenuOpen_( true );
}

/*
* CG_AddAward
*/
void CG_AddAward( const char *str ) {
	if( !str || !str[0] ) {
		return;
	}

	Q_strncpyz( cg.award_lines[cg.award_head % MAX_AWARD_LINES], CG_TranslateString( str ), MAX_CONFIGSTRING_CHARS );
	cg.award_times[cg.award_head % MAX_AWARD_LINES] = cg.time;
	cg.award_head++;
}

/*
* CG_SC_AddAward
*/
static void CG_SC_AddAward( void ) {
	CG_AddAward( trap_Cmd_Argv( 1 ) );
}

typedef struct
{
	const char *name;
	void ( *func )( void );
} svcmd_t;

static const svcmd_t cg_svcmds[] =
{
	{ "pr", CG_SC_Print },
	{ "ch", CG_SC_ChatPrint },
	{ "tch", CG_SC_ChatPrint },
	{ "tvch", CG_SC_TVChatPrint },
	{ "cp", CG_SC_CenterPrint },
	{ "cpf", CG_SC_CenterPrintFormat },
	{ "obry", CG_SC_Obituary },
	{ "scb", CG_SC_Scoreboard },
	{ "plstats", CG_SC_PlayerStats },
	{ "mm", CG_SC_MatchMessage },
	{ "mapmsg", CG_SC_HelpMessage },
	{ "ti", CG_CS_UpdateTeamInfo },
	{ "demoget", CG_SC_DemoGet },
	{ "cha", CG_SC_ChannelAdd },
	{ "chr", CG_SC_ChannelRemove },
	{ "mecu", CG_SC_MenuCustom },
	{ "meop", CG_SC_MenuOpen },
	{ "memo", CG_SC_MenuModal },
	{ "motd", CG_SC_MOTD },
	{ "aw", CG_SC_AddAward },
	{ "qm", CG_SC_MenuQuick },

	{ NULL }
};

/*
* CG_GameCommand
*/
void CG_GameCommand( const char *command ) {
	char *s;
	const svcmd_t *cmd;

	trap_Cmd_TokenizeString( command );

	s = trap_Cmd_Argv( 0 );
	for( cmd = cg_svcmds; cmd->name; cmd++ ) {
		if( !strcmp( s, cmd->name ) ) {
			cmd->func();
			return;
		}
	}

	CG_Printf( "Unknown game command: %s\n", s );
}

/*
==========================================================================

CGAME COMMANDS

==========================================================================
*/

/*
* CG_UseItem
*/
void CG_UseItem( const char *name ) {
	gsitem_t *item;

	if( !cg.frame.valid || cgs.demoPlaying ) {
		return;
	}

	if( !name ) {
		return;
	}

	item = GS_Cmd_UseItem( &cg.frame.playerState, name, 0 );
	if( item ) {
		if( item->type & IT_WEAPON ) {
			CG_Predict_ChangeWeapon( item->tag );
			cg.lastWeapon = cg.predictedPlayerState.stats[STAT_PENDING_WEAPON];
		}

		trap_Cmd_ExecuteText( EXEC_NOW, va( "cmd use %i", item->tag ) );
	}
}

/*
* CG_Cmd_UseItem_f
*/
static void CG_Cmd_UseItem_f( void ) {
	if( !trap_Cmd_Argc() ) {
		CG_Printf( "Usage: 'use <item name>' or 'use <item index>'\n" );
		return;
	}

	CG_UseItem( trap_Cmd_Args() );
}

/*
* CG_Cmd_NextWeapon_f
*/
static void CG_Cmd_NextWeapon_f( void ) {
	gsitem_t *item;

	if( !cg.frame.valid ) {
		return;
	}

	if( cgs.demoPlaying || cg.predictedPlayerState.pmove.pm_type == PM_CHASECAM ) {
		CG_ChaseStep( 1 );
		return;
	}

	item = GS_Cmd_NextWeapon_f( &cg.frame.playerState, cg.predictedWeaponSwitch );
	if( item ) {
		CG_Predict_ChangeWeapon( item->tag );
		trap_Cmd_ExecuteText( EXEC_NOW, va( "cmd use %i", item->tag ) );
		cg.lastWeapon = cg.predictedPlayerState.stats[STAT_PENDING_WEAPON];
	}
}

/*
* CG_Cmd_PrevWeapon_f
*/
static void CG_Cmd_PrevWeapon_f( void ) {
	gsitem_t *item;

	if( !cg.frame.valid ) {
		return;
	}

	if( cgs.demoPlaying || cg.predictedPlayerState.pmove.pm_type == PM_CHASECAM ) {
		CG_ChaseStep( -1 );
		return;
	}

	item = GS_Cmd_PrevWeapon_f( &cg.frame.playerState, cg.predictedWeaponSwitch );
	if( item ) {
		CG_Predict_ChangeWeapon( item->tag );
		trap_Cmd_ExecuteText( EXEC_NOW, va( "cmd use %i", item->tag ) );
		cg.lastWeapon = cg.predictedPlayerState.stats[STAT_PENDING_WEAPON];
	}
}

/*
* CG_Cmd_PrevWeapon_f
*/
static void CG_Cmd_LastWeapon_f( void ) {
	gsitem_t *item;

	if( !cg.frame.valid || cgs.demoPlaying ) {
		return;
	}

	if( cg.lastWeapon != WEAP_NONE && cg.lastWeapon != cg.predictedPlayerState.stats[STAT_PENDING_WEAPON] ) {
		item = GS_Cmd_UseItem( &cg.frame.playerState, va( "%i", cg.lastWeapon ), IT_WEAPON );
		if( item ) {
			if( item->type & IT_WEAPON ) {
				CG_Predict_ChangeWeapon( item->tag );
			}

			trap_Cmd_ExecuteText( EXEC_NOW, va( "cmd use %i", item->tag ) );
			cg.lastWeapon = cg.predictedPlayerState.stats[STAT_PENDING_WEAPON];
		}
	}
}

/*
* CG_Cmd_WeaponCross_f
*/
static void CG_Cmd_WeaponCross_f( void ) {
	int i;
	int quarter = -1, first;
	int w[2], count = 0, selected = -1, select;
	gsitem_t *item;

	if( !cg.frame.valid ) {
		return;
	}

	if( trap_Cmd_Argc() > 1 ) {
		quarter = atoi( trap_Cmd_Argv( 1 ) );
	}

	if( ( quarter < 0 ) || ( quarter > 4 ) ) {
		CG_Printf( "Usage: '%s <0-4>' (0 - just show, 1 - GB/MG, 2 - RG/GL, 3 - RL/PG, 4 - LG/EB)\n", trap_Cmd_Argv( 0 ) );
		return;
	}

	if( cgs.demoPlaying || ( cg.predictedPlayerState.pmove.pm_type != PM_NORMAL ) ) {
		if( cgs.demoPlaying ||
			( cg.predictedPlayerState.pmove.pm_type == PM_SPECTATOR ) ||
			( cg.predictedPlayerState.pmove.pm_type == PM_CHASECAM ) ) {
			switch( quarter ) {
				case 1:
				case 3:
					CG_SwitchChaseCamMode();
					break;
				case 2:
					CG_ChaseStep( 1 );
					break;
				case 4:
					CG_ChaseStep( -1 );
					break;
			}
		}
		return;
	}

	CG_ShowWeaponCross();

	if( !quarter ) {
		return;
	}

	quarter--;
	first = quarter << 1;

	for( i = 0; i < 2; i++ ) {
		if( !cg.predictedPlayerState.inventory[WEAP_GUNBLADE + first + i] ) {
			continue;
		}
		if( ( first + i ) /* show uncharged gunblade */ &&
			!cg.predictedPlayerState.inventory[AMMO_GUNBLADE + first + i] &&
			!cg.predictedPlayerState.inventory[AMMO_WEAK_GUNBLADE + first + i] ) {
			continue;
		}

		if( cg.predictedPlayerState.stats[STAT_PENDING_WEAPON] == ( WEAP_GUNBLADE + first + i ) ) {
			selected = i;
		}

		w[count] = first + i;
		count++;
	}

	if( !count ) {
		return;
	}

	if( count == 2 ) {
		if( selected >= 0 ) {
			select = selected ^ 1;
		} else {
			select = ( cg.lastCrossWeapons >> quarter ) & 1;
		}
	} else {
		if( selected >= 0 ) {
			return;
		}
		select = 0;
	}

	item = GS_Cmd_UseItem( &cg.frame.playerState, va( "%i", WEAP_GUNBLADE + w[select] ), IT_WEAPON );
	if( item ) {
		if( item->type & IT_WEAPON ) {
			CG_Predict_ChangeWeapon( item->tag );
		}
		trap_Cmd_ExecuteText( EXEC_NOW, va( "cmd use %i", item->tag ) );
		cg.lastCrossWeapons = ( cg.lastCrossWeapons & ~( 1 << quarter ) ) | ( ( w[select] & 1 ) << quarter );
	}
}

/*
* CG_Viewpos_f
*/
static void CG_Viewpos_f( void ) {
	CG_Printf( "\"origin\" \"%i %i %i\"\n", (int)cg.view.origin[0], (int)cg.view.origin[1], (int)cg.view.origin[2] );
	CG_Printf( "\"angles\" \"%i %i %i\"\n", (int)cg.view.angles[0], (int)cg.view.angles[1], (int)cg.view.angles[2] );
}

// ======================================================================

/*
* CG_GametypeMenuCmdAdd_f
*/
static void CG_GametypeMenuCmdAdd_f( void ) {
	cgs.hasGametypeMenu = true;
}

/*
* CG_PlayerNamesCompletionExt_f
*
* Helper function
*/
static char **CG_PlayerNamesCompletionExt_f( const char *partial, bool teamOnly ) {
	int i;
	int team = cg_entities[cgs.playerNum + 1].current.team;
	char **matches = NULL;
	int num_matches = 0;

	if( partial ) {
		size_t partial_len = strlen( partial );

		matches = (char **) CG_Malloc( sizeof( char * ) * ( gs.maxclients + 1 ) );
		for( i = 0; i < gs.maxclients; i++ ) {
			cg_clientInfo_t *info = cgs.clientInfo + i;
			if( !info->cleanname[0] ) {
				continue;
			}
			if( teamOnly && ( cg_entities[i + 1].current.team != team ) ) {
				continue;
			}
			if( !Q_strnicmp( info->cleanname, partial, partial_len ) ) {
				matches[num_matches++] = info->cleanname;
			}
		}
		matches[num_matches] = NULL;
	}

	return matches;
}

/*
* CG_PlayerNamesCompletion_f
*/
static char **CG_PlayerNamesCompletion_f( const char *partial ) {
	return CG_PlayerNamesCompletionExt_f( partial, false );
}

/*
* CG_TeamPlayerNamesCompletion_f
*/
static char **CG_TeamPlayerNamesCompletion_f( const char *partial ) {
	return CG_PlayerNamesCompletionExt_f( partial, true );
}

/*
* CG_SayCmdAdd_f
*/
static void CG_SayCmdAdd_f( void ) {
	trap_Cmd_SetCompletionFunc( "say", &CG_PlayerNamesCompletion_f );
}

/*
* CG_SayTeamCmdAdd_f
*/
static void CG_SayTeamCmdAdd_f( void ) {
	trap_Cmd_SetCompletionFunc( "say_team", &CG_TeamPlayerNamesCompletion_f );
}

/*
* CG_StatsCmdAdd_f
*/
static void CG_StatsCmdAdd_f( void ) {
	trap_Cmd_SetCompletionFunc( "stats", &CG_PlayerNamesCompletion_f );
}

/*
* CG_WhoisCmdAdd_f
*/
static void CG_WhoisCmdAdd_f( void ) {
	trap_Cmd_SetCompletionFunc( "whois", &CG_PlayerNamesCompletion_f );
}

// server commands
static svcmd_t cg_consvcmds[] =
{
	{ "gametypemenu", CG_GametypeMenuCmdAdd_f },
	{ "say", CG_SayCmdAdd_f },
	{ "say_team", CG_SayTeamCmdAdd_f },
	{ "stats", CG_StatsCmdAdd_f },
	{ "whois", CG_WhoisCmdAdd_f },

	{ NULL, NULL }
};

// local cgame commands
typedef struct
{
	const char *name;
	void ( *func )( void );
	bool allowdemo;
} cgcmd_t;

static const cgcmd_t cgcmds[] =
{
	{ "score", CG_ToggleScores_f, true },
	{ "+scores", CG_ScoresOn_f, true },
	{ "-scores", CG_ScoresOff_f, true },
	{ "demoget", CG_Cmd_DemoGet_f, false },
	{ "demolist", NULL, false },
	{ "use", CG_Cmd_UseItem_f, false },
	{ "weapnext", CG_Cmd_NextWeapon_f, true },
	{ "weapprev", CG_Cmd_PrevWeapon_f, true },
	{ "weaplast", CG_Cmd_LastWeapon_f, true },
	{ "weapcross", CG_Cmd_WeaponCross_f, true },
	{ "viewpos", CG_Viewpos_f, true },
	{ "players", NULL, false },
	{ "spectators", NULL, false },

	{ NULL, NULL, false }
};

/*
* CG_RegisterCGameCommands
*/
void CG_RegisterCGameCommands( void ) {
	int i;
	char *name;
	const cgcmd_t *cmd;

	if( !cgs.demoPlaying ) {
		const svcmd_t *svcmd;

		// add game side commands
		for( i = 0; i < MAX_GAMECOMMANDS; i++ ) {
			name = cgs.configStrings[CS_GAMECOMMANDS + i];
			if( !name[0] ) {
				continue;
			}

			// check for local command overrides
			for( cmd = cgcmds; cmd->name; cmd++ ) {
				if( !Q_stricmp( cmd->name, name ) ) {
					break;
				}
			}
			if( cmd->name ) {
				continue;
			}

			trap_Cmd_AddCommand( name, NULL );

			// check for server commands we might want to do some special things for..
			for( svcmd = cg_consvcmds; svcmd->name; svcmd++ ) {
				if( !Q_stricmp( svcmd->name, name ) ) {
					if( svcmd->func ) {
						svcmd->func();
					}
					break;
				}
			}
		}
	}

	// add local commands
	for( cmd = cgcmds; cmd->name; cmd++ ) {
		if( cgs.demoPlaying && !cmd->allowdemo ) {
			continue;
		}
		trap_Cmd_AddCommand( cmd->name, cmd->func );
	}
}

/*
* CG_UnregisterCGameCommands
*/
void CG_UnregisterCGameCommands( void ) {
	int i;
	char *name;
	const cgcmd_t *cmd;

	if( !cgs.demoPlaying ) {
		// remove game commands
		for( i = 0; i < MAX_GAMECOMMANDS; i++ ) {
			name = cgs.configStrings[CS_GAMECOMMANDS + i];
			if( !name[0] ) {
				continue;
			}

			// check for local command overrides so we don't try
			// to unregister them twice
			for( cmd = cgcmds; cmd->name; cmd++ ) {
				if( !Q_stricmp( cmd->name, name ) ) {
					break;
				}
			}
			if( cmd->name ) {
				continue;
			}

			trap_Cmd_RemoveCommand( name );
		}

		cgs.hasGametypeMenu = false;
	}

	// remove local commands
	for( cmd = cgcmds; cmd->name; cmd++ ) {
		if( cgs.demoPlaying && !cmd->allowdemo ) {
			continue;
		}
		trap_Cmd_RemoveCommand( cmd->name );
	}
}
