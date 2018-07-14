#include "V8Handler.h"
#include "RenderProcessHandler.h"

inline RenderProcessLogger* WswCefV8Handler::Logger() {
	return renderProcessHandler->Logger();
}

bool WswCefV8Handler::Execute( const CefString& name,
							   CefRefPtr<CefV8Value> object,
							   const CefV8ValueList& arguments,
							   CefRefPtr<CefV8Value>& retval,
							   CefString& exception ) {
	CEF_REQUIRE_RENDERER_THREAD();

	for( PendingRequestLauncher *launcher = requestLaunchersHead; launcher; launcher = launcher->Next() ) {
		if( !launcher->Method().compare( name ) ) {
			Logger()->Debug( "Found a launcher %s for a request", launcher->LogTag().c_str() );
			launcher->StartExec( arguments, retval, exception );
			return true;
		}
	}

	if( !name.compare( "notifyUiPageReady" ) ) {
		retval = CefV8Value::CreateNull();
		auto message( CefProcessMessage::Create( "uiPageReady" ) );
		CefV8Context::GetCurrentContext()->GetBrowser()->SendProcessMessage( PID_BROWSER, message );
		return true;
	}

	Logger()->Error( "Can't handle unknown JS call %s\n", name.ToString().c_str() );
	return false;
}

bool WswCefV8Handler::TryHandle( CefRefPtr<CefBrowser> &browser, CefRefPtr<CefProcessMessage> &message ) {
	CefString name( message->GetName() );
	for( SimplexMessageHandler *handler = messageHandlersHead; handler; handler = handler->Next() ) {
		if( !handler->MessageName().compare( name ) ) {
			handler->Handle( browser, message );
			return true;
		}
	}

	for( PendingRequestLauncher *launcher = requestLaunchersHead; launcher; launcher = launcher->Next() ) {
		// Just check the name match ensuring this really is a callback.
		// The implementation could be faster but the total number of message kinds is insignificant.
		if( !launcher->Method().compare( name ) ) {
			ProcessAsAwaitedReply( message );
			return true;
		}
	}

	return false;
}

void WswCefV8Handler::ProcessAsAwaitedReply( CefRefPtr<CefProcessMessage> &message ) {
	auto args( message->GetArgumentList() );
	if( args->GetSize() < 1 ) {
		std::string name( message->GetName().ToString() );
		Logger()->Error( "Empty arguments list for a message `%s` that is awaited for firing a callback", name.c_str() );
		return;
	}

	const int id = args->GetInt( 0 );
	// These non-atomic operations should be safe as all JS interaction is performed in the renderer thread
	auto iter = callbacks.find( id );
	if( iter == callbacks.end() ) {
		std::string name( message->GetName().ToString() );
		Logger()->Error( "Can't find a callback by id %d for a message `%s`", id, name.c_str() );
		return;
	}

	( *iter ).second->FireCallback( message );
	callbacks.erase( iter );
}