#ifndef QFUSION_MOVEMENTPREDICTIONCONTEXT_H
#define QFUSION_MOVEMENTPREDICTIONCONTEXT_H

class BaseMovementAction;

#include "BotInput.h"
#include "MovementState.h"
#include "NavMeshQueryCache.h"
#include "SameFloorClusterAreasCache.h"
#include "EnvironmentTraceCache.h"

struct MovementActionRecord {
	BotInput botInput;

private:
	int16_t modifiedVelocity[3];

public:
	int8_t pendingWeapon : 7;
	bool hasModifiedVelocity : 1;

	inline MovementActionRecord()
		: pendingWeapon( -1 ),
		  hasModifiedVelocity( false ) {}

	inline void Clear() {
		botInput.Clear();
		pendingWeapon = -1;
		hasModifiedVelocity = false;
	}

	inline void SetModifiedVelocity( const Vec3 &velocity ) {
		SetModifiedVelocity( velocity.Data() );
	}

	inline void SetModifiedVelocity( const vec3_t velocity ) {
		for( int i = 0; i < 3; ++i ) {
			int snappedVelocityComponent = (int)( velocity[i] * 16.0f );
			if( snappedVelocityComponent > std::numeric_limits<signed short>::max() ) {
				snappedVelocityComponent = std::numeric_limits<signed short>::max();
			} else if( snappedVelocityComponent < std::numeric_limits<signed short>::min() ) {
				snappedVelocityComponent = std::numeric_limits<signed short>::min();
			}
			modifiedVelocity[i] = (signed short)snappedVelocityComponent;
		}
		hasModifiedVelocity = true;
	}

	inline Vec3 ModifiedVelocity() const {
		assert( hasModifiedVelocity );
		float scale = 1.0f / 16.0f;
		return Vec3( scale * modifiedVelocity[0], scale * modifiedVelocity[1], scale * modifiedVelocity[2] );
	}
};

struct MovementPredictionConstants {
	enum SequenceStopReason : uint8_t {
		UNSPECIFIED, // An empty initial value, should be replaced by SWITCHED on actual use
		SUCCEEDED,   // The sequence has been completed successfully
		SWITCHED,    // The action cannot be applied in the current environment, another action is suggested
		DISABLED,    // The action is disabled for application, another action is suggested
		FAILED       // A prediction step has lead to a failure
	};

	static constexpr unsigned MAX_SAVED_LANDING_AREAS = 16;
};

class MovementPredictionContext : public MovementPredictionConstants
{
	friend class BotTriggerPendingWeaponJumpMovementAction;

	edict_t *const self;
public:
	static constexpr unsigned MAX_PREDICTED_STATES = 48;

	struct alignas ( 1 )HitWhileRunningTestResult {
		bool canHitAsIs : 1;
		bool mayHitOverridingPitch : 1;

		inline HitWhileRunningTestResult()
		{
			static_assert( sizeof( *this ) == 1, "" );
			*( (uint8_t *)( this ) ) = 0;
		}

		inline bool CanHit() const { return canHitAsIs || mayHitOverridingPitch; }

		// Use the method and not a static var (the method result should be inlined w/o any static memory access)
		static inline HitWhileRunningTestResult Failure() { return HitWhileRunningTestResult(); }
	};

	BotSameFloorClusterAreasCache sameFloorClusterAreasCache;
	BotNavMeshQueryCache navMeshQueryCache;
private:
	struct PredictedMovementAction {
		AiEntityPhysicsState entityPhysicsState;
		MovementActionRecord record;
		BaseMovementAction *action;
		int64_t timestamp;
		unsigned stepMillis;
		unsigned movementStatesMask;

		PredictedMovementAction()
			: action(nullptr),
			  timestamp( 0 ),
			  stepMillis( 0 ),
			  movementStatesMask( 0 ) {}
	};

	StaticVector<PredictedMovementAction, MAX_PREDICTED_STATES> predictedMovementActions;
	StaticVector<BotMovementState, MAX_PREDICTED_STATES> botMovementStatesStack;
	StaticVector<player_state_t, MAX_PREDICTED_STATES> playerStatesStack;
	StaticVector<signed char, MAX_PREDICTED_STATES> pendingWeaponsStack;

	template <typename T, unsigned N>
	class CachesStack
	{
		static_assert( sizeof( uint64_t ) * 8 >= N, "64-bit bitset capacity overflow" );

		StaticVector<T, N> values;
		uint64_t isCachedBitset;

		inline void SetBit( unsigned bit ) { isCachedBitset |= ( ( (uint64_t)1 ) << bit ); }
		inline void ClearBit( unsigned bit ) { isCachedBitset &= ~( ( (uint64_t)1 ) << bit ); }

	public:
		inline CachesStack() : isCachedBitset( 0 ) {}

		inline void SetCachedValue( const T &value ) {
			assert( values.size() );
			SetBit( values.size() - 1 );
			values.back() = value;
		}
		inline void SetCachedValue( T &&value ) {
			assert( values.size() );
			SetBit( values.size() - 1 );
			values.back() = std::move( value );
		}
		// When cache stack growth for balancing is needed and no value exists for current stack pos, use this method
		inline void PushDummyNonCachedValue( T &&value = T() ) {
			ClearBit( values.size() );
			values.emplace_back( std::move( value ) );
		}
		// Should be used when the cached type cannot be copied or moved (use this pointer to allocate a value in-place)
		inline T *UnsafeGrowForNonCachedValue() {
			ClearBit( values.size() );
			return values.unsafe_grow_back();
		}
		inline T *GetUnsafeBufferForCachedValue() {
			SetBit( values.size() - 1 );
			return &values[0] + ( values.size() - 1 );
		}
		inline const T *GetCached() const {
			assert( values.size() );
			return ( isCachedBitset & ( ( (uint64_t)1 ) << ( values.size() - 1 ) ) ) ? &values.back() : nullptr;
		}
		inline const T *GetCachedValueBelowTopOfStack() const {
			assert( values.size() );
			if( values.size() == 1 ) {
				return nullptr;
			}
			return ( isCachedBitset & ( ( (uint64_t)1 ) << ( values.size() - 2 ) ) ) ? &values[values.size() - 2] : nullptr;
		}

		inline unsigned Size() const { return values.size(); }
		// Use when cache stack is being rolled back
		inline void PopToSize( unsigned newSize ) {
			assert( newSize <= values.size() );
			values.truncate( newSize );
		}
	};

	CachesStack<Ai::ReachChainVector, MAX_PREDICTED_STATES> reachChainsCachesStack;
	CachesStack<BotInput, MAX_PREDICTED_STATES> defaultBotInputsCachesStack;
	CachesStack<HitWhileRunningTestResult, MAX_PREDICTED_STATES> mayHitWhileRunningCachesStack;
	CachesStack<bool, MAX_PREDICTED_STATES> canSafelyKeepHighSpeedCachesStack;
	StaticVector<EnvironmentTraceCache, MAX_PREDICTED_STATES> environmentTestResultsStack;
public:
	struct NearbyTriggersCache {
		vec3_t lastComputedForMins;
		vec3_t lastComputedForMaxs;

		int numJumppadEnts;
		int numTeleportEnts;
		int numPlatformEnts;
		int numOtherEnts;

		static constexpr auto MAX_GROUP_ENTITIES = 12;
		static constexpr auto MAX_OTHER_ENTITIES = 20;

		uint16_t jumppadEntNums[MAX_GROUP_ENTITIES];
		uint16_t teleportEntNums[MAX_GROUP_ENTITIES];
		uint16_t platformEntNums[MAX_GROUP_ENTITIES];
		uint16_t otherEntNums[MAX_OTHER_ENTITIES];

		int triggerTravelFlags[3];
		const int *triggerNumEnts[3];
		const uint16_t *triggerEntNums[3];

		NearbyTriggersCache()
			: numJumppadEnts( 0 ),
			  numTeleportEnts( 0 ),
			  numPlatformEnts( 0 ),
			  numOtherEnts( 0 ) {
			// Set illegal bounds to force update on first access
			ClearBounds( lastComputedForMins, lastComputedForMaxs );

			triggerTravelFlags[0] = TRAVEL_JUMPPAD;
			triggerTravelFlags[1] = TRAVEL_TELEPORT;
			triggerTravelFlags[2] = TRAVEL_ELEVATOR;

			triggerNumEnts[0] = &numJumppadEnts;
			triggerNumEnts[1] = &numTeleportEnts;
			triggerNumEnts[2] = &numPlatformEnts;

			triggerEntNums[0] = &jumppadEntNums[0];
			triggerEntNums[1] = &teleportEntNums[0];
			triggerEntNums[2] = &platformEntNums[0];
		}

		void EnsureValidForBounds( const vec3_t absMins, const vec3_t absMaxs );
	} nearbyTriggersCache;

	BotMovementState *movementState;
	MovementActionRecord *record;

	const player_state_t *oldPlayerState;
	player_state_t *currPlayerState;

	BaseMovementAction *actionSuggestedByAction;
	BaseMovementAction *activeAction;

	unsigned totalMillisAhead;
	unsigned predictionStepMillis;
	// Must be set to game.frameTime for the first step!
	unsigned oldStepMillis;

	unsigned topOfStackIndex;
	unsigned savepointTopOfStackIndex;

	SequenceStopReason sequenceStopReason;
	bool isCompleted;
	bool cannotApplyAction;
	bool shouldRollback;

	struct FrameEvents {
		static constexpr auto MAX_TOUCHED_OTHER_TRIGGERS = 16;
		// Not teleports, jumppads or platforms (usually items).
		// Non-null classname is the only restriction applied.
		uint16_t otherTouchedTriggerEnts[MAX_TOUCHED_OTHER_TRIGGERS];
		int numOtherTouchedTriggers;

		bool hasJumped: 1;
		bool hasDashed: 1;
		bool hasWalljumped: 1;
		bool hasTakenFallDamage: 1;

		bool hasTouchedJumppad: 1;
		bool hasTouchedTeleporter: 1;
		bool hasTouchedPlatform: 1;

		inline FrameEvents() {
			Clear();
		}

		inline void Clear() {
			numOtherTouchedTriggers = 0;
			hasJumped = false;
			hasDashed = false;
			hasWalljumped = false;
			hasTakenFallDamage = false;
			hasTouchedJumppad = false;
			hasTouchedTeleporter = false;
			hasTouchedPlatform = false;
		}
	};

	FrameEvents frameEvents;

	class BaseMovementAction *SuggestSuitableAction();
	inline class BaseMovementAction *SuggestAnyAction();

	inline Vec3 NavTargetOrigin() const;
	inline float NavTargetRadius() const;
	inline bool IsCloseToNavTarget() const;
	inline int CurrAasAreaNum() const;
	inline int CurrGroundedAasAreaNum() const;
	inline int NavTargetAasAreaNum() const;
	inline bool IsInNavTargetArea() const;

	bool CanSafelyKeepHighSpeed();

	const Ai::ReachChainVector &NextReachChain();
	inline EnvironmentTraceCache &TraceCache();
	inline EnvironmentTraceCache::ObstacleAvoidanceResult TryAvoidFullHeightObstacles( float correctionFraction );
	inline EnvironmentTraceCache::ObstacleAvoidanceResult TryAvoidJumpableObstacles( float correctionFraction );

	// Do not return boolean value, avoid extra branching. Checking results if necessary is enough.
	void NextReachNumAndTravelTimeToNavTarget( int *reachNum, int *travelTimeToNavTarget );

	inline int NextReachNum() {
		int results[2];
		NextReachNumAndTravelTimeToNavTarget( results, results + 1 );
		return results[0];
	}
	inline int TravelTimeToNavTarget() {
		int results[2];
		NextReachNumAndTravelTimeToNavTarget( results, results + 1 );
		return results[1];
	}

	inline const AiAasRouteCache *RouteCache() const;
	inline const ArrayRange<int> TravelFlags() const;

	explicit MovementPredictionContext( edict_t *self_ )
		: self( self_ )
		, sameFloorClusterAreasCache( self_ )
		, navMeshQueryCache( self_ )
		, movementState( nullptr )
		, record( nullptr )
		, oldPlayerState( nullptr )
		, currPlayerState( nullptr )
		, actionSuggestedByAction( nullptr )
		, activeAction( nullptr )
		, totalMillisAhead( 0 )
		, predictionStepMillis( 0 )
		, oldStepMillis( 0 )
		, topOfStackIndex( 0 )
		, savepointTopOfStackIndex( 0 )
		, sequenceStopReason( SequenceStopReason::SUCCEEDED )
		, isCompleted( false )
		, cannotApplyAction( false )
		, shouldRollback( false ) {}

	HitWhileRunningTestResult MayHitWhileRunning();

	void BuildPlan();
	bool NextPredictionStep();
	void SetupStackForStep();

	void NextMovementStep();

	inline const AiEntityPhysicsState &PhysicsStateBeforeStep() const {
		return predictedMovementActions[topOfStackIndex].entityPhysicsState;
	}

	inline bool CanGrowStackForNextStep() const {
		// Note: topOfStackIndex is an array index, MAX_PREDICTED_STATES is an array size
		return this->topOfStackIndex + 1 < MAX_PREDICTED_STATES;
	}

	inline void SaveActionOnStack( BaseMovementAction *action );

	// Frame index is restricted to topOfStack or topOfStack + 1
	inline void MarkSavepoint( BaseMovementAction *markedBy, unsigned frameIndex );

	inline void SetPendingRollback();
	inline void RollbackToSavepoint();
	inline void SetPendingWeapon( int weapon );
	inline void SaveSuggestedActionForNextFrame( BaseMovementAction *action );
	inline unsigned MillisAheadForFrameStart( unsigned frameIndex ) const;

	class BaseMovementAction *GetCachedActionAndRecordForCurrTime( MovementActionRecord *record_ );

	void SetDefaultBotInput();

	void Debug( const char *format, ... ) const;
	// We want to have a full control over movement code assertions, so use custom ones for this class
	inline void Assert( bool condition, const char *message = nullptr ) const;
	template <typename T>
	inline void Assert( T conditionLikeValue, const char *message = nullptr ) const {
		Assert( conditionLikeValue != 0, message );
	}

	inline float GetRunSpeed() const;
	inline float GetJumpSpeed() const;
	inline float GetDashSpeed() const;

	void CheatingAccelerate( float frac );

	inline void CheatingCorrectVelocity( const Vec3 &target ) {
		CheatingCorrectVelocity( target.Data() );
	}

	void CheatingCorrectVelocity( const vec3_t target );
	void CheatingCorrectVelocity( float velocity2DDirDotToTarget2DDir, const Vec3 &toTargetDir2D );

	void OnInterceptedPredictedEvent( int ev, int parm );
	void OnInterceptedPMoveTouchTriggers( pmove_t *pm, vec3_t const previousOrigin );

	class BaseMovementAction *GetActionAndRecordForCurrTime( MovementActionRecord *record_ );

	// Might be called for failed attempts too
	void ShowBuiltPlanPath() const;
};

#endif
