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

class VideoModesSource {
	unsigned index { 0 };
	const int currWidth;
	const int currHeight;
	bool wasCurrModeListed { false };
public:
	VideoModesSource()
		: currWidth( (int)api->Cvar_Value( "vid_width" ) ),
		  currHeight( (int)api->Cvar_Value( "vid_height" ) ) {}

	bool Next( int *width, int *height ) {
		if( api->VID_GetModeInfo( width, height, index++ ) ) {
			if( *width == currWidth && *height == currHeight ) {
				wasCurrModeListed = true;
			}
			return true;
		}
		if( !wasCurrModeListed ) {
			*width = currWidth;
			*height = currHeight;
			wasCurrModeListed = true;
			return true;
		}
		return false;
	}
};

void GetVideoModesRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> ingoing ) {
	const int id = ingoing->GetArgumentList()->GetInt( 0 );

	auto outgoing( NewMessage() );
	auto args( outgoing->GetArgumentList() );
	size_t argNum = 0;
	args->SetInt( argNum++, id );

	int width, height;
	VideoModesSource videoModesSource;
	while( videoModesSource.Next( &width, &height ) ) {
		args->SetInt( argNum++, width );
		args->SetInt( argNum++, height );
	}

	browser->SendProcessMessage( PID_RENDERER, outgoing );
}

void GetVideoModesRequest::FireCallback( CefRefPtr<CefProcessMessage> reply ) {
	auto argPrinter = []( CefStringBuilder &sb, CefRefPtr<CefListValue> &args, size_t argNum ) {
		sb << args->GetInt( argNum );
	};
	FireSingleArgAggregateCallback<ArrayOfPairsBuildHelper>( reply, "width", "height", argPrinter );
}