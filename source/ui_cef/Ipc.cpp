#include "CefApp.h"
#include "CefClient.h"
#include "Ipc.h"
#include "RenderProcessHandler.h"
#include "V8Handler.h"

inline RenderProcessLogger* WswCefV8Handler::Logger() {
	return renderProcessHandler->Logger();
}

inline RenderProcessLogger *PendingCallbackRequest::Logger() {
	return parent->Logger();
}

const CefString PendingCallbackRequest::getCVar( "getCVar" );
const CefString PendingCallbackRequest::setCVar( "setCVar" );
const CefString PendingCallbackRequest::executeCmd( "executeCmd" );
const CefString PendingCallbackRequest::getVideoModes( "getVideoModes" );
const CefString PendingCallbackRequest::getDemosAndSubDirs( "getDemosAndSubDirs" );
const CefString PendingCallbackRequest::getDemoMetaData( "getDemoMetaData" );
const CefString PendingCallbackRequest::getHuds( "getHuds" );
const CefString PendingCallbackRequest::getGametypes( "getGametypes" );
const CefString PendingCallbackRequest::getMaps( "getMaps" );
const CefString PendingCallbackRequest::getLocalizedStrings( "getLocalizedStrings" );
const CefString PendingCallbackRequest::getKeyBindings( "getKeyBindings" );
const CefString PendingCallbackRequest::getKeyNames( "getKeyNames" );
const CefString PendingCallbackRequest::drawWorldModel( "drawWorldModel" );

PendingCallbackRequest::PendingCallbackRequest( WswCefV8Handler *parent_,
												CefRefPtr<CefV8Context> context_,
												CefRefPtr<CefV8Value> callback_,
												const CefString &method_ )
	: parent( parent_ )
	, id( parent_->NextCallId() )
	, context( context_ )
	, callback( callback_ )
	, method( method_ ) {}

void PendingCallbackRequest::ReportNumArgsMismatch( size_t actual, const char *expected ) {
	std::string tag = "PendingCallbackRequest@" + method.ToString();
	Logger()->Error( "%s: message args size %d does not match expected %s", tag.c_str(), (int)actual, expected );
}

void PendingCallbackRequest::ExecuteCallback( const CefV8ValueList &args ) {
	if( !callback->ExecuteFunctionWithContext( context, nullptr, args ).get() ) {
		std::string tag = "PendingCallbackRequest@" + method.ToString();
		Logger()->Error( "%s: JS callback execution failed", tag.c_str() );
	}
}

PendingRequestLauncher::PendingRequestLauncher( WswCefV8Handler *parent_, const CefString &method_ )
	: parent( parent_ ), method( method_ ), logTag( "PendingRequestLauncher@" + method_.ToString() ) {
	this->next = parent_->requestLaunchersHead;
	parent_->requestLaunchersHead = this;
}

CallbackRequestHandler::CallbackRequestHandler( WswCefClient *parent_, const CefString &method_ )
	: parent( parent_ ), method( method_ ), logTag( "CallbackRequestHandler@" + method_.ToString() ) {
	this->next = parent->requestHandlersHead;
	parent->requestHandlersHead = this;
}

void PendingRequestLauncher::Commit( std::shared_ptr<PendingCallbackRequest> request,
									 const CefRefPtr<CefV8Context> &context,
									 CefRefPtr<CefProcessMessage> message,
									 CefRefPtr<CefV8Value> &retVal,
									 CefString &exception ) {
	const int id = request->Id();

#ifndef PUBLIC_BUILD
	// Sanity checks
	auto messageArgs( message->GetArgumentList() );
	if( messageArgs->GetSize() < 1 ) {
		const char *format = "%s: Commit(): Sanity check failed: An outgoing message %s has an empty args list";
		Logger()->Error( format, logTag.c_str(), message->GetName().ToString().c_str() );
	}
	if( messageArgs->GetInt( 0 ) != id ) {
		const char *format = "%s: Sanity check failed: A first argument of an outgoing message %s is not a callback id";
		Logger()->Error( format, logTag.c_str(), message->GetName().ToString().c_str() );
	}
#endif

	// Both of these calls might theoretically (but very unlikely) fail.
	// Start from that one that is easy to rollback.
	// Note: we can be sure that the reply cannot arrive before this call returns
	// (all JS interaction is performed sequentially in the renderer thread)
	parent->callbacks[id] = request;
	bool succeeded = false;
	try {
		context->GetBrowser()->SendProcessMessage( PID_BROWSER, message );
		succeeded = true;
	} catch( ... ) {
		parent->callbacks.erase( id );
	}
	if( succeeded ) {
		retVal = CefV8Value::CreateNull();
	} else {
		exception = "Can't send a message to the browser process";
	}
}

const CefString SimplexMessage::updateScreen( "updateScreen" );
const CefString SimplexMessage::mouseSet( "mouseSet" );
const CefString SimplexMessage::gameCommand( "gameCommand" );

SimplexMessageHandler::SimplexMessageHandler( WswCefV8Handler *parent_, const CefString &messageName_ )
	: parent( parent_ )
	, messageName( messageName_ )
	, logTag( "MessageHandler@" + messageName_.ToString() ) {
	this->next = parent_->messageHandlersHead;
	parent_->messageHandlersHead = this;
}

void SimplexMessageSender::SendProcessMessage( CefRefPtr<CefProcessMessage> message ) {
	parent->SendProcessMessage( message );
}

void SimplexMessageSender::DeleteMessage( SimplexMessage *message ) {
	// The message is either allocated using NewPooledObject()
	// or allocated dynamically in a default heap
	// (if the message pipe has deferred these messages transmission till the ui has signaled its ready state).
	// If the message is allocated somewhere else, a default heap will surely detect it / crash.
	if( message->ShouldDeleteSelf() ) {
		message->DeleteSelf();
	} else {
		delete message;
	}
}

std::string SimplexMessageHandler::DescribeException( const CefString &code, CefRefPtr<CefV8Exception> exception ) {
	std::stringstream s;

	s << "An execution of `" << code.ToString() << "` has failed with exception ";
	s << "at line " << exception->GetLineNumber() << ", ";
	s << "column " << exception->GetStartColumn() << ": ";
	s << '`' << exception->GetMessage().ToString() << '`';

	return s.str();
}

void SimplexMessageHandler::Handle( CefRefPtr<CefBrowser> &browser, CefRefPtr<CefProcessMessage> &processMessage ) {
	RenderProcessLogger logger( browser );

	auto frame = browser->GetMainFrame();
	auto context = frame->GetV8Context();
	if( !context->Enter() ) {
		logger.Error( "%s: Cannot enter V8 Context", logTag.c_str() );
		return;
	}

	SimplexMessage *deserializedMessage = DeserializeMessage( processMessage );
	if( !deserializedMessage ) {
		logger.Error( "%s: Message deserialization has failed", logTag.c_str() );
		return;
	}

	CefStringBuilder stringBuilder;
	if( !GetCodeToCall( deserializedMessage, stringBuilder ) ) {
		logger.Error( "%s: Cannot build code to call, looks like the message is malformed", logTag.c_str() );
		deserializedMessage->DeleteSelf();
		return;
	}

	deserializedMessage->DeleteSelf();

	CefString code( stringBuilder.ReleaseOwnership() );

	logger.Debug( "%s: About to execute ```%s```", logTag.c_str(), code.ToString().c_str() );

	CefRefPtr<CefV8Value> retVal;
	CefRefPtr<CefV8Exception> exception;
	if( context->Eval( code, frame->GetURL(), 0, retVal, exception ) ) {
		if( !context->Exit() ) {
			logger.Warning( "%s: context->Exit() call failed after successful Eval() call", logTag.c_str() );
		}
		return;
	}

	logger.Error( "%s: %s", logTag.c_str(), DescribeException( code, exception ).c_str() );
	if( !context->Exit() ) {
		logger.Warning( "%s: context->Exit() call failed after unsuccessful Eval() call", logTag.c_str() );
	}
}