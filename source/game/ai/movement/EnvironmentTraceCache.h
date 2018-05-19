#ifndef QFUSION_ENVIRONMENTTRACECACHE_H
#define QFUSION_ENVIRONMENTTRACECACHE_H

#include "../ai_local.h"

class MovementPredictionContext;

class EnvironmentTraceCache
{
public:
	static constexpr float TRACE_DEPTH = 32.0f;

	struct TraceResult {
		trace_t trace;
		vec3_t traceDir;

		inline bool IsEmpty() const { return trace.fraction == 1.0f; }
	};

	enum class ObstacleAvoidanceResult {
		NO_OBSTACLES,
		CORRECTED,
		KEPT_AS_IS
	};

	static const int sideDirXYMoves[8][2];
	static const float sideDirXYFractions[8][2];

private:
	// Precache this reference as it is used on every prediction step
	const aas_areasettings_t *aasAreaSettings;

	TraceResult results[16];
	unsigned resultsMask;
	bool didAreaTest;
	bool hasNoFullHeightObstaclesAround;

	template <typename T>
	static inline void Assert( T condition, const char *message = nullptr ) {
		// There is a define in the source file, we do not want to either expose it to this header
		// or to move all inlines that use it to the source
#ifndef PUBLIC_BUILD
		if( !condition ) {
			if( message ) {
				AI_FailWith( "EnvironmentTraceCache::Assert()", "%s\n", message );
			} else {
				AI_FailWith( "EnvironmentTraceCache::Assert()", "An assertion has failed\n" );
			}
		}
#endif
	}

	// The resultFlag arg is supplied only for assertions check
	inline const TraceResult &ResultForIndex( unsigned resultFlag, int index ) const {
		Assert( resultFlag == 1u << index, "The result flag does not match the index" );
		Assert( resultFlag & this->resultsMask, "A result is not present for the index" );
		return results[index];
	}

	ObstacleAvoidanceResult TryAvoidObstacles( class MovementPredictionContext *context,
											   Vec3 *intendedLookVec,
											   float correctionFraction,
											   unsigned sidesShift );

	inline static void MakeTraceDir( unsigned dirNum, const vec3_t front2DDir, const vec3_t right2DDir, vec3_t traceDir );
	inline static bool CanSkipTracingForAreaHeight( const vec3_t origin, const aas_area_t &area, float minZOffset );
	void SetFullHeightCachedTracesEmpty( const vec3_t front2DDir, const vec3_t right2DDir );
	void SetJumpableHeightCachedTracesEmpty( const vec3_t front2DDir, const vec3_t right2DDir );
	bool TrySkipTracingForCurrOrigin( class MovementPredictionContext *context,
									  const vec3_t front2DDir, const vec3_t right2DDir );

	inline unsigned SelectNonBlockedDirs( class MovementPredictionContext *context, unsigned *nonBlockedDirIndices );

public:
	enum Side {
		FRONT = 1 << 0,
		FIRST_SIDE = FRONT,
		BACK = 1 << 1,
		LEFT = 1 << 2,
		RIGHT = 1 << 3,
		FRONT_LEFT = 1 << 4,
		FRONT_RIGHT = 1 << 5,
		BACK_LEFT = 1 << 6,
		BACK_RIGHT = 1 << 7,
		LAST_SIDE = BACK_RIGHT
	};

	static constexpr unsigned FULL_SIDES_MASK = ( 1 << 8 ) - 1;
	static constexpr unsigned ALL_SIDES_MASK = ( 1 << 16 ) - 1;
	static constexpr unsigned JUMPABLE_SIDES_MASK = ALL_SIDES_MASK & ~FULL_SIDES_MASK;

	inline unsigned FullHeightMask( unsigned sidesMask ) const {
		Assert( sidesMask & FULL_SIDES_MASK );
		return sidesMask;
	}
	inline unsigned JumpableHeightMask( unsigned sidesMask ) const {
		Assert( sidesMask & FULL_SIDES_MASK );
		return sidesMask << 8;
	}

	inline const TraceResult &FullHeightFrontTrace() const { return ResultForIndex( FullHeightMask( FRONT ), 0 ); }
	inline const TraceResult &FullHeightBackTrace() const { return ResultForIndex( FullHeightMask( BACK ), 1 ); }
	inline const TraceResult &FullHeightLeftTrace() const { return ResultForIndex( FullHeightMask( LEFT ), 2 ); }
	inline const TraceResult &FullHeightRightTrace() const { return ResultForIndex( FullHeightMask( RIGHT ), 3 ); }
	inline const TraceResult &FullHeightFrontLeftTrace() const { return ResultForIndex( FullHeightMask( FRONT_LEFT ), 4 ); }
	inline const TraceResult &FullHeightFrontRightTrace() const { return ResultForIndex( FullHeightMask( FRONT_RIGHT ), 5 ); }
	inline const TraceResult &FullHeightBackLeftTrace() const { return ResultForIndex( FullHeightMask( BACK_LEFT ), 6 ); }
	inline const TraceResult &FullHeightBackRightTrace() const { return ResultForIndex( FullHeightMask( BACK_RIGHT ), 7 ); }

	inline const TraceResult &JumpableHeightFrontTrace() const { return ResultForIndex( JumpableHeightMask( FRONT ), 8 ); }
	inline const TraceResult &JumpableHeightBackTrace() const { return ResultForIndex( JumpableHeightMask( BACK ), 9 ); }
	inline const TraceResult &JumpableHeightLeftTrace() const { return ResultForIndex( JumpableHeightMask( LEFT ), 10 ); }
	inline const TraceResult &JumpableHeightRightTrace() const { return ResultForIndex( JumpableHeightMask( RIGHT ), 11 ); }
	inline const TraceResult &JumpableHeightFrontLeftTrace() const {
		return ResultForIndex( JumpableHeightMask( FRONT_LEFT ), 12 );
	}
	inline const TraceResult &JumpableHeightFrontRightTrace() const {
		return ResultForIndex( JumpableHeightMask( FRONT_RIGHT ), 13 );
	}
	inline const TraceResult &JumpableHeightBackLeftTrace() const {
		return ResultForIndex( JumpableHeightMask( BACK_LEFT ), 14 );
	}
	inline const TraceResult &JumpableHeightBackRightTrace() const {
		return ResultForIndex( JumpableHeightMask( BACK_RIGHT ), 15 );
	}

	inline EnvironmentTraceCache() {
		// Shut an analyzer up
		memset( this, 0, sizeof( EnvironmentTraceCache ) );
		this->aasAreaSettings = AiAasWorld::Instance()->AreaSettings();
	}

	void TestForResultsMask( class MovementPredictionContext *context, unsigned requiredResultsMask );

	bool CanSkipPMoveCollision( class MovementPredictionContext *context );

	inline ObstacleAvoidanceResult TryAvoidJumpableObstacles( class MovementPredictionContext *context,
															  Vec3 *intendedLookVec, float correctionFraction ) {
		return TryAvoidObstacles( context, intendedLookVec, correctionFraction, 8 );
	}
	inline ObstacleAvoidanceResult TryAvoidFullHeightObstacles( class MovementPredictionContext *context,
																Vec3 *intendedLookVec, float correctionFraction ) {
		return TryAvoidObstacles( context, intendedLookVec, correctionFraction, 0 );
	}

	inline const TraceResult &FullHeightTraceForSideIndex( unsigned index ) const {
		return ResultForIndex( 1U << index, index );
	}
	inline const TraceResult &JumpableHeightTraceForSideIndex( unsigned index ) const {
		return ResultForIndex( 1U << ( index + 8 ), index + 8 );
	}

	void MakeRandomizedKeyMovesToTarget( MovementPredictionContext *context, const Vec3 &intendedMoveDir, int *keyMoves );
	void MakeKeyMovesToTarget( MovementPredictionContext *context, const Vec3 &intendedMoveDir, int *keyMoves );
	void MakeRandomKeyMoves( MovementPredictionContext *context, int *keyMoves );
};

#endif
