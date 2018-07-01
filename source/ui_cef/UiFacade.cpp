#include "UiFacade.h"
#include "CefApp.h"
#include "Api.h"

#include "../qcommon/version.warsow.h"

UiFacade *UiFacade::instance = nullptr;

bool UiFacade::Init( int argc, char **argv, void *hInstance, int width_, int height_,
					 int demoProtocol_, const char *demoExtension_, const char *basePath_ ) {
	// Create an instance first.
	// It is expected to be present when various CEF initialization callbacks are fired.
	instance = new UiFacade( width_, height_, demoProtocol_, demoExtension_, basePath_ );

	if( !InitCef( argc, argv, hInstance ) ) {
		delete instance;
		return false;
	}

	return true;
}

bool UiFacade::InitCef( int argc, char **argv, void *hInstance ) {
	CefMainArgs mainArgs( argc, argv );

	CefRefPtr<WswCefApp> app( new WswCefApp );

	CefSettings settings;
	CefString( &settings.locale ).FromASCII( "en_US" );
	settings.multi_threaded_message_loop = false;
	settings.external_message_pump = true;
#ifndef PUBLIC_BUILD
	settings.log_severity = api->Cvar_Value( "developer ") ? LOGSEVERITY_VERBOSE : LOGSEVERITY_DEFAULT;
#else
	settings.log_severity = api->Cvar_Value( "developer" ) ? LOGSEVERITY_VERBOSE : LOGSEVERITY_DISABLE;
#endif

	// Hacks! Unfortunately CEF always expects an absolute path
	std::string realPath( api->Cvar_String( "fs_realpath" ) );
	// TODO: Make sure it's arch/platform-compatible!
	CefString( &settings.browser_subprocess_path ).FromASCII( ( realPath + "/ui_cef_process.x86_64" ).c_str() );
	CefString( &settings.resources_dir_path ).FromASCII( ( realPath + "/cef_resources/" ).c_str() );
	CefString( &settings.locales_dir_path ).FromASCII( ( realPath + "/cef_resources/locales/" ).c_str() );
	// TODO settings.cache_path;
	// TODO settings.user_data_path;
	settings.windowless_rendering_enabled = true;

	return CefInitialize( mainArgs, settings, app.get(), nullptr );
}

UiFacade::UiFacade( int width_, int height_, int demoProtocol_, const char *demoExtension_, const char *basePath_ )
	: renderHandler( nullptr )
	, width( width_ )
	, height( height_ )
	, demoProtocol( demoProtocol_ )
	, demoExtension( demoExtension_ )
	, basePath( basePath_ )
	, messagePipe( this ) {
	api->Cmd_AddCommand( "menu_force", &MenuForceHandler );
	api->Cmd_AddCommand( "menu_open", &MenuOpenHandler );
	api->Cmd_AddCommand( "menu_modal", &MenuModalHandler );
	api->Cmd_AddCommand( "menu_close", &MenuCloseHandler );
}

UiFacade::~UiFacade() {
	CefShutdown();

	api->Cmd_RemoveCommand( "menu_force" );
	api->Cmd_RemoveCommand( "menu_open" );
	api->Cmd_RemoveCommand( "menu_modal" );
	api->Cmd_RemoveCommand( "menu_close" );
}

void UiFacade::MenuCommand() {
	messagePipe.ExecuteCommand( api->Cmd_Argc(), api->Cmd_Argv );
}

void UiFacade::RegisterBrowser( CefRefPtr<CefBrowser> browser_ ) {
	this->browser = browser_;
}

void UiFacade::UnregisterBrowser( CefRefPtr<CefBrowser> browser_ ) {
	this->browser = nullptr;
}

void UiFacade::RegisterRenderHandler( CefRenderHandler *handler_ ) {
	assert( handler_ );
	assert( !renderHandler );
	renderHandler = dynamic_cast<WswCefRenderHandler *>( handler_ );
	assert( renderHandler );
}

inline const char *NullToEmpty( const char *s ) {
	return s ? s : "";
}

void UiFacade::Refresh( int64_t time, int clientState, int serverState,
						bool demoPlaying, const char *demoName, bool demoPaused,
						unsigned int demoTime, bool backGround, bool showCursor ) {
	CefDoMessageLoopWork();

	mainScreenState.Flip();

	auto &state = mainScreenState.Curr();
	state.clientState = clientState;
	state.serverState = serverState;
	state.demoPlaying = demoPlaying;
	state.demoName = NullToEmpty( demoName );
	state.demoPaused = demoPaused;
	state.demoTime = demoTime;
	state.background = backGround;
	state.showCursor = showCursor;

	messagePipe.UpdateMainScreenState( mainScreenState.Prev(), mainScreenState.Curr() );

	DrawUi();
}

void UiFacade::UpdateConnectScreen( const char *serverName, const char *rejectMessage,
									int downloadType, const char *downloadFilename,
									float downloadPercent, int downloadSpeed,
									int connectCount, bool backGround ) {
	CefDoMessageLoopWork();

	connectScreenState.Flip();

	auto &state = connectScreenState.Curr();
	state.serverName = NullToEmpty( serverName );
	state.rejectMessage = NullToEmpty( rejectMessage );
	state.downloadType = downloadType;
	state.downloadFileName = NullToEmpty( downloadFilename );
	state.downloadPercent = downloadPercent;
	state.connectCount = connectCount;
	state.background = backGround;

	messagePipe.UpdateConnectScreenState( connectScreenState.Prev(), connectScreenState.Curr() );

	DrawUi();
}

void UiFacade::DrawUi() {
	auto *uiImage = api->R_RegisterRawPic( "uiBuffer", width, height, renderHandler->drawnEveryFrameBuffer, 4 );
	vec4_t color = { 1.0f, 1.0f, 1.0f, 1.0f };
	api->R_DrawStretchPic( 0, 0, width, height, 0.0f, 0.0f, 1.0f, 1.0f, color, uiImage );
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

class DirectoryWalker {
protected:
	char buffer[1024];
	const char *extension;

	// A directory is specified at the moment of this call, so the object is reusable for many directories
	void Exec( const char *dir_ );
	void ParseBuffer();
	size_t ScanFilename( const char *p, const char **lastDot );
public:
	// Filter options should be specified here
	explicit DirectoryWalker( const char *extension_ )
		: extension( extension_ ) {}

	virtual ~DirectoryWalker() {}
	virtual void ConsumeEntry( const char *p, size_t len, const char *lastDot ) = 0;
};

class StlCompatDirectoryWalker final: public DirectoryWalker {
	std::vector<std::string> result;
	bool stripExtension;
public:
	StlCompatDirectoryWalker( const char *extension_, bool stripExtension_ )
		: DirectoryWalker( extension_ ), stripExtension( extension_ && stripExtension_ ) {};

	std::vector<std::string> Exec( const char *dir );

	void ConsumeEntry( const char *p, size_t len, const char *lastDot ) override {
		if( stripExtension && lastDot ) {
			len = (size_t)( lastDot - p );
		}
		result.emplace_back( std::string( p, len ) );
	}
};

void DirectoryWalker::Exec( const char *dir_ ) {
	int totalFiles = api->FS_GetFileList( dir_, extension, nullptr, 0, 0, 0 );
	for( int startAtFile = 0; startAtFile < totalFiles; ) {
		int numFiles = api->FS_GetFileList( dir_, extension, buffer, sizeof( buffer ), startAtFile, totalFiles );
		if( !numFiles ) {
			// Go to next start file on failure
			startAtFile++;
			continue;
		}
		ParseBuffer();
		startAtFile += numFiles;
	}
}

size_t DirectoryWalker::ScanFilename( const char *p, const char **lastDot ) {
	const char *const oldp = p;
	// Scan for the zero byte, marking the first dot as well
	for(;; ) {
		char ch = *p;
		if( !ch ) {
			break;
		}
		if( ch == '.' ) {
			*lastDot = p;
		}
		p++;
	}
	return (size_t)( p - oldp );
}

void DirectoryWalker::ParseBuffer() {
	size_t len = 0;
	// Hop over the last zero byte on every iteration
	for( const char *p = buffer; p - buffer < sizeof( buffer ); p += len + 1 ) {
		const char *lastDot = nullptr;
		len = ScanFilename( p, &lastDot );
		if( !len ) {
			break;
		}
		// Skip hidden files and directory links
		if( *p == '.' ) {
			continue;
		}
		ConsumeEntry( p, len, lastDot );
	}
}

std::vector<std::string> StlCompatDirectoryWalker::Exec( const char *dir ) {
	DirectoryWalker::Exec( dir );

	// Clear the current buffer and at the same time return the temporary buffer by moving
	std::vector<std::string> retVal;
	result.swap( retVal );
	return retVal;
}

std::pair<UiFacade::FilesList, UiFacade::FilesList> UiFacade::FindDemosAndSubDirs( const std::string &dir ) {
	const char *realDir = dir.empty() ? "demos" : dir.c_str();

	auto findFiles = [&]( const char *ext ) {
		StlCompatDirectoryWalker walker( ext, false );
		return walker.Exec( realDir );
	};

	return std::make_pair( findFiles( APP_DEMO_EXTENSION_STR ), findFiles( "/" ) );
};

UiFacade::FilesList UiFacade::GetHuds() {
	StlCompatDirectoryWalker walker( ".hud", false );
	auto rawFiles = walker.Exec( "huds" );
	// We should not list touch huds now, and should not supply ones as touch screens
	// are not currently supported in CEF ui, but lets prevent listing touch huds anyway.
	std::vector<std::string> result;
	result.reserve( rawFiles.size() );
	for( auto &raw: rawFiles ) {
		if( raw.find( "touch" ) == std::string::npos ) {
			result.emplace_back( std::move( raw ) );
		}
	}
	return result;
}

class GametypesRetrievalHelper: public DirectoryWalker {
	UiFacade::GametypesList result;

	void ParseFile( const char *contents );
	inline const char *SkipWhile( const char *contents, const std::function<bool(char)> &fn );
public:
	GametypesRetrievalHelper(): DirectoryWalker( ".gt" ) {}

	UiFacade::GametypesList Exec();

	void ConsumeEntry( const char *p, size_t, const char *lastDot ) override;
};

UiFacade::GametypesList GametypesRetrievalHelper::Exec() {
	DirectoryWalker::Exec( "progs/gametypes" );

	UiFacade::GametypesList retVal;
	result.swap( retVal );
	return retVal;
}

void GametypesRetrievalHelper::ConsumeEntry( const char *p, size_t, const char *lastDot ) {
	std::string path( "progs/gametypes/" );
	path += std::string( p, lastDot - p );
	path += ".gtd";

	int fp;
	int fileSize = api->FS_FOpenFile( path.c_str(), &fp, FS_READ );
	if( fileSize <= 0 ) {
		return;
	}

	std::unique_ptr<char[]> buffer( new char[fileSize + 1u] );
	int readResult = api->FS_Read( buffer.get(), (size_t)fileSize, fp );
	api->FS_FCloseFile( fp );
	if( readResult != fileSize ) {
		return;
	}

	buffer.get()[fileSize] = 0;
	ParseFile( buffer.get() );
}

inline const char *GametypesRetrievalHelper::SkipWhile( const char *contents, const std::function<bool(char)> &fn ) {
	while( *contents && fn( *contents ) ) {
		contents++;
	}
	return contents;
}

void GametypesRetrievalHelper::ParseFile( const char *contents ) {
	const char *title = SkipWhile( contents, ::isspace );
	if( !*title ) {
		return;
	}

	const char *desc = SkipWhile( title, [](char c) { return c != '\r' && c != '\n'; } );
	size_t titleLen = desc - title;
	if( !titleLen ) {
		return;
	}

	desc = SkipWhile( desc, isspace );
	if( !*desc ) {
		return;
	}

	// This part requires some special handling
	const char *p = desc;
	const char *lastNonSpaceChar = desc;
	for(; *p; p++ ) {
		if( !isspace( *p ) ) {
			lastNonSpaceChar = p;
		}
	}

	size_t descLen = lastNonSpaceChar - desc + 1;
	result.emplace_back( std::make_pair( std::string( title, titleLen ), std::string( desc, descLen ) ) );
}

std::vector<std::pair<std::string, std::string>> UiFacade::GetGametypes() {
	return GametypesRetrievalHelper().Exec();
}

UiFacade::DemoMetaData UiFacade::GetDemoMetaData( const std::string &path ) {
	char localBuffer[2048];
	std::unique_ptr<char[]> allocationHolder;
	char *metaData = localBuffer;

	size_t realSize = api->CL_ReadDemoMetaData( path.c_str(), localBuffer, sizeof( localBuffer ) );
	if( realSize > sizeof( localBuffer ) ) {
		std::unique_ptr<char[]> allocated( new char[realSize] );
		metaData = allocated.get();

		// Check whether we have read the same data (might have been modified)
		if( api->CL_ReadDemoMetaData( path.c_str(), metaData, realSize ) != realSize ) {
			return std::vector<std::pair<std::string, std::string>>();
		}

		allocationHolder.swap( allocated );
	}

	return ParseDemoMetaData( metaData, realSize );
}

UiFacade::DemoMetaData UiFacade::ParseDemoMetaData( const char *p, size_t size ) {
	class ResultBuilder {
		const char *tokens[2];
		size_t lens[2];
		int index { 0 };

		DemoMetaData result;
	public:
		void ConsumeToken( const char *token, size_t len ) {
			std::tie( tokens[index], lens[index] ) = std::make_pair( token, len );
			if( ( index++ ) < 1 ) {
				return;
			}
			result.emplace_back( std::make_pair( std::string( tokens[0], lens[0] ), std::string( tokens[1], lens[1] ) ) );
			index = 0;
		}
		DemoMetaData Result() { return std::move( result ); }
	};

	ResultBuilder builder;

	const char *const endp = p + size;
	for( size_t len = 0; p < endp; p += len + 1 ) {
		len = strlen( p );
		builder.ConsumeToken( p, len );
	}

	return builder.Result();
}