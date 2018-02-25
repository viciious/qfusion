#include "ai_trajectory_predictor.h"

static void Trace( trace_t *tr, vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, edict_t *, int contentmask ) {
	return trap_CM_TransformedBoxTrace( tr, start, end, mins, maxs, nullptr, contentmask, nullptr, nullptr );
}

AiTrajectoryPredictor::StopEvent AiTrajectoryPredictor::Run( const Vec3 &startVelocity,
															 const Vec3 &startOrigin,
															 Results *results ) {
	if( !results->trace ) {
		results->trace = &this->localTrace;
	}

	startOrigin.CopyTo( this->prevOrigin );
	startOrigin.CopyTo( results->origin );

	this->traceFunc = Trace;
	this->contentMask = CONTENTS_SOLID;
	this->ignore = nullptr;

	if( stopEventFlags & HIT_ENTITY ) {
		traceFunc = G_Trace;
		contentMask |= ( MASK_PLAYERSOLID & ~CONTENTS_SOLID );
		if( ignoreEntNum > 0 ) {
			ignore = game.edicts + ignoreEntNum;
		}
	}

	if( stopEventFlags & ( HIT_LIQUID | LEAVE_LIQUID ) ) {
		contentMask |= CONTENTS_WATER;
	}

	this->aasWorld = AiAasWorld::Instance();

	results->millisAhead = stepMillis;
	for( unsigned i = 1; i <= numSteps; ++i ) {
		if( int flags = RunStep( startOrigin, startVelocity, results ) ) {
			return (StopEvent)flags;
		}

		results->millisAhead += stepMillis;
		prevOrigin.Set( results->origin );
	}

	if( extrapolateLastStep ) {
		// That's enough, a larger value cannot be even stored in a float
		results->millisAhead = 99999;
		// Save last origin
		Vec3 lastOrigin( prevOrigin );
		if( int flags = RunStep( startOrigin, startVelocity, results ) ) {
			return (StopEvent)flags;
		} else {
			// Restore the last origin overwritten by an extrapolated value that is very likely to be outside a map
			lastOrigin.CopyTo( results->origin );
			return StopEvent::DONE;
		}
	}

	return StopEvent::DONE;
}

int AiTrajectoryPredictor::RunStep( const Vec3 &startOrigin, const Vec3 &startVelocity, Results *results ) {
	const float gravity = level.gravity;
	const float t = 0.001f * results->millisAhead;

	int resultFlags = 0;

	results->origin[0] = startOrigin.X() + startVelocity.X() * t;
	results->origin[1] = startOrigin.Y() + startVelocity.Y() * t;
	results->origin[2] = startOrigin.Z() + startVelocity.Z() * t - 0.5f * gravity * t * t;

	if( stopEventFlags & ( HIT_SOLID | HIT_LIQUID | LEAVE_LIQUID ) ) {
		int stepMask = contentMask;
		// Ignore water/lava/slime while leaving it
		if( !( stopEventFlags & LEAVE_LIQUID ) ) {
			if( startVelocity.Z() - gravity * t >= 0 ) {
				stepMask &= ~CONTENTS_WATER;
			}
		}

		traceFunc( results->trace, prevOrigin.Data(), mins, maxs, results->origin, ignore, stepMask );

		if( results->trace->fraction != 1.0f ) {
			resultFlags |= ( results->trace->ent > 0 ? HIT_ENTITY : HIT_SOLID );
			VectorCopy( results->trace->endpos, results->origin );
		}
	}

	if( stopEventFlags >= ENTER_AREA_NUM ) {
		resultFlags |= InspectAasWorldTrace( results );
	}

#if 0
	// Don't put this debug code in OnPredictionStep()
	// since it might be overridden and calling the base method is not mandatory
	int color = 0;
	switch( ( results->millisAhead / stepMillis ) % 3 ) {
		case 0: color = COLOR_RGB( 255, 0, 0 ); break;
		case 1: color = COLOR_RGB( 0, 255, 0 ); break;
		case 2: color = COLOR_RGB( 0, 0, 255 ); break;
	}
	AITools_DrawColorLine( prevOrigin.Data(), results->origin, color, 0 );
#endif

	if( !OnPredictionStep( prevOrigin, results ) ) {
		resultFlags |= INTERRUPTED;
	}

	return resultFlags;
}

static inline int TestFlagsMatch( int flagsValue, int expectedFlag, int *result ) {
	if( flagsValue & expectedFlag ) {
		*result |= expectedFlag;
		return expectedFlag;
	}
	return 0;
}

static inline int TestFlagsMismatch( int flagsValue, int expectedFlag, int *result ) {
	if( !( flagsValue & expectedFlag ) ) {
		*result |= expectedFlag;
		return expectedFlag;
	}
	return 0;
}

template<int Flags>
int AiTrajectoryPredictor::InspectAasWorldTraceForFlags( const int *areaNums, int numTracedAreas, Results *results ) {
	const auto *areaSettings = aasWorld->AreaSettings();

	int result = 0;
	for( int i = 0; i < numTracedAreas; ++i ) {
		const int areaNum = areaNums[i];
		if( stopEventFlags & ( Flags & ENTER_AREA_NUM ) ) {
			if( enterAreaNum == areaNum ) {
				results->enterAreaNum = areaNum;
				result |= ENTER_AREA_NUM;
			}
		}

		if( stopEventFlags & ( Flags & LEAVE_AREA_NUM ) ) {
			if( areaNum != leaveAreaNum ) {
				results->leaveAreaNum = areaNum;
				result |= LEAVE_AREA_NUM;
			}
		}

		if( stopEventFlags & ( Flags & ENTER_AREA_FLAGS ) ) {
			result |= TestFlagsMatch( areaSettings[areaNum].areaflags, ENTER_AREA_FLAGS, &results->enterAreaFlags );
		}

		if( stopEventFlags & ( Flags & LEAVE_AREA_FLAGS ) ) {
			result |= TestFlagsMismatch( areaSettings[areaNum].areaflags, LEAVE_AREA_FLAGS, &results->leaveAreaFlags );
		}

		if( stopEventFlags & ( Flags & ENTER_AREA_CONTENTS ) ) {
			result |= TestFlagsMatch( areaSettings[areaNum].contents, ENTER_AREA_CONTENTS, &results->enterAreaContents );
		}

		if( stopEventFlags & ( Flags & LEAVE_AREA_CONTENTS ) ) {
			result |= TestFlagsMismatch( areaSettings[areaNum].contents, LEAVE_AREA_CONTENTS, &results->leaveAreaContents );
		}
	}

	return result;
}

int AiTrajectoryPredictor::InspectAasWorldTrace( Results *results ) {
	// Make sure its always legal to access areaNums[numAreas - (1|2)]
	int areasBuffer[128 + 2];
	areasBuffer[0] = 0;
	areasBuffer[1] = 1;
	int *areaNums = areasBuffer + 2;

	int numAreas = aasWorld->TraceAreas( prevOrigin.Data(), results->origin, areaNums, 128 );
	results->lastAreaNum = areaNums[numAreas - 1];
	// Even if there are areas in the trace, the last area might be zero if areas trace ends in solid
	if( !results->lastAreaNum ) {
		results->lastAreaNum = areaNums[numAreas - 2];
	}

	// Now try selecting an optimized generated code version for the most frequent cases
	if( stopEventFlags == ENTER_AREA_CONTENTS ) {
		return InspectAasWorldTraceForFlags<ENTER_AREA_CONTENTS>( areaNums, numAreas, results );
	}

	constexpr auto ENTER_MASK = ENTER_AREA_NUM | ENTER_AREA_FLAGS | ENTER_AREA_CONTENTS;
	constexpr auto LEAVE_MASK = LEAVE_AREA_NUM | LEAVE_AREA_FLAGS | LEAVE_AREA_CONTENTS;

	if( ( stopEventFlags & ENTER_MASK ) && ~( stopEventFlags & LEAVE_MASK ) ) {
		return InspectAasWorldTraceForFlags<ENTER_MASK>( areaNums, numAreas, results );
	}

	return InspectAasWorldTraceForFlags<ENTER_MASK | LEAVE_MASK>( areaNums, numAreas, results );
}
