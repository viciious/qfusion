#include "UiFacade.h"
#include "CefApp.h"
#include "CefClient.h"
#include "Api.h"


UiFacade *UiFacade::instance = nullptr;

bool UiFacade::Init( int argc, char **argv, void *hInstance, int width_, int height_,
					 int demoProtocol_, const char *demoExtension_, const char *basePath_ ) {
	// Create an instance first.
	// It is expected to be present when various CEF initialization callbacks are fired.
	instance = new UiFacade( width_, height_, demoProtocol_, demoExtension_, basePath_ );

	if( !InitCef( argc, argv, hInstance ) ) {
		delete instance;
		return false;
	}

	return true;
}

bool UiFacade::InitCef( int argc, char **argv, void *hInstance ) {
	CefMainArgs mainArgs( argc, argv );

	CefRefPtr<WswCefApp> app( new WswCefApp );

	CefSettings settings;
	CefString( &settings.locale ).FromASCII( "en_US" );
	settings.multi_threaded_message_loop = false;
	settings.external_message_pump = true;
#ifndef PUBLIC_BUILD
	settings.log_severity = api->Cvar_Value( "developer ") ? LOGSEVERITY_VERBOSE : LOGSEVERITY_DEFAULT;
#else
	settings.log_severity = api->Cvar_Value( "developer" ) ? LOGSEVERITY_VERBOSE : LOGSEVERITY_DISABLE;
#endif

	// Hacks! Unfortunately CEF always expects an absolute path
	std::string realPath( api->Cvar_String( "fs_realpath" ) );
	// TODO: Make sure it's arch/platform-compatible!
	CefString( &settings.browser_subprocess_path ).FromASCII( ( realPath + "/ui_cef_process.x86_64" ).c_str() );
	CefString( &settings.resources_dir_path ).FromASCII( ( realPath + "/cef_resources/" ).c_str() );
	CefString( &settings.locales_dir_path ).FromASCII( ( realPath + "/cef_resources/locales/" ).c_str() );
	// TODO settings.cache_path;
	// TODO settings.user_data_path;
	settings.windowless_rendering_enabled = true;

	return CefInitialize( mainArgs, settings, app.get(), nullptr );
}

UiFacade::UiFacade( int width_, int height_, int demoProtocol_, const char *demoExtension_, const char *basePath_ )
	: renderHandler( nullptr )
	, width( width_ )
	, height( height_ )
	, demoProtocol( demoProtocol_ )
	, demoExtension( demoExtension_ )
	, basePath( basePath_ )
	, messagePipe( this ) {
	api->Cmd_AddCommand( "menu_force", &MenuForceHandler );
	api->Cmd_AddCommand( "menu_open", &MenuOpenHandler );
	api->Cmd_AddCommand( "menu_modal", &MenuModalHandler );
	api->Cmd_AddCommand( "menu_close", &MenuCloseHandler );
}

UiFacade::~UiFacade() {
	CefShutdown();

	api->Cmd_RemoveCommand( "menu_force" );
	api->Cmd_RemoveCommand( "menu_open" );
	api->Cmd_RemoveCommand( "menu_modal" );
	api->Cmd_RemoveCommand( "menu_close" );
}

void UiFacade::MenuCommand() {
	messagePipe.ExecuteCommand( api->Cmd_Argc(), api->Cmd_Argv );
}

void UiFacade::RegisterBrowser( CefRefPtr<CefBrowser> browser_ ) {
	this->browser = browser_;
}

void UiFacade::UnregisterBrowser( CefRefPtr<CefBrowser> browser_ ) {
	this->browser = nullptr;
}

void UiFacade::RegisterRenderHandler( CefRenderHandler *handler_ ) {
	assert( handler_ );
	assert( !renderHandler );
	renderHandler = dynamic_cast<WswCefRenderHandler *>( handler_ );
	assert( renderHandler );
}

inline const char *NullToEmpty( const char *s ) {
	return s ? s : "";
}

void UiFacade::Refresh( int64_t time, int clientState, int serverState,
						bool demoPlaying, const char *demoName, bool demoPaused,
						unsigned int demoTime, bool backGround, bool showCursor ) {
	CefDoMessageLoopWork();

	// If there was no prior UpdateConnectScreen() call this frame, flip
	// (get a free storage for data written this frame),
	// otherwise continue writing to storage reserved in UpdateConnectScreen()
	if( !hasFlippedStateThisFrame ) {
		interleavedStateStorage.Flip();
	}

	auto &mainState = interleavedStateStorage.Curr().mainState;

	mainState.clientState = clientState;
	mainState.serverState = serverState;
	mainState.background = backGround;
	mainState.showCursor = showCursor;

	mainState.demoPlaybackState = nullptr;
	if( demoPlaying ) {
		auto *dps = mainState.demoPlaybackState = &interleavedStateStorage.Curr().demoPlaybackState;
		dps->demoName = demoName;
		dps->time = demoTime;
		dps->paused = demoPaused;
	}

	if( !hasFlippedStateThisFrame ) {
		mainState.connectionState = nullptr;
	} else {
		mainState.connectionState = &interleavedStateStorage.Curr().connectionState;
	}

	hasFlippedStateThisFrame = false;

	messagePipe.UpdateScreenState( interleavedStateStorage.Prev().mainState, mainState );

	DrawUi();
}

void UiFacade::UpdateConnectScreen( const char *serverName, const char *rejectMessage,
									int downloadType, const char *downloadFilename,
									float downloadPercent, int downloadSpeed,
									int connectCount, bool backGround ) {
	CefDoMessageLoopWork();

	// Prepare free storage for this frame updates
	interleavedStateStorage.Flip();
	// Don't flip again this frame in Refresh()
	hasFlippedStateThisFrame = true;

	auto &state = interleavedStateStorage.Curr().connectionState;
	state.serverName = NullToEmpty( serverName );
	state.rejectMessage = NullToEmpty( rejectMessage );
	state.downloadType = downloadType;
	state.downloadFileName = NullToEmpty( downloadFilename );
	state.downloadPercent = downloadPercent;
	state.connectCount = connectCount;

	DrawUi();
}

void UiFacade::DrawUi() {
	auto *uiImage = api->R_RegisterRawPic( "uiBuffer", width, height, renderHandler->drawnEveryFrameBuffer, 4 );
	vec4_t color = { 1.0f, 1.0f, 1.0f, 1.0f };
	api->R_DrawStretchPic( 0, 0, width, height, 0.0f, 0.0f, 1.0f, 1.0f, color, uiImage );
}
