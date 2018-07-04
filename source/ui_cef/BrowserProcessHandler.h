#ifndef UI_CEF_BROWSER_PROCESS_HANDLER_H
#define UI_CEF_BROWSER_PROCESS_HANDLER_H

#include "include/cef_browser_process_handler.h"

class WswCefBrowserProcessHandler: public CefBrowserProcessHandler {
public:
	void OnContextInitialized() override;

	IMPLEMENT_REFCOUNTING( WswCefBrowserProcessHandler );
};

#endif
