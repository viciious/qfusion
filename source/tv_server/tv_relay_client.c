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

#include "tv_local.h"

#include "tv_relay_client.h"

#include "tv_relay.h"
#include "tv_downstream.h"

/*
* TV_Relay_BuildClientFrameSnap
*/
void TV_Relay_BuildClientFrameSnap( relay_t *relay, client_t *client ) {
	edict_t *clent;
	entity_state_t backup_state = { 0 };
	entity_shared_t backup_shared = { 0 };
	vec_t *skyorg = NULL, origin[3];

	if( relay->configstrings[CS_SKYBOX][0] != '\0' ) {
		int noents = 0;
		float f1 = 0, f2 = 0;

		if( sscanf( relay->configstrings[CS_SKYBOX], "%f %f %f %f %f %i", &origin[0], &origin[1], &origin[2], &f1, &f2, &noents ) >= 3 ) {
			if( !noents ) {
				skyorg = origin;
			}
		}
	}

	// pretend client occupies our slot on real server
	clent = client->edict;
	if( relay->playernum >= 0 ) {
		client->edict = EDICT_NUM( relay, relay->playernum + 1 );

		backup_state = client->edict->s;
		client->edict->s = clent->s;
		client->edict->s.number = relay->playernum + 1;

		backup_shared = client->edict->r;
		client->edict->r = clent->r;
		if( client->mv ) {
			client->edict->r.client->ps.POVnum = relay->playernum + 1;
		}
	} else {
		if( client->mv ) {
			client->edict = NULL;
		} else {
			assert( client->edict != NULL );
			if( !client->edict ) {
				return;
			}
		}
	}

	relay->fatvis.skyorg = skyorg;      // HACK HACK HACK
	SNAP_BuildClientFrameSnap( relay->cms, &relay->gi, relay->framenum, relay->realtime, &relay->fatvis,
							   client, relay->module_export->GetGameState( relay->module ),
							   &relay->client_entities,
							   true, tv_mempool, 0 );

	if( relay->playernum >= 0 ) {
		client->edict->s = backup_state;
		client->edict->r = backup_shared;
	}
	client->edict = clent;
}

/*
* TV_Relay_SendClientDatagram
*/
static bool TV_Relay_SendClientDatagram( relay_t *relay, client_t *client ) {
	uint8_t msg_buf[MAX_MSGLEN];
	msg_t msg;
	snapshot_t *frame;

	assert( relay );
	assert( client );
	assert( relay == client->relay );

	TV_Downstream_InitClientMessage( client, &msg, msg_buf, sizeof( msg_buf ) );

	TV_Downstream_AddReliableCommandsToMessage( client, &msg );

	// send over all the relevant entity_state_t
	// and the player_state_t
	TV_Relay_BuildClientFrameSnap( relay, client );

	frame = relay->curFrame;
	SNAP_WriteFrameSnapToClient( &relay->gi, client, &msg, relay->framenum, relay->serverTime, relay->baselines,
								 &relay->client_entities, frame->numgamecommands, frame->gamecommands, frame->gamecommandsData );

	return TV_Downstream_SendMessageToClient( client, &msg );
}

/*
* TV_Relay_ReconnectClients
*/
void TV_Relay_ReconnectClients( relay_t *relay ) {
	int i;
	client_t *client;

	if( relay->state < CA_CONNECTED ) {
		return;
	}

	relay->state = CA_HANDSHAKE;

	// send a message to each connected client
	for( i = 0, client = tvs.clients; i < tv_maxclients->integer; i++, client++ ) {
		if( client->relay != relay ) {
			continue;
		}

		// needs to reconnect
		if( client->state > CS_CONNECTING ) {
			client->state = CS_CONNECTING;
		}

		client->lastframe = -1;
		memset( client->gameCommands, 0, sizeof( client->gameCommands ) );

		TV_Downstream_SendServerCommand( client, "changing" );
	}

	TV_Relay_SendClientMessages( relay );

	// send a message to each connected client
	for( i = 0, client = tvs.clients; i < tv_maxclients->integer; i++, client++ ) {
		if( client->relay != relay ) {
			continue;
		}
		if( client->state < CS_CONNECTING ) {
			continue;
		}

		TV_Downstream_SendServerCommand( client, "reconnect" );
	}
}

/*
* TV_Relay_SendClientMessages
*/
void TV_Relay_SendClientMessages( relay_t *relay ) {
	int i;
	client_t *client;

	assert( relay );

	// send a message to each connected client
	for( i = 0, client = tvs.clients; i < tv_maxclients->integer; i++, client++ ) {
		if( client->state != CS_SPAWNED ) {
			continue;
		}
		if( client->relay != relay ) {
			continue;
		}

		if( !TV_Relay_SendClientDatagram( relay, client ) ) {
			Com_Printf( "%s" S_COLOR_WHITE ": Error sending message: %s\n", client->name, NET_ErrorString() );
			if( client->reliable ) {
				TV_Downstream_DropClient( client, DROP_TYPE_GENERAL, "Error sending message: %s\n",
										  NET_ErrorString() );
			}
		}
	}
}

/*
* TV_Relay_ClientUserinfoChanged
*/
void TV_Relay_ClientUserinfoChanged( relay_t *relay, client_t *client ) {
	assert( relay );
	assert( client );
	assert( client->relay == relay );

	relay->module_export->ClientUserinfoChanged( relay->module, client->edict, client->userinfo );
}

/*
* TV_Relay_ClientCommand_f
*/
bool TV_Relay_ClientCommand_f( relay_t *relay, client_t *client ) {
	assert( relay );
	assert( client );
	assert( relay == client->relay );

	return relay->module_export->ClientCommand( relay->module, client->edict );
}

/*
* TV_Relay_ClientBegin
*/
void TV_Relay_ClientBegin( relay_t *relay, client_t *client ) {
	assert( relay );
	assert( client );
	assert( relay == client->relay );

	relay->module_export->ClientBegin( relay->module, client->edict );
}

/*
* TV_Relay_ClientDisconnect
*/
void TV_Relay_ClientDisconnect( relay_t *relay, client_t *client ) {
	assert( relay );
	assert( relay->module_export );
	assert( client );

	relay->module_export->ClientDisconnect( relay->module, client->edict );
	relay->num_active_specs--;
	if( relay->num_active_specs < 0 ) {
		relay->num_active_specs = 0;
	}

	// update the upstream name to "name (no_of_players)"
	TV_Relay_UpstreamUserinfoChanged( relay );
}

/*
* TV_Relay_CanConnect
*/
bool TV_Relay_CanConnect( relay_t *relay, client_t *client, char *userinfo ) {
	assert( relay );
	assert( client );

	if( relay->state == CA_CONNECTING || !relay->module_export ) {
		return false;
	}

	// get the game a chance to reject this upstream
	return relay->module_export->CanConnect( relay->module, userinfo );
}

/*
* TV_Relay_ClientConnect
*/
void TV_Relay_ClientConnect( relay_t *relay, client_t *client ) {
	int edictnum;

	assert( relay );
	assert( relay->module_export );
	assert( client );

	edictnum = client - tvs.clients;
	client->edict = LOCAL_EDICT_NUM( relay, edictnum );
	client->relay = relay;

	relay->module_export->ClientConnect( relay->module, client->edict, client->userinfo );
	relay->num_active_specs++;

	// update the upstream name to "name (no_of_players)"
	TV_Relay_UpstreamUserinfoChanged( relay );

	// parse some info from the info strings
	TV_Downstream_UserinfoChanged( client );
}
