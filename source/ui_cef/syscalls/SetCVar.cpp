#include "SyscallsLocal.h"

void SetCVarRequestLauncher::StartExec( const CefV8ValueList &arguments,
										CefRefPtr<CefV8Value> &retval,
										CefString &exception ) {
	if( arguments.size() != 3 && arguments.size() != 4 ) {
		exception = "Illegal arguments list size, should be 2 or 3";
		return;
	}

	CefString name, value;
	if( !TryGetString( arguments[0], "name", name, exception ) ) {
		return;
	}
	if( !TryGetString( arguments[1], "value", value, exception ) ) {
		return;
	}

	bool forceSet = false;
	if( arguments.size() == 4 ) {
		CefString s;
		if( !TryGetString( arguments[2], "force", s, exception ) ) {
			return;
		}
		if( s.compare( "force" ) ) {
			exception = "Only a string literal \"force\" is expected for a 3rd argument in this case";
			return;
		}
		forceSet = true;
	}

	if( !ValidateCallback( arguments.back(), exception ) ) {
		return;
	}

	auto context( CefV8Context::GetCurrentContext() );
	auto request( NewRequest( context, arguments.back() ) );

	auto message( CefProcessMessage::Create( "setCVar" ) );
	auto messageArgs( message->GetArgumentList() );
	messageArgs->SetInt( 0, request->Id() );
	messageArgs->SetString( 1, name );
	messageArgs->SetString( 2, value );
	if( forceSet ) {
		messageArgs->SetBool( 3, forceSet );
	}

	Commit( std::move( request ), context, message, retval, exception );
}

void SetCVarRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> ingoing ) {
	auto ingoingArgs( ingoing->GetArgumentList() );
	const int id = ingoingArgs->GetInt( 0 );
	std::string name( ingoingArgs->GetString( 1 ).ToString() );
	std::string value( ingoingArgs->GetString( 2 ).ToString() );
	bool force = false;
	if( ingoingArgs->GetSize() == 4 ) {
		force = ingoingArgs->GetBool( 3 );
	}

	bool forced = false;
	if( force ) {
		forced = ( api->Cvar_ForceSet( name.c_str(), value.c_str() ) ) != nullptr;
	} else {
		api->Cvar_Set( name.c_str(), value.c_str() );
	}

	auto outgoing( NewMessage() );
	outgoing->GetArgumentList()->SetInt( 0, id );
	if( force ) {
		outgoing->GetArgumentList()->SetBool( 1, forced );
	}

	browser->SendProcessMessage( PID_RENDERER, outgoing );
}

void SetCVarRequest::FireCallback( CefRefPtr<CefProcessMessage> reply ) {
	auto args( reply->GetArgumentList() );
	auto size = args->GetSize();
	if( size != 1 && size != 2 ) {
		ReportNumArgsMismatch( size, "1 or 2" );
		return;
	}

	CefV8ValueList callbackArgs;
	if( size == 2 ) {
		callbackArgs.emplace_back( CefV8Value::CreateBool( args->GetBool( 1 ) ) );
	}

	ExecuteCallback( callbackArgs );
}