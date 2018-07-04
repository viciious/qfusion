#include "CefClient.h"

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

bool WswCefClient::OnProcessMessageReceived( CefRefPtr<CefBrowser> browser,
											 CefProcessId source_process,
											 CefRefPtr<CefProcessMessage> processMessage ) {
	CEF_REQUIRE_UI_THREAD();

	auto name( processMessage->GetName() );
	auto messageArgs( processMessage->GetArgumentList() );
	if( !name.compare( "log" ) ) {
		std::string message( messageArgs->GetString( 0 ).ToString() );
		//cef_log_severity_t severity = (cef_log_severity_t)messageArgs->GetInt( 1 );
		// TODO: Use the severity for console character codes output
		printf("Log message from browser process: \n%s\n", message.c_str() );
		return true;
	}

	for( CallbackRequestHandler *handler = requestHandlersHead; handler; handler = handler->Next() ) {
		if( !handler->Method().compare( name ) ) {
			printf( "Found a handler %s for a request\n", handler->LogTag().c_str() );
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