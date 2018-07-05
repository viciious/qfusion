#include "SyscallsLocal.h"

// For demo protocol version
#include "../../qcommon/version.warsow.h"

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

typedef std::vector<std::string> FilesList;

class PostDemosAndSubDirsTask: public IOPendingCallbackRequestTask {
	FilesList demos, subDirs;
public:
	PostDemosAndSubDirsTask( FSPendingCallbackRequestTask *parent, FilesList &&demos_, FilesList &&subDirs_ )
		: IOPendingCallbackRequestTask( parent ), demos( demos_ ), subDirs( subDirs_ ) {}

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

	IMPLEMENT_REFCOUNTING( PostDemosAndSubDirsTask );
};

class GetDemosAndSubDirsTask: public FSPendingCallbackRequestTask {
	std::string dir;
public:
	GetDemosAndSubDirsTask( CefRefPtr<CefBrowser> browser, int callId, std::string &&dir_ )
		: FSPendingCallbackRequestTask( browser, callId ), dir( dir_ ) {}

	CefRefPtr<IOPendingCallbackRequestTask> CreatePostResultsTask() override {
		const char *realDir = dir.empty() ? "demos" : dir.c_str();
		auto findFiles = [&]( const char *ext ) {
			StlCompatDirectoryWalker walker( ext, false );
			return walker.Exec( realDir );
		};
		return AsCefPtr( new PostDemosAndSubDirsTask( this, findFiles( APP_DEMO_EXTENSION_STR ), findFiles( "/" ) ) );
	}

	IMPLEMENT_REFCOUNTING( GetDemosAndSubDirsTask );
};

void GetDemosAndSubDirsRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser,
													   CefRefPtr<CefProcessMessage> ingoing ) {
	auto ingoingArgs( ingoing->GetArgumentList() );
	const int callId = ingoingArgs->GetInt( 1 );
	std::string dir( ingoingArgs->GetString( 2 ) );
	CefPostTask( TID_FILE_BACKGROUND, AsCefPtr( new GetDemosAndSubDirsTask( browser, callId, std::move( dir ) ) ) );
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

