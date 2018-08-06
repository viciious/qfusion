#ifndef UI_CEF_ANIM_FRAMES_PARSER_H
#define UI_CEF_ANIM_FRAMES_PARSER_H

#include "ObjectFieldsGetter.h"
#include "../CameraAnimator.h"

/**
 * A helper for parsing and validation camera anim frames from a JS array
 */
class CameraAnimParser {
	CefRefPtr<CefV8Value> &framesArray;
	const CefString &scope;

	bool ValidateAddedFrame( const std::vector<CameraAnimFrame> &frames, CefString &exception );
	bool ValidateLoopingAnim( const std::vector<CameraAnimFrame> &frames, CefString &exception );
public:
	CameraAnimParser( CefRefPtr<CefV8Value> &framesArray_, const CefString &scope_ )
		: framesArray( framesArray_ ), scope( scope_ ) {}

	bool Parse( std::vector<CameraAnimFrame> &frames, bool looping, CefString &exception );
};

#endif
