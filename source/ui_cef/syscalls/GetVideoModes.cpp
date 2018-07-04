#include "SyscallsLocal.h"

void GetVideoModesRequestLauncher::StartExec( const CefV8ValueList &arguments,
											  CefRefPtr<CefV8Value> &retval,
											  CefString &exception ) {
	if( arguments.size() != 1 ) {
		exception = "Illegal arguments list size, must be a single argument";
		return;
	}

	if( !ValidateCallback( arguments.back(), exception ) ) {
		return;
	}

	auto context( CefV8Context::GetCurrentContext() );
	auto request( NewRequest( context, arguments.back() ) );

	auto message( NewMessage() );
	message->GetArgumentList()->SetInt( 0, request->Id() );

	Commit( std::move( request ), context, message, retval, exception );
}

UiFacade::VideoModesList UiFacade::GetVideoModes() {
	VideoModesList result;
	result.reserve( 32 );

	const auto currWidth = (int)api->Cvar_Value( "vid_width" );
	const auto currHeight = (int)api->Cvar_Value( "vid_height" );

	bool isCurrModeListed = false;
	int width, height;
	for( unsigned i = 0; api->VID_GetModeInfo( &width, &height, i ); ++i ) {
		if( currWidth == width && currHeight == height ) {
			isCurrModeListed = true;
		}
		result.emplace_back( std::make_pair( width, height ) );
	}

	if( !isCurrModeListed ) {
		result.emplace_back( std::make_pair( currWidth, currHeight ) );
	}

	return result;
};

void GetVideoModesRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> ingoing ) {
	const int id = ingoing->GetArgumentList()->GetInt( 0 );

	std::vector<std::pair<int, int>> modes( UiFacade::GetVideoModes() );

	auto outgoing( NewMessage() );
	auto args( outgoing->GetArgumentList() );
	args->SetInt( 0, id );
	size_t i = 1;
	for( const auto &mode: modes ) {
		args->SetInt( i++, mode.first );
		args->SetInt( i++, mode.second );
	}

	browser->SendProcessMessage( PID_RENDERER, outgoing );
}

void GetVideoModesRequest::FireCallback( CefRefPtr<CefProcessMessage> reply ) {
	auto argPrinter = []( CefStringBuilder &sb, CefRefPtr<CefListValue> &args, size_t argNum ) {
		sb << args->GetInt( argNum );
	};
	FireSingleArgAggregateCallback<ArrayOfPairsBuildHelper>( reply, "width", "height", argPrinter );
}