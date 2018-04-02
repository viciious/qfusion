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

#include "q_arch.h"
#include "q_math.h" // fixme : needed for MAX_S_COLORS define
#include "q_shared.h"

//============================================================================

const char *SOUND_EXTENSIONS[] = { ".ogg", ".wav" };
const size_t NUM_SOUND_EXTENSIONS = sizeof( SOUND_EXTENSIONS ) / sizeof( SOUND_EXTENSIONS[0] );

const char *IMAGE_EXTENSIONS[] = { ".tga", ".jpg", ".png", ".pcx", ".wal", ".ktx" }; // .ktx must be the last extension
const size_t NUM_IMAGE_EXTENSIONS = sizeof( IMAGE_EXTENSIONS ) / sizeof( IMAGE_EXTENSIONS[0] );

//============================================================================

/*
* COM_SanitizeFilePath
*
* Changes \ character to / characters in the string
* Does NOT validate the string at all
* Must be used before other functions are aplied to the string (or those functions might function improperly)
*/
char *COM_SanitizeFilePath( char *path ) {
	char *p;

	assert( path );

	p = path;
	while( *p && ( p = strchr( p, '\\' ) ) ) {
		*p = '/';
		p++;
	}

	return path;
}

/*
* COM_ValidateFilename
*/
bool COM_ValidateFilename( const char *filename ) {
	assert( filename );

	if( !filename || !filename[0] ) {
		return false;
	}

	// we don't allow \ in filenames, all user inputted \ characters are converted to /
	if( strchr( filename, '\\' ) ) {
		return false;
	}

	return true;
}

/*
* COM_ValidateRelativeFilename
*/
bool COM_ValidateRelativeFilename( const char *filename ) {
	if( !COM_ValidateFilename( filename ) ) {
		return false;
	}

	if( strstr( filename, ".." ) || strstr( filename, "//" ) ) {
		return false;
	}

	if( *filename == '/' || *filename == '.' ) {
		return false;
	}

	return true;
}

/*
* COM_StripExtension
*/
void COM_StripExtension( char *filename ) {
	char *src, *last = NULL;

	last = strrchr( filename, '/' );
	src = strrchr( last ? last : filename, '.' );
	if( src && *( src + 1 ) ) {
		*src = 0;
	}
}

/*
* COM_FileExtension
*/
const char *COM_FileExtension( const char *filename ) {
	const char *src, *last;

	last = strrchr( filename, '/' );
	src = strrchr( last ? last : filename, '.' );
	if( src && *( src + 1 ) ) {
		return src;
	}

	return NULL;
}

/*
* COM_DefaultExtension
* If path doesn't have extension, appends one to it
* If there is no room for it overwrites the end of the path
*/
void COM_DefaultExtension( char *path, const char *extension, size_t size ) {
	const char *src, *last;
	size_t extlen;

	assert( extension && extension[0] && strlen( extension ) < size );

	extlen = strlen( extension );

	// if path doesn't have a .EXT, append extension
	// (extension should include the .)
	last = strrchr( path, '/' );
	src = strrchr( last ? last : path, '.' );
	if( src && *( src + 1 ) ) {
		return;             // it has an extension

	}
	if( strlen( path ) + extlen >= size ) {
		path[size - extlen - 1] = 0;
	}
	Q_strncatz( path, extension, size );
}

/*
* COM_ReplaceExtension
* Replaces current extension, if there is none appends one
* If there is no room for it overwrites the end of the path
*/
void COM_ReplaceExtension( char *path, const char *extension, size_t size ) {
	assert( path );
	assert( extension && extension[0] && strlen( extension ) < size );

	COM_StripExtension( path );
	//COM_DefaultExtension( path, extension, size );

	// Vic: using COM_DefaultExtension here breaks filenames with multiple dots
	// and we have just stripped the extension in COM_StripExtension anyway
	if( *path && path[strlen( path ) - 1] != '/' ) {
		Q_strncatz( path, extension, size );
	}
}

/*
* COM_FileBase
*/
const char *COM_FileBase( const char *in ) {
	const char *s;

	s = strrchr( in, '/' );
	if( s ) {
		return s + 1;
	}

	return in;
}

/*
* COM_StripFilename
*
* Cuts the string of, at the last / or erases the whole string if not found
*/
void COM_StripFilename( char *filename ) {
	char *p;

	p = strrchr( filename, '/' );
	if( !p ) {
		p = filename;
	}

	*p = 0;
}

/*
* COM_FilePathLength
*
* Returns the length from start of string to the character before last /
*/
int COM_FilePathLength( const char *in ) {
	const char *s;

	s = strrchr( in, '/' );
	if( !s ) {
		s = in;
	}

	return s - in;
}

//============================================================================
//
//					BYTE ORDER FUNCTIONS
//
//============================================================================

short ShortSwap( short l ) {
	uint8_t b1, b2;

	b1 = l & 255;
	b2 = ( l >> 8 ) & 255;

	return ( b1 << 8 ) + b2;
}

#if !defined ( ENDIAN_LITTLE ) && !defined ( ENDIAN_BIG )
static short ShortNoSwap( short l ) {
	return l;
}
#endif

int LongSwap( int l ) {
	uint8_t b1, b2, b3, b4;

	b1 = l & 255;
	b2 = ( l >> 8 ) & 255;
	b3 = ( l >> 16 ) & 255;
	b4 = ( l >> 24 ) & 255;

	return ( (int)b1 << 24 ) + ( (int)b2 << 16 ) + ( (int)b3 << 8 ) + b4;
}

#if !defined ( ENDIAN_LITTLE ) && !defined ( ENDIAN_BIG )
static int LongNoSwap( int l ) {
	return l;
}
#endif

float FloatSwap( float f ) {
	union {
		float f;
		uint8_t b[4];
	} dat1, dat2;


	dat1.f = f;
	dat2.b[0] = dat1.b[3];
	dat2.b[1] = dat1.b[2];
	dat2.b[2] = dat1.b[1];
	dat2.b[3] = dat1.b[0];
	return dat2.f;
}

#if !defined ( ENDIAN_LITTLE ) && !defined ( ENDIAN_BIG )
static float FloatNoSwap( float f ) {
	return f;
}
#endif

#if !defined ( ENDIAN_LITTLE ) && !defined ( ENDIAN_BIG )

/*
* Swap_Init
*/
static void Swap_Init( void ) {
	uint8_t swaptest[2] = { 1, 0 };

	// set the byte swapping variables in a portable manner
	if( *(short *)swaptest == 1 ) {
		BigShort = ShortSwap;
		LittleShort = ShortNoSwap;
		BigLong = LongSwap;
		LittleLong = LongNoSwap;
		BigFloat = FloatSwap;
		LittleFloat = FloatNoSwap;
	} else {
		BigShort = ShortNoSwap;
		LittleShort = ShortSwap;
		BigLong = LongNoSwap;
		LittleLong = LongSwap;
		BigFloat = FloatNoSwap;
		LittleFloat = FloatSwap;
	}
}

static short BigShortDetectSwap( short l ) { Swap_Init(); return BigShort( l ); }
static short LittleShortDetectSwap( short l ) { Swap_Init(); return LittleShort( l ); }

static int BigLongDetectSwap( int l ) { Swap_Init(); return BigLong( l ); }
static int LittleLongDetectSwap( int l ) { Swap_Init(); return LittleLong( l ); }

static float BigFloatDetectSwap( float l ) { Swap_Init(); return BigFloat( l ); }
static float LittleFloatDetectSwap( float l ) { Swap_Init(); return LittleFloat( l ); }

short ( *BigShort )( short l ) = &BigShortDetectSwap;
short ( *LittleShort )( short l ) = &LittleShortDetectSwap;
int ( *BigLong )( int l ) = &BigLongDetectSwap;
int ( *LittleLong )( int l ) = &LittleLongDetectSwap;
float ( *BigFloat )( float l ) = &BigFloatDetectSwap;
float ( *LittleFloat )( float l ) = &LittleFloatDetectSwap;
#endif


/*
* TempVector
*
* This is just a convenience function
* for making temporary vectors for function calls
*/
float *tv( float x, float y, float z ) {
	static int index;
	static float vecs[8][3];
	float *v;

	// use an array so that multiple tempvectors won't collide
	// for a while
	v = vecs[index];
	index = ( index + 1 ) & 7;

	v[0] = x;
	v[1] = y;
	v[2] = z;

	return v;
}

/*
* VectorToString
*
* This is just a convenience function for printing vectors
*/
char *vtos( float v[3] ) {
	static int index;
	static char str[8][32];
	char *s;

	// use an array so that multiple vtos won't collide
	s = str[index];
	index = ( index + 1 ) & 7;

	Q_snprintfz( s, 32, "(%+6.3f %+6.3f %+6.3f)", v[0], v[1], v[2] );

	return s;
}

/*
* va_r
*
* does a varargs printf into a temp buffer, so I don't need to have
* varargs versions of all text functions.
*/
char *va_r( char *dest, size_t size, const char *format, ... ) {
	va_list argptr;
	va_start( argptr, format );
	Q_vsnprintfz( dest, size, format, argptr );
	va_end( argptr );
	return dest;
}

/*
* va
*
* does a varargs printf into a temp buffer, so I don't need to have
* varargs versions of all text functions.
*/
char *va( const char *format, ... ) {
	va_list argptr;
	static int str_index;
	static char string[8][2048];

	str_index = ( str_index + 1 ) & 7;
	va_start( argptr, format );
	Q_vsnprintfz( string[str_index], sizeof( string[0] ), format, argptr );
	va_end( argptr );

	return string[str_index];
}

/*
* COM_Compress
*
* Parse a token out of a string
*/
int COM_Compress( char *data_p ) {
	char *in, *out;
	int c;
	bool newline = false, whitespace = false;

	in = out = data_p;
	if( in ) {
		while( ( c = *in ) != 0 ) {
			// skip double slash comments
			if( c == '/' && in[1] == '/' ) {
				while( *in && *in != '\n' ) {
					in++;
				}
				// skip /* */ comments
			} else if( c == '/' && in[1] == '*' ) {
				while( *in && ( *in != '*' || in[1] != '/' ) )
					in++;
				if( *in ) {
					in += 2;
				}
				// record when we hit a newline
			} else if( c == '\n' || c == '\r' ) {
				newline = true;
				in++;
				// record when we hit whitespace
			} else if( c == ' ' || c == '\t' ) {
				whitespace = true;
				in++;
				// an actual token
			} else {
				// if we have a pending newline, emit it (and it counts as whitespace)
				if( newline ) {
					*out++ = '\n';
					newline = false;
					whitespace = false;
				}
				if( whitespace ) {
					*out++ = ' ';
					whitespace = false;
				}

				// copy quoted strings unmolested
				if( c == '"' ) {
					*out++ = c;
					in++;
					while( 1 ) {
						c = *in;
						if( c && c != '"' ) {
							*out++ = c;
							in++;
						} else {
							break;
						}
					}
					if( c == '"' ) {
						*out++ = c;
						in++;
					}
				} else {
					*out = c;
					out++;
					in++;
				}
			}
		}
	}
	*out = 0;
	return out - data_p;
}

/*
* COM_ParseExt2_r
*
* Parse a token out of a string
*/
char *COM_ParseExt2_r( char *token, size_t token_size, const char **data_p, bool nl, bool sq ) {
	int c;
	unsigned len;
	const char *data;
	bool newlines = false;

	data = *data_p;
	len = 0;
	token[0] = 0;

	if( !data ) {
		*data_p = NULL;
		return token;
	}

	// skip whitespace
skipwhite:
	while( (unsigned char)( c = *data ) <= ' ' ) {
		if( c == 0 ) {
			*data_p = NULL;
			return token;
		}
		if( c == '\n' ) {
			newlines = true;
		}
		data++;
	}

	if( newlines && !nl ) {
		*data_p = data;
		return token;
	}

	// skip // comments
	if( c == '/' && data[1] == '/' ) {
		data += 2;

		while( *data && *data != '\n' )
			data++;
		goto skipwhite;
	}

	// skip /* */ comments
	if( c == '/' && data[1] == '*' ) {
		data += 2;

		while( 1 ) {
			if( !*data ) {
				break;
			}
			if( *data != '*' || *( data + 1 ) != '/' ) {
				data++;
			} else {
				data += 2;
				break;
			}
		}
		goto skipwhite;
	}

	// handle quoted strings specially
	if( c == '\"' ) {
		if( sq ) {
			data++;
		}
		while( 1 ) {
			c = *data++;
			if( c == '\"' || !c ) {
				if( !c ) {
					data--;
				}

				if( ( len < token_size ) && ( !sq ) ) {
					token[len] = '\"';
					len++;
					//data++;
				}

				if( len == token_size ) {
					//Com_Printf ("Token exceeded %i chars, discarded.\n", (int)token_size);
					len = 0;
				}
				token[len] = 0;
				*data_p = data;
				return token;
			}
			if( len < token_size ) {
				token[len] = c;
				len++;
			}
		}
	}

	// parse a regular word
	do {
		if( len < token_size ) {
			token[len] = c;
			len++;
		}
		data++;
		c = *data;
	} while( (unsigned char)c > 32 );

	if( len == token_size ) {
		//Com_Printf ("Token exceeded %i chars, discarded.\n", (int)token_size);
		len = 0;
	}
	token[len] = 0;

	*data_p = data;
	return token;
}

static char com_token[MAX_TOKEN_CHARS];

/*
 * COM_ParseExt
 *
 * Parse a token out of a string
 */
char *COM_ParseExt2( const char **data_p, bool nl, bool sq ) {
	return COM_ParseExt2_r( com_token, MAX_TOKEN_CHARS, data_p, nl, sq );
}

/*
* Q_GrabCharFromColorString
*
* Parses a char or color escape sequence and advances (*pstr)
* "c" receives the character
* "colorindex", if not NULL, receives color indexes (0..10)
* Return values:
* GRABCHAR_END - end of string reached; *c will be '\0';  *colorindex is undefined
* GRABCHAR_CHAR - printable char parsed and saved to *c;  *colorindex is undefined
* GRABCHAR_COLOR - color escape parsed and saved to *colorindex;  *c is undefined
*/
int Q_GrabCharFromColorString( const char **pstr, char *c, int *colorindex ) {
	switch( **pstr ) {
		case '\0':
			*c = '\0';
			return GRABCHAR_END;

		case Q_COLOR_ESCAPE:
			if( ( *pstr )[1] >= '0' && ( *pstr )[1] < '0' + MAX_S_COLORS ) {
				if( colorindex ) {
					*colorindex = ColorIndex( ( *pstr )[1] );
				}
				( *pstr ) += 2; // skip the ^7
				return GRABCHAR_COLOR;
			} else if( ( *pstr )[1] == Q_COLOR_ESCAPE ) {
				*c = Q_COLOR_ESCAPE;
				( *pstr ) += 2; // skip the ^^
				return GRABCHAR_CHAR;
			}
		/* fall through */

		default:
			// normal char
			*c = **pstr;
			( *pstr )++;
			return GRABCHAR_CHAR;
	}
}

// Like Q_GrabCharFromColorString, but reads whole UTF-8 sequences
// and returns wide chars
int Q_GrabWCharFromColorString( const char **pstr, wchar_t *wc, int *colorindex ) {
	wchar_t num;

	num = Q_GrabWCharFromUtf8String( pstr );
	switch( num ) {
		case 0:
			*wc = 0;
			return GRABCHAR_END;

		case Q_COLOR_ESCAPE:
			if( **pstr >= '0' && **pstr < '0' + MAX_S_COLORS ) {
				if( colorindex ) {
					*colorindex = ColorIndex( **pstr );
				}
				( *pstr )++; // skip the color code
				return GRABCHAR_COLOR;
			} else if( **pstr == Q_COLOR_ESCAPE ) {
				*wc = Q_COLOR_ESCAPE;
				( *pstr )++; // skip the second ^
				return GRABCHAR_CHAR;
			}
		/* fall through */

		default:
			// normal char
			*wc = num;
			return GRABCHAR_CHAR;
	}
}


/*
* COM_RemoveColorTokensExt
*
* Remove color tokens from a string
* If "draw" is set, all printable ^^ and ^ will be become ^^ (e.g. ^a --> ^^a),
* so the result string may end up up to 1.5 times longer
* (only a final ^ really needs duplicating, it's just easier to do it for all)
*/
const char *COM_RemoveColorTokensExt( const char *str, bool draw ) {
	static char cleanString[MAX_STRING_CHARS];
	char *out = cleanString, *end = cleanString + sizeof( cleanString );
	const char *in = str;
	char c;
	int gc;

	while( out + 1 < end ) {
		gc = Q_GrabCharFromColorString( &in, &c, NULL );
		if( gc == GRABCHAR_CHAR ) {
			if( c == Q_COLOR_ESCAPE && draw ) {
				// write two tokens so ^^1 doesn't turn into ^1 which is a color code
				if( out + 2 == end ) {
					break;
				}
				*out++ = Q_COLOR_ESCAPE;
				*out++ = Q_COLOR_ESCAPE;
			} else {
				*out++ = c;
			}
		} else if( gc == GRABCHAR_COLOR ) {
			;
		} else if( gc == GRABCHAR_END ) {
			break;
		} else {
			assert( 0 );
		}
	}

	*out = '\0';
	return cleanString;
}

/*
* COM_SanitizeColorString
*
* Redundant color codes are removed: "^1^2text" ==> "^2text", "a^7" --> "a"
* Color codes preceding whitespace are moved to before the first non-whitespace
* char: "^1  a" ==> "  ^1a" (makes trimming spaces from the resulting string easier)
* A trailing ^ is duplicated: "a^" --> "a^^", to make sure we can easily append ^7
* (so the output may need 1 byte more than input)
* ----------------------------
* "bufsize" is size of output buffer including trailing zero, so strlen() of output
* string will be bufsize-1 at max
* "maxprintablechars" is how many printable (non-color-escape) characters
* to write at most. Use -1 for no limit.
* "startcolor" is the assumed color of the string if there are no color escapes
* E.g. if startcolor is 7, leading ^7 sequences will be dropped;
* if "startcolor" is -1, initial color is undefined, so "^7foo" will be written as is.
* ----------------------------
* Returns number of printable chars written
*/
int COM_SanitizeColorString( const char *str, char *buf, int bufsize, int maxprintablechars, int startcolor ) {
	char *out = buf, *end = buf + bufsize;
	const char *in = str;
	int oldcolor = startcolor, newcolor = startcolor;
	char c;
	int gc, colorindex;
	int c_printable = 0;

	if( maxprintablechars == -1 ) {
		maxprintablechars = INT_MAX;
	}

	while( out + 1 < end && c_printable < maxprintablechars ) {
		gc = Q_GrabCharFromColorString( &in, &c, &colorindex );

		if( gc == GRABCHAR_CHAR ) {
			bool emitcolor = ( newcolor != oldcolor && c != ' ' ) ? true : false;
			int numbytes = ( c == Q_COLOR_ESCAPE ) ? 2 : 1; // ^ will be duplicated
			if( emitcolor ) {
				numbytes += 2;
			}

			if( !( out + numbytes < end ) ) {
				break;  // no space to fit everything, so drop all

			}
			// emit the color escape if necessary
			if( emitcolor ) {
				*out++ = Q_COLOR_ESCAPE;
				*out++ = newcolor + '0';
				oldcolor = newcolor;
			}

			// emit the printable char
			*out++ = c;
			if( c == Q_COLOR_ESCAPE ) {
				*out++ = Q_COLOR_ESCAPE;
			}
			c_printable++;

		} else if( gc == GRABCHAR_COLOR ) {
			newcolor = colorindex;
		} else if( gc == GRABCHAR_END ) {
			break;
		} else {
			assert( 0 );
		}
	}
	*out = '\0';

	return c_printable;
}

/*
* Q_ColorStringTerminator
*
* Returns a color sequence to append to input string so that subsequent
* characters have desired color (we can't just append ^7 because the string
* may end in a ^, that would make the ^7 printable chars and color would stay)
* Initial color is assumed to be white
* Possible return values (assuming finalcolor is 7):
* "" if no color needs to be appended,
* "^7" or
* "^^7" if the string ends in an unterminated ^
*/
const char *Q_ColorStringTerminator( const char *str, int finalcolor ) {
	char c;
	int lastcolor = ColorIndex( COLOR_WHITE ), colorindex;
	const char *s = str;

	// see what color the string ends in
	while( 1 ) {
		int gc = Q_GrabCharFromColorString( &s, &c, &colorindex );
		if( gc == GRABCHAR_CHAR ) {
			;
		} else if( gc == GRABCHAR_COLOR ) {
			lastcolor = colorindex;
		} else if( gc == GRABCHAR_END ) {
			break;
		} else {
			assert( 0 );
		}
	}

	if( lastcolor == finalcolor ) {
		return "";
	} else {
		int escapecount = 0;
		static char buf[4];
		char *p = buf;

		// count up trailing ^'s
		while( --s >= str )
			if( *s == Q_COLOR_ESCAPE ) {
				escapecount++;
			} else {
				break;
			}

		if( escapecount & 1 ) {
			*p++ = Q_COLOR_ESCAPE;
		}
		*p++ = Q_COLOR_ESCAPE;
		*p++ = '0' + finalcolor;
		*p++ = '\0';

		return buf;
	}
}

/*
* Q_ColorStrLastColor
*
* Returns the last color in a string, or the previous color specified in the argument.
*/
int Q_ColorStrLastColor( int previous, const char *s, int maxlen ) {
	char c;
	const char *end = s;
	int lastcolor = previous, colorindex;

	if( maxlen > 0 ) {
		end += maxlen;
	}

	while( ( s < end ) || ( maxlen < 0 ) ) {
		int gc = Q_GrabCharFromColorString( &s, &c, &colorindex );
		if( gc == GRABCHAR_CHAR ) {
			;
		} else if( gc == GRABCHAR_COLOR ) {
			lastcolor = colorindex;
		} else if( gc == GRABCHAR_END ) {
			break;
		} else {
			assert( 0 );
		}
	}

	return lastcolor;
}


/*
* COM_RemoveJunkChars
*
* Remove junk chars from a string (created for autoaction filenames)
*/
const char *COM_RemoveJunkChars( const char *in ) {
	static char cleanString[MAX_STRING_CHARS];
	char *out = cleanString, *end = cleanString + sizeof( cleanString ) - 1;

	if( in ) {
		while( *in && ( out < end ) ) {
			if( isalpha( *in ) || isdigit( *in ) ) {
				// keep it
				*out = *in;
				in++;
				out++;
			} else if( *in == '<' || *in == '[' || *in == '{' ) {
				*out = '(';
				in++;
				out++;
			} else if( *in == '>' || *in == ']' || *in == '}' ) {
				*out = ')';
				in++;
				out++;
			} else if( *in == '.' || *in == '/' || *in == '_' ) {
				*out = '_';
				in++;
				out++;
			} else {
				// another char
				// skip it
				in++;
			}
		}
	}

	*out = '\0';
	return cleanString;
}

/*
* COM_ReadColorRGBString
*/
int COM_ReadColorRGBString( const char *in ) {
	int playerColor[3];
	if( in && in[0] ) {
		if( sscanf( in, "%3i %3i %3i", &playerColor[0], &playerColor[1], &playerColor[2] ) == 3 ) {
			return COLOR_RGB( playerColor[0], playerColor[1], playerColor[2] );
		}
	}
	return -1;
}

int COM_ValidatePlayerColor( int rgbcolor ) {
	int r, g, b;

	r = COLOR_R( rgbcolor );
	g = COLOR_G( rgbcolor );
	b = COLOR_B( rgbcolor );

	if( r >= 200 || g >= 200 || b >= 200 ) {
		return rgbcolor;
	}

	if( r + g >= 255 || g + b >= 255 || r + b >= 255 ) {
		return rgbcolor;
	}

	if( r + g + b >= 128 * 3 ) {
		return rgbcolor;
	}

	r = r > 127 ? 255 : 128 + r;
	g = g > 127 ? 255 : 128 + g;
	b = b > 127 ? 255 : 128 + b;
	return COLOR_RGB( r, g, b );
}

/*
* COM_ListNameForPosition
*/
char *COM_ListNameForPosition( const char *namesList, int position, const char separator ) {
	static char buf[MAX_STRING_CHARS];
	const char *s, *t;
	char *b;
	int count, len;

	if( !namesList ) {
		return NULL;
	}

	// set up the tittle from the spinner names
	s = namesList;
	t = s;
	count = 0;
	buf[0] = 0;
	b = buf;
	while( *s && ( s = strchr( s, separator ) ) ) {
		if( count == position ) {
			len = s - t;
			if( len <= 0 ) {
				return NULL;
			}
			if( len > MAX_STRING_CHARS - 1 ) {
				len = MAX_STRING_CHARS - 1;
			}
			s = t + len;
			while( t <= s ) {
				if( *t == separator || t == s ) {
					*b = 0;
					break;
				}

				*b = *t;
				t++;
				b++;
			}

			break;
		}

		count++;
		s++;
		t = s;
	}

	if( buf[0] == 0 ) {
		return NULL;
	}

	return buf;
}

//============================================================================
//
//					LIBRARY REPLACEMENT FUNCTIONS
//
//============================================================================

/*
* Q_memset32
*/
void *Q_memset32( void *dest, int c, size_t dwords ) {
	assert( ( (size_t)dest & 0x03 ) == 0 );

#if defined ( __GNUC__ ) && defined ( id386 )
	__asm__ (   "cld\n"
				"rep; stosl\n"
				: // nada
				: "c" ( dwords ), "D" ( dest ), "a" ( c )
				);
#elif defined ( _WIN32 ) && defined ( id386 )
	__asm {
		cld
		mov edi, dest
		mov eax, c
		mov ecx, dwords
		repne stosd
	}
#else
	{
		size_t i;
		int *idest = (int *)dest;
		for( i = 0; i < dwords; i++ )
			*idest++ = c;
	}
#endif

	return dest;
}

/*
* Q_strncpyz
*/
void Q_strncpyz( char *dest, const char *src, size_t size ) {
#ifdef HAVE_STRLCPY
	strlcpy( dest, src, size );
#else
	if( size ) {
		while( --size && ( *dest++ = *src++ ) ) ;
		*dest = '\0';
	}
#endif
}

/*
* Q_strncatz
*/
void Q_strncatz( char *dest, const char *src, size_t size ) {
#ifdef HAVE_STRLCAT
	strlcat( dest, src, size );
#else
	if( size ) {
		while( --size && *dest++ ) ;
		if( size ) {
			dest--; size++;
			while( --size && ( *dest++ = *src++ ) ) ;
		}
		*dest = '\0';
	}
#endif
}

/*
* Q_vsnprintfz
*/
int Q_vsnprintfz( char *dest, size_t size, const char *format, va_list argptr ) {
	int len;

	assert( dest );
	assert( size );

	len = vsnprintf( dest, size, format, argptr );
	dest[size - 1] = 0;

	return len;
}

/*
* Q_snprintfz
*/
int Q_snprintfz( char *dest, size_t size, const char *format, ... ) {
	int len;
	va_list argptr;

	va_start( argptr, format );
	len = Q_vsnprintfz( dest, size, format, argptr );
	va_end( argptr );

	return len;
}

/*
* Q_strupr
*/
char *Q_strupr( char *s ) {
	char *p;

	if( s ) {
		for( p = s; *s; s++ )
			*s = toupper( *s );
		return p;
	}

	return NULL;
}

/*
* Q_strlwr
*/
char *Q_strlwr( char *s ) {
	char *p;

	if( s ) {
		for( p = s; *s; s++ )
			*s = tolower( *s );
		return p;
	}

	return NULL;
}

/*
* Q_strlocate
*/
const char *Q_strlocate( const char *s, const char *substr, int skip ) {
	int i;
	const char *p = NULL;
	size_t substr_len;

	if( !s || !*s ) {
		return NULL;
	}

	if( !substr || !*substr ) {
		return NULL;
	}

	substr_len = strlen( substr );

	for( i = 0; i <= skip; i++, s = p + substr_len ) {
		if( !( p = strstr( s, substr ) ) ) {
			return NULL;
		}
	}
	return p;
}

/*
* Q_strcount
*/
size_t Q_strcount( const char *s, const char *substr ) {
	size_t cnt;
	size_t substr_len;

	if( !s || !*s ) {
		return 0;
	}

	if( !substr || !*substr ) {
		return 0;
	}

	substr_len = strlen( substr );

	cnt = 0;
	while( ( s = strstr( s, substr ) ) != NULL ) {
		cnt++;
		s += substr_len;
	}

	return cnt;
}

/*
* Q_strrstr
*/
const char *Q_strrstr( const char *s, const char *substr ) {
	const char *p;

	s = p = strstr( s, substr );
	while( s != NULL ) {
		p = s;
		s = strstr( s + 1, substr );
	}

	return p;
}

/*
* Q_trim
*/
#define IS_TRIMMED_CHAR( s ) ( ( s ) == ' ' || ( s ) == '\t' || ( s ) == '\r' || ( s ) == '\n' )
char *Q_trim( char *s ) {
	char *t = s;
	size_t len;

	// remove leading whitespace
	while( IS_TRIMMED_CHAR( *t ) ) t++;
	len = strlen( s ) - ( t - s );
	if( s != t ) {
		memmove( s, t, len + 1 );
	}

	// remove trailing whitespace
	while( len && IS_TRIMMED_CHAR( s[len - 1] ) )
		s[--len] = '\0';

	return s;
}

/*
* Q_WCharUtf8Length
*
* Returns the length of wchar_t encoded as UTF-8.
*/
size_t Q_WCharUtf8Length( wchar_t wc ) {
	unsigned int num = wc;

	if( !num ) {
		return 0;
	}
	if( num <= 0x7f ) {
		return 1;
	}
	if( num <= 0x7ff ) {
		return 2;
	}
	if( num <= 0xffff ) {
		return 3;
	}
	return 1; // 4-octet sequences are replaced with '?'
}

/*
* Q_WCharToUtf8
*
* Converts wchar_t to UTF-8 and returns the length of the written sequence.
*/
size_t Q_WCharToUtf8( wchar_t wc, char *dest, size_t bufsize ) {
	unsigned int num = wc;
	size_t ret = 0;

	if( num ) {
		if( num <= 0x7f ) {
			if( bufsize > 1 ) {
				*dest++ = num;
				ret = 1;
			}
		} else if( num <= 0x7ff ) {
			if( bufsize > 2 ) {
				*dest++ = 0xC0 | ( num >> 6 );
				*dest++ = 0x80 | ( num & 0x3f );
				ret = 2;
			}
		} else if( num <= 0xffff ) {
			if( bufsize > 3 ) {
				*dest++ = 0xE0 | ( num >> 12 );
				*dest++ = 0x80 | ( ( num & 0xfc0 ) >> 6 );
				*dest++ = 0x80 | ( num & 0x003f );
				ret = 3;
			}
		} else {
			// sorry, we don't support 4-octet sequences
			if( bufsize > 1 ) {
				*dest++ = '?';
				ret = 1;
			}
		}
	}

	if( bufsize > ret ) {
		*dest++ = '\0';
	}

	return ret;
}

/*
* Q_WCharToUtf8Char
*
* Converts wchar_t to UTF-8 using a temporary buffer.
*/
char *Q_WCharToUtf8Char( wchar_t wc ) {
	static char buf[5]; // longest valid utf-8 sequence is 4 bytes
	Q_WCharToUtf8( wc, buf, sizeof( buf ) );
	return buf;
}

/*
* Q_WCharToUtf8String
*
* Converts a wchar_t string (of the system native wchar size) to UTF-8.
* Returns the length of the written string.
*/
size_t Q_WCharToUtf8String( const wchar_t *ws, char *dest, size_t bufsize ) {
	size_t len = 0, utflen;

	if( !bufsize ) {
		return 0;
	}

	dest[0] = '\0';

	while( ( bufsize > 1 ) && *ws ) {
		utflen = Q_WCharToUtf8( *ws, dest, bufsize );
		if( !utflen ) {
			break;
		}

		ws++;
		dest += utflen;
		bufsize -= utflen;
		len += utflen;
	}

	return len;
}

/*
* Q_Utf8SyncPos
*
* For line editing: if we're in the middle of a UTF-8 sequence,
* skip left or right to the start of a UTF-8 sequence (or end of string)
* 'dir' should be UTF8SYNC_LEFT or UTF8SYNC_RIGHT
* Returns new position
* ------------------------------
* (To be pedantic, I must note that we may skip too many continuation chars
* in a malformed UTF-8 string.  But malformed UTF-8 isn't supposed to get
* into the console input line, and even if it does, we'll only be happy
* to delete it all with one BACKSPACE stroke)
*/
int Q_Utf8SyncPos( const char *str, int pos, int dir ) {
	// Skip until we hit an ASCII char (char & 0x80 == 0)
	// or the start of a utf-8 sequence (char & 0xC0 == 0xC0).
	if( dir == UTF8SYNC_LEFT ) {
		while( pos > 0 && !( ( str[pos] & 0x80 ) == 0 || ( str[pos] & 0x40 ) ) )
			pos--;
	} else {
		while( !( ( str[pos] & 0x80 ) == 0 || ( str[pos] & 0x40 ) ) )
			pos++;
	}

	return pos;
}

// returns a wide char, advances (*pstr) to next char (unless at end of string)
wchar_t Q_GrabWCharFromUtf8String( const char **pstr ) {
	int part, i;
	wchar_t val;
	const char *src = *pstr;

	if( !*src ) {
		return 0;
	}

	part = ( unsigned char )*src;
	src++;

	if( !( part & 0x80 ) ) { // 1 octet
		val = part;
	} else if( ( part & 0xE0 ) == 0xC0 ) {   // 2 octets
		val = ( part & 0x1F ) << 6;
		if( ( *src & 0xC0 ) != 0x80 ) {
			val = '?'; // incomplete 2-octet sequence (including unexpected '\0')
		} else {
			val |= *src & 0x3f;
			src++;
			if( val < 0x80 ) {
				val = '?';  // overlong utf-8 sequence
			}
		}
	} else if( ( part & 0xF0 ) == 0xE0 ) {   // 3 octets
		val = ( part & 0x0F ) << 12;
		if( ( *src & 0xC0 ) != 0x80 ) { // check 2nd octet
			val = '?';
		} else {
			val |= ( *src & 0x3f ) << 6;
			src++;
			if( ( *src & 0xC0 ) != 0x80 ) { // check 3rd octet
				val = '?';
			} else {
				val |= *src & 0x3f;
				src++;
				if( val < 0x800 ) {
					val = '?';  // overlong sequence
				}
			}
		}
	} else if( ( part & 0xF8 ) == 0xF0 ) {   // 4 octets
		// throw it away (it may be a valid sequence, we just don't support it)
		val = '?';
		for( i = 0; i < 4 && ( *src & 0xC0 ) == 0x80; i++ )
			src++;
	} else {
		val = '?';  // invalid utf-8 octet

	}
	*pstr = src;
	return val;
}

/*
* Q_FixTruncatedUtf8
*
* Terminates a UTF-8 string correctly if it's cut to a specific buffer length (for instance, when using strncpyz).
*/
void Q_FixTruncatedUtf8( char *str ) {
	size_t len = strlen( str );
	const char *temp;
	if( !len ) {
		return;
	}

	len = Q_Utf8SyncPos( str, len - 1, UTF8SYNC_LEFT );
	temp = str + len;
	if( ( *temp != '?' ) && ( Q_GrabWCharFromUtf8String( &temp ) == '?' ) ) {
		str[len] = '\0';
	}
}

/*
* Q_IsBreakingSpace
*/
bool Q_IsBreakingSpace( const char *str ) {
	const unsigned char *s = ( const unsigned char * )str;

	switch( s[0] ) {
		case ' ':
		case '\t':
			return true;
		case 0xe3:
			return ( s[1] == 0x80 ) && ( s[2] == 0x80 );
		case 0xe2:
			return ( s[1] == 0x80 ) && ( s[2] >= 0x80 ) && ( s[2] <= 0x8b );
	}

	return false;
}

/*
* Q_IsBreakingSpaceChar
*
* Same as IsBreakingSpace, but for a single character.
*/
bool Q_IsBreakingSpaceChar( wchar_t c ) {
	return ( c == ' ' ) || ( c == '\t' ) || ( c == 0x3000 ) || ( ( c >= 0x2000 ) && ( c <= 0x200b ) );
}

/*
* Q_isdigit
*/
bool Q_isdigit( const char *str ) {
	if( str && *str ) {
		while( isdigit( *str ) ) str++;
		if( !*str ) {
			return true;
		}
	}
	return false;
}

/*
* Q_chrreplace
*/
char *Q_chrreplace( char *s, const char subj, const char repl ) {
	char *t = s;
	while( ( t = strchr( t, subj ) ) != NULL )
		*t++ = repl;
	return s;
}

/*
* Q_urlencode_unsafechars
*/
void Q_urlencode_unsafechars( const char *src, char *dst, size_t dst_size ) {
	size_t i, n, len;

	assert( src );
	assert( dst );

	if( !src || !dst || !dst_size ) {
		return;
	}

	len = strlen( src );
	if( len >= dst_size ) {
		len = dst_size - 1;
	}

	// urlencode
	n = 0;
	for( i = 0; i < len && n < dst_size - 1; i++ ) {
		char c = src[i];

		if( c == ' ' || c == '#' || c == '%' ||
			c == '<' || c == '>' || c == '{' || c == '}' ||
			c == '|' || c == '\\' || c == '^' || c == '~' ||
			c == '[' || c == ']' ) {
			// urlencode
			if( n + 3 >= dst_size ) {
				// not enough space
				break;
			}

			dst[n  ] = '%';
			sprintf( &dst[n + 1], "%02x", (int)c );
			n += 3;
		} else {
			dst[n] = src[i];
			n++;
		}
	}
	dst[n] = '\0';
}

/*
* Q_urldecode
*/
#define hex2dec( x ) ( ( ( x ) <= '9' ? ( x ) - '0' : ( ( x ) <= 'F' ) ? ( x ) - 'A' + 10 : ( x ) - 'a' + 10 ) )
size_t Q_urldecode( const char *src, char *dst, size_t dst_size ) {
	char *dst_start = dst, *dst_end = dst + dst_size - 1;
	const char *src_end;

	if( !src || !dst || !dst_size ) {
		return 0;
	}

	src_end = src + strlen( src );
	while( src < src_end ) {
		if( dst == dst_end ) {
			break;
		}
		if( ( *src == '%' ) && ( src + 2 < src_end ) &&
			( isxdigit( src[1] ) && isxdigit( src[2] ) ) ) {
			*dst++ = ( hex2dec( src[1] ) << 4 ) + hex2dec( src[2] );
			src += 3;
		} else {
			*dst++ = *src++;
		}
	}

	*dst = '\0';
	return dst - dst_start;
}

//=====================================================================
//
//  INFO STRINGS
//
//=====================================================================


/*
* COM_ValidateConfigstring
*/
bool COM_ValidateConfigstring( const char *string ) {
	const char *p;
	bool opened = false;
	int parity = 0;

	if( !string ) {
		return false;
	}

	p = string;
	while( *p ) {
		if( *p == '\"' ) {
			if( opened ) {
				parity--;
				opened = false;
			} else {
				parity++;
				opened = true;
			}
		}
		p++;
	}

	if( parity != 0 ) {
		return false;
	}

	return true;
}

/*
* Info_ValidateValue
*/
static bool Info_ValidateValue( const char *value ) {
	assert( value );

	if( !value ) {
		return false;
	}

	if( strlen( value ) >= MAX_INFO_VALUE ) {
		return false;
	}

	if( strchr( value, '\\' ) ) {
		return false;
	}

	if( strchr( value, ';' ) ) {
		return false;
	}

	if( strchr( value, '"' ) ) {
		return false;
	}

	return true;
}

/*
* Info_ValidateKey
*/
static bool Info_ValidateKey( const char *key ) {
	assert( key );

	if( !key ) {
		return false;
	}

	if( !key[0] ) {
		return false;
	}

	if( strlen( key ) >= MAX_INFO_KEY ) {
		return false;
	}

	if( strchr( key, '\\' ) ) {
		return false;
	}

	if( strchr( key, ';' ) ) {
		return false;
	}

	if( strchr( key, '"' ) ) {
		return false;
	}

	return true;
}

/*
* Info_Validate
*
* Some characters are illegal in info strings because they
* can mess up the server's parsing
*/
bool Info_Validate( const char *info ) {
	const char *p, *start;

	assert( info );

	if( !info ) {
		return false;
	}

	if( strlen( info ) >= MAX_INFO_STRING ) {
		return false;
	}

	if( strchr( info, '\"' ) ) {
		return false;
	}

	if( strchr( info, ';' ) ) {
		return false;
	}

	if( strchr( info, '"' ) ) {
		return false;
	}

	p = info;

	while( p && *p ) {
		if( *p++ != '\\' ) {
			return false;
		}

		start = p;
		p = strchr( start, '\\' );
		if( !p ) { // missing key
			return false;
		}
		if( p - start >= MAX_INFO_KEY ) { // too long
			return false;
		}

		p++; // skip the \ char

		start = p;
		p = strchr( start, '\\' );
		if( ( p && p - start >= MAX_INFO_KEY ) || ( !p && strlen( start ) >= MAX_INFO_KEY ) ) { // too long
			return false;
		}
	}

	return true;
}

/*
* Info_CleanValue
*
* Removes invalid characters from a userinfo value.
* Used internally by steamlib at least.
*/
void Info_CleanValue( const char *in, char *out, size_t outsize ) {
	size_t len = 0;
	int c;

	if( !outsize ) {
		return;
	}

	clamp_high( outsize, MAX_INFO_VALUE );

	while( ( len + 1 < outsize ) && ( ( ( c = *in ) != '\0' ) ) ) {
		in++;

		if( c == '\\' ) {
			continue;
		}
		if( c == ';' ) {
			continue;
		}
		if( c == '"' ) {
			continue;
		}

		out[len++] = c;
	}

	out[len] = '\0';
}

/*
* Info_FindKey
*
* Returns the pointer to the \ character if key is found
* Otherwise returns NULL
*/
static char *Info_FindKey( const char *info, const char *key ) {
	const char *p, *start;
	size_t key_len;

	assert( Info_Validate( info ) );
	assert( Info_ValidateKey( key ) );

	if( !Info_Validate( info ) || !Info_ValidateKey( key ) ) {
		return NULL;
	}

	p = info;
	key_len = strlen( key );

	while( p && *p ) {
		start = p;

		p++; // skip the \ char
		if( !strncmp( key, p, key_len ) && p[key_len] == '\\' ) {
			return (char *)start;
		}

		p = strchr( p, '\\' );
		if( !p ) {
			return NULL;
		}

		p++; // skip the \ char
		p = strchr( p, '\\' );
	}

	return NULL;
}

/*
* Info_ValueForKey
*
* Searches the string for the given
* key and returns the associated value, or NULL
*/
char *Info_ValueForKey( const char *info, const char *key ) {
	static char value[2][MAX_INFO_VALUE]; // use two buffers so compares work without stomping on each other
	static int valueindex;
	const char *p, *start;
	size_t len;

	assert( info && Info_Validate( info ) );
	assert( key && Info_ValidateKey( key ) );

	if( !Info_Validate( info ) || !Info_ValidateKey( key ) ) {
		return NULL;
	}

	valueindex ^= 1;

	p = Info_FindKey( info, key );
	if( !p ) {
		return NULL;
	}

	p++; // skip the \ char
	p = strchr( p, '\\' );
	if( !p ) {
		return NULL;
	}

	p++; // skip the \ char
	start = p;
	p = strchr( p, '\\' );
	if( !p ) {
		len = strlen( start );
	} else {
		len = p - start;
	}

	if( len >= MAX_INFO_VALUE ) {
		assert( false );
		return NULL;
	}
	strncpy( value[valueindex], start, len );
	value[valueindex][len] = 0;

	return value[valueindex];
}

/*
* Info_RemoveKey
*/
void Info_RemoveKey( char *info, const char *key ) {
	assert( info && Info_Validate( info ) );
	assert( key && Info_ValidateKey( key ) );

	if( !Info_Validate( info ) || !Info_ValidateKey( key ) ) {
		return;
	}

	while( 1 ) {
		char *start, *p;

		p = Info_FindKey( info, key );
		if( !p ) {
			return;
		}

		start = p;

		p++; // skip the \ char
		p = strchr( p, '\\' );
		if( p ) {
			p++; // skip the \ char
			p = strchr( p, '\\' );
		}

		if( !p ) {
			*start = 0;
		} else {
			// aiwa : fixed possible source and destination overlap with strcpy()
			memmove( start, p, strlen( p ) + 1 );
		}
	}
}

/*
* Info_SetValueForKey
*/
bool Info_SetValueForKey( char *info, const char *key, const char *value ) {
	char pair[MAX_INFO_KEY + MAX_INFO_VALUE + 1];

	assert( info && Info_Validate( info ) );
	assert( key && Info_ValidateKey( key ) );
	assert( value && Info_ValidateValue( value ) );

	if( !Info_Validate( info ) || !Info_ValidateKey( key ) || !Info_ValidateValue( value ) ) {
		return false;
	}

	Info_RemoveKey( info, key );

	Q_snprintfz( pair, sizeof( pair ), "\\%s\\%s", key, value );

	if( strlen( pair ) + strlen( info ) > MAX_INFO_STRING ) {
		return false;
	}

	Q_strncatz( info, pair, MAX_INFO_STRING );

	return true;
}


//=====================================================================
//
//  SOUND ATTENUATION
//
//=====================================================================

/*
* Q_GainForAttenuation
*/
float Q_GainForAttenuation( int model, float maxdistance, float refdistance, float dist, float attenuation ) {
	float gain = 0.0f;

	switch( model ) {
		case 0:
			//gain = (1 - AL_ROLLOFF_FACTOR * (distance * AL_REFERENCE_DISTANCE) / (AL_MAX_DISTANCE - AL_REFERENCE_DISTANCE))
			//AL_LINEAR_DISTANCE
			dist = min( dist, maxdistance );
			gain = ( 1 - attenuation * ( dist - refdistance ) / ( maxdistance - refdistance ) );
			break;
		case 1:
		default:
			//gain = (1 - AL_ROLLOFF_FACTOR * (distance - AL_REFERENCE_DISTANCE) / (AL_MAX_DISTANCE - AL_REFERENCE_DISTANCE))
			//AL_LINEAR_DISTANCE_CLAMPED
			dist = max( dist, refdistance );
			dist = min( dist, maxdistance );
			gain = ( 1 - attenuation * ( dist - refdistance ) / ( maxdistance - refdistance ) );
			break;
		case 2:
			//gain = AL_REFERENCE_DISTANCE / (AL_REFERENCE_DISTANCE + AL_ROLLOFF_FACTOR * (distance - AL_REFERENCE_DISTANCE));
			//AL_INVERSE_DISTANCE
			gain = refdistance / ( refdistance + attenuation * ( dist - refdistance ) );
			break;
		case 3:
			//AL_INVERSE_DISTANCE_CLAMPED
			//gain = AL_REFERENCE_DISTANCE / (AL_REFERENCE_DISTANCE + AL_ROLLOFF_FACTOR * (distance - AL_REFERENCE_DISTANCE));
			dist = max( dist, refdistance );
			dist = min( dist, maxdistance );
			gain = refdistance / ( refdistance + attenuation * ( dist - refdistance ) );
			break;
		case 4:
			//AL_EXPONENT_DISTANCE
			//gain = (distance / AL_REFERENCE_DISTANCE) ^ (- AL_ROLLOFF_FACTOR)
			gain = pow( ( dist / refdistance ), ( -attenuation ) );
			break;
		case 5:
			//AL_EXPONENT_DISTANCE_CLAMPED
			//gain = (distance / AL_REFERENCE_DISTANCE) ^ (- AL_ROLLOFF_FACTOR)
			dist = max( dist, refdistance );
			dist = min( dist, maxdistance );
			gain = pow( ( dist / refdistance ), ( -attenuation ) );
			break;
		case 6:
			// qfusion gain
			dist -= 80;
			if( dist < 0 ) {
				dist = 0;
			}
			gain = 1.0 - dist * attenuation * 0.0001;
			break;
	}

	return gain;
}

//==========================================

/*
    ch : rewrite the creator functions so that they take
    these functions as callback parameters and store these
    to the allocator objects

        void *Alloc( size_t size, const char *filename, int fileline );
        void Free( void *data, const char *filename, int fileline );

    and then forget the pool. this is so that we can generalize these utilities
    to be used in all modules, like Game Sound etc..
*/

/*
    BLOCK ALLOCATOR

    cant implicitly free elements.
    memory is not linear nor in order, no random access supported.

    create new block allocator
        BlockAllocator( pool, elemSize, blockSize, alloc, free )

    allocate element with the allocator
        BA_Alloc( block_allocator )

    release the memory of the allocator
        BlockAllocator_Free( block_allocator )

    TODO: Sys_Error instead of return 0?
*/

#define BA_DEFAULT_BLOCKSIZE    32

// typedef void *( *alloc_function_t )( size_t, const char*, int );
// typedef void ( *free_function_t )( void *ptr );

typedef struct block_alloc_block_s {
	void *base;                 // base memory
	size_t numAllocs;           // number of allocations on this block
	struct block_alloc_block_s *prev;   // umm, we dont really need prev?
	struct block_alloc_block_s *next;
} block_alloc_block_t;

struct block_allocator_s {
	size_t blockSize;   // number of elements in block
	size_t elemSize;    // number of bytes in elements
	block_alloc_block_t *blocks;

	alloc_function_t alloc;
	free_function_t free;
};

block_allocator_t * BlockAllocator( size_t elemSize, size_t blockSize, alloc_function_t alloc_function, free_function_t free_function ) {
	block_allocator_t *ba;

	if( !elemSize ) {
		return NULL;
	}

	if( !blockSize ) {
		blockSize = BA_DEFAULT_BLOCKSIZE;
	}

	ba = (block_allocator_t*)alloc_function( sizeof( *ba ), __FILE__, __LINE__ );
	if( !ba ) {
		Sys_Error( "BlockAllocator: Failed to create allocator\n" );
		return NULL;
	}

	memset( ba, 0, sizeof( *ba ) );
	ba->blockSize = blockSize;
	ba->elemSize = elemSize;

	ba->alloc = alloc_function;
	ba->free = free_function;

	return ba;
}

void *BA_Alloc( block_allocator_t *ba ) {
	block_alloc_block_t *b;

	// find a free block
	for( b = ba->blocks; b ; b = b->next ) {
		if( b->numAllocs < ba->blockSize ) {
			b->numAllocs++;
			return ( (unsigned char*)b->base ) + ( ( b->numAllocs - 1 ) * ba->elemSize );
		}
	}

	// lets allocate new block
	b = (block_alloc_block_t*)ba->alloc( sizeof( *b ) + ba->blockSize * ba->elemSize, __FILE__, __LINE__ );
	if( !b ) {
		Sys_Error( "BlockAllocator: Failed to allocate element\n" );
		return NULL;
	}

	memset( b, 0, sizeof( *b ) );
	b->base = (void*)( &b[1] );

	// since no linearity nor ordering is required,
	// just drop this to the start of the list
	b->next = ba->blocks;
	b->prev = 0;
	if( ba->blocks ) {
		ba->blocks->prev = b;
	}
	ba->blocks = b;

	b->numAllocs++;
	return b->base;
}

void BlockAllocator_Free( block_allocator_t *ba ) {
	block_alloc_block_t *b, *next;

	// release all memory
	for( b = ba->blocks; b; ) {
		next = b->next;
		ba->free( b, __FILE__, __LINE__ );
		b = next;
	}

	ba->free( ba, __FILE__, __LINE__ );
}

//============================================

/*
    Linear Alloator

    cant implicitly free elements.
    memory is linear and does support random access.

    create new linear allocator  (preAllocate is num of elements)
        LinearAllocator( pool, elemSize, preAllocate)

    allocate element with the allocator (on "top" of the block)
        LA_Alloc( linear_allocator )

    get pointer to an element
        LA_Pointer( linear_allocator, size_t index )

    release the memory of the allocator
        LinearAllocator_Free(linear_allocator )

    TODO: resize?
    TODO: Sys_Error instead of return 0?
*/

// minimum number of preallocated elems, also in reallocation
#define LA_MIN_PREALLOCATE  16

struct linear_allocator_s {
	void *base;
	size_t elemSize;
	size_t allocatedElems;  // number of elements allocated and in-use
	size_t allocatedActual; // number of actual elements allocated

	alloc_function_t alloc;
	free_function_t free;
};

linear_allocator_t * LinearAllocator( size_t elemSize, size_t preAllocate, alloc_function_t alloc_function, free_function_t free_function ) {
	linear_allocator_t *la;
	size_t size;

	if( !elemSize ) {
		return NULL;
	}

	if( preAllocate < LA_MIN_PREALLOCATE ) {
		preAllocate = LA_MIN_PREALLOCATE;
	}

	size = preAllocate * elemSize + sizeof( *la );

	la = (linear_allocator_t*)alloc_function( size, __FILE__, __LINE__ );
	if( !la ) {
		Sys_Error( "LinearAllocator: failed to create allocator\n" );
		return NULL;
	}
	memset( la, 0, sizeof( *la ) );
	la->base = (void*)( &la[1] );
	la->elemSize = elemSize;
	la->allocatedElems = 0;
	la->allocatedActual = preAllocate;

	la->alloc = alloc_function;
	la->free = free_function;

	return la;
}

void *LA_Alloc( linear_allocator_t *la ) {
	size_t currSize, newSize;

	if( la->allocatedElems < la->allocatedActual ) {
		la->allocatedElems++;
		return ( (unsigned char*)la->base ) + ( la->allocatedElems - 1 ) * la->elemSize;
	}

	currSize = sizeof( *la ) + la->allocatedActual * la->elemSize;
	newSize = currSize + LA_MIN_PREALLOCATE * la->elemSize;

	la = (linear_allocator_t*)la->alloc( newSize, __FILE__, __LINE__ );
	if( !la ) {
		Sys_Error( "LinearAllocator: Failed to allocate element\n" );
		return NULL;
	}

	// fix the base pointer
	la->base = (void*)( &la[1] );

	// and the size
	la->allocatedActual += LA_MIN_PREALLOCATE;
	la->allocatedElems++;
	return ( (unsigned char*)la->base ) + ( la->allocatedElems - 1 ) * la->elemSize;
}

void *LA_Pointer( linear_allocator_t *la, size_t index ) {
	// Sys_Error?
	if( index >= la->allocatedElems ) {
		Sys_Error( "LinearAllocator: Incorrect index in LA_Pointer\n" );
		return NULL;
	}

	return ( (unsigned char*)la->base ) + index * la->elemSize;
}

size_t LA_Size( linear_allocator_t *la ) {
	return la->allocatedElems;
}

void LinearAllocator_Free( linear_allocator_t *la ) {
	la->free( la, __FILE__, __LINE__ );
}
