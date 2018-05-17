#include "WorldState.h"

#ifndef PUBLIC_BUILD

WorldState::WorldState( edict_t *self_ ) {
	// Shut up an analyzer
	memset( this, 0, sizeof( WorldState ) );

	// Initialize these fields after the memset() call
	this->self = self_;
	this->isCopiedFromOtherWorldState = false;

	// If state bits are not initialized, vars often does not get printed in debug output.
	// This is useful for release non-public builds too, not only for debug ones.
	for( unsigned i = 0; i < NUM_ORIGIN_VARS; ++i ) {
		auto *packedFields = (OriginVar::PackedFields *)&originVarsData[i * 4 + 3];
		packedFields->ignore = true;
		packedFields->epsilon = 1;
		packedFields->satisfyOp = (unsigned char)SatisfyOp::EQ;
	}

	for( unsigned i = 0; i < NUM_ORIGIN_LAZY_VARS; ++i ) {
		auto *packedFields = (OriginLazyVarBase::PackedFields * )&originLazyVarsData[i * 4 + 3];
		packedFields->stateBits = OriginLazyVarBase::PENDING;
		packedFields->ignore = true;
		packedFields->epsilon = 1;
		packedFields->satisfyOp = (unsigned char)SatisfyOp::EQ;
	}

	for( unsigned i = 0; i < NUM_DUAL_ORIGIN_LAZY_VARS; ++i ) {
		auto *packedFields = (DualOriginLazyVar::PackedFields * )&dualOriginLazyVarsData[i * 4 + 3];
		packedFields->stateBits = OriginLazyVarBase::PENDING;
		packedFields->ignore = true;
		packedFields->epsilon = 1;
		packedFields->satisfyOp = (unsigned char)SatisfyOp::EQ;
	}

	scriptAttachment = GENERIC_asNewScriptWorldStateAttachment( self_ );
}

#endif

void WorldState::SetIgnoreAll( bool ignore ) {
	if( ignore ) {
		unsignedVarsIgnoreFlags = std::numeric_limits<decltype( unsignedVarsIgnoreFlags )>::max();
		floatVarsIgnoreFlags = std::numeric_limits<decltype( floatVarsIgnoreFlags )>::max();
		shortVarsIgnoreFlags = std::numeric_limits<decltype( shortVarsIgnoreFlags )>::max();
		boolVarsIgnoreFlags = std::numeric_limits<decltype( boolVarsIgnoreFlags )>::max();
	} else {
		unsignedVarsIgnoreFlags = 0;
		floatVarsIgnoreFlags = 0;
		shortVarsIgnoreFlags = 0;
		boolVarsIgnoreFlags = 0;
	}

	for( unsigned i = 0; i < NUM_ORIGIN_VARS; ++i )
		( (OriginVar::PackedFields *)&originVarsData[i * 4 + 3] )->ignore = ignore;

	for( unsigned i = 0; i < NUM_ORIGIN_LAZY_VARS; ++i )
		( (OriginLazyVar::PackedFields *)&originLazyVarsData[i * 4 + 3] )->ignore = ignore;

	for( unsigned i = 0; i < NUM_DUAL_ORIGIN_LAZY_VARS; ++i )
		( (DualOriginLazyVar::PackedFields *)&dualOriginLazyVarsData[i * 4 + 3] )->ignore = ignore;

	if( scriptAttachment ) {
		GENERIC_asSetScriptWorldStateAttachmentIgnoreAllVars( scriptAttachment, ignore );
	}
}

// Use this macro so one have to write condition that matches the corresponding case and not its negation
#define TEST_OR_FAIL( condition )  \
	do                               \
	{                                \
		if( !( condition ) ) {            \
			return false; }            \
	}                                \
	while( 0 )

template <typename T>
static inline bool TestCareFlags( T thisFlags, T thatFlags ) {
	T careMask = thisFlags ^ std::numeric_limits<T>::max();
	T thatCareMask = thatFlags ^ std::numeric_limits<T>::max();
	// There are vars that this world state cares about and that one do not
	if( careMask & ~thatCareMask ) {
		return false;
	}

	return true;
}

#define TEST_GENERIC_COMPARABLE_VARS_SATISFACTION( values, flags, ops )             \
	do                                                                                \
	{                                                                                 \
		if( !TestCareFlags( flags, that.flags ) ) { return false; }                          \
		decltype( flags )mask = 1;                                                     \
		for( unsigned i = 0; i < sizeof( values ) / sizeof( values[0] ); ++i, mask <<= 1 )     \
		{                                                                             \
			if( flags & mask ) { continue; }                                               \
			switch( this->GetVarSatisfyOp( ops, i ) )                                    \
			{                                                                         \
				case SatisfyOp::EQ: TEST_OR_FAIL( values[i] == that.values[i] ); break; \
				case SatisfyOp::NE: TEST_OR_FAIL( values[i] != that.values[i] ); break; \
				case SatisfyOp::GT: TEST_OR_FAIL( values[i] > that.values[i] ); break;  \
				case SatisfyOp::GE: TEST_OR_FAIL( values[i] >= that.values[i] ); break; \
				case SatisfyOp::LS: TEST_OR_FAIL( values[i] < that.values[i] ); break;  \
				case SatisfyOp::LE: TEST_OR_FAIL( values[i] <= that.values[i] ); break; \
			}                                                                         \
		}                                                                             \
	} while( 0 )

bool WorldState::IsSatisfiedBy( const WorldState &that ) const {
	// Test bool vars first since it is cheaper and would reject non-matching `that`state quickly
	if( !TestCareFlags( boolVarsIgnoreFlags, that.boolVarsIgnoreFlags ) ) {
		return false;
	}

	auto boolVarsCareMask = boolVarsIgnoreFlags ^ std::numeric_limits<decltype( boolVarsIgnoreFlags )>::max();
	// If values masked for this ignore flags do not match
	if( ( boolVarsValues & boolVarsCareMask ) != ( that.boolVarsValues & boolVarsCareMask ) ) {
		return false;
	}

	TEST_GENERIC_COMPARABLE_VARS_SATISFACTION( unsignedVarsValues, unsignedVarsIgnoreFlags, unsignedVarsSatisfyOps );
	static_assert( NUM_FLOAT_VARS == 0, "Implement satisfaction tests for float vars" );
	TEST_GENERIC_COMPARABLE_VARS_SATISFACTION( shortVarsValues, shortVarsIgnoreFlags, shortVarsSatisfyOps );

	for( int i = 0, offset = 0; i < NUM_ORIGIN_VARS; ++i, offset += 4 ) {
		const OriginVar::PackedFields &packed = *(OriginVar::PackedFields *)( &originVarsData[offset + 3] );
		const OriginVar::PackedFields &thatPacked = *(OriginVar::PackedFields *)( &that.originVarsData[offset + 3] );

		if( packed.ignore ) {
			continue;
		}
		if( thatPacked.ignore ) {
			return false;
		}

		const float epsilon = packed.epsilon * 4.0f;
		const short *thisOriginData = originVarsData + offset;
		const short *thatOriginData = that.originVarsData + offset;

		switch( (SatisfyOp)packed.satisfyOp ) {
			case SatisfyOp::EQ:
				if( DistanceSquared( thisOriginData, thatOriginData ) > epsilon * epsilon ) {
					return false;
				}
				break;
			case SatisfyOp::NE:
				if( DistanceSquared( thisOriginData, thatOriginData ) < epsilon * epsilon ) {
					return false;
				}
				break;
			default:
				AI_FailWith( "WorldState::IsSatisfiedBy()", "Illegal packed.satisfyOp bits: %d\n", (int)packed.satisfyOp );
		}
	}

	if( !SniperRangeTacticalSpotVar().IsSatisfiedBy( that.SniperRangeTacticalSpotVar() ) ) {
		return false;
	}
	if( !FarRangeTacticalSpotVar().IsSatisfiedBy( that.FarRangeTacticalSpotVar() ) ) {
		return false;
	}
	if( !MiddleRangeTacticalSpotVar().IsSatisfiedBy( that.MiddleRangeTacticalSpotVar() ) ) {
		return false;
	}
	if( !CloseRangeTacticalSpotVar().IsSatisfiedBy( that.CloseRangeTacticalSpotVar() ) ) {
		return false;
	}
	if( !CoverSpotVar().IsSatisfiedBy( that.CoverSpotVar() ) ) {
		return false;
	}

	if( !RunAwayTeleportOriginVar().IsSatisfiedBy( that.RunAwayTeleportOriginVar() ) ) {
		return false;
	}
	if( !RunAwayJumppadOriginVar().IsSatisfiedBy( that.RunAwayJumppadOriginVar() ) ) {
		return false;
	}
	if( !RunAwayElevatorOriginVar().IsSatisfiedBy( that.RunAwayElevatorOriginVar() ) ) {
		return false;
	}

	if( !scriptAttachment ) {
		return true;
	}

	return GENERIC_asIsScriptWorldStateAttachmentSatisfiedBy( scriptAttachment, that.scriptAttachment );
}

uint32_t WorldState::Hash() const {
	uint32_t result = 37;

	decltype( unsignedVarsIgnoreFlags )unsignedVarsMask = 1;
	for( int i = 0; i < NUM_UNSIGNED_VARS; ++i, unsignedVarsMask <<= 1 ) {
		if( unsignedVarsIgnoreFlags & unsignedVarsMask ) {
			continue;
		}
		result = result * 17 + unsignedVarsValues[i];
		result = result * 17 + (unsigned)GetVarSatisfyOp( unsignedVarsSatisfyOps, i ) + 1;
	}

	static_assert( NUM_FLOAT_VARS == 0, "Implement hashing for float vars" );

	decltype( shortVarsIgnoreFlags )shortVarsMask = 1;
	for( int i = 0; i < NUM_SHORT_VARS; ++i ) {
		if( shortVarsIgnoreFlags & shortVarsMask ) {
			result = result * 17 + shortVarsValues[i];
			result = result * 17 + (unsigned)GetVarSatisfyOp( shortVarsSatisfyOps, i ) + 1;
		}
	}

	result = result * 17;
	result += boolVarsValues & ( boolVarsIgnoreFlags ^ std::numeric_limits<decltype( boolVarsIgnoreFlags )>::max() );

	for( int i = 0; i < NUM_ORIGIN_VARS; ++i ) {
		const auto &packed = *(OriginVar::PackedFields *)&originVarsData[i * 4 + 3];
		if( !( packed.ignore ) ) {
			for( int j = 0; j < 4; ++j )
				result = result * 17 + originVarsData[i * 4 + j];
		}
	}

	result = result * 17 + SniperRangeTacticalSpotVar().Hash();
	result = result * 17 + FarRangeTacticalSpotVar().Hash();
	result = result * 17 + MiddleRangeTacticalSpotVar().Hash();
	result = result * 17 + CloseRangeTacticalSpotVar().Hash();
	result = result * 17 + CoverSpotVar().Hash();

	result = result * 17 + RunAwayTeleportOriginVar().Hash();
	result = result * 17 + RunAwayJumppadOriginVar().Hash();
	result = result * 17 + RunAwayElevatorOriginVar().Hash();

	if( !scriptAttachment ) {
		return result;
	}

	return result * 17 + (uint32_t)GENERIC_asScriptWorldStateAttachmentHash( scriptAttachment );
}

#define TEST_VARS_EQUALITY( values, flags, ops )                           \
	do                                                                       \
	{                                                                        \
		if( flags != that.flags ) {                                             \
			return false; }                                                    \
		decltype( flags )mask = 1;                                            \
		for( unsigned i = 0; i < sizeof( values ) / sizeof( values[0] ); ++i )         \
		{                                                                    \
			if( !( flags & mask ) )                                             \
			{                                                                \
				if( values[i] != that.values[i] ) {                             \
					return false; }                                            \
				if( GetVarSatisfyOp( ops, i ) != GetVarSatisfyOp( that.ops, i ) ) { \
					return false; }                                            \
			}                                                                \
			mask <<= 1;                                                      \
		}                                                                    \
	}                                                                        \
	while( 0 )

bool WorldState::operator==( const WorldState &that ) const {
	// Test bool vars first since it is cheaper and would reject non-matching `that` state quickly

	if( boolVarsIgnoreFlags != that.boolVarsIgnoreFlags ) {
		return false;
	}

	auto boolVarsCareFlags = boolVarsIgnoreFlags ^ std::numeric_limits<decltype( boolVarsIgnoreFlags )>::max();
	if( ( boolVarsValues & boolVarsCareFlags ) != ( that.boolVarsValues & boolVarsCareFlags ) ) {
		return false;
	}

	TEST_VARS_EQUALITY( unsignedVarsValues, unsignedVarsIgnoreFlags, unsignedVarsSatisfyOps );
	static_assert( NUM_FLOAT_VARS == 0, "Implement equality tests for float vars" );
	TEST_VARS_EQUALITY( shortVarsValues, shortVarsIgnoreFlags, shortVarsSatisfyOps );

	for( int i = 0, offset = 0; i < NUM_ORIGIN_VARS; ++i, offset += 4 ) {
		const auto &packed = *( (OriginVar::PackedFields *)&originVarsData[offset + 3] );
		const auto &thatPacked = *( (OriginVar::PackedFields *)&that.originVarsData[offset + 3] );
		if( !packed.ignore ) {
			if( thatPacked.ignore ) {
				return false;
			}

			for( int j = 0; j < 4; ++j ) {
				if( originVarsData[offset + j] != that.originVarsData[offset + j] ) {
					return false;
				}
			}
		} else if( !thatPacked.ignore ) {
			return false;
		}
	}

	if( SniperRangeTacticalSpotVar() != that.SniperRangeTacticalSpotVar() ) {
		return false;
	}
	if( FarRangeTacticalSpotVar() != that.FarRangeTacticalSpotVar() ) {
		return false;
	}
	if( MiddleRangeTacticalSpotVar() != that.MiddleRangeTacticalSpotVar() ) {
		return false;
	}
	if( CloseRangeTacticalSpotVar() != that.CloseRangeTacticalSpotVar() ) {
		return false;
	}
	if( CoverSpotVar() != that.CoverSpotVar() ) {
		return false;
	}

	if( RunAwayTeleportOriginVar() != that.RunAwayTeleportOriginVar() ) {
		return false;
	}
	if( RunAwayJumppadOriginVar() != that.RunAwayJumppadOriginVar() ) {
		return false;
	}
	if( RunAwayElevatorOriginVar() != that.RunAwayElevatorOriginVar() ) {
		return false;
	}

	if( !scriptAttachment ) {
		return true;
	}

	return GENERIC_asScriptWorldStateAttachmentEquals( scriptAttachment, that.scriptAttachment );
}

void WorldState::DebugPrint( const char *tag ) const {
	// We have to list all vars manually
	// A list of vars does not (and should not) exist
	// since vars instances do not (and should not) exist in optimized by a compiler code
	// (WorldState members are accessed directly instead)

	GoalItemWaitTimeVar().DebugPrint( tag );
	SimilarWorldStateInstanceIdVar().DebugPrint( tag );

	HealthVar().DebugPrint( tag );
	ArmorVar().DebugPrint( tag );
	RawDamageToKillVar().DebugPrint( tag );

	BotOriginVar().DebugPrint( tag );
	EnemyOriginVar().DebugPrint( tag );
	NavTargetOriginVar().DebugPrint( tag );
	PendingOriginVar().DebugPrint( tag );

	HasQuadVar().DebugPrint( tag );
	HasShellVar().DebugPrint( tag );
	EnemyHasQuadVar().DebugPrint( tag );
	HasThreateningEnemyVar().DebugPrint( tag );
	HasJustPickedGoalItemVar().DebugPrint( tag );

	HasPositionalAdvantageVar().DebugPrint( tag );
	CanHitEnemyVar().DebugPrint( tag );
	EnemyCanHitVar().DebugPrint( tag );
	HasJustKilledEnemyVar().DebugPrint( tag );

	IsRunningAwayVar().DebugPrint( tag );
	HasRunAwayVar().DebugPrint( tag );

	HasJustTeleportedVar().DebugPrint( tag );
	HasJustTouchedJumppadVar().DebugPrint( tag );
	HasJustEnteredElevatorVar().DebugPrint( tag );

	HasPendingCoverSpotVar().DebugPrint( tag );
	HasPendingRunAwayTeleportVar().DebugPrint( tag );
	HasPendingRunAwayJumppadVar().DebugPrint( tag );
	HasPendingRunAwayElevatorVar().DebugPrint( tag );

	HasGoodSniperRangeWeaponsVar().DebugPrint( tag );
	HasGoodFarRangeWeaponsVar().DebugPrint( tag );
	HasGoodMiddleRangeWeaponsVar().DebugPrint( tag );
	HasGoodCloseRangeWeaponsVar().DebugPrint( tag );

	EnemyHasGoodSniperRangeWeaponsVar().DebugPrint( tag );
	EnemyHasGoodFarRangeWeaponsVar().DebugPrint( tag );
	EnemyHasGoodMiddleRangeWeaponsVar().DebugPrint( tag );
	EnemyHasGoodCloseRangeWeaponsVar().DebugPrint( tag );

	SniperRangeTacticalSpotVar().DebugPrint( tag );
	FarRangeTacticalSpotVar().DebugPrint( tag );
	MiddleRangeTacticalSpotVar().DebugPrint( tag );
	CloseRangeTacticalSpotVar().DebugPrint( tag );

	RunAwayTeleportOriginVar().DebugPrint( tag );
	RunAwayJumppadOriginVar().DebugPrint( tag );
	RunAwayElevatorOriginVar().DebugPrint( tag );

	if( scriptAttachment ) {
		GENERIC_asDebugPrintScriptWorldStateAttachment( scriptAttachment );
	}
}

#define PRINT_DIFF( varName )                             \
	do                                                      \
	{                                                       \
		if( this->varName ## Var() != that.varName ## Var() )    \
		{                                                   \
			this->varName ## Var().DebugPrint( oldTag );        \
			that.varName ## Var().DebugPrint( newTag );         \
		}                                                   \
	} while( 0 )

void WorldState::DebugPrintDiff( const WorldState &that, const char *oldTag, const char *newTag ) const {
	PRINT_DIFF( GoalItemWaitTime );
	PRINT_DIFF( SimilarWorldStateInstanceId );

	PRINT_DIFF( Health );
	PRINT_DIFF( Armor );
	PRINT_DIFF( RawDamageToKill );

	PRINT_DIFF( BotOrigin );
	PRINT_DIFF( EnemyOrigin );
	PRINT_DIFF( NavTargetOrigin );
	PRINT_DIFF( PendingOrigin );

	PRINT_DIFF( HasQuad );
	PRINT_DIFF( HasShell );
	PRINT_DIFF( EnemyHasQuad );
	PRINT_DIFF( HasThreateningEnemy );
	PRINT_DIFF( HasJustPickedGoalItem );

	PRINT_DIFF( HasPositionalAdvantage );
	PRINT_DIFF( CanHitEnemy );
	PRINT_DIFF( EnemyCanHit );
	PRINT_DIFF( HasJustKilledEnemy );

	PRINT_DIFF( IsRunningAway );
	PRINT_DIFF( HasRunAway );

	PRINT_DIFF( HasJustTeleported );
	PRINT_DIFF( HasJustTouchedJumppad );
	PRINT_DIFF( HasJustEnteredElevator );

	PRINT_DIFF( HasPendingCoverSpot );
	PRINT_DIFF( HasPendingRunAwayTeleport );
	PRINT_DIFF( HasPendingRunAwayJumppad );
	PRINT_DIFF( HasPendingRunAwayElevator );

	PRINT_DIFF( HasGoodSniperRangeWeapons );
	PRINT_DIFF( HasGoodFarRangeWeapons );
	PRINT_DIFF( HasGoodSniperRangeWeapons );
	PRINT_DIFF( HasGoodCloseRangeWeapons );

	PRINT_DIFF( EnemyHasGoodSniperRangeWeapons );
	PRINT_DIFF( EnemyHasGoodFarRangeWeapons );
	PRINT_DIFF( EnemyHasGoodSniperRangeWeapons );
	PRINT_DIFF( EnemyHasGoodCloseRangeWeapons );

	PRINT_DIFF( SniperRangeTacticalSpot );
	PRINT_DIFF( FarRangeTacticalSpot );
	PRINT_DIFF( MiddleRangeTacticalSpot );
	PRINT_DIFF( CloseRangeTacticalSpot );

	PRINT_DIFF( RunAwayTeleportOrigin );
	PRINT_DIFF( RunAwayJumppadOrigin );
	PRINT_DIFF( RunAwayElevatorOrigin );

	if( scriptAttachment ) {
		GENERIC_asDebugPrintScriptWorldStateAttachmentDiff( scriptAttachment, that.scriptAttachment );
	}
}
