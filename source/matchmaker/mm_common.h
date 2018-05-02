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

// mm_common.h -- matchmaker definitions for client and server exe's (not modules)

#ifndef __MM_COMMON_H
#define __MM_COMMON_H

#include "../qcommon/wswcurl.h"
#include "../matchmaker/mm_query.h"

// these are in milliseconds
#define MM_HEARTBEAT_INTERVAL   15 * 1000
#define MM_LOGOUT_TIMEOUT       3 * 1000

// for client only
#define MM_LOGIN2_INTERVAL      2 * 1000  // milliseconds
#define MM_LOGIN2_RETRIES       7

extern cvar_t *mm_url;

void MM_PasswordWrite( const char *user, const char *password );
// returns password as static string
const char *MM_PasswordRead( const char *user );

char ** MM_ParseResponse( wswcurl_req *req, int *argc );
void MM_FreeResponse( char **argv );

void MM_Init( void );
void MM_Shutdown( void );
void MM_Frame( const int realmsec );

void StatQuery_Init( void );
void StatQuery_Shutdown( void );
stat_query_api_t *StatQuery_GetAPI( void );

#endif
