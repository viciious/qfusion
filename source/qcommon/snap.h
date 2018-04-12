#ifndef QFUSION_SNAP_H
#define QFUSION_SNAP_H

#ifdef __cplusplus
extern "C" {
#endif

#define SNAP_MAX_DEMO_META_DATA_SIZE    16 * 1024

// define this 0 to disable compression of demo files
#define SNAP_DEMO_GZ                    FS_GZ

void SNAP_ParseBaseline( struct msg_s *msg, entity_state_t *baselines );
void SNAP_SkipFrame( struct msg_s *msg, struct snapshot_s *header );
struct snapshot_s *SNAP_ParseFrame( struct msg_s *msg, struct snapshot_s *lastFrame, int *suppressCount,
									struct snapshot_s *backup, entity_state_t *baselines, int showNet );

void SNAP_WriteFrameSnapToClient( struct ginfo_s *gi, struct client_s *client, struct msg_s *msg,
								  int64_t frameNum, int64_t gameTime,
								  entity_state_t *baselines, struct client_entities_s *client_entities,
								  int numcmds, gcommand_t *commands, const char *commandsData );

// Use PVS culling for sounds.
// Note: changes gameplay experience, use with caution.
#define SNAP_HINT_CULL_SOUND_WITH_PVS     ( 1 )
// Try determining visibility via raycasting in collision world.
// Might lead to false negatives and heavy CPU load,
// but overall is recommended to use in untrusted environments.
#define SNAP_HINT_USE_RAYCAST_CULLING     ( 2 )
// Cull entities that are not in the front hemisphere of viewer.
// Currently it is a last hope against cheaters in an untrusted environment,
// but would be useful for slowly-turning vehicles in future.
#define SNAP_HINT_USE_VIEW_DIR_CULLING    ( 4 )
// If an entity would have been culled without attached sound events,
// but cannot be culled due to mentioned attachments presence,
// transmit fake angles, etc. to confuse a wallhack user
#define SNAP_HINT_SHADOW_EVENTS_DATA      ( 8 )

// Names for vars corresponding to the hint flags
#define SNAP_VAR_CULL_SOUND_WITH_PVS     ( "sv_snap_aggressive_sound_culling" )
#define SNAP_VAR_USE_RAYCAST_CULLING     ( "sv_snap_players_raycast_culling" )
#define SNAP_VAR_USE_VIEWDIR_CULLING     ( "sv_snap_aggressive_fov_culling" )
#define SNAP_VAR_SHADOW_EVENTS_DATA      ( "sv_snap_shadow_events_data" )

void SNAP_BuildClientFrameSnap( struct cmodel_state_s *cms, struct ginfo_s *gi, int64_t frameNum, int64_t timeStamp,
								struct fatvis_s *fatvis, struct client_s *client,
								game_state_t *gameState, struct client_entities_s *client_entities,
								bool relay, struct mempool_s *mempool, int snapHintFlags );

void SNAP_FreeClientFrames( struct client_s *client );

void SNAP_RecordDemoMessage( int demofile, struct msg_s *msg, int offset );
int SNAP_ReadDemoMessage( int demofile, struct msg_s *msg );
void SNAP_BeginDemoRecording( int demofile, unsigned int spawncount, unsigned int snapFrameTime,
							  const char *sv_name, unsigned int sv_bitflags, struct purelist_s *purelist,
							  char *configstrings, entity_state_t *baselines );
void SNAP_StopDemoRecording( int demofile );
void SNAP_WriteDemoMetaData( const char *filename, const char *meta_data, size_t meta_data_realsize );
size_t SNAP_ClearDemoMeta( char *meta_data, size_t meta_data_max_size );
size_t SNAP_SetDemoMetaKeyValue( char *meta_data, size_t meta_data_max_size, size_t meta_data_realsize,
								 const char *key, const char *value );
size_t SNAP_ReadDemoMetaData( int demofile, char *meta_data, size_t meta_data_size );

#ifdef __cplusplus
}
#endif

#endif

