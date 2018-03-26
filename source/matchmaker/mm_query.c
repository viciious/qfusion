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

#include "../qcommon/qcommon.h"
#include "../matchmaker/mm_common.h"
#include "../matchmaker/mm_query.h"
#include "../qcommon/wswcurl.h"
#include "../qcommon/cjson.h"
#include "../qalgo/base64.h"
#include "../qcommon/compression.h"

#define SQALLOC( x )    Mem_Alloc( sq_mempool, ( x ) )
#define SQFREE( x )     Mem_Free( ( x ) )
#define SQREALLOC( x, y )   Mem_Realloc( ( x ), ( y ) )

struct stat_query_s {
	wswcurl_req *req;
	cJSON       *json_out;      // root of all
	cJSON       *json_in;

	bool has_json;

	// if 'req' is NULL we have a GET cause that has to be created
	// just before launch when we have all parameters
	// url is stored only for GET, cause we can pass it in POST directly to wswcurl
	char *url;

	char *iface;

	void (*callback_fn)( stat_query_t *, bool, void * );
	void *customp;

	// cached responses
	char *response_raw;
	char **response_tokens;
	int response_numtokens;

};

//===============================================

stat_query_api_t sq_export;
mempool_t *sq_mempool = NULL;
int sq_refcount = 0;    // Refcount Init/Shutdown if server and client exists on same process

static void StatQuery_DestroyQuery( stat_query_t *query );

//===============================================

static void StatQuery_CacheResponseRaw( stat_query_t *query ) {
	size_t respSize;

	wswcurl_getsize( query->req, &respSize );
	if( !respSize ) {
		return;
	}

	// read the response string
	query->response_raw = SQALLOC( respSize + 1 );
	respSize = wswcurl_read( query->req, query->response_raw, respSize );
	query->response_raw[respSize] = '\0';
}

static void StatQuery_CacheTokenized( stat_query_t *query ) {
	size_t respSize;
	char *buffer, *realBuffer, *p, *end, **tokens;
	int numTokens, startOfs, endToken;

	if( !query->response_raw ) {
		StatQuery_CacheResponseRaw( query );
	}
	if( !query->response_raw ) {
		return;
	}

	respSize = strlen( query->response_raw );
	buffer = query->response_raw;

	// count the number of tokens (could this be done in 1 pass?)
	p = buffer;
	end = buffer + respSize;
	numTokens = 0;
	startOfs = 0;
	while( p < end ) {
		// skip whitespace
		while( p < end && *p <= ' ' )
			p++;

		if( p >= end ) {
			break;
		}

		if( !numTokens ) {
			startOfs = p - buffer;
		}

		numTokens++;

		// skip the token
		while( p < end && *p > ' ' )
			p++;
	}

	// fail
	if( !numTokens ) {
		SQFREE( buffer );
		return;
	}

	tokens = SQALLOC( ( numTokens + 1 ) * sizeof( char* ) );

	// allocate the actual buffer that we are going to return
	if( startOfs > 0 ) {
		realBuffer = SQALLOC( ( respSize - startOfs ) + 1 );
		memcpy( realBuffer, buffer + startOfs, respSize - startOfs );
		SQFREE( buffer );
		buffer = realBuffer;
		respSize -= startOfs;
		buffer[respSize] = '\0';
	}

	// 2nd pass, mark all tokens
	p = buffer;
	end = buffer + respSize;
	endToken = numTokens;
	numTokens = 0;
	while( p < end && numTokens < endToken ) {
		// we made the buffer point into the first character
		// so we can shuffle this loop a little
		tokens[numTokens++] = p;

		// skip the token
		while( p < end && *p > ' ' )
			p++;

		if( p >= end ) {
			break;
		}

		// skip whitespace
		while( p < end && *p <= ' ' )
			*p++ = '\0';
	}
	*p = '\0';  // we left room for 1 character
	tokens[numTokens] = 0;

	query->response_tokens = tokens;
	query->response_numtokens = numTokens;
}

static void StatQuery_CallbackGeneric( wswcurl_req *req, int status, void *customp ) {
	const char *content_type;
	stat_query_t *query = (stat_query_t *)customp;
	bool success = status > 0 ? true : false;

	// print some stuff out
	if( status < 0 ) {
		Com_Printf( "StatQuery HTTP error: \"%s\", url \"%s\"\n", wswcurl_errorstr( status ), wswcurl_get_effective_url( req ) );
	} else {
		// check the MIME type and see if we have JSON, if so parse it up
		content_type = wswcurl_get_content_type( req );
		if( content_type && !strcmp( content_type, "application/json" ) ) {
			StatQuery_CacheResponseRaw( query );
			if( query->response_raw ) {
				query->json_in = cJSON_Parse( query->response_raw );
			}
		}
	}

	if( query->callback_fn ) {
		query->callback_fn( query, success, query->customp );
	}

	StatQuery_DestroyQuery( query );
}

static const char *StatQuery_JsonTypeToString( int t ) {
	switch( t ) {
		case cJSON_False:   return "cJSON_False";
		case cJSON_True:    return "cJSON_True";
		case cJSON_NULL:    return "cJSON_NULL";
		case cJSON_Number:  return "cJSON_Number";
		case cJSON_String:  return "cJSON_String";
		case cJSON_Array:   return "cJSON_Array";
		case cJSON_Object:  return "cJSON_Object";
		default:            return "Unknown";
	}

	return "";
}

static double StatQuery_JsonToNumber( cJSON *obj ) {
	if( !obj ) {
		Com_Printf( S_COLOR_YELLOW "StatQuery_JsonToNumber: obj == null\n" );
		return 0.0;
	}

	switch( obj->type ) {
		case cJSON_Number:
			return obj->valuedouble;
		case cJSON_String:
			return atof( obj->valuestring );
		case cJSON_True:
			return (double)true;
		case cJSON_False:
			return (double)false;
		default:
			Com_Printf( "StatQuery: Couldnt cast JSON type %s to number (object name %s)\n",
						StatQuery_JsonTypeToString( obj->type ), obj->string != 0 ? obj->string : "" );
			return NAN;
	}

	return NAN;
}

static const char *StatQuery_JsonToString( cJSON *obj ) {
	static char buffer[128];

	if( !obj ) {
		Com_Printf( S_COLOR_YELLOW "StatQuery_JsonToString: obj == null\n" );
		buffer[0] = '\0';
		return buffer;
	}

	switch( obj->type ) {
		case cJSON_String:
			return obj->valuestring;
		case cJSON_Number:
			Q_snprintfz( buffer, sizeof( buffer ), "%f", obj->valuedouble );
			break;
		case cJSON_True:
			return "1";
		case cJSON_False:
			return "0";
		default:
			Com_Printf( "StatQuery: Couldnt cast JSON type %s to string (object name %s)\n",
						StatQuery_JsonTypeToString( obj->type ), obj->string != 0 ? obj->string : "" );
			return NULL;
	}

	return NULL;
}

//===============================================

// TODO; prefer GET/POST
static stat_query_t *StatQuery_CreateQuery( const char *iface, const char *str, bool get ) {
	stat_query_t *query;

	assert( str != NULL );
	if( str == NULL ) {
		return NULL;
	}

	query = SQALLOC( sizeof( *query ) );
	memset( query, 0, sizeof( *query ) );

	if( iface && *iface ) {
		query->iface = SQALLOC( strlen( iface ) + 1 );
		strcpy( query->iface, iface );
	}

	query->json_out = cJSON_CreateObject();

	if( str[0] == '/' ) {
		str += 1;
	}

	if( !get ) {
		query->req = wswcurl_create( iface, "%s/%s", mm_url->string, str );
	} else {
		// add in '/', '?' and '\0' = 3
		query->url = SQALLOC( strlen( mm_url->string ) + strlen( str ) + 3 );
		// ch : lazy code :(
		strcpy( query->url, mm_url->string );
		strcat( query->url, "/" );
		strcat( query->url, str );
		strcat( query->url, "?" );
	}

	return query;
}

static void StatQuery_DestroyQuery( stat_query_t *query ) {
	// close wswcurl and json_in json_out
	if( query->req ) {
		wswcurl_delete( query->req );
	}

	// cached responses
	if( query->response_tokens ) {
		if( query->response_tokens[0] && query->response_tokens[0] != query->response_raw ) {
			SQFREE( query->response_tokens[0] );
		}
		SQFREE( query->response_tokens );
	}

	if( query->response_raw ) {
		SQFREE( query->response_raw );
	}

	SQFREE( query->iface );
	SQFREE( query->url );

	// actual query
	SQFREE( query );
}

static void StatQuery_SetCallback( stat_query_t *query, void ( *callback_fn )( stat_query_t *, bool, void * ), void *customp ) {
	query->callback_fn = callback_fn;
	query->customp = customp;
}

static void StatQuery_Prepare( stat_query_t *query ) {
	if( !query->req && query->url ) {
		// GET request, finish the url and create the object
		query->req = wswcurl_create( query->iface, query->url );
	}
	// only allow json for POST requests
	else if( query->has_json ) {
		const char *json_text;
		size_t jsonSize, b64Size;
		unsigned long compSize;
		void *compData, *b64Data;
		int z_result;

		if( query->url ) {
			Com_Printf( "StatQuery: Tried to add JSON field to GET request\n" );
			return;
		}

		json_text = cJSON_Print( query->json_out );
		jsonSize = strlen( json_text );

		// compress
		compSize = ( jsonSize * 1.1 ) + 12;
		compData = SQALLOC( compSize );
		if( compData == NULL ) {
			Com_Printf( "StatQuery: Failed to allocate space for compressed JSON\n" );
			return;
		}
		z_result = qzcompress( compData, &compSize, (unsigned char*)json_text, jsonSize );
		if( z_result != Z_OK ) {
			Com_Printf( "StatQuery: Failed to compress JSON\n" );
			SQFREE( compData );
			return;
		}

		// base64
		b64Data = base64_encode( compData, compSize, &b64Size );
		if( b64Data == NULL ) {
			Com_Printf( "StatQuery: Failed to base64_encode JSON\n" );
			SQFREE( compData );
			return;
		}

		// Com_Printf("Match report size: %u, compressed: %u, base64'd: %u\n", reportSize, compSize, b64Size );

		// we dont need this anymore
		SQFREE( compData );
		compData = NULL;

		// set the json field to POST request
		wswcurl_formadd_raw( query->req, "data", b64Data, b64Size );

		free( b64Data );
	}
}

static void StatQuery_Send( stat_query_t *query ) {
	StatQuery_Prepare( query );

	// check whether our curl request is valid (might have been delayed-allocated in StatQuery_Prepare)
	if( !query->req ) {
		if( query->callback_fn ) {
			query->callback_fn( query, false, query->customp );
		}
		StatQuery_DestroyQuery( query );
		return;
	}

	wswcurl_stream_callbacks( query->req, NULL, StatQuery_CallbackGeneric, NULL, (void*)query );
	wswcurl_start( query->req );
}

static void StatQuery_SetField( stat_query_t *query, const char *name, const char *value ) {
	if( query->req ) {
		wswcurl_formadd( query->req, name, "%s", value );
	} else if( query->url ) {
		// GET request, store parameters
		// add in '=', '&' and '\0' = 3

		// FIXME: add proper URL encode
		size_t len = strlen( query->url ) + strlen( name ) + strlen( value ) + 3;
		query->url = SQREALLOC( query->url, len );
		strcat( query->url, name );
		strcat( query->url, "=" );
		strcat( query->url, value );
		strcat( query->url, "&" );
	}
}

static stat_query_section_t *StatQuery_CreateSection( stat_query_t *query, stat_query_section_t *parent, const char *section_name ) {
	cJSON *cparent = (cJSON *)parent;
	cJSON *this_section = NULL;

	// this is only set in CreateSection/Array, needless to set in atoms
	query->has_json = true;

	this_section = cJSON_CreateObject();

	if( cparent == NULL ) {
		cparent = query->json_out;
	}

	if( cparent->type == cJSON_Array ) {
		cJSON_AddItemToArray( cparent, this_section );
	} else if( cparent->type == cJSON_Object ) {
		cJSON_AddItemToObject( cparent, section_name, this_section );
	}

	return (stat_query_section_t*)this_section;
}

static stat_query_section_t *StatQuery_CreateArray( stat_query_t *query, stat_query_section_t *parent, const char *section_name ) {
	cJSON *cparent = (cJSON *)parent;
	cJSON *this_section = NULL;

	query->has_json = true;

	this_section = cJSON_CreateArray();

	if( cparent == NULL ) {
		cparent = query->json_out;
	}

	if( cparent->type == cJSON_Array ) {
		cJSON_AddItemToArray( cparent, this_section );
	} else if( cparent->type == cJSON_Object ) {
		cJSON_AddItemToObject( cparent, section_name, this_section );
	}

	return (stat_query_section_t *)this_section;
}

static void StatQuery_SetString( stat_query_section_t *section, const char *prop_name, const char *prop_value ) {
	cJSON_AddStringToObject( (cJSON *)section, prop_name, prop_value );
}

static void StatQuery_SetNumber( stat_query_section_t *section, const char *prop_name, double prop_value ) {
	cJSON_AddNumberToObject( (cJSON *)section, prop_name, prop_value );
}

// remove these 2 in favour of GetArraySection( array, idx ) + SetString/SetNumber
static void StatQuery_SetArrayString( stat_query_section_t *array, int idx, const char *prop_name, const char *prop_value ) {
	cJSON *section;

	section = cJSON_GetArrayItem( (cJSON *)array, idx );
	StatQuery_SetString( (stat_query_section_t*)section, prop_name, prop_value );
}

static void StatQuery_SetArrayNumber( stat_query_section_t *array, int idx, const char *prop_name, double prop_value ) {
	cJSON *section;

	section = cJSON_GetArrayItem( (cJSON *)array, idx );
	StatQuery_SetNumber( (stat_query_section_t*)section, prop_name, prop_value );
}

static void StatQuery_AddArrayString( stat_query_section_t *array, const char *value ) {
	cJSON *object;

	object = cJSON_CreateString( value );
	cJSON_AddItemToArray( (cJSON *)array, object );
}

static void StatQuery_AddArrayNumber( stat_query_section_t *array, double prop_value ) {
	cJSON *object;

	object = cJSON_CreateNumber( prop_value );
	cJSON_AddItemToArray( (cJSON *)array, object );
}

static bool StatQuery_IsArray( const stat_query_section_t *section ) {
	return section && ( (cJSON *)section )->type == cJSON_Array;
}

static bool StatQuery_IsObject( const stat_query_section_t *section ) {
	return section && ( (cJSON *)section )->type == cJSON_Object;
}

static stat_query_section_t *StatQuery_GetRoot( stat_query_t *query ) {
	return (stat_query_section_t *)query->json_in;
}

static stat_query_section_t *StatQuery_GetOutRoot( stat_query_t *query ) {
	return (stat_query_section_t *)query->json_out;
}

static stat_query_section_t *StatQuery_GetSection( stat_query_section_t *parent, const char *name ) {
	return (stat_query_section_t *)cJSON_GetObjectItem( (cJSON *)parent, name );
}

static double StatQuery_GetNumber( stat_query_section_t *parent, const char *name ) {
	cJSON *object = cJSON_GetObjectItem( (cJSON *)parent, name );
	return StatQuery_JsonToNumber( object );
}

static const char *StatQuery_GetString( stat_query_section_t *parent, const char *name ) {
	cJSON *object = cJSON_GetObjectItem( (cJSON *)parent, name );
	return StatQuery_JsonToString( object );
}

static int StatQuery_GetArraySize( stat_query_section_t *section ) {
	return cJSON_GetArraySize( (cJSON *)section );
}

static stat_query_section_t *StatQuery_GetArraySection( stat_query_section_t *parent, int idx ) {
	return (stat_query_section_t *)cJSON_GetArrayItem( (cJSON *)parent, idx );
}

static double StatQuery_GetArrayNumber( stat_query_section_t *parent, int idx ) {
	cJSON *object = cJSON_GetArrayItem( (cJSON *)parent, idx );
	return StatQuery_JsonToNumber( object );
}

static const char *StatQuery_GetArrayString( stat_query_section_t *parent, int idx ) {
	cJSON *object = cJSON_GetArrayItem( (cJSON *)parent, idx );
	return StatQuery_JsonToString( object );
}

static const char *StatQuery_GetRawResponse( stat_query_t *query ) {
	if( !query->response_raw ) {
		StatQuery_CacheResponseRaw( query );
	}
	return query->response_raw;
}

// char *const *StatQuery_GetTokenizedResponse( stat_query_t *query, int *argc )
static char **StatQuery_GetTokenizedResponse( stat_query_t *query, int *argc ) {
	if( !query->response_tokens ) {
		StatQuery_CacheTokenized( query );
	}

	if( argc ) {
		*argc = query->response_numtokens;
	}

	return query->response_tokens;
}

static void StatQuery_Poll( void ) {
	// TODO: handle and state validation
	wswcurl_perform();
}

//===============================================

void *SQ_JSON_Alloc( size_t size ) {
	return SQALLOC( size );
}

void SQ_JSON_Free( void *ptr ) {
	SQFREE( ptr );
}

stat_query_api_t *StatQuery_GetAPI( void ) {
	return &sq_export;
}

void StatQuery_Init( void ) {
	cJSON_Hooks hooks;

	if( sq_mempool == NULL ) {
		sq_mempool = Mem_AllocPool( NULL, "StatQuery" );
	}

	// already initialized?
	if( sq_refcount++ > 0 ) {
		return;
	}

	// populate API structure
	sq_export.CreateQuery = StatQuery_CreateQuery;
	sq_export.DestroyQuery = StatQuery_DestroyQuery;
	sq_export.SetCallback = StatQuery_SetCallback;
	sq_export.Send = StatQuery_Send;
	sq_export.SetField = StatQuery_SetField;
	sq_export.GetRoot = StatQuery_GetRoot;
	sq_export.GetOutRoot = StatQuery_GetOutRoot;
	sq_export.GetSection = StatQuery_GetSection;
	sq_export.GetNumber = StatQuery_GetNumber;
	sq_export.GetString = StatQuery_GetString;
	sq_export.GetArraySize = StatQuery_GetArraySize;
	sq_export.GetArraySection = StatQuery_GetArraySection;
	sq_export.GetArrayNumber = StatQuery_GetArrayNumber;
	sq_export.GetArrayString = StatQuery_GetArrayString;
	sq_export.CreateSection = StatQuery_CreateSection;
	sq_export.CreateArray = StatQuery_CreateArray;
	sq_export.SetString = StatQuery_SetString;
	sq_export.SetNumber = StatQuery_SetNumber;
	sq_export.SetArrayString = StatQuery_SetArrayString;
	sq_export.SetArrayNumber = StatQuery_SetArrayNumber;
	sq_export.AddArrayString = StatQuery_AddArrayString;
	sq_export.AddArrayNumber = StatQuery_AddArrayNumber;
	sq_export.IsArray = StatQuery_IsArray;
	sq_export.IsObject = StatQuery_IsObject;
	sq_export.GetRawResponse = StatQuery_GetRawResponse;
	sq_export.GetTokenizedResponse = StatQuery_GetTokenizedResponse;
	sq_export.Poll = StatQuery_Poll;

	// init JSON
	hooks.malloc_fn = SQ_JSON_Alloc;
	hooks.free_fn = SQ_JSON_Free;
	cJSON_InitHooks( &hooks );
}

void StatQuery_Shutdown( void ) {
	// remaining references?
	if( --sq_refcount > 0 ) {
		return;
	}

	memset( &sq_export, 0, sizeof( sq_export ) );

	if( sq_mempool != NULL ) {
		Mem_FreePool( &sq_mempool );
	}

	sq_mempool = NULL;
}
