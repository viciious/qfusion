#ifndef UI_CEF_RENDER_PROCESS_HANDLER_H
#define UI_CEF_RENDER_PROCESS_HANDLER_H

#include "Logger.h"
#include "V8Handler.h"

#include "include/cef_render_process_handler.h"

class WswCefRenderProcessHandler: public CefRenderProcessHandler {
	CefRefPtr<WswCefV8Handler> v8Handler;
	std::shared_ptr<RenderProcessLogger> logger;
public:
	void OnWebKitInitialized() override;

	void OnBrowserCreated( CefRefPtr<CefBrowser> browser ) override;
	void OnBrowserDestroyed( CefRefPtr<CefBrowser> browser ) override;

	bool OnProcessMessageReceived( CefRefPtr<CefBrowser> browser,
								   CefProcessId source_process,
								   CefRefPtr<CefProcessMessage> message ) override;

	RenderProcessLogger *Logger() { return logger.get(); };

	IMPLEMENT_REFCOUNTING( WswCefRenderProcessHandler );
};

#endif
