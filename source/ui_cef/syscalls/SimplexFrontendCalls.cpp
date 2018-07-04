#include "SyscallsLocal.h"

#include "../../gameshared/q_comref.h"
#include "../../server/server.h"

bool GameCommandHandler::GetCodeToCall( CefRefPtr<CefProcessMessage> &message, CefStringBuilder &sb ) {
	auto messageArgs( message->GetArgumentList() );
	size_t numArgs = messageArgs->GetSize();

	sb << "ui.onGameCommand.apply(null, [";
	for( size_t i = 0; i < numArgs; ++i ) {
		sb << '"' << messageArgs->GetString( i ) << '"' << ',';
	}

	if( numArgs ) {
		// Chop the last comma
		sb.ChopLast();
	}
	sb << "])";

	return true;
}

bool MouseSetHandler::GetCodeToCall( CefRefPtr<CefProcessMessage> &message, CefStringBuilder &sb ) {
	auto args( message->GetArgumentList() );

	sb << "ui.onMouseSet({ ";
	sb << "context : "      << args->GetInt( 0 ) << ',';
	sb << "mx : "           << args->GetInt( 1 ) << ',';
	sb << "my : "           << args->GetInt( 2 ) << ',';
	sb << "showCursor : "   << args->GetBool( 3 );
	sb << " })";

	return true;
}

static const char *DownloadTypeAsParam( int type ) {
	switch( type ) {
		case DOWNLOADTYPE_NONE:
			return "\"none\"";
		case DOWNLOADTYPE_WEB:
			return "\"http\"";
		case DOWNLOADTYPE_SERVER:
			return "\"builtin\"";
		default:
			return "\"\"";
	}
}

static const char *ClientStateAsParam( int state ) {
	switch( state ) {
		case CA_GETTING_TICKET:
		case CA_CONNECTING:
		case CA_HANDSHAKE:
		case CA_CONNECTED:
		case CA_LOADING:
			return "\"connecting\"";
		case CA_ACTIVE:
			return "\"active\"";
		case CA_CINEMATIC:
			return "\"cinematic\"";
		default:
			return "\"disconnected\"";
	}
}

static const char *ServerStateAsParam( int state ) {
	switch( state ) {
		case ss_game:
			return "\"active\"";
		case ss_loading:
			return "\"loading\"";
		default:
			return "\"off\"";
	}
}

bool UpdateConnectScreenHandler::GetCodeToCall( CefRefPtr<CefProcessMessage> &message, CefStringBuilder &sb ) {
	auto args( message->GetArgumentList() );

	CefString rejectMessage( args->GetString( 1 ) );

	sb << "ui.updateConnectScreenState({ ";
	sb << " serverName : \""  << args->GetString( 0 ) << "\",";
	sb << " connectCount: "   << args->GetInt( 6 ) << ",";
	sb << " background : "    << args->GetBool( 7 ) << ",";

	if( !rejectMessage.empty() ) {
		sb << " rejectMessage: \"" << rejectMessage << "\",";
	}

	int downloadType = args->GetInt( 3 );
	if( downloadType != DOWNLOADTYPE_NONE ) {
		sb << " download: { ";
		sb << " file: \""   << args->GetString( 2 ) << "\",";
		sb << " type: "     << DownloadTypeAsParam( downloadType );
		sb << " percent: "  << args->GetDouble( 4 ) << ',';
		sb << " speed: "    << args->GetDouble( 5 ) << ',';
		sb << " },";
	}

	sb.ChopLast();
	sb << " })";

	return true;
}

bool UpdateMainScreenHandler::GetCodeToCall( CefRefPtr<CefProcessMessage> &message, CefStringBuilder &sb ) {
	auto args( message->GetArgumentList() );

	sb << "ui.updateMainScreenState({ ";
	sb << " clientState : "    << ClientStateAsParam( args->GetInt( 0 ) ) << ',';
	sb << " serverState : "    << ServerStateAsParam( args->GetInt( 1 ) ) << ',';
	if( args->GetBool( 4 ) ) {
		sb << "demoPlayback : { ";
		sb << " state: \"" << ( args->GetBool( 5 ) ? "paused" : "playing" ) << "\",";
		sb << " file: \"" << args->GetString( 3 ) << "\",",
		sb << " time: " << args->GetInt( 2 );
		sb << " },";
	}

	sb << " showCursor : "     << args->GetBool( 6 ) << ',';
	sb << " background : "     << args->GetBool( 7 );
	sb << " })";

	return true;
}