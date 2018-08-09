#ifndef UI_CEF_SYSCALLS_LOCAL_H
#define UI_CEF_SYSCALLS_LOCAL_H

#include "../Api.h"
#include "../Ipc.h"
#include "../CefApp.h"
#include "../UiFacade.h"

// Hack for (temporarily) downgraded CEF distribution
#define TID_FILE_BACKGROUND ( TID_FILE )

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

	std::vector<std::string> Exec( const char *dir ) {
		DirectoryWalker::Exec( dir );

		// Clear the current buffer and at the same time return the temporary buffer by moving
		std::vector<std::string> retVal;
		result.swap( retVal );
		return retVal;
	}

	void ConsumeEntry( const char *p, size_t len, const char *lastDot ) override {
		if( stripExtension && lastDot ) {
			len = (size_t)( lastDot - p );
		}
		result.emplace_back( std::string( p, len ) );
	}
};

inline size_t WriteVec( CefListValue *argsList, size_t argNum, const float *vector, int size ) {
	for( int i = 0; i < size; ++i ) {
		argsList->SetDouble( argNum++, vector[i] );
	}
	return argNum;
}

inline size_t ReadVec( CefListValue *argsList, size_t argNum, float *vector, int size ) {
	for( int i = 0; i < size; ++i ) {
		vector[i] = (vec_t)argsList->GetDouble( argNum++ );
	}
	return argNum;
}

inline size_t WriteVec3( CefListValue *argsList, size_t argNum, const vec3_t vector ) {
	return WriteVec( argsList, argNum, vector, 3 );
}

inline size_t ReadVec3( CefListValue *argsList, size_t argNum, vec3_t vector ) {
	return ReadVec( argsList, argNum, vector, 3 );
}

inline size_t WriteVec2( CefListValue *argsList, size_t argNum, const vec2_t vector ) {
	return WriteVec( argsList, argNum, vector, 2 );
}

inline size_t ReadVec2( CefListValue *argsList, size_t argNum, vec2_t vector ) {
	return ReadVec( argsList, argNum, vector, 2 );
}

size_t WriteCameraAnim( CefListValue *argsList, size_t argNum, bool looping, const std::vector<CameraAnimFrame> &frames );

size_t ReadCameraAnim( CefListValue *argsList, size_t argNum, bool *looping, std::vector<CameraAnimFrame> &frames );

#endif
