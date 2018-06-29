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


class WswCefRenderProcessHandler;

class RenderProcessLogger {
	CefRefPtr<CefBrowser> browser;

	virtual void SendLogMessage( cef_log_severity_t severity, const char *format, va_list va );
public:
	explicit RenderProcessLogger( CefRefPtr<CefBrowser> browser_ ): browser( browser_ ) {}

#ifndef _MSC_VER
	void Debug( const char *format, ... ) __attribute__( ( format( printf, 2, 3 ) ) );
	void Info( const char *format, ... ) __attribute__( ( format( printf, 2, 3 ) ) );
	void Warning( const char *format, ... ) __attribute__( ( format( printf, 2, 3 ) ) );
	void Error( const char *format, ... ) __attribute__( ( format( printf, 2, 3 ) ) );
#else
	void Debug( _Printf_format_string_ const char *format, ... );
	void Info( _Printf_format_string_ const char *format, ... );
	void Warning( _Printf_format_string_ const char *format, ... );
	void Error( _Printf_format_string_ const char *format, ... );
#endif

	bool UsesBrowser( const CefRefPtr<CefBrowser> &browser_ ) const {
		return this->browser->IsSame( browser_ );
	}
};

class WswCefV8Handler;

class PendingCallbackRequest {
	WswCefV8Handler *const parent;
protected:
	const int id;
	CefRefPtr<CefV8Context> context;
	CefRefPtr<CefV8Value> callback;
	const CefString &method;

	inline RenderProcessLogger *Logger();

	inline void ReportNumArgsMismatch( size_t actual, const char *expected );
	inline void ExecuteCallback( const CefV8ValueList &args );
public:
	PendingCallbackRequest( WswCefV8Handler *parent_,
							CefRefPtr<CefV8Context> context_,
							CefRefPtr<CefV8Value> callback_,
							const CefString &method_ );

	int Id() const { return id; }

	virtual void FireCallback( CefRefPtr<CefProcessMessage> reply ) = 0;

	static const CefString getCVar;
	static const CefString setCVar;
	static const CefString executeCmd;
	static const CefString getVideoModes;
	static const CefString getDemosAndSubDirs;
	static const CefString getDemoMetaData;
};

class PendingRequestLauncher {
protected:
	WswCefV8Handler *const parent;
	const CefString &method;
	const std::string logTag;
	PendingRequestLauncher *next;

	inline RenderProcessLogger *Logger();

	inline bool TryGetString( const CefRefPtr<CefV8Value> &jsValue, const char *tag, CefString &value, CefString &ex );
	inline bool ValidateCallback( const CefRefPtr<CefV8Value> &jsValue, CefString &exception );

	CefRefPtr<CefProcessMessage> NewMessage() {
		return CefProcessMessage::Create( method );
	}

	void Commit( std::shared_ptr<PendingCallbackRequest> request,
				 const CefRefPtr<CefV8Context> &context,
				 CefRefPtr<CefProcessMessage> message,
				 CefRefPtr<CefV8Value> &retVal,
				 CefString &exception );
public:
	explicit PendingRequestLauncher( WswCefV8Handler *parent_, const CefString &method_ );

	const CefString &Method() const { return method; }
	PendingRequestLauncher *Next() { return next; }
	const std::string &LogTag() const { return logTag; }

	virtual void StartExec( const CefV8ValueList &jsArgs, CefRefPtr<CefV8Value> &retVal, CefString &exception ) = 0;
};

template <typename Request>
class TypedPendingRequestLauncher: public PendingRequestLauncher {
protected:
	inline std::shared_ptr<Request> NewRequest( CefRefPtr<CefV8Context> context, CefRefPtr<CefV8Value> callback ) {
		return std::make_shared<Request>( parent, context, callback );
	}

public:
	TypedPendingRequestLauncher( WswCefV8Handler *parent_, const CefString &method_ )
		: PendingRequestLauncher( parent_, method_ ) {}
};

class WswCefClient;

class CallbackRequestHandler {
protected:
	WswCefClient *const parent;
	const CefString &method;
	const std::string logTag;
	CallbackRequestHandler *next;

	CefRefPtr<CefProcessMessage> NewMessage() {
		return CefProcessMessage::Create( method );
	}
public:
	CallbackRequestHandler( WswCefClient *parent_, const CefString &method_ );

	CallbackRequestHandler *Next() { return next; }
	const CefString &Method() { return method; }
	const std::string &LogTag() const { return logTag; }

	virtual void ReplyToRequest( CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> ingoing ) = 0;
};

#define DERIVE_PENDING_CALLBACK_REQUEST( Derived, method )                                                           \
class Derived: public PendingCallbackRequest {                                                                       \
public:																												 \
	Derived( WswCefV8Handler *parent_, CefRefPtr<CefV8Context> context_, CefRefPtr<CefV8Value> callback_ )           \
		: PendingCallbackRequest( parent_, context_, callback_, method ) {}                                          \
	void FireCallback( CefRefPtr<CefProcessMessage> reply ) override;                                                \
};                                                                                                                   \
																													 \
class Derived##Launcher: public TypedPendingRequestLauncher<Derived> {                                               \
public:                                                                                                              \
	explicit Derived##Launcher( WswCefV8Handler *parent_ ): TypedPendingRequestLauncher( parent_, method ) {}        \
	void StartExec( const CefV8ValueList &jsArgs, CefRefPtr<CefV8Value> &retVal, CefString &exception ) override;    \
};																													 \
																													 \
class Derived##Handler: public CallbackRequestHandler {																 \
public:																												 \
	explicit Derived##Handler( WswCefClient *parent_ ): CallbackRequestHandler( parent_, method ) {}				 \
	void ReplyToRequest( CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> ingoing ) override;             \
}

DERIVE_PENDING_CALLBACK_REQUEST( GetCVarRequest, PendingCallbackRequest::getCVar );

DERIVE_PENDING_CALLBACK_REQUEST( SetCVarRequest, PendingCallbackRequest::setCVar );

DERIVE_PENDING_CALLBACK_REQUEST( ExecuteCmdRequest, PendingCallbackRequest::executeCmd );

DERIVE_PENDING_CALLBACK_REQUEST( GetVideoModesRequest, PendingCallbackRequest::getVideoModes );

DERIVE_PENDING_CALLBACK_REQUEST( GetDemosAndSubDirsRequest, PendingCallbackRequest::getDemosAndSubDirs );

DERIVE_PENDING_CALLBACK_REQUEST( GetDemoMetaDataRequest, PendingCallbackRequest::getDemoMetaData );

class WswCefV8Handler: public CefV8Handler {
	friend class PendingCallbackRequest;
	friend class PendingRequestLauncher;
	friend class WswCefRenderProcessHandler;

	WswCefRenderProcessHandler *renderProcessHandler;

	PendingRequestLauncher *requestLaunchersHead;

	GetCVarRequestLauncher getCVar;
	SetCVarRequestLauncher setCVar;
	ExecuteCmdRequestLauncher executeCmd;
	GetVideoModesRequestLauncher getVideoModes;
	GetDemosAndSubDirsRequestLauncher getDemosAndSubDirs;
	GetDemoMetaDataRequestLauncher getDemoMetaData;

	std::unordered_map<int, std::shared_ptr<PendingCallbackRequest>> callbacks;
	// We use an unsigned counter to ensure that the overflow behaviour is defined
	unsigned callId;

	inline int NextCallId() { return (int)( callId++ ); }

	void ProcessAsAwaitedReply( CefRefPtr<CefProcessMessage> &message );

	inline RenderProcessLogger *Logger();
public:
	explicit WswCefV8Handler( WswCefRenderProcessHandler *renderProcessHandler_ )
		: renderProcessHandler( renderProcessHandler_ )
		, requestLaunchersHead( nullptr )
		, getCVar( this )
		, setCVar( this )
		, executeCmd( this )
		, getVideoModes( this )
		, getDemosAndSubDirs( this )
		, getDemoMetaData( this )
		, callId( 0 ) {}

	bool Execute( const CefString& name,
				  CefRefPtr<CefV8Value> object,
				  const CefV8ValueList& arguments,
				  CefRefPtr<CefV8Value>& retval,
				  CefString& exception ) override;

	bool CheckForReply( const CefString &name, CefRefPtr<CefProcessMessage> message );

	IMPLEMENT_REFCOUNTING( WswCefV8Handler );
};

class WswCefRenderProcessHandler: public CefRenderProcessHandler {
	static const char *ClientStateAsParam( int state );
	static const char *ServerStateAsParam( int state );
	static const char *DownloadTypeAsParam( int type );

	CefString MakeGameCommandCall( const CefRefPtr<CefProcessMessage> &message );
	CefString MakeMouseSetCall( const CefRefPtr<CefProcessMessage> &message );
	CefString MakeUpdateMainScreenStateCall( const CefRefPtr<CefProcessMessage> &message );
	CefString MakeUpdateConnectScreenStateCall( const CefRefPtr<CefProcessMessage> &message );

	CefString DescribeException( const std::string &code, CefRefPtr<CefV8Exception> exception );

	bool ExecuteJavascript( CefRefPtr<CefBrowser> browser, const std::string &code );

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

inline RenderProcessLogger* WswCefV8Handler::Logger() {
	return renderProcessHandler->Logger();
}

inline RenderProcessLogger *PendingCallbackRequest::Logger() {
	return parent->Logger();
}

inline RenderProcessLogger *PendingRequestLauncher::Logger() {
	return parent->Logger();
}

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

class WswCefClient;

class WswCefClient: public CefClient, public CefLifeSpanHandler {
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
		, renderHandler( new WswCefRenderHandler ) {
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
