#ifndef QFUSION_AI_TRAJECTORY_PREDICTOR_H
#define QFUSION_AI_TRAJECTORY_PREDICTOR_H

#include "ai_local.h"

class AiTrajectoryPredictor {
public:
	struct Results {
		trace_t *trace;
		vec3_t origin;
		unsigned millisAhead;
		int enterAreaNum;
		int enterAreaFlags;
		int enterAreaContents;
		int leaveAreaNum;
		int leaveAreaContents;
		int leaveAreaFlags;
		int lastAreaNum;

		Results() {
			Clear();
		}

		void Clear() {
			memset( this, 0, sizeof( *this ) );
		}
	};

protected:
	trace_t localTrace;
	Vec3 prevOrigin;

	void ( *traceFunc )( trace_t *tr, vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, edict_t *, int contentmask );
	edict_t *ignore;
	int contentMask;

	const AiAasWorld *aasWorld;

	vec3_t mins, maxs;
	unsigned stepMillis;
	unsigned numSteps;

	int stopEventFlags;

	int enterAreaNum;
	int enterAreaContents;
	int enterAreaFlags;

	int leaveAreaNum;
	int leaveAreaContents;
	int leaveAreaFlags;

	int ignoreEntNum;
	bool extrapolateLastStep;

	int RunStep( const Vec3 &startOrigin, const Vec3 &startVelocity, Results *results );
	int InspectAasWorldTrace( Results *results );

	// A template that allows code generation for specific flags statically omitting checks for non-specified flags
	template <int Flags>
	int InspectAasWorldTraceForFlags( const int *areaNums, int numTracedAreas, Results *results );

	template<typename BooleanLike>
	void SetStopEventsBit( BooleanLike condition, int bit ) {
		// Check whether its a power of two
		assert( !( bit & ( bit - 1 ) ) );
		if( condition ) {
			stopEventFlags |= bit;
		} else {
			stopEventFlags &= ~bit;
		}
	}
public:
	AiTrajectoryPredictor()
		: prevOrigin( 0, 0, 0 ),
		  traceFunc( nullptr ),
		  ignore( nullptr ),
		  contentMask( 0 ),
		  aasWorld( nullptr ),
		  stepMillis( 128 ), numSteps( 8 ),
		  stopEventFlags( 0 ),
		  enterAreaNum( 0 ), enterAreaContents( 0 ), enterAreaFlags( 0 ),
		  leaveAreaNum( 0 ), leaveAreaContents( 0 ), leaveAreaFlags( 0 ),
		  ignoreEntNum( 0 ),
		  extrapolateLastStep( false ) {
		VectorCopy( vec3_origin, mins );
		VectorCopy( vec3_origin, maxs );
	}

	inline void SetColliderBounds( const vec_t *mins_, const vec_t *maxs_ ) {
		VectorCopy( mins_, this->mins );
		VectorCopy( maxs_, this->maxs );
	}

	inline void SetStepMillis( unsigned stepMillis_ ) { this->stepMillis = stepMillis_; }
	inline void SetNumSteps( unsigned numSteps_ ) { this->numSteps = numSteps_; }

	inline void SetEnterAreaNum( int areaNum_ ) {
		this->enterAreaNum = areaNum_;
		SetStopEventsBit( areaNum_, ENTER_AREA_NUM );
	}

	inline void SetLeaveAreaNum( int areaNum_ ) {
		this->leaveAreaNum = areaNum_;
		SetStopEventsBit( areaNum_, LEAVE_AREA_NUM );
	};

	inline void SetEnterAreaProps( int flags_, int contents_ ) {
		this->enterAreaFlags = flags_;
		this->enterAreaContents = contents_;
		SetStopEventsBit( flags_, ENTER_AREA_FLAGS );
		SetStopEventsBit( contents_, ENTER_AREA_CONTENTS );
	}

	inline void SetLeaveAreaProps( int flags_, int contents_ ) {
		this->leaveAreaFlags = flags_;
		this->leaveAreaContents = contents_;
		SetStopEventsBit( flags_, LEAVE_AREA_FLAGS );
		SetStopEventsBit( contents_, LEAVE_AREA_CONTENTS );
	}

	inline void SetEntitiesCollisionProps( bool collideEntities, int ignoreEntNum_ ) {
		assert( ignoreEntNum_ >= 0 );
		this->ignoreEntNum = ignoreEntNum_;
		SetStopEventsBit( collideEntities, HIT_ENTITY );
	}

	inline void SetExtrapolateLastStep( bool value ) { this->extrapolateLastStep = value; }

	enum StopEvent {
		DONE                 = 1 << 0,
		INTERRUPTED          = 1 << 1,
		HIT_SOLID            = 1 << 2,
		HIT_ENTITY           = 1 << 3,
		HIT_LIQUID           = 1 << 4,
		LEAVE_LIQUID         = 1 << 5,
		ENTER_AREA_NUM       = 1 << 6,
		LEAVE_AREA_NUM       = 1 << 7,
		ENTER_AREA_FLAGS     = 1 << 8,
		LEAVE_AREA_FLAGS     = 1 << 9,
		ENTER_AREA_CONTENTS  = 1 << 10,
		LEAVE_AREA_CONTENTS  = 1 << 11
	};

	inline void AddStopEventFlags( int flags ) { this->stopEventFlags |= (StopEvent)flags; }

	StopEvent Run( const Vec3 &startVelocity, const Vec3 &startOrigin, Results *results );

	StopEvent Run( const vec3_t startVelocity, const vec3_t startOrigin, Results *results ) {
		return Run( Vec3( startVelocity ), Vec3( startOrigin ), results );
	}

	// Allows doing some additional actions on each step.
	// Interrupts execution by returning false.
	virtual bool OnPredictionStep( const Vec3 &segmentStart, const Results *results ) { return true; }
};

#endif
