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

/*
* snd_decoder.h
* Sound decoder management
* It only decodes things, so technically it's not a COdec, but who cares?
*
* 2005-08-31
*  Started
*/

#include "snd_local.h"

// Codec functions
typedef void *( *DECODER_LOAD )( const char *filename, snd_info_t *info );
typedef snd_stream_t *( *DECODER_OPEN )( const char *filename, bool *delay );
typedef bool ( *DECODER_CONT_OPEN )( snd_stream_t *stream );
typedef int ( *DECODER_READ )( snd_stream_t *stream, int bytes, void *buffer );
typedef bool ( *DECODER_RESET )( snd_stream_t *stream );
typedef bool ( *DECODER_EOF )( snd_stream_t *stream );
typedef void ( *DECODER_CLOSE )( snd_stream_t *stream );
typedef int ( *DECODER_TELL )( snd_stream_t *stream );
typedef int ( *DECODER_SEEK )( snd_stream_t *stream, int offset, int whence );

// Codec data structure
struct snd_decoder_s {
	const char *ext;
	DECODER_LOAD load;
	DECODER_OPEN open;
	DECODER_CONT_OPEN cont_open;
	DECODER_READ read;
	DECODER_CLOSE close;
	DECODER_RESET reset;
	DECODER_EOF eof;
	DECODER_TELL tell;
	DECODER_SEEK seek;
	snd_decoder_t *next;
};

/**
* Util functions used by decoders
*/
snd_stream_t *decoder_stream_init( snd_decoder_t *decoder );
void decoder_stream_shutdown( snd_stream_t *stream );

/**
* WAV Codec
*/
extern snd_decoder_t wav_decoder;
void *decoder_wav_load( const char *filename, snd_info_t *info );
snd_stream_t *decoder_wav_open( const char *filename, bool *delay );
bool decoder_wav_cont_open( snd_stream_t *stream );
int decoder_wav_read( snd_stream_t *stream, int bytes, void *buffer );
void decoder_wav_close( snd_stream_t *stream );
bool decoder_wav_reset( snd_stream_t *stream );
bool decoder_wav_eof( snd_stream_t *stream );
int decoder_wav_tell( snd_stream_t *stream );
int decoder_wav_seek( snd_stream_t *stream, int offset, int whence );

/**
* Ogg Vorbis decoder
*/
extern snd_decoder_t ogg_decoder, ogv_decoder;
void *decoder_ogg_load( const char *filename, snd_info_t *info );
snd_stream_t *decoder_ogg_open( const char *filename, bool *delay );
bool decoder_ogg_cont_open( snd_stream_t *stream );
int decoder_ogg_read( snd_stream_t *stream, int bytes, void *buffer );
void decoder_ogg_close( snd_stream_t *stream );
bool decoder_ogg_reset( snd_stream_t *stream );
bool decoder_ogg_eof( snd_stream_t *stream );
int decoder_ogg_tell( snd_stream_t *stream );
int decoder_ogg_seek( snd_stream_t *stream, int offset, int whence );

bool SNDOGG_Init( bool verbose );
void SNDOGG_Shutdown( bool verbose );
