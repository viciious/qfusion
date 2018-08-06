#ifndef UI_CEF_VIEW_ANIMATOR_H
#define UI_CEF_VIEW_ANIMATOR_H

#include "../gameshared/q_math.h"
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <vector>

struct CameraAnimFrame {
	static constexpr float DEFAULT_FOV = 90.0f;

	vec3_t origin;
	vec3_t angles;
	float fov;
	unsigned timestamp;
};

class CameraAnimator {
	struct InternalFrame {
		vec3_t origin;
		quat_t orientation;
		float fov;
		unsigned timestamp;
	};

	std::vector<InternalFrame> frames;
	int64_t lastRefreshAt { 0 };

	vec3_t currOrigin;
	mat3_t currAxis;
	float currFov;

	int currFrameNum { 0 };
	unsigned currAnimTime { 0 };
	bool looping { false };

	void ResetWithFrames( const CameraAnimFrame *framesBegin, const CameraAnimFrame *framesEnd, bool looping_ );

	void SetInterpolatedValues( const InternalFrame &from, const InternalFrame &to, float frac );
public:
	inline const float *Origin() const { return currOrigin; }
	inline const float *Axis() const { return currAxis; }
	inline float Fov() const { return currFov; }

	void ResetWithSequence( const CameraAnimFrame *framesBegin, const CameraAnimFrame *framesEnd ) {
		ResetWithFrames( framesBegin, framesEnd, false );
	}

	void ResetWithLoop( const CameraAnimFrame *framesBegin, const CameraAnimFrame *framesEnd ) {
		ResetWithFrames( framesBegin, framesEnd, true );
	}

	void Refresh( int64_t rendererTime );
};

#endif