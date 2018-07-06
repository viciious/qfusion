#ifndef UI_CEF_CLIENT_H
#define UI_CEF_CLIENT_H

#include "UiFacade.h"
#include "Ipc.h"

#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_render_handler.h"

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


class WswCefClient: public CefClient, public CefLifeSpanHandler, public CefContextMenuHandler {
	friend class CallbackRequestHandler;
public:
	IMPLEMENT_REFCOUNTING( WswCefClient );

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

public:
	CefRefPtr<WswCefRenderHandler> renderHandler;

	WswCefClient()
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
		, renderHandler( new WswCefRenderHandler ) {
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
};

#endif
