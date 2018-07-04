#ifndef QFUSION_WSWCEFAPP_H
#define QFUSION_WSWCEFAPP_H

#include "include/cef_app.h"
#include "include/cef_client.h"
#include "UiFacade.h"
#include "CefStringBuilder.h"

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

	template<typename BuildHelper>
	void FireSingleArgAggregateCallback( CefRefPtr<CefProcessMessage> &reply ) {
		CefStringBuilder stringBuilder;
		BuildHelper buildHelper( stringBuilder );
		FireSingleArgAggregateCallback( reply->GetArgumentList(), buildHelper );
	}

	template<typename BuildHelper, typename HelperArg1>
	void FireSingleArgAggregateCallback( CefRefPtr<CefProcessMessage> &reply, const HelperArg1 &arg1 ) {
		CefStringBuilder stringBuilder;
		BuildHelper buildHelper( stringBuilder, arg1 );
		FireSingleArgAggregateCallback( reply->GetArgumentList(), buildHelper );
	};

	template <typename BuildHelper, typename HelperArg1, typename HelperArg2>
	void FireSingleArgAggregateCallback( CefRefPtr<CefProcessMessage> &reply,
										 const HelperArg1 &arg1,
										 const HelperArg2 arg2 ) {
		CefStringBuilder stringBuilder;
		BuildHelper buildHelper( stringBuilder, arg1, arg2 );
		FireSingleArgAggregateCallback( reply->GetArgumentList(), buildHelper );
	};

	template <typename BuildHelper, typename HelperArg1, typename HelperArg2, typename HelperArg3>
	void FireSingleArgAggregateCallback( CefRefPtr<CefProcessMessage> &reply,
										 const HelperArg1 &arg1,
										 const HelperArg2 &arg2,
										 const HelperArg3 &arg3 ) {
		CefStringBuilder stringBuilder;
		BuildHelper buildHelper( stringBuilder, arg1, arg2, arg3 );
		FireSingleArgAggregateCallback( reply->GetArgumentList(), buildHelper );
	};

	// The first parameter differs from template ones intentionally to avoid ambiguous calls
	inline void FireSingleArgAggregateCallback( CefRefPtr<CefListValue> args, AggregateBuildHelper &abh );
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
	static const CefString getHuds;
	static const CefString getGametypes;
	static const CefString getMaps;
	static const CefString getLocalizedStrings;
	static const CefString getKeyBindings;
	static const CefString getKeyNames;
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

	void DefaultSingleArgStartExecImpl( const CefV8ValueList &jsArgs,
										CefRefPtr<CefV8Value> &retVal,
										CefString &exception );
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
}                                                                                                                    \

#define DERIVE_PENDING_REQUEST_LAUNCHER( Derived, method )                                                           \
class Derived##Launcher: public virtual TypedPendingRequestLauncher<Derived> {                                       \
public:                                                                                                              \
	explicit Derived##Launcher( WswCefV8Handler *parent_ ): TypedPendingRequestLauncher( parent_, method ) {}        \
	void StartExec( const CefV8ValueList &jsArgs, CefRefPtr<CefV8Value> &retVal, CefString &exception ) override;    \
}																													 \

#define DERIVE_CALLBACK_REQUEST_HANDLER( Derived, method )															 \
class Derived##Handler: public CallbackRequestHandler {																 \
public:																												 \
	explicit Derived##Handler( WswCefClient *parent_ ): CallbackRequestHandler( parent_, method ) {}				 \
	void ReplyToRequest( CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> ingoing ) override;             \
}

#define  DERIVE_REQUEST_IPC_HELPERS( Derived, method )    \
	DERIVE_PENDING_CALLBACK_REQUEST( Derived, method );  \
	DERIVE_PENDING_REQUEST_LAUNCHER( Derived, method );  \
	DERIVE_CALLBACK_REQUEST_HANDLER( Derived, method )

DERIVE_REQUEST_IPC_HELPERS( GetCVarRequest, PendingCallbackRequest::getCVar );
DERIVE_REQUEST_IPC_HELPERS( SetCVarRequest, PendingCallbackRequest::setCVar );
DERIVE_REQUEST_IPC_HELPERS( ExecuteCmdRequest, PendingCallbackRequest::executeCmd );
DERIVE_REQUEST_IPC_HELPERS( GetVideoModesRequest, PendingCallbackRequest::getVideoModes );
DERIVE_REQUEST_IPC_HELPERS( GetDemosAndSubDirsRequest, PendingCallbackRequest::getDemosAndSubDirs );
DERIVE_REQUEST_IPC_HELPERS( GetDemoMetaDataRequest, PendingCallbackRequest::getDemoMetaData );
DERIVE_REQUEST_IPC_HELPERS( GetHudsRequest, PendingCallbackRequest::getHuds );
DERIVE_REQUEST_IPC_HELPERS( GetGametypesRequest, PendingCallbackRequest::getGametypes );
DERIVE_REQUEST_IPC_HELPERS( GetMapsRequest, PendingCallbackRequest::getMaps );
DERIVE_REQUEST_IPC_HELPERS( GetLocalizedStringsRequest, PendingCallbackRequest::getLocalizedStrings );

template <typename Request>
class RequestForKeysLauncher: public TypedPendingRequestLauncher<Request> {
public:
	RequestForKeysLauncher( WswCefV8Handler *parent_, const CefString &method_ )
		: TypedPendingRequestLauncher<Request>( parent_, method_ ) {}

	void StartExec( const CefV8ValueList &jsArgs, CefRefPtr<CefV8Value> &retVal, CefString &exception ) override;
};

class RequestForKeysHandler: public CallbackRequestHandler {
	virtual const char *GetForKey( int key ) = 0;
public:
	RequestForKeysHandler( WswCefClient *parent_, const CefString &method_ )
		: CallbackRequestHandler( parent_, method_ ) {}

	void ReplyToRequest( CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> ingoing ) override;
};

#define DERIVE_REQUEST_FOR_KEYS_LAUNCHER( Request, method )             \
class Request##Launcher: public RequestForKeysLauncher<Request> {       \
public:                                                                 \
	explicit Request##Launcher( WswCefV8Handler *parent_ )              \
		: RequestForKeysLauncher<Request>( parent_, method ) {}         \
}

#define DERIVE_REQUEST_FOR_KEYS_HANDLER( Request, method )       \
class Request##Handler: public RequestForKeysHandler {           \
	const char *GetForKey( int key ) override;                   \
public:                                                          \
	explicit Request##Handler( WswCefClient *parent_ )           \
		: RequestForKeysHandler( parent_, method ) {}            \
}

DERIVE_PENDING_CALLBACK_REQUEST( GetKeyBindingsRequest, PendingCallbackRequest::getKeyBindings );
DERIVE_REQUEST_FOR_KEYS_LAUNCHER( GetKeyBindingsRequest, PendingCallbackRequest::getKeyBindings );
DERIVE_REQUEST_FOR_KEYS_HANDLER( GetKeyBindingsRequest, PendingCallbackRequest::getKeyBindings );

DERIVE_PENDING_CALLBACK_REQUEST( GetKeyNamesRequest, PendingCallbackRequest::getKeyNames );
DERIVE_REQUEST_FOR_KEYS_LAUNCHER( GetKeyNamesRequest, PendingCallbackRequest::getKeyNames );
DERIVE_REQUEST_FOR_KEYS_HANDLER( GetKeyNamesRequest, PendingCallbackRequest::getKeyNames );

class ExecutingJSMessageHandler {
protected:
	WswCefV8Handler *parent;
	const CefString &messageName;
	std::string logTag;
	ExecutingJSMessageHandler *next;

	virtual bool GetCodeToCall( CefRefPtr<CefProcessMessage> &message, CefStringBuilder &sb ) = 0;

	std::string DescribeException( const CefString &code, CefRefPtr<CefV8Exception> exception );
public:
	explicit ExecutingJSMessageHandler( WswCefV8Handler *parent_, const CefString &messageName_ );

	const CefString &MessageName() { return messageName; }
	ExecutingJSMessageHandler *Next() { return next; };

	void Handle( CefRefPtr<CefBrowser> &browser, CefRefPtr<CefProcessMessage> &message );

	static const CefString updateMainScreen;
	static const CefString updateConnectScreen;
	static const CefString mouseSet;
	static const CefString gameCommand;
};

#define DERIVE_MESSAGE_HANDLER( Derived, messageName )                                               \
class Derived: public ExecutingJSMessageHandler {                                                    \
	bool GetCodeToCall( CefRefPtr<CefProcessMessage> &message, CefStringBuilder &sb ) override;      \
public:                                                                                              \
	explicit Derived( WswCefV8Handler *parent_ )                                                     \
		: ExecutingJSMessageHandler( parent_, messageName ) {}                                       \
};

DERIVE_MESSAGE_HANDLER( UpdateMainScreenHandler, ExecutingJSMessageHandler::updateMainScreen );
DERIVE_MESSAGE_HANDLER( UpdateConnectScreenHandler, ExecutingJSMessageHandler::updateConnectScreen );
DERIVE_MESSAGE_HANDLER( MouseSetHandler, ExecutingJSMessageHandler::mouseSet );
DERIVE_MESSAGE_HANDLER( GameCommandHandler, ExecutingJSMessageHandler::gameCommand );

class WswCefV8Handler: public CefV8Handler {
	friend class PendingCallbackRequest;
	friend class PendingRequestLauncher;
	friend class WswCefRenderProcessHandler;
	friend class ExecutingJSMessageHandler;

	WswCefRenderProcessHandler *renderProcessHandler;

	PendingRequestLauncher *requestLaunchersHead;

	ExecutingJSMessageHandler *messageHandlersHead;

	GetCVarRequestLauncher getCVar;
	SetCVarRequestLauncher setCVar;
	ExecuteCmdRequestLauncher executeCmd;
	GetVideoModesRequestLauncher getVideoModes;
	GetDemosAndSubDirsRequestLauncher getDemosAndSubDirs;
	GetDemoMetaDataRequestLauncher getDemoMetaData;
	GetHudsRequestLauncher getHuds;
	GetGametypesRequestLauncher getGametypes;
	GetMapsRequestLauncher getMaps;
	GetLocalizedStringsRequestLauncher getLocalizedStrings;
	GetKeyBindingsRequestLauncher getKeyBindings;
	GetKeyNamesRequestLauncher getKeyNames;

	GameCommandHandler gameCommandHandler;
	MouseSetHandler mouseSetHandler;
	UpdateConnectScreenHandler updateConnectScreenHandler;
	UpdateMainScreenHandler updateMainScreenHandler;

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
		, messageHandlersHead( nullptr )
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
		, gameCommandHandler( this )
		, mouseSetHandler( this )
		, updateConnectScreenHandler( this )
		, updateMainScreenHandler( this )
		, callId( 0 ) {}

	bool Execute( const CefString& name,
				  CefRefPtr<CefV8Value> object,
				  const CefV8ValueList& arguments,
				  CefRefPtr<CefV8Value>& retval,
				  CefString& exception ) override;

	bool TryHandle( CefRefPtr<CefBrowser> &browser, CefRefPtr<CefProcessMessage> &message );

	IMPLEMENT_REFCOUNTING( WswCefV8Handler );
};

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
