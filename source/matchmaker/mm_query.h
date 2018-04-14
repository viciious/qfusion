/*
Copyright (C) 2011 Christian Holmberg

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

#ifndef __MM_QUERY_H__
#define __MM_QUERY_H__

// Public API for StatQuery for both .exe and modules

typedef struct stat_query_s stat_query_t;
typedef /* struct stat_query_section_s */ void* stat_query_section_t;

/**
 * Names of requests form parameters.
 * Usage of symbolic predefined names makes it easier
 * to synchronize requests schema with the server-expected one.
 * (most of these parameter names are used multiple times).
 */
#define MM_FORM_SERVER_SESSION "server_session"
#define MM_FORM_CLIENT_SESSION "client_session"
#define MM_FORM_TICKET "ticket"
#define MM_FORM_HANDLE "handle"
#define MM_FORM_LOGIN "login"
#define MM_FORM_PASSWORD "password"
#define MM_FORM_PORT "port"
#define MM_FORM_AUTH_KEY "auth_key"
#define MM_FORM_SERVER_NAME "server_name"
#define MM_FORM_DEMOS_BASEURL "demos_baseurl"
#define MM_FORM_SERVER_ADDRESS "server_address"
#define MM_FORM_CLIENT_ADDRESS "client_address"
#define MM_FORM_JSON_ATTACHMENT "json_attachment"

typedef struct stat_query_api_s {
	stat_query_t *( *CreateQuery )( const char *iface, const char *str, bool get );
	// this is automatically called after calling users callback function so you rarely need to call this yourself
	void ( *DestroyQuery )( stat_query_t *query );
	void ( *SetCallback )( stat_query_t *query, void ( *callback_fn )( stat_query_t *, bool, void * ), void *customp );
	// you may or may not be allowed to call this directly
	void ( *Send )( stat_query_t *query );

	// argument for POST/GET request
	// note that nothing is encoded in StatQuery atm, so pre-encode or watch what you put in here!
	void ( *SetField )( stat_query_t *query, const char *name, const char *value );

	// Get data properties
	stat_query_section_t *( *GetRoot )( stat_query_t * query );
	stat_query_section_t *( *GetOutRoot )( stat_query_t *query );
	// named sections/arrays and properties
	stat_query_section_t *( *GetSection )( stat_query_section_t * parent, const char *name );
	double ( *GetNumber )( stat_query_section_t *parent, const char *name );
	double ( *GetNumberOrDefault )( stat_query_section_t *parent, const char *name, double defaultValue );
	const char *( *GetString )( stat_query_section_t * parent, const char *name );
	const char *( *GetStringOrDefault )( stat_query_section_t *parent, const char *name, const char *defaultValue );
	// indexed sections and properties from array
	int ( *GetArraySize )( stat_query_section_t *array );
	stat_query_section_t *( *GetArraySection )( stat_query_section_t * parent, int idx );
	double ( *GetArrayNumber )( stat_query_section_t *array, int idx );
	const char *( *GetArrayString )( stat_query_section_t * array, int idx );

	// Set data properties
	// named section with key/value pairs inside (leave sectionname NULL, if inside array)
	stat_query_section_t *( *CreateSection )( stat_query_t * query, stat_query_section_t * parent, const char *sectionname );
	// named array with unnamed items inside
	stat_query_section_t *( *CreateArray )( stat_query_t * query, stat_query_section_t * parent, const char *arrayname );
	// named properties
	void ( *SetString )( stat_query_section_t *section, const char *name, const char *value );
	void ( *SetNumber )( stat_query_section_t *section, const char *prop_name, double prop_value );
	// set property inside sections in array (TODO: remove in favour of GetSection/CreateSection + AddArrayString/Number
	void ( *SetArrayString )( stat_query_section_t *array, int idx, const char *prop_name, const char *prop_value );
	void ( *SetArrayNumber )( stat_query_section_t *array, int idx, const char *prop_name, double prop_value );
	// add unnamed properties to arrays
	void ( *AddArrayString )( stat_query_section_t *array, const char *prop_value );
	void ( *AddArrayNumber )( stat_query_section_t *array, double prop_value );

	bool ( *IsArray )( const stat_query_section_t *section );
	bool ( *IsObject )( const stat_query_section_t *section );

	const char *( *GetRawResponse )( stat_query_t * query );
	// char *const *( *GetTokenizedResponse )( stat_query_t *query, int *argc );
	char **( *GetTokenizedResponse )( stat_query_t * query, int *argc );

	// translates to wswcurl_perform()
	void ( *Poll )( void );
} stat_query_api_t;

#endif
