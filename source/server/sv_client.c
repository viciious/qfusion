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
// sv_client.c -- server code for moving users

#include "server.h"


//============================================================================
//
//		CLIENT
//
//============================================================================

void SV_ClientResetCommandBuffers( client_t *client ) {
	// reset the reliable commands buffer
	client->clientCommandExecuted = 0;
	client->reliableAcknowledge = 0;
	client->reliableSequence = 0;
	client->reliableSent = 0;
	memset( client->reliableCommands, 0, sizeof( client->reliableCommands ) );

	// reset the usercommands buffer(clc_move)
	client->UcmdTime = 0;
	client->UcmdExecuted = 0;
	client->UcmdReceived = 0;
	memset( client->ucmds, 0, sizeof( client->ucmds ) );

	// reset snapshots delta-compression
	client->lastframe = -1;
	client->lastSentFrameNum = 0;
}

void SV_ClientCloseDownload( client_t *client ) {
	if( client->download.file ) {
		FS_FCloseFile( client->download.file );
	}
	if( client->download.name ) {
		Mem_ZoneFree( client->download.name );
	}
	memset( &client->download, 0, sizeof( client->download ) );
}

/*
* SV_ClientConnect
* accept the new client
* this is the only place a client_t is ever initialized
*/
bool SV_ClientConnect( const socket_t *socket, const netadr_t *address,
					   client_t *client, char *userinfo,
					   int game_port, int challenge, bool fakeClient,
					   mm_uuid_t ticket_id, mm_uuid_t session_id ) {
	int i;
	edict_t *ent;
	int edictnum;
	char uuid_buffer[UUID_BUFFER_SIZE];

	edictnum = ( client - svs.clients ) + 1;
	ent = EDICT_NUM( edictnum );

	// make sure the client state is reset before an mm connection callback gets called
	memset( client, 0, sizeof( *client ) );

	// give mm a chance to reject if the server is locked ready for mm
	// must be called before ge->ClientConnect
	// ch : rly ignore fakeClient and tvClient here?

	session_id = SV_MM_ClientConnect( client, address, userinfo, ticket_id, session_id );
	if( Uuid_IsZeroUuid( session_id ) ) {
		return false;
	}

	// we need to set local sessions to userinfo ourselves
	if( Uuid_IsFFFsUuid( session_id ) ) {
		Info_SetValueForKey( userinfo, "cl_mm_session", Uuid_ToString( uuid_buffer, session_id ) );
	}

	// get the game a chance to reject this connection or modify the userinfo
	if( !ge->ClientConnect( ent, userinfo, fakeClient ) ) {
		return false;
	}

	// the connection is accepted, set up the client slot
	client->edict = ent;
	client->challenge = challenge; // save challenge for checksumming

	client->mm_session = session_id;
	client->mm_ticket = ticket_id;

	if( socket ) {
		switch( socket->type ) {
#ifdef TCP_ALLOW_CONNECT
			case SOCKET_TCP:
				client->reliable = true;
				client->individual_socket = true;
				client->socket = *socket;
				break;
#endif

			case SOCKET_UDP:
			case SOCKET_LOOPBACK:
				client->reliable = false;
				client->individual_socket = false;
				client->socket.open = false;
				break;

			default:
				assert( false );
		}
	} else {
		assert( fakeClient );
		client->reliable = false;
		client->individual_socket = false;
		client->socket.open = false;
	}

	SV_ClientResetCommandBuffers( client );

	// reset timeouts
	client->lastPacketReceivedTime = svs.realtime;
	client->lastconnect = Sys_Milliseconds();

	// init the connection
	client->state = CS_CONNECTING;

	if( fakeClient ) {
		client->netchan.remoteAddress.type = NA_NOTRANSMIT; // fake-clients can't transmit
		Info_SetValueForKey( userinfo, "cl_mm_session", Uuid_ToString( uuid_buffer, Uuid_FFFsUuid() ) );
	} else {
		if( client->individual_socket ) {
			Netchan_Setup( &client->netchan, &client->socket, address, game_port );
		} else {
			Netchan_Setup( &client->netchan, socket, address, game_port );
		}
	}


	// create default rating for the client and current gametype
	ge->AddDefaultRating( ent, NULL );

	// parse some info from the info strings
	client->userinfoLatchTimeout = Sys_Milliseconds() + USERINFO_UPDATE_COOLDOWN_MSEC;
	Q_strncpyz( client->userinfo, userinfo, sizeof( client->userinfo ) );
	SV_UserinfoChanged( client );

	// generate session id
	for( i = 0; i < sizeof( svs.clients[0].session ) - 1; i++ ) {
		const unsigned char symbols[65] =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
		client->session[i] = symbols[rand() % ( sizeof( symbols ) - 1 )];
	}
	client->session[i] = '\0';

	SV_Web_AddGameClient( client->session, client - svs.clients, &client->netchan.remoteAddress );

	return true;
}

/*
* SV_DropClient
*
* Called when the player is totally leaving the server, either willingly
* or unwillingly.  This is NOT called if the entire server is quiting
* or crashing.
*/
void SV_DropClient( client_t *drop, int type, const char *format, ... ) {
	va_list argptr;
	char *reason;
	char string[1024];

	if( format ) {
		va_start( argptr, format );
		Q_vsnprintfz( string, sizeof( string ), format, argptr );
		va_end( argptr );
		reason = string;
	} else {
		Q_strncpyz( string, "User disconnected", sizeof( string ) );
		reason = NULL;
	}

	// remove the rating of the client
	if( drop->edict ) {
		ge->RemoveRating( drop->edict );
	}

	// add the disconnect
	if( drop->edict && ( drop->edict->r.svflags & SVF_FAKECLIENT ) ) {
		ge->ClientDisconnect( drop->edict, reason );
		SV_ClientResetCommandBuffers( drop ); // make sure everything is clean
	} else {
		SV_InitClientMessage( drop, &tmpMessage, NULL, 0 );
		SV_SendServerCommand( drop, "disconnect %i \"%s\"", type, string );
		SV_AddReliableCommandsToMessage( drop, &tmpMessage );

		SV_SendMessageToClient( drop, &tmpMessage );
		Netchan_PushAllFragments( &drop->netchan );

		if( drop->state >= CS_CONNECTED ) {
			// call the prog function for removing a client
			// this will remove the body, among other things
			ge->ClientDisconnect( drop->edict, reason );
		} else if( drop->name[0] ) {
			Com_Printf( "Connecting client %s%s disconnected (%s%s)\n", drop->name, S_COLOR_WHITE, reason,
						S_COLOR_WHITE );
		}
	}

	SV_MM_ClientDisconnect( drop );

	SNAP_FreeClientFrames( drop );

	SV_Web_RemoveGameClient( drop->session );

	if( drop->download.name ) {
		SV_ClientCloseDownload( drop );
	}

	if( drop->individual_socket ) {
		NET_CloseSocket( &drop->socket );
	}

	if( drop->mv ) {
		sv.num_mv_clients--;
		drop->mv = false;
	}

	drop->state = CS_ZOMBIE;    // become free in a few seconds
	drop->name[0] = 0;
}


/*
============================================================

CLIENT COMMAND EXECUTION

============================================================
*/

/*
* SV_New_f
*
* Sends the first message from the server to a connected client.
* This will be sent on the initial connection and upon each server load.
*/
static void SV_New_f( client_t *client ) {
	int playernum;
	unsigned int numpure;
	purelist_t *purefile;
	edict_t *ent;
	int sv_bitflags = 0;

	Com_DPrintf( "New() from %s\n", client->name );

	// if in CS_AWAITING we have sent the response packet the new once already,
	// but client might have not got it so we send it again
	if( client->state >= CS_SPAWNED ) {
		Com_Printf( "New not valid -- already spawned\n" );
		return;
	}

	//
	// serverdata needs to go over for all types of servers
	// to make sure the protocol is right, and to set the gamedir
	//
	SV_InitClientMessage( client, &tmpMessage, NULL, 0 );

	// send the serverdata
	MSG_WriteUint8( &tmpMessage, svc_serverdata );
	MSG_WriteInt32( &tmpMessage, APP_PROTOCOL_VERSION );
	MSG_WriteInt32( &tmpMessage, svs.spawncount );
	MSG_WriteInt16( &tmpMessage, (unsigned short)svc.snapFrameTime );
	MSG_WriteString( &tmpMessage, FS_BaseGameDirectory() );
	MSG_WriteString( &tmpMessage, FS_GameDirectory() );

	playernum = client - svs.clients;
	MSG_WriteInt16( &tmpMessage, playernum );

	// send full levelname
	MSG_WriteString( &tmpMessage, sv.mapname );

	//
	// game server
	//
	if( sv.state == ss_game ) {
		// set up the entity for the client
		ent = EDICT_NUM( playernum + 1 );
		ent->s.number = playernum + 1;
		client->edict = ent;

		if( sv_pure->integer ) {
			sv_bitflags |= SV_BITFLAGS_PURE;
		}
		if( client->reliable ) {
			sv_bitflags |= SV_BITFLAGS_RELIABLE;
		}
		if( SV_Web_Running() ) {
			const char *baseurl = SV_Web_UpstreamBaseUrl();
			sv_bitflags |= SV_BITFLAGS_HTTP;
			if( baseurl[0] ) {
				sv_bitflags |= SV_BITFLAGS_HTTP_BASEURL;
			}
		}
		MSG_WriteUint8( &tmpMessage, sv_bitflags );
	}

	if( sv_bitflags & SV_BITFLAGS_HTTP ) {
		if( sv_bitflags & SV_BITFLAGS_HTTP_BASEURL ) {
			MSG_WriteString( &tmpMessage, sv_http_upstream_baseurl->string );
		} else {
			MSG_WriteInt16( &tmpMessage, sv_http_port->integer ); // HTTP port number
		}
	}

	// always write purelist
	numpure = Com_CountPureListFiles( svs.purelist );
	if( numpure > (short)0x7fff ) {
		Com_Error( ERR_DROP, "Error: Too many pure files." );
	}

	MSG_WriteInt16( &tmpMessage, numpure );

	purefile = svs.purelist;
	while( purefile ) {
		MSG_WriteString( &tmpMessage, purefile->filename );
		MSG_WriteInt32( &tmpMessage, purefile->checksum );
		purefile = purefile->next;
	}

	SV_ClientResetCommandBuffers( client );

	SV_SendMessageToClient( client, &tmpMessage );
	Netchan_PushAllFragments( &client->netchan );

	// don't let it send reliable commands until we get the first configstring request
	client->state = CS_CONNECTING;
}

/*
* SV_Configstrings_f
*/
static void SV_Configstrings_f( client_t *client ) {
	int start;

	if( client->state == CS_CONNECTING ) {
		Com_DPrintf( "Start Configstrings() from %s\n", client->name );
		client->state = CS_CONNECTED;
	} else {
		Com_DPrintf( "Configstrings() from %s\n", client->name );
	}

	if( client->state != CS_CONNECTED ) {
		Com_Printf( "configstrings not valid -- already spawned\n" );
		return;
	}

	// handle the case of a level changing while a client was connecting
	if( atoi( Cmd_Argv( 1 ) ) != svs.spawncount ) {
		Com_Printf( "SV_Configstrings_f from different level\n" );
		SV_SendServerCommand( client, "reconnect" );
		return;
	}

	start = atoi( Cmd_Argv( 2 ) );
	if( start < 0 ) {
		start = 0;
	}

	// write a packet full of data
	while( start < MAX_CONFIGSTRINGS &&
		   client->reliableSequence - client->reliableAcknowledge < MAX_RELIABLE_COMMANDS - 8 ) {
		if( sv.configstrings[start][0] ) {
			SV_SendServerCommand( client, "cs %i \"%s\"", start, sv.configstrings[start] );
		}
		start++;
	}

	// send next command
	if( start == MAX_CONFIGSTRINGS ) {
		SV_SendServerCommand( client, "cmd baselines %i 0", svs.spawncount );
	} else {
		SV_SendServerCommand( client, "cmd configstrings %i %i", svs.spawncount, start );
	}
}

/*
* SV_Baselines_f
*/
static void SV_Baselines_f( client_t *client ) {
	int start;
	entity_state_t nullstate;
	entity_state_t *base;

	Com_DPrintf( "Baselines() from %s\n", client->name );

	if( client->state != CS_CONNECTED ) {
		Com_Printf( "baselines not valid -- already spawned\n" );
		return;
	}

	// handle the case of a level changing while a client was connecting
	if( atoi( Cmd_Argv( 1 ) ) != svs.spawncount ) {
		Com_Printf( "SV_Baselines_f from different level\n" );
		SV_New_f( client );
		return;
	}

	start = atoi( Cmd_Argv( 2 ) );
	if( start < 0 ) {
		start = 0;
	}

	memset( &nullstate, 0, sizeof( nullstate ) );

	// write a packet full of data
	SV_InitClientMessage( client, &tmpMessage, NULL, 0 );

	while( tmpMessage.cursize < FRAGMENT_SIZE * 3 && start < MAX_EDICTS ) {
		base = &sv.baselines[start];
		if( base->modelindex || base->sound || base->effects ) {
			MSG_WriteUint8( &tmpMessage, svc_spawnbaseline );
			MSG_WriteDeltaEntity( &tmpMessage, &nullstate, base, true );
		}
		start++;
	}

	// send next command
	if( start == MAX_EDICTS ) {
		SV_SendServerCommand( client, "precache %i", svs.spawncount );
	} else {
		SV_SendServerCommand( client, "cmd baselines %i %i", svs.spawncount, start );
	}

	SV_AddReliableCommandsToMessage( client, &tmpMessage );
	SV_SendMessageToClient( client, &tmpMessage );
}

/*
* SV_Begin_f
*/
static void SV_Begin_f( client_t *client ) {
	Com_DPrintf( "Begin() from %s\n", client->name );

	// wsw : r1q2[start] : could be abused to respawn or cause spam/other mod-specific problems
	if( client->state != CS_CONNECTED ) {
		if( dedicated->integer ) {
			Com_Printf( "SV_Begin_f: 'Begin' from already spawned client: %s.\n", client->name );
		}
		SV_DropClient( client, DROP_TYPE_GENERAL, "Error: Begin while connected" );
		return;
	}
	// wsw : r1q2[end]

	// handle the case of a level changing while a client was connecting
	if( atoi( Cmd_Argv( 1 ) ) != svs.spawncount ) {
		Com_Printf( "SV_Begin_f from different level\n" );
		SV_SendServerCommand( client, "changing" );
		SV_SendServerCommand( client, "reconnect" );
		return;
	}

	client->state = CS_SPAWNED;

	// call the game begin function
	ge->ClientBegin( client->edict );
}

//=============================================================================


/*
* SV_NextDownload_f
*
* Responds to reliable nextdl packet with unreliable download packet
* If nextdl packet's offet information is negative, download will be stopped
*/
static void SV_NextDownload_f( client_t *client ) {
	int blocksize;
	int offset;
	uint8_t data[FRAGMENT_SIZE * 2];

	if( !client->download.name ) {
		Com_Printf( "nextdl message for client with no download active, from: %s\n", client->name );
		return;
	}

	if( Q_stricmp( client->download.name, Cmd_Argv( 1 ) ) ) {
		Com_Printf( "nextdl message for wrong filename, from: %s\n", client->name );
		return;
	}

	offset = atoi( Cmd_Argv( 2 ) );

	if( offset > client->download.size ) {
		Com_Printf( "nextdl message with too big offset, from: %s\n", client->name );
		return;
	}

	if( offset == -1 ) {
		Com_Printf( "Upload of %s to %s%s completed\n", client->download.name, client->name, S_COLOR_WHITE );
		SV_ClientCloseDownload( client );
		return;
	}

	if( offset < 0 ) {
		Com_Printf( "Upload of %s to %s%s failed\n", client->download.name, client->name, S_COLOR_WHITE );
		SV_ClientCloseDownload( client );
		return;
	}

	if( !client->download.file ) {
		Com_Printf( "Starting server upload of %s to %s\n", client->download.name, client->name );

		client->download.size = FS_FOpenBaseFile( client->download.name, &client->download.file, FS_READ );
		if( !client->download.file || client->download.size < 0 ) {
			Com_Printf( "Error opening %s for uploading\n", client->download.name );
			SV_ClientCloseDownload( client );
			return;
		}
	}

	SV_InitClientMessage( client, &tmpMessage, NULL, 0 );
	SV_AddReliableCommandsToMessage( client, &tmpMessage );

	blocksize = client->download.size - offset;
	// jalfixme: adapt download to user rate setting and sv_maxrate setting.
	if( blocksize > sizeof( data ) ) {
		blocksize = sizeof( data );
	}
	if( offset + blocksize > client->download.size ) {
		blocksize = client->download.size - offset;
	}
	if( blocksize < 0 ) {
		blocksize = 0;
	}

	if( blocksize > 0 ) {
		FS_Seek( client->download.file, offset, FS_SEEK_SET );
		blocksize = FS_Read( data, blocksize, client->download.file );
	}

	MSG_WriteUint8( &tmpMessage, svc_download );
	MSG_WriteString( &tmpMessage, client->download.name );
	MSG_WriteInt32( &tmpMessage, offset );
	MSG_WriteInt32( &tmpMessage, blocksize );
	if( blocksize > 0 ) {
		MSG_CopyData( &tmpMessage, data, blocksize );
	}
	SV_SendMessageToClient( client, &tmpMessage );

	client->download.timeout = svs.realtime + 10000;
}

/*
* SV_GameAllowDownload
* Asks game function whether to allow downloading of a file
*/
static bool SV_GameAllowDownload( client_t *client, const char *requestname, const char *uploadname ) {
	if( client->state < CS_SPAWNED ) {
		return false;
	}

	// allow downloading demos
	if( SV_IsDemoDownloadRequest( requestname ) ) {
		return sv_uploads_demos->integer != 0;
	}

	return false;
}

/*
* SV_DenyDownload
* Helper function for generating initdownload packets for denying download
*/
static void SV_DenyDownload( client_t *client, const char *reason ) {
	// size -1 is used to signal that it's refused
	// URL field is used for deny reason
	SV_InitClientMessage( client, &tmpMessage, NULL, 0 );
	SV_SendServerCommand( client, "initdownload \"%s\" %i %u %i \"%s\"", "", -1, 0, false, reason ? reason : "" );
	SV_AddReliableCommandsToMessage( client, &tmpMessage );
	SV_SendMessageToClient( client, &tmpMessage );
}

static bool SV_FilenameForDownloadRequest( const char *requestname, bool requestpak,
										   const char **uploadname, const char **errormsg ) {
	if( FS_CheckPakExtension( requestname ) ) {
		if( !requestpak ) {
			*errormsg = "Pak file requested as a non pak file";
			return false;
		}
		if( FS_FOpenBaseFile( requestname, NULL, FS_READ ) == -1 ) {
			*errormsg = "File not found";
			return false;
		}

		*uploadname = requestname;
	} else {
		if( FS_FOpenFile( requestname, NULL, FS_READ ) == -1 ) {
			*errormsg = "File not found";
			return false;
		}

		// check if file is inside a PAK
		if( requestpak ) {
			*uploadname = FS_PakNameForFile( requestname );
			if( !*uploadname ) {
				*errormsg = "File not available in pack";
				return false;
			}
		} else {
			*uploadname = FS_BaseNameForFile( requestname );
			if( !*uploadname ) {
				*errormsg = "File only available in pack";
				return false;
			}
		}
	}
	return true;
}

/*
* SV_BeginDownload_f
* Responds to reliable download packet with reliable initdownload packet
*/
static void SV_BeginDownload_f( client_t *client ) {
	const char *requestname;
	const char *uploadname;
	size_t alloc_size;
	unsigned checksum;
	char *url;
	const char *errormsg = NULL;
	bool allow, requestpak;
	bool local_http = SV_Web_Running() && sv_uploads_http->integer != 0;

	requestpak = ( atoi( Cmd_Argv( 1 ) ) == 1 );
	requestname = Cmd_Argv( 2 );

	if( !requestname[0] || !COM_ValidateRelativeFilename( requestname ) ) {
		SV_DenyDownload( client, "Invalid filename" );
		return;
	}

	if( !SV_FilenameForDownloadRequest( requestname, requestpak, &uploadname, &errormsg ) ) {
		assert( errormsg != NULL );
		SV_DenyDownload( client, errormsg );
		return;
	}

	if( FS_CheckPakExtension( uploadname ) ) {
		allow = false;

		// allow downloading paks from the pure list, if not spawned
		if( client->state < CS_SPAWNED ) {
			purelist_t *purefile;

			purefile = svs.purelist;
			while( purefile ) {
				if( !strcmp( uploadname, purefile->filename ) ) {
					allow = true;
					break;
				}
				purefile = purefile->next;
			}
		}

		// game module has a change to allow extra downloads
		if( !allow && !SV_GameAllowDownload( client, requestname, uploadname ) ) {
			SV_DenyDownload( client, "Downloading of this file is not allowed" );
			return;
		}
	} else {
		if( !SV_GameAllowDownload( client, requestname, uploadname ) ) {
			SV_DenyDownload( client, "Downloading of this file is not allowed" );
			return;
		}
	}

	// we will just overwrite old download, if any
	if( client->download.name ) {
		SV_ClientCloseDownload( client );
	}

	client->download.size = FS_LoadBaseFile( uploadname, NULL, NULL, 0 );
	if( client->download.size == -1 ) {
		Com_Printf( "Error getting size of %s for uploading\n", uploadname );
		client->download.size = 0;
		SV_DenyDownload( client, "Error getting file size" );
		return;
	}

	checksum = FS_ChecksumBaseFile( uploadname, false );
	client->download.timeout = svs.realtime + 1000 * 60 * 60; // this is web download timeout

	alloc_size = sizeof( char ) * ( strlen( uploadname ) + 1 );
	client->download.name = Mem_ZoneMalloc( alloc_size );
	Q_strncpyz( client->download.name, uploadname, alloc_size );

	Com_Printf( "Offering %s to %s\n", client->download.name, client->name );

	if( FS_CheckPakExtension( uploadname ) && ( local_http || sv_uploads_baseurl->string[0] != 0 ) ) {
		// .pk3 and .pak download from the web
		if( local_http ) {
			goto local_download;
		} else {
			alloc_size = sizeof( char ) * ( strlen( sv_uploads_baseurl->string ) + 1 );
			url = Mem_TempMalloc( alloc_size );
			Q_snprintfz( url, alloc_size, "%s/", sv_uploads_baseurl->string );
		}
	} else if( SV_IsDemoDownloadRequest( requestname ) && ( local_http || sv_uploads_demos_baseurl->string[0] != 0 ) ) {
		// demo file download from the web
		if( local_http ) {
local_download:
			alloc_size = sizeof( char ) * ( 6 + strlen( uploadname ) * 3 + 1 );
			url = Mem_TempMalloc( alloc_size );
			Q_snprintfz( url, alloc_size, "files/" );
			Q_urlencode_unsafechars( uploadname, url + 6, alloc_size - 6 );
		} else {
			alloc_size = sizeof( char ) * ( strlen( sv_uploads_demos_baseurl->string ) + 1 );
			url = Mem_TempMalloc( alloc_size );
			Q_snprintfz( url, alloc_size, "%s/", sv_uploads_demos_baseurl->string );
		}
	} else {
		url = NULL;
	}

	// start the download
	SV_InitClientMessage( client, &tmpMessage, NULL, 0 );
	SV_SendServerCommand( client, "initdownload \"%s\" %i %u %i \"%s\"", client->download.name,
						  client->download.size, checksum, local_http ? 1 : 0, ( url ? url : "" ) );
	SV_AddReliableCommandsToMessage( client, &tmpMessage );
	SV_SendMessageToClient( client, &tmpMessage );

	if( url ) {
		Mem_TempFree( url );
		url = NULL;
	}
}

//============================================================================


/*
* SV_Disconnect_f
* The client is going to disconnect, so remove the connection immediately
*/
static void SV_Disconnect_f( client_t *client ) {
	SV_DropClient( client, DROP_TYPE_GENERAL, NULL );
}


/*
* SV_ShowServerinfo_f
* Dumps the serverinfo info string
*/
static void SV_ShowServerinfo_f( client_t *client ) {
	Info_Print( Cvar_Serverinfo() );
}

/*
* SV_UserinfoCommand_f
*/
static void SV_UserinfoCommand_f( client_t *client ) {
	char *info;
	int64_t time;

	info = Cmd_Argv( 1 );
	if( !Info_Validate( info ) ) {
		SV_DropClient( client, DROP_TYPE_GENERAL, "%s", "Error: Invalid userinfo" );
		return;
	}

	time = Sys_Milliseconds();
	if( client->userinfoLatchTimeout > time ) {
		Q_strncpyz( client->userinfoLatched, info, sizeof( client->userinfo ) );
	} else {
		Q_strncpyz( client->userinfo, info, sizeof( client->userinfo ) );

		client->userinfoLatched[0] = '\0';
		client->userinfoLatchTimeout = time + USERINFO_UPDATE_COOLDOWN_MSEC;

		SV_UserinfoChanged( client );
	}
}

/*
* SV_NoDelta_f
*/
static void SV_NoDelta_f( client_t *client ) {
	client->nodelta = true;
	client->nodelta_frame = 0;
	client->lastframe = -1; // jal : I'm not sure about this. Seems like it's missing but...
}

/*
* SV_Multiview_f
*/
static void SV_Multiview_f( client_t *client ) {
	bool mv;

	mv = ( atoi( Cmd_Argv( 1 ) ) != 0 );

	if( client->mv == mv ) {
		return;
	}

	// (Temporarily) disallow multi-view clients.
	// It still seems to be useful in future
#if 0
	if( !ge->ClientMultiviewChanged( client->edict, mv ) ) {
		return;
	}

	if( mv ) {
		if( sv.num_mv_clients < sv_maxmvclients->integer ) {
			client->mv = true;
			sv.num_mv_clients++;
		} else {
			SV_AddGameCommand( client, "pr \"Can't multiview: maximum number of allowed multiview clients reached.\"" );
			return;
		}
	} else {
		assert( sv.num_mv_clients );
		client->mv = false;
		sv.num_mv_clients--;
	}
#endif
}

typedef struct {
	const char *name;
	void ( *func )( client_t *client );
} ucmd_t;

ucmd_t ucmds[] =
{
	// auto issued
	{ "new", SV_New_f },
	{ "configstrings", SV_Configstrings_f },
	{ "baselines", SV_Baselines_f },
	{ "begin", SV_Begin_f },
	{ "disconnect", SV_Disconnect_f },
	{ "usri", SV_UserinfoCommand_f },

	{ "nodelta", SV_NoDelta_f },

	{ "multiview", SV_Multiview_f },

	// issued by hand at client consoles
	{ "info", SV_ShowServerinfo_f },

	{ "download", SV_BeginDownload_f },
	{ "nextdl", SV_NextDownload_f },

	// server demo downloads
	{ "demolist", SV_DemoList_f },
	{ "demoget", SV_DemoGet_f },

	{ "svmotd", SV_MOTD_Get_f },

	{ NULL, NULL }
};

/*
* SV_ExecuteUserCommand
*/
static void SV_ExecuteUserCommand( client_t *client, const char *s ) {
	ucmd_t *u;

	Cmd_TokenizeString( s );

	for( u = ucmds; u->name; u++ ) {
		if( !strcmp( Cmd_Argv( 0 ), u->name ) ) {
			u->func( client );
			break;
		}
	}

	if( client->state >= CS_SPAWNED && !u->name && sv.state == ss_game ) {
		ge->ClientCommand( client->edict );
	}
}


/*
===========================================================================

USER CMD EXECUTION

===========================================================================
*/

/*
* SV_FindNextUserCommand - Returns the next valid usercmd_t in execution list
*/
usercmd_t *SV_FindNextUserCommand( client_t *client ) {
	usercmd_t *ucmd;
	int64_t higherTime = 0;
	unsigned int i;

	higherTime = svs.gametime; // ucmds can never have a higher timestamp than server time, unless cheating
	ucmd = NULL;
	if( client ) {
		for( i = client->UcmdExecuted + 1; i <= client->UcmdReceived; i++ ) {
			// skip backups if already executed
			if( client->UcmdTime >= client->ucmds[i & CMD_MASK].serverTimeStamp ) {
				continue;
			}

			if( ucmd == NULL || client->ucmds[i & CMD_MASK].serverTimeStamp < higherTime ) {
				higherTime = client->ucmds[i & CMD_MASK].serverTimeStamp;
				ucmd = &client->ucmds[i & CMD_MASK];
			}
		}
	}

	return ucmd;
}

/*
* SV_ExecuteClientThinks - Execute all pending usercmd_t
*/
void SV_ExecuteClientThinks( int clientNum ) {
	unsigned int msec;
	int64_t minUcmdTime;
	int timeDelta;
	client_t *client;
	usercmd_t *ucmd;

	if( clientNum >= sv_maxclients->integer || clientNum < 0 ) {
		return;
	}

	client = svs.clients + clientNum;
	if( client->state < CS_SPAWNED ) {
		return;
	}

	if( client->edict->r.svflags & SVF_FAKECLIENT ) {
		return;
	}

	// don't let client command time delay too far away in the past
	minUcmdTime = ( svs.gametime > 999 ) ? ( svs.gametime - 999 ) : 0;
	if( client->UcmdTime < minUcmdTime ) {
		client->UcmdTime = minUcmdTime;
	}

	while( ( ucmd = SV_FindNextUserCommand( client ) ) != NULL ) {
		msec = ucmd->serverTimeStamp - client->UcmdTime;
		clamp( msec, 1, 200 );
		ucmd->msec = msec;
		timeDelta = 0;
		if( client->lastframe > 0 ) {
			timeDelta = -(int)( svs.gametime - ucmd->serverTimeStamp );
		}

		ge->ClientThink( client->edict, ucmd, timeDelta );

		client->UcmdTime = ucmd->serverTimeStamp;
	}

	// we did the entire update
	client->UcmdExecuted = client->UcmdReceived;
}

/*
* SV_ParseMoveCommand
*/
static void SV_ParseMoveCommand( client_t *client, msg_t *msg ) {
	unsigned int i, ucmdHead, ucmdFirst, ucmdCount;
	usercmd_t nullcmd;
	int lastframe;

	lastframe = MSG_ReadInt32( msg );

	// read the id of the first ucmd we will receive
	ucmdHead = (unsigned int)MSG_ReadInt32( msg );
	// read the number of ucmds we will receive
	ucmdCount = (unsigned int)MSG_ReadUint8( msg );

	if( ucmdCount > CMD_MASK ) {
		SV_DropClient( client, DROP_TYPE_GENERAL, "%s", "Error: Ucmd overflow" );
		return;
	}

	ucmdFirst = ucmdHead > ucmdCount ? ucmdHead - ucmdCount : 0;
	client->UcmdReceived = ucmdHead < 1 ? 0 : ucmdHead - 1;

	// read the user commands
	for( i = ucmdFirst; i < ucmdHead; i++ ) {
		if( i == ucmdFirst ) { // first one isn't delta compressed
			memset( &nullcmd, 0, sizeof( nullcmd ) );
			// jalfixme: check for too old overflood
			MSG_ReadDeltaUsercmd( msg, &nullcmd, &client->ucmds[i & CMD_MASK] );
		} else {
			MSG_ReadDeltaUsercmd( msg, &client->ucmds[( i - 1 ) & CMD_MASK], &client->ucmds[i & CMD_MASK] );
		}
	}

	if( client->state != CS_SPAWNED ) {
		client->lastframe = -1;
		return;
	}

	// calc ping
	if( lastframe != client->lastframe ) {
		client->lastframe = lastframe;
		if( client->lastframe > 0 ) {
			// FIXME: Medar: ping is in gametime, should be in realtime
			//client->frame_latency[client->lastframe&(LATENCY_COUNTS-1)] = svs.gametime - (client->frames[client->lastframe & UPDATE_MASK].sentTimeStamp;
			// this is more accurate. A little bit hackish, but more accurate
			client->frame_latency[client->lastframe & ( LATENCY_COUNTS - 1 )] = svs.gametime - ( client->ucmds[client->UcmdReceived & CMD_MASK].serverTimeStamp + svc.snapFrameTime );
		}
	}
}

/*
* SV_ParseClientMessage
* The current message is parsed for the given client
*/
void SV_ParseClientMessage( client_t *client, msg_t *msg ) {
	char *s;
	bool move_issued;
	int64_t cmdNum;

	if( !msg ) {
		return;
	}

	SV_UpdateActivity();

	// only allow one move command
	move_issued = false;
	while( msg->readcount < msg->cursize ) {
		int c;
		
		c = MSG_ReadUint8( msg );
		switch( c ) {
			default:
				Com_Printf( "SV_ParseClientMessage: unknown command char\n" );
				SV_DropClient( client, DROP_TYPE_GENERAL, "%s", "Error: Unknown command char" );
				return;

			case clc_nop:
				break;

			case clc_move:
			{
				if( move_issued ) {
					return; // someone is trying to cheat...

				}
				move_issued = true;
				SV_ParseMoveCommand( client, msg );
			}
			break;

			case clc_svcack:
			{
				if( client->reliable ) {
					Com_Printf( "SV_ParseClientMessage: svack from reliable client\n" );
					SV_DropClient( client, DROP_TYPE_GENERAL, "%s", "Error: svack from reliable client" );
					return;
				}
				cmdNum = MSG_ReadIntBase128( msg );
				if( cmdNum < client->reliableAcknowledge || cmdNum > client->reliableSent ) {
					//SV_DropClient( client, DROP_TYPE_GENERAL, "%s", "Error: bad server command acknowledged" );
					return;
				}
				client->reliableAcknowledge = cmdNum;
			}
			break;

			case clc_clientcommand:
				if( !client->reliable ) {
					cmdNum = MSG_ReadIntBase128( msg );
					if( cmdNum <= client->clientCommandExecuted ) {
						s = MSG_ReadString( msg ); // read but ignore
						continue;
					}
					client->clientCommandExecuted = cmdNum;
				}
				s = MSG_ReadString( msg );
				SV_ExecuteUserCommand( client, s );
				if( client->state == CS_ZOMBIE ) {
					return; // disconnect command
				}
				break;

			case clc_extension:
				if( 1 ) {
					int ext, len;

					ext = MSG_ReadUint8( msg );  // extension id
					MSG_ReadUint8( msg );        // version number
					len = MSG_ReadInt16( msg ); // command length

					switch( ext ) {
						default:
							// unsupported
							MSG_SkipData( msg, len );
							break;
					}
				}
				break;
		}
	}

	if( msg->readcount > msg->cursize ) {
		Com_Printf( "SV_ParseClientMessage: badread\n" );
		SV_DropClient( client, DROP_TYPE_GENERAL, "%s", "Error: Bad message" );
		return;
	}
}
