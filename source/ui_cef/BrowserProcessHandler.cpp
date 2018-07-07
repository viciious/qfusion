#include "BrowserProcessHandler.h"
#include "CefApp.h"
#include "CefClient.h"
#include "Api.h"

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
