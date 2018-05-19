#include "EnvironmentTraceCache.h"
#include "MovementLocal.h"

inline unsigned EnvironmentTraceCache::SelectNonBlockedDirs( Context *context, unsigned *nonBlockedDirIndices ) {
	this->TestForResultsMask( context, this->FullHeightMask( FULL_SIDES_MASK ) );

	unsigned numNonBlockedDirs = 0;
	for( unsigned i = 0; i < 8; ++i ) {
		if( this->FullHeightTraceForSideIndex( i ).IsEmpty() ) {
			nonBlockedDirIndices[numNonBlockedDirs++] = i;
		}
	}
	return numNonBlockedDirs;
}

void EnvironmentTraceCache::MakeRandomizedKeyMovesToTarget( Context *context, const Vec3 &intendedMoveDir, int *keyMoves ) {
	unsigned nonBlockedDirIndices[8];
	unsigned numNonBlockedDirs = SelectNonBlockedDirs( context, nonBlockedDirIndices );

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const Vec3 forwardDir( entityPhysicsState.ForwardDir() );
	const Vec3 rightDir( entityPhysicsState.RightDir() );
	assert( ( intendedMoveDir.Length() - 1.0f ) < 0.0001f );

	// Choose randomly from all non-blocked dirs based on scores
	// For each non - blocked area make an interval having a corresponding to the area score length.
	// An interval is defined by lower and upper bounds.
	// Upper bounds are stored in the array.
	// Lower bounds are upper bounds of the previous array memner (if any) or 0 for the first array memeber.
	float dirDistributionUpperBound[8];
	float scoresSum = 0.0f;
	for( unsigned i = 0; i < numNonBlockedDirs; ++i ) {
		vec3_t keyMoveVec;
		const float *dirFractions = sideDirXYFractions[nonBlockedDirIndices[i]];
		VectorScale( forwardDir.Data(), dirFractions[0], keyMoveVec );
		VectorMA( keyMoveVec, dirFractions[1], rightDir.Data(), keyMoveVec );
		scoresSum += 0.55f + 0.45f * intendedMoveDir.Dot( keyMoveVec );
		dirDistributionUpperBound[i] = scoresSum;
	}

	// A uniformly distributed random number in (0, scoresSum)
	const float rn = random() * scoresSum;
	for( unsigned i = 0; i < numNonBlockedDirs; ++i ) {
		if( rn > dirDistributionUpperBound[i] ) {
			continue;
		}

		int dirIndex = nonBlockedDirIndices[i];
		const int *dirMoves = sideDirXYMoves[dirIndex];
		Vector2Copy( dirMoves, keyMoves );
		return;
	}

	Vector2Set( keyMoves, 0, 0 );
}

void EnvironmentTraceCache::MakeKeyMovesToTarget( Context *context, const Vec3 &intendedMoveDir, int *keyMoves ) {
	unsigned nonBlockedDirIndices[8];
	unsigned numNonBlockedDirs = SelectNonBlockedDirs( context, nonBlockedDirIndices );

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const Vec3 forwardDir( entityPhysicsState.ForwardDir() );
	const Vec3 rightDir( entityPhysicsState.RightDir() );
	assert( ( intendedMoveDir.Length() - 1.0f ) < 0.0001f );

	float bestScore = 0.0f;
	unsigned bestDirIndex = (unsigned)-1;
	for( unsigned i = 0; i < numNonBlockedDirs; ++i ) {
		vec3_t keyMoveVec;
		unsigned dirIndex = nonBlockedDirIndices[i];
		const float *dirFractions = sideDirXYFractions[dirIndex];
		VectorScale( forwardDir.Data(), dirFractions[0], keyMoveVec );
		VectorMA( keyMoveVec, dirFractions[1], rightDir.Data(), keyMoveVec );
		float score = 0.55f + 0.45f * intendedMoveDir.Dot( keyMoveVec );
		if( score > bestScore ) {
			bestScore = score;
			bestDirIndex = dirIndex;
		}
	}
	if( bestScore > 0 ) {
		const int *dirMoves = sideDirXYMoves[bestDirIndex];
		Vector2Copy( dirMoves, keyMoves );
		return;
	}

	Vector2Set( keyMoves, 0, 0 );
}

void EnvironmentTraceCache::MakeRandomKeyMoves( Context *context, int *keyMoves ) {
	unsigned nonBlockedDirIndices[8];
	unsigned numNonBlockedDirs = SelectNonBlockedDirs( context, nonBlockedDirIndices );
	if( numNonBlockedDirs ) {
		int dirIndex = nonBlockedDirIndices[(unsigned)( 0.9999f * numNonBlockedDirs * random() )];
		const int *dirMoves = sideDirXYMoves[dirIndex];
		Vector2Copy( dirMoves, keyMoves );
		return;
	}
	Vector2Set( keyMoves, 0, 0 );
}

void EnvironmentTraceCache::SetFullHeightCachedTracesEmpty( const vec3_t front2DDir, const vec3_t right2DDir ) {
	for( unsigned i = 0; i < 6; ++i ) {
		auto &fullResult = results[i + 0];
		auto &jumpableResult = results[i + 6];
		fullResult.trace.fraction = 1.0f;
		fullResult.trace.fraction = 1.0f;
		// We have to save a legal trace dir
		MakeTraceDir( i, front2DDir, right2DDir, fullResult.traceDir );
		VectorCopy( fullResult.traceDir, jumpableResult.traceDir );
	}
	resultsMask |= ALL_SIDES_MASK;
	hasNoFullHeightObstaclesAround = true;
}

void EnvironmentTraceCache::SetJumpableHeightCachedTracesEmpty( const vec3_t front2DDir, const vec3_t right2DDir ) {
	for( unsigned i = 0; i < 6; ++i ) {
		auto &result = results[i + 6];
		result.trace.fraction = 1.0f;
		// We have to save a legal trace dir
		MakeTraceDir( i, front2DDir, right2DDir, result.traceDir );
	}
	resultsMask |= JUMPABLE_SIDES_MASK;
}

inline bool EnvironmentTraceCache::CanSkipTracingForAreaHeight( const vec3_t origin,
																const aas_area_t &area,
																float minZOffset ) {
	if( area.mins[2] >= origin[2] + minZOffset ) {
		return false;
	}
	if( area.maxs[2] <= origin[2] + playerbox_stand_maxs[2] ) {
		return false;
	}

	return true;
}

const int EnvironmentTraceCache::sideDirXYMoves[8][2] =
	{
		{ +1, +0 }, // forward
		{ -1, +0 }, // back
		{ +0, -1 }, // left
		{ +0, +1 }, // right
		{ +1, -1 }, // front left
		{ +1, +1 }, // front right
		{ -1, -1 }, // back left
		{ -1, +1 }, // back right
	};

const float EnvironmentTraceCache::sideDirXYFractions[8][2] =
	{
		{ +1.000f, +0.000f }, // front
		{ -1.000f, +0.000f }, // back
		{ +0.000f, -1.000f }, // left
		{ +0.000f, +1.000f }, // right
		{ +0.707f, -0.707f }, // front left
		{ +0.707f, +0.707f }, // front right
		{ -0.707f, -0.707f }, // back left
		{ -0.707f, +0.707f }, // back right
	};

inline void EnvironmentTraceCache::MakeTraceDir( unsigned dirNum, const vec3_t front2DDir,
												 const vec3_t right2DDir, vec3_t traceDir ) {
	const float *dirFractions = sideDirXYFractions[dirNum];
	VectorScale( front2DDir, dirFractions[0], traceDir );
	VectorMA( traceDir, dirFractions[1], right2DDir, traceDir );
	VectorNormalizeFast( traceDir );
}

bool EnvironmentTraceCache::TrySkipTracingForCurrOrigin( Context *context, const vec3_t front2DDir, const vec3_t right2DDir ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const int areaNum = entityPhysicsState.CurrAasAreaNum();
	if( !areaNum ) {
		return false;
	}

	const auto *aasWorld = AiAasWorld::Instance();
	const auto &area = aasWorld->Areas()[areaNum];
	const auto &areaSettings = aasWorld->AreaSettings()[areaNum];
	const float *origin = entityPhysicsState.Origin();

	// Extend playerbox XY bounds by TRACE_DEPTH
	Vec3 mins( origin[0] - TRACE_DEPTH, origin[1] - TRACE_DEPTH, origin[2] );
	Vec3 maxs( origin[0] + TRACE_DEPTH, origin[1] + TRACE_DEPTH, origin[2] );
	mins += playerbox_stand_mins;
	maxs += playerbox_stand_maxs;

	// We have to add some offset to the area bounds (an area is not always a box)
	const float areaBoundsOffset = ( areaSettings.areaflags & AREA_WALL ) ? 40.0f : 16.0f;

	int sideNum = 0;
	for(; sideNum < 2; ++sideNum ) {
		if( area.mins[sideNum] + areaBoundsOffset >= mins.Data()[sideNum] ) {
			break;
		}
		if( area.maxs[sideNum] + areaBoundsOffset <= maxs.Data()[sideNum] ) {
			break;
		}
	}

	// If the area bounds test has lead to conclusion that there is enough free space in side directions
	if( sideNum == 2 ) {
		if( CanSkipTracingForAreaHeight( origin, area, playerbox_stand_mins[2] + 0.25f ) ) {
			SetFullHeightCachedTracesEmpty( front2DDir, right2DDir );
			return true;
		}

		if( CanSkipTracingForAreaHeight( origin, area, playerbox_stand_maxs[2] + AI_JUMPABLE_HEIGHT - 0.5f ) ) {
			SetJumpableHeightCachedTracesEmpty( front2DDir, right2DDir );
			// We might still need to perform full height traces in TestForResultsMask()
			return false;
		}
	}

	// Test bounds around the origin.
	// Doing these tests can save expensive trace calls for separate directions

	// Convert these bounds to relative for being used as trace args
	mins -= origin;
	maxs -= origin;

	trace_t trace;
	mins.Z() += 0.25f;
	StaticWorldTrace( &trace, origin, origin, MASK_SOLID | MASK_WATER, mins.Data(), maxs.Data() );
	if( trace.fraction == 1.0f ) {
		SetFullHeightCachedTracesEmpty( front2DDir, right2DDir );
		return true;
	}

	mins.Z() += AI_JUMPABLE_HEIGHT - 1.0f;
	StaticWorldTrace( &trace, origin, origin, MASK_SOLID | MASK_WATER, mins.Data(), maxs.Data() );
	if( trace.fraction == 1.0f ) {
		SetJumpableHeightCachedTracesEmpty( front2DDir, right2DDir );
		// We might still need to perform full height traces in TestForResultsMask()
		return false;
	}

	return false;
}

void EnvironmentTraceCache::TestForResultsMask( Context *context, unsigned requiredResultsMask ) {
	// There must not be any extra bits
	Assert( ( requiredResultsMask & ~ALL_SIDES_MASK ) == 0 );
	// All required traces have been already cached
	if( ( this->resultsMask & requiredResultsMask ) == requiredResultsMask ) {
		return;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	vec3_t front2DDir, right2DDir, traceEnd;
	Vec3 angles( entityPhysicsState.Angles() );
	angles.Data()[PITCH] = 0.0f;
	AngleVectors( angles.Data(), front2DDir, right2DDir, nullptr );

	if( !this->didAreaTest ) {
		this->didAreaTest = true;
		if( TrySkipTracingForCurrOrigin( context, front2DDir, right2DDir ) ) {
			return;
		}
	}

	const float *origin = entityPhysicsState.Origin();

	// First, test all full side traces.
	// If a full side trace is empty, a corresponding "jumpable" side trace can be set as empty too.

	// Test these bits for a quick computations shortcut
	unsigned actualFullSides = this->resultsMask & FULL_SIDES_MASK;
	unsigned resultFullSides = requiredResultsMask & FULL_SIDES_MASK;
	if( ( actualFullSides & resultFullSides ) != resultFullSides ) {
		const unsigned endMask = FullHeightMask( LAST_SIDE );
		for( unsigned i = 0, mask = FullHeightMask( FIRST_SIDE ); mask <= endMask; ++i, mask *= 2 ) {
			if( !( mask & requiredResultsMask ) || ( mask & this->resultsMask ) ) {
				continue;
			}

			MakeTraceDir( i, front2DDir, right2DDir, traceEnd );
			// Save the trace dir
			auto &fullResult = results[i];
			VectorCopy( traceEnd, fullResult.traceDir );
			// Convert from a direction to the end point
			VectorScale( traceEnd, TRACE_DEPTH, traceEnd );
			VectorAdd( traceEnd, origin, traceEnd );
			StaticWorldTrace( &fullResult.trace, origin, traceEnd, MASK_SOLID | MASK_WATER,
							  playerbox_stand_mins, playerbox_stand_maxs );
			this->resultsMask |= mask;
			// If full trace is empty, we can set partial trace as empty too
			if( fullResult.trace.fraction == 1.0f ) {
				auto &jumpableResult = results[i + 6];
				jumpableResult.trace.fraction = 1.0f;
				VectorCopy( fullResult.traceDir, jumpableResult.traceDir );
				this->resultsMask |= ( mask << 6 );
			}
		}
	}

	unsigned actualJumpableSides = this->resultsMask & JUMPABLE_SIDES_MASK;
	unsigned resultJumpableSides = requiredResultsMask & JUMPABLE_SIDES_MASK;
	if( ( actualJumpableSides & resultJumpableSides ) != resultJumpableSides ) {
		Vec3 mins( playerbox_stand_mins );
		mins.Z() += AI_JUMPABLE_HEIGHT;
		const unsigned endMask = JumpableHeightMask( LAST_SIDE );
		for( unsigned i = 0, mask = JumpableHeightMask( FIRST_SIDE ); mask <= endMask; ++i, mask *= 2 ) {
			if( !( mask & requiredResultsMask ) || ( mask & this->resultsMask ) ) {
				continue;
			}

			MakeTraceDir( i, front2DDir, right2DDir, traceEnd );
			// Save the trace dir
			auto &result = results[i + 6];
			VectorCopy( traceEnd, result.traceDir );
			// Convert from a direction to the end point
			VectorScale( traceEnd, TRACE_DEPTH, traceEnd );
			VectorAdd( traceEnd, origin, traceEnd );
			StaticWorldTrace( &result.trace, origin, traceEnd, MASK_SOLID | MASK_WATER, mins.Data(), playerbox_stand_maxs );
			this->resultsMask |= mask;
		}
	}

	// Check whether all requires side traces have been computed
	Assert( ( this->resultsMask & requiredResultsMask ) == requiredResultsMask );
}

bool EnvironmentTraceCache::CanSkipPMoveCollision( Context *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	// We might still need to check steps even if there is no full height obstacles around.
	if( entityPhysicsState.GroundEntity() ) {
		return false;
	}

	// If the bot does not move upwards
	if( entityPhysicsState.HeightOverGround() <= 12.0f && entityPhysicsState.Velocity()[2] <= 10 ) {
		return false;
	}

	const float expectedShift = entityPhysicsState.Speed() * context->predictionStepMillis * 0.001f;
	const int areaFlags = aasAreaSettings[entityPhysicsState.CurrAasAreaNum()].areaflags;
	// All greater shift flags imply this (and other lesser ones flags) flag being set too
	if( areaFlags & AREA_SKIP_COLLISION_16 ) {
		const float precomputedShifts[3] = { 16.0f, 32.0f, 48.0f };
		const int flagsForShifts[3] = { AREA_SKIP_COLLISION_16, AREA_SKIP_COLLISION_32, AREA_SKIP_COLLISION_48 };
		// Start from the minimal shift
		for( int i = 0; i < 3; ++i ) {
			if( ( expectedShift < precomputedShifts[i] ) && ( areaFlags & flagsForShifts[i] ) ) {
				return true;
			}
		}
	}

	// Do not force computations in this case.
	// Otherwise there is no speedup shown according to testing results.
	if( !this->didAreaTest ) {
		return false;
	}

	// Return the already computed result
	return this->hasNoFullHeightObstaclesAround;
}

// Make a type alias to fit into a line length limit
typedef EnvironmentTraceCache::ObstacleAvoidanceResult ObstacleAvoidanceResult;

ObstacleAvoidanceResult EnvironmentTraceCache::TryAvoidObstacles( Context *context,
																  Vec3 *intendedLookVec,
																  float correctionFraction,
																  unsigned sidesShift ) {
	TestForResultsMask( context, FRONT << sidesShift );
	const TraceResult &frontResult = results[0 + sidesShift];
	if( frontResult.trace.fraction == 1.0f ) {
		return ObstacleAvoidanceResult::NO_OBSTACLES;
	}

	TestForResultsMask( context, ( LEFT | RIGHT | FRONT_LEFT | FRONT_RIGHT ) << sidesShift );

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	Vec3 velocityDir2D( entityPhysicsState.Velocity()[0], entityPhysicsState.Velocity()[1], 0 );
	velocityDir2D *= 1.0f / entityPhysicsState.Speed2D();

	// Make velocity direction dot product affect score stronger for lower correction fraction (for high speed)

	// This weight corresponds to a kept part of a trace fraction
	const float alpha = 0.51f + 0.24f * correctionFraction;
	// This weight corresponds to an added or subtracted part of a trace fraction multiplied by the dot product
	const float beta = 0.49f - 0.24f * correctionFraction;
	// Make sure a score is always positive
	Assert( alpha > beta );
	// Make sure that score is kept as is for the maximal dot product
	Assert( fabsf( alpha + beta - 1.0f ) < 0.0001f );

	float maxScore = frontResult.trace.fraction * ( alpha + beta * velocityDir2D.Dot( results[0 + sidesShift].traceDir ) );
	const TraceResult *bestTraceResult = nullptr;

	for( unsigned i = 2; i <= 5; ++i ) {
		const TraceResult &result = results[i + sidesShift];
		float score = result.trace.fraction;
		// Make sure that non-blocked directions are in another category
		if( score == 1.0f ) {
			score *= 3.0f;
		}

		score *= alpha + beta * velocityDir2D.Dot( result.traceDir );
		if( score <= maxScore ) {
			continue;
		}

		maxScore = score;
		bestTraceResult = &result;
	}

	if( bestTraceResult ) {
		intendedLookVec->NormalizeFast();
		*intendedLookVec *= ( 1.0f - correctionFraction );
		VectorMA( intendedLookVec->Data(), correctionFraction, bestTraceResult->traceDir, intendedLookVec->Data() );
		// There is no need to normalize intendedLookVec (we had to do it for correction fraction application)
		return ObstacleAvoidanceResult::CORRECTED;
	}

	return ObstacleAvoidanceResult::KEPT_AS_IS;
}