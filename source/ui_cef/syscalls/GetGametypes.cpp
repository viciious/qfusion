#include "SyscallsLocal.h"

void GetGametypesRequestLauncher::StartExec( const CefV8ValueList &jsArgs, CefRefPtr<CefV8Value> &retVal, CefString &ex ) {
	return DefaultSingleArgStartExecImpl( jsArgs, retVal, ex );
}

typedef std::vector<std::pair<std::string, std::string>> GametypesList;

class GametypesRetrievalHelper: public DirectoryWalker {
	GametypesList result;

	void ParseFile( const char *contents );
	inline const char *SkipWhile( const char *contents, const std::function<bool(char)> &fn );
public:
	GametypesRetrievalHelper(): DirectoryWalker( ".gt" ) {}

	GametypesList Exec();

	void ConsumeEntry( const char *p, size_t, const char *lastDot ) override;
};

GametypesList GametypesRetrievalHelper::Exec() {
	DirectoryWalker::Exec( "progs/gametypes" );

	GametypesList retVal;
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

class PostGametypesTask: public IOPendingCallbackRequestTask {
	GametypesList gametypes;
public:
	PostGametypesTask( FSPendingCallbackRequestTask *parent, GametypesList &&gametypes_ )
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
		return AsCefPtr( new PostGametypesTask( this, GametypesRetrievalHelper().Exec() ) );
	}

	IMPLEMENT_REFCOUNTING( GetGametypesTask );
};

void GetGametypesRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> ingoing ) {
	CefPostTask( TID_FILE_BACKGROUND, AsCefPtr( new GetGametypesTask( browser, ingoing ) ) );
}

void GetGametypesRequest::FireCallback( CefRefPtr<CefProcessMessage> reply ) {
	FireSingleArgAggregateCallback<ObjectBuildHelper>( reply );
}
