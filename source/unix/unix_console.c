#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

#include "../qcommon/qcommon.h"

bool stdin_active = true;
cvar_t *nostdout = NULL;
bool nostdout_backup_val = false;

char *Sys_ConsoleInput( void ) {
	static char text[256];
	int len;
	fd_set fdset;
	struct timeval timeout;

	if( !dedicated || !dedicated->integer ) {
		return NULL;
	}

	if( !stdin_active ) {
		return NULL;
	}

	FD_ZERO( &fdset );
	FD_SET( 0, &fdset ); // stdin
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	if( select( 1, &fdset, NULL, NULL, &timeout ) == -1 || !FD_ISSET( 0, &fdset ) ) {
		return NULL;
	}

	len = read( 0, text, sizeof( text ) );
	if( len == 0 ) { // eof!
		Com_Printf( "EOF from stdin, console input disabled...\n" );
		stdin_active = false;
		return NULL;
	}

	if( len < 1 ) {
		return NULL;
	}

	text[len - 1] = 0; // rip off the /n and terminate

	return text;
}

static void Sys_AnsiColorPrint( const char *msg ) {
	static char buffer[2096];
	int length = 0;
	static const int q3ToAnsi[ MAX_S_COLORS ] =
	{
		30, // COLOR_BLACK
		31, // COLOR_RED
		32, // COLOR_GREEN
		33, // COLOR_YELLOW
		34, // COLOR_BLUE
		36, // COLOR_CYAN
		35, // COLOR_MAGENTA
		0   // COLOR_WHITE
	};

	while( *msg ) {
		char c = *msg;
		int colorindex;

		int gc = Q_GrabCharFromColorString( &msg, &c, &colorindex );
		if( gc == GRABCHAR_COLOR || ( gc == GRABCHAR_CHAR && c == '\n' ) ) {
			// First empty the buffer
			if( length > 0 ) {
				buffer[length] = '\0';
				fputs( buffer, stdout );
				length = 0;
			}

			if( c == '\n' ) {
				// Issue a reset and then the newline
				fputs( "\033[0m\n", stdout );
			} else {
				// Print the color code
				Q_snprintfz( buffer, sizeof( buffer ), "\033[%dm", q3ToAnsi[ colorindex ] );
				fputs( buffer, stdout );
			}
		} else if( gc == GRABCHAR_END ) {
			break;
		} else {
			if( length >= sizeof( buffer ) - 1 ) {
				break;
			}
			buffer[length++] = c;
		}
	}

	// Empty anything still left in the buffer
	if( length > 0 ) {
		buffer[length] = '\0';
		fputs( buffer, stdout );
	}
}

void Sys_ConsoleOutput( char *string ) {
	if( Cvar_Initialized() ) {
		if( !nostdout ) {
			nostdout = Cvar_Get( "nostdout", "0", 0 );
		}
		nostdout_backup_val = nostdout && nostdout->integer;
	}

	if( nostdout_backup_val ) {
		return;
	}

	if( isatty( STDOUT_FILENO ) ) {
		Sys_AnsiColorPrint( string );
	} else {
		fputs( COM_RemoveColorTokens( string ), stdout );
	}
}
