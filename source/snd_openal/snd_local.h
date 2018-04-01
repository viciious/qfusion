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
// snd_local.h -- private OpenAL sound functions



//#define VORBISLIB_RUNTIME // enable this define for dynamic linked vorbis libraries

// it's in qcommon.h too, but we don't include it for modules
typedef struct { const char *name; void **funcPointer; } dllfunc_t;

#include "../gameshared/q_arch.h"
#include "../gameshared/q_math.h"
#include "../gameshared/q_shared.h"
#include "../gameshared/q_cvar.h"

#include "../client/snd_public.h"
#include "snd_syscalls.h"
#include "snd_cmdque.h"

#include "qal.h"

#ifdef _WIN32
#define ALDRIVER "OpenAL32.dll"
#define ALDEVICE_DEFAULT "Generic Software"
#elif defined ( __MACOSX__ )
#define ALDRIVER "/System/Library/Frameworks/OpenAL.framework/OpenAL"
#define ALDEVICE_DEFAULT NULL
#else
#define ALDRIVER "libopenal.so.1"
#define ALDRIVER_ALT "libopenal.so.0"
#define ALDEVICE_DEFAULT NULL
#endif

#ifdef __MACOSX__
#define MAX_SRC 64
#else
#define MAX_SRC 128
#endif

extern struct mempool_s *soundpool;

#define S_MemAlloc( pool, size ) trap_MemAlloc( pool, size, __FILE__, __LINE__ )
#define S_MemFree( mem ) trap_MemFree( mem, __FILE__, __LINE__ )
#define S_MemAllocPool( name ) trap_MemAllocPool( name, __FILE__, __LINE__ )
#define S_MemFreePool( pool ) trap_MemFreePool( pool, __FILE__, __LINE__ )
#define S_MemEmptyPool( pool ) trap_MemEmptyPool( pool, __FILE__, __LINE__ )

#define S_Malloc( size ) S_MemAlloc( soundpool, size )
#define S_Free( data ) S_MemFree( data )

typedef struct sfx_s {
	int id;
	char filename[MAX_QPATH];
	int registration_sequence;
	ALuint buffer;      // OpenAL buffer
	bool inMemory;
	bool isLocked;
	int used;           // Time last used
} sfx_t;

extern cvar_t *s_volume;
extern cvar_t *s_musicvolume;
extern cvar_t *s_sources;
extern cvar_t *s_stereo2mono;

extern cvar_t *s_doppler;
extern cvar_t *s_sound_velocity;
extern cvar_t *s_environment_effects;
extern cvar_t *s_environment_sampling_quality;
extern cvar_t *s_environment_effects_scale;
extern cvar_t *s_hrtf;
// Has effect only if environment effects are turned on
extern cvar_t *s_attenuate_on_obstruction;

extern cvar_t *s_globalfocus;

extern int s_attenuation_model;
extern float s_attenuation_maxdistance;
extern float s_attenuation_refdistance;

extern ALCdevice *alDevice;
extern ALCcontext *alContext;

#define SRCPRI_AMBIENT  0   // Ambient sound effects
#define SRCPRI_LOOP 1   // Looping (not ambient) sound effects
#define SRCPRI_ONESHOT  2   // One-shot sounds
#define SRCPRI_LOCAL    3   // Local sounds
#define SRCPRI_STREAM   4   // Streams (music, cutscenes)

/*
* Exported functions
*/
int S_API( void );
void S_Error( const char *format, ... );

void S_FreeSounds( void );
void S_StopAllSounds( bool stopMusic );

void S_Clear( void );
void S_Activate( bool active );

void S_SetAttenuationModel( int model, float maxdistance, float refdistance );

// playing
struct sfx_s *S_RegisterSound( const char *sample );

void S_StartFixedSound( struct sfx_s *sfx, const vec3_t origin, int channel, float fvol, float attenuation );
void S_StartRelativeSound( struct sfx_s *sfx, int entnum, int channel, float fvol, float attenuation );
void S_StartGlobalSound( struct sfx_s *sfx, int channel, float fvol );

void S_StartLocalSound( sfx_t *sfx, float fvol );

void S_AddLoopSound( struct sfx_s *sfx, int entnum, float fvol, float attenuation );

// cinema
void S_RawSamples( unsigned int samples, unsigned int rate,
				   unsigned short width, unsigned short channels, const uint8_t *data, bool music );
void S_RawSamples2( unsigned int samples, unsigned int rate,
					unsigned short width, unsigned short channels, const uint8_t *data, bool music, float fvol );
void S_PositionedRawSamples( int entnum, float fvol, float attenuation,
							 unsigned int samples, unsigned int rate,
							 unsigned short width, unsigned short channels, const uint8_t *data );

// music
void S_StartBackgroundTrack( const char *intro, const char *loop, int mode );
void S_StopBackgroundTrack( void );
void S_PrevBackgroundTrack( void );
void S_NextBackgroundTrack( void );
void S_PauseBackgroundTrack( void );
void S_LockBackgroundTrack( bool lock );

/*
* Util (snd_al.c)
*/
ALuint S_SoundFormat( int width, int channels );
const char *S_ErrorMessage( ALenum error );
ALuint S_GetBufferLength( ALuint buffer );
void *S_BackgroundUpdateProc( void *param );

/*
* Buffer management
*/
void S_InitBuffers( void );
void S_ShutdownBuffers( void );
void S_SoundList_f( void );
void S_UseBuffer( sfx_t *sfx );
ALuint S_GetALBuffer( const sfx_t *sfx );
sfx_t *S_FindBuffer( const char *filename );
void S_MarkBufferFree( sfx_t *sfx );
sfx_t *S_FindFreeBuffer( void );
void S_ForEachBuffer( void ( *callback )( sfx_t *sfx ) );
sfx_t *S_GetBufferById( int id );
bool S_LoadBuffer( sfx_t *sfx );
bool S_UnloadBuffer( sfx_t *sfx );

struct src_s;

class Effect {
public:
	virtual void BindOrUpdate( src_s *src ) = 0;
	virtual void InterpolateProps( const Effect *oldOne, int timeDelta ) = 0;
	virtual ~Effect() {}

	template <typename T> static const T Cast( const Effect *effect ) {
		// We might think of optimizing it in future, that's why the method is favourable over the direct cast
		return dynamic_cast<T>( const_cast<Effect *>( effect ) );
	}
protected:
	ALint type;
	Effect( ALint type_ ): type( type_ ) {}

	class Interpolator {
		float newWeight, oldWeight;
	public:
		explicit Interpolator( int timeDelta ) {
			assert( timeDelta >= 0 );
			float frac = timeDelta / 175.0f;
			if( frac <= 1.0f ) {
				newWeight = 0.5f + 0.3f * frac;
				oldWeight = 0.5f - 0.3f * frac;
			} else {
				newWeight = 1.0f;
				oldWeight = 0.0f;
			}
		}

		float operator()( float rawNewValue, float oldValue, float mins, float maxs ) const {
			float result = newWeight * ( rawNewValue ) + oldWeight * ( oldValue );
			clamp( result, mins, maxs );
			return result;
		}
	};

	void CheckCurrentlyBoundEffect( src_s *src );
	virtual void IntiallySetupEffect( src_s *src );
	virtual float GetMasterGain( src_s *src ) = 0;
	void AttachEffect( src_s *src );
};

class UnderwaterFlangerEffect final: public Effect {
	void IntiallySetupEffect( src_s *src ) override;
	float GetMasterGain( src_s *src ) override;
public:
	float directObstruction;
	UnderwaterFlangerEffect(): Effect( AL_EFFECT_FLANGER ) {}
	void BindOrUpdate( src_s *src ) override;
	void InterpolateProps( const Effect *oldOne, int timeDelta ) override;
};

class ReverbEffect final: public Effect {
	float GetMasterGain( struct src_s *src ) override;
public:
	float directObstruction;
	float density;              // [0.0 ... 1.0]    default 1.0
	float diffusion;            // [0.0 ... 1.0]    default 1.0
	float gain;                 // [0.0 ... 1.0]    default 0.32
	float gainHf;               // [0.0 ... 1.0]    default 0.89
	float decayTime;            // [0.1 ... 20.0]   default 1.49
	float reflectionsGain;      // [0.0 ... 3.16]   default 0.05
	float reflectionsDelay;     // [0.0 ... 0.3]    default 0.007
	float lateReverbDelay;      // [0.0 ... 0.1]    default 0.011

	ReverbEffect(): Effect( AL_EFFECT_REVERB ) {}
	void BindOrUpdate( struct src_s *src ) override;
	void InterpolateProps( const Effect *oldOne, int timeDelta ) override;

	void CopyReverbProps( const ReverbEffect *that ) {
		// Avoid memcpy... This is not a POD type
		density = that->density;
		diffusion = that->diffusion;
		gain = that->gain;
		gainHf = that->gainHf;
		decayTime = that->decayTime;
		reflectionsGain = that->reflectionsGain;
		reflectionsDelay = that->reflectionsDelay;
		lateReverbDelay = that->lateReverbDelay;
	}
};

typedef struct {
	float quality;
	unsigned numSamples;
	uint16_t valueIndex;
} samplingProps_t;

typedef struct envUpdateState_s {
	sfx_t *parent;

	int64_t nextEnvUpdateAt;
	int64_t lastEnvUpdateAt;

	Effect *oldEffect;
	Effect *effect;

	samplingProps_t directObstructionSamplingProps;
	samplingProps_t reverbPrimaryRaysSamplingProps;

	vec3_t lastUpdateOrigin;
	vec3_t lastUpdateVelocity;

	int entNum;
	float attenuation;

	float priorityInQueue;

	bool wasInLiquid;
} envUpdateState_t;

/*
* Source management
*/
typedef struct src_s {
	ALuint source;

	ALuint directFilter;
	ALuint effect;
	ALuint effectSlot;

	sfx_t *sfx;

	cvar_t *volumeVar;

	int64_t lastUse;    // Last time used
	int priority;
	int entNum;
	int channel;

	float fvol; // volume modifier, for s_volume updating
	float attenuation;

	bool isActive;
	bool isLocked;
	bool isLooping;
	bool isTracking;
	bool keepAlive;

	envUpdateState_t envUpdateState;

	vec3_t origin, velocity; // for local culling
} src_t;

#define QF_METERS_PER_UNIT ( 0.038f )

extern src_t srclist[MAX_SRC];
extern int src_count;

bool S_InitSources( int maxEntities, bool verbose );
void S_ShutdownSources( void );
void S_UpdateSources( void );
src_t *S_AllocSource( int priority, int entnum, int channel );
src_t *S_FindSource( int entnum, int channel );
void S_LockSource( src_t *src );
void S_UnlockSource( src_t *src );
void S_StopAllSources( void );
ALuint S_GetALSource( const src_t *src );
src_t *S_AllocRawSource( int entNum, float fvol, float attenuation, cvar_t *volumeVar );
void S_SetEntitySpatialization( int entnum, const vec3_t origin, const vec3_t velocity );

/*
* Music
*/
void S_UpdateMusic( void );

/*
* Stream
*/
void S_UpdateStreams( void );
void S_StopStreams( void );
void S_StopRawSamples( void );

/*
* Decoder
*/
typedef struct snd_info_s {
	int rate;
	int width;
	int channels;
	int samples;
	int size;
} snd_info_t;

typedef struct snd_decoder_s snd_decoder_t;
typedef struct snd_stream_s {
	snd_decoder_t *decoder;
	bool isUrl;
	snd_info_t info; // TODO: Change to AL_FORMAT?
	void *ptr; // decoder specific stuff
} snd_stream_t;

typedef struct bgTrack_s {
	char *filename;
	bool ignore;
	bool isUrl;
	bool loop;
	bool muteOnPause;
	snd_stream_t *stream;

	struct bgTrack_s *next; // the next track to be played, the looping part aways points to itself
	struct bgTrack_s *prev; // previous track in the playlist
	struct bgTrack_s *anext; // allocation linked list
} bgTrack_t;

bool S_InitDecoders( bool verbose );
void S_ShutdownDecoders( bool verbose );
void *S_LoadSound( const char *filename, snd_info_t *info );
snd_stream_t *S_OpenStream( const char *filename, bool *delay );
bool S_ContOpenStream( snd_stream_t *stream );
int S_ReadStream( snd_stream_t *stream, int bytes, void *buffer );
void S_CloseStream( snd_stream_t *stream );
bool S_ResetStream( snd_stream_t *stream );
bool S_EoStream( snd_stream_t *stream );
int S_SeekSteam( snd_stream_t *stream, int ofs, int whence );

void S_BeginAviDemo( void );
void S_StopAviDemo( void );

//====================================================================

#ifdef __cplusplus
extern "C" {
#endif

unsigned S_GetRawSamplesLength( void );
unsigned S_GetPositionedRawSamplesLength( int entnum );

/*
* Exported functions
*/
bool SF_Init( void *hwnd, int maxEntities, bool verbose );
void SF_Shutdown( bool verbose );
void SF_EndRegistration( void );
void SF_BeginRegistration( void );
sfx_t *SF_RegisterSound( const char *name );
void SF_StartBackgroundTrack( const char *intro, const char *loop, int mode );
void SF_StopBackgroundTrack( void );
void SF_LockBackgroundTrack( bool lock );
void SF_StopAllSounds( bool clear, bool stopMusic );
void SF_PrevBackgroundTrack( void );
void SF_NextBackgroundTrack( void );
void SF_PauseBackgroundTrack( void );
void SF_Activate( bool active );
void SF_BeginAviDemo( void );
void SF_StopAviDemo( void );
void SF_SetAttenuationModel( int model, float maxdistance, float refdistance );
void SF_SetEntitySpatialization( int entnum, const vec3_t origin, const vec3_t velocity );
void SF_SetAttenuationModel( int model, float maxdistance, float refdistance );
void SF_StartFixedSound( sfx_t *sfx, const vec3_t origin, int channel, float fvol, float attenuation );
void SF_StartRelativeSound( sfx_t *sfx, int entnum, int channel, float fvol, float attenuation );
void SF_StartGlobalSound( sfx_t *sfx, int channel, float fvol );
void SF_StartLocalSoundByName( const char *sound );
void SF_StartLocalSound( sfx_t *sfx, float fvol );
void SF_Clear( void );
void SF_AddLoopSound( sfx_t *sfx, int entnum, float fvol, float attenuation );
void SF_Update( const vec3_t origin, const vec3_t velocity, const mat3_t axis, bool avidump );
void SF_RawSamples( unsigned int samples, unsigned int rate, unsigned short width,
					unsigned short channels, const uint8_t *data, bool music );
void SF_PositionedRawSamples( int entnum, float fvol, float attenuation,
							  unsigned int samples, unsigned int rate,
							  unsigned short width, unsigned short channels, const uint8_t *data );

#ifdef __cplusplus
}
#endif