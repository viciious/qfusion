#include "SyscallsLocal.h"

void GetMapsRequestLauncher::StartExec( const CefV8ValueList &jsArgs, CefRefPtr<CefV8Value> &retVal, CefString &ex ) {
	return DefaultSingleArgStartExecImpl( jsArgs, retVal, ex );
}

class MapsListSource {
	int index { 0 };
	char buffer[( MAX_CONFIGSTRING_CHARS + 1 ) * 2];
public:
	bool Next( const char **shortName, const char **fullName ) {
		// TODO: These all APIs are horribly inefficient...
		// Transfer an ownership of dynamically allocated strings instead
		if( !api->ML_GetMapByNum( index++, buffer, sizeof( buffer ) ) ) {
			return false;
		}
		*shortName = buffer;
		*fullName = buffer + strlen( *shortName ) + 1;
		return true;
	}
};

void GetMapsRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> ingoing ) {
	const int id = ingoing->GetArgumentList()->GetInt( 0 );
	auto message( CefProcessMessage::Create( PendingCallbackRequest::getMaps ) );
	auto args( message->GetArgumentList() );

	size_t argNum = 0;
	args->SetInt( argNum++, id );

	// TODO: Is not it all so expensive that worth a different thread?
	// Unfortunately heap operations that lock everything are the most expensive part.

	const char *shortName, *fullName;
	MapsListSource mapsListSource;
	while( mapsListSource.Next( &shortName, &fullName ) ) {
		args->SetString( argNum++, shortName );
		args->SetString( argNum++, fullName );
	}

	browser->SendProcessMessage( PID_RENDERER, message );
}

void GetMapsRequest::FireCallback( CefRefPtr<CefProcessMessage> reply ) {
	auto printer = AggregateBuildHelper::QuotedStringPrinter();
	FireSingleArgAggregateCallback<ObjectBuildHelper>( reply, printer, printer );
}