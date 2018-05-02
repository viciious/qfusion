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
// client.h -- primary header for client

#include "../qcommon/qcommon.h"
#include "../ref_gl/r_public.h"
#include "../cgame/cg_public.h"
#include "../ftlib/ftlib_public.h"
#include "../matchmaker/mm_rating.h"
#include "snd_public.h"
#include "../qcommon/steam.h"

#include "vid.h"
#include "input.h"
#include "keys.h"
#include "console.h"
#include "l10n.h"

typedef struct shader_s shader_t;
typedef struct qfontface_s qfontface_t;

//=============================================================================

#define MAX_TIMEDELTAS_BACKUP 8
#define MASK_TIMEDELTAS_BACKUP ( MAX_TIMEDELTAS_BACKUP - 1 )

typedef struct {
	int frames;
	int64_t startTime;
	int64_t lastTime;
	int counts[100];
} cl_timedemo_t;

typedef struct {
	void *h;
	int width, height;
	bool keepRatio;
	bool allowConsole;
	bool redraw;
	bool paused;
	int pause_cnt;
	bool yuv;
	int64_t startTime;
	int64_t pauseTime;
	uint8_t *pic;
	int aspect_numerator, aspect_denominator;
	ref_yuv_t *cyuv;
	float framerate;
} cl_cintematics_t;

//
// the client_state_t structure is wiped completely at every
// server map change
//
typedef struct {
	int timeoutcount;

	cl_timedemo_t timedemo;

	int cmdNum;                     // current cmd
	usercmd_t *cmds;                // [CMD_BACKUP] each mesage will send several old cmds
	int *cmd_time;                  // [CMD_BACKUP] time sent, for calculating pings

	int receivedSnapNum;
	int pendingSnapNum;
	int currentSnapNum;
	int previousSnapNum;
	int suppressCount;              // number of messages rate suppressed
	snapshot_t *snapShots;          // [CMD_BACKUP]
	uint8_t *frames_areabits;

	cmodel_state_t *cms;
	cmodel_state_t *sound_cms;      // a separated collison model instance for sound if it needs thread safety

	// the client maintains its own idea of view angles, which are
	// sent to the server each frame.  It is cleared to 0 upon entering each level.
	// the server sends a delta each frame which is added to the locally
	// tracked view angles to account for standing on rotating objects,
	// and teleport direction changes
	vec3_t viewangles;

	int serverTimeDeltas[MAX_TIMEDELTAS_BACKUP];
	int newServerTimeDelta;         // the time difference with the server time, or at least our best guess about it
	int serverTimeDelta;            // the time difference with the server time, or at least our best guess about it
	int64_t serverTime;             // the best match we can guess about current time in the server
	unsigned int snapFrameTime;

	//
	// non-gameserver information
	cl_cintematics_t cin;

	//
	// server state information
	//
	int servercount;        // server identification for prespawns
	int playernum;
	bool gamestart;

	char servermessage[MAX_STRING_CHARS];
	char configstrings[MAX_CONFIGSTRINGS][MAX_CONFIGSTRING_CHARS];
} client_state_t;

extern client_state_t cl;

/*
==================================================================

the client_static_t structure is persistant through an arbitrary number
of server connections

==================================================================
*/

typedef struct download_list_s download_list_t;

struct download_list_s {
	char *filename;
	download_list_t *next;
};

typedef struct {
	// for request
	char *requestname;              // file we requested from the server (NULL if none requested)
	bool requestnext;           // whether to request next download after this, for precaching
	bool requestpak;            // whether to only allow .pk3/.pak or only allow normal file
	int64_t timeout;
	int64_t timestart;

	// both downloads
	char *name;                     // name of the file in download, relative to base path
	char *origname;                 // name of the file in download as originally passed by the server
	char *tempname;                 // temporary location, relative to base path
	size_t size;
	unsigned checksum;

	double percent;
	int successCount;               // so we know to restart media
	download_list_t *list;          // list of all tried downloads, so we don't request same pk3 twice

	// server download
	int filenum;
	size_t offset;
	int retries;
	size_t baseoffset;              // for download speed calculation when resuming downloads

	// web download
	bool web;
	bool web_official;
	bool web_official_only;
	char *web_url;                  // download URL, passed by the server
	bool web_local_http;

	bool disconnect;            // set when user tries to disconnect, to allow cleaning up webdownload
	bool pending_reconnect;     // set when we ignored a map change command to avoid stopping the download
	bool cancelled;             // to allow cleaning up of temporary download file
} download_t;

typedef struct {
	char *name;

	bool recording;
	bool waiting;       // don't record until a non-delta message is received
	bool playing;
	bool paused;        // A boolean to test if demo is paused -- PLX

	int file;
	char *filename;

	time_t localtime;       // time of day of demo recording
	int64_t time;           // milliseconds passed since the start of the demo
	int64_t duration, basetime;

	bool play_jump;
	bool play_jump_latched;
	int64_t play_jump_time;
	bool play_ignore_next_frametime;

	bool avi;
	bool avi_video, avi_audio;
	bool pending_avi;
	bool pause_on_stop;
	int avi_frame;

	char meta_data[SNAP_MAX_DEMO_META_DATA_SIZE];
	size_t meta_data_realsize;
} cl_demo_t;

typedef cl_demo_t demorec_t;

typedef struct {
	connstate_t state;          // only set through CL_SetClientState
	keydest_t key_dest;
	keydest_t old_key_dest;
	bool quickmenu;

	int64_t framecount;
	int64_t realtime;               // always increasing, no clamping, etc
	int64_t gametime;               // always increasing, no clamping, etc
	int frametime;                  // milliseconds since last frame
	int realFrameTime;

	socket_t socket_loopback;
	socket_t socket_udp;
	socket_t socket_udp6;
#ifdef TCP_SUPPORT
	socket_t socket_tcp;
#endif

	// screen rendering information
	bool cgameActive;
	int mediaRandomSeed;
	bool mediaInitialized;

	unsigned int disable_screen;    // showing loading plaque between levels
	                                // or changing rendering dlls
	                                // if time gets > 30 seconds ahead, break it

	// connection information
	char *servername;               // name of server from original connect
	socket_type_t servertype;       // socket type used to connect to the server
	netadr_t serveraddress;         // address of that server
	int64_t connect_time;               // for connection retransmits
	int connect_count;

	socket_t *socket;               // socket used by current connection
	bool reliable;
	bool mv;

	netadr_t rconaddress;       // address where we are sending rcon messages, to ignore other print packets

	netadr_t httpaddress;           // address of the builtin HTTP server
	char *httpbaseurl;              // http://<httpaddress>/

	bool rejected;          // these are used when the server rejects our connection
	int rejecttype;
	char rejectmessage[80];

	netchan_t netchan;

	int challenge;              // from the server to use for connecting

	download_t download;

	bool registrationOpen;

	// demo recording info must be here, so it isn't cleared on level change
	cl_demo_t demo;

	// these shaders have nothing to do with media
	shader_t *whiteShader;
	shader_t *consoleShader;

	// system font
	qfontface_t *consoleFont;

	// these are our reliable messages that go to the server
	int64_t reliableSequence;          // the last one we put in the list to be sent
	int64_t reliableSent;              // the last one we sent to the server
	int64_t reliableAcknowledge;       // the last one the server has executed
	char reliableCommands[MAX_RELIABLE_COMMANDS][MAX_STRING_CHARS];

	// reliable messages received from server
	int64_t lastExecutedServerCommand;          // last server command grabbed or executed with CL_GetServerCommand

	// ucmds buffer
	int64_t ucmdAcknowledged;
	int64_t ucmdHead;
	int64_t ucmdSent;

	// times when we got/sent last valid packets from/to server
	int64_t lastPacketSentTime;
	int64_t lastPacketReceivedTime;

	// pure list
	bool sv_pure;
	bool sv_tv;
	bool pure_restart;

	purelist_t *purelist;

	mm_uuid_t mm_session;
	mm_uuid_t mm_ticket;
	clientRating_t *ratings;

	char session[MAX_INFO_VALUE];

	void *wakelock;
} client_static_t;

extern client_static_t cls;

//=============================================================================

//
// cvars
//
extern cvar_t *cl_stereo_separation;
extern cvar_t *cl_stereo;

extern cvar_t *cl_shownet;

extern cvar_t *cl_extrapolationTime;
extern cvar_t *cl_extrapolate;

extern cvar_t *cl_timedemo;
extern cvar_t *cl_demoavi_video;
extern cvar_t *cl_demoavi_audio;
extern cvar_t *cl_demoavi_fps;
extern cvar_t *cl_demoavi_scissor;

// wsw : debug netcode
extern cvar_t *cl_debug_serverCmd;
extern cvar_t *cl_debug_timeDelta;

extern cvar_t *cl_downloads;
extern cvar_t *cl_downloads_from_web;
extern cvar_t *cl_downloads_from_web_timeout;
extern cvar_t *cl_download_allow_modules;

// delta from this if not from a previous frame
extern entity_state_t cl_baselines[MAX_EDICTS];

//=============================================================================


//
// cl_cin.c
//
bool SCR_DrawCinematic( void );
void SCR_RunCinematic( void );
void SCR_StopCinematic( void );
void SCR_FinishCinematic( void );
bool SCR_AllowCinematicConsole( void );
void SCR_PauseCinematic( bool pause );
void CL_InitCinematics( void );
void CL_ShutdownCinematics( void );
float SCR_CinematicFramerate( void );

//
// cl_main.c
//
void CL_Init( void );
void CL_Quit( void );

void CL_UpdateClientCommandsToServer( msg_t *msg );
void CL_AddReliableCommand( /*const*/ char *cmd );
void CL_Netchan_Transmit( msg_t *msg );
void CL_SendMessagesToServer( bool sendNow );
void CL_RestartTimeDeltas( int newTimeDelta );
void CL_AdjustServerTime( unsigned int gamemsec );

char *CL_GetClipboardData( void );
void CL_SetClipboardData( const char *data );
void CL_FreeClipboardData( char *data );
keydest_t CL_GetKeyDest( void );              // wsw : aiwa : we need this information for graphical plugins (e.g. IRC)
void CL_SetKeyDest( keydest_t key_dest );
void CL_SetOldKeyDest( keydest_t key_dest );
void CL_ResetServerCount( void );
void CL_SetClientState( int state );
connstate_t CL_GetClientState( void );  // wsw : aiwa : we need this information for graphical plugins (e.g. IRC)
void CL_ClearState( void );
void CL_ReadPackets( void );
void CL_Disconnect_f( void );
void CL_S_Restart( bool noVideo );

bool CL_IsBrowserAvailable( void );
void CL_OpenURLInBrowser( const char *url );

void CL_Reconnect_f( void );
void CL_ServerReconnect_f( void );
void CL_Changing_f( void );
void CL_Precache_f( void );
void CL_ForwardToServer_f( void );
void CL_ServerDisconnect_f( void );

size_t CL_GetBaseServerURL( char *buffer, size_t buffer_size );

int CL_AddSessionHttpRequestHeaders( const char *url, const char **headers );
void CL_AsyncStreamRequest( const char *url, const char **headers, int timeout, int resumeFrom,
							size_t ( *read_cb )( const void *, size_t, float, int, const char *, void * ),
							void ( *done_cb )( int, const char *, void * ),
							void ( *header_cb )( const char *, void * ), void *privatep, bool urlencodeUnsafe );

//
// cl_game.c
//
void CL_GameModule_Init( void );
void CL_GameModule_Reset( void );
void CL_GameModule_Shutdown( void );
void CL_GameModule_ConfigString( int number, const char *value );
void CL_GameModule_EscapeKey( void );
float CL_GameModule_GetSensitivityScale( float sens, float zoomSens );
bool CL_GameModule_NewSnapshot( int pendingSnapshot );
void CL_GameModule_RenderView( float stereo_separation );
void CL_GameModule_GetEntitySpatilization( int entnum, vec3_t origin, vec3_t velocity );
void CL_GameModule_InputFrame( int frameTime );
void CL_GameModule_ClearInputState( void );
unsigned CL_GameModule_GetButtonBits( void );
void CL_GameModule_AddViewAngles( vec3_t viewAngles );
void CL_GameModule_AddMovement( vec3_t movement );
void CL_GameModule_MouseMove( int dx, int dy );
void CL_GameModule_TouchEvent( int id, touchevent_t type, int x, int y, int64_t time );
bool CL_GameModule_IsTouchDown( int id );

//
// cl_sound.c
//
void CL_SoundModule_Init( bool verbose );
void CL_SoundModule_Shutdown( bool verbose );
void CL_SoundModule_BeginRegistration( void );
void CL_SoundModule_EndRegistration( void );
void CL_SoundModule_StopAllSounds( bool clear, bool stopMusic );
void CL_SoundModule_Clear( void );
void CL_SoundModule_SetEntitySpatilization( int entNum, vec3_t origin, vec3_t velocity );
void CL_SoundModule_Update( const vec3_t origin, const vec3_t velocity, const mat3_t axis, const char *identity, bool avidump );
void CL_SoundModule_Activate( bool activate );
struct sfx_s *CL_SoundModule_RegisterSound( const char *sample );
void CL_SoundModule_StartFixedSound( struct sfx_s *sfx, const vec3_t origin, int channel, float fvol, float attenuation );
void CL_SoundModule_StartRelativeSound( struct sfx_s *sfx, int entnum, int channel, float fvol, float attenuation );
void CL_SoundModule_StartGlobalSound( struct sfx_s *sfx, int channel, float fvol );
void CL_SoundModule_StartLocalSoundByName( const char *s );
void CL_SoundModule_StartLocalSound( struct sfx_s *sfx, float fvol );
void CL_SoundModule_AddLoopSound( struct sfx_s *sfx, int entnum, float fvol, float attenuation );
void CL_SoundModule_RawSamples( unsigned int samples, unsigned int rate,
								unsigned short width, unsigned short channels, const uint8_t *data, bool music );
void CL_SoundModule_PositionedRawSamples( int entnum, float fvol, float attenuation,
										  unsigned int samples, unsigned int rate,
										  unsigned short width, unsigned short channels, const uint8_t *data );
unsigned int CL_SoundModule_GetRawSamplesLength( void );
unsigned int CL_SoundModule_GetPositionedRawSamplesLength( int entnum );
void CL_SoundModule_StartBackgroundTrack( const char *intro, const char *loop, int mode );
void CL_SoundModule_StopBackgroundTrack( void );
void CL_SoundModule_LockBackgroundTrack( bool lock );
void CL_SoundModule_BeginAviDemo( void );
void CL_SoundModule_StopAviDemo( void );

void CL_Mumble_Init( void );
void CL_Mumble_Link( void );
void CL_Mumble_Unlink( void );
void CL_Mumble_Update( const vec3_t origin, const mat3_t axis, const char *identity );
void CL_Mumble_Shutdown( void );

//
// cl_ui.c
//
void CL_UIModule_Init( void );
void CL_UIModule_Shutdown( void );
void CL_UIModule_TouchAllAssets( void );
void CL_UIModule_Keydown( int key );
void CL_UIModule_Keyup( int key );
void CL_UIModule_KeydownQuick( int key );
void CL_UIModule_KeyupQuick( int key );
void CL_UIModule_CharEvent( wchar_t key );
bool CL_UIModule_TouchEvent( int id, touchevent_t type, int x, int y );
bool CL_UIModule_TouchEventQuick( int id, touchevent_t type, int x, int y );
bool CL_UIModule_IsTouchDown( int id );
bool CL_UIModule_IsTouchDownQuick( int id );
void CL_UIModule_CancelTouches( void );
void CL_UIModule_Refresh( bool backGround, bool showCursor );
void CL_UIModule_UpdateConnectScreen( bool backGround );
void CL_UIModule_ForceMenuOn( void );
void CL_UIModule_ForceMenuOff( void );
void CL_UIModule_ShowQuickMenu( bool show );
bool CL_UIModule_HaveQuickMenu( void );
void CL_UIModule_AddToServerList( const char *adr, const char *info );
void CL_UIModule_MouseMove( int frameTime, int dx, int dy );
void CL_UIModule_MouseSet( int mx, int my, bool showCursor );

//
// cl_serverlist.c
//
void CL_ParseGetInfoResponse( const socket_t *socket, const netadr_t *address, msg_t *msg );
void CL_ParseGetStatusResponse( const socket_t *socket, const netadr_t *address, msg_t *msg );
void CL_QueryGetInfoMessage_f( void );
void CL_QueryGetStatusMessage_f( void );
void CL_ParseStatusMessage( const socket_t *socket, const netadr_t *address, msg_t *msg );
void CL_ParseGetServersResponse( const socket_t *socket, const netadr_t *address, msg_t *msg, bool extended );
void CL_GetServers_f( void );
void CL_PingServer_f( void );
void CL_ServerListFrame( void );
void CL_InitServerList( void );
void CL_ShutDownServerList( void );

//
// cl_input.c
//
void CL_InitInput( void );
void CL_ShutdownInput( void );
void CL_UserInputFrame( int realMsec );
void CL_WriteUcmdsToMessage( msg_t *msg );

/**
* Mouse input for systems with basic mouse support (without centering
* and possibly without toggleable cursor).
*/
void CL_MouseSet( int mx, int my, bool showCursor );

void CL_TouchEvent( int id, touchevent_t type, int x, int y, int64_t time );

/**
 * Resets the input state to the same as when no input is done,
 * mainly when the current input dest can't receive events anymore.
 */
void CL_ClearInputState( void );



//
// cl_demo.c
//
void CL_WriteDemoMessage( msg_t *msg );
void CL_DemoCompleted( void );
void CL_PlayDemo_f( void );
void CL_PlayDemoToAvi_f( void );
void CL_ReadDemoPackets( void );
void CL_LatchedDemoJump( void );
void CL_Stop_f( void );
void CL_Record_f( void );
void CL_PauseDemo_f( void );
void CL_DemoJump_f( void );
void CL_BeginDemoAviDump( void );
size_t CL_ReadDemoMetaData( const char *demopath, char *meta_data, size_t meta_data_size );
char **CL_DemoComplete( const char *partial );
#define CL_WriteAvi() ( cls.demo.avi && cls.state == CA_ACTIVE && cls.demo.playing && !cls.demo.play_jump )
#define CL_SetDemoMetaKeyValue( k,v ) cls.demo.meta_data_realsize = SNAP_SetDemoMetaKeyValue( cls.demo.meta_data, sizeof( cls.demo.meta_data ), cls.demo.meta_data_realsize, k, v )

//
// cl_parse.c
//
void CL_ParseServerMessage( msg_t *msg );
#define SHOWNET( msg,s ) _SHOWNET( msg,s,cl_shownet->integer );

void CL_FreeDownloadList( void );
bool CL_CheckOrDownloadFile( const char *filename );

bool CL_DownloadRequest( const char *filename, bool requestpak );
void CL_DownloadStatus_f( void );
void CL_DownloadCancel_f( void );
void CL_DownloadDone( void );
void CL_RequestNextDownload( void );
void CL_CheckDownloadTimeout( void );

//
// cl_screen.c
//
void SCR_InitScreen( void );
void SCR_ShutdownScreen( void );
void SCR_EnableQuickMenu( bool enable );
bool SCR_IsQuickMenuShown( void );
void SCR_UpdateScreen( void );
void SCR_BeginLoadingPlaque( void );
void SCR_EndLoadingPlaque( void );
void SCR_DebugGraph( float value, float r, float g, float b );
void SCR_RunConsole( int msec );
void SCR_RegisterConsoleMedia( void );
void SCR_ShutDownConsoleMedia( void );
void SCR_ResetSystemFontConsoleSize( void );
void SCR_ChangeSystemFontConsoleSize( int ch );
qfontface_t *SCR_RegisterFont( const char *family, int style, unsigned int size );
qfontface_t *SCR_RegisterSpecialFont( const char *family, int style, unsigned int size );
size_t SCR_FontSize( qfontface_t *font );
size_t SCR_FontHeight( qfontface_t *font );
size_t SCR_strWidth( const char *str, qfontface_t *font, size_t maxlen, int flags );
size_t SCR_StrlenForWidth( const char *str, qfontface_t *font, size_t maxwidth, int flags );
int SCR_FontUnderline( qfontface_t *font, int *thickness );
size_t SCR_FontAdvance( qfontface_t *font );
size_t SCR_FontXHeight( qfontface_t *font );
fdrawchar_t SCR_SetDrawCharIntercept( fdrawchar_t intercept );
int SCR_DrawString( int x, int y, int align, const char *str, qfontface_t *font, vec4_t color, int flags );
size_t SCR_DrawStringWidth( int x, int y, int align, const char *str, size_t maxwidth, qfontface_t *font, vec4_t color, int flags );
void SCR_DrawClampString( int x, int y, const char *str, int xmin, int ymin, int xmax, int ymax, qfontface_t *font, vec4_t color, int flags );
int SCR_DrawMultilineString( int x, int y, const char *str, int halign, int maxwidth, int maxlines, qfontface_t *font, vec4_t color, int flags );
void SCR_DrawRawChar( int x, int y, wchar_t num, qfontface_t *font, vec4_t color );
void SCR_DrawClampChar( int x, int y, wchar_t num, int xmin, int ymin, int xmax, int ymax, qfontface_t *font, vec4_t color );
void SCR_DrawFillRect( int x, int y, int w, int h, vec4_t color );
void SCR_DrawClampFillRect( int x, int y, int w, int h, int xmin, int ymin, int xmax, int ymax, vec4_t color );
void SCR_DrawChat( int x, int y, int width, struct qfontface_s *font );

void CL_InitMedia( void );
void CL_ShutdownMedia( void );
void CL_RestartMedia( void );

void CL_AddNetgraph( void );

extern float scr_con_current;
extern float scr_conlines;       // lines of console to display

extern ref_export_t re;     // interface to refresh .dll

//
// cl_mm.c
//
//extern cvar_t *cl_mmserver;

void CL_MM_Init( void );
void CL_MM_Shutdown( bool logout );
void CL_MM_Frame( void );
bool CL_MM_CanConnect( void );
bool CL_MM_WaitForLogin( void );

bool CL_MM_Initialized( void );
bool CL_MM_Connect( const netadr_t *address );

// exported to UI
bool CL_MM_Login( const char *user, const char *password );
bool CL_MM_Logout( bool force );
int CL_MM_GetLoginState( void );
size_t CL_MM_GetLastErrorMessage( char *buffer, size_t buffer_size );
size_t CL_MM_GetProfileURL( char *buffer, size_t buffer_size, bool rml );
size_t CL_MM_GetBaseWebURL( char *buffer, size_t buffer_size );

//
// sys import
//

/**
 * Initializes the parts of the platform module required to run the client.
 */
void CL_Sys_Init( void );

/**
 * Shuts down the client parts of the platform module.
 */
void CL_Sys_Shutdown( void );
