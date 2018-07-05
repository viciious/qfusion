#include "SyscallsLocal.h"

void GetHudsRequestLauncher::StartExec( const CefV8ValueList &jsArgs, CefRefPtr<CefV8Value> &retVal, CefString &ex ) {
	return DefaultSingleArgStartExecImpl( jsArgs, retVal, ex );
}

typedef std::vector<std::string> FilesList;

static FilesList FindHuds() {
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

class PostHudsTask: public IOPendingCallbackRequestTask {
	FilesList huds;
public:
	PostHudsTask( FSPendingCallbackRequestTask *parent, FilesList &&huds_ )
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
		return AsCefPtr( new PostHudsTask( this, FindHuds() ) );
	}

	IMPLEMENT_REFCOUNTING( GetHudsTask );
};

void GetHudsRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> ingoing ) {
	CefPostTask( TID_FILE_BACKGROUND, AsCefPtr( new GetHudsTask( browser, ingoing ) ) );
}

void GetHudsRequest::FireCallback( CefRefPtr<CefProcessMessage> reply ) {
	FireSingleArgAggregateCallback<ArrayBuildHelper>( reply );
}