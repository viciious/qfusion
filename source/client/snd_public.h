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

// snd_public.h -- sound dll information visible to engine

#define SOUND_API_VERSION   44

#define ATTN_NONE 0

//===============================================================

struct trace_s;

//
// functions provided by the main engine
//
typedef struct {
	// drops to console a client game error
#ifndef _MSC_VER
	void ( *Error )( const char *msg ) __attribute__( ( noreturn ) );
#else
	void ( *Error )( const char *msg );
#endif


	// console messages
	void ( *Print )( const char *msg );

	// console variable interaction
	cvar_t *( *Cvar_Get )( const char *name, const char *value, int flags );
	cvar_t *( *Cvar_Set )( const char *name, const char *value );
	void ( *Cvar_SetValue )( const char *name, float value );
	cvar_t *( *Cvar_ForceSet )( const char *name, const char *value );      // will return 0 0 if not found
	float ( *Cvar_Value )( const char *name );
	const char *( *Cvar_String )( const char *name );

	int ( *Cmd_Argc )( void );
	char *( *Cmd_Argv )( int arg );
	char *( *Cmd_Args )( void );        // concatenation of all argv >= 1

	void ( *Cmd_AddCommand )( const char *name, void ( *cmd )( void ) );
	void ( *Cmd_RemoveCommand )( const char *cmd_name );
	void ( *Cmd_ExecuteText )( int exec_when, const char *text );
	void ( *Cmd_Execute )( void );
	void ( *Cmd_SetCompletionFunc )( const char *cmd_name, char **( *completion_func )( const char *partial ) );

	// files will be memory mapped read only
	// the returned buffer may be part of a larger pak file,
	// or a discrete file from anywhere in the quake search path
	// a -1 return means the file does not exist
	// NULL can be passed for buf to just determine existance
	int ( *FS_FOpenFile )( const char *filename, int *filenum, int mode );
	int ( *FS_Read )( void *buffer, size_t len, int file );
	int ( *FS_Write )( const void *buffer, size_t len, int file );
	int ( *FS_Print )( int file, const char *msg );
	int ( *FS_Tell )( int file );
	int ( *FS_Seek )( int file, int offset, int whence );
	int ( *FS_Eof )( int file );
	int ( *FS_Flush )( int file );
	void ( *FS_FCloseFile )( int file );
	bool ( *FS_RemoveFile )( const char *filename );
	int ( *FS_GetFileList )( const char *dir, const char *extension, char *buf, size_t bufsize, int start, int end );
	bool ( *FS_IsUrl )( const char *url );

	int64_t ( *Sys_Milliseconds )( void );
	uint64_t ( *Sys_Microseconds )( void );
	void ( *Sys_Sleep )( unsigned int milliseconds );

	void *( *Sys_LoadLibrary )( const char *name, dllfunc_t * funcs );
	void ( *Sys_UnloadLibrary )( void **lib );

	// managed memory allocation
	struct mempool_s *( *Mem_AllocPool )( const char *name, const char *filename, int fileline );
	void *( *Mem_Alloc )( struct mempool_s *pool, size_t size, const char *filename, int fileline );
	void ( *Mem_Free )( void *data, const char *filename, int fileline );
	void ( *Mem_FreePool )( struct mempool_s **pool, const char *filename, int fileline );
	void ( *Mem_EmptyPool )( struct mempool_s *pool, const char *filename, int fileline );

	// environment sampling
	void ( *Trace )( struct trace_s *tr, vec3_t start, vec3_t end, vec3_t mins, vec3_t maxs, int mask );
	int ( *PointContents )( vec3_t p );
	// these calls serve utility of PVS sampling forcing efficient usage patterns
	int ( *PointLeafNum )( const vec3_t p );
	int ( *NumLeafs )();
	const vec3_t *( *GetLeafBounds )( int leafnum );
	bool ( *LeafsInPVS )( int leafnum1, int leafnum2 );

	const char *( *GetConfigString )( int index );

	// multithreading
	struct qthread_s *( *Thread_Create )( void *( *routine )( void* ), void *param );
	void ( *Thread_Join )( struct qthread_s *thread );
	void ( *Thread_Yield )( void );
	struct qmutex_s *( *Mutex_Create )( void );
	void ( *Mutex_Destroy )( struct qmutex_s **mutex );
	void ( *Mutex_Lock )( struct qmutex_s *mutex );
	void ( *Mutex_Unlock )( struct qmutex_s *mutex );

	struct qbufPipe_s *( *BufPipe_Create )( size_t bufSize, int flags );
	void ( *BufPipe_Destroy )( struct qbufPipe_s **pqueue );
	void ( *BufPipe_Finish )( struct qbufPipe_s *queue );
	void ( *BufPipe_WriteCmd )( struct qbufPipe_s *queue, const void *cmd, unsigned cmd_size );
	int ( *BufPipe_ReadCmds )( struct qbufPipe_s *queue, unsigned( **cmdHandlers )( const void * ) );
	void ( *BufPipe_Wait )( struct qbufPipe_s *queue, int ( *read )( struct qbufPipe_s *, unsigned( ** )( const void * ), bool ),
							unsigned( **cmdHandlers )( const void * ), unsigned timeout_msec );
} sound_import_t;

//
// functions exported by the sound subsystem
//
typedef struct {
	// if API is different, the dll cannot be used
	int ( *API )( void );

	// the init function will be called at each restart
	bool ( *Init )( void *hwnd, int maxEntities, bool verbose );
	void ( *Shutdown )( bool verbose );

	void ( *BeginRegistration )( void );
	void ( *EndRegistration )( void );

	void ( *StopAllSounds )( bool clear, bool stopMusic );

	void ( *Clear )( void );
	void ( *Update )( const vec3_t origin, const vec3_t velocity, const mat3_t axis, bool avidump );
	void ( *Activate )( bool active );

	void ( *SetAttenuationModel )( int model, float maxdistance, float refdistance );
	void ( *SetEntitySpatialization )( int entnum, const vec3_t origin, const vec3_t velocity );

	// playing
	struct sfx_s *( *RegisterSound )( const char *sample );
	void ( *StartFixedSound )( struct sfx_s *sfx, const vec3_t origin, int channel, float fvol, float attenuation );
	void ( *StartRelativeSound )( struct sfx_s *sfx, int entnum, int channel, float fvol, float attenuation );
	void ( *StartGlobalSound )( struct sfx_s *sfx, int channel, float fvol );
	void ( *StartLocalSoundByName )( const char *s );
	void ( *StartLocalSound )( struct sfx_s *sfx, float fvol );
	void ( *AddLoopSound )( struct sfx_s *sfx, int entnum, float fvol, float attenuation );

	// cinema
	void ( *RawSamples )( unsigned int samples, unsigned int rate, unsigned short width, unsigned short channels, const uint8_t *data, bool music );
	void ( *PositionedRawSamples )( int entnum, float fvol, float attenuation,
									unsigned int samples, unsigned int rate,
									unsigned short width, unsigned short channels, const uint8_t *data );
	unsigned int ( *GetRawSamplesLength )( void );
	unsigned int ( *GetPositionedRawSamplesLength )( int entnum );

	// music
	void ( *StartBackgroundTrack )( const char *intro, const char *loop, int mode );
	void ( *StopBackgroundTrack )( void );
	void ( *LockBackgroundTrack )( bool lock );

	// avi dump
	void ( *BeginAviDemo )( void );
	void ( *StopAviDemo )( void );
} sound_export_t;
