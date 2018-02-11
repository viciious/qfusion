/*
Copyright (C) 2012 Chasseur de bots

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

#include "angelscript.h"
#include "../gameshared/q_angeliface.h"

#define G_AsMalloc                              G_LevelMalloc
#define G_AsFree                                G_LevelFree

extern angelwrap_api_t *angelExport;

#define GAME_SCRIPTS_DIRECTORY              "progs"

#define GAMETYPE_SCRIPTS_MODULE_NAME        "gametype"
#define GAMETYPE_SCRIPTS_DIRECTORY          "gametypes"

#define MAP_SCRIPTS_MODULE_NAME             "map"
#define MAP_SCRIPTS_DIRECTORY               "maps"
#define MAP_SCRIPTS_PROJECT_EXTENSION       ".mp"

#define GAME_AS_ENGINE()                    ( static_cast<asIScriptEngine *>( game.asEngine ) )

asIScriptModule *G_LoadGameScript( const char *moduleName, const char *dir, const char *filename, const char *ext );
bool G_ExecutionErrorReport( int error );

typedef struct asEnumVal_s {
	const char * name;
	int value;
} asEnumVal_t;

typedef struct asEnum_s {
	const char * name;
	const asEnumVal_t * values;
} asEnum_t;

typedef struct asFuncdef_s {
	const char * declaration;
} asFuncdef_t;

typedef struct asBehavior_s {
	asEBehaviours behavior;
	const char * declaration;
	asSFuncPtr funcPointer;
	asECallConvTypes callConv;
} asBehavior_t;

typedef struct asMethod_s {
	const char * declaration;
	asSFuncPtr funcPointer;
	asECallConvTypes callConv;
} asMethod_t;

typedef struct asProperty_s {
	const char * declaration;
	unsigned int offset;
} asProperty_t;

typedef struct asClassDescriptor_s {
	const char * name;
	asDWORD typeFlags;
	size_t size;
	const asFuncdef_t * funcdefs;
	const asBehavior_t * objBehaviors;
	const asMethod_t * objMethods;
	const asProperty_t * objProperties;
	const void * stringFactory;
	const void * stringFactory_asGeneric;
} asClassDescriptor_t;

typedef struct asglobfuncs_s {
	const char *declaration;
	asSFuncPtr pointer;
	void **asFuncPtr;
} asglobfuncs_t;

typedef struct asglobproperties_s {
	const char *declaration;
	void *pointer;
} asglobproperties_t;

static void asnullfunc( void ) {}

#define ASLIB_LOCAL_CLASS_DESCR( x )

// Changed from offsetof() to asOFFSET() to avoid warnings
// about getting offsets of fields on non-standard-layout types
// (while type layouts in these cases are predictable and these offsets work fine)
#define ASLIB_FOFFSET( s,m )                      ( ( unsigned )asOFFSET( s,m ) )

#define ASLIB_ENUM_VAL( name )                    { #name,(int)name }
#define ASLIB_ENUM_VAL_NULL                     { NULL, 0 }

#define ASLIB_ENUM_NULL                         { NULL, NULL }

#define ASLIB_FUNCTION_DECL( type,name,params )   (#type " " #name #params )

#define ASLIB_PROPERTY_DECL( type,name )          #type " " #name

#define ASLIB_FUNCTION_NULL                     NULL
#define ASLIB_FUNCDEF_NULL                      { ASLIB_FUNCTION_NULL }
#define ASLIB_BEHAVIOR_NULL                     { asBEHAVE_CONSTRUCT, ASLIB_FUNCTION_NULL, asFUNCTION( asnullfunc ), asCALL_CDECL }
#define ASLIB_METHOD_NULL                       { ASLIB_FUNCTION_NULL, asFUNCTION( asnullfunc ), asCALL_CDECL }
#define ASLIB_PROPERTY_NULL                     { NULL, 0 }

#define ASLIB_Malloc( s )                         ( aslib_import.Mem_Alloc( s,__FILE__,__LINE__ ) )
#define ASLIB_Free( x )                           ( aslib_import.Mem_Free( x,__FILE__,__LINE__ ) )
