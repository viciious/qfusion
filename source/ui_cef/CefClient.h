#ifndef UI_CEF_CLIENT_H
#define UI_CEF_CLIENT_H

#include "UiFacade.h"
#include "Ipc.h"

#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_render_handler.h"

class WswCefRenderHandler: public CefRenderHandler {
	uint8_t *browserRenderedBuffer;
	const int width;
	const int height;

	void FillRect( CefRect &rect ) {
		rect.x = 0;
		rect.y = 0;
		rect.width = width;
		rect.height = height;
	}

public:
	WswCefRenderHandler( int width_, int height_ ): width( width_ ), height( height_ ) {
		// Check dimensions for sanity
		assert( width > 0 && width <= ( 1 << 16 ) );
		assert( height > 0 && height <= ( 1 << 16 ) );
		browserRenderedBuffer = new uint8_t[width * height * NumColorComponents()];
	}

	~WswCefRenderHandler() override {
		delete browserRenderedBuffer;
	}

	const uint8_t *BrowserRenderedBuffer() const { return browserRenderedBuffer; }
	int Width() const { return width; }
	int Height() const { return height; }
	int NumColorComponents() const { return 4; }

	bool GetRootScreenRect( CefRefPtr<CefBrowser> browser, CefRect& rect ) override {
		FillRect( rect );
		return true;
	}

	bool GetViewRect( CefRefPtr<CefBrowser> browser, CefRect& rect ) override {
		FillRect( rect );
		return true;
	}

	void OnPaint( CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList& dirtyRects,
				  const void* buffer, int width_, int height_ ) override;

	IMPLEMENT_REFCOUNTING( WswCefRenderHandler );
};


class WswCefClient: public CefClient, public CefLifeSpanHandler, public CefContextMenuHandler {
	friend class CallbackRequestHandler;

	CallbackRequestHandler *requestHandlersHead;

	GetCVarRequestHandler getCVar;
	SetCVarRequestHandler setCVar;
	ExecuteCmdRequestHandler executeCmd;
	GetVideoModesRequestHandler getVideoModes;
	GetDemosAndSubDirsRequestHandler getDemosAndSubDirs;
	GetDemoMetaDataRequestHandler getDemoMetaData;
	GetHudsRequestHandler getHuds;
	GetGametypesRequestHandler getGametypes;
	GetMapsRequestHandler getMaps;
	GetLocalizedStringsRequestHandler getLocalizedStrings;
	GetKeyBindingsRequestHandler getKeyBindings;
	GetKeyNamesRequestHandler getKeyNames;

	CefRefPtr<WswCefRenderHandler> renderHandler;

	inline Logger *Logger() { return UiFacade::Instance()->Logger(); }
public:
	WswCefClient( int width, int height )
		: requestHandlersHead( nullptr )
		, getCVar( this )
		, setCVar( this )
		, executeCmd( this )
		, getVideoModes( this )
		, getDemosAndSubDirs( this )
		, getDemoMetaData( this )
		, getHuds( this )
		, getGametypes( this )
		, getMaps( this )
		, getLocalizedStrings( this )
		, getKeyBindings( this )
		, getKeyNames( this )
		, renderHandler( new WswCefRenderHandler( width, height ) ) {
		UiFacade::Instance()->RegisterRenderHandler( renderHandler.get() );
	}

	CefRefPtr<CefRenderHandler> GetRenderHandler() override {
		return renderHandler;
	}

	CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
		return this;
	}

	CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override {
		return this;
	}

	void OnBeforeContextMenu( CefRefPtr<CefBrowser> browser,
							  CefRefPtr<CefFrame> frame,
							  CefRefPtr<CefContextMenuParams> params,
							  CefRefPtr<CefMenuModel> model ) override {
		// Disable the menu...
		model->Clear();
	}

	void OnAfterCreated( CefRefPtr<CefBrowser> browser ) override {
		UiFacade::Instance()->RegisterBrowser( browser );
	}

	void OnBeforeClose( CefRefPtr<CefBrowser> browser ) override {
		UiFacade::Instance()->UnregisterBrowser( browser );
	}

	bool OnProcessMessageReceived( CefRefPtr<CefBrowser> browser,
								   CefProcessId source_process,
								   CefRefPtr<CefProcessMessage> message ) override;

	IMPLEMENT_REFCOUNTING( WswCefClient );
};

#endif
