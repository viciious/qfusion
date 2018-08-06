#include "CameraAnimator.h"

void CameraAnimator::ResetWithFrames( const CameraAnimFrame *framesBegin, const CameraAnimFrame *framesEnd, bool looping_ ) {
	assert( framesEnd > framesBegin );

	if( looping_ ) {
		// First and last frames must have the same parameters except the timestamp
		assert( VectorCompare( framesBegin[0].origin, framesEnd[-1].origin ) );
		assert( VectorCompare( framesBegin[0].angles, framesEnd[-1].angles ) );
		assert( framesBegin[0].fov == framesEnd[-1].fov );
	}

	this->looping = looping_;
	this->currFrameNum = 0;
	this->currAnimTime = 0;

	// Provide initial values for immediate use
	VectorCopy( framesBegin->origin, currOrigin );
	AnglesToAxis( framesBegin->angles, currAxis );
	this->currFov = framesBegin->fov;

	// Convert given frames to internal frames
	this->frames.clear();
	this->frames.reserve( framesEnd - framesBegin );
	for( const auto *framePtr = framesBegin; framePtr != framesEnd; ++framePtr ) {
		InternalFrame frame;
		VectorCopy( framePtr->origin, frame.origin );
		frame.timestamp = framePtr->timestamp;
		frame.fov = framePtr->fov;
		mat3_t m;
		AnglesToAxis( framePtr->angles, m );
		Quat_FromMatrix3( m, frame.orientation );
		this->frames.emplace_back( frame );
	}
}

void CameraAnimator::Refresh( int64_t rendererTime ) {
	// Do not update anything where there is no actual animation
	if( frames.size() <= 1 ) {
		return;
	}

	int64_t delta = rendererTime - this->lastRefreshAt;
	assert( delta >= 0 );
	// Disallow huge hops
	clamp_high( delta, 64 );

	unsigned newAnimTime = currAnimTime + (unsigned)delta;
	if( looping && newAnimTime > frames.back().timestamp ) {
		newAnimTime = newAnimTime % frames.back().timestamp;
		currFrameNum = 0;
		currAnimTime = 0;
	}

	int nextFrameNum = currFrameNum;
	// Find a frame that has a timestamp >= newAnimTime
	for(;;) {
		if( nextFrameNum == frames.size() ) {
			break;
		}
		if( frames[nextFrameNum].timestamp >= newAnimTime ) {
			break;
		}
		currFrameNum = nextFrameNum;
		nextFrameNum++;
	}

	// If we have found a next frame
	if( nextFrameNum < frames.size() ) {
		assert( currFrameNum != nextFrameNum );
		float frac = newAnimTime - frames[currFrameNum].timestamp;
		frac *= 1.0f / ( frames[nextFrameNum].timestamp - frames[currFrameNum].timestamp );
		SetInterpolatedValues( frames[currFrameNum], frames[nextFrameNum], frac );
		currAnimTime = newAnimTime;
		return;
	}

	SetInterpolatedValues( frames.back(), frames.back(), 1.0f );
	currAnimTime = frames.back().timestamp;
}

void CameraAnimator::SetInterpolatedValues( const InternalFrame &from,
											const InternalFrame &to,
											float frac ) {
	VectorLerp( from.origin, frac, to.origin, this->currOrigin );
	quat_t orientation;
	Quat_Lerp( from.orientation, to.orientation, frac, orientation );
	Quat_ToMatrix3( orientation, this->currAxis );
	this->currFov = from.fov + frac * ( to.fov - from.fov );
}
