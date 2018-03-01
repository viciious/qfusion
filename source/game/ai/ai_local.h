/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
--------------------------------------------------------------
The ACE Bot is a product of Steve Yeager, and is available from
the ACE Bot homepage, at http://www.axionfx.com/ace.

This program is a modification of the ACE Bot, and is therefore
in NO WAY supported by Steve Yeager.
*/

//==========================================================
#ifndef QFUSION_AI_LOCAL_H
#define QFUSION_AI_LOCAL_H

#include "../g_local.h"
#include "../../gameshared/q_collision.h"

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

// First try to include <math.h> for M_* defines
#ifndef __USE_MATH_DEFINES
#define __USE_MATH_DEFINES 1
#endif
#include <math.h>

// If the <math.h> inclusion above still did not help
// (in case when it was included earlier without __USE_MATH_DEFINES)
#ifndef M_SQRT1_2
#define M_SQRT1_2 ( 0.70710678118654752440 )
#endif

#include <algorithm>
#include <utility>
#include <stdarg.h>

// Platform states:
constexpr auto STATE_TOP    = 0;
constexpr auto STATE_BOTTOM = 1;
constexpr auto STATE_UP     = 2;
constexpr auto STATE_DOWN   = 3;

constexpr auto MAX_NAVENTS = MAX_EDICTS;

constexpr auto AI_STEPSIZE          = STEPSIZE; // 18
constexpr auto AI_JUMPABLE_HEIGHT   = 50;
constexpr auto AI_JUMPABLE_DISTANCE = 360;
constexpr auto AI_WATERJUMP_HEIGHT  = 24;
constexpr auto AI_MIN_RJ_HEIGHT     = 128;
constexpr auto AI_MAX_RJ_HEIGHT     = 512;
constexpr auto AI_GOAL_SR_RADIUS    = 200;
constexpr auto AI_GOAL_SR_LR_RADIUS = 600;

constexpr int AI_GOAL_SR_MILLIS    = 750;
constexpr int AI_GOAL_SR_LR_MILLIS = 1500;

constexpr auto MASK_AISOLID = CONTENTS_SOLID | CONTENTS_PLAYERCLIP | CONTENTS_BODY | CONTENTS_MONSTERCLIP;

typedef enum {
	AI_WEAPON_AIM_TYPE_INSTANT_HIT,
	AI_WEAPON_AIM_TYPE_PREDICTION,
	AI_WEAPON_AIM_TYPE_PREDICTION_EXPLOSIVE,
	AI_WEAPON_AIM_TYPE_DROP
} ai_weapon_aim_type;

ai_weapon_aim_type BuiltinWeaponAimType( int builtinWeapon, int fireMode );

inline bool IsBuiltinWeaponContinuousFire( int builtinWeapon ) {
	return builtinWeapon == WEAP_LASERGUN || builtinWeapon == WEAP_PLASMAGUN || builtinWeapon == WEAP_MACHINEGUN;
}

int BuiltinWeaponTier( int builtinWeapon );
int FindBestWeaponTier( const gclient_t *client );

void *GENERIC_asInstantiateGoal( void *factoryObject, edict_t *owner, class BotScriptGoal *nativeGoal );
void *GENERIC_asInstantiateAction( void *factoryObject, edict_t *owner, class BotScriptAction *nativeAction );

void GENERIC_asActivateScriptActionRecord( void *scriptObject );
void GENERIC_asDeactivateScriptActionRecord( void *scriptObject );
void GENERIC_asDeleteScriptActionRecord( void *scriptObject );
int GENERIC_asCheckScriptActionRecordStatus( void *scriptObject, const class WorldState &currWorldState );
void *GENERIC_asTryApplyScriptAction( void *scriptObject, const class WorldState &worldState );
float GENERIC_asGetScriptGoalWeight( void *scriptObject, const class WorldState &currWorldState );
void GENERIC_asGetScriptGoalDesiredWorldState( void *scriptObject, class WorldState *worldState );
void GENERIC_asOnScriptGoalPlanBuildingStarted( void *scriptObject );
void GENERIC_asOnScriptGoalPlanBuildingCompleted( void *scriptObject, bool succeeded );

bool GT_asBotWouldDropHealth( const gclient_t *client );
void GT_asBotDropHealth( gclient_t *client );
bool GT_asBotWouldDropArmor( const gclient_t *client );
void GT_asBotDropArmor( gclient_t *client );

void GT_asBotTouchedGoal( const ai_handle_t *bot, const edict_t *goalEnt );
void GT_asBotReachedGoalRadius( const ai_handle_t *bot, const edict_t *goalEnt );

// These functions return a score in range [0, 1].
// Default score should be 0.5f, and it should be returned
// when a GT script does not provide these function counterparts.
// Note that offence and defence score are not complementary but independent.
float GT_asPlayerOffensiveAbilitiesRating( const gclient_t *client );
float GT_asPlayerDefenciveAbilitiesRating( const gclient_t *client );

struct AiScriptWeaponDef {
	int weaponNum;
	int tier;
	float minRange;
	float maxRange;
	float bestRange;
	float projectileSpeed;
	float splashRadius;
	float maxSelfDamage;
	ai_weapon_aim_type aimType;
	bool isContinuousFire;
};

int GT_asGetScriptWeaponsNum( const gclient_t *client );
bool GT_asGetScriptWeaponDef( const gclient_t *client, int scriptWeaponNum, AiScriptWeaponDef *weaponDef );
int GT_asGetScriptWeaponCooldown( const gclient_t *client, int scriptWeaponNum );
bool GT_asSelectScriptWeapon( gclient_t *client, int scriptWeaponNum );
bool GT_asFireScriptWeapon( gclient_t *client, int scriptWeaponNum );

#include "ai_aas_world.h"
#include "vec3.h"

typedef struct ai_handle_s {
	ai_type type;

	int asFactored, asRefCount;

	ai_handle_t *prev, *next;

	class Ai * aiRef;
	class Bot * botRef;
} ai_handle_t;

#ifndef _MSC_VER
void AI_Debug( const char *tag, const char *format, ... ) __attribute__( ( format( printf, 2, 3 ) ) );
void AI_Debugv( const char *tag, const char *format, va_list va );
void AI_FailWith( const char *tag, const char *format, ... ) __attribute__( ( format( printf, 2, 3 ) ) ) __attribute__( ( noreturn ) );
void AI_FailWithv( const char *tag, const char *format, va_list va ) __attribute__( ( noreturn ) );
#else
void AI_Debug( const char *tag, _Printf_format_string_ const char *format, ... );
void AI_Debugv( const char *tag, const char *format, va_list va );
__declspec( noreturn ) void AI_FailWith( const char *tag, _Printf_format_string_ const char *format, ... );
__declspec( noreturn ) void AI_FailWithv( const char *tag, const char *format, va_list va );
#endif

inline float Clamp( float value ) {
	clamp( value, 0.0f, 1.0f );
	return value;
}

inline float Clamp( float value, float minValue, float maxValue ) {
	clamp( value, minValue, maxValue );
	return value;
}

inline float BoundedFraction( float value, float bound ) {
	return std::min( value, bound ) / bound;
}

inline unsigned From0UpToMax( unsigned maxValue, float ratio ) {
//#ifdef _DEBUG
	if( ratio < 0 || ratio > 1 ) {
		AI_FailWith( "From0UpToMax()", "ratio %f is out of valid [0,1] bounds", ratio );
	}
//#endif
	return (unsigned)( maxValue * ratio );
}

inline unsigned From1UpToMax( unsigned maxValue, float ratio ) {
//#ifdef _DEBUG
	if( !maxValue ) {
		AI_FailWith( "From1UpToMax()", "maxValue is 0" );
	}
//#endif
	return 1 + From0UpToMax( maxValue - 1, ratio );
}

inline void SetPacked4uVec( const vec3_t vec, int16_t *packed ) {
	packed[0] = (int16_t)( vec[0] / 4.0f );
	packed[1] = (int16_t)( vec[1] / 4.0f );
	packed[2] = (int16_t)( vec[2] / 4.0f );
}

inline void SetPacked4uVec( const Vec3 &vec, int16_t *packed ) {
	SetPacked4uVec( vec.Data(), packed );
}

inline Vec3 GetUnpacked4uVec( const int16_t *packed ) {
	return Vec3( packed[0] * 4, packed[1] * 4, packed[2] * 4 );
}

inline const char *Nick( const edict_t *ent ) {
	if( !ent ) {
		return "???";
	}
	if( ent->r.client ) {
		return ent->r.client->netname;
	}
	return ent->classname;
}

inline const char *WeapName( int weapon ) {
	return GS_GetWeaponDef( weapon )->name;
}

//----------------------------------------------------------

//game
//----------------------------------------------------------
void Use_Plat( edict_t *ent, edict_t *other, edict_t *activator );

void AITools_DrawLine( const vec3_t origin, const vec3_t dest );
void AITools_DrawColorLine( const vec3_t origin, const vec3_t dest, int color, int parm );

void GetHashAndLength( const char *str, unsigned *hash, unsigned *length );
unsigned GetHashForLength( const char *str, unsigned length );

// A cheaper version of G_Trace() that does not check against entities
inline void StaticWorldTrace( trace_t *trace, const vec3_t from, const vec3_t to, int contentsMask,
							  const vec3_t mins = vec3_origin, const vec3_t maxs = vec3_origin ) {
	assert( from );
	float *from_ = const_cast<float *>( from );
	assert( to );
	float *to_ = const_cast<float *>( to );
	assert( mins );
	float *mins_ = const_cast<float *>( mins );
	assert( maxs );
	float *maxs_ = const_cast<float *>( maxs );
	trap_CM_TransformedBoxTrace( trace, from_, to_, mins_, maxs_, nullptr, contentsMask, nullptr, nullptr );
}

// This shorthand is for backward compatibility and some degree of convenience
inline void SolidWorldTrace( trace_t *trace, const vec3_t from, const vec3_t to,
							 const vec3_t mins = vec3_origin, const vec3_t maxs = vec3_origin ) {
	StaticWorldTrace( trace, from, to, MASK_SOLID, mins, maxs );
}

struct EntAndScore {
	int entNum;
	float score;
	EntAndScore(): entNum( 0 ), score( 0.0f ) {}
	EntAndScore( int entNum_, float score_ ) : entNum( entNum_ ), score( score_ ) {}
	bool operator<( const EntAndScore &that ) const { return score > that.score; }
};

struct AreaAndScore {
	int areaNum;
	float score;
	AreaAndScore(): areaNum( 0 ), score( 0.0f ) {}
	AreaAndScore( int areaNum_, float score_ ) : areaNum( areaNum_ ), score( score_ ) {}
	bool operator<( const AreaAndScore &that ) const { return score > that.score; }
};

template<typename T>
class ArrayRange {
	const T *begin_;
	const T *end_;
public:
	ArrayRange( const T *basePtr, size_t size ) {
		begin_ = basePtr;
		end_ = basePtr + size;
	}
	const T *begin() const { return begin_; }
	const T *end() const { return end_; }
};

// This is a compact storage for 64-bit values.
// If an int64_t field is used in an array of tiny structs,
// a substantial amount of space can be lost for alignment.
class alignas ( 4 )Int64Align4 {
	uint32_t parts[2];
public:
	operator int64_t() const {
		return (int64_t)( ( (uint64_t)parts[0] << 32 ) | parts[1] );
	}
	Int64Align4 operator=( int64_t value ) {
		parts[0] = (uint32_t)( ( (uint64_t)value >> 32 ) & 0xFFFFFFFFu );
		parts[1] = (uint32_t)( ( (uint64_t)value >> 00 ) & 0xFFFFFFFFu );
		return *this;
	}
};

#endif