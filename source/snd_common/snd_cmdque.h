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

#ifndef SND_CMDQUEUE_H
#define SND_CMDQUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

#define SND_COMMANDS_BUFSIZE    0x100000

#define SND_SPATIALIZE_ENTS_MAX 8

typedef struct qbufPipe_s sndCmdPipe_t;
typedef unsigned (*pipeCmdHandler_t)( const void * );

enum {
	SND_CMD_INIT,
	SND_CMD_SHUTDOWN,
	SND_CMD_CLEAR,
	SND_CMD_STOP_ALL_SOUNDS,
	SND_CMD_FREE_SFX,
	SND_CMD_LOAD_SFX,
	SND_CMD_SET_ATTENUATION_MODEL,
	SND_CMD_SET_ENTITY_SPATIALIZATION,
	SND_CMD_SET_LISTENER,
	SND_CMD_START_LOCAL_SOUND,
	SND_CMD_START_FIXED_SOUND,
	SND_CMD_START_GLOBAL_SOUND,
	SND_CMD_START_RELATIVE_SOUND,
	SND_CMD_START_BACKGROUND_TRACK,
	SND_CMD_STOP_BACKGROUND_TRACK,
	SND_CMD_LOCK_BACKGROUND_TRACK,
	SND_CMD_ADD_LOOP_SOUND,
	SND_CMD_ADVANCE_BACKGROUND_TRACK,
	SND_CMD_PAUSE_BACKGROUND_TRACK,
	SND_CMD_ACTIVATE,
	SND_CMD_AVI_DEMO,
	SND_CMD_RAW_SAMPLES,
	SND_CMD_POSITIONED_RAW_SAMPLES,
	SND_CMD_STUFFCMD,
	SND_CMD_SET_MUL_ENTITY_SPATIALIZATION,

	SND_CMD_NUM_CMDS
};

typedef struct {
	int entnum;
	float origin[3];
	float velocity[3];
} smdCmdSpatialization_t;

typedef struct {
	int id;
	void *hwnd;
	int maxents;
	int verbose;
} sndCmdInit_t;

typedef struct {
	int id;
	int verbose;
} sndCmdShutdown_t;

typedef struct {
	int id;
} sndCmdClear_t;

typedef struct {
	int id;
	int clear;
	int stopMusic;
} sndCmdStop_t;

typedef struct {
	int id;
	int sfx;
} sndCmdFreeSfx_t;

typedef struct {
	int id;
	int sfx;
} sndCmdLoadSfx_t;

typedef struct {
	int id;
	int model;
	float maxdistance;
	float refdistance;
} sndCmdSetAttenuationModel_t;

typedef struct {
	int id;
	int entnum;
	float origin[3];
	float velocity[3];
} sndCmdSetEntitySpatialization_t;

typedef struct {
	int id;
	float origin[3];
	float velocity[3];
	float axis[9];
	int avidump;
} sndCmdSetListener_t;

typedef struct {
	int id;
	int sfx;
	float fvol;
} sndCmdStartLocalSound_t;

typedef struct {
	int id;
	int sfx;
	float origin[3];
	int channel;
	float fvol;
	float attenuation;
} sndCmdStartFixedSound_t;

typedef struct {
	int id;
	int sfx;
	int entnum;
	int channel;
	float fvol;
	float attenuation;
} sndCmdStartRelativeSound_t;

typedef struct {
	int id;
	int sfx;
	int channel;
	float fvol;
} sndCmdStartGlobalSound_t;

typedef struct {
	int id;
	char intro[64];
	char loop[64];
	int mode;
} sndCmdStartBackgroundTrack_t;

typedef struct {
	int id;
} sndCmdStopBackgroundTrack_t;

typedef struct {
	int id;
	int lock;
} sndCmdLockBackgroundTrack_t;

typedef struct {
	int id;
	int sfx;
	float fvol;
	float attenuation;
	int entnum;
} sndAddLoopSoundCmd_t;

typedef struct {
	int id;
	int val;
} sndAdvanceBackgroundTrackCmd_t;

typedef struct {
	int id;
} sndPauseBackgroundTrackCmd_t;

typedef struct {
	int id;
	int active;
} sndActivateCmd_t;

typedef struct {
	int id;
	int begin;
} sndAviDemo_t;

typedef struct {
	int id;
	unsigned int samples;
	unsigned int rate;
	unsigned short width;
	unsigned short channels;
	uint8_t *data;
	bool music;
} sndRawSamplesCmd_t;

typedef struct {
	int id;
	int entnum;
	float fvol;
	float attenuation;
	unsigned int samples;
	unsigned int rate;
	unsigned short width;
	unsigned short channels;
	uint8_t *data;
} sndPositionedRawSamplesCmd_t;

typedef struct {
	int id;
	char text[80];
} sndStuffCmd_t;

typedef struct {
	int id;
	unsigned numents;
	int entnum[SND_SPATIALIZE_ENTS_MAX];
	float origin[SND_SPATIALIZE_ENTS_MAX][3];
	float velocity[SND_SPATIALIZE_ENTS_MAX][3];
} sndCmdSetMulEntitySpatialization_t;

sndCmdPipe_t *S_CreateSoundCmdPipe( void );
void S_DestroySoundCmdPipe( sndCmdPipe_t **pqueue );
int S_ReadEnqueuedCmds( sndCmdPipe_t *queue, pipeCmdHandler_t *cmdHandlers );
void S_WaitEnqueuedCmds( qbufPipe_t *queue, int ( *read )( qbufPipe_t *, unsigned( ** )( const void * ), bool ),
						 unsigned( **cmdHandlers )( const void * ), unsigned timeout_msec );
void S_FinishSoundCmdPipe( sndCmdPipe_t *queue );

void S_IssueInitCmd( sndCmdPipe_t *queue, void *hwnd, int maxents, bool verbose );
void S_IssueShutdownCmd( sndCmdPipe_t *queue, bool verbose );
void S_IssueClearCmd( sndCmdPipe_t *queue );
void S_IssueStopAllSoundsCmd( sndCmdPipe_t *queue, bool clear, bool stopMusic );
void S_IssueFreeSfxCmd( sndCmdPipe_t *queue, int sfx );
void S_IssueLoadSfxCmd( sndCmdPipe_t *queue, int sfx );
void S_IssueSetAttenuationCmd( sndCmdPipe_t *queue, int model,
							   float maxdistance, float refdistance );
void S_IssueSetEntitySpatializationCmd( sndCmdPipe_t *queue, const smdCmdSpatialization_t *spat );
void S_IssueSetListenerCmd( sndCmdPipe_t *queue, const vec3_t origin,
							const vec3_t velocity, const mat3_t axis, bool avidump );
void S_IssueStartLocalSoundCmd( sndCmdPipe_t *queue, int sfx, float fvol );
void S_IssueStartFixedSoundCmd( sndCmdPipe_t *queue, int sfx, const vec3_t origin,
								int channel, float fvol, float attenuation );
void S_IssueStartGlobalSoundCmd( sndCmdPipe_t *queue, int sfx, int channel,
								 float fvol );
void S_IssueStartRelativeSoundCmd( sndCmdPipe_t *queue, int sfx, int entnum,
								   int channel, float fvol, float attenuation );
void S_IssueStartBackgroundTrackCmd( sndCmdPipe_t *queue, const char *intro,
									 const char *loop, int mode );
void S_IssueStopBackgroundTrackCmd( sndCmdPipe_t *queue );
void S_IssueLockBackgroundTrackCmd( sndCmdPipe_t *queue, bool lock );
void S_IssueAddLoopSoundCmd( sndCmdPipe_t *queue, int sfx, int entnum,
							 float fvol, float attenuation );
void S_IssueAdvanceBackgroundTrackCmd( sndCmdPipe_t *queue, int val );
void S_IssuePauseBackgroundTrackCmd( sndCmdPipe_t *queue );
void S_IssueActivateCmd( sndCmdPipe_t *queue, bool active );
void S_IssueAviDemoCmd( sndCmdPipe_t *queue, bool begin );
void S_IssueRawSamplesCmd( sndCmdPipe_t *queue, unsigned int samples,
						   unsigned int rate, unsigned short width, unsigned short channels,
						   uint8_t *data, bool music );
void S_IssuePositionedRawSamplesCmd( sndCmdPipe_t *queue, int entnum,
									 float fvol, float attenuation, unsigned int samples, unsigned int rate,
									 unsigned short width, unsigned short channels, uint8_t *data );
void S_IssueStuffCmd( sndCmdPipe_t *queue, const char *text );
void S_IssueSetMulEntitySpatializationCmd( sndCmdPipe_t *queue, unsigned numEnts,
										   const smdCmdSpatialization_t *spat );

#ifdef __cplusplus
}
#endif

#endif // SND_CMDQUEUE_H
