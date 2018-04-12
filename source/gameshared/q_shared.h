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

#ifndef GAME_QCOMMON_H
#define GAME_QCOMMON_H

#include "q_arch.h"

#ifdef __cplusplus
extern "C" {
#endif

//==============================================

#if !defined ( ENDIAN_LITTLE ) && !defined ( ENDIAN_BIG )
#if defined ( __i386__ ) || defined ( __ia64__ ) || defined ( WIN32 ) || ( defined ( __alpha__ ) || defined ( __alpha ) ) || defined ( __arm__ ) || ( defined ( __mips__ ) && defined ( __MIPSEL__ ) ) || defined ( __LITTLE_ENDIAN__ ) || defined ( __x86_64__ )
#define ENDIAN_LITTLE
#else
#define ENDIAN_BIG
#endif
#endif

short ShortSwap( short l );
int LongSwap( int l );
float FloatSwap( float f );

#ifdef ENDIAN_LITTLE
// little endian
#define BigShort( l ) ShortSwap( l )
#define LittleShort( l ) ( l )
#define BigLong( l ) LongSwap( l )
#define LittleLong( l ) ( l )
#define BigFloat( l ) FloatSwap( l )
#define LittleFloat( l ) ( l )
#elif defined ( ENDIAN_BIG )
// big endian
#define BigShort( l ) ( l )
#define LittleShort( l ) ShortSwap( l )
#define BigLong( l ) ( l )
#define LittleLong( l ) LongSwap( l )
#define BigFloat( l ) ( l )
#define LittleFloat( l ) FloatSwap( l )
#else
// figure it out at runtime
extern short ( *BigShort )( short l );
extern short ( *LittleShort )( short l );
extern int ( *BigLong )( int l );
extern int ( *LittleLong )( int l );
extern float ( *BigFloat )( float l );
extern float ( *LittleFloat )( float l );
#endif

//==============================================

// command line execution flags
#define EXEC_NOW                    0           // don't return until completed
#define EXEC_INSERT                 1           // insert at current position, but don't run yet
#define EXEC_APPEND                 2           // add to end of the command buffer

//=============================================
// fonts
//=============================================

#define DEFAULT_SYSTEM_FONT_FAMILY          "Droid Sans"
#define DEFAULT_SYSTEM_FONT_FAMILY_FALLBACK "Droid Sans Fallback"
#define DEFAULT_SYSTEM_FONT_FAMILY_MONO     "Droid Sans Mono"
#define DEFAULT_SYSTEM_FONT_SMALL_SIZE      14
#define DEFAULT_SYSTEM_FONT_MEDIUM_SIZE     16
#define DEFAULT_SYSTEM_FONT_BIG_SIZE        24
#define DEFAULT_SYSTEM_FONT_STYLE           0

#define DEFAULT_SCOREBOARD_FONT_FAMILY      "Droid Sans"
#define DEFAULT_SCOREBOARD_MONO_FONT_FAMILY "Droid Sans Mono"
#define DEFAULT_SCOREBOARD_TITLE_FONT_FAMILY "Hemi Head"
#define DEFAULT_SCOREBOARD_FONT_SIZE        12
#define DEFAULT_SCOREBOARD_TITLE_FONT_SIZE  24
#define DEFAULT_SCOREBOARD_FONT_STYLE       0

#define ALIGN_LEFT_TOP              0
#define ALIGN_CENTER_TOP            1
#define ALIGN_RIGHT_TOP             2
#define ALIGN_LEFT_MIDDLE           3
#define ALIGN_CENTER_MIDDLE         4
#define ALIGN_RIGHT_MIDDLE          5
#define ALIGN_LEFT_BOTTOM           6
#define ALIGN_CENTER_BOTTOM         7
#define ALIGN_RIGHT_BOTTOM          8

//==============================================================
//
//PATHLIB
//
//==============================================================

char *COM_SanitizeFilePath( char *filename );
bool COM_ValidateFilename( const char *filename );
bool COM_ValidateRelativeFilename( const char *filename );
void COM_StripExtension( char *filename );
const char *COM_FileExtension( const char *in );
void COM_DefaultExtension( char *path, const char *extension, size_t size );
void COM_ReplaceExtension( char *path, const char *extension, size_t size );
const char *COM_FileBase( const char *in );
void COM_StripFilename( char *filename );
int COM_FilePathLength( const char *in );

// data is an in/out parm, returns a parsed out token
char *COM_ParseExt2_r( char *token, size_t token_size, const char **data_p, bool nl, bool sq );
#define COM_ParseExt_r( token, token_size, data_p, nl ) COM_ParseExt2_r( token, token_size, (const char **)data_p, nl, true )
#define COM_Parse_r( token, token_size, data_p )   COM_ParseExt_r( token, token_size, data_p, true )

char *COM_ParseExt2( const char **data_p, bool nl, bool sq );
#define COM_ParseExt( data_p, nl ) COM_ParseExt2( (const char **)data_p, nl, true )
#define COM_Parse( data_p )   COM_ParseExt( data_p, true )

int COM_Compress( char *data_p );
const char *COM_RemoveJunkChars( const char *in );
int COM_ReadColorRGBString( const char *in );
int COM_ValidatePlayerColor( int rgbcolor );
bool COM_ValidateConfigstring( const char *string );

char *COM_ListNameForPosition( const char *namesList, int position, const char separator );

//==============================================================
//
// STRINGLIB
//
//==============================================================

#define MAX_QPATH                   64          // max length of a quake game pathname

#define MAX_STRING_CHARS            1024        // max length of a string passed to Cmd_TokenizeString
#define MAX_STRING_TOKENS           256         // max tokens resulting from Cmd_TokenizeString
#define MAX_TOKEN_CHARS             1024        // max length of an individual token
#define MAX_CONFIGSTRING_CHARS      MAX_QPATH   // max length of a configstring string

#define MAX_NAME_BYTES              32          // max length of a player name, including trailing \0
#define MAX_NAME_CHARS              15          // max visible characters in a name (color tokens and \0 not counted)

#define MAX_CHAT_BYTES              151         // max length of a chat message, including color tokens and trailing \0

#ifndef STR_HELPER
#define STR_HELPER( s )                 # s
#define STR_TOSTR( x )                  STR_HELPER( x )
#endif

//=============================================
// string colors
//=============================================

#define Q_COLOR_ESCAPE  '^'
#define S_COLOR_ESCAPE  "^"

#define COLOR_BLACK     '0'
#define COLOR_RED       '1'
#define COLOR_GREEN     '2'
#define COLOR_YELLOW    '3'
#define COLOR_BLUE      '4'
#define COLOR_CYAN      '5'
#define COLOR_MAGENTA   '6'
#define COLOR_WHITE     '7'
#define COLOR_ORANGE    '8'
#define COLOR_GREY      '9'
#define ColorIndex( c )   ( ( ( ( c ) - '0' ) < MAX_S_COLORS ) && ( ( ( c ) - '0' ) >= 0 ) ? ( ( c ) - '0' ) : 7 )

#define S_COLOR_BLACK   "^0"
#define S_COLOR_RED     "^1"
#define S_COLOR_GREEN   "^2"
#define S_COLOR_YELLOW  "^3"
#define S_COLOR_BLUE    "^4"
#define S_COLOR_CYAN    "^5"
#define S_COLOR_MAGENTA "^6"
#define S_COLOR_WHITE   "^7"
#define S_COLOR_ORANGE  "^8"
#define S_COLOR_GREY    "^9"

#define COLOR_R( rgba )       ( ( rgba ) & 0xFF )
#define COLOR_G( rgba )       ( ( ( rgba ) >> 8 ) & 0xFF )
#define COLOR_B( rgba )       ( ( ( rgba ) >> 16 ) & 0xFF )
#define COLOR_A( rgba )       ( ( ( rgba ) >> 24 ) & 0xFF )
#define COLOR_RGB( r, g, b )    ( ( ( r ) << 0 ) | ( ( g ) << 8 ) | ( ( b ) << 16 ) )
#define COLOR_RGBA( r, g, b, a ) ( ( ( r ) << 0 ) | ( ( g ) << 8 ) | ( ( b ) << 16 ) | ( ( a ) << 24 ) )

//=============================================
// strings
//=============================================

void Q_strncpyz( char *dest, const char *src, size_t size );
void Q_strncatz( char *dest, const char *src, size_t size );

int Q_vsnprintfz( char *dest, size_t size, const char *format, va_list argptr );

#ifndef _MSC_VER
int Q_snprintfz( char *dest, size_t size, const char *format, ... ) __attribute__( ( format( printf, 3, 4 ) ) );
#else
int Q_snprintfz( char *dest, size_t size, _Printf_format_string_ const char *format, ... );
#endif

char *Q_strupr( char *s );
char *Q_strlwr( char *s );
const char *Q_strlocate( const char *s, const char *substr, int skip );
size_t Q_strcount( const char *s, const char *substr );
const char *Q_strrstr( const char *s, const char *substr );
bool Q_isdigit( const char *str );
char *Q_trim( char *s );
char *Q_chrreplace( char *s, const char subj, const char repl );

/**
 * Converts the given null-terminated string to an URL encoded null-terminated string.
 * Only "unsafe" subset of characters are encoded.
 */
void Q_urlencode_unsafechars( const char *src, char *dst, size_t dst_size );
/**
 * Converts the given URL-encoded string to a null-terminated plain string. Returns
 * total (untruncated) length of the resulting string.
 */
size_t Q_urldecode( const char *src, char *dst, size_t dst_size );

void *Q_memset32( void *dest, int c, size_t dwords );

// color string functions ("^1text" etc)
#define GRABCHAR_END    0
#define GRABCHAR_CHAR   1
#define GRABCHAR_COLOR  2
int Q_GrabCharFromColorString( const char **pstr, char *c, int *colorindex );
const char *COM_RemoveColorTokensExt( const char *str, bool draw );
#define COM_RemoveColorTokens( in ) COM_RemoveColorTokensExt( in,false )
int COM_SanitizeColorString( const char *str, char *buf, int bufsize, int maxprintablechars, int startcolor );
const char *Q_ColorStringTerminator( const char *str, int finalcolor );
int Q_ColorStrLastColor( int previous, const char *s, int maxlen );

size_t Q_WCharUtf8Length( wchar_t wc );
size_t Q_WCharToUtf8( wchar_t wc, char *dest, size_t bufsize );
char *Q_WCharToUtf8Char( wchar_t wc );
size_t Q_WCharToUtf8String( const wchar_t *ws, char *dest, size_t bufsize );
wchar_t Q_GrabWCharFromUtf8String( const char **pstr );
int Q_GrabWCharFromColorString( const char **pstr, wchar_t *wc, int *colorindex );
#define UTF8SYNC_LEFT 0
#define UTF8SYNC_RIGHT 1
int Q_Utf8SyncPos( const char *str, int pos, int dir );
void Q_FixTruncatedUtf8( char *str );
bool Q_IsBreakingSpace( const char *str );
bool Q_IsBreakingSpaceChar( wchar_t c );

float *tv( float x, float y, float z );
char *vtos( float v[3] );

#ifndef _MSC_VER
char *va( const char *format, ... ) __attribute__( ( format( printf, 1, 2 ) ) );
char *va_r( char *dst, size_t size, const char *format, ... ) __attribute__( ( format( printf, 3, 4 ) ) );
#else
char *va( _Printf_format_string_ const char *format, ... );
char *va_r( char *dst, size_t size, _Printf_format_string_ const char *format, ... );
#endif

//
// key / value info strings
//
#define MAX_INFO_KEY        64
#define MAX_INFO_VALUE      64
#define MAX_INFO_STRING     512

char *Info_ValueForKey( const char *s, const char *key );
void Info_RemoveKey( char *s, const char *key );
bool Info_SetValueForKey( char *s, const char *key, const char *value );
bool Info_Validate( const char *s );
void Info_CleanValue( const char *in, char *out, size_t outsize );

//==============================================

//
// per-level limits
//
#define MAX_CLIENTS                 64          // absolute limit
#define MAX_EDICTS                  1024        // must change protocol to increase more
#define MAX_LIGHTSTYLES             256
#define MAX_MODELS                  1024        // these are sent over the net as shorts
#define MAX_SOUNDS                  1024        // so they cannot be blindly increased
#define MAX_IMAGES                  256
#define MAX_SKINFILES               256
#define MAX_ITEMS                   64          // 16x4
#define MAX_GENERAL                 128         // general config strings
#define MAX_MMPLAYERINFOS           128

//============================================
// HTTP
//============================================
#define HTTP_CODE_OK                        200
#define HTTP_CODE_PARTIAL_CONTENT           206

//============================================
// sound
//============================================

//#define S_DEFAULT_ATTENUATION_MODEL		1
#define S_DEFAULT_ATTENUATION_MODEL         3
#define S_DEFAULT_ATTENUATION_MAXDISTANCE   8000
#define S_DEFAULT_ATTENUATION_REFDISTANCE   125

float Q_GainForAttenuation( int model, float maxdistance, float refdistance, float dist, float attenuation );

//=============================================

extern const char *SOUND_EXTENSIONS[];
extern const size_t NUM_SOUND_EXTENSIONS;

extern const char *IMAGE_EXTENSIONS[];
extern const size_t NUM_IMAGE_EXTENSIONS;

//============================================
// memory utilities
//============================================

typedef struct block_allocator_s block_allocator_t;
typedef struct linear_allocator_s linear_allocator_t;

typedef void *( *alloc_function_t )( size_t, const char*, int );
typedef void ( *free_function_t )( void *ptr, const char*, int );

// Block Allocator
block_allocator_t * BlockAllocator( size_t elemSize, size_t blockSize, alloc_function_t alloc_function, free_function_t free_function );
void BlockAllocator_Free( block_allocator_t *ba );
void *BA_Alloc( block_allocator_t *ba );

linear_allocator_t * LinearAllocator( size_t elemSize, size_t preAllocate, alloc_function_t alloc_function, free_function_t free_function );
void LinearAllocator_Free( linear_allocator_t *la );
void *LA_Alloc( linear_allocator_t *la );
void *LA_Pointer( linear_allocator_t *la, size_t index );
size_t LA_Size( linear_allocator_t *la );

//==============================================================
//
//SYSTEM SPECIFIC
//
//==============================================================

typedef enum {
	ERR_FATAL,      // exit the entire game with a popup window
	ERR_DROP,       // print to console and disconnect from game
} com_error_code_t;

// this is only here so the functions in q_shared.c and q_math.c can link

#ifndef _MSC_VER
void Sys_Error( const char *error, ... ) __attribute__( ( format( printf, 1, 2 ) ) ) __attribute__( ( noreturn ) );
void Com_Printf( const char *msg, ... ) __attribute__( ( format( printf, 1, 2 ) ) );
void Com_Error( com_error_code_t code, const char *format, ... ) __attribute__( ( format( printf, 2, 3 ) ) ) __attribute__( ( noreturn ) );
#else
__declspec( noreturn ) void Sys_Error( _Printf_format_string_ const char *error, ... );
void Com_Printf( _Printf_format_string_ const char *msg, ... );
__declspec( noreturn ) void Com_Error( com_error_code_t code, _Printf_format_string_ const char *format, ... );
#endif

//==============================================================
//
//FILESYSTEM
//
//==============================================================


#define FS_READ             0
#define FS_WRITE            1
#define FS_APPEND           2
#define FS_NOSIZE           0x80    // FS_NOSIZE bit tells that we're not interested in real size of the file
// it is merely a hint a proper file size may still be returned by FS_Open
#define FS_GZ               0x100   // compress on write and decompress on read automatically
// doesn't work for pk3 files
#define FS_UPDATE           0x200
#define FS_SECURE           0x400
#define FS_CACHE            0x800

#define FS_RWA_MASK         ( FS_READ | FS_WRITE | FS_APPEND )

#define FS_SEEK_CUR         0
#define FS_SEEK_SET         1
#define FS_SEEK_END         2

typedef enum {
	FS_MEDIA_IMAGES,

	FS_MEDIA_NUM_TYPES
} fs_mediatype_t;

//==============================================================
//
//THREADS
//
//==============================================================

// equals to INFINITE on Windows and SDL_MUTEX_MAXWAIT
#define Q_THREADS_WAIT_INFINITE 0xFFFFFFFF

//==============================================================

// connection state of the client in the server
typedef enum {
	CS_FREE,            // can be reused for a new connection
	CS_ZOMBIE,          // client has been disconnected, but don't reuse
	                    // connection for a couple seconds
	CS_CONNECTING,      // has send a "new" command, is awaiting for fetching configstrings
	CS_CONNECTED,       // has been assigned to a client_t, but not in game yet
	CS_SPAWNED          // client is fully in game
} sv_client_state_t;

typedef enum {
	key_game,
	key_console,
	key_message,
	key_menu,
	key_delegate
} keydest_t;

typedef enum {
	rserr_ok,
	rserr_invalid_fullscreen,
	rserr_invalid_mode,
	rserr_invalid_driver,
	rserr_restart_required,
	rserr_unknown
} rserr_t;

// font style flags
typedef enum {
	QFONT_STYLE_NONE            = 0,
	QFONT_STYLE_ITALIC          = ( 1 << 0 ),
	QFONT_STYLE_BOLD            = ( 1 << 1 ),
	QFONT_STYLE_MASK            = ( 1 << 2 ) - 1
} qfontstyle_t;

// font drawing flags
typedef enum {
	TEXTDRAWFLAG_NO_COLORS  = 1 << 0,   // draw color codes instead of applying them
	TEXTDRAWFLAG_KERNING    = 1 << 1
} textdrawflag_t;

typedef enum {
	TOUCH_DOWN,
	TOUCH_UP,
	TOUCH_MOVE
} touchevent_t;

typedef enum {
	IN_DEVICE_KEYBOARD      = 1 << 0,
	IN_DEVICE_MOUSE         = 1 << 1,
	IN_DEVICE_JOYSTICK      = 1 << 2,
	IN_DEVICE_TOUCHSCREEN   = 1 << 3,
	IN_DEVICE_SOFTKEYBOARD  = 1 << 4
} in_devicemask_t;

#ifdef __cplusplus
};
#endif

#endif
