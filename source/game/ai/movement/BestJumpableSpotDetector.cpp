#include "BestJumpableSpotDetector.h"
#include "MovementLocal.h"

typedef BestJumpableSpotDetector::SpotAndScore SpotAndScore;

const SpotAndScore *BestJumpableSpotDetector::Exec( const vec3_t startOrigin_, unsigned *millis ) {
	VectorCopy( startOrigin_, this->startOrigin );
	this->startOrigin[2] += 4.0f;

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
		GetVelocityForJumpingToSpot( velocityForJumping, spotAndScore );

		predictionResults.Clear();
		predictor.SetEnterAreaNum( testSpotAreaNums ? spotAndScore->areaNum : 0 );
		auto predictionStopEvent = predictor.Run( velocityForJumping, startOrigin, &predictionResults );

		// A bot has entered an area of "bad" contents
		if( predictionStopEvent & AiTrajectoryPredictor::ENTER_AREA_CONTENTS ) {
			continue;
		}

		if( !( predictionStopEvent & AiTrajectoryPredictor::ENTER_AREA_NUM ) ) {
			if( !InspectBumpingPoint( spotAndScore ) ) {
				continue;
			}
		}

		if( millis ) {
			*millis = predictionResults.millisAhead;
		}

#ifndef PUBLIC_BUILD
		// Make sure the returned timeout value is feasible
		if( predictionResults.millisAhead > 9999 ) {
			AI_FailWith( "BestJumpableSpotsDetector::Exec()", "Don't extrapolate last step (we need a valid timeout)\n" );
		}
#endif
		return spotAndScore;
	}

	return nullptr;
}

bool BestJumpableSpotDetector::InspectBumpingPoint( const SpotAndScore *spotAndScore ) const {
	constexpr auto badCmContents = CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_DONOTENTER;
	if( predictionResults.trace->contents & badCmContents ) {
		return false;
	}

	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const auto *aasReach = aasWorld->Reachabilities();

	trace_t trace;

	if( predictor.StopEventFlags() >= AiTrajectoryPredictor::ENTER_AREA_NUM ) {
		if( spotAndScore->areaNum > 0 ) {
			const int floorClusterNum = aasWorld->FloorClusterNum( spotAndScore->areaNum );
			if( floorClusterNum && floorClusterNum == aasWorld->FloorClusterNum( predictionResults.lastAreaNum ) ) {
				if( Distance2DSquared( spotAndScore->origin, predictionResults.origin ) < SQUARE( 192.0f ) ) {
					return true;
				}
			}
		}
	}

	vec3_t originForMissTest;
	if( ISWALKABLEPLANE( &predictionResults.trace->plane ) ) {
		// If areas were tested and an area has been set in prediction results
		if( predictor.StopEventFlags() >= AiTrajectoryPredictor::ENTER_AREA_NUM ) {
			// Try testing whether the spot area is reachable from this area.
			// Otherwise these tests are way too restrictive.
			const auto &areaSettings = aasAreaSettings[predictionResults.lastAreaNum];
			int reachNum = areaSettings.firstreachablearea;
			const int endReachNum = reachNum + areaSettings.numreachableareas;
			for(; reachNum != endReachNum; ++reachNum ) {
				if( aasReach[reachNum].areanum != spotAndScore->tag ) {
					continue;
				}
				int travelType = ( aasReach[reachNum].traveltype & TRAVELTYPE_MASK );
				if( ( travelType == TRAVEL_WALK ) || ( travelType == TRAVEL_WALKOFFLEDGE ) ) {
					return true;
				}
			}
		}

		VectorAdd( predictionResults.trace->endpos, predictionResults.trace->plane.normal, originForMissTest );
	} else {
		// Reject a-priori in this case
		if( DistanceSquared( predictionResults.trace->endpos, spotAndScore->origin ) > SQUARE( 128 ) ) {
			return false;
		}

		Vec3 traceStart( predictionResults.trace->plane.normal );
		traceStart *= 4.0f;
		traceStart += predictionResults.trace->endpos;
		Vec3 traceEnd( traceStart );
		traceEnd.Z() -= 64.0f;

		StaticWorldTrace( &trace, traceStart.Data(), traceEnd.Data(), MASK_SOLID | MASK_WATER );

		if( trace.fraction == 1.0f ) {
			return false;
		}
		if( !ISWALKABLEPLANE( &trace.plane ) ) {
			return false;
		}
		if( trace.contents & badCmContents ) {
			return false;
		}

		VectorAdd( trace.endpos, trace.plane.normal, originForMissTest );
	}

	const float squareDistance = DistanceSquared( spotAndScore->origin, originForMissTest );
	if( squareDistance > SQUARE( 48.0f ) ) {
		return false;
	}
	if( squareDistance < SQUARE( 24.0f ) ) {
		return true;
	}

	SolidWorldTrace( &trace, spotAndScore->origin, originForMissTest );
	return trace.fraction == 1.0f;
}

void BestRegularJumpableSpotDetector::GetVelocityForJumpingToSpot( vec3_t velocity, const SpotAndScore *spot ) {
	VectorSubtract( spot->origin, startOrigin, velocity );
	velocity[2] = 0;
	float scale2D =  run2DSpeed / sqrtf( SQUARE( velocity[0] ) + SQUARE( velocity[1] ) );
	velocity[0] *= scale2D;
	velocity[1] *= scale2D;
	velocity[2] = jumpZSpeed;
}

const SpotAndScore *BestRegularJumpableSpotDetector::Exec( const vec3_t startOrigin_, unsigned *millis ) {
#ifndef PUBLIC_BUILD
	if( run2DSpeed <= 0 || jumpZSpeed <= 0 ) {
		AI_FailWith( "BestRegularJumpableSpotsDetector::Exec()", "Illegal jump physics props (have they been set?)\n" );
	}
#endif

	// Cannot be initialized in constructor called for the global instance
	this->aasWorld = AiAasWorld::Instance();
	const SpotAndScore *result = BestJumpableSpotDetector::Exec( startOrigin_, millis );
	run2DSpeed = 0.0f;
	jumpZSpeed = 0.0f;

	// Nulliffy supplied references to avoid unintended reusing
	this->routeCache = nullptr;
	this->navTargetAreaNum = 0;
	this->startTravelTimeToTarget = 0;
	return result;
}