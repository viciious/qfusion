#ifndef QFUSION_WORLD_STATE_H
#define QFUSION_WORLD_STATE_H

#include "../ai_local.h"

float DamageToKill( float health, float armor, float armorProtection, float armorDegradation );

inline float DamageToKill( float health, float armor ) {
	return DamageToKill( health, armor, g_armor_protection->value, g_armor_degradation->value );
}

#define DECLARE_COMPARABLE_VAR_CLASS( className, type )                           \
	class className                                                                 \
	{                                                                               \
		friend class WorldState;                                                    \
protected:                                                                      \
		WorldState *parent;                                                         \
		const char *name;                                                           \
		short index;                                                                \
		className( const WorldState *parent_, short index_, const char *name_ )       \
			: parent( const_cast<WorldState *>( parent_ ) ), name( name_ ), index( index_ ) \
		{}                                                                          \
public:                                                                         \
		inline const type &Value() const                                            \
		{                                                                           \
			return parent->type ## VarsValues[index];                                 \
		}                                                                           \
		inline className &SetValue( type value )                                      \
		{                                                                           \
			parent->type ## VarsValues[index] = value; return *this;                  \
		}                                                                           \
		inline operator type() const { return parent->type ## VarsValues[index]; }    \
		inline bool Ignore() const                                                  \
		{                                                                           \
			return ( parent->type ## VarsIgnoreFlags & ( 1 << index ) ) != 0;             \
		}                                                                           \
		inline className &SetIgnore( bool ignore )                                    \
		{                                                                           \
			if( ignore ) {                                                             \
				parent->type ## VarsIgnoreFlags |= 1 << index; }                        \
			else {                                                                    \
				parent->type ## VarsIgnoreFlags &= ~( 1 << index ); }                     \
			return *this;                                                           \
		}                                                                           \
		inline WorldState::SatisfyOp SatisfyOp() const                              \
		{                                                                           \
			return parent->GetVarSatisfyOp( parent->type ## VarsSatisfyOps, index );    \
		}                                                                           \
		inline className &SetSatisfyOp( WorldState::SatisfyOp op )                    \
		{                                                                           \
			parent->SetVarSatisfyOp( parent->type ## VarsSatisfyOps, index, op );       \
			return *this;                                                           \
		}                                                                           \
		inline bool IsSatisfiedBy( type value ) const                                 \
		{                                                                           \
			switch( parent->GetVarSatisfyOp( parent->type ## VarsSatisfyOps, index ) )   \
			{                                                                       \
				case WorldState::SatisfyOp::EQ: return Value() == value;            \
				case WorldState::SatisfyOp::NE: return Value() != value;            \
				case WorldState::SatisfyOp::GT: return Value() > value;             \
				case WorldState::SatisfyOp::GE: return Value() >= value;            \
				case WorldState::SatisfyOp::LS: return Value() < value;             \
				case WorldState::SatisfyOp::LE: return Value() <= value;            \
			}                                                                       \
		}                                                                           \
		inline void DebugPrint( const char *tag ) const;                              \
	}

void *GENERIC_asNewScriptWorldStateAttachment( const edict_t *self );
void GENERIC_asDeleteScriptWorldStateAttachment( const edict_t *self, void *attachment );
void *GENERIC_asCopyScriptWorldStateAttachment( const edict_t *self, const void *attachment );
void GENERIC_asSetScriptWorldStateAttachmentIgnoreAllVars( void *attachment, bool ignore );
// A native WorldState is provided too (sometimes you want to modify it in scripts).
void GENERIC_asPrepareScriptWorldStateAttachment( const edict_t *self, WorldState *worldState, void *attachment );
unsigned GENERIC_asScriptWorldStateAttachmentHash( const void *attachment );
bool GENERIC_asScriptWorldStateAttachmentEquals( const void *lhs, const void *rhs );
bool GENERIC_asIsScriptWorldStateAttachmentSatisfiedBy( const void *lhs, const void *rhs );
void GENERIC_asDebugPrintScriptWorldStateAttachment( const void *attachment );
void GENERIC_asDebugPrintScriptWorldStateAttachmentDiff( const void *lhs, const void *rhs );

class WorldState
{
	friend class FloatBaseVar;
	friend class BoolVar;

public:
	enum class SatisfyOp : unsigned char {
		EQ,
		NE,
		GT,
		GE,
		LS,
		LE
	};

private:
	edict_t *self;
	void *scriptAttachment;
#ifndef PUBLIC_BUILD
	bool isCopiedFromOtherWorldState;
#endif

	// WorldState operations such as copying and testing for satisfaction must be fast,
	// so vars components are stored in separate arrays for tight data packing.
	// Var types visible for external code are just thin wrappers around pointers to these values.

	enum {
		GoalItemWaitTime,
		// Used to make a distinction between world states that are similar (has all other vars matching)
		// but not really identical from human logic point of view
		// (there are some other factors that are not reflected in this world state)
		// Usually these world states are terminal for some other (non-active) goal,
		// and there are many action chains that lead to these world states.
		// A planner would fail on duplicated world states without introduction of this var.
		SimilarWorldStateInstanceId,

		NUM_UNSIGNED_VARS
	};

	enum {
		NUM_FLOAT_VARS
	};

	enum {
		Health,
		Armor,
		RawDamageToKill,
		PotentialDangerDamage,
		ThreatInflictedDamage,

		NUM_SHORT_VARS
	};

	enum {
		HasQuad,
		HasShell,
		EnemyHasQuad,
		HasThreateningEnemy,
		HasJustPickedGoalItem,

		HasPositionalAdvantage,
		CanHitEnemy,
		EnemyCanHit,
		HasJustKilledEnemy,

		IsRunningAway,
		HasRunAway,

		HasReactedToDanger,
		HasReactedToThreat,

		IsReactingToEnemyLost,
		HasReactedToEnemyLost,
		MightSeeLostEnemyAfterTurn,

		HasJustTeleported,
		HasJustTouchedJumppad,
		HasJustEnteredElevator,

		HasPendingCoverSpot,
		HasPendingRunAwayTeleport,
		HasPendingRunAwayJumppad,
		HasPendingRunAwayElevator,

		HasGoodSniperRangeWeapons,
		HasGoodFarRangeWeapons,
		HasGoodMiddleRangeWeapons,
		HasGoodCloseRangeWeapons,

		EnemyHasGoodSniperRangeWeapons,
		EnemyHasGoodFarRangeWeapons,
		EnemyHasGoodMiddleRangeWeapons,
		EnemyHasGoodCloseRangeWeapons,

		NUM_BOOL_VARS
	};

	enum {
		BotOrigin,
		EnemyOrigin,
		NavTargetOrigin,
		PendingOrigin,

		DangerHitPoint,
		DangerDirection,
		// There are no reasons to make it lazy since it always gets computed
		// if a danger is present because ReactToDanger is a very high priority goal
		DodgeDangerSpot,
		ThreatPossibleOrigin,
		LostEnemyLastSeenOrigin,

		NUM_ORIGIN_VARS
	};

	enum {
		SniperRangeTacticalSpot,
		FarRangeTacticalSpot,
		MiddleRangeTacticalSpot,
		CloseRangeTacticalSpot,
		CoverSpot,

		NUM_ORIGIN_LAZY_VARS
	};

	enum {
		RunAwayTeleportOrigin,
		RunAwayJumppadOrigin,
		RunAwayElevatorOrigin,

		NUM_DUAL_ORIGIN_LAZY_VARS
	};

	uint32_t boolVarsValues;
	static_assert( 8 * sizeof( decltype( boolVarsValues ) ) >= NUM_BOOL_VARS, "Values capacity overflow" );

	unsigned unsignedVarsValues[NUM_UNSIGNED_VARS];
	static_assert( !NUM_FLOAT_VARS, "Remove the + 1 MSVC non-zero array size fix" );
	float floatVarsValues[NUM_FLOAT_VARS + 1];
	short shortVarsValues[NUM_SHORT_VARS];
	// Each origin (lazy) var needs a room for value (3 array cells) and misc packed data (1 cell)
	short originVarsData[NUM_ORIGIN_VARS * 4];
	short originLazyVarsData[NUM_ORIGIN_LAZY_VARS * 4];
	// Each dual origin var needs a room for value (3 array cells), misc packed data (1 cell), value 2 (3 cells)
	short dualOriginLazyVarsData[NUM_DUAL_ORIGIN_LAZY_VARS * 7];

	uint32_t boolVarsIgnoreFlags;
	uint8_t unsignedVarsIgnoreFlags;
	uint8_t floatVarsIgnoreFlags;
	uint8_t shortVarsIgnoreFlags;

	static_assert( 8 * ( sizeof( decltype( boolVarsIgnoreFlags ) ) ) >= NUM_BOOL_VARS, "Flags capacity overflow" );
	static_assert( 8 * ( sizeof( decltype( unsignedVarsIgnoreFlags ) ) ) >= NUM_UNSIGNED_VARS, "Flags capacity overflow" );
	static_assert( 8 * ( sizeof( decltype( floatVarsIgnoreFlags ) ) ) >= NUM_FLOAT_VARS, "Flags capacity overflow" );
	static_assert( 8 * ( sizeof( decltype( shortVarsIgnoreFlags ) ) ) >= NUM_SHORT_VARS, "Flags capacity overflow" );

	// 4 bits for a SatisfyOp is enough, pack ops for two vars in a single byte
	uint8_t unsignedVarsSatisfyOps[NUM_UNSIGNED_VARS / 2 + 1];
	uint8_t floatVarsSatisfyOps[NUM_FLOAT_VARS / 2 + 1];
	uint8_t shortVarsSatisfyOps[NUM_SHORT_VARS / 2 + 1];

	inline SatisfyOp GetVarSatisfyOp( const uint8_t *ops, int varIndex ) const;

	inline void SetVarSatisfyOp( uint8_t *ops, int varIndex, SatisfyOp value );

	inline const short *BotOriginData() const { return originVarsData + BotOrigin * 4; }
	inline const short *EnemyOriginData() const { return originVarsData + EnemyOrigin * 4; }

	const short *GetSniperRangeTacticalSpot();
	const short *GetFarRangeTacticalSpot();
	const short *GetMiddleRangeTacticalSpot();
	const short *GetCloseRangeTacticalSpot();
	const short *GetCoverSpot();

	const short *GetRunAwayTeleportOrigin();
	const short *GetRunAwayJumppadOrigin();
	const short *GetRunAwayElevatorOrigin();

	inline void CopyFromOtherWorldState( const WorldState &that ) {
		memcpy( this, &that, sizeof( WorldState ) );
#ifndef PUBLIC_BUILD
		isCopiedFromOtherWorldState = true;
#endif
	}

public:
#ifndef PUBLIC_BUILD
	inline bool IsCopiedFromOtherWorldState() { return isCopiedFromOtherWorldState; }
#endif

	inline WorldState &operator=( const WorldState &that ) {
		if( scriptAttachment ) {
			GENERIC_asDeleteScriptWorldStateAttachment( self, scriptAttachment );
		}
		CopyFromOtherWorldState( that );
		// We check the argument outside of the function call to avoid wasting cycles on an empty call
		if( that.scriptAttachment ) {
			this->scriptAttachment = GENERIC_asCopyScriptWorldStateAttachment( self, that.scriptAttachment );
		}
		return *this;
	}
	inline WorldState( const WorldState &that ) {
		*this = that;
	}
	inline WorldState &operator=( WorldState &&that ) {
		if( scriptAttachment ) {
			GENERIC_asDeleteScriptWorldStateAttachment( self, scriptAttachment );
		}
		CopyFromOtherWorldState( that );
		// Release the attachment ownership (if any)
		that.scriptAttachment = nullptr;
		return *this;
	}
	inline WorldState( WorldState &&that ) {
		*this = std::move( that );
	}

	// Exposed for script interface
	const void *ScriptAttachment() const { return scriptAttachment; }
	void *ScriptAttachment() { return scriptAttachment; }

	DECLARE_COMPARABLE_VAR_CLASS( UnsignedVar, unsigned );

	DECLARE_COMPARABLE_VAR_CLASS( FloatVar, float );

	DECLARE_COMPARABLE_VAR_CLASS( ShortVar, short );

#define VAR_NAME_FORMAT "%-32.32s"

	class BoolVar
	{
		friend class WorldState;
		WorldState *parent;
		const char *name;
		short index;
		BoolVar( const WorldState *parent_, short index_, const char *name_ )
			: parent( const_cast<WorldState *>( parent_ ) ), name( name_ ), index( index_ ) {}

public:
		inline bool Value() const {
			return ( parent->boolVarsValues & ( 1 << index ) ) != 0;
		}
		inline BoolVar &SetValue( bool value ) {
			if( value ) {
				parent->boolVarsValues |= ( 1 << index );
			} else {
				parent->boolVarsValues &= ~( 1 << index );
			}
			return *this;
		}
		inline operator bool() const { return Value(); }
		inline bool IsSatisfiedBy( bool value ) const {
			return Value() == value;
		}
		inline bool Ignore() const {
			return ( parent->boolVarsIgnoreFlags & ( 1 << index ) ) != 0;
		}
		inline BoolVar SetIgnore( bool ignore ) {
			if( ignore ) {
				parent->boolVarsIgnoreFlags |= ( 1 << index );
			} else {
				parent->boolVarsIgnoreFlags &= ~( 1 << index );
			}
			return *this;
		}
		inline void DebugPrint( const char *tag ) const;
	};

	// Stores a 3-dimensional world space origin vector. Dimensions are rounded up to 4 units.
	class OriginVar
	{
		friend class WorldState;
		WorldState *parent;
		const char *name;
		short index;
		OriginVar( const WorldState *parent_, short index_, const char *name_ )
			: parent( const_cast<WorldState *>( parent_ ) ), name( name_ ), index( index_ ) {}

		inline short *Data() { return &parent->originVarsData[index * 4]; }
		inline const short *Data() const { return &parent->originVarsData[index * 4]; }

		struct alignas( 2 )PackedFields {
			bool ignore : 1;
			uint8_t satisfyOp : 5;
			uint8_t epsilon : 8;

			bool operator==( const PackedFields &that ) const {
				return *( (const short *)this ) == *( (const short *)&that );
			}
		};

		static_assert( sizeof( PackedFields ) == sizeof( short ), "" );
		static_assert( alignof( PackedFields ) == alignof( short ), "" );

		inline PackedFields &Packed() {
			return *(PackedFields *)&Data()[3];
		}
		inline const PackedFields &Packed() const {
			return *(const PackedFields *)&Data()[3];
		}

public:
		// Each coordinate is rounded up to 4 units
		// Thus maximal rounding distance error = sqrt(dx*dx + dy*dy + dz*dz) = sqrt(4*4 + 4*4 + 4*4)
		static constexpr float MAX_ROUNDING_SQUARE_DISTANCE_ERROR = 3 * 4 * 4;

		inline float DistanceTo( const OriginVar &that ) const;

		inline Vec3 Value() const {
			return Vec3( 4 * Data()[0], 4 * Data()[1], 4 * Data()[2] );
		}
		inline OriginVar &SetValue( float x, float y, float z ) {
			Data()[0] = (short)( ( (int)x ) / 4 );
			Data()[1] = (short)( ( (int)y ) / 4 );
			Data()[2] = (short)( ( (int)z ) / 4 );
			return *this;
		}
		inline OriginVar &SetValue( const Vec3 &value ) {
			return SetValue( value.X(), value.Y(), value.Z() );
		}
		inline OriginVar &SetValue( const vec3_t value ) {
			return SetValue( value[0], value[1], value[2] );
		}
		inline bool Ignore() const {
			return Packed().ignore;
		}
		inline OriginVar &SetIgnore( bool ignore ) {
			Packed().ignore = ignore;
			return *this;
		}

		inline OriginVar &SetSatisfyOp( WorldState::SatisfyOp op, float epsilon );

		inline WorldState::SatisfyOp SatisfyOp() const {
			return (WorldState::SatisfyOp)( Packed().satisfyOp );
		}

		inline float SatisfyEpsilon() const {
			return ( Packed().epsilon ) * 4;
		}

		inline bool operator==( const OriginVar &that ) const;
		inline bool operator!=( const OriginVar &that ) const { return !( *this == that ); }
		inline void DebugPrint( const char *tag ) const;
	};

	class OriginLazyVarBase
	{
		friend class OriginVar;

public:
		typedef const short *(WorldState::*ValueSupplier)();

protected:
		friend class WorldState;
		WorldState *parent;
		const char *name;
		ValueSupplier supplier;
		short *varsData;
		short index;

		inline OriginLazyVarBase( const WorldState *parent_,
								  short index_,
								  ValueSupplier supplier_,
								  const short *varsData_,
								  const char *name_ )
			: parent( const_cast<WorldState *>( parent_ ) ),
			name( name_ ),
			supplier( supplier_ ),
			varsData( const_cast<short *>( varsData_ ) ),
			index( index_ ) {}

		inline short *Data() { return &varsData[index * 4]; }
		inline const short *Data() const { return &varsData[index * 4]; }

		struct alignas( 2 )PackedFields {
			bool ignore : 1;
			uint8_t stateBits : 4;
			uint8_t satisfyOp : 1;
			uint8_t epsilon : 8;

			bool operator==( const PackedFields &that ) const {
				return *( (const short *)this ) == *( (const short *)&that );
			}
		};

		static_assert( sizeof( PackedFields ) == sizeof( short ), "" );
		static_assert( alignof( PackedFields ) == alignof( short ), "" );

		inline PackedFields &Packed() {
			return *(PackedFields *)&varsData[index * 4 + 3];
		}
		inline const PackedFields &Packed() const {
			return *(const PackedFields *)&varsData[index * 4 + 3];
		}

		inline unsigned char StateBits() const {
			return (unsigned char)Packed().stateBits;
		}
		// It gets called from a const function, thats why it is const too
		inline void SetStateBits( unsigned char stateBits ) const {
			const_cast<OriginLazyVarBase *>( this )->Packed().stateBits = stateBits;
		}

		// This values are chosen in this way to allow zero-cost conversion to bool from ABSENT/PRESENT state.
		static constexpr unsigned char ABSENT = 0;
		static constexpr unsigned char PRESENT = 1;
		static constexpr unsigned char PENDING = 2;

public:
		// Each coordinate is rounded up to 4 units
		// Thus maximal rounding distance error = sqrt(dx*dx + dy*dy + dz*dz) = sqrt(4*4 + 4*4 + 4*4)
		static constexpr float MAX_ROUNDING_SQUARE_DISTANCE_ERROR = 3 * 4 * 4;

		inline Vec3 Value() const;

		inline void Reset() {
			SetStateBits( PENDING );
			Packed().ignore = false;
		}

		inline bool Ignore() const {
			return Packed().ignore;
		}

		inline OriginLazyVarBase &SetIgnore( bool ignore ) {
			Packed().ignore = ignore;
			return *this;
		}

		inline OriginLazyVarBase &SetSatisfyOp( SatisfyOp op, float epsilon );

		inline WorldState::SatisfyOp SatisfyOp() const {
			return (WorldState::SatisfyOp)( Packed().satisfyOp );
		}

		inline float SatisfyEpsilon() const {
			return Packed().epsilon * 4;
		}

		inline float DistanceTo( const OriginVar &that ) const;
	};

	class OriginLazyVar : public OriginLazyVarBase
	{
		friend class WorldState;

private:
		OriginLazyVar( const WorldState *parent_, short index_, ValueSupplier supplier_, const char *name_ )
			: OriginLazyVarBase( parent_, index_, supplier_, parent_->originLazyVarsData, name_ ) {}

public:
		inline bool IsPresent() const;

		inline bool IgnoreOrAbsent() const {
			return Ignore() || !IsPresent();
		}

		inline bool operator==( const OriginLazyVar &that ) const;
		inline bool operator!=( const OriginLazyVar &that ) { return !( *this == that ); }

		inline uint32_t Hash() const;
		inline bool IsSatisfiedBy( const OriginLazyVar &that ) const;
		inline void DebugPrint( const char *tag ) const;
	};

	class DualOriginLazyVar : public OriginLazyVarBase
	{
		friend class WorldState;

private:
		DualOriginLazyVar( const WorldState *parent_, short index_, ValueSupplier supplier_, const char *name_ )
			: OriginLazyVarBase( parent_, index_, supplier_, parent_->dualOriginLazyVarsData, name_ ) {}

		inline short *Data2() { return Data() + 4; }
		inline const short *Data2() const { return Data() + 4; }

public:
		inline Vec3 Value2() const;
		inline bool IsPresent() const;

		inline bool IgnoreOrAbsent() const {
			return Ignore() || !IsPresent();
		}

		inline bool operator==( const DualOriginLazyVar &that ) const;
		inline bool operator!=( const DualOriginLazyVar &that ) { return !( *this == that ); }
		inline uint32_t Hash() const;

		inline bool IsSatisfiedBy( const DualOriginLazyVar &that ) const;
		inline void DebugPrint( const char *tag ) const;
	};

#define DECLARE_UNSIGNED_VAR( varName ) UnsignedVar varName ## Var() const { return UnsignedVar( this, varName, #varName ); }
#define DECLARE_SHORT_VAR( varName ) ShortVar varName ## Var() const { return ShortVar( this, varName, #varName ); }
#define DECLARE_BOOL_VAR( varName ) BoolVar varName ## Var() const { return BoolVar( this, varName, #varName ); }
#define DECLARE_ORIGIN_VAR( varName ) OriginVar varName ## Var() const { return OriginVar( this, varName, #varName ); }
#define DECLARE_ORIGIN_LAZY_VAR( varName ) OriginLazyVar varName ## Var() const   \
	{                                                                             \
		return OriginLazyVar( this, varName, &WorldState::Get ## varName, #varName ); \
	}
#define DECLARE_DUAL_ORIGIN_LAZY_VAR( varName ) DualOriginLazyVar varName ## Var() const \
	{                                                                                    \
		return DualOriginLazyVar( this, varName, &WorldState::Get ## varName, #varName );    \
	}

#ifndef PUBLIC_BUILD
	WorldState( edict_t *self_ );
#else
	inline WorldState( edict_t *self_ ) : self( self_ ) {
		scriptAttachment = GENERIC_asNewScriptWorldStateAttachment( self_ );
	}
#endif

	inline ~WorldState() {
		if( scriptAttachment ) {
			GENERIC_asDeleteScriptWorldStateAttachment( self, scriptAttachment );
		}
	}

	bool IsSatisfiedBy( const WorldState &that ) const;

	uint32_t Hash() const;
	bool operator==( const WorldState &that ) const;

	void SetIgnoreAll( bool ignore );

	inline void PrepareAttachment() {
		if( scriptAttachment ) {
			GENERIC_asPrepareScriptWorldStateAttachment( self, this, scriptAttachment );
		}
	}

	DECLARE_UNSIGNED_VAR( GoalItemWaitTime )
	DECLARE_UNSIGNED_VAR( SimilarWorldStateInstanceId )

	DECLARE_SHORT_VAR( Health )
	DECLARE_SHORT_VAR( Armor )
	DECLARE_SHORT_VAR( RawDamageToKill )
	DECLARE_SHORT_VAR( PotentialDangerDamage )
	DECLARE_SHORT_VAR( ThreatInflictedDamage )

	DECLARE_BOOL_VAR( HasQuad )
	DECLARE_BOOL_VAR( HasShell )
	DECLARE_BOOL_VAR( EnemyHasQuad )
	DECLARE_BOOL_VAR( HasThreateningEnemy )
	DECLARE_BOOL_VAR( HasJustPickedGoalItem )

	DECLARE_BOOL_VAR( IsRunningAway )
	DECLARE_BOOL_VAR( HasRunAway )

	DECLARE_BOOL_VAR( HasReactedToDanger )
	DECLARE_BOOL_VAR( HasReactedToThreat )

	DECLARE_BOOL_VAR( IsReactingToEnemyLost )
	DECLARE_BOOL_VAR( HasReactedToEnemyLost )
	DECLARE_BOOL_VAR( MightSeeLostEnemyAfterTurn )

	DECLARE_BOOL_VAR( HasJustTeleported )
	DECLARE_BOOL_VAR( HasJustTouchedJumppad )
	DECLARE_BOOL_VAR( HasJustEnteredElevator )

	DECLARE_BOOL_VAR( HasPendingCoverSpot )
	DECLARE_BOOL_VAR( HasPendingRunAwayTeleport )
	DECLARE_BOOL_VAR( HasPendingRunAwayJumppad )
	DECLARE_BOOL_VAR( HasPendingRunAwayElevator )

	DECLARE_BOOL_VAR( HasPositionalAdvantage )
	DECLARE_BOOL_VAR( CanHitEnemy )
	DECLARE_BOOL_VAR( EnemyCanHit )
	DECLARE_BOOL_VAR( HasJustKilledEnemy )

	DECLARE_BOOL_VAR( HasGoodSniperRangeWeapons )
	DECLARE_BOOL_VAR( HasGoodFarRangeWeapons )
	DECLARE_BOOL_VAR( HasGoodMiddleRangeWeapons )
	DECLARE_BOOL_VAR( HasGoodCloseRangeWeapons )

	DECLARE_BOOL_VAR( EnemyHasGoodSniperRangeWeapons )
	DECLARE_BOOL_VAR( EnemyHasGoodFarRangeWeapons )
	DECLARE_BOOL_VAR( EnemyHasGoodMiddleRangeWeapons )
	DECLARE_BOOL_VAR( EnemyHasGoodCloseRangeWeapons )

	DECLARE_ORIGIN_VAR( BotOrigin )
	DECLARE_ORIGIN_VAR( EnemyOrigin )
	DECLARE_ORIGIN_VAR( NavTargetOrigin )
	DECLARE_ORIGIN_VAR( PendingOrigin )

	DECLARE_ORIGIN_VAR( DangerHitPoint )
	DECLARE_ORIGIN_VAR( DangerDirection )
	DECLARE_ORIGIN_VAR( DodgeDangerSpot )
	DECLARE_ORIGIN_VAR( ThreatPossibleOrigin )
	DECLARE_ORIGIN_VAR( LostEnemyLastSeenOrigin )

	DECLARE_ORIGIN_LAZY_VAR( SniperRangeTacticalSpot )
	DECLARE_ORIGIN_LAZY_VAR( FarRangeTacticalSpot )
	DECLARE_ORIGIN_LAZY_VAR( MiddleRangeTacticalSpot )
	DECLARE_ORIGIN_LAZY_VAR( CloseRangeTacticalSpot )
	DECLARE_ORIGIN_LAZY_VAR( CoverSpot )

	DECLARE_DUAL_ORIGIN_LAZY_VAR( RunAwayTeleportOrigin )
	DECLARE_DUAL_ORIGIN_LAZY_VAR( RunAwayJumppadOrigin )
	DECLARE_DUAL_ORIGIN_LAZY_VAR( RunAwayElevatorOrigin )

#undef DECLARE_UNSIGNED_VAR
#undef DECLARE_SHORT_VAR
#undef DECLARE_BOOL_VAR
#undef DECLARE_ORIGIN_VAR
#undef DECLARE_ORIGIN_LAZY_VAR
#undef DECLARE_DUAL_ORIGIN_LAZY_VAR

	inline float DistanceToEnemy() const { return BotOriginVar().DistanceTo( EnemyOriginVar() ); }
	inline float DistanceToNavTarget() const { return BotOriginVar().DistanceTo( NavTargetOriginVar() ); }

	inline float DistanceToSniperRangeTacticalSpot() const {
		return SniperRangeTacticalSpotVar().DistanceTo( BotOriginVar() );
	}
	inline float DistanceToFarRangeTacticalSpot() const {
		return FarRangeTacticalSpotVar().DistanceTo( BotOriginVar() );
	}
	inline float DistanceToMiddleRangeTacticalSpot() const {
		return MiddleRangeTacticalSpotVar().DistanceTo( BotOriginVar() );
	}
	inline float DistanceToCloseRangeTacticalSpot() const {
		return CloseRangeTacticalSpotVar().DistanceTo( BotOriginVar() );
	}
	inline float DistanceToCoverSpot() const {
		return CoverSpotVar().DistanceTo( BotOriginVar() );
	}

	inline void ResetTacticalSpots() {
		SniperRangeTacticalSpotVar().Reset();
		FarRangeTacticalSpotVar().Reset();
		MiddleRangeTacticalSpotVar().Reset();
		CloseRangeTacticalSpotVar().Reset();
		CoverSpotVar().Reset();

		RunAwayTeleportOriginVar().Reset();
		RunAwayJumppadOriginVar().Reset();
		RunAwayElevatorOriginVar().Reset();
	}

	constexpr static float FAR_RANGE_MAX = 2.5f * 900.0f;
	constexpr static float MIDDLE_RANGE_MAX = 900.0f;
	constexpr static float CLOSE_RANGE_MAX = 175.0f;

	inline bool EnemyIsOnSniperRange() const {
		return DistanceToEnemy() > FAR_RANGE_MAX;
	}
	inline bool EnemyIsOnFarRange() const {
		return DistanceToEnemy() > MIDDLE_RANGE_MAX && DistanceToEnemy() <= FAR_RANGE_MAX;
	}
	inline bool EnemyIsOnMiddleRange() const {
		return DistanceToEnemy() > CLOSE_RANGE_MAX && DistanceToEnemy() <= MIDDLE_RANGE_MAX;
	}
	inline bool EnemyIsOnCloseRange() const {
		return DistanceToEnemy() <= CLOSE_RANGE_MAX;
	}

	inline float DamageToBeKilled() const {
		float damageToBeKilled = ::DamageToKill( HealthVar(), ArmorVar() );
		if( HasShellVar() ) {
			damageToBeKilled *= 4.0f;
		}
		if( EnemyHasQuadVar() ) {
			damageToBeKilled /= 4.0f;
		}
		return damageToBeKilled;
	}

	inline float DamageToKill() const {
		float damageToKill = RawDamageToKillVar();
		if( HasQuadVar() ) {
			damageToKill /= 4.0f;
		}
		return damageToKill;
	}

	inline float KillToBeKilledDamageRatio() const {
		return DamageToKill() / DamageToBeKilled();
	}

	void DebugPrint( const char *tag ) const;

	void DebugPrintDiff( const WorldState &that, const char *oldTag, const char *newTag ) const;
};

inline WorldState::SatisfyOp WorldState::GetVarSatisfyOp( const uint8_t *ops, int varIndex ) const {
	const uint8_t &byte = ops[varIndex / 2];
	// Try to avoid branches, use shift to select hi or lo part of a byte
	auto shift = ( varIndex % 2 ) * 4;
	// Do a left-shift to move the part value to rightmost 4 bits, then apply a mask for these 4 bits
	return (SatisfyOp)( ( byte >> shift ) & 0xF );
}

inline void WorldState::SetVarSatisfyOp( uint8_t *ops, int varIndex, SatisfyOp value ) {
	uint8_t &byte = ops[varIndex / 2];
	auto varShift = ( varIndex % 2 ) * 4;
	// The other packed op (hi or lo part) should be preserved.
	// If varShift is 4, complementaryShift is 0 and vice versa.
	auto complementaryShift = ( ( ( varIndex % 2 ) + 1 ) % 2 ) * 4;

#ifdef _DEBUG
	if( ( ( 0xF << varShift ) | ( 0xF << complementaryShift ) ) != 0xFF ) {
		AI_FailWith( "WorldState::SetVarSatisfyOp()", "Var shift and complementary shift masks do not compose a byte\n" );
	}
#endif

	// This mask allows to extract the kept part of a byte
	uint8_t keptPartMask = (uint8_t)( 0xF << complementaryShift );
	//
	uint8_t keptPart = ( ( byte << complementaryShift ) & keptPartMask );
	uint8_t newPart = (unsigned char)value << varShift;
	// Combine parts
	byte = keptPart | newPart;
}

inline void WorldState::UnsignedVar::DebugPrint( const char *tag ) const {
	if( Ignore() ) {
		AI_Debug( tag, VAR_NAME_FORMAT ": (ignored)\n", name );
	} else {
		AI_Debug( tag, VAR_NAME_FORMAT ": %u\n", name, Value() );
	}
}

inline void WorldState::FloatVar::DebugPrint( const char *tag ) const {
	if( Ignore() ) {
		AI_Debug( tag, VAR_NAME_FORMAT ": (ignored)\n", name );
	} else {
		AI_Debug( tag, VAR_NAME_FORMAT ": %f\n", name, Value() );
	}
}

inline void WorldState::ShortVar::DebugPrint( const char *tag ) const {
	if( Ignore() ) {
		AI_Debug( tag, VAR_NAME_FORMAT ": (ignored)\n", name );
	} else {
		AI_Debug( tag, VAR_NAME_FORMAT ": %hi\n", name, Value() );
	}
}

inline void WorldState::BoolVar::DebugPrint( const char *tag ) const {
	if( Ignore() ) {
		AI_Debug( tag, VAR_NAME_FORMAT ": (ignored)\n", name );
	} else {
		AI_Debug( tag, VAR_NAME_FORMAT ": %s\n", name, Value() ? "true" : "false" );
	}
}

inline void WorldState::OriginVar::DebugPrint( const char *tag ) const {
	if( Ignore() ) {
		AI_Debug( tag, VAR_NAME_FORMAT ": (ignored)\n", name );
		return;
	}
	Vec3 value( Value() );
	AI_Debug( tag, VAR_NAME_FORMAT ": %f %f %f\n", name, value.X(), value.Y(), value.Z() );
}

inline float WorldState::OriginVar::DistanceTo( const OriginVar &that ) const {
#ifdef _DEBUG
	if( this->Ignore() ) {
		AI_FailWith( "OriginVar::GetDistance()", "`this` var is ignored\n" );
	}
	if( that.Ignore() ) {
		AI_FailWith( "OriginVar::GetDistance()", "`that` var is ignored\n" );
	}
	// Its might be legal from coding point of view, but does not make sense
	if( this->parent != that.parent ) {
		AI_FailWith( "OriginVar::GetDistance()", "Vars belong to different world states\n" );
	}
#endif
	vec3_t unpackedThis, unpackedThat;
	VectorCopy( Data(), unpackedThis );
	VectorCopy( that.Data(), unpackedThat );
	VectorScale( unpackedThis, 4.0f, unpackedThis );
	VectorScale( unpackedThat, 4.0f, unpackedThat );
	return DistanceFast( unpackedThis, unpackedThat );
}

inline WorldState::OriginVar &WorldState::OriginVar::SetSatisfyOp( WorldState::SatisfyOp op, float epsilon ) {
#ifdef _DEBUG
	if( op != SatisfyOp::EQ && op != SatisfyOp::NE ) {
		AI_FailWith( "OriginVar::SetSatisfyOp()", "Illegal satisfy op %d for this kind of var\n", (int)op );
	}
	if( epsilon < 4.0f || epsilon >= 1024.0f ) {
		AI_FailWith( "OriginVar::SetSatisfyOp()", "An epsilon %f is out of valid [4, 1024] range\n", epsilon );
	}
#endif
	// Up to 8 bits
	auto packedEpsilon = (uint8_t)( (unsigned)epsilon / 4 );
	auto packedOp = (uint8_t)op;
	Packed().epsilon = packedEpsilon;
	Packed().satisfyOp = packedOp;
	return *this;
}

inline bool WorldState::OriginVar::operator==( const OriginVar &that ) const {
	if( !Packed().ignore ) {
		if( !( Packed() == that.Packed() ) ) {
			return false;
		}

		return VectorCompare( Data(), that.Data() );
	}

	return that.Packed().ignore;
}

inline Vec3 WorldState::OriginLazyVarBase::Value() const {
	if( StateBits() == PRESENT ) {
		return Vec3( 4 * Data()[0], 4 * Data()[1], 4 * Data()[2] );
	}

	AI_FailWith( "OriginLazyVar::Value()", "Attempt to get a value of var #%hd which is not in PRESENT state\n", index );
}

inline WorldState::OriginLazyVarBase &WorldState::OriginLazyVarBase::SetSatisfyOp( WorldState::SatisfyOp op, float epsilon ) {
#ifdef _DEBUG
	if( op != WorldState::SatisfyOp::EQ && op != WorldState::SatisfyOp::NE ) {
		AI_FailWith( "OriginLazyVarBase::SetSatisfyOp()", "Illegal satisfy op %d for this kind of var\n", (int)op );
	}
	if( epsilon < 4.0f || epsilon >= 1024.0f ) {
		AI_FailWith( "OriginLazyVarBase::SetSatisfyOp()", "An epsilon %f is out of valid [4, 1024] range\n", epsilon );
	}
#endif
	// Up to 8 bits
	auto packedEpsilon = (uint8_t)( (unsigned)epsilon / 4 );
	// A single bit
	auto packedOp = (uint8_t)op;
	static_assert( (unsigned)WorldState::SatisfyOp::EQ == 0, "SatisfyOp can't be packed in a single bit" );
	static_assert( (unsigned)WorldState::SatisfyOp::NE == 1, "SatisfyOp can't be packed in a single bit" );
	Packed().epsilon = packedEpsilon;
	Packed().satisfyOp = packedOp;
	return *this;
}

inline float WorldState::OriginLazyVarBase::DistanceTo( const OriginVar &that ) const {
#ifdef _DEBUG
	if( this->Ignore() ) {
		AI_FailWith( "OriginLazyVar::GetDistance(const OriginVar &)", "`this` var is ignored\n" );
	}
	if( that.Ignore() ) {
		AI_FailWith( "OriginLazyVar::GetDistance(const OriginVar &)", "`that` var is ignored\n" );
	}
	if( this->parent != that.parent ) {
		AI_FailWith( "OriginLazyVar::GetDistance(const OriginVar &)", "Vars belong to different world states\n" );
	}
#endif
	vec3_t unpackedThis, unpackedThat;
	VectorCopy( Data(), unpackedThis );
	VectorCopy( that.Data(), unpackedThat );
	VectorScale( unpackedThis, 4.0f, unpackedThis );
	VectorScale( unpackedThat, 4.0f, unpackedThat );
	return DistanceFast( unpackedThis, unpackedThat );
}

inline bool WorldState::OriginLazyVar::IsPresent() const {
	unsigned char stateBits = StateBits();
	if( stateBits != PENDING ) {
		return stateBits != ABSENT;
	}

	const short *packedValues = ( parent->*supplier )();
	if( packedValues ) {
		short *data = const_cast<short*>( Data() );
		VectorCopy( packedValues, data );
		SetStateBits( PRESENT );
		return true;
	}
	SetStateBits( ABSENT );
	return false;
}

inline bool WorldState::OriginLazyVar::operator==( const OriginLazyVar &that ) const {
#ifdef _DEBUG
	if( this->index != that.index ) {
		AI_FailWith( "OriginLazyVar::IsSatisfiedBy()", "Vars index mismatch\n" );
	}
#endif
	if( !Packed().ignore ) {
		if( that.Packed().ignore ) {
			return false;
		}
		auto stateBits = StateBits();
		if( stateBits != that.StateBits() ) {
			return false;
		}
		if( stateBits != PRESENT ) {
			return true;
		}

		if( !( Packed() == that.Packed() ) ) {
			return false;
		}

		return VectorCompare( Data(), that.Data() );
	}
	// `that` should be ignored too
	return that.Packed().ignore;
}

inline uint32_t WorldState::OriginLazyVar::Hash() const {
	auto stateBits = StateBits();
	if( stateBits != PRESENT ) {
		return stateBits;
	}
	const unsigned short *data = (const unsigned short*)Data();
	return ( data[0] | ( data[1] << 16 ) ) ^ ( data[2] | ( data[3] << 16 ) );
}

inline void WorldState::OriginLazyVar::DebugPrint( const char *tag ) const {
	if( Ignore() ) {
		AI_Debug( tag, VAR_NAME_FORMAT ": (ignored)\n", name );
		return;
	}
	switch( StateBits() ) {
		case PENDING:
			AI_Debug( tag, VAR_NAME_FORMAT ": (pending)\n", name );
			break;
		case ABSENT:
			AI_Debug( tag, VAR_NAME_FORMAT ": (absent)\n", name );
			break;
		case PRESENT:
			Vec3 value( Value() );
			AI_Debug( tag, VAR_NAME_FORMAT ": %f %f %f\n", name, value.X(), value.Y(), value.Z() );
			break;
	}
}

inline bool WorldState::OriginLazyVar::IsSatisfiedBy( const OriginLazyVar &that ) const {
#ifdef _DEBUG
	if( this->index != that.index ) {
		AI_FailWith( "OriginLazyVar::IsSatisfiedBy()", "Vars index mismatch\n" );
	}
#endif
	if( Packed().ignore ) {
		return true;
	}
	if( that.Packed().ignore ) {
		return false;
	}

	auto stateBits = this->StateBits();
	// Do not force a lazy value to be computed
	if( stateBits != that.StateBits() ) {
		return false;
	}
	if( stateBits != PRESENT ) {
		return true;
	}

	const float epsilon = SatisfyEpsilon();
	switch( SatisfyOp() ) {
		case WorldState::SatisfyOp::EQ:
			if( DistanceSquared( Data(), that.Data() ) > epsilon * epsilon ) {
				return false;
			}
			break;
		case WorldState::SatisfyOp::NE:
			if( DistanceSquared( Data(), that.Data() ) < epsilon * epsilon ) {
				return false;
			}
			break;
		default:
			AI_FailWith( "OriginLazyVar::IsSatisfiedBy()", "Illegal satisfy op %d\n", (int)SatisfyOp() );
	}
	return true;
}

inline Vec3 WorldState::DualOriginLazyVar::Value2() const {
	if( StateBits() == PRESENT ) {
		return Vec3( Data2()[0], Data2()[1], Data2()[2] );
	}

	AI_FailWith( "OriginLazyVar::Value2()", "Attempt to get a 2nd value of var #%hd which is not in PRESENT state\n", index );
}

inline bool WorldState::DualOriginLazyVar::IsSatisfiedBy( const DualOriginLazyVar &that ) const {
#ifdef _DEBUG
	if( this->index != that.index ) {
		AI_FailWith( "OriginLazyVar::IsSatisfiedBy()", "Vars index mismatch\n" );
	}
#endif
	if( Packed().ignore ) {
		return true;
	}
	if( that.Packed().ignore ) {
		return false;
	}

	auto stateBits = this->StateBits();
	// Do not force a lazy value to be computed
	if( stateBits != that.StateBits() ) {
		return false;
	}
	if( stateBits != PRESENT ) {
		return true;
	}

	if( !( Packed() == that.Packed() ) ) {
		return false;
	}

	const float epsilon = SatisfyEpsilon();
	switch( SatisfyOp() ) {
		case WorldState::SatisfyOp::EQ:
			if( DistanceSquared( Data(), that.Data() ) > epsilon * epsilon ) {
				return false;
			}
			if( DistanceSquared( Data2(), that.Data2() ) > epsilon * epsilon ) {
				return false;
			}
			break;
		case WorldState::SatisfyOp::NE:
			if( DistanceSquared( Data(), that.Data() ) < epsilon * epsilon ) {
				return false;
			}
			if( DistanceSquared( Data2(), that.Data2() ) < epsilon * epsilon ) {
				return false;
			}
			break;
		default:
			AI_FailWith( "DualOriginLazyVar::IsSatisfiedBy()", "Illegal satisfy op %d\n", (int)SatisfyOp() );
	}
	return true;
}

inline bool WorldState::DualOriginLazyVar::operator==( const DualOriginLazyVar &that ) const {
#ifdef _DEBUG
	if( this->index != that.index ) {
		AI_FailWith( "OriginLazyVar::IsSatisfiedBy()", "Vars index mismatch\n" );
	}
#endif
	if( !Packed().ignore ) {
		if( that.Packed().ignore ) {
			return false;
		}
		auto stateBits = StateBits();
		if( stateBits != that.StateBits() ) {
			return false;
		}
		if( stateBits != PRESENT ) {
			return true;
		}
		return VectorCompare( Data(), that.Data() ) && VectorCompare( Data2(), that.Data2() );
	}
	// `that` should be ignored too
	return that.Packed().ignore;
}

inline void WorldState::DualOriginLazyVar::DebugPrint( const char *tag ) const {
	if( Ignore() ) {
		AI_Debug( tag, VAR_NAME_FORMAT ": (ignored)\n", name );
		return;
	}
	switch( StateBits() ) {
		case PENDING:
			AI_Debug( tag, VAR_NAME_FORMAT ": (pending)\n", name );
			break;
		case ABSENT:
			AI_Debug( tag, VAR_NAME_FORMAT ": (absent)\n", name );
			break;
		case PRESENT:
			Vec3 v( Value() ), v2( Value2() );
			constexpr const char *format = VAR_NAME_FORMAT ": %f %f %f, %f %f %f\n";
			AI_Debug( tag, format, name, v.X(), v.Y(), v.Z(), v2.X(), v2.Y(), v2.Z() );
			break;
	}
}

inline bool WorldState::DualOriginLazyVar::IsPresent() const {
	unsigned char stateBits = StateBits();
	if( stateBits != PENDING ) {
		return stateBits != ABSENT;
	}

	const short *packedValues = ( parent->*supplier )();
	if( packedValues ) {
		short *data = const_cast<short*>( Data() );
		short *data2 = const_cast<short*>( Data2() );
		VectorCopy( packedValues, data );
		VectorCopy( packedValues + 3, data2 );
		SetStateBits( PRESENT );
		return true;
	}
	SetStateBits( ABSENT );
	return false;
}

inline uint32_t WorldState::DualOriginLazyVar::Hash() const {
	auto stateBits = StateBits();
	if( stateBits != PRESENT ) {
		return stateBits;
	}
	unsigned originHash = ( 17U * Data()[0] + ( Data()[1] | ( Data()[2] << 16 ) ) );
	unsigned originHash2 = ( 17U * Data2()[0] + ( Data2()[1] | ( Data2()[2] << 16 ) ) );
	uint32_t result = 17 * ( *(short *)&Packed() );
	result = result * 31 + originHash;
	result = result * 31 + originHash2;
	return result;
}

#undef VAR_NAME_FORMAT

#endif
