/*
Copyright (C) 2002-2003 Victor Luchits

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

*/

#ifndef __GS_PUBLIC_H
#define __GS_PUBLIC_H

#include "gs_ref.h"

// shared callbacks

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _MSC_VER
extern void ( *module_Printf )( const char *format, ... ) __attribute__( ( format( printf, 1, 2 ) ) );
extern void ( *module_Error )( const char *format, ... ) __attribute__( ( format( printf, 1, 2 ) ) ) __attribute__( ( noreturn ) );
#else
extern void ( *module_Printf )( _Printf_format_string_ const char *format, ... );
extern void ( *module_Error )( _Printf_format_string_ const char *format, ... );
#endif

extern void *( *module_Malloc )( size_t size );
extern void ( *module_Free )( void *data );
extern void ( *module_Trace )( trace_t *t, vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, int ignore, int contentmask, int timeDelta );
extern entity_state_t *( *module_GetEntityState )( int entNum, int deltaTime );
extern int ( *module_PointContents )( vec3_t point, int timeDelta );
extern void ( *module_PredictedEvent )( int entNum, int ev, int parm );
extern void ( *module_PMoveTouchTriggers )( pmove_t *pm, vec3_t previous_origin );
extern const char *( *module_GetConfigString )( int index );

//===============================================================
//		WARSOW player AAboxes sizes

extern vec3_t playerbox_stand_mins;
extern vec3_t playerbox_stand_maxs;
extern int playerbox_stand_viewheight;

extern vec3_t playerbox_crouch_mins;
extern vec3_t playerbox_crouch_maxs;
extern int playerbox_crouch_viewheight;

extern vec3_t playerbox_gib_mins;
extern vec3_t playerbox_gib_maxs;
extern int playerbox_gib_viewheight;

// item box
extern vec3_t item_box_mins;
extern vec3_t item_box_maxs;

#define BASEGRAVITY 800
#define GRAVITY 850
#define GRAVITY_COMPENSATE ( (float)GRAVITY / (float)BASEGRAVITY )

#define ZOOMTIME 60
#define CROUCHTIME 100
#define DEFAULT_PLAYERSPEED_STANDARD 320.0f
#define DEFAULT_PLAYERSPEED_INSTAGIB 320.0f
#define DEFAULT_PLAYERSPEED_RACE 320.0f
#define DEFAULT_JUMPSPEED 280.0f
#define DEFAULT_DASHSPEED 450.0f
#define PROJECTILE_PRESTEP 100
#define ELECTROBOLT_RANGE 9001

#define MIN_FOV             60
#define MAX_FOV             140
#define DEFAULT_FOV         100

#define MIN_ZOOMFOV         3
#define MAX_ZOOMFOV         60
#define DEFAULT_ZOOMFOV     30

//==================================================================

enum {
	MATCH_STATE_NONE,
	MATCH_STATE_WARMUP,
	MATCH_STATE_COUNTDOWN,
	MATCH_STATE_PLAYTIME,
	MATCH_STATE_POSTMATCH,
	MATCH_STATE_WAITEXIT,

	MATCH_STATE_TOTAL
};

enum {
	GS_MODULE_GAME = 1,
	GS_MODULE_CGAME,
};


enum {
	GAMESTAT_FLAGS,
	GAMESTAT_MATCHSTATE,
	GAMESTAT_MATCHSTART,
	GAMESTAT_MATCHDURATION,
	GAMESTAT_CLOCKOVERRIDE,
	GAMESTAT_MAXPLAYERSINTEAM,
	GAMESTAT_COLORCORRECTION,
};

#define GAMESTAT_FLAG_PAUSED ( 1 << 0LL )
#define GAMESTAT_FLAG_WAITING ( 1 << 1LL )
#define GAMESTAT_FLAG_INSTAGIB ( 1 << 2LL )
#define GAMESTAT_FLAG_MATCHEXTENDED ( 1 << 3LL )
#define GAMESTAT_FLAG_FALLDAMAGE ( 1 << 4LL )
#define GAMESTAT_FLAG_HASCHALLENGERS ( 1 << 5LL )
#define GAMESTAT_FLAG_INHIBITSHOOTING ( 1 << 6LL )
#define GAMESTAT_FLAG_ISTEAMBASED ( 1 << 7LL )
#define GAMESTAT_FLAG_ISRACE ( 1 << 8LL )
#define GAMESTAT_FLAG_COUNTDOWN ( 1 << 9LL )
#define GAMESTAT_FLAG_SELFDAMAGE ( 1 << 10LL )
#define GAMESTAT_FLAG_INFINITEAMMO ( 1 << 11LL )
#define GAMESTAT_FLAG_CANFORCEMODELS ( 1 << 12LL )
#define GAMESTAT_FLAG_CANSHOWMINIMAP ( 1 << 13LL )
#define GAMESTAT_FLAG_TEAMONLYMINIMAP ( 1 << 14LL )
#define GAMESTAT_FLAG_MMCOMPATIBLE ( 1 << 15LL )
#define GAMESTAT_FLAG_ISTUTORIAL ( 1 << 16LL )
#define GAMESTAT_FLAG_CANDROPWEAPON ( 1 << 17LL )

typedef struct {
	int module;
	int maxclients;
	char gametypeName[MAX_CONFIGSTRING_CHARS];
	game_state_t gameState;
} gs_state_t;

extern gs_state_t gs;

#define GS_GamestatSetFlag( flag, b ) ( b ? ( gs.gameState.stats[GAMESTAT_FLAGS] |= flag ) : ( gs.gameState.stats[GAMESTAT_FLAGS] &= ~flag ) )
#define GS_Instagib() ( ( gs.gameState.stats[GAMESTAT_FLAGS] & GAMESTAT_FLAG_INSTAGIB ) ? true : false )
#define GS_FallDamage() ( ( gs.gameState.stats[GAMESTAT_FLAGS] & GAMESTAT_FLAG_FALLDAMAGE ) ? true : false )
#define GS_ShootingDisabled() ( ( gs.gameState.stats[GAMESTAT_FLAGS] & GAMESTAT_FLAG_INHIBITSHOOTING ) ? true : false )
#define GS_HasChallengers() ( ( gs.gameState.stats[GAMESTAT_FLAGS] & GAMESTAT_FLAG_HASCHALLENGERS ) ? true : false )
#define GS_TeamBasedGametype() ( ( gs.gameState.stats[GAMESTAT_FLAGS] & GAMESTAT_FLAG_ISTEAMBASED ) ? true : false )
#define GS_RaceGametype() ( ( gs.gameState.stats[GAMESTAT_FLAGS] & GAMESTAT_FLAG_ISRACE ) ? true : false )
#define GS_MatchPaused() ( ( gs.gameState.stats[GAMESTAT_FLAGS] & GAMESTAT_FLAG_PAUSED ) ? true : false )
#define GS_MatchWaiting() ( ( gs.gameState.stats[GAMESTAT_FLAGS] & GAMESTAT_FLAG_WAITING ) ? true : false )
#define GS_MatchExtended() ( ( gs.gameState.stats[GAMESTAT_FLAGS] & GAMESTAT_FLAG_MATCHEXTENDED ) ? true : false )
#define GS_SelfDamage() ( ( gs.gameState.stats[GAMESTAT_FLAGS] & GAMESTAT_FLAG_SELFDAMAGE ) ? true : false )
#define GS_Countdown() ( ( gs.gameState.stats[GAMESTAT_FLAGS] & GAMESTAT_FLAG_COUNTDOWN ) ? true : false )
#define GS_InfiniteAmmo() ( ( gs.gameState.stats[GAMESTAT_FLAGS] & GAMESTAT_FLAG_INFINITEAMMO ) ? true : false )
#define GS_CanForceModels() ( ( gs.gameState.stats[GAMESTAT_FLAGS] & GAMESTAT_FLAG_CANFORCEMODELS ) ? true : false )
#define GS_CanShowMinimap() ( ( gs.gameState.stats[GAMESTAT_FLAGS] & GAMESTAT_FLAG_CANSHOWMINIMAP ) ? true : false )
#define GS_TeamOnlyMinimap() ( ( gs.gameState.stats[GAMESTAT_FLAGS] & GAMESTAT_FLAG_TEAMONLYMINIMAP ) ? true : false )
#define GS_MMCompatible() ( ( gs.gameState.stats[GAMESTAT_FLAGS] & GAMESTAT_FLAG_MMCOMPATIBLE ) ? true : false )
#define GS_TutorialGametype() ( gs.gameState.stats[GAMESTAT_FLAGS] & GAMESTAT_FLAG_ISTUTORIAL ? true : false )
#define GS_CanDropWeapon() ( gs.gameState.stats[GAMESTAT_FLAGS] & GAMESTAT_FLAG_CANDROPWEAPON ? true : false )

#define GS_MatchState() ( gs.gameState.stats[GAMESTAT_MATCHSTATE] )
#define GS_MaxPlayersInTeam() ( gs.gameState.stats[GAMESTAT_MAXPLAYERSINTEAM] )
#define GS_InvidualGameType() ( GS_MaxPlayersInTeam() == 1 ? true : false )

#define GS_MatchDuration() ( gs.gameState.stats[GAMESTAT_MATCHDURATION] )
#define GS_MatchStartTime() ( gs.gameState.stats[GAMESTAT_MATCHSTART] )
#define GS_MatchEndTime() ( gs.gameState.stats[GAMESTAT_MATCHDURATION] ? gs.gameState.stats[GAMESTAT_MATCHSTART] + gs.gameState.stats[GAMESTAT_MATCHDURATION] : 0 )
#define GS_MatchClockOverride() ( gs.gameState.stats[GAMESTAT_CLOCKOVERRIDE] )

#define GS_ColorCorrection() ( gs.gameState.stats[GAMESTAT_COLORCORRECTION] )

#define DEFAULT_PLAYERSPEED ( GS_RaceGametype() ? DEFAULT_PLAYERSPEED_RACE : ( GS_Instagib() ? DEFAULT_PLAYERSPEED_INSTAGIB : DEFAULT_PLAYERSPEED_STANDARD ) )

//==================================================================

// S_DEFAULT_ATTENUATION_MODEL	"3"
#define ATTN_NONE               0       // full volume the entire level
#define ATTN_DISTANT            0.5     // distant sound (most likely explosions)
#define ATTN_NORM               1       // players, weapons, etc
#define ATTN_IDLE               2.5     // stuff around you
#define ATTN_STATIC             5       // diminish very rapidly with distance
#define ATTN_WHISPER            10      // must be very close to hear it

#if 0
// S_DEFAULT_ATTENUATION_MODEL			"1"
#define ATTN_NONE               0   // full volume the entire level
#define ATTN_DISTANT            2   // distant sound (most likely explosions)
#define ATTN_NORM               5   // players, weapons, etc
#define ATTN_IDLE               8   // stuff around you
#define ATTN_STATIC             10  // diminish very rapidly with distance
#define ATTN_WHISPER            15  // must be very close to hear it
#endif

// sound channels
// channel 0 never willingly overrides
// other channels (1-7) always override a playing sound on that channel
enum {
	CHAN_AUTO,
	CHAN_PAIN,
	CHAN_VOICE,
	CHAN_ITEM,
	CHAN_BODY,
	CHAN_MUZZLEFLASH,
	CHAN_ANNOUNCER,

	CHAN_TOTAL,

	CHAN_FIXED = 128
};

//==================================================================

#define ISWALKABLEPLANE( x ) ( ( (cplane_t *)x )->normal[2] >= 0.7 )

// box slide movement code (not used for player)
#define MAX_SLIDEMOVE_CLIP_PLANES   16

#define SLIDEMOVE_PLANEINTERACT_EPSILON 0.05
#define SLIDEMOVEFLAG_PLANE_TOUCHED 16
#define SLIDEMOVEFLAG_WALL_BLOCKED  8
#define SLIDEMOVEFLAG_TRAPPED       4
#define SLIDEMOVEFLAG_BLOCKED       2   // it was blocked at some point, doesn't mean it didn't slide along the blocking object
#define SLIDEMOVEFLAG_MOVED     1

typedef struct {
	vec3_t velocity;
	vec3_t origin;
	vec3_t mins, maxs;
	float remainingTime;

	vec3_t gravityDir;
	float slideBounce;
	int groundEntity;

	int passent, contentmask;

	int numClipPlanes;
	vec3_t clipPlaneNormals[MAX_SLIDEMOVE_CLIP_PLANES];

	int numtouch;
	int touchents[MAXTOUCH];
} move_t;

int GS_SlideMove( move_t *move );
void GS_ClipVelocity( vec3_t in, vec3_t normal, vec3_t out, float overbounce );

int GS_LinearMovement( const entity_state_t *ent, int64_t time, vec3_t dest );
void GS_LinearMovementDelta( const entity_state_t *ent, int64_t oldTime, int64_t curTime, vec3_t dest );

//==============================================================
//
//PLAYER MOVEMENT CODE
//
//Common between server and client so prediction matches
//
//==============================================================

#define STEPSIZE 18
enum {
	GS_CLASSICBUNNY,
	GS_NEWBUNNY,

	GS_MAXBUNNIES
};

void Pmove( pmove_t *pmove );

//===============================================================

//==================
//SPLITMODELS
//==================

// The parts must be listed in draw order
enum {
	LOWER = 0,
	UPPER,
	HEAD,

	PMODEL_PARTS
};

// -Torso DEATH frames and Legs DEATH frames must be the same.

// ANIMATIONS

enum {
	ANIM_NONE = 0
	, BOTH_DEATH1       //Death animation
	, BOTH_DEAD1        //corpse on the ground
	, BOTH_DEATH2
	, BOTH_DEAD2
	, BOTH_DEATH3
	, BOTH_DEAD3

	, LEGS_STAND_IDLE
	, LEGS_WALK_FORWARD
	, LEGS_WALK_BACK
	, LEGS_WALK_LEFT
	, LEGS_WALK_RIGHT

	, LEGS_RUN_FORWARD
	, LEGS_RUN_BACK
	, LEGS_RUN_LEFT
	, LEGS_RUN_RIGHT

	, LEGS_JUMP_LEG1
	, LEGS_JUMP_LEG2
	, LEGS_JUMP_NEUTRAL
	, LEGS_LAND

	, LEGS_CROUCH_IDLE
	, LEGS_CROUCH_WALK

	, LEGS_SWIM_FORWARD
	, LEGS_SWIM_NEUTRAL

	, LEGS_WALLJUMP
	, LEGS_WALLJUMP_LEFT
	, LEGS_WALLJUMP_RIGHT
	, LEGS_WALLJUMP_BACK

	, LEGS_DASH
	, LEGS_DASH_LEFT
	, LEGS_DASH_RIGHT
	, LEGS_DASH_BACK

	, TORSO_HOLD_BLADE
	, TORSO_HOLD_PISTOL
	, TORSO_HOLD_LIGHTWEAPON
	, TORSO_HOLD_HEAVYWEAPON
	, TORSO_HOLD_AIMWEAPON

	, TORSO_SHOOT_BLADE
	, TORSO_SHOOT_PISTOL
	, TORSO_SHOOT_LIGHTWEAPON
	, TORSO_SHOOT_HEAVYWEAPON
	, TORSO_SHOOT_AIMWEAPON

	, TORSO_WEAPON_SWITCHOUT
	, TORSO_WEAPON_SWITCHIN

	, TORSO_DROPHOLD
	, TORSO_DROP

	, TORSO_SWIM

	, TORSO_PAIN1
	, TORSO_PAIN2
	, TORSO_PAIN3

	, PMODEL_TOTAL_ANIMATIONS
};

enum {
	WEAPON,
	EXPANSION,
	BARREL,
	FLASH,
	HAND,

	WEAPMODEL_PARTS
};

// gender stuff
enum {
	GENDER_MALE,
	GENDER_FEMALE,
	GENDER_NEUTRAL
};

//===============================================================

#define HEALTH_TO_INT( x )    ( ( x ) < 1.0f ? (int)ceil( ( x ) ) : (int)floor( ( x ) + 0.5f ) )
#define ARMOR_TO_INT( x )     ( ( x ) < 1.0f ? (int)floor( ( x ) + 0.5f ) : (int)floor( ( x ) + 0.5f ) )

#define ARMOR_DEGRADATION 0.66 // how much armor is lost per damage point taken
#define ARMOR_PROTECTION 0.66 // how much damage is removed per damage point taken
#define ARMOR_DECAY_MAX_ARMOR 0 // decay to this value ( 0 disabled )

#define INSTA_SHIELD_MAX    100.0f

// gs_items - shared items definitions

//==================
//	ITEM TAGS
//==================
// max weapons allowed by the net protocol are 128
typedef enum {
	WEAP_NONE = 0,
	WEAP_GUNBLADE,
	WEAP_MACHINEGUN,
	WEAP_RIOTGUN,
	WEAP_GRENADELAUNCHER,
	WEAP_ROCKETLAUNCHER,
	WEAP_PLASMAGUN,
	WEAP_LASERGUN,
	WEAP_ELECTROBOLT,
	WEAP_SHOCKWAVE,
	WEAP_INSTAGUN,

	WEAP_TOTAL
} weapon_tag_t;

typedef enum {
	AMMO_NONE = 0,
	AMMO_GUNBLADE = WEAP_TOTAL,
	AMMO_BULLETS,
	AMMO_SHELLS,
	AMMO_GRENADES,
	AMMO_ROCKETS,
	AMMO_PLASMA,
	AMMO_LASERS,
	AMMO_BOLTS,
	AMMO_WAVES,
	AMMO_INSTAS,

	AMMO_WEAK_GUNBLADE, //this is the blade
	AMMO_WEAK_BULLETS,
	AMMO_WEAK_SHELLS,
	AMMO_WEAK_GRENADES,
	AMMO_WEAK_ROCKETS,
	AMMO_WEAK_PLASMA,
	AMMO_WEAK_LASERS,
	AMMO_WEAK_BOLTS,
	AMMO_WEAK_WAVES,
	AMMO_WEAK_INSTAS,

	AMMO_TOTAL

} ammo_tag_t;

typedef enum {
	ARMOR_NONE = 0,
	ARMOR_GA = AMMO_TOTAL,
	ARMOR_YA,
	ARMOR_RA,
	ARMOR_SHARD,

	ARMOR_TOTAL

} armor_tag_t;

typedef enum {
	HEALTH_NONE = 0,
	HEALTH_SMALL = ARMOR_TOTAL,
	HEALTH_MEDIUM,
	HEALTH_LARGE,
	HEALTH_MEGA,
	HEALTH_ULTRA,

	HEALTH_TOTAL

} health_tag_t;

typedef enum {
	POWERUP_NONE = 0,
	POWERUP_QUAD = HEALTH_TOTAL,
	POWERUP_SHELL,
	POWERUP_REGEN,

	POWERUP_TOTAL

} powerup_tag_t;

typedef enum {
	AMMO_PACK_WEAK = POWERUP_TOTAL,
	AMMO_PACK_STRONG,
	AMMO_PACK,

	ITEMS_TOTAL
} otheritems_tag_t;

#define GS_MAX_ITEM_TAGS ITEMS_TOTAL

#define MAX_ITEM_MODELS 2

// gsitem_t->flags
#define ITFLAG_PICKABLE     1
#define ITFLAG_USABLE       2
#define ITFLAG_DROPABLE     4
#define ITFLAG_STAY_COOP    8

// gsitem_t->type
// define as bitflags values so they can be masked
typedef enum {
	IT_WEAPON = 1
	, IT_AMMO = 2
	, IT_ARMOR = 4
	, IT_POWERUP = 8
	, IT_HEALTH = 64
} itemtype_t;

#define G_INSTAGIB_NEGATE_ITEMMASK ( IT_WEAPON | IT_AMMO | IT_ARMOR | IT_POWERUP | IT_HEALTH )

typedef struct gitem_s {
	//header
	char *classname;        // spawning name
	int tag;
	itemtype_t type;
	int flags;              // actions the item does in the game

	//media
	char *world_model[MAX_ITEM_MODELS];
	char *icon;
	char *simpleitem;       // Kurim : we use different images for representing simpleitems
	char *pickup_sound;
	int effects;


	char *name;      // for printing on pickup
	char *shortname;       // for printing on messages
	char *color;            // for printing on messages

	int quantity;           // how much it gives at picking
	int inventory_max;      // how much quantity of this the inventory can carry

	// special
	int ammo_tag;           // uses this ammo, for weapons
	int weakammo_tag;

	void *info;             // miscelanea info goes pointed in here

	// space separated string of stuff to precache that's not mentioned above
	char *precache_models;
	char *precache_sounds;
	char *precache_images;
} gsitem_t;

extern gsitem_t itemdefs[];

gsitem_t *GS_FindItemByTag( int tag );
gsitem_t *GS_FindItemByClassname( const char *classname );
gsitem_t *GS_FindItemByName( const char *name );
gsitem_t *GS_Cmd_UseItem( player_state_t *playerState, const char *string, int typeMask );
gsitem_t *GS_Cmd_NextWeapon_f( player_state_t *playerState, int predictedWeaponSwitch );
gsitem_t *GS_Cmd_PrevWeapon_f( player_state_t *playerState, int predictedWeaponSwitch );
int GS_Armor_TagForCount( float armorcount );
int GS_Armor_MaxCountForTag( int tag );
int GS_Armor_PickupCountForTag( int tag );

//===============================================================

//===================
//	GAMETYPES
//===================

enum {
	TEAM_SPECTATOR,
	TEAM_PLAYERS,
	TEAM_ALPHA,
	TEAM_BETA,

	GS_MAX_TEAMS
};

// teams
const char *GS_TeamName( int team );
const char *GS_DefaultTeamName( int team );
const char *GS_TeamSkinName( int team );
int GS_Teams_TeamFromName( const char *teamname );
bool GS_IsTeamDamage( entity_state_t *targ, entity_state_t *attacker );

//===============================================================

//gs_players.c

enum {
	BASE_CHANNEL,
	EVENT_CHANNEL,
	PLAYERANIM_CHANNELS
};

typedef struct {
	int newanim[PMODEL_PARTS];
} gs_animationbuffer_t;

typedef struct {
	int anim;
	int frame;
	int64_t startTimestamp;
	float lerpFrac;
} gs_animstate_t;

typedef struct {
	// animations in the mixer
	gs_animstate_t curAnims[PMODEL_PARTS][PLAYERANIM_CHANNELS];
	gs_animationbuffer_t buffer[PLAYERANIM_CHANNELS];

	// results
	int frame[PMODEL_PARTS];
	int oldframe[PMODEL_PARTS];
	float lerpFrac[PMODEL_PARTS];
} gs_pmodel_animationstate_t;

typedef struct {
	int firstframe[PMODEL_TOTAL_ANIMATIONS];
	int lastframe[PMODEL_TOTAL_ANIMATIONS];
	int loopingframes[PMODEL_TOTAL_ANIMATIONS];
	float frametime[PMODEL_TOTAL_ANIMATIONS];
} gs_pmodel_animationset_t;

int GS_UpdateBaseAnims( entity_state_t *state, vec3_t velocity );
void GS_PModel_AnimToFrame( int64_t curTime, gs_pmodel_animationset_t *animSet, gs_pmodel_animationstate_t *anim );
void GS_PlayerModel_ClearEventAnimations( gs_pmodel_animationset_t *animSet, gs_pmodel_animationstate_t *animState );
void GS_PlayerModel_AddAnimation( gs_pmodel_animationstate_t *animState, int loweranim, int upperanim, int headanim, int channel );


//===============================================================

typedef enum {
	MATCHMESSAGE_NONE
	, MATCHMESSAGE_CHALLENGERS_QUEUE
	, MATCHMESSAGE_ENTER_CHALLENGERS_QUEUE
	, MATCHMESSAGE_SPECTATOR_MODES
	, MATCHMESSAGE_GET_READY
	, MATCHMESSAGE_WAITING_FOR_PLAYERS
} matchmessage_t;

// gs_misc.c
void GS_SetGametypeName( const char *name );
void GS_Obituary( void *victim, int gender, void *attacker, int mod, char *message, char *message2 );
void GS_TouchPushTrigger( player_state_t *playerState, entity_state_t *pusher );
int GS_WaterLevel( entity_state_t *state, vec3_t mins, vec3_t maxs );
void GS_BBoxForEntityState( entity_state_t *state, vec3_t mins, vec3_t maxs );
float GS_FrameForTime( int *frame, int64_t curTime, int64_t startTimeStamp, float frametime, int firstframe, int lastframe, int loopingframes, bool forceLoop );

//===============================================================

// pmove->pm_features
#define PMFEAT_CROUCH           ( 1 << 0 )
#define PMFEAT_WALK             ( 1 << 1 )
#define PMFEAT_JUMP             ( 1 << 2 )
#define PMFEAT_DASH             ( 1 << 3 )
#define PMFEAT_WALLJUMP         ( 1 << 4 )
#define PMFEAT_FWDBUNNY         ( 1 << 5 )
#define PMFEAT_AIRCONTROL       ( 1 << 6 )
#define PMFEAT_ZOOM             ( 1 << 7 )
#define PMFEAT_GHOSTMOVE        ( 1 << 8 )
#define PMFEAT_CONTINOUSJUMP    ( 1 << 9 )
#define PMFEAT_ITEMPICK         ( 1 << 10 )
#define PMFEAT_GUNBLADEAUTOATTACK ( 1 << 11 )
#define PMFEAT_WEAPONSWITCH     ( 1 << 12 )
#define PMFEAT_CORNERSKIMMING   ( 1 << 13 )
#define PMFEAT_CROUCHSLIDING    ( 1 << 14 )

#define PMFEAT_ALL              ( 0xFFFF )
#define PMFEAT_DEFAULT          ( PMFEAT_ALL & ~(PMFEAT_GHOSTMOVE|PMFEAT_CORNERSKIMMING|PMFEAT_CROUCHSLIDING) )

enum {
	STAT_LAYOUTS = 0
	, STAT_HEALTH
	, STAT_ARMOR
	, STAT_WEAPON
	, STAT_WEAPON_TIME
	, STAT_PENDING_WEAPON

	, STAT_PICKUP_ITEM

	, STAT_SCORE
	, STAT_TEAM
	, STAT_REALTEAM
	, STAT_NEXT_RESPAWN

	, STAT_POINTED_PLAYER
	, STAT_POINTED_TEAMPLAYER

	, STAT_TEAM_ALPHA_SCORE
	, STAT_TEAM_BETA_SCORE

	, STAT_LAST_KILLER

	// the stats below this point are set by the gametype scripts
	, GS_GAMETYPE_STATS_START = 32

	, STAT_PROGRESS_SELF = GS_GAMETYPE_STATS_START
	, STAT_PROGRESS_OTHER
	, STAT_PROGRESS_ALPHA
	, STAT_PROGRESS_BETA

	, STAT_IMAGE_SELF
	, STAT_IMAGE_OTHER
	, STAT_IMAGE_ALPHA
	, STAT_IMAGE_BETA

	, STAT_TIME_SELF
	, STAT_TIME_BEST
	, STAT_TIME_RECORD
	, STAT_TIME_ALPHA
	, STAT_TIME_BETA

	, STAT_MESSAGE_SELF
	, STAT_MESSAGE_OTHER
	, STAT_MESSAGE_ALPHA
	, STAT_MESSAGE_BETA

	, STAT_IMAGE_CLASSACTION1
	, STAT_IMAGE_CLASSACTION2
	, STAT_IMAGE_DROP_ITEM

	, GS_GAMETYPE_STATS_END = PS_MAX_STATS

	, MAX_STATS = PS_MAX_STATS //64
};

#define ISGAMETYPESTAT( x ) ( ( x >= GS_GAMETYPE_STATS_START ) && ( x < GS_GAMETYPE_STATS_END ) )

#ifdef __GNUC__
static const char *gs_keyicon_names[] __attribute__( ( unused ) ) =
{
#else
static const char *gs_keyicon_names[] = {
#endif
	"forward",
	"backward",
	"left",
	"right",
	"fire",
	"jump",
	"crouch",
	"special"
};

// STAT_LAYOUTS flag bits meanings
#define STAT_LAYOUT_SPECDEAD        0x00000001
#define STAT_LAYOUT_INSTANTRESPAWN  0x00000002
#define STAT_LAYOUT_SCOREBOARD      0x00000004
#define STAT_LAYOUT_TEAMTAB         0x00000008
#define STAT_LAYOUT_CHALLENGER      0x00000010 // player is in challengers queue (used for ingame menu)
#define STAT_LAYOUT_READY           0x00000020 // player is ready (used for ingame menu)
//#define	STAT_LAYOUT_UNUSED			0x00000040
#define STAT_LAYOUT_SPECTEAMONLY    0x00000080

#define STAT_NOTSET                 -9999 // used for stats that don't have meaningful value atm.

//===============================================================

// means of death
#define MOD_UNKNOWN     0

typedef enum {

	//MOD_UNKNOWN,

	// Weapon damage :
	MOD_GUNBLADE_W = 36,
	MOD_GUNBLADE_S,
	MOD_MACHINEGUN_W,
	MOD_MACHINEGUN_S,
	MOD_RIOTGUN_W,
	MOD_RIOTGUN_S,
	MOD_GRENADE_W,
	MOD_GRENADE_S,
	MOD_ROCKET_W,
	MOD_ROCKET_S,
	MOD_PLASMA_W,
	MOD_PLASMA_S,
	MOD_ELECTROBOLT_W,
	MOD_ELECTROBOLT_S,
	MOD_SHOCKWAVE_W,
	MOD_SHOCKWAVE_S,
	MOD_INSTAGUN_W,
	MOD_INSTAGUN_S,
	MOD_LASERGUN_W,
	MOD_LASERGUN_S,
	MOD_GRENADE_SPLASH_W,
	MOD_GRENADE_SPLASH_S,
	MOD_ROCKET_SPLASH_W,
	MOD_ROCKET_SPLASH_S,
	MOD_PLASMA_SPLASH_W,
	MOD_PLASMA_SPLASH_S,
	MOD_SHOCKWAVE_SPLASH_W,  // explosion splash
	MOD_SHOCKWAVE_SPLASH_S,
	MOD_SHOCKWAVE_CORONA_W,  // flying projectile radius damage
	MOD_SHOCKWAVE_CORONA_S,

	// World damage
	MOD_WATER,
	MOD_SLIME,
	MOD_LAVA,
	MOD_CRUSH, // moving item blocked by player
	MOD_TELEFRAG,
	MOD_FALLING,
	MOD_SUICIDE,
	MOD_EXPLOSIVE,

	// probably not used
	MOD_BARREL,
	MOD_BOMB,
	MOD_EXIT, // except this one : player leaves
	MOD_SPLASH,
	MOD_TARGET_LASER,
	MOD_TRIGGER_HURT,
	MOD_HIT,

} mod_damage_t;

//===============================================================

//
// events, event parms
//
#ifdef THIS_IS_DISABLED
enum {
	FOOTSTEP_NORMAL,
	FOOTSTEP_BOOT,
	FOOTSTEP_FLESH,
	FOOTSTEP_MECH,
	FOOTSTEP_ENERGY,
	FOOTSTEP_METAL,
	FOOTSTEP_SPLASH,

	FOOTSTEP_TOTAL
};
#endif
enum {
	PAIN_20,
	PAIN_30,
	PAIN_60,
	PAIN_100,
	PAIN_WARSHELL,

	PAIN_TOTAL
};

//fire modes as event parm
enum {
	FIRE_MODE_WEAK,
	FIRE_MODE_STRONG,

	FIRE_MODES_COUNT
};

// vsay tokens list
enum {
	VSAY_GENERIC,
	VSAY_NEEDHEALTH,
	VSAY_NEEDWEAPON,
	VSAY_NEEDARMOR,
	VSAY_AFFIRMATIVE,
	VSAY_NEGATIVE,
	VSAY_YES,
	VSAY_NO,
	VSAY_ONDEFENSE,
	VSAY_ONOFFENSE,
	VSAY_OOPS,
	VSAY_SORRY,
	VSAY_THANKS,
	VSAY_NOPROBLEM,
	VSAY_YEEHAA,
	VSAY_GOODGAME,
	VSAY_DEFEND,
	VSAY_ATTACK,
	VSAY_NEEDBACKUP,
	VSAY_BOOO,
	VSAY_NEEDDEFENSE,
	VSAY_NEEDOFFENSE,
	VSAY_NEEDHELP,
	VSAY_ROGER,
	VSAY_ARMORFREE,
	VSAY_AREASECURED,
	VSAY_SHUTUP,
	VSAY_BOOMSTICK,
	VSAY_GOTOPOWERUP,
	VSAY_GOTOQUAD,
	VSAY_OK,
	VSAY_DEFEND_A,
	VSAY_ATTACK_A,
	VSAY_DEFEND_B,
	VSAY_ATTACK_B,

	VSAY_TOTAL = 128
};

// entity_state_t->event values
#define PREDICTABLE_EVENTS_MAX 32
typedef enum {
	EV_NONE,

	// predictable events

	EV_WEAPONACTIVATE,
	EV_FIREWEAPON,
	EV_ELECTROTRAIL,
	EV_INSTATRAIL,
	EV_FIRE_RIOTGUN,
	EV_FIRE_BULLET,
	EV_SMOOTHREFIREWEAPON,
	EV_NOAMMOCLICK,

	EV_DASH,

	EV_WALLJUMP,
	EV_WALLJUMP_FAILED,
	EV_DOUBLEJUMP,
	EV_JUMP,
	EV_JUMP_PAD,
	EV_FALL,

	// non predictable events

	EV_WEAPONDROP = PREDICTABLE_EVENTS_MAX,

	EV_ITEM_RESPAWN,
	EV_PAIN,
	EV_DIE,
	EV_GIB,

	EV_PLAYER_RESPAWN,
	EV_PLAYER_TELEPORT_IN,
	EV_PLAYER_TELEPORT_OUT,

	EV_GESTURE,
	EV_DROP,
	EV_SPOG,

	EV_BLOOD,

	EV_BLADE_IMPACT,
	EV_GUNBLADEBLAST_IMPACT,
	EV_GRENADE_BOUNCE,
	EV_GRENADE_EXPLOSION,
	EV_ROCKET_EXPLOSION,
	EV_PLASMA_EXPLOSION,
	EV_BOLT_EXPLOSION,
	EV_WAVE_EXPLOSION,
	EV_INSTA_EXPLOSION,

	// 3 spots reserved for new weapons sfx, so
	// the events below don't change their numbers easily
	EV_FREE2,
	EV_FREE3,
	EV_FREE4,

	EV_EXPLOSION1,
	EV_EXPLOSION2,

	EV_BLASTER,
	EV_SPARKS,
	EV_BULLET_SPARKS,

	EV_SEXEDSOUND,

	EV_VSAY,

	EV_LASER_SPARKS,

	EV_FIRE_SHOTGUN,
	EV_PNODE,
	EV_GREEN_LASER,

	// func movers
	EV_PLAT_HIT_TOP,
	EV_PLAT_HIT_BOTTOM,
	EV_PLAT_START_MOVING,

	EV_DOOR_HIT_TOP,
	EV_DOOR_HIT_BOTTOM,
	EV_DOOR_START_MOVING,

	EV_BUTTON_FIRE,

	EV_TRAIN_STOP,
	EV_TRAIN_START,

	MAX_EVENTS = 128
} entity_event_t;

typedef enum {
	PSEV_NONE = 0,
	PSEV_HIT,
	PSEV_PICKUP,
	PSEV_DAMAGE_20,
	PSEV_DAMAGE_40,
	PSEV_DAMAGE_60,
	PSEV_DAMAGE_80,
	PSEV_INDEXEDSOUND,
	PSEV_ANNOUNCER,
	PSEV_ANNOUNCER_QUEUED,

	PSEV_MAX_EVENTS = 0xFF
} playerstate_event_t;

//===============================================================

// entity_state_t->type values
enum {
	ET_GENERIC,
	ET_PLAYER,
	ET_CORPSE,
	ET_BEAM,
	ET_PORTALSURFACE,
	ET_PUSH_TRIGGER,

	ET_GIB,         // leave a trail
	ET_BLASTER,     // redlight + trail
	ET_ELECTRO_WEAK,
	ET_ROCKET,      // redlight + trail
	ET_GRENADE,
	ET_PLASMA,
	ET_WAVE,

	ET_SPRITE,

	ET_ITEM,        // for simple items
	ET_LASERBEAM,   // for continuous beams
	ET_CURVELASERBEAM, // for curved beams
	ET_FLAG_BASE,

	ET_MINIMAP_ICON,
	ET_DECAL,
	ET_ITEM_TIMER,  // for specs only
	ET_PARTICLES,
	ET_SPAWN_INDICATOR,

	ET_VIDEO_SPEAKER,
	ET_RADAR,       // same as ET_SPRITE but sets NO_DEPTH_TEST bit

	// eventual entities: types below this will get event treatment
	ET_EVENT = EVENT_ENTITIES_START,
	ET_SOUNDEVENT,

	ET_TOTAL_TYPES, // current count
	MAX_ENTITY_TYPES = 128
};

// entity_state_t->effects
// Effects are things handled on the client side (lights, particles, frame animations)
// that happen constantly on the given entity.
// An entity that has effects will be sent to the client
// even if it has a zero index model.
#define EF_ROTATE_AND_BOB           1           // rotate and bob (bonus items)
#define EF_SHELL                    2
#define EF_STRONG_WEAPON            4
#define EF_QUAD                     8
#define EF_CARRIER                  16
#define EF_BUSYICON                 32
#define EF_FLAG_TRAIL               64          // player is carrying the enemy flag
#define EF_TAKEDAMAGE               128
#define EF_TEAMCOLOR_TRANSITION     256
#define EF_EXPIRING_QUAD            512
#define EF_EXPIRING_SHELL           1024
#define EF_GODMODE                  2048
#define EF_REGEN                    4096
#define EF_EXPIRING_REGEN           8192
#define EF_GHOST                    16384

// oh, this is so nasty... (reuse effect bits for different entity types)
#define EF_NOPORTALENTS             EF_CARRIER
#define EF_PLAYER_STUNNED           EF_ROTATE_AND_BOB
#define EF_PLAYER_HIDENAME          EF_TEAMCOLOR_TRANSITION

// these ones can be only set from client side
#define EF_AMMOBOX                  ( 1 << 16 )
#define EF_RACEGHOST                ( 1 << 17 )
#define EF_OUTLINE                  ( 1 << 18 )
#define EF_GHOSTITEM                ( 1 << 19 )

//===============================================================
// gs_weapons.c

extern char *gs_weaponStateNames[];

enum {
	WEAPON_STATE_READY,
	WEAPON_STATE_ACTIVATING,
	WEAPON_STATE_DROPPING,
	WEAPON_STATE_POWERING,
	WEAPON_STATE_COOLDOWN,
	WEAPON_STATE_FIRING,
	WEAPON_STATE_RELOADING,     // clip loading
	WEAPON_STATE_NOAMMOCLICK,
	WEAPON_STATE_REFIRE,        // projectile loading
	WEAPON_STATE_REFIRESTRONG
};

typedef struct firedef_s {
	//ammo def
	int fire_mode;
	int ammo_id;
	int usage_count;
	int projectile_count;

	// timings
	unsigned int weaponup_time;
	unsigned int weapondown_time;
	unsigned int reload_time;
	unsigned int cooldown_time;
	unsigned int timeout;
	bool smooth_refire;

	// damages
	float damage;
	float selfdamage;
	int knockback;
	int stun;
	int splash_radius;
	float splashfrac;
	int mindamage;
	int minknockback;

	// projectile def
	int speed;
	int spread;     // horizontal spread
	int v_spread;   // vertical spread
	float friction;
	float gravity;
	int prestep;

	// ammo amounts
	int weapon_pickup;
	int ammo_pickup;
	int ammo_max;
	int ammo_low;
} firedef_t;

typedef struct {
	char *name;
	int weapon_id;

	firedef_t firedef;
	firedef_t firedef_weak;
} gs_weapon_definition_t;

gs_weapon_definition_t *GS_GetWeaponDef( int weapon );
gs_weapon_definition_t *GS_GetWeaponDefExt( int weapon, bool race );
void GS_InitWeapons( void );
int GS_SelectBestWeapon( player_state_t *playerState );
bool GS_CheckAmmoInWeapon( player_state_t *playerState, int checkweapon );
firedef_t *GS_FiredefForPlayerState( player_state_t *playerState, int checkweapon );
int GS_ThinkPlayerWeapon( player_state_t *playerState, int buttons, int msecs, int timeDelta );
trace_t *GS_TraceBullet( trace_t    *trace, vec3_t start, vec3_t dir, float r, float u, int range, int ignore, int timeDelta );
void GS_TraceLaserBeam( trace_t *trace, vec3_t origin, vec3_t angles, float range, int ignore, int timeDelta, void ( *impact )( trace_t *tr, vec3_t dir ) );
void GS_TraceCurveLaserBeam( trace_t *trace, vec3_t origin, vec3_t angles, vec3_t blendPoint, int ignore, int timeDelta, void ( *impact )( trace_t *tr, vec3_t dir ) );

#define CURVELASERBEAM_SUBDIVISIONS ( 6 )
#define MAX_CURVELASERBEAM_SUBDIVISIONS ( 24 )
#define CURVELASERBEAM_BACKTIME     60
#define LASERGUN_WEAK_TRAIL_BACKUP  32 // 0.5 second backup at 62 fps, which is the ucmd fps ratio
#define LASERGUN_WEAK_TRAIL_MASK    ( LASERGUN_WEAK_TRAIL_BACKUP - 1 )

typedef struct {
	vec3_t origins[LASERGUN_WEAK_TRAIL_BACKUP];
	int64_t timeStamps[LASERGUN_WEAK_TRAIL_BACKUP];
	bool teleported[LASERGUN_WEAK_TRAIL_BACKUP];
	int head;
}gs_laserbeamtrail_t;

void GS_AddLaserbeamPoint( gs_laserbeamtrail_t *trail, player_state_t *playerState, int64_t timeStamp );
bool G_GetLaserbeamPoint( gs_laserbeamtrail_t *trail, player_state_t *playerState, int64_t timeStamp, vec3_t out );

//===============================================================
// gs_weapondefs.c

extern firedef_t ammoFireDefs[];

#ifdef __cplusplus
};
#endif

#endif // __GS_PUBLIC_H
