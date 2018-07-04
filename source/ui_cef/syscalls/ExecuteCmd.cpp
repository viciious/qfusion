#include "SyscallsLocal.h"

void ExecuteCmdRequestLauncher::StartExec( const CefV8ValueList &arguments,
										   CefRefPtr<CefV8Value> &retval,
										   CefString &exception ) {
	if( arguments.size() != 3 ) {
		exception = "Illegal arguments list size, expected 3";
		return;
	}

	// We prefer passing `whence` as a string to simplify debugging, even if an integer is sufficient.
	CefString whenceString;
	if( !TryGetString( arguments[0], "whence", whenceString, exception ) ) {
		return;
	}

	int whence;
	if( !whenceString.compare( "now" ) ) {
		whence = EXEC_NOW;
	} else if( !whenceString.compare( "insert" ) ) {
		whence = EXEC_INSERT;
	} else if( !whenceString.compare( "append" ) ) {
		whence = EXEC_APPEND;
	} else {
		exception = "Illegal `whence` parameter. `now`, `insert` or `append` are expected";
		return;
	}

	CefString text;
	if( !TryGetString( arguments[1], "text", text, exception ) ) {
		return;
	}

	if( !ValidateCallback( arguments.back(), exception ) ) {
		return;
	}

	auto context( CefV8Context::GetCurrentContext() );
	auto request( NewRequest( context, arguments.back() ) );

	auto message( CefProcessMessage::Create( "executeCmd" ) );
	auto messageArgs( message->GetArgumentList() );
	messageArgs->SetInt( 0, request->Id() );
	messageArgs->SetInt( 1, whence );
	messageArgs->SetString( 2, text );

	Commit( std::move( request ), context, message, retval, exception );
}

void ExecuteCmdRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> ingoing ) {
	auto ingoingArgs( ingoing->GetArgumentList() );
	const int id = ingoingArgs->GetInt( 0 );

	api->Cmd_ExecuteText( ingoingArgs->GetInt( 1 ), ingoingArgs->GetString( 2 ).ToString().c_str() );

	auto outgoing( NewMessage() );
	outgoing->GetArgumentList()->SetInt( 0, id );
	browser->SendProcessMessage( PID_RENDERER, outgoing );
}

void ExecuteCmdRequest::FireCallback( CefRefPtr<CefProcessMessage> ) {
	ExecuteCallback( CefV8ValueList() );
}