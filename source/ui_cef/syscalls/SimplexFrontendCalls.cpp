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

bool UpdateScreenHandler::GetCodeToCall( CefRefPtr<CefProcessMessage> &message, CefStringBuilder &sb ) {
	auto args( message->GetArgumentList() );

	size_t argNum = 0;

	sb << "ui.updateScreen({ ";
	sb << " clientState : "    << ClientStateAsParam( args->GetInt( argNum++ ) ) << ',';
	sb << " serverState : "    << ServerStateAsParam( args->GetInt( argNum++ ) ) << ',';
	sb << " showCursor : "     << args->GetBool( argNum++ ) << ',';
	sb << " background : "     << args->GetBool( argNum++ ) << ',';

	int attachments = args->GetInt( argNum++ );
	if( !attachments ) {
		sb.ChopLast();
		sb << " })";
		return true;
	}

	if( attachments & MainScreenState::DEMO_PLAYBACK_ATTACHMENT ) {
		sb << " demoPlayback: { ";
		sb << " time : " << (unsigned)args->GetInt( argNum++ ) << ',';
		sb << " paused: " << args->GetBool( argNum++ ) << ',';
		if( argNum < args->GetSize() ) {
			demoName = ( args->GetString( argNum++ ) );
		}
		sb << " demoName : \'" << demoName << '\'';
		sb << " }";
		sb << " })";
		return true;
	}

	// Read always transmitted args
	int connectCount = args->GetInt( argNum++ );
	int downloadType = args->GetInt( argNum++ );
	float downloadSpeed = (float)args->GetDouble( argNum++ );
	float downloadPercent = (float)args->GetDouble( argNum++ );
	int stringAttachmentFlags = args->GetInt( argNum++ );
	if( stringAttachmentFlags & ConnectionState::SERVER_NAME_ATTACHMENT ) {
		serverName = args->GetString( argNum++ );
	}
	if( stringAttachmentFlags & ConnectionState::REJECT_MESSAGE_ATTACHMENT ) {
		rejectMessage = args->GetString( argNum++ );
	}
	if( stringAttachmentFlags & ConnectionState::DOWNLOAD_FILENAME_ATTACHMENT ) {
		downloadFilename = args->GetString( argNum++ );
	}

	sb << " connectionState : {";
	if( !rejectMessage.empty() ) {
		sb << "  rejectMessage : '" << rejectMessage << "',";
	}
	if( downloadType ) {
		sb << " download : {";
		sb << " fileName : '" << downloadFilename << "',";
		sb << " type : '" << DownloadTypeAsParam( downloadType ) << "',";
		sb << " speed : '" << downloadSpeed << ',';
		sb << " percent : '" << downloadPercent;
		sb << "},";
	}
	sb << "  connectCount: " << connectCount;
	sb << "}";

    sb.ChopLast();
    sb << " }";
   	sb << " })";

	return true;
}