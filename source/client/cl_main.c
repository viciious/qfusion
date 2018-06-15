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
// cl_main.c  -- client main loop

#include "client.h"
#include "ftlib.h"
#include "../qcommon/asyncstream.h"
#include "../qalgo/hash.h"

cvar_t *cl_stereo_separation;
cvar_t *cl_stereo;

cvar_t *rcon_client_password;
cvar_t *rcon_address;

cvar_t *cl_timeout;
cvar_t *cl_maxfps;
cvar_t *cl_sleep;
cvar_t *cl_pps;
cvar_t *cl_shownet;

cvar_t *cl_extrapolationTime;
cvar_t *cl_extrapolate;

cvar_t *cl_timedemo;
cvar_t *cl_demoavi_video;
cvar_t *cl_demoavi_audio;
cvar_t *cl_demoavi_fps;
cvar_t *cl_demoavi_scissor;

cvar_t *sensitivity;
cvar_t *zoomsens;
cvar_t *m_accel;
cvar_t *m_accelStyle;
cvar_t *m_accelOffset;
cvar_t *m_accelPow;
cvar_t *m_filter;
cvar_t *m_filterStrength;
cvar_t *m_sensCap;

cvar_t *m_pitch;
cvar_t *m_yaw;

//
// userinfo
//
cvar_t *info_password;
cvar_t *rate;

cvar_t *cl_masterservers;

// wsw : debug netcode
cvar_t *cl_debug_serverCmd;
cvar_t *cl_debug_timeDelta;

cvar_t *cl_downloads;
cvar_t *cl_downloads_from_web;
cvar_t *cl_downloads_from_web_timeout;
cvar_t *cl_download_allow_modules;
cvar_t *cl_checkForUpdate;


static char cl_nextString[MAX_STRING_CHARS];
static char cl_connectChain[MAX_STRING_CHARS];

client_static_t cls;
client_state_t cl;

entity_state_t cl_baselines[MAX_EDICTS];

static bool cl_initialized = false;

static async_stream_module_t *cl_async_stream;

//======================================================================


/*
=======================================================================

CLIENT RELIABLE COMMAND COMMUNICATION

=======================================================================
*/

/*
* CL_AddReliableCommand
*
* The given command will be transmitted to the server, and is gauranteed to
* not have future usercmd_t executed before it is executed
*/
void CL_AddReliableCommand( /*const*/ char *cmd ) {
	int index;

	if( !cmd || !strlen( cmd ) ) {
		return;
	}

	// if we would be losing an old command that hasn't been acknowledged,
	// we must drop the connection
	if( cls.reliableSequence > cls.reliableAcknowledge + MAX_RELIABLE_COMMANDS ) {
		cls.reliableAcknowledge = cls.reliableSequence; // try to avoid loops
		Com_Error( ERR_DROP, "Client command overflow %i %i", cls.reliableAcknowledge, cls.reliableSequence );
	}
	cls.reliableSequence++;
	index = cls.reliableSequence & ( MAX_RELIABLE_COMMANDS - 1 );
	Q_strncpyz( cls.reliableCommands[index], cmd, sizeof( cls.reliableCommands[index] ) );
}

/*
* CL_UpdateClientCommandsToServer
*
* Add the pending commands to the message
*/
void CL_UpdateClientCommandsToServer( msg_t *msg ) {
	int64_t i;

	// write any unacknowledged clientCommands
	for( i = cls.reliableAcknowledge + 1; i <= cls.reliableSequence; i++ ) {
		if( !strlen( cls.reliableCommands[i & ( MAX_RELIABLE_COMMANDS - 1 )] ) ) {
			continue;
		}

		MSG_WriteUint8( msg, clc_clientcommand );
		if( !cls.reliable ) {
			MSG_WriteIntBase128( msg, i );
		}
		MSG_WriteString( msg, cls.reliableCommands[i & ( MAX_RELIABLE_COMMANDS - 1 )] );
	}

	cls.reliableSent = cls.reliableSequence;
	if( cls.reliable ) {
		cls.reliableAcknowledge = cls.reliableSent;
	}
}

/*
* CL_ForwardToServer_f
*/
void CL_ForwardToServer_f( void ) {
	if( cls.demo.playing ) {
		return;
	}

	if( cls.state != CA_CONNECTED && cls.state != CA_ACTIVE ) {
		Com_Printf( "Can't \"%s\", not connected\n", Cmd_Argv( 0 ) );
		return;
	}

	// don't forward the first argument
	if( Cmd_Argc() > 1 ) {
		CL_AddReliableCommand( Cmd_Args() );
	}
}

/*
* CL_ServerDisconnect_f
*/
void CL_ServerDisconnect_f( void ) {
	char menuparms[MAX_STRING_CHARS];
	int type;
	char reason[MAX_STRING_CHARS];

	type = atoi( Cmd_Argv( 1 ) );
	if( type < 0 || type >= DROP_TYPE_TOTAL ) {
		type = DROP_TYPE_GENERAL;
	}

	Q_strncpyz( reason, Cmd_Argv( 2 ), sizeof( reason ) );

	CL_Disconnect_f();

	Com_Printf( "Connection was closed by server: %s\n", reason );

	Q_snprintfz( menuparms, sizeof( menuparms ), "menu_open connfailed dropreason %i servername \"%s\" droptype %i rejectmessage \"%s\"",
				 DROP_REASON_CONNTERMINATED, cls.servername, type, reason );

	Cbuf_ExecuteText( EXEC_NOW, menuparms );
}

/*
* CL_Quit
*/
void CL_Quit( void ) {
	CL_Disconnect( NULL );
	Com_Quit();
}

/*
* CL_Quit_f
*/
static void CL_Quit_f( void ) {
	CL_Quit();
}

/*
* CL_SendConnectPacket
*
* We have gotten a challenge from the server, so try and
* connect.
*/
static void CL_SendConnectPacket( void ) {
	char uuid_buffer[UUID_BUFFER_SIZE];

	userinfo_modified = false;

	Uuid_ToString( uuid_buffer, cls.mm_ticket );
	Com_DPrintf( "CL_MM_Initialized: %d, cls.mm_ticket: %s\n", CL_MM_Initialized(), uuid_buffer );
	Netchan_OutOfBandPrint( cls.socket, &cls.serveraddress, "connect %i %i %i \"%s\" %i %s\n",
							APP_PROTOCOL_VERSION, Netchan_GamePort(), cls.challenge, Cvar_Userinfo(), 0, uuid_buffer );
}

/*
* CL_CheckForResend
*
* Resend a connect message if the last one has timed out
*/
static void CL_CheckForResend( void ) {
	// FIXME: should use cls.realtime, but it can be old here after starting a server
	int64_t realtime = Sys_Milliseconds();

	if( cls.demo.playing ) {
		return;
	}

	// if the local server is running and we aren't then connect
	if( cls.state == CA_DISCONNECTED && Com_ServerState() ) {
		CL_SetClientState( CA_CONNECTING );
		if( cls.servername ) {
			Mem_ZoneFree( cls.servername );
		}
		cls.servername = ZoneCopyString( "localhost" );
		cls.servertype = SOCKET_LOOPBACK;
		NET_InitAddress( &cls.serveraddress, NA_LOOPBACK );
		if( !NET_OpenSocket( &cls.socket_loopback, cls.servertype, &cls.serveraddress, false ) ) {
			Com_Error( ERR_FATAL, "Couldn't open the loopback socket\n" );
			return;
		}
		cls.socket = &cls.socket_loopback;
	}

	// resend if we haven't gotten a reply yet
	if( cls.state == CA_CONNECTING && !cls.reliable ) {
		if( realtime - cls.connect_time < 3000 ) {
			return;
		}
		if( cls.connect_count > 3 ) {
			CL_Disconnect( "Connection timed out" );
			return;
		}
		cls.connect_count++;
		cls.connect_time = realtime; // for retransmit requests

		Com_Printf( "Connecting to %s...\n", cls.servername );

		Netchan_OutOfBandPrint( cls.socket, &cls.serveraddress, "getchallenge\n" );
	}

	if( cls.state == CA_CONNECTING && cls.reliable ) {
		if( realtime - cls.connect_time < 3000 ) {
			return;
		}

#ifdef TCP_ALLOW_CONNECT
		if( cls.socket->type == SOCKET_TCP && !cls.socket->connected ) {
			connection_status_t status;

			if( !cls.connect_count ) {
				Com_Printf( "Connecting to %s...\n", cls.servername );

				status = NET_Connect( cls.socket, &cls.serveraddress );
			} else {
				Com_Printf( "Checking connection to %s...\n", cls.servername );

				status = NET_CheckConnect( cls.socket );
			}

			cls.connect_count++;
			cls.connect_time = realtime;

			if( status == CONNECTION_FAILED ) {
				CL_Disconnect( va( "TCP connection failed: %s", NET_ErrorString() ) );
				return;
			}

			if( status == CONNECTION_INPROGRESS ) {
				return;
			}

			Com_Printf( "Connection made, asking for challenge %s...\n", cls.servername );
			Netchan_OutOfBandPrint( cls.socket, &cls.serveraddress, "getchallenge\n" );
			return;
		}
#endif

		if( realtime - cls.connect_time < 10000 ) {
			return;
		}

		CL_Disconnect( "Connection timed out" );
	}
}

/*
* CL_Connect
*/
static void CL_Connect( const char *servername, socket_type_t type, netadr_t *address, const char *serverchain ) {
	netadr_t socketaddress;
	connstate_t newstate;

	cl_connectChain[0] = '\0';
	cl_nextString[0] = '\0';

	CL_Disconnect( NULL );

	switch( type ) {
		case SOCKET_LOOPBACK:
			NET_InitAddress( &socketaddress, NA_LOOPBACK );
			if( !NET_OpenSocket( &cls.socket_loopback, SOCKET_LOOPBACK, &socketaddress, false ) ) {
				Com_Error( ERR_FATAL, "Couldn't open the loopback socket: %s\n", NET_ErrorString() ); // FIXME
				return;
			}
			cls.socket = &cls.socket_loopback;
			cls.reliable = false;
			break;

		case SOCKET_UDP:
			cls.socket = ( address->type == NA_IP6 ?  &cls.socket_udp6 :  &cls.socket_udp );
			cls.reliable = false;
			break;

#ifdef TCP_ALLOW_CONNECT
		case SOCKET_TCP:
			NET_InitAddress( &socketaddress, address->type );
			if( !NET_OpenSocket( &cls.socket_tcp, SOCKET_TCP, &socketaddress, false ) ) {
				Com_Error( ERR_FATAL, "Couldn't open the TCP socket\n" ); // FIXME
				return;
			}
			NET_SetSocketNoDelay( &cls.socket_tcp, 1 );
			cls.socket = &cls.socket_tcp;
			cls.reliable = true;
			break;
#endif

		default:
			assert( false );
			return;
	}

	cls.servertype = type;
	cls.serveraddress = *address;
	if( NET_GetAddressPort( &cls.serveraddress ) == 0 ) {
		NET_SetAddressPort( &cls.serveraddress, PORT_SERVER );
	}

	if( cls.servername ) {
		Mem_ZoneFree( cls.servername );
	}
	cls.servername = ZoneCopyString( servername );

	memset( cl.configstrings, 0, sizeof( cl.configstrings ) );

	// If the server supports matchmaking and that we are authenticated, try getting a matchmaking ticket before joining the server
	newstate = CA_CONNECTING;
	if( CL_MM_Initialized() ) {
		// if( MM_GetStatus() == MM_STATUS_AUTHENTICATED && CL_MM_GetTicket( serversession ) )
		if( CL_MM_Connect( &cls.serveraddress ) ) {
			newstate = CA_GETTING_TICKET;
		}
	}
	CL_SetClientState( newstate );

	if( serverchain[0] ) {
		Q_strncpyz( cl_connectChain, serverchain, sizeof( cl_connectChain ) );
	}

	cls.connect_time = -99999; // CL_CheckForResend() will fire immediately
	cls.connect_count = 0;
	cls.rejected = false;
	cls.lastPacketReceivedTime = cls.realtime; // reset the timeout limit
	cls.mv = false;
}

/*
* CL_Connect_Cmd_f
*/
static void CL_Connect_Cmd_f( socket_type_t socket ) {
	netadr_t serveraddress;
	char *servername, password[64], autowatch[64] = { 0 };
	const char *extension;
	char *connectstring, *connectstring_base;
	const char *tmp, *scheme = APP_URI_SCHEME, *proto_scheme = APP_URI_PROTO_SCHEME;
	const char *serverchain;

	if( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: %s <server>\n", Cmd_Argv( 0 ) );
		return;
	}

	connectstring_base = TempCopyString( Cmd_Argv( 1 ) );
	connectstring = connectstring_base;
	serverchain = Cmd_Argc() >= 3 ? Cmd_Argv( 2 ) : "";

	if( !Q_strnicmp( connectstring, proto_scheme, strlen( proto_scheme ) ) ) {
		connectstring += strlen( proto_scheme );
	} else if( !Q_strnicmp( connectstring, scheme, strlen( scheme ) ) ) {
		connectstring += strlen( scheme );
	}

	extension = COM_FileExtension( connectstring );
	if( extension && !Q_stricmp( extension, APP_DEMO_EXTENSION_STR ) ) {
		char *temp;
		size_t temp_size;
		const char *http_scheme = "http://";

		if( !Q_strnicmp( connectstring, http_scheme, strlen( http_scheme ) ) ) {
			connectstring += strlen( http_scheme );
		}

		temp_size = strlen( "demo " ) + strlen( http_scheme ) + strlen( connectstring ) + 1;
		temp = Mem_TempMalloc( temp_size );
		Q_snprintfz( temp, temp_size, "demo %s%s", http_scheme, connectstring );

		Cbuf_ExecuteText( EXEC_NOW, temp );

		Mem_TempFree( temp );
		Mem_TempFree( connectstring_base );
		return;
	}

	if( ( tmp = Q_strrstr( connectstring, "@" ) ) != NULL ) {
		Q_strncpyz( password, connectstring, min( sizeof( password ),( tmp - connectstring + 1 ) ) );
		Cvar_Set( "password", password );
		connectstring = connectstring + ( tmp - connectstring ) + 1;
	}

	if( ( tmp = Q_strrstr( connectstring, "#" ) ) != NULL ) {
		Q_strncpyz( autowatch, COM_RemoveColorTokens( tmp + 1 ), sizeof( autowatch ) );
		connectstring[tmp - connectstring] = '\0';
	}

	if( ( tmp = Q_strrstr( connectstring, "/" ) ) != NULL ) {
		connectstring[tmp - connectstring] = '\0';
	}

	Cvar_ForceSet( "autowatch", autowatch );

	if( !NET_StringToAddress( connectstring, &serveraddress ) ) {
		Mem_TempFree( connectstring_base );
		Com_Printf( "Bad server address\n" );
		return;
	}

	// wait until MM allows us to connect to a server
	// (not in a middle of login process or anything)
	CL_MM_WaitForLogin();

	servername = TempCopyString( connectstring );
	CL_Connect( servername, ( serveraddress.type == NA_LOOPBACK ? SOCKET_LOOPBACK : socket ),
				&serveraddress, serverchain );

	Mem_TempFree( servername );
	Mem_TempFree( connectstring_base );
}

/*
* CL_Connect_f
*/
static void CL_Connect_f( void ) {
	CL_Connect_Cmd_f( SOCKET_UDP );
}

/*
* CL_TCPConnect_f
*/
#if defined( TCP_ALLOW_CONNECT )
static void CL_TCPConnect_f( void ) {
	CL_Connect_Cmd_f( SOCKET_TCP );
}
#endif


/*
* CL_Rcon_f
*
* Send the rest of the command line over as
* an unconnected command.
*/
static void CL_Rcon_f( void ) {
	char message[1024];
	int i;
	const socket_t *socket;
	const netadr_t *address;

	if( cls.demo.playing ) {
		return;
	}

	if( rcon_client_password->string[0] == '\0' ) {
		Com_Printf( "You must set 'rcon_password' before issuing an rcon command.\n" );
		return;
	}

	// wsw : jal : check for msg len abuse (thx to r1Q2)
	if( strlen( Cmd_Args() ) + strlen( rcon_client_password->string ) + 16 >= sizeof( message ) ) {
		Com_Printf( "Length of password + command exceeds maximum allowed length.\n" );
		return;
	}

	message[0] = (uint8_t)255;
	message[1] = (uint8_t)255;
	message[2] = (uint8_t)255;
	message[3] = (uint8_t)255;
	message[4] = 0;

	Q_strncatz( message, "rcon ", sizeof( message ) );

	Q_strncatz( message, rcon_client_password->string, sizeof( message ) );
	Q_strncatz( message, " ", sizeof( message ) );

	for( i = 1; i < Cmd_Argc(); i++ ) {
		Q_strncatz( message, "\"", sizeof( message ) );
		Q_strncatz( message, Cmd_Argv( i ), sizeof( message ) );
		Q_strncatz( message, "\" ", sizeof( message ) );
	}

	if( cls.state >= CA_CONNECTED ) {
		socket = cls.netchan.socket;
		address = &cls.netchan.remoteAddress;
	} else {
		if( !strlen( rcon_address->string ) ) {
			Com_Printf( "You must be connected, or set the 'rcon_address' cvar to issue rcon commands\n" );
			return;
		}

		if( rcon_address->modified ) {
			if( !NET_StringToAddress( rcon_address->string, &cls.rconaddress ) ) {
				Com_Printf( "Bad rcon_address.\n" );
				return; // we don't clear modified, so it will whine the next time too
			}
			if( NET_GetAddressPort( &cls.rconaddress ) == 0 ) {
				NET_SetAddressPort( &cls.rconaddress, PORT_SERVER );
			}

			rcon_address->modified = false;
		}

		socket = ( cls.rconaddress.type == NA_IP6 ? &cls.socket_udp6 : &cls.socket_udp );
		address = &cls.rconaddress;
	}

	NET_SendPacket( socket, message, (int)strlen( message ) + 1, address );
}

/*
* CL_GetClipboardData
*/
char *CL_GetClipboardData( void ) {
	return Sys_GetClipboardData();
}

/*
* CL_SetClipboardData
*/
void CL_SetClipboardData( const char *data ) {
	Sys_SetClipboardData( data );
}

/*
* CL_FreeClipboardData
*/
void CL_FreeClipboardData( char *data ) {
	Sys_FreeClipboardData( data );
}

/*
* CL_IsBrowserAvailable
*/
bool CL_IsBrowserAvailable( void ) {
	return Sys_IsBrowserAvailable();
}

/*
* CL_OpenURLInBrowser
*/
void CL_OpenURLInBrowser( const char *url ) {
	Sys_OpenURLInBrowser( url );
}

/*
* CL_GetKeyDest
*/
keydest_t CL_GetKeyDest( void ) {
	return cls.key_dest;
}

/*
* CL_SetKeyDest
*/
void CL_SetKeyDest( keydest_t key_dest ) {
	if( key_dest < key_game || key_dest > key_delegate ) {
		Com_Error( ERR_DROP, "CL_SetKeyDest: invalid key_dest" );
	}

	if( cls.key_dest != key_dest ) {
		CL_ClearInputState();
		cls.key_dest = key_dest;
		Con_SetMessageMode();
	}
}

/*
* CL_SetOldKeyDest
*/
void CL_SetOldKeyDest( keydest_t key_dest ) {
	if( key_dest < key_game || key_dest > key_delegate ) {
		Com_Error( ERR_DROP, "CL_SetKeyDest: invalid key_dest" );
	}
	cls.old_key_dest = key_dest;
}

/*
* CL_GetBaseServerURL
*/
size_t CL_GetBaseServerURL( char *buffer, size_t buffer_size ) {
	const char *web_url = cls.httpbaseurl;

	if( !buffer || !buffer_size ) {
		return 0;
	}
	if( !web_url || !*web_url ) {
		*buffer = '\0';
		return 0;
	}

	Q_strncpyz( buffer, web_url, buffer_size );
	return strlen( web_url );
}

/*
* CL_ResetServerCount
*/
void CL_ResetServerCount( void ) {
	cl.servercount = -1;
}

/*
* CL_BeginRegistration
*/
static void CL_BeginRegistration( void ) {
	if( cls.registrationOpen ) {
		return;
	}

	cls.registrationOpen = true;

	re.BeginRegistration();
	CL_SoundModule_BeginRegistration();
}

/*
* CL_EndRegistration
*/
static void CL_EndRegistration( void ) {
	if( !cls.registrationOpen ) {
		return;
	}

	cls.registrationOpen = false;

	FTLIB_TouchAllFonts();
	CL_UIModule_TouchAllAssets();
	re.EndRegistration();
	CL_SoundModule_EndRegistration();
}

/*
* CL_ClearState
*/
void CL_ClearState( void ) {
	if( cl.cms ) {
		CM_ReleaseReference( cl.cms );
		cl.cms = NULL;
	}

	if( cl.frames_areabits ) {
		Mem_Free( cl.frames_areabits );
		cl.frames_areabits = NULL;
	}

	if( cl.cmds ) {
		Mem_Free( cl.cmds );
		cl.cmds = NULL;
	}

	if( cl.cmd_time ) {
		Mem_Free( cl.cmd_time );
		cl.cmd_time = NULL;
	}

	if( cl.snapShots ) {
		Mem_Free( cl.snapShots );
		cl.snapShots = NULL;
	}

	// wipe the entire cl structure
	memset( &cl, 0, sizeof( client_state_t ) );
	memset( cl_baselines, 0, sizeof( cl_baselines ) );

	cl.cmds = Mem_ZoneMalloc( sizeof( *cl.cmds ) * CMD_BACKUP );
	cl.cmd_time = Mem_ZoneMalloc( sizeof( *cl.cmd_time ) * CMD_BACKUP );
	cl.snapShots = Mem_ZoneMalloc( sizeof( *cl.snapShots ) * CMD_BACKUP );

	//userinfo_modified = true;
	cls.lastExecutedServerCommand = 0;
	cls.reliableAcknowledge = 0;
	cls.reliableSequence = 0;
	cls.reliableSent = 0;
	memset( cls.reliableCommands, 0, sizeof( cls.reliableCommands ) );
	// reset ucmds buffer
	cls.ucmdHead = 0;
	cls.ucmdSent = 0;
	cls.ucmdAcknowledged = 0;

	//restart realtime and lastPacket times
	cls.realtime = 0;
	cls.gametime = 0;
	cls.lastPacketSentTime = 0;
	cls.lastPacketReceivedTime = 0;

	if( cls.wakelock ) {
		Sys_ReleaseWakeLock( cls.wakelock );
		cls.wakelock = NULL;
	}
}


/*
* CL_SetNext_f
*
* Next is used to set an action which is executed at disconnecting.
*/
static void CL_SetNext_f( void ) {
	if( Cmd_Argc() < 2 ) {
		Com_Printf( "USAGE: next <commands>\n" );
		return;
	}

	// jalfixme: I'm afraid of this being too powerful, since it basically
	// is allowed to execute everything. Shall we check for something?
	Q_strncpyz( cl_nextString, Cmd_Args(), sizeof( cl_nextString ) );
	Com_Printf( "NEXT: %s\n", cl_nextString );
}


/*
* CL_ExecuteNext
*/
static void CL_ExecuteNext( void ) {
	if( !strlen( cl_nextString ) ) {
		return;
	}

	Cbuf_ExecuteText( EXEC_APPEND, cl_nextString );
	memset( cl_nextString, 0, sizeof( cl_nextString ) );
}

/*
* CL_Disconnect_SendCommand
*
* Sends a disconnect message to the server
*/
static void CL_Disconnect_SendCommand( void ) {
	// wsw : jal : send the packet 3 times to make sure isn't lost
	CL_AddReliableCommand( "disconnect" );
	CL_SendMessagesToServer( true );
	CL_AddReliableCommand( "disconnect" );
	CL_SendMessagesToServer( true );
	CL_AddReliableCommand( "disconnect" );
	CL_SendMessagesToServer( true );
}

/*
* CL_Disconnect
*
* Goes from a connected state to full screen console state
* Sends a disconnect message to the server
* This is also called on Com_Error, so it shouldn't cause any errors
*/
void CL_Disconnect( const char *message ) {
	char menuparms[MAX_STRING_CHARS];
	bool wasconnecting;

	// We have to shut down webdownloading first
	if( cls.download.web && !cls.download.disconnect ) {
		cls.download.disconnect = true;
		return;
	}

	if( cls.state == CA_UNINITIALIZED ) {
		return;
	}
	if( cls.state == CA_DISCONNECTED ) {
		goto done;
	}

	if( cls.state < CA_CONNECTED ) {
		wasconnecting = true;
	} else {
		wasconnecting = false;
	}

	SV_ShutdownGame( "Owner left the listen server", false );

	if( cl_timedemo && cl_timedemo->integer ) {
		int i;
		int64_t sumcounts = 0;

		Com_Printf( "\n" );
		for( i = 1; i < 100; i++ ) {
			if( cl.timedemo.counts[i] > 0 ) {
				float fps, perc;
				
				fps = 1000.0 / i;
				perc = cl.timedemo.counts[i] * 100.0 / cl.timedemo.frames;
				sumcounts += i * cl.timedemo.counts[i];

				Com_Printf( "%2ims - %7.2ffps: %6.2f%%\n", i, fps, perc );
			}
		}

		Com_Printf( "\n" );
		if( time > 0 ) {
			float mean = 1000.0 / (double)sumcounts * cl.timedemo.frames;
			int64_t duration = Sys_Milliseconds() - cl.timedemo.startTime;
			Com_Printf( "%3.1f seconds: %3.1f mean fps\n", duration / 1000.0, mean );
		}
	}

	cls.connect_time = 0;
	cls.connect_count = 0;
	cls.rejected = false;

	if( cls.demo.recording ) {
		CL_Stop_f();
	}

	if( cls.state == CA_CINEMATIC ) {
		SCR_StopCinematic();
	} else if( cls.demo.playing ) {
		CL_DemoCompleted();
	} else {
		CL_Disconnect_SendCommand(); // send a disconnect message to the server

	}
	FS_RemovePurePaks();

	Com_FreePureList( &cls.purelist );

	cls.sv_pure = false;

	// udp is kept open all the time, for connectionless messages
	if( cls.socket && cls.socket->type != SOCKET_UDP ) {
		NET_CloseSocket( cls.socket );
	}

	cls.socket = NULL;
	cls.reliable = false;
	cls.mv = false;

	if( cls.httpbaseurl ) {
		Mem_Free( cls.httpbaseurl );
		cls.httpbaseurl = NULL;
	}

	re.Finish();

	CL_EndRegistration();

	CL_RestartMedia();

	CL_Mumble_Unlink();

	CL_ClearState();
	CL_SetClientState( CA_DISCONNECTED );

	if( cls.download.requestname ) {
		cls.download.pending_reconnect = false;
		cls.download.cancelled = true;
		CL_DownloadDone();
	}

	if( cl_connectChain[0] == '\0' ) {
		if( message != NULL ) {
			Q_snprintfz( menuparms, sizeof( menuparms ), "menu_open connfailed dropreason %i servername \"%s\" droptype %i rejectmessage \"%s\"",
						 ( wasconnecting ? DROP_REASON_CONNFAILED : DROP_REASON_CONNERROR ), cls.servername, DROP_TYPE_GENERAL, message );

			Cbuf_ExecuteText( EXEC_NOW, menuparms );
		}
	} else {
		const char *s = strchr( cl_connectChain, ',' );
		if( s ) {
			cl_connectChain[s - cl_connectChain] = '\0';
		} else {
			s = cl_connectChain + strlen( cl_connectChain ) - 1;
		}
		Q_snprintfz( cl_nextString, sizeof( cl_nextString ), "connect \"%s\" \"%s\"", cl_connectChain, s + 1 );
	}

done:
	SCR_EndLoadingPlaque(); // get rid of loading plaque

	// in case we disconnect while in download phase
	CL_FreeDownloadList();

	CL_ExecuteNext(); // start next action if any is defined
}

void CL_Disconnect_f( void ) {
	cl_connectChain[0] = '\0';
	cl_nextString[0] = '\0';

	// We have to shut down webdownloading first
	if( cls.download.web ) {
		cls.download.disconnect = true;
		return;
	}

	CL_Disconnect( NULL );
}

/*
* CL_Changing_f
*
* Just sent as a hint to the client that they should
* drop to full console
*/
void CL_Changing_f( void ) {
	//ZOID
	//if we are downloading, we don't change!  This so we don't suddenly stop downloading a map
	if( cls.download.filenum || cls.download.web ) {
		return;
	}

	if( cls.demo.recording ) {
		CL_Stop_f();
	}

	Com_DPrintf( "CL:Changing\n" );

	memset( cl.configstrings, 0, sizeof( cl.configstrings ) );

	// ignore snapshots from previous connection
	cl.pendingSnapNum = cl.currentSnapNum = cl.receivedSnapNum = 0;

	CL_SetClientState( CA_CONNECTED ); // not active anymore, but not disconnected
}

/*
* CL_ServerReconnect_f
*
* The server is changing levels
*/
void CL_ServerReconnect_f( void ) {
	if( cls.demo.playing ) {
		return;
	}

	//if we are downloading, we don't change!  This so we don't suddenly stop downloading a map
	if( cls.download.filenum || cls.download.web ) {
		cls.download.pending_reconnect = true;
		return;
	}

	if( cls.state < CA_CONNECTED ) {
		Com_Printf( "Error: CL_ServerReconnect_f while not connected\n" );
		return;
	}

	if( cls.demo.recording ) {
		CL_Stop_f();
	}

	cls.connect_count = 0;
	cls.rejected = false;

	CL_GameModule_Shutdown();
	CL_SoundModule_StopAllSounds( true, true );

	Com_Printf( "Reconnecting...\n" );

#ifdef TCP_ALLOW_CONNECT
	cls.connect_time = Sys_Milliseconds();
#else
	cls.connect_time = Sys_Milliseconds() - 1500;
#endif

	memset( cl.configstrings, 0, sizeof( cl.configstrings ) );
	CL_SetClientState( CA_HANDSHAKE );
	CL_AddReliableCommand( "new" );
}

/*
* CL_Reconnect_f
*
* User reconnect command.
*/
void CL_Reconnect_f( void ) {
	char *servername;
	socket_type_t servertype;
	netadr_t serveraddress;

	if( !cls.servername ) {
		Com_Printf( "Can't reconnect, never connected\n" );
		return;
	}

	cl_connectChain[0] = '\0';
	cl_nextString[0] = '\0';

	servername = TempCopyString( cls.servername );
	servertype = cls.servertype;
	serveraddress = cls.serveraddress;
	CL_Disconnect( NULL );
	CL_Connect( servername, servertype, &serveraddress, "" );
	Mem_TempFree( servername );
}

/*
* CL_ConnectionlessPacket
*
* Responses to broadcasts, etc
*/
static void CL_ConnectionlessPacket( const socket_t *socket, const netadr_t *address, msg_t *msg ) {
	char *s;
	char *c;

	MSG_BeginReading( msg );
	MSG_ReadInt32( msg ); // skip the -1

	s = MSG_ReadStringLine( msg );

	if( !strncmp( s, "getserversResponse\\", 19 ) ) {
		Com_DPrintf( "%s: %s\n", NET_AddressToString( address ), "getserversResponse" );
		CL_ParseGetServersResponse( socket, address, msg, false );
		return;
	}

	if( !strncmp( s, "getserversExtResponse", 21 ) ) {
		Com_DPrintf( "%s: %s\n", NET_AddressToString( address ), "getserversExtResponse" );
		CL_ParseGetServersResponse( socket, address, msg, true );
		return;
	}

	Cmd_TokenizeString( s );
	c = Cmd_Argv( 0 );

	Com_DPrintf( "%s: %s\n", NET_AddressToString( address ), s );

	// server responding to a status broadcast
	if( !strcmp( c, "info" ) ) {
		CL_ParseStatusMessage( socket, address, msg );
		return;
	}

	// jal : wsw
	// server responding to a detailed info broadcast
	if( !strcmp( c, "infoResponse" ) ) {
		CL_ParseGetInfoResponse( socket, address, msg );
		return;
	}
	if( !strcmp( c, "statusResponse" ) ) {
		CL_ParseGetStatusResponse( socket, address, msg );
		return;
	}

	if( cls.demo.playing ) {
		Com_DPrintf( "Received connectionless cmd \"%s\" from %s while playing a demo\n", s, NET_AddressToString( address ) );
		return;
	}

	// server connection
	if( !strcmp( c, "client_connect" ) ) {
		if( cls.state == CA_CONNECTED ) {
			Com_Printf( "Dup connect received.  Ignored.\n" );
			return;
		}
		// these two are from Q3
		if( cls.state != CA_CONNECTING ) {
			Com_Printf( "client_connect packet while not connecting.  Ignored.\n" );
			return;
		}
		if( !NET_CompareAddress( address, &cls.serveraddress ) ) {
			Com_Printf( "client_connect from a different address.  Ignored.\n" );
			Com_Printf( "Was %s should have been %s\n", NET_AddressToString( address ),
						NET_AddressToString( &cls.serveraddress ) );
			return;
		}

		cls.rejected = false;

		Q_strncpyz( cls.session, MSG_ReadStringLine( msg ), sizeof( cls.session ) );

		Netchan_Setup( &cls.netchan, socket, address, Netchan_GamePort() );
		memset( cl.configstrings, 0, sizeof( cl.configstrings ) );
		CL_SetClientState( CA_HANDSHAKE );
		CL_AddReliableCommand( "new" );
		return;
	}

	// reject packet, used to inform the client that connection attemp didn't succeed
	if( !strcmp( c, "reject" ) ) {
		int rejectflag;

		if( cls.state != CA_CONNECTING ) {
			Com_Printf( "reject packet while not connecting, ignored\n" );
			return;
		}
		if( !NET_CompareAddress( address, &cls.serveraddress ) ) {
			Com_Printf( "reject from a different address, ignored\n" );
			Com_Printf( "Was %s should have been %s\n", NET_AddressToString( address ),
						NET_AddressToString( &cls.serveraddress ) );
			return;
		}

		cls.rejected = true;

		cls.rejecttype = atoi( MSG_ReadStringLine( msg ) );
		if( cls.rejecttype < 0 || cls.rejecttype >= DROP_TYPE_TOTAL ) {
			cls.rejecttype = DROP_TYPE_GENERAL;
		}

		rejectflag = atoi( MSG_ReadStringLine( msg ) );

		Q_strncpyz( cls.rejectmessage, MSG_ReadStringLine( msg ), sizeof( cls.rejectmessage ) );
		if( strlen( cls.rejectmessage ) > sizeof( cls.rejectmessage ) - 2 ) {
			cls.rejectmessage[strlen( cls.rejectmessage ) - 2] = '.';
			cls.rejectmessage[strlen( cls.rejectmessage ) - 1] = '.';
			cls.rejectmessage[strlen( cls.rejectmessage )] = '.';
		}

		Com_Printf( "Connection refused: %s\n", cls.rejectmessage );
		if( rejectflag & DROP_FLAG_AUTORECONNECT ) {
			Com_Printf( "Automatic reconnecting allowed.\n" );
		} else {
			char menuparms[MAX_STRING_CHARS];

			Com_Printf( "Automatic reconnecting not allowed.\n" );

			CL_Disconnect( NULL );
			Q_snprintfz( menuparms, sizeof( menuparms ), "menu_open connfailed dropreason %i servername \"%s\" droptype %i rejectmessage \"%s\"",
						 DROP_REASON_CONNFAILED, cls.servername, cls.rejecttype, cls.rejectmessage );

			Cbuf_ExecuteText( EXEC_NOW, menuparms );
		}

		return;
	}

	// remote command from gui front end
	if( !strcmp( c, "cmd" ) ) {
		if( !NET_IsLocalAddress( address ) ) {
			Com_Printf( "Command packet from remote host, ignored\n" );
			return;
		}
		Sys_AppActivate();
		s = MSG_ReadString( msg );
		Cbuf_AddText( s );
		Cbuf_AddText( "\n" );
		return;
	}
	// print command from somewhere
	if( !strcmp( c, "print" ) ) {
		// CA_CONNECTING is allowed, because old servers send protocol mismatch connection error message with it
		if( ( ( cls.state != CA_UNINITIALIZED && cls.state != CA_DISCONNECTED ) &&
			  NET_CompareAddress( address, &cls.serveraddress ) ) ||
			( rcon_address->string[0] != '\0' && NET_CompareAddress( address, &cls.rconaddress ) ) ) {
			s = MSG_ReadString( msg );
			Com_Printf( "%s", s );
			return;
		} else {
			Com_Printf( "Print packet from unknown host, ignored\n" );
			return;
		}
	}

	// ping from somewhere
	if( !strcmp( c, "ping" ) ) {
		// send any args back with the acknowledgement
		Netchan_OutOfBandPrint( socket, address, "ack %s", Cmd_Args() );
		return;
	}

	// ack from somewhere
	if( !strcmp( c, "ack" ) ) {
		return;
	}

	// challenge from the server we are connecting to
	if( !strcmp( c, "challenge" ) ) {
		// these two are from Q3
		if( cls.state != CA_CONNECTING ) {
			Com_Printf( "challenge packet while not connecting, ignored\n" );
			return;
		}
		if( !NET_CompareAddress( address, &cls.serveraddress ) ) {
			Com_Printf( "challenge from a different address, ignored\n" );
			Com_Printf( "Was %s", NET_AddressToString( address ) );
			Com_Printf( " should have been %s\n", NET_AddressToString( &cls.serveraddress ) );
			return;
		}

		cls.challenge = atoi( Cmd_Argv( 1 ) );
		//wsw : r1q2[start]
		//r1: reset the timer so we don't send dup. getchallenges
		cls.connect_time = Sys_Milliseconds();
		//wsw : r1q2[end]
		CL_SendConnectPacket();
		return;
	}

	// echo request from server
	if( !strcmp( c, "echo" ) ) {
		Netchan_OutOfBandPrint( socket, address, "%s", Cmd_Argv( 1 ) );
		return;
	}

	Com_Printf( "Unknown connectionless packet from %s\n%s\n", NET_AddressToString( address ), c );
}

/*
* CL_ProcessPacket
*/
static bool CL_ProcessPacket( netchan_t *netchan, msg_t *msg ) {
	int zerror;

	if( !Netchan_Process( netchan, msg ) ) {
		return false; // wasn't accepted for some reason

	}
	// now if compressed, expand it
	MSG_BeginReading( msg );
	MSG_ReadInt32( msg ); // sequence
	MSG_ReadInt32( msg ); // sequence_ack
	if( msg->compressed ) {
		zerror = Netchan_DecompressMessage( msg );
		if( zerror < 0 ) {
			// compression error. Drop the packet
			Com_Printf( "CL_ProcessPacket: Compression error %i. Dropping packet\n", zerror );
			return false;
		}
	}

	return true;
}

/*
* CL_ReadPackets
*/
void CL_ReadPackets( void ) {
	static msg_t msg;
	static uint8_t msgData[MAX_MSGLEN];
	int socketind, ret;
	socket_t *socket;
	netadr_t address;

	socket_t* sockets [] =
	{
		&cls.socket_loopback,
		&cls.socket_udp,
		&cls.socket_udp6,
#ifdef TCP_ALLOW_CONNECT
		&cls.socket_tcp
#endif
	};

	MSG_Init( &msg, msgData, sizeof( msgData ) );

	for( socketind = 0; socketind < sizeof( sockets ) / sizeof( sockets[0] ); socketind++ ) {
		socket = sockets[socketind];

#ifdef TCP_ALLOW_CONNECT
		if( socket->type == SOCKET_TCP && !socket->connected ) {
			continue;
		}
#endif

		while( socket->open && ( ret = NET_GetPacket( socket, &address, &msg ) ) != 0 ) {
			if( ret == -1 ) {
				Com_Printf( "Error receiving packet with %s: %s\n", NET_SocketToString( socket ), NET_ErrorString() );
				if( cls.reliable && cls.socket == socket ) {
					CL_Disconnect( va( "Error receiving packet: %s\n", NET_ErrorString() ) );
				}

				continue;
			}

			// remote command packet
			if( *(int *)msg.data == -1 ) {
				CL_ConnectionlessPacket( socket, &address, &msg );
				continue;
			}

			if( cls.demo.playing ) {
				// only allow connectionless packets during demo playback
				continue;
			}

			if( cls.state == CA_DISCONNECTED || cls.state == CA_GETTING_TICKET || cls.state == CA_CONNECTING || cls.state == CA_CINEMATIC ) {
				Com_DPrintf( "%s: Not connected\n", NET_AddressToString( &address ) );
				continue; // dump it if not connected
			}

			if( msg.cursize < 8 ) {
				//wsw : r1q2[start]
				//r1: delegated to DPrintf (someone could spam your console with crap otherwise)
				Com_DPrintf( "%s: Runt packet\n", NET_AddressToString( &address ) );
				//wsw : r1q2[end]
				continue;
			}

			//
			// packet from server
			//
			if( !NET_CompareAddress( &address, &cls.netchan.remoteAddress ) ) {
				Com_DPrintf( "%s: Sequenced packet without connection\n", NET_AddressToString( &address ) );
				continue;
			}
			if( !CL_ProcessPacket( &cls.netchan, &msg ) ) {
				continue; // wasn't accepted for some reason, like only one fragment of bigger message

			}
			CL_ParseServerMessage( &msg );
			cls.lastPacketReceivedTime = cls.realtime;

#ifdef TCP_ALLOW_CONNECT
			// we might have just been disconnected
			if( socket->type == SOCKET_TCP && !socket->connected ) {
				break;
			}
#endif
		}
	}

	if( cls.demo.playing ) {
		return;
	}

	// not expected, but could happen if cls.realtime is cleared and lastPacketReceivedTime is not
	if( cls.lastPacketReceivedTime > cls.realtime ) {
		cls.lastPacketReceivedTime = cls.realtime;
	}

	// check timeout
	if( cls.state >= CA_HANDSHAKE && cls.state != CA_CINEMATIC && cls.lastPacketReceivedTime ) {
		if( cls.lastPacketReceivedTime + cl_timeout->value * 1000 < cls.realtime ) {
			if( ++cl.timeoutcount > 5 ) { // timeoutcount saves debugger
				Com_Printf( "\nServer connection timed out.\n" );
				CL_Disconnect( "Connection timed out" );
				return;
			}
		}
	} else {
		cl.timeoutcount = 0;
	}
}

//=============================================================================

/*
* CL_Userinfo_f
*/
static void CL_Userinfo_f( void ) {
	Com_Printf( "User info settings:\n" );
	Info_Print( Cvar_Userinfo() );
}

static int precache_check; // for autodownload of precache items
static int precache_spawncount;
static int precache_tex;
static int precache_pure;

#define PLAYER_MULT 5

// ENV_CNT is map load
#define ENV_CNT ( CS_PLAYERINFOS + MAX_CLIENTS * PLAYER_MULT )
#define TEXTURE_CNT ( ENV_CNT + 1 )

static unsigned int CL_LoadMap( const char *name ) {
	int i;
	int areas;

	unsigned int map_checksum;

	assert( !cl.cms );

	// if local server is running, share the collision model,
	// increasing the ref counter
	if( Com_ServerState() ) {
		cl.cms = Com_ServerCM( &map_checksum );
	} else {
		cl.cms = CM_New( NULL );
		CM_LoadMap( cl.cms, name, true, &map_checksum );
	}

	CM_AddReference( cl.cms );

	assert( cl.cms );

	// allocate memory for areabits
	areas = CM_NumAreas( cl.cms );
	areas *= CM_AreaRowSize( cl.cms );

	cl.frames_areabits = Mem_ZoneMalloc( UPDATE_BACKUP * areas );
	for( i = 0; i < UPDATE_BACKUP; i++ ) {
		cl.snapShots[i].areabytes = areas;
		cl.snapShots[i].areabits = cl.frames_areabits + i * areas;
	}

	// check memory integrity
	Mem_DebugCheckSentinelsGlobal();

	return map_checksum;
}

void CL_RequestNextDownload( void ) {
	char tempname[MAX_CONFIGSTRING_CHARS + 4];
	purelist_t *purefile;
	int i;

	if( cls.state != CA_CONNECTED ) {
		return;
	}

	// pure list
	if( cls.sv_pure ) {
		// skip
		if( !cl_downloads->integer ) {
			precache_pure = -1;
		}

		// try downloading
		if( precache_pure != -1 ) {
			i = 0;
			purefile = cls.purelist;
			while( i < precache_pure && purefile ) {
				purefile = purefile->next;
				i++;
			}

			while( purefile ) {
				precache_pure++;
				if( !CL_CheckOrDownloadFile( purefile->filename ) ) {
					return;
				}
				purefile = purefile->next;
			}
			precache_pure = -1;
		}

		if( precache_pure == -1 ) {
			bool failed = false;
			char message[MAX_STRING_CHARS];

			Q_snprintfz( message, sizeof( message ), "Pure check failed:" );

			purefile = cls.purelist;
			while( purefile ) {
				Com_DPrintf( "Adding pure file: %s\n", purefile->filename );
				if( !FS_AddPurePak( purefile->checksum ) ) {
					failed = true;
					Q_strncatz( message, " ", sizeof( message ) );
					Q_strncatz( message, purefile->filename, sizeof( message ) );
				}
				purefile = purefile->next;
			}

			if( failed ) {
				Com_Error( ERR_DROP, "%s", message );
				return;
			}
		}
	}

	// skip if download not allowed
	if( !cl_downloads->integer && precache_check < ENV_CNT ) {
		precache_check = ENV_CNT;
	}

	//ZOID
	if( precache_check == CS_WORLDMODEL ) { // confirm map
		precache_check = CS_MODELS; // 0 isn't used

		if( !CL_CheckOrDownloadFile( cl.configstrings[CS_WORLDMODEL] ) ) {
			return; // started a download
		}
	}

	if( precache_check >= CS_MODELS && precache_check < CS_MODELS + MAX_MODELS ) {
		while( precache_check < CS_MODELS + MAX_MODELS && cl.configstrings[precache_check][0] ) {
			if( cl.configstrings[precache_check][0] == '*' ||
				cl.configstrings[precache_check][0] == '$' || // disable playermodel downloading for now
				cl.configstrings[precache_check][0] == '#' ) {
				precache_check++;
				continue;
			}

			if( !CL_CheckOrDownloadFile( cl.configstrings[precache_check++] ) ) {
				return; // started a download
			}
		}
		precache_check = CS_SOUNDS;
	}

	if( precache_check >= CS_SOUNDS && precache_check < CS_SOUNDS + MAX_SOUNDS ) {
		if( precache_check == CS_SOUNDS ) {
			precache_check++; // zero is blank

		}
		while( precache_check < CS_SOUNDS + MAX_SOUNDS && cl.configstrings[precache_check][0] ) {
			if( cl.configstrings[precache_check][0] == '*' ) { // sexed sounds
				precache_check++;
				continue;
			}
			Q_strncpyz( tempname, cl.configstrings[precache_check++], sizeof( tempname ) );
			if( !COM_FileExtension( tempname ) ) {
				if( !FS_FirstExtension( tempname, SOUND_EXTENSIONS, NUM_SOUND_EXTENSIONS ) ) {
					COM_DefaultExtension( tempname, ".wav", sizeof( tempname ) );
					if( !CL_CheckOrDownloadFile( tempname ) ) {
						return; // started a download
					}
				}
			} else {
				if( !CL_CheckOrDownloadFile( tempname ) ) {
					return; // started a download
				}
			}
		}
		precache_check = CS_IMAGES;
	}
	if( precache_check >= CS_IMAGES && precache_check < CS_IMAGES + MAX_IMAGES ) {
		if( precache_check == CS_IMAGES ) {
			precache_check++; // zero is blank

		}
		// precache phase completed
		precache_check = ENV_CNT;
	}

	if( precache_check == ENV_CNT ) {
		bool restart = false;
		bool vid_restart = false;
		const char *restart_msg = "";
		unsigned map_checksum;

		// we're done with the download phase, so clear the list
		CL_FreeDownloadList();
		if( cls.pure_restart ) {
			restart = true;
			restart_msg = "Pure server. Restarting media...";
		}
		if( cls.download.successCount ) {
			restart = true;
			vid_restart = true;
			restart_msg = "Files downloaded. Restarting media...";
		}

		CL_BeginRegistration();

		if( restart ) {
			Com_Printf( "%s\n", restart_msg );

			if( vid_restart ) {
				// no media is going to survive a vid_restart...
				Cbuf_ExecuteText( EXEC_NOW, "s_restart 1\n" );
			} else {
				// make sure all media assets will be freed
				CL_EndRegistration();
				CL_BeginRegistration();
			}
		}

		if( !vid_restart ) {
			CL_RestartMedia();
		}

		cls.download.successCount = 0;

		map_checksum = CL_LoadMap( cl.configstrings[CS_WORLDMODEL] );
		if( map_checksum != (unsigned)atoi( cl.configstrings[CS_MAPCHECKSUM] ) ) {
			Com_Error( ERR_DROP, "Local map version differs from server: %u != '%u'",
					   map_checksum, (unsigned)atoi( cl.configstrings[CS_MAPCHECKSUM] ) );
			return;
		}

		precache_check = TEXTURE_CNT;
	}

	if( precache_check == TEXTURE_CNT ) {
		precache_check = TEXTURE_CNT + 1;
		precache_tex = 0;
	}

	// confirm existance of textures, download any that don't exist
	if( precache_check == TEXTURE_CNT + 1 ) {
		precache_check = TEXTURE_CNT + 999;
	}

	// load client game module
	CL_GameModule_Init();
	CL_AddReliableCommand( va( "begin %i\n", precache_spawncount ) );

	CL_Mumble_Link();
}

/*
* CL_Precache_f
*
* The server will send this command right
* before allowing the client into the server
*/
void CL_Precache_f( void ) {
	FS_RemovePurePaks();

	if( cls.demo.playing ) {
		if( !cls.demo.play_jump ) {
			CL_LoadMap( cl.configstrings[CS_WORLDMODEL] );

			CL_GameModule_Init();
		} else {
			CL_GameModule_Reset();
			CL_SoundModule_StopAllSounds( false, false );
		}

		cls.demo.play_ignore_next_frametime = true;

		return;
	}

	precache_pure = 0;
	precache_check = CS_WORLDMODEL;
	precache_spawncount = atoi( Cmd_Argv( 1 ) );

	CL_RequestNextDownload();
}

/*
* CL_WriteConfiguration
*
* Writes key bindings, archived cvars and aliases to a config file
*/
static void CL_WriteConfiguration( const char *name, bool warn ) {
	int file;

	if( FS_FOpenFile( name, &file, FS_WRITE ) == -1 ) {
		Com_Printf( "Couldn't write %s.\n", name );
		return;
	}

	if( warn ) {
		// Write 'Warsow' with UTF-8 encoded section sign in place of the s to aid
		// text editors in recognizing the file's encoding
		FS_Printf( file, "// This file is automatically generated by " APPLICATION_UTF8 ", do not modify.\r\n" );
	}

	FS_Printf( file, "\r\n// key bindings\r\n" );
	Key_WriteBindings( file );

	FS_Printf( file, "\r\n// variables\r\n" );
	Cvar_WriteVariables( file );

	FS_Printf( file, "\r\n// aliases\r\n" );
	Cmd_WriteAliases( file );

	FS_FCloseFile( file );
}


/*
* CL_WriteConfig_f
*/
static void CL_WriteConfig_f( void ) {
	char *name;
	int name_size;

	if( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: writeconfig <filename>\n" );
		return;
	}

	name_size = sizeof( char ) * ( strlen( Cmd_Argv( 1 ) ) + strlen( ".cfg" ) + 1 );
	name = Mem_TempMalloc( name_size );
	Q_strncpyz( name, Cmd_Argv( 1 ), name_size );
	COM_SanitizeFilePath( name );

	if( !COM_ValidateRelativeFilename( name ) ) {
		Com_Printf( "Invalid filename" );
		Mem_TempFree( name );
		return;
	}

	COM_DefaultExtension( name, ".cfg", name_size );

	Com_Printf( "Writing: %s\n", name );
	CL_WriteConfiguration( name, false );

	Mem_TempFree( name );
}

/*
* CL_SetClientState
*/
void CL_SetClientState( int state ) {
	cls.state = state;
	Com_SetClientState( state );

	if( state <= CA_DISCONNECTED ) {
		Steam_AdvertiseGame( NULL, 0 );
	}

	switch( state ) {
		case CA_DISCONNECTED:
			Con_Close();
			CL_UIModule_Refresh( true, true );
			CL_UIModule_ForceMenuOn();
			//CL_UIModule_MenuMain ();
			CL_SetKeyDest( key_menu );
			//SCR_UpdateScreen();
			break;
		case CA_GETTING_TICKET:
		case CA_CONNECTING:
			cls.cgameActive = false;
			Con_Close();
			CL_UIModule_ForceMenuOff();
			CL_SoundModule_StopBackgroundTrack();
			CL_SoundModule_Clear();
			CL_SetKeyDest( key_game );
			//SCR_UpdateScreen();
			break;
		case CA_CONNECTED:
			cls.cgameActive = false;
			Con_Close();
			Cvar_FixCheatVars();
			//SCR_UpdateScreen();
			break;
		case CA_ACTIVE:
		case CA_CINEMATIC:
			cl_connectChain[0] = '\0';
			CL_EndRegistration();
			Con_Close();
			CL_UIModule_Refresh( false, false );
			CL_UIModule_ForceMenuOff();
			CL_SetKeyDest( key_game );
			//SCR_UpdateScreen();
			CL_AddReliableCommand( "svmotd 1" );
			CL_SoundModule_Clear();
			break;
		default:
			break;
	}
}

/*
* CL_GetClientState
*/
connstate_t CL_GetClientState( void ) {
	return cls.state;
}

/*
* CL_InitMedia
*/
void CL_InitMedia( void ) {
	if( cls.mediaInitialized ) {
		return;
	}
	if( cls.state == CA_UNINITIALIZED ) {
		return;
	}
	if( !VID_RefreshIsActive() ) {
		return;
	}

	// random seed to be shared among game modules so pseudo-random stuff is in sync
	if( cls.state != CA_CONNECTED ) {
		srand( time( NULL ) );
		cls.mediaRandomSeed = rand();
	}

	cls.mediaInitialized = true;

	CL_SoundModule_StopAllSounds( true, true );

	// register console font and background
	SCR_RegisterConsoleMedia();

	SCR_EnableQuickMenu( false );

	// load user interface
	CL_UIModule_Init();

	// check memory integrity
	Mem_DebugCheckSentinelsGlobal();
}

/*
* CL_ShutdownMedia
*/
void CL_ShutdownMedia( void ) {
	if( !cls.mediaInitialized ) {
		return;
	}
	if( !VID_RefreshIsActive() ) {
		return;
	}

	cls.mediaInitialized = false;

	CL_SoundModule_StopAllSounds( true, true );

	// shutdown cgame
	CL_GameModule_Shutdown();

	// shutdown user interface
	CL_UIModule_Shutdown();

	SCR_ShutDownConsoleMedia();

	SCR_StopCinematic();
}

/*
* CL_RestartMedia
*/
void CL_RestartMedia( void ) {
	if( !VID_RefreshIsActive() ) {
		return;
	}

	if( cls.mediaInitialized ) {
		// shutdown cgame
		CL_GameModule_Shutdown();

		cls.mediaInitialized = false;
	}

	CL_SoundModule_StopAllSounds( true, true );

	// random seed to be shared among game modules so pseudo-random stuff is in sync
	if( cls.state != CA_CONNECTED ) {
		srand( time( NULL ) );
		cls.mediaRandomSeed = rand();
	}

	cls.mediaInitialized = true;

	FTLIB_TouchAllFonts();

	// register console font and background
	SCR_RegisterConsoleMedia();

	CL_UIModule_ForceMenuOff();

	CL_UIModule_TouchAllAssets();

	// check memory integrity
	Mem_DebugCheckSentinelsGlobal();
}

/*
* CL_S_Restart
*
* Restart the sound subsystem so it can pick up new parameters and flush all sounds
*/
void CL_S_Restart( bool noVideo ) {
	bool verbose = ( Cmd_Argc() >= 2 ? true : false );

	// The cgame and game must also be forced to restart because handles will become invalid
	// VID_Restart also forces an audio restart
	if( !noVideo ) {
		VID_Restart( verbose, true );
		VID_CheckChanges();
	} else {
		CL_SoundModule_Shutdown( verbose );
		CL_SoundModule_Init( verbose );
	}
}

/*
* CL_S_Restart_f
*
* Restart the sound subsystem so it can pick up new parameters and flush all sounds
*/
static void CL_S_Restart_f( void ) {
	CL_S_Restart( false );
}

/*
* CL_ShowIP_f - wsw : jal : taken from Q3 (it only shows the ip when server was started)
*/
static void CL_ShowIP_f( void ) {
	NET_ShowIP();
}

/*
* CL_ShowServerIP_f - wsw : pb : show the ip:port of the server the client is connected to
*/
static void CL_ShowServerIP_f( void ) {
	if( cls.state != CA_CONNECTED && cls.state != CA_ACTIVE ) {
		Com_Printf( "Not connected to a server\n" );
		return;
	}

	Com_Printf( "Connected to server:\n" );
	Com_Printf( "Name: %s\n", cls.servername );
	Com_Printf( "Address: %s\n", NET_AddressToString( &cls.serveraddress ) );
}

/*
* CL_InitLocal
*/
static void CL_InitLocal( void ) {
	cvar_t *name, *color;

	cls.state = CA_DISCONNECTED;
	Com_SetClientState( CA_DISCONNECTED );

	//
	// register our variables
	//
	cl_stereo_separation =  Cvar_Get( "cl_stereo_separation", "0.4", CVAR_ARCHIVE );
	cl_stereo =     Cvar_Get( "cl_stereo", "0", CVAR_ARCHIVE );

	cl_maxfps =     Cvar_Get( "cl_maxfps", "250", CVAR_ARCHIVE );
	cl_sleep =      Cvar_Get( "cl_sleep", "1", CVAR_ARCHIVE );
	cl_pps =        Cvar_Get( "cl_pps", "40", CVAR_ARCHIVE );

	cl_extrapolationTime =  Cvar_Get( "cl_extrapolationTime", "0", CVAR_DEVELOPER );
	cl_extrapolate = Cvar_Get( "cl_extrapolate", "1", CVAR_ARCHIVE );

	cl_masterservers =  Cvar_Get( "masterservers", DEFAULT_MASTER_SERVERS_IPS, 0 );

	cl_shownet =        Cvar_Get( "cl_shownet", "0", 0 );
	cl_timeout =        Cvar_Get( "cl_timeout", "120", 0 );
	cl_timedemo =       Cvar_Get( "timedemo", "0", CVAR_CHEAT );
	cl_demoavi_video =  Cvar_Get( "cl_demoavi_video", "1", CVAR_ARCHIVE );
	cl_demoavi_audio =  Cvar_Get( "cl_demoavi_audio", "0", CVAR_ARCHIVE );
	cl_demoavi_fps =    Cvar_Get( "cl_demoavi_fps", "30.3", CVAR_ARCHIVE );
	cl_demoavi_fps->modified = true;
	cl_demoavi_scissor =    Cvar_Get( "cl_demoavi_scissor", "0", CVAR_ARCHIVE );

	rcon_client_password =  Cvar_Get( "rcon_password", "", 0 );
	rcon_address =      Cvar_Get( "rcon_address", "", 0 );

	// wsw : debug netcode
	cl_debug_serverCmd =    Cvar_Get( "cl_debug_serverCmd", "0", CVAR_ARCHIVE | CVAR_CHEAT );
	cl_debug_timeDelta =    Cvar_Get( "cl_debug_timeDelta", "0", CVAR_ARCHIVE /*|CVAR_CHEAT*/ );

	cl_downloads =      Cvar_Get( "cl_downloads", "1", CVAR_ARCHIVE );
	cl_downloads_from_web = Cvar_Get( "cl_downloads_from_web", "1", CVAR_ARCHIVE | CVAR_READONLY );
	cl_downloads_from_web_timeout = Cvar_Get( "cl_downloads_from_web_timeout", "600", CVAR_ARCHIVE );
	cl_download_allow_modules = Cvar_Get( "cl_download_allow_modules", "1", CVAR_ARCHIVE );
	cl_checkForUpdate = Cvar_Get( "cl_checkForUpdate", "1", CVAR_ARCHIVE );

	//
	// userinfo
	//
	info_password =     Cvar_Get( "password", "", CVAR_USERINFO );
	rate =          Cvar_Get( "rate", "60000", CVAR_DEVELOPER ); // FIXME

	name = Cvar_Get( "name", "", CVAR_USERINFO | CVAR_ARCHIVE );
	if( !name->string[0] ) {
		char steamname[MAX_NAME_BYTES * 4], *steamnameIn = steamname, *steamnameOut = steamname, c;
		steamname[0] = '\0';
		Steam_GetPersonaName( steamname, sizeof( steamname ) );
		while( ( c = *steamnameIn ) != '\0' ) {
			steamnameIn++;
			if( ( c < 32 ) || ( c >= 127 ) || ( c == '\\' ) || ( c == ';' ) || ( c == '"' ) ) {
				continue;
			}

			*( steamnameOut++ ) = c;
		}
		*steamnameOut = '\0';

		if( !( COM_RemoveColorTokens( steamname )[0] ) ) {
			Q_strncpyz( steamname, "Player", sizeof( steamname ) );
		}

		Cvar_Set( name->name, steamname );
	}

	Cvar_Get( "clan", "", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get( "model", DEFAULT_PLAYERMODEL, CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get( "skin", DEFAULT_PLAYERSKIN, CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get( "hand", "0", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get( "handicap", "0", CVAR_USERINFO | CVAR_ARCHIVE );

	Cvar_Get( "cl_download_name", "", CVAR_READONLY );
	Cvar_Get( "cl_download_percent", "0", CVAR_READONLY );

	color = Cvar_Get( "color", "", CVAR_ARCHIVE | CVAR_USERINFO );
	if( COM_ReadColorRGBString( color->string ) == -1 ) {
		time_t long_time; // random isn't working fine at this point.
		unsigned int hash; // so we get the user local time and use its hash
		int rgbcolor;
		time( &long_time );
		hash = COM_SuperFastHash64BitInt( ( uint64_t )long_time );
		rgbcolor = COM_ValidatePlayerColor( COLOR_RGB( hash & 0xff, ( hash >> 8 ) & 0xff, ( hash >> 16 ) & 0xff ) );
		Cvar_Set( color->name, va( "%i %i %i", COLOR_R( rgbcolor ), COLOR_G( rgbcolor ), COLOR_B( rgbcolor ) ) );
	}

	//
	// register our commands
	//
	Cmd_AddCommand( "s_restart", CL_S_Restart_f );
	Cmd_AddCommand( "cmd", CL_ForwardToServer_f );
	Cmd_AddCommand( "requestservers", CL_GetServers_f );
	Cmd_AddCommand( "getinfo", CL_QueryGetInfoMessage_f ); // wsw : jal : ask for server info
	Cmd_AddCommand( "getstatus", CL_QueryGetStatusMessage_f ); // wsw : jal : ask for server info
	Cmd_AddCommand( "userinfo", CL_Userinfo_f );
	Cmd_AddCommand( "disconnect", CL_Disconnect_f );
	Cmd_AddCommand( "record", CL_Record_f );
	Cmd_AddCommand( "stop", CL_Stop_f );
	Cmd_AddCommand( "quit", CL_Quit_f );
	Cmd_AddCommand( "connect", CL_Connect_f );
#if defined( TCP_ALLOW_CONNECT ) && defined( TCP_ALLOW_CONNECT_CLIENT )
	Cmd_AddCommand( "tcpconnect", CL_TCPConnect_f );
#endif
	Cmd_AddCommand( "reconnect", CL_Reconnect_f );
	Cmd_AddCommand( "rcon", CL_Rcon_f );
	Cmd_AddCommand( "writeconfig", CL_WriteConfig_f );
	Cmd_AddCommand( "showip", CL_ShowIP_f ); // jal : wsw : print our ip
	Cmd_AddCommand( "demo", CL_PlayDemo_f );
	Cmd_AddCommand( "demoavi", CL_PlayDemoToAvi_f );
	Cmd_AddCommand( "next", CL_SetNext_f );
	Cmd_AddCommand( "pingserver", CL_PingServer_f );
	Cmd_AddCommand( "demopause", CL_PauseDemo_f );
	Cmd_AddCommand( "demojump", CL_DemoJump_f );
	Cmd_AddCommand( "showserverip", CL_ShowServerIP_f );
	Cmd_AddCommand( "downloadstatus", CL_DownloadStatus_f );
	Cmd_AddCommand( "downloadcancel", CL_DownloadCancel_f );

	Cmd_SetCompletionFunc( "demo", CL_DemoComplete );
	Cmd_SetCompletionFunc( "demoavi", CL_DemoComplete );
}

/*
* CL_ShutdownLocal
*/
static void CL_ShutdownLocal( void ) {
	cls.state = CA_UNINITIALIZED;
	Com_SetClientState( CA_UNINITIALIZED );

	Cmd_RemoveCommand( "s_restart" );
	Cmd_RemoveCommand( "cmd" );
	Cmd_RemoveCommand( "requestservers" );
	Cmd_RemoveCommand( "getinfo" );
	Cmd_RemoveCommand( "getstatus" );
	Cmd_RemoveCommand( "userinfo" );
	Cmd_RemoveCommand( "disconnect" );
	Cmd_RemoveCommand( "record" );
	Cmd_RemoveCommand( "stop" );
	Cmd_RemoveCommand( "quit" );
	Cmd_RemoveCommand( "connect" );
#if defined( TCP_ALLOW_CONNECT )
	Cmd_RemoveCommand( "tcpconnect" );
#endif
	Cmd_RemoveCommand( "reconnect" );
	Cmd_RemoveCommand( "rcon" );
	Cmd_RemoveCommand( "writeconfig" );
	Cmd_RemoveCommand( "showip" );
	Cmd_RemoveCommand( "demo" );
	Cmd_RemoveCommand( "demoavi" );
	Cmd_RemoveCommand( "next" );
	Cmd_RemoveCommand( "pingserver" );
	Cmd_RemoveCommand( "demopause" );
	Cmd_RemoveCommand( "demojump" );
	Cmd_RemoveCommand( "showserverip" );
	Cmd_RemoveCommand( "downloadstatus" );
	Cmd_RemoveCommand( "downloadcancel" );
}

//============================================================================

/*
* CL_TimedemoStats
*/
static void CL_TimedemoStats( void ) {
	if( cl_timedemo->integer && cls.demo.playing ) {
		int64_t lastTime = cl.timedemo.lastTime;
		if( lastTime != 0 ) {
			int msec;
			int64_t curTime;

			msec = re.GetAverageFrametime();

			curTime = Sys_Milliseconds();
			if( msec  >= 100 ) {
				cl.timedemo.counts[99]++;
			} else {
				cl.timedemo.counts[msec]++;
			}
			cl.timedemo.lastTime = curTime;
			return;
		}
		cl.timedemo.lastTime = Sys_Milliseconds();
	}
}

/*
* CL_AdjustServerTime - adjust delta to new frame snap timestamp
*/
void CL_AdjustServerTime( unsigned int gameMsec ) {
	// hurry up if coming late (unless in demos)
	if( !cls.demo.playing ) {
		if( ( cl.newServerTimeDelta < cl.serverTimeDelta ) && gameMsec > 0 ) {
			cl.serverTimeDelta--;
		}
		if( cl.newServerTimeDelta > cl.serverTimeDelta ) {
			cl.serverTimeDelta++;
		}
	}

	cl.serverTime = cls.gametime + cl.serverTimeDelta;

	// it launches a new snapshot when the timestamp of the CURRENT snap is reached.
	if( cl.pendingSnapNum && ( cl.serverTime >= cl.snapShots[cl.currentSnapNum & UPDATE_MASK].serverTime ) ) {
		// fire next snapshot
		if( CL_GameModule_NewSnapshot( cl.pendingSnapNum ) ) {
			cl.previousSnapNum = cl.currentSnapNum;
			cl.currentSnapNum = cl.pendingSnapNum;
			cl.pendingSnapNum = 0;

			// getting a valid snapshot ends the connection process
			if( cls.state == CA_CONNECTED ) {
				CL_SetClientState( CA_ACTIVE );
			}
		}
	}
}

/*
* CL_RestartTimeDeltas
*/
void CL_RestartTimeDeltas( int newTimeDelta ) {
	int i;

	cl.serverTimeDelta = cl.newServerTimeDelta = newTimeDelta;
	for( i = 0; i < MAX_TIMEDELTAS_BACKUP; i++ )
		cl.serverTimeDeltas[i] = newTimeDelta;

	if( cl_debug_timeDelta->integer ) {
		Com_Printf( S_COLOR_CYAN "***** timeDelta restarted\n" );
	}
}

/*
* CL_SmoothTimeDeltas
*/
int CL_SmoothTimeDeltas( void ) {
	int i, count;
	double delta;
	snapshot_t  *snap;

	if( cls.demo.playing ) {
		if( cl.currentSnapNum <= 0 ) { // if first snap
			return cl.serverTimeDeltas[cl.pendingSnapNum & MASK_TIMEDELTAS_BACKUP];
		}

		return cl.serverTimeDeltas[cl.currentSnapNum & MASK_TIMEDELTAS_BACKUP];
	}

	i = cl.receivedSnapNum - min( MAX_TIMEDELTAS_BACKUP, 8 );
	if( i < 0 ) {
		i = 0;
	}

	for( delta = 0, count = 0; i <= cl.receivedSnapNum; i++ ) {
		snap = &cl.snapShots[i & UPDATE_MASK];
		if( snap->valid && snap->serverFrame == i ) {
			delta += (double)cl.serverTimeDeltas[i & MASK_TIMEDELTAS_BACKUP];
			count++;
		}
	}

	if( !count ) {
		return 0;
	}

	return (int)( delta / (double)count );
}

/*
* CL_UpdateSnapshot - Check for pending snapshots, and fire if needed
*/
void CL_UpdateSnapshot( void ) {
	snapshot_t  *snap;
	int i;

	// see if there is any pending snap to be fired
	if( !cl.pendingSnapNum && ( cl.currentSnapNum != cl.receivedSnapNum ) ) {
		snap = NULL;
		for( i = cl.currentSnapNum + 1; i <= cl.receivedSnapNum; i++ ) {
			if( cl.snapShots[i & UPDATE_MASK].valid && ( cl.snapShots[i & UPDATE_MASK].serverFrame > cl.currentSnapNum ) ) {
				snap = &cl.snapShots[i & UPDATE_MASK];
				//torbenh: this break was the source of the lag bug at cl_fps < sv_pps
				//break;
			}
		}

		if( snap ) { // valid pending snap found
			cl.pendingSnapNum = snap->serverFrame;

			cl.newServerTimeDelta = CL_SmoothTimeDeltas();

			if( cl_extrapolationTime->modified ) {
				if( cl_extrapolationTime->integer > (int)cl.snapFrameTime - 1 ) {
					Cvar_ForceSet( "cl_extrapolationTime", va( "%i", (int)cl.snapFrameTime - 1 ) );
				} else if( cl_extrapolationTime->integer < 0 ) {
					Cvar_ForceSet( "cl_extrapolationTime", "0" );
				}

				cl_extrapolationTime->modified = false;
			}

			if( !cls.demo.playing && cl_extrapolate->integer ) {
				cl.newServerTimeDelta += cl_extrapolationTime->integer;
			}

			// if we don't have current snap (or delay is too big) don't wait to fire the pending one
			if( ( !cls.demo.play_jump && cl.currentSnapNum <= 0 ) ||
				( !cls.demo.playing && abs( cl.newServerTimeDelta - cl.serverTimeDelta ) > 200 ) ) {
				cl.serverTimeDelta = cl.newServerTimeDelta;
			}

			// don't either wait if in a timedemo
			if( cls.demo.playing && cl_timedemo->integer ) {
				cl.serverTimeDelta = cl.newServerTimeDelta;
			}
		}
	}
}

/*
* CL_Netchan_Transmit
*/
void CL_Netchan_Transmit( msg_t *msg ) {
	// if we got here with unsent fragments, fire them all now
	Netchan_PushAllFragments( &cls.netchan );

	if( msg->cursize > 60 ) {
		int zerror = Netchan_CompressMessage( msg );
		if( zerror < 0 ) { // it's compression error, just send uncompressed
			Com_DPrintf( "CL_Netchan_Transmit (ignoring compression): Compression error %i\n", zerror );
		}
	}

	Netchan_Transmit( &cls.netchan, msg );
	cls.lastPacketSentTime = cls.realtime;
}

/*
* CL_MaxPacketsReached
*/
static bool CL_MaxPacketsReached( void ) {
	static int64_t lastPacketTime = 0;
	static float roundingMsec = 0.0f;
	int minpackettime;
	int elapsedTime;

	if( lastPacketTime > cls.realtime ) {
		lastPacketTime = cls.realtime;
	}

	if( cl_pps->integer > 62 || cl_pps->integer < 20 ) {
		Com_Printf( "'cl_pps' value is out of valid range, resetting to default\n" );
		Cvar_ForceSet( "cl_pps", va( "%s", cl_pps->dvalue ) );
	}

	elapsedTime = cls.realtime - lastPacketTime;
	if( cls.mv ) {
		minpackettime = ( 1000.0f / 2 );
	} else {
		float minTime = ( 1000.0f / cl_pps->value );

		// don't let cl_pps be smaller than sv_pps
		if( cls.state == CA_ACTIVE && !cls.demo.playing && cl.snapFrameTime ) {
			if( (unsigned int)minTime > cl.snapFrameTime ) {
				minTime = cl.snapFrameTime;
			}
		}

		minpackettime = (int)minTime;
		roundingMsec += minTime - (int)minTime;
		if( roundingMsec >= 1.0f ) {
			minpackettime += (int)roundingMsec;
			roundingMsec -= (int)roundingMsec;
		}
	}

	if( elapsedTime < minpackettime ) {
		return false;
	}

	lastPacketTime = cls.realtime;
	return true;
}

/*
* CL_SendMessagesToServer
*/
void CL_SendMessagesToServer( bool sendNow ) {
	msg_t message;
	uint8_t messageData[MAX_MSGLEN];

	if( cls.state == CA_DISCONNECTED || cls.state == CA_GETTING_TICKET || cls.state == CA_CONNECTING || cls.state == CA_CINEMATIC ) {
		return;
	}

	if( cls.demo.playing ) {
		return;
	}

	MSG_Init( &message, messageData, sizeof( messageData ) );
	MSG_Clear( &message );

	// send only reliable commands during connecting time
	if( cls.state < CA_ACTIVE ) {
		if( sendNow || cls.realtime > 100 + cls.lastPacketSentTime ) {
			// write the command ack
			if( !cls.reliable ) {
				MSG_WriteUint8( &message, clc_svcack );
				MSG_WriteIntBase128( &message, cls.lastExecutedServerCommand );
			}
			//write up the clc commands
			CL_UpdateClientCommandsToServer( &message );
			if( message.cursize > 0 ) {
				CL_Netchan_Transmit( &message );
			}
		}
	} else if( sendNow || CL_MaxPacketsReached() ) {
		// write the command ack
		if( !cls.reliable ) {
			MSG_WriteUint8( &message, clc_svcack );
			MSG_WriteIntBase128( &message, cls.lastExecutedServerCommand );
		}
		// send a userinfo update if needed
		if( userinfo_modified ) {
			userinfo_modified = false;
			CL_AddReliableCommand( va( "usri \"%s\"", Cvar_Userinfo() ) );
		}
		CL_UpdateClientCommandsToServer( &message );
		CL_WriteUcmdsToMessage( &message );
		if( message.cursize > 0 ) {
			CL_Netchan_Transmit( &message );
		}
	}
}

/*
* CL_NetFrame
*/
static void CL_NetFrame( int realMsec, int gameMsec ) {
	// read packets from server
	if( realMsec > 5000 ) { // if in the debugger last frame, don't timeout
		cls.lastPacketReceivedTime = cls.realtime;
	}

	if( cls.demo.playing ) {
		CL_ReadDemoPackets(); // fetch results from demo file
	}
	CL_ReadPackets(); // fetch results from server

	// send packets to server
	if( cls.netchan.unsentFragments ) {
		Netchan_TransmitNextFragment( &cls.netchan );
	} else {
		CL_SendMessagesToServer( false );
	}

	// resend a connection request if necessary
	CL_CheckForResend();
	CL_CheckDownloadTimeout();

	CL_ServerListFrame();
}

/*
* CL_Frame
*/
void CL_Frame( int realMsec, int gameMsec ) {
	static int allRealMsec = 0, allGameMsec = 0, extraMsec = 0;
	static float roundingMsec = 0.0f;
	int minMsec;
	float maxFps;

	if( dedicated->integer ) {
		return;
	}

	cls.realtime += realMsec;

	if( cls.demo.playing && cls.demo.play_ignore_next_frametime ) {
		gameMsec = 0;
		cls.demo.play_ignore_next_frametime = false;
	}

	if( cl_demoavi_fps->modified ) {
		float newvalue = 1000.0f / (int)( 1000.0f / cl_demoavi_fps->value );
		if( fabs( newvalue - cl_demoavi_fps->value ) > 0.001 ) {
			Com_Printf( "cl_demoavi_fps value has been adjusted to %.4f\n", newvalue );
		}

		Cvar_SetValue( "cl_demoavi_fps", newvalue );
		cl_demoavi_fps->modified = false;
	}

	// demoavi
	if( ( cls.demo.avi || cls.demo.pending_avi ) && cls.state == CA_ACTIVE ) {
		if( cls.demo.pending_avi && !cls.demo.avi ) {
			cls.demo.pending_avi = false;
			CL_BeginDemoAviDump();
		}

		// fixed time for next frame
		if( cls.demo.avi_video ) {
			gameMsec = ( 1000.0 / (double)cl_demoavi_fps->integer ) * Cvar_Value( "timescale" );
			if( gameMsec < 1 ) {
				gameMsec = 1;
			}
		}
	}

	if( cls.demo.playing ) {
		if( cls.demo.paused ) {
			gameMsec = 0;
		} else {
			CL_LatchedDemoJump();
		}
	}

	cls.gametime += gameMsec;

	allRealMsec += realMsec;
	allGameMsec += gameMsec;

	CL_UpdateSnapshot();
	CL_AdjustServerTime( gameMsec );
	CL_UserInputFrame( realMsec );
	CL_NetFrame( realMsec, gameMsec );
	CL_MM_Frame();

	if( cls.state == CA_CINEMATIC ) {
#if 1
		maxFps = 10000.0f;
		minMsec = 1;
		roundingMsec = 0;
#else
		maxFps = SCR_CinematicFramerate() * 2;
		if( maxFps < 24 ) {
			maxFps = 24.0f;
		}
		minMsec = max( ( 1000.0f / maxFps ), 1 );
		roundingMsec += max( ( 1000.0f / maxFps ), 1.0f ) - minMsec;
#endif
	} else if( cls.state == CA_DISCONNECTED ) {
		maxFps = 60;
		minMsec = 1000.0f / maxFps;
		roundingMsec += 1000.0f / maxFps - minMsec;
	} else if( cl_maxfps->integer > 0 && !(cl_timedemo->integer && cls.demo.playing)
			   && !( cls.demo.avi_video && cls.state == CA_ACTIVE ) ) {
		const int absMinFps = 24;

		// do not allow setting cl_maxfps to very low values to prevent cheating
		if( cl_maxfps->integer < absMinFps ) {
			Cvar_ForceSet( "cl_maxfps", STR_TOSTR( absMinFps ) );
		}
		maxFps = VID_AppIsMinimized() ? absMinFps : cl_maxfps->value;
		minMsec = max( ( 1000.0f / maxFps ), 1 );
		roundingMsec += max( ( 1000.0f / maxFps ), 1.0f ) - minMsec;
	} else {
		maxFps = 10000.0f;
		minMsec = 1;
		roundingMsec = 0;
	}

	if( roundingMsec >= 1.0f ) {
		minMsec += (int)roundingMsec;
		roundingMsec -= (int)roundingMsec;
	}

	if( allRealMsec + extraMsec < minMsec ) {
		// let CPU sleep while playing fullscreen video, while minimized
		// or when cl_sleep is enabled
		bool sleep = cl_sleep->integer != 0 ||
			cls.state == CA_CINEMATIC || cls.state == CA_DISCONNECTED ||
			!VID_AppIsActive() || VID_AppIsMinimized(); // FIXME: not sure about listen server here..

		if( sleep && minMsec - extraMsec > 1 ) {
			Sys_Sleep( minMsec - extraMsec - 1 );
		}
		return;
	}

	cls.frametime = allGameMsec;
	cls.realFrameTime = allRealMsec;
#if 1
	if( allRealMsec < minMsec ) { // is compensating for a too slow frame
		extraMsec -= ( minMsec - allRealMsec );
		clamp( extraMsec, 0, 100 );
	} else {   // too slow, or exact frame
		extraMsec = allRealMsec - minMsec;
		clamp( extraMsec, 0, 100 );
	}
#else
	extraMsec = allRealMsec - minMsec;
	clamp( extraMsec, 0, minMsec );
#endif

	CL_TimedemoStats();

	// allow rendering DLL change
	VID_CheckChanges();

	// update the screen
	if( host_speeds->integer ) {
		time_before_ref = Sys_Milliseconds();
	}
	SCR_UpdateScreen();
	if( host_speeds->integer ) {
		time_after_ref = Sys_Milliseconds();
	}

	if( CL_WriteAvi() ) {
		int frame = ++cls.demo.avi_frame;
		if( cls.demo.avi_video ) {
			re.WriteAviFrame( frame, cl_demoavi_scissor->integer );
		}
	}

	// update audio
	if( cls.state != CA_ACTIVE ) {
		// if the loading plaque is up, clear everything out to make sure we aren't looping a dirty
		// dma buffer while loading
		if( cls.disable_screen ) {
			CL_SoundModule_Clear();
		} else {
			CL_SoundModule_Update( vec3_origin, vec3_origin, axis_identity, NULL, false );
		}
	}

	// advance local effects for next frame
	SCR_RunCinematic();
	SCR_RunConsole( allRealMsec );

	allRealMsec = 0;
	allGameMsec = 0;

	cls.framecount++;
}


//============================================================================

static char *updateRemoteData;
static size_t updateRemoteDataSize;

/*
* CL_CheckForUpdateDoneCb
*/
static void CL_CheckForUpdateDoneCb( int status, const char *contentType, void *privatep ) {
	float local_version, net_version;

	if( status != 200 ) {
		goto done;
	}

	// got the file
	// this look stupid but is the safe way to do it
	local_version = atof( va( "%4.3f", APP_VERSION ) );
	net_version = atof( updateRemoteData );

	// we have the version
	//Com_Printf("CheckForUpdate: local: %f net: %f\n", local_version, net_version);
	if( net_version > local_version ) {
		char cmd[1024];
		char net_version_str[16], *s;

		Q_snprintfz( net_version_str, sizeof( net_version_str ), "%4.3f", net_version );
		s = net_version_str + strlen( net_version_str ) - 1;
		while( *s == '0' ) s--;
		if( *s == '.' && *( s + 1 ) == '0' ) {
			s++;                                // for whole version numbers
		}
		net_version_str[s - net_version_str + 1] = '\0';

		// you should update
		Com_Printf( APPLICATION " version %s is available.\nVisit " APP_URL " for more information\n", net_version_str );
		Q_snprintfz( cmd, sizeof( cmd ), "menu_modal modal_update version \"%s\" app \"" APPLICATION "\""
					 " url " "\"" APP_URL "\"", net_version_str );
		Cbuf_ExecuteText( EXEC_APPEND, cmd );
	} else if( net_version == local_version ) {
		Com_Printf( "Your %s version is up-to-date.\n", APPLICATION );
	}

done:
	if( updateRemoteData ) {
		Mem_Free( updateRemoteData );
		updateRemoteData = NULL;
		updateRemoteDataSize = 0;
	}
}

/*
* CL_CheckForUpdateReadCb
*/
static size_t CL_CheckForUpdateReadCb( const void *buf, size_t numb, float percentage,
									   int status, const char *contentType, void *privatep ) {
	char *newbuf;

	if( status < 0 || status >= 300 ) {
		return 0;
	}

	newbuf = Mem_ZoneMalloc( updateRemoteDataSize + numb + 1 );
	memcpy( newbuf, updateRemoteData, updateRemoteDataSize - 1 );
	memcpy( newbuf + updateRemoteDataSize - 1, buf, numb );
	newbuf[numb] = '\0'; // EOF

	Mem_Free( updateRemoteData );
	updateRemoteData = newbuf;
	updateRemoteDataSize = updateRemoteDataSize + numb + 1;

	return numb;
}

#define TRACKING_PROFILE_ID "profile.id"

/*
* CL_CheckForUpdateHeaderCb
*
* Read the Set-Profile-Id header which works as a cookie and
* store the read value in a file. All subsequent update requests
* will echo back this id.
*/
static void CL_CheckForUpdateHeaderCb( const char *buf, void *privatep ) {
	const char *str;

	if( ( str  = (char*)strstr( buf, "SET-PROFILE-ID:" ) ) ) {
		const char *val;
		size_t val_size;

		str += 15;
		while( *str && ( *str == ' ' ) ) {
			str++;
		}

		val = str;
		while( *str && ( *str != '\r' ) && ( *str != '\n' ) ) {
			str++;
		}
		val_size = str - val;

		if( val_size > 0 ) {
			int filenum;

			if( FS_FOpenFile( TRACKING_PROFILE_ID, &filenum, FS_WRITE ) < 0 ) {
				return;
			}

			FS_Write( val, val_size - 1, filenum );
			FS_FCloseFile( filenum );
		}
	}
}

/*
* CL_CheckForUpdate
*
* retrieve a file with the last version umber on a web server, compare with current version
* display a message box in case the user need to update
*/
static void CL_CheckForUpdate( void ) {
#ifdef PUBLIC_BUILD
#define HTTP_HEADER_SIZE    128

	char url[MAX_STRING_CHARS];
	char *resolution;
	char *campaign;
	int campaignSize;
	char *profileId;
	int profileIdSize;
	int headerNum = 0;
	const char *headers[] = {
		NULL, NULL,
		NULL, NULL,
		NULL, NULL,
		NULL, NULL,
		NULL, NULL,
		NULL, NULL,
		NULL
	};

	if( !cl_checkForUpdate->integer ) {
		return;
	}
	if( Steam_GetSteamID() ) {
		return;
	}

	if( updateRemoteData ) {
		return; // still not done with the previous iteration?..

	}
	// step one get the last version file
	Com_Printf( "Checking for " APPLICATION " update.\n" );

	Q_snprintfz( url, sizeof( url ), "%s%s", APP_UPDATE_URL, APP_CLIENT_UPDATE_FILE );

	updateRemoteDataSize = 1;
	updateRemoteData = Mem_ZoneMalloc( 1 );
	*updateRemoteData = '\0';

	// send screen resolution in UA-pixels header
	resolution = Mem_TempMalloc( HTTP_HEADER_SIZE );
	Q_snprintfz( resolution, HTTP_HEADER_SIZE, "%ix%i", viddef.width, viddef.height );
	headers[headerNum++] = "UA-pixels";
	headers[headerNum++] = resolution;

	// send campaign ID
	campaign = NULL;
	campaignSize = FS_LoadBaseFile( "campaign.txt", (void **)&campaign, NULL, 0 );
	if( campaignSize > 0 ) {
		headers[headerNum++] = "X-Campaign";
		headers[headerNum++] = campaign;
	}

	// send profile ID
	profileId = NULL;
	profileIdSize = FS_LoadFile( TRACKING_PROFILE_ID, (void **)&profileId, NULL, 0 );
	if( profileIdSize > 0 ) {
		headers[headerNum++] = "X-Profile-Id";
		headers[headerNum++] = Q_strlwr( profileId );
	}

	// send language
	headers[headerNum++] = "X-Lang";
	headers[headerNum++] = L10n_GetUserLanguage();

	headerNum += CL_AddSessionHttpRequestHeaders( url, &headers[headerNum] );

	CL_AsyncStreamRequest( url, headers, 15, 0, CL_CheckForUpdateReadCb, CL_CheckForUpdateDoneCb,
						   CL_CheckForUpdateHeaderCb, NULL, false );

	Mem_TempFree( resolution );

	if( campaign ) {
		FS_FreeBaseFile( campaign );
	}
	if( profileId ) {
		FS_FreeBaseFile( profileId );
	}
#endif
}

//============================================================================

/*
* CL_AsyncStream_Alloc
*/
static void *CL_AsyncStream_Alloc( size_t size, const char *filename, int fileline ) {
	return _Mem_Alloc( zoneMemPool, size, 0, 0, filename, fileline );
}

/*
* CL_AsyncStream_Free
*/
static void CL_AsyncStream_Free( void *data, const char *filename, int fileline ) {
	_Mem_Free( data, 0, 0, filename, fileline );
}

/*
* CL_InitAsyncStream
*/
static void CL_InitAsyncStream( void ) {
	cl_async_stream = AsyncStream_InitModule( "Client", CL_AsyncStream_Alloc, CL_AsyncStream_Free );
}

/*
* CL_ShutdownAsyncStream
*/
static void CL_ShutdownAsyncStream( void ) {
	if( !cl_async_stream ) {
		return;
	}

	AsyncStream_ShutdownModule( cl_async_stream );
	cl_async_stream = NULL;
}

/*
* CL_AddSessionHttpRequestHeaders
*/
int CL_AddSessionHttpRequestHeaders( const char *url, const char **headers ) {
	static char pH[32];

	if( cls.httpbaseurl && *cls.httpbaseurl ) {
		if( !strncmp( url, cls.httpbaseurl, strlen( cls.httpbaseurl ) ) ) {
			Q_snprintfz( pH, sizeof( pH ), "%i", cl.playernum );

			headers[0] = "X-Client";
			headers[1] = pH;
			headers[2] = "X-Session";
			headers[3] = cls.session;
			return 4;
		}
	}
	return 0;
}

/*
* CL_AsyncStreamRequest
*/
void CL_AsyncStreamRequest( const char *url, const char **headers, int timeout, int resumeFrom,
							size_t ( *read_cb )( const void *, size_t, float, int, const char *, void * ),
							void ( *done_cb )( int, const char *, void * ),
							void ( *header_cb )( const char *, void * ), void *privatep, bool urlencodeUnsafe ) {
	char *tmpUrl = NULL;
	const char *safeUrl;

	if( urlencodeUnsafe ) {
		// urlencode unsafe characters
		size_t allocSize = strlen( url ) * 3 + 1;
		tmpUrl = ( char * )Mem_TempMalloc( allocSize );
		AsyncStream_UrlEncodeUnsafeChars( url, tmpUrl, allocSize );

		safeUrl = tmpUrl;
	} else {
		safeUrl = url;
	}

	AsyncStream_PerformRequestExt( cl_async_stream, safeUrl, "GET", NULL, headers, timeout,
								   resumeFrom, read_cb, done_cb, (async_stream_header_cb_t)header_cb, NULL );

	if( urlencodeUnsafe ) {
		Mem_TempFree( tmpUrl );
	}
}

//============================================================================

/*
* CL_Init
*/
void CL_Init( void ) {
	netadr_t address;
	cvar_t *cl_port;
	cvar_t *cl_port6;

	assert( !cl_initialized );

	if( dedicated->integer ) {
		return; // nothing running on the client

	}
	cl_initialized = true;

	// all archived variables will now be loaded

	Con_Init();

	CL_Sys_Init();

	// init localization subsystem
	L10n_Init();

	Steam_Init();

	VID_Init();

	CL_ClearState();

	// IPv4
	NET_InitAddress( &address, NA_IP );
	cl_port = Cvar_Get( "cl_port", "0", CVAR_NOSET );
	NET_SetAddressPort( &address, cl_port->integer );
	if( !NET_OpenSocket( &cls.socket_udp, SOCKET_UDP, &address, false ) ) {
		Com_Error( ERR_FATAL, "Couldn't open UDP socket: %s", NET_ErrorString() );
	}

	// IPv6
	NET_InitAddress( &address, NA_IP6 );
	cl_port6 = Cvar_Get( "cl_port6", "0", CVAR_NOSET );
	NET_SetAddressPort( &address, cl_port6->integer );
	if( !NET_OpenSocket( &cls.socket_udp6, SOCKET_UDP, &address, false ) ) {
		Com_Printf( "Error: Couldn't open UDP6 socket: %s", NET_ErrorString() );
	}

	SCR_InitScreen();
	cls.disable_screen = true; // don't draw yet

	CL_InitCinematics();

	CL_InitLocal();
	CL_InitInput();

	CL_InitAsyncStream();

	CL_InitMedia();

	CL_UIModule_ForceMenuOn();

	// check for update
	CL_CheckForUpdate();

	CL_InitServerList();

	CL_MM_Init();

	ML_Init();

	CL_Mumble_Init();
}

/*
* CL_Shutdown
*
* FIXME: this is a callback from Sys_Quit and Com_Error.  It would be better
* to run quit through here before the final handoff to the sys code.
*/
void CL_Shutdown( void ) {
	if( !cl_initialized ) {
		return;
	}

	CL_SoundModule_StopAllSounds( true, true );

	ML_Shutdown();
	CL_MM_Shutdown( true );
	CL_ShutDownServerList();

	CL_WriteConfiguration( "config.cfg", true );

	CL_Disconnect( NULL );
	NET_CloseSocket( &cls.socket_udp );
	NET_CloseSocket( &cls.socket_udp6 );
	// TOCHECK: Shouldn't we close the TCP socket too?
	if( cls.servername ) {
		Mem_ZoneFree( cls.servername );
		cls.servername = NULL;
	}

	CL_UIModule_Shutdown();
	CL_GameModule_Shutdown();
	CL_SoundModule_Shutdown( true );
	CL_ShutdownInput();
	CL_Mumble_Shutdown();
	L10n_Shutdown();
	VID_Shutdown();

	CL_ShutdownMedia();

	CL_ShutdownCinematics();

	CL_ShutdownAsyncStream();

	CL_ShutdownLocal();

	SCR_ShutdownScreen();

	Steam_Shutdown();

	CL_Sys_Shutdown();

	Con_Shutdown();

	cls.state = CA_UNINITIALIZED;
	cl_initialized = false;
}
