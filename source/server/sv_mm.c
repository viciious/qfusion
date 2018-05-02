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

#include <time.h>       // just for dev

#include "server.h"
#include "../gameshared/q_shared.h"

#include "../matchmaker/mm_common.h"
#include "../matchmaker/mm_rating.h"
#include "../matchmaker/mm_query.h"

// interval between successive attempts to get match UUID from the mm
#define SV_MM_MATCH_UUID_FETCH_INTERVAL     20  // in seconds

/*
* private vars
*/
static bool sv_mm_initialized = false;
static mm_uuid_t sv_mm_session;

// local session counter
static unsigned int sv_mm_localsession;
static int64_t sv_mm_last_heartbeat;
static bool sv_mm_logout_semaphore = false;

// flag for gamestate = game-on
static bool sv_mm_gameon = false;

static stat_query_t *sv_login_query = NULL;

static stat_query_api_t *sq_api = NULL;

static char sv_mm_match_uuid[37];
static unsigned sv_mm_next_match_uuid_fetch;
static stat_query_t *sv_mm_match_uuid_fetch_query;
static void (*sv_mm_match_uuid_callback_fn)( const char *uuid );

/*
* public vars
*/
cvar_t *sv_mm_authkey;
cvar_t *sv_mm_enable;
cvar_t *sv_mm_loginonly;
cvar_t *sv_mm_debug_reportbots;

/*
* prototypes
*/
static void SV_MM_ReportMatch( const char *report );
static bool SV_MM_Login( void );
static void SV_MM_Logout( bool force );
static void SV_MM_GetMatchUUIDThink( void );

/*
* Utilities
*/
static client_t *SV_MM_ClientForSession( mm_uuid_t session_id ) {
	int i;
	client_t *cl;

	for( i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++ ) {
		// also ignore zombies?
		if( cl->state == CS_FREE ) {
			continue;
		}

		if( Uuid_Compare( cl->mm_session, session_id ) ) {
			return cl;
		}
	}

	return NULL;
}

//======================================
//		HTTP REQUESTS
//======================================

struct stat_query_s *SV_MM_CreateQuery( const char *iface, const char *url, bool get ) {
	return sq_api->CreateQuery( sv_ip->string, url, false );
}

void SV_MM_SendQuery( struct stat_query_s *query ) {
	char uuid_buffer[UUID_BUFFER_SIZE];
	// add our session id
	sq_api->SetField( query, MM_FORM_SERVER_SESSION, Uuid_ToString( uuid_buffer, sv_mm_session ) );
	sq_api->Send( query );
}

// TODO: instead of this, factor ClientDisconnect to game module which can flag
// the gamestate in that function
void SV_MM_GameState( bool gameon ) {
	sv_mm_gameon = gameon;
}

static void sv_mm_heartbeat_done( stat_query_t *query, bool success, void *customp ) {
}

void SV_MM_Heartbeat( void ) {
	stat_query_t *query;
	char uuid_buffer[UUID_BUFFER_SIZE];

	if( !sv_mm_initialized || !Uuid_IsValidSessionId( sv_mm_session ) ) {
		return;
	}

	// push a request
	query = sq_api->CreateQuery( sv_ip->string, "server/heartbeat", false );
	if( query == NULL ) {
		return;
	}

	// servers own session (TODO: put this to a cookie or smth)
	sq_api->SetField( query, MM_FORM_SERVER_SESSION, Uuid_ToString( uuid_buffer, sv_mm_session ) );

	// redundant atm
	sq_api->SetCallback( query, sv_mm_heartbeat_done, NULL );
	sq_api->Send( query );
}

typedef struct client_disconnect_context_s {
	mm_uuid_t session_id;
} client_disconnect_context_t;

/*
* sv_mm_clientdisconnect_done
* This only exists so that we are sure the message got through
*/
static void sv_mm_clientdisconnect_done( stat_query_t *query, bool success, void *customp ) {
	client_disconnect_context_t *context;
	char uuid_buffer[UUID_BUFFER_SIZE];

	context = ( client_disconnect_context_t *)customp;

	if( success == true ) {
		Com_Printf( "SV_MM_ClientDisconnect: Acknowledged %s\n", Uuid_ToString( uuid_buffer, context->session_id ) );
	} else {
		Com_Printf( "SV_MM_ClientDisconnect: Error\n" );
	}

	Mem_ZoneFree( context );
}

void SV_MM_ClientDisconnect( client_t *client ) {
	stat_query_t *query;
	client_disconnect_context_t *context;
	char uuid_buffer[UUID_BUFFER_SIZE];

	if( !sv_mm_initialized || !Uuid_IsValidSessionId( sv_mm_session ) ) {
		return;
	}

	// do we need to tell about anonymous clients?
	if( !Uuid_IsValidSessionId( client->mm_session ) ) {
		return;
	}

	// push a request
	query = sq_api->CreateQuery( sv_ip->string, "server/clientDisconnect", false );
	if( query == NULL ) {
		return;
	}

	sq_api->SetField( query, MM_FORM_SERVER_SESSION, Uuid_ToString( uuid_buffer, sv_mm_session ) );
	sq_api->SetField( query, MM_FORM_CLIENT_SESSION, Uuid_ToString( uuid_buffer, client->mm_session ) );

	context = ( client_disconnect_context_t *)Mem_ZoneMalloc( sizeof( client_disconnect_context_t ) );
	context->session_id = client->mm_session;

	//
	sq_api->SetCallback( query, sv_mm_clientdisconnect_done, context );
	sq_api->Send( query );
}

typedef struct client_done_context_s {
	mm_uuid_t session_id;
	client_t *client;
} client_done_context_t;

/*
* sv_clientconnect_done
* callback for clientconnect POST request
*/
static void sv_mm_clientconnect_done( stat_query_t *query, bool success, void *customp ) {
	bool userinfo_changed;
	char uuid_buffer[UUID_BUFFER_SIZE];

	client_done_context_t *context = ( client_done_context_t *)customp;
	// Save local copies of the context fields on stack
	client_t *cl = context->client;
	// Free no longer needed context
	Mem_ZoneFree( customp );

	// Happens if a game module rejects connection
	if( !cl->edict ) {
		Com_Printf( "SV_MM_ClientConnect: The client is no longer valid\n" );
		return;
	}

	if( !success ) {
		Com_Printf( "SV_MM_ClientConnect: Remote or network failure\n" );
		goto failure;
	}

	if( !query ) {
		Com_Printf( "SV_MM_ParseResponse: Failed to parse data\n" );
		goto failure;
	}

	stat_query_section_t *root = sq_api->GetRoot( query );
	if( (int)sq_api->GetNumberOrDefault( root, "banned", 0 ) ) {
		const char *reason = sq_api->GetStringOrDefault( root, "reason", "" );
		if( !reason || !*reason ) {
			reason = "Your account at " APP_URL " has been banned.";
		}

		SV_DropClient( cl, DROP_TYPE_GENERAL, "Error: %s", reason );
		return;
	}

	const int status = (int)sq_api->GetNumberOrDefault( root, "status", 0 );
	if( !status ) {
		const char *error = sq_api->GetStringOrDefault( root, "error", "" );
		if( error && *error ) {
			Com_Printf( "SV_MM_ClientConnect: Request error at remote host: %s\n", error );
		} else {
			Com_Printf( "SV_MM_ClientConnect: Bad or missing response status\n" );
		}
		goto failure;
	}

	// Note: we have omitted client session id comparisons,
	// it has to be performed on server anyway and a mismatch yields a failed response.

	// Check
	stat_query_section_t *info_section = sq_api->GetSection( root, "player_info" );
	if( !info_section ) {
		Com_Printf( "SV_MM_ParseResponse: Missing `player_info` section\n" );
		goto failure;
	}

	const char *login = sq_api->GetStringOrDefault( info_section, "login", "" );
	if( !login || !*login ) {
		Com_Printf( "SV_MM_ParseResponse: Missing `login` field\n" );
		goto failure;
	}

	Q_strncpyz( cl->mm_login, login, sizeof( cl->mm_login ) );
	if( !Info_SetValueForKey( cl->userinfo, "cl_mm_login", login ) ) {
		Com_Printf( "Failed to set infokey 'cl_mm_login' for player %s\n", login );
	}

	const char *mmflags = sq_api->GetStringOrDefault( root, "mmflags", "" );
	if( mmflags && *mmflags ) {
		if( !Info_SetValueForKey( cl->userinfo, "mmflags", mmflags ) ) {
			Com_Printf( "Failed to set infokey 'mmflags' for player %s\n", login );
		}
	}

	userinfo_changed = true;

	stat_query_section_t *ratings_section = sq_api->GetSection( root, "ratings" );
	if( ge && ratings_section ) {
		int idx = 0;
		stat_query_section_t *element = sq_api->GetArraySection( ratings_section, idx++ );
		edict_t *ent = EDICT_NUM( ( cl - svs.clients ) + 1 );
		while( element ) {
			ge->AddRating( ent, sq_api->GetString( element, "gametype" ),
						   (float)sq_api->GetNumber( element, "rating" ),
						   (float)sq_api->GetNumber( element, "deviation" ) );
			element = sq_api->GetArraySection( ratings_section, idx++ );
		}
	}

	goto success;
failure:

	// unable to validate client, either kick him out or force local session
	if( sv_mm_loginonly->integer ) {
		SV_DropClient( cl, DROP_TYPE_GENERAL, "%s", "Error: This server requires login. Create account at " APP_URL );
		return;
	}

	// TODO: Does it have to be unique?
	mm_uuid_t session_id = Uuid_FFFsUuid();
	Uuid_ToString( uuid_buffer, session_id );
	Com_Printf( "SV_MM_ClientConnect: Forcing local_session %s on client %s\n", uuid_buffer, cl->name );
	cl->mm_session = session_id;
	userinfo_changed = true;

success:
	if( userinfo_changed ) {
		SV_UserinfoChanged( cl );
	}

	Com_Printf( "SV_MM_ClientConnect: %s with session id %s\n", cl->name, Uuid_ToString( uuid_buffer, cl->mm_session ) );
}

mm_uuid_t SV_MM_ClientConnect( client_t *client, const netadr_t *address, char *userinfo, mm_uuid_t ticket, mm_uuid_t session_id ) {
	stat_query_t *query;
	client_done_context_t *context;
	char uuid_buffer[UUID_BUFFER_SIZE];

	// return of -1 is not an error, it just marks a dummy local session
	if( !sv_mm_initialized || !Uuid_IsValidSessionId( sv_mm_session ) ) {
		return Uuid_FFFsUuid();
	}

	if( !Uuid_IsValidSessionId( session_id ) || !Uuid_IsValidSessionId( ticket ) ) {
		if( sv_mm_loginonly->integer ) {
			Com_Printf( "SV_MM_ClientConnect: Login-only\n");
			return Uuid_ZeroUuid();
		}
		Com_Printf( "SV_MM_ClientConnect: Invalid session id or ticket, marking as anonymous\n" );
		return Uuid_FFFsUuid();
	}

	// push a request
	query = sq_api->CreateQuery( sv_ip->string, "/server/clientConnect", false );
	if( query == NULL ) {
		return Uuid_ZeroUuid();
	}

	sq_api->SetField( query, MM_FORM_SERVER_SESSION, Uuid_ToString( uuid_buffer, sv_mm_session ) );
	sq_api->SetField( query, MM_FORM_TICKET, Uuid_ToString( uuid_buffer, ticket ) );
	sq_api->SetField( query, MM_FORM_CLIENT_SESSION, Uuid_ToString( uuid_buffer, session_id ) );
	sq_api->SetField( query, MM_FORM_CLIENT_ADDRESS, NET_AddressToString( address ) );

	context = ( client_done_context_t *)Mem_ZoneMalloc( sizeof( client_done_context_t ) );
	context->session_id = session_id;
	context->client = client;

	sq_api->SetCallback( query, sv_mm_clientconnect_done, context );
	sq_api->Send( query );

	return session_id;
}

void SV_MM_Frame( void ) {
	int64_t time;

	if( sv_mm_enable->modified ) {
		if( sv_mm_enable->integer && !sv_mm_initialized ) {
			SV_MM_Login();
		} else if( !sv_mm_enable->integer && sv_mm_initialized ) {
			SV_MM_Logout( false );
		}

		sv_mm_enable->modified = false;
	}

	if( sv_mm_initialized ) {
		if( sv_mm_logout_semaphore ) {
			// logout process is finished so we can shutdown game
			SV_MM_Shutdown( false );
			sv_mm_logout_semaphore = false;
			return;
		}

		// heartbeat
		time = Sys_Milliseconds();
		if( ( sv_mm_last_heartbeat + MM_HEARTBEAT_INTERVAL ) < time ) {
			SV_MM_Heartbeat();
			sv_mm_last_heartbeat = time;
		}

		SV_MM_GetMatchUUIDThink();
	}
}

bool SV_MM_Initialized( void ) {
	return sv_mm_initialized;
}


static void sv_mm_logout_done( stat_query_t *query, bool success, void *customp ) {
	Com_Printf( "SV_MM_Logout: Loggin off..\n" );

	// ignore response-status and just mark us as logged-out
	sv_mm_logout_semaphore = true;
}

/*
* SV_MM_Logout
*/
static void SV_MM_Logout( bool force ) {
	stat_query_t *query;
	int64_t timeout;
	char uuid_buffer[UUID_BUFFER_SIZE];

	if( !sv_mm_initialized || !Uuid_IsValidSessionId( sv_mm_session ) ) {
		return;
	}

	query = sq_api->CreateQuery( sv_ip->string, "server/logout", false );
	if( query == NULL ) {
		return;
	}

	sv_mm_logout_semaphore = false;

	// TODO: pull the authkey out of cvar into file
	sq_api->SetField( query, MM_FORM_SERVER_SESSION, Uuid_ToString( uuid_buffer, sv_mm_session ) );
	sq_api->SetCallback( query, sv_mm_logout_done, NULL );
	sq_api->Send( query );

	if( force ) {
		timeout = Sys_Milliseconds();
		while( !sv_mm_logout_semaphore && Sys_Milliseconds() < ( timeout + MM_LOGOUT_TIMEOUT ) ) {
			sq_api->Poll();

			Sys_Sleep( 10 );
		}

		if( !sv_mm_logout_semaphore ) {
			Com_Printf( "SV_MM_Logout: Failed to force logout\n" );
		} else {
			Com_Printf( "SV_MM_Logout: force logout successful\n" );
		}

		sv_mm_logout_semaphore = false;

		// dont call this, we are coming from shutdown
		// SV_MM_Shutdown( false );
	}
}

/*
* sv_mm_login_done
* callback for login post request
*/
static void sv_mm_login_done( stat_query_t *query, bool success, void *customp ) {
	char uuid_buffer[UUID_BUFFER_SIZE];

	sv_mm_initialized = false;
	sv_login_query = NULL;

	if( !success ) {
		Com_Printf( "SV_MM_Login_Done: Error\n" );
		Cvar_ForceSet( sv_mm_enable->name, "0" );
		return;
	}

	Com_DPrintf( "SV_MM_Login: %s\n", sq_api->GetRawResponse( query ) );

	/*
	 * ch : JSON API
	 * {
	 *		status: [int], 0 on failure
	 *		session_id: [uuid], present on success
	 * }
	 */
	stat_query_section_t *root = sq_api->GetRoot( query );
	if( !root ) {
		Com_Printf( "SV_MM_Login: Failed to parse data\n" );
		goto failure;
	}

	const int status = (int)sq_api->GetNumberOrDefault( root, "status", 0 );
	if( !status ) {
		const char *error = sq_api->GetStringOrDefault( root, "error", "" );
		if( error && *error ) {
			Com_Printf( "SV_MM_Login: Request error at remote host: %s\n", error );
		}
		goto failure;
	}

	const char *sessionString = sq_api->GetStringOrDefault( root, "session_id", "" );
	if( !Uuid_FromString( sessionString, &sv_mm_session ) ) {
		Com_Printf( "SV_MM_Login: Failed to parse session string %s\n", sessionString );
		goto failure;
	}

	sv_mm_initialized = Uuid_IsValidSessionId( sv_mm_session );
	if( !sv_mm_initialized ) {
		goto failure;
	}

	Com_Printf( "SV_MM_Login: Success, session id %s\n", Uuid_ToString( uuid_buffer, sv_mm_session ) );

	return;
failure:
	Com_Printf( "SV_MM_Login: Failed, no session id\n" );
	Cvar_ForceSet( sv_mm_enable->name, "0" );
}

/*
* SV_MM_Login
*/
static bool SV_MM_Login( void ) {
	stat_query_t *query;

	if( sv_login_query != NULL || sv_mm_initialized ) {
		return false;
	}
	if( sv_mm_authkey->string[0] == '\0' ) {
		Cvar_ForceSet( sv_mm_enable->name, "0" );
		return false;
	}

	Com_Printf( "SV_MM_Login: Creating query\n" );

	query = sq_api->CreateQuery( sv_ip->string, "server/login", false );
	if( query == NULL ) {
		return false;
	}

	sq_api->SetField( query, MM_FORM_AUTH_KEY, sv_mm_authkey->string );
	sq_api->SetField( query, MM_FORM_PORT, va( "%d", sv_port->integer ) );
	sq_api->SetField( query, MM_FORM_SERVER_NAME, sv.configstrings[CS_HOSTNAME] );
	sq_api->SetField( query, MM_FORM_SERVER_ADDRESS, sv_ip->string );
	sq_api->SetField( query, MM_FORM_DEMOS_BASEURL, sv_uploads_demos_baseurl->string );
	sq_api->SetCallback( query, sv_mm_login_done, NULL );
	sq_api->Send( query );

	sv_login_query = query;

	return true;
}

/*
* sv_mm_match_uuid_done
* callback for match uuid fetching
*/
static void sv_mm_match_uuid_done( stat_query_t *query, bool success, void *customp ) {
	stat_query_section_t *root;

	// set the repeat timer, which will be ignored in case we successfully parse the response
	sv_mm_next_match_uuid_fetch = Sys_Milliseconds() + SV_MM_MATCH_UUID_FETCH_INTERVAL * 1000;
	sv_mm_match_uuid_fetch_query = NULL;

	if( !success ) {
		return;
	}

	Com_DPrintf( "SV_MM_GetMatchUUID: %s\n", sq_api->GetRawResponse( query ) );

	/*
	 * JSON API
	 * {
	 *		uuid: [string]
	 * }
	 */
	root = sq_api->GetRoot( query );
	if( root == NULL ) {
		Com_Printf( "SV_MM_GetMatchUUID: Failed to parse data\n" );
	} else {
		Q_strncpyz( sv_mm_match_uuid, sq_api->GetString( root, "uuid" ), sizeof( sv_mm_match_uuid ) );
		if( sv_mm_match_uuid_callback_fn ) {
			// fire the callback function
			sv_mm_match_uuid_callback_fn( sv_mm_match_uuid );
		}
	}
}

/*
* SV_MM_GetMatchUUIDThink
*
* Repeatedly query the matchmaker for match UUID until we get one.
*/
static void SV_MM_GetMatchUUIDThink( void ) {
	stat_query_t *query;
	char uuid_buffer[UUID_BUFFER_SIZE];

	if( !sv_mm_initialized || !Uuid_IsValidSessionId( sv_mm_session ) ) {
		return;
	}
	if( sv_mm_next_match_uuid_fetch > Sys_Milliseconds() ) {
		// not ready yet
		return;
	}
	if( sv_mm_match_uuid_fetch_query != NULL ) {
		// already in progress
		return;
	}
	if( sv_mm_match_uuid[0] != '\0' ) {
		// we have already queried the server
		return;
	}

	// ok, get it now!
	Com_DPrintf( "SV_MM_GetMatchUUIDThink: Creating query\n" );

	query = sq_api->CreateQuery( sv_ip->string, "server/matchUuid", false );
	if( query == NULL ) {
		return;
	}

	sq_api->SetField( query, MM_FORM_SERVER_SESSION, Uuid_ToString( uuid_buffer, sv_mm_session ) );
	sq_api->SetCallback( query, sv_mm_match_uuid_done, NULL );
	sq_api->Send( query );

	sv_mm_match_uuid_fetch_query = query;
}

/*
* SV_MM_GetMatchUUID
*
* Start querying the server for match UUID. Fire the callback function
* upon success.
*/
void SV_MM_GetMatchUUID( void ( *callback_fn )( const char *uuid ) ) {
	if( !sv_mm_initialized ) {
		return;
	}
	if( sv_mm_match_uuid_fetch_query != NULL ) {
		// already in progress
		return;
	}
	if( sv_mm_next_match_uuid_fetch > Sys_Milliseconds() ) {
		// not ready yet
		return;
	}

	sv_mm_match_uuid[0] = '\0';
	sv_mm_match_uuid_callback_fn = callback_fn;

	// think now!
	sv_mm_next_match_uuid_fetch = Sys_Milliseconds();
	SV_MM_GetMatchUUIDThink();
}

/*
* SV_MM_Init
*/
void SV_MM_Init( void ) {
	sv_mm_initialized = false;
	sv_mm_session = Uuid_ZeroUuid();
	sv_mm_localsession = 0;
	sv_mm_last_heartbeat = 0;
	sv_mm_logout_semaphore = false;

	sv_mm_gameon = false;

	sv_mm_match_uuid[0] = '\0';
	sv_mm_next_match_uuid_fetch = Sys_Milliseconds();
	sv_mm_match_uuid_fetch_query = NULL;
	sv_mm_match_uuid_callback_fn = NULL;

	StatQuery_Init();
	sq_api = StatQuery_GetAPI();

	/*
	* create cvars
	* ch : had to make sv_mm_enable to cmdline only, because of possible errors
	* if enabled while players on server
	*/
	sv_mm_enable = Cvar_Get( "sv_mm_enable", "0", CVAR_ARCHIVE | CVAR_NOSET | CVAR_SERVERINFO );
	sv_mm_loginonly = Cvar_Get( "sv_mm_loginonly", "0", CVAR_ARCHIVE | CVAR_SERVERINFO );
	sv_mm_debug_reportbots = Cvar_Get( "sv_mm_debug_reportbots", "0", CVAR_CHEAT );

	// this is used by game, but to pass it to client, we'll initialize it in sv
	Cvar_Get( "sv_skillRating", va( "%.0f", MM_RATING_DEFAULT ), CVAR_READONLY | CVAR_SERVERINFO );

	// TODO: remove as cvar
	sv_mm_authkey = Cvar_Get( "sv_mm_authkey", "", CVAR_ARCHIVE );

	/*
	* login
	*/
	sv_login_query = NULL;
	//if( sv_mm_enable->integer )
	//	SV_MM_Login();
	sv_mm_enable->modified = true;
}

void SV_MM_Shutdown( bool logout ) {
	if( !sv_mm_initialized ) {
		return;
	}

	Com_Printf( "SV_MM_Shutdown..\n" );

	if( logout ) {
		// logout is always force in here
		SV_MM_Logout( true );
	}

	Cvar_ForceSet( "sv_mm_enable", "0" );

	sv_mm_gameon = false;

	sv_mm_last_heartbeat = 0;
	sv_mm_logout_semaphore = false;

	sv_mm_initialized = false;
	sv_mm_session = Uuid_ZeroUuid();

	StatQuery_Shutdown();
	sq_api = NULL;
}
