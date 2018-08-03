#ifndef QFUSION_UICEF_H
#define QFUSION_UICEF_H

#include <stdlib.h>
#include <stdint.h>

#include "../gameshared/q_cvar.h"
#include "../gameshared/q_math.h"
#include "../gameshared/q_shared.h"
#include "../cgame/ref.h"

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#define UI_API_VERSION      67

#define UI_CONTEXT_MAIN 1
#define UI_CONTEXT_QUICK 2

#ifdef __cplusplus
extern "C" {
#endif

struct shader_s;

typedef struct {
	// halts the application
#ifndef _MSC_VER
	void ( *Error )( const char *str ) __attribute__( ( noreturn ) );
#else
	void ( *Error )( const char *str );
#endif

	// console messages
	void ( *Print )( const char *str );

	// console variable interaction
	cvar_t *( *Cvar_Get )( const char *name, const char *value, int flags );
	cvar_t *( *Cvar_Set )( const char *name, const char *value );
	cvar_t *( *Cvar_ForceSet )( const char *name, const char *value );      // will return 0 0 if not found
	void ( *Cvar_SetValue )( const char *name, float value );
	float ( *Cvar_Value )( const char *name );
	const char *( *Cvar_String )( const char *name );

	int ( *Cmd_Argc )( void );
	const char *( *Cmd_Argv )( int arg );
	char *( *Cmd_Args )( void );        // concatenation of all argv >= 1

	void ( *Cmd_AddCommand )( const char *name, void ( *cmd )( void ) );
	void ( *Cmd_RemoveCommand )( const char *cmd_name );
	void ( *Cmd_ExecuteText )( int exec_when, const char *text );
	void ( *Cmd_Execute )( void );
	void ( *Cmd_SetCompletionFunc )( const char *cmd_name, char **( *completion_func )( const char *partial ) );

	void ( *R_ClearScene )( void );
	void ( *R_AddLightStyleToScene )( int style, float r, float g, float b );
	void ( *R_AddPolyToScene )( const poly_t *poly );
	void ( *R_RenderScene )( const refdef_t *fd );
	void ( *R_BlurScreen )( void );
	void ( *R_EndFrame )( void );
	void ( *R_RegisterWorldModel )( const char *name );
	void ( *R_ModelBounds )( const struct model_s *mod, vec3_t mins, vec3_t maxs );
	void ( *R_ModelFrameBounds )( const struct model_s *mod, int frame, vec3_t mins, vec3_t maxs );
	struct model_s *( *R_RegisterModel )( const char *name );
	struct shader_s *( *R_RegisterSkin )( const char *name );
	struct shader_s *( *R_RegisterPic )( const char *name );
	struct shader_s *( *R_RegisterRawPic )( const char *name, int width, int height, uint8_t *data, int samples );
	struct shader_s *( *R_RegisterLevelshot )( const char *name, struct shader_s *defaultPic, bool *matchesDefault );
	struct skinfile_s *( *R_RegisterSkinFile )( const char *name );
	struct shader_s *( *R_RegisterVideo )( const char *name );
	struct shader_s *( *R_RegisterLinearPic )( const char *name );
	bool ( *R_LerpTag )( orientation_t *orient, const struct model_s *mod, int oldframe, int frame, float lerpfrac, const char *name );
	void ( *R_DrawStretchPic )( int x, int y, int w, int h, float s1, float t1, float s2, float t2, const vec4_t color, const struct shader_s *shader );
	void ( *R_DrawStretchRaw )( int x, int y, int w, int h, int cols, int rows, float s1, float t1, float s2, float t2, uint8_t *data );
	void ( *R_DrawStretchPoly )( const struct poly_s *poly, float x_offset, float y_offset );
	void ( *R_DrawRotatedStretchPic )( int x, int y, int w, int h, float s1, float t1, float s2, float t2, float angle, const vec4_t color, const struct shader_s *shader );
	void ( *R_Scissor )( int x, int y, int w, int h );
	void ( *R_GetScissor )( int *x, int *y, int *w, int *h );
	void ( *R_ResetScissor )( void );
	void ( *R_GetShaderDimensions )( const struct shader_s *shader, int *width, int *height );
	void ( *R_TransformVectorToScreen )( const refdef_t *rd, vec3_t const in, vec2_t out );
	int ( *R_SkeletalGetNumBones )( const struct model_s *mod, int *numFrames );
	int ( *R_SkeletalGetBoneInfo )( const struct model_s *mod, int bone, char *name, size_t name_size, int *flags );
	void ( *R_SkeletalGetBonePose )( const struct model_s *mod, int bone, int frame, bonepose_t *bonepose );
	struct cinematics_s *( *R_GetShaderCinematic )( struct shader_s *shader );

	struct sfx_s *( *S_RegisterSound )( const char *name );
	void ( *S_StartLocalSound )( const char *s );
	void ( *S_StartBackgroundTrack )( const char *intro, const char *loop, int mode );
	void ( *S_StopBackgroundTrack )( void );

	// files will be memory mapped read only
	// the returned buffer may be part of a larger pak file,
	// or a discrete file from anywhere in the quake search path
	// a -1 return means the file does not exist
	// NULL can be passed for buf to just determine existance
	// you can also open URL's, but you cant use anything else but
	// FS_Read (blocking) and FS_CloseFile
	int ( *FS_FOpenFile )( const char *filename, int *filenum, int mode );
	int ( *FS_Read )( void *buffer, size_t len, int file );
	int ( *FS_Write )( const void *buffer, size_t len, int file );
	int ( *FS_Print )( int file, const char *msg );
	int ( *FS_Tell )( int file );
	int ( *FS_Seek )( int file, int offset, int whence );
	int ( *FS_Eof )( int file );
	int ( *FS_Flush )( int file );
	void ( *FS_FCloseFile )( int file );
	bool ( *FS_RemoveFile )( const char *filename );
	int ( *FS_GetFileList )( const char *dir, const char *extension, char *buf, size_t bufsize, int start, int end );
	int ( *FS_GetGameDirectoryList )( char *buf, size_t bufsize );
	const char *( *FS_FirstExtension )( const char *filename, const char *extensions[], int num_extensions );
	bool ( *FS_MoveFile )( const char *src, const char *dst );
	bool ( *FS_MoveCacheFile )( const char *src, const char *dst );
	bool ( *FS_IsUrl )( const char *url );
	time_t ( *FS_FileMTime )( const char *filename );
	bool ( *FS_RemoveDirectory )( const char *dirname );
	ssize_t ( *FS_GetRealPath )( const char *path, char *buffer, size_t bufferSize );

	void ( *GetConfigString )( int i, char *str, int size );

	void ( *CL_Quit )( void );
	void ( *CL_SetKeyDest )( keydest_t key_dest );
	void ( *CL_ResetServerCount )( void );
	char *( *CL_GetClipboardData )( void );
	void ( *CL_SetClipboardData )( const char *data );
	void ( *CL_FreeClipboardData )( char *data );
	bool ( *CL_IsBrowserAvailable )( void );
	void ( *CL_OpenURLInBrowser )( const char *url );
	size_t ( *CL_ReadDemoMetaData )( const char *demopath, char *meta_data, size_t meta_data_size );
	int ( *CL_PlayerNum )( void );

	// maplist
	const char *( *ML_GetFilename )( const char *fullname );
	const char *( *ML_GetFullname )( const char *filename );
	size_t ( *ML_GetMapByNum )( int num, char *out, size_t size );

	// MatchMaker
	bool ( *MM_Login )( const char *user, const char *password );
	bool ( *MM_Logout )( bool force );
	int ( *MM_GetLoginState )( void );
	size_t ( *MM_GetLastErrorMessage )( char *buffer, size_t buffer_size );
	size_t ( *MM_GetProfileURL )( char *buffer, size_t buffer_size, bool rml );
	size_t ( *MM_GetBaseWebURL )( char *buffer, size_t buffer_size );

	void ( *L10n_ClearDomain )( void );
	void ( *L10n_LoadLangPOFile )( const char *filepath );
	const char *( *L10n_TranslateString )( const char *string );
	const char *( *L10n_GetUserLanguage )( void );

	size_t ( *GetBaseServerURL )( char *buffer, size_t buffer_size );

	const char *( *Key_GetBindingBuf )( int binding );
	const char *( *Key_KeynumToString )( int keynum );
	int ( *Key_StringToKeynum )( const char* s );
	void ( *Key_SetBinding )( int keynum, const char *binding );
	bool ( *Key_IsDown )( int keynum );

	bool ( *VID_GetModeInfo )( int *width, int *height, unsigned mode );
	void ( *VID_FlashWindow )( int count );
} ui_import_t;

extern ui_import_t *api;

typedef struct {
	// if API is different, the dll cannot be used
	int ( *API )( void );
	// We have to provide arguments for process/sub process creation
	// TODO: We should supply an absolute path
	bool ( *Init )( int argc, char **argv, void *hInstance, int vidWidth, int vidHeight, int protocol, const char *demoExtension, const char *basePath );
	void ( *Shutdown )( void );

	void ( *TouchAllAssets )( void );

	void ( *Refresh )( int64_t time, int clientState, int serverState,
					   bool demoPlaying, const char *demoName, bool demoPaused, unsigned int demoTime,
					   bool backGround, bool showCursor );

	void ( *UpdateConnectScreen )( const char *serverName, const char *rejectmessage,
								   int downloadType, const char *downloadfilename, float downloadPercent, int downloadSpeed,
								   int connectCount, bool backGround );

	void ( *Keydown )( int context, int key );
	void ( *Keyup )( int context, int key );
	void ( *CharEvent )( int context, wchar_t key );

	void ( *MouseMove )( int context, int frameTime, int dx, int dy );
	void ( *MouseSet )( int context, int mx, int my, bool showCursor );

	bool ( *TouchEvent )( int context, int id, touchevent_t type, int x, int y );
	bool ( *IsTouchDown )( int context, int id );
	void ( *CancelTouches )( int context );

	void ( *ForceMenuOff )( void );
	bool ( *HaveQuickMenu )( void );
	void ( *ShowQuickMenu )( bool show );
	void ( *AddToServerList )( const char *adr, const char *info );
} ui_export_t;

#ifdef __cplusplus
}
#endif

#endif
