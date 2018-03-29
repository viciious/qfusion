/*
Copyright (C) 2014 Victor Luchits

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

#ifdef __cplusplus
extern "C" {
#endif

// =====================================================================

/*
* S_CreateSoundCmdPipe
*/
sndCmdPipe_t *S_CreateSoundCmdPipe( void ) {
	return trap_BufPipe_Create( SND_COMMANDS_BUFSIZE, 0 );
}

/*
* S_DestroySoundCmdPipe
*/
void S_DestroySoundCmdPipe( sndCmdPipe_t **pqueue ) {
	trap_BufPipe_Destroy( pqueue );
}

/*
* S_FinishSoundCmdPipe
*
* Blocks until the reader thread handles all commands
* or terminates with an error.
*/
void S_FinishSoundCmdPipe( sndCmdPipe_t *queue ) {
	trap_BufPipe_Finish( queue );
}

/*
* S_EnqueueCmd
*/
static void S_EnqueueCmd( sndCmdPipe_t *queue, const void *cmd, unsigned cmd_size ) {
	trap_BufPipe_WriteCmd( queue, cmd, cmd_size );
}

/*
* S_IssueInitCmd
*/
void S_IssueInitCmd( sndCmdPipe_t *queue, void *hwnd, int maxents, bool verbose ) {
	sndCmdInit_t cmd;
	cmd.id = SND_CMD_INIT;
	cmd.hwnd = hwnd;
	cmd.maxents = maxents;
	cmd.verbose = verbose == true ? 1 : 0;
	S_EnqueueCmd( queue, &cmd, sizeof( cmd ) );
}

/*
* S_IssueShutdownCmd
*/
void S_IssueShutdownCmd( sndCmdPipe_t *queue, bool verbose ) {
	sndCmdShutdown_t cmd;
	cmd.id = SND_CMD_SHUTDOWN;
	cmd.verbose = verbose == true ? 1 : 0;
	S_EnqueueCmd( queue, &cmd, sizeof( cmd ) );
}

/*
* S_IssueClearCmd
*/
void S_IssueClearCmd( sndCmdPipe_t *queue ) {
	sndCmdClear_t cmd;
	cmd.id = SND_CMD_CLEAR;
	S_EnqueueCmd( queue, &cmd, sizeof( cmd ) );
}

/*
* S_IssueStopAllSoundsCmd
*/
void S_IssueStopAllSoundsCmd( sndCmdPipe_t *queue, bool clear, bool stopMusic ) {
	sndCmdStop_t cmd;
	cmd.id = SND_CMD_STOP_ALL_SOUNDS;
	cmd.clear = clear;
	cmd.stopMusic = stopMusic;
	S_EnqueueCmd( queue, &cmd, sizeof( cmd ) );
}

/*
* S_IssueFreeSfxCmd
*/
void S_IssueFreeSfxCmd( sndCmdPipe_t *queue, int sfx ) {
	sndCmdFreeSfx_t cmd;
	cmd.id = SND_CMD_FREE_SFX;
	cmd.sfx = sfx;
	S_EnqueueCmd( queue, &cmd, sizeof( cmd ) );
}

/*
* S_IssueLoadSfxCmd
*/
void S_IssueLoadSfxCmd( sndCmdPipe_t *queue, int sfx ) {
	sndCmdLoadSfx_t cmd;
	cmd.id = SND_CMD_LOAD_SFX;
	cmd.sfx = sfx;
	S_EnqueueCmd( queue, &cmd, sizeof( cmd ) );
}

/*
* S_IssueSetAttenuationCmd
*/
void S_IssueSetAttenuationCmd( sndCmdPipe_t *queue, int model,
							   float maxdistance, float refdistance ) {
	sndCmdSetAttenuationModel_t cmd;
	cmd.id = SND_CMD_SET_ATTENUATION_MODEL;
	cmd.model = model;
	cmd.maxdistance = maxdistance;
	cmd.refdistance = refdistance;
	S_EnqueueCmd( queue, &cmd, sizeof( cmd ) );
}

/*
* S_IssueSetEntitySpatializationCmd
*/
void S_IssueSetEntitySpatializationCmd( sndCmdPipe_t *queue, const smdCmdSpatialization_t *spat ) {
	unsigned i;

	sndCmdSetEntitySpatialization_t cmd;
	cmd.id = SND_CMD_SET_ENTITY_SPATIALIZATION;
	cmd.entnum = spat->entnum;
	for( i = 0; i < 3; i++ ) {
		cmd.origin[i] = spat->origin[i];
		cmd.velocity[i] = spat->velocity[i];
	}

	S_EnqueueCmd( queue, &cmd, sizeof( cmd ) );
}

/*
* S_IssueSetListenerCmd
*/
void S_IssueSetListenerCmd( sndCmdPipe_t *queue, const vec3_t origin,
							const vec3_t velocity, const mat3_t axis, bool avidump ) {
	unsigned i;

	sndCmdSetListener_t cmd;
	cmd.id = SND_CMD_SET_LISTENER;
	cmd.avidump = (int)avidump;
	for( i = 0; i < 3; i++ ) {
		cmd.origin[i] = origin[i];
		cmd.velocity[i] = velocity[i];
	}
	for( i = 0; i < 9; i++ ) {
		cmd.axis[i] = axis[i];
	}

	S_EnqueueCmd( queue, &cmd, sizeof( cmd ) );
}

/*
* S_IssueStartLocalSoundCmd
*/
void S_IssueStartLocalSoundCmd( sndCmdPipe_t *queue, int sfx, float fvol ) {
	sndCmdStartLocalSound_t cmd;
	cmd.id = SND_CMD_START_LOCAL_SOUND;
	cmd.sfx = sfx;
	cmd.fvol = fvol;
	S_EnqueueCmd( queue, &cmd, sizeof( cmd ) );
}

/*
* S_IssueStartFixedSoundCmd
*/
void S_IssueStartFixedSoundCmd( sndCmdPipe_t *queue, int sfx, const vec3_t origin,
								int channel, float fvol, float attenuation ) {
	unsigned i;

	sndCmdStartFixedSound_t cmd;
	cmd.id = SND_CMD_START_FIXED_SOUND;
	cmd.sfx = sfx;
	for( i = 0; i < 3; i++ ) {
		cmd.origin[i] = origin[i];
	}
	cmd.channel = channel;
	cmd.fvol = fvol;
	cmd.attenuation = attenuation;

	S_EnqueueCmd( queue, &cmd, sizeof( cmd ) );
}

/*
* S_IssueStartGlobalSoundCmd
*/
void S_IssueStartGlobalSoundCmd( sndCmdPipe_t *queue, int sfx, int channel,
								 float fvol ) {
	sndCmdStartGlobalSound_t cmd;
	cmd.id = SND_CMD_START_GLOBAL_SOUND;
	cmd.sfx = sfx;
	cmd.channel = channel;
	cmd.fvol = fvol;
	S_EnqueueCmd( queue, &cmd, sizeof( cmd ) );
}

/*
* S_IssueStartRelativeSoundCmd
*/
void S_IssueStartRelativeSoundCmd( sndCmdPipe_t *queue, int sfx, int entnum,
								   int channel, float fvol, float attenuation ) {
	sndCmdStartRelativeSound_t cmd;
	cmd.id = SND_CMD_START_RELATIVE_SOUND;
	cmd.sfx = sfx;
	cmd.entnum = entnum;
	cmd.channel = channel;
	cmd.fvol = fvol;
	cmd.attenuation = attenuation;
	S_EnqueueCmd( queue, &cmd, sizeof( cmd ) );
}

/*
* S_IssueStartBackgroundTrackCmd
*/
void S_IssueStartBackgroundTrackCmd( sndCmdPipe_t *queue, const char *intro,
									 const char *loop, int mode ) {
	sndCmdStartBackgroundTrack_t cmd;

	cmd.id = SND_CMD_START_BACKGROUND_TRACK;
	Q_strncpyz( cmd.intro, intro ? intro : "", sizeof( cmd.intro ) );
	Q_strncpyz( cmd.loop, loop ? loop : "", sizeof( cmd.loop ) );
	cmd.mode = mode;

	S_EnqueueCmd( queue, &cmd, sizeof( cmd ) );
}

/*
* S_IssueStopBackgroundTrackCmd
*/
void S_IssueStopBackgroundTrackCmd( sndCmdPipe_t *queue ) {
	sndCmdStopBackgroundTrack_t cmd;
	cmd.id = SND_CMD_STOP_BACKGROUND_TRACK;
	S_EnqueueCmd( queue, &cmd, sizeof( cmd ) );
}

/*
* S_IssueLockBackgroundTrackCmd
*/
void S_IssueLockBackgroundTrackCmd( sndCmdPipe_t *queue, bool lock ) {
	sndCmdLockBackgroundTrack_t cmd;
	cmd.id = SND_CMD_LOCK_BACKGROUND_TRACK;
	cmd.lock = lock == true ? 1 : 0;
	S_EnqueueCmd( queue, &cmd, sizeof( cmd ) );
}

/*
* S_IssueAddLoopSoundCmd
*/
void S_IssueAddLoopSoundCmd( sndCmdPipe_t *queue, int sfx, int entnum,
							 float fvol, float attenuation ) {
	sndAddLoopSoundCmd_t cmd;
	cmd.id = SND_CMD_ADD_LOOP_SOUND;
	cmd.sfx = sfx;
	cmd.entnum = entnum;
	cmd.fvol = fvol;
	cmd.attenuation = attenuation;
	S_EnqueueCmd( queue, &cmd, sizeof( cmd ) );
}

/*
* S_IssueAdvanceBackgroundTrackCmd
*/
void S_IssueAdvanceBackgroundTrackCmd( sndCmdPipe_t *queue, int val ) {
	sndAdvanceBackgroundTrackCmd_t cmd;
	cmd.id = SND_CMD_ADVANCE_BACKGROUND_TRACK;
	cmd.val = val;
	S_EnqueueCmd( queue, &cmd, sizeof( cmd ) );
}

/*
* S_IssuePauseBackgroundTrackCmd
*/
void S_IssuePauseBackgroundTrackCmd( sndCmdPipe_t *queue ) {
	sndPauseBackgroundTrackCmd_t cmd;
	cmd.id = SND_CMD_PAUSE_BACKGROUND_TRACK;
	S_EnqueueCmd( queue, &cmd, sizeof( cmd ) );
}

/*
* S_IssueActivateCmd
*/
void S_IssueActivateCmd( sndCmdPipe_t *queue, bool active ) {
	sndActivateCmd_t cmd;
	cmd.id = SND_CMD_ACTIVATE;
	cmd.active = active == true ? 1 : 0;
	S_EnqueueCmd( queue, &cmd, sizeof( cmd ) );
}

/*
* S_IssueAviDemoCmd
*/
void S_IssueAviDemoCmd( sndCmdPipe_t *queue, bool begin ) {
	sndAviDemo_t cmd;
	cmd.id = SND_CMD_AVI_DEMO;
	cmd.begin = begin ? 1 : 0;
	S_EnqueueCmd( queue, &cmd, sizeof( cmd ) );
}

/*
* S_IssueRawSamplesCmd
*/
void S_IssueRawSamplesCmd( sndCmdPipe_t *queue, unsigned int samples,
						   unsigned int rate, unsigned short width, unsigned short channels,
						   uint8_t *data, bool music ) {
	sndRawSamplesCmd_t cmd;
	cmd.id = SND_CMD_RAW_SAMPLES;
	cmd.samples = samples;
	cmd.rate = rate;
	cmd.width = width;
	cmd.channels = channels;
	cmd.data = data;
	cmd.music = music;
	S_EnqueueCmd( queue, &cmd, sizeof( cmd ) );
}

/*
* S_IssuePositionedRawSamplesCmd
*/
void S_IssuePositionedRawSamplesCmd( sndCmdPipe_t *queue, int entnum,
									 float fvol, float attenuation, unsigned int samples, unsigned int rate,
									 unsigned short width, unsigned short channels, uint8_t *data ) {
	sndPositionedRawSamplesCmd_t cmd;
	cmd.id = SND_CMD_POSITIONED_RAW_SAMPLES;
	cmd.entnum = entnum;
	cmd.fvol = fvol;
	cmd.attenuation = attenuation;
	cmd.samples = samples;
	cmd.rate = rate;
	cmd.width = width;
	cmd.channels = channels;
	cmd.data = data;
	S_EnqueueCmd( queue, &cmd, sizeof( cmd ) );
}

/*
* S_IssueStuffCmd
*/
void S_IssueStuffCmd( sndCmdPipe_t *queue, const char *text ) {
	sndStuffCmd_t cmd;
	cmd.id = SND_CMD_STUFFCMD;
	Q_strncpyz( cmd.text, text, sizeof( cmd.text ) );
	S_EnqueueCmd( queue, &cmd, sizeof( cmd ) );
}

/*
* S_IssueSetMulEntitySpatializationCmd
*/
void S_IssueSetMulEntitySpatializationCmd( sndCmdPipe_t *queue, unsigned numEnts,
										   const smdCmdSpatialization_t *spat ) {
	unsigned i, j;

	for( j = 0; j < numEnts; ) {
		unsigned n;
		sndCmdSetMulEntitySpatialization_t cmd;

		cmd.id = SND_CMD_SET_MUL_ENTITY_SPATIALIZATION;
		cmd.numents = numEnts - j;
		if( cmd.numents > SND_SPATIALIZE_ENTS_MAX ) {
			cmd.numents = SND_SPATIALIZE_ENTS_MAX;
		}

		for( n = 0; n < cmd.numents; n++ ) {
			cmd.entnum[n] = spat[n].entnum;
			for( i = 0; i < 3; i++ ) {
				cmd.origin[n][i] = spat[n].origin[i];
				cmd.velocity[n][i] = spat[n].velocity[i];
			}
		}

		S_EnqueueCmd( queue, &cmd, sizeof( cmd ) );

		j += cmd.numents;
	}
}

/*
* S_ReadEnqueuedCmds
*/
int S_ReadEnqueuedCmds( sndCmdPipe_t *queue, pipeCmdHandler_t *cmdHandlers ) {
	return trap_BufPipe_ReadCmds( queue, cmdHandlers );
}

/*
* S_WaitEnqueuedCmds
*/
void S_WaitEnqueuedCmds( sndCmdPipe_t *queue, int ( *read )( sndCmdPipe_t *, unsigned( ** )( const void * ), bool ),
						 unsigned( **cmdHandlers )( const void * ), unsigned timeout_msec ) {
	trap_BufPipe_Wait( queue, read, cmdHandlers, timeout_msec );
}

#ifdef __cplusplus
}
#endif