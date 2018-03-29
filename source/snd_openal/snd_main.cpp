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

#include "snd_local.h"
#include "snd_cmdque.h"
#include "snd_env_sampler.h"

#ifndef __cplusplus
extern C {
#endif

static sndCmdPipe_t *s_cmdPipe;

static struct qthread_s *s_backThread;

struct mempool_s *soundpool;

cvar_t *s_volume;
cvar_t *s_musicvolume;
cvar_t *s_openAL_device;

cvar_t *s_doppler;
cvar_t *s_sound_velocity;
cvar_t *s_environment_effects;
cvar_t *s_environment_sampling_quality;
cvar_t *s_environment_effects_scale;
cvar_t *s_hrtf;
cvar_t *s_attenuate_on_obstruction;
cvar_t *s_stereo2mono;
cvar_t *s_globalfocus;

static int s_registration_sequence = 1;
static bool s_registering;

// batch entity spatializations
static unsigned s_num_ent_spats;
static smdCmdSpatialization_t s_ent_spats[SND_SPATIALIZE_ENTS_MAX];
static const unsigned s_max_ent_spats = sizeof( s_ent_spats ) / sizeof( s_ent_spats[0] );

static void SF_UnregisterSound( sfx_t *sfx );
static void SF_FreeSound( sfx_t *sfx );

/*
* Commands
*/

#ifdef ENABLE_PLAY
static void SF_Play_f( void ) {
	int i;
	char name[MAX_QPATH];

	i = 1;
	while( i < trap_Cmd_Argc() ) {
		Q_strncpyz( name, trap_Cmd_Argv( i ), sizeof( name ) );

		S_StartLocalSound( name );
		i++;
	}
}
#endif // ENABLE_PLAY

/*
* SF_Music
*/
static void SF_Music_f( void ) {
	if( trap_Cmd_Argc() == 2 ) {
		SF_StartBackgroundTrack( trap_Cmd_Argv( 1 ), trap_Cmd_Argv( 1 ), 0 );
	} else if( trap_Cmd_Argc() == 3 ) {
		SF_StartBackgroundTrack( trap_Cmd_Argv( 1 ), trap_Cmd_Argv( 2 ), 0 );
	} else {
		Com_Printf( "music <intro|playlist> [loop|shuffle]\n" );
		return;
	}
}

/*
* SF_SoundList
*/
static void SF_SoundList_f( void ) {
	S_IssueStuffCmd( s_cmdPipe, "soundlist" );
}

/*
* SF_ListDevices_f
*/
static void SF_ListDevices_f( void ) {
	S_IssueStuffCmd( s_cmdPipe, "devicelist" );
}

/*
* SF_Init
*/
bool SF_Init( void *hwnd, int maxEntities, bool verbose ) {
	soundpool = S_MemAllocPool( "OpenAL sound module" );

	s_num_ent_spats = 0;

#ifdef OPENAL_RUNTIME
	if( !QAL_Init( ALDRIVER, verbose ) ) {
#ifdef ALDRIVER_ALT
		if( !QAL_Init( ALDRIVER_ALT, verbose ) )
#endif
		{
			Com_Printf( "Failed to load OpenAL library: %s\n", ALDRIVER );
			return false;
		}
	}
#endif

	s_volume = trap_Cvar_Get( "s_volume", "0.8", CVAR_ARCHIVE );
	s_musicvolume = trap_Cvar_Get( "s_musicvolume", "0.05", CVAR_ARCHIVE );
	s_doppler = trap_Cvar_Get( "s_doppler", "1.0", CVAR_ARCHIVE );
	s_sound_velocity = trap_Cvar_Get( "s_sound_velocity", "10976", CVAR_DEVELOPER );
	s_stereo2mono = trap_Cvar_Get( "s_stereo2mono", "0", CVAR_ARCHIVE );
	s_globalfocus = trap_Cvar_Get( "s_globalfocus", "0", CVAR_ARCHIVE );

	s_environment_effects = trap_Cvar_Get( "s_environment_effects", "1", CVAR_ARCHIVE | CVAR_LATCH_SOUND );
	if( !QAL_Is_EFX_ExtensionSupported() ) {
		trap_Cvar_ForceSet( s_environment_effects->name, "0" );
	}
	s_environment_sampling_quality = trap_Cvar_Get( "s_environment_sampling_quality", "0.5", CVAR_ARCHIVE );
	if ( s_environment_sampling_quality->value < 0 || s_environment_sampling_quality->value > 1.0f ) {
		trap_Cvar_ForceSet( s_environment_sampling_quality->name, "0.5" );
	}
	s_environment_effects_scale = trap_Cvar_Get( "s_environment_effects_scale", "0.5", CVAR_ARCHIVE );
	if( s_environment_effects_scale->value < 0 || s_environment_effects_scale->value > 1.0f ) {
		trap_Cvar_ForceSet( s_environment_effects_scale->name, "0.5" );
	}
	s_hrtf = trap_Cvar_Get( "s_hrtf", "1", CVAR_ARCHIVE | CVAR_LATCH_SOUND );
	s_attenuate_on_obstruction = trap_Cvar_Get( "s_attenuate_on_obstruction", "1", CVAR_ARCHIVE );

	ENV_Init();

#ifdef ENABLE_PLAY
	trap_Cmd_AddCommand( "play", SF_Play_f );
#endif
	trap_Cmd_AddCommand( "music", SF_Music_f );
	trap_Cmd_AddCommand( "stopmusic", SF_StopBackgroundTrack );
	trap_Cmd_AddCommand( "prevmusic", SF_PrevBackgroundTrack );
	trap_Cmd_AddCommand( "nextmusic", SF_NextBackgroundTrack );
	trap_Cmd_AddCommand( "pausemusic", SF_PauseBackgroundTrack );
	trap_Cmd_AddCommand( "soundlist", SF_SoundList_f );
	trap_Cmd_AddCommand( "s_devices", SF_ListDevices_f );

	s_cmdPipe = S_CreateSoundCmdPipe();
	if( !s_cmdPipe ) {
		return false;
	}

	s_backThread = trap_Thread_Create( S_BackgroundUpdateProc, s_cmdPipe );

	S_IssueInitCmd( s_cmdPipe, hwnd, maxEntities, verbose );

	S_FinishSoundCmdPipe( s_cmdPipe );

	if( !alContext ) {
		return false;
	}

	S_InitBuffers();

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

	// wake up the mixer
	SF_Activate( true );

	// wait for the queue to be processed
	S_FinishSoundCmdPipe( s_cmdPipe );

	S_ShutdownBuffers();

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
	trap_Cmd_RemoveCommand( "stopmusic" );
	trap_Cmd_RemoveCommand( "prevmusic" );
	trap_Cmd_RemoveCommand( "nextmusic" );
	trap_Cmd_RemoveCommand( "pausemusic" );
	trap_Cmd_RemoveCommand( "soundlist" );
	trap_Cmd_RemoveCommand( "s_devices" );

	ENV_Shutdown();

	QAL_Shutdown();

	S_MemFreePool( &soundpool );
}

void SF_BeginRegistration( void ) {
	s_registration_sequence++;
	if( !s_registration_sequence ) {
		s_registration_sequence = 1;
	}
	s_registering = true;

	// wait for the queue to be processed
	S_FinishSoundCmdPipe( s_cmdPipe );
}

void SF_EndRegistration( void ) {
	// wait for the queue to be processed
	S_FinishSoundCmdPipe( s_cmdPipe );

	S_ForEachBuffer( SF_UnregisterSound );

	// wait for the queue to be processed
	S_FinishSoundCmdPipe( s_cmdPipe );

	S_ForEachBuffer( SF_FreeSound );

	s_registering = false;

}

/*
* SF_RegisterSound
*/
sfx_t *SF_RegisterSound( const char *name ) {
	sfx_t *sfx;

	assert( name );

	sfx = S_FindBuffer( name );
	S_IssueLoadSfxCmd( s_cmdPipe, sfx->id );
	sfx->used = trap_Milliseconds();
	sfx->registration_sequence = s_registration_sequence;
	return sfx;
}

/*
* SF_UnregisterSound
*/
static void SF_UnregisterSound( sfx_t *sfx ) {
	if( sfx->filename[0] == '\0' ) {
		return;
	}
	if( sfx->registration_sequence != s_registration_sequence ) {
		S_IssueFreeSfxCmd( s_cmdPipe, sfx->id );
	}
}

/*
* SF_FreeSound
*/
static void SF_FreeSound( sfx_t *sfx ) {
	if( !sfx->registration_sequence ) {
		return;
	}
	if( sfx->registration_sequence != s_registration_sequence ) {
		S_MarkBufferFree( sfx );
	}
}

/*
* SF_Activate
*/
void SF_Activate( bool active ) {
	if( !active && s_globalfocus->integer ) {
		return;
	}

	SF_LockBackgroundTrack( !active );

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
		S_IssueStartFixedSoundCmd( s_cmdPipe, sfx->id, origin, channel, fvol, attenuation );
	}
}

/*
* SF_StartRelativeSound
*/
void SF_StartRelativeSound( sfx_t *sfx, int entnum, int channel, float fvol, float attenuation ) {
	if( sfx != NULL ) {
		S_IssueStartRelativeSoundCmd( s_cmdPipe, sfx->id, entnum, channel, fvol, attenuation );
	}
}

/*
* SF_StartGlobalSound
*/
void SF_StartGlobalSound( sfx_t *sfx, int channel, float fvol ) {
	if( sfx != NULL ) {
		S_IssueStartGlobalSoundCmd( s_cmdPipe, sfx->id, channel, fvol );
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

	S_IssueStartLocalSoundCmd( s_cmdPipe, sfx->id, 1.0f );
}

/*
* SF_StartLocalSound
*/
void SF_StartLocalSound( sfx_t *sfx, float fvol ) {
	if( sfx != NULL ) {
		S_IssueStartLocalSoundCmd( s_cmdPipe, sfx->id, fvol );
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
		S_IssueAddLoopSoundCmd( s_cmdPipe, sfx->id, entnum, fvol, attenuation );
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
	uint8_t *data_copy = (uint8_t *)S_Malloc( data_size );

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
	uint8_t *data_copy = (uint8_t *)S_Malloc( data_size );

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

#ifndef __cplusplus
extern C {
#endif

#endif
