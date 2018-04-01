/*
Copyright (C) 2002-2003 Victor Luchits

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
#include "client.h"

static sound_export_t *se;
static mempool_t *cl_soundmodulepool;

static void *sound_library = NULL;

static cvar_t *s_module = NULL;
static cvar_t *s_module_fallback = NULL;
static bool s_loaded = false;

static void CL_SoundModule_SetAttenuationModel( void );

/*
* CL_SetSoundExtension
*/
static char *CL_SetSoundExtension( const char *name ) {
	unsigned int i;
	char *finalname;
	size_t finalname_size, maxlen;
	const char *extension;

	assert( name );

	if( COM_FileExtension( name ) ) {
		return TempCopyString( name );
	}

	maxlen = 0;
	for( i = 0; i < NUM_SOUND_EXTENSIONS; i++ ) {
		if( strlen( SOUND_EXTENSIONS[i] ) > maxlen ) {
			maxlen = strlen( SOUND_EXTENSIONS[i] );
		}
	}

	finalname_size = strlen( name ) + maxlen + 1;
	finalname = Mem_TempMalloc( finalname_size );
	Q_strncpyz( finalname, name, finalname_size );

	extension = FS_FirstExtension( finalname, SOUND_EXTENSIONS, NUM_SOUND_EXTENSIONS );
	if( extension ) {
		Q_strncatz( finalname, extension, finalname_size );
	}
	// if not found, we just pass it without the extension

	return finalname;
}

#ifndef _MSC_VER
static void CL_SoundModule_Error( const char *msg ) __attribute__( ( noreturn ) );
#else
__declspec( noreturn ) static void CL_SoundModule_Error( const char *msg );
#endif

/*
* CL_SoundModule_Error
*/
static void CL_SoundModule_Error( const char *msg ) {
	Com_Error( ERR_FATAL, "%s", msg );
}

/*
* CL_SoundModule_Print
*/
static void CL_SoundModule_Print( const char *msg ) {
	Com_Printf( "%s", msg );
}

/*
* CL_SoundModule_MemAlloc
*/
static void *CL_SoundModule_MemAlloc( mempool_t *pool, size_t size, const char *filename, int fileline ) {
	return _Mem_Alloc( pool, size, MEMPOOL_SOUND, 0, filename, fileline );
}

/*
* CL_SoundModule_MemFree
*/
static void CL_SoundModule_MemFree( void *data, const char *filename, int fileline ) {
	_Mem_Free( data, MEMPOOL_SOUND, 0, filename, fileline );
}

/*
* CL_SoundModule_MemAllocPool
*/
static mempool_t *CL_SoundModule_MemAllocPool( const char *name, const char *filename, int fileline ) {
	return _Mem_AllocPool( cl_soundmodulepool, name, MEMPOOL_SOUND, filename, fileline );
}

/*
* CL_SoundModule_MemFreePool
*/
static void CL_SoundModule_MemFreePool( mempool_t **pool, const char *filename, int fileline ) {
	_Mem_FreePool( pool, MEMPOOL_SOUND, 0, filename, fileline );
}

/*
* CL_SoundModule_MemEmptyPool
*/
static void CL_SoundModule_MemEmptyPool( mempool_t *pool, const char *filename, int fileline ) {
	_Mem_EmptyPool( pool, MEMPOOL_SOUND, 0, filename, fileline );
}

/*
* CL_SoundModule_Load
*
* Helper function to try loading sound module with certain name
*/
static bool CL_SoundModule_Load( const char *name, sound_import_t *import, bool verbose ) {
	int apiversion;
	size_t file_size;
	char *file;
	void *( *GetSoundAPI )( void * );
	dllfunc_t funcs[2];

	if( verbose ) {
		Com_Printf( "Loading sound module: %s\n", name );
	}

	file_size = strlen( LIB_DIRECTORY "/" LIB_PREFIX "snd_" ) + strlen( name ) + 1 + strlen( ARCH ) + strlen( LIB_SUFFIX ) + 1;
	file = Mem_TempMalloc( file_size );
	Q_snprintfz( file, file_size, LIB_DIRECTORY "/" LIB_PREFIX "snd_%s_" ARCH LIB_SUFFIX, name );

	funcs[0].name = "GetSoundAPI";
	funcs[0].funcPointer = ( void ** )&GetSoundAPI;
	funcs[1].name = NULL;
	sound_library = Com_LoadLibrary( file, funcs );

	Mem_TempFree( file );

	if( !sound_library ) {
		Com_Printf( "Loading %s failed\n", name );
		return false;
	}

	s_loaded = true;

	se = ( sound_export_t * )GetSoundAPI( import );
	apiversion = se->API();
	if( apiversion != SOUND_API_VERSION ) {
		CL_SoundModule_Shutdown( verbose );
		Com_Printf( "Wrong module version for %s: %i, not %i\n", name, apiversion, SOUND_API_VERSION );
		return false;
	}

	if( !se->Init( VID_GetWindowHandle(), MAX_EDICTS, verbose ) ) {
		CL_SoundModule_Shutdown( verbose );
		Com_Printf( "Initialization of %s failed\n", name );
		return false;
	}

	if( verbose ) {
		Com_Printf( "Initialization of %s successful\n", name );
	}

	return true;
}

static void CL_SoundModule_Trace( trace_t *tr, vec3_t start, vec3_t end, vec3_t mins, vec3_t maxs, int mask ) {
	if( cl.sound_cms ) {
		CM_TransformedBoxTrace( cl.sound_cms, tr, start, end, mins, maxs, NULL, mask, NULL, NULL );
		return;
	}

	tr->fraction = 1.0f;
	tr->startsolid = false;
	tr->allsolid = false;
}

static int CL_SoundModule_PointContents( vec3_t p ) {
	if( cl.sound_cms ) {
		return CM_TransformedPointContents( cl.sound_cms, p, NULL, NULL, NULL );
	}
	return 0;
}

static int CL_SoundModule_PointLeafNum( const vec3_t p ) {
	if( cl.sound_cms ) {
		return CM_PointLeafnum( cl.sound_cms, p );
	}
	return 0;
}

static bool CL_SoundModule_LeafsInPVS( int leafnum1, int leafnum2 ) {
	if( cl.sound_cms ) {
		return CM_LeafsInPVS( cl.sound_cms, leafnum1, leafnum2 );
	}
	return true;
}

/*
* CL_SoundModule_Init
*/
void CL_SoundModule_Init( bool verbose ) {
	static const char *sound_modules[] = { "qf", "openal" };
	static const int num_sound_modules = sizeof( sound_modules ) / sizeof( sound_modules[0] );
	int sm, smfb;
	sound_import_t import;

	if( !s_module ) {
		s_module = Cvar_Get( "s_module", "1", CVAR_ARCHIVE | CVAR_LATCH_SOUND );
	}
	if( !s_module_fallback ) {
		s_module_fallback = Cvar_Get( "s_module_fallback", "2", CVAR_LATCH_SOUND );
	}

	// unload anything we have now
	CL_SoundModule_Shutdown( verbose );

	if( verbose ) {
		Com_Printf( "------- sound initialization -------\n" );
	}

	Cvar_GetLatchedVars( CVAR_LATCH_SOUND );

	if( s_module->integer < 0 || s_module->integer > num_sound_modules ) {
		Com_Printf( "Invalid value for s_module (%i), reseting to default\n", s_module->integer );
		Cvar_ForceSet( "s_module", s_module->dvalue );
	}

	if( s_module_fallback->integer < 0 || s_module_fallback->integer > num_sound_modules ) {
		Com_Printf( "Invalid value for s_module_fallback (%i), reseting to default\n", s_module_fallback->integer );
		Cvar_ForceSet( "s_module_fallback", s_module_fallback->dvalue );
	}

	if( !s_module->integer ) {
		if( verbose ) {
			Com_Printf( "Not loading a sound module\n" );
			Com_Printf( "------------------------------------\n" );
		}
		return;
	}

	cl_soundmodulepool = Mem_AllocPool( NULL, "Client Sound Module" );

	import.Error = CL_SoundModule_Error;
	import.Print = CL_SoundModule_Print;

	import.Cvar_Get = Cvar_Get;
	import.Cvar_Set = Cvar_Set;
	import.Cvar_SetValue = Cvar_SetValue;
	import.Cvar_ForceSet = Cvar_ForceSet;
	import.Cvar_String = Cvar_String;
	import.Cvar_Value = Cvar_Value;

	import.Cmd_Argc = Cmd_Argc;
	import.Cmd_Argv = Cmd_Argv;
	import.Cmd_Args = Cmd_Args;

	import.Cmd_AddCommand = Cmd_AddCommand;
	import.Cmd_RemoveCommand = Cmd_RemoveCommand;
	import.Cmd_ExecuteText = Cbuf_ExecuteText;
	import.Cmd_Execute = Cbuf_Execute;
	import.Cmd_SetCompletionFunc = Cmd_SetCompletionFunc;

	import.FS_FOpenFile = FS_FOpenFile;
	import.FS_Read = FS_Read;
	import.FS_Write = FS_Write;
	import.FS_Print = FS_Print;
	import.FS_Tell = FS_Tell;
	import.FS_Seek = FS_Seek;
	import.FS_Eof = FS_Eof;
	import.FS_Flush = FS_Flush;
	import.FS_FCloseFile = FS_FCloseFile;
	import.FS_RemoveFile = FS_RemoveFile;
	import.FS_GetFileList = FS_GetFileList;
	import.FS_IsUrl = FS_IsUrl;

	import.Sys_Milliseconds = Sys_Milliseconds;
	import.Sys_Microseconds = Sys_Microseconds;
	import.Sys_Sleep = Sys_Sleep;

	import.Sys_LoadLibrary = Com_LoadSysLibrary;
	import.Sys_UnloadLibrary = Com_UnloadLibrary;

	import.Mem_Alloc = CL_SoundModule_MemAlloc;
	import.Mem_Free = CL_SoundModule_MemFree;
	import.Mem_AllocPool = CL_SoundModule_MemAllocPool;
	import.Mem_FreePool = CL_SoundModule_MemFreePool;
	import.Mem_EmptyPool = CL_SoundModule_MemEmptyPool;

	import.Trace = CL_SoundModule_Trace;
	import.PointContents = CL_SoundModule_PointContents;
	import.PointLeafNum = CL_SoundModule_PointLeafNum;
	import.LeafsInPVS = CL_SoundModule_LeafsInPVS;

	import.Thread_Create = QThread_Create;
	import.Thread_Join = QThread_Join;
	import.Thread_Yield = QThread_Yield;
	import.Mutex_Create = QMutex_Create;
	import.Mutex_Destroy = QMutex_Destroy;
	import.Mutex_Lock = QMutex_Lock;
	import.Mutex_Unlock = QMutex_Unlock;

	import.BufPipe_Create = QBufPipe_Create;
	import.BufPipe_Destroy = QBufPipe_Destroy;
	import.BufPipe_Finish = QBufPipe_Finish;
	import.BufPipe_WriteCmd = QBufPipe_WriteCmd;
	import.BufPipe_ReadCmds = QBufPipe_ReadCmds;
	import.BufPipe_Wait = QBufPipe_Wait;

	sm = bound( 1, s_module->integer, num_sound_modules );
	smfb = bound( 0, s_module_fallback->integer, num_sound_modules );

	if( !CL_SoundModule_Load( sound_modules[sm - 1], &import, verbose ) ) {
		if( s_module->integer == smfb || !smfb ||
			!CL_SoundModule_Load( sound_modules[smfb - 1], &import, verbose ) ) {
			if( verbose ) {
				Com_Printf( "Couldn't load a sound module\n" );
				Com_Printf( "------------------------------------\n" );
			}
			Mem_FreePool( &cl_soundmodulepool );
			se = NULL;
			return;
		}
		Cvar_ForceSet( "s_module", va( "%i", smfb ) );
	}

	CL_SoundModule_SetAttenuationModel();

	// check memory integrity
	Mem_DebugCheckSentinelsGlobal();

	if( verbose ) {
		Com_Printf( "------------------------------------\n" );
	}
}

/*
* CL_SoundModule_Shutdown
*/
void CL_SoundModule_Shutdown( bool verbose ) {
	if( !s_loaded ) {
		return;
	}

	s_loaded = false;

	if( se && se->API() == SOUND_API_VERSION ) {
		se->Shutdown( verbose );
		se = NULL;
	}

	if( cl.sound_cms && cl.sound_cms != cl.cms ) {
		CM_ReleaseReference( cl.sound_cms );
	}
	cl.sound_cms = NULL;

	Com_UnloadLibrary( &sound_library );
	Mem_FreePool( &cl_soundmodulepool );
}

/*
* CL_SoundModule_BeginRegistration
*/
void CL_SoundModule_BeginRegistration( void ) {
	if( se ) {
		se->BeginRegistration();
	}

	cl.sound_cms = NULL;
}

/*
* CL_SoundModule_EndRegistration
*/
void CL_SoundModule_EndRegistration( void ) {
	if( se ) {
		se->EndRegistration();

		// CM setup has to be done here due to initialization order issues after restarts
		assert( !cl.sound_cms );
		// Just share the client CM state as it is now fully reentrant and thread-safe
		cl.sound_cms = cl.cms;
	}
}

/*
* CL_SoundModule_StopAllSounds
*/
void CL_SoundModule_StopAllSounds( bool clear, bool stopMusic ) {
	if( se ) {
		se->StopAllSounds( clear, stopMusic );
	}
}

/*
* CL_SoundModule_Clear
*/
void CL_SoundModule_Clear( void ) {
	if( se ) {
		se->Clear();
	}
}

/*
* CL_SoundModule_SetEntitySpatilization
*/
void CL_SoundModule_SetEntitySpatilization( int entNum, vec3_t origin, vec3_t velocity ) {
	if( se ) {
		se->SetEntitySpatialization( entNum, origin, velocity );
	}
}

/*
* CL_SoundModule_Update
*/
void CL_SoundModule_Update( const vec3_t origin, const vec3_t velocity, const mat3_t axis,
							const char *identity, bool avidump ) {
	if( se ) {
		se->Update( origin, velocity, axis, avidump );
	}
	CL_Mumble_Update( origin, axis, identity );
}

/*
* CL_SoundModule_Activate
*/
void CL_SoundModule_Activate( bool activate ) {
	if( se ) {
		se->Activate( activate );
	}
}

/*
* CL_SoundModule_SetAttenuationModel
*/
static void CL_SoundModule_SetAttenuationModel( void ) {
#ifdef PUBLIC_BUILD
	const int model = S_DEFAULT_ATTENUATION_MODEL;
	const float maxdistance = S_DEFAULT_ATTENUATION_MAXDISTANCE;
	const float refdistance = S_DEFAULT_ATTENUATION_REFDISTANCE;
#else
	cvar_t *s_attenuation_model = Cvar_Get( "s_attenuation_model", va( "%i", S_DEFAULT_ATTENUATION_MODEL ), CVAR_DEVELOPER | CVAR_LATCH_SOUND );
	cvar_t *s_attenuation_maxdistance = Cvar_Get( "s_attenuation_maxdistance", va( "%i", S_DEFAULT_ATTENUATION_MAXDISTANCE ), CVAR_DEVELOPER | CVAR_LATCH_SOUND );
	cvar_t *s_attenuation_refdistance = Cvar_Get( "s_attenuation_refdistance", va( "%i", S_DEFAULT_ATTENUATION_REFDISTANCE ), CVAR_DEVELOPER | CVAR_LATCH_SOUND );

	const int model = s_attenuation_model->integer;
	const float maxdistance = s_attenuation_maxdistance->value;
	const float refdistance = s_attenuation_refdistance->value;
#endif

	if( se ) {
		se->SetAttenuationModel( model, maxdistance, refdistance );
	}
}

/*
* CL_SoundModule_RegisterSound
*/
struct sfx_s *CL_SoundModule_RegisterSound( const char *name ) {
	assert( name );

	if( se ) {
		struct sfx_s *retval;
		char *finalname;

		finalname = CL_SetSoundExtension( name );
		retval = se->RegisterSound( finalname );
		Mem_TempFree( finalname );

		return retval;
	} else {
		return NULL;
	}
}

/*
* CL_SoundModule_StartFixedSound
*/
void CL_SoundModule_StartFixedSound( struct sfx_s *sfx, const vec3_t origin, int channel, float fvol,
									 float attenuation ) {
	if( se ) {
		se->StartFixedSound( sfx, origin, channel, fvol, attenuation );
	}
}

/*
* CL_SoundModule_StartRelativeSound
*/
void CL_SoundModule_StartRelativeSound( struct sfx_s *sfx, int entnum, int channel, float fvol, float attenuation ) {
	if( se ) {
		se->StartRelativeSound( sfx, entnum, channel, fvol, attenuation );
	}
}

/*
* CL_SoundModule_StartGlobalSound
*/
void CL_SoundModule_StartGlobalSound( struct sfx_s *sfx, int channel, float fvol ) {
	if( se ) {
		se->StartGlobalSound( sfx, channel, fvol );
	}
}

/*
* CL_SoundModule_StartLocalSound
*/
void CL_SoundModule_StartLocalSoundByName( const char *name ) {
	assert( name );

	if( se ) {
		char *finalname;

		finalname = CL_SetSoundExtension( name );
		se->StartLocalSoundByName( finalname );
		Mem_TempFree( finalname );
	}
}

/*
* CL_SoundModule_StartLocalSound
*/
void CL_SoundModule_StartLocalSound( struct sfx_s *sfx, float fvol ) {
	if( se ) {
		se->StartLocalSound( sfx, fvol );
	}
}

/*
* CL_SoundModule_AddLoopSound
*/
void CL_SoundModule_AddLoopSound( struct sfx_s *sfx, int entnum, float fvol, float attenuation ) {
	if( se ) {
		se->AddLoopSound( sfx, entnum, fvol, attenuation );
	}
}

/*
* CL_SoundModule_RawSamples
*/
void CL_SoundModule_RawSamples( unsigned int samples, unsigned int rate,
								unsigned short width, unsigned short channels, const uint8_t *data, bool music ) {
	if( se ) {
		se->RawSamples( samples, rate, width, channels, data, music );
	}
}

/*
* CL_SoundModule_PositionedRawSamples
*/
void CL_SoundModule_PositionedRawSamples( int entnum, float fvol, float attenuation,
										  unsigned int samples, unsigned int rate,
										  unsigned short width, unsigned short channels, const uint8_t *data ) {
	if( se ) {
		se->PositionedRawSamples( entnum, fvol, attenuation,
								  samples, rate, width, channels, data );
	}
}

/*
* CL_SoundModule_GetRawSamplesLength
*/
unsigned int CL_SoundModule_GetRawSamplesLength( void ) {
	return se ? se->GetRawSamplesLength() : 0;
}

/*
* CL_SoundModule_GetPositionedRawSamplesLength
*/
unsigned int CL_SoundModule_GetPositionedRawSamplesLength( int entnum ) {
	return se ? se->GetPositionedRawSamplesLength( entnum ) : 0;
}

/*
* CL_SoundModule_StartBackgroundTrack
*/
void CL_SoundModule_StartBackgroundTrack( const char *intro, const char *loop, int mode ) {
	assert( intro );

	if( se ) {
		char *finalintro, *finalloop;

		finalintro = CL_SetSoundExtension( intro );
		if( loop ) {
			finalloop = CL_SetSoundExtension( loop );
		} else {
			finalloop = NULL;
		}
		se->StartBackgroundTrack( finalintro, finalloop, mode );
		Mem_TempFree( finalintro );
		if( finalloop ) {
			Mem_TempFree( finalloop );
		}
	}
}

/*
* CL_SoundModule_StopBackgroundTrack
*/
void CL_SoundModule_StopBackgroundTrack( void ) {
	if( se ) {
		se->StopBackgroundTrack();
	}
}

/*
* CL_SoundModule_LockBackgroundTrack
*/
void CL_SoundModule_LockBackgroundTrack( bool lock ) {
	if( se ) {
		se->LockBackgroundTrack( lock );
	}
}

/*
* CL_SoundModule_BeginAviDemo
*/
void CL_SoundModule_BeginAviDemo( void ) {
	if( se ) {
		se->BeginAviDemo();
	}
}

/*
* CL_SoundModule_StopAviDemo
*/
void CL_SoundModule_StopAviDemo( void ) {
	if( se ) {
		se->StopAviDemo();
	}
}

/*
=======================================================================

MUMBLE SUPPORT

=======================================================================
*/

#ifdef MUMBLE_SUPPORT

#include "libmumblelink/libmumblelink.h"

static cvar_t *cl_mumble;
static cvar_t *cl_mumble_scale;

/*
* CL_Mumble_Init
*/
void CL_Mumble_Init( void ) {
	cl_mumble =         Cvar_Get( "cl_mumble", "0", CVAR_ARCHIVE | CVAR_LATCH );
	cl_mumble_scale =   Cvar_Get( "cl_mumble_scale", "0.0254", CVAR_ARCHIVE );
}

/*
* CL_Mumble_Link
*/
void CL_Mumble_Link( void ) {
	if( !cl_mumble->integer ) {
		return;
	}

	if( !mumble_islinked() ) {
		int ret = mumble_link( APPLICATION );
		Com_Printf( "Mumble: Linking to Mumble application %s\n", ret == 0 ? "ok" : "failed" );
	}
}

/*
* CL_Mumble_Unlink
*/
void CL_Mumble_Unlink( void ) {
	if( !cl_mumble->integer ) {
		return;
	}

	if( mumble_islinked() ) {
		Com_Printf( "Mumble: Unlinking from Mumble application\n" );
		mumble_unlink();
	}
}

/*
* CL_Mumble_Update
*/
void CL_Mumble_Update( const vec3_t origin, const mat3_t axis, const char *identity ) {
	vec3_t mp, mf, mt;
	char context[256];

	if( !cl_mumble->integer ) {
		return;
	}
	if( !identity ) {
		return;
	}

	VectorScale( origin, cl_mumble_scale->value, mp );
	VectorCopy( &axis[AXIS_FORWARD], mf );
	VectorCopy( &axis[AXIS_UP], mt );

	if( cl_mumble->integer == 2 ) {
		Com_Printf( "MumbleUpdate:\n%f, %f, %f\n%f, %f, %f\n%f, %f, %f", mp[0], mp[1], mp[2], mf[0], mf[1], mf[2], mt[0], mt[1], mt[2] );
	}

	mumble_update_coordinates( mp, mf, mt );

	// for Mumble 1.2+  http://mumble.sourceforge.net/Link
	mumble_set_identity( identity );

	// TODO: add team to context?
	Q_strncpyz( context, NET_AddressToString( &cls.serveraddress ), sizeof( context ) );
	mumble_set_context( ( const unsigned char * )context, strlen( context ) + 1 );
}

/*
* CL_Mumble_Shutdown
*/
void CL_Mumble_Shutdown( void ) {
}

#else

/*
* CL_Mumble_Init
*/
void CL_Mumble_Init( void ) {
}

/*
* CL_Mumble_Link
*/
void CL_Mumble_Link( void ) {
}

/*
* CL_Mumble_Unlink
*/
void CL_Mumble_Unlink( void ) {
}

/*
* CL_Mumble_Update
*/
void CL_Mumble_Update( const vec3_t origin, const mat3_t axis, const char *identity ) {
}

/*
* CL_Mumble_Shutdown
*/
void CL_Mumble_Shutdown( void ) {
}

#endif
