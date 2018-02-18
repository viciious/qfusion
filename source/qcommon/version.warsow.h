/*
Copyright (C) 2007 Victor Luchits

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

#ifndef APPLICATION
#define APPLICATION                     "Warsow"
#endif

#ifndef APPLICATION_UTF8
#define APPLICATION_UTF8                "War\xC2\xA7ow"
#endif

#define DEFAULT_BASEGAME                "basewsw"

#ifndef APP_VERSION_MAJOR
#define APP_VERSION_MAJOR               2
#endif

#ifndef APP_VERSION_MINOR
#define APP_VERSION_MINOR               2
#endif

#ifndef APP_VERSION_UPDATE
#define APP_VERSION_UPDATE              0
#endif

#ifndef APP_VERSION
#define APP_VERSION                     APP_VERSION_MAJOR + APP_VERSION_MINOR * 0.1 + APP_VERSION_UPDATE * 0.01
#endif

#ifdef PUBLIC_BUILD
#define APP_PROTOCOL_VERSION            23
#else
#define APP_PROTOCOL_VERSION            2300
#endif

#ifdef PUBLIC_BUILD
#define APP_DEMO_PROTOCOL_VERSION       21
#else
#define APP_DEMO_PROTOCOL_VERSION       21
#endif

#ifndef APP_URL
#define APP_URL                         "http://www.warsow.net/"
#endif

#ifndef APP_COPYRIGHT_OWNER
#define APP_COPYRIGHT_OWNER             "Warsow Development Team"
#endif

#ifndef APP_SCREENSHOTS_PREFIX
#define APP_SCREENSHOTS_PREFIX          "wsw_"
#endif

#undef STR_HELPER
#undef STR_TOSTR

#define STR_HELPER( s )                 # s
#define STR_TOSTR( x )                  STR_HELPER( x )

#define APP_PROTOCOL_VERSION_STR        STR_TOSTR( APP_PROTOCOL_VERSION )
#define APP_DEMO_PROTOCOL_VERSION_STR   STR_TOSTR( APP_DEMO_PROTOCOL_VERSION )
#define APP_DEMO_EXTENSION_STR          ".wdz" APP_DEMO_PROTOCOL_VERSION_STR

#define APP_URI_SCHEME                  APPLICATION "://"
#define APP_URI_PROTO_SCHEME            APPLICATION STR_TOSTR( APP_PROTOCOL_VERSION ) "://"

#ifndef APP_VERSION_STR
#define APP_VERSION_STR                 STR_TOSTR( APP_VERSION_MAJOR ) "." STR_TOSTR( APP_VERSION_MINOR ) STR_TOSTR( APP_VERSION_UPDATE )
#endif

#ifndef APP_VERSION_STR_MAJORMINOR
#define APP_VERSION_STR_MAJORMINOR      STR_TOSTR( APP_VERSION_MAJOR ) STR_TOSTR( APP_VERSION_MINOR )
#endif

#ifndef APP_UPDATE_URL
#define APP_UPDATE_URL                  "http://update.warsow.net/"
#define APP_SERVER_UPDATE_DIRECTORY     "autoupdate/" STR_TOSTR ( APP_VERSION_MAJOR ) "." STR_TOSTR( APP_VERSION_MINOR ) "/"
#define APP_SERVER_UPDATE_FILE          "filelist.txt"
#define APP_CLIENT_UPDATE_FILE          "warsow_last_version.txt"
#endif

#ifdef PUBLIC_BUILD
#define APP_MATCHMAKER_URL              "https://mm.warsow.net:1338"
#define APP_MATCHMAKER_WEB_URL          "https://www.warsow.net/wmm/"
#else
#define APP_MATCHMAKER_URL              "http://mm-dev.warsow.net:1337"
#define APP_MATCHMAKER_WEB_URL          "http://www-dev.warsow.net/wmm/"
#endif

#ifndef APP_UI_BASEPATH
#define APP_UI_BASEPATH                 "ui/porkui"
#endif

#ifndef APP_STARTUP_COLOR
#define APP_STARTUP_COLOR               0x1c1416
#endif

//
// the following macros are only used by the windows resource file
//
#ifdef __GNUC__

#ifndef APP_VERSION_RC_STR
#define APP_VERSION_RC_STR              STR_TOSTR( APP_VERSION_MAJOR ) "." STR_TOSTR( APP_VERSION_MINOR )
#endif

#ifndef APP_FILEVERSION_RC_STR
#define APP_FILEVERSION_RC_STR          STR_TOSTR( APP_VERSION_MAJOR ) "," STR_TOSTR( APP_VERSION_MINOR ) "," STR_TOSTR( APP_VERSION_UPDATE ) ",0"
#endif

#else // __GNUC__

#ifndef APP_VERSION_RC
#define APP_VERSION_RC                  APP_VERSION_MAJOR.APP_VERSION_MINOR
#endif

#ifndef APP_VERSION_RC_STR
#define APP_VERSION_RC_STR              STR_TOSTR( APP_VERSION_RC )
#endif

#ifndef APP_FILEVERSION_RC
#define APP_FILEVERSION_RC              APP_VERSION_MAJOR,APP_VERSION_MINOR,APP_VERSION_UPDATE,0
#endif

#ifndef APP_FILEVERSION_RC_STR
#define APP_FILEVERSION_RC_STR          STR_TOSTR( APP_FILEVERSION_RC )
#endif

#endif // __GNUC__

#ifndef APP_DEFAULT_LANGUAGE
#define APP_DEFAULT_LANGUAGE            "en"
#endif

#ifndef APP_ICO_ICON
#define APP_ICO_ICON                    "../../icons/warsow.ico"
#endif

#ifndef APP_DEMO_ICO_ICON
#define APP_DEMO_ICO_ICON               "../../icons/warsow-demo.ico"
#endif

#ifndef APP_XPM_ICON
#define APP_XPM_ICON                    "../../icons/warsow256x256.xpm"
#endif

#ifndef APP_STEAMID
#define APP_STEAMID                     265850
#endif
