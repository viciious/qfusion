#include "CameraAnimParser.h"

static const CefString originField( "origin" );
static const CefString anglesField( "angles" );
static const CefString timestampField( "timestamp" );
static const CefString fovField( "fov" );

bool CameraAnimParser::Parse( std::vector<CameraAnimFrame> &frames, bool looping, CefString &exception ) {
	assert( frames.empty() );

	const int arrayLength = framesArray->GetArrayLength();
	if( !arrayLength ) {
		exception = "No anim frames are specified";
		return false;
	}

	for( int i = 0, end = arrayLength; i < end; ++i ) {
		// TODO: Get rid of allocations here
		CefString elemScope = scope.ToString() + "[" + std::to_string( i ) + "]";

		auto elemValue( framesArray->GetValue( i ) );
		if( !elemValue->IsObject() ) {
			exception = "A value of `" + scope.ToString() + "` array element #" + std::to_string( i ) + " is not an object";
			return false;
		}

		CameraAnimFrame frame;

		ObjectFieldsGetter getter( elemValue );
		if( !getter.GetVec3( originField, frame.origin, exception, elemScope )) {
			return false;
		}
		if( !getter.GetVec3( anglesField, frame.angles, exception, elemScope )) {
			return false;
		}

		if( !getter.GetUInt( timestampField, &frame.timestamp, exception, elemScope )) {
			return false;
		}

		if( getter.ContainsField( fovField ) ) {
			if( !getter.GetFloat( fovField, &frame.fov, exception, elemScope ) ) {
				return false;
			}
		} else {
			frame.fov = CameraAnimFrame::DEFAULT_FOV;
		}

		frames.emplace_back( frame );
		if( !ValidateAddedFrame( frames, exception ) ) {
			return false;
		}
	}

	if( looping ) {
		return ValidateLoopingAnim( frames, exception );
	}

	return true;
}

bool CameraAnimParser::ValidateAddedFrame( const std::vector<CameraAnimFrame> &frames, CefString &exception ) {
	assert( !frames.empty() );
	if( frames.size() == 1 ) {
		if( frames.front().timestamp ) {
			exception = "The timestamp must be zero for the first frame";
			return false;
		}
		return true;
	}

	// Notice casts in the RHS... otherwise the subtraction is done on unsigned numbers
	const int64_t delta = (int64_t)frames.back().timestamp - (int64_t)frames[frames.size() - 2].timestamp;
	const char *error = nullptr;
	if( delta <= 0 ) {
		error = " violates monotonic increase contract";
	} else if( delta < 16 ) {
		error = " is way too close to the previous one (should be at least 16 millis ahead)";
	} else if( delta > (int64_t) ( 1u << 20u ) ) {
		error = " is way too far from the previous one (is this a numeric error?)";
	}

	if( error ) {
		exception = "The timestamp for frame #" + std::to_string( frames.size() - 1 ) + error;
		return false;
	}

	return true;
}

bool CameraAnimParser::ValidateLoopingAnim( const std::vector<CameraAnimFrame> &frames, CefString &exception ) {
	if( frames.size() == 1 ) {
		return true;
	}

	if( !VectorCompare( frames.front().origin, frames.back().origin ) ) {
		exception = "An origin must match for the first and last loop frames";
		return false;
	}

	if( !VectorCompare( frames.front().angles, frames.back().angles ) ) {
		exception = "Angles must match for the first and last loop frames";
		return false;
	}

	if( frames.front().fov != frames.back().fov ) {
		exception = "A fov must match for the first and last loop frames";
		return false;
	}

	return true;
}