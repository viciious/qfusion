#ifndef UI_CEF_BROWSER_PROCESS_HANDLER_H
#define UI_CEF_BROWSER_PROCESS_HANDLER_H

#include "include/cef_browser_process_handler.h"

class WswCefBrowserProcessHandler: public CefBrowserProcessHandler {
	int width;
	int height;
public:
	WswCefBrowserProcessHandler( int width_, int height_ )
		: width( width_ ), height( height_ ) {}

	void OnContextInitialized() override;

	IMPLEMENT_REFCOUNTING( WswCefBrowserProcessHandler );
};

#endif
