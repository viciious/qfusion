#include "SyscallsLocal.h"

static const std::map<CefString, int> cvarFlagNamesTable = {
	{ "archive"       , CVAR_ARCHIVE },
	{ "userinfo"      , CVAR_USERINFO },
	{ "serverinfo"    , CVAR_SERVERINFO },
	{ "noset"         , CVAR_NOSET },
	{ "latch"         , CVAR_LATCH },
	{ "latch_video"   , CVAR_LATCH_VIDEO },
	{ "latch_sound"   , CVAR_LATCH_SOUND },
	{ "cheat"         , CVAR_CHEAT },
	{ "readonly"      , CVAR_READONLY },
	{ "developer"     , CVAR_DEVELOPER }
};

void GetCVarRequestLauncher::StartExec( const CefV8ValueList &arguments, CefRefPtr<CefV8Value> &retval, CefString &exception ) {
	if( arguments.size() < 3 ) {
		exception = "Illegal arguments list size, should be 3 or 4";
		return;
	}
	CefString name, defaultValue;
	if( !TryGetString( arguments[0], "name", name, exception ) ) {
		return;
	}
	if( !TryGetString( arguments[1], "defaultValue", defaultValue, exception ) ) {
		return;
	}

	int flags = 0;
	if( arguments.size() == 4 ) {
		auto flagsArray( arguments[2] );
		if( !flagsArray->IsArray() ) {
			exception = "An array of flags is expected for a 3rd argument in this case";
			return;
		}
		for( int i = 0, end = flagsArray->GetArrayLength(); i < end; ++i ) {
			CefRefPtr<CefV8Value> flagValue( flagsArray->GetValue( i ) );
			// See GetValue() documentation
			if( !flagValue.get() ) {
				exception = "Can't get an array value";
				return;
			}
			if( !flagValue->IsString() ) {
				exception = "A flags array is allowed to hold only string values";
				return;
			}
			auto flagString( flagValue->GetStringValue() );
			auto it = ::cvarFlagNamesTable.find( flagString );
			if( it == ::cvarFlagNamesTable.end() ) {
				exception = std::string( "Unknown CVar flag value " ) + flagString.ToString();
				return;
			}
			flags |= ( *it ).second;
		}
	}

	if( !ValidateCallback( arguments.back(), exception ) ) {
		return;
	}

	auto context( CefV8Context::GetCurrentContext() );
	auto message( NewMessage() );
	auto messageArgs( message->GetArgumentList() );

	auto request( NewRequest( context, arguments.back() ) );

	messageArgs->SetInt( 0, request->Id() );
	messageArgs->SetString( 1, name );
	messageArgs->SetString( 2, defaultValue );
	messageArgs->SetInt( 3, flags );

	Commit( std::move( request ), context, message, retval, exception );
}

void GetCVarRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> ingoing ) {
	auto ingoingArgs( ingoing->GetArgumentList() );
	int id = ingoingArgs->GetInt( 1 );
	std::string name( ingoingArgs->GetString( 1 ).ToString() );
	std::string value( ingoingArgs->GetString( 2 ).ToString() );
	int flags = 0;
	if( ingoingArgs->GetSize() == 4 ) {
		flags = ingoingArgs->GetInt( 3 );
	}

	cvar_t *var = api->Cvar_Get( name.c_str(), value.c_str(), flags );
	auto outgoing( NewMessage() );
	auto outgoingArgs( outgoing->GetArgumentList() );
	outgoingArgs->SetInt( 0, id );
	outgoingArgs->SetString( 1, name );
	outgoingArgs->SetString( 2, var->string );
	browser->SendProcessMessage( PID_RENDERER, outgoing );
}

void GetCVarRequest::FireCallback( CefRefPtr<CefProcessMessage> reply ) {
	auto args( reply->GetArgumentList() );
	size_t numArgs = args->GetSize();
	if( numArgs != 3 ) {
		ReportNumArgsMismatch( numArgs, "3" );
		return;
	}

	ExecuteCallback( { CefV8Value::CreateString( args->GetString( 2 ) ) } );
}