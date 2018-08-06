#include "SyscallsLocal.h"
#include "ObjectFieldsGetter.h"
#include "CameraAnimParser.h"

static const CefString mapField( "map" );
static const CefString blurredField( "blurred" );
static const CefString animSeqField( "animSeq" );
static const CefString animLoopField( "animLoop" );

inline size_t WriteVec3( CefListValue *argsList, size_t argNum, vec3_t vector ) {
	for( int i = 0; i < 3; ++i ) {
		argsList->SetDouble( argNum++, vector[i] );
	}
	return argNum;
}

inline size_t ReadVec3( CefListValue *argsList, size_t argNum, vec3_t vector ) {
	for( int i = 0; i < 3; ++i ) {
		vector[i] = (vec_t)argsList->GetDouble( argNum++ );
	}
	return argNum;
}

void DrawWorldModelRequestLauncher::StartExec( const CefV8ValueList &jsArgs,
											   CefRefPtr<CefV8Value> &retVal,
											   CefString &exception ) {
	if( jsArgs.size() != 2 ) {
		exception = "Illegal arguments list size, expected 2";
		return;
	}

	if( !ValidateCallback( jsArgs[1], exception ) ) {
		return;
	}

	auto paramsObject( jsArgs[0] );
	if( !paramsObject->IsObject() ) {
		exception = "The first argument must be an object containing the call parameters";
		return;
	}

	ObjectFieldsGetter fieldsGetter( paramsObject );

	CefString map;
	if( !fieldsGetter.GetString( mapField, map, exception ) ) {
		return;
	}

	// Try doing a minimal validation here, JS is very error-prone
	std::string testedMap( map );
	if( testedMap.size() < 4 || strcmp( testedMap.data() + testedMap.size() - 4, ".bsp" ) != 0 ) {
		exception = "A \".bsp\" extension of the map is expected";
		return;
	}

	if( testedMap.find( '/' ) != std::string::npos ) {
		exception = "Specify just a map name and not an absolute path (it is assumed to be found in maps/)";
		return;
	}

	bool blurred = false;
	if( fieldsGetter.ContainsField( blurredField ) ) {
		if( !fieldsGetter.GetBool( blurredField, &blurred, exception ) ) {
			return;
		}
	}

	const bool isAnimSeqPresent = fieldsGetter.ContainsField( animSeqField );
	const bool isAnimLoopPresent = fieldsGetter.ContainsField( animLoopField );
	if( isAnimSeqPresent && isAnimLoopPresent ) {
		exception = "Both " + animSeqField.ToString() + " and " + animLoopField.ToString() + " fields are present";
		return;
	}
	if( !isAnimSeqPresent && !isAnimLoopPresent ) {
		exception = "Neither " + animSeqField.ToString() + ", nor " + animLoopField.ToString() + " fields are present";
	}

	const CefString *animFieldName;
	CefRefPtr<CefV8Value> animArrayValue;
	if( isAnimSeqPresent ) {
		if( !fieldsGetter.GetArray( animSeqField, animArrayValue, exception ) ) {
			return;
		}
		animFieldName = &animSeqField;
	} else {
		if( !fieldsGetter.GetArray( animLoopField, animArrayValue, exception ) ) {
			return;
		}
		animFieldName = &animLoopField;
	}

	std::vector<CameraAnimFrame> animFrames;
	CameraAnimParser parser( animArrayValue, *animFieldName );
	if( !parser.Parse( animFrames, isAnimLoopPresent, exception ) ) {
		return;
	}

	auto context( CefV8Context::GetCurrentContext() );
	auto request( NewRequest( context, jsArgs.back() ) );

	auto message( NewMessage() );
	auto messageArgs( message->GetArgumentList() );
	size_t argNum = 0;

	messageArgs->SetString( argNum++, map );
	messageArgs->SetBool( argNum++, blurred );
	messageArgs->SetBool( argNum++, isAnimLoopPresent );
	messageArgs->SetInt( argNum++, (int)animFrames.size() );
	for( auto &frame: animFrames ) {
		argNum = WriteVec3( messageArgs, argNum, frame.origin );
		// TODO: Use compressed representation for fields below?
		argNum = WriteVec3( messageArgs, argNum, frame.angles );
		messageArgs->SetInt( argNum++, (int)frame.fov );
		messageArgs->SetInt( argNum++, frame.timestamp );
	}

	Commit( std::move( request ), context, message, retVal, exception );
}

void DrawWorldModelRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser,
												   CefRefPtr<CefProcessMessage> ingoing ) {
	auto ingoingArgs( ingoing->GetArgumentList() );
	size_t argNum = 0;
	const std::string map( ingoingArgs->GetString( argNum++ ).ToString() );
	const bool blurred = ingoingArgs->GetBool( argNum++ );
	const bool looping = ingoingArgs->GetBool( argNum++ );
	int numFrames = ingoingArgs->GetInt( argNum++ );
	std::vector<CameraAnimFrame> frames;
	for( int i = 0; i < numFrames; ++i ) {
		CameraAnimFrame frame;
		argNum = ReadVec3( ingoingArgs, argNum, frame.origin );
		argNum = ReadVec3( ingoingArgs, argNum, frame.angles );
		frame.fov = ingoingArgs->GetInt( argNum++ );
		frame.timestamp = (unsigned)ingoingArgs->GetInt( argNum++ );
		frames.emplace_back( frame );
	}

	auto outgoing( NewMessage() );

	std::string mapPath("maps/");
	mapPath += map;

	// Validate map presence. This is tricky as "ui" map is not listed in the maplist.
	if( api->FS_FileMTime( mapPath.c_str() ) <= 0 ) {
		outgoing->GetArgumentList()->SetString( 0, "no such map " + mapPath );
		browser->SendProcessMessage( PID_RENDERER, outgoing );
	}

	UiFacade::Instance()->StartShowingWorldModel( mapPath.c_str(), blurred, looping, frames );
	outgoing->GetArgumentList()->SetString( 0, "success" );
	browser->SendProcessMessage( PID_RENDERER, outgoing );
}

void DrawWorldModelRequest::FireCallback( CefRefPtr<CefProcessMessage> message ) {
	ExecuteCallback( { CefV8Value::CreateString( message->GetArgumentList()->GetString( 0 ) ) } );
}