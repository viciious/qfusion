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

#include <SDL.h>
#include "../client/client.h"

SDL_Window *sdl_window;

static int VID_WndProc( void *wnd, int ev, int p1, int p2 ) {
	sdl_window = wnd;
	return 0;
}

/*
 * VID_Sys_Init
 */
rserr_t VID_Sys_Init( const char *applicationName, const char *screenshotsPrefix, int startupColor,
					  const int *iconXPM, void *parentWindow, bool verbose ) {
	return re.Init( applicationName, screenshotsPrefix, startupColor, 0, iconXPM,
					NULL, VID_WndProc, parentWindow, verbose );
}

/*
 * VID_UpdateWindowPosAndSize
 */
void VID_UpdateWindowPosAndSize( int x, int y ) {
	SDL_SetWindowPosition( sdl_window, x, y );
}

/*
 * VID_EnableAltTab
 */
void VID_EnableAltTab( bool enable ) {
}

/*
 * VID_GetWindowHandle - The sound module may require the handle when using Window's directsound
 */
void *VID_GetWindowHandle( void ) {
	return (void *)NULL;
}

/*
 * VID_EnableWinKeys
 */
void VID_EnableWinKeys( bool enable ) {
}

/*
 * VID_FlashWindow
 *
 * Sends a flash message to inactive window
 */
void VID_FlashWindow( int count ) {
}

/*
 * VID_GetSysModes
 */
unsigned int VID_GetSysModes( vidmode_t *modes ) {
#ifdef __APPLE__
	// only support borderless fullscreen because Alt+Tab doesn't work in fullscreen
	if( modes ) {
		VID_GetDefaultMode( &modes[0].width, &modes[0].height );
	}
	return 1;
#else
	int num;
	SDL_DisplayMode mode;
	int prevwidth = 0, prevheight = 0;
	unsigned int ret = 0;

	num = SDL_GetNumDisplayModes( 0 );
	if( num < 1 ) {
		return 0;
	}

	while( num-- ) { // reverse to help the sorting a little
		if( SDL_GetDisplayMode( 0, num, &mode ) ) {
			continue;
		}

		if( SDL_BITSPERPIXEL( mode.format ) < 15 ) {
			continue;
		}

		if( ( mode.w == prevwidth ) && ( mode.h == prevheight ) ) {
			continue;
		}

		if( modes ) {
			modes[ret].width = mode.w;
			modes[ret].height = mode.h;
		}

		prevwidth = mode.w;
		prevheight = mode.h;

		ret++;
	}

	return ret;
#endif
}

/*
 * VID_GetDefaultMode
 */
bool VID_GetDefaultMode( int *width, int *height ) {
	SDL_DisplayMode mode;
	SDL_GetDesktopDisplayMode( 0, &mode );

	*width = mode.w;
	*height = mode.h;

	return true;
}

/*
 * VID_GetPixelRatio
 */
float VID_GetPixelRatio( void ) {
#if SDL_VERSION_ATLEAST( 2,0,4 )
	float vdpi;

	if( SDL_GetDisplayDPI( 0, NULL, NULL, &vdpi ) == 0 ) {
		return vdpi / 96.0f;
	}
#endif

	return 1.0f; // TODO: check if retina?
}
