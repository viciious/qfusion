#include "SyscallsLocal.h"
#include "ObjectFieldsGetter.h"
#include "CameraAnimParser.h"

static const CefString mapField( "map" );
static const CefString blurredField( "blurred" );
static const CefString seqField( "animSeq" );
static const CefString loopField( "animLoop" );

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

	const CefString *animFieldName = nullptr;
	auto animArrayField( CameraAnimParser::FindAnimField( fieldsGetter, seqField, loopField, &animFieldName, exception ) );
	if( !animArrayField ) {
		return;
	}

	std::vector<CameraAnimFrame> animFrames;
	CameraAnimParser parser( animArrayField, *animFieldName );
	const bool isAnimLooping = ( animFieldName == &loopField );
	if( !parser.Parse( animFrames, animFieldName == &loopField, exception ) ) {
		return;
	}

	auto context( CefV8Context::GetCurrentContext() );
	auto request( NewRequest( context, jsArgs.back() ) );

	auto message( NewMessage() );
	auto messageArgs( message->GetArgumentList() );
	size_t argNum = 0;

	messageArgs->SetString( argNum++, map );
	messageArgs->SetBool( argNum++, blurred );
	WriteCameraAnim( messageArgs, argNum, isAnimLooping, animFrames );

	Commit( std::move( request ), context, message, retVal, exception );
}

size_t WriteCameraAnim( CefListValue *argsList, size_t argNum, bool looping, const std::vector<CameraAnimFrame> &frames ) {
	argsList->SetBool( argNum++, looping );
	argsList->SetInt( argNum++, (int)frames.size() );
	for( const auto &frame: frames ) {
		argNum = WriteVec3( argsList, argNum, frame.origin );
		argNum = WriteVec3( argsList, argNum, frame.lookAt );
		// TODO: Use compressed representation for fields below?
		argsList->SetInt( argNum++, (int)frame.fov );
		argsList->SetInt( argNum++, (int)frame.timestamp );
	}
	return argNum;
}

size_t ReadCameraAnim( CefListValue *argsList, size_t argNum, bool *looping, std::vector<CameraAnimFrame> &frames ) {
	assert( frames.empty() );
	*looping = argsList->GetBool( argNum++ );
	const int numFrames = argsList->GetInt( argNum++ );
	for( int i = 0; i < numFrames; ++i ) {
		CameraAnimFrame frame;
		argNum = ReadVec3( argsList, argNum, frame.origin );
		argNum = ReadVec3( argsList, argNum, frame.lookAt );
		frame.fov = (float)argsList->GetInt( argNum++ );
		frame.timestamp = (unsigned)argsList->GetInt( argNum++ );
		frames.emplace_back( frame );
	}
	return argNum;
}

void DrawWorldModelRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser,
												   CefRefPtr<CefProcessMessage> ingoing ) {
	auto ingoingArgs( ingoing->GetArgumentList() );
	size_t argNum = 0;
	const std::string map( ingoingArgs->GetString( argNum++ ).ToString() );
	const bool blurred = ingoingArgs->GetBool( argNum++ );

	bool looping = false;
	std::vector<CameraAnimFrame> frames;
	ReadCameraAnim( ingoingArgs, argNum, &looping, frames );

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