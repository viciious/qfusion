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
#include <snd_cmdque.h>
#include "snd_env_sampler.h"

#include <algorithm>

src_t srclist[MAX_SRC];
int src_count = 0;
static bool src_inited = false;

typedef struct sentity_s {
	src_t *src;
	int touched;    // Sound present this update?
	vec3_t origin;
	vec3_t velocity;
} sentity_t;
static sentity_t *entlist = NULL; //[MAX_EDICTS];
static int max_ents;

static void S_AdjustGain( src_t *src ) {
	if( auto *effect = src->envUpdateState.effect ) {
		effect->AdjustGain( src );
		return;
	}

	if( src->volumeVar ) {
		qalSourcef( src->source, AL_GAIN, src->fvol * src->volumeVar->value );
	} else {
		qalSourcef( src->source, AL_GAIN, src->fvol * s_volume->value );
	}
}

/*
* source_setup
*/
static void source_setup( src_t *src, sfx_t *sfx, int priority, int entNum, int channel, float fvol, float attenuation ) {
	ALuint buffer = 0;

	// Mark the SFX as used, and grab the raw AL buffer
	if( sfx ) {
		S_UseBuffer( sfx );
		buffer = S_GetALBuffer( sfx );
	}

	clamp_low( attenuation, 0.0f );

	src->lastUse = trap_Milliseconds();
	src->sfx = sfx;
	src->priority = priority;
	src->entNum = entNum;
	src->channel = channel;
	src->fvol = fvol;
	src->attenuation = attenuation;
	src->isActive = true;
	src->isLocked = false;
	src->isLooping = false;
	src->isTracking = false;
	src->isLingering = false;
	src->volumeVar = s_volume;
	VectorClear( src->origin );
	VectorClear( src->velocity );

	qalSourcefv( src->source, AL_POSITION, vec3_origin );
	qalSourcefv( src->source, AL_VELOCITY, vec3_origin );
	qalSourcef( src->source, AL_GAIN, fvol * s_volume->value );
	qalSourcei( src->source, AL_SOURCE_RELATIVE, AL_FALSE );
	qalSourcei( src->source, AL_LOOPING, AL_FALSE );
	qalSourcei( src->source, AL_BUFFER, buffer );

	qalSourcef( src->source, AL_REFERENCE_DISTANCE, s_attenuation_refdistance );
	qalSourcef( src->source, AL_MAX_DISTANCE, s_attenuation_maxdistance );
	qalSourcef( src->source, AL_ROLLOFF_FACTOR, attenuation );

	ENV_RegisterSource( src );
}

/*
* source_kill
*/
static void source_kill( src_t *src ) {
	int numbufs;
	ALuint source = src->source;
	ALuint buffer;

	if( src->isLocked ) {
		return;
	}

	if( src->isActive ) {
		qalSourceStop( source );
	} else {
		// Un-queue all queued buffers
		qalGetSourcei( source, AL_BUFFERS_QUEUED, &numbufs );
		while( numbufs-- ) {
			qalSourceUnqueueBuffers( source, 1, &buffer );
		}
	}

	// Un-queue all processed buffers
	qalGetSourcei( source, AL_BUFFERS_PROCESSED, &numbufs );
	while( numbufs-- ) {
		qalSourceUnqueueBuffers( source, 1, &buffer );
	}

	qalSourcei( src->source, AL_BUFFER, AL_NONE );

	src->sfx = 0;
	src->lastUse = 0;
	src->priority = 0;
	src->entNum = -1;
	src->channel = -1;
	src->fvol = 1;
	src->isActive = false;
	src->isLocked = false;
	src->isLooping = false;
	src->isTracking = false;

	src->isLingering = false;
	src->lingeringTimeoutAt = 0;

	ENV_UnregisterSource( src );
}

void Effect::CheckCurrentlyBoundEffect( src_t *src ) {
	ALint effectType;

	// We limit every source to have only a single effect.
	// This is required to comply with the runtime effects count restriction.
	// If the effect type has been changed, we have to delete an existing effect.
	qalGetEffecti( src->effect, AL_EFFECT_TYPE, &effectType );
	if( this->type == effectType ) {
		return;
	}

	// Detach the slot from the source
	qalSource3i( src->source, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL );
	// Detach the effect from the slot
	qalAuxiliaryEffectSloti( src->effectSlot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL );

	// TODO: Can we reuse the effect?
	qalDeleteEffects( 1, &src->effect );

	IntiallySetupEffect( src );
}

void Effect::IntiallySetupEffect( src_t *src ) {
	qalGenEffects( 1, &src->effect );
	qalEffecti( src->effect, AL_EFFECT_TYPE, this->type );
}

float Effect::GetMasterGain( src_s *src ) const {
	return src->fvol * src->volumeVar->value;
}

void Effect::AdjustGain( src_t *src ) const {
	qalSourcef( src->source, AL_GAIN, GetMasterGain( src ) );
}

void Effect::AttachEffect( src_t *src ) {
	// Set gain in any case (useful if the "attenuate on obstruction" flag has been turned off).
	AdjustGain( src );

	// Attach the effect to the slot
	qalAuxiliaryEffectSloti( src->effectSlot, AL_EFFECTSLOT_EFFECT, src->effect );
	// Feed the slot from the source
	qalSource3i( src->source, AL_AUXILIARY_SEND_FILTER, src->effectSlot, 0, AL_FILTER_NULL );
}

void UnderwaterFlangerEffect::IntiallySetupEffect( src_t *src ) {
	Effect::IntiallySetupEffect( src );
	// This is the only place where the flanger gets tweaked
	qalEffectf( src->effect, AL_FLANGER_DEPTH, 0.5f );
	qalEffectf( src->effect, AL_FLANGER_FEEDBACK, -0.4f );
}

float UnderwaterFlangerEffect::GetMasterGain( src_t *src ) const {
	float gain = src->fvol * src->volumeVar->value;
	// Lower gain significantly if there is a medium transition
	// (if the listener is not in liquid and the source is, and vice versa)
	if( hasMediumTransition ) {
		gain *= 0.25f;
	}

	if( !s_attenuate_on_obstruction->integer ) {
		return gain;
	}

	// Modify the gain by the direct obstruction factor
	// Lowering the gain by 1/3 on full obstruction is fairly sufficient (its not linearly perceived)
	gain *= 1.0f - 0.33f * directObstruction;
	assert( gain >= 0.0f && gain <= 1.0f );
	return gain;
}

void UnderwaterFlangerEffect::BindOrUpdate( src_t *src ) {
	CheckCurrentlyBoundEffect( src );

	qalFilterf( src->directFilter, AL_LOWPASS_GAINHF, 1.0f - directObstruction );

	AttachEffect( src );
}

float ReverbEffect::GetMasterGain( src_t *src ) const {
	float gain = src->fvol * src->volumeVar->value;
	if( !s_attenuate_on_obstruction->integer ) {
		return gain;
	}

	// Both partial obstruction factors are within [0, 1] range, so we multiply by 0.5
	float obstructionFactor = 0.5f * ( this->directObstruction + this->secondaryRaysObstruction );
	assert( obstructionFactor >= 0.0f && obstructionFactor <= 1.0f );
	// Lowering the gain by 1/3 is enough
	gain *= 1.0f - 0.33f * obstructionFactor;
	assert( gain >= 0.0f && gain <= 1.0f );
	return gain;
}

void StandardReverbEffect::BindOrUpdate( src_t *src ) {
	CheckCurrentlyBoundEffect( src );

	qalEffectf( src->effect, AL_REVERB_DENSITY, this->density );
	qalEffectf( src->effect, AL_REVERB_DIFFUSION, this->diffusion );
	qalEffectf( src->effect, AL_REVERB_GAIN, this->gain );
	qalEffectf( src->effect, AL_REVERB_GAINHF, this->gainHf );
	qalEffectf( src->effect, AL_REVERB_DECAY_TIME, this->decayTime );
	qalEffectf( src->effect, AL_REVERB_REFLECTIONS_GAIN, this->reflectionsGain );
	qalEffectf( src->effect, AL_REVERB_REFLECTIONS_DELAY, this->reflectionsDelay );
	qalEffectf( src->effect, AL_REVERB_LATE_REVERB_DELAY, this->lateReverbDelay );

	qalFilterf( src->directFilter, AL_LOWPASS_GAINHF, 1.0f - directObstruction );

	AttachEffect( src );
}

void EaxReverbEffect::BindOrUpdate( src_t *src ) {
	CheckCurrentlyBoundEffect( src );

	qalEffectf( src->effect, AL_EAXREVERB_DENSITY, this->density );
	qalEffectf( src->effect, AL_EAXREVERB_DIFFUSION, this->diffusion );
	qalEffectf( src->effect, AL_EAXREVERB_GAIN, this->gain );
	qalEffectf( src->effect, AL_EAXREVERB_GAINHF, this->gainHf );
	qalEffectf( src->effect, AL_EAXREVERB_DECAY_TIME, this->decayTime );
	qalEffectf( src->effect, AL_EAXREVERB_REFLECTIONS_GAIN, this->reflectionsGain );
	qalEffectf( src->effect, AL_EAXREVERB_REFLECTIONS_DELAY, this->reflectionsDelay );
	qalEffectf( src->effect, AL_EAXREVERB_LATE_REVERB_DELAY, this->lateReverbDelay );

	qalFilterf( src->directFilter, AL_LOWPASS_GAINHF, 1.0f - directObstruction );

	AttachEffect( src );
}

void ChorusEffect::BindOrUpdate( struct src_s *src ) {
	CheckCurrentlyBoundEffect( src );

	qalEffecti( src->effect, AL_CHORUS_PHASE, phase );
	qalEffecti( src->effect, AL_CHORUS_WAVEFORM, waveform );

	qalEffectf( src->effect, AL_CHORUS_DELAY, delay );
	qalEffectf( src->effect, AL_CHORUS_DEPTH, depth );
	qalEffectf( src->effect, AL_CHORUS_RATE, rate );
	qalEffectf( src->effect, AL_CHORUS_FEEDBACK, feedback );

	qalFilterf( src->directFilter, AL_LOWPASS_GAINHF, 1.0f );

	AttachEffect( src );
}

void DistortionEffect::BindOrUpdate( struct src_s *src ) {
	CheckCurrentlyBoundEffect( src );

	qalEffectf( src->effect, AL_DISTORTION_EDGE, edge );
	qalEffectf( src->effect, AL_DISTORTION_EDGE, gain );

	qalFilterf( src->directFilter, AL_LOWPASS_GAINHF, 1.0f );

	AttachEffect( src );
}

void EchoEffect::BindOrUpdate( struct src_s *src ) {
	CheckCurrentlyBoundEffect( src );

	qalEffectf( src->effect, AL_ECHO_DELAY, delay );
	qalEffectf( src->effect, AL_ECHO_LRDELAY, lrDelay );
	qalEffectf( src->effect, AL_ECHO_DAMPING, damping );
	qalEffectf( src->effect, AL_ECHO_FEEDBACK, feedback );
	qalEffectf( src->effect, AL_ECHO_SPREAD, spread );

	qalFilterf( src->directFilter, AL_LOWPASS_GAINHF, 1.0f );

	AttachEffect( src );
}

/*
* source_spatialize
*/
static void source_spatialize( src_t *src ) {
	if( !src->attenuation ) {
		qalSourcei( src->source, AL_SOURCE_RELATIVE, AL_TRUE );
		// this was set at source_setup, no need to redo every frame
		//qalSourcefv( src->source, AL_POSITION, vec3_origin );
		//qalSourcefv( src->source, AL_VELOCITY, vec3_origin );
		return;
	}

	if( src->isTracking ) {
		VectorCopy( entlist[src->entNum].origin, src->origin );
		VectorCopy( entlist[src->entNum].velocity, src->velocity );
	}

	qalSourcei( src->source, AL_SOURCE_RELATIVE, AL_FALSE );
	qalSourcefv( src->source, AL_POSITION, src->origin );
	qalSourcefv( src->source, AL_VELOCITY, src->velocity );
}

/*
* source_loop
*/
static void source_loop( int priority, sfx_t *sfx, int entNum, float fvol, float attenuation ) {
	src_t *src;
	bool new_source = false;

	if( !sfx ) {
		return;
	}

	if( entNum < 0 || entNum >= max_ents ) {
		return;
	}

	// Do we need to start a new sound playing?
	if( !entlist[entNum].src ) {
		src = S_AllocSource( priority, entNum, 0 );
		if( !src ) {
			return;
		}
		new_source = true;
	} else if( entlist[entNum].src->sfx != sfx ) {
		// Need to restart. Just re-use this channel
		src = entlist[entNum].src;
		source_kill( src );
		new_source = true;
	} else {
		src = entlist[entNum].src;
	}

	if( new_source ) {
		source_setup( src, sfx, priority, entNum, -1, fvol, attenuation );
		qalSourcei( src->source, AL_LOOPING, AL_TRUE );
		src->isLooping = true;

		entlist[entNum].src = src;
	}

	S_AdjustGain( src );

	qalSourcef( src->source, AL_REFERENCE_DISTANCE, s_attenuation_refdistance );
	qalSourcef( src->source, AL_MAX_DISTANCE, s_attenuation_maxdistance );
	qalSourcef( src->source, AL_ROLLOFF_FACTOR, attenuation );

	if( new_source ) {
		if( src->attenuation ) {
			src->isTracking = true;
		}

		source_spatialize( src );

		qalSourcePlay( src->source );
	}

	entlist[entNum].touched = true;
}

static void S_ShutdownSourceEFX( src_t *src ) {
	if( src->directFilter ) {
		// Detach the filter from the source
		qalSourcei( src->source, AL_DIRECT_FILTER, AL_FILTER_NULL );
		qalDeleteFilters( 1, &src->directFilter );
		src->directFilter = 0;
	}

	if( src->effect && src->effectSlot ) {
		// Detach the effect from the source
		qalSource3i( src->source, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, 0 );
		// Detach the effect from the slot
		qalAuxiliaryEffectSloti( src->effectSlot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL );
	}

	if( src->effect ) {
		qalDeleteEffects( 1, &src->effect );
		src->effect = 0;
	}

	if( src->effectSlot ) {
		qalDeleteAuxiliaryEffectSlots( 1, &src->effectSlot );
		src->effectSlot = 0;
	}

	// Suppress errors if any
	qalGetError();
}

static bool S_InitSourceEFX( src_t *src ) {
	src->directFilter = 0;
	src->effect = 0;
	src->effectSlot = 0;

	qalGenFilters( 1, &src->directFilter );
	if( qalGetError() != AL_NO_ERROR ) {
		goto cleanup;
	}

	qalFilteri( src->directFilter, AL_FILTER_TYPE, AL_FILTER_LOWPASS );
	if( qalGetError() != AL_NO_ERROR ) {
		goto cleanup;
	}

	// Set default filter values (no actual attenuation)
	qalFilterf( src->directFilter, AL_LOWPASS_GAIN, 1.0f );
	qalFilterf( src->directFilter, AL_LOWPASS_GAINHF, 1.0f );

	// Attach the filter to the source
	qalSourcei( src->source, AL_DIRECT_FILTER, src->directFilter );
	if( qalGetError() != AL_NO_ERROR ) {
		goto cleanup;
	}

	qalGenEffects( 1, &src->effect );
	if( qalGetError() != AL_NO_ERROR ) {
		goto cleanup;
	}

	qalEffecti( src->effect, AL_EFFECT_TYPE, AL_EFFECT_REVERB );
	if( qalGetError() != AL_NO_ERROR ) {
		goto cleanup;
	}

	// Actually disable the reverb effect
	qalEffectf( src->effect, AL_REVERB_GAIN, 0.0f );
	if ( qalGetError() != AL_NO_ERROR ) {
		goto cleanup;
	}

	qalGenAuxiliaryEffectSlots( 1, &src->effectSlot );
	if( qalGetError() != AL_NO_ERROR ) {
		goto cleanup;
	}

	// Attach the effect to the slot
	qalAuxiliaryEffectSloti( src->effectSlot, AL_EFFECTSLOT_EFFECT, src->effect );
	if( qalGetError() != AL_NO_ERROR ) {
		goto cleanup;
	}

	// Feed the slot from the source
	qalSource3i( src->source, AL_AUXILIARY_SEND_FILTER, src->effectSlot, 0, AL_FILTER_NULL );
	if( qalGetError() != AL_NO_ERROR ) {
		goto cleanup;
	}

	return true;

cleanup:
	S_ShutdownSourceEFX( src );
	return false;
}

/*
* S_InitSources
*/
bool S_InitSources( int maxEntities, bool verbose ) {
	int i, j, maxSrc = MAX_SRC;
	bool useEfx = s_environment_effects->integer != 0;

	// Although we handle the failure of too many sources/effects allocation,
	// the AL library still prints an error message to stdout and it might be confusing.
	// Limit the number of sources (and attached effects) to this value a-priori.
	// This code also relies on recent versions on the library.
	// There still is a failure if a user tries to load a dated library.
	if ( useEfx && !strcmp( qalGetString( AL_VENDOR ), "OpenAL Community" ) ) {
		maxSrc = 64;
	}

	memset( srclist, 0, sizeof( srclist ) );
	src_count = 0;

	// Allocate as many sources as possible
	for( i = 0; i < maxSrc; i++ ) {
		qalGenSources( 1, &srclist[i].source );
		if( qalGetError() != AL_NO_ERROR ) {
			break;
		}

		if( useEfx ) {
			if( !S_InitSourceEFX( &srclist[i] ) ) {
				if( src_count >= 16 ) {
					// We have created a minimally acceptable sources/effects set.
					// Just delete an orphan source without corresponding effects and stop sources creation.
					qalDeleteSources( 1, &srclist[i].source );
					break;
				}

				Com_Printf( S_COLOR_YELLOW "Warning: Cannot create enough sound effects.\n" );
				Com_Printf( S_COLOR_YELLOW "Environment sound effects will be unavailable.\n" );
				Com_Printf( S_COLOR_YELLOW "Make sure you are using the recent OpenAL runtime.\n" );
				trap_Cvar_ForceSet( s_environment_effects->name, "0" );

				// Cleanup already created effects while keeping sources
				for( j = 0; j < src_count; ++j ) {
					S_ShutdownSourceEFX( &srclist[j] );
				}

				// Continue creating sources, now without corresponding effects
				useEfx = false;
			}
		}

		src_count++;
	}

	if( !src_count ) {
		return false;
	}

	if( verbose ) {
		Com_Printf( "allocated %d sources\n", src_count );
	}

	if( maxEntities < 1 ) {
		return false;
	}

	entlist = ( sentity_t * )S_Malloc( sizeof( sentity_t ) * maxEntities );
	max_ents = maxEntities;

	src_inited = true;
	return true;
}

/*
* S_ShutdownSources
*/
void S_ShutdownSources( void ) {
	int i;

	if( !src_inited ) {
		return;
	}

	// Destroy all the sources
	for( i = 0; i < src_count; i++ ) {
		// This call expects that the AL source is valid
		S_ShutdownSourceEFX( &srclist[i] );
		qalSourceStop( srclist[i].source );
		qalDeleteSources( 1, &srclist[i].source );
	}

	memset( srclist, 0, sizeof( srclist ) );

	S_Free( entlist );
	entlist = NULL;

	src_inited = false;
}

/*
* S_SetEntitySpatialization
*/
void S_SetEntitySpatialization( int entnum, const vec3_t origin, const vec3_t velocity ) {
	sentity_t *sent;

	if( entnum < 0 || entnum > max_ents ) {
		return;
	}

	sent = entlist + entnum;
	VectorCopy( origin, sent->origin );
	VectorCopy( velocity, sent->velocity );
}

/**
* A zombie is a source that still has an "active" flag but AL reports that it is stopped (is completed).
*/
static void S_ProcessZombieSources( src_t **zombieSources, int numZombieSources, int numActiveEffects, int64_t millisNow );

/*
* S_UpdateSources
*/
void S_UpdateSources( void ) {
	int i, entNum;
	ALint state;

	const int64_t millisNow = trap_Milliseconds();

	src_t *zombieSources[MAX_SRC];
	int numZombieSources = 0;
	int numActiveEffects = 0;

	for( i = 0; i < src_count; i++ ) {
		src_t *src = &srclist[i];
		if( !src->isActive ) {
			continue;
		}

		if( src->envUpdateState.effect ) {
			numActiveEffects++;
		}

		if( src->isLocked ) {
			continue;
		}

		if( src->volumeVar->modified ) {
			S_AdjustGain( &srclist[i] );
		}

		entNum = src->entNum;

		qalGetSourcei( src->source, AL_SOURCE_STATE, &state );
		if( state == AL_STOPPED ) {
			// If there is no effect attached, kill the source immediately.
			// Do not even bother adding this source to a list of zombie sources.
			if( !src->envUpdateState.effect ) {
				source_kill( src );
			} else {
				zombieSources[numZombieSources++] = src;
			}
			if( entNum >= 0 && entNum < max_ents ) {
				entlist[entNum].src = NULL;
			}
			src->entNum = -1;
			continue;
		}

		if( src->isLooping ) {
			// If a looping effect hasn't been touched this frame, kill it
			// Note: lingering produces bad results in this case
			if( !entlist[entNum].touched ) {
				// Don't even bother adding this source to a list of zombie sources...
				source_kill( &srclist[i] );
				// Do not misinform zombies processing logic
				if( src->envUpdateState.effect ) {
					numActiveEffects--;
				}
				entlist[entNum].src = NULL;
			} else {
				entlist[entNum].touched = false;
			}
		}

		source_spatialize( src );
	}

	S_ProcessZombieSources( zombieSources, numZombieSources, numActiveEffects, millisNow );
}

/**
* A zombie is a source that still has an "active" flag but AL reports that it is stopped (is completed).
*/
static void S_ProcessZombieSources( src_t **zombieSources, int numZombieSources, int numActiveEffects, int64_t millisNow ) {
	// First, kill all sources with expired lingering timeout
	for( int i = 0; i < numZombieSources; ) {
		src_t *const src = zombieSources[i];
		// Adding a source to "zombies" list makes sense only for sources with attached effects
		assert( src->envUpdateState.effect );

		// If the source is not lingering, set the lingering state
		if( !src->isLingering ) {
			src->isLingering = true;
			src->lingeringTimeoutAt = millisNow + src->envUpdateState.effect->GetLingeringTimeout();
			i++;
			continue;
		}

		// If the source lingering timeout has not expired
		if( src->lingeringTimeoutAt > millisNow ) {
			i++;
			continue;
		}

		source_kill( src );
		// Replace the current array cell by the last one, and repeat testing this cell next iteration
		zombieSources[i] = zombieSources[numZombieSources - 1];
		numZombieSources--;
		numActiveEffects--;
	}

	// Now we know an actual number of zombie sources and active effects left.
	// Aside from that, all zombie sources left in list are lingering.

	int effectsNumberThreshold = s_effects_number_threshold->integer;
	if( effectsNumberThreshold < 8 ) {
		effectsNumberThreshold = 8;
		trap_Cvar_ForceSet( s_effects_number_threshold->name, "8" );
	} else if( effectsNumberThreshold > 32 ) {
		effectsNumberThreshold = 32;
		trap_Cvar_ForceSet( s_effects_number_threshold->name, "32" );
	}

	if( numActiveEffects <= effectsNumberThreshold ) {
		return;
	}

	auto zombieSourceComparator = [=]( const src_t *lhs, const src_t *rhs ) {
		// Let sounds that have a lower quality hint be evicted first from the max heap
		// (The natural comparison order uses the opposite sign).
		return lhs->sfx->qualityHint > rhs->sfx->qualityHint;
	};

	std::make_heap( zombieSources, zombieSources + numZombieSources, zombieSourceComparator );

	// Cache results of Effect::ShouldKeepLingering() calls
	bool keepEffectLingering[MAX_SRC];
	memset( keepEffectLingering, 0, sizeof( keepEffectLingering ) );

	// Prefer copying globals to locals if they're accessed in loop
	// (an access to globals is indirect and is performed via "global relocation table")
	src_t *const srcBegin = srclist;

	for(;; ) {
		if( numActiveEffects <= effectsNumberThreshold ) {
			return;
		}
		if( numZombieSources <= 0 ) {
			break;
		}

		std::pop_heap( zombieSources, zombieSources + numZombieSources, zombieSourceComparator );
		src_t *const src = zombieSources[numZombieSources - 1];
		numZombieSources--;

		if( src->envUpdateState.effect->ShouldKeepLingering( src->sfx->qualityHint, millisNow ) ) {
			keepEffectLingering[src - srcBegin] = true;
			continue;
		}

		source_kill( src );
		numActiveEffects--;
	}

	// Start disabling effects completely.
	// This is fairly slow path but having excessive active effects count is much worse.
	// Note that effects status might have been changed.

	vec3_t listenerOrigin;
	qalGetListener3f( AL_POSITION, listenerOrigin + 0, listenerOrigin + 1, listenerOrigin + 2 );

	src_t *disableEffectCandidates[MAX_SRC];
	float sourceScores[MAX_SRC];
	int numDisableEffectCandidates = 0;

	for( src_t *src = srcBegin; src != srcBegin + MAX_SRC; ++src ) {
		if( !src->isActive ) {
			continue;
		}
		if( !src->envUpdateState.effect ) {
			continue;
		}
		// If it was considered to keep effect lingering, do not touch the effect even if we want to disable some
		if( keepEffectLingering[src - srcBegin] ) {
			continue;
		}

		float squareDistance = DistanceSquared( listenerOrigin, src->origin );
		if( squareDistance < 72 * 72 ) {
			continue;
		}

		disableEffectCandidates[numDisableEffectCandidates++] = src;
		float evictionScore = sqrtf( squareDistance );
		evictionScore *= 1.0f / ( 0.5f + src->sfx->qualityHint );
		// Give looping sources higher priority, otherwise it might sound weird
		// if most of sources become inactive but the looping sound does not have an effect.
		evictionScore *= src->isLooping ? 0.5f : 1.0f;
		sourceScores[src - srcBegin] = evictionScore;
	}

	// Use capture by reference, MSVC tries to capture an array by value and consequently fails
	auto disableEffectComparator = [&]( const src_t *lhs, const src_t *rhs ) {
		// Keep the natural order, a value with greater eviction score should be evicted first
		return sourceScores[lhs - srcBegin] > sourceScores[rhs - srcBegin];
	};

	std::make_heap( disableEffectCandidates, disableEffectCandidates + numDisableEffectCandidates, disableEffectComparator );

	for(;; ) {
		if( numActiveEffects <= effectsNumberThreshold ) {
			break;
		}
		if( numDisableEffectCandidates < 0 ) {
			break;
		}

		std::pop_heap( disableEffectCandidates, disableEffectCandidates + numDisableEffectCandidates, disableEffectComparator );
		src_t *src = disableEffectCandidates[numDisableEffectCandidates - 1];
		numDisableEffectCandidates--;

		ENV_UnregisterSource( src );
		numActiveEffects--;
	}
}


/*
* S_AllocSource
*/
src_t *S_AllocSource( int priority, int entNum, int channel ) {
	int i;
	int empty = -1;
	int weakest = -1;
	int64_t weakest_time = trap_Milliseconds();
	int weakest_priority = priority;

	for( i = 0; i < src_count; i++ ) {
		if( srclist[i].isLocked ) {
			continue;
		}

		if( !srclist[i].isActive && ( empty == -1 ) ) {
			empty = i;
		}

		if( srclist[i].priority < weakest_priority ||
			( srclist[i].priority == weakest_priority && srclist[i].lastUse < weakest_time ) ) {
			weakest_priority = srclist[i].priority;
			weakest_time = srclist[i].lastUse;
			weakest = i;
		}

		// Is it an exact match, and not on channel 0?
		if( ( srclist[i].entNum == entNum ) && ( srclist[i].channel == channel ) && ( channel != 0 ) ) {
			source_kill( &srclist[i] );
			return &srclist[i];
		}
	}

	if( empty != -1 ) {
		return &srclist[empty];
	}

	if( weakest != -1 ) {
		source_kill( &srclist[weakest] );
		return &srclist[weakest];
	}

	return NULL;
}

/*
* S_LockSource
*/
void S_LockSource( src_t *src ) {
	src->isLocked = true;
}

/*
* S_UnlockSource
*/
void S_UnlockSource( src_t *src ) {
	src->isLocked = false;
}

/*
* S_UnlockSource
*/
void S_KeepSourceAlive( src_t *src, bool alive ) {
	src->keepAlive = alive;
}

/*
* S_GetALSource
*/
ALuint S_GetALSource( const src_t *src ) {
	return src->source;
}

/*
* S_StartLocalSound
*/
void S_StartLocalSound( sfx_t *sfx, float fvol ) {
	src_t *src;

	if( !sfx ) {
		return;
	}

	src = S_AllocSource( SRCPRI_LOCAL, -1, 0 );
	if( !src ) {
		return;
	}

	S_UseBuffer( sfx );

	source_setup( src, sfx, SRCPRI_LOCAL, -1, 0, fvol, ATTN_NONE );
	qalSourcei( src->source, AL_SOURCE_RELATIVE, AL_TRUE );

	qalSourcePlay( src->source );
}

/*
* S_StartSound
*/
static void S_StartSound( sfx_t *sfx, const vec3_t origin, int entNum, int channel, float fvol, float attenuation ) {
	src_t *src;

	if( !sfx ) {
		return;
	}

	src = S_AllocSource( SRCPRI_ONESHOT, entNum, channel );
	if( !src ) {
		return;
	}

	source_setup( src, sfx, SRCPRI_ONESHOT, entNum, channel, fvol, attenuation );

	if( src->attenuation ) {
		if( origin ) {
			VectorCopy( origin, src->origin );
		} else {
			src->isTracking = true;
		}
	}

	source_spatialize( src );

	qalSourcePlay( src->source );
}

/*
* S_StartFixedSound
*/
void S_StartFixedSound( sfx_t *sfx, const vec3_t origin, int channel, float fvol, float attenuation ) {
	S_StartSound( sfx, origin, 0, channel, fvol, attenuation );
}

/*
* S_StartRelativeSound
*/
void S_StartRelativeSound( sfx_t *sfx, int entnum, int channel, float fvol, float attenuation ) {
	S_StartSound( sfx, NULL, entnum, channel, fvol, attenuation );
}

/*
* S_StartGlobalSound
*/
void S_StartGlobalSound( sfx_t *sfx, int channel, float fvol ) {
	S_StartSound( sfx, NULL, 0, channel, fvol, ATTN_NONE );
}

/*
* S_AddLoopSound
*/
void S_AddLoopSound( sfx_t *sfx, int entnum, float fvol, float attenuation ) {
	source_loop( SRCPRI_LOOP, sfx, entnum, fvol, attenuation );
}

/*
* S_AllocRawSource
*/
src_t *S_AllocRawSource( int entNum, float fvol, float attenuation, cvar_t *volumeVar ) {
	src_t *src;

	if( !volumeVar ) {
		volumeVar = s_volume;
	}

	src = S_AllocSource( SRCPRI_STREAM, entNum, 0 );
	if( !src ) {
		return NULL;
	}

	source_setup( src, NULL, SRCPRI_STREAM, entNum, 0, fvol, attenuation );

	if( src->attenuation && entNum > 0 ) {
		src->isTracking = true;
	}

	src->volumeVar = volumeVar;
	S_AdjustGain( src );

	source_spatialize( src );
	return src;
}

/*
* S_StopAllSources
*/
void S_StopAllSources( void ) {
	int i;

	for( i = 0; i < src_count; i++ )
		source_kill( &srclist[i] );
}
