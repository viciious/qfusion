#include "CefApp.h"
#include "UiFacade.h"
#include "Api.h"

#include "include/wrapper/cef_helpers.h"

#include <chrono>

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
		"})();";

	v8Handler = CefRefPtr<WswCefV8Handler>( new WswCefV8Handler );
	if( !CefRegisterExtension( "v8/gameUi", code, v8Handler ) ) {
		// TODO: We do not have a browser instance at this moment
	}
}

void WswCefRenderProcessHandler::SendLogMessage( CefRefPtr<CefBrowser> browser, const CefString &message ) {
	auto processMessage( CefProcessMessage::Create( "log" ) );
	auto messageArgs( processMessage->GetArgumentList() );
	messageArgs->SetString( 0, message );
	messageArgs->SetInt( 1, LOGSEVERITY_INFO );
	browser->SendProcessMessage( PID_BROWSER, processMessage );
}

bool WswCefRenderProcessHandler::OnProcessMessageReceived( CefRefPtr<CefBrowser> browser,
														   CefProcessId source_process,
														   CefRefPtr<CefProcessMessage> message ) {
	const CefString &name = message->GetName();
	if( !name.compare( "command" ) ) {
		ExecuteJavascript( browser, MakeGameCommandCall( message ) );
		return true;
	}

	// Got confirmation for command execution request
	if( !name.compare( "executeCmd" ) ) {
		v8Handler->FireExecuteCmdCallback( message );
		return true;
	}

	// Got reply for a JS-initiated call
	if( !name.compare( "getCVar" ) ) {
		v8Handler->FireGetCVarCallback( message );
		return true;
	}

	// Got reply for a JS-initiated call
	if( !name.compare( "setCVar" ) ) {
		v8Handler->FireSetCVarCallback( message );
		return true;
	}

	SendLogMessage( browser, std::string( "Unexpected message name `" ) + name.ToString() + '`' );
	return false;
}

std::string WswCefRenderProcessHandler::MakeGameCommandCall( CefRefPtr<CefProcessMessage> &message ) {
	auto messageArgs( message->GetArgumentList() );
	size_t numArgs = messageArgs->GetSize();

	CefString s;
	std::string result;
	// TODO: We can compute an exact amount using 2 passes and precache GetString() result calls
	// but this is pointless as there will be many other allocations in CefString -> Std::string conversion
	result.reserve( numArgs * 32u + 32 );
	result += "ui.onGameCommand.apply(null, [";
	for( size_t i = 0; i < numArgs; ++i ) {
		result += '"';
		result += messageArgs->GetString( i ).ToString();
		result += '"';
		result += ',';
	}
	if( numArgs ) {
		// Chop the last comma
		result.pop_back();
	}
	result += "])";
	return result;
}

std::string WswCefRenderProcessHandler::DescribeException( const std::string &code, CefRefPtr<CefV8Exception> exception ) {
	std::stringstream s;
	s << "An execution of `" << code << "` has failed with exception: ";
	s << "message=`" << exception->GetMessage().ToString() << "`,";
	s << "line=" << exception->GetLineNumber() << ",";
	s << "column=" << exception->GetStartColumn();
	return s.str();
}

bool WswCefRenderProcessHandler::ExecuteJavascript( CefRefPtr<CefBrowser> browser, const std::string &code ) {
	auto frame = browser->GetMainFrame();
	auto context = frame->GetV8Context();
	if( !context->Enter() ) {
		return false;
	}

	CefRefPtr<CefV8Value> retVal;
	CefRefPtr<CefV8Exception> exception;
	if( context->Eval( code, frame->GetURL(), 0, retVal, exception ) ) {
		if( !context->Exit() ) {
			SendLogMessage( browser, "Warning: context->Exit() call failed after successful Eval() call" );
		}
		return true;
	}

	SendLogMessage( browser, DescribeException( code, exception ) );
	if( !context->Exit() ) {
		SendLogMessage( browser, "Warning: context->Exit() call failed after unsuccessful Eval() call" );
	}
	return true;
}

void WswCefClient::ReplyForGetCVarRequest( CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> ingoing ) {
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

void WswCefClient::ReplyForSetCVarRequest( CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> ingoing ) {
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

	auto outgoing( CefProcessMessage::Create( "setCVar" ) );
	outgoing->GetArgumentList()->SetInt( 0, id );
	if( force ) {
		outgoing->GetArgumentList()->SetBool( 1, forced );
	}

	browser->SendProcessMessage( PID_RENDERER, outgoing );
}

void WswCefClient::ReplyForExecuteCmdRequest( CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> ingoing ) {
	auto ingoingArgs( ingoing->GetArgumentList() );
	const int id = ingoingArgs->GetInt( 0 );

	api->Cmd_ExecuteText( ingoingArgs->GetInt( 1 ), ingoingArgs->GetString( 2 ).ToString().c_str() );

	auto outgoing( CefProcessMessage::Create( "executeCmd" ) );
	outgoing->GetArgumentList()->SetInt( 0, id );
	browser->SendProcessMessage( PID_RENDERER, outgoing );
}

bool WswCefClient::OnProcessMessageReceived( CefRefPtr<CefBrowser> browser,
											 CefProcessId source_process,
											 CefRefPtr<CefProcessMessage> processMessage ) {
	CEF_REQUIRE_UI_THREAD();

	auto name( processMessage->GetName() );
	if( !name.compare( "log" ) ) {
		auto messageArgs( processMessage->GetArgumentList() );
		std::string message( messageArgs->GetString( 0 ).ToString() );
		//cef_log_severity_t severity = (cef_log_severity_t)messageArgs->GetInt( 1 );
		// TODO: Use the severity for console character codes output
		printf("Log message from browser process: \n%s\n", message.c_str() );
		return true;
	}

	if( !name.compare( "executeCmd" ) ) {
		ReplyForExecuteCmdRequest( browser, processMessage );
		return true;
	}

	if( !name.compare( "uiPageReady" ) ) {
		UiFacade::Instance()->OnUiPageReady();
		return true;
	}

	if( !name.compare( "getCVar" ) ) {
		ReplyForGetCVarRequest( browser, processMessage );
		return true;
	}

	if( !name.compare( "setCVar" ) ) {
		ReplyForSetCVarRequest( browser, processMessage );
		return true;
	}

	return false;
}

inline bool WswCefV8Handler::TryGetString( const CefRefPtr<CefV8Value> &jsValue,
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

inline bool WswCefV8Handler::ValidateCallback( const CefRefPtr<CefV8Value> &jsValue, CefString &exception ) {
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

void WswCefV8Handler::PostGetCVarRequest( const CefV8ValueList &arguments,
										  CefRefPtr<CefV8Value> &retval,
										  CefString &exception ) {
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
	const int id = NextCallId();
	callbacks[id] = std::make_pair( context, arguments.back() );

	auto message( CefProcessMessage::Create( "getCVar" ) );
	auto messageArgs( message->GetArgumentList() );
	messageArgs->SetInt( 0, id );
	messageArgs->SetString( 1, name );
	messageArgs->SetString( 2, defaultValue );
	messageArgs->SetInt( 3, flags );

	retval = CefV8Value::CreateNull();
	context->GetBrowser()->SendProcessMessage( PID_BROWSER, message );
}

void WswCefV8Handler::PostSetCVarRequest( const CefV8ValueList &arguments,
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
	const int id = NextCallId();
	callbacks[id] = std::make_pair( context, arguments.back() );

	auto message( CefProcessMessage::Create( "setCVar" ) );
	auto messageArgs( message->GetArgumentList() );
	messageArgs->SetInt( 0, id );
	messageArgs->SetString( 1, name );
	messageArgs->SetString( 2, value );
	if( forceSet ) {
		messageArgs->SetBool( 3, forceSet );
	}

	retval = CefV8Value::CreateNull();
	context->GetBrowser()->SendProcessMessage( PID_BROWSER, message );
}

void WswCefV8Handler::PostExecuteCmdRequest( const CefV8ValueList &arguments,
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
	const int id = NextCallId();
	callbacks[id] = std::make_pair( context, arguments.back() );

	auto message( CefProcessMessage::Create( "executeCmd" ) );
	auto messageArgs( message->GetArgumentList() );
	messageArgs->SetInt( 0, id );
	messageArgs->SetInt( 1, whence );
	messageArgs->SetString( 2, text );

	retval = CefV8Value::CreateNull();
	context->GetBrowser()->SendProcessMessage( PID_BROWSER, message );
}

bool WswCefV8Handler::TryUnregisterCallback( int id, CefRefPtr<CefV8Context> &context, CefRefPtr<CefV8Value> &callback ) {
	auto it = callbacks.find( id );
	if( it != callbacks.end() ) {
		context = ( *it ).second.first;
		callback = ( *it ).second.second;
		callbacks.erase( it );
		return true;
	}

	// TODO: Report an error...
	return false;
}

void WswCefV8Handler::FireGetCVarCallback( CefRefPtr<CefProcessMessage> reply ) {
	auto args( reply->GetArgumentList() );
	if( args->GetSize() != 3 ) {
		// TODO: Report an error...
		return;
	}

	CefRefPtr<CefV8Context> context;
	CefRefPtr<CefV8Value> callback;
	if( !TryUnregisterCallback( args->GetInt( 0 ), context, callback ) ) {
		return;
	}

	CefV8ValueList callbackArgs;
	callbackArgs.emplace_back( CefV8Value::CreateString( args->GetString( 1 ) ) );
	if( !callback->ExecuteFunctionWithContext( context, nullptr, callbackArgs ).get() ) {
		// TODO: Report execution error
		return;
	}
}

void WswCefV8Handler::FireSetCVarCallback( CefRefPtr<CefProcessMessage> reply ) {
	auto args( reply->GetArgumentList() );
	auto size = args->GetSize();
	if( size != 1 && size != 2 ) {
		// TODO: Report an error...
		return;
	}

	CefRefPtr<CefV8Context> context;
	CefRefPtr<CefV8Value> callback;
	if( !TryUnregisterCallback( args->GetInt( 0 ), context, callback ) ) {
		return;
	}

	CefV8ValueList callbackArgs;
	if( size == 2 ) {
		callbackArgs.emplace_back( CefV8Value::CreateBool( args->GetBool( 1 ) ) );
	}
	if( !callback->ExecuteFunctionWithContext( context, nullptr, callbackArgs ).get() ) {
		// TODO: Report execution error
		return;
	}
}

void WswCefV8Handler::FireExecuteCmdCallback( CefRefPtr<CefProcessMessage> reply ) {
	auto args( reply->GetArgumentList() );
	if( args->GetSize() != 1 ) {
		// TODO: Report an error...
		return;
	}

	CefRefPtr<CefV8Context> context;
	CefRefPtr<CefV8Value> callback;
	if( !TryUnregisterCallback( args->GetInt( 0 ), context, callback ) ) {
		return;
	}

	CefV8ValueList callbackArgs;
	if( !callback->ExecuteFunctionWithContext( context, nullptr, callbackArgs ).get() ) {
		// TODO: Report execution error
		return;
	}
}

bool WswCefV8Handler::Execute( const CefString& name,
							   CefRefPtr<CefV8Value> object,
							   const CefV8ValueList& arguments,
							   CefRefPtr<CefV8Value>& retval,
							   CefString& exception ) {
	if( !name.compare( "executeCmd" ) ) {
		PostExecuteCmdRequest( arguments, retval, exception );
		return true;
	}

	if( !name.compare( "notifyUiPageReady" ) ) {
		retval = CefV8Value::CreateNull();
		auto message( CefProcessMessage::Create( "uiPageReady" ) );
		CefV8Context::GetCurrentContext()->GetBrowser()->SendProcessMessage( PID_BROWSER, message );
		return true;
	}

	if( !name.compare( "getCVar" ) ) {
		PostGetCVarRequest( arguments, retval, exception );
		return true;
	}

	if( !name.compare( "setCVar" ) ) {
		PostSetCVarRequest( arguments, retval, exception );
		return true;
	}

	return false;
}