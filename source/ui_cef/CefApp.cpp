#include "CefApp.h"
#include "UiFacade.h"
#include "Api.h"
#include "CefStringBuilder.h"

#include "include/wrapper/cef_helpers.h"

#include <chrono>
#include "../gameshared/q_comref.h"
#include "../server/server.h"

WswCefApp::WswCefApp()
	: browserProcessHandler( new WswCefBrowserProcessHandler )
	, renderProcessHandler( new WswCefRenderProcessHandler ) {}

void WswCefApp::OnBeforeCommandLineProcessing( const CefString& process_type, CefRefPtr<CefCommandLine> command_line ) {
	command_line->AppendSwitch( "no-proxy-server" );
	command_line->AppendSwitchWithValue( "lang", "en-US" );
}

void WswCefApp::OnRegisterCustomSchemes( CefRawPtr<CefSchemeRegistrar> registrar ) {
	registrar->AddCustomScheme( "ui", false, false, false, true, true, true );
}

class WswCefResourceHandler;

class ReadResourceTask: public CefTask {
	std::string filePath;
	WswCefResourceHandler *parent;

	void NotifyOfResult( const uint8_t *fileData, int fileSize );
public:
	ReadResourceTask( std::string &&filePath_, WswCefResourceHandler *parent_ )
		: filePath( filePath_ ), parent( parent_ ) {}

	void Execute() override;

	IMPLEMENT_REFCOUNTING( ReadResourceTask );
};

class NotifyOfReadResultTask: public CefTask {
	WswCefResourceHandler *parent;
	const uint8_t *fileData;
	int fileSize;
public:
	NotifyOfReadResultTask( WswCefResourceHandler *parent_, const uint8_t *fileData_, int fileSize_ )
		: parent( parent_ ), fileData( fileData_ ), fileSize( fileSize_ ) {}

	void Execute() override;

	IMPLEMENT_REFCOUNTING( NotifyOfReadResultTask );
};

class WswCefResourceHandler: public CefResourceHandler {
	std::string urlValidationError;
	CefRefPtr<CefCallback> processRequestCallback;
	// An address of a literal
	const char *mime;
	int fileSize;
	const uint8_t *fileData;
	int bytesAlreadyRead;
	bool isAGetRequest;

	static const char *schema;
	static const int schemaOffset;

	const std::string ValidateUrl( const std::string &url );
	const std::string TrySaveMime( const char *ext );

	void FailWith( CefRefPtr<CefResponse> response, const std::string statusText, int statusCode, cef_errorcode_t cefCode ) {
		FailWith( response, statusText.c_str(), statusCode, cefCode );
	}

	void FailWith( CefRefPtr<CefResponse> response, const char *statusText, int statusCode, cef_errorcode_t cefCode ) {
		response->SetStatusText( statusText );
		response->SetStatus( statusCode );
		response->SetError( cefCode );
	}
public:
	WswCefResourceHandler()
		: mime( nullptr )
		, fileSize( 0 )
		, fileData( nullptr )
		, bytesAlreadyRead( 0 )
		, isAGetRequest( true ) {}

	~WswCefResourceHandler() override {
		delete fileData;
	}

	bool ProcessRequest( CefRefPtr<CefRequest> request, CefRefPtr<CefCallback> callback ) override;

	void GetResponseHeaders( CefRefPtr<CefResponse> response, int64 &response_length, CefString &redirectUrl ) override;
	bool ReadResponse( void* data_out, int bytes_to_read, int &bytes_read, CefRefPtr<CefCallback> callback ) override;

	bool CanGetCookie( const CefCookie & ) override { return false; }
	bool CanSetCookie( const CefCookie & ) override { return false; }

	void Cancel() override {}

	void NotifyOfReadResult( const uint8_t *fileData, int fileSize ) {
		this->fileData = fileData;
		this->fileSize = fileSize;
		processRequestCallback->Continue();
	}

	IMPLEMENT_REFCOUNTING( WswCefResourceHandler );
};

const char *WswCefResourceHandler::schema = "ui://";
const int WswCefResourceHandler::schemaOffset = (int)strlen( schema );

bool WswCefResourceHandler::ProcessRequest( CefRefPtr<CefRequest> request, CefRefPtr<CefCallback> callback ) {
	if( request->GetMethod().compare( "GET" ) ) {
		// Defer reporting error
		isAGetRequest = false;
		callback->Continue();
		return true;
	}

	std::string url( request->GetURL().ToString() );
	this->urlValidationError = ValidateUrl( url );
	if( !this->urlValidationError.empty() ) {
		callback->Continue();
		return true;
	}

	std::string filePath( "ui/" );
	filePath += url.c_str() + schemaOffset;

	// Save the callback that will be activated after reading the file contents
	processRequestCallback = callback;
	// Launch reading a file in a file IO thread...
	CefPostTask( TID_FILE, AsCefPtr( new ReadResourceTask( std::move( filePath ), this ) ) );
	return true;
}

const std::string WswCefResourceHandler::ValidateUrl( const std::string &url ) {
	if( strstr( url.c_str(), schema ) != url.c_str() ) {
		return std::string( "The url must start with `" ) + schema + '`';
	}

	const char *s = url.c_str() + schemaOffset;
	char ch;
	int dotIndex = -1;
	char lastCh = '\0';
	for(; ( ch = *s++ ); lastCh = ch ) {
		if( ( ch >= 'a' && ch <= 'z' ) || ( ch >= 'A' && ch <= 'Z' ) ) {
			continue;
		}
		if( ( ch >= '0' && ch <= '9' ) || ( ch == '_' || ch == '-' ) ) {
			continue;
		}
		if( ch == '/' ) {
			if( lastCh == '.' || lastCh == '/' ) {
				return std::string("Illegal character `/` following `.` or `/`" );
			}
			if( dotIndex >= 0 ) {
				return std::string( "Illegal `/` character in the extension part" );
			}
			if( !( s - 1 - url.c_str() ) ) {
				return std::string( "Illegal `/` character at the URL start" );
			}
			continue;
		}
		if( ch == '.' ) {
			if( lastCh == '/' || lastCh == '.' ) {
				return std::string( "Illegal character `/` following a preceding character `/` or `.`" );
			}
			if( dotIndex >= 0 ) {
				return std::string( "Duplicated dot character in the URL" );
			}
			dotIndex = (int)( s - 1 - url.c_str() );
			if( dotIndex == 0 ) {
				return std::string( "Illegal dot character at resource part start" );
			}
			continue;
		}

		return std::string( "Illegal character `" ) + ch +
			   "`. Only ASCII letters, numbers, `-/._` characters are allowed ";
	}

	if( dotIndex < 0 ) {
		return std::string( "An extension was not specified. Cannot determine response mime type" );
	}

	if( dotIndex + 1u == url.size() ) {
		return std::string( "The extension part is empty" );
	}

	return TrySaveMime( url.c_str() + dotIndex + 1 );
}

const std::string WswCefResourceHandler::TrySaveMime( const char *ext ) {
	if( !strcmp( ext, "html" ) || !strcmp( ext, "htm" ) ) {
		mime = "text/html";
	} else if( !strcmp( ext, "css" ) ) {
		mime = "text/css";
	} else if( !strcmp( ext, "txt" ) ) {
		mime = "text/plain";
	} else if( !strcmp( ext, "js" ) ) {
		mime = "application/javascript";
	} else if( !strcmp( ext, "json" ) ) {
		mime = "application/json";
	} else if( !strcmp( ext, "jpg" ) || !strcmp( ext, "jpeg" ) ) {
		mime = "image/jpeg";
	} else if( !strcmp( ext, "png" ) ) {
		mime = "image/png";
	} else if( !strcmp( ext, "ogg" ) ) {
		mime = "audio/ogg";
	} else if( !strcmp( ext, "webm" ) ) {
		mime = "video/webm";
	} else {
		return std::string( "Unknown extension " ) + ext;
	}

	// Should not allocate
	return std::string();
}

void ReadResourceTask::NotifyOfResult( const uint8_t *fileData, int fileSize ) {
	CefPostTask( TID_IO, AsCefPtr( new NotifyOfReadResultTask( parent, fileData, fileSize ) ) );
}

void NotifyOfReadResultTask::Execute() {
	parent->NotifyOfReadResult( fileData, fileSize );
}

void ReadResourceTask::Execute() {
	int fileHandle;
	int fileSize = api->FS_FOpenFile( filePath.c_str(), &fileHandle, FS_READ );
	if( fileSize <= 0 ) {
		NotifyOfResult( nullptr, 0 );
		return;
	}

	struct FileGuard {
		int fileHandle;
		explicit FileGuard( int fileHandle_ ): fileHandle( fileHandle_ ) {}
		~FileGuard() { api->FS_FCloseFile( fileHandle ); }
	};

	FileGuard fileGuard( fileHandle );

	// Release the allocated data unless the ownership is explicitly revoked
	std::unique_ptr<uint8_t[]> dataGuard( new uint8_t[fileSize] );

	int readResult = api->FS_Read( dataGuard.get(), (unsigned)fileSize, fileHandle );
	if( readResult != fileSize ) {
		NotifyOfResult( nullptr, 0 );
	}

	// Transfer the ownership of the allocated data
	NotifyOfResult( dataGuard.release(), fileSize );
}

void WswCefResourceHandler::GetResponseHeaders( CefRefPtr<CefResponse> response,
												int64 &response_length,
												CefString &redirectUrl ) {
	response_length = 0;

	// We have deferred this until we could return a response in well-known way
	if( !isAGetRequest ) {
		FailWith( response, "Only Get requests are allowed", 400, ERR_METHOD_NOT_SUPPORTED );
	}

	if( !urlValidationError.empty() ) {
		FailWith( response, urlValidationError, 400, ERR_INVALID_URL );
		return;
	}

	if( !fileData ) {
		FailWith( response, "Cannot open the file in the game filesystem", 404, ERR_FILE_NOT_FOUND );
		return;
	}

	response->SetStatus( 200 );
	response->SetStatusText( "OK" );
	response->SetMimeType( mime );
	response_length = fileSize;
}

bool WswCefResourceHandler::ReadResponse( void *data_out,
										  int bytes_to_read,
										  int &bytes_read,
										  CefRefPtr<CefCallback> callback ) {
	int bytesAvailable = fileSize - bytesAlreadyRead;
	int bytesToActuallyRead = std::min( bytesAvailable, bytes_to_read );
	// Stop resource reading
	if( !bytesToActuallyRead ) {
		bytes_read = 0;
		callback->Continue();
		return false;
	}

	memcpy( data_out, fileData + bytesAlreadyRead, (unsigned)bytesToActuallyRead );
	bytesAlreadyRead += bytesToActuallyRead;
	bytes_read = bytesToActuallyRead;
	callback->Continue();
	return true;
}

class WswCefSchemeHandlerFactory: public CefSchemeHandlerFactory {
	IMPLEMENT_REFCOUNTING( WswCefSchemeHandlerFactory );
public:
	CefRefPtr<CefResourceHandler> Create( CefRefPtr<CefBrowser> browser,
										  CefRefPtr<CefFrame> frame,
										  const CefString& scheme_name,
										  CefRefPtr<CefRequest> request ) override {
		return CefRefPtr<CefResourceHandler>( new WswCefResourceHandler );
	}
};

void WswCefBrowserProcessHandler::OnContextInitialized() {
	CefRefPtr<CefSchemeHandlerFactory> schemeHandlerFactory( new WswCefSchemeHandlerFactory );
	CefRegisterSchemeHandlerFactory( "ui", "ignored domain name", schemeHandlerFactory );

	CefWindowInfo info;
	info.SetAsWindowless( 0 );
	CefBrowserSettings settings;
	CefRefPtr<WswCefClient> client( new WswCefClient );
	CefString url( AsCefString( "ui://index.html" ) );
	CefBrowserHost::CreateBrowserSync( info, client, url, settings, nullptr );
}

void WswCefRenderHandler::OnPaint( CefRefPtr<CefBrowser> browser,
								   CefRenderHandler::PaintElementType type,
								   const CefRenderHandler::RectList &dirtyRects,
								   const void *buffer, int width, int height ) {
	assert( width == this->width );
	assert( height == this->height );
	// TODO: If the total amount of pixel data is small, copy piecewise
	// TODO: I'm unsure if this would be faster due to non-optimal cache access patterns
	memcpy( drawnEveryFrameBuffer, buffer, width * height * 4u );
}

#define IMPLEMENT_LOGGER_METHOD( Name, SEVERITY )                \
void RenderProcessLogger::Name( const char *format, ... ) {   \
	va_list va;                                                  \
	va_start( va, format );                                      \
	SendLogMessage( SEVERITY, format, va );                      \
	va_end( va );                                                \
}

IMPLEMENT_LOGGER_METHOD( Debug, LOGSEVERITY_DEBUG )
IMPLEMENT_LOGGER_METHOD( Info, LOGSEVERITY_INFO )
IMPLEMENT_LOGGER_METHOD( Warning, LOGSEVERITY_WARNING )
IMPLEMENT_LOGGER_METHOD( Error, LOGSEVERITY_ERROR )

void RenderProcessLogger::SendLogMessage( cef_log_severity_t severity, const char *format, va_list va ) {
	char buffer[2048];
	vsnprintf( buffer, sizeof( buffer ), format, va );

	auto message( CefProcessMessage::Create( "log" ) );
	auto args( message->GetArgumentList() );
	args->SetString( 0, buffer );
	args->SetInt( 1, severity );
	browser->SendProcessMessage( PID_BROWSER, message );
}

void WswCefRenderProcessHandler::OnBrowserCreated( CefRefPtr<CefBrowser> browser ) {
	if( !logger.get() ) {
		auto newLogger( std::make_shared<RenderProcessLogger>( browser ) );
		logger.swap( newLogger );
	}
}

void WswCefRenderProcessHandler::OnBrowserDestroyed( CefRefPtr<CefBrowser> browser ) {
	if( logger->UsesBrowser( browser ) ) {
		std::shared_ptr<RenderProcessLogger> emptyLogger;
		logger.swap( emptyLogger );
	}
}

void WswCefRenderProcessHandler::OnWebKitInitialized() {
	const char *code =
		"var ui; if (!ui) { ui = {}; }"
		"(function() {"
		"	ui.notifyUiPageReady = function() {"
		"		native function notifyUiPageReady();"
		"		notifyUiPageReady();"
		"	};"
		"	ui.getCVar = function(name, defaultValue, callback) {"
		"   	native function getCVar(name, defaultValue, callback);"
		"		getCVar(name, defaultValue, callback);"
		"	};"
		"	ui.setCVar = function(name, value, callback) {"
		"		native function setCVar(name, value, callback);"
		"       setCVar(name, value, callback);"
		"	};"
		"	ui.executeNow = function(text, callback) {"
		"		native function executeCmd(whence, text, callback);"
		"		executeCmd('now', text, callback);"
		"	};"
		"	ui.insertToExecute = function(text, callback) {"
		"		native function executeCmd(whence, text, callback);"
		"		executeCmd('insert', text, callback);"
		"	};"
		"	ui.appendToExecute = function(text, callback) {"
		"		native function executeCmd(whence, text, callback);"
		"		executeCmd('append', text, callback);"
		"	};"
		"	ui.getVideoModes = function(callback) {"
		"		native function getVideoModes(callback);"
		"		/* Complex object are passed as a JSON string */"
		"		getVideoModes(function(result) { callback(JSON.parse(result)); });"
		"	};"
		"	ui.getDemosAndSubDirs = function(dir, callback) {"
		"		native function getDemosAndSubDirs(dir, callback);"
		"		/* Two arrays of strings are passed as strings */"
		"		getDemosAndSubDirs(dir, function(demos, subDirs) {"
		"			callback(JSON.parse(demos), JSON.parse(subDirs));"
		"		});"
		"	};"
		"	ui.getDemoMetaData = function(fullPath, callback) {"
		"		native function getDemoMetaData(fullPath, callback);"
		"		/* Complex objects are passed as a JSON string */"
		"		getDemoMetaData(fullPath, function(metaData) {"
		"			callback(JSON.parse(metaData));"
		"		});"
		"	};"
		"	ui.getHuds = function(callback) {"
		"		native function getHuds(callback);"
		"		/* Array of huds is passed as a string */"
		"		getHuds(function(hudsList) {"
		"			callback(JSON.parse(hudsList));"
		"		});"
		"	};"
		"	ui.getGametypes = function(callback) {"
		"		native function getGametypes(callback);"
		"		getGametypes(function(serialized) {"
		"			callback(JSON.parse(serialized));"
		"		});"
  		"	};"
		"	ui.getMaps = function(callback) {"
		"		native function getMaps(callback);"
		"		getMaps(function(serialized) {"
		"			callback(JSON.parse(serialized));"
		"		});"
		"	};"
		"	ui.getLocalizedStrings = function(strings, callback) {"
		"		native function getLocalizedStrings(strings, callback);"
		"		getLocalizedStrings(strings, function(serializedObject) {"
		"			callback(JSON.parse(serializedObject));"
		"		});"
		"	};"
		"})();";

	v8Handler = CefRefPtr<WswCefV8Handler>( new WswCefV8Handler( this ) );
	if( !CefRegisterExtension( "v8/gameUi", code, v8Handler ) ) {
		// TODO: We do not have a browser instance at this moment
	}
}

bool WswCefRenderProcessHandler::OnProcessMessageReceived( CefRefPtr<CefBrowser> browser,
														   CefProcessId source_process,
														   CefRefPtr<CefProcessMessage> message ) {
	const CefString &name = message->GetName();
	if( !name.compare( "command" ) ) {
		ExecuteJavascript( browser, MakeGameCommandCall( message ) );
		return true;
	}

	if( !name.compare( "updateMainScreenState" ) ) {
		ExecuteJavascript( browser, MakeUpdateMainScreenStateCall( message ) );
		return true;
	}

	if( !name.compare( "updateConnectScreenState" ) ) {
		ExecuteJavascript( browser, MakeUpdateConnectScreenStateCall( message ) );
		return true;
	}

	if( !name.compare( "mouseSet" ) ) {
		ExecuteJavascript( browser, MakeMouseSetCall( message ) );
		return true;
	}

	// Got confirmation/reply for a JS-initiated call
	if( v8Handler->CheckForReply( name, message ) ) {
		return true;
	}

	Logger()->Warning( "Unexpected message name `%s`", name.ToString().c_str() );
	return false;
}

CefString WswCefRenderProcessHandler::MakeGameCommandCall( const CefRefPtr<CefProcessMessage> &message ) {
	auto messageArgs( message->GetArgumentList() );
	size_t numArgs = messageArgs->GetSize();

	// TODO: Precompute args size
	CefStringBuilder sb( numArgs * 32u + 32 );
	sb << "ui.onGameCommand.apply(null, [";
	for( size_t i = 0; i < numArgs; ++i ) {
		sb << '"' << messageArgs->GetString( i ) << '"' << ',';
	}

	if( numArgs ) {
		// Chop the last comma
		sb.ChopLast();
	}
	sb << "])";

	return sb.ReleaseOwnership();
}

CefString WswCefRenderProcessHandler::MakeMouseSetCall( const CefRefPtr<CefProcessMessage> &message ) {
	auto args( message->GetArgumentList() );
	CefStringBuilder sb;
	sb << "ui.onMouseSet({ ";
	sb << "context : "      << args->GetInt( 0 ) << ',';
	sb << "mx : "           << args->GetInt( 1 ) << ',';
	sb << "my : "           << args->GetInt( 2 ) << ',';
	sb << "showCursor : "   << args->GetBool( 3 );
	sb << " })";
	return sb.ReleaseOwnership();
}

const char *WswCefRenderProcessHandler::DownloadTypeAsParam( int type ) {
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

const char *WswCefRenderProcessHandler::ClientStateAsParam( int state ) {
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

const char *WswCefRenderProcessHandler::ServerStateAsParam( int state ) {
	switch( state ) {
		case ss_game:
			return "\"active\"";
		case ss_loading:
			return "\"loading\"";
		default:
			return "\"off\"";
	}
}

CefString WswCefRenderProcessHandler::MakeUpdateConnectScreenStateCall( const CefRefPtr<CefProcessMessage> &message ) {
	auto args( message->GetArgumentList() );

	CefString rejectMessage( args->GetString( 1 ) );

	CefStringBuilder sb;

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

	return sb.ReleaseOwnership();
}

CefString WswCefRenderProcessHandler::MakeUpdateMainScreenStateCall( const CefRefPtr<CefProcessMessage> &message ) {
	auto args( message->GetArgumentList() );

	CefStringBuilder sb;

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

	return sb.ReleaseOwnership();
}

CefString WswCefRenderProcessHandler::DescribeException( const std::string &code, CefRefPtr<CefV8Exception> exception ) {
	CefStringBuilder sb;
	sb << "An execution of `" << code << "` has failed with exception: ";

	sb << "message=`" << exception->GetMessage() << "`,";
	sb << "line="     << exception->GetLineNumber() << ",";
	sb << "column="   << exception->GetStartColumn();

	return sb.ReleaseOwnership();
}

bool WswCefRenderProcessHandler::ExecuteJavascript( CefRefPtr<CefBrowser> browser, const std::string &code ) {
	auto frame = browser->GetMainFrame();
	auto context = frame->GetV8Context();
	if( !context->Enter() ) {
		return false;
	}

	Logger()->Debug( "About to execute ```%s```", code.c_str() );

	CefRefPtr<CefV8Value> retVal;
	CefRefPtr<CefV8Exception> exception;
	if( context->Eval( code, frame->GetURL(), 0, retVal, exception ) ) {
		if( !context->Exit() ) {
			Logger()->Warning( "context->Exit() call failed after successful Eval() call" );
		}
		return true;
	}

	Logger()->Warning( "%s", DescribeException( code, exception ).ToString().c_str() );
	if( !context->Exit() ) {
		Logger()->Warning( "context->Exit() call failed after unsuccessful Eval() call" );
	}
	return true;
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
	auto outgoing( CefProcessMessage::Create( "getCVar" ) );
	auto outgoingArgs( outgoing->GetArgumentList() );
	outgoingArgs->SetInt( 0, id );
	outgoingArgs->SetString( 1, name );
	outgoingArgs->SetString( 2, var->string );
	browser->SendProcessMessage( PID_RENDERER, outgoing );
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

void ExecuteCmdRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> ingoing ) {
	auto ingoingArgs( ingoing->GetArgumentList() );
	const int id = ingoingArgs->GetInt( 0 );

	api->Cmd_ExecuteText( ingoingArgs->GetInt( 1 ), ingoingArgs->GetString( 2 ).ToString().c_str() );

	auto outgoing( NewMessage() );
	outgoing->GetArgumentList()->SetInt( 0, id );
	browser->SendProcessMessage( PID_RENDERER, outgoing );
}

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

class IOPendingCallbackRequestTask;

// Performs FS ops in TID_FILE or TID_FILE_BACKGROUND thread
class FSPendingCallbackRequestTask: public CefTask {
	friend class IOPendingCallbackRequestTask;

	CefRefPtr<CefBrowser> browser;
	const int callId;
public:
	FSPendingCallbackRequestTask( CefRefPtr<CefBrowser> browser_, int callId_ )
		: browser( browser_ ), callId( callId_ ) {}

	FSPendingCallbackRequestTask( CefRefPtr<CefBrowser> browser_, const CefRefPtr<CefProcessMessage> &message )
		: browser( browser_ ), callId( message->GetArgumentList()->GetInt( 0 ) ) {}

	virtual CefRefPtr<IOPendingCallbackRequestTask> CreatePostResultsTask() = 0;

	void Execute() final {
		DCHECK( CefCurrentlyOn( TID_FILE ) || CefCurrentlyOn( TID_FILE_BACKGROUND ) );
		CefPostTask( TID_IO, CreatePostResultsTask() );
	}
};



// Sends results retrieved by a corresponding FS task in TID_IO back to the renderer process
class IOPendingCallbackRequestTask: public CefTask {
	CefRefPtr<CefBrowser> browser;
protected:
	const int callId;

	virtual CefRefPtr<CefProcessMessage> FillMessage() = 0;

	// Helpers for building a message
	template <typename Item>
	struct ArgSetter {
		typedef std::function<void( CefRefPtr<CefListValue> &, size_t, const Item & )> Type;
	};

	template <typename Container, typename Item>
	size_t AddEntries( const Container &container,
					   CefRefPtr<CefListValue> messageArgs,
					   std::function<void( CefRefPtr<CefListValue> &, size_t, const Item & )> argSetter ) {
		size_t argNum = messageArgs->GetSize();
		for( const Item &item: container ) {
			argSetter( messageArgs, argNum++, item );
		}
		return argNum;
	};

	template <typename Container, typename First, typename Second>
	size_t AddEntries( const Container &container,
					   CefRefPtr<CefListValue> messageArgs,
					   std::function<void( CefRefPtr<CefListValue> &, size_t, const First & )> setterFor1st,
					   std::function<void( CefRefPtr<CefListValue> &, size_t, const Second & )> setterFor2nd ) {
		size_t argNum = messageArgs->GetSize();
		for( const std::pair<First, Second> &pair: container ) {
			setterFor1st( messageArgs, argNum++, pair.first );
			setterFor2nd( messageArgs, argNum++, pair.second );
		}
		return argNum;
	};

	inline std::function<void( CefRefPtr<CefListValue> &, size_t, const std::string & )> StringSetter() {
		return []( CefRefPtr<CefListValue> &args, size_t argNum, const std::string &s ) {
			args->SetString( argNum, s );
		};
	};
public:
	explicit IOPendingCallbackRequestTask( FSPendingCallbackRequestTask *parent )
		: browser( parent->browser ), callId( parent->callId ) {}

	void Execute() final {
		CEF_REQUIRE_IO_THREAD();

		auto message( FillMessage() );
#ifndef PUBLIC_BUILD
		auto args( message->GetArgumentList() );
		if( args->GetSize() < 1 ) {
			// TODO: Crash...
		}
		if( args->GetInt( 0 ) != callId ) {
			// TODO: Crash...
		}
#endif
		browser->SendProcessMessage( PID_RENDERER, message );
	}
};

class ReplyWithDemoInfoTask: public IOPendingCallbackRequestTask {
	UiFacade::FilesList demos, subDirs;
public:
	ReplyWithDemoInfoTask( FSPendingCallbackRequestTask *parent,
						   UiFacade::FilesList &&demos_,
						   UiFacade::FilesList &&subDirs_ )
		: IOPendingCallbackRequestTask( parent )
		, demos( demos_ )
		, subDirs( subDirs_ ) {}

	CefRefPtr<CefProcessMessage> FillMessage() override {
		auto message( CefProcessMessage::Create( PendingCallbackRequest::getDemosAndSubDirs ) );
		auto args( message->GetArgumentList() );

		args->SetInt( 0, callId );
		args->SetInt( 1, (int)demos.size() );
		size_t nextArgNum = AddEntries( demos, args, StringSetter() );
		args->SetInt( nextArgNum, (int)subDirs.size() );
		AddEntries( subDirs, args, StringSetter() );

		return message;
	}

	IMPLEMENT_REFCOUNTING( ReplyWithDemoInfoTask );
};

class RetrieveDemoInfoTask: public FSPendingCallbackRequestTask {
	std::string dir;
public:
	RetrieveDemoInfoTask( CefRefPtr<CefBrowser> browser, int callId, std::string &&dir_ )
		: FSPendingCallbackRequestTask( browser, callId ), dir( dir_ ) {}

	CefRefPtr<IOPendingCallbackRequestTask> CreatePostResultsTask() override {
		UiFacade::FilesList demos, subDirs;
		std::tie( demos, subDirs ) = UiFacade::FindDemosAndSubDirs( dir );
		return AsCefPtr( new ReplyWithDemoInfoTask( this, std::move( demos ), std::move( subDirs ) ) );
	}

	IMPLEMENT_REFCOUNTING( RetrieveDemoInfoTask );
};

void GetDemosAndSubDirsRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser,
													   CefRefPtr<CefProcessMessage> ingoing ) {
	auto ingoingArgs( ingoing->GetArgumentList() );
	const int callId = ingoingArgs->GetInt( 1 );
	std::string dir( ingoingArgs->GetString( 2 ) );
	CefPostTask( TID_FILE_BACKGROUND, AsCefPtr( new RetrieveDemoInfoTask( browser, callId, std::move( dir ) ) ) );
}

class PostDemoMetaDataTask: public IOPendingCallbackRequestTask {
	UiFacade::DemoMetaData metaData;
public:
	PostDemoMetaDataTask( FSPendingCallbackRequestTask *parent, UiFacade::DemoMetaData &&metaData_ )
		: IOPendingCallbackRequestTask( parent ), metaData( metaData_ ) {}

	CefRefPtr<CefProcessMessage> FillMessage() override {
		auto message( CefProcessMessage::Create( PendingCallbackRequest::getDemoMetaData ) );
		auto args( message->GetArgumentList() );
		args->SetInt( 0, callId );
		AddEntries( metaData, args, StringSetter(), StringSetter() );
		return message;
	}

	IMPLEMENT_REFCOUNTING( PostDemoMetaDataTask );
};

class GetDemoMetaDataTask: public FSPendingCallbackRequestTask {
	std::string path;
public:
	GetDemoMetaDataTask( CefRefPtr<CefBrowser> browser_, int callId_, std::string &&path_ )
		: FSPendingCallbackRequestTask( browser_, callId_ ), path( path_ ) {}

	CefRefPtr<IOPendingCallbackRequestTask> CreatePostResultsTask() override {
		return AsCefPtr( new PostDemoMetaDataTask( this, UiFacade::GetDemoMetaData( path ) ) );
	}

	IMPLEMENT_REFCOUNTING( GetDemoMetaDataTask );
};

void GetDemoMetaDataRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> ingoing ) {
	auto ingoingArgs( ingoing->GetArgumentList() );
	const int callId = ingoingArgs->GetInt( 0 );
	std::string path( ingoingArgs->GetString( 1 ) );
	CefPostTask( TID_FILE, AsCefPtr( new GetDemoMetaDataTask( browser, callId, std::move( path ) ) ) );
}

class PostHudsTask: public IOPendingCallbackRequestTask {
	UiFacade::FilesList huds;
public:
	PostHudsTask( FSPendingCallbackRequestTask *parent, UiFacade::FilesList &&huds_ )
		: IOPendingCallbackRequestTask( parent ), huds( huds_ ) {}

	CefRefPtr<CefProcessMessage> FillMessage() override {
		auto message( CefProcessMessage::Create( PendingCallbackRequest::getHuds ) );
		auto args( message->GetArgumentList() );
		args->SetInt( 0, callId );
		AddEntries( huds, args, StringSetter() );
		return message;
	}

	IMPLEMENT_REFCOUNTING( PostHudsTask );
};

class GetHudsTask: public FSPendingCallbackRequestTask {
public:
	GetHudsTask( CefRefPtr<CefBrowser> browser_, CefRefPtr<CefProcessMessage> message )
		: FSPendingCallbackRequestTask( browser_, message ) {}

	CefRefPtr<IOPendingCallbackRequestTask> CreatePostResultsTask() override {
		return AsCefPtr( new PostHudsTask( this, UiFacade::GetHuds() ) );
	}

	IMPLEMENT_REFCOUNTING( GetHudsTask );
};

void GetHudsRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> ingoing ) {
	CefPostTask( TID_FILE_BACKGROUND, AsCefPtr( new GetHudsTask( browser, ingoing ) ) );
}

class PostGametypesTask: public IOPendingCallbackRequestTask {
	UiFacade::GametypesList gametypes;
public:
	PostGametypesTask( FSPendingCallbackRequestTask *parent, UiFacade::GametypesList &&gametypes_ )
		: IOPendingCallbackRequestTask( parent ), gametypes( gametypes_ ) {}

	CefRefPtr<CefProcessMessage> FillMessage() override {
		auto message( CefProcessMessage::Create( PendingCallbackRequest::getHuds ) );
		auto args( message->GetArgumentList() );
		args->SetInt( 0, callId );
		AddEntries( gametypes, args, StringSetter(), StringSetter() );
		return message;
	}

	IMPLEMENT_REFCOUNTING( PostGametypesTask );
};

class GetGametypesTask: public FSPendingCallbackRequestTask {
public:
	GetGametypesTask( CefRefPtr<CefBrowser> browser_, CefRefPtr<CefProcessMessage> message )
		: FSPendingCallbackRequestTask( browser_, message ) {}

	CefRefPtr<IOPendingCallbackRequestTask> CreatePostResultsTask() override {
		return AsCefPtr( new PostGametypesTask( this, UiFacade::GetGametypes() ) );
	}

	IMPLEMENT_REFCOUNTING( GetGametypesTask );
};

void GetGametypesRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> ingoing ) {
	CefPostTask( TID_FILE_BACKGROUND, AsCefPtr( new GetGametypesTask( browser, ingoing ) ) );
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

bool WswCefClient::OnProcessMessageReceived( CefRefPtr<CefBrowser> browser,
											 CefProcessId source_process,
											 CefRefPtr<CefProcessMessage> processMessage ) {
	CEF_REQUIRE_UI_THREAD();

	auto name( processMessage->GetName() );
	auto messageArgs( processMessage->GetArgumentList() );
	if( !name.compare( "log" ) ) {
		std::string message( messageArgs->GetString( 0 ).ToString() );
		//cef_log_severity_t severity = (cef_log_severity_t)messageArgs->GetInt( 1 );
		// TODO: Use the severity for console character codes output
		printf("Log message from browser process: \n%s\n", message.c_str() );
		return true;
	}

	for( CallbackRequestHandler *handler = requestHandlersHead; handler; handler = handler->Next() ) {
		if( !handler->Method().compare( name ) ) {
			printf( "Found a handler %s for a request\n", handler->LogTag().c_str() );
			handler->ReplyToRequest( browser, processMessage );
			return true;
		}
	}

	if( !name.compare( "uiPageReady" ) ) {
		UiFacade::Instance()->OnUiPageReady();
		return true;
	}

	return false;
}

const CefString PendingCallbackRequest::getCVar( "getCVar" );
const CefString PendingCallbackRequest::setCVar( "setCVar" );
const CefString PendingCallbackRequest::executeCmd( "executeCmd" );
const CefString PendingCallbackRequest::getVideoModes( "getVideoModes" );
const CefString PendingCallbackRequest::getDemosAndSubDirs( "getDemosAndSubDirs" );
const CefString PendingCallbackRequest::getDemoMetaData( "getDemoMetaData" );
const CefString PendingCallbackRequest::getHuds( "getHuds" );
const CefString PendingCallbackRequest::getGametypes( "getGametypes" );
const CefString PendingCallbackRequest::getMaps( "getMaps" );
const CefString PendingCallbackRequest::getLocalizedStrings( "getLocalizedStrings" );

PendingCallbackRequest::PendingCallbackRequest( WswCefV8Handler *parent_,
												CefRefPtr<CefV8Context> context_,
												CefRefPtr<CefV8Value> callback_,
												const CefString &method_ )
	: parent( parent_ )
	, id( parent_->NextCallId() )
	, context( context_ )
	, callback( callback_ )
	, method( method_ ) {}

inline void PendingCallbackRequest::ReportNumArgsMismatch( size_t actual, const char *expected ) {
	std::string tag = "PendingCallbackRequest@" + method.ToString();
	Logger()->Error( "%s: message args size %d does not match expected %s", tag.c_str(), (int)actual, expected );
}

inline void PendingCallbackRequest::ExecuteCallback( const CefV8ValueList &args ) {
	if( !callback->ExecuteFunctionWithContext( context, nullptr, args ).get() ) {
		std::string tag = "PendingCallbackRequest@" + method.ToString();
		Logger()->Error( "%s: JS callback execution failed", tag.c_str() );
	}
}

PendingRequestLauncher::PendingRequestLauncher( WswCefV8Handler *parent_, const CefString &method_ )
	: parent( parent_ ), method( method_ ), logTag( "PendingRequestLauncher@" + method_.ToString() ) {
	this->next = parent_->requestLaunchersHead;
	parent_->requestLaunchersHead = this;
}

CallbackRequestHandler::CallbackRequestHandler( WswCefClient *parent_, const CefString &method_ )
	: parent( parent_ ), method( method_ ), logTag( "CallbackRequestHandler@" + method_.ToString() ) {
	this->next = parent->requestHandlersHead;
	parent->requestHandlersHead = this;
}

void PendingRequestLauncher::Commit( std::shared_ptr<PendingCallbackRequest> request,
									 const CefRefPtr<CefV8Context> &context,
									 CefRefPtr<CefProcessMessage> message,
									 CefRefPtr<CefV8Value> &retVal,
									 CefString &exception ) {
	const int id = request->Id();

#ifndef PUBLIC_BUILD
	// Sanity checks
	auto messageArgs( message->GetArgumentList() );
	if( messageArgs->GetSize() < 1 ) {
		const char *format = "%s: Commit(): Sanity check failed: An outgoing message %s has an empty args list";
		Logger()->Error( format, logTag.c_str(), message->GetName().ToString().c_str() );
	}
	if( messageArgs->GetInt( 0 ) != id ) {
		const char *format = "%s: Sanity check failed: A first argument of an outgoing message %s is not a callback id";
		Logger()->Error( format, logTag.c_str(), message->GetName().ToString().c_str() );
	}
#endif

	// Both of these calls might theoretically (but very unlikely) fail.
	// Start from that one that is easy to rollback.
	// Note: we can be sure that the reply cannot arrive before this call returns
	// (all JS interaction is performed sequentially in the renderer thread)
	parent->callbacks[id] = request;
	bool succeeded = false;
	try {
		context->GetBrowser()->SendProcessMessage( PID_BROWSER, message );
		succeeded = true;
	} catch( ... ) {
		parent->callbacks.erase( id );
	}
	if( succeeded ) {
		retVal = CefV8Value::CreateNull();
	} else {
		exception = "Can't send a message to the browser process";
	}
}

inline bool PendingRequestLauncher::TryGetString( const CefRefPtr<CefV8Value> &jsValue,
												  const char *tag,
												  CefString &value,
												  CefString &exception ) {
	if( !jsValue->IsString() ) {
		exception = std::string( "The value of argument `" ) + tag + "` is not a string";
		return false;
	}

	value = jsValue->GetStringValue();
	return true;
}

inline bool PendingRequestLauncher::ValidateCallback( const CefRefPtr<CefV8Value> &jsValue, CefString &exception ) {
	if( !jsValue->IsFunction() ) {
		exception = "The value of the last argument that is supposed to be a callback is not a function";
		return false;
	}

	return true;
}

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
	auto message( CefProcessMessage::Create( "getCVar" ) );
	auto messageArgs( message->GetArgumentList() );

	auto request( NewRequest( context, arguments.back() ) );

	messageArgs->SetInt( 0, request->Id() );
	messageArgs->SetString( 1, name );
	messageArgs->SetString( 2, defaultValue );
	messageArgs->SetInt( 3, flags );

	Commit( std::move( request ), context, message, retval, exception );
}

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

void GetDemosAndSubDirsRequestLauncher::StartExec( const CefV8ValueList &args,
												   CefRefPtr<CefV8Value> &retval,
												   CefString &exception ) {
	if( args.size() != 2 ) {
		exception = "Illegal arguments list size, there must be two arguments";
		return;
	}

	CefString dir;
	if( !TryGetString( args[0], "dir", dir, exception ) ) {
		return;
	}
	if( !ValidateCallback( args.back(), exception ) ) {
		return;
	}

	auto context( CefV8Context::GetCurrentContext() );
	auto request( NewRequest( context, args.back() ) );

	auto message( NewMessage() );
	auto messageArgs( message->GetArgumentList() );
	messageArgs->SetInt( 0, request->Id() );
	messageArgs->SetString( 1, dir );

	Commit( std::move( request ), context, message, retval, exception );
}

void GetDemoMetaDataRequestLauncher::StartExec( const CefV8ValueList &jsArgs,
												CefRefPtr<CefV8Value> &retVal,
												CefString &exception ) {
	if( jsArgs.size() != 2 ) {
		exception = "Illegal arguments list size, there must be two arguments";
		return;
	}

	CefString path;
	if( !TryGetString( jsArgs[0], "path", path, exception ) ) {
		return;
	}

	// TODO: Validate path, JS is very error-prone

	if( !ValidateCallback( jsArgs.back(), exception ) ) {
		return;
	}

	auto context( CefV8Context::GetCurrentContext() );
	auto request( NewRequest( context, jsArgs.back() ) );

	auto message( NewMessage() );
	auto messageArgs( message->GetArgumentList() );
	messageArgs->SetInt( 0, request->Id() );
	messageArgs->SetString( 1, path );

	Commit( std::move( request ), context, message, retVal, exception );
}

template <typename Request>
void TypedPendingRequestLauncher<Request>::DefaultSingleArgStartExecImpl( const CefV8ValueList &jsArgs,
																		  CefRefPtr<CefV8Value> &retVal,
																		  CefString &exception ) {
	if( jsArgs.size() != 1 ) {
		exception = "Illegal arguments list size, there must be a single argument";
		return;
	}

	if( !ValidateCallback( jsArgs.back(), exception ) ) {
		return;
	}

	auto context( CefV8Context::GetCurrentContext() );
	auto request( NewRequest( context, jsArgs.back() ) );
	auto message( NewMessage() );
	message->GetArgumentList()->SetInt( 0, request->Id() );

	Commit( std::move( request ), context, message, retVal, exception );
}

void GetHudsRequestLauncher::StartExec( const CefV8ValueList &jsArgs, CefRefPtr<CefV8Value> &retVal, CefString &ex ) {
	return DefaultSingleArgStartExecImpl( jsArgs, retVal, ex );
}

void GetGametypesRequestLauncher::StartExec( const CefV8ValueList &jsArgs, CefRefPtr<CefV8Value> &retVal, CefString &ex ) {
	return DefaultSingleArgStartExecImpl( jsArgs, retVal, ex );
}

void GetMapsRequestLauncher::StartExec( const CefV8ValueList &jsArgs, CefRefPtr<CefV8Value> &retVal, CefString &ex ) {
	return DefaultSingleArgStartExecImpl( jsArgs, retVal, ex );
}

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

void GetCVarRequest::FireCallback( CefRefPtr<CefProcessMessage> reply ) {
	auto args( reply->GetArgumentList() );
	size_t numArgs = args->GetSize();
	if( numArgs != 3 ) {
		ReportNumArgsMismatch( numArgs, "3" );
		return;
	}

	ExecuteCallback( { CefV8Value::CreateString( args->GetString( 1 ) ) } );
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

void ExecuteCmdRequest::FireCallback( CefRefPtr<CefProcessMessage> reply ) {
	auto args( reply->GetArgumentList() );
	if( args->GetSize() != 1 ) {
		ReportNumArgsMismatch( 1, "1" );
		return;
	}

	ExecuteCallback( CefV8ValueList() );
}

void GetVideoModesRequest::FireCallback( CefRefPtr<CefProcessMessage> reply ) {
	auto args( reply->GetArgumentList() );
	const size_t numArgs = args->GetSize();
	if( numArgs < 3 || ( numArgs % 2 == 0 ) ) {
		ReportNumArgsMismatch( numArgs, "at least 3, an odd value" );
		return;
	}

	CefStringBuilder stringBuilder;
	auto argPrinter = []( CefStringBuilder &sb, CefRefPtr<CefListValue> &args, size_t argNum ) {
		sb << args->GetInt( argNum );
	};
	ArrayOfPairsBuildHelper buildHelper( stringBuilder, "width", "height", argPrinter );
	ExecuteCallback( { CefV8Value::CreateString( stringBuilder.ReleaseOwnership() ) } );
}

void GetDemosAndSubDirsRequest::FireCallback( CefRefPtr<CefProcessMessage> reply ) {
	auto args( reply->GetArgumentList() );
	const size_t numArgs = args->GetSize();
	if( numArgs < 3 ) {
		ReportNumArgsMismatch( numArgs, "at least 3" );
		return;
	}

	size_t argNum = 1;
	CefV8ValueList callbackArgs;
	CefStringBuilder stringBuilder;
	for( int arrayGroup = 0; arrayGroup < 2; ++arrayGroup ) {
		stringBuilder.Clear();
		ArrayBuildHelper buildHelper( stringBuilder );
		size_t numGroupArgs = (size_t)args->GetInt( argNum++ );
		buildHelper.PrintArgs( args, argNum, numGroupArgs );
		argNum += numGroupArgs;
		callbackArgs.emplace_back( CefV8Value::CreateString( stringBuilder.ReleaseOwnership() ) );
	}

	ExecuteCallback( callbackArgs );
}

void GetDemoMetaDataRequest::FireCallback( CefRefPtr<CefProcessMessage> reply ) {
	auto args( reply->GetArgumentList() );
	size_t numArgs = args->GetSize();
	if( numArgs < 1 || !( numArgs % 2 ) ) {
		ReportNumArgsMismatch( numArgs, "at least 1, an odd value" );
		return;
	}

	// Escape keys in quotes... not sure if this protects from whitespaces in key names
	CefStringBuilder stringBuilder;
	auto printer = AggregateBuildHelper::QuotedStringPrinter( '\"' );
	ObjectBuildHelper buildHelper( stringBuilder, printer, printer );
	buildHelper.PrintArgs( args, 1, numArgs - 1 );
	ExecuteCallback( { CefV8Value::CreateString( stringBuilder.ReleaseOwnership() ) } );
}

void GetHudsRequest::FireCallback( CefRefPtr<CefProcessMessage> reply ) {
	auto args( reply->GetArgumentList() );
	size_t numArgs = args->GetSize();
	if( numArgs < 1 ) {
		ReportNumArgsMismatch( numArgs, "at least 1" );
		return;
	}

	CefStringBuilder stringBuilder;
	ArrayBuildHelper buildHelper( stringBuilder );
	buildHelper.PrintArgs( args, 1, numArgs - 1 );
	ExecuteCallback( { CefV8Value::CreateString( stringBuilder.ReleaseOwnership() ) } );
}

void GetGametypesRequest::FireCallback( CefRefPtr<CefProcessMessage> reply ) {
	auto args( reply->GetArgumentList() );
	size_t numArgs = args->GetSize();
	if( numArgs < 1 ) {
		ReportNumArgsMismatch( numArgs, "at least 1" );
		return;
	}

	CefStringBuilder stringBuilder;
	ObjectBuildHelper buildHelper( stringBuilder );
	buildHelper.PrintArgs( args, 1, numArgs - 1 );
	ExecuteCallback( { CefV8Value::CreateString( stringBuilder.ReleaseOwnership() ) } );
}

void GetMapsRequest::FireCallback( CefRefPtr<CefProcessMessage> reply ) {
	auto args( reply->GetArgumentList() );
	size_t numArgs = args->GetSize();
	if( numArgs < 1 ) {
		ReportNumArgsMismatch( numArgs, "at least 1" );
		return;
	}

	CefStringBuilder stringBuilder;
	ArrayOfPairsBuildHelper buildHelper( stringBuilder, "short", "full" );
	buildHelper.PrintArgs( args, 1, numArgs - 1 );
	ExecuteCallback( { CefV8Value::CreateString( stringBuilder.ReleaseOwnership() ) } );
}

void GetLocalizedStringsRequest::FireCallback( CefRefPtr<CefProcessMessage> reply ) {
	auto args( reply->GetArgumentList() );
	size_t numArgs = args->GetSize();
	if( numArgs < 1 ) {
		ReportNumArgsMismatch( numArgs, "at least 1" );
		return;
	}

	CefStringBuilder stringBuilder;
	ObjectBuildHelper buildHelper( stringBuilder );
	buildHelper.PrintArgs( args, 1, numArgs - 1 );
	ExecuteCallback( { CefV8Value::CreateString( stringBuilder.ReleaseOwnership() ) } );
}

bool WswCefV8Handler::Execute( const CefString& name,
							   CefRefPtr<CefV8Value> object,
							   const CefV8ValueList& arguments,
							   CefRefPtr<CefV8Value>& retval,
							   CefString& exception ) {
	CEF_REQUIRE_RENDERER_THREAD();

	for( PendingRequestLauncher *launcher = requestLaunchersHead; launcher; launcher = launcher->Next() ) {
		if( !launcher->Method().compare( name ) ) {
			Logger()->Debug( "Found a launcher %s for a request", launcher->LogTag().c_str() );
			launcher->StartExec( arguments, retval, exception );
			return true;
		}
	}

	if( !name.compare( "notifyUiPageReady" ) ) {
		retval = CefV8Value::CreateNull();
		auto message( CefProcessMessage::Create( "uiPageReady" ) );
		CefV8Context::GetCurrentContext()->GetBrowser()->SendProcessMessage( PID_BROWSER, message );
		return true;
	}

	Logger()->Error( "Can't handle unknown JS call %s\n", name.ToString().c_str() );
	return false;
}

bool WswCefV8Handler::CheckForReply( const CefString &name, CefRefPtr<CefProcessMessage> message ) {
	for( PendingRequestLauncher *launcher = requestLaunchersHead; launcher; launcher = launcher->Next() ) {
		// Just check the name match ensuring this really is a callback.
		// The implementation could be faster but the total number of message kinds is insignificant.
		if( !launcher->Method().compare( name ) ) {
			ProcessAsAwaitedReply( message );
			return true;
		}
	}

	return false;
}

void WswCefV8Handler::ProcessAsAwaitedReply( CefRefPtr<CefProcessMessage> &message ) {
	auto args( message->GetArgumentList() );
	if( args->GetSize() < 1 ) {
		std::string name( message->GetName().ToString() );
		Logger()->Error( "Empty arguments list for a message `%s` that is awaited for firing a callback", name.c_str() );
		return;
	}

	const int id = args->GetInt( 0 );
	// These non-atomic operations should be safe as all JS interaction is performed in the renderer thread
	auto iter = callbacks.find( id );
	if( iter == callbacks.end() ) {
		std::string name( message->GetName().ToString() );
		Logger()->Error( "Can't find a callback by id %d for a message `%s`", id, name.c_str() );
		return;
	}

	( *iter ).second->FireCallback( message );
	callbacks.erase( iter );
}