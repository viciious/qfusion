#include "Api.h"
#include "CefApp.h"
#include "UiFacade.h"

#include "include/cef_base.h"
#include "include/cef_app.h"
#include "include/wrapper/cef_helpers.h"

// if API is different, the dll cannot be used
extern "C" int UI_API( void ) {
	return UI_API_VERSION;
}

// We have to provide arguments for process/sub process creation
bool UI_Init( int argc, char **argv, void *hInstance, int vidWidth, int vidHeight,
			  int protocol, const char *demoExtension, const char *basePath ) {
	return UiFacade::Init( argc, argv, hInstance, vidWidth, vidHeight, protocol, demoExtension, basePath );
}

void UI_Shutdown() {
	UiFacade::Shutdown();
}

void UI_TouchAllAssets() {
	// Unused for now...
}

// OK we have to track state here and if there were state changes emit signals for the background process
extern "C" void UI_Refresh( int64_t time, int clientState, int serverState,
							bool demoPlaying, const char *demoName, bool demoPaused,
							unsigned int demoTime, bool backGround, bool showCursor ) {
	UiFacade::Instance()->Refresh( time, clientState, serverState, demoPlaying, demoName,
								   demoPaused, demoTime, backGround, showCursor );
}

// Same applies to here
extern "C" void UI_UpdateConnectScreen( const char *serverName, const char *rejectMessage,
										int downloadType, const char *downloadFilename,
										float downloadPercent, int downloadSpeed,
										int connectCount, bool backGround ) {
	UiFacade::Instance()->UpdateConnectScreen( serverName, rejectMessage, downloadType, downloadFilename,
											   downloadPercent, downloadSpeed, connectCount, backGround );
}

extern "C" void UI_Keydown( int context, int key ) {
	UiFacade::Instance()->Keydown( context, key );
}

extern "C" void UI_Keyup( int context, int key ) {
	UiFacade::Instance()->Keyup( context, key );
}

extern "C" void UI_CharEvent( int context, wchar_t key ) {
	UiFacade::Instance()->CharEvent( context, key );
}

extern "C" void UI_MouseMove( int context, int frameTime, int dx, int dy ) {
	UiFacade::Instance()->MouseMove( context, frameTime, dx, dy );
}

extern "C" void UI_MouseSet( int context, int mx, int my, bool showCursor ) {
	UiFacade::Instance()->MouseSet( context, mx, my, showCursor );
}

extern "C" void UI_ForceMenuOff( void ) {
	UiFacade::Instance()->ForceMenuOff();
}

extern "C" bool UI_HaveQuickMenu( void ) {
	// TODO: We can't say, it requires polling!
	// TODO: Change apis for explicit polling!
	return false;
}

bool UI_TouchEvent( int context, int id, touchevent_t type, int x, int y ) {
	// Unsupported!
	return false;
}

bool UI_IsTouchDown( int context, int id ) {
	// Unsupported!
	return false;
}

void UI_CancelTouches( int context ) {
	// Unsupported!
}

void UI_ShowQuickMenu( bool show ) {
	UiFacade::Instance()->ShowQuickMenu( show );
}

void UI_AddToServerList( const char *adr, const char *info ) {
	// TODO: This is kept just to provide some procedure address
	// Ask UI/frontend folks what do they expect
}

ui_import_t *api;

static ui_import_t api_local;

extern "C" QF_DLL_EXPORT ui_export_t *GetUIAPI( ui_import_t *import ) {
	static ui_export_t exported;

	exported.Init = UI_Init;
	exported.Shutdown = UI_Shutdown;
	exported.API = UI_API;
	exported.TouchAllAssets = UI_TouchAllAssets;
	exported.Refresh = UI_Refresh;
	exported.UpdateConnectScreen = UI_UpdateConnectScreen;
	exported.AddToServerList = UI_AddToServerList;
	exported.ShowQuickMenu = UI_ShowQuickMenu;
	exported.HaveQuickMenu = UI_HaveQuickMenu;
	exported.ForceMenuOff = UI_ForceMenuOff;
	exported.Keyup = UI_Keyup;
	exported.Keydown = UI_Keydown;
	exported.MouseSet = UI_MouseSet;
	exported.MouseMove = UI_MouseMove;
	exported.CharEvent = UI_CharEvent;
	exported.TouchEvent = UI_TouchEvent;
	exported.IsTouchDown = UI_IsTouchDown;
	exported.CancelTouches = UI_CancelTouches;

	api_local = *import;
	api = &api_local;

	return &exported;
}

#ifndef UI_HARD_LINKED
#include <stdarg.h>

// this is only here so the functions in q_shared.c and q_math.c can link
void Sys_Error( const char *format, ... ) {
	va_list argptr;
	char msg[3072];

	va_start( argptr, format );
	Q_vsnprintfz( msg, sizeof( msg ), format, argptr );
	va_end( argptr );

	api->Error( msg );
}

void Com_Printf( const char *format, ... ) {
	va_list argptr;
	char msg[3072];

	va_start( argptr, format );
	Q_vsnprintfz( msg, sizeof( msg ), format, argptr );
	va_end( argptr );

	api->Print( msg );
}
#endif