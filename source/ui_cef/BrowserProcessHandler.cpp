#include "BrowserProcessHandler.h"
#include "CefApp.h"
#include "CefClient.h"
#include "Api.h"

class WswCefResourceHandler;

class OpenFileTask: public CefTask {
	const std::string &filePath;
	WswCefResourceHandler *parent;

	void NotifyOfResult( int filePointer, int fileSize );
public:
	OpenFileTask( WswCefResourceHandler *parent_, const std::string &filePath_ )
		: filePath( filePath_ ), parent( parent_ ) {}

	void Execute() override;

	IMPLEMENT_REFCOUNTING( OpenFileTask );
};

class NotifyOfOpenFileTask: public CefTask {
	WswCefResourceHandler *const parent;
	const int filePointer;
	const int fileSize;
public:
	NotifyOfOpenFileTask( WswCefResourceHandler *parent_, int filePointer_, int fileSize_ )
		: parent( parent_ ), filePointer( filePointer_ ), fileSize( fileSize_ ) {}

	void Execute() override;

	IMPLEMENT_REFCOUNTING( NotifyOfOpenFileTask );
};

class ReadChunkTask: public CefTask {
	WswCefResourceHandler *const parent;
	void *const buffer;
	const int filePointer;
	const int bytesToRead;
	const int seekTo;

	void NotifyOfResult( int result );
public:
	ReadChunkTask( WswCefResourceHandler *parent_, int filePointer_, void *buffer_, int bytesToRead_, int seekTo_ = -1 ):
		parent( parent_ ), buffer( buffer_), filePointer( filePointer_ ), bytesToRead( bytesToRead_ ), seekTo( seekTo_ ) {}

	void Execute() override;

	IMPLEMENT_REFCOUNTING( ReadChunkTask );
};

class NotifyOfChunkReadTask: public CefTask {
	WswCefResourceHandler *const parent;
	const int result;
public:
	NotifyOfChunkReadTask( WswCefResourceHandler *parent_, int result_ )
		: parent( parent_ ), result( result_ ) {}

	void Execute() override;

	IMPLEMENT_REFCOUNTING( NotifyOfChunkReadTask );
};

class WswCefResourceHandler: public CefResourceHandler {
	std::string validationError;
	std::string filePath;
	CefRefPtr<CefCallback> processRequestCallback;
	CefRefPtr<CefCallback> readResponseCallback;
	// An address of a literal
	const char *mime { nullptr };
	int filePointer { -1 };
	int fileSize { 0 };
	int readResult { -1 };
	int parsedRangeStart { -1 };
	int parsedRangeEnd { -1 };
	int realRangeStart { -1 };
	int realRangeEnd { -1 };
	int seekTo { -1 };
	bool isAGetRequest { true };

	static const char *schema;
	static const int schemaOffset;

	const std::string ValidateUrl( const std::string &url );
	const std::string TrySaveMime( const char *ext );
	const std::string TrySaveRequestRange( const std::string &rawRange );

	void FailWith( CefRefPtr<CefResponse> response, const std::string statusText, int statusCode, cef_errorcode_t cefCode ) {
		FailWith( response, statusText.c_str(), statusCode, cefCode );
	}

	void FailWith( CefRefPtr<CefResponse> response, const char *statusText, int statusCode, cef_errorcode_t cefCode ) {
		response->SetStatusText( statusText );
		response->SetStatus( statusCode );
		response->SetError( cefCode );
	}
public:
	~WswCefResourceHandler() override {
		if( filePointer >= 0 ) {
			api->FS_FCloseFile( filePointer );
		}
	}

	bool ProcessRequest( CefRefPtr<CefRequest> request, CefRefPtr<CefCallback> callback ) override;

	void GetResponseHeaders( CefRefPtr<CefResponse> response, int64 &response_length, CefString &redirectUrl ) override;
	bool ReadResponse( void* data_out, int bytes_to_read, int &bytes_read, CefRefPtr<CefCallback> callback ) override;

	bool CanGetCookie( const CefCookie & ) override { return false; }
	bool CanSetCookie( const CefCookie & ) override { return false; }

	void Cancel() override {
		if( processRequestCallback ) {
			processRequestCallback->Cancel();
			processRequestCallback = nullptr;
		}
		if( readResponseCallback ) {
			readResponseCallback->Cancel();
			readResponseCallback = nullptr;
		}
	}

	void NotifyOfOpenResult( int filePointer_, int fileSize_ ) {
		CEF_REQUIRE_IO_THREAD();
		if( !processRequestCallback ) {
			return;
		}

		this->filePointer = filePointer_;
		this->fileSize = fileSize_;
		processRequestCallback->Continue();
	}

	void NotifyOfReadResult( int result_ ) {
		CEF_REQUIRE_IO_THREAD();
		this->readResult = result_;
		if( !this->readResponseCallback ) {
			return;
		}
		if( this->readResult >= 0 ) {
			this->readResponseCallback->Continue();
		} else {
			this->readResponseCallback->Cancel();
		}
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
	this->validationError = ValidateUrl( url );
	if( !this->validationError.empty() ) {
		callback->Continue();
		return true;
	}

	CefRequest::HeaderMap headerMap;
	request->GetHeaderMap( headerMap );
	if( !headerMap.empty() ) {
		auto iter = headerMap.find( CefString( "Range" ) );
		if( iter != headerMap.end() ) {
			this->validationError = TrySaveRequestRange( iter->second.ToString() );
			if( !this->validationError.empty() ) {
				callback->Continue();
				return true;
			}
		}
	}

	assert( filePath.empty() );
	filePath += "ui/";
	filePath += ( url.c_str() + schemaOffset );

	// Save the callback that will be activated after reading the file contents
	processRequestCallback = callback;
	// Launch reading a file in a file IO thread...
	CefPostTask( TID_FILE, AsCefPtr( new OpenFileTask( this, filePath ) ) );
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

const std::string WswCefResourceHandler::TrySaveRequestRange( const std::string &rawRange ) {
	const char *s = rawRange.c_str();
	while ( isspace( *s ) ) {
		s++;
	}

	if( strstr( s, "bytes" ) != s )	{
		return "Only bytes are supported for the Range header units";
	}

	s += strlen( "bytes" );
	while( isspace( *s ) ) {
		s++;
	}

	if( *s != '=' ) {
		return "`bytes` unit must be followed by `=`";
	}

	s++;
	while( isspace( *s ) ) {
		s++;
	}

	char *endptr;
	auto start = strtoul( s, &endptr, 10 );
	if( ( start == ULONG_MAX && errno == ERANGE ) || start > INT_MAX ) {
		return "Malformed range start, only positive 32-bit integer numbers are allowed";
	}
	this->parsedRangeStart = (int)start;

	if( *endptr != '-' ) {
		return "Malformed range, a hyphen is expected as a separator, got " + std::string( endptr );
	}

	s = endptr + 1;
	if( !*s ) {
		// Should not allocate
		return std::string();
	}

	auto end = strtoul( s, &endptr, 10 );
	if( ( end == ULONG_MAX && errno == ERANGE ) || end > INT_MAX ) {
		return "Malformed range end, only positive 32-bit integer numbers are allowed";
	}
	this->parsedRangeEnd = (int)end;

	if( parsedRangeStart > parsedRangeEnd ) {
		return "Malformed range, start is greater than the end";
	}

	if( *endptr ) {
		return "Only a single document part can be specified";
	}

	return std::string();
}

void WswCefResourceHandler::GetResponseHeaders( CefRefPtr<CefResponse> response,
												int64 &response_length,
												CefString &redirectUrl ) {
	response_length = 0;

	// We have deferred this until we could return a response in well-known way
	if( !isAGetRequest ) {
		FailWith( response, "Only GET requests are allowed", 400, ERR_METHOD_NOT_SUPPORTED );
	}

	if( !validationError.empty() ) {
		FailWith( response, validationError, 400, ERR_INVALID_URL );
		return;
	}

	if( filePointer < 0 ) {
		FailWith( response, "Cannot open the file in the game filesystem", 404, ERR_FILE_NOT_FOUND );
		return;
	}

	response->SetMimeType( mime );
	// There was no range specified, return with 200
	if( parsedRangeStart < 0 && parsedRangeEnd < 0 ) {
		response->SetStatus( 200 );
		response->SetStatusText( "OK" );
		response_length = fileSize;
		return;
	}

	realRangeStart = parsedRangeStart < 0 ? 0 : parsedRangeStart;
	if( realRangeStart >= fileSize ) {
		FailWith( response, "The range start is greater than the file size", 400, ERR_INVALID_ARGUMENT );
		return;
	}

	realRangeEnd = parsedRangeEnd < 0 ? parsedRangeStart + 64 * 1024 : parsedRangeEnd;
	if( realRangeEnd >= fileSize ) {
		realRangeEnd = std::max( 0, fileSize - 1 );
	}

	seekTo = realRangeStart;

	CefResponse::HeaderMap headerMap;
	std::stringstream contentRange;
	contentRange << realRangeStart << "-" << realRangeEnd << "/" << fileSize;
	headerMap.emplace( std::make_pair( CefString( "Content-Range" ), CefString( contentRange.str() ) ) );
	headerMap.emplace( std::make_pair( CefString( "Accept-Ranges" ), CefString( "bytes" ) ) );
	headerMap.emplace( std::make_pair( CefString( "Content-Length" ), CefString( std::to_string( fileSize ) ) ) );
	response->SetHeaderMap( headerMap );
	response->SetStatus( 206 );
	response->SetStatusText( "Partial content" );
	response_length = fileSize;
}

bool WswCefResourceHandler::ReadResponse( void *data_out,
										  int bytes_to_read,
										  int &bytes_read,
										  CefRefPtr<CefCallback> callback ) {
	if( this->readResponseCallback ) {
		this->readResponseCallback = nullptr;
		// ReadResponse() must not be called if the request has been canceled once we were notified about read results
		assert( this->readResult >= 0 );
		// Notify we have read these bytes
		bytes_read = this->readResult;
		// Return immediately. Return false to terminate if there is no bytes left
		return this->readResult > 0;
	}

	// Schedule a task
	this->readResponseCallback = callback;
	CefPostTask( TID_FILE, AsCefPtr( new ReadChunkTask( this, filePointer, data_out, bytes_to_read, seekTo ) ) );
	seekTo = -1;
	// Wait for reading result
	bytes_read = 0;
	return true;
}

void OpenFileTask::NotifyOfResult( int filePointer, int fileSize ) {
	CefPostTask( TID_IO, AsCefPtr( new NotifyOfOpenFileTask( parent, filePointer, fileSize ) ) );
}

void OpenFileTask::Execute() {
	CEF_REQUIRE_FILE_THREAD();

	int filePointer = -1;
	int maybeFileSize = api->FS_FOpenFile( filePath.c_str(), &filePointer, FS_READ );
	NotifyOfResult( filePointer, maybeFileSize );
}

void NotifyOfOpenFileTask::Execute() {
	parent->NotifyOfOpenResult( filePointer, fileSize );
}

void ReadChunkTask::NotifyOfResult( int result ) {
	CefPostTask( TID_IO, AsCefPtr( new NotifyOfChunkReadTask( parent, result ) ) );
}

void ReadChunkTask::Execute() {
	CEF_REQUIRE_FILE_THREAD();

	if( seekTo >= 0 ) {
		if( api->FS_Seek( filePointer, seekTo, FS_SEEK_SET ) < 0 ) {
			NotifyOfResult( -1 );
			return;
		}
	}

	int result = api->FS_Read( buffer, (size_t)bytesToRead, filePointer );
	NotifyOfResult( result );
}

void NotifyOfChunkReadTask::Execute() {
	parent->NotifyOfReadResult( this->result );
}

class WswCefSchemeHandlerFactory: public CefSchemeHandlerFactory {
public:
	CefRefPtr<CefResourceHandler> Create( CefRefPtr<CefBrowser> browser,
										  CefRefPtr<CefFrame> frame,
										  const CefString& scheme_name,
										  CefRefPtr<CefRequest> request ) override {
		return CefRefPtr<CefResourceHandler>( new WswCefResourceHandler );
	}

	IMPLEMENT_REFCOUNTING( WswCefSchemeHandlerFactory );
};

void WswCefBrowserProcessHandler::OnContextInitialized() {
	CefRefPtr<CefSchemeHandlerFactory> schemeHandlerFactory( new WswCefSchemeHandlerFactory );
	CefRegisterSchemeHandlerFactory( "ui", "ignored domain name", schemeHandlerFactory );

	CefWindowInfo info;
	info.SetAsWindowless( 0 );
	CefBrowserSettings settings;
	CefRefPtr<WswCefClient> client( new WswCefClient( width, height ) );
	CefString url( "ui://index.html" );
	CefBrowserHost::CreateBrowserSync( info, client, url, settings, nullptr );
}
