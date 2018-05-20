#ifndef QFUSION_MOVEMENTSTATE_H
#define QFUSION_MOVEMENTSTATE_H

#include "BotInput.h"
#include "../ai_base_ai.h"

class MovementPredictionContext;

class alignas ( 2 )AiCampingSpot
{
	// Fields of this class are packed to allow cheap copying of class instances in bot movement prediction code
	friend class Bot;
	friend class BotCampingSpotState;

	int16_t origin[3];
	int16_t lookAtPoint[3];
	uint16_t radius;
	uint8_t alertness;
	AiCampingSpot() : radius( 32 ), alertness( 255 ), hasLookAtPoint( false ) {}

public:
	bool hasLookAtPoint;

	inline float Radius() const { return radius; }
	inline float Alertness() const { return alertness / 256.0f; }
	inline Vec3 Origin() const { return GetUnpacked4uVec( origin ); }
	inline Vec3 LookAtPoint() const { return GetUnpacked4uVec( lookAtPoint ); }
	// Warning! This does not set hasLookAtPoint, only used to store a vector in (initially unsused) lookAtPoint field
	// This behaviour is used when lookAtPoint is controlled manually by an external code.
	inline void SetLookAtPoint( const Vec3 &lookAtPoint_ ) { SetPacked4uVec( lookAtPoint_, lookAtPoint ); }

	AiCampingSpot( const Vec3 &origin_, float radius_, float alertness_ = 0.75f )
		: radius( (uint16_t)( radius_ ) ), alertness( (uint8_t)( alertness_ * 255 ) ), hasLookAtPoint( false )
	{
		SetPacked4uVec( origin_, origin );
	}

	AiCampingSpot( const vec3_t &origin_, float radius_, float alertness_ = 0.75f )
		: radius( (uint16_t)radius_ ), alertness( (uint8_t)( alertness_ * 255 ) ), hasLookAtPoint( false )
	{
		SetPacked4uVec( origin_, origin );
	}

	AiCampingSpot( const vec3_t &origin_, const vec3_t &lookAtPoint_, float radius_, float alertness_ = 0.75f )
		: radius( (uint16_t)radius_ ), alertness( (uint8_t)( alertness_ * 255 ) ), hasLookAtPoint( true )
	{
		SetPacked4uVec( origin_, origin );
		SetPacked4uVec( lookAtPoint_, lookAtPoint );
	}

	AiCampingSpot( const Vec3 &origin_, const Vec3 &lookAtPoint_, float radius_, float alertness_ = 0.75f )
		: radius( (uint16_t)radius_ ), alertness( (uint8_t)( alertness_ * 255 ) ), hasLookAtPoint( true )
	{
		SetPacked4uVec( origin_, origin );
		SetPacked4uVec( lookAtPoint_, lookAtPoint );
	}
};

class alignas ( 2 )AiPendingLookAtPoint
{
	// Fields of this class are packed to allow cheap copying of class instances in bot movement prediction code
	friend struct BotPendingLookAtPointState;

	int16_t origin[3];
	// Floating point values greater than 1.0f are allowed (unless they are significantly greater than 1.0f);
	uint16_t turnSpeedMultiplier;

	AiPendingLookAtPoint() {
		// Shut an analyzer up
		turnSpeedMultiplier = 16;
	}

public:
	inline Vec3 Origin() const { return GetUnpacked4uVec( origin ); }
	inline float TurnSpeedMultiplier() const { return turnSpeedMultiplier / 16.0f; };

	AiPendingLookAtPoint( const vec3_t origin_, float turnSpeedMultiplier_ )
		: turnSpeedMultiplier( (uint16_t)( std::min( 255.0f, turnSpeedMultiplier_ * 16.0f ) ) )
	{
		SetPacked4uVec( origin_, origin );
	}

	AiPendingLookAtPoint( const Vec3 &origin_, float turnSpeedMultiplier_ )
		: turnSpeedMultiplier(  (uint16_t)( std::min( 255.0f, turnSpeedMultiplier_ * 16.0f ) ) )
	{
		SetPacked4uVec( origin_, origin );
	}
};

struct BotAerialMovementState {
protected:
	bool ShouldDeactivate( const edict_t *self, const class MovementPredictionContext *context = nullptr ) const;
};

struct alignas ( 2 )BotJumppadMovementState : protected BotAerialMovementState {
	// Fields of this class are packed to allow cheap copying of class instances in bot movement prediction code

private:
	static_assert( MAX_EDICTS < ( 1 << 16 ), "Cannot store jumppad entity number in 16 bits" );
	uint16_t jumppadEntNum;

public:
	// Should be set by Bot::TouchedJumppad() callback (its get called in ClientThink())
	// It gets processed by movement code in next frame
	bool hasTouchedJumppad;
	// If this flag is set, bot is in "jumppad" movement state
	bool hasEnteredJumppad;

	inline BotJumppadMovementState()
		: jumppadEntNum( 0 ), hasTouchedJumppad( false ), hasEnteredJumppad( false ) {}

	// Useless but kept for structural type conformance with other movement states
	inline void Frame( unsigned frameTime ) {}

	inline bool IsActive() const {
		return ( hasTouchedJumppad || hasEnteredJumppad );
	}

	inline void Deactivate() {
		hasTouchedJumppad = false;
		hasEnteredJumppad = false;
	}

	inline void Activate( const edict_t *triggerEnt ) {
		hasTouchedJumppad = true;
		// Keep hasEnteredJumppad as-is (a jumppad might be touched again few millis later)
		jumppadEntNum = ( decltype( jumppadEntNum ) )( ENTNUM( const_cast<edict_t *>( triggerEnt ) ) );
	}

	inline void TryDeactivate( const edict_t *self, const MovementPredictionContext *context = nullptr ) {
		if( ShouldDeactivate( self, context ) ) {
			Deactivate();
		}
	}

	inline const edict_t *JumppadEntity() const { return game.edicts + jumppadEntNum; }
};

class alignas ( 2 )BotWeaponJumpMovementState : protected BotAerialMovementState
{
	int16_t jumpTarget[3];
	int16_t fireTarget[3];

public:
	bool hasPendingWeaponJump : 1;
	bool hasTriggeredRocketJump : 1;
	bool hasCorrectedWeaponJump : 1;

	BotWeaponJumpMovementState()
		: hasPendingWeaponJump( false ), hasTriggeredRocketJump( false ), hasCorrectedWeaponJump( false ) {}

	inline void Frame( unsigned frameTime ) {}

	inline Vec3 JumpTarget() const { return GetUnpacked4uVec( jumpTarget ); }
	inline Vec3 FireTarget() const { return GetUnpacked4uVec( fireTarget ); }

	inline bool IsActive() const {
		return ( hasPendingWeaponJump || hasTriggeredRocketJump || hasCorrectedWeaponJump );
	}

	inline void TryDeactivate( const edict_t *self, const class MovementPredictionContext *context = nullptr ) {
		if( ShouldDeactivate( self, context ) ) {
			Deactivate();
		}
	}

	inline void Deactivate() {
		hasPendingWeaponJump = false;
		hasTriggeredRocketJump = false;
		hasCorrectedWeaponJump = false;
	}

	inline void Activate( const Vec3 &jumpTarget_, const Vec3 &fireTarget_,
						  unsigned timeoutPeriod, int64_t levelTime = level.time ) {
		SetPacked4uVec( jumpTarget_, jumpTarget );
		SetPacked4uVec( fireTarget_, fireTarget );
		hasPendingWeaponJump = true;
		hasTriggeredRocketJump = false;
		hasCorrectedWeaponJump = false;
	}
};

struct alignas ( 2 )BotPendingLookAtPointState {
	AiPendingLookAtPoint pendingLookAtPoint;

private:
	unsigned char timeLeft;

public:
	inline BotPendingLookAtPointState() : timeLeft( 0 ) {}

	inline void Frame( unsigned frameTime ) {
		timeLeft = ( decltype( timeLeft ) ) std::max( 0, ( (int)timeLeft * 4 - (int)frameTime ) / 4 );
	}

	inline bool IsActive() const { return timeLeft > 0; }

	// Timeout period is limited by 1000 millis
	inline void Activate( const AiPendingLookAtPoint &pendingLookAtPoint_, unsigned timeoutPeriod = 500U ) {
		this->pendingLookAtPoint = pendingLookAtPoint_;
		this->timeLeft = ( decltype( this->timeLeft ) )( std::min( 1000U, timeoutPeriod ) / 4 );
	}

	inline void Deactivate() { timeLeft = 0; }

	inline void TryDeactivate( const edict_t *self, const class MovementPredictionContext *context = nullptr ) {
		if( !IsActive() ) {
			Deactivate();
		}
	}
};

class alignas ( 2 )BotCampingSpotState
{
	mutable AiCampingSpot campingSpot;
	// When to change chosen strafe dir
	mutable uint16_t moveDirsTimeLeft;
	// When to change randomly chosen look-at-point (if the point is not initially specified)
	mutable uint16_t lookAtPointTimeLeft;
	int8_t forwardMove : 4;
	int8_t rightMove : 4;
	bool isTriggered;

	inline unsigned StrafeDirTimeout() const {
		return (unsigned)( 400 + 100 * random() + 300 * ( 1.0f - campingSpot.Alertness() ) );
	}
	inline unsigned LookAtPointTimeout() const {
		return (unsigned)( 800 + 200 * random() + 2000 * ( 1.0f - campingSpot.Alertness() ) );
	}

public:
	inline BotCampingSpotState()
		: moveDirsTimeLeft( 0 )
		, lookAtPointTimeLeft( 0 )
		, forwardMove( 0 )
		, rightMove( 0 )
		, isTriggered( false ) {}

	inline void Frame( unsigned frameTime ) {
		moveDirsTimeLeft = ( uint16_t ) std::max( 0, (int)moveDirsTimeLeft - (int)frameTime );
		lookAtPointTimeLeft = ( uint16_t ) std::max( 0, (int)lookAtPointTimeLeft - (int)frameTime );
	}

	inline bool IsActive() const { return isTriggered; }

	inline void Activate( const AiCampingSpot &campingSpot_ ) {
		// Reset dir timers if and only if an actual origin has been significantly changed.
		// Otherwise this leads to "jitter" movement on the same point
		// when prediction errors prevent using a predicted action
		if( this->Origin().SquareDistance2DTo( campingSpot_.Origin() ) > 16 * 16 ) {
			moveDirsTimeLeft = 0;
			lookAtPointTimeLeft = 0;
		}
		this->campingSpot = campingSpot_;
		this->isTriggered = true;
	}

	inline void Deactivate() { isTriggered = false; }

	void TryDeactivate( const edict_t *self, const class MovementPredictionContext *context = nullptr );

	inline Vec3 Origin() const { return campingSpot.Origin(); }
	inline float Radius() const { return campingSpot.Radius(); }

	AiPendingLookAtPoint GetOrUpdateRandomLookAtPoint() const;

	inline float Alertness() const { return campingSpot.Alertness(); }

	inline int ForwardMove() const { return forwardMove; }
	inline int RightMove() const { return rightMove; }

	inline bool AreKeyMoveDirsValid() { return moveDirsTimeLeft > 0; }

	inline void SetKeyMoveDirs( int forwardMove_, int rightMove_ ) {
		this->forwardMove = ( decltype( this->forwardMove ) )forwardMove_;
		this->rightMove = ( decltype( this->rightMove ) )rightMove_;
	}
};

class alignas ( 2 )BotKeyMoveDirsState
{
	uint16_t timeLeft;
	int8_t forwardMove;
	int8_t rightMove;

public:
	static constexpr uint16_t TIMEOUT_PERIOD = 512;

	inline BotKeyMoveDirsState()
		: timeLeft( 0 ), forwardMove( 0 ), rightMove( 0 ) {}

	inline void Frame( unsigned frameTime ) {
		timeLeft = ( decltype( timeLeft ) ) std::max( 0, ( (int)timeLeft - (int)frameTime ) );
	}

	inline bool IsActive() const { return !timeLeft; }

	inline void TryDeactivate( const edict_t *self, const class MovementPredictionContext *context = nullptr ) {}

	inline void Deactivate() { timeLeft = 0; }

	inline void Activate( int forwardMove_, int rightMove_ ) {
		this->forwardMove = ( decltype( this->forwardMove ) )forwardMove_;
		this->rightMove = ( decltype( this->rightMove ) )rightMove_;
		this->timeLeft = TIMEOUT_PERIOD;
	}

	inline int ForwardMove() const { return forwardMove; }
	inline int RightMove() const { return rightMove; }
};

class alignas ( 2 )BotFlyUntilLandingMovementState : protected BotAerialMovementState
{
	int16_t target[3];
	uint16_t landingDistanceThreshold;
	bool isTriggered : 1;
	// If not set, uses target Z level as landing threshold
	bool usesDistanceThreshold : 1;
	bool isLanding : 1;

public:
	inline BotFlyUntilLandingMovementState()
		: landingDistanceThreshold( 0 ),
		  isTriggered( false ),
		  usesDistanceThreshold( false ),
		  isLanding( false ) {}

	inline void Frame( unsigned frameTime ) {}

	bool CheckForLanding( const class MovementPredictionContext *context );

	inline void Activate( const vec3_t target_, float landingDistanceThreshold_ ) {
		SetPacked4uVec( target_, this->target );
		landingDistanceThreshold = ( decltype( landingDistanceThreshold ) )( landingDistanceThreshold_ );
		isTriggered = true;
		usesDistanceThreshold = true;
		isLanding = false;
	}

	inline void Activate( const Vec3 &target_, float landingDistanceThreshold_ ) {
		Activate( target_.Data(), landingDistanceThreshold_ );
	}

	inline void Activate( float startLandingAtZ ) {
		this->target[2] = (short)startLandingAtZ;
		isTriggered = true;
		usesDistanceThreshold = false;
		isLanding = false;
	}

	inline bool IsActive() const { return isTriggered; }

	inline void Deactivate() { isTriggered = false; }

	inline void TryDeactivate( const edict_t *self, const class MovementPredictionContext *context = nullptr ) {
		if( ShouldDeactivate( self, context ) ) {
			Deactivate();
		}
	}

	inline Vec3 Target() const { return GetUnpacked4uVec( target ); }
};

class Bot;

struct alignas ( 4 )BotMovementState {
	// We want to pack members tightly to reduce copying cost of this struct during the planning process
	static_assert( alignof( AiEntityPhysicsState ) == 4, "Members order by alignment is broken" );
	AiEntityPhysicsState entityPhysicsState;
	static_assert( alignof( BotCampingSpotState ) == 2, "Members order by alignment is broken" );
	BotCampingSpotState campingSpotState;
	static_assert( alignof( BotJumppadMovementState ) == 2, "Members order by alignment is broken" );
	BotJumppadMovementState jumppadMovementState;
	static_assert( alignof( BotWeaponJumpMovementState ) == 2, "Members order by alignment is broken" );
	BotWeaponJumpMovementState weaponJumpMovementState;
	static_assert( alignof( BotPendingLookAtPointState ) == 2, "Members order by alignment is broken" );
	BotPendingLookAtPointState pendingLookAtPointState;
	static_assert( alignof( BotFlyUntilLandingMovementState ) == 2, "Members order by alignment is broken" );
	BotFlyUntilLandingMovementState flyUntilLandingMovementState;
	static_assert( alignof( BotKeyMoveDirsState ) == 2, "Members order by alignment is broken" );
	BotKeyMoveDirsState keyMoveDirsState;

	// A current input rotation kind that is used in this state.
	// This value is saved to prevent choice jitter trying to apply an input rotation.
	// (The current input rotation kind has a bit less restrictive application conditions).
	BotInputRotation inputRotation;

	inline BotMovementState()
		: inputRotation( BotInputRotation::NONE ) {
	}

	inline void Frame( unsigned frameTime ) {
		jumppadMovementState.Frame( frameTime );
		weaponJumpMovementState.Frame( frameTime );
		pendingLookAtPointState.Frame( frameTime );
		campingSpotState.Frame( frameTime );
		keyMoveDirsState.Frame( frameTime );
		flyUntilLandingMovementState.Frame( frameTime );
	}

	inline void TryDeactivateContainedStates( const edict_t *self, MovementPredictionContext *context ) {
		jumppadMovementState.TryDeactivate( self, context );
		weaponJumpMovementState.TryDeactivate( self, context );
		pendingLookAtPointState.TryDeactivate( self, context );
		campingSpotState.TryDeactivate( self, context );
		keyMoveDirsState.TryDeactivate( self, context );
		flyUntilLandingMovementState.TryDeactivate( self, context );
	}

	inline void Reset() {
		jumppadMovementState.Deactivate();
		weaponJumpMovementState.Deactivate();
		pendingLookAtPointState.Deactivate();
		campingSpotState.Deactivate();
		keyMoveDirsState.Deactivate();
		flyUntilLandingMovementState.Deactivate();
	}

	inline unsigned GetContainedStatesMask() const {
		unsigned result = 0;
		result |= ( (unsigned)( jumppadMovementState.IsActive() ) ) << 0;
		result |= ( (unsigned)( weaponJumpMovementState.IsActive() ) ) << 1;
		result |= ( (unsigned)( pendingLookAtPointState.IsActive() ) ) << 2;
		result |= ( (unsigned)( campingSpotState.IsActive() ) ) << 3;
		// Skip keyMoveDirsState.
		// It either should not affect movement at all if regular movement is chosen,
		// or should be handled solely by the combat movement code.
		result |= ( (unsigned)( flyUntilLandingMovementState.IsActive() ) ) << 4;
		return result;
	}

	bool TestActualStatesForExpectedMask( unsigned expectedStatesMask, const Bot *bot ) const;
};

#endif
