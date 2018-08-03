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

#include "qcommon.h"

#include "sys_fs.h"
#include "sys_threads.h"

#include "compression.h"
#include "wswcurl.h"
#include "../qalgo/md5.h"
#include "../qalgo/q_trie.h"

/*
=============================================================================

QUAKE FILESYSTEM

=============================================================================
*/

#define FS_MAX_PATH                 1024

#define FS_ZIP_BUFSIZE              0x00004000

#define FS_ZIP_BUFREADCOMMENT       0x00000400
#define FS_ZIP_SIZELOCALHEADER      0x0000001e
#define FS_ZIP_SIZECENTRALDIRITEM   0x0000002e

#define FS_ZIP_LOCALHEADERMAGIC     0x04034b50
#define FS_ZIP_CENTRALHEADERMAGIC   0x02014b50
#define FS_ZIP_ENDHEADERMAGIC       0x06054b50

#define FS_PAK_MANIFEST_FILE        "manifest.txt"

#define FZ_GZ_BUFSIZE               0x00020000

enum {
	FS_SEARCH_NONE = 0,
	FS_SEARCH_PAKS = 1 << 0,
	FS_SEARCH_DIRS = 1 << 1,
	FS_SEARCH_ALL = ( FS_SEARCH_PAKS | FS_SEARCH_DIRS )
};

typedef enum {
	FS_PURE_NONE        = 0,
	FS_PURE_IMPLICIT    = 1,
	FS_PURE_EXPLICIT    = 2
} fs_pure_t;

static const char *pak_extensions[] = { "pkwsw", "pk3", "pk2", NULL };

static const char *forbidden_gamedirs[] = {
	"docs",
	"libs",
	"browser",
	APPLICATION ".app",
	"downloads",
	NULL
};

typedef struct {
	unsigned char readBuffer[FS_ZIP_BUFSIZE]; // internal buffer for compressed data
	z_stream zstream;                       // zLib stream structure for inflate
	size_t compressedSize;
	size_t restReadCompressed;            // number of bytes to be decompressed
} zipEntry_t;

#define FS_PACKFILE_DEFLATED        1
#define FS_PACKFILE_COHERENT        2
#define FS_PACKFILE_DIRECTORY       4

#define FS_PACKFILE_NUM_THREADS     4     // including the main thread

typedef struct packfile_s {
	char *name;
	char *pakname;
	void *vfsHandle;            // handle to the pack in VFS
	unsigned flags;
	unsigned compressedSize;    // compressed size
	unsigned uncompressedSize;  // uncompressed size
	unsigned offset;            // relative offset of local header
	time_t mtime;               // latest modified time, if available
} packfile_t;

//
// in memory
//
typedef struct pack_s {
	char *filename;     // full path
	char *manifest;
	unsigned checksum;
	fs_pure_t pure;
	bool deferred_load;
	struct pack_s *deferred_pack;
	void *sysHandle;
	void *vfsHandle;
	int numFiles;
	packfile_t *files;
	char *fileNames;
	trie_t *trie;
} pack_t;

typedef struct filehandle_s {
	FILE *fstream;
	packfile_t *pakFile;
	void *vfsHandle;
	unsigned pakOffset;
	unsigned uncompressedSize;      // uncompressed size
	unsigned offset;                // current read/write pos
	zipEntry_t *zipEntry;
	gzFile gzstream;
	int gzlevel;

	wswcurl_req *streamHandle;
	bool streamDone;
	size_t streamOffset;
	fs_read_cb read_cb;
	fs_done_cb done_cb;
	void *customp;

	void *mapping;
	size_t mapping_size;
	size_t mapping_offset;

	struct filehandle_s *prev, *next;
} filehandle_t;

typedef struct searchpath_s {
	char *path;                     // set on both, packs and directories, won't include the pack name, just path
	pack_t *pack;
	struct searchpath_s *base;      // parent basepath
	struct searchpath_s *next;
} searchpath_t;

typedef struct {
	char *name;
	searchpath_t *searchPath;
} searchfile_t;

static searchfile_t *fs_searchfiles;
static int fs_numsearchfiles;
static int fs_cursearchfiles;

static cvar_t *fs_basepath;
static cvar_t *fs_cdpath;
static cvar_t *fs_usehomedir;
static cvar_t *fs_usedownloadsdir;
static cvar_t *fs_basegame;
static cvar_t *fs_game;

static searchpath_t *fs_basepaths = NULL;       // directories without gamedirs
static searchpath_t *fs_searchpaths = NULL;     // game search directories, plus paks
static qmutex_t *fs_searchpaths_mutex;

static searchpath_t *fs_base_searchpaths;       // same as above, but without extra gamedirs
static searchpath_t *fs_root_searchpath;        // base path directory
static searchpath_t *fs_write_searchpath;       // write directory
static searchpath_t *fs_downloads_searchpath;   // write directory for downloads from game servers

static mempool_t *fs_mempool;

#define FS_Malloc( size ) Mem_Alloc( fs_mempool, size )
#define FS_Realloc( data, size ) Mem_Realloc( data, size )
#define FS_Free( data ) Mem_Free( data )

#define FS_MAX_BLOCK_SIZE   0x10000
#define FS_MAX_HANDLES      1024

static filehandle_t fs_filehandles[FS_MAX_HANDLES];
static filehandle_t fs_filehandles_headnode, *fs_free_filehandles;
static qmutex_t *fs_fh_mutex;

static int fs_notifications = 0;

static int FS_AddNotifications( int bitmask );

static bool fs_initialized = false;

/*

All of Quake's data access is through a hierchal file system, but the contents of the file system
can be transparently merged from several sources.

*/

static inline unsigned int LittleLongRaw( const uint8_t *raw ) {
	return ( raw[3] << 24 ) | ( raw[2] << 16 ) | ( raw[1] << 8 ) | raw[0];
}

static inline unsigned short LittleShortRaw( const uint8_t *raw ) {
	return ( raw[1] << 8 ) | raw[0];
}

/*
* FS_ZipCheckFileCoherency
*
* Read the local header of the current zipfile
* Check the coherency of the local header and info in the end of central directory about this file
*/
static unsigned FS_ZipCheckFileCoherency( FILE *f, packfile_t *file ) {
	unsigned flags;
	unsigned char localHeader[31], compressed;

	if( fseek( f, Sys_VFS_FileOffset( file->vfsHandle ) + file->offset, SEEK_SET ) != 0 ) {
		return 0;
	}
	if( fread( localHeader, 1, sizeof( localHeader ), f ) != sizeof( localHeader ) ) {
		return 0;
	}

	// check the magic
	if( LittleLongRaw( &localHeader[0] ) != FS_ZIP_LOCALHEADERMAGIC ) {
		return 0;
	}
	compressed = LittleShortRaw( &localHeader[8] );
	if( ( compressed == Z_DEFLATED ) && !( file->flags & FS_PACKFILE_DEFLATED ) ) {
		return 0;
	} else if( !compressed && ( file->flags & FS_PACKFILE_DEFLATED ) ) {
		return 0;
	}

	flags = LittleShortRaw( &localHeader[6] ) & 8;
	if( ( LittleLongRaw( &localHeader[18] ) != file->compressedSize ) && !flags ) {
		return 0;
	}
	if( ( LittleLongRaw( &localHeader[22] ) != file->uncompressedSize ) && !flags ) {
		return 0;
	}

	return FS_ZIP_SIZELOCALHEADER + LittleShortRaw( &localHeader[26] ) + ( unsigned )LittleShortRaw( &localHeader[28] );
}

static int FS_SortStrings( const char **first, const char **second ) {
	return Q_stricmp( *first, *second );
}

/*
* FS_CopyString
*/
static char *FS_CopyString( const char *in ) {
	int size;
	char *out;

	size = sizeof( char ) * ( strlen( in ) + 1 );
	out = ( char* )FS_Malloc( size );
	Q_strncpyz( out, in, size );

	return out;
}

/*
* FS_ListFiles
*/
static char **FS_ListFiles( char *findname, int *numfiles, unsigned musthave, unsigned canthave ) {
	const char *s;
	int nfiles = 0;
	static char **list = NULL;

	s = Sys_FS_FindFirst( findname, musthave, canthave );
	while( s ) {
		if( COM_ValidateFilename( s ) ) {
			nfiles++;
		}
		s = Sys_FS_FindNext( musthave, canthave );
	}
	Sys_FS_FindClose();

	if( !nfiles ) {
		return NULL;
	}

	*numfiles = nfiles;
	nfiles++; // add space for a guard
	list = ( char** )Mem_ZoneMalloc( sizeof( char * ) * nfiles );

	s = Sys_FS_FindFirst( findname, musthave, canthave );
	nfiles = 0;
	while( s ) {
		if( !COM_ValidateFilename( s ) ) {
			continue;
		}

		list[nfiles] = ZoneCopyString( s );

#ifdef _WIN32
		Q_strlwr( list[nfiles] );
#endif
		nfiles++;
		s = Sys_FS_FindNext( musthave, canthave );
	}
	Sys_FS_FindClose();

	list[nfiles] = NULL;
	return list;
}

/*
* FS_VFSPathForFileName
*/
static bool FS_VFSPathForFileName( const searchpath_t *search, const char *fileName, char *vfsPath, size_t vfsPathSize ) {
	if( search == fs_root_searchpath ) {
		Q_strncpyz( vfsPath, fileName, vfsPathSize );
		return true;
	}

	if( search->base == fs_root_searchpath ) {
		const char *vfsGameDir;

		vfsGameDir = strrchr( search->path, '/' );
		if( vfsGameDir ) {
			vfsGameDir++;
		} else {
			vfsGameDir = search->path;
		}

		Q_snprintfz( vfsPath, vfsPathSize, "%s/%s", vfsGameDir, fileName );
		return true;
	}

	return false;
}

/*
* FS_SearchPakForFile
*/
static bool FS_SearchPakForFile( pack_t *pak, const char *filename, packfile_t **pout ) {
	packfile_t *pakFile = NULL;
	trie_error_t trie_error;

	assert( pak );
	assert( filename );

	trie_error = Trie_Find( pak->trie, filename, TRIE_EXACT_MATCH, ( void ** )&pakFile );
	if( pout ) {
		*pout = pakFile;
	}
	return trie_error == TRIE_OK ? true : false;
}

/*
* FS_PakFileLength
*/
#define FS_PakFileLength( pakFile ) ( ( pakFile )->uncompressedSize )

/*
* FS_SearchDirectoryForFile
*/
static bool FS_SearchDirectoryForFile( searchpath_t *search, const char *filename, char *path, size_t path_size, void **vfsHandle ) {
	FILE *f;
	char tempname[FS_MAX_PATH], vfstempname[FS_MAX_PATH];
	bool found = false;

	assert( search );
	assert( !search->pack );
	assert( filename );

	Q_snprintfz( tempname, sizeof( tempname ), "%s/%s", search->path, filename );

	f = fopen( tempname, "rb" );
	if( f ) {
		fclose( f );
		found = true;
	}

	if( !found && vfsHandle && FS_VFSPathForFileName( search, filename, vfstempname, sizeof( vfstempname ) ) ) {
		*vfsHandle = Sys_VFS_FindFile( vfstempname );
		if( *vfsHandle ) {
			found = true;
		}
	}

	if( found && path ) {
		Q_strncpyz( path, tempname, path_size );
	}

	return found;
}

/*
* FS_FileLength
*/
static int FS_FileLength( FILE *f, bool close ) {
	int pos, end;

	assert( f != NULL );
	if( !f ) {
		return -1;
	}

	pos = ftell( f );
	fseek( f, 0, SEEK_END );
	end = ftell( f );
	fseek( f, pos, SEEK_SET );

	if( close ) {
		fclose( f );
	}

	return end;
}

/*
* FS_SearchPathForFile
*
* Gives the searchpath element where this file exists, or NULL if it doesn't
*/
static searchpath_t *FS_SearchPathForFile( const char *filename, packfile_t **pout, char *path, size_t path_size, void **vfsHandle, int mode ) {
	searchpath_t *search;
	packfile_t *search_pak;
	searchpath_t *implicitpure;
	packfile_t *implicitpure_pak;
	bool purepass;
	searchpath_t *result;

	if( !COM_ValidateRelativeFilename( filename ) ) {
		return NULL;
	}

	if( pout ) {
		*pout = NULL;
	}
	if( path && path_size ) {
		path[0] = '\0';
	}

	result = NULL;
	purepass = true;
	implicitpure = NULL;
	implicitpure_pak = NULL;

	// search through the path, one element at a time
	QMutex_Lock( fs_searchpaths_mutex );
	search = fs_searchpaths;
	while( search ) {
		// is the element a pak file?
		if( search->pack ) {
			if( mode & FS_SEARCH_PAKS ) {
				if( ( search->pack->pure > FS_PURE_NONE ) == purepass ) {
					if( FS_SearchPakForFile( search->pack, filename, &search_pak ) ) {
						// if we find an explicitly pure pak, return immediately
						if( !purepass || search->pack->pure == FS_PURE_EXPLICIT ) {
							if( pout ) {
								*pout = search_pak;
							}
							result = search;
							goto return_result;
						}
						// otherwise store the pointer but keep searching for an explicit pak
						else if( implicitpure == NULL ) {
							implicitpure = search;
							implicitpure_pak = search_pak;
						}
					}
				}
			}
		} else {
			if( mode & FS_SEARCH_DIRS ) {
				if( !purepass ) {
					if( FS_SearchDirectoryForFile( search, filename, path, path_size, vfsHandle ) ) {
						result = search;
						goto return_result;
					}
				}
			}
		}

		if( !search->next && purepass ) {
			if( implicitpure ) {
				// return file from an implicitly pure pak
				if( pout ) {
					*pout = implicitpure_pak;
				}
				result = implicitpure;
				goto return_result;
			}
			search = fs_searchpaths;
			purepass = false;
		} else {
			search = search->next;
		}
	}

return_result:
	QMutex_Unlock( fs_searchpaths_mutex );
	return result;
}

/*
* FS_SearchPathForBaseFile
*
* Gives the searchpath element where this file exists, or NULL if it doesn't
*/
static searchpath_t *FS_SearchPathForBaseFile( const char *filename, char *path, size_t path_size, void **vfsHandle ) {
	searchpath_t *search;

	if( !COM_ValidateRelativeFilename( filename ) ) {
		return NULL;
	}

	if( path && path_size ) {
		path[0] = '\0';
	}

	// search through the path, one element at a time
	search = fs_basepaths;
	while( search ) {
		if( FS_SearchDirectoryForFile( search, filename, path, path_size, vfsHandle ) ) {
			return search;
		}

		search = search->next;
	}

	return NULL;
}

/*
* FS_PakNameForPath
*/
static const char *FS_PakNameForPath( pack_t *pack ) {
	const char *p;

	// only give the basename part
	p = pack->filename + strlen( pack->filename ) - 1;
	while( p != pack->filename && *p != '/' )
		p--;
	if( p != pack->filename ) {
		p--;
		while( p != pack->filename && *p != '/' )
			p--;
		if( p != pack->filename ) {
			p++;
		}
	}

	return p;
}

/*
* FS_PakNameForFile
*/
const char *FS_PakNameForFile( const char *filename ) {
	searchpath_t *search = FS_SearchPathForFile( filename, NULL, NULL, 0, NULL, FS_SEARCH_PAKS );

	if( !search || !search->pack ) {
		return NULL;
	}

	return FS_PakNameForPath( search->pack );
}

/*
* FS_VFSHandleForPakName
*
* Takes an explicit (not game tree related) path to a pak file.
*/
static void *FS_VFSHandleForPakName( const char *packfilename ) {
	// treat VFS similar to the root directory
	size_t rootPathLength;

	if( !fs_root_searchpath ) {
		return NULL;
	}

	rootPathLength = strlen( fs_root_searchpath->path );
	if( Q_strnicmp( packfilename, fs_root_searchpath->path, rootPathLength ) || packfilename[rootPathLength] != '/' ) {
		return NULL;
	}

	return Sys_VFS_FindFile( packfilename + rootPathLength + 1 );
}

/*
* Cmd_PakFile_f
*/
static void Cmd_PakFile_f( void ) {
	const char *s = FS_PakNameForFile( Cmd_Argv( 1 ) );

	if( !s ) {
		Com_Printf( "Pakfile: File is not loaded from pak file.\n" );
	} else {
		Com_Printf( "Pakfile: %s\n", s );
	}
}

/*
* FS_IsExplicitPurePak
*/
bool FS_IsExplicitPurePak( const char *pakname, bool *wrongver ) {
	bool pure;
	const char *begin;
	const char *pakbasename, *extension;
	size_t pakbasename_len, extension_len;

	pakbasename = COM_FileBase( pakname );
	pakbasename_len = strlen( pakbasename );
	extension = COM_FileExtension( pakbasename );
	extension_len = strlen( extension );

	// check for "pure" suffix
	pure = false;
	begin = pakbasename + pakbasename_len - strlen( "pure" ) - extension_len;
	if( begin < pakbasename ) {
		return false;
	}

	if( !Q_strnicmp( begin, "pure", strlen( "pure" ) ) ) {
		pure = true;
	}

	// check version match
	if( wrongver ) {
		begin = pakbasename + pakbasename_len - strlen( APP_VERSION_STR_MAJORMINOR "pure" ) - extension_len;
		*wrongver = begin < pakbasename || Q_strnicmp( begin,  APP_VERSION_STR_MAJORMINOR, strlen( APP_VERSION_STR_MAJORMINOR ) ) != 0;
	}

	return pure;
}

/*
* FS_GetExplicitPurePakList
*/
int FS_GetExplicitPurePakList( char ***paknames ) {
	searchpath_t *search;
	int numpaks, i, e;

	// count them
	numpaks = 0;

	QMutex_Lock( fs_searchpaths_mutex );

	for( e = 0; pak_extensions[e]; e++ ) {
		for( search = fs_searchpaths; search; search = search->next ) {
			if( !search->pack ) {
				continue;
			}
			if( search->pack->pure != FS_PURE_EXPLICIT ) {
				continue;
			}
			numpaks++;
		}
	}

	if( numpaks ) {
		*paknames = ( char** )Mem_ZoneMalloc( sizeof( char * ) * numpaks );

		i = 0;
		for( e = 0; pak_extensions[e]; e++ ) {
			for( search = fs_searchpaths; search; search = search->next ) {
				if( !search->pack ) {
					continue;
				}
				if( search->pack->pure != FS_PURE_EXPLICIT ) {
					continue;
				}

				assert( i < numpaks );
				( *paknames )[i] = ZoneCopyString( FS_PakNameForPath( search->pack ) );
				i++;
			}
		}
		assert( i == numpaks );
	}

	QMutex_Unlock( fs_searchpaths_mutex );

	return numpaks;
}

/*
* FS_OpenFileHandle
*/
static int FS_OpenFileHandle( void ) {
	filehandle_t *fh;

	QMutex_Lock( fs_fh_mutex );

	if( !fs_free_filehandles ) {
		QMutex_Unlock( fs_fh_mutex );
		Sys_Error( "FS_OpenFileHandle: no free file handles" );
	}

	fh = fs_free_filehandles;
	fs_free_filehandles = fh->next;

	// put the handle at the start of the list
	memset( fh, 0, sizeof( *fh ) );
	fh->prev = &fs_filehandles_headnode;
	fh->next = fs_filehandles_headnode.next;
	fh->next->prev = fh;
	fh->prev->next = fh;

	QMutex_Unlock( fs_fh_mutex );

	return ( fh - fs_filehandles ) + 1;
}

/*
* FS_FileHandleForNum
*/
static inline filehandle_t *FS_FileHandleForNum( int file ) {
	if( file < 1 || file > FS_MAX_HANDLES ) {
		Sys_Error( "FS_FileHandleForNum: bad handle: %i", file );
	}
	return &fs_filehandles[file - 1];
}

/*
* FS_FileNumForHandle
*/
static inline int FS_FileNumForHandle( filehandle_t *fh ) {
	int file;

	file = ( fh - fs_filehandles ) + 1;
	if( file < 1 || file > FS_MAX_HANDLES ) {
		Sys_Error( "FS_FileHandleForNum: bad handle: %i", file );
	}
	return file;
}

/*
* FS_CloseFileHandle
*/
static void FS_CloseFileHandle( filehandle_t *fh ) {
	QMutex_Lock( fs_fh_mutex );

	// remove from linked open list
	fh->prev->next = fh->next;
	fh->next->prev = fh->prev;

	// insert into linked free list
	fh->next = fs_free_filehandles;
	fs_free_filehandles = fh;

	QMutex_Unlock( fs_fh_mutex );
}

/*
* FS_FirstExtension
* Searches the paths for file matching with one of the extensions
* If found returns the extension otherwise NULL
* extensions parameter is string with extensions separated by spaces
*/
const char *FS_FirstExtension( const char *filename, const char *extensions[], int num_extensions ) {
	char **filenames;           // slots for testable filenames
	size_t filename_size;       // size of one slot
	int i;
	size_t max_extension_length;
	searchpath_t *search;
	bool purepass;
	const char *implicitpure;
	const char *result;

	assert( filename && extensions );

	if( !num_extensions ) {
		return NULL;
	}

#ifndef NDEBUG
	for( i = 0; i < num_extensions; i++ )
		assert( extensions[i] && extensions[i][0] );
#endif

	if( !COM_ValidateRelativeFilename( filename ) ) {
		return NULL;
	}

	max_extension_length = 0;
	for( i = 0; i < num_extensions; i++ ) {
		if( strlen( extensions[i] ) > max_extension_length ) {
			max_extension_length = strlen( extensions[i] );
		}
	}

	// set the filenames to be tested
	filenames = ( char** )alloca( sizeof( char * ) * num_extensions );
	filename_size = sizeof( char ) * ( strlen( filename ) + max_extension_length + 1 );

	for( i = 0; i < num_extensions; i++ ) {
		if( i ) {
			filenames[i] = ( char * )( ( uint8_t * )filenames[0] + filename_size * i );
		} else {
			filenames[i] = ( char* )alloca( filename_size * num_extensions );
		}
		Q_strncpyz( filenames[i], filename, filename_size );
		COM_ReplaceExtension( filenames[i], extensions[i], filename_size );
	}

	result = NULL;
	purepass = true;
	implicitpure = NULL;

	// search through the path, one element at a time
	QMutex_Lock( fs_searchpaths_mutex );
	search = fs_searchpaths;
	while( search ) {
		if( search->pack ) { // is the element a pak file?
			if( ( search->pack->pure > FS_PURE_NONE ) == purepass ) {
				for( i = 0; i < num_extensions; i++ ) {
					if( FS_SearchPakForFile( search->pack, filenames[i], NULL ) ) {
						if( !purepass || search->pack->pure == FS_PURE_EXPLICIT ) {
							result = extensions[i];
							goto return_result;
						} else if( implicitpure == NULL ) {
							implicitpure = extensions[i];
							break;
						}
					}
				}
			}
		} else {
			if( !purepass ) {
				for( i = 0; i < num_extensions; i++ ) {
					void *vfsHandle = NULL; // search in VFS as well
					if( FS_SearchDirectoryForFile( search, filenames[i], NULL, 0, &vfsHandle ) ) {
						result = extensions[i];
						goto return_result;
					}
				}
			}
		}

		if( !search->next && purepass ) {
			if( implicitpure ) {
				result = implicitpure;
				goto return_result;
			}
			search = fs_searchpaths;
			purepass = false;
		} else {
			search = search->next;
		}
	}

return_result:
	QMutex_Unlock( fs_searchpaths_mutex );

	return result;
}

/*
* FS_FileExists
*/
static int FS_FileExists( const char *filename, bool base, bool inVFS ) {
	searchpath_t *search;
	packfile_t *pakFile = NULL;
	void *vfsHandle = NULL;
	char tempname[FS_MAX_PATH];

	if( FS_IsUrl( filename ) ) {
		size_t rxSize;
		int filenum;

		rxSize = FS_FOpenFile( filename, &filenum, FS_READ );
		if( !filenum ) {
			return -1;
		}

		return rxSize;
	}

	if( base ) {
		search = FS_SearchPathForBaseFile( filename, tempname, sizeof( tempname ), inVFS ? &vfsHandle : NULL );
	} else {
		search = FS_SearchPathForFile( filename, &pakFile, tempname, sizeof( tempname ), inVFS ? &vfsHandle : NULL, FS_SEARCH_ALL );
	}

	if( !search ) {
		return -1;
	}

	if( pakFile ) {
		assert( !base );
		return pakFile->uncompressedSize;
	} else if( vfsHandle ) {
		return Sys_VFS_FileSize( vfsHandle );
	} else {
		assert( tempname[0] != '\0' );
		if( tempname[0] != '\0' ) {
			return FS_FileLength( fopen( tempname, "rb" ), true );
		}
	}

	return -1;
}

/*
* FS_AbsoluteFileExists
*/
static int FS_AbsoluteFileExists( const char *filename ) {
	FILE *f;

	if( !COM_ValidateFilename( filename ) ) {
		return -1;
	}

	f = fopen( filename, "rb" );
	if( !f ) {
		return -1;
	}

	return FS_FileLength( f, true );
}

/*
* FS_PakFileExists
*/
bool FS_PakFileExists( const char *packfilename ) {
	return FS_FileExists( packfilename, true, true ) != -1;
}

/*
* FS_FileModeStr
*/
static void FS_FileModeStr( int mode, char *modestr, size_t size ) {
	int rwa = mode & FS_RWA_MASK;
	Q_snprintfz( modestr, size, "%sb%s",
				 rwa == FS_WRITE ? "w" : ( rwa == FS_APPEND ? "a" : "r" ),
				 mode & FS_UPDATE ? "+" : "" );
}

/*
* FS_FOpenAbsoluteFile
*
* Same for absolute files, won't look inside paks.
*/
int FS_FOpenAbsoluteFile( const char *filename, int *filenum, int mode ) {
	FILE *f = NULL;
	gzFile gzf = NULL;
	filehandle_t *file;
	int end;
	bool gz;
	bool update;
	int realmode;
	char modestr[4] = { 0, 0, 0, 0 };

	// FS_NOSIZE bit tells that we're not interested in real size of the file
	// probably useful for streamed URLS

	realmode = mode;
	gz = mode & FS_GZ ? true : false;
	update = mode & FS_UPDATE ? true : false;
	mode = mode & FS_RWA_MASK;

	assert( filenum || mode == FS_READ );

	if( gz && update ) {
		return -1; // unsupported

	}
	if( filenum ) {
		*filenum = 0;
	}

	if( !filenum ) {
		if( mode == FS_READ ) {
			return FS_AbsoluteFileExists( filename );
		}
		return -1;
	}

	if( !COM_ValidateFilename( filename ) ) {
		return -1;
	}

	if( mode == FS_WRITE || mode == FS_APPEND ) {
		FS_CreateAbsolutePath( filename );
	}

	FS_FileModeStr( realmode, modestr, sizeof( modestr ) );

	if( gz ) {
		gzf = qgzopen( filename, modestr );
	} else {
		f = fopen( filename, modestr );
	}
	if( !f && !gzf ) {
		Com_DPrintf( "FS_FOpenAbsoluteFile: can't %s %s\n", ( mode == FS_READ ? "find" : "write to" ), filename );
		return -1;
	}

	end = ( mode == FS_WRITE || gz ? 0 : FS_FileLength( f, false ) );

	*filenum = FS_OpenFileHandle();
	file = &fs_filehandles[*filenum - 1];
	file->fstream = f;
	file->uncompressedSize = end;
	file->gzstream = gzf;
	file->gzlevel = Z_DEFAULT_COMPRESSION;

#if ZLIB_VER_MAJOR >= 1 && ZLIB_VER_MINOR >= 2 && ZLIB_VER_REVISION >= 4
	if( gzf ) {
		qgzbuffer( gzf, FZ_GZ_BUFSIZE );
	}
#endif

	return end;
}

/*
* FS_IsUrl
*/
bool FS_IsUrl( const char *url ) {
	if( !strncmp( url, "http://", 7 ) ) {
		return true;
	}
	if( !strncmp( url, "https://", 8 ) ) {
		return true;
	}
	return false;
}

/*
* FS_StreamDoneSimpleCb
*
* Callback for wswcurl
*/
static void FS_StreamDoneSimpleCb( wswcurl_req *req, int status, void *customp ) {
	filehandle_t *fh = (filehandle_t *)customp;
	fh->streamDone = true;
}

/*
* _FS_FOpenPakFile
*/
static int _FS_FOpenPakFile( packfile_t *pakFile, int *filenum ) {
	filehandle_t *file;

	*filenum = 0;

	if( !pakFile ) {
		return -1;
	}
	if( pakFile->flags & FS_PACKFILE_DIRECTORY ) {
		return -1;
	}

	*filenum = FS_OpenFileHandle();
	file = &fs_filehandles[*filenum - 1];
	file->fstream = fopen( pakFile->vfsHandle ? Sys_VFS_VFSName( pakFile->vfsHandle ) : pakFile->pakname, "rb" );
	if( !file->fstream ) {
		Com_Error( ERR_FATAL, "Error opening pak file: %s", pakFile->pakname );
	}
	file->uncompressedSize = pakFile->uncompressedSize;
	file->zipEntry = NULL;
	file->pakFile = pakFile;

	if( !( pakFile->flags & FS_PACKFILE_COHERENT ) ) {
		unsigned offset = FS_ZipCheckFileCoherency( file->fstream, pakFile );
		if( !offset ) {
			Com_DPrintf( "_FS_FOpenPakFile: can't get proper offset for %s\n", pakFile->name );
			return -1;
		}
		pakFile->offset += offset;
		pakFile->flags |= FS_PACKFILE_COHERENT;
	}
	file->pakOffset = Sys_VFS_FileOffset( pakFile->vfsHandle ) + pakFile->offset;

	if( pakFile->flags & FS_PACKFILE_DEFLATED ) {
		file->zipEntry = ( zipEntry_t* )Mem_Alloc( fs_mempool, sizeof( zipEntry_t ) );
		file->zipEntry->compressedSize = pakFile->compressedSize;
		file->zipEntry->restReadCompressed = pakFile->compressedSize;

		// windowBits is passed < 0 to tell that there is no zlib header.
		// Note that in this case inflate *requires* an extra "dummy" byte
		// after the compressed stream in order to complete decompression and
		// return Z_STREAM_END. We don't want absolutely Z_STREAM_END because we known the
		// size of both compressed and uncompressed data
		if( qzinflateInit2( &file->zipEntry->zstream, -MAX_WBITS ) != Z_OK ) {
			Com_DPrintf( "_FS_FOpenPakFile: can't inflate %s\n", pakFile->name );
			return -1;
		}
	}

	if( fseek( file->fstream, file->pakOffset, SEEK_SET ) != 0 ) {
		Com_DPrintf( "_FS_FOpenPakFile: can't inflate %s\n", pakFile->name );
		return -1;
	}

	return pakFile->uncompressedSize;
}

/*
* _FS_FOpenFile
*
* Finds the file in the search path. Returns filesize and an open handle
* Used for streaming data out of either a pak file or a separate file.
*/
static int _FS_FOpenFile( const char *filename, int *filenum, int mode, bool base ) {
	searchpath_t *search;
	filehandle_t *file;
	bool noSize;
	bool gz;
	bool update;
	bool secure;
	bool cache;
	packfile_t *pakFile = NULL;
	gzFile gzf = NULL;
	void *vfsHandle = NULL;
	int realmode;
	char tempname[FS_MAX_PATH];

	// FS_NOSIZE bit tells that we're not interested in real size of the file
	// probably useful for streamed URLS

	realmode = mode;
	gz = mode & FS_GZ ? true : false;
	noSize = mode & FS_NOSIZE ? true : false;
	update = mode & FS_UPDATE ? true : false;
	secure = mode & FS_SECURE ? true : false;
	cache = mode & FS_CACHE ? true : false;
	mode = mode & FS_RWA_MASK;

	assert( mode == FS_READ || mode == FS_WRITE || mode == FS_APPEND );
	assert( filenum || mode == FS_READ );

	if( filenum ) {
		*filenum = 0;
	}

	if( !filenum ) {
		if( mode == FS_READ ) {
			return FS_FileExists( filename, base, !gz );
		}
		return -1;
	}

	if( FS_IsUrl( filename ) ) {
		size_t rxSize, rxReceived;

		if( mode == FS_WRITE || mode == FS_APPEND ) {
			Com_DPrintf( "FS_OpenFile: Tried to open URL %s in write-mode!", filename );
			return -1;
		}

		*filenum = FS_OpenFileHandle();
		file = &fs_filehandles[*filenum - 1];
		file->read_cb = NULL;
		file->done_cb = NULL;
		file->streamDone = false;
		file->streamOffset = 0;
		file->customp = file;

		file->streamHandle = wswcurl_create( NULL, "%s", filename );

		if( !file->streamHandle ) {
			FS_FCloseFile( *filenum );
			*filenum = 0;
			return -1;
		}

		wswcurl_stream_callbacks( file->streamHandle, NULL, FS_StreamDoneSimpleCb, NULL, file );
		wswcurl_start( file->streamHandle );

		if( noSize ) {
			return 0;
		}

		// check the expected file size..
		rxSize = wswcurl_getsize( file->streamHandle, &rxReceived );

		// if the size is 0 and it's EOF, return an error
		if( rxSize == 0 && wswcurl_eof( file->streamHandle ) ) {
			FS_FCloseFile( *filenum );
			*filenum = 0;
			return -1;
		}
		return rxSize;
	}

	if( ( mode == FS_WRITE || mode == FS_APPEND ) || update || secure || cache ) {
		int end;
		char modestr[4] = { 0, 0, 0, 0 };
		FILE *f = NULL;
		const char *dir;

		dir = FS_WriteDirectory();
		if( secure ) {
			dir = FS_SecureDirectory();
		} else if( cache ) {
			dir = FS_CacheDirectory();
		}

		if( base ) {
			Q_snprintfz( tempname, sizeof( tempname ), "%s/%s", dir, filename );
		} else {
			Q_snprintfz( tempname, sizeof( tempname ), "%s/%s/%s", dir, FS_GameDirectory(), filename );
		}
		FS_CreateAbsolutePath( tempname );

		FS_FileModeStr( realmode, modestr, sizeof( modestr ) );

		if( gz ) {
			gzf = qgzopen( tempname, modestr );
		} else {
			f = fopen( tempname, modestr );
		}
		if( !f && !gzf ) {
			return -1;
		}

		end = 0;
		if( mode == FS_APPEND || mode == FS_READ || update ) {
			end = f ? FS_FileLength( f, false ) : 0;
		}

		*filenum = FS_OpenFileHandle();
		file = &fs_filehandles[*filenum - 1];
		file->fstream = f;
		file->uncompressedSize = end;
		file->gzstream = gzf;
		file->gzlevel = Z_DEFAULT_COMPRESSION;

#if ZLIB_VER_MAJOR >= 1 && ZLIB_VER_MINOR >= 2 && ZLIB_VER_REVISION >= 4
		if( gzf ) {
			qgzbuffer( gzf, FZ_GZ_BUFSIZE );
		}
#endif
		return end;
	}

	if( base ) {
		search = FS_SearchPathForBaseFile( filename, tempname, sizeof( tempname ), !gz ? &vfsHandle : NULL );
	} else {
		search = FS_SearchPathForFile( filename, &pakFile, tempname, sizeof( tempname ), !gz ? &vfsHandle : NULL, FS_SEARCH_ALL );
	}

	if( !search ) {
		goto notfound_dprint;
	}

	if( pakFile ) {
		int uncompressedSize;

		assert( !base );

		uncompressedSize = _FS_FOpenPakFile( pakFile, filenum );
		if( uncompressedSize < 0 ) {
			if( *filenum > 0 ) {
				FS_FCloseFile( *filenum );
			}
			goto error;
		}

		Com_DPrintf( "PackFile: %s : %s\n", search->pack->filename, filename );
		return uncompressedSize;
	} else if( vfsHandle ) {
		const char *vfsName;
		FILE *f;
		unsigned int vfsOffset;

		vfsName = Sys_VFS_VFSName( vfsHandle );
		f = fopen( vfsName, "rb" );
		if( !f ) {
			Com_Error( ERR_FATAL, "Error opening VFS file: %s", vfsName );
			goto error;
		}

		vfsOffset = Sys_VFS_FileOffset( vfsHandle );
		if( fseek( f, vfsOffset, SEEK_SET ) != 0 ) {
			Com_DPrintf( "FS_FOpen%sFile: can't seek %s\n", ( base ? "Base" : "" ), vfsName );
			fclose( f );
			goto error;
		}

		*filenum = FS_OpenFileHandle();
		file = &fs_filehandles[*filenum - 1];
		file->vfsHandle = vfsHandle;
		file->fstream = f;
		file->pakOffset = vfsOffset;
		file->uncompressedSize = Sys_VFS_FileSize( vfsHandle );

		Com_DPrintf( "FS_FOpen%sFile: VFS %s : %s\n", ( base ? "Base" : "" ), vfsName, tempname );
		return file->uncompressedSize;
	} else {
		int end;
		FILE *f;

		assert( tempname[0] != '\0' );
		if( tempname[0] == '\0' ) {
			goto error;
		}

		f = fopen( tempname, "rb" );
		end = FS_FileLength( f, gz );

		if( gz ) {
			f = NULL;
			gzf = qgzopen( tempname, "rb" );
			assert( gzf );
		}

		*filenum = FS_OpenFileHandle();
		file = &fs_filehandles[*filenum - 1];
		file->fstream = f;
		file->uncompressedSize = end;
		file->gzstream = gzf;
		file->gzlevel = Z_DEFAULT_COMPRESSION;

		Com_DPrintf( "FS_FOpen%sFile: %s\n", ( base ? "Base" : "" ), tempname );
		return end;
	}

notfound_dprint:
	Com_DPrintf( "FS_FOpen%sFile: can't find %s\n", ( base ? "Base" : "" ), filename );

error:
	*filenum = 0;
	return -1;
}

/*
* FS_FOpenFile
*
* Used for streaming data out of either a pak file or a separate file.
*/
int FS_FOpenFile( const char *filename, int *filenum, int mode ) {
	return _FS_FOpenFile( filename, filenum, mode, false );
}

/*
* FS_FOpenBaseFile
*
* Same for base files, won't look inside paks.
*/
int FS_FOpenBaseFile( const char *filename, int *filenum, int mode ) {
	return _FS_FOpenFile( filename, filenum, mode, true );
}

/*
* FS_FCloseFile
*/
void FS_FCloseFile( int file ) {
	filehandle_t *fh;

	if( !file ) {
		return; // return silently

	}
	fh = FS_FileHandleForNum( file );

	if( fh->zipEntry ) {
		qzinflateEnd( &fh->zipEntry->zstream );
		Mem_Free( fh->zipEntry );
		fh->zipEntry = NULL;
	}
	if( fh->fstream ) {
		fclose( fh->fstream );
		fh->fstream = NULL;
	}
	if( fh->streamHandle ) {
		if( fh->done_cb && !fh->streamDone ) {
			// premature closing of file, call done-callback
			// ch : FIXME proper solution for status-code for FS_ callbacks
			// as for curl, errors are negative values
			fh->streamDone = true;
			fh->done_cb( file, 0, fh->customp );
		}
		wswcurl_delete( fh->streamHandle );
		fh->streamHandle = NULL;
		fh->customp = NULL;
		fh->done_cb = NULL;
		fh->read_cb = NULL;
	}
	if( fh->gzstream ) {
		qgzclose( fh->gzstream );
		fh->gzstream = NULL;
	}

	FS_CloseFileHandle( fh );
}

/*
* FS_ReadStream
*/
static int FS_ReadStream( uint8_t *buf, size_t len, filehandle_t *fh ) {
	size_t numb;

	numb = wswcurl_read( fh->streamHandle, buf, len );
	if( numb < len ) {
		fh->streamDone = true;
	}

	return numb;
}

/*
* FS_ReadZipFile
*
* Properly handles partial reads, used by FS_Read and FS_Seek
*/
static int FS_ReadZipFile( uint8_t *buf, size_t len, filehandle_t *fh ) {
	zipEntry_t *zipEntry;
	int error, flush;
	size_t read, block;
	uLong totalOutBefore;

	zipEntry = fh->zipEntry;
	zipEntry->zstream.next_out = buf;
	zipEntry->zstream.avail_out = (uInt)len;

	totalOutBefore = zipEntry->zstream.total_out;
	flush = ( ( len == fh->uncompressedSize )
			  && ( zipEntry->restReadCompressed <= FS_ZIP_BUFSIZE ) && !zipEntry->zstream.avail_in ? Z_FINISH : Z_SYNC_FLUSH );

	do {
		// read in chunks but attempt to read the whole file first
		if( !zipEntry->zstream.avail_in && zipEntry->restReadCompressed ) {
			block = min( zipEntry->restReadCompressed, FS_ZIP_BUFSIZE );

			read = fread( zipEntry->readBuffer, 1, block, fh->fstream );

			if( read != block ) {
				Sys_Error( "FS_Read: can't read %i bytes", block );
			}

			zipEntry->restReadCompressed -= block;
			zipEntry->zstream.next_in = (Bytef *)zipEntry->readBuffer;
			zipEntry->zstream.avail_in = (uInt)block;
		}

		error = qzinflate( &zipEntry->zstream, flush );

		if( error == Z_STREAM_END ) {
			break;
		}
		if( error != Z_OK ) {
			Sys_Error( "FS_ReadZipFile: can't inflate file" );
		}
	} while( zipEntry->zstream.avail_out > 0 );

	return (int)( zipEntry->zstream.total_out - totalOutBefore );
}

/*
* FS_ReadFile
*
* Properly handles partial reads
*/
static int FS_ReadFile( uint8_t *buf, size_t len, filehandle_t *fh ) {
	return (int)fread( buf, 1, len, fh->fstream );
}

/*
* FS_Read
*
* Properly handles partial reads
*/
int FS_Read( void *buffer, size_t len, int file ) {
	filehandle_t *fh;
	int total;

	// read in chunks for progress bar
	if( !len || !buffer ) {
		return 0;
	}

	fh = FS_FileHandleForNum( file );

	if( fh->fstream && ( fh->pakFile || fh->vfsHandle ) && len + fh->offset > fh->uncompressedSize ) {
		len = fh->uncompressedSize - fh->offset;
		if( !len ) {
			return 0;
		}
	}

	if( fh->zipEntry ) {
		total = FS_ReadZipFile( ( uint8_t * )buffer, len, fh );
	} else if( fh->streamHandle ) {
		total = FS_ReadStream( (uint8_t *)buffer, len, fh );
	} else if( fh->gzstream ) {
		total = qgzread( fh->gzstream, buffer, len );
	} else if( fh->fstream ) {
		total = FS_ReadFile( ( uint8_t * )buffer, len, fh );
	} else {
		return 0;
	}

	if( total < 0 ) {
		return total;
	}

	fh->offset += (unsigned)total;
	return total;
}

/*
* FS_Print
*/
int FS_Print( int file, const char *msg ) {
	return ( msg ? FS_Write( msg, strlen( msg ), file ) : 0 );
}

/*
* FS_Printf
*/
int FS_Printf( int file, const char *format, ... ) {
	char msg[8192];
	size_t len;
	va_list argptr;

	va_start( argptr, format );
	if( ( len = Q_vsnprintfz( msg, sizeof( msg ), format, argptr ) ) >= sizeof( msg ) - 1 ) {
		msg[sizeof( msg ) - 1] = '\0';
		Com_Printf( "FS_Printf: Buffer overflow" );
	}
	va_end( argptr );

	return FS_Write( msg, len, file );
}

/*
* FS_Write
*
* Properly handles partial writes
*/
int FS_Write( const void *buffer, size_t len, int file ) {
	filehandle_t *fh;
	size_t written;
	uint8_t *buf;

	fh = FS_FileHandleForNum( file );
	if( fh->zipEntry ) {
		Sys_Error( "FS_Write: writing to compressed file" );
	}

	if( fh->gzstream ) {
		return qgzwrite( fh->gzstream, buffer, len );
	}

	if( !fh->fstream ) {
		return 0;
	}

	buf = ( uint8_t * )buffer;
	if( !buf ) {
		return 0;
	}

	written = fwrite( buf, 1, len, fh->fstream );
	if( written != len ) {
		Sys_Error( "FS_Write: can't write %i bytes", len );
	}

	fh->offset += written;

	return written;
}

/*
* FS_Tell
*/
int FS_Tell( int file ) {
	filehandle_t *fh;

	fh = FS_FileHandleForNum( file );

	if( fh->gzstream ) {
		return qgztell( fh->gzstream );
	}
	if( fh->streamHandle ) {
		return wswcurl_tell( fh->streamHandle );
	}
	return (int)fh->offset;
}

/*
* FS_Seek
*/
int FS_Seek( int file, int offset, int whence ) {
	filehandle_t *fh;
	zipEntry_t *zipEntry;
	int error, currentOffset;
	size_t remaining, block;
	uint8_t buf[FS_ZIP_BUFSIZE * 4];

	fh = FS_FileHandleForNum( file );

	if( fh->gzstream ) {
		return qgzseek( fh->gzstream, offset,
						whence == FS_SEEK_CUR ? SEEK_CUR :
						( whence == FS_SEEK_END ? SEEK_END :
						  ( whence == FS_SEEK_SET ? SEEK_SET : -1 ) ) );
	}

	if( fh->streamHandle ) {
		fh->uncompressedSize = wswcurl_getsize( fh->streamHandle, NULL );
	}

	currentOffset = (int)fh->offset;

	if( whence == FS_SEEK_CUR ) {
		offset += currentOffset;
	} else if( whence == FS_SEEK_END ) {
		offset += fh->uncompressedSize;
	} else if( whence != FS_SEEK_SET ) {
		return -1;
	}

	// clamp so we don't get out of bounds
	if( offset < 0 ) {
		return -1;
	}

	if( fh->streamHandle ) {
		size_t rxReceived, returned;
		wswcurl_req *newreq;
		char *url;

		if( offset == currentOffset ) {
			wswcurl_ignore_bytes( fh->streamHandle, 0 );
			return 0;
		}

		wswcurl_getsize( fh->streamHandle, &rxReceived );
		returned = wswcurl_tell( fh->streamHandle );
		if( (int)rxReceived < offset ) {
			return -1;
		} else if( offset >= (int)returned ) {
			wswcurl_ignore_bytes( fh->streamHandle, offset - returned );
			return 0;
		}

		// kill the current stream
		// start a new one with byte offset
		url = FS_CopyString( wswcurl_get_url( fh->streamHandle ) );

		newreq = wswcurl_create( NULL, "%s", url );
		if( !newreq ) {
			FS_Free( url );
			return -1;
		}

		wswcurl_delete( fh->streamHandle );
		fh->streamHandle = newreq;

		wswcurl_set_resume_from( newreq, offset );
		wswcurl_stream_callbacks( newreq, NULL, FS_StreamDoneSimpleCb, NULL, fh );
		wswcurl_start( newreq );

		FS_Free( url );

		fh->offset = offset;
		return 0;
	}

	if( offset == currentOffset ) {
		return 0;
	}

	if( !fh->fstream ) {
		return -1;
	}
	if( offset > (int)fh->uncompressedSize ) {
		return -1;
	}

	if( !fh->zipEntry ) {
		fh->offset = offset;
		return fseek( fh->fstream, fh->pakOffset + offset, SEEK_SET );
	}

	// compressed files, doh
	zipEntry = fh->zipEntry;

	if( offset > currentOffset ) {
		offset -= currentOffset;
	} else {
		if( fseek( fh->fstream, fh->pakOffset, SEEK_SET ) != 0 ) {
			return -1;
		}

		zipEntry->zstream.next_in = zipEntry->readBuffer;
		zipEntry->zstream.avail_in = 0;
		error = qzinflateReset( &zipEntry->zstream );
		if( error != Z_OK ) {
			Sys_Error( "FS_Seek: can't inflateReset file" );
		}

		fh->offset = 0;
		zipEntry->restReadCompressed = zipEntry->compressedSize;
	}

	remaining = offset;
	do {
		block = min( remaining, sizeof( buf ) );

		FS_ReadZipFile( buf, block, fh );

		remaining -= block;
	} while( remaining > 0 );

	fh->offset += offset;
	return 0;
}

/*
* FS_Eof
*/
int FS_Eof( int file ) {
	filehandle_t *fh;

	fh = FS_FileHandleForNum( file );
	if( fh->streamHandle ) {
		return wswcurl_eof( fh->streamHandle );
	}
	if( fh->zipEntry ) {
		return fh->zipEntry->restReadCompressed == 0;
	}
	if( fh->gzstream ) {
		return qgzeof( fh->gzstream );
	}
	if( fh->fstream ) {
		return ( fh->pakFile || fh->vfsHandle ) ? fh->offset >= fh->uncompressedSize : feof( fh->fstream );
	}
	return 1;
}

/*
* FS_FFlush
*/
int FS_Flush( int file ) {
	filehandle_t *fh;

	fh = FS_FileHandleForNum( file );
	if( fh->gzstream ) {
		return qgzflush( fh->gzstream, Z_FINISH );
	}
	if( !fh->fstream ) {
		return 0;
	}

	return fflush( fh->fstream );
}

/*
* FS_FileNo
*
* Returns the file handle that can be used in system calls.
* Optionally returns the offset of the data if the file is in a pack or the VFS.
*/
int FS_FileNo( int file, size_t *offset ) {
	filehandle_t *fh;

	if( offset ) {
		*offset = 0;
	}

	fh = FS_FileHandleForNum( file );
	if( fh->fstream && !fh->zipEntry && !fh->gzstream ) {
		if( offset ) {
			*offset = fh->pakOffset;
		}
		return Sys_FS_FileNo( fh->fstream );
	}

	return -1;
}

/*
* FS_SetCompressionLevel
*/
void FS_SetCompressionLevel( int file, int level ) {
	filehandle_t *fh = FS_FileHandleForNum( file );
	if( fh->gzstream ) {
		fh->gzlevel = level;
		qgzsetparams( fh->gzstream, level,  Z_DEFAULT_STRATEGY );
	}
}

/*
* FS_GetCompressionLevel
*/
int FS_GetCompressionLevel( int file ) {
	filehandle_t *fh = FS_FileHandleForNum( file );
	if( fh->gzstream ) {
		return fh->gzlevel;
	}
	return 0;
}

/*
* _FS_LoadFile
*/
static int _FS_LoadFile( int fhandle, unsigned int len, void **buffer, void *stack, size_t stackSize, const char *filename, int fileline ) {
	uint8_t *buf;

	if( !fhandle ) {
		if( buffer ) {
			*buffer = NULL;
		}
		return -1;
	}

	if( !buffer ) {
		FS_FCloseFile( fhandle );
		return len;
	}

	if( stack && ( stackSize > len ) ) {
		buf = ( uint8_t* )stack;
	} else {
		buf = ( uint8_t* )_Mem_AllocExt( tempMemPool, len + 1, 0, 0, 0, 0, filename, fileline );
	}
	buf[len] = 0;
	*buffer = buf;

	FS_Read( buf, len, fhandle );
	FS_FCloseFile( fhandle );

	return len;
}

/*
* FS_LoadFileExt
*
* Filename are relative to the quake search path
* a null buffer will just return the file length without loading
*/
int FS_LoadFileExt( const char *path, int flags, void **buffer, void *stack, size_t stackSize, const char *filename, int fileline ) {
	unsigned int len;
	int fhandle;

	// look for it in the filesystem or pack files
	len = FS_FOpenFile( path, &fhandle, FS_READ | flags );
	return _FS_LoadFile( fhandle, len, buffer, stack, stackSize, filename, fileline );
}

/*
* FS_LoadBaseFileExt
*
* a NULL buffer will just return the file length without loading
*/
int FS_LoadBaseFileExt( const char *path, int flags, void **buffer, void *stack, size_t stackSize, const char *filename, int fileline ) {
	unsigned int len;
	int fhandle;

	// look for it in the filesystem
	len = FS_FOpenBaseFile( path, &fhandle, FS_READ | flags );
	return _FS_LoadFile( fhandle, len, buffer, stack, stackSize, filename, fileline );
}

/*
* FS_MMapBaseFile
*/
void *FS_MMapBaseFile( int file, size_t size, size_t offset ) {
	void *data;
	filehandle_t *fh;

	if( !size ) {
		return NULL;
	}

	fh = FS_FileHandleForNum( file );
	if( !fh->fstream || fh->vfsHandle || fh->mapping ) {
		return NULL;
	}

	data = Sys_FS_MMapFile( Sys_FS_FileNo( fh->fstream ), size, offset, &fh->mapping, &fh->mapping_offset );
	fh->mapping_size = size;
	return data;
}

/*
* FS_UnMMapBaseFile
*/
void FS_UnMMapBaseFile( int file, void *data ) {
	filehandle_t *fh;

	fh = FS_FileHandleForNum( file );
	if( !fh->mapping ) {
		return;
	}

	Sys_FS_UnMMapFile( fh->mapping, data, fh->mapping_size, fh->mapping_offset );
	fh->mapping = NULL;
}

/*
* FS_FreeFile
*/
void FS_FreeFile( void *buffer ) {
	Mem_TempFree( buffer );
}

/*
* FS_FreeBaseFile
*/
void FS_FreeBaseFile( void *buffer ) {
	FS_FreeFile( buffer );
}

/*
* FS_ChecksumAbsoluteFile
*/
unsigned FS_ChecksumAbsoluteFile( const char *filename ) {
	uint8_t buffer[FS_MAX_BLOCK_SIZE];
	int left, length, filenum;
	md5_byte_t digest[16];
	md5_state_t state;

	Com_DPrintf( "Calculating checksum for file: %s\n", filename );

	md5_init( &state );

	left = FS_FOpenAbsoluteFile( filename, &filenum, FS_READ );
	if( left == -1 ) {
		return 0;
	}

	while( ( length = FS_Read( buffer, sizeof( buffer ), filenum ) ) ) {
		left -= length;
		md5_append( &state, (md5_byte_t *)buffer, length );
	}

	FS_FCloseFile( filenum );
	md5_finish( &state, digest );

	if( left != 0 ) {
		return 0;
	}

	return md5_reduce( digest );
}

/*
* FS_ChecksumZipFile
*/
static unsigned FS_ChecksumZipFile( const char *filename, int numPakFiles, int *checksums ) {
	md5_byte_t digest[16];
	md5_state_t state;
	int pakFileInd;

	Com_DPrintf( "Calculating checksum for file: %s\n", filename );

	md5_init( &state );

	for( pakFileInd = 0; pakFileInd < numPakFiles; pakFileInd++ ) {
		int fixedChecksum;

		// If we're on a big endian architecture, we must swap the checksums before appending them to the MD5 digest
		fixedChecksum = LittleLong( checksums[pakFileInd] );
		md5_append( &state, (md5_byte_t *)&fixedChecksum, sizeof( fixedChecksum ) );
	}

	md5_finish( &state, digest );

	return md5_reduce( digest );
}

/*
* FS_PakChecksum
*/
static unsigned FS_PakChecksum( const char *filename ) {
	int diff;
	searchpath_t *search;
	unsigned checksum = 0;

	QMutex_Lock( fs_searchpaths_mutex );

	for( search = fs_searchpaths; search; search = search->next ) {
		if( search->pack ) {
			// filename is a basename, so we only compare the end of the names
			diff = strlen( search->pack->filename ) - strlen( filename );
			if( diff >= 0 && !strcmp( search->pack->filename + diff, filename ) ) {
				checksum = search->pack->checksum;
				break;
			}
		}
	}

	QMutex_Unlock( fs_searchpaths_mutex );

	return checksum;
}

/*
* FS_ChecksumBaseFile
*
* ignorePakChecksum - if true, returns md5 digest of file contents as found on the filesystem
*                     otherwise, may return cached pak checksum
*/
unsigned FS_ChecksumBaseFile( const char *filename, bool ignorePakChecksum ) {
	const char *fullname;

	if( !ignorePakChecksum && FS_CheckPakExtension( filename ) ) {
		return FS_PakChecksum( filename );
	}

	fullname = FS_AbsoluteNameForBaseFile( filename );
	if( !fullname ) {
		return false;
	}

	return FS_ChecksumAbsoluteFile( fullname );
}

/*
* FS_AddPurePak
*/
bool FS_AddPurePak( unsigned checksum ) {
	searchpath_t *search;
	bool result = false;

	QMutex_Lock( fs_searchpaths_mutex );

	for( search = fs_searchpaths; search; search = search->next ) {
		if( search->pack && search->pack->checksum == checksum ) {
			if( search->pack->pure < FS_PURE_IMPLICIT ) {
				search->pack->pure = FS_PURE_IMPLICIT;
			}
			result = true;
			break;
		}
	}

	QMutex_Unlock( fs_searchpaths_mutex );

	return result;
}

/*
* FS_RemovePurePaks
*/
void FS_RemovePurePaks( void ) {
	searchpath_t *search;

	QMutex_Lock( fs_searchpaths_mutex );

	for( search = fs_searchpaths; search; search = search->next ) {
		if( search->pack && search->pack->pure == FS_PURE_IMPLICIT ) {
			search->pack->pure = FS_PURE_NONE;
		}
	}

	QMutex_Unlock( fs_searchpaths_mutex );
}

/*
* FS_IsPureFile
*/
bool FS_IsPureFile( const char *filename ) {
	searchpath_t *search = FS_SearchPathForFile( filename, NULL, NULL, 0, NULL, FS_SEARCH_PAKS );

	if( !search || !search->pack ) {
		return false;
	}

	return search->pack->pure;
}

/*
* FS_FileManifest
*/
const char *FS_FileManifest( const char *filename ) {
	searchpath_t *search = FS_SearchPathForFile( filename, NULL, NULL, 0, NULL, FS_SEARCH_PAKS );

	if( !search || !search->pack ) {
		return NULL;
	}

	return search->pack->manifest;
}

/*
* FS_RemoveAbsoluteFile
*/
bool FS_RemoveAbsoluteFile( const char *filename ) {
	if( !COM_ValidateFilename( filename ) ) {
		return false;
	}

	// ch : this should return false on error, true on success, c++'ify:
	// return ( !remove( filename ) );
	return ( remove( filename ) == 0 ? true : false );
}

/*
* _FS_RemoveFile
*/
static bool _FS_RemoveFile( const char *filename, bool base ) {
	const char *fullname;

	if( base ) {
		fullname = FS_AbsoluteNameForBaseFile( filename );
	} else {
		fullname = FS_AbsoluteNameForFile( filename );
	}

	if( !fullname ) {
		return false;
	}

	if( strncmp( fullname, FS_WriteDirectory(), strlen( FS_WriteDirectory() ) ) ) {
		return false;
	}

	return ( FS_RemoveAbsoluteFile( fullname ) );
}

/*
* FS_RemoveBaseFile
*/
bool FS_RemoveBaseFile( const char *filename ) {
	return _FS_RemoveFile( filename, true );
}

/*
* FS_RemoveFile
*/
bool FS_RemoveFile( const char *filename ) {
	return _FS_RemoveFile( filename, false );
}

/*
* _FS_CopyFile
*/
bool _FS_CopyFile( const char *src, const char *dst, bool base, bool absolute ) {
	int srcnum, dstnum, length, result, l;
	uint8_t buffer[FS_MAX_BLOCK_SIZE];

	length = _FS_FOpenFile( src, &srcnum, FS_READ, base );
	if( length == -1 ) {
		return false;
	}

	if( absolute ) {
		result = FS_FOpenAbsoluteFile( dst, &dstnum, FS_WRITE );
	} else {
		result = _FS_FOpenFile( dst, &dstnum, FS_WRITE, base );
	}
	if( result == -1 ) {
		FS_FCloseFile( srcnum );
		return false;
	}

	while( true ) {
		l = FS_Read( buffer, sizeof( buffer ), srcnum );
		if( !l ) {
			break;
		}
		FS_Write( buffer, l, dstnum );
		length -= l;
	}

	FS_FCloseFile( dstnum );
	FS_FCloseFile( srcnum );

	if( length != 0 ) {
		_FS_RemoveFile( dst, base );
		return false;
	}

	return true;
}

/*
* FS_CopyFile
*/
bool FS_CopyFile( const char *src, const char *dst ) {
	return _FS_CopyFile( src, dst, false, false );
}

/*
* FS_CopyBaseFile
*/
bool FS_CopyBaseFile( const char *src, const char *dst ) {
	return _FS_CopyFile( src, dst, true, false );
}

/*
* FS_ExtractFile
*/
bool FS_ExtractFile( const char *src, const char *dst ) {
	return _FS_CopyFile( src, dst, false, true );
}

/*
* _FS_MoveFile
*/
bool _FS_MoveFile( const char *src, const char *dst, bool base, const char *dir ) {
	char temp[FS_MAX_PATH];
	const char *fullname, *fulldestname;

	if( base ) {
		fullname = FS_AbsoluteNameForBaseFile( src );
	} else {
		fullname = FS_AbsoluteNameForFile( src );
	}

	if( !fullname ) {
		return false;
	}

	if( strncmp( fullname, dir, strlen( dir ) ) ) {
		return false;
	}

	if( !COM_ValidateRelativeFilename( dst ) ) {
		return false;
	}

	if( base ) {
		fulldestname = va_r( temp, sizeof( temp ), "%s/%s", dir, dst );
	} else {
		fulldestname = va_r( temp, sizeof( temp ), "%s/%s/%s", dir, FS_GameDirectory(), dst );
	}
	return rename( fullname, fulldestname ) == 0 ? true : false;
}

/*
* FS_MoveFile
*/
bool FS_MoveFile( const char *src, const char *dst ) {
	return _FS_MoveFile( src, dst, false, FS_WriteDirectory() );
}

/*
* FS_MoveBaseFile
*/
bool FS_MoveBaseFile( const char *src, const char *dst ) {
	return _FS_MoveFile( src, dst, true, FS_WriteDirectory() );
}

/*
* FS_MoveCacheFile
*/
bool FS_MoveCacheFile( const char *src, const char *dst ) {
	return _FS_MoveFile( src, dst, false, FS_CacheDirectory() );
}

/*
* _FS_FileMTime
*/
static time_t _FS_FileMTime( const char *filename, bool base ) {
	searchpath_t *search;
	packfile_t *pakFile = NULL;
	char tempname[FS_MAX_PATH];

	if( base ) {
		search = FS_SearchPathForBaseFile( filename, tempname, sizeof( tempname ), NULL );
	} else {
		search = FS_SearchPathForFile( filename, &pakFile, tempname, sizeof( tempname ), NULL, FS_SEARCH_ALL );
	}

	if( !search ) {
		return 0;
	}

	if( pakFile ) {
		assert( !base );
		return pakFile->mtime;
	} else {
		assert( tempname[0] != '\0' );
		return Sys_FS_FileMTime( tempname );
	}

	return -1;
}

/*
* FS_FileMTime
*/
time_t FS_FileMTime( const char *filename ) {
	return _FS_FileMTime( filename, false );
}

/*
* FS_BaseFileMTime
*/
time_t FS_BaseFileMTime( const char *filename ) {
	return _FS_FileMTime( filename, true );
}

/*
* FS_RemoveAbsoluteDirectory
*/
bool FS_RemoveAbsoluteDirectory( const char *dirname ) {
	if( !COM_ValidateFilename( dirname ) ) {
		return false;
	}

	return ( Sys_FS_RemoveDirectory( dirname ) );
}

/*
* FS_RemoveBaseDirectory
*/
bool FS_RemoveBaseDirectory( const char *dirname ) {
	char temp[FS_MAX_PATH];

	if( !COM_ValidateRelativeFilename( dirname ) ) {
		return false;
	}

	return ( FS_RemoveAbsoluteDirectory( va_r( temp, sizeof( temp ), "%s/%s", FS_WriteDirectory(), dirname ) ) );
}

/*
* FS_RemoveDirectory
*/
bool FS_RemoveDirectory( const char *dirname ) {
	char temp[FS_MAX_PATH];

	if( !COM_ValidateRelativeFilename( dirname ) ) {
		return false;
	}

	return ( FS_RemoveAbsoluteDirectory( va_r( temp, sizeof( temp ), "%s/%s/%s", FS_WriteDirectory(), FS_GameDirectory(), dirname ) ) );
}

ssize_t FS_GetRealPath( const char *path, char *buffer, size_t bufferSize ) {
	return Sys_FS_GetRealPath( path, buffer, bufferSize );
}

/*
* FS_ReadPackManifest
*/
static void FS_ReadPackManifest( pack_t *pack ) {
	int size;
	int file = 0;
	packfile_t *pakFile = NULL;

	if( !FS_SearchPakForFile( pack, FS_PAK_MANIFEST_FILE, &pakFile ) ) {
		return;
	}

	size = _FS_FOpenPakFile( pakFile, &file );
	if( ( size > -1 ) && file ) {
		pack->manifest = ( char* )FS_Malloc( size + 1 );

		// read the file into memory
		FS_Read( ( uint8_t * )pack->manifest, size, file );
		FS_FCloseFile( file );

		// compress (get rid of comments, etc)
		COM_Compress( pack->manifest );
	}
}

/*
* FS_ZipSearchCentralDir
*
* Locate the central directory of a zipfile (at the end, just before the global comment)
*/
static unsigned FS_ZipSearchCentralDir( FILE *fin, void *vfsHandle ) {
	unsigned fileSize, backRead;
	unsigned maxBack = 0xffff; // maximum size of global comment
	unsigned char buf[FS_ZIP_BUFREADCOMMENT + 4];
	unsigned vfsOffset = 0;

	if( vfsHandle ) {
		vfsOffset = Sys_VFS_FileOffset( vfsHandle );
		if( fseek( fin, vfsOffset + Sys_VFS_FileSize( vfsHandle ), SEEK_SET ) != 0 ) {
			return 0;
		}
	} else {
		if( fseek( fin, 0, SEEK_END ) != 0 ) {
			return 0;
		}
	}

	fileSize = ftell( fin ) - vfsOffset;
	if( maxBack > fileSize ) {
		maxBack = fileSize;
	}

	backRead = 4;
	while( backRead < maxBack ) {
		unsigned i, readSize, readPos;

		if( backRead + FS_ZIP_BUFREADCOMMENT > maxBack ) {
			backRead = maxBack;
		} else {
			backRead += FS_ZIP_BUFREADCOMMENT;
		}

		readPos = fileSize - backRead;
		readSize = min( FS_ZIP_BUFREADCOMMENT + 4, backRead );
		if( readSize < 4 ) {
			continue;
		}

		if( fseek( fin, vfsOffset + readPos, SEEK_SET ) != 0 ) {
			break;
		}
		if( fread( buf, 1, readSize, fin ) != readSize ) {
			break;
		}

		for( i = readSize - 3; i--; ) {
			// check the magic
			if( LittleLongRaw( buf + i ) == FS_ZIP_ENDHEADERMAGIC ) {
				return readPos + i;
			}
		}
	}

	return 0;
}

/*
* FS_DosTimeToUnixtime
*
* Converts DOS time to tm struct
*/
static time_t FS_DosTimeToUnixtime( unsigned dosDateTime ) {
	unsigned dosDate;
	struct tm ttm = { 0 };
	time_t time;

	dosDate = (unsigned)( dosDateTime >> 16 );

	ttm.tm_hour = ( dosDateTime & 0xF800 ) / 0x800;
	ttm.tm_min  = ( dosDateTime & 0x7E0 ) / 0x20;
	ttm.tm_sec  =  2 * ( dosDateTime & 0x1f );

	ttm.tm_mday = dosDate & 0x1f;
	ttm.tm_mon  = ( ( ( dosDate ) & 0x1E0 ) / 0x20 ) - 1;
	ttm.tm_year = ( ( dosDate & 0x0FE00 ) / 0x0200 ) + 1980 - 1900;

	time = mktime( &ttm );
	return time;
}

/*
* FS_ZipGetFileInfo
*
* Get Info about the current file in the zipfile, with internal only info
*/
static unsigned FS_ZipGetFileInfo( FILE *f, void *vfsHandle, unsigned pos, unsigned byteBeforeTheZipFile,
								   packfile_t *file, size_t *fileNameLen, int *crc ) {
	size_t sizeRead;
	unsigned dosDateTime;
	unsigned compressed;
	unsigned char infoHeader[46]; // we can't use a struct here because of packing

	if( fseek( f, Sys_VFS_FileOffset( vfsHandle ) + pos, SEEK_SET ) != 0 ) {
		return 0;
	}
	if( fread( infoHeader, 1, sizeof( infoHeader ), f ) != sizeof( infoHeader ) ) {
		return 0;
	}

	// check the magic
	if( LittleLongRaw( &infoHeader[0] ) != FS_ZIP_CENTRALHEADERMAGIC ) {
		return 0;
	}

	compressed = LittleShortRaw( &infoHeader[10] );
	if( compressed && ( compressed != Z_DEFLATED ) ) {
		return 0;
	}

	dosDateTime = LittleLongRaw( &infoHeader[12] );

	if( crc ) {
		*crc = LittleLongRaw( &infoHeader[16] );
	}
	if( file ) {
		if( compressed == Z_DEFLATED ) {
			file->flags |= FS_PACKFILE_DEFLATED;
		}
		file->compressedSize = LittleLongRaw( &infoHeader[20] );
		file->uncompressedSize = LittleLongRaw( &infoHeader[24] );
		file->offset = LittleLongRaw( &infoHeader[42] ) + byteBeforeTheZipFile;
		file->mtime = FS_DosTimeToUnixtime( dosDateTime );
	}

	sizeRead = ( size_t )LittleShortRaw( &infoHeader[28] );
	if( !sizeRead ) {
		return 0;
	}

	if( fileNameLen ) {
		*fileNameLen = sizeRead;
	}

	if( file ) {
		if( fread( file->name, 1, sizeRead, f ) != sizeRead ) {
			return 0;
		}

		*( file->name + sizeRead ) = 0;
		if( *( file->name + sizeRead - 1 ) == '/' ) {
			file->flags |= FS_PACKFILE_DIRECTORY;
		}
	}

	return FS_ZIP_SIZECENTRALDIRITEM + ( unsigned )LittleShortRaw( &infoHeader[28] ) +
		   ( unsigned )LittleShortRaw( &infoHeader[30] ) + ( unsigned )LittleShortRaw( &infoHeader[32] );
}

/*
* FS_LoadZipFile
*
* Takes an explicit (not game tree related) path to a pak file.
*
* Loads the header and directory, adding the files at the beginning
* of the list so they override previous pack files.
*/
static pack_t *FS_LoadZipFile( const char *packfilename, bool silent ) {
	int i;
	int *checksums = NULL;
	int numFiles;
	size_t namesLen, len;
	pack_t *pack = NULL;
	packfile_t *file;
	FILE *fin = NULL;
	char *names;
	unsigned char zipHeader[20]; // we can't use a struct here because of packing
	unsigned offset, centralPos, sizeCentralDir, offsetCentralDir, byteBeforeTheZipFile;
	bool modulepack;
	bool expectUncompressedFiles;
	int manifestFilesize;
	void *handle = NULL;
	void *vfsHandle = NULL;

	if( FS_AbsoluteFileExists( packfilename ) == -1 ) {
		vfsHandle = FS_VFSHandleForPakName( packfilename );
	}

	if( !vfsHandle ) {
		// lock the file for reading, but don't throw fatal error
		handle = Sys_FS_LockFile( packfilename );
		if( handle == NULL ) {
			if( !silent ) {
				Com_Printf( "Error locking a zip pak file: %s\n", packfilename );
			}
			goto error;
		}
	}

	fin = fopen( vfsHandle ? Sys_VFS_VFSName( vfsHandle ) : packfilename, "rb" );
	if( fin == NULL ) {
		if( !silent ) {
			Com_Printf( "Error opening a zip pak file: %s\n", packfilename );
		}
		goto error;
	}
	centralPos = FS_ZipSearchCentralDir( fin, vfsHandle );
	if( centralPos == 0 ) {
		if( !silent ) {
			Com_Printf( "No central directory found for a zip pak file: %s\n", packfilename );
		}
		goto error;
	}
	if( fseek( fin, Sys_VFS_FileOffset( vfsHandle ) + centralPos, SEEK_SET ) != 0 ) {
		if( !silent ) {
			Com_Printf( "Error seeking a zip pak file: %s\n", packfilename );
		}
		goto error;
	}
	if( fread( zipHeader, 1, sizeof( zipHeader ), fin ) != sizeof( zipHeader ) ) {
		if( !silent ) {
			Com_Printf( "Error reading a zip pak file: %s\n", packfilename );
		}
		goto error;
	}

	// total number of entries in the central dir on this disk
	numFiles = LittleShortRaw( &zipHeader[8] );
	if( !numFiles ) {
		if( !silent ) {
			Com_Printf( "%s is not a valid zip pak file\n", packfilename );
		}
		goto error;
	}
	if( LittleShortRaw( &zipHeader[10] ) != numFiles || LittleShortRaw( &zipHeader[6] ) != 0
		|| LittleShortRaw( &zipHeader[4] ) != 0 ) {
		if( !silent ) {
			Com_Printf( "%s is not a valid zip pak file\n", packfilename );
		}
		goto error;
	}

	// size of the central directory
	sizeCentralDir = LittleLongRaw( &zipHeader[12] );

	// offset of start of central directory with respect to the starting disk number
	offsetCentralDir = LittleLongRaw( &zipHeader[16] );
	if( centralPos < offsetCentralDir + sizeCentralDir ) {
		if( !silent ) {
			Com_Printf( "%s is not a valid zip pak file\n", packfilename );
		}
		goto error;
	}
	byteBeforeTheZipFile = centralPos - offsetCentralDir - sizeCentralDir;

	for( i = 0, namesLen = 0, centralPos = offsetCentralDir + byteBeforeTheZipFile; i < numFiles; i++, centralPos += offset ) {
		offset = FS_ZipGetFileInfo( fin, vfsHandle, centralPos, byteBeforeTheZipFile, NULL, &len, NULL );
		if( !offset ) {
			if( !silent ) {
				Com_Printf( "%s is not a valid zip pak file\n", packfilename );
			}
			goto error; // something wrong occured
		}
		namesLen += len + 1;
	}

	namesLen += 1; // add space for a guard

	pack = ( pack_t* )FS_Malloc( (int)( sizeof( pack_t ) + numFiles * sizeof( packfile_t ) + namesLen ) );
	pack->filename = FS_CopyString( packfilename );
	pack->files = ( packfile_t * )( ( uint8_t * )pack + sizeof( pack_t ) );
	pack->fileNames = names = ( char * )( ( uint8_t * )pack->files + numFiles * sizeof( packfile_t ) );
	pack->numFiles = numFiles;
	pack->sysHandle = handle;
	pack->vfsHandle = vfsHandle;
	pack->trie = NULL;
	pack->pure = FS_IsExplicitPurePak( packfilename, NULL ) ? FS_PURE_EXPLICIT : FS_PURE_NONE;

	Trie_Create( TRIE_CASE_INSENSITIVE, &pack->trie );

	// allocate temp memory for files' checksums
	checksums = ( int* )Mem_TempMallocExt( ( numFiles + 1 ) * sizeof( *checksums ), 0 );

	if( !Q_strnicmp( COM_FileBase( packfilename ), "modules", strlen( "modules" ) ) ) {
		modulepack = true;
	} else {
		modulepack = false;
	}

	// We can't check file compression at the first pass since reading pak file info
	// requires providing a buffer of arbitrary length for the file name.
	// This check should be very rarely triggered anyway.
	expectUncompressedFiles = Q_strrstr( packfilename, ".pkwsw" ) != NULL;

	manifestFilesize = -1;

	// add all files to the trie
	for( i = 0, file = pack->files, centralPos = offsetCentralDir + byteBeforeTheZipFile; i < numFiles; i++, file++, centralPos += offset, names += len + 1 ) {
		const char *ext;
		trie_error_t trie_err;
		packfile_t *trie_file;

		file->name = names;
		file->pakname = pack->filename;
		file->vfsHandle = vfsHandle;

		offset = FS_ZipGetFileInfo( fin, vfsHandle, centralPos, byteBeforeTheZipFile, file, &len, &checksums[i] );

		if( expectUncompressedFiles ) {
			if( file->flags & FS_PACKFILE_DEFLATED ) {
				if( !silent ) {
					const char *fmt = "%s contains a compressed file: %s, compression is disallowed for this kind of paks\n";
					Com_Printf( fmt, packfilename, file->name );
				}
				goto error;
			}
		}

		if( !COM_ValidateRelativeFilename( file->name ) ) {
			if( !silent ) {
				Com_Printf( "%s contains filename that's not allowed: %s\n", packfilename, file->name );
			}
			goto error;
		}

		ext = COM_FileExtension( file->name );
		// only module packs can include libraries
		if( !modulepack ) {
			if( ext && ( !Q_stricmp( ext, ".so" ) || !Q_stricmp( ext, ".dll" ) || !Q_stricmp( ext, ".dylib" ) ) ) {
				if( !silent ) {
					Com_Printf( "%s is not module pack, but includes module file: %s\n", packfilename, file->name );
				}
				goto error;
			}
		} else {
			if( !Q_stricmp( file->name, FS_PAK_MANIFEST_FILE ) && !( file->flags & FS_PACKFILE_DIRECTORY ) ) {
				manifestFilesize = file->uncompressedSize;
			}
		}

		trie_err = Trie_Replace( pack->trie, file->name, file, (void **)&trie_file );
		if( trie_err == TRIE_KEY_NOT_FOUND ) {
			Trie_Insert( pack->trie, file->name, file );
		}
	}

	fclose( fin );
	fin = NULL;

	checksums[numFiles] = 0x1234567; // add some pseudo-random stuff
	pack->checksum = FS_ChecksumZipFile( pack->filename, numFiles + 1, checksums );

	if( !pack->checksum ) {
		if( !silent ) {
			Com_Printf( "Couldn't generate checksum for a zip pak file: %s\n", packfilename );
		}
		goto error;
	}

	Mem_TempFree( checksums );

	// read manifest file if it's a module pak
	if( modulepack && manifestFilesize > 0 ) {
		FS_ReadPackManifest( pack );
	}

	if( !silent ) {
		Com_Printf( "Added a zip pak file %s (%i files)\n", pack->filename, pack->numFiles );
	}

	return pack;

error:
	if( fin ) {
		fclose( fin );
	}
	if( pack ) {
		if( pack->trie ) {
			Trie_Destroy( pack->trie );
		}
		if( pack->filename ) {
			FS_Free( pack->filename );
		}
		FS_Free( pack );
	}
	if( checksums ) {
		Mem_TempFree( checksums );
	}
	if( handle != NULL ) {
		Sys_FS_UnlockFile( handle );
	}

	return NULL;
}

/*
* FS_LoadPackFile
*/
static pack_t *FS_LoadPackFile( const char *packfilename, bool silent ) {
	const char *ext;
	char tempname[FS_MAX_PATH];

	Q_strncpyz( tempname, packfilename, sizeof( tempname ) );

	ext = COM_FileExtension( tempname );
	if( !ext ) {
		return NULL;
	}

	if( !Q_stricmp( ext, ".tmp" ) ) {
		COM_StripExtension( tempname );
		ext = COM_FileExtension( tempname );
		if( !ext ) {
			return NULL;
		}
	}

	if( !Q_stricmp( ext, ".pkwsw" ) || !Q_stricmp( ext, ".pk3" ) || !Q_stricmp( ext, ".pk2" ) ) {
		return FS_LoadZipFile( packfilename, silent );
	}
	return NULL;
}


/*
* FS_FindPackFilePos
*
* Find the right position for a newly added pak file
*/
static bool FS_FindPackFilePos( const char *filename, searchpath_t **psearch, searchpath_t **pprev, searchpath_t **pnext ) {
	const char *fullname;
	searchpath_t *search, *compare, *prev;
	size_t path_size;
	bool founddir;
	bool result = true;

	fullname = filename;
	path_size = sizeof( char ) * ( COM_FilePathLength( fullname ) + 1 );

	search = ( searchpath_t* )FS_Malloc( sizeof( searchpath_t ) );
	search->path = ( char* )FS_Malloc( path_size );
	Q_strncpyz( search->path, filename, path_size );

	if( psearch ) {
		*psearch = NULL;
	}
	if( pprev ) {
		*pprev = NULL;
	}
	if( pnext ) {
		*pnext = NULL;
	}

	QMutex_Lock( fs_searchpaths_mutex );

	prev = NULL;
	compare = fs_searchpaths;

	// find the right position
	founddir = false;
	while( compare ) {
		int cmp = 0;

		if( compare->pack ) {
			cmp = Q_stricmp( COM_FileBase( compare->pack->filename ), COM_FileBase( filename ) );
			if( !cmp ) {
				result = false;
				goto return_result;
			}
		}

		if( !Q_stricmp( compare->path, search->path ) ) {
			if( compare->pack && cmp < 0 ) {
				break;
			}
			if( !founddir ) {
				founddir = true;
			}
		} else if( founddir ) {
			break;
		}

		prev = compare;
		compare = compare->next;
	}

	if( psearch ) {
		*psearch = search;
	} else {
		Mem_Free( search );
	}

	if( pprev ) {
		*pprev = prev;
	}
	if( pnext ) {
		*pnext = compare;
	}

return_result:
	QMutex_Unlock( fs_searchpaths_mutex );

	return result;
}

/*
* FS_FreePakFile
*/
static void FS_FreePakFile( pack_t *pack ) {
	if( pack->sysHandle ) {
		Sys_FS_UnlockFile( pack->sysHandle );
	}
	Trie_Destroy( pack->trie );
	FS_Free( pack->filename );
	FS_Free( pack );
}

/*
* FS_IsPakValid
*/
bool FS_IsPakValid( const char *filename, unsigned *checksum ) {
	const char *fullname = FS_AbsoluteNameForBaseFile( filename );
	pack_t *pakfile;

	if( !fullname ) {
		return false;
	}

	pakfile = FS_LoadPackFile( fullname, true );
	if( pakfile ) { // unlock and free, we don't need this file anyway
		if( checksum ) {
			*checksum = pakfile->checksum;
		}
		FS_FreePakFile( pakfile );
		return true;
	}

	return false;
}

/*
* FS_CheckPakExtension
*/
bool FS_CheckPakExtension( const char *filename ) {
	int i;
	const char *ext;

	ext = COM_FileExtension( filename );
	if( !ext ) {
		return false;
	}
	ext++;

	for( i = 0; pak_extensions[i]; i++ ) {
		if( !Q_stricmp( ext, pak_extensions[i] )  ) {
			return true;
		}
	}

	return false;
}

/*
* FS_PatternMatchesPackfile
*/
static int FS_PatternMatchesPackfile( void *file, void *pattern ) {
	const char *cpattern = ( const char * )pattern;
	packfile_t *packfile = ( packfile_t * )file;

	assert( cpattern != NULL );
	assert( packfile != NULL );

	return Com_GlobMatch( cpattern, packfile->name, false );
}

/*
* FS_PathGetFileListExt
*/
static int FS_PathGetFileListExt( searchpath_t *search, const char *dir, const char *extension, searchfile_t *files, size_t size ) {
	int i;
	unsigned found;
	size_t dirlen, extlen;
	char tempname[FS_MAX_PATH];

	assert( search );
	assert( !dir || dir[strlen( dir ) - 1] != '/' );
	assert( files );
	assert( size );

	if( !search || ( dir && dir[strlen( dir ) - 1] == '/' ) || !files || !size ) {
		return 0;
	}

	found = 0;
	dirlen = 0;
	extlen = 0;

	if( dir ) {
		dirlen = strlen( dir );
	}

	if( extension /* && extension[0] != '/'*/ ) {
		extlen = strlen( extension );
	} else {
		extlen = strlen( "*.*" );
	}

	if( !search->pack ) {
		char vfstempname[FS_MAX_PATH];
		size_t pathlen;
		int numfiles = 0, numvfsfiles = 0, totalnumfiles;
		char **filenames, **vfsfilenames = NULL, *filepath;
		const char *filename;
		unsigned int musthave, canthave;

		musthave = 0;
		canthave = SFF_HIDDEN | SFF_SYSTEM;

		pathlen = strlen( search->path ) + 1;

		if( extension ) {
			if( extension[0] != '/' ) {
				canthave |= SFF_SUBDIR;
			} else {
				musthave |= SFF_SUBDIR;
			}
		}

		Q_snprintfz( tempname, sizeof( tempname ), "%s/%s%s*%s",
					 search->path, dir, dirlen ? "/" : "", ( extension && ( extension[0] != '/' ) ) ? extension : ".*" );
		filenames = FS_ListFiles( tempname, &numfiles, musthave, canthave );

		Q_snprintfz( tempname, sizeof( tempname ), "%s%s*%s",
					 dir, dirlen ? "/" : "", ( extension && ( extension[0] != '/' ) ) ? extension : "" );
		if( FS_VFSPathForFileName( search, tempname, vfstempname, sizeof( vfstempname ) ) ) {
			vfsfilenames = Sys_VFS_ListFiles( vfstempname, NULL, &numvfsfiles, !( musthave & SFF_SUBDIR ), !( canthave & SFF_SUBDIR ) );
		}

		totalnumfiles = numfiles + numvfsfiles; // not caring about duplicates because they will be removed later
		for( i = 0; i < totalnumfiles; i++ ) {
			if( i < numfiles ) {
				// real file
				filepath = filenames[i];
				filename = filepath + pathlen + ( dirlen ? dirlen + 1 : 0 );
			} else {
				// VFS file
				const char *p;

				filepath = vfsfilenames[i - numfiles];
				filename = filepath + ( dirlen ? dirlen + 1 : 0 );
				if( search->base ) {
					// skip game directory
					p = strchr( filename, '/' );
					if( p ) {
						filename = p + 1;
					}
				}

				// skip subdirectories
				p = strchr( filename, '/' );
				if( p && p[1] ) {
					Mem_ZoneFree( filepath );
					continue;
				}
			}

			if( found < size ) {
				size_t len = strlen( filename );

				if( ( musthave & SFF_SUBDIR ) ) {
					if( filename[len - 1] != '/' ) {
						files[found].name = ( char* )Mem_ZoneMalloc( len + 2 );
						strcpy( files[found].name, filename );
						files[found].name[len] = '/';
						files[found].name[len + 1] = 0;
					} else {
						files[found].name = ZoneCopyString( filename );
					}
				} else {
					if( extension && ( len <= extlen ) ) {
						Mem_ZoneFree( filepath );
						continue;
					}
					files[found].name = ZoneCopyString( filename );
				}
				files[found].searchPath = search;
				found++;
			}

			Mem_ZoneFree( filepath );
		}
		Mem_ZoneFree( filenames );
		Mem_ZoneFree( vfsfilenames );

		return found;
	} else {
		unsigned int t;
		struct trie_dump_s *trie_dump = NULL;
		trie_error_t trie_err;
		char *pattern;

		Q_snprintfz( tempname, sizeof( tempname ), "%s%s*%s",
					 dirlen ? dir : "",
					 dirlen ? "/" : "",
					 extension ? extension : "" );

		pattern = tempname;
		trie_err = Trie_DumpIf( search->pack->trie, dirlen ? dir : "", TRIE_DUMP_VALUES,
								FS_PatternMatchesPackfile, pattern, &trie_dump );

		if( trie_err == TRIE_OK ) {
			char *name;
			const char *p;
			packfile_t *pakfile;

			for( t = 0; t < trie_dump->size; t++ ) {
				pakfile = ( (packfile_t *) ( trie_dump->key_value_vector[t].value ) );
				name = dirlen ? pakfile->name + dirlen + 1 : pakfile->name;

				if( !name[0] ) {
					continue;
				}

				// ignore subdirectories
				p = strchr( name, '/' );
				if( p ) {
					if( *( p + 1 ) ) {
						continue;
					}
				}

				files[found].name = name;
				files[found].searchPath = search;
				if( ++found == size ) {
					break;
				}
			}
		}

		Trie_FreeDump( trie_dump );
	}

	return found;
}

#define FS_MIN_SEARCHFILES      0x400
#define FS_MAX_SEARCHFILES      0xFFFF          // cap
static int FS_SortFilesCmp( const searchfile_t *file1, const searchfile_t *file2 ) {
	return Q_stricmp( ( file1 )->name, ( file2 )->name );
}

/*
* FS_GetFileListExt_
*
* Directory names should not contain a trailing /
* Directory names, beginning with a '<' only return downloaded files.
* Directory names, beginning with a '>' only return stock/official files.
*/
static int FS_GetFileListExt_( const char *dir, const char *extension, char *buf, size_t *bufsize, int maxFiles, int start, int end ) {
	int i;
	int allfound = 0, found, limit;
	size_t len, alllen;
	searchpath_t *search;
	searchfile_t *files;
	static int maxFilesCache;
	static char dircache[MAX_QPATH], extcache[MAX_QPATH];
	bool useCache;
	bool onlyDownloads = false, skipDownloads = false;

	assert( !dir || dir[strlen( dir ) - 1] != '/' );

	if( dir && dir[strlen( dir ) - 1] == '/' ) {
		return 0;
	}

	if( fs_cursearchfiles ) {
		useCache = ( maxFilesCache == maxFiles ? true : false );
		if( useCache ) {
			useCache = dir ? ( !strcmp( dircache, dir ) ? true : false ) : ( dircache[0] == '\0' ? true : false );
			if( useCache ) {
				useCache = extension ? ( !strcmp( extcache, extension ) ? true : false ) : ( extcache[0] == '\0' ? true : false );
			}
		}
	} else {
		useCache = false;
	}

	maxFilesCache = maxFiles;
	if( dir ) {
		Q_strncpyz( dircache, dir, sizeof( dircache ) );
	} else {
		dircache[0] = '\0';
	}
	if( extension ) {
		Q_strncpyz( extcache, extension, sizeof( extcache ) );
	} else {
		extcache[0] = '\0';
	}

	if( dir[0] == '<' ) {
		onlyDownloads = true;
		dir++;
	} else if( dir[0] == '>' ) {
		skipDownloads = true;
		dir++;
	}

	files = fs_searchfiles;
	if( !useCache ) {
		QMutex_Lock( fs_searchpaths_mutex );

		search = fs_searchpaths;
		while( search ) {
			if( fs_downloads_searchpath ) {
				if( ( onlyDownloads && search->base != fs_downloads_searchpath ) ||
					( skipDownloads && search->base == fs_downloads_searchpath ) ) {
					search = search->next;
					continue;
				}
			}

			limit = maxFiles ? min( fs_numsearchfiles, maxFiles ) : fs_numsearchfiles;
			found = FS_PathGetFileListExt( search, dir, extension, files + allfound,
										   fs_numsearchfiles - allfound );

			if( allfound + found == fs_numsearchfiles ) {
				if( limit == maxFiles || fs_numsearchfiles == FS_MAX_SEARCHFILES ) {
					break; // we are done
				}
				fs_numsearchfiles *= 2;
				if( fs_numsearchfiles > FS_MAX_SEARCHFILES ) {
					fs_numsearchfiles = FS_MAX_SEARCHFILES;
				}
				fs_searchfiles = files = ( searchfile_t* )FS_Realloc( fs_searchfiles, sizeof( searchfile_t ) * fs_numsearchfiles );
				if( !search->pack ) {
					for( i = 0; i < found; i++ )
						Mem_ZoneFree( files[allfound + i].name );
				}
				continue;
			}

			allfound += found;
			search = search->next;
		}

		QMutex_Unlock( fs_searchpaths_mutex );

		qsort( files, allfound, sizeof( searchfile_t ), ( int ( * )( const void *, const void * ) )FS_SortFilesCmp );

		// remove all duplicates
		for( i = 1; i < allfound; ) {
			if( FS_SortFilesCmp( &files[i - 1], &files[i] ) ) {
				i++;
				continue;
			}

			if( !files[i - 1].searchPath->pack ) {
				Mem_ZoneFree( files[i - 1].name );
			}
			memmove( &files[i - 1], &files[i], ( allfound - i ) * sizeof( *files ) );
			allfound--;
		}
	}

	if( !useCache ) {
		fs_cursearchfiles = allfound;
	} else {
		allfound = fs_cursearchfiles;
	}

	if( start < 0 ) {
		start = 0;
	}
	if( !end ) {
		end = allfound;
	} else if( end > allfound ) {
		end = allfound;
	}

	if( bufsize ) {
		found = 0;

		if( buf ) {
			alllen = 0;
			for( i = start; i < end; i++ ) {
				len = strlen( files[i].name );
				if( *bufsize < len + 1 + alllen + 1 ) {
					break; // we are done
				}
				strcpy( buf + alllen, files[i].name );
				alllen += len + 1;
				found++;
			}
			buf[alllen] = '\0';
		} else {
			*bufsize = 0;
			for( i = start; i < end; found++, i++ )
				*bufsize += strlen( files[i].name ) + 1;
			*bufsize = *bufsize + 1;
		}

		return found;
	}

	return allfound;
}

/*
* FS_GetFileList
*/
int FS_GetFileListExt( const char *dir, const char *extension, char *buf, size_t *bufsize, int start, int end ) {
	//	return FS_GetFileListExt_( dir, extension, buf, bufsize, buf2, buf2size, 0, 0, 0 );		// 0 - no limit
	return FS_GetFileListExt_( dir, extension, buf, bufsize, FS_MAX_SEARCHFILES, start, end );
}

/*
* FS_GetFileList
*/
int FS_GetFileList( const char *dir, const char *extension, char *buf, size_t bufsize, int start, int end ) {
	//	return FS_GetFileListExt_( dir, extension, buf, &bufsize, 0, start, end );				// 0 - no limit
	return FS_GetFileListExt_( dir, extension, buf, &bufsize, FS_MAX_SEARCHFILES, start, end );
}

/*
* FS_GameDirectory
*
* Returns the current game directory, without the path
*/
const char *FS_GameDirectory( void ) {
	assert( fs_game && fs_game->string && fs_game->string[0] );
	return fs_game->string;
}

/*
* FS_BaseGameDirectory
*
* Returns the current base game directory, without the path
*/
const char *FS_BaseGameDirectory( void ) {
	assert( fs_basegame && fs_basegame->string && fs_basegame->string[0] );
	return fs_basegame->string;
}

/*
* FS_WriteDirectory
*
* Returns directory where we can write, no gamedir attached
*/
const char *FS_WriteDirectory( void ) {
	return fs_write_searchpath->path;
}

/*
* FS_CacheDirectory
*
* Returns directory where we can write cached files
*/
const char *FS_CacheDirectory( void ) {
	const char *dir = Sys_FS_GetCacheDirectory();
	return dir ? dir : FS_WriteDirectory();
}

/*
* FS_SecureDirectory
*
* Returns directory with higher security (for instance, not accessible for other apps)
*/
const char *FS_SecureDirectory( void ) {
	const char *dir = Sys_FS_GetSecureDirectory();
	return dir ? dir : FS_WriteDirectory();
}

/*
* FS_MediaDirectory
*
* Returns the external directory for media files
*/
const char *FS_MediaDirectory( fs_mediatype_t type ) {
	return Sys_FS_GetMediaDirectory( type );
}

/*
* FS_DownloadsDirectory
*
* Returns directory where we can store downloads to, no gamedir attached.
* Returns NULL if downloads are disabled.
*/
const char *FS_DownloadsDirectory( void ) {
	if( fs_downloads_searchpath ) {
		return fs_downloads_searchpath->path;
	}
	return NULL;
}

/*
* FS_RuntimeDirectory
*
* Returns directory where we can write non-essential runtime files to, no gamedir attached
*/
const char *FS_RuntimeDirectory( void ) {
	const char *dir = Sys_FS_GetRuntimeDirectory();
	return dir ? dir : FS_WriteDirectory();
}

/*
* FS_Path_f
*/
static void FS_Path_f( void ) {
	searchpath_t *s;

	Com_Printf( "Current search path:\n" );

	QMutex_Lock( fs_searchpaths_mutex );

	if( fs_searchpaths != fs_base_searchpaths ) {
		Com_Printf( "Mod files:\n" );
	}
	for( s = fs_searchpaths; s; s = s->next ) {
		if( s == fs_base_searchpaths ) {
			Com_Printf( "Base files:\n" );
		}
		if( s->pack ) {
			Com_Printf( "%s (%s%s%i files)\n", s->pack->filename,
						( s->pack->vfsHandle ? "in VFS, " : "" ), ( s->pack->pure ? "pure, " : "" ), s->pack->numFiles );
		} else {
			Com_Printf( "%s\n", s->path );
		}
	}

	QMutex_Unlock( fs_searchpaths_mutex );
}

/*
* FS_CreateAbsolutePath
*
* Creates any directories needed to store the given filename
*/
void FS_CreateAbsolutePath( const char *path ) {
	char *ofs;

	for( ofs = ( char * )path + 1; *ofs; ofs++ ) {
		if( *ofs == '/' ) {
			// create the directory
			*ofs = 0;
			Sys_FS_CreateDirectory( path );
			*ofs = '/';
		}
	}
}

/*
* FS_AbsoluteNameForFile
*
* Gives absolute name for a game file
* NULL if not found, or file is in pak
*/
const char *FS_AbsoluteNameForFile( const char *filename ) {
	static char absolutename[FS_MAX_PATH];
	searchpath_t *search = FS_SearchPathForFile( filename, NULL, NULL, 0, NULL, FS_SEARCH_DIRS );

	if( !search || search->pack ) {
		return NULL;
	}

	Q_snprintfz( absolutename, sizeof( absolutename ), "%s/%s", search->path, filename );
	return absolutename;
}

/*
* FS_AbsoluteNameForBaseFile
*
* Gives absolute name for a base file
* NULL if not found
*/
const char *FS_AbsoluteNameForBaseFile( const char *filename ) {
	static char absolutename[FS_MAX_PATH];
	searchpath_t *search = FS_SearchPathForBaseFile( filename, NULL, 0, NULL );

	if( !search ) {
		return NULL;
	}

	Q_snprintfz( absolutename, sizeof( absolutename ), "%s/%s", search->path, filename );
	return absolutename;
}

/*
* FS_BaseNameForFile
*/
const char *FS_BaseNameForFile( const char *filename ) {
	const char *p;
	void *vfsHandle = NULL; // search in VFS as well
	searchpath_t *search = FS_SearchPathForFile( filename, NULL, NULL, 0, &vfsHandle, FS_SEARCH_DIRS );

	if( !search || search->pack ) {
		return NULL;
	}

	// only give the basename part
	p = strrchr( search->path, '/' );

	if( !p ) {
		return va( "%s/%s", search->path, filename );
	}
	return va( "%s/%s", p + 1, filename );
}

/*
* FS_GetGameDirectoryList
*/
int FS_GetGameDirectoryList( char *buf, size_t bufsize ) {
	char **modnames;
	int i, j, length, nummods, nummods_total;
	size_t len, alllen;
	const char *basename, *s;
	searchpath_t *basepath;
	char temp[FS_MAX_PATH + 2];

	if( !buf ) {
		return 0;
	}

	nummods_total = 0;
	alllen = 0;
	buf[0] = '\0';

	basepath = fs_basepaths;
	while( basepath ) {
		if( ( modnames = FS_ListFiles( va_r( temp, sizeof( temp ), "%s/*", basepath->path ), &nummods, SFF_SUBDIR, SFF_HIDDEN | SFF_SYSTEM ) ) ) {
			for( i = 0; i < nummods; i++ ) {
				basename = COM_FileBase( modnames[i] );

				// forbidden directory?
				for( j = 0; forbidden_gamedirs[j]; j++ ) {
					if( !Q_stricmp( forbidden_gamedirs[j], basename ) ) {
						break;
					}
				}
				if( forbidden_gamedirs[j] ) {
					continue;
				}

				// already added?
				s = buf;
				for( j = 0; j < nummods_total; j++, s += length + 1 ) {
					length = strlen( s );
					if( !Q_stricmp( s, basename ) ) {
						break;
					}
				}
				if( j != nummods_total ) {
					continue;
				}

				// too little space?
				len = strlen( basename );
				if( bufsize <= len + alllen ) {
					break;
				}

				// ok, add it
				strcpy( buf + alllen, basename );
				alllen += len + 1;
				buf[alllen] = '\0';
				Mem_ZoneFree( modnames[i] );
				nummods_total++;
			}
			Mem_ZoneFree( modnames );
		}
		basepath = basepath->next;
	}

	return nummods_total;
}

/*
* FS_GamePathPaks
*/
static char **FS_GamePathPaks( searchpath_t *basepath, const char *gamedir, int *numpaks ) {
	int i, e, numpakfiles;
	char **paknames = NULL;
	char pattern[FS_MAX_PATH], basePattern[FS_MAX_PATH];

	numpakfiles = 0;
	for( e = 0; pak_extensions[e]; e++ ) {
		int numvfsfiles = 0, numfiles = 0;
		char **vfsfilenames = NULL, **filenames;

		Q_snprintfz( pattern, sizeof( pattern ), "%s/*.%s", gamedir, pak_extensions[e] );
		Q_snprintfz( basePattern, sizeof( basePattern ), "%s/%s", basepath->path, pattern );

		if( basepath == fs_root_searchpath ) { // only add VFS once per game, treat it like the installation directory
			vfsfilenames = Sys_VFS_ListFiles( pattern, basepath->path, &numvfsfiles, true, false );
		}
		filenames = FS_ListFiles( basePattern, &numfiles, 0, SFF_SUBDIR | SFF_HIDDEN | SFF_SYSTEM );
		if( vfsfilenames || filenames ) {
			if( numpakfiles || ( vfsfilenames && filenames ) ) {
				if( paknames ) {
					paknames = ( char** )Mem_Realloc( paknames, sizeof( *paknames ) * ( numpakfiles + numvfsfiles + numfiles + 1 ) );
				} else {
					paknames = ( char** )Mem_ZoneMalloc( sizeof( *paknames ) * ( numpakfiles + numvfsfiles + numfiles + 1 ) );
				}

				for( i = 0; i < numvfsfiles; i++ )
					paknames[numpakfiles + i] = vfsfilenames[i];
				for( i = 0; i < numfiles; i++ )
					paknames[numpakfiles + numvfsfiles + i] = filenames[i];
				paknames[numpakfiles + numvfsfiles + i] = NULL;

				numpakfiles += numvfsfiles + numfiles;

				if( filenames ) {
					Mem_Free( filenames );
				}
				if( vfsfilenames ) {
					Mem_Free( vfsfilenames );
				}
			} else if( vfsfilenames && !filenames ) {
				paknames = vfsfilenames;
				numpakfiles = numvfsfiles;
			} else {
				paknames = filenames;
				numpakfiles = numfiles;
			}
		}
	}

	if( numpakfiles != 0 ) {
		qsort( paknames, numpakfiles, sizeof( char * ), ( int ( * )( const void *, const void * ) )FS_SortStrings );

		for( i = 0; i < numpakfiles; ) {
			bool skip = false;
			bool wrongpure;

			// ignore similarly named paks if they appear in both vfs and fs
			skip = skip || ( i && !Q_stricmp( paknames[i], paknames[i - 1] ) );

			// ignore pure data and modules pak files from other versions
			skip = skip || ( FS_IsExplicitPurePak( paknames[i], &wrongpure ) && wrongpure );

			if( skip ) {
				Mem_Free( paknames[i] );
				memmove( &paknames[i], &paknames[i + 1], ( numpakfiles-- - i ) * sizeof( *paknames ) );
				continue;
			}

			i++;
		}
	}

	*numpaks = numpakfiles;
	return paknames;
}

/*
* FS_TouchGamePath
*/
static int FS_TouchGamePath( searchpath_t *basepath, const char *gamedir, bool initial ) {
	int i, totalpaks, newpaks;
	size_t path_size;
	searchpath_t *search, *prev, *next;
	pack_t *pak;
	char **paknames;

	Sys_VFS_TouchGamePath( gamedir, initial );

	QMutex_Lock( fs_searchpaths_mutex );

	// add directory to the list of search paths so pak files can stack properly
	if( initial ) {
		search = ( searchpath_t* )FS_Malloc( sizeof( searchpath_t ) );

		path_size = sizeof( char ) * ( strlen( basepath->path ) + 1 + strlen( gamedir ) + 1 );
		search->path = ( char* )FS_Malloc( path_size );
		search->base = basepath;
		Q_snprintfz( search->path, path_size, "%s/%s", basepath->path, gamedir );

		search->next = fs_searchpaths;
		fs_searchpaths = search;
	}

	newpaks = 0;
	totalpaks = 0;
	if( ( paknames = FS_GamePathPaks( basepath, gamedir, &totalpaks ) ) != 0 ) {
		for( i = 0; i < totalpaks; i++ ) {
			// ignore already loaded pak files if updating
			searchpath_t *compare = fs_searchpaths;
			while( compare ) {
				if( compare->pack ) {
					int cmp = Q_stricmp( COM_FileBase( compare->pack->filename ), COM_FileBase( paknames[i] ) );
					if( !cmp ) {
						if( !Q_stricmp( compare->pack->filename, paknames[i] ) ) {
							goto freename;
						}
					}
				}
				compare = compare->next;
			}

			if( !FS_FindPackFilePos( paknames[i], NULL, NULL, NULL ) ) {
				// well, we couldn't find a suitable position for this pak file, probably because
				// it's going to be overriden by a similarly named file elsewhere
				continue;
			}

			// deferred loading
			pak = ( pack_t* )FS_Malloc( sizeof( *pak ) );
			pak->filename = FS_CopyString( paknames[i] );
			pak->deferred_pack = NULL;
			pak->deferred_load = true;

			// now insert it for real
			if( FS_FindPackFilePos( paknames[i], &search, &prev, &next ) ) {
				search->base = basepath;
				search->pack = pak;
				if( !prev ) {
					search->next = fs_searchpaths;
					fs_searchpaths = search;
				} else {
					prev->next = search;
					search->next = next;
				}
				newpaks++;
			}
freename:
			Mem_ZoneFree( paknames[i] );
		}
		Mem_ZoneFree( paknames );
	}

	QMutex_Unlock( fs_searchpaths_mutex );

	return newpaks;
}

/*
* FS_AddGamePath
*/
static int FS_AddGamePath( searchpath_t *basepath, const char *gamedir ) {
	return FS_TouchGamePath( basepath, gamedir, true );
}

/*
* FS_UpdateGamePath
*/
static int FS_UpdateGamePath( searchpath_t *basepath, const char *gamedir ) {
	return FS_TouchGamePath( basepath, gamedir, false );
}

/*
* FS_ReplaceDeferredPaks
*/
static void FS_ReplaceDeferredPaks( void ) {
	searchpath_t *search;
	searchpath_t *prev;

	QMutex_Lock( fs_searchpaths_mutex );

	// scan for deferred paks with matching shard id
	prev = NULL;
	for( search = fs_searchpaths; search != NULL; ) {
		pack_t *pak = search->pack;

		if( pak && pak->deferred_load ) {
			if( !pak->deferred_pack ) {
				// failed to load this one, remove
				if( prev ) {
					prev->next = search->next;
				}
				FS_FreePakFile( pak );
				FS_Free( search );
				search = prev;
			} else {
				// update prev pointers
				search->pack = pak->deferred_pack;
				FS_FreePakFile( pak );
			}
		}
		prev = search;
		search = search->next;
	}

	QMutex_Unlock( fs_searchpaths_mutex );
}

/*
* FS_LoadDeferredPaks_Job
*/
typedef struct {
	volatile int *cnt;
	int maxcnt;
	pack_t **packs;
	qmutex_t *mutex;
} deferred_pack_arg_t;

static void *FS_LoadDeferredPaks_Job( void *parg ) {
	int i;
	pack_t *pack;
	deferred_pack_arg_t *arg = parg;

	while( true ) {
		i = Sys_Atomic_Add( arg->cnt, 1, arg->mutex );
		if( i >= arg->maxcnt ) {
			break;
		}

		pack = arg->packs[i];

		assert( pack != NULL );
		assert( pack->deferred_load );

		pack->deferred_pack = FS_LoadPackFile( pack->filename, false );
	}

	return NULL;
}

/*
* FS_LoadDeferredPaks
*/
static void FS_LoadDeferredPaks( int newpaks ) {
	int i;
	volatile int cnt;
	qthread_t *threads[FS_PACKFILE_NUM_THREADS - 1] = { NULL };
	const int num_threads = min( newpaks, FS_PACKFILE_NUM_THREADS ) - 1;
	pack_t **packs;
	searchpath_t *search;
	deferred_pack_arg_t *arg;

	if( !newpaks ) {
		return;
	}

	packs = Mem_TempMalloc( sizeof( *packs ) * ( newpaks + 1 ) );
	if( !packs ) {
		return;
	}

	cnt = 0;
	for( search = fs_searchpaths; search != NULL; search = search->next ) {
		if( search->pack && search->pack->deferred_load ) {
			packs[cnt++] = search->pack;
			if( cnt == newpaks ) {
				break;
			}
		}
	}

	arg = Mem_TempMalloc( sizeof( *arg ) );
	arg->cnt = Mem_TempMalloc( sizeof( int ) );
	arg->maxcnt = newpaks;
	arg->packs = packs;
	arg->mutex = QMutex_Create();

	if( num_threads > 0 ) {
		for( i = 0; i < num_threads; i++ )
			threads[i] = QThread_Create( FS_LoadDeferredPaks_Job, arg );
	}

	FS_LoadDeferredPaks_Job( arg );

	if( num_threads > 0 ) {
		for( i = 0; i < num_threads; i++ )
			QThread_Join( threads[i] );
	}

	FS_ReplaceDeferredPaks();

	Mem_TempFree( (void *)arg->cnt );
	Mem_TempFree( arg->packs );
	QMutex_Destroy( &arg->mutex );
	Mem_TempFree( arg );
}

/*
* FS_RemoveExtraPaks
*/
static void FS_RemoveExtraPaks( searchpath_t *old ) {
	searchpath_t *compare, *search, *prev;

	QMutex_Lock( fs_searchpaths_mutex );

	// scan for many paks with same name, but different base directory, and remove extra ones
	compare = fs_searchpaths;
	while( compare && compare != old ) {
		if( compare->pack ) {
			prev = compare;
			search = compare->next;
			while( search && search != old ) {
				if( search->pack &&
					!strcmp( COM_FileBase( search->pack->filename ), COM_FileBase( compare->pack->filename ) ) ) {
					Com_Printf( "Removed a duplicate zip pak file %s\n", search->pack->filename );
					prev->next = search->next;
					FS_FreePakFile( search->pack );
					FS_Free( search );
					search = prev;
				}

				prev = search;
				search = search->next;
			}
		}
		compare = compare->next;
	}

	QMutex_Unlock( fs_searchpaths_mutex );
}

/*
* FS_TouchGameDirectory
*/
static int FS_TouchGameDirectory( const char *gamedir, bool initial ) {
	int newpaks;
	searchpath_t *old, *prev, *basepath;

	// add for every basepath, in reverse order
	QMutex_Lock( fs_searchpaths_mutex );

	old = fs_searchpaths;
	prev = NULL;
	newpaks = 0;
	while( prev != fs_basepaths ) {
		basepath = fs_basepaths;
		while( basepath->next != prev )
			basepath = basepath->next;
		if( initial ) {
			newpaks += FS_AddGamePath( basepath, gamedir );
		} else {
			newpaks += FS_UpdateGamePath( basepath, gamedir );
		}
		prev = basepath;
	}

	// possibly spawn a few threads to load deferred packs in parallel
	if( newpaks ) {
		FS_LoadDeferredPaks( newpaks );
	}

	// FIXME: remove the initial check?
	// not sure whether removing pak files on the fly is such a good idea
	if( initial && newpaks ) {
		FS_RemoveExtraPaks( old );
	}

	QMutex_Unlock( fs_searchpaths_mutex );

	return newpaks;
}

/*
* FS_AddGameDirectory
*/
static int FS_AddGameDirectory( const char *gamedir ) {
	return FS_TouchGameDirectory( gamedir, true );
}

/*
* FS_UpdateGameDirectory
*/
static int FS_UpdateGameDirectory( const char *gamedir ) {
	return FS_TouchGameDirectory( gamedir, false );
}

/*
* FS_SetGameDirectory
*
* Sets the gamedir and path to a different directory.
*/
bool FS_SetGameDirectory( const char *dir, bool force ) {
	int i;
	searchpath_t *next;

	if( !force && Com_ClientState() >= CA_CONNECTED && !Com_DemoPlaying() ) {
		Com_Printf( "Can't change game directory while connected\n" );
		return false;
	}

	Com_Printf( "Changing game directory to: %s\n", dir );

	if( !COM_ValidateRelativeFilename( dir ) ) {
		Com_Printf( "Invalid name.\n" );
		return false;
	}

	if( strchr( dir, '/' ) ) {
		Com_Printf( "Game directory must be a single filename, not a path\n" );
		return false;
	}

	for( i = 0; forbidden_gamedirs[i]; i++ ) {
		if( !Q_stricmp( forbidden_gamedirs[i], dir ) ) {
			Com_Printf( "Forbidden game directory\n" );
			return false;
		}
	}

	if( fs_initialized && ( !dedicated || !dedicated->integer ) ) {
		Cmd_ExecuteString( "writeconfig config.cfg" );
	}

	// free up any current game dir info
	QMutex_Lock( fs_searchpaths_mutex );
	while( fs_searchpaths != fs_base_searchpaths ) {
		if( fs_searchpaths->pack ) {
			FS_FreePakFile( fs_searchpaths->pack );
		}
		FS_Free( fs_searchpaths->path );
		next = fs_searchpaths->next;
		FS_Free( fs_searchpaths );
		fs_searchpaths = next;
	}
	QMutex_Unlock( fs_searchpaths_mutex );

	if( !strcmp( dir, fs_basegame->string ) || ( *dir == 0 ) ) {
		Cvar_ForceSet( "fs_game", fs_basegame->string );
	} else {
		Cvar_ForceSet( "fs_game", dir );
		FS_AddGameDirectory( dir );
	}

	// if game directory is present but we haven't initialized filesystem yet,
	// that means fs_game was set via early commands and autoexec.cfg (and confi.cfg in the
	// case of client) will be executed in Qcommon_Init, so prevent double execution
	if( fs_initialized ) {
		if( !dedicated || !dedicated->integer ) {
			Cbuf_AddText( "exec config.cfg\n" );
			Cbuf_AddText( "exec autoexec.cfg\n" );

			// flush all data, so it will be forced to reload
			Cbuf_AddText( "s_restart 1\nin_restart\n" );
		} else {
			Cbuf_AddText( "exec dedicated_autoexec.cfg\n" );
		}
	}

	Cbuf_Execute();

	return true;
}

/*
* FS_AddBasePath
*/
static void FS_AddBasePath( const char *path ) {
	searchpath_t *newpath;

	newpath = ( searchpath_t* )FS_Malloc( sizeof( searchpath_t ) );
	newpath->path = FS_CopyString( path );
	newpath->next = fs_basepaths;
	fs_basepaths = newpath;
	COM_SanitizeFilePath( newpath->path );
}

/*
* FS_FreeSearchFiles
*/
static void FS_FreeSearchFiles( void ) {
	int i;

	// free temp memory
	for( i = 0; i < fs_cursearchfiles; i++ ) {
		if( !fs_searchfiles[i].searchPath->pack ) {
			Mem_ZoneFree( fs_searchfiles[i].name );
		}
	}
	fs_cursearchfiles = 0;
}

/*
* FS_GetNotifications
*/
int FS_GetNotifications( void ) {
	return fs_notifications;
}

/*
* FS_AddNotifications
*/
static int FS_AddNotifications( int bitmask ) {
	fs_notifications |= bitmask;
	return fs_notifications;
}

/*
* FS_RemoveNotifications
*/
int FS_RemoveNotifications( int bitmask ) {
	fs_notifications &= ~bitmask;
	return fs_notifications;
}

/*
* FS_AddFileToMedia
*/
void FS_AddFileToMedia( const char *filename ) {
	Sys_FS_AddFileToMedia( filename );
}

/*
* Cmd_FS_SearchExt_f
*/
static void Cmd_FS_SearchExt_f( int cantHaveFlags, int mustHaveFlags ) {
	char *pattern;
	int argc = Cmd_Argc();
	int total;
	searchpath_t *search;

	if( argc != 2 ) {
		Com_Printf( "Usage: %s <pattern>\n", Cmd_Argv( 0 ) );
		return;
	}

	total = 0;
	pattern = Cmd_Argv( 1 );

	QMutex_Lock( fs_searchpaths_mutex );

	for( search = fs_searchpaths; search; search = search->next ) {
		unsigned int i;
		pack_t *pack;
		packfile_t *pakfile;
		bool first;
		struct trie_dump_s *trie_dump = NULL;
		trie_error_t trie_err;

		pack = search->pack;
		if( !pack ) {
			continue;
		}

		trie_err = Trie_DumpIf( pack->trie, "", TRIE_DUMP_VALUES,
								FS_PatternMatchesPackfile, pattern, &trie_dump );

		if( trie_err == TRIE_OK ) {
			first = true;

			for( i = 0; i < trie_dump->size; i++ ) {
				pakfile = ( (packfile_t *) ( trie_dump->key_value_vector[i].value ) );

				if( mustHaveFlags && !( pakfile->flags & mustHaveFlags ) ) {
					continue;
				}
				if( cantHaveFlags && ( pakfile->flags & cantHaveFlags ) ) {
					continue;
				}

				if( first ) {
					Com_Printf( "\n" S_COLOR_YELLOW "%s%s\n", pack->filename, pack->pure ? " (P)" : "" );
					first = false;
				}
				Com_Printf( "   %s\n", pakfile->name );
				total++;
			}
		}
		Trie_FreeDump( trie_dump );
	}

	QMutex_Unlock( fs_searchpaths_mutex );

	Com_Printf( "\nFound " S_COLOR_YELLOW "%i" S_COLOR_WHITE " files matching the pattern.\n", total );
}

/*
* Cmd_FS_Search_f
*/
static void Cmd_FS_Search_f( void ) {
	Cmd_FS_SearchExt_f( 0, 0 );
}

/*
* Cmd_FS_Untouched_f
*/
static void Cmd_FS_Untouched_f( void ) {
	Cmd_FS_SearchExt_f( FS_PACKFILE_DIRECTORY | FS_PACKFILE_COHERENT, 0 );
}

/*
* Cmd_FileChecksum_f
*/
static void Cmd_FileChecksum_f( void ) {
	unsigned int checksum, checksum2;
	const char *filename;

	if( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: %s <path>\n", Cmd_Argv( 0 ) );
		return;
	}

	filename = Cmd_Argv( 1 );
	if( !COM_FileExtension( filename ) ) {
		Com_Printf( "No file extension provided\n" );
		return;
	}

	if( !COM_ValidateRelativeFilename( filename ) ) {
		Com_Printf( "Invalid filename\n" );
		return;
	}

	checksum = FS_ChecksumBaseFile( filename, false );
	checksum2 = FS_ChecksumBaseFile( filename, true );

	if( !checksum || !checksum2 ) {
		Com_Printf( "%s not found\n", filename );
		return;
	}

	Com_Printf( "%u %s %u\n", checksum, filename, checksum2 );
}

/*
* Cmd_FileMTime_f
*/
static void Cmd_FileMTime_f( void ) {
	time_t mtime;
	const char *filename;
	struct tm *newtime;
	bool base;

	if( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: %s <path> [base]\n", Cmd_Argv( 0 ) );
		return;
	}

	filename = Cmd_Argv( 1 );
	base = atoi( Cmd_Argv( 2 ) ) != 0 ? true : false;

	if( !COM_ValidateRelativeFilename( filename ) ) {
		Com_Printf( "Invalid filename\n" );
		return;
	}

	mtime = _FS_FileMTime( filename, base );
	newtime = localtime( &mtime );

	if( mtime < 0 || !newtime ) {
		Com_Printf( "Uknown mtime for %s\n", filename );
		return;
	}

	Com_Printf(
		"%s was last modified on: "
		"%04d-%02d-%02d %02d:%02d:%02d\n",
		filename,
		newtime->tm_year + 1900, newtime->tm_mon + 1, newtime->tm_mday, newtime->tm_hour, newtime->tm_min, newtime->tm_sec
		);
}

/*
* FS_Init
*/
void FS_Init( void ) {
	int i;
	const char *homedir;
	const char *cachedir;
	char downloadsdir[FS_MAX_PATH];

	assert( !fs_initialized );

	fs_fh_mutex = QMutex_Create();
	fs_searchpaths_mutex = QMutex_Create();

	fs_mempool = Mem_AllocPool( NULL, "Filesystem" );

	Cmd_AddCommand( "fs_path", FS_Path_f );
	Cmd_AddCommand( "fs_pakfile", Cmd_PakFile_f );
	Cmd_AddCommand( "fs_search", Cmd_FS_Search_f );
	Cmd_AddCommand( "fs_checksum", Cmd_FileChecksum_f );
	Cmd_AddCommand( "fs_mtime", Cmd_FileMTime_f );
	Cmd_AddCommand( "fs_untoched", Cmd_FS_Untouched_f );

	fs_numsearchfiles = FS_MIN_SEARCHFILES;
	fs_searchfiles = ( searchfile_t* )FS_Malloc( sizeof( searchfile_t ) * fs_numsearchfiles );

	memset( fs_filehandles, 0, sizeof( fs_filehandles ) );

	//
	// link filehandles
	//
	fs_free_filehandles = fs_filehandles;
	fs_filehandles_headnode.prev = &fs_filehandles_headnode;
	fs_filehandles_headnode.next = &fs_filehandles_headnode;
	for( i = 0; i < FS_MAX_HANDLES - 1; i++ )
		fs_filehandles[i].next = &fs_filehandles[i + 1];

	//
	// set basepaths
	//
	fs_cdpath = Cvar_Get( "fs_cdpath", "", CVAR_NOSET );
	fs_basepath = Cvar_Get( "fs_basepath", ".", CVAR_NOSET );
	homedir = Sys_FS_GetHomeDirectory();
	if( homedir != NULL )
#ifdef PUBLIC_BUILD
	{ fs_usehomedir = Cvar_Get( "fs_usehomedir", "1", CVAR_NOSET );}
#else
	{ fs_usehomedir = Cvar_Get( "fs_usehomedir", "0", CVAR_NOSET );}
#endif
	fs_usedownloadsdir = Cvar_Get( "fs_usedownloadsdir", "1", CVAR_NOSET );

	fs_downloads_searchpath = NULL;
	if( fs_usedownloadsdir->integer ) {
		if( homedir != NULL && fs_usehomedir->integer ) {
			Q_snprintfz( downloadsdir, sizeof( downloadsdir ), "%s/%s", homedir, "downloads" );
		} else {
			Q_snprintfz( downloadsdir, sizeof( downloadsdir ), "%s", "downloads" );
		}

		FS_AddBasePath( downloadsdir );
		fs_downloads_searchpath = fs_basepaths;
	}

	if( fs_cdpath->string[0] ) {
		FS_AddBasePath( fs_cdpath->string );
	}

	FS_AddBasePath( fs_basepath->string );
	fs_root_searchpath = fs_basepaths;
	fs_write_searchpath = fs_basepaths;

	if( homedir != NULL && fs_usehomedir->integer ) {
		FS_AddBasePath( homedir );
		fs_write_searchpath = fs_basepaths;
	}

	cachedir = Sys_FS_GetCacheDirectory();
	if( cachedir ) {
		FS_AddBasePath( cachedir );
	}

	Sys_VFS_Init();

	//
	// set game directories
	//
	fs_basegame = Cvar_Get( "fs_basegame", DEFAULT_BASEGAME, CVAR_NOSET );
	if( !fs_basegame->string[0] ) {
		Cvar_ForceSet( "fs_basegame", DEFAULT_BASEGAME );
	}
	fs_game = Cvar_Get( "fs_game", fs_basegame->string, CVAR_LATCH | CVAR_SERVERINFO );
	if( !fs_game->string[0] ) {
		Cvar_ForceSet( "fs_game", fs_basegame->string );
	}

	FS_AddGameDirectory( fs_basegame->string );

	fs_base_searchpaths = fs_searchpaths;

	if( strcmp( fs_game->string, fs_basegame->string ) ) {
		FS_SetGameDirectory( fs_game->string, false );
	}

	// no notifications after startup
	FS_RemoveNotifications( ~0 );

	// done
	Com_Printf( "Using %s for writing\n", FS_WriteDirectory() );

	fs_cursearchfiles = 0;

	fs_initialized = true;
}

/*
* FS_Rescan
*/
int FS_Rescan( void ) {
	int newpaks = 0;

	newpaks += FS_UpdateGameDirectory( fs_basegame->string );
	if( strcmp( fs_game->string, fs_basegame->string ) ) {
		newpaks += FS_UpdateGameDirectory( fs_game->string );
	}

	if( newpaks ) {
		FS_AddNotifications( FS_NOTIFY_NEWPAKS );
	}

	return newpaks;
}

/*
* FS_Frame
*/
void FS_Frame( void ) {
	FS_FreeSearchFiles();
}

/*
* FS_Shutdown
*/
void FS_Shutdown( void ) {
	searchpath_t *search;

	if( !fs_initialized ) {
		return;
	}

	Cmd_RemoveCommand( "fs_path" );
	Cmd_RemoveCommand( "fs_pakfile" );
	Cmd_RemoveCommand( "fs_search" );
	Cmd_RemoveCommand( "fs_checksum" );
	Cmd_RemoveCommand( "fs_mtime" );
	Cmd_RemoveCommand( "fs_untoched" );

	FS_FreeSearchFiles();
	FS_Free( fs_searchfiles );
	fs_numsearchfiles = 0;

	QMutex_Lock( fs_searchpaths_mutex );

	while( fs_searchpaths ) {
		search = fs_searchpaths;
		fs_searchpaths = search->next;

		if( search->pack ) {
			FS_FreePakFile( search->pack );
		}
		FS_Free( search->path );
		FS_Free( search );
	}

	QMutex_Unlock( fs_searchpaths_mutex );

	while( fs_basepaths ) {
		search = fs_basepaths;
		fs_basepaths = search->next;

		FS_Free( search->path );
		FS_Free( search );
	}

	Sys_VFS_Shutdown();

	Mem_FreePool( &fs_mempool );

	QMutex_Destroy( &fs_fh_mutex );
	QMutex_Destroy( &fs_searchpaths_mutex );

	fs_initialized = false;
}
