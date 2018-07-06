#ifndef UI_CEF_IPC_H
#define UI_CEF_IPC_H

#include "CefStringBuilder.h"

#include "include/cef_v8.h"
#include "include/wrapper/cef_helpers.h"

class WswCefRenderProcessHandler;

class WswCefV8Handler;

class RenderProcessLogger;

class PendingCallbackRequest {
	WswCefV8Handler *const parent;
protected:
	const int id;
	CefRefPtr<CefV8Context> context;
	CefRefPtr<CefV8Value> callback;
	const CefString &method;

	inline RenderProcessLogger *Logger();

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
	inline void FireSingleArgAggregateCallback( CefRefPtr<CefListValue> args, AggregateBuildHelper &abh ) {
		CefStringBuilder &stringBuilder = abh.PrintArgs( args, 1, args->GetSize() - 1 );
		ExecuteCallback( { CefV8Value::CreateString( stringBuilder.ReleaseOwnership() ) } );
	}

	void ReportNumArgsMismatch( size_t actual, const char *expected );
	void ExecuteCallback( const CefV8ValueList &args );
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

	inline bool TryGetString( const CefRefPtr<CefV8Value> &jsValue,
							  const char *tag,
							  CefString &value,
							  CefString &exception ) {
		if( !jsValue->IsString() ) {
			exception = std::string( "The value of argument `" ) + tag + "` is not a string";
			return false;
		}

		value = jsValue->GetStringValue();
		return true;
	}

	inline bool ValidateCallback( const CefRefPtr<CefV8Value> &jsValue, CefString &exception ) {
		if( !jsValue->IsFunction() ) {
			exception = "The value of the last argument that is supposed to be a callback is not a function";
			return false;
		}

		return true;
	}

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
										CefString &exception ) {
		if( jsArgs.size() != 1 ) {
			exception = "Illegal arguments list size, there must be a single argument";
			return;
		}

		if( !ValidateCallback( jsArgs.back(), exception ) ) {
			return;
		}

		auto context( CefV8Context::GetCurrentContext() );
		auto request( NewRequest( context, jsArgs.back() ) );
		auto message( NewMessage() );
		message->GetArgumentList()->SetInt( 0, request->Id() );

		Commit( std::move( request ), context, message, retVal, exception );
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

	static const CefString updateScreen;
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

class UpdateScreenHandler: public ExecutingJSMessageHandler {
	// Hide strings delta transmission from JS code
	CefString demoName;
	CefString serverName;
	CefString rejectMessage;
	CefString downloadFilename;

	bool GetCodeToCall( CefRefPtr<CefProcessMessage> &message, CefStringBuilder &sb ) override;
public:
	explicit UpdateScreenHandler( WswCefV8Handler *parent_ )
		: ExecutingJSMessageHandler( parent_, ExecutingJSMessageHandler::updateScreen ) {}
};

DERIVE_MESSAGE_HANDLER( MouseSetHandler, ExecutingJSMessageHandler::mouseSet );
DERIVE_MESSAGE_HANDLER( GameCommandHandler, ExecutingJSMessageHandler::gameCommand );

#endif
