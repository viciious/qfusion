/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2005 Stuart Dalton (badcdev@gmail.com)

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Foobar; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#define OV_EXCLUDE_STATIC_CALLBACKS

#include "snd_decoder.h"
#include <vorbis/vorbisfile.h>

#ifdef VORBISLIB_RUNTIME

void *vorbisLibrary = NULL;

int ( *qov_clear )( OggVorbis_File *vf );
int ( *qov_open_callbacks )( void *datasource, OggVorbis_File *vf, const char *initial, long ibytes, ov_callbacks callbacks );
ogg_int64_t ( *qov_pcm_total )( OggVorbis_File *vf, int i );
vorbis_info *( *qov_info )( OggVorbis_File *vf, int link );
long ( *qov_read )( OggVorbis_File *vf, char *buffer, int length, int bigendianp, int word, int sgned, int *bitstream );
long ( *qov_streams )( OggVorbis_File *vf );
long ( *qov_seekable )( OggVorbis_File *vf );
int ( *qov_pcm_seek )( OggVorbis_File *vf, ogg_int64_t pos );

dllfunc_t oggvorbisfuncs[] =
{
	{ "ov_clear", ( void ** )&qov_clear },
	{ "ov_open_callbacks", ( void ** )&qov_open_callbacks },
	{ "ov_pcm_total", ( void ** )&qov_pcm_total },
	{ "ov_info", ( void ** )&qov_info },
	{ "ov_read", ( void ** )&qov_read },
	{ "ov_streams", ( void ** )&qov_streams },
	{ "ov_seekable", ( void ** )&qov_seekable },
	{ "ov_pcm_seek", ( void ** )&qov_pcm_seek },

	{ NULL, NULL }
};

#else // VORBISLIB_RUNTIME

#define qov_clear ov_clear
#define qov_open_callbacks ov_open_callbacks
#define qov_pcm_total ov_pcm_total
#define qov_info ov_info
#define qov_read ov_read
#define qov_streams ov_streams
#define qov_seekable ov_seekable
#define qov_pcm_seek ov_pcm_seek

#endif // VORBISLIB_RUNTIME

/*
* SNDOGG_Shutdown
*/
void SNDOGG_Shutdown( bool verbose ) {
#ifdef VORBISLIB_RUNTIME
	if( vorbisLibrary ) {
		trap_UnloadLibrary( &vorbisLibrary );
	}
#endif
}

/*
* SNDOGG_Init
*/
bool SNDOGG_Init( bool verbose ) {
#ifdef VORBISLIB_RUNTIME
	if( vorbisLibrary ) {
		SNDOGG_Shutdown( verbose );
	}

	vorbisLibrary = trap_LoadLibrary( LIBVORBISFILE_LIBNAME, oggvorbisfuncs );
	if( !vorbisLibrary ) {
		if( verbose ) {
			Com_Printf( "Couldn't load %s\n", LIBVORBISFILE_LIBNAME );
		}
		return false;
	}
#endif
	return true;
}

//=============================================================================

typedef struct snd_ogg_stream_s snd_ogg_stream_t;

struct snd_ogg_stream_s {
	OggVorbis_File *vorbisfile;
	int filenum;
};

static bool read_ogg_header( OggVorbis_File *vorbisfile, snd_info_t *info ) {
	vorbis_info *vorbisinfo;

	vorbisinfo = qov_info( vorbisfile, -1 );
	if( !vorbisinfo ) {
		return false;
	}

	info->rate = vorbisinfo->rate;
	info->width = 2;
	info->channels = vorbisinfo->channels;
	info->samples = (int)qov_pcm_total( vorbisfile, -1 );
	info->size = info->samples * info->channels * info->width;

	return true;
}

static void decoder_ogg_stream_shutdown( snd_stream_t *stream ) {
	S_Free( stream->ptr );
	decoder_stream_shutdown( stream );
}

/*
* Callback functions to get oggs from paks
*/

static size_t ovcb_read( void *ptr, size_t size, size_t nb, void *datasource ) {
	intptr_t filenum = (intptr_t) datasource;

	return trap_FS_Read( ptr, size * nb, (int) filenum ) / size;
}

static int ovcb_seek( void *datasource, ogg_int64_t offset, int whence ) {
	intptr_t filenum = (intptr_t) datasource;

	switch( whence ) {
		case SEEK_SET: return trap_FS_Seek( (int) filenum, (int) offset, FS_SEEK_SET );
		case SEEK_CUR: return trap_FS_Seek( (int) filenum, (int) offset, FS_SEEK_CUR );
		case SEEK_END: return trap_FS_Seek( (int) filenum, (int) offset, FS_SEEK_END );
	}

	return 0;
}

static int ovcb_close( void *datasource ) {
	intptr_t filenum = (intptr_t) datasource;

	trap_FS_FCloseFile( (int) filenum );
	return 0;
}

static long ovcb_tell( void *datasource ) {
	intptr_t filenum = (intptr_t) datasource;

	return trap_FS_Tell( (int) filenum );
}

/**
* OGG decoder
*/
snd_decoder_t ogg_decoder =
{
	".ogg",
	decoder_ogg_load,
	decoder_ogg_open,
	decoder_ogg_cont_open,
	decoder_ogg_read,
	decoder_ogg_close,
	decoder_ogg_reset,
	decoder_ogg_eof,
	decoder_ogg_tell,
	decoder_ogg_seek,
	NULL
};

snd_decoder_t ogv_decoder =
{
	".ogv",
	decoder_ogg_load,
	decoder_ogg_open,
	decoder_ogg_cont_open,
	decoder_ogg_read,
	decoder_ogg_close,
	decoder_ogg_reset,
	decoder_ogg_eof,
	decoder_ogg_tell,
	decoder_ogg_seek,
	NULL
};

void *decoder_ogg_load( const char *filename, snd_info_t *info ) {
	OggVorbis_File vorbisfile;
	int filenum, bitstream, bytes_read, bytes_read_total;
	char *buffer;
	ov_callbacks callbacks = { ovcb_read, ovcb_seek, ovcb_close, ovcb_tell };

	trap_FS_FOpenFile( filename, &filenum, FS_READ | FS_NOSIZE );
	if( !filenum ) {
		return NULL;
	}

	if( trap_FS_IsUrl( filename ) ) {
		callbacks.seek_func = NULL;
		callbacks.tell_func = NULL;
	}

	if( qov_open_callbacks( (void *) (intptr_t) filenum, &vorbisfile, NULL, 0, callbacks ) < 0 ) {
		Com_Printf( "Could not open %s for reading\n", filename );
		trap_FS_FCloseFile( filenum );
		qov_clear( &vorbisfile );
		return NULL;
	}

	if( callbacks.seek_func && !qov_seekable( &vorbisfile ) ) {
		Com_Printf( "Error unsupported .ogg file (not seekable): %s\n", filename );
		qov_clear( &vorbisfile ); // Does FS_FCloseFile
		return NULL;
	}

	if( qov_streams( &vorbisfile ) != 1 ) {
		Com_Printf( "Error unsupported .ogg file (multiple logical bitstreams): %s\n", filename );
		qov_clear( &vorbisfile ); // Does FS_FCloseFile
		return NULL;
	}

	if( !read_ogg_header( &vorbisfile, info ) ) {
		Com_Printf( "Error reading .ogg file header: %s\n", filename );
		qov_clear( &vorbisfile ); // Does FS_FCloseFile
		return NULL;
	}

	buffer = (char *)S_Malloc( info->size );

	bytes_read_total = 0;
	do {
#ifdef ENDIAN_BIG
		bytes_read = qov_read( &vorbisfile, buffer + bytes_read_total, info->size - bytes_read_total, 1, 2, 1, &bitstream );
#elif defined ( ENDIAN_LITTLE )
		bytes_read = qov_read( &vorbisfile, buffer + bytes_read_total, info->size - bytes_read_total, 0, 2, 1, &bitstream );
#else
#error "runtime endianess detection support missing"
#endif
		bytes_read_total += bytes_read;
	} while( bytes_read > 0 && bytes_read_total < info->size );

	qov_clear( &vorbisfile ); // Does FS_FCloseFile
	if( !bytes_read_total ) {
		Com_Printf( "Error reading .ogg file: %s\n", filename );
		S_Free( buffer );
		return NULL;
	}

	return buffer;
}

snd_stream_t *decoder_ogg_open( const char *filename, bool *delay ) {
	snd_stream_t *stream;
	snd_ogg_stream_t *ogg_stream;

	// Open
	stream = decoder_stream_init( &ogg_decoder );
	if( !stream ) {
		Com_Printf( "Error initializing .ogg stream: %s\n", filename );
		return NULL;
	}

	stream->isUrl = trap_FS_IsUrl( filename );
	stream->ptr = S_Malloc( sizeof( snd_ogg_stream_t ) );

	ogg_stream = (snd_ogg_stream_t *)stream->ptr;
	ogg_stream->vorbisfile = NULL;

	trap_FS_FOpenFile( filename, &ogg_stream->filenum, FS_READ | FS_NOSIZE );
	if( !ogg_stream->filenum ) {
		decoder_ogg_stream_shutdown( stream );
		return NULL;
	}

	if( delay ) {
		*delay = false;
	}

	if( stream->isUrl && delay ) {
		*delay = true;
		return stream;
	}

	if( !decoder_ogg_cont_open( stream ) ) {
		decoder_ogg_close( stream );
		return NULL;
	}

	return stream;
}

bool decoder_ogg_cont_open( snd_stream_t *stream ) {
	snd_ogg_stream_t *ogg_stream;
	ov_callbacks callbacks = { ovcb_read, ovcb_seek, ovcb_close, ovcb_tell };

	ogg_stream = (snd_ogg_stream_t *)stream->ptr;
	ogg_stream->vorbisfile = (OggVorbis_File *)S_Malloc( sizeof( *ogg_stream->vorbisfile ) );

	if( stream->isUrl ) {
		callbacks.seek_func = NULL;
		callbacks.tell_func = NULL;
	}

	if( qov_open_callbacks( (void *) (intptr_t) ogg_stream->filenum, ogg_stream->vorbisfile, NULL, 0, callbacks ) < 0 ) {
		Com_Printf( "Couldn't open .ogg file for reading\n" );
		trap_FS_FCloseFile( ogg_stream->filenum );
		return false;
	}

	if( callbacks.seek_func && !qov_seekable( ogg_stream->vorbisfile ) ) {
		Com_Printf( "Error unsupported .ogg file (not seekable)\n" );
		return false;
	}

	if( !read_ogg_header( ogg_stream->vorbisfile, &stream->info ) ) {
		Com_Printf( "Error reading .ogg file header\n" );
		return false;
	}

	return true;
}

int decoder_ogg_read( snd_stream_t *stream, int bytes, void *buffer ) {
	snd_ogg_stream_t *ogg_stream = (snd_ogg_stream_t *)stream->ptr;
	int bitstream, bytes_read, bytes_read_total = 0;
	int holes = 0;

	do {
#ifdef ENDIAN_BIG
		bytes_read = qov_read( ogg_stream->vorbisfile, (char *)buffer + bytes_read_total, bytes - bytes_read_total, 1, 2, 1,
							   &bitstream );
#elif defined ( ENDIAN_LITTLE )
		bytes_read = qov_read( ogg_stream->vorbisfile, (char *)buffer + bytes_read_total, bytes - bytes_read_total, 0, 2, 1,
							   &bitstream );
#else
#error "runtime endianess detection support missing"
#endif
		if( bytes_read < 0 ) {
			if( bytes_read == OV_HOLE ) {
				if( holes++ == 3 ) {
					break;
				}
				continue;
			}
			break;
		}
		bytes_read_total += bytes_read;
	} while( ( bytes_read > 0 || bytes_read == OV_HOLE ) && bytes_read_total < bytes );

	return bytes_read_total;
}

void decoder_ogg_close( snd_stream_t *stream ) {
	snd_ogg_stream_t *ogg_stream = (snd_ogg_stream_t *)stream->ptr;

	if( ogg_stream->vorbisfile ) {
		qov_clear( ogg_stream->vorbisfile ); // Does FS_FCloseFile
		S_Free( ogg_stream->vorbisfile );
	} else if( ogg_stream->filenum ) {
		trap_FS_FCloseFile( ogg_stream->filenum );
	}
	ogg_stream->vorbisfile = NULL;
	ogg_stream->filenum = 0;
	decoder_ogg_stream_shutdown( stream );
}

bool decoder_ogg_reset( snd_stream_t *stream ) {
	snd_ogg_stream_t *ogg_stream;

	if( stream->isUrl ) {
		return false;
	}

	ogg_stream = (snd_ogg_stream_t *)stream->ptr;

	// can't use ov_pcm_seek on .ogv files because of
	// https://trac.xiph.org/ticket/1486
	// so just seek to the beginning of the file
	return trap_FS_Seek( ogg_stream->filenum, 0, FS_SEEK_SET ) == 0 ? true : false;
}

bool decoder_ogg_eof( snd_stream_t *stream ) {
	snd_ogg_stream_t *ogg_stream = (snd_ogg_stream_t *)stream->ptr;

	return trap_FS_Eof( ogg_stream->filenum );
}

int decoder_ogg_tell( snd_stream_t *stream ) {
	snd_ogg_stream_t *ogg_stream = (snd_ogg_stream_t *)stream->ptr;
	return trap_FS_Tell( ogg_stream->filenum );
}

int decoder_ogg_seek( snd_stream_t *stream, int offset, int whence ) {
	snd_ogg_stream_t *ogg_stream = (snd_ogg_stream_t *)stream->ptr;

	switch( whence ) {
		case SEEK_SET: return trap_FS_Seek( ogg_stream->filenum, offset, FS_SEEK_SET );
		case SEEK_CUR: return trap_FS_Seek( ogg_stream->filenum, offset, FS_SEEK_CUR );
		case SEEK_END: return trap_FS_Seek( ogg_stream->filenum, offset, FS_SEEK_END );
	}

	return -1;
}
