#include "CefApp.h"
#include "BrowserProcessHandler.h"
#include "RenderProcessHandler.h"

WswCefApp::WswCefApp( int width, int height )
	: browserProcessHandler( new WswCefBrowserProcessHandler( width, height ) )
	, renderProcessHandler( new WswCefRenderProcessHandler ) {}

void WswCefApp::OnBeforeCommandLineProcessing( const CefString& process_type, CefRefPtr<CefCommandLine> command_line ) {
	command_line->AppendSwitch( "no-proxy-server" );
	command_line->AppendSwitchWithValue( "lang", "en-US" );
}

void WswCefApp::OnRegisterCustomSchemes( CefRawPtr<CefSchemeRegistrar> registrar ) {
	registrar->AddCustomScheme( "ui", false, false, false, true, true, true );
}