#ifndef QFUSION_WSWCEFAPP_H
#define QFUSION_WSWCEFAPP_H

#include "include/cef_app.h"
#include "include/cef_client.h"

inline CefString AsCefString( const char *ascii ) {
	CefString cs;
	if( !cs.FromASCII( ascii ) ) {
		abort();
	}
	return cs;
}

template <typename T>
inline CefRefPtr<T> AsCefPtr( T *value ) {
	return CefRefPtr<T>( value );
}

class WswCefApp: public CefApp {
	CefRefPtr<CefBrowserProcessHandler> browserProcessHandler;
	CefRefPtr<CefRenderProcessHandler> renderProcessHandler;

public:
	IMPLEMENT_REFCOUNTING( WswCefApp );

public:
	WswCefApp();

	void OnBeforeCommandLineProcessing( const CefString& process_type, CefRefPtr<CefCommandLine> command_line ) override;
	void OnRegisterCustomSchemes( CefRawPtr<CefSchemeRegistrar> registrar) override;

	CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
		return browserProcessHandler;
	}

	CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
		return renderProcessHandler;
	}
};

class WswCefBrowserProcessHandler: public CefBrowserProcessHandler {
public:
	IMPLEMENT_REFCOUNTING( WswCefBrowserProcessHandler );

	void OnContextInitialized() override;
};

class WswCefRenderProcessHandler: public CefRenderProcessHandler {
public:
	IMPLEMENT_REFCOUNTING( WswCefRenderProcessHandler );
};

class UiFacade;

class WswCefRenderHandler: public CefRenderHandler {
	const int width;
	const int height;

	void FillRect( CefRect &rect ) {
		rect.x = 0;
		rect.y = 0;
		rect.width = width;
		rect.height = height;
	}

public:
	uint8_t *drawnEveryFrameBuffer;

	IMPLEMENT_REFCOUNTING( WswCefRenderHandler );
public:
	WswCefRenderHandler(): width( 1024 ), height( 768 ) {
		drawnEveryFrameBuffer = new uint8_t[width * height * 4];
	}

	~WswCefRenderHandler() override {
		delete drawnEveryFrameBuffer;
	}

	bool GetRootScreenRect( CefRefPtr<CefBrowser> browser, CefRect& rect ) override {
		FillRect( rect );
		return true;
	}

	bool GetViewRect( CefRefPtr<CefBrowser> browser, CefRect& rect ) override {
		FillRect( rect );
		return true;
	}

	void OnPaint( CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList& dirtyRects,
				  const void* buffer, int width, int height ) override;
};

extern WswCefRenderHandler *globalRenderHandler;

class WswCefClient: public CefClient, public CefLifeSpanHandler {
public:
	IMPLEMENT_REFCOUNTING( WswCefClient );
public:
	CefRefPtr<WswCefRenderHandler> renderHandler;

	WswCefClient(): renderHandler( new WswCefRenderHandler ) {
		globalRenderHandler = renderHandler.get();
	}

	CefRefPtr<CefRenderHandler> GetRenderHandler() override {
		return renderHandler;
	}
};

class WswCefV8Handler: public CefV8Handler {
public:
	IMPLEMENT_REFCOUNTING( WswCefV8Handler );
};

#endif
