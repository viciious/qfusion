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

#define MUSIC_BUFFER_SIZE       8192

#define MUSIC_PRELOAD_MSEC      200

#define MUSIC_BUFFERING_SIZE    ( MUSIC_BUFFER_SIZE * 4 + 4000 )
#define BACKGROUND_TRACK_BUFFERING_TIMEOUT  5000

// =================================

static bgTrack_t *s_bgTrack;
static bgTrack_t *s_bgTrackHead;

static bool s_bgTrackPaused = false;  // the track is manually paused
static int s_bgTrackLocked = 0;     // the track is blocked by the game (e.g. the window's minimized)
static bool s_bgTrackMuted = false;
static volatile bool s_bgTrackBuffering = false;
static volatile bool s_bgTrackLoading = false; // unset by s_bgOpenThread when finished loading
static struct qthread_s *s_bgOpenThread;

/*
* S_AllocTrack
*/
static bgTrack_t *S_AllocTrack( const char *filename ) {
	bgTrack_t *track;

	track = (bgTrack_t *)S_Malloc( sizeof( *track ) + strlen( filename ) + 1 );
	track->stream = NULL;
	track->ignore = false;
	track->filename = (char *)( (uint8_t *)track + sizeof( *track ) );
	strcpy( track->filename, filename );
	track->isUrl = trap_FS_IsUrl( filename );
	track->muteOnPause = track->isUrl;
	track->anext = s_bgTrackHead;
	s_bgTrackHead = track;

	return track;
}

/*
* S_ValidMusicFile
*/
static bool S_ValidMusicFile( bgTrack_t *track ) {
	return ( track->stream != NULL ) && ( !track->isUrl || !S_EoStream( track->stream ) );
}

/*
* S_CloseMusicTrack
*/
static void S_CloseMusicTrack( bgTrack_t *track ) {
	if( !track->stream ) {
		return;
	}

	S_CloseStream( track->stream );
	track->stream = NULL;
}

/*
* S_OpenMusicTrack
*/
static bool S_OpenMusicTrack( bgTrack_t *track, bool *buffering ) {
	const char *filename = track->filename;

	if( track->ignore ) {
		return false;
	}

mark0:
	if( buffering ) {
		*buffering = false;
	}

	if( !track->stream ) {
		bool delay = false;

		track->stream = S_OpenStream( filename, &delay );
		if( track->stream && delay ) {
			// let the background track buffer for a while
			//Com_Printf( "S_OpenMusicTrack: buffering %s...\n", track->filename );
			if( buffering ) {
				*buffering = true;
			}
		}
	} else {
		if( !S_ResetStream( track->stream ) ) {
			// if seeking failed for whatever reason (stream?), try reopening again
			S_CloseMusicTrack( track );
			goto mark0;
		}
	}

	return true;
}

/*
* S_PrevMusicTrack
*/
static bgTrack_t *S_PrevMusicTrack( bgTrack_t *track ) {
	bgTrack_t *prev;

	prev = track ? track->prev : NULL;
	if( prev ) {
		track = prev->next;        // HACK to prevent endless loops where original 'track' comes from stack
	}
	while( prev && prev != track ) {
		if( !prev->ignore ) {
			break;
		}
		prev = prev->next;
	}

	return prev;
}

/*
* S_NextMusicTrack
*/
static bgTrack_t *S_NextMusicTrack( bgTrack_t *track ) {
	bgTrack_t *next;

	next = track ? track->next : NULL;
	if( next ) {
		track = next->prev;        // HACK to prevent endless loops where original 'track' comes from stack
	}
	while( next && next != track ) {
		if( !next->ignore ) {
			break;
		}
		next = next->next;
	}

	return next;
}

/*
* S_OpenBackgroundTrackProc
*/
static void *S_OpenBackgroundTrackProc( void *ptrack ) {
	bgTrack_t *track = (bgTrack_t *)ptrack;
	unsigned start;
	bool buffering;

	S_OpenMusicTrack( track, &buffering );

	s_bgTrackBuffering = buffering;

	start = trap_Milliseconds();
	while( s_bgTrackBuffering ) {
		if( trap_Milliseconds() > start + BACKGROUND_TRACK_BUFFERING_TIMEOUT ) {
		} else if( S_EoStream( track->stream ) ) {
		} else {
			if( S_SeekSteam( track->stream, MUSIC_BUFFERING_SIZE, SEEK_SET ) < 0 ) {
				continue;
			}
			S_SeekSteam( track->stream, 0, SEEK_SET );
		}

		// in case we delayed openening to let the stream be cached for a while,
		// start actually reading from it now
		if( !S_ContOpenStream( track->stream ) ) {
			track->ignore = true;
		}
		s_bgTrackBuffering = false;
	}

	s_bgTrack = track;
	s_bgTrackLoading = false;
	return NULL;
}

/*
* S_OpenBackgroundTrackTask
*/
static void S_OpenBackgroundTrackTask( bgTrack_t *track ) {
	s_bgTrackLoading = true;
	s_bgTrackBuffering = false;
	s_bgOpenThread = trap_Thread_Create( S_OpenBackgroundTrackProc, track );
}

/*
* S_CloseBackgroundTrackTask
*/
static void S_CloseBackgroundTrackTask( void ) {
	s_bgTrackBuffering = false;
	trap_Thread_Join( s_bgOpenThread );
	s_bgOpenThread = NULL;
}

// =================================

#define MAX_PLAYLIST_ITEMS 1024
typedef struct playlistItem_s {
	bgTrack_t *track;
	int order;
} playlistItem_t;

/*
* R_SortPlaylistItems
*/
static int R_PlaylistItemCmp( const playlistItem_t *i1, const playlistItem_t *i2 ) {
	if( i1->order > i2->order ) {
		return 1;
	}
	if( i2->order > i1->order ) {
		return -1;
	}
	return 0;
}

static void R_SortPlaylistItems( int numItems, playlistItem_t *items ) {
	qsort( items, numItems, sizeof( *items ), ( int ( * )( const void *, const void * ) )R_PlaylistItemCmp );
}

/*
* S_ReadPlaylistFile
*/
static bgTrack_t *S_ReadPlaylistFile( const char *filename, bool shuffle, bool loop ) {
	int filenum, length;
	char *tmpname = 0;
	size_t tmpname_size = 0;
	char *data, *line, *entry;
	playlistItem_t items[MAX_PLAYLIST_ITEMS];
	int i, numItems = 0;

	length = trap_FS_FOpenFile( filename, &filenum, FS_READ );
	if( length < 0 ) {
		return NULL;
	}

	// load the playlist into memory
	data = (char *)S_Malloc( length + 1 );
	trap_FS_Read( data, length, filenum );
	trap_FS_FCloseFile( filenum );

	srand( time( NULL ) );

	while( *data ) {
		size_t s;

		entry = data;

		// read the whole line
		for( line = data; *line != '\0' && *line != '\n'; line++ ) ;

		// continue reading from the next character, if possible
		data = ( *line == '\0' ? line : line + 1 );

		*line = '\0';

		// trim whitespaces, tabs, etc
		entry = Q_trim( entry );

		// special M3U entry or comment
		if( !*entry || *entry == '#' ) {
			continue;
		}

		if( trap_FS_IsUrl( entry ) ) {
			items[numItems].track = S_AllocTrack( entry );
		} else {
			// append the entry name to playlist path
			s = strlen( filename ) + 1 + strlen( entry ) + 1;
			if( s > tmpname_size ) {
				if( tmpname ) {
					S_Free( tmpname );
				}
				tmpname_size = s;
				tmpname = (char *)S_Malloc( tmpname_size );
			}

			Q_strncpyz( tmpname, filename, tmpname_size );
			COM_StripFilename( tmpname );
			Q_strncatz( tmpname, "/", tmpname_size );
			Q_strncatz( tmpname, entry, tmpname_size );
			COM_SanitizeFilePath( tmpname );

			items[numItems].track = S_AllocTrack( tmpname );
		}

		if( ++numItems == MAX_PLAYLIST_ITEMS ) {
			break;
		}
	}

	if( tmpname ) {
		S_Free( tmpname );
		tmpname = NULL;
	}

	if( !numItems ) {
		return NULL;
	}

	// set the playing order
	for( i = 0; i < numItems; i++ )
		items[i].order = ( shuffle ? ( rand() % numItems ) : i );

	// sort the playlist
	R_SortPlaylistItems( numItems, items );

	// link the playlist
	for( i = 1; i < numItems; i++ ) {
		items[i - 1].track->next = items[i].track;
		items[i].track->prev = items[i - 1].track;
		items[i].track->loop = loop;
	}
	items[numItems - 1].track->next = items[0].track;
	items[0].track->prev = items[numItems - 1].track;
	items[0].track->loop = loop;

	return items[0].track;
}

/*
* S_AdvanceBackgroundTrack
*/
static bool S_AdvanceBackgroundTrack( int n ) {
	bgTrack_t *track;

	if( n < 0 ) {
		track = S_PrevMusicTrack( s_bgTrack );
	} else {
		track = S_NextMusicTrack( s_bgTrack );
	}

	if( track && track != s_bgTrack ) {
		S_CloseBackgroundTrackTask();
		S_CloseMusicTrack( s_bgTrack );
		S_OpenBackgroundTrackTask( track );
		return true;
	}

	return false;
}

// =================================

/*
* Local helper functions
*/
static bool music_process( void ) {
	int l = 0;
	snd_stream_t *music_stream;
	uint8_t decode_buffer[MUSIC_BUFFER_SIZE];

	while( S_GetRawSamplesLength() < MUSIC_PRELOAD_MSEC ) {
		music_stream = s_bgTrack->stream;
		if( music_stream ) {
			l = S_ReadStream( music_stream, MUSIC_BUFFER_SIZE, decode_buffer );
		} else {
			l = 0;
		}

		if( !l ) {
			if( !s_bgTrack->loop ) {
				if( !S_AdvanceBackgroundTrack( 1 ) ) {
					if( !S_ValidMusicFile( s_bgTrack ) ) {
						return false;
					}
				}

				if( s_bgTrackBuffering || s_bgTrackLoading ) {
					return true;
				}
			}

			music_stream = s_bgTrack->stream;
			if( !music_stream || !S_ResetStream( music_stream ) ) {
				// if failed, close the track?
				return false;
			}

			continue;
		}

		S_RawSamples2( l / ( music_stream->info.width * music_stream->info.channels ),
					   music_stream->info.rate, music_stream->info.width,
					   music_stream->info.channels, decode_buffer, true,
					   s_bgTrackMuted ? 0 : 1 );
	}

	return true;
}

/*
* Sound system wide functions (snd_loc.h)
*/

void S_UpdateMusic( void ) {
	if( !s_bgTrack ) {
		return;
	}
	if( !s_musicvolume->value && !s_bgTrack->muteOnPause ) {
		return;
	}
	if( s_bgTrackLoading || s_bgTrackPaused || s_bgTrackLocked > 0 ) {
		return;
	}

	if( !music_process() ) {
		Com_Printf( "Error processing music data\n" );
		S_StopBackgroundTrack();
		return;
	}
}

/*
* Global functions (sound.h)
*/
void S_StartBackgroundTrack( const char *intro, const char *loop, int mode ) {
	const char *ext;
	bgTrack_t *introTrack, *loopTrack;
	bgTrack_t *firstTrack = NULL;

	// Stop any existing music that might be playing
	S_StopBackgroundTrack();

	if( !intro || !intro[0] ) {
		return;
	}

	s_bgTrackMuted = false;
	s_bgTrackPaused = false;

	ext = COM_FileExtension( intro );
	if( ext && !Q_stricmp( ext, ".m3u" ) ) {
		// mode bits:
		// 1 - shuffle
		// 2 - loop the selected track
		// 3 - stream (even if muted)
		if( loop && loop[0] ) {
			mode = atoi( loop );
		}

		firstTrack = S_ReadPlaylistFile( intro,
										 mode & 1 ? true : false, mode & 2 ? true : false );
		if( firstTrack ) {
			goto start_playback;
		}
	}

	// the intro track loops unless another loop track has been specified
	introTrack = S_AllocTrack( intro );
	introTrack->next = introTrack->prev = introTrack;
	introTrack->muteOnPause = introTrack->isUrl || mode & 4 ? true : false;

	if( loop && loop[0] && Q_stricmp( intro, loop ) ) {
		loopTrack = S_AllocTrack( loop );
		if( S_OpenMusicTrack( loopTrack, NULL ) ) {
			S_CloseMusicTrack( loopTrack );

			introTrack->next = introTrack->prev = loopTrack;
			introTrack->loop = false;

			loopTrack->loop = true;
			loopTrack->muteOnPause = loopTrack->isUrl || mode & 4 ? true : false;
			loopTrack->next = loopTrack->prev = loopTrack;
		}
	}

	firstTrack = introTrack;

start_playback:

	if( !firstTrack || firstTrack->ignore ) {
		S_StopBackgroundTrack();
		return;
	}

	S_OpenBackgroundTrackTask( firstTrack );

	S_UpdateMusic();
}

void S_StopBackgroundTrack( void ) {
	bgTrack_t *next;

	S_StopRawSamples();

	S_CloseBackgroundTrackTask();

	while( s_bgTrackHead ) {
		next = s_bgTrackHead->anext;

		S_CloseMusicTrack( s_bgTrackHead );
		S_Free( s_bgTrackHead );

		s_bgTrackHead = next;
	}

	s_bgTrack = NULL;
	s_bgTrackHead = NULL;

	s_bgTrackMuted = false;
	s_bgTrackPaused = false;
}

/*
* S_PrevBackgroundTrack
*/
void S_PrevBackgroundTrack( void ) {
	S_AdvanceBackgroundTrack( -1 );
}

/*
* S_NextBackgroundTrack
*/
void S_NextBackgroundTrack( void ) {
	S_AdvanceBackgroundTrack(  1 );
}

/*
* S_PauseBackgroundTrack
*/
void S_PauseBackgroundTrack( void ) {
	if( !s_bgTrack ) {
		return;
	}

	// in case of a streaming URL or video, mute but keep streaming
	if( s_bgTrack->muteOnPause ) {
		s_bgTrackMuted = !s_bgTrackMuted;
		return;
	}

	s_bgTrackPaused = !s_bgTrackPaused;
}

/*
* S_LockBackgroundTrack
*/
void S_LockBackgroundTrack( bool lock ) {
	if( s_bgTrack && !s_bgTrack->isUrl ) {
		s_bgTrackLocked += lock ? 1 : -1;
	} else {
		s_bgTrackLocked = 0;
	}
}
