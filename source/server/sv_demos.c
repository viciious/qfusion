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

#include "server.h"

#define SV_DEMO_DIR va( "demos/server%s%s", sv_demodir->string[0] ? "/" : "", sv_demodir->string[0] ? sv_demodir->string : "" )

/*
* SV_Demo_WriteMessage
*
* Writes given message to the demofile
*/
static void SV_Demo_WriteMessage( msg_t *msg ) {
	assert( svs.demo.file );
	if( !svs.demo.file ) {
		return;
	}

	SNAP_RecordDemoMessage( svs.demo.file, msg, 0 );
}

/*
* SV_Demo_WriteStartMessages
*/
static void SV_Demo_WriteStartMessages( void ) {
	// clear demo meta data, we'll write some keys later
	svs.demo.meta_data_realsize = SNAP_ClearDemoMeta( svs.demo.meta_data, sizeof( svs.demo.meta_data ) );

	SNAP_BeginDemoRecording( svs.demo.file, svs.spawncount, svc.snapFrameTime, sv.mapname, SV_BITFLAGS_RELIABLE,
							 svs.purelist, sv.configstrings[0], sv.baselines );
}

/*
* SV_Demo_WriteSnap
*/
void SV_Demo_WriteSnap( void ) {
	int i;
	msg_t msg;
	uint8_t msg_buffer[MAX_MSGLEN];

	if( !svs.demo.file ) {
		return;
	}

	for( i = 0; i < sv_maxclients->integer; i++ ) {
		if( svs.clients[i].state >= CS_SPAWNED && svs.clients[i].edict &&
			!( svs.clients[i].edict->r.svflags & SVF_NOCLIENT ) ) {
			break;
		}
	}
	if( i == sv_maxclients->integer ) { // FIXME
		Com_Printf( "No players left, stopping server side demo recording\n" );
		SV_Demo_Stop_f();
		return;
	}

	MSG_Init( &msg, msg_buffer, sizeof( msg_buffer ) );

	SV_BuildClientFrameSnap( &svs.demo.client, 0 );

	SV_WriteFrameSnapToClient( &svs.demo.client, &msg );

	SV_AddReliableCommandsToMessage( &svs.demo.client, &msg );

	SV_Demo_WriteMessage( &msg );

	svs.demo.duration = svs.gametime - svs.demo.basetime;
	svs.demo.client.lastframe = sv.framenum; // FIXME: is this needed?
}

/*
* SV_Demo_InitClient
*/
static void SV_Demo_InitClient( void ) {
	memset( &svs.demo.client, 0, sizeof( svs.demo.client ) );

	svs.demo.client.mv = true;
	svs.demo.client.reliable = true;

	svs.demo.client.reliableAcknowledge = 0;
	svs.demo.client.reliableSequence = 0;
	svs.demo.client.reliableSent = 0;
	memset( svs.demo.client.reliableCommands, 0, sizeof( svs.demo.client.reliableCommands ) );

	svs.demo.client.lastframe = sv.framenum - 1;
	svs.demo.client.nodelta = false;
}

/*
* SV_Demo_Start_f
*
* Begins server demo recording.
*/
void SV_Demo_Start_f( void ) {
	int demofilename_size, i;

	if( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: serverrecord <demoname>\n" );
		return;
	}

	if( svs.demo.file ) {
		Com_Printf( "Already recording\n" );
		return;
	}

	if( sv.state != ss_game ) {
		Com_Printf( "Must be in a level to record\n" );
		return;
	}

	for( i = 0; i < sv_maxclients->integer; i++ ) {
		if( svs.clients[i].state >= CS_SPAWNED && svs.clients[i].edict &&
			!( svs.clients[i].edict->r.svflags & SVF_NOCLIENT ) ) {
			break;
		}
	}
	if( i == sv_maxclients->integer ) {
		Com_Printf( "No players in game, can't record a demo\n" );
		return;
	}

	//
	// open the demo file
	//

	// real name
	demofilename_size =
		sizeof( char ) * ( strlen( SV_DEMO_DIR ) + 1 + strlen( Cmd_Args() ) + strlen( APP_DEMO_EXTENSION_STR ) + 1 );
	svs.demo.filename = Mem_ZoneMalloc( demofilename_size );

	Q_snprintfz( svs.demo.filename, demofilename_size, "%s/%s", SV_DEMO_DIR, Cmd_Args() );

	COM_SanitizeFilePath( svs.demo.filename );

	if( !COM_ValidateRelativeFilename( svs.demo.filename ) ) {
		Mem_ZoneFree( svs.demo.filename );
		svs.demo.filename = NULL;
		Com_Printf( "Invalid filename.\n" );
		return;
	}

	COM_DefaultExtension( svs.demo.filename, APP_DEMO_EXTENSION_STR, demofilename_size );

	// temp name
	demofilename_size = sizeof( char ) * ( strlen( svs.demo.filename ) + strlen( ".rec" ) + 1 );
	svs.demo.tempname = Mem_ZoneMalloc( demofilename_size );
	Q_snprintfz( svs.demo.tempname, demofilename_size, "%s.rec", svs.demo.filename );

	// open it
	if( FS_FOpenFile( svs.demo.tempname, &svs.demo.file, FS_WRITE | SNAP_DEMO_GZ ) == -1 ) {
		Com_Printf( "Error: Couldn't open file: %s\n", svs.demo.tempname );
		Mem_ZoneFree( svs.demo.filename );
		svs.demo.filename = NULL;
		Mem_ZoneFree( svs.demo.tempname );
		svs.demo.tempname = NULL;
		return;
	}

	Com_Printf( "Recording server demo: %s\n", svs.demo.filename );

	SV_Demo_InitClient();

	// write serverdata, configstrings and baselines
	svs.demo.duration = 0;
	svs.demo.basetime = svs.gametime;
	svs.demo.localtime = time( NULL );
	SV_Demo_WriteStartMessages();

	// write one nodelta frame
	svs.demo.client.nodelta = true;
	SV_Demo_WriteSnap();
	svs.demo.client.nodelta = false;
}

/*
* SV_Demo_Stop
*/
static void SV_Demo_Stop( bool cancel, bool silent ) {
	if( !svs.demo.file ) {
		if( !silent ) {
			Com_Printf( "No server demo recording in progress\n" );
		}
		return;
	}

	if( cancel ) {
		Com_Printf( "Canceled server demo recording: %s\n", svs.demo.filename );
	} else {
		SNAP_StopDemoRecording( svs.demo.file );

		Com_Printf( "Stopped server demo recording: %s\n", svs.demo.filename );
	}

	FS_FCloseFile( svs.demo.file );
	svs.demo.file = 0;

	if( cancel ) {
		if( !FS_RemoveFile( svs.demo.tempname ) ) {
			Com_Printf( "Error: Failed to delete the temporary server demo file\n" );
		}
	} else {
		// write some meta information about the match/demo
		SV_SetDemoMetaKeyValue( "hostname", sv.configstrings[CS_HOSTNAME] );
		SV_SetDemoMetaKeyValue( "localtime", va( "%u", svs.demo.localtime ) );
		SV_SetDemoMetaKeyValue( "multipov", "1" );
		SV_SetDemoMetaKeyValue( "duration", va( "%u", (int)ceil( svs.demo.duration / 1000.0f ) ) );
		SV_SetDemoMetaKeyValue( "mapname", sv.configstrings[CS_MAPNAME] );
		SV_SetDemoMetaKeyValue( "gametype", sv.configstrings[CS_GAMETYPENAME] );
		SV_SetDemoMetaKeyValue( "levelname", sv.configstrings[CS_MESSAGE] );
		SV_SetDemoMetaKeyValue( "matchname", sv.configstrings[CS_MATCHNAME] );
		SV_SetDemoMetaKeyValue( "matchscore", sv.configstrings[CS_MATCHSCORE] );
		SV_SetDemoMetaKeyValue( "matchuuid", sv.configstrings[CS_MATCHUUID] );

		SNAP_WriteDemoMetaData( svs.demo.tempname, svs.demo.meta_data, svs.demo.meta_data_realsize );

		if( !FS_MoveFile( svs.demo.tempname, svs.demo.filename ) ) {
			Com_Printf( "Error: Failed to rename the server demo file\n" );
		}
	}

	svs.demo.localtime = 0;
	svs.demo.basetime = svs.demo.duration = 0;

	SNAP_FreeClientFrames( &svs.demo.client );

	Mem_ZoneFree( svs.demo.filename );
	svs.demo.filename = NULL;
	Mem_ZoneFree( svs.demo.tempname );
	svs.demo.tempname = NULL;
}

/*
* SV_Demo_Stop_f
*
* Console command for stopping server demo recording.
*/
void SV_Demo_Stop_f( void ) {
	SV_Demo_Stop( false, atoi( Cmd_Argv( 1 ) ) != 0 );
}

/*
* SV_Demo_Cancel_f
*
* Cancels the server demo recording (stop, remove file)
*/
void SV_Demo_Cancel_f( void ) {
	SV_Demo_Stop( true, atoi( Cmd_Argv( 1 ) ) != 0 );
}

/*
* SV_Demo_Purge_f
*
* Removes the server demo files
*/
void SV_Demo_Purge_f( void ) {
	char *buffer;
	char *p, *s, num[8];
	char path[256];
	size_t extlen, length, bufSize;
	unsigned int i, numdemos, numautodemos, maxautodemos;

	if( Cmd_Argc() > 2 ) {
		Com_Printf( "Usage: serverrecordpurge [maxautodemos]\n" );
		return;
	}

	maxautodemos = 0;
	if( Cmd_Argc() == 2 ) {
		maxautodemos = atoi( Cmd_Argv( 1 ) );
	}

	numdemos = FS_GetFileListExt( SV_DEMO_DIR, APP_DEMO_EXTENSION_STR, NULL, &bufSize, 0, 0 );
	if( !numdemos ) {
		return;
	}

	extlen = strlen( APP_DEMO_EXTENSION_STR );
	buffer = Mem_TempMalloc( bufSize );
	FS_GetFileList( SV_DEMO_DIR, APP_DEMO_EXTENSION_STR, buffer, bufSize, 0, 0 );

	numautodemos = 0;
	s = buffer;
	for( i = 0; i < numdemos; i++, s += length + 1 ) {
		length = strlen( s );
		if( length < strlen( "_auto9999" ) + extlen ) {
			continue;
		}

		p = s + length - strlen( "_auto9999" ) - extlen;
		if( strncmp( p, "_auto", strlen( "_auto" ) ) ) {
			continue;
		}

		p += strlen( "_auto" );
		Q_snprintfz( num, sizeof( num ), "%04i", atoi( p ) );
		if( strncmp( p, num, 4 ) ) {
			continue;
		}

		numautodemos++;
	}

	if( numautodemos <= maxautodemos ) {
		Mem_TempFree( buffer );
		return;
	}

	s = buffer;
	for( i = 0; i < numdemos; i++, s += length + 1 ) {
		length = strlen( s );
		if( length < strlen( "_auto9999" ) + extlen ) {
			continue;
		}

		p = s + length - strlen( "_auto9999" ) - extlen;
		if( strncmp( p, "_auto", strlen( "_auto" ) ) ) {
			continue;
		}

		p += strlen( "_auto" );
		Q_snprintfz( num, sizeof( num ), "%04i", atoi( p ) );
		if( strncmp( p, num, 4 ) ) {
			continue;
		}

		Q_snprintfz( path, sizeof( path ), "%s/%s", SV_DEMO_DIR, s );
		Com_Printf( "Removing old autorecord demo: %s\n", path );
		if( !FS_RemoveFile( path ) ) {
			Com_Printf( "Error, couldn't remove file: %s\n", path );
			continue;
		}

		if( --numautodemos == maxautodemos ) {
			break;
		}
	}

	Mem_TempFree( buffer );
}

/*
* SV_DemoList_f
*/
#define DEMOS_PER_VIEW  30
void SV_DemoList_f( client_t *client ) {
	char message[MAX_STRING_CHARS];
	char numpr[16];
	char buffer[MAX_STRING_CHARS];
	char *s, *p;
	size_t j, length, length_escaped, pos, extlen;
	int numdemos, i, start = -1, end, k;

	if( client->state < CS_SPAWNED ) {
		return;
	}

	if( Cmd_Argc() > 2 ) {
		SV_AddGameCommand( client, "pr \"Usage: demolist [starting position]\n\"" );
		return;
	}

	if( Cmd_Argc() == 2 ) {
		start = atoi( Cmd_Argv( 1 ) ) - 1;
		if( start < 0 ) {
			SV_AddGameCommand( client, "pr \"Usage: demolist [starting position]\n\"" );
			return;
		}
	}

	Q_strncpyz( message, "pr \"Available demos:\n----------------\n", sizeof( message ) );

	numdemos = FS_GetFileList( SV_DEMO_DIR, APP_DEMO_EXTENSION_STR, NULL, 0, 0, 0 );
	if( numdemos ) {
		if( start < 0 ) {
			start = max( 0, numdemos - DEMOS_PER_VIEW );
		} else if( start > numdemos - 1 ) {
			start = numdemos - 1;
		}

		if( start > 0 ) {
			Q_strncatz( message, "...\n", sizeof( message ) );
		}

		end = start + DEMOS_PER_VIEW;
		if( end > numdemos ) {
			end = numdemos;
		}

		extlen = strlen( APP_DEMO_EXTENSION_STR );

		i = start;
		do {
			if( ( k = FS_GetFileList( SV_DEMO_DIR, APP_DEMO_EXTENSION_STR, buffer, sizeof( buffer ), i, end ) ) == 0 ) {
				i++;
				continue;
			}

			for( s = buffer; k > 0; k--, s += length + 1, i++ ) {
				length = strlen( s );

				length_escaped = length;
				p = s;
				while( ( p = strchr( p, '\\' ) ) )
					length_escaped++;

				Q_snprintfz( numpr, sizeof( numpr ), "%i: ", i + 1 );
				if( strlen( message ) + strlen( numpr ) + length_escaped - extlen + 1 + 5 >= sizeof( message ) ) {
					Q_strncatz( message, "\"", sizeof( message ) );
					SV_AddGameCommand( client, message );

					Q_strncpyz( message, "pr \"", sizeof( message ) );
					if( strlen( "demoget " ) + strlen( numpr ) + length_escaped - extlen + 1 + 5 >= sizeof( message ) ) {
						continue;
					}
				}

				Q_strncatz( message, numpr, sizeof( message ) );
				pos = strlen( message );
				for( j = 0; j < length - extlen; j++ ) {
					assert( s[j] != '\\' );
					if( s[j] == '"' ) {
						message[pos++] = '\\';
					}
					message[pos++] = s[j];
				}
				message[pos++] = '\n';
				message[pos] = '\0';
			}
		} while( i < end );

		if( end < numdemos ) {
			Q_strncatz( message, "...\n", sizeof( message ) );
		}
	} else {
		Q_strncatz( message, "none\n", sizeof( message ) );
	}

	Q_strncatz( message, "\"", sizeof( message ) );

	SV_AddGameCommand( client, message );
}

/*
* SV_DemoGet_f
*
* Responds to clients demoget request with: demoget "filename"
* If nothing is found, responds with demoget without filename, so client knowns it wasn't found
*/
void SV_DemoGet_f( client_t *client ) {
	int num, numdemos;
	char message[MAX_STRING_CHARS];
	char buffer[MAX_STRING_CHARS];
	char *s, *p;
	size_t j, length, length_escaped, pos, pos_bak, msglen;

	if( client->state < CS_SPAWNED ) {
		return;
	}
	if( Cmd_Argc() != 2 ) {
		return;
	}

	Q_strncpyz( message, "demoget \"", sizeof( message ) );
	Q_strncatz( message, SV_DEMO_DIR, sizeof( message ) );
	msglen = strlen( message );
	message[msglen++] = '/';

	pos = pos_bak = msglen;

	numdemos = FS_GetFileList( SV_DEMO_DIR, APP_DEMO_EXTENSION_STR, NULL, 0, 0, 0 );
	if( numdemos ) {
		if( Cmd_Argv( 1 )[0] == '.' ) {
			num = numdemos - strlen( Cmd_Argv( 1 ) );
		} else {
			num = atoi( Cmd_Argv( 1 ) ) - 1;
		}
		clamp( num, 0, numdemos - 1 );

		numdemos = FS_GetFileList( SV_DEMO_DIR, APP_DEMO_EXTENSION_STR, buffer, sizeof( buffer ), num, num + 1 );
		if( numdemos ) {
			s = buffer;
			length = strlen( buffer );

			length_escaped = length;
			p = s;
			while( ( p = strchr( p, '\\' ) ) )
				length_escaped++;

			if( msglen + length_escaped + 1 + 5 < sizeof( message ) ) {
				for( j = 0; j < length; j++ ) {
					assert( s[j] != '\\' );
					if( s[j] == '"' ) {
						message[pos++] = '\\';
					}
					message[pos++] = s[j];
				}
			}
		}
	}

	if( pos == pos_bak ) {
		return;
	}

	message[pos++] = '"';
	message[pos] = '\0';

	SV_AddGameCommand( client, message );
}

/*
* SV_IsDemoDownloadRequest
*/
bool SV_IsDemoDownloadRequest( const char *request ) {
	const char *ext;
	const char *demoDir = SV_DEMO_DIR;
	const size_t demoDirLen = strlen( demoDir );

	if( !request ) {
		return false;
	}
	if( strlen( request ) <= demoDirLen + 1 + strlen( APP_DEMO_EXTENSION_STR ) ) {
		// should at least contain demo dir name and demo file extension
		return false;
	}

	if( Q_strnicmp( request, demoDir, demoDirLen ) || request[demoDirLen] != '/' ) {
		// nah, wrong dir
		return false;
	}

	ext = COM_FileExtension( request );
	if( !ext || Q_stricmp( ext, APP_DEMO_EXTENSION_STR ) ) {
		// wrong extension
		return false;
	}

	return true;
}
