#include "CefClient.h"

void WswCefRenderHandler::OnPaint( CefRefPtr<CefBrowser> browser,
								   CefRenderHandler::PaintElementType type,
								   const CefRenderHandler::RectList &dirtyRects,
								   const void *buffer, int width_, int height_ ) {
	assert( width_ == this->width );
	assert( height_ == this->height );
	// TODO: If the total amount of pixel data is small, copy piecewise
	// TODO: I'm unsure if this would be faster due to non-optimal cache access patterns
	memcpy( browserRenderedBuffer, buffer, (size_t)( width * height * NumColorComponents() ) );
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