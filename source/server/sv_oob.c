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
#include "../matchmaker/mm_common.h"

typedef struct sv_master_s {
	netadr_t address;
	bool steam;
} sv_master_t;

static sv_master_t sv_masters[MAX_MASTERS];

extern cvar_t *sv_masterservers;
extern cvar_t *sv_masterservers_steam;
extern cvar_t *sv_hostname;
extern cvar_t *sv_skilllevel;
extern cvar_t *sv_reconnectlimit;     // minimum seconds between connect messages
extern cvar_t *rcon_password;         // password for remote server commands
extern cvar_t *sv_iplimit;


//==============================================================================
//
//MASTER SERVERS MANAGEMENT
//
//==============================================================================


/*
* SV_AddMaster_f
* Add a master server to the list
*/
static void SV_AddMaster_f( char *address, bool steam ) {
	int i;

	if( !address || !address[0] ) {
		return;
	}

	if( !sv_public->integer ) {
		Com_Printf( "'SV_AddMaster_f' Only public servers use masters.\n" );
		return;
	}

	//never go public when not acting as a game server
	if( sv.state > ss_game ) {
		return;
	}

	for( i = 0; i < MAX_MASTERS; i++ ) {
		sv_master_t *master = &sv_masters[i];

		if( master->address.type != NA_NOTRANSMIT ) {
			continue;
		}

		if( !NET_StringToAddress( address, &master->address ) ) {
			Com_Printf( "'SV_AddMaster_f' Bad Master server address: %s\n", address );
			return;
		}

		if( NET_GetAddressPort( &master->address ) == 0 ) {
			NET_SetAddressPort( &master->address, steam ? PORT_MASTER_STEAM : PORT_MASTER );
		}

		master->steam = steam;

		Com_Printf( "Added new master server #%i at %s\n", i, NET_AddressToString( &master->address ) );
		return;
	}

	Com_Printf( "'SV_AddMaster_f' List of master servers is already full\n" );
}

/*
* SV_ResolveMaster
*/
static void SV_ResolveMaster( void ) {
	char *master, *mlist;

	// wsw : jal : initialize masters list
	memset( sv_masters, 0, sizeof( sv_masters ) );

	//never go public when not acting as a game server
	if( sv.state > ss_game ) {
		return;
	}

	if( !sv_public->integer ) {
		return;
	}

	mlist = sv_masterservers->string;
	if( *mlist ) {
		while( mlist ) {
			master = COM_Parse( &mlist );
			if( !master[0] ) {
				break;
			}

			SV_AddMaster_f( master, false );
		}
	}

#if APP_STEAMID
	mlist = sv_masterservers_steam->string;
	if( *mlist ) {
		while( mlist ) {
			master = COM_Parse( &mlist );
			if( !master[0] ) {
				break;
			}

			SV_AddMaster_f( master, true );
		}
	}
#endif

	svc.lastMasterResolve = Sys_Milliseconds();
}

/*
* SV_InitMaster
* Set up the main master server
*/
void SV_InitMaster( void ) {
	SV_ResolveMaster();

	svc.nextHeartbeat = Sys_Milliseconds() + HEARTBEAT_SECONDS * 1000; // wait a while before sending first heartbeat
}

/*
* SV_UpdateMaster
*/
void SV_UpdateMaster( void ) {
	// refresh master server IP addresses periodically
	if( svc.lastMasterResolve + TTL_MASTERS < Sys_Milliseconds() ) {
		SV_ResolveMaster();
	}
}

/*
* SV_MasterHeartbeat
* Send a message to the master every few minutes to
* let it know we are alive, and log information
*/
void SV_MasterHeartbeat( void ) {
	int64_t time = Sys_Milliseconds();
	int i;

	if( svc.nextHeartbeat > time ) {
		return;
	}

	svc.nextHeartbeat = time + HEARTBEAT_SECONDS * 1000;

	if( !sv_public->integer || ( sv_maxclients->integer == 1 ) ) {
		return;
	}

	// never go public when not acting as a game server
	if( sv.state > ss_game ) {
		return;
	}

	// send to group master
	for( i = 0; i < MAX_MASTERS; i++ ) {
		sv_master_t *master = &sv_masters[i];

		if( master->address.type != NA_NOTRANSMIT ) {
			socket_t *socket;

			if( dedicated && dedicated->integer ) {
				Com_Printf( "Sending heartbeat to %s\n", NET_AddressToString( &master->address ) );
			}

			socket = ( master->address.type == NA_IP6 ? &svs.socket_udp6 : &svs.socket_udp );

			if( master->steam ) {
				uint8_t steamHeartbeat = 'q';
				NET_SendPacket( socket, &steamHeartbeat, sizeof( steamHeartbeat ), &master->address );
			} else {
				// warning: "DarkPlaces" is a protocol name here, not a game name. Do not replace it.
				Netchan_OutOfBandPrint( socket, &master->address, "heartbeat DarkPlaces\n" );
			}
		}
	}
}

/*
* SV_MasterSendQuit
* Notifies Steam master servers that the server is shutting down.
*/
void SV_MasterSendQuit( void ) {
	int i;
	const char quitMessage[] = "b\n";

	if( !sv_public->integer || ( sv_maxclients->integer == 1 ) ) {
		return;
	}

	// never go public when not acting as a game server
	if( sv.state > ss_game ) {
		return;
	}

	// send to group master
	for( i = 0; i < MAX_MASTERS; i++ ) {
		sv_master_t *master = &sv_masters[i];

		if( master->steam && ( master->address.type != NA_NOTRANSMIT ) ) {
			socket_t *socket = ( master->address.type == NA_IP6 ? &svs.socket_udp6 : &svs.socket_udp );

			if( dedicated && dedicated->integer ) {
				Com_Printf( "Sending quit to %s\n", NET_AddressToString( &master->address ) );
			}

			NET_SendPacket( socket, ( const uint8_t * )quitMessage, sizeof( quitMessage ), &master->address );
		}
	}
}


//============================================================================

/*
* SV_LongInfoString
* Builds the string that is sent as heartbeats and status replies
*/
static char *SV_LongInfoString( bool fullStatus ) {
	char tempstr[1024] = { 0 };
	const char *gametype;
	static char status[MAX_MSGLEN - 16];
	int i, bots, count;
	client_t *cl;
	size_t statusLength;
	size_t tempstrLength;

	Q_strncpyz( status, Cvar_Serverinfo(), sizeof( status ) );

	// convert "g_gametype" to "gametype"
	gametype = Info_ValueForKey( status, "g_gametype" );
	if( gametype ) {
		Info_RemoveKey( status, "g_gametype" );
		Info_SetValueForKey( status, "gametype", gametype );
	}

	statusLength = strlen( status );

	bots = 0;
	count = 0;
	for( i = 0; i < sv_maxclients->integer; i++ ) {
		cl = &svs.clients[i];
		if( cl->state >= CS_CONNECTED ) {
			if( cl->edict->r.svflags & SVF_FAKECLIENT || cl->tvclient ) {
				bots++;
			}
			count++;
		}
	}

	if( bots ) {
		Q_snprintfz( tempstr, sizeof( tempstr ), "\\bots\\%i", bots );
	}
	Q_snprintfz( tempstr + strlen( tempstr ), sizeof( tempstr ) - strlen( tempstr ), "\\clients\\%i%s", count, fullStatus ? "\n" : "" );
	tempstrLength = strlen( tempstr );
	if( statusLength + tempstrLength >= sizeof( status ) ) {
		return status; // can't hold any more
	}
	Q_strncpyz( status + statusLength, tempstr, sizeof( status ) - statusLength );
	statusLength += tempstrLength;

	if( fullStatus ) {
		for( i = 0; i < sv_maxclients->integer; i++ ) {
			cl = &svs.clients[i];
			if( cl->state >= CS_CONNECTED ) {
				Q_snprintfz( tempstr, sizeof( tempstr ), "%i %i \"%s\" %i\n",
							 cl->edict->r.client->r.frags, cl->ping, cl->name, cl->edict->s.team );
				tempstrLength = strlen( tempstr );
				if( statusLength + tempstrLength >= sizeof( status ) ) {
					break; // can't hold any more
				}
				Q_strncpyz( status + statusLength, tempstr, sizeof( status ) - statusLength );
				statusLength += tempstrLength;
			}
		}
	}

	return status;
}

/*
* SV_ShortInfoString
* Generates a short info string for broadcast scan replies
*/
#define MAX_STRING_SVCINFOSTRING 180
#define MAX_SVCINFOSTRING_LEN ( MAX_STRING_SVCINFOSTRING - 4 )
static char *SV_ShortInfoString( void ) {
	static char string[MAX_STRING_SVCINFOSTRING];
	char hostname[64];
	char entry[20];
	size_t len;
	int i, count, bots;
	int maxcount;
	const char *password;

	bots = 0;
	count = 0;
	for( i = 0; i < sv_maxclients->integer; i++ ) {
		if( svs.clients[i].state >= CS_CONNECTED ) {
			if( svs.clients[i].edict->r.svflags & SVF_FAKECLIENT || svs.clients[i].tvclient ) {
				bots++;
			} else {
				count++;
			}
		}
	}
	maxcount = sv_maxclients->integer - bots;

	//format:
	//" \377\377\377\377info\\n\\server_name\\m\\map name\\u\\clients/maxclients\\g\\gametype\\s\\skill\\EOT "

	Q_strncpyz( hostname, sv_hostname->string, sizeof( hostname ) );
	Q_snprintfz( string, sizeof( string ),
				 "\\\\n\\\\%s\\\\m\\\\%8s\\\\u\\\\%2i/%2i\\\\",
				 hostname,
				 sv.mapname,
				 count > 99 ? 99 : count,
				 maxcount > 99 ? 99 : maxcount
				 );

	len = strlen( string );
	Q_snprintfz( entry, sizeof( entry ), "g\\\\%6s\\\\", Cvar_String( "g_gametype" ) );
	if( MAX_SVCINFOSTRING_LEN - len > strlen( entry ) ) {
		Q_strncatz( string, entry, sizeof( string ) );
		len = strlen( string );
	}

	if( Q_stricmp( FS_GameDirectory(), FS_BaseGameDirectory() ) ) {
		Q_snprintfz( entry, sizeof( entry ), "mo\\\\%8s\\\\", FS_GameDirectory() );
		if( MAX_SVCINFOSTRING_LEN - len > strlen( entry ) ) {
			Q_strncatz( string, entry, sizeof( string ) );
			len = strlen( string );
		}
	}

	if( Cvar_Value( "g_instagib" ) ) {
		Q_snprintfz( entry, sizeof( entry ), "ig\\\\1\\\\" );
		if( MAX_SVCINFOSTRING_LEN - len > strlen( entry ) ) {
			Q_strncatz( string, entry, sizeof( string ) );
			len = strlen( string );
		}
	}


	Q_snprintfz( entry, sizeof( entry ), "s\\\\%1d\\\\", sv_skilllevel->integer );
	if( MAX_SVCINFOSTRING_LEN - len > strlen( entry ) ) {
		Q_strncatz( string, entry, sizeof( string ) );
		len = strlen( string );
	}

	password = Cvar_String( "password" );
	if( password[0] != '\0' ) {
		Q_snprintfz( entry, sizeof( entry ), "p\\\\1\\\\" );
		if( MAX_SVCINFOSTRING_LEN - len > strlen( entry ) ) {
			Q_strncatz( string, entry, sizeof( string ) );
			len = strlen( string );
		}
	}

	if( bots ) {
		Q_snprintfz( entry, sizeof( entry ), "b\\\\%2i\\\\", bots > 99 ? 99 : bots );
		if( MAX_SVCINFOSTRING_LEN - len > strlen( entry ) ) {
			Q_strncatz( string, entry, sizeof( string ) );
			len = strlen( string );
		}
	}

	if( SV_MM_Initialized() ) {
		Q_snprintfz( entry, sizeof( entry ), "mm\\\\1\\\\" );
		if( MAX_SVCINFOSTRING_LEN - len > strlen( entry ) ) {
			Q_strncatz( string, entry, sizeof( string ) );
			len = strlen( string );
		}
	}

	if( Cvar_Value( "g_race_gametype" ) ) {
		Q_snprintfz( entry, sizeof( entry ), "r\\\\1\\\\" );
		if( MAX_SVCINFOSTRING_LEN - len > strlen( entry ) ) {
			Q_strncatz( string, entry, sizeof( string ) );
			len = strlen( string );
		}
	}

	// finish it
	Q_strncatz( string, "EOT", sizeof( string ) );
	return string;
}



//==============================================================================
//
//OUT OF BAND COMMANDS
//
//==============================================================================


/*
* SVC_Ack
*/
static void SVC_Ack( const socket_t *socket, const netadr_t *address ) {
	Com_Printf( "Ping acknowledge from %s\n", NET_AddressToString( address ) );
}

/*
* SVC_Ping
* Just responds with an acknowledgement
*/
static void SVC_Ping( const socket_t *socket, const netadr_t *address ) {
	// send any arguments back with ack
	Netchan_OutOfBandPrint( socket, address, "ack %s", Cmd_Args() );
}

/*
* SVC_InfoResponse
*
* Responds with short info for broadcast scans
* The second parameter should be the current protocol version number.
*/
static void SVC_InfoResponse( const socket_t *socket, const netadr_t *address ) {
	int i, count;
	char *string;
	bool allow_empty = false, allow_full = false;

	if( sv_showInfoQueries->integer ) {
		Com_Printf( "Info Packet %s\n", NET_AddressToString( address ) );
	}

	// KoFFiE: When not public and coming from a LAN address
	//         assume broadcast and respond anyway, otherwise ignore
	if( ( ( !sv_public->integer ) && ( !NET_IsLANAddress( address ) ) ) ||
		( sv_maxclients->integer == 1 ) ) {
		return;
	}

	// ignore when in invalid server state
	if( sv.state < ss_loading || sv.state > ss_game ) {
		return;
	}

	// don't reply when we are locked for mm
	// if( SV_MM_IsLocked() )
	//	return;

	// different protocol version
	if( atoi( Cmd_Argv( 1 ) ) != APP_PROTOCOL_VERSION ) {
		return;
	}

	// check for full/empty filtered states
	for( i = 0; i < Cmd_Argc(); i++ ) {
		if( !Q_stricmp( Cmd_Argv( i ), "full" ) ) {
			allow_full = true;
		}

		if( !Q_stricmp( Cmd_Argv( i ), "empty" ) ) {
			allow_empty = true;
		}
	}

	count = 0;
	for( i = 0; i < sv_maxclients->integer; i++ ) {
		if( svs.clients[i].state >= CS_CONNECTED ) {
			count++;
		}
	}

	if( ( count == sv_maxclients->integer ) && !allow_full ) {
		return;
	}

	if( ( count == 0 ) && !allow_empty ) {
		return;
	}

	string = SV_ShortInfoString();
	if( string ) {
		Netchan_OutOfBandPrint( socket, address, "info\n%s", string );
	}
}

/*
* SVC_SendInfoString
*/
static void SVC_SendInfoString( const socket_t *socket, const netadr_t *address, const char *requestType, const char *responseType, bool fullStatus ) {
	char *string;

	if( sv_showInfoQueries->integer ) {
		Com_Printf( "%s Packet %s\n", requestType, NET_AddressToString( address ) );
	}

	// KoFFiE: When not public and coming from a LAN address
	//         assume broadcast and respond anyway, otherwise ignore
	if( ( ( !sv_public->integer ) && ( !NET_IsLANAddress( address ) ) ) ||
		( sv_maxclients->integer == 1 ) ) {
		return;
	}

	// ignore when in invalid server state
	if( sv.state < ss_loading || sv.state > ss_game ) {
		return;
	}

	// don't reply when we are locked for mm
	// if( SV_MM_IsLocked() )
	//	return;

	// send the same string that we would give for a status OOB command
	string = SV_LongInfoString( fullStatus );
	if( string ) {
		Netchan_OutOfBandPrint( socket, address, "%s\n\\challenge\\%s%s", responseType, Cmd_Argv( 1 ), string );
	}
}

/*
* SVC_GetInfoResponse
*/
static void SVC_GetInfoResponse( const socket_t *socket, const netadr_t *address ) {
	SVC_SendInfoString( socket, address, "GetInfo", "infoResponse", false );
}

/*
* SVC_GetStatusResponse
*/
static void SVC_GetStatusResponse( const socket_t *socket, const netadr_t *address ) {
	SVC_SendInfoString( socket, address, "GetStatus", "statusResponse", true );
}


/*
* SVC_GetChallenge
*
* Returns a challenge number that can be used
* in a subsequent client_connect command.
* We do this to prevent denial of service attacks that
* flood the server with invalid connection IPs.  With a
* challenge, they must give a valid IP address.
*/
static void SVC_GetChallenge( const socket_t *socket, const netadr_t *address ) {
	int i;
	int oldest;
	int oldestTime;

	oldest = 0;
	oldestTime = 0x7fffffff;

	if( sv_showChallenge->integer ) {
		Com_Printf( "Challenge Packet %s\n", NET_AddressToString( address ) );
	}

	// see if we already have a challenge for this ip
	for( i = 0; i < MAX_CHALLENGES; i++ ) {
		if( NET_CompareBaseAddress( address, &svs.challenges[i].adr ) ) {
			break;
		}
		if( svs.challenges[i].time < oldestTime ) {
			oldestTime = svs.challenges[i].time;
			oldest = i;
		}
	}

	if( i == MAX_CHALLENGES ) {
		// overwrite the oldest
		svs.challenges[oldest].challenge = rand() & 0x7fff;
		svs.challenges[oldest].adr = *address;
		svs.challenges[oldest].time = Sys_Milliseconds();
		i = oldest;
	}

	Netchan_OutOfBandPrint( socket, address, "challenge %i", svs.challenges[i].challenge );
}


/*
* SVC_DirectConnect
* A connection request that did not come from the master
*/
static void SVC_DirectConnect( const socket_t *socket, const netadr_t *address ) {
#ifdef TCP_ALLOW_CONNECT
	int incoming = 0;
#endif
	char userinfo[MAX_INFO_STRING];
	client_t *cl, *newcl;
	int i, version, game_port, challenge;
	int previousclients;
	mm_uuid_t session_id, ticket_id;
	char *session_id_str;
	bool tv_client;
	int64_t time;

	Com_DPrintf( "SVC_DirectConnect (%s)\n", Cmd_Args() );

	version = atoi( Cmd_Argv( 1 ) );
	if( version != APP_PROTOCOL_VERSION ) {
		if( version <= 6 ) { // before reject packet was added
			Netchan_OutOfBandPrint( socket, address, "print\nServer is version %4.2f. Protocol %3i\n",
									APP_VERSION, APP_PROTOCOL_VERSION );
		} else {
			Netchan_OutOfBandPrint( socket, address,
									"reject\n%i\n%i\nServer and client don't have the same version\n", DROP_TYPE_GENERAL, 0 );
		}
		Com_DPrintf( "    rejected connect from protocol %i\n", version );
		return;
	}

	game_port = atoi( Cmd_Argv( 2 ) );
	challenge = atoi( Cmd_Argv( 3 ) );
	tv_client = ( atoi( Cmd_Argv( 5 ) ) & 1 ? true : false );

	if( !Info_Validate( Cmd_Argv( 4 ) ) ) {
		Netchan_OutOfBandPrint( socket, address, "reject\n%i\n%i\nInvalid userinfo string\n", DROP_TYPE_GENERAL, 0 );
		Com_DPrintf( "Connection from %s refused: invalid userinfo string\n", NET_AddressToString( address ) );
		return;
	}

	Q_strncpyz( userinfo, Cmd_Argv( 4 ), sizeof( userinfo ) );

	// force the IP key/value pair so the game can filter based on ip
	if( !Info_SetValueForKey( userinfo, "socket", NET_SocketTypeToString( socket->type ) ) ) {
		Netchan_OutOfBandPrint( socket, address, "reject\n%i\n%i\nError: Couldn't set userinfo (socket)\n",
								DROP_TYPE_GENERAL, 0 );
		Com_DPrintf( "Connection from %s refused: couldn't set userinfo (socket)\n", NET_AddressToString( address ) );
		return;
	}
	if( !Info_SetValueForKey( userinfo, "ip", NET_AddressToString( address ) ) ) {
		Netchan_OutOfBandPrint( socket, address, "reject\n%i\n%i\nError: Couldn't set userinfo (ip)\n",
								DROP_TYPE_GENERAL, 0 );
		Com_DPrintf( "Connection from %s refused: couldn't set userinfo (ip)\n", NET_AddressToString( address ) );
		return;
	}

	if( Cmd_Argc() >= 7 ) {
		// we have extended information, ticket-id and session-id
		Com_Printf( "Extended information %s\n", Cmd_Argv( 6 ) );
		if( !Uuid_FromString( Cmd_Argv( 6 ), &ticket_id ) ) {
			ticket_id = session_id = Uuid_ZeroUuid();
		} else {
			session_id_str = Info_ValueForKey( userinfo, "cl_mm_session" );
			if( !Uuid_FromString( session_id_str, &session_id ) ) {
				ticket_id = session_id = Uuid_ZeroUuid();
			}
		}
	} else {
		ticket_id = session_id = Uuid_ZeroUuid();
	}

#ifdef TCP_ALLOW_CONNECT
	if( socket->type == SOCKET_TCP ) {
		// find the connection
		for( i = 0; i < MAX_INCOMING_CONNECTIONS; i++ ) {
			if( !svs.incoming[i].active ) {
				continue;
			}

			if( NET_CompareAddress( &svs.incoming[i].address, address ) && socket == &svs.incoming[i].socket ) {
				break;
			}
		}
		if( i == MAX_INCOMING_CONNECTIONS ) {
			Com_Error( ERR_FATAL, "Incoming connection not found.\n" );
			return;
		}
		incoming = i;
	}
#endif

	// see if the challenge is valid
	for( i = 0; i < MAX_CHALLENGES; i++ ) {
		if( NET_CompareBaseAddress( address, &svs.challenges[i].adr ) ) {
			if( challenge == svs.challenges[i].challenge ) {
				svs.challenges[i].challenge = 0; // wsw : r1q2 : reset challenge
				svs.challenges[i].time = 0;
				NET_InitAddress( &svs.challenges[i].adr, NA_NOTRANSMIT );
				break; // good
			}
			Netchan_OutOfBandPrint( socket, address, "reject\n%i\n%i\nBad challenge\n",
									DROP_TYPE_GENERAL, DROP_FLAG_AUTORECONNECT );
			return;
		}
	}
	if( i == MAX_CHALLENGES ) {
		Netchan_OutOfBandPrint( socket, address, "reject\n%i\n%i\nNo challenge for address\n",
								DROP_TYPE_GENERAL, DROP_FLAG_AUTORECONNECT );
		return;
	}

	//r1: limit connections from a single IP
	if( sv_iplimit->integer ) {
		previousclients = 0;
		for( i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++ ) {
			if( cl->state == CS_FREE ) {
				continue;
			}
			if( NET_CompareBaseAddress( address, &cl->netchan.remoteAddress ) ) {
				//r1: zombies are less dangerous
				if( cl->state == CS_ZOMBIE ) {
					previousclients++;
				} else {
					previousclients += 2;
				}
			}
		}

		if( previousclients >= sv_iplimit->integer * 2 ) {
			Netchan_OutOfBandPrint( socket, address, "reject\n%i\n%i\nToo many connections from your host\n", DROP_TYPE_GENERAL,
									DROP_FLAG_AUTORECONNECT );
			Com_DPrintf( "%s:connect rejected : too many connections\n", NET_AddressToString( address ) );
			return;
		}
	}

	newcl = NULL;

	// if there is already a slot for this ip, reuse it
	time = Sys_Milliseconds();
	for( i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++ ) {
		if( cl->state == CS_FREE ) {
			continue;
		}
		if( NET_CompareAddress( address, &cl->netchan.remoteAddress ) ||
			( NET_CompareBaseAddress( address, &cl->netchan.remoteAddress ) && cl->netchan.game_port == game_port ) ) {
			if( !NET_IsLocalAddress( address ) &&
				( time - cl->lastconnect ) < (unsigned)( sv_reconnectlimit->integer * 1000 ) ) {
				Com_DPrintf( "%s:reconnect rejected : too soon\n", NET_AddressToString( address ) );
				return;
			}
			Com_Printf( "%s:reconnect\n", NET_AddressToString( address ) );
			newcl = cl;
			break;
		}
	}

	// find a client slot
	if( !newcl ) {
		for( i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++ ) {
			if( cl->state == CS_FREE ) {
				newcl = cl;
				break;
			}
			// overwrite fakeclient if no free spots found
			if( cl->state && cl->edict && ( cl->edict->r.svflags & SVF_FAKECLIENT ) ) {
				newcl = cl;
			}
		}
		if( !newcl ) {
			Netchan_OutOfBandPrint( socket, address, "reject\n%i\n%i\nServer is full\n", DROP_TYPE_GENERAL,
									DROP_FLAG_AUTORECONNECT );
			Com_DPrintf( "Server is full. Rejected a connection.\n" );
			return;
		}
		if( newcl->state && newcl->edict && ( newcl->edict->r.svflags & SVF_FAKECLIENT ) ) {
			SV_DropClient( newcl, DROP_TYPE_GENERAL, "%s", "Need room for a real player" );
		}
	}

	// get the game a chance to reject this connection or modify the userinfo
	if( !SV_ClientConnect( socket, address, newcl, userinfo, game_port, challenge, false,
						   tv_client, ticket_id, session_id ) ) {
		char *rejtype, *rejflag, *rejtypeflag, *rejmsg;

		rejtype = Info_ValueForKey( userinfo, "rejtype" );
		if( !rejtype ) {
			rejtype = "0";
		}
		rejflag = Info_ValueForKey( userinfo, "rejflag" );
		if( !rejflag ) {
			rejflag = "0";
		}
		// hax because Info_ValueForKey can only be called twice in a row
		rejtypeflag = va( "%s\n%s", rejtype, rejflag );

		rejmsg = Info_ValueForKey( userinfo, "rejmsg" );
		if( !rejmsg ) {
			rejmsg = "Game module rejected connection";
		}

		Netchan_OutOfBandPrint( socket, address, "reject\n%s\n%s\n", rejtypeflag, rejmsg );

		Com_DPrintf( "Game rejected a connection.\n" );
		return;
	}

	// send the connect packet to the client
	Netchan_OutOfBandPrint( socket, address, "client_connect\n%s", newcl->session );

	// free the incoming entry
#ifdef TCP_ALLOW_CONNECT
	if( socket->type == SOCKET_TCP ) {
		svs.incoming[incoming].active = false;
		svs.incoming[incoming].socket.open = false;
	}
#endif
}

/*
* SVC_FakeConnect
* (Not a real out of band command)
* A connection request that came from the game module
*/
int SVC_FakeConnect( char *fakeUserinfo, char *fakeSocketType, const char *fakeIP ) {
	mm_uuid_t session_id, ticket_id;
	int i;
	char userinfo[MAX_INFO_STRING];
	client_t *cl, *newcl;
	netadr_t address;

	Com_DPrintf( "SVC_FakeConnect ()\n" );

	if( !fakeUserinfo ) {
		fakeUserinfo = "";
	}
	if( !fakeIP ) {
		fakeIP = "127.0.0.1";
	}
	if( !fakeSocketType ) {
		fakeIP = "loopback";
	}

	Q_strncpyz( userinfo, fakeUserinfo, sizeof( userinfo ) );

	// force the IP key/value pair so the game can filter based on ip
	if( !Info_SetValueForKey( userinfo, "socket", fakeSocketType ) ) {
		return -1;
	}
	if( !Info_SetValueForKey( userinfo, "ip", fakeIP ) ) {
		return -1;
	}

	// find a client slot
	newcl = NULL;
	for( i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++ ) {
		if( cl->state == CS_FREE ) {
			newcl = cl;
			break;
		}
	}
	if( !newcl ) {
		Com_DPrintf( "Rejected a connection.\n" );
		return -1;
	}

	NET_InitAddress( &address, NA_NOTRANSMIT );
	// get the game a chance to reject this connection or modify the userinfo
	session_id = Uuid_ZeroUuid();
	ticket_id  = Uuid_ZeroUuid();
	if( !SV_ClientConnect( NULL, &address, newcl, userinfo, -1, -1, true, false, session_id, ticket_id ) ) {
		Com_DPrintf( "Game rejected a connection.\n" );
		return -1;
	}

	// directly call the game begin function
	newcl->state = CS_SPAWNED;
	ge->ClientBegin( newcl->edict );

	return NUM_FOR_EDICT( newcl->edict );
}

/*
* Rcon_Validate
*/
static int Rcon_Validate( void ) {
	if( !strlen( rcon_password->string ) ) {
		return 0;
	}

	if( strcmp( Cmd_Argv( 1 ), rcon_password->string ) ) {
		return 0;
	}

	return 1;
}

/*
* SVC_RemoteCommand
*
* A client issued an rcon command.
* Shift down the remaining args
* Redirect all printfs
*/
static void SVC_RemoteCommand( const socket_t *socket, const netadr_t *address ) {
	int i;
	char remaining[1024];
	flush_params_t extra;

	i = Rcon_Validate();

	if( i == 0 ) {
		Com_Printf( "Bad rcon from %s:\n%s\n", NET_AddressToString( address ), Cmd_Args() );
	} else {
		Com_Printf( "Rcon from %s:\n%s\n", NET_AddressToString( address ), Cmd_Args() );
	}

	extra.socket = socket;
	extra.address = address;
	Com_BeginRedirect( RD_PACKET, sv_outputbuf, SV_OUTPUTBUF_LENGTH, SV_FlushRedirect, ( const void * )&extra );

	if( sv_showRcon->integer ) {
		Com_Printf( "Rcon Packet %s\n", NET_AddressToString( address ) );
	}

	if( !Rcon_Validate() ) {
		Com_Printf( "Bad rcon_password.\n" );
	} else {
		remaining[0] = 0;

		for( i = 2; i < Cmd_Argc(); i++ ) {
			Q_strncatz( remaining, "\"", sizeof( remaining ) );
			Q_strncatz( remaining, Cmd_Argv( i ), sizeof( remaining ) );
			Q_strncatz( remaining, "\" ", sizeof( remaining ) );
		}

		Cmd_ExecuteString( remaining );
	}

	Com_EndRedirect();
}

#define MAX_STEAMQUERY_PACKETLEN 1260
#define MAX_STEAMQUERY_TAG_STRING 128

/**
 * Writes the tags of the server for filtering in the Steam server browser.
 *
 * @param tags string where to write the tags (at least MAX_STEAMQUERY_TAG_STRING bytes)
 */
static void SV_GetSteamTags( char *tags ) {
	// Currently there is no way to filter by tag in the game itself,
	// so this is mostly to make sure the tags aren't empty on old servers if they are added.

	Q_strncpyz( tags, Cvar_String( "g_gametype" ), MAX_STEAMQUERY_TAG_STRING );

	if( Cvar_Value( "g_instagib" ) ) {
		if( tags[0] ) {
			Q_strncatz( tags, ",", MAX_STEAMQUERY_TAG_STRING );
		}
		Q_strncatz( tags, "instagib", MAX_STEAMQUERY_TAG_STRING );
	}

	// If sv_tags cvar is added, every comma-separated tag from the cvar must be added separately
	// (so the last tag exceeding MAX_STEAMQUERY_TAG_STRING isn't cut off)
	// and validated not to contain any characters disallowed in userinfo (CVAR_SERVERINFO).
}

/**
 * Responds to a Steam server query.
 *
 * @param s       query string
 * @param socket  response socket
 * @param address response address
 * @param inmsg   message for arguments
 * @return whether the request was handled as a Steam query
 */
bool SV_SteamServerQuery( const char *s, const socket_t *socket, const netadr_t *address, msg_t *inmsg ) {
#if APP_STEAMID
	if( sv.state < ss_loading || sv.state > ss_game ) {
		return false; // server not running

	}
	if( ( !sv_public->integer && !NET_IsLANAddress( address ) ) || ( sv_maxclients->integer == 1 ) ) {
		return false;
	}

	if( !strcmp( s, "i" ) ) {
		// ping
		const char pingResponse[] = "j00000000000000";
		Netchan_OutOfBand( socket, address, sizeof( pingResponse ), ( const uint8_t * )pingResponse );
		return true;
	}

	if( !strcmp( s, "W" ) || !strcmp( s, "U\xFF\xFF\xFF\xFF" ) ) {
		// challenge - security feature, but since we don't send multiple packets always return 0
		const uint8_t challengeResponse[] = { 'A', 0, 0, 0, 0 };
		Netchan_OutOfBand( socket, address, sizeof( challengeResponse ), ( const uint8_t * )challengeResponse );
		return true;
	}

	if( !strcmp( s, "TSource Engine Query" ) ) {
		// server info
		char hostname[MAX_INFO_VALUE];
		char gamedir[MAX_QPATH];
		char gamename[128];
		char version[32];
		char tags[MAX_STEAMQUERY_TAG_STRING];
		int i, players = 0, bots = 0, maxclients = 0;
		int flags = 0x80 | 0x01; // game port | game ID containing app ID
		client_t *cl;
		msg_t msg;
		uint8_t msgbuf[MAX_STEAMQUERY_PACKETLEN - sizeof( int32_t )];

		if( sv_showInfoQueries->integer ) {
			Com_Printf( "Steam Info Packet %s\n", NET_AddressToString( address ) );
		}

		Q_strncpyz( hostname, COM_RemoveColorTokens( sv_hostname->string ), sizeof( hostname ) );
		if( !hostname[0] ) {
			Q_strncpyz( hostname, sv_hostname->dvalue, sizeof( hostname ) );
		}
		Q_strncpyz( gamedir, FS_GameDirectory(), sizeof( gamedir ) );

		Q_strncpyz( gamename, APPLICATION, sizeof( gamename ) );
		if( Cvar_Value( "g_instagib" ) ) {
			Q_strncatz( gamename, " IG", sizeof( gamename ) );
		}
		if( sv.configstrings[CS_GAMETYPETITLE][0] || sv.configstrings[CS_GAMETYPENAME][0] ) {
			Q_strncatz( gamename, " ", sizeof( gamename ) );
			Q_strncatz( gamename,
						sv.configstrings[sv.configstrings[CS_GAMETYPETITLE][0] ? CS_GAMETYPETITLE : CS_GAMETYPENAME],
						sizeof( gamename ) );
		}

		for( i = 0; i < sv_maxclients->integer; i++ ) {
			cl = &svs.clients[i];
			if( cl->state >= CS_CONNECTED ) {
				if( cl->tvclient ) { // exclude TV from the max players count
					continue;
				}
				if( cl->edict->r.svflags & SVF_FAKECLIENT ) {
					bots++;
				}
				players++;
			}
			maxclients++;
		}

		Q_snprintfz( version, sizeof( version ), "%i.%i.0.0", APP_VERSION_MAJOR, APP_VERSION_MINOR );

		SV_GetSteamTags( tags );
		if( tags[0] ) {
			flags |= 0x20;
		}

		MSG_Init( &msg, msgbuf, sizeof( msgbuf ) );
		MSG_WriteUint8( &msg, 'I' );
		MSG_WriteUint8( &msg, APP_PROTOCOL_VERSION );
		MSG_WriteString( &msg, hostname );
		MSG_WriteString( &msg, sv.mapname );
		MSG_WriteString( &msg, gamedir );
		MSG_WriteString( &msg, gamename );
		MSG_WriteInt16( &msg, 0 ); // app ID specified later
		MSG_WriteUint8( &msg, min( players, 99 ) );
		MSG_WriteUint8( &msg, min( maxclients, 99 ) );
		MSG_WriteUint8( &msg, min( bots, 99 ) );
		MSG_WriteUint8( &msg, ( dedicated && dedicated->integer ) ? 'd' : 'l' );
		MSG_WriteUint8( &msg, STEAMQUERY_OS );
		MSG_WriteUint8( &msg, Cvar_String( "password" )[0] ? 1 : 0 );
		MSG_WriteUint8( &msg, 0 ); // VAC insecure
		MSG_WriteString( &msg, version );
		MSG_WriteUint8( &msg, flags );
		// port
		MSG_WriteInt16( &msg, sv_port->integer );
		// tags
		if( flags & 0x20 ) {
			MSG_WriteString( &msg, tags );
		}
		// 64-bit game ID - needed to specify app ID
		MSG_WriteInt32( &msg, APP_STEAMID & 0xffffff );
		MSG_WriteInt32( &msg, 0 );
		Netchan_OutOfBand( socket, address, msg.cursize, msg.data );
		return true;
	}

	if( s[0] == 'U' ) {
		// players
		msg_t msg;
		uint8_t msgbuf[MAX_STEAMQUERY_PACKETLEN - sizeof( int32_t )];
		int i, players = 0;
		client_t *cl;
		char name[MAX_NAME_BYTES];
		int64_t time = Sys_Milliseconds();

		if( sv_showInfoQueries->integer ) {
			Com_Printf( "Steam Players Packet %s\n", NET_AddressToString( address ) );
		}

		MSG_Init( &msg, msgbuf, sizeof( msgbuf ) );
		MSG_WriteUint8( &msg, 'D' );
		MSG_WriteUint8( &msg, 0 );

		for( i = 0; i < sv_maxclients->integer; i++ ) {
			cl = &svs.clients[i];
			if( ( cl->state < CS_CONNECTED ) || cl->tvclient ) {
				continue;
			}

			Q_strncpyz( name, COM_RemoveColorTokens( cl->name ), sizeof( name ) );
			if( ( msg.cursize + 10 + strlen( name ) ) > sizeof( msgbuf ) ) {
				break;
			}

			MSG_WriteUint8( &msg, i );
			MSG_WriteString( &msg, name );
			MSG_WriteInt32( &msg, cl->edict->r.client->r.frags );
			MSG_WriteFloat( &msg, ( float )( time - cl->lastconnect ) * 0.001f );

			players++;
			if( players == 99 ) {
				break;
			}
		}

		msgbuf[1] = players;
		Netchan_OutOfBand( socket, address, msg.cursize, msg.data );
		return true;
	}

	if( !strcmp( s, "s" ) ) {
		// master server query, terminated by \n, followed by the challenge
		int i;
		bool fromMaster = false;
		int challenge;
		char gamedir[MAX_QPATH], basedir[MAX_QPATH], tags[MAX_STEAMQUERY_TAG_STRING];
		int players = 0, bots = 0, maxclients = 0;
		client_t *cl;
		char msg[MAX_STEAMQUERY_PACKETLEN];

		for( i = 0; i < MAX_MASTERS; i++ ) {
			if( sv_masters[i].steam && NET_CompareAddress( address, &sv_masters[i].address ) ) {
				fromMaster = true;
				break;
			}
		}
		if( !fromMaster ) {
			return true;
		}

		if( sv_showInfoQueries->integer ) {
			Com_Printf( "Steam Master Server Info Packet %s\n", NET_AddressToString( address ) );
		}

		challenge = MSG_ReadInt32( inmsg );

		Q_strncpyz( gamedir, FS_GameDirectory(), sizeof( gamedir ) );
		Q_strncpyz( basedir, FS_BaseGameDirectory(), sizeof( basedir ) );
		SV_GetSteamTags( tags );

		for( i = 0; i < sv_maxclients->integer; i++ ) {
			cl = &svs.clients[i];
			if( cl->state >= CS_CONNECTED ) {
				if( cl->tvclient ) { // exclude TV from the max players count
					continue;
				}
				if( cl->edict->r.svflags & SVF_FAKECLIENT ) {
					bots++;
				}
				players++;
			}
			maxclients++;
		}

		Q_snprintfz( msg, sizeof( msg ),
					 "0\n\\protocol\\7\\challenge\\%i" // protocol must be 7 to match Source
					 "\\players\\%i\\max\\%i\\bots\\%i"
					 "\\gamedir\\%s\\map\\%s"
					 "\\password\\%i\\os\\%c"
					 "\\lan\\%i\\region\\255"
					 "%s%s"
					 "\\type\\%c\\secure\\0"
					 "\\version\\%i.%i.0.0"
					 "\\product\\%s\n",
					 challenge,
					 min( players, 99 ), min( maxclients, 99 ), min( bots, 99 ),
					 gamedir, sv.mapname,
					 Cvar_String( "password" )[0] ? 1 : 0, STEAMQUERY_OS,
					 sv_public->integer ? 0 : 1,
					 tags[0] ? "\\gametype\\" /* legacy - "gametype", not "tags" */ : "", tags,
					 ( dedicated && dedicated->integer ) ? 'd' : 'l',
					 APP_VERSION_MAJOR, APP_VERSION_MINOR,
					 basedir );
		NET_SendPacket( socket, ( const uint8_t * )msg, strlen( msg ), address );

		return true;
	}

	if( s[0] == 'O' ) {
		// out of date message
		static bool printed = false;
		if( !printed ) {
			int i;
			for( i = 0; i < MAX_MASTERS; i++ ) {
				if( sv_masters[i].steam && NET_CompareAddress( address, &sv_masters[i].address ) ) {
					Com_Printf( "Server is out of date and cannot be added to the Steam master servers.\n" );
					printed = true;
					return true;
				}
			}
		}
		return true;
	}
#endif

	return false;
}

typedef struct {
	char *name;
	void ( *func )( const socket_t *socket, const netadr_t *address );
} connectionless_cmd_t;

connectionless_cmd_t connectionless_cmds[] =
{
	{ "ping", SVC_Ping },
	{ "ack", SVC_Ack },
	{ "info", SVC_InfoResponse },
	{ "getinfo", SVC_GetInfoResponse },
	{ "getstatus", SVC_GetStatusResponse },
	{ "getchallenge", SVC_GetChallenge },
	{ "connect", SVC_DirectConnect },
	{ "rcon", SVC_RemoteCommand },
	//{ "cmd", SV_MMC_Cmd },

	{ NULL, NULL }
};

/*
* SV_ConnectionlessPacket
*
* A connectionless packet has four leading 0xff
* characters to distinguish it from a game channel.
* Clients that are in the game can still send
* connectionless packets.
*/
void SV_ConnectionlessPacket( const socket_t *socket, const netadr_t *address, msg_t *msg ) {
	connectionless_cmd_t *cmd;
	char *s, *c;

	MSG_BeginReading( msg );
	MSG_ReadInt32( msg );    // skip the -1 marker

	s = MSG_ReadStringLine( msg );

	if( SV_SteamServerQuery( s, socket, address, msg ) ) {
		return;
	}

	Cmd_TokenizeString( s );

	c = Cmd_Argv( 0 );
	Com_DPrintf( "Packet %s : %s\n", NET_AddressToString( address ), c );

	for( cmd = connectionless_cmds; cmd->name; cmd++ ) {
		if( !strcmp( c, cmd->name ) ) {
			cmd->func( socket, address );
			return;
		}
	}

	Com_DPrintf( "Bad connectionless packet from %s:\n%s\n", NET_AddressToString( address ), s );
}
