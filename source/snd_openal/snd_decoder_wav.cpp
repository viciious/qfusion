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

#include "snd_decoder.h"

typedef struct snd_wav_stream_s snd_wav_stream_t;

struct snd_wav_stream_s {
	int filenum;
	int position;
	int content_start;
};


/**
* Wave file reading
*/
static int FGetLittleLong( int f ) {
	int v;

	trap_FS_Read( &v, sizeof( v ), f );

	return LittleLong( v );
}

static int FGetLittleShort( int f ) {
	short v;

	trap_FS_Read( &v, sizeof( v ), f );

	return LittleShort( v );
}

static int readChunkInfo( int f, char *name ) {
	int len, read;

	name[4] = 0;

	read = trap_FS_Read( name, 4, f );
	if( read != 4 ) {
		return 0;
	}

	len = FGetLittleLong( f );
	if( len < 0 || len > 0xffffffff ) {
		return 0;
	}

	len = ( len + 1 ) & ~1; // pad to word boundary
	return len;
}

static void skipChunk( int f, int length ) {
	size_t toread;
	uint8_t buffer[32 * 1024];

	while( length > 0 ) {
		toread = length;
		if( toread > sizeof( buffer ) ) {
			toread = sizeof( buffer );
		}
		trap_FS_Read( buffer, toread, f );
		length -= toread;
	}
}

// returns the length of the data in the chunk, or 0 if not found
static int findWavChunk( int filenum, const char *chunk ) {
	char name[5];
	int len;

	// This is a bit dangerous...
	while( true ) {
		len = readChunkInfo( filenum, name );

		// Read failure?
		if( !len ) {
			return 0;
		}

		// If this is the right chunk, return
		if( !strcmp( name, chunk ) ) {
			return len;
		}

		// Not the right chunk - skip it
		skipChunk( filenum, len );
	}
}

static void byteSwapRawSamples( int samples, int width, int channels, const uint8_t *data ) {
	int i;

	if( LittleShort( 256 ) == 256 ) {
		return;
	}

	if( width != 2 ) {
		return;
	}

	if( channels == 2 ) {
		samples <<= 1;
	}

	for( i = 0; i < samples; i++ )
		( (short *)data )[i] = LittleShort( ( (short *)data )[i] );
}

static bool read_wav_header( int filenum, snd_info_t *info ) {
	char dump[16];
	int fmtlen = 0;

	// skip the riff wav header
	trap_FS_Read( dump, 12, filenum );

	// Scan for the format chunk
	if( !( fmtlen = findWavChunk( filenum, "fmt " ) ) ) {
		Com_Printf( "Error reading wav header: No fmt chunk\n" );
		return false;
	}

	// Save the parameters
	FGetLittleShort( filenum );
	info->channels = FGetLittleShort( filenum );
	info->rate = FGetLittleLong( filenum );
	FGetLittleLong( filenum );
	FGetLittleShort( filenum );
	info->width = FGetLittleShort( filenum ) / 8;

	// Skip the rest of the format chunk if required
	if( fmtlen > 16 ) {
		fmtlen -= 16;
		skipChunk( filenum, fmtlen );
	}

	// Scan for the data chunk
	if( !( info->size = findWavChunk( filenum, "data" ) ) ) {
		Com_Printf( "Error reading wav header: No data chunk\n" );
		return false;
	}
	info->samples = ( info->size / info->width ) / info->channels;

	return true;
}

static void decoder_wav_stream_shutdown( snd_stream_t *stream ) {
	S_Free( stream->ptr );
	decoder_stream_shutdown( stream );
}

/**
* WAV decoder
*/
snd_decoder_t wav_decoder =
{
	".wav",
	decoder_wav_load,
	decoder_wav_open,
	decoder_wav_cont_open,
	decoder_wav_read,
	decoder_wav_close,
	decoder_wav_reset,
	decoder_wav_eof,
	decoder_wav_tell,
	decoder_wav_seek,
	NULL
};

void *decoder_wav_load( const char *filename, snd_info_t *info ) {
	int filenum;
	int read;
	void *buffer;

	if( trap_FS_IsUrl( filename ) ) {
		return NULL;
	}

	trap_FS_FOpenFile( filename, &filenum, FS_READ | FS_NOSIZE );
	if( !filenum ) {
		return NULL;
	}

	if( !read_wav_header( filenum, info ) ) {
		trap_FS_FCloseFile( filenum );
		Com_Printf( "Can't understand .wav file: %s\n", filename );
		return NULL;
	}

	buffer = S_Malloc( info->size );
	read = trap_FS_Read( buffer, info->size, filenum );
	if( read != info->size ) {
		S_Free( buffer );
		trap_FS_FCloseFile( filenum );
		Com_Printf( "Error reading .wav file: %s\n", filename );
		return NULL;
	}

	byteSwapRawSamples( info->samples, info->width, info->channels, (uint8_t *)buffer );

	trap_FS_FCloseFile( filenum );

	return buffer;
}

snd_stream_t *decoder_wav_open( const char *filename, bool *delay ) {
	snd_stream_t *stream;
	snd_wav_stream_t *wav_stream;

	stream = decoder_stream_init( &wav_decoder );
	if( !stream ) {
		return NULL;
	}

	stream->isUrl = trap_FS_IsUrl( filename );
	if( stream->isUrl ) {
		return NULL;
	}

	if( delay ) {
		*delay = false;
	}

	stream->ptr = S_Malloc( sizeof( snd_wav_stream_t ) );
	wav_stream = (snd_wav_stream_t *)stream->ptr;

	trap_FS_FOpenFile( filename, &wav_stream->filenum, FS_READ | FS_NOSIZE );
	if( !wav_stream->filenum ) {
		decoder_wav_stream_shutdown( stream );
		return NULL;
	}

	if( !decoder_wav_cont_open( stream ) ) {
		return NULL;
	}

	return stream;
}

bool decoder_wav_cont_open( snd_stream_t *stream ) {
	snd_wav_stream_t *wav_stream;

	if( !stream ) {
		return false;
	}

	wav_stream = (snd_wav_stream_t *)stream->ptr;

	if( !read_wav_header( wav_stream->filenum, &stream->info ) ) {
		decoder_wav_close( stream );
		return false;
	}

	wav_stream->content_start = wav_stream->position;
	return true;
}

int decoder_wav_read( snd_stream_t *stream, int bytes, void *buffer ) {
	snd_wav_stream_t *wav_stream = (snd_wav_stream_t *)stream->ptr;
	int remaining = stream->info.size - wav_stream->position;
	int samples, bytes_read;

	if( remaining <= 0 ) {
		return 0;
	}

	if( bytes > remaining ) {
		bytes_read = remaining;
	} else {
		bytes_read = bytes;
	}

	wav_stream->position += bytes_read;
	samples = ( bytes_read / stream->info.width ) / stream->info.channels;

	trap_FS_Read( buffer, bytes_read, wav_stream->filenum );
	byteSwapRawSamples( samples, stream->info.width, stream->info.channels, (const uint8_t *)buffer );

	return bytes_read;
}

void decoder_wav_close( snd_stream_t *stream ) {
	snd_wav_stream_t *wav_stream = (snd_wav_stream_t *)stream->ptr;

	trap_FS_FCloseFile( wav_stream->filenum );
	decoder_wav_stream_shutdown( stream );
}

bool decoder_wav_reset( snd_stream_t *stream ) {
	snd_wav_stream_t *wav_stream = (snd_wav_stream_t *)stream->ptr;

	if( trap_FS_Seek( wav_stream->filenum, wav_stream->content_start, FS_SEEK_SET ) ) {
		return false;
	}

	wav_stream->position = wav_stream->content_start;
	return true;
}

bool decoder_wav_eof( snd_stream_t *stream ) {
	snd_wav_stream_t *wav_stream = (snd_wav_stream_t *)stream->ptr;
	return trap_FS_Eof( wav_stream->filenum );
}

int decoder_wav_tell( snd_stream_t *stream ) {
	snd_wav_stream_t *wav_stream = (snd_wav_stream_t *)stream->ptr;
	return trap_FS_Tell( wav_stream->filenum );
}

int decoder_wav_seek( snd_stream_t *stream, int offset, int whence ) {
	snd_wav_stream_t *wav_stream = (snd_wav_stream_t *)stream->ptr;

	switch( whence ) {
		case SEEK_SET: return trap_FS_Seek( wav_stream->filenum, offset, FS_SEEK_SET );
		case SEEK_CUR: return trap_FS_Seek( wav_stream->filenum, offset, FS_SEEK_CUR );
		case SEEK_END: return trap_FS_Seek( wav_stream->filenum, offset, FS_SEEK_END );
	}

	return -1;
}
