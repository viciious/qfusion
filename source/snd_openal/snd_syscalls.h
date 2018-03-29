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

#ifdef __cplusplus
extern "C" {
#endif

#ifdef SOUND_HARD_LINKED
#define SOUND_IMPORT sndi_imp_local
#endif

extern sound_import_t SOUND_IMPORT;

typedef struct qthread_s qthread_t;
typedef struct qmutex_s qmutex_t;
typedef struct qbufPipe_s qbufPipe_t;

static inline void trap_Print( const char *msg ) {
	SOUND_IMPORT.Print( msg );
}

#ifndef _MSC_VER
static inline void trap_Error( const char *msg ) __attribute__( ( noreturn ) );
#else
__declspec( noreturn ) static inline void trap_Error( const char *msg );
#endif


static inline void trap_Error( const char *msg ) {
	SOUND_IMPORT.Error( msg );
}

// cvars
static inline cvar_t *trap_Cvar_Get( const char *name, const char *value, int flags ) {
	return SOUND_IMPORT.Cvar_Get( name, value, flags );
}

static inline cvar_t *trap_Cvar_Set( const char *name, const char *value ) {
	return SOUND_IMPORT.Cvar_Set( name, value );
}

static inline void trap_Cvar_SetValue( const char *name, float value ) {
	SOUND_IMPORT.Cvar_SetValue( name, value );
}

static inline cvar_t *trap_Cvar_ForceSet( const char *name, const char *value ) {
	return SOUND_IMPORT.Cvar_ForceSet( name, value );
}

static inline float trap_Cvar_Value( const char *name ) {
	return SOUND_IMPORT.Cvar_Value( name );
}

static inline const char *trap_Cvar_String( const char *name ) {
	return SOUND_IMPORT.Cvar_String( name );
}

static inline int trap_Cmd_Argc( void ) {
	return SOUND_IMPORT.Cmd_Argc();
}

static inline char *trap_Cmd_Argv( int arg ) {
	return SOUND_IMPORT.Cmd_Argv( arg );
}

static inline char *trap_Cmd_Args( void ) {
	return SOUND_IMPORT.Cmd_Args();
}

static inline void trap_Cmd_AddCommand( const char *name, void ( *cmd )( void ) ) {
	SOUND_IMPORT.Cmd_AddCommand( name, cmd );
}

static inline void trap_Cmd_RemoveCommand( const char *cmd_name ) {
	SOUND_IMPORT.Cmd_RemoveCommand( cmd_name );
}

static inline void trap_Cmd_ExecuteText( int exec_when, const char *text ) {
	SOUND_IMPORT.Cmd_ExecuteText( exec_when, text );
}

static inline void trap_Cmd_Execute( void ) {
	SOUND_IMPORT.Cmd_Execute();
}

static inline int trap_FS_FOpenFile( const char *filename, int *filenum, int mode ) {
	return SOUND_IMPORT.FS_FOpenFile( filename, filenum, mode );
}

static inline int trap_FS_Read( void *buffer, size_t len, int file ) {
	return SOUND_IMPORT.FS_Read( buffer, len, file );
}

static inline int trap_FS_Write( const void *buffer, size_t len, int file ) {
	return SOUND_IMPORT.FS_Write( buffer, len, file );
}

static inline int trap_FS_Print( int file, const char *msg ) {
	return SOUND_IMPORT.FS_Print( file, msg );
}

static inline int trap_FS_Tell( int file ) {
	return SOUND_IMPORT.FS_Tell( file );
}

static inline int trap_FS_Seek( int file, int offset, int whence ) {
	return SOUND_IMPORT.FS_Seek( file, offset, whence );
}

static inline int trap_FS_Eof( int file ) {
	return SOUND_IMPORT.FS_Eof( file );
}

static inline int trap_FS_Flush( int file ) {
	return SOUND_IMPORT.FS_Flush( file );
}

static inline void trap_FS_FCloseFile( int file ) {
	SOUND_IMPORT.FS_FCloseFile( file );
}

static inline bool trap_FS_RemoveFile( const char *filename ) {
	return SOUND_IMPORT.FS_RemoveFile( filename );
}

static inline int trap_FS_GetFileList( const char *dir, const char *extension, char *buf, size_t bufsize, int start, int end ) {
	return SOUND_IMPORT.FS_GetFileList( dir, extension, buf, bufsize, start, end );
}

static inline bool trap_FS_IsUrl( const char *url ) {
	return SOUND_IMPORT.FS_IsUrl( url );
}

// misc
static inline int64_t trap_Milliseconds( void ) {
	return SOUND_IMPORT.Sys_Milliseconds();
}

static inline void trap_Sleep( unsigned int milliseconds ) {
	SOUND_IMPORT.Sys_Sleep( milliseconds );
}

static inline struct mempool_s *trap_MemAllocPool( const char *name, const char *filename, int fileline ) {
	return SOUND_IMPORT.Mem_AllocPool( name, filename, fileline );
}

static inline ATTRIBUTE_MALLOC void *trap_MemAlloc( struct mempool_s *pool, size_t size, const char *filename, int fileline ) {
	return SOUND_IMPORT.Mem_Alloc( pool, size, filename, fileline );
}

static inline void trap_MemFree( void *data, const char *filename, int fileline ) {
	SOUND_IMPORT.Mem_Free( data, filename, fileline );
}

static inline void trap_MemFreePool( struct mempool_s **pool, const char *filename, int fileline ) {
	SOUND_IMPORT.Mem_FreePool( pool, filename, fileline );
}

static inline void trap_MemEmptyPool( struct mempool_s *pool, const char *filename, int fileline ) {
	SOUND_IMPORT.Mem_EmptyPool( pool, filename, fileline );
}

static inline void *trap_LoadLibrary( const char *name, dllfunc_t *funcs ) {
	return SOUND_IMPORT.Sys_LoadLibrary( name, funcs );
}

static inline void trap_UnloadLibrary( void **lib ) {
	SOUND_IMPORT.Sys_UnloadLibrary( lib );
}

static inline void trap_Trace( struct trace_s *tr, vec3_t start, vec3_t end, vec3_t mins, vec3_t maxs, int mask ) {
	SOUND_IMPORT.Trace( tr, start, end, mins, maxs, mask );
}

static inline int trap_PointContents( vec3_t p ) {
	return SOUND_IMPORT.PointContents( p );
}

static inline int trap_PointLeafNum( const vec3_t p ) {
	return SOUND_IMPORT.PointLeafNum( p );
}

static inline bool trap_LeafsInPVS( int leafnum1, int leafnum2 ) {
	// The imported call does not perform this check.
	// And also, testing it here allows a faster cutoff avoiding a cross-library call.
	if( leafnum1 == leafnum2 ) {
		return true;
	}
	return SOUND_IMPORT.LeafsInPVS( leafnum1, leafnum2 );
}

static inline struct qthread_s *trap_Thread_Create( void *( *routine )( void* ), void *param ) {
	return SOUND_IMPORT.Thread_Create( routine, param );
}

static inline void trap_Thread_Join( struct qthread_s *thread ) {
	SOUND_IMPORT.Thread_Join( thread );
}

static inline void trap_Thread_Yield( void ) {
	SOUND_IMPORT.Thread_Yield();
}

static inline struct qmutex_s *trap_Mutex_Create( void ) {
	return SOUND_IMPORT.Mutex_Create();
}

static inline void trap_Mutex_Destroy( struct qmutex_s **mutex ) {
	SOUND_IMPORT.Mutex_Destroy( mutex );
}

static inline void trap_Mutex_Lock( struct qmutex_s *mutex ) {
	SOUND_IMPORT.Mutex_Lock( mutex );
}

static inline void trap_Mutex_Unlock( struct qmutex_s *mutex ) {
	SOUND_IMPORT.Mutex_Unlock( mutex );
}

static inline qbufPipe_t *trap_BufPipe_Create( size_t bufSize, int flags ) {
	return SOUND_IMPORT.BufPipe_Create( bufSize, flags );
}

static inline void trap_BufPipe_Destroy( qbufPipe_t **pqueue ) {
	SOUND_IMPORT.BufPipe_Destroy( pqueue );
}

static inline void trap_BufPipe_Finish( qbufPipe_t *queue ) {
	SOUND_IMPORT.BufPipe_Finish( queue );
}

static inline void trap_BufPipe_WriteCmd( qbufPipe_t *queue, const void *cmd, unsigned cmd_size ) {
	SOUND_IMPORT.BufPipe_WriteCmd( queue, cmd, cmd_size );
}

static inline int trap_BufPipe_ReadCmds( qbufPipe_t *queue, unsigned( **cmdHandlers )( const void * ) ) {
	return SOUND_IMPORT.BufPipe_ReadCmds( queue, cmdHandlers );
}

static inline void trap_BufPipe_Wait( qbufPipe_t *queue, int ( *read )( qbufPipe_t *, unsigned( ** )( const void * ), bool ),
									  unsigned( **cmdHandlers )( const void * ), unsigned timeout_msec ) {
	SOUND_IMPORT.BufPipe_Wait( queue, read, cmdHandlers, timeout_msec );
}

#ifdef __cplusplus
}
#endif