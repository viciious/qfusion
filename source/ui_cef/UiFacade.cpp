#include "UiFacade.h"
#include "CefApp.h"
#include "Api.h"

UiFacade *UiFacade::Create( int argc, char **argv, void *hInstance, int width_, int height_,
							int demoProtocol_, const char *demoExtension_, const char *basePath_ ) {
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

	if( !CefInitialize( mainArgs, settings, app, nullptr ) ) {
		// TODO: Report failure...
		printf("Cef initialize failed\n");
		return nullptr;
	}

	try {
		return new UiFacade( width_, height_,demoProtocol_, demoExtension_, basePath_ );
	} catch(...) {
		CefShutdown();
		throw;
	}
}

UiFacade::~UiFacade() {
	CefShutdown();
}

void UiFacade::Refresh( int64_t time, int clientState, int serverState,
						bool demoPlaying, const char *demoName, bool demoPaused,
						unsigned int demoTime, bool backGround, bool showCursor ) {
	// TODO: Check whether there's a delta to send

	CefDoMessageLoopWork();

	if( !globalRenderHandler ) {
		return;
	}

	auto *uiImage = api->R_RegisterRawPic( "uiBuffer", 1024, 768, globalRenderHandler->drawnEveryFrameBuffer, 4 );
	vec4_t color = { 1.0f, 1.0f, 1.0f, 1.0f };
	api->R_DrawStretchPic( 0, 0, 1024, 768, 0, 0, 1, 1, color, uiImage );
}

void UiFacade::UpdateConnectScreen( const char *serverName, const char *rejectMessage,
									int downloadType, const char *downloadFilename,
									float downloadPercent, int downloadSpeed,
									int connectCount, bool backGround ) {
	// Check whether there's a delta to send
}