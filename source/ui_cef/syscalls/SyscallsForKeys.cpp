#include "SyscallsLocal.h"

void RequestForKeysHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> ingoing ) {
	auto ingoingArgs( ingoing->GetArgumentList() );
	const int id = ingoingArgs->GetInt( 0 );

	auto outgoing( CefProcessMessage::Create( method ) );
	auto outgoingArgs( outgoing->GetArgumentList() );
	outgoingArgs->SetInt( 0, id );

	const size_t ingoingArgsSize = ingoingArgs->GetSize();
	if( ingoingArgsSize > 1 ) {
		size_t outgoingArgNum = 1;
		for( size_t ingoingArgNum = 1; ingoingArgNum < ingoingArgsSize; ++ingoingArgNum ) {
			int key = ingoingArgs->GetInt( ingoingArgNum );
			outgoingArgs->SetInt( outgoingArgNum++, key );
			outgoingArgs->SetString( outgoingArgNum++, GetForKey( key ) );
		}
	} else {
		size_t argNum = 1;
		for( int i = 0; i < 256; ++i ) {
			outgoingArgs->SetInt( argNum++, i );
			outgoingArgs->SetString( argNum++, GetForKey( i ) );
		}
	}

	browser->SendProcessMessage( PID_RENDERER, outgoing );
}

// Its cleaner to define it here than bloat subclass one-liners with lambdas

const char *GetKeyBindingsRequestHandler::GetForKey( int key ) {
	return api->Key_GetBindingBuf( key );
}

const char *GetKeyNamesRequestHandler::GetForKey( int key ) {
	return api->Key_KeynumToString( key );
}

static const auto keyPrinter = []( CefStringBuilder &sb, CefRefPtr<CefListValue> &args, size_t argNum ) {
	sb << '\"' << args->GetInt( argNum ) << '\"';
};

void GetKeyBindingsRequest::FireCallback( CefRefPtr<CefProcessMessage> reply ) {
	FireSingleArgAggregateCallback<ObjectBuildHelper>( reply, keyPrinter, AggregateBuildHelper::QuotedStringPrinter() );
}

void GetKeyNamesRequest::FireCallback( CefRefPtr<CefProcessMessage> reply ) {
	FireSingleArgAggregateCallback<ObjectBuildHelper>( reply, keyPrinter, AggregateBuildHelper::QuotedStringPrinter() );
}