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

#include "snd_local.h"
#include "snd_cmdque.h"

static sndCmdPipe_t *s_cmdPipe;

static struct qthread_s *s_backThread;

static int s_registration_sequence;
static bool s_registering;

// batch entity spatializations
static unsigned s_num_ent_spats;
static smdCmdSpatialization_t s_ent_spats[SND_SPATIALIZE_ENTS_MAX];
static const unsigned s_max_ent_spats = sizeof( s_ent_spats ) / sizeof( s_ent_spats[0] );

struct mempool_s *soundpool;

cvar_t *developer;

cvar_t *s_volume;
cvar_t *s_musicvolume;
cvar_t *s_testsound;
cvar_t *s_khz;
cvar_t *s_show;
cvar_t *s_mixahead;
cvar_t *s_swapstereo;
cvar_t *s_pseudoAcoustics;
cvar_t *s_separationDelay;
cvar_t *s_globalfocus;

sfx_t known_sfx[MAX_SFX];
int num_sfx;

/*
===============================================================================

console functions

===============================================================================
*/

#ifdef ENABLE_PLAY
static void SF_Play_f( void ) {
	int i;
	sfx_t *sfx;

	for( i = 1; i < trap_Cmd_Argc(); i++ ) {
		sfx = SF_RegisterSound( trap_Cmd_Argv( i ) );
		if( !sfx ) {
			Com_Printf( "Couldn't play: %s\n", trap_Cmd_Argv( i ) );
			continue;
		}
		S_StartGlobalSound( sfx, 0, 1.0 );
	}
}
#endif // ENABLE_PLAY

/*
* SF_SoundList
*/
static void SF_SoundList_f( void ) {
	S_IssueStuffCmd( s_cmdPipe, "soundlist" );
}

/*
* S_Music
*/
static void SF_Music_f( void ) {
	if( trap_Cmd_Argc() < 2 ) {
		Com_Printf( "music: <introfile|playlist> [loopfile|shuffle]\n" );
		return;
	}

	SF_StartBackgroundTrack( trap_Cmd_Argv( 1 ), trap_Cmd_Argv( 2 ), 0 );
}

/*
* S_SoundInfo_f
*/
static void SF_SoundInfo_f( void ) {
	Com_Printf( "%5d stereo\n", dma.channels - 1 );
	Com_Printf( "%5d samples\n", dma.samples );
	Com_Printf( "%5d samplepos\n", dma.samplepos );
	Com_Printf( "%5d samplebits\n", dma.samplebits );
	Com_Printf( "%5d submission_chunk\n", dma.submission_chunk );
	Com_Printf( "%5d speed\n", dma.speed );
	Com_Printf( "0x%" PRIXPTR" dma buffer\n", (uintptr_t)dma.buffer );
}

/*
* SF_StopAllSounds_f
*/
static void SF_StopAllSounds_f( void ) {
	SF_StopAllSounds( true, true );
}

// =======================================================================
// Load a sound
// =======================================================================

/*
* SF_FindName
*/
static sfx_t *SF_FindName( const char *name ) {
	int i;
	sfx_t *sfx;

	if( !name ) {
		S_Error( "SF_FindName: NULL" );
	}
	if( !name[0] != '\0' ) {
		assert( name[0] != '\0' );
		S_Error( "SF_FindName: empty name" );
	}

	if( strlen( name ) >= MAX_QPATH ) {
		S_Error( "Sound name too long: %s", name );
	}

	// see if already loaded
	for( i = 0; i < num_sfx; i++ ) {
		if( !strcmp( known_sfx[i].name, name ) ) {
			return &known_sfx[i];
		}
	}

	// find a free sfx
	for( i = 0; i < num_sfx; i++ ) {
		if( !known_sfx[i].name[0] ) {
			break;
		}
	}

	if( i == num_sfx ) {
		if( num_sfx == MAX_SFX ) {
			S_Error( "S_FindName: out of sfx_t" );
		}
		num_sfx++;
	}

	sfx = &known_sfx[i];
	memset( sfx, 0, sizeof( *sfx ) );
	Q_strncpyz( sfx->name, name, sizeof( sfx->name ) );
	sfx->isUrl = trap_FS_IsUrl( name );

	return sfx;
}

/*
* SF_BeginRegistration
*/
void SF_BeginRegistration( void ) {
	s_registration_sequence++;
	if( !s_registration_sequence ) {
		s_registration_sequence = 1;
	}
	s_registering = true;

	// wait for the queue to be processed
	S_FinishSoundCmdPipe( s_cmdPipe );
}

/*
* SF_RegisterSound
*/
sfx_t *SF_RegisterSound( const char *name ) {
	sfx_t *sfx;
	int sfxnum;

	assert( name );

	sfx = SF_FindName( name );
	if( sfx->registration_sequence != s_registration_sequence ) {
		sfx->registration_sequence = s_registration_sequence;

		// evenly balance the load between two threads during registration
		sfxnum = sfx - known_sfx;
		if( !s_registering || sfxnum & 1 ) {
			S_IssueLoadSfxCmd( s_cmdPipe, sfxnum );
		} else {
			S_LoadSound( sfx );
		}
	}
	return sfx;
}

/*
* SF_FreeSounds
*/
void SF_FreeSounds( void ) {
	int i;
	sfx_t *sfx;

	// wait for the queue to be processed
	S_FinishSoundCmdPipe( s_cmdPipe );

	// free all sounds
	for( i = 0, sfx = known_sfx; i < num_sfx; i++, sfx++ ) {
		if( !sfx->name[0] ) {
			continue;
		}
		S_Free( sfx->cache );
		memset( sfx, 0, sizeof( *sfx ) );
	}
}

/*
* SF_EndRegistration
*/
void SF_EndRegistration( void ) {
	int i;
	sfx_t *sfx;

	// wait for the queue to be processed
	S_FinishSoundCmdPipe( s_cmdPipe );

	s_registering = false;

	// free any sounds not from this registration sequence
	for( i = 0, sfx = known_sfx; i < num_sfx; i++, sfx++ ) {
		if( !sfx->name[0] ) {
			continue;
		}
		if( sfx->registration_sequence != s_registration_sequence ) {
			// we don't need this sound
			S_Free( sfx->cache );
			memset( sfx, 0, sizeof( *sfx ) );
		}
	}
}

/*
* SF_Init
*/
bool SF_Init( void *hwnd, int maxEntities, bool verbose ) {
	soundpool = S_MemAllocPool( "QF Sound Module" );

	developer = trap_Cvar_Get( "developer", "0", 0 );

	s_volume = trap_Cvar_Get( "s_volume", "0.8", CVAR_ARCHIVE );
	s_musicvolume = trap_Cvar_Get( "s_musicvolume", "0.05", CVAR_ARCHIVE );
	s_khz = trap_Cvar_Get( "s_khz", "44", CVAR_ARCHIVE | CVAR_LATCH_SOUND );
	s_mixahead = trap_Cvar_Get( "s_mixahead", "0.14", CVAR_ARCHIVE );
	s_show = trap_Cvar_Get( "s_show", "0", CVAR_CHEAT );
	s_testsound = trap_Cvar_Get( "s_testsound", "0", 0 );
	s_swapstereo = trap_Cvar_Get( "s_swapstereo", "0", CVAR_ARCHIVE );
	s_pseudoAcoustics = trap_Cvar_Get( "s_pseudoAcoustics", "0", CVAR_ARCHIVE );
	s_separationDelay = trap_Cvar_Get( "s_separationDelay", "1.0", CVAR_ARCHIVE );
	s_globalfocus = trap_Cvar_Get( "s_globalfocus", "0", CVAR_ARCHIVE );

#ifdef ENABLE_PLAY
	trap_Cmd_AddCommand( "play", SF_Play_f );
#endif
	trap_Cmd_AddCommand( "music", SF_Music_f );
	trap_Cmd_AddCommand( "stopsound", SF_StopAllSounds_f );
	trap_Cmd_AddCommand( "stopmusic", SF_StopBackgroundTrack );
	trap_Cmd_AddCommand( "prevmusic", SF_PrevBackgroundTrack );
	trap_Cmd_AddCommand( "nextmusic", SF_NextBackgroundTrack );
	trap_Cmd_AddCommand( "pausemusic", SF_PauseBackgroundTrack );
	trap_Cmd_AddCommand( "soundlist", SF_SoundList_f );
	trap_Cmd_AddCommand( "soundinfo", SF_SoundInfo_f );

	num_sfx = 0;

	s_num_ent_spats = 0;

	s_registration_sequence = 1;
	s_registering = false;

	s_cmdPipe = S_CreateSoundCmdPipe();
	if( !s_cmdPipe ) {
		return false;
	}

	s_backThread = trap_Thread_Create( S_BackgroundUpdateProc, s_cmdPipe );

	S_IssueInitCmd( s_cmdPipe, hwnd, maxEntities, verbose );

	S_FinishSoundCmdPipe( s_cmdPipe );

	if( !dma.buffer ) {
		return false;
	}

	SF_SetAttenuationModel( S_DEFAULT_ATTENUATION_MODEL,
							S_DEFAULT_ATTENUATION_MAXDISTANCE, S_DEFAULT_ATTENUATION_REFDISTANCE );

	return true;
}


/*
* SF_Shutdown
*/
void SF_Shutdown( bool verbose ) {
	if( !soundpool ) {
		return;
	}

	SF_StopAllSounds( true, true );

	// free all sounds
	SF_FreeSounds();

	// wake up the mixer
	SF_Activate( true );

	// shutdown backend
	S_IssueShutdownCmd( s_cmdPipe, verbose );

	// wait for the queue to be processed
	S_FinishSoundCmdPipe( s_cmdPipe );

	// wait for the backend thread to die
	trap_Thread_Join( s_backThread );
	s_backThread = NULL;

	S_DestroySoundCmdPipe( &s_cmdPipe );

#ifdef ENABLE_PLAY
	trap_Cmd_RemoveCommand( "play" );
#endif
	trap_Cmd_RemoveCommand( "music" );
	trap_Cmd_RemoveCommand( "stopsound" );
	trap_Cmd_RemoveCommand( "stopmusic" );
	trap_Cmd_RemoveCommand( "prevmusic" );
	trap_Cmd_RemoveCommand( "nextmusic" );
	trap_Cmd_RemoveCommand( "pausemusic" );
	trap_Cmd_RemoveCommand( "soundlist" );
	trap_Cmd_RemoveCommand( "soundinfo" );

	S_MemFreePool( &soundpool );

	s_registering = false;

	num_sfx = 0;
}

/*
* SF_Activate
*/
void SF_Activate( bool active ) {
	if( !active && s_globalfocus->integer ) {
		return;
	}
	S_IssueActivateCmd( s_cmdPipe, active );
}

/*
* SF_StartBackgroundTrack
*/
void SF_StartBackgroundTrack( const char *intro, const char *loop, int mode ) {
	S_IssueStartBackgroundTrackCmd( s_cmdPipe, intro, loop, mode );
}

/*
* SF_StopBackgroundTrack
*/
void SF_StopBackgroundTrack( void ) {
	S_IssueStopBackgroundTrackCmd( s_cmdPipe );
}

/*
* SF_LockBackgroundTrack
*/
void SF_LockBackgroundTrack( bool lock ) {
	S_IssueLockBackgroundTrackCmd( s_cmdPipe, lock );
}

/*
* SF_StopAllSounds
*/
void SF_StopAllSounds( bool clear, bool stopMusic ) {
	S_IssueStopAllSoundsCmd( s_cmdPipe, clear, stopMusic );
}

/*
* SF_PrevBackgroundTrack
*/
void SF_PrevBackgroundTrack( void ) {
	S_IssueAdvanceBackgroundTrackCmd( s_cmdPipe, -1 );
}

/*
* SF_NextBackgroundTrack
*/
void SF_NextBackgroundTrack( void ) {
	S_IssueAdvanceBackgroundTrackCmd( s_cmdPipe, 1 );
}

/*
* SF_PauseBackgroundTrack
*/
void SF_PauseBackgroundTrack( void ) {
	S_IssuePauseBackgroundTrackCmd( s_cmdPipe );
}

/*
* SF_BeginAviDemo
*/
void SF_BeginAviDemo( void ) {
	S_IssueAviDemoCmd( s_cmdPipe, true );
}

/*
* SF_StopAviDemo
*/
void SF_StopAviDemo( void ) {
	S_IssueAviDemoCmd( s_cmdPipe, false );
}

/*
* SF_SetAttenuationModel
*/
void SF_SetAttenuationModel( int model, float maxdistance, float refdistance ) {
	S_IssueSetAttenuationCmd( s_cmdPipe, model, maxdistance, refdistance );
}

/*
* SF_SetEntitySpatialization
*/
void SF_SetEntitySpatialization( int entnum, const vec3_t origin, const vec3_t velocity ) {
	smdCmdSpatialization_t *spat;

	if( s_num_ent_spats == s_max_ent_spats ) {
		// flush all spatializations at once to free room
		S_IssueSetMulEntitySpatializationCmd( s_cmdPipe, s_num_ent_spats, s_ent_spats );
		s_num_ent_spats = 0;
	}

	spat = &s_ent_spats[s_num_ent_spats++];
	spat->entnum = entnum;
	VectorCopy( origin, spat->origin );
	VectorCopy( velocity, spat->velocity );
}

/*
* SF_StartFixedSound
*/
void SF_StartFixedSound( sfx_t *sfx, const vec3_t origin, int channel, float fvol, float attenuation ) {
	if( sfx != NULL ) {
		S_IssueStartFixedSoundCmd( s_cmdPipe, sfx - known_sfx, origin, channel, fvol, attenuation );
	}
}

/*
* SF_StartRelativeSound
*/
void SF_StartRelativeSound( sfx_t *sfx, int entnum, int channel, float fvol, float attenuation ) {
	if( sfx != NULL ) {
		S_IssueStartRelativeSoundCmd( s_cmdPipe, sfx - known_sfx, entnum, channel, fvol, attenuation );
	}
}

/*
* SF_StartGlobalSound
*/
void SF_StartGlobalSound( sfx_t *sfx, int channel, float fvol ) {
	if( sfx != NULL ) {
		S_IssueStartGlobalSoundCmd( s_cmdPipe, sfx - known_sfx, channel, fvol );
	}
}

/*
* SF_StartLocalSoundByName
*/
void SF_StartLocalSoundByName( const char *sound ) {
	sfx_t *sfx;

	sfx = SF_RegisterSound( sound );
	if( !sfx ) {
		Com_Printf( "S_StartLocalSoundByName: can't cache %s\n", sound );
		return;
	}

	S_IssueStartLocalSoundCmd( s_cmdPipe, sfx - known_sfx, 1.0f );
}

/*
* SF_StartLocalSound
*/
void SF_StartLocalSound( sfx_t *sfx, float fvol ) {
	if( sfx != NULL ) {
		S_IssueStartLocalSoundCmd( s_cmdPipe, sfx - known_sfx, fvol );
	}
}

/*
* SF_Clear
*/
void SF_Clear( void ) {
	S_IssueClearCmd( s_cmdPipe );
}

/*
* SF_AddLoopSound
*/
void SF_AddLoopSound( sfx_t *sfx, int entnum, float fvol, float attenuation ) {
	if( sfx != NULL ) {
		S_IssueAddLoopSoundCmd( s_cmdPipe, sfx - known_sfx, entnum, fvol, attenuation );
	}
}

/*
* SF_Update
*/
void SF_Update( const vec3_t origin, const vec3_t velocity, const mat3_t axis, bool avidump ) {
	if( s_num_ent_spats ) {
		S_IssueSetMulEntitySpatializationCmd( s_cmdPipe, s_num_ent_spats, s_ent_spats );
		s_num_ent_spats = 0;
	}

	S_IssueSetListenerCmd( s_cmdPipe, origin, velocity, axis, avidump );
}

/*
* SF_RawSamples
*/
void SF_RawSamples( unsigned int samples, unsigned int rate, unsigned short width,
					unsigned short channels, const uint8_t *data, bool music ) {
	size_t data_size = samples * width * channels;
	uint8_t *data_copy = S_Malloc( data_size );

	memcpy( data_copy, data, data_size );

	S_IssueRawSamplesCmd( s_cmdPipe, samples, rate, width, channels, data_copy, music );
}

/*
* SF_PositionedRawSamples
*/
void SF_PositionedRawSamples( int entnum, float fvol, float attenuation,
							  unsigned int samples, unsigned int rate,
							  unsigned short width, unsigned short channels, const uint8_t *data ) {
	size_t data_size = samples * width * channels;
	uint8_t *data_copy = S_Malloc( data_size );

	memcpy( data_copy, data, data_size );

	S_IssuePositionedRawSamplesCmd( s_cmdPipe, entnum, fvol, attenuation,
									samples, rate, width, channels, data_copy );
}

// =====================================================================

/*
* S_API
*/
int S_API( void ) {
	return SOUND_API_VERSION;
}

/*
* S_Error
*/
void S_Error( const char *format, ... ) {
	va_list argptr;
	char msg[1024];

	va_start( argptr, format );
	Q_vsnprintfz( msg, sizeof( msg ), format, argptr );
	va_end( argptr );

	trap_Error( msg );
}

#ifndef SOUND_HARD_LINKED

// this is only here so the functions in q_shared.c and q_math.c can link
void Sys_Error( const char *format, ... ) {
	va_list argptr;
	char msg[3072];

	va_start( argptr, format );
	Q_vsnprintfz( msg, sizeof( msg ), format, argptr );
	va_end( argptr );

	trap_Error( msg );
}

void Com_Printf( const char *format, ... ) {
	va_list argptr;
	char msg[3072];

	va_start( argptr, format );
	Q_vsnprintfz( msg, sizeof( msg ), format, argptr );
	va_end( argptr );

	trap_Print( msg );
}

#if defined ( HAVE_DLLMAIN )
int WINAPI DLLMain( void *hinstDll, unsigned long dwReason, void *reserved ) {
	return 1;
}
#endif

#endif
