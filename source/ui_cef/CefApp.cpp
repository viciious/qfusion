#include "CefApp.h"
#include "UiFacade.h"
#include "Api.h"

#include "include/wrapper/cef_helpers.h"

WswCefApp::WswCefApp()
	: browserProcessHandler( new WswCefBrowserProcessHandler )
	, renderProcessHandler( new WswCefRenderProcessHandler ) {}

void WswCefApp::OnBeforeCommandLineProcessing( const CefString& process_type, CefRefPtr<CefCommandLine> command_line ) {
	command_line->AppendSwitch( "no-proxy-server" );
	command_line->AppendSwitchWithValue( "lang", "en-US" );
}

void WswCefBrowserProcessHandler::OnContextInitialized() {
	CefWindowInfo info;
	info.SetAsWindowless( 0 );
	CefBrowserSettings settings;
	CefRefPtr<WswCefClient> client( new WswCefClient );
	CefString url( AsCefString( "http://warsow.net/servers" ) );
	CefBrowserHost::CreateBrowser( info, client, url, settings, nullptr );
}


WswCefRenderHandler *globalRenderHandler = nullptr;

void WswCefRenderHandler::OnPaint( CefRefPtr<CefBrowser> browser,
								   CefRenderHandler::PaintElementType type,
								   const CefRenderHandler::RectList &dirtyRects,
								   const void *buffer, int width, int height ) {
	assert( width == this->width );
	assert( height == this->height );
	// TODO: If the total amount of pixel data is small, copy piecewise
	// TODO: I'm unsure if this would be faster due to non-optimal cache access patterns
	memcpy( drawnEveryFrameBuffer, buffer, width * height * 4u );
}