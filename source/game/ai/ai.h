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

#ifndef QFUSION_AI_H
#define QFUSION_AI_H

typedef enum {
	AI_INACTIVE,
	AI_ISBOT,
	AI_ISMONSTER,

	AI_MAX_TYPES
} ai_type;

typedef struct ai_handle_s ai_handle_t;

extern const struct asEnum_s asAIEnums[];
extern const struct asClassDescriptor_s *asAIClassesDescriptors[];
extern const struct asglobfuncs_s asAIGlobFuncs[];

typedef enum {
	// A nav entity should be reached at touch (as regular items like weapons, armors, etc.)
	// A nav entity should be spawned, otherwise an AI may (and usually should) wait for it.
	// This flag cannot be combined with REACH_AT_RADIUS or REACH_ON_EVENT.
	AI_NAV_REACH_AT_TOUCH = 0x1,
	// A nav entity should be reached by proximity. Default proximity raduis is close to a player height.
	// This flag cannot be combined with REACH_AT_TOUCH or REACH_ON_EVENT.
	AI_NAV_REACH_AT_RADIUS = 0x2,
	// A nav entity may be reached on an event (when a callee decides), an AI should wait on the item for it.
	// This flag cannot be combined with REACH_AT_TOUCH or REACH_AT_RADIUS.
	AI_NAV_REACH_ON_EVENT = 0x4,
	// A nav entity may (and should) be reached in group (like a bomb being planted).
	// If this flag is set, all AI's in a squad (if any) will try to reach it.
	// Otherwise a squad will select a goal grabber, and other AIs will try to assist it (like Quad pickup).
	AI_NAV_REACH_IN_GROUP = 0x8,
	// One of the following callbacks:
	// AI_asOnBotTouchedGoal() for REACH_AT_TOUCH
	// AI_asOnBotIsCloseToGoal() for REACH_AT_RADIUS
	// should be called on corresponding event detected by bot native code
	AI_NAV_NOTIFY_SCRIPT = 0x100,
	// A nav entity will disappear at its next think.
	// An AI may skip this goal if it thinks that goal will disappear before it may be reached.
	AI_NAV_DROPPED = 0x1000,
	// A nav entity is movable (e.g. is a player).
	// Use this flag cautious to prevent AI cheating with revealing enemy origin.
	// (A good use case for this flag is a flag carrier of the own team, not the enemy one).
	// Note that all clients are already added as such entities at level start.
	// If you really want to force AI pursue some client, set its external entity weight.
	// Note that client nav entities also have REACH_ON_EVENT flags.
	AI_NAV_MOVABLE = 0x4000
} ai_nav_entity_flags;

// Should be called before static entities spawn
void AI_InitLevel( void );
// Should be called before level and entities data cleanup
void AI_Shutdown( void );
void AI_BeforeLevelLevelScriptShutdown( void );
// Should be called before level restart
void AI_AfterLevelScriptShutdown();
// Should be called before all entities (including AI's and clients) think
void AI_CommonFrame( void );
// Should be called when an AI joins a team
void AI_JoinedTeam( edict_t *ent, int team );

void AI_InitGametypeScript( class asIScriptModule *module );
void AI_ResetGametypeScript();

// Should be called when a static item is spawned or a dropped item stopped its movement
void AI_AddNavEntity( edict_t *ent, ai_nav_entity_flags flags );
// Should be called when an item edict is about to be freed
void AI_RemoveNavEntity( edict_t *ent );
// Should be called when an item has been considered to be reached by this function caller
// (A corresponding nav entity should be added with a AI_NAV_REACH_ON_EVENT flag)
void AI_NavEntityReached( edict_t *ent );

void        AI_Think( edict_t *self );
void        G_FreeAI( edict_t *ent );
void        G_SpawnAI( edict_t *ent, float skillLevel = 0.1f );
ai_type     AI_GetType( const ai_handle_t *ai );
void        AI_TouchedEntity( edict_t *self, edict_t *ent );
void        AI_DamagedEntity( edict_t *self, edict_t *ent, int damage );
void        AI_Pain( edict_t *self, edict_t *attacker, int kick, int damage );
void        AI_Knockback( edict_t *self, edict_t *attacker, const vec3_t basedir, int kick, int dflags );

void        AI_RegisterEvent( edict_t *ent, int event, int parm );

void        AI_SpawnBot( const char *team );
void        AI_RemoveBot( const char *name );
void        AI_RemoveBots();
void        AI_Respawn( edict_t *ent );

void        AI_Cheat_NoTarget( edict_t *ent );

#endif