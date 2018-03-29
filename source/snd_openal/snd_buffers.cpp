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

#include "snd_local.h"

#define MAX_SFX 4096
sfx_t knownSfx[MAX_SFX];
static bool buffers_inited = false;

/*
* Local helper functions
*/

void * stereo_mono( void *data, snd_info_t *info ) {
	int i, interleave, gain;
	void *outdata;

	outdata = S_Malloc( info->samples * info->width );
	interleave = info->channels * info->width;
	gain = s_stereo2mono->integer;
	clamp( gain, -1, 1 );

	if( info->width == 2 ) {
		short *pin, *pout;

		pin = (short*)data;
		pout = (short*)outdata;

		for( i = 0; i < info->size; i += interleave, pin += info->channels, pout++ ) {
			*pout = ( ( 1 - gain ) * pin[0] + ( 1 + gain ) * pin[1] ) / 2;
		}
	} else if( info->width == 1 ) {
		char *pin, *pout;

		pin = (char*)data;
		pout = (char*)outdata;

		for( i = 0; i < info->size; i += interleave, pin += info->channels, pout++ ) {
			*pout = ( ( 1 - gain ) * pin[0] + ( 1 + gain ) * pin[1] ) / 2;
		}
	} else {
		S_Free( outdata );
		return NULL;
	}

	info->channels = 1;
	info->size = info->samples * info->width;

	return outdata;
}

static sfx_t *buffer_find_free( void ) {
	int i;

	for( i = 0; i < MAX_SFX; i++ ) {
		if( knownSfx[i].filename[0] == '\0' ) {
			return &knownSfx[i];
		}
	}

	S_Error( "Sound Limit Exceeded.\n" );
	return NULL;
}

sfx_t *S_GetBufferById( int id ) {
	if( id < 0 || id >= MAX_SFX ) {
		return NULL;
	}
	return knownSfx + id;
}

bool S_UnloadBuffer( sfx_t *sfx ) {
	ALenum error;

	if( !sfx ) {
		return false;
	}
	if( sfx->filename[0] == '\0' || sfx->isLocked || !sfx->inMemory ) {
		return false;
	}

	qalDeleteBuffers( 1, &sfx->buffer );
	if( ( error = qalGetError() ) != AL_NO_ERROR ) {
		Com_Printf( "Couldn't delete sound buffer for %s (%s)", sfx->filename, S_ErrorMessage( error ) );
		sfx->isLocked = true;
		return false;
	}

	sfx->inMemory = false;

	return true;
}

// Remove the least recently used sound effect from memory
static bool buffer_evict() {
	int i;
	int candinate = -1;
	int candinate_value = trap_Milliseconds();

	for( i = 0; i < MAX_SFX; i++ ) {
		if( knownSfx[i].filename[0] == '\0' || !knownSfx[i].inMemory || knownSfx[i].isLocked ) {
			continue;
		}

		if( knownSfx[i].used < candinate_value ) {
			candinate = i;
			candinate_value = knownSfx[i].used;
		}
	}

	if( candinate != -1 ) {
		return S_UnloadBuffer( &knownSfx[candinate] );
	}

	return false;
}

bool S_LoadBuffer( sfx_t *sfx ) {
	ALenum error;
	void *data;
	snd_info_t info;
	ALuint format;

	if( !sfx ) {
		return false;
	}
	if( sfx->filename[0] == '\0' || sfx->inMemory ) {
		return false;
	}
	if( trap_FS_IsUrl( sfx->filename ) ) {
		return false;
	}

	data = S_LoadSound( sfx->filename, &info );
	if( !data ) {
		//Com_DPrintf( "Couldn't load %s\n", sfx->filename );
		return false;
	}

	if( info.channels > 1 ) {
		void *temp = stereo_mono( data, &info );
		if( temp ) {
			S_Free( data );
			data = temp;
		}
	}

	format = S_SoundFormat( info.width, info.channels );

	qalGenBuffers( 1, &sfx->buffer );
	if( ( error = qalGetError() ) != AL_NO_ERROR ) {
		S_Free( data );
		Com_Printf( "Couldn't create a sound buffer for %s (%s)\n", sfx->filename, S_ErrorMessage( error ) );
		return false;
	}

	qalBufferData( sfx->buffer, format, data, info.size, info.rate );
	error = qalGetError();

	// If we ran out of memory, start evicting the least recently used sounds
	while( error == AL_OUT_OF_MEMORY ) {
		if( !buffer_evict() ) {
			S_Free( data );
			Com_Printf( "Out of memory loading %s\n", sfx->filename );
			return false;
		}

		// Try load it again
		qalGetError();
		qalBufferData( sfx->buffer, format, data, info.size, info.rate );
		error = qalGetError();
	}

	// Some other error condition
	if( error != AL_NO_ERROR ) {
		S_Free( data );
		Com_Printf( "Couldn't fill sound buffer for %s (%s)", sfx->filename, S_ErrorMessage( error ) );
		return false;
	}

	S_Free( data );
	sfx->inMemory = true;

	return true;
}

/*
* Sound system wide functions (snd_al_local.h)
*/

// Find a sound effect if loaded, set up a handle otherwise
sfx_t *S_FindBuffer( const char *filename ) {
	sfx_t *sfx;
	int i;

	for( i = 0; i < MAX_SFX; i++ ) {
		if( !Q_stricmp( knownSfx[i].filename, filename ) ) {
			return &knownSfx[i];
		}
	}

	sfx = buffer_find_free();

	memset( sfx, 0, sizeof( *sfx ) );
	sfx->id = sfx - knownSfx;
	Q_strncpyz( sfx->filename, filename, sizeof( sfx->filename ) );

	return sfx;
}

void S_MarkBufferFree( sfx_t *sfx ) {
	sfx->filename[0] = '\0';
	sfx->registration_sequence = 0;
	sfx->used = 0;
}

void S_ForEachBuffer( void ( *callback )( sfx_t *sfx ) ) {
	int i;

	if( !buffers_inited ) {
		return;
	}

	for( i = 0; i < MAX_SFX; i++ ) {
		callback( knownSfx + i );
	}
}

void S_InitBuffers( void ) {
	if( buffers_inited ) {
		return;
	}

	memset( knownSfx, 0, sizeof( knownSfx ) );

	buffers_inited = true;
}

void S_ShutdownBuffers( void ) {
	int i;

	if( !buffers_inited ) {
		return;
	}

	for( i = 0; i < MAX_SFX; i++ )
		S_UnloadBuffer( &knownSfx[i] );

	memset( knownSfx, 0, sizeof( knownSfx ) );
	buffers_inited = false;
}

void S_SoundList_f( void ) {
	int i;

	for( i = 0; i < MAX_SFX; i++ ) {
		if( knownSfx[i].filename[0] != '\0' ) {
			if( knownSfx[i].isLocked ) {
				Com_Printf( "L" );
			} else {
				Com_Printf( " " );
			}

			if( knownSfx[i].inMemory ) {
				Com_Printf( "M" );
			} else {
				Com_Printf( " " );
			}

			Com_Printf( " : %s\n", knownSfx[i].filename );
		}
	}
}

void S_UseBuffer( sfx_t *sfx ) {
	if( sfx->filename[0] == '\0' ) {
		return;
	}

	if( !sfx->inMemory ) {
		S_LoadBuffer( sfx );
	}

	sfx->used = trap_Milliseconds();
}

ALuint S_GetALBuffer( const sfx_t *sfx ) {
	return sfx->buffer;
}

/**
* Global functions (sound.h)
*/

void S_FreeSounds() {
	S_ShutdownBuffers();
	S_InitBuffers();
}

void S_Clear() {
}
