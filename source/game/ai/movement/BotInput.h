#ifndef QFUSION_BOTINPUT_H
#define QFUSION_BOTINPUT_H

#include "../ai_local.h"

enum class BotInputRotation : uint8_t {
	NONE = 0,
	BACK = 1 << 0,
	RIGHT = 1 << 1,
	LEFT = 1 << 2,
	SIDE_KINDS_MASK = ( 1 << 1 ) | ( 1 << 2 ),
	ALL_KINDS_MASK = ( 1 << 3 ) - 1
};

class alignas ( 4 )BotInput
{
	friend class MovementPredictionContext;
	// Todo: Pack since it is required to be normalized now?
	Vec3 intendedLookDir;
	// A copy of self->s.angles for modification
	// We do not want to do deeply hidden angles update in the aiming functions,
	// the BotInput should be only mutable thing in the related code.
	// Should be copied back to self->s.angles if it has been modified when the BotInput gets applied.
	Vec3 alreadyComputedAngles;
	BotInputRotation allowedRotationMask;
	uint8_t turnSpeedMultiplier;
	signed ucmdForwardMove : 2;
	signed ucmdSideMove : 2;
	signed ucmdUpMove : 2;
	bool attackButton : 1;
	bool specialButton : 1;
	bool walkButton : 1;

public:
	bool fireScriptWeapon : 1;
	bool isUcmdSet : 1;
	bool isLookDirSet : 1;
	bool hasAlreadyComputedAngles : 1;
	bool canOverrideUcmd : 1;
	bool shouldOverrideUcmd : 1;
	bool canOverrideLookVec : 1;
	bool shouldOverrideLookVec : 1;
	bool canOverridePitch : 1;
	bool applyExtraViewPrecision : 1;

	inline BotInput()
		: intendedLookDir( NAN, NAN, NAN ),
		  alreadyComputedAngles( NAN, NAN, NAN )
	{
		Clear();
	}

	inline void Clear() {
		memset( this, 0, sizeof( BotInput ) );
		// Restore default values overwritten by the memset() call
		turnSpeedMultiplier = 16;
		allowedRotationMask = BotInputRotation::ALL_KINDS_MASK;
	}

	inline void SetAllowedRotationMask( BotInputRotation rotationMask ) {
		this->allowedRotationMask = rotationMask;
	}

	bool IsRotationAllowed( BotInputRotation rotation ) {
		return ( (int)allowedRotationMask & (int)rotation ) != 0;
	}

	// Button accessors are kept for backward compatibility with existing bot movement code
	inline void SetAttackButton( bool isSet ) { attackButton = isSet; }
	inline void SetSpecialButton( bool isSet ) { specialButton = isSet; }
	inline void SetWalkButton( bool isSet ) { walkButton = isSet; }

	inline bool IsAttackButtonSet() const { return attackButton; }
	inline bool IsSpecialButtonSet() const { return specialButton; }
	inline bool IsWalkButtonSet() const { return walkButton; }

	inline int ForwardMovement() const { return ucmdForwardMove; }
	inline int RightMovement() const { return ucmdSideMove; }
	inline int UpMovement() const { return ucmdUpMove; }

	inline bool IsCrouching() const { return UpMovement() < 0; }

	inline void SetForwardMovement( int movement ) { ucmdForwardMove = movement; }
	inline void SetRightMovement( int movement ) { ucmdSideMove = movement; }
	inline void SetUpMovement( int movement ) { ucmdUpMove = movement; }

	inline void ClearMovementDirections() {
		ucmdForwardMove = 0;
		ucmdSideMove = 0;
		ucmdUpMove = 0;
	}

	inline void ClearButtons() {
		attackButton = false;
		specialButton = false;
		walkButton = false;
	}

	inline float TurnSpeedMultiplier() const { return turnSpeedMultiplier / 16.0f; }
	inline void SetTurnSpeedMultiplier( float value ) {
		turnSpeedMultiplier = ( decltype( turnSpeedMultiplier ) )( value * 16.0f );
	}

	inline void CopyToUcmd( usercmd_t *ucmd ) const {
		ucmd->forwardmove = 127 * ForwardMovement();
		ucmd->sidemove = 127 * RightMovement();
		ucmd->upmove = 127 * UpMovement();

		ucmd->buttons = 0;
		if( attackButton ) {
			ucmd->buttons |= BUTTON_ATTACK;
		}
		if( specialButton ) {
			ucmd->buttons |= BUTTON_SPECIAL;
		}
		if( walkButton ) {
			ucmd->buttons |= BUTTON_WALK;
		}
	}

	inline void SetAlreadyComputedAngles( const Vec3 &angles ) {
		alreadyComputedAngles = angles;
		hasAlreadyComputedAngles = true;
	}

	inline const Vec3 &AlreadyComputedAngles() const {
		return alreadyComputedAngles;
	}

	inline void SetIntendedLookDir( const Vec3 &intendedLookVec, bool alreadyNormalized = false ) {
		SetIntendedLookDir( intendedLookVec.Data(), alreadyNormalized );
	}

	inline void SetIntendedLookDir( const vec3_t intendedLookVec, bool alreadyNormalized = false ) {
		this->intendedLookDir.Set( intendedLookVec );
		if( !alreadyNormalized ) {
			this->intendedLookDir.NormalizeFast();
		}
#ifndef PUBLIC_BUILD
		else if( fabsf( this->intendedLookDir.NormalizeFast() - 1.0f ) > 0.1f ) {
			AI_FailWith( "BotInput::SetIntendedLookDir()", "The argument is claimed to be normalized but it isn't\n" );
		}
#endif
		this->isLookDirSet = true;
	}

	inline const Vec3 &IntendedLookDir() const {
		return intendedLookDir;
	}
};

#endif
