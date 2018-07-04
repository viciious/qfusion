#include "SyscallsLocal.h"

void GetMapsRequestLauncher::StartExec( const CefV8ValueList &jsArgs, CefRefPtr<CefV8Value> &retVal, CefString &ex ) {
	return DefaultSingleArgStartExecImpl( jsArgs, retVal, ex );
}

UiFacade::MapsList UiFacade::GetMaps() {
	UiFacade::MapsList result;
	char buffer[( MAX_CONFIGSTRING_CHARS + 1 ) * 2];
	// TODO: These all APIs are horribly inefficient... Transfer an ownership of dynamically allocated strings instead
	for( int i = 0; api->ML_GetMapByNum( i, buffer, sizeof( buffer ) ); ++i ) {
		const char *shortName = buffer;
		size_t shortNameLen = strlen( buffer );
		const char *fullName = buffer + shortNameLen + 1;
		size_t fullNameLen = strlen( fullName );
		auto pair( std::make_pair( std::string( shortName, shortNameLen ), std::string( fullName, fullNameLen ) ) );
		result.emplace_back( std::move( pair ) );
	}
	return result;
}



void GetMapsRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> ingoing ) {
	const int id = ingoing->GetArgumentList()->GetInt( 0 );
	auto message( CefProcessMessage::Create( PendingCallbackRequest::getMaps ) );
	auto args( message->GetArgumentList() );

	size_t argNum = 0;
	args->SetInt( argNum++, id );

	// TODO: Is not it so expensive that worth a different thread?
	// Unfortunately heap operations that lock everything are the most expensive part.
	auto mapList( UiFacade::GetMaps() );
	for( const auto &mapNames: mapList ) {
		args->SetString( argNum++, mapNames.first );
		args->SetString( argNum++, mapNames.second );
	}

	browser->SendProcessMessage( PID_RENDERER, message );
}

void GetMapsRequest::FireCallback( CefRefPtr<CefProcessMessage> reply ) {
	auto printer = AggregateBuildHelper::QuotedStringPrinter();
	FireSingleArgAggregateCallback<ObjectBuildHelper>( reply, printer, printer );
}