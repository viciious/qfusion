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

ALCdevice *alDevice = NULL;
ALCcontext *alContext = NULL;

#define UPDATE_MSEC 10
static int64_t s_last_update_time;

int s_attenuation_model = 0;
float s_attenuation_maxdistance = 0;
float s_attenuation_refdistance = 0;

static bool snd_shutdown_bug = false;

/*
* Commands
*/

/*
* S_ListDevices_f
*/
static void S_ListDevices_f( void ) {
	char *device, *defaultDevice, *curDevice;

	Com_Printf( "Available OpenAL devices:\n" );

	defaultDevice = ( char * )qalcGetString( NULL, ALC_DEFAULT_DEVICE_SPECIFIER );
	curDevice = ( char * )qalcGetString( alDevice, ALC_DEVICE_SPECIFIER );
	device = ( char * )qalcGetString( NULL, ALC_DEVICE_SPECIFIER );

	for( ; *device; device += strlen( device ) + 1 ) {
		if( defaultDevice && !strcmp( device, defaultDevice ) ) {
			Com_Printf( "(def) : " );
		} else if( curDevice && !strcmp( device, curDevice ) ) {
			Com_Printf( "(cur) : " );
		} else {
			Com_Printf( "      : " );
		}

		Com_Printf( "%s\n", device );
	}
}

/*
* S_SoundFormat
*/
ALuint S_SoundFormat( int width, int channels ) {
	if( width == 1 ) {
		if( channels == 1 ) {
			return AL_FORMAT_MONO8;
		} else if( channels == 2 ) {
			return AL_FORMAT_STEREO8;
		}
	} else if( width == 2 ) {
		if( channels == 1 ) {
			return AL_FORMAT_MONO16;
		} else if( channels == 2 ) {
			return AL_FORMAT_STEREO16;
		}
	}

	Com_Printf( "Unknown sound format: %i channels, %i bits.\n", channels, width * 8 );
	return AL_FORMAT_MONO16;
}

/*
* S_GetBufferLength
*
* Returns buffer length expressed in milliseconds
*/
ALuint S_GetBufferLength( ALuint buffer ) {
	ALint size, bits, channels, freq;

	qalGetBufferi( buffer, AL_SIZE, &size );
	qalGetBufferi( buffer, AL_BITS, &bits );
	qalGetBufferi( buffer, AL_FREQUENCY, &freq );
	qalGetBufferi( buffer, AL_CHANNELS, &channels );

	if( qalGetError() != AL_NO_ERROR ) {
		return 0;
	}
	return (ALuint)( (ALfloat)( size / ( bits / 8 ) / channels ) * 1000.0 / freq + 0.5f );
}

/*
* S_ErrorMessage
*/
const char *S_ErrorMessage( ALenum error ) {
	switch( error ) {
		case AL_NO_ERROR:
			return "No error";
		case AL_INVALID_NAME:
			return "Invalid name";
		case AL_INVALID_ENUM:
			return "Invalid enumerator";
		case AL_INVALID_VALUE:
			return "Invalid value";
		case AL_INVALID_OPERATION:
			return "Invalid operation";
		case AL_OUT_OF_MEMORY:
			return "Out of memory";
		default:
			return "Unknown error";
	}
}

#define DECLARE_REVERB_PARAM( param, member, minValue, maxValue, defaultValue ) \
	{ #param, param, offsetof( reverbProps_t, member ), minValue, maxValue, defaultValue }

const reverbParamDef_t reverbParamsDefs[NUM_REVERB_PARAMS] = {
	DECLARE_REVERB_PARAM( AL_REVERB_DENSITY, density, 0.0f, 1.0f, 1.0f ),
	DECLARE_REVERB_PARAM( AL_REVERB_DIFFUSION, diffusion, 0.0f, 1.0f, 1.0f ),
	DECLARE_REVERB_PARAM( AL_REVERB_GAIN, gain, 0.0f, 1.0f, 0.32f ),
	DECLARE_REVERB_PARAM( AL_REVERB_GAINHF, gainHf, 0.0f, 1.0f, 0.89f ),
	DECLARE_REVERB_PARAM( AL_REVERB_DECAY_TIME, decayTime, 0.1f, 20.0f, 1.49f ),
	DECLARE_REVERB_PARAM( AL_REVERB_REFLECTIONS_GAIN, reflectionsGain, 0.0f, 3.16f, 0.05f ),
	DECLARE_REVERB_PARAM( AL_REVERB_REFLECTIONS_DELAY, reflectionsDelay, 0.0f, 0.3f, 0.007f ),
	DECLARE_REVERB_PARAM( AL_REVERB_LATE_REVERB_GAIN, lateReverbGain, 0.0f, 10.0f, 1.26f ),
	DECLARE_REVERB_PARAM( AL_REVERB_LATE_REVERB_DELAY, lateReverbDelay, 0.0f, 0.1f, 0.011f )
};

/*
* S_Init
*/
static bool S_Init( void *hwnd, int maxEntities, bool verbose ) {
	int numDevices;
	int userDeviceNum = -1;
	char *devices, *defaultDevice;
	cvar_t *s_openAL_device;
	int attrList[6];
	int *attrPtr;

	alDevice = NULL;
	alContext = NULL;

	s_last_update_time = 0;

	// get system default device identifier
	defaultDevice = ( char * )qalcGetString( NULL, ALC_DEFAULT_DEVICE_SPECIFIER );
	if( !defaultDevice ) {
		Com_Printf( "Failed to get openAL default device\n" );
		return false;
	}

	s_openAL_device = trap_Cvar_Get( "s_openAL_device", ALDEVICE_DEFAULT ? ALDEVICE_DEFAULT : defaultDevice, CVAR_ARCHIVE | CVAR_LATCH_SOUND );

	devices = ( char * )qalcGetString( NULL, ALC_DEVICE_SPECIFIER );
	for( numDevices = 0; *devices; devices += strlen( devices ) + 1, numDevices++ ) {
		if( !Q_stricmp( s_openAL_device->string, devices ) ) {
			userDeviceNum = numDevices;

			// force case sensitive
			if( strcmp( s_openAL_device->string, devices ) ) {
				trap_Cvar_ForceSet( "s_openAL_device", devices );
			}
		}
	}

	if( !numDevices ) {
		Com_Printf( "Failed to get openAL devices\n" );
		return false;
	}

	// the device assigned by the user is not available
	if( userDeviceNum == -1 ) {
		Com_Printf( "'s_openAL_device': incorrect device name, reseting to default\n" );

		trap_Cvar_ForceSet( "s_openAL_device", ALDEVICE_DEFAULT ? ALDEVICE_DEFAULT : defaultDevice );

		devices = ( char * )qalcGetString( NULL, ALC_DEVICE_SPECIFIER );
		for( numDevices = 0; *devices; devices += strlen( devices ) + 1, numDevices++ ) {
			if( !Q_stricmp( s_openAL_device->string, devices ) ) {
				userDeviceNum = numDevices;
			}
		}

		if( userDeviceNum == -1 ) {
			trap_Cvar_ForceSet( "s_openAL_device", defaultDevice );
		}
	}

	alDevice = qalcOpenDevice( (const ALchar *)s_openAL_device->string );
	if( !alDevice ) {
		Com_Printf( "Failed to open device\n" );
		return false;
	}

	attrPtr = &attrList[0];
	if( QAL_Is_EFX_ExtensionSupported() && s_environment_effects->integer ) {
		// We limit each source to a single "auxiliary send" for optimization purposes.
		// This means each source has a single auxiliary output that feeds an effect aside from a direct output.
		*attrPtr++ = ALC_MAX_AUXILIARY_SENDS;
		*attrPtr++ = 1;
	}

	if( QAL_Is_HRTF_ExtensionSupported() ) {
		*attrPtr++ = ALC_HRTF_SOFT;
		*attrPtr++ = s_hrtf->integer ? 1 : 0;
	} else if( s_hrtf->integer ) {
		Com_Printf( S_COLOR_YELLOW "Warning: HRTF support has not been found.\n" );
		// HRTF still might be forced by some other way (e.g. by some proprietary runtime).
		Com_Printf( S_COLOR_YELLOW "HRTF effects will not be guaranteed.\n" );
		Com_Printf( S_COLOR_YELLOW "HRTF feature requires the recent version of OpenAL SOFT runtime.\n" );
		trap_Cvar_ForceSet( s_hrtf->name, "0" );
	}

	// Terminate the attributes pairs list
	*attrPtr++ = 0;
	*attrPtr++ = 0;

	// Create context
	alContext = qalcCreateContext( alDevice, attrList );
	if( !alContext ) {
		Com_Printf( "Failed to create context\n" );
		return false;
	}
	qalcMakeContextCurrent( alContext );

	if( verbose ) {
		Com_Printf( "OpenAL initialized\n" );

		if( numDevices ) {
			int i;

			Com_Printf( "  Devices:    " );

			devices = ( char * )qalcGetString( NULL, ALC_DEVICE_SPECIFIER );
			for( i = 0; *devices; devices += strlen( devices ) + 1, i++ )
				Com_Printf( "%s%s", devices, ( i < numDevices - 1 ) ? ", " : "" );
			Com_Printf( "\n" );

			if( defaultDevice && *defaultDevice ) {
				Com_Printf( "  Default system device: %s\n", defaultDevice );
			}

			Com_Printf( "\n" );
		}

		Com_Printf( "  Device:       %s\n", qalcGetString( alDevice, ALC_DEVICE_SPECIFIER ) );
		Com_Printf( "  Vendor:       %s\n", qalGetString( AL_VENDOR ) );
		Com_Printf( "  Version:      %s\n", qalGetString( AL_VERSION ) );
		Com_Printf( "  Renderer:     %s\n", qalGetString( AL_RENDERER ) );
		Com_Printf( "  EFX support:  %s\n", QAL_Is_EFX_ExtensionSupported() ? "yes" : "no" );
		Com_Printf( "  HRTF support: %s\n", QAL_Is_HRTF_ExtensionSupported() ? "yes" : "no" );
		Com_Printf( "  Extensions:   %s\n", qalGetString( AL_EXTENSIONS ) );
	}

	// Check for Linux shutdown race condition
	if( !Q_stricmp( qalGetString( AL_VENDOR ), "J. Valenzuela" ) ) {
		snd_shutdown_bug = true;
	}

	qalDopplerFactor( s_doppler->value );
	// Defer s_sound_velocity application to S_Update() in order to avoid code duplication
	s_sound_velocity->modified = true;
	s_doppler->modified = false;

	S_SetAttenuationModel( S_DEFAULT_ATTENUATION_MODEL, S_DEFAULT_ATTENUATION_MAXDISTANCE, S_DEFAULT_ATTENUATION_REFDISTANCE );

	S_LockBackgroundTrack( false );

	if( !S_InitDecoders( verbose ) ) {
		Com_Printf( "Failed to init decoders\n" );
		return false;
	}
	if( !S_InitSources( maxEntities, verbose ) ) {
		Com_Printf( "Failed to init sources\n" );
		return false;
	}

	// If we have reached this condition, EFX support is guaranteed.
	if( s_environment_effects->integer ) {
		// Set the value once
		qalListenerf( AL_METERS_PER_UNIT, QF_METERS_PER_UNIT );
		if( qalGetError() != AL_NO_ERROR ) {
			return false;
		}
	}

	return true;
}

/*
* S_Shutdown
*/
static void S_Shutdown( bool verbose ) {
	S_StopStreams();
	S_LockBackgroundTrack( false );
	S_StopBackgroundTrack();

	S_ShutdownSources();
	S_ShutdownDecoders( verbose );

	if( alContext ) {
		if( !snd_shutdown_bug ) {
			qalcMakeContextCurrent( NULL );
		}

		qalcDestroyContext( alContext );
		alContext = NULL;
	}

	if( alDevice ) {
		qalcCloseDevice( alDevice );
		alDevice = NULL;
	}
}

/*
* S_SetAttenuationModel
*/
void S_SetAttenuationModel( int model, float maxdistance, float refdistance ) {
	s_attenuation_model = model;
	s_attenuation_maxdistance = maxdistance;
	s_attenuation_refdistance = refdistance;

	switch( model ) {
		case 0:
			qalDistanceModel( AL_LINEAR_DISTANCE );
			break;
		case 1:
		default:
			qalDistanceModel( AL_LINEAR_DISTANCE_CLAMPED );
			break;
		case 2:
			qalDistanceModel( AL_INVERSE_DISTANCE );
			break;
		case 3:
			qalDistanceModel( AL_INVERSE_DISTANCE_CLAMPED );
			break;
		case 4:
			qalDistanceModel( AL_EXPONENT_DISTANCE );
			break;
		case 5:
			qalDistanceModel( AL_EXPONENT_DISTANCE_CLAMPED );
			break;
	}
}

/*
* S_SetListener
*/
static void S_SetListener( const vec3_t origin, const vec3_t velocity, const mat3_t axis ) {
	float orientation[6];

	orientation[0] = axis[AXIS_FORWARD + 0];
	orientation[1] = axis[AXIS_FORWARD + 1];
	orientation[2] = axis[AXIS_FORWARD + 2];
	orientation[3] = axis[AXIS_UP + 0];
	orientation[4] = axis[AXIS_UP + 1];
	orientation[5] = axis[AXIS_UP + 2];

	qalListenerfv( AL_POSITION, origin );
	qalListenerfv( AL_VELOCITY, velocity );
	qalListenerfv( AL_ORIENTATION, orientation );

	ENV_UpdateListener( origin, velocity );
}

/*
* S_Update
*/
static void S_Update( void ) {
	S_UpdateMusic();

	S_UpdateStreams();

	s_volume->modified = false; // Checked by src and stream
	s_musicvolume->modified = false; // Checked by stream and music

	if( s_doppler->modified ) {
		if( s_doppler->value > 0.0f ) {
			qalDopplerFactor( s_doppler->value );
		} else {
			qalDopplerFactor( 0.0f );
		}
		s_doppler->modified = false;
	}

	if( s_sound_velocity->modified ) {
		// If environment effects are supported, we can set units to meters ratio.
		// In this case, we have to scale the hardcoded s_sound_velocity value.
		// (The engine used to assume that units to meters ratio is 1).
		float appliedVelocity = s_sound_velocity->value;
		if( appliedVelocity <= 0.0f ) {
			appliedVelocity = 0.0f;
		} else if( s_environment_effects->integer ) {
			appliedVelocity *= QF_METERS_PER_UNIT;
		}
		qalDopplerVelocity( appliedVelocity );
		if( qalSpeedOfSound ) {
			qalSpeedOfSound( appliedVelocity );
		}
		s_sound_velocity->modified = false;
	}
}

/*
* S_StopAllSounds
*/
void S_StopAllSounds( bool stopMusic ) {
	S_StopStreams();
	S_StopAllSources();
	if( stopMusic ) {
		S_StopBackgroundTrack();
	}
}

/*
* S_Activate
*/
void S_Activate( bool activate ) {
	S_LockBackgroundTrack( !activate );

	// TODO: Actually stop playing sounds while not active?
	if( activate ) {
		qalListenerf( AL_GAIN, 1 );
	} else {
		qalListenerf( AL_GAIN, 0 );
	}
}

/*
* S_BeginAviDemo
*/
void S_BeginAviDemo( void ) {
}

/*
* S_StopAviDemo
*/
void S_StopAviDemo( void ) {
}

// =====================================================================

/*
* S_HandleInitCmd
*/
static unsigned S_HandleInitCmd( const sndCmdInit_t *cmd ) {
	//Com_Printf("S_HandleShutdownCmd\n");
	S_Init( cmd->hwnd, cmd->maxents, cmd->verbose );
	return sizeof( *cmd );
}

/*
* S_HandleShutdownCmd
*/
static unsigned S_HandleShutdownCmd( const sndCmdShutdown_t *cmd ) {
	//Com_Printf("S_HandleShutdownCmd\n");
	S_Shutdown( cmd->verbose );
	return 0; // terminate
}

/*
* S_HandleClearCmd
*/
static unsigned S_HandleClearCmd( const sndCmdClear_t *cmd ) {
	//Com_Printf("S_HandleClearCmd\n");
	S_Clear();
	return sizeof( *cmd );
}

/*
* S_HandleStopCmd
*/
static unsigned S_HandleStopCmd( const sndCmdStop_t *cmd ) {
	//Com_Printf("S_HandleStopCmd\n");
	S_StopAllSounds( cmd->stopMusic );
	return sizeof( *cmd );
}

/*
* S_HandleFreeSfxCmd
*/
static unsigned S_HandleFreeSfxCmd( const sndCmdFreeSfx_t *cmd ) {
	//Com_Printf("S_HandleFreeSfxCmd\n");
	S_UnloadBuffer( S_GetBufferById( cmd->sfx ) );
	return sizeof( *cmd );
}

/*
* S_HandleLoadSfxCmd
*/
static unsigned S_HandleLoadSfxCmd( const sndCmdLoadSfx_t *cmd ) {
	//Com_Printf("S_HandleLoadSfxCmd\n");
	S_LoadBuffer( S_GetBufferById( cmd->sfx ) );
	return sizeof( *cmd );
}

/*
* S_HandleSetAttenuationModelCmd
*/
static unsigned S_HandleSetAttenuationModelCmd( const sndCmdSetAttenuationModel_t *cmd ) {
	//Com_Printf("S_HandleSetAttenuationModelCmd\n");
	S_SetAttenuationModel( cmd->model, cmd->maxdistance, cmd->refdistance );
	return sizeof( *cmd );
}

/*
* S_HandleSetEntitySpatializationCmd
*/
static unsigned S_HandleSetEntitySpatializationCmd( const sndCmdSetEntitySpatialization_t *cmd ) {
	//Com_Printf("S_HandleSetEntitySpatializationCmd\n");
	S_SetEntitySpatialization( cmd->entnum, cmd->origin, cmd->velocity );
	return sizeof( *cmd );
}

/*
* S_HandleSetListernerCmd
*/
static unsigned S_HandleSetListernerCmd( const sndCmdSetListener_t *cmd ) {
	//Com_Printf("S_HandleSetListernerCmd\n");
	S_SetListener( cmd->origin, cmd->velocity, cmd->axis );
	S_UpdateSources();
	return sizeof( *cmd );
}

/*
* S_HandleStartLocalSoundCmd
*/
static unsigned S_HandleStartLocalSoundCmd( const sndCmdStartLocalSound_t *cmd ) {
	//Com_Printf("S_HandleStartFixedSoundCmd\n");
	S_StartLocalSound( S_GetBufferById( cmd->sfx ), cmd->fvol );
	return sizeof( *cmd );
}

/*
* S_HandleStartFixedSoundCmd
*/
static unsigned S_HandleStartFixedSoundCmd( const sndCmdStartFixedSound_t *cmd ) {
	//Com_Printf("S_HandleStartFixedSoundCmd\n");
	S_StartFixedSound( S_GetBufferById( cmd->sfx ), cmd->origin, cmd->channel, cmd->fvol, cmd->attenuation );
	return sizeof( *cmd );
}

/*
* S_HandleStartRelativeSoundCmd
*/
static unsigned S_HandleStartRelativeSoundCmd( const sndCmdStartRelativeSound_t *cmd ) {
	//Com_Printf("S_HandleStartRelativeSoundCmd\n");
	S_StartRelativeSound( S_GetBufferById( cmd->sfx ), cmd->entnum, cmd->channel, cmd->fvol, cmd->attenuation );
	return sizeof( *cmd );
}

/*
* S_HandleStartGlobalSoundCmd
*/
static unsigned S_HandleStartGlobalSoundCmd( const sndCmdStartGlobalSound_t *cmd ) {
	//Com_Printf("S_HandleStartGlobalSoundCmd\n");
	S_StartGlobalSound( S_GetBufferById( cmd->sfx ), cmd->channel, cmd->fvol );
	return sizeof( *cmd );
}

/*
* S_HandleStartBackgroundTrackCmd
*/
static unsigned S_HandleStartBackgroundTrackCmd( const sndCmdStartBackgroundTrack_t *cmd ) {
	//Com_Printf("S_HandleStartBackgroundTrackCmd\n");
	S_StartBackgroundTrack( cmd->intro, cmd->loop, cmd->mode );
	return sizeof( *cmd );
}

/*
* S_HandleStopBackgroundTrackCmd
*/
static unsigned S_HandleStopBackgroundTrackCmd( const sndCmdStopBackgroundTrack_t *cmd ) {
	//Com_Printf("S_HandleStopBackgroundTrackCmd\n");
	S_StopBackgroundTrack();
	return sizeof( *cmd );
}

/*
* S_HandleLockBackgroundTrackCmd
*/
static unsigned S_HandleLockBackgroundTrackCmd( const sndCmdLockBackgroundTrack_t *cmd ) {
	//Com_Printf("S_HandleLockBackgroundTrackCmd\n");
	S_LockBackgroundTrack( cmd->lock );
	return sizeof( *cmd );
}

/*
* S_HandleAddLoopSoundCmd
*/
static unsigned S_HandleAddLoopSoundCmd( const sndAddLoopSoundCmd_t *cmd ) {
	//Com_Printf("S_HandleAddLoopSoundCmd\n");
	S_AddLoopSound( S_GetBufferById( cmd->sfx ), cmd->entnum, cmd->fvol, cmd->attenuation );
	return sizeof( *cmd );
}

/*
* S_HandleAdvanceBackgroundTrackCmd
*/
static unsigned S_HandleAdvanceBackgroundTrackCmd( const sndAdvanceBackgroundTrackCmd_t *cmd ) {
	//Com_Printf("S_HandleAdvanceBackgroundTrackCmd\n");
	if( cmd->val < 0 ) {
		S_PrevBackgroundTrack();
	} else if( cmd->val > 0 ) {
		S_NextBackgroundTrack();
	}
	return sizeof( *cmd );
}

/*
* S_HandlePauseBackgroundTrackCmd
*/
static unsigned S_HandlePauseBackgroundTrackCmd( const sndPauseBackgroundTrackCmd_t *cmd ) {
	//Com_Printf("S_HandlePauseBackgroundTrackCmd\n");
	S_PauseBackgroundTrack();
	return sizeof( *cmd );
}

/*
* S_HandleActivateCmd
*/
static unsigned S_HandleActivateCmd( const sndActivateCmd_t *cmd ) {
	//Com_Printf("S_HandleActivateCmd\n");

	S_Clear();

	S_Activate( cmd->active ? true : false );

	return sizeof( *cmd );
}

/*
* S_HandleAviDemoCmd
*/
static unsigned S_HandleAviDemoCmd( const sndAviDemo_t *cmd ) {
	if( cmd->begin ) {
		S_BeginAviDemo();
	} else {
		S_StopAviDemo();
	}
	return sizeof( *cmd );
}

/*
* S_HandleRawSamplesCmd
*/
static unsigned S_HandleRawSamplesCmd( const sndRawSamplesCmd_t *cmd ) {
	S_RawSamples( cmd->samples, cmd->rate, cmd->width, cmd->channels,
				  cmd->data, cmd->music );
	S_Free( ( void * )cmd->data );
	return sizeof( *cmd );
}

/*
* S_HandlePositionedRawSamplesCmd
*/
static unsigned S_HandlePositionedRawSamplesCmd( const sndPositionedRawSamplesCmd_t *cmd ) {
	S_PositionedRawSamples( cmd->entnum, cmd->fvol, cmd->attenuation,
							cmd->samples, cmd->rate, cmd->width, cmd->channels, cmd->data );
	S_Free( ( void * )cmd->data );
	return sizeof( *cmd );
}

/*
* S_HandleStuffCmd
*/
static unsigned S_HandleStuffCmd( const sndStuffCmd_t *cmd ) {
	if( !Q_stricmp( cmd->text, "soundlist" ) ) {
		S_SoundList_f();
	} else if( !Q_stricmp( cmd->text, "devicelist" ) ) {
		S_ListDevices_f();
	}
	return sizeof( *cmd );
}

/*
* S_HandleSetMulEntitySpatializationCmd
*/
static unsigned S_HandleSetMulEntitySpatializationCmd( const sndCmdSetMulEntitySpatialization_t *cmd ) {
	unsigned i;
	//Com_Printf("S_HandleSetEntitySpatializationCmd\n");
	for( i = 0; i < cmd->numents; i++ )
		S_SetEntitySpatialization( cmd->entnum[i], cmd->origin[i], cmd->velocity[i] );
	return sizeof( *cmd );
}

static pipeCmdHandler_t sndCmdHandlers[SND_CMD_NUM_CMDS] =
{
	/* SND_CMD_INIT */
	(pipeCmdHandler_t)S_HandleInitCmd,
	/* SND_CMD_SHUTDOWN */
	(pipeCmdHandler_t)S_HandleShutdownCmd,
	/* SND_CMD_CLEAR */
	(pipeCmdHandler_t)S_HandleClearCmd,
	/* SND_CMD_STOP_ALL_SOUNDS */
	(pipeCmdHandler_t)S_HandleStopCmd,
	/* SND_CMD_FREE_SFX */
	(pipeCmdHandler_t)S_HandleFreeSfxCmd,
	/* SND_CMD_LOAD_SFX */
	(pipeCmdHandler_t)S_HandleLoadSfxCmd,
	/* SND_CMD_SET_ATTENUATION_MODEL */
	(pipeCmdHandler_t)S_HandleSetAttenuationModelCmd,
	/* SND_CMD_SET_ENTITY_SPATIALIZATION */
	(pipeCmdHandler_t)S_HandleSetEntitySpatializationCmd,
	/* SND_CMD_SET_LISTENER */
	(pipeCmdHandler_t)S_HandleSetListernerCmd,
	/* SND_CMD_START_LOCAL_SOUND */
	(pipeCmdHandler_t)S_HandleStartLocalSoundCmd,
	/* SND_CMD_START_FIXED_SOUND */
	(pipeCmdHandler_t)S_HandleStartFixedSoundCmd,
	/* SND_CMD_START_GLOBAL_SOUND */
	(pipeCmdHandler_t)S_HandleStartGlobalSoundCmd,
	/* SND_CMD_START_RELATIVE_SOUND */
	(pipeCmdHandler_t)S_HandleStartRelativeSoundCmd,
	/* SND_CMD_START_BACKGROUND_TRACK */
	(pipeCmdHandler_t)S_HandleStartBackgroundTrackCmd,
	/* SND_CMD_STOP_BACKGROUND_TRACK */
	(pipeCmdHandler_t)S_HandleStopBackgroundTrackCmd,
	/* SND_CMD_LOCK_BACKGROUND_TRACK */
	(pipeCmdHandler_t)S_HandleLockBackgroundTrackCmd,
	/* SND_CMD_ADD_LOOP_SOUND */
	(pipeCmdHandler_t)S_HandleAddLoopSoundCmd,
	/* SND_CMD_ADVANCE_BACKGROUND_TRACK */
	(pipeCmdHandler_t)S_HandleAdvanceBackgroundTrackCmd,
	/* SND_CMD_PAUSE_BACKGROUND_TRACK */
	(pipeCmdHandler_t)S_HandlePauseBackgroundTrackCmd,
	/* SND_CMD_ACTIVATE */
	(pipeCmdHandler_t)S_HandleActivateCmd,
	/* SND_CMD_AVI_DEMO */
	(pipeCmdHandler_t)S_HandleAviDemoCmd,
	/* SND_CMD_RAW_SAMPLES */
	(pipeCmdHandler_t)S_HandleRawSamplesCmd,
	/* SND_CMD_POSITIONED_RAW_SAMPLES */
	(pipeCmdHandler_t)S_HandlePositionedRawSamplesCmd,
	/* SND_CMD_STUFFCMD */
	(pipeCmdHandler_t)S_HandleStuffCmd,
	/* SND_CMD_SET_MUL_ENTITY_SPATIALIZATION */
	(pipeCmdHandler_t)S_HandleSetMulEntitySpatializationCmd
};

/*
* S_EnqueuedCmdsWaiter
*/
static int S_EnqueuedCmdsWaiter( sndCmdPipe_t *queue, pipeCmdHandler_t *cmdHandlers, bool timeout ) {
	int read = S_ReadEnqueuedCmds( queue, cmdHandlers );
	int64_t now = trap_Milliseconds();

	if( read < 0 ) {
		// shutdown
		return read;
	}

	if( timeout || now >= s_last_update_time + UPDATE_MSEC ) {
		s_last_update_time = now;
		S_Update();
	}

	return read;
}

/*
* S_BackgroundUpdateProc
*/
void *S_BackgroundUpdateProc( void *param ) {
	sndCmdPipe_t *s_cmdQueue = (sndCmdPipe_t *)param;

	S_WaitEnqueuedCmds( s_cmdQueue, S_EnqueuedCmdsWaiter, sndCmdHandlers, UPDATE_MSEC );

	return NULL;
}
