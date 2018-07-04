#include "SyscallsLocal.h"

void GetLocalizedStringsRequestLauncher::StartExec( const CefV8ValueList &jsArgs,
													CefRefPtr<CefV8Value> &retVal,
													CefString &exception ) {
	if( jsArgs.size() != 2 ) {
		exception = "Illegal arguments list size, there must be two arguments";
		return;
	}

	auto stringsArray( jsArgs[0] );
	if( !stringsArray->IsArray() ) {
		exception = "The first argument must be an array of strings";
		return;
	}

	if( !ValidateCallback( jsArgs.back(), exception ) ) {
		return;
	}

	// TODO: Fetch and validate array args before this?
	// Not sure if a message creation is expensive.
	// Should not impact the happy code path anyway.
	auto context( CefV8Context::GetCurrentContext() );
	auto request( NewRequest( context, jsArgs.back() ) );
	auto message( NewMessage() );
	auto messageArgs( message->GetArgumentList() );
	size_t argNum = 0;
	messageArgs->SetInt( argNum++, request->Id() );
	for( int i = 0, length = stringsArray->GetArrayLength(); i < length; ++i ) {
		auto elemValue( stringsArray->GetValue( i ) );
		if( !elemValue->IsString() ) {
			std::stringstream ss;
			ss << "The array value at " << i << " is not a string";
			exception = ss.str();
			return;
		}
		messageArgs->SetString( argNum++, elemValue->GetStringValue() );
	}

	Commit( std::move( request ), context, message, retVal, exception );
}

UiFacade::LocalizedPairsList UiFacade::GetLocalizedStrings( const std::vector<std::string> &request ) {
	UiFacade::LocalizedPairsList result;
	result.reserve( request.size() );

	for( auto &s: request ) {
		if( const char *localized = api->L10n_TranslateString( s.c_str() ) ) {
			result.emplace_back( std::make_pair( s, std::string( localized ) ) );
		} else {
			result.emplace_back( std::make_pair( s, s ) );
		}
	}

	return result;
}

void GetLocalizedStringsRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser,
														CefRefPtr<CefProcessMessage> ingoing ) {
	auto ingoingArgs( ingoing->GetArgumentList() );
	const size_t ingoingArgsSize = ingoingArgs->GetSize();
	const int id = ingoingArgs->GetInt( 0 );

	std::vector<std::string> localizationRequest;
	localizationRequest.reserve( ingoingArgsSize - 1 );
	for( size_t i = 1; i < ingoingArgsSize; ++i ) {
		localizationRequest.emplace_back( ingoingArgs->GetString( i ) );
	}

	auto localizedPairs( UiFacade::GetLocalizedStrings( localizationRequest ) );

	auto message( CefProcessMessage::Create( method ) );
	auto outgoingArgs( message->GetArgumentList() );

	size_t argNum = 0;
	outgoingArgs->SetInt( argNum++, id );

	for( auto &pair: localizedPairs ) {
		outgoingArgs->SetString( argNum++, pair.first );
		outgoingArgs->SetString( argNum++, pair.second );
	}

	browser->SendProcessMessage( PID_RENDERER, message );
}

void GetLocalizedStringsRequest::FireCallback( CefRefPtr<CefProcessMessage> reply ) {
	FireSingleArgAggregateCallback<ObjectBuildHelper>( reply );
}