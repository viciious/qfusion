#include "BestJumpableSpotDetector.h"
#include "MovementLocal.h"

unsigned BestJumpableSpotDetector::Exec( const vec3_t startOrigin_, vec3_t spotOrigin_ ) {
	VectorCopy( startOrigin_, this->startOrigin );
	this->startOrigin[2] += 4.0f;

	constexpr auto badCmContents = CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_DONOTENTER;

	// Make sure we can spot missing initialization easily
	SpotAndScore *spotsBegin = nullptr, *spotsEnd = nullptr;
	GetCandidateSpots( &spotsBegin, &spotsEnd );

#ifndef PUBLIC_BUILD
	if( !std::is_heap( spotsBegin, spotsEnd ) ) {
		AI_FailWith( "BestJumpableSpotsDetector::Exec()", "A given spots range must point to a max-heap\n" );
	}
#endif

	while( spotsBegin != spotsEnd ) {
		// Evict the best candidate from the heap
		std::pop_heap( spotsBegin, spotsEnd );
		const SpotAndScore *spotAndScore = spotsEnd - 1;
		spotsEnd--;

		vec3_t velocityForJumping;
		GetVelocityForJumpingToSpot( velocityForJumping, spotAndScore->origin );

		predictionResults.Clear();
		auto predictionStopEvent = predictor.Run( velocityForJumping, startOrigin, &predictionResults );

		// A bot has entered an area of "bad" contents
		if( predictionStopEvent & AiTrajectoryPredictor::ENTER_AREA_CONTENTS ) {
			continue;
		}

		if( predictionStopEvent & ( AiTrajectoryPredictor::HIT_SOLID | AiTrajectoryPredictor::HIT_LIQUID ) ) {
			if( !ISWALKABLEPLANE( &predictionResults.trace->plane ) ) {
				continue;
			}
			if( predictionResults.trace->contents & badCmContents ) {
				continue;
			}
		} else {
			// Do a ground sampling
			trace_t trace;
			Vec3 traceEnd( predictionResults.origin );
			traceEnd.Z() -= 64.0f;
			StaticWorldTrace( &trace, predictionResults.origin, traceEnd.Data(), MASK_SOLID | MASK_WATER );
			if( trace.fraction == 1.0f ) {
				continue;
			}
			if( !ISWALKABLEPLANE( &trace.plane ) ) {
				continue;
			}
			if( trace.contents & badCmContents ) {
				continue;
			}
		}

		VectorCopy( spotAndScore->origin, spotOrigin_ );

#ifndef PUBLIC_BUILD
		// Make sure the returned timeout value is feasible
		if( predictionResults.millisAhead > 9999 ) {
			AI_FailWith( "BestJumpableSpotsDetector::Exec()", "Don't extrapolate last step (we need a valid timeout)\n" );
		}
#endif
		return predictionResults.millisAhead;
	}

	return 0;
}

void BestRegularJumpableSpotDetector::GetVelocityForJumpingToSpot( vec3_t velocity, const vec3_t spot ) {
	VectorSubtract( spot, startOrigin, velocity );
	velocity[2] = 0;
	float scale2D =  run2DSpeed / sqrtf( SQUARE( velocity[0] ) + SQUARE( velocity[1] ) );
	velocity[0] *= scale2D;
	velocity[1] *= scale2D;
	velocity[2] = jumpZSpeed;
}

unsigned BestRegularJumpableSpotDetector::Exec( const vec3_t startOrigin_, vec3_t spotOrigin_ ) {
#ifndef PUBLIC_BUILD
	if( run2DSpeed <= 0 || jumpZSpeed <= 0 ) {
		AI_FailWith( "BestRegularJumpableSpotsDetector::Exec()", "Illegal jump physics props (have they been set?)\n" );
	}
#endif

	// Cannot be initialized in constructor called for the global instance
	this->aasWorld = AiAasWorld::Instance();
	unsigned result = BestJumpableSpotDetector::Exec( startOrigin_, spotOrigin_ );
	run2DSpeed = 0.0f;
	jumpZSpeed = 0.0f;

	// Nulliffy supplied references to avoid unintended reusing
	this->routeCache = nullptr;
	this->navTargetAreaNum = 0;
	this->startTravelTimeToTarget = 0;
	return result;
}