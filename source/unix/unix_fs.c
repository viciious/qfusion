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

#include "../qcommon/qcommon.h"

#include "../qcommon/sys_fs.h"

#define __USE_BSD

#include <dirent.h>

#ifdef __linux__
#include <linux/limits.h>
#endif

#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>

#ifdef __ANDROID__
#include "../android/android_sys.h"
#endif

// Mac OS X and FreeBSD don't know the readdir64 and dirent64
#if ( defined ( __FreeBSD__ ) || defined ( __ANDROID__ ) || !defined( _LARGEFILE64_SOURCE ) )
#define readdir64 readdir
#define dirent64 dirent
#endif

static char *findbase = NULL;
static size_t findbase_size = 0;
static const char *findpattern = NULL;
static char *findpath = NULL;
static size_t findpath_size = 0;
static DIR *fdir = NULL;
static int fdots = 0;

/*
* FS_DirentIsDir
*/
static bool FS_DirentIsDir( const struct dirent64 *d, const char *base ) {
#if ( defined( _DIRENT_HAVE_D_TYPE ) || defined( __ANDROID__ ) ) && defined( DT_DIR )
	return ( d->d_type == DT_DIR );
#else
	size_t pathSize;
	char *path;
	struct stat st;

	pathSize = strlen( base ) + 1 + strlen( d->d_name ) + 1;
	path = alloca( pathSize );
	Q_snprintfz( path, pathSize, "%s/%s", base, d->d_name );
	if( stat( path, &st ) ) {
		return false;
	}
	return S_ISDIR( st.st_mode ) != 0;
#endif
}

/*
* CompareAttributes
*/
static bool CompareAttributes( const struct dirent64 *d, const char *base, unsigned musthave, unsigned canthave ) {
	bool isDir;
	bool checkDir;

	assert( d );

	isDir = false;
	checkDir = ( canthave & SFF_SUBDIR ) || ( musthave & SFF_SUBDIR );
	if( checkDir ) {
		isDir = FS_DirentIsDir( d, base );
	}

	if( isDir && ( canthave & SFF_SUBDIR ) ) {
		return false;
	}
	if( ( musthave & SFF_SUBDIR ) && !isDir ) {
		return false;
	}

	return true;
}

/*
* CompareAttributesForPath
*/
static bool CompareAttributesForPath( const struct dirent64 *d, const char *path, unsigned musthave, unsigned canthave ) {
	return true;
}

/*
* Sys_FS_FindFirst
*/
const char *Sys_FS_FindFirst( const char *path, unsigned musthave, unsigned canhave ) {
	char *p;

	assert( path );
	assert( !fdir );
	assert( !findbase && !findpattern && !findpath && !findpath_size );

	if( fdir ) {
		Sys_Error( "Sys_BeginFind without close" );
	}

	findbase_size = strlen( path );
	assert( findbase_size );
	findbase_size += 1;

	findbase = Mem_TempMalloc( sizeof( char ) * findbase_size );
	Q_strncpyz( findbase, path, sizeof( char ) * findbase_size );

	if( ( p = strrchr( findbase, '/' ) ) ) {
		*p = 0;
		if( !strcmp( p + 1, "*.*" ) ) { // *.* to *
			*( p + 2 ) = 0;
		}
		findpattern = p + 1;
	} else {
		findpattern = "*";
	}

	if( !( fdir = opendir( findbase ) ) ) {
		return NULL;
	}

	fdots = 2; // . and ..
	return Sys_FS_FindNext( musthave, canhave );
}

/*
* Sys_FS_FindNext
*/
const char *Sys_FS_FindNext( unsigned musthave, unsigned canhave ) {
	struct dirent64 *d;

	assert( fdir );
	assert( findbase && findpattern );

	if( !fdir ) {
		return NULL;
	}

	while( ( d = readdir64( fdir ) ) != NULL ) {
		if( !CompareAttributes( d, findbase, musthave, canhave ) ) {
			continue;
		}

		if( fdots > 0 ) {
			// . and .. never match
			const char *base = COM_FileBase( d->d_name );
			if( !strcmp( base, "." ) || !strcmp( base, ".." ) ) {
				fdots--;
				continue;
			}
		}

		if( !*findpattern || Com_GlobMatch( findpattern, d->d_name, 0 ) ) {
			const char *dname = d->d_name;
			size_t dname_len = strlen( dname );
			size_t size = sizeof( char ) * ( findbase_size + dname_len + 1 + 1 );
			if( findpath_size < size ) {
				if( findpath ) {
					Mem_TempFree( findpath );
				}
				findpath_size = size * 2; // extra size to reduce reallocs
				findpath = Mem_TempMalloc( findpath_size );
			}

			Q_snprintfz( findpath, findpath_size, "%s/%s%s", findbase, dname,
						 dname[dname_len - 1] != '/' && FS_DirentIsDir( d, findbase ) ? "/" : "" );
			if( CompareAttributesForPath( d, findpath, musthave, canhave ) ) {
				return findpath;
			}
		}
	}

	return NULL;
}

void Sys_FS_FindClose( void ) {
	assert( findbase );

	if( fdir ) {
		closedir( fdir );
		fdir = NULL;
	}

	fdots = 0;

	Mem_TempFree( findbase );
	findbase = NULL;
	findbase_size = 0;
	findpattern = NULL;

	if( findpath ) {
		Mem_TempFree( findpath );
		findpath = NULL;
		findpath_size = 0;
	}
}

/*
* Sys_FS_GetHomeDirectory
*/
const char *Sys_FS_GetHomeDirectory( void ) {
	static char home[PATH_MAX] = { '\0' };

	if( home[0] == '\0' ) {
#ifndef __ANDROID__
		const char *homeEnv = getenv( "HOME" );
		const char *base = NULL, *local = "";

#ifdef __MACOSX__
		base = homeEnv;
		local = "Library/Application Support/";
#else
		base = getenv( "XDG_DATA_HOME" );
		local = "";
		if( !base ) {
			base = homeEnv;
			local = ".local/share/";
		}
#endif

		if( base ) {
#ifdef __MACOSX__
			Q_snprintfz( home, sizeof( home ), "%s/%s%s-%d.%d", base, local, APPLICATION,
						 APP_VERSION_MAJOR, APP_VERSION_MINOR );
#else
			Q_snprintfz( home, sizeof( home ), "%s/%s%c%s-%d.%d", base, local, tolower( *( (const char *)APPLICATION ) ),
						 ( (const char *)APPLICATION ) + 1, APP_VERSION_MAJOR, APP_VERSION_MINOR );
#endif
		}
#endif
	}

	if( home[0] == '\0' ) {
		return NULL;
	}
	return home;
}

/*
* Sys_FS_GetCacheDirectory
*/
const char *Sys_FS_GetCacheDirectory( void ) {
	static char cache[PATH_MAX] = { '\0' };

	if( cache[0] == '\0' ) {
#ifdef __ANDROID__
		Q_snprintfz( cache, sizeof( cache ), "%s/cache/%d.%d",
					 sys_android_internalDataPath, APP_VERSION_MAJOR, APP_VERSION_MINOR );
#else
		const char *homeEnv = getenv( "HOME" );
		const char *base = NULL, *local = "";

#ifdef __MACOSX__
		base = homeEnv;
		local = "Library/Caches/";
#else
		base = getenv( "XDG_CACHE_HOME" );
		local = "";
		if( !base ) {
			base = homeEnv;
			local = ".cache/";
		}
#endif

		if( base ) {
#ifdef __MACOSX__
			Q_snprintfz( cache, sizeof( cache ), "%s/%s%s-%d.%d", base, local, APPLICATION,
						 APP_VERSION_MAJOR, APP_VERSION_MINOR );
#else
			Q_snprintfz( cache, sizeof( cache ), "%s/%s%c%s-%d.%d", base, local, tolower( *( (const char *)APPLICATION ) ),
						 ( (const char *)APPLICATION ) + 1, APP_VERSION_MAJOR, APP_VERSION_MINOR );
#endif
		}
#endif
	}

	if( cache[0] == '\0' ) {
		return NULL;
	}
	return cache;
}

/*
* Sys_FS_GetSecureDirectory
*/
const char *Sys_FS_GetSecureDirectory( void ) {
#ifdef __ANDROID__
	static char dir[PATH_MAX] = { '\0' };
	if( !dir[0] ) {
		Q_snprintfz( dir, sizeof( dir ), "%s/%d.%d",
					 sys_android_app->activity->internalDataPath, APP_VERSION_MAJOR, APP_VERSION_MINOR );
	}
	return dir;
#else
	return NULL;
#endif
}

/*
* Sys_FS_GetMediaDirectory
*/
const char *Sys_FS_GetMediaDirectory( fs_mediatype_t type ) {
#ifdef __ANDROID__
	static char paths[FS_MEDIA_NUM_TYPES][PATH_MAX];
	static int pathsChecked;
	const char *publicDir;
	JNIEnv *env;
	jclass envClass;
	jmethodID getPublicDirectory;
	jstring js;
	jobject file;
	jclass fileClass;
	jmethodID getAbsolutePath;
	const char *pathUTF;

	if( paths[type][0] ) {
		return paths[type];
	}

	if( pathsChecked & ( 1 << type ) ) {
		return NULL;
	}

	switch( type ) {
		case FS_MEDIA_IMAGES:
			publicDir = "Pictures";
			break;
		default:
			return NULL;
	}

	pathsChecked |= 1 << type;

	env = Sys_Android_GetJNIEnv();

	envClass = ( *env )->FindClass( env, "android/os/Environment" );
	getPublicDirectory = ( *env )->GetStaticMethodID( env, envClass, "getExternalStoragePublicDirectory",
													  "(Ljava/lang/String;)Ljava/io/File;" );
	js = ( *env )->NewStringUTF( env, publicDir );
	file = ( *env )->CallStaticObjectMethod( env, envClass, getPublicDirectory, js );
	( *env )->DeleteLocalRef( env, js );
	( *env )->DeleteLocalRef( env, envClass );
	if( !file ) {
		return NULL;
	}

	fileClass = ( *env )->FindClass( env, "java/io/File" );
	getAbsolutePath = ( *env )->GetMethodID( env, fileClass, "getAbsolutePath", "()Ljava/lang/String;" );
	js = ( *env )->CallObjectMethod( env, file, getAbsolutePath );
	( *env )->DeleteLocalRef( env, file );
	( *env )->DeleteLocalRef( env, fileClass );

	pathUTF = ( *env )->GetStringUTFChars( env, js, NULL );
	Q_strncpyz( paths[type], pathUTF, sizeof( paths[0] ) );
	( *env )->ReleaseStringUTFChars( env, js, pathUTF );
	( *env )->DeleteLocalRef( env, js );

	return paths[type];

#else
	return NULL;
#endif
}

/*
* Sys_FS_GetRuntimeDirectory
*/
const char *Sys_FS_GetRuntimeDirectory( void ) {
	// disabled because some distributions mount /var/run with 'noexec' flag and consequently
	// game libs fail to load with 'failed to map segment from shared object' error
#if 0
	static char runtime[PATH_MAX] = { '\0' };

	if( runtime[0] == '\0' ) {
#ifndef __ANDROID__
#ifndef __MACOSX__
		const char *base = NULL, *local = "";

		base = getenv( "XDG_RUNTIME_DIR" );
		local = "";

		if( base ) {
			Q_snprintfz( runtime, sizeof( runtime ), "%s/%s%c%s-%d.%d", base, local, tolower( *( (const char *)APPLICATION ) ),
						 ( (const char *)APPLICATION ) + 1, APP_VERSION_MAJOR, APP_VERSION_MINOR );
		}
#endif
#endif
	}

	if( runtime[0] != '\0' ) {
		return runtime;
	}
#endif

	return NULL;
}

/*
* Sys_FS_LockFile
*/
void *Sys_FS_LockFile( const char *path ) {
	return (void *)1; // return non-NULL pointer
}

/*
* Sys_FS_UnlockFile
*/
void Sys_FS_UnlockFile( void *handle ) {
}

/*
* Sys_FS_CreateDirectory
*/
bool Sys_FS_CreateDirectory( const char *path ) {
	return ( !mkdir( path, 0777 ) );
}

/*
* Sys_FS_RemoveDirectory
*/
bool Sys_FS_RemoveDirectory( const char *path ) {
	return ( !rmdir( path ) );
}

ssize_t Sys_FS_GetRealPath( const char *path, char *buffer, size_t bufferSize ) {
	// If the buffer is a-priori capable of storing the path, use it
	if( bufferSize >= PATH_MAX ) {
		if( realpath( path, buffer ) ) {
			return strlen( buffer );
		}
		return -1;
	}

	// Let this call allocate its result as needed
	const char *resolved = realpath( buffer, NULL );
	if( !resolved ) {
		return -1;
	}

	// Check whether the result fits the buffer
	size_t len = strlen( resolved );
	if( len + 1 > bufferSize ) {
		free( resolved );
		return -1;
	}

	// Copy the result along with the last zero byte
	memcpy( buffer, resolved, len + 1 );
	free( resolved );
	return len;
}

/*
* Sys_FS_FileMTime
*/
time_t Sys_FS_FileMTime( const char *filename ) {
	struct stat buffer;
	int status;

	status = stat( filename, &buffer );
	if( status ) {
		return -1;
	}
	return buffer.st_mtime;
}

/*
* Sys_FS_FileNo
*/
int Sys_FS_FileNo( FILE *fp ) {
	return fileno( fp );
}

/*
* Sys_FS_MMapFile
*/
void *Sys_FS_MMapFile( int fileno, size_t size, size_t offset, void **mapping, size_t *mapping_offset ) {
	static unsigned offsetmask = 0;
	size_t offsetpad;

	if( !offsetmask ) {
		offsetmask = ~( sysconf( _SC_PAGESIZE ) - 1 );
	}
	offsetpad = offset - ( offset & offsetmask );

	void *data = mmap( NULL, size + offsetpad, PROT_READ, MAP_PRIVATE, fileno, offset - offsetpad );
	if( !data ) {
		return NULL;
	}

	*mapping = (void *)1;
	*mapping_offset = offsetpad;
	return data + offsetpad;
}

/*
* Sys_FS_UnMMapFile
*/
void Sys_FS_UnMMapFile( void *mapping, void *data, size_t size, size_t mapping_offset ) {
	if( !data ) {
		return;
	}
	munmap( (char *)data - mapping_offset, size + mapping_offset );
}

/*
* Sys_FS_AddFileToMedia
*/
void Sys_FS_AddFileToMedia( const char *filename ) {
#ifdef __ANDROID__
	ANativeActivity *activity = sys_android_app->activity;
	JNIEnv *env = Sys_Android_GetJNIEnv();
	char path[PATH_MAX];
	jobject file, uri, intent;

	if( !realpath( filename, path ) ) {
		return;
	}

	{
		jclass fileClass;
		jmethodID ctor;
		jstring pathname;

		fileClass = ( *env )->FindClass( env, "java/io/File" );
		ctor = ( *env )->GetMethodID( env, fileClass, "<init>", "(Ljava/lang/String;)V" );
		pathname = ( *env )->NewStringUTF( env, path );
		file = ( *env )->NewObject( env, fileClass, ctor, pathname );
		( *env )->DeleteLocalRef( env, pathname );
		( *env )->DeleteLocalRef( env, fileClass );
	}

	if( !file ) {
		return;
	}

	{
		jclass uriClass;
		jmethodID fromFile;

		uriClass = ( *env )->FindClass( env, "android/net/Uri" );
		fromFile = ( *env )->GetStaticMethodID( env, uriClass, "fromFile", "(Ljava/io/File;)Landroid/net/Uri;" );
		uri = ( *env )->CallStaticObjectMethod( env, uriClass, fromFile, file );
		( *env )->DeleteLocalRef( env, file );
		( *env )->DeleteLocalRef( env, uriClass );
	}

	if( !uri ) {
		return;
	}

	{
		jclass intentClass;
		jmethodID ctor, setData;
		jstring action;
		jobject intentRef;

		intentClass = ( *env )->FindClass( env, "android/content/Intent" );

		ctor = ( *env )->GetMethodID( env, intentClass, "<init>", "(Ljava/lang/String;)V" );
		action = ( *env )->NewStringUTF( env, "android.intent.action.MEDIA_SCANNER_SCAN_FILE" );
		intent = ( *env )->NewObject( env, intentClass, ctor, action );
		( *env )->DeleteLocalRef( env, action );

		setData = ( *env )->GetMethodID( env, intentClass, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;" );
		intentRef = ( *env )->CallObjectMethod( env, intent, setData, uri );
		( *env )->DeleteLocalRef( env, uri );
		( *env )->DeleteLocalRef( env, intentRef );

		( *env )->DeleteLocalRef( env, intentClass );
	}

	{
		jmethodID sendBroadcast;

		sendBroadcast = ( *env )->GetMethodID( env, sys_android_activityClass, "sendBroadcast", "(Landroid/content/Intent;)V" );
		( *env )->CallVoidMethod( env, activity->clazz, sendBroadcast, intent );
		( *env )->DeleteLocalRef( env, intent );
	}
#endif
}
