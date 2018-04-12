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

static bool sv_initialized = false;

mempool_t *sv_mempool;

// IPv4
cvar_t *sv_ip;
cvar_t *sv_port;

// IPv6
cvar_t *sv_ip6;
cvar_t *sv_port6;

cvar_t *sv_enforcetime;

cvar_t *sv_timeout;            // seconds without any message
cvar_t *sv_zombietime;         // seconds to sink messages after disconnect

cvar_t *rcon_password;         // password for remote server commands

cvar_t *sv_uploads_http;
cvar_t *sv_uploads_baseurl;
cvar_t *sv_uploads_demos;
cvar_t *sv_uploads_demos_baseurl;

cvar_t *sv_pure;
cvar_t *sv_pure_forcemodulepk3;

cvar_t *sv_maxclients;
cvar_t *sv_maxmvclients;

#ifdef TCP_ALLOW_CONNECT
cvar_t *sv_tcp;
#endif

#ifdef HTTP_SUPPORT
cvar_t *sv_http;
cvar_t *sv_http_ip;
cvar_t *sv_http_ipv6;
cvar_t *sv_http_port;
cvar_t *sv_http_upstream_baseurl;
cvar_t *sv_http_upstream_ip;
cvar_t *sv_http_upstream_realip_header;
#endif

cvar_t *sv_showclamp;
cvar_t *sv_showRcon;
cvar_t *sv_showChallenge;
cvar_t *sv_showInfoQueries;
cvar_t *sv_highchars;

cvar_t *sv_hostname;
cvar_t *sv_public;         // should heartbeats be sent
cvar_t *sv_defaultmap;

cvar_t *sv_iplimit;

cvar_t *sv_reconnectlimit; // minimum seconds between connect messages

// wsw : jal

cvar_t *sv_maxrate;
cvar_t *sv_compresspackets;
cvar_t *sv_masterservers;
cvar_t *sv_masterservers_steam;
cvar_t *sv_skilllevel;

// wsw : debug netcode
cvar_t *sv_debug_serverCmd;

cvar_t *sv_MOTD;
cvar_t *sv_MOTDFile;
cvar_t *sv_MOTDString;

cvar_t *sv_autoUpdate;
cvar_t *sv_lastAutoUpdate;

cvar_t *sv_demodir;

cvar_t *sv_snap_aggressive_sound_culling;
cvar_t *sv_snap_raycast_players_culling;
cvar_t *sv_snap_aggressive_fov_culling;
cvar_t *sv_snap_shadow_events_data;

//============================================================================

/*
* SV_CalcPings
*
* Updates the cl->ping variables
*/
static void SV_CalcPings( void ) {
	unsigned int i, j;
	client_t *cl;
	unsigned int total, count, lat, best;

	for( i = 0; i < (unsigned int)sv_maxclients->integer; i++ ) {
		cl = &svs.clients[i];
		if( cl->state != CS_SPAWNED ) {
			continue;
		}
		if( cl->edict && ( cl->edict->r.svflags & SVF_FAKECLIENT ) ) {
			continue;
		}

		total = 0;
		count = 0;
		best = 9999;
		for( j = 0; j < LATENCY_COUNTS; j++ ) {
			if( cl->frame_latency[j] > 0 ) {
				lat = (unsigned)cl->frame_latency[j];
				if( lat < best ) {
					best = lat;
				}

				total += lat;
				count++;
			}
		}

		if( !count ) {
			cl->ping = 0;
		} else
#if 1
		{ cl->ping = ( best + ( total / count ) ) * 0.5f;}
#else
		{ cl->ping = total / count;}
#endif
		// let the game dll know about the ping
		cl->edict->r.client->r.ping = cl->ping;
	}
}

/*
* SV_ProcessPacket
*/
static bool SV_ProcessPacket( netchan_t *netchan, msg_t *msg ) {
	int zerror;

	if( !Netchan_Process( netchan, msg ) ) {
		return false; // wasn't accepted for some reason

	}
	// now if compressed, expand it
	MSG_BeginReading( msg );
	MSG_ReadInt32( msg ); // sequence
	MSG_ReadInt32( msg ); // sequence_ack
	MSG_ReadInt16( msg ); // game_port
	if( msg->compressed ) {
		zerror = Netchan_DecompressMessage( msg );
		if( zerror < 0 ) {
			// compression error. Drop the packet
			Com_DPrintf( "SV_ProcessPacket: Compression error %i. Dropping packet\n", zerror );
			return false;
		}
	}

	return true;
}

/*
* SV_ReadPackets
*/
static void SV_ReadPackets( void ) {
	int i, socketind, ret;
	client_t *cl;
#ifdef TCP_ALLOW_CONNECT
	socket_t newsocket;
#endif
	int game_port;
	socket_t *socket;
	netadr_t address;

	static msg_t msg;
	static uint8_t msgData[MAX_MSGLEN];

#ifdef TCP_ALLOW_CONNECT
	socket_t* tcpsockets [] =
	{
		&svs.socket_tcp,
		&svs.socket_tcp6
	};
#endif

	socket_t* sockets [] =
	{
		&svs.socket_loopback,
		&svs.socket_udp,
		&svs.socket_udp6,
	};

	MSG_Init( &msg, msgData, sizeof( msgData ) );

#ifdef TCP_ALLOW_CONNECT
	for( socketind = 0; socketind < sizeof( tcpsockets ) / sizeof( tcpsockets[0] ); socketind++ ) {
		socket = tcpsockets[socketind];

		if( socket->open ) {
			while( true ) {
				// find a free slot
				for( i = 0; i < MAX_INCOMING_CONNECTIONS; i++ ) {
					if( !svs.incoming[i].active ) {
						break;
					}
				}
				if( i == MAX_INCOMING_CONNECTIONS ) {
					break;
				}

				if( ( ret = NET_Accept( socket, &newsocket, &address ) ) == 0 ) {
					break;
				}
				if( ret == -1 ) {
					Com_Printf( "NET_Accept: Error: %s\n", NET_ErrorString() );
					continue;
				}

				Com_Printf( "New TCP connection from %s\n", NET_AddressToString( &address ) );

				svs.incoming[i].active = true;
				svs.incoming[i].socket = newsocket;
				svs.incoming[i].address = address;
				svs.incoming[i].time = svs.realtime;
			}
		}

		for( i = 0; i < MAX_INCOMING_CONNECTIONS; i++ ) {
			if( !svs.incoming[i].active ) {
				continue;
			}

			ret = NET_GetPacket( &svs.incoming[i].socket, &address, &msg );
			if( ret == -1 ) {
				Com_Printf( "NET_GetPacket: Error: %s\n", NET_ErrorString() );
				NET_CloseSocket( &svs.incoming[i].socket );
				svs.incoming[i].active = false;
			} else if( ret == 1 ) {
				if( *(int *)msg.data != -1 ) {
					Com_Printf( "Sequence packet without connection\n" );
					NET_CloseSocket( &svs.incoming[i].socket );
					svs.incoming[i].active = false;
					continue;
				}

				Com_Printf( "Connectionless TCP packet from: %s\n", NET_AddressToString( &address ) );

				SV_ConnectionlessPacket( &svs.incoming[i].socket, &address, &msg );
			}
		}
	}
#endif

	for( socketind = 0; socketind < sizeof( sockets ) / sizeof( sockets[0] ); socketind++ ) {
		socket = sockets[socketind];

		if( !socket->open ) {
			continue;
		}

		while( ( ret = NET_GetPacket( socket, &address, &msg ) ) != 0 ) {
			if( ret == -1 ) {
				Com_Printf( "NET_GetPacket: Error: %s\n", NET_ErrorString() );
				continue;
			}

			// check for connectionless packet (0xffffffff) first
			if( *(int *)msg.data == -1 ) {
				SV_ConnectionlessPacket( socket, &address, &msg );
				continue;
			}

			// read the game port out of the message so we can fix up
			// stupid address translating routers
			MSG_BeginReading( &msg );
			MSG_ReadInt32( &msg ); // sequence number
			MSG_ReadInt32( &msg ); // sequence number
			game_port = MSG_ReadInt16( &msg ) & 0xffff;
			// data follows

			// check for packets from connected clients
			for( i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++ ) {
				unsigned short addr_port;

				if( cl->state == CS_FREE || cl->state == CS_ZOMBIE ) {
					continue;
				}
				if( cl->edict && ( cl->edict->r.svflags & SVF_FAKECLIENT ) ) {
					continue;
				}
				if( !NET_CompareBaseAddress( &address, &cl->netchan.remoteAddress ) ) {
					continue;
				}
				if( cl->netchan.game_port != game_port ) {
					continue;
				}

				addr_port = NET_GetAddressPort( &address );
				if( NET_GetAddressPort( &cl->netchan.remoteAddress ) != addr_port ) {
					Com_Printf( "SV_ReadPackets: fixing up a translated port\n" );
					NET_SetAddressPort( &cl->netchan.remoteAddress, addr_port );
				}

				if( SV_ProcessPacket( &cl->netchan, &msg ) ) { // this is a valid, sequenced packet, so process it
					cl->lastPacketReceivedTime = svs.realtime;
					SV_ParseClientMessage( cl, &msg );
				}
				break;
			}
		}
	}

	// handle clients with individual sockets
	for( i = 0; i < sv_maxclients->integer; i++ ) {
		cl = &svs.clients[i];

		if( cl->state == CS_ZOMBIE || cl->state == CS_FREE ) {
			continue;
		}

		if( !cl->individual_socket ) {
			continue;
		}

		// not while, we only handle one packet per client at a time here
		if( ( ret = NET_GetPacket( cl->netchan.socket, &address, &msg ) ) != 0 ) {
			if( ret == -1 ) {
				Com_Printf( "Error receiving packet from %s: %s\n", NET_AddressToString( &cl->netchan.remoteAddress ),
							NET_ErrorString() );
				if( cl->reliable ) {
					SV_DropClient( cl, DROP_TYPE_GENERAL, "Error receiving packet: %s", NET_ErrorString() );
				}
			} else {
				if( SV_ProcessPacket( &cl->netchan, &msg ) ) {
					// this is a valid, sequenced packet, so process it
					cl->lastPacketReceivedTime = svs.realtime;
					SV_ParseClientMessage( cl, &msg );
				}
			}
		}
	}
}

/*
* SV_CheckTimeouts
*
* If a packet has not been received from a client for timeout->value
* seconds, drop the conneciton.  Server frames are used instead of
* realtime to avoid dropping the local client while debugging.
*
* When a client is normally dropped, the client_t goes into a zombie state
* for a few seconds to make sure any final reliable message gets resent
* if necessary
*/
static void SV_CheckTimeouts( void ) {
	client_t *cl;
	int i;

#ifdef TCP_ALLOW_CONNECT
	// timeout incoming connections
	for( i = 0; i < MAX_INCOMING_CONNECTIONS; i++ ) {
		if( svs.incoming[i].active && svs.incoming[i].time + 1000 * 15 < svs.realtime ) {
			Com_Printf( "Incoming TCP connection from %s timed out\n", NET_AddressToString( &svs.incoming[i].address ) );
			NET_CloseSocket( &svs.incoming[i].socket );
			svs.incoming[i].active = false;
		}
	}
#endif

	// timeout clients
	for( i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++ ) {
		// fake clients do not timeout
		if( cl->edict && ( cl->edict->r.svflags & SVF_FAKECLIENT ) ) {
			cl->lastPacketReceivedTime = svs.realtime;
		}
		// message times may be wrong across a changelevel
		else if( cl->lastPacketReceivedTime > svs.realtime ) {
			cl->lastPacketReceivedTime = svs.realtime;
		}

		if( cl->state == CS_ZOMBIE && cl->lastPacketReceivedTime + 1000 * sv_zombietime->value < svs.realtime ) {
			cl->state = CS_FREE; // can now be reused
			if( cl->individual_socket ) {
				NET_CloseSocket( &cl->socket );
			}
			continue;
		}

		if( ( cl->state != CS_FREE && cl->state != CS_ZOMBIE ) &&
			( cl->lastPacketReceivedTime + 1000 * sv_timeout->value < svs.realtime ) ) {
			SV_DropClient( cl, DROP_TYPE_GENERAL, "%s", "Error: Connection timed out" );
			cl->state = CS_FREE; // don't bother with zombie state
			if( cl->socket.open ) {
				NET_CloseSocket( &cl->socket );
			}
		}

		// timeout downloads left open
		if( ( cl->state != CS_FREE && cl->state != CS_ZOMBIE ) &&
			( cl->download.name && cl->download.timeout < svs.realtime ) ) {
			Com_Printf( "Download of %s to %s%s timed out\n", cl->download.name, cl->name, S_COLOR_WHITE );
			SV_ClientCloseDownload( cl );
		}
	}
}

/*
* SV_CheckLatchedUserinfoChanges
*
* To prevent flooding other players, consecutive userinfo updates are delayed,
* and only the last one is applied.
* Applies latched userinfo updates if the timeout is over.
*/
static void SV_CheckLatchedUserinfoChanges( void ) {
	client_t *cl;
	int i;
	int64_t time = Sys_Milliseconds();

	for( i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++ ) {
		if( cl->state == CS_FREE || cl->state == CS_ZOMBIE ) {
			continue;
		}

		if( cl->userinfoLatched[0] && cl->userinfoLatchTimeout <= time ) {
			Q_strncpyz( cl->userinfo, cl->userinfoLatched, sizeof( cl->userinfo ) );

			cl->userinfoLatched[0] = '\0';

			SV_UserinfoChanged( cl );
		}
	}
}

//#define WORLDFRAMETIME 25 // 40fps
//#define WORLDFRAMETIME 20 // 50fps
#define WORLDFRAMETIME 16 // 62.5fps
/*
* SV_RunGameFrame
*/
static bool SV_RunGameFrame( int msec ) {
	static int64_t accTime = 0;
	bool refreshSnapshot;
	bool refreshGameModule;
	bool sentFragments;

	accTime += msec;

	refreshSnapshot = false;
	refreshGameModule = false;

	sentFragments = SV_SendClientsFragments();

	// see if it's time to run a new game frame
	if( accTime >= WORLDFRAMETIME ) {
		refreshGameModule = true;
	}

	// see if it's time for a new snapshot
	if( !sentFragments && svs.gametime >= sv.nextSnapTime ) {
		refreshSnapshot = true;
		refreshGameModule = true;
	}

	// if there aren't pending packets to be sent, we can sleep
	if( dedicated->integer && !sentFragments && !refreshSnapshot ) {
		int sleeptime = min( WORLDFRAMETIME - ( accTime + 1 ), sv.nextSnapTime - ( svs.gametime + 1 ) );

		if( sleeptime > 0 ) {
			socket_t *sockets [] = { &svs.socket_udp, &svs.socket_udp6 };
			socket_t *opened_sockets [sizeof( sockets ) / sizeof( sockets[0] ) + 1 ];
			size_t sock_ind, open_ind;

			// Pass only the opened sockets to the sleep function
			open_ind = 0;
			for( sock_ind = 0; sock_ind < sizeof( sockets ) / sizeof( sockets[0] ); sock_ind++ ) {
				socket_t *sock = sockets[sock_ind];
				if( sock->open ) {
					opened_sockets[open_ind] = sock;
					open_ind++;
				}
			}
			opened_sockets[open_ind] = NULL;

			NET_Sleep( sleeptime, opened_sockets );
		}
	}

	if( refreshGameModule ) {
		int64_t moduleTime;

		// update ping based on the last known frame from all clients
		SV_CalcPings();

		if( accTime >= WORLDFRAMETIME ) {
			moduleTime = WORLDFRAMETIME;
			accTime -= WORLDFRAMETIME;
			if( accTime >= WORLDFRAMETIME ) { // don't let it accumulate more than 1 frame
				accTime = WORLDFRAMETIME - 1;
			}
		} else {
			moduleTime = accTime;
			accTime = 0;
		}

		if( host_speeds->integer ) {
			time_before_game = Sys_Milliseconds();
		}

		ge->RunFrame( moduleTime, svs.gametime );

		if( host_speeds->integer ) {
			time_after_game = Sys_Milliseconds();
		}
	}

	// if we don't have to send a snapshot we are done here
	if( refreshSnapshot ) {
		int extraSnapTime;

		// set up for sending a snapshot
		sv.framenum++;
		ge->SnapFrame();

		// set time for next snapshot
		extraSnapTime = (int)( svs.gametime - sv.nextSnapTime );
		if( extraSnapTime > svc.snapFrameTime * 0.5 ) { // don't let too much time be accumulated
			extraSnapTime = svc.snapFrameTime * 0.5;
		}

		sv.nextSnapTime = svs.gametime + ( svc.snapFrameTime - extraSnapTime );

		return true;
	}

	return false;
}


/*
static bool SV_RunGameFrame( int msec )
{
int extraTime = 0;
static unsigned int accTime = 0;

accTime += msec;

// move autonomous things around if enough time has passed
if( svs.gametime < sv.nextSnapTime )
{
if( svs.gametime + svc.snapFrameTime < sv.nextSnapTime )
{
if( sv_showclamp->integer )
Com_Printf( "sv lowclamp\n" );
sv.nextSnapTime = svs.gametime + svc.snapFrameTime;
return false;
}

// see if it's time to advance the world
if( accTime >= WORLDFRAMETIME )
{
if( host_speeds->integer )
time_before_game = Sys_Milliseconds();

ge->RunFrame( WORLDFRAMETIME, svs.gametime );

if( host_speeds->integer )
time_after_game = Sys_Milliseconds();

accTime = accTime - WORLDFRAMETIME;
}

if( !SV_SendClientsFragments() )
{
// FIXME: gametime might slower/faster than real time
if( dedicated->integer )
{
socket_t *sockets[] = { &svs.socket_udp, NULL };
NET_Sleep( min( WORLDFRAMETIME - accTime, sv.nextSnapTime - svs.gametime ), sockets );
}
}

return false;
}

if( sv.nextSnapTime <= svs.gametime )
{
extraTime = (int)( svs.gametime - sv.nextSnapTime );
}
if( extraTime >= msec )
extraTime = msec - 1;

sv.nextSnapTime = ( svs.gametime + svc.snapFrameTime ) - extraTime;

// Execute all clients pending move commands
if( accTime )
{
ge->RunFrame( accTime, svs.gametime );
accTime = 0;
}

// update ping based on the last known frame from all clients
SV_CalcPings();

sv.framenum++;
ge->SnapFrame();

return true;
}
*/
static void SV_CheckDefaultMap( void ) {
	if( svc.autostarted ) {
		return;
	}

	svc.autostarted = true;
	if( dedicated->integer ) {
		if( ( sv.state == ss_dead ) && sv_defaultmap && strlen( sv_defaultmap->string ) && !strlen( sv.mapname ) ) {
			Cbuf_ExecuteText( EXEC_APPEND, va( "map %s\n", sv_defaultmap->string ) );
		}
	}
}

/*
* SV_UpdateActivity
*/
void SV_UpdateActivity( void ) {
	svc.lastActivity = Sys_Milliseconds();
	//Com_Printf( "Server activity\n" );
}

/*
* SV_CheckAutoUpdate
*/
static void SV_CheckAutoUpdate( void ) {
	int64_t days;
	int64_t uptimeMinute;

	if( !sv_pure->integer && sv_autoUpdate->integer ) {
		Com_Printf( "WARNING: Autoupdate is not available for unpure servers.\n" );
		Cvar_ForceSet( "sv_autoUpdate", "0" );
	}

	if( !sv_autoUpdate->integer || !dedicated->integer ) {
		return;
	}

	days = (unsigned int)sv_lastAutoUpdate->integer;
	uptimeMinute = ( Sys_Milliseconds() / 60000 ) % 60;

	// daily check
	if( ( days < Com_DaysSince1900() ) && ( uptimeMinute == svc.autoUpdateMinute ) ) {
		SV_AutoUpdateFromWeb( false );
	}
}

/*
* SV_CheckPostUpdateRestart
*/
static void SV_CheckPostUpdateRestart( void ) {
	// do not if there has been any activity in last 5 minutes
	if( ( svc.lastActivity + 300000 ) > Sys_Milliseconds() ) {
		return;
	}

	// if there are any new filesystem entries, restart
	if( FS_GetNotifications() & FS_NOTIFY_NEWPAKS ) {
		if( sv.state != ss_dead ) {
			// restart the current map, SV_Map also rescans the filesystem
			Com_Printf( "The server will now restart...\n\n" );

			// start the default map if current map isn't available
			Cbuf_ExecuteText( EXEC_APPEND, va( "map %s\n", svs.mapcmd[0] ? svs.mapcmd : sv_defaultmap->string ) );
		}
	}
}

/*
* SV_CheckMatchUUID_Callback
*/
static void SV_CheckMatchUUID_Callback( const char *uuid ) {
	Q_strncpyz( sv.configstrings[CS_MATCHUUID], uuid, sizeof( sv.configstrings[0] ) );
}

/*
* SV_CheckMatchUUID
*
* See if the game module or the server itself have reset the
* match UUID configstring. If so, and we're connected to the
* matchmaker, fetch a new UUID.
*/
static void SV_CheckMatchUUID( void ) {
	if( sv.configstrings[CS_MATCHUUID][0] != '\0' ) {
		return;
	}
	SV_MM_GetMatchUUID( &SV_CheckMatchUUID_Callback );
}

/*
* SV_Frame
*/
void SV_Frame( unsigned realmsec, unsigned gamemsec ) {
	time_before_game = time_after_game = 0;

	// if server is not active, do nothing
	if( !svs.initialized ) {
		SV_CheckDefaultMap();
		return;
	}

	svs.realtime += realmsec;
	svs.gametime += gamemsec;

	// check timeouts
	SV_CheckTimeouts();

	// get packets from clients
	SV_ReadPackets();

	// apply latched userinfo changes
	SV_CheckLatchedUserinfoChanges();

	// let everything in the world think and move
	if( SV_RunGameFrame( gamemsec ) ) {
		// send messages back to the clients that had packets read this frame
		SV_SendClientMessages();

		// write snap to server demo file
		SV_Demo_WriteSnap();

		// run matchmaker stuff
		SV_CheckMatchUUID();

		SV_MM_Frame();

		// send a heartbeat to the master if needed
		SV_MasterHeartbeat();

		// clear teleport flags, etc for next frame
		ge->ClearSnap();
	}

	// handle HTTP connections
	SV_Web_GameFrame( ge->WebRequest );

	SV_CheckAutoUpdate();

	SV_CheckPostUpdateRestart();
}

//============================================================================

/*
* SV_UserinfoChanged
*
* Pull specific info from a newly changed userinfo string
* into a more C friendly form.
*/
void SV_UserinfoChanged( client_t *client ) {
	char *val;
	int ival;

	assert( client );
	assert( Info_Validate( client->userinfo ) );

	if( !client->edict || !( client->edict->r.svflags & SVF_FAKECLIENT ) ) {
		// force the IP key/value pair so the game can filter based on ip
		if( !Info_SetValueForKey( client->userinfo, "socket", NET_SocketTypeToString( client->netchan.socket->type ) ) ) {
			SV_DropClient( client, DROP_TYPE_GENERAL, "%s", "Error: Couldn't set userinfo (socket)\n" );
			return;
		}
		if( !Info_SetValueForKey( client->userinfo, "ip", NET_AddressToString( &client->netchan.remoteAddress ) ) ) {
			SV_DropClient( client, DROP_TYPE_GENERAL, "%s", "Error: Couldn't set userinfo (ip)\n" );
			return;
		}
	}

	// mm session
	ival = 0;
	val = Info_ValueForKey( client->userinfo, "cl_mm_session" );
	if( val ) {
		ival = atoi( val );
	}
	if( !val || ival != client->mm_session ) {
		Info_SetValueForKey( client->userinfo, "cl_mm_session", va( "%d", client->mm_session ) );
	}

	// mm login
	if( client->mm_login[0] != '\0' ) {
		Info_SetValueForKey( client->userinfo, "cl_mm_login", client->mm_login );
	} else {
		Info_RemoveKey( client->userinfo, "cl_mm_login" );
	}

	// call prog code to allow overrides
	ge->ClientUserinfoChanged( client->edict, client->userinfo );

	if( !Info_Validate( client->userinfo ) ) {
		SV_DropClient( client, DROP_TYPE_GENERAL, "%s", "Error: Invalid userinfo (after game)" );
		return;
	}

	// we assume that game module deals with setting a correct name
	val = Info_ValueForKey( client->userinfo, "name" );
	if( !val || !val[0] ) {
		SV_DropClient( client, DROP_TYPE_GENERAL, "%s", "Error: No name set" );
		return;
	}
	Q_strncpyz( client->name, val, sizeof( client->name ) );

#ifndef RATEKILLED
	// rate command
	if( NET_IsLANAddress( &client->netchan.remoteAddress ) ) {
		client->rate = 99999; // lans should not rate limit
	} else {
		val = Info_ValueForKey( client->userinfo, "rate" );
		if( val && val[0] ) {
			int newrate;

			newrate = atoi( val );
			if( sv_maxrate->integer && newrate > sv_maxrate->integer ) {
				newrate = sv_maxrate->integer;
			} else if( newrate > 90000 ) {
				newrate = 90000;
			}
			if( newrate < 1000 ) {
				newrate = 1000;
			}
			if( client->rate != newrate ) {
				client->rate = newrate;
				Com_Printf( "%s%s has rate %i\n", client->name, S_COLOR_WHITE, client->rate );
			}
		} else {
			client->rate = 5000;
		}
	}
#endif
}


//============================================================================

/*
* SV_Init
*
* Only called at plat.exe startup, not for each game
*/
void SV_Init( void ) {
	cvar_t *sv_pps;
	cvar_t *sv_fps;

	assert( !sv_initialized );

	memset( &svc, 0, sizeof( svc ) );

	SV_InitOperatorCommands();

	sv_mempool = Mem_AllocPool( NULL, "Server" );

	Cvar_Get( "sv_cheats", "0", CVAR_SERVERINFO | CVAR_LATCH );
	Cvar_Get( "protocol", va( "%i", APP_PROTOCOL_VERSION ), CVAR_SERVERINFO | CVAR_NOSET );

	sv_ip =             Cvar_Get( "sv_ip", "", CVAR_ARCHIVE | CVAR_LATCH );
	sv_port =           Cvar_Get( "sv_port", va( "%i", PORT_SERVER ), CVAR_ARCHIVE | CVAR_LATCH );

	sv_ip6 =            Cvar_Get( "sv_ip6", "::", CVAR_ARCHIVE | CVAR_LATCH );
	sv_port6 =          Cvar_Get( "sv_port6", va( "%i", PORT_SERVER ), CVAR_ARCHIVE | CVAR_LATCH );

#ifdef TCP_ALLOW_CONNECT
	sv_tcp =            Cvar_Get( "sv_tcp", "1", CVAR_SERVERINFO | CVAR_ARCHIVE | CVAR_LATCH );
#endif

#ifdef HTTP_SUPPORT
	sv_http =           Cvar_Get( "sv_http", "1", CVAR_SERVERINFO | CVAR_ARCHIVE | CVAR_LATCH );
	sv_http_port =      Cvar_Get( "sv_http_port", va( "%i", PORT_HTTP_SERVER ), CVAR_ARCHIVE | CVAR_LATCH );
	sv_http_ip =        Cvar_Get( "sv_http_ip", "", CVAR_ARCHIVE | CVAR_LATCH );
	sv_http_ipv6 =      Cvar_Get( "sv_http_ipv6", "", CVAR_ARCHIVE | CVAR_LATCH );
	sv_http_upstream_baseurl =  Cvar_Get( "sv_http_upstream_baseurl", "", CVAR_ARCHIVE | CVAR_LATCH );
	sv_http_upstream_realip_header = Cvar_Get( "sv_http_upstream_realip_header", "", CVAR_ARCHIVE );
	sv_http_upstream_ip = Cvar_Get( "sv_http_upstream_ip", "", CVAR_ARCHIVE );
#endif

	rcon_password =         Cvar_Get( "rcon_password", "", 0 );
	sv_hostname =           Cvar_Get( "sv_hostname", APPLICATION " server", CVAR_SERVERINFO | CVAR_ARCHIVE );
	sv_timeout =            Cvar_Get( "sv_timeout", "125", 0 );
	sv_zombietime =         Cvar_Get( "sv_zombietime", "2", 0 );
	sv_enforcetime =        Cvar_Get( "sv_enforcetime", "1", 0 );
	sv_showclamp =          Cvar_Get( "sv_showclamp", "0", 0 );
	sv_showRcon =           Cvar_Get( "sv_showRcon", "1", 0 );
	sv_showChallenge =      Cvar_Get( "sv_showChallenge", "0", 0 );
	sv_showInfoQueries =    Cvar_Get( "sv_showInfoQueries", "0", 0 );
	sv_highchars =          Cvar_Get( "sv_highchars", "1", 0 );

	sv_uploads_http =       Cvar_Get( "sv_uploads_http", "1", CVAR_READONLY );
	sv_uploads_baseurl =    Cvar_Get( "sv_uploads_baseurl", "", CVAR_ARCHIVE );
	sv_uploads_demos =      Cvar_Get( "sv_uploads_demos", "1", CVAR_ARCHIVE );
	sv_uploads_demos_baseurl =  Cvar_Get( "sv_uploads_demos_baseurl", "", CVAR_ARCHIVE );
	if( dedicated->integer ) {
		sv_autoUpdate = Cvar_Get( "sv_autoUpdate", "1", CVAR_ARCHIVE );

		sv_pure =       Cvar_Get( "sv_pure", "1", CVAR_ARCHIVE | CVAR_LATCH | CVAR_SERVERINFO );

#ifdef PUBLIC_BUILD
		sv_public =     Cvar_Get( "sv_public", "1", CVAR_ARCHIVE | CVAR_LATCH );
#else
		sv_public =     Cvar_Get( "sv_public", "0", CVAR_ARCHIVE | CVAR_LATCH );
#endif
	} else {
		sv_autoUpdate = Cvar_Get( "sv_autoUpdate", "0", CVAR_READONLY );

		sv_pure =       Cvar_Get( "sv_pure", "0", CVAR_ARCHIVE | CVAR_LATCH | CVAR_SERVERINFO );
		sv_public =     Cvar_Get( "sv_public", "0", CVAR_ARCHIVE );
	}

	sv_iplimit = Cvar_Get( "sv_iplimit", "3", CVAR_ARCHIVE );

	sv_lastAutoUpdate = Cvar_Get( "sv_lastAutoUpdate", "0", CVAR_READONLY | CVAR_ARCHIVE );
	sv_pure_forcemodulepk3 =    Cvar_Get( "sv_pure_forcemodulepk3", "", CVAR_LATCH );

	sv_defaultmap =         Cvar_Get( "sv_defaultmap", "wdm1", CVAR_ARCHIVE );
	sv_reconnectlimit =     Cvar_Get( "sv_reconnectlimit", "3", CVAR_ARCHIVE );
	sv_maxclients =         Cvar_Get( "sv_maxclients", "16", CVAR_ARCHIVE | CVAR_SERVERINFO | CVAR_LATCH );
	sv_maxmvclients =       Cvar_Get( "sv_maxmvclients", "4", CVAR_ARCHIVE | CVAR_SERVERINFO );

	Cvar_Get( "sv_modmanifest", "", CVAR_READONLY );
	Cvar_ForceSet( "sv_modmanifest", "" );

	// fix invalid sv_maxclients values
	if( sv_maxclients->integer < 1 ) {
		Cvar_FullSet( "sv_maxclients", "1", CVAR_ARCHIVE | CVAR_SERVERINFO | CVAR_LATCH, true );
	} else if( sv_maxclients->integer > MAX_CLIENTS ) {
		Cvar_FullSet( "sv_maxclients", va( "%i", MAX_CLIENTS ), CVAR_ARCHIVE | CVAR_SERVERINFO | CVAR_LATCH, true );
	}

	sv_demodir = Cvar_Get( "sv_demodir", "", CVAR_NOSET );
	if( sv_demodir->string[0] && Com_GlobMatch( "*[^0-9a-zA-Z_@]*", sv_demodir->string, false ) ) {
		Com_Printf( "Invalid demo prefix string: %s\n", sv_demodir->string );
		Cvar_ForceSet( "sv_demodir", "" );
	}

	// wsw : jal : cap client's exceding server rules
	sv_maxrate =            Cvar_Get( "sv_maxrate", "0", CVAR_DEVELOPER );
	sv_compresspackets =        Cvar_Get( "sv_compresspackets", "1", CVAR_DEVELOPER );
	sv_skilllevel =         Cvar_Get( "sv_skilllevel", "2", CVAR_SERVERINFO | CVAR_ARCHIVE | CVAR_LATCH );

	if( sv_skilllevel->integer > 2 ) {
		Cvar_ForceSet( "sv_skilllevel", "2" );
	}
	if( sv_skilllevel->integer < 0 ) {
		Cvar_ForceSet( "sv_skilllevel", "0" );
	}

	sv_masterservers =          Cvar_Get( "masterservers", DEFAULT_MASTER_SERVERS_IPS, CVAR_LATCH );
	sv_masterservers_steam =    Cvar_Get( "masterservers_steam", DEFAULT_MASTER_SERVERS_STEAM_IPS, CVAR_LATCH );

	sv_debug_serverCmd =        Cvar_Get( "sv_debug_serverCmd", "0", CVAR_ARCHIVE );

	sv_MOTD = Cvar_Get( "sv_MOTD", "0", CVAR_ARCHIVE );
	sv_MOTDFile = Cvar_Get( "sv_MOTDFile", "", CVAR_ARCHIVE );
	sv_MOTDString = Cvar_Get( "sv_MOTDString", "", CVAR_ARCHIVE );
	SV_MOTD_Update();

	// this is a message holder for shared use
	MSG_Init( &tmpMessage, tmpMessageData, sizeof( tmpMessageData ) );

	// init server updates ratio
	if( dedicated->integer ) {
		sv_pps = Cvar_Get( "sv_pps", "20", CVAR_SERVERINFO | CVAR_NOSET );
	} else {
		sv_pps = Cvar_Get( "sv_pps", "20", CVAR_SERVERINFO );
	}
	svc.snapFrameTime = (int)( 1000 / sv_pps->value );
	if( svc.snapFrameTime > 200 ) { // too slow, also, netcode uses a byte
		Cvar_ForceSet( "sv_pps", "5" );
		svc.snapFrameTime = 200;
	} else if( svc.snapFrameTime < 10 ) {   // abusive
		Cvar_ForceSet( "sv_pps", "100" );
		svc.snapFrameTime = 10;
	}

	sv_fps = Cvar_Get( "sv_fps", "62", CVAR_NOSET );
	svc.gameFrameTime = (int)( 1000 / sv_fps->value );
	if( svc.gameFrameTime > svc.snapFrameTime ) { // gamecode can never be slower than snaps
		svc.gameFrameTime = svc.snapFrameTime;
		Cvar_ForceSet( "sv_fps", sv_pps->dvalue );
	}

	svc.autoUpdateMinute = rand() % 60;

	sv_snap_aggressive_sound_culling = Cvar_Get( SNAP_VAR_CULL_SOUND_WITH_PVS , "0", CVAR_SERVERINFO | CVAR_ARCHIVE );
	sv_snap_raycast_players_culling = Cvar_Get( SNAP_VAR_USE_RAYCAST_CULLING, "1", CVAR_SERVERINFO | CVAR_ARCHIVE );
	sv_snap_aggressive_fov_culling = Cvar_Get( SNAP_VAR_USE_VIEWDIR_CULLING, "0", CVAR_SERVERINFO | CVAR_ARCHIVE );
	sv_snap_shadow_events_data = Cvar_Get( SNAP_VAR_SHADOW_EVENTS_DATA, "1", CVAR_SERVERINFO | CVAR_ARCHIVE );

	Com_Printf( "Game running at %i fps. Server transmit at %i pps\n", sv_fps->integer, sv_pps->integer );

	//init the master servers list
	SV_InitMaster();

	SV_MM_Init();

	ML_Init();

	SV_Web_Init();

	sv_initialized = true;
}

/*
* SV_Shutdown
*
* Called once when the program is shutting down
*/
void SV_Shutdown( const char *finalmsg ) {
	if( !sv_initialized ) {
		return;
	}
	sv_initialized = false;

	SV_Web_Shutdown();
	ML_Shutdown();
	SV_MM_Shutdown( true );
	SV_ShutdownGame( finalmsg, false );

	SV_ShutdownOperatorCommands();

	Mem_FreePool( &sv_mempool );
}
