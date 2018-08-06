#include "CefClient.h"
#include "UiFacade.h"

bool WswCefDisplayHandler::OnConsoleMessage( CefRefPtr<CefBrowser> browser,
											 const CefString &message,
											 const CefString &source,
											 int line ) {
	parent->Logger()->Info( "[JS-CON.LOG] %s:%d: `%s`\n", source.ToString().c_str(), line, message.ToString().c_str() );
	return true;
}

bool WswCefClient::OnProcessMessageReceived( CefRefPtr<CefBrowser> browser,
											 CefProcessId source_process,
											 CefRefPtr<CefProcessMessage> processMessage ) {
	CEF_REQUIRE_UI_THREAD();

	auto name( processMessage->GetName() );
	auto messageArgs( processMessage->GetArgumentList() );
	if( !name.compare( "log" ) ) {
		std::string message( messageArgs->GetString( 0 ).ToString() );
		const char *format = "[UI-PROCESS]: %s";
		switch( (cef_log_severity_t)messageArgs->GetInt( 1 ) ) {
			case LOGSEVERITY_WARNING:
				Logger()->Warning( format, message.c_str() );
				break;
			case LOGSEVERITY_ERROR:
				Logger()->Error( format, message.c_str() );
				break;
			default:
				Logger()->Info( format, message.c_str() );
		}
		return true;
	}

	for( CallbackRequestHandler *handler = requestHandlersHead; handler; handler = handler->Next() ) {
		if( !handler->Method().compare( name ) ) {
			Logger()->Debug( "Found a handler %s for a request\n", handler->LogTag().c_str() );
			handler->ReplyToRequest( browser, processMessage );
			return true;
		}
	}

	if( !name.compare( "uiPageReady" ) ) {
		UiFacade::Instance()->OnUiPageReady();
		return true;
	}

	return false;
}