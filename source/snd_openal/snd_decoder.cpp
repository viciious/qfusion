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

static snd_decoder_t *decoders;

/*
* Local helper functions
*/

// This should always be called with the extension removed
static snd_decoder_t *findCodec( const char *filename ) {
	snd_decoder_t *decoder = decoders;
	const char *extension = COM_FileExtension( filename );

	if( extension ) {
		while( decoder ) {
			if( !Q_stricmp( extension, decoder->ext ) ) {
				return decoder;
			}

			decoder = decoder->next;
		}
	}

	return NULL;
}

static void decoder_register( snd_decoder_t *decoder ) {
	decoder->next = decoders;
	decoders = decoder;
}

/**
* Sound system wide functions (snd_local.h)
*/

bool S_InitDecoders( bool verbose ) {
	// First codec has the priority.
	decoders = NULL;

	decoder_register( &wav_decoder );
	if( SNDOGG_Init( verbose ) ) {
		decoder_register( &ogg_decoder );
		decoder_register( &ogv_decoder );
	}

	return true;
}

void S_ShutdownDecoders( bool verbose ) {
	decoders = NULL;
	SNDOGG_Shutdown( verbose );
}

void *S_LoadSound( const char *filename, snd_info_t *info ) {
	snd_decoder_t *decoder;
	char fn[MAX_QPATH];

	decoder = findCodec( filename );
	if( !decoder ) {
		//Com_Printf( "No decoder found for file: %s\n", filename );
		return NULL;
	}

	Q_strncpyz( fn, filename, sizeof( fn ) );
	COM_DefaultExtension( fn, decoder->ext, sizeof( fn ) );

	return decoder->load( fn, info );
}

snd_stream_t *S_OpenStream( const char *filename, bool *delay ) {
	snd_decoder_t *decoder;
	char fn[MAX_QPATH];

	decoder = findCodec( filename );
	if( !decoder ) {
		//Com_Printf( "No decoder found for file: %s\n", filename );
		return NULL;
	}

	Q_strncpyz( fn, filename, sizeof( fn ) );
	COM_DefaultExtension( fn, decoder->ext, sizeof( fn ) );

	return decoder->open( fn, delay );
}

bool S_ContOpenStream( snd_stream_t *stream ) {
	return stream->decoder->cont_open( stream );
}

int S_ReadStream( snd_stream_t *stream, int bytes, void *buffer ) {
	return stream->decoder->read( stream, bytes, buffer );
}

void S_CloseStream( snd_stream_t *stream ) {
	stream->decoder->close( stream );
}

bool S_ResetStream( snd_stream_t *stream ) {
	return stream->decoder->reset( stream );
}

bool S_EoStream( snd_stream_t *stream ) {
	return stream->decoder->eof( stream );
}

int S_SeekSteam( snd_stream_t *stream, int ofs, int whence ) {
	return stream->decoder->seek( stream, ofs, whence );
}

/**
* Util functions used by decoders (snd_decoder.h)
*/
snd_stream_t *decoder_stream_init( snd_decoder_t *decoder ) {
	snd_stream_t *stream;

	// Allocate a stream
	stream = (snd_stream_t *)S_Malloc( sizeof( snd_stream_t ) );
	stream->decoder = decoder;
	return stream;
}

void decoder_stream_shutdown( snd_stream_t *stream ) {
	S_Free( stream );
}
