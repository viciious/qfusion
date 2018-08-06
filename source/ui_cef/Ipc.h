#ifndef UI_CEF_IPC_H
#define UI_CEF_IPC_H

#include "Allocator.h"
#include "CefStringBuilder.h"

#include "include/cef_v8.h"
#include "include/wrapper/cef_helpers.h"

class WswCefRenderProcessHandler;

class WswCefV8Handler;

class RenderProcessLogger;

class MessagePipe;

class Logger;

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
	static const CefString drawWorldModel;
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

DERIVE_REQUEST_IPC_HELPERS( DrawWorldModelRequest, PendingCallbackRequest::drawWorldModel );

class SimplexMessage: public AllocatorChild {
	const CefString &name;
public:
	SimplexMessage( const CefString &name_, RawAllocator *allocator_ )
		: AllocatorChild( allocator_ ), name( name_ ) {}

	const CefString &Name() const { return name; }

	static const CefString updateScreen;
	static const CefString mouseSet;
	static const CefString gameCommand;
};

/**
 * A sender of a simplex Frontend -> Backend message that runs in the main process.
 */
class SimplexMessageSender {
	MessagePipe *parent;
	const CefString &messageName;
protected:
	inline CefRefPtr<CefProcessMessage> NewProcessMessage() {
		return CefProcessMessage::Create( messageName );
	}

	void SendProcessMessage( CefRefPtr<CefProcessMessage> message );

	void DeleteMessage( SimplexMessage *message );
public:
	const CefString &MessageName() const { return messageName; }

	/**
	 * The name tells explicitly that the sender acquires an ownership of the message.
	 * Don't try accessing the message in a caller code after this call.
	 * Both NewPooledObject() and default-heap allocations are supported.
	 */
	virtual void AcquireAndSend( SimplexMessage *message ) = 0;

	SimplexMessageSender( MessagePipe *parent_, const CefString &messageName_ )
		: parent( parent_ ), messageName( messageName_ ) {}
};

/**
 * A handler of a simplex Frontend -> Backend message that runs in the UI process.
 */
class SimplexMessageHandler {
protected:
	WswCefV8Handler *parent;
	const CefString &messageName;
	std::string logTag;
	SimplexMessageHandler *next;

	// Note: We do not want these methods below accept templates for several reasons.
	// TODO: Split "message deserializer" and "message handler"?

	/**
	 * Creates a simplex message using a raw data read from the process message.
	 * The message is assumed to be allocated via its allocator ().
	 * DeleteSelf() should be called.
	 * Might return null if deserialization has failed.
	 */
	virtual SimplexMessage *DeserializeMessage( CefRefPtr<CefProcessMessage> &message ) = 0;
	/**
	 * Given the message constructed by DeserializeMessage(),
	 * builds the code of the corresponding Javascript call.
	 * Returns false on failure.
	 */
	virtual bool GetCodeToCall( const SimplexMessage *message, CefStringBuilder &sb ) = 0;

	std::string DescribeException( const CefString &code, CefRefPtr<CefV8Exception> exception );

	inline Logger *Logger();
public:
	explicit SimplexMessageHandler( WswCefV8Handler *parent_, const CefString &messageName_ );

	const CefString &MessageName() { return messageName; }
	SimplexMessageHandler *Next() { return next; };

	void Handle( CefRefPtr<CefBrowser> &browser, CefRefPtr<CefProcessMessage> &message );
};

/**
 * Defines an interface that has two implementations
 * (an optimized one is used for the happy frequently used path)
 */
class GameCommandMessage: public SimplexMessage {
public:
	explicit GameCommandMessage( RawAllocator *allocator_ )
		: SimplexMessage( SimplexMessage::gameCommand, allocator_ ) {}

	virtual int GetNumArgs() const = 0;

	/**
	 * @note This signature does not enforce efficient usage patterns (reusing)
	 * but there were some troubles tied to CefString behaviour otherwise.
	 */
	virtual CefString GetArg( int argNum ) const = 0;
};

/**
 * Takes an ownership over its arguments and keeps it during the entire lifecycle.
 * Should be used for keeping game command messages while the message pipe is not ready.
 */
class DeferredGameCommandMessage: public GameCommandMessage {
public:
	typedef std::vector<std::string> ArgsList;

	ArgsList args;

	DeferredGameCommandMessage( ArgsList &&args_, RawAllocator *allocator_ )
		: GameCommandMessage( allocator_ ), args( args_ ) {}

	int GetNumArgs() const override {
		return (int)args.size();
	}

	CefString GetArg( int argNum ) const override {
		return CefString( args[argNum] );
	}
};

/**
 * A proxy implementation of the game command interface
 * that redirects its calls to underlying data managed by some external code.
 */
class ProxyingGameCommandMessage: public GameCommandMessage {
public:
	typedef std::function<CefString(int)> ArgGetter;
private:
	const int numArgs;
	ArgGetter argGetter;
public:
	ProxyingGameCommandMessage( int numArgs_, ArgGetter &&argGetter_, RawAllocator *allocator_ )
		: GameCommandMessage( allocator_ ), numArgs( numArgs_ ), argGetter( argGetter_ ) {}

	int GetNumArgs() const override { return (int)numArgs; }

	CefString GetArg( int argNum ) const override {
		return argGetter( argNum );
	}

	static ProxyingGameCommandMessage *NewPooledObject( int numArgs_, ArgGetter &&argGetter_ );
};

class MouseSetMessage: public SimplexMessage {
public:
	int context;
	int mx, my;
	bool showCursor;

	MouseSetMessage( int context_, int mx_, int my_, bool showCursor_, RawAllocator *allocator_ )
		: SimplexMessage( SimplexMessage::mouseSet, allocator_ )
		, context( context_ )
		, mx( mx_ ), my( my_ )
		, showCursor( showCursor_ ) {
		// Sanity checks, have already helped to spot bugs
		assert( context == 0 || context == 1 );
		assert( mx >= 0 && mx < ( 1 << 16 ) );
		assert( my >= 0 && my < ( 1 << 16 ) );
	}

	static MouseSetMessage *NewPooledObject( int context_, int mx_, int my_, bool showCursor_ );
};

struct MainScreenState;
struct ConnectionState;
struct DemoPlaybackState;

class UpdateScreenMessage: public SimplexMessage {
	/**
	 * @note in order to save allocations every frame, fields of a reused object are just cleaned up
	 */
	void OnBeforeAllocatorFreeCall() override;
public:
	/**
	 * We do not want fusing MainScreenState and UpdateScreenMessage even if it's possible.
	 */
	MainScreenState *screenState { nullptr };

	explicit UpdateScreenMessage( RawAllocator *allocator_ )
		: SimplexMessage( SimplexMessage::updateScreen, allocator_ ) {}

	static UpdateScreenMessage *NewPooledObject( MainScreenState *screenState );
};

#define DERIVE_MESSAGE_SENDER( Derived, messageName )              \
class Derived##Sender: public SimplexMessageSender {               \
public:                                                            \
	explicit Derived##Sender( MessagePipe *parent_ )               \
		: SimplexMessageSender( parent_, messageName ) {}          \
	void AcquireAndSend( SimplexMessage *message ) override;       \
}

#define DERIVE_MESSAGE_HANDLER( Derived, messageName )                                        \
class Derived##Handler: public SimplexMessageHandler {                                        \
	bool GetCodeToCall( const SimplexMessage *message, CefStringBuilder &sb ) override;       \
	SimplexMessage *DeserializeMessage( CefRefPtr<CefProcessMessage> &message ) override;     \
public:                                                                                       \
	explicit Derived##Handler( WswCefV8Handler *parent_ )                                     \
		: SimplexMessageHandler( parent_, messageName ) {}                                    \
}

class UpdateScreenSender: public SimplexMessageSender {
	MainScreenState *prevState;
	bool forceUpdate { true };
	void SaveStateAndRelease( UpdateScreenMessage *message );
public:
	explicit UpdateScreenSender( MessagePipe *parent_ );
	~UpdateScreenSender();

	void AcquireAndSend( SimplexMessage *message ) override;
};

class UpdateScreenHandler: public SimplexMessageHandler {
	/**
	 * These fields are not transmitted with every message.
	 * Look at MainScreenState for encoding details
	 */
	std::string demoName;
	std::string serverName;
	std::string rejectMessage;
	std::string downloadFileName;

	bool GetCodeToCall( const SimplexMessage *message, CefStringBuilder &sb ) override;
	SimplexMessage *DeserializeMessage( CefRefPtr<CefProcessMessage> &message ) override;
public:
	explicit UpdateScreenHandler( WswCefV8Handler *parent_ )
		: SimplexMessageHandler( parent_, SimplexMessage::updateScreen ) {}
};

DERIVE_MESSAGE_SENDER( MouseSet, SimplexMessage::mouseSet );
DERIVE_MESSAGE_HANDLER( MouseSet, SimplexMessage::mouseSet );

DERIVE_MESSAGE_SENDER( GameCommand, SimplexMessage::gameCommand );
DERIVE_MESSAGE_HANDLER( GameCommand, SimplexMessage::gameCommand );

#endif
