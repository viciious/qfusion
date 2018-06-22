#ifndef QFUSION_WSWCEFAPP_H
#define QFUSION_WSWCEFAPP_H

#include "include/cef_app.h"
#include "include/cef_client.h"
#include "UiFacade.h"

#include <unordered_map>
#include <atomic>

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

class WswCefV8Handler: public CefV8Handler {
	friend class WswCefRenderProcessHandler;

	std::unordered_map<int, std::pair<CefRefPtr<CefV8Context>, CefRefPtr<CefV8Value>>> callbacks;
	// We use an unsigned counter to ensure that the overflow behaviour is defined
	unsigned callId;

	inline int NextCallId() { return (int)( callId++ ); }

	void PostGetCVarRequest( const CefV8ValueList &arguments, CefRefPtr<CefV8Value> &retval, CefString &exception );
	void PostSetCVarRequest( const CefV8ValueList &arguments, CefRefPtr<CefV8Value> &retval, CefString &exception );
	void PostExecuteCmdRequest( const CefV8ValueList &arguments, CefRefPtr<CefV8Value> &retval, CefString &exception );

	inline bool TryGetString( const CefRefPtr<CefV8Value> &jsValue, const char *tag, CefString &value, CefString &ex );
	inline bool ValidateCallback( const CefRefPtr<CefV8Value> &jsValue, CefString &exception );

	void FireGetCVarCallback( CefRefPtr<CefProcessMessage> reply );
	void FireSetCVarCallback( CefRefPtr<CefProcessMessage> reply );
	void FireExecuteCmdCallback( CefRefPtr<CefProcessMessage> reply );

	inline bool TryUnregisterCallback( int id, CefRefPtr<CefV8Context> &context, CefRefPtr<CefV8Value> &callback );
public:
	WswCefV8Handler(): callId( 0 ) {}

	bool Execute( const CefString& name,
				  CefRefPtr<CefV8Value> object,
				  const CefV8ValueList& arguments,
				  CefRefPtr<CefV8Value>& retval,
				  CefString& exception ) override;

	IMPLEMENT_REFCOUNTING( WswCefV8Handler );
};

class WswCefRenderProcessHandler: public CefRenderProcessHandler {
	void SendLogMessage( CefRefPtr<CefBrowser> browser, const std::string &message ) {
		return SendLogMessage( browser, CefString( message ) );
	}
	void SendLogMessage( CefRefPtr<CefBrowser> browser, const char *message ) {
		return SendLogMessage( browser, CefString( message ) );
	}

	void SendLogMessage( CefRefPtr<CefBrowser> browser, const CefString &message );

	std::string MakeGameCommandCall( CefRefPtr<CefProcessMessage> &message );
	std::string DescribeException( const std::string &code, CefRefPtr<CefV8Exception> exception );

	bool ExecuteJavascript( CefRefPtr<CefBrowser> browser, const std::string &code );

	CefRefPtr<WswCefV8Handler> v8Handler;
public:
	IMPLEMENT_REFCOUNTING( WswCefRenderProcessHandler );

	void OnWebKitInitialized() override;

	bool OnProcessMessageReceived( CefRefPtr<CefBrowser> browser,
								   CefProcessId source_process,
								   CefRefPtr<CefProcessMessage> message ) override;
};

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

class WswCefClient: public CefClient, public CefLifeSpanHandler {
public:
	IMPLEMENT_REFCOUNTING( WswCefClient );

	void ReplyForGetCVarRequest( CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> message );
	void ReplyForSetCVarRequest( CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> message );
	void ReplyForExecuteCmdRequest( CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> message );
public:
	CefRefPtr<WswCefRenderHandler> renderHandler;

	WswCefClient() : renderHandler( new WswCefRenderHandler ) {
		UiFacade::Instance()->RegisterRenderHandler( renderHandler.get() );
	}

	CefRefPtr<CefRenderHandler> GetRenderHandler() override {
		return renderHandler;
	}

	CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
		return this;
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
