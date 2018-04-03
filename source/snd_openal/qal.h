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
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#ifndef __QAL_H__
#define __QAL_H__

#ifdef OPENAL_RUNTIME
#define AL_NO_PROTOTYPES
#define ALC_NO_PROTOTYPES
#endif

#if defined ( __MACOSX__ )
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#elif defined( __ANDROID__ )
#include <AL/al.h>
#include <AL/alc.h>
#else
#include <al.h>
#include <alc.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef OPENAL_RUNTIME
extern LPALENABLE qalEnable;
extern LPALDISABLE qalDisable;
extern LPALISENABLED qalIsEnabled;
extern LPALGETSTRING qalGetString;
extern LPALGETBOOLEANV qalGetBooleanv;
extern LPALGETINTEGERV qalGetIntegerv;
extern LPALGETFLOATV qalGetFloatv;
extern LPALGETDOUBLEV qalGetDoublev;
extern LPALGETBOOLEAN qalGetBoolean;
extern LPALGETINTEGER qalGetInteger;
extern LPALGETFLOAT qalGetFloat;
extern LPALGETDOUBLE qalGetDouble;
extern LPALGETERROR qalGetError;
extern LPALISEXTENSIONPRESENT qalIsExtensionPresent;
extern LPALGETPROCADDRESS qalGetProcAddress;
extern LPALGETENUMVALUE qalGetEnumValue;
extern LPALLISTENERF qalListenerf;
extern LPALLISTENER3F qalListener3f;
extern LPALLISTENERFV qalListenerfv;
extern LPALLISTENERI qalListeneri;
extern LPALLISTENER3I qalListener3i;
extern LPALLISTENERIV qalListeneriv;
extern LPALGETLISTENERF qalGetListenerf;
extern LPALGETLISTENER3F qalGetListener3f;
extern LPALGETLISTENERFV qalGetListenerfv;
extern LPALGETLISTENERI qalGetListeneri;
extern LPALGETLISTENER3I qalGetListener3i;
extern LPALGETLISTENERIV qalGetListeneriv;
extern LPALGENSOURCES qalGenSources;
extern LPALDELETESOURCES qalDeleteSources;
extern LPALISSOURCE qalIsSource;
extern LPALSOURCEF qalSourcef;
extern LPALSOURCE3F qalSource3f;
extern LPALSOURCEFV qalSourcefv;
extern LPALSOURCEI qalSourcei;
extern LPALSOURCE3I qalSource3i;
extern LPALSOURCEIV qalSourceiv;
extern LPALGETSOURCEF qalGetSourcef;
extern LPALGETSOURCE3F qalGetSource3f;
extern LPALGETSOURCEFV qalGetSourcefv;
extern LPALGETSOURCEI qalGetSourcei;
extern LPALGETSOURCE3I qalGetSource3i;
extern LPALGETSOURCEIV qalGetSourceiv;
extern LPALSOURCEPLAYV qalSourcePlayv;
extern LPALSOURCESTOPV qalSourceStopv;
extern LPALSOURCEREWINDV qalSourceRewindv;
extern LPALSOURCEPAUSEV qalSourcePausev;
extern LPALSOURCEPLAY qalSourcePlay;
extern LPALSOURCESTOP qalSourceStop;
extern LPALSOURCEREWIND qalSourceRewind;
extern LPALSOURCEPAUSE qalSourcePause;
extern LPALSOURCEQUEUEBUFFERS qalSourceQueueBuffers;
extern LPALSOURCEUNQUEUEBUFFERS qalSourceUnqueueBuffers;
extern LPALGENBUFFERS qalGenBuffers;
extern LPALDELETEBUFFERS qalDeleteBuffers;
extern LPALISBUFFER qalIsBuffer;
extern LPALBUFFERDATA qalBufferData;
extern LPALBUFFERF qalBufferf;
extern LPALBUFFER3F qalBuffer3f;
extern LPALBUFFERFV qalBufferfv;
extern LPALBUFFERF qalBufferi;
extern LPALBUFFER3F qalBuffer3i;
extern LPALBUFFERFV qalBufferiv;
extern LPALGETBUFFERF qalGetBufferf;
extern LPALGETBUFFER3F qalGetBuffer3f;
extern LPALGETBUFFERFV qalGetBufferfv;
extern LPALGETBUFFERI qalGetBufferi;
extern LPALGETBUFFER3I qalGetBuffer3i;
extern LPALGETBUFFERIV qalGetBufferiv;
extern LPALDOPPLERFACTOR qalDopplerFactor;
extern LPALDOPPLERVELOCITY qalDopplerVelocity;
extern LPALSPEEDOFSOUND qalSpeedOfSound;
extern LPALDISTANCEMODEL qalDistanceModel;

extern LPALCCREATECONTEXT qalcCreateContext;
extern LPALCMAKECONTEXTCURRENT qalcMakeContextCurrent;
extern LPALCPROCESSCONTEXT qalcProcessContext;
extern LPALCSUSPENDCONTEXT qalcSuspendContext;
extern LPALCDESTROYCONTEXT qalcDestroyContext;
extern LPALCGETCURRENTCONTEXT qalcGetCurrentContext;
extern LPALCGETCONTEXTSDEVICE qalcGetContextsDevice;
extern LPALCOPENDEVICE qalcOpenDevice;
extern LPALCCLOSEDEVICE qalcCloseDevice;
extern LPALCGETERROR qalcGetError;
extern LPALCISEXTENSIONPRESENT qalcIsExtensionPresent;
extern LPALCGETPROCADDRESS qalcGetProcAddress;
extern LPALCGETENUMVALUE qalcGetEnumValue;
extern LPALCGETSTRING qalcGetString;
extern LPALCGETINTEGERV qalcGetIntegerv;
extern LPALCCAPTUREOPENDEVICE qalcCaptureOpenDevice;
extern LPALCCAPTURECLOSEDEVICE qalcCaptureCloseDevice;
extern LPALCCAPTURESTART qalcCaptureStart;
extern LPALCCAPTURESTOP qalcCaptureStop;
extern LPALCCAPTURESAMPLES qalcCaptureSamples;
#else
#define qalEnable alEnable
#define qalDisable alDisable
#define qalIsEnabled alIsEnabled
#define qalGetString alGetString
#define qalGetBooleanv alGetBooleanv
#define qalGetIntegerv alGetIntegerv
#define qalGetFloatv alGetFloatv
#define qalGetDoublev alGetDoublev
#define qalGetBoolean alGetBoolean
#define qalGetInteger alGetInteger
#define qalGetFloat alGetFloat
#define qalGetDouble alGetDouble
#define qalGetError alGetError
#define qalIsExtensionPresent alIsExtensionPresent
#define qalGetProcAddress alGetProcAddress
#define qalGetEnumValue alGetEnumValue
#define qalListenerf alListenerf
#define qalListener3f alListener3f
#define qalListenerfv alListenerfv
#define qalListeneri alListeneri
#define qalListener3i alListener3i
#define qalListeneriv alListeneriv
#define qalGetListenerf alGetListenerf
#define qalGetListener3f alGetListener3f
#define qalGetListenerfv alGetListenerfv
#define qalGetListeneri alGetListeneri
#define qalGetListener3i alGetListener3i
#define qalGetListeneriv alGetListeneriv
#define qalGenSources alGenSources
#define qalDeleteSources alDeleteSources
#define qalIsSource alIsSource
#define qalSourcef alSourcef
#define qalSource3f alSource3f
#define qalSourcefv alSourcefv
#define qalSourcei alSourcei
#define qalSource3i alSource3i
#define qalSourceiv alSourceiv
#define qalGetSourcef alGetSourcef
#define qalGetSource3f alGetSource3f
#define qalGetSourcefv alGetSourcefv
#define qalGetSourcei alGetSourcei
#define qalGetSource3i alGetSource3i
#define qalGetSourceiv alGetSourceiv
#define qalSourcePlayv alSourcePlayv
#define qalSourceStopv alSourceStopv
#define qalSourceRewindv alSourceRewindv
#define qalSourcePausev alSourcePausev
#define qalSourcePlay alSourcePlay
#define qalSourceStop alSourceStop
#define qalSourceRewind alSourceRewind
#define qalSourcePause alSourcePause
#define qalSourceQueueBuffers alSourceQueueBuffers
#define qalSourceUnqueueBuffers alSourceUnqueueBuffers
#define qalGenBuffers alGenBuffers
#define qalDeleteBuffers alDeleteBuffers
#define qalIsBuffer alIsBuffer
#define qalBufferData alBufferData
#define qalBufferf alBufferf
#define qalBuffer3f alBuffer3f
#define qalBufferfv alBufferfv
#define qalBufferi alBufferi
#define qalBuffer3i alBuffer3i
#define qalBufferiv alBufferiv
#define qalGetBufferf alGetBufferf
#define qalGetBuffer3f alGetBuffer3f
#define qalGetBufferfv alGetBufferfv
#define qalGetBufferi alGetBufferi
#define qalGetBuffer3i alGetBuffer3i
#define qalGetBufferiv alGetBufferiv
#define qalDopplerFactor alDopplerFactor
#define qalDopplerVelocity alDopplerVelocity
#define qalSpeedOfSound alSpeedOfSound
#define qalDistanceModel alDistanceModel

#define qalcCreateContext alcCreateContext
#define qalcMakeContextCurrent alcMakeContextCurrent
#define qalcProcessContext alcProcessContext
#define qalcSuspendContext alcSuspendContext
#define qalcDestroyContext alcDestroyContext
#define qalcGetCurrentContext alcGetCurrentContext
#define qalcGetContextsDevice alcGetContextsDevice
#define qalcOpenDevice alcOpenDevice
#define qalcCloseDevice alcCloseDevice
#define qalcGetError alcGetError
#define qalcIsExtensionPresent alcIsExtensionPresent
#define qalcGetProcAddress alcGetProcAddress
#define qalcGetEnumValue alcGetEnumValue
#define qalcGetString alcGetString
#define qalcGetIntegerv alcGetIntegerv
#define qalcCaptureOpenDevice alcCaptureOpenDevice
#define qalcCaptureCloseDevice alcCaptureCloseDevice
#define qalcCaptureStart alcCaptureStart
#define qalcCaptureStop alcCaptureStop
#define qalcCaptureSamples alcCaptureSamples
#endif

/* HRTF */
#ifndef ALC_HRTF_SOFT
#define ALC_HRTF_SOFT                            0x1992
#endif

// QFusion: always load EFX extension functions manually

typedef void ( AL_APIENTRY * LPALGENEFFECTS )( ALsizei, ALuint* );
typedef void ( AL_APIENTRY * LPALDELETEEFFECTS )( ALsizei, const ALuint* );
typedef ALboolean ( AL_APIENTRY * LPALISEFFECT )( ALuint );
typedef void ( AL_APIENTRY * LPALEFFECTI )( ALuint, ALenum, ALint );
typedef void ( AL_APIENTRY * LPALEFFECTIV )( ALuint, ALenum, const ALint* );
typedef void ( AL_APIENTRY * LPALEFFECTF )( ALuint, ALenum, ALfloat );
typedef void ( AL_APIENTRY * LPALEFFECTFV )( ALuint, ALenum, const ALfloat* );
typedef void ( AL_APIENTRY * LPALGETEFFECTI )( ALuint, ALenum, ALint* );
typedef void ( AL_APIENTRY * LPALGETEFFECTIV )( ALuint, ALenum, ALint* );
typedef void ( AL_APIENTRY * LPALGETEFFECTF )( ALuint, ALenum, ALfloat* );
typedef void ( AL_APIENTRY * LPALGETEFFECTFV )( ALuint, ALenum, ALfloat* );

typedef void ( AL_APIENTRY * LPALGENFILTERS )( ALsizei, ALuint* );
typedef void ( AL_APIENTRY * LPALDELETEFILTERS )( ALsizei, const ALuint* );
typedef ALboolean ( AL_APIENTRY * LPALISFILTER )( ALuint );
typedef void ( AL_APIENTRY * LPALFILTERI )( ALuint, ALenum, ALint );
typedef void ( AL_APIENTRY * LPALFILTERIV )( ALuint, ALenum, const ALint* );
typedef void ( AL_APIENTRY * LPALFILTERF )( ALuint, ALenum, ALfloat );
typedef void ( AL_APIENTRY * LPALFILTERFV )( ALuint, ALenum, const ALfloat* );
typedef void ( AL_APIENTRY * LPALGETFILTERI )( ALuint, ALenum, ALint* );
typedef void ( AL_APIENTRY * LPALGETFILTERIV )( ALuint, ALenum, ALint* );
typedef void ( AL_APIENTRY * LPALGETFILTERF )( ALuint, ALenum, ALfloat* );
typedef void ( AL_APIENTRY * LPALGETFILTERFV )( ALuint, ALenum, ALfloat* );

typedef void ( AL_APIENTRY * LPALGENAUXILIARYEFFECTSLOTS )( ALsizei, ALuint* );
typedef void ( AL_APIENTRY * LPALDELETEAUXILIARYEFFECTSLOTS )( ALsizei, const ALuint* );
typedef ALboolean ( AL_APIENTRY * LPALISAUXILIARYEFFECTSLOT )( ALuint );
typedef void ( AL_APIENTRY * LPALAUXILIARYEFFECTSLOTI )( ALuint, ALenum, ALint );
typedef void ( AL_APIENTRY * LPALAUXILIARYEFFECTSLOTIV )( ALuint, ALenum, const ALint* );
typedef void ( AL_APIENTRY * LPALAUXILIARYEFFECTSLOTF )( ALuint, ALenum, ALfloat );
typedef void ( AL_APIENTRY * LPALAUXILIARYEFFECTSLOTFV )( ALuint, ALenum, const ALfloat* );
typedef void ( AL_APIENTRY * LPALGETAUXILIARYEFFECTSLOTI )( ALuint, ALenum, ALint* );
typedef void ( AL_APIENTRY * LPALGETAUXILIARYEFFECTSLOTIV )( ALuint, ALenum, ALint* );
typedef void ( AL_APIENTRY * LPALGETAUXILIARYEFFECTSLOTF )( ALuint, ALenum, ALfloat* );
typedef void ( AL_APIENTRY * LPALGETAUXILIARYEFFECTSLOTFV )( ALuint, ALenum, ALfloat* );

extern LPALGENEFFECTS qalGenEffects;
extern LPALDELETEEFFECTS qalDeleteEffects;
extern LPALISEFFECT qalIsEffect;
extern LPALEFFECTI qalEffecti;
extern LPALEFFECTIV qalEffectiv;
extern LPALEFFECTF qalEffectf;
extern LPALEFFECTFV qalEffectfv;
extern LPALGETEFFECTI qalGetEffecti;
extern LPALGETEFFECTIV qalGetEffeciv;
extern LPALGETEFFECTF qalGetEffectf;
extern LPALGETEFFECTFV qalGetEffectfv;

extern LPALGENFILTERS qalGenFilters;
extern LPALDELETEFILTERS qalDeleteFilters;
extern LPALISFILTER qalIsFilter;
extern LPALFILTERI qalFilteri;
extern LPALFILTERIV qalFilteriv;
extern LPALFILTERF qalFilterf;
extern LPALFILTERFV qalFilterfv;
extern LPALGETFILTERI qalGetFilteri;
extern LPALGETFILTERIV qalGetFilteriv;
extern LPALGETFILTERF qalGetFilterf;
extern LPALGETFILTERFV qalGetFilterfv;

extern LPALGENAUXILIARYEFFECTSLOTS qalGenAuxiliaryEffectSlots;
extern LPALDELETEAUXILIARYEFFECTSLOTS qalDeleteAuxiliaryEffectSlots;
extern LPALISAUXILIARYEFFECTSLOT qalIsAuxiliaryEffectSlot;
extern LPALAUXILIARYEFFECTSLOTI qalAuxiliaryEffectSloti;
extern LPALAUXILIARYEFFECTSLOTIV qalAuxiliaryEffectSlotiv;
extern LPALAUXILIARYEFFECTSLOTF qalAuxiliaryEffectSlotf;
extern LPALAUXILIARYEFFECTSLOTFV qalAuxiliaryEffectSlotfv;
extern LPALGETAUXILIARYEFFECTSLOTI qalGetAuxiliaryEffectSloti;
extern LPALGETAUXILIARYEFFECTSLOTIV qalGetAuxiliaryEffectSlotiv;
extern LPALGETAUXILIARYEFFECTSLOTF qalGetAuxiliaryEffectSlotf;
extern LPALGETAUXILIARYEFFECTSLOTFV qalGetAuxiliaryEffectSlotfv;

#define ALC_MAX_AUXILIARY_SENDS                  0x20003

/* Listener properties. */
#define AL_METERS_PER_UNIT                       0x20004

/* Effect properties. */

/* Reverb effect parameters */
#define AL_REVERB_DENSITY                        0x0001
#define AL_REVERB_DIFFUSION                      0x0002
#define AL_REVERB_GAIN                           0x0003
#define AL_REVERB_GAINHF                         0x0004
#define AL_REVERB_DECAY_TIME                     0x0005
#define AL_REVERB_DECAY_HFRATIO                  0x0006
#define AL_REVERB_REFLECTIONS_GAIN               0x0007
#define AL_REVERB_REFLECTIONS_DELAY              0x0008
#define AL_REVERB_LATE_REVERB_GAIN               0x0009
#define AL_REVERB_LATE_REVERB_DELAY              0x000A
#define AL_REVERB_AIR_ABSORPTION_GAINHF          0x000B
#define AL_REVERB_ROOM_ROLLOFF_FACTOR            0x000C
#define AL_REVERB_DECAY_HFLIMIT                  0x000D

/* Flanger effect parameters */
#define AL_FLANGER_WAVEFORM                      0x0001
#define AL_FLANGER_PHASE                         0x0002
#define AL_FLANGER_RATE                          0x0003
#define AL_FLANGER_DEPTH                         0x0004
#define AL_FLANGER_FEEDBACK                      0x0005
#define AL_FLANGER_DELAY                         0x0006

/* Effect type */
#define AL_EFFECT_FIRST_PARAMETER                0x0000
#define AL_EFFECT_LAST_PARAMETER                 0x8000
#define AL_EFFECT_TYPE                           0x8001

/* Effect types, used with the AL_EFFECT_TYPE property */
#define AL_EFFECT_NULL                           0x0000
#define AL_EFFECT_REVERB                         0x0001
#define AL_EFFECT_CHORUS                         0x0002
#define AL_EFFECT_DISTORTION                     0x0003
#define AL_EFFECT_ECHO                           0x0004
#define AL_EFFECT_FLANGER                        0x0005
#define AL_EFFECT_FREQUENCY_SHIFTER              0x0006
#define AL_EFFECT_VOCAL_MORPHER                  0x0007
#define AL_EFFECT_PITCH_SHIFTER                  0x0008
#define AL_EFFECT_RING_MODULATOR                 0x0009
#define AL_EFFECT_AUTOWAH                        0x000A
#define AL_EFFECT_COMPRESSOR                     0x000B
#define AL_EFFECT_EQUALIZER                      0x000C
#define AL_EFFECT_EAXREVERB                      0x8000

/* Auxiliary Effect Slot properties. */
#define AL_EFFECTSLOT_EFFECT                     0x0001
#define AL_EFFECTSLOT_GAIN                       0x0002
#define AL_EFFECTSLOT_AUXILIARY_SEND_AUTO        0x0003

/* NULL Auxiliary Slot ID to disable a source send. */
#define AL_EFFECTSLOT_NULL                       0x0000

/* Source properties. */
#define AL_DIRECT_FILTER                         0x20005
#define AL_AUXILIARY_SEND_FILTER                 0x20006
#define AL_AIR_ABSORPTION_FACTOR                 0x20007
#define AL_ROOM_ROLLOFF_FACTOR                   0x20008
#define AL_CONE_OUTER_GAINHF                     0x20009
#define AL_DIRECT_FILTER_GAINHF_AUTO             0x2000A
#define AL_AUXILIARY_SEND_FILTER_GAIN_AUTO       0x2000B
#define AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO     0x2000C

/* Filter properties. */

/* Lowpass filter parameters */
#define AL_LOWPASS_GAIN                          0x0001
#define AL_LOWPASS_GAINHF                        0x0002

/* Highpass filter parameters */
#define AL_HIGHPASS_GAIN                         0x0001
#define AL_HIGHPASS_GAINLF                       0x0002

/* Bandpass filter parameters */
#define AL_BANDPASS_GAIN                         0x0001
#define AL_BANDPASS_GAINLF                       0x0002
#define AL_BANDPASS_GAINHF                       0x0003

/* Filter type */
#define AL_FILTER_FIRST_PARAMETER                0x0000
#define AL_FILTER_LAST_PARAMETER                 0x8000
#define AL_FILTER_TYPE                           0x8001

/* Filter types, used with the AL_FILTER_TYPE property */
#define AL_FILTER_NULL                           0x0000
#define AL_FILTER_LOWPASS                        0x0001
#define AL_FILTER_HIGHPASS                       0x0002
#define AL_FILTER_BANDPASS                       0x0003

bool QAL_Init( const char *libname, bool verbose );
void QAL_Shutdown( void );

bool QAL_Is_EFX_ExtensionSupported( ALCdevice *device );

bool QAL_Is_HRTF_ExtensionSupported( ALCdevice *device );

#ifdef __cplusplus
}
#endif

#endif  // __QAL_H__
