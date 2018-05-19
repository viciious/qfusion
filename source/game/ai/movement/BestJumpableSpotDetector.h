#ifndef QFUSION_BESTJUMPABLESPOTDETECTOR_H
#define QFUSION_BESTJUMPABLESPOTDETECTOR_H

#include "../ai_trajectory_predictor.h"
#include "../navigation/AasRouteCache.h"

// Generalizes jumpable/weapon-jumpable spots detection
class BestJumpableSpotDetector {
public:
	struct SpotAndScore {
		vec3_t origin;
		float score;

		SpotAndScore( const vec3_t origin_, float score_ ) {
			VectorCopy( origin_, this->origin );
			this->score = score_;
		}

		// Do not negate the comparison result as we usually do, since spots are assumed to be stored in a max-heap
		bool operator<( const SpotAndScore &that ) const { return this->score < that.score; }
	};
protected:
	AiTrajectoryPredictor predictor;
	AiTrajectoryPredictor::Results predictionResults;
	vec3_t startOrigin;

	// A returned range should be a max-heap (best spots be evicted first)
	virtual void GetCandidateSpots( SpotAndScore **begin, SpotAndScore **end ) = 0;

	// Can be overridden in different ways, e.g.:
	// for regular jumping velocity XY is a 2 direction to spot multiplied by run speed, Z is the jump speed
	// for weapon jumping velocity is a direction to spot multiplied by a value derived from knockback and player mass
	virtual void GetVelocityForJumpingToSpot( vec3_t velocity, const vec3_t spot ) = 0;
public:
	BestJumpableSpotDetector() {
		auto badAreaContents = AREACONTENTS_LAVA | AREACONTENTS_SLIME | AREACONTENTS_WATER | AREACONTENTS_DONOTENTER;
		predictor.SetEnterAreaProps( 0, badAreaContents );
		predictor.AddStopEventFlags( AiTrajectoryPredictor::HIT_SOLID );
	}

	virtual ~BestJumpableSpotDetector() {}

	inline void SetStepMillis( unsigned stepMillis ) {
		predictor.SetStepMillis( stepMillis );
	}

	inline void SetNumSteps( unsigned numSteps ) {
		predictor.SetNumSteps( numSteps );
	}

	// Might vary for different purposes, e.g.
	// for rocketjumping a full playerbox should be used, for barrier jumping we can zero mins
	inline void SetColliderBounds( const vec3_t mins, const vec3_t maxs ) {
		predictor.SetColliderBounds( mins, maxs );
	}

	// Returns an estimated jump travel time on success or zero on failure
	virtual unsigned Exec( const vec3_t startOrigin_, vec3_t spotOrigin_ );
};

class BestRegularJumpableSpotDetector: public BestJumpableSpotDetector {
	float run2DSpeed;
	float jumpZSpeed;
protected:
	void GetVelocityForJumpingToSpot( vec3_t velocity, const vec3_t spot ) final;

	const AiAasWorld *aasWorld;
	// If set, a travel time to a nav target is used as a criterion instead of distance proximity
	const AiAasRouteCache *routeCache;
	int navTargetAreaNum;
	int startTravelTimeToTarget;
public:
	void SetJumpPhysicsProps( float run2DSpeed_, float jumpZSpeed_ ) {
		this->run2DSpeed = run2DSpeed_;
		this->jumpZSpeed = jumpZSpeed_;
	}

	BestRegularJumpableSpotDetector()
		: run2DSpeed( 0.0f )
		, jumpZSpeed( 0.0f )
		, aasWorld( nullptr )
		, routeCache( nullptr )
		, navTargetAreaNum( 0 )
		, startTravelTimeToTarget( 0 ) {}

	inline void AddRoutingParams( const AiAasRouteCache *routeCache_, int navTargetAreaNum_, int startTravelTimeToTarget_ ) {
		this->routeCache = routeCache_;
		this->navTargetAreaNum = navTargetAreaNum_;
		this->startTravelTimeToTarget = startTravelTimeToTarget_;
	}

	unsigned Exec( const vec3_t startOrigin_, vec3_t spotOrigin_ ) override;
};

#endif
