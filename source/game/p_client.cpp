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
*/

#include "g_local.h"

#define PLAYER_MASS 200

/*
* player_pain
*/
void player_pain( edict_t *self, edict_t *other, float kick, int damage ) {
	// player pain is handled at the end of the frame in P_DamageFeedback
}

/*
* player_think
*/
void player_think( edict_t *self ) {
	// player entities do not think
}

/*
* ClientObituary
*/
static void ClientObituary( edict_t *self, edict_t *inflictor, edict_t *attacker ) {
	int mod;
	char message[64];
	char message2[64];

	if( level.gametype.disableObituaries ) {
		return;
	}

	mod = meansOfDeath;

	GS_Obituary( self, G_PlayerGender( self ), attacker, mod, message, message2 );

	// duplicate message at server console for logging
	if( attacker && attacker->r.client ) {
		if( attacker != self ) { // regular death message
			self->enemy = attacker;
			if( dedicated->integer ) {
				G_Printf( "%s%s %s %s%s%s\n", self->r.client->netname, S_COLOR_WHITE, message,
						  attacker->r.client->netname, S_COLOR_WHITE, message2 );
			}
		} else {      // suicide
			self->enemy = NULL;
			if( dedicated->integer ) {
				G_Printf( "%s %s%s\n", self->r.client->netname, S_COLOR_WHITE, message );
			}
		}

		G_Obituary( self, attacker, mod );
	} else {      // wrong place, suicide, etc.
		self->enemy = NULL;
		if( dedicated->integer ) {
			G_Printf( "%s %s%s\n", self->r.client->netname, S_COLOR_WHITE, message );
		}

		G_Obituary( self, ( attacker == self ) ? self : world, mod );
	}
}


//=======================================================
// DEAD BODIES
//=======================================================

/*
* G_Client_UnlinkBodies
*/
static void G_Client_UnlinkBodies( edict_t *ent ) {
	edict_t *body;
	int i;

	// find bodies linked to us
	body = &game.edicts[gs.maxclients + 1];
	for( i = 0; i < BODY_QUEUE_SIZE; body++, i++ ) {
		if( !body->r.inuse ) {
			continue;
		}

		if( body->activator == ent ) {
			// this is our body
			body->activator = NULL;
		}
	}
}

/*
* InitBodyQue
*/
void G_InitBodyQueue( void ) {
	int i;
	edict_t *ent;

	level.body_que = 0;
	for( i = 0; i < BODY_QUEUE_SIZE; i++ ) {
		ent = G_Spawn();
		ent->classname = "bodyque";
	}
}

/*
* body_die
*/
static void body_die( edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, const vec3_t point ) {
	if( self->health >= GIB_HEALTH ) {
		return;
	}

	ThrowSmallPileOfGibs( self, damage );
	self->s.origin[2] -= 48;
	ThrowClientHead( self, damage );
	self->nextThink = level.time + 3000 + random() * 3000;
}

/*
* body_think
*/
static void body_think( edict_t *self ) {
	self->health = GIB_HEALTH - 1;

	//effect: small gibs, and only when it is still a body, not a gibbed head.
	if( self->s.type == ET_CORPSE ) {
		ThrowSmallPileOfGibs( self, 25 );
	}

	//disallow interaction with the world.
	self->takedamage = DAMAGE_NO;
	self->r.solid = SOLID_NOT;
	self->s.sound = 0;
	self->flags |= FL_NO_KNOCKBACK;
	self->s.type = ET_GENERIC;
	self->r.svflags &= ~SVF_CORPSE;
	self->r.svflags |= SVF_NOCLIENT;
	self->s.modelindex = 0;
	self->s.modelindex2 = 0;
	VectorClear( self->velocity );
	VectorClear( self->avelocity );
	self->movetype = MOVETYPE_NONE;
	self->think = NULL;

	//memset( &self->snap, 0, sizeof(self->snap) );
	GClip_UnlinkEntity( self );
}

/*
* body_ready
*/
static void body_ready( edict_t *body ) {
	body->takedamage = DAMAGE_YES;
	body->r.solid = SOLID_YES;
	body->think = body_think; // body self destruction countdown
	body->nextThink = level.time + g_deadbody_autogib_delay->integer + ( crandom() * g_deadbody_autogib_delay->value * 0.25f ) ;
	GClip_LinkEntity( body );
}

/*
* CopyToBodyQue
*/
static edict_t *CopyToBodyQue( edict_t *ent, edict_t *attacker, int damage ) {
	edict_t *body;
	int contents;

	if( GS_RaceGametype() ) {
		return NULL;
	}

	contents = G_PointContents( ent->s.origin );
	if( contents & CONTENTS_NODROP ) {
		return NULL;
	}

	G_Client_UnlinkBodies( ent );

	// grab a body que and cycle to the next one
	body = &game.edicts[gs.maxclients + level.body_que + 1];
	level.body_que = ( level.body_que + 1 ) % BODY_QUEUE_SIZE;

	// send an effect on the removed body
	if( body->s.modelindex && body->s.type == ET_CORPSE ) {
		ThrowSmallPileOfGibs( body, 10 );
	}

	GClip_UnlinkEntity( body );

	memset( body, 0, sizeof( edict_t ) ); //clean up garbage

	//init body edict
	G_InitEdict( body );
	body->classname = "body";
	body->health = ent->health;
	body->mass = ent->mass;
	body->r.owner = ent->r.owner;
	body->s.type = ent->s.type;
	body->s.team = ent->s.team;
	body->s.effects = 0;
	body->r.svflags = SVF_CORPSE;
	body->r.svflags &= ~SVF_NOCLIENT;
	body->activator = ent;
	if( g_deadbody_followkiller->integer ) {
		body->enemy = attacker;
	}

	//use flat yaw
	body->s.angles[PITCH] = 0;
	body->s.angles[ROLL] = 0;
	body->s.angles[YAW] = ent->s.angles[YAW];
	body->s.modelindex2 = 0; // <-  is bodyOwner when in ET_CORPSE, but not in ET_GENERIC or ET_PLAYER
	body->s.weapon = 0;

	//copy player position and box size
	VectorCopy( ent->s.origin, body->s.origin );
	VectorCopy( ent->s.origin, body->olds.origin );
	VectorCopy( ent->r.mins, body->r.mins );
	VectorCopy( ent->r.maxs, body->r.maxs );
	VectorCopy( ent->r.absmin, body->r.absmin );
	VectorCopy( ent->r.absmax, body->r.absmax );
	VectorCopy( ent->r.size, body->r.size );
	VectorCopy( ent->velocity, body->velocity );
	body->r.maxs[2] = body->r.mins[2] + 8;

	body->r.solid = SOLID_YES;
	body->takedamage = DAMAGE_YES;
	body->r.clipmask = CONTENTS_SOLID | CONTENTS_PLAYERCLIP;
	body->movetype = MOVETYPE_TOSS;
	body->die = body_die;
	body->think = body_think; // body self destruction countdown

	if( ent->health < GIB_HEALTH
		|| meansOfDeath == MOD_ELECTROBOLT_S /* electrobolt always gibs */ ) {
		ThrowSmallPileOfGibs( body, damage );

		// reset gib impulse
		VectorClear( body->velocity );
		ThrowClientHead( body, damage ); // sets ET_GIB

		body->s.frame = 0;
		body->nextThink = level.time + 3000 + random() * 3000;
		body->deadflag = DEAD_DEAD;
	} else if( ent->s.type == ET_PLAYER ) {
		// copy the model
		body->s.type = ET_CORPSE;
		body->s.modelindex = ent->s.modelindex;
		body->s.bodyOwner = ent->s.number; // bodyOwner is the same as modelindex2
		body->s.skinnum = ent->s.skinnum;
		body->s.teleported = true;

		// launch the death animation on the body
		{
			static int i;
			i = ( i + 1 ) % 3;
			G_AddEvent( body, EV_DIE, i, true );
			switch( i ) {
				default:
				case 0:
					body->s.frame = ( ( BOTH_DEAD1 & 0x3F ) | ( BOTH_DEAD1 & 0x3F ) << 6 | ( 0 & 0xF ) << 12 );
					break;
				case 1:
					body->s.frame = ( ( BOTH_DEAD2 & 0x3F ) | ( BOTH_DEAD2 & 0x3F ) << 6 | ( 0 & 0xF ) << 12 );
					break;
				case 2:
					body->s.frame = ( ( BOTH_DEAD3 & 0x3F ) | ( BOTH_DEAD3 & 0x3F ) << 6 | ( 0 & 0xF ) << 12 );
					break;
			}
		}

		body->think = body_ready;
		body->takedamage = DAMAGE_NO;
		body->r.solid = SOLID_NOT;
		body->nextThink = level.time + 500; // make damageable in 0.5 seconds
	} else {   // wasn't a player, just copy it's model
		VectorClear( body->velocity );
		body->s.modelindex = ent->s.modelindex;
		body->s.frame = ent->s.frame;
		body->nextThink = level.time + 5000 + random() * 10000;
	}

	GClip_LinkEntity( body );
	return body;
}

/*
* player_die
*/
void player_die( edict_t *ent, edict_t *inflictor, edict_t *attacker, int damage, const vec3_t point ) {
	snap_edict_t snap_backup = ent->snap;
	client_snapreset_t resp_snap_backup = ent->r.client->resp.snap;

	VectorClear( ent->avelocity );

	ent->s.angles[0] = 0;
	ent->s.angles[2] = 0;
	ent->s.sound = 0;

	ent->r.solid = SOLID_NOT;

	ent->r.client->teamstate.last_killer = attacker;

	// player death
	ent->s.angles[YAW] = ent->r.client->ps.viewangles[YAW] = LookAtKillerYAW( ent, inflictor, attacker );
	ClientObituary( ent, inflictor, attacker );

	// create a body
	CopyToBodyQue( ent, attacker, damage );
	ent->enemy = NULL;

	// clear his combo stats
	G_AwardResetPlayerComboStats( ent );

	// go ghost (also resets snap)
	G_GhostClient( ent );

	ent->deathTimeStamp = level.time;

	VectorClear( ent->velocity );
	VectorClear( ent->avelocity );
	ent->snap = snap_backup;
	ent->r.client->resp.snap = resp_snap_backup;
	ent->r.client->resp.snap.buttons = 0;
	GClip_LinkEntity( ent );
}

/*
* G_Client_UpdateActivity
*/
void G_Client_UpdateActivity( gclient_t *client ) {
	if( !client ) {
		return;
	}
	client->level.last_activity = level.time;
}

/*
* G_Client_InactivityRemove
*/
void G_Client_InactivityRemove( gclient_t *client ) {
	if( !client ) {
		return;
	}

	if( trap_GetClientState( client - game.clients ) < CS_SPAWNED ) {
		return;
	}

	if( client->ps.pmove.pm_type != PM_NORMAL ) {
		return;
	}

	if( g_inactivity_maxtime->modified ) {
		if( g_inactivity_maxtime->value <= 0.0f ) {
			trap_Cvar_ForceSet( "g_inactivity_maxtime", "0.0" );
		} else if( g_inactivity_maxtime->value < 15.0f ) {
			trap_Cvar_ForceSet( "g_inactivity_maxtime", "15.0" );
		}

		g_inactivity_maxtime->modified = false;
	}

	if( g_inactivity_maxtime->value == 0.0f ) {
		return;
	}

	if( ( GS_MatchState() != MATCH_STATE_PLAYTIME ) || !level.gametype.removeInactivePlayers ) {
		return;
	}

	// inactive for too long
	if( client->level.last_activity && client->level.last_activity + ( g_inactivity_maxtime->value * 1000 ) < level.time ) {
		if( client->team >= TEAM_PLAYERS && client->team < GS_MAX_TEAMS ) {
			edict_t *ent = &game.edicts[ client - game.clients + 1 ];

			// move to spectators and reset the queue time, effectively removing from the challengers queue
			G_Teams_SetTeam( ent, TEAM_SPECTATOR );
			client->queueTimeStamp = 0;

			G_PrintMsg( NULL, "%s" S_COLOR_YELLOW " has been moved to spectator after %.1f seconds of inactivity\n", client->netname, g_inactivity_maxtime->value );
		}
	}
}

static void G_Client_AssignTeamSkin( edict_t *ent, char *userinfo ) {
	char skin[MAX_QPATH], model[MAX_QPATH];
	const char *userskin, *usermodel;

	// index skin file
	userskin = GS_TeamSkinName( ent->s.team ); // is it a team skin?
	if( !userskin ) { // NULL indicates *user defined*
		userskin = Info_ValueForKey( userinfo, "skin" );
		if( !userskin || !userskin[0] || !COM_ValidateRelativeFilename( userskin ) ||
			strchr( userskin, '/' ) || strstr( userskin, "invisibility" ) ) {
			userskin = NULL;
		}
	}

	// index player model
	usermodel = Info_ValueForKey( userinfo, "model" );
	if( !usermodel || !usermodel[0] || !COM_ValidateRelativeFilename( usermodel ) || strchr( usermodel, '/' ) ) {
		usermodel = NULL;
	}

	if( userskin && usermodel ) {
		Q_snprintfz( model, sizeof( model ), "$models/players/%s", usermodel );
		Q_snprintfz( skin, sizeof( skin ), "models/players/%s/%s", usermodel, userskin );
	} else {
		Q_snprintfz( model, sizeof( model ), "$models/players/%s", DEFAULT_PLAYERMODEL );
		Q_snprintfz( skin, sizeof( skin ), "models/players/%s/%s", DEFAULT_PLAYERMODEL, DEFAULT_PLAYERSKIN );
	}

	if( !ent->deadflag ) {
		ent->s.modelindex = trap_ModelIndex( model );
	}
	ent->s.skinnum = trap_SkinIndex( skin );
}

/*
* G_ClientClearStats
*/
void G_ClientClearStats( edict_t *ent ) {
	if( !ent || !ent->r.client ) {
		return;
	}

	ent->r.client->level.stats.Clear();
}

/*
* G_GhostClient
*/
void G_GhostClient( edict_t *ent ) {
	G_DeathAwards( ent );

	ent->movetype = MOVETYPE_NONE;
	ent->r.solid = SOLID_NOT;

	memset( &ent->snap, 0, sizeof( ent->snap ) );
	memset( &ent->r.client->resp.snap, 0, sizeof( ent->r.client->resp.snap ) );
	memset( &ent->r.client->resp.chase, 0, sizeof( ent->r.client->resp.chase ) );
	memset( &ent->r.client->resp.awardInfo, 0, sizeof( ent->r.client->resp.awardInfo ) );
	ent->r.client->resp.next_drown_time = 0;
	ent->r.client->resp.old_waterlevel = 0;
	ent->r.client->resp.old_watertype = 0;

	ent->s.modelindex = ent->s.modelindex2 = ent->s.skinnum = 0;
	ent->s.effects = 0;
	ent->s.weapon = 0;
	ent->s.sound = 0;
	ent->s.light = 0;
	ent->viewheight = 0;
	ent->takedamage = DAMAGE_NO;

	// clear inventory
	memset( ent->r.client->ps.inventory, 0, sizeof( ent->r.client->ps.inventory ) );

	ent->r.client->ps.stats[STAT_WEAPON] = ent->r.client->ps.stats[STAT_PENDING_WEAPON] = WEAP_NONE;
	ent->r.client->ps.weaponState = WEAPON_STATE_READY;
	ent->r.client->ps.stats[STAT_WEAPON_TIME] = 0;

	G_SetPlayerHelpMessage( ent, 0 );

	GClip_LinkEntity( ent );
}

/*
* G_ClientRespawn
*/
void G_ClientRespawn( edict_t *self, bool ghost ) {
	int i;
	edict_t *spawnpoint;
	vec3_t spawn_origin, spawn_angles;
	gclient_t *client;
	int old_team;

	G_DeathAwards( self );

	G_SpawnQueue_RemoveClient( self );

	self->r.svflags &= ~SVF_NOCLIENT;

	//if invalid be spectator
	if( self->r.client->team < 0 || self->r.client->team >= GS_MAX_TEAMS ) {
		self->r.client->team = TEAM_SPECTATOR;
	}

	// force ghost always to true when in spectator team
	if( self->r.client->team == TEAM_SPECTATOR ) {
		ghost = true;
	}

	old_team = self->s.team;
	if( self->r.client->teamstate.is_coach ) {
		ghost = true;
	}

	GClip_UnlinkEntity( self );

	client = self->r.client;

	memset( &client->resp, 0, sizeof( client->resp ) );
	memset( &client->ps, 0, sizeof( client->ps ) );
	client->resp.timeStamp = level.time;
	client->ps.playerNum = PLAYERNUM( self );

	// clear entity values
	memset( &self->snap, 0, sizeof( self->snap ) );
	memset( &self->s, 0, sizeof( self->s ) );
	memset( &self->olds, 0, sizeof( self->olds ) );
	memset( &self->invpak, 0, sizeof( self->invpak ) );

	self->s.number = self->olds.number = ENTNUM( self );

	// relink client struct
	self->r.client = &game.clients[PLAYERNUM( self )];

	// update team
	self->s.team = client->team;

	self->deadflag = DEAD_NO;
	self->s.type = ET_PLAYER;
	self->groundentity = NULL;
	self->takedamage = DAMAGE_AIM;
	self->think = player_think;
	self->pain = player_pain;
	self->die = player_die;
	self->viewheight = playerbox_stand_viewheight;
	self->r.inuse = true;
	self->mass = PLAYER_MASS;
	self->air_finished = level.time + ( 12 * 1000 );
	self->r.clipmask = MASK_PLAYERSOLID;
	self->waterlevel = 0;
	self->watertype = 0;
	self->flags &= ~FL_NO_KNOCKBACK;
	self->r.svflags &= ~SVF_CORPSE;
	self->enemy = NULL;
	self->r.owner = NULL;
	self->max_health = 100;
	self->health = self->max_health;

	if( AI_GetType( self->ai ) == AI_ISBOT ) {
		self->think = NULL;
		self->classname = "bot";
	} else if( self->r.svflags & SVF_FAKECLIENT ) {
		self->classname = "fakeclient";
	} else {
		self->classname = "player";
	}

	VectorCopy( playerbox_stand_mins, self->r.mins );
	VectorCopy( playerbox_stand_maxs, self->r.maxs );
	VectorClear( self->velocity );
	VectorClear( self->avelocity );

	client->ps.POVnum = ENTNUM( self );

	// set movement info
	client->ps.pmove.stats[PM_STAT_MAXSPEED] = (short)DEFAULT_PLAYERSPEED;
	client->ps.pmove.stats[PM_STAT_JUMPSPEED] = (short)DEFAULT_JUMPSPEED;
	client->ps.pmove.stats[PM_STAT_DASHSPEED] = (short)DEFAULT_DASHSPEED;

	if( ghost ) {
		self->r.solid = SOLID_NOT;
		self->movetype = MOVETYPE_NOCLIP;
		if( self->s.team == TEAM_SPECTATOR ) {
			self->r.svflags |= SVF_NOCLIENT;
		}
	} else {
		self->r.client->resp.takeStun = true;
		self->r.solid = SOLID_YES;
		self->movetype = MOVETYPE_PLAYER;
		client->ps.pmove.stats[PM_STAT_FEATURES] = static_cast<unsigned short>( PMFEAT_DEFAULT );
		if( !g_allow_bunny->integer ) {
			client->ps.pmove.stats[PM_STAT_FEATURES] &= ~( PMFEAT_AIRCONTROL | PMFEAT_FWDBUNNY );
		}
	}

	ClientUserinfoChanged( self, client->userinfo );

	if( old_team != self->s.team ) {
		G_Teams_UpdateMembersList();
	}

	SelectSpawnPoint( self, &spawnpoint, spawn_origin, spawn_angles );
	VectorCopy( spawn_origin, client->ps.pmove.origin );
	VectorCopy( spawn_origin, self->s.origin );

	// set angles
	self->s.angles[PITCH] = 0;
	self->s.angles[YAW] = anglemod( spawn_angles[YAW] );
	self->s.angles[ROLL] = 0;
	VectorCopy( self->s.angles, client->ps.viewangles );

	// set the delta angle
	for( i = 0; i < 3; i++ )
		client->ps.pmove.delta_angles[i] = ANGLE2SHORT( client->ps.viewangles[i] ) - client->ucmd.angles[i];

	// don't put spectators in the game
	if( !ghost ) {
		if( KillBox( self ) ) {
		}
	}

	self->s.attenuation = ATTN_NORM;

	self->s.teleported = true;

	self->aiIntrinsicEnemyWeight = 1.0f;
	self->aiVisibilityDistance = 999999.9f;

	// hold in place briefly
	client->ps.pmove.pm_flags = PMF_TIME_TELEPORT;
	client->ps.pmove.pm_time = 14;
	client->ps.pmove.stats[PM_STAT_NOUSERCONTROL] = CLIENT_RESPAWN_FREEZE_DELAY;
	client->ps.pmove.stats[PM_STAT_NOAUTOATTACK] = 1000;

	// set race stats to invisible
	client->ps.stats[STAT_TIME_SELF] = STAT_NOTSET;
	client->ps.stats[STAT_TIME_BEST] = STAT_NOTSET;
	client->ps.stats[STAT_TIME_RECORD] = STAT_NOTSET;
	client->ps.stats[STAT_TIME_ALPHA] = STAT_NOTSET;
	client->ps.stats[STAT_TIME_BETA] = STAT_NOTSET;

	self->r.client->level.respawnCount++;

	G_UseTargets( spawnpoint, self );

	GClip_LinkEntity( self );

	// let the gametypes perform their changes
	if( game.asEngine != NULL ) {
		GT_asCallPlayerRespawn( self, old_team, self->s.team );
	} else {
		G_Gametype_GENERIC_ClientRespawn( self, old_team, self->s.team );
	}

	AI_Respawn( self );
}

/*
* G_PlayerCanTeleport
*
* Checks if the player can be teleported.
*/
bool G_PlayerCanTeleport( edict_t *player ) {
	if( !player->r.client ) {
		return false;
	}
	if( player->r.client->ps.pmove.pm_type > PM_SPECTATOR ) {
		return false;
	}
	if( GS_MatchState() == MATCH_STATE_COUNTDOWN ) { // match countdown
		return false;
	}
	return true;
}

/*
* G_TeleportPlayer
*/
void G_TeleportPlayer( edict_t *player, edict_t *dest ) {
	int i;
	vec3_t velocity;
	mat3_t axis;
	float speed;
	gclient_t *client = player->r.client;

	if( !dest ) {
		return;
	}
	if( !client ) {
		return;
	}

	// draw the teleport entering effect
	G_TeleportEffect( player, false );

	//
	// teleport the player
	//

	// from racesow - use old pmove velocity
	VectorCopy( client->old_pmove.velocity, velocity );

	velocity[2] = 0; // ignore vertical velocity
	speed = VectorLengthFast( velocity );

	AnglesToAxis( dest->s.angles, axis );
	VectorScale( &axis[AXIS_FORWARD], speed, client->ps.pmove.velocity );

	VectorCopy( dest->s.angles, client->ps.viewangles );
	VectorCopy( dest->s.origin, client->ps.pmove.origin );

	// set the delta angle
	for( i = 0; i < 3; i++ )
		client->ps.pmove.delta_angles[i] = ANGLE2SHORT( client->ps.viewangles[i] ) - client->ucmd.angles[i];

	client->ps.pmove.pm_flags |= PMF_TIME_TELEPORT;
	client->ps.pmove.pm_time = 1; // force the minimum no control delay
	player->s.teleported = true;

	// update the entity from the pmove
	VectorCopy( client->ps.viewangles, player->s.angles );
	VectorCopy( client->ps.pmove.origin, player->s.origin );
	VectorCopy( client->ps.pmove.origin, player->olds.origin );
	VectorCopy( client->ps.pmove.velocity, player->velocity );

	// unlink to make sure it can't possibly interfere with KillBox
	GClip_UnlinkEntity( player );

	// kill anything at the destination
	KillBox( player );

	GClip_LinkEntity( player );

	// add the teleport effect at the destination
	G_TeleportEffect( player, true );
}

//==============================================================

/*
* ClientBegin
* called when a client has finished connecting, and is ready
* to be placed into the game.  This will happen every level load.
*/
void ClientBegin( edict_t *ent ) {
	gclient_t *client = ent->r.client;
	const char *mm_login;

	memset( &client->ucmd, 0, sizeof( client->ucmd ) );
	client->level.Reset();
	client->level.timeStamp = level.time;
	G_Client_UpdateActivity( client ); // activity detected

	client->team = TEAM_SPECTATOR;
	G_ClientRespawn( ent, true ); // respawn as ghost
	ent->movetype = MOVETYPE_NOCLIP; // allow freefly

	G_UpdatePlayerMatchMsg( ent );

	mm_login = Info_ValueForKey( client->userinfo, "cl_mm_login" );
	if( mm_login && *mm_login && client->mm_session.IsValidSessionId() ) {
		G_PrintMsg( NULL, "%s" S_COLOR_WHITE " (" S_COLOR_YELLOW "%s" S_COLOR_WHITE ") entered the game\n", client->netname, mm_login );
	} else {
		if( !level.gametype.disableObituaries || !( ent->r.svflags & SVF_FAKECLIENT ) ) {
			G_PrintMsg( NULL, "%s" S_COLOR_WHITE " entered the game\n", client->netname );
		}
	}

	client->level.respawnCount = 0; // clear respawncount
	client->connecting = false;

	// schedule the next scoreboard update
	client->level.scoreboard_time = game.realtime + scoreboardInterval - ( game.realtime % scoreboardInterval );

	G_ClientEndSnapFrame( ent ); // make sure all view stuff is valid

	// let the gametype scripts now this client just entered the level
	G_Gametype_ScoreEvent( client, "enterGame", NULL );
}

/*
* strip_highchars
* kill all chars with code >= 127
* (127 is not exactly a highchar, but we drop it, too)
*/
static void strip_highchars( char *in ) {
	char *out = in;
	for( ; *in; in++ )
		if( ( unsigned char )*in < 127 ) {
			*out++ = *in;
		}
	*out = 0;
}

/*
* G_SanitizeUserString
*/
static int G_SanitizeUserString( char *string, size_t size ) {
	static char *colorless = NULL;
	static size_t colorless_size = 0;
	int i, c_ascii;

	// life is hard, UTF-8 will have to go
	strip_highchars( string );

	COM_SanitizeColorString( va( "%s", string ), string, size, -1, COLOR_WHITE );

	Q_trim( string );

	if( colorless_size < strlen( string ) + 1 ) {
		colorless_size = strlen( string ) + 1;

		G_Free( colorless );
		colorless = ( char * )G_Malloc( colorless_size );
	}

	Q_strncpyz( colorless, COM_RemoveColorTokens( string ), colorless_size );

	// require at least one non-whitespace ascii char in the string
	// (this will upset people who would like to have a name entirely in a non-latin
	// script, but it makes damn sure you can't get an empty name by exploiting some
	// utf-8 decoder quirk)
	c_ascii = 0;
	for( i = 0; colorless[i]; i++ )
		if( colorless[i] > 32 && colorless[i] < 127 ) {
			c_ascii++;
		}

	return c_ascii;
}

/*
* G_SetName
*/
static void G_SetName( edict_t *ent, const char *original_name ) {
	const char *invalid_prefixes[] = { "console", "[team]", "[spec]", "[bot]", "[coach]", "[tv]", NULL };
	edict_t *other;
	char name[MAX_NAME_BYTES];
	char colorless[MAX_NAME_BYTES];
	int i, trynum, trylen;
	int c_ascii;
	int maxchars;

	if( !ent->r.client ) {
		return;
	}

	// we allow NULL to be passed for name
	if( !original_name ) {
		original_name = "";
	}

	Q_strncpyz( name, original_name, sizeof( name ) );

	c_ascii = G_SanitizeUserString( name, sizeof( name ) );
	if( !c_ascii ) {
		Q_strncpyz( name, "Player", sizeof( name ) );
	}
	Q_strncpyz( colorless, COM_RemoveColorTokens( name ), sizeof( colorless ) );

	if( !( ent->r.svflags & SVF_FAKECLIENT ) ) {
		for( i = 0; invalid_prefixes[i] != NULL; i++ ) {
			if( !Q_strnicmp( colorless, invalid_prefixes[i], strlen( invalid_prefixes[i] ) ) ) {
				Q_strncpyz( name, "Player", sizeof( name ) );
				Q_strncpyz( colorless, COM_RemoveColorTokens( name ), sizeof( colorless ) );
				break;
			}
		}
	}

	maxchars = MAX_NAME_CHARS;
	if( ent->r.client->isTV ) {
		maxchars = min( maxchars + 10, MAX_NAME_BYTES - 1 );
	}

	// Limit the name to MAX_NAME_CHARS printable characters
	// (non-ascii utf-8 sequences are currently counted as 2 or more each, sorry)
	COM_SanitizeColorString( va( "%s", name ), name, sizeof( name ),
							 maxchars, COLOR_WHITE );
	Q_strncpyz( colorless, COM_RemoveColorTokens( name ), sizeof( colorless ) );

	trynum = 1;
	do {
		for( i = 0; i < gs.maxclients; i++ ) {
			other = game.edicts + 1 + i;
			if( !other->r.inuse || !other->r.client || other == ent ) {
				continue;
			}

			// if nick is already in use, try with (number) appended
			if( !Q_stricmp( colorless, COM_RemoveColorTokens( other->r.client->netname ) ) ) {
				if( trynum != 1 ) { // remove last try
					name[strlen( name ) - strlen( va( "%s(%i)", S_COLOR_WHITE, trynum - 1 ) )] = 0;
				}

				// make sure there is enough space for the postfix
				trylen = strlen( va( "%s(%i)", S_COLOR_WHITE, trynum ) );
				if( (int)strlen( colorless ) + trylen > maxchars ) {
					COM_SanitizeColorString( va( "%s", name ), name, sizeof( name ),
											 maxchars - trylen, COLOR_WHITE );
					Q_strncpyz( colorless, COM_RemoveColorTokens( name ), sizeof( colorless ) );
				}

				// add the postfix
				Q_strncatz( name, va( "%s(%i)", S_COLOR_WHITE, trynum ), sizeof( name ) );
				Q_strncpyz( colorless, COM_RemoveColorTokens( name ), sizeof( colorless ) );

				// go trough all clients again
				trynum++;
				break;
			}
		}
	} while( i != gs.maxclients && trynum <= MAX_CLIENTS );

	Q_strncpyz( ent->r.client->netname, name, sizeof( ent->r.client->netname ) );
}

/*
* G_SetClan
*/
static void G_SetClan( edict_t *ent, const char *original_clan ) {
	const char *invalid_values[] = { "console", "spec", "bot", "coach", "tv", NULL };
	char clan[MAX_CLANNAME_BYTES];
	char colorless[MAX_CLANNAME_BYTES];
	int i;
	int c_ascii;
	int maxchars;

	if( !ent->r.client ) {
		return;
	}

	// we allow NULL to be passed for clan name
	if( ent->r.svflags & SVF_FAKECLIENT ) {
		original_clan = "BOT";
	} else if( !original_clan ) {
		original_clan = "";
	}

	Q_strncpyz( clan, original_clan, sizeof( clan ) );
	COM_Compress( clan );

	c_ascii = G_SanitizeUserString( clan, sizeof( clan ) );
	if( !c_ascii ) {
		clan[0] = colorless[0] = '\0';
	} else {
		Q_strncpyz( colorless, COM_RemoveColorTokens( clan ), sizeof( colorless ) );
	}

	if( !( ent->r.svflags & SVF_FAKECLIENT ) ) {
		for( i = 0; invalid_values[i] != NULL; i++ ) {
			if( !Q_strnicmp( colorless, invalid_values[i], strlen( invalid_values[i] ) ) ) {
				clan[0] = colorless[0] = '\0';
				break;
			}
		}
	}

	// clan names can not contain spaces
	Q_chrreplace( clan, ' ', '_' );

	// clan names can not start with an ampersand
	{
		char *t;
		int len;

		t = clan;
		while( *t == '&' ) t++;
		len = strlen( clan ) - ( t - clan );
		if( clan != t ) {
			memmove( clan, t, len + 1 );
		}
	}

	maxchars = MAX_CLANNAME_CHARS;

	// Limit the name to MAX_NAME_CHARS printable characters
	// (non-ascii utf-8 sequences are currently counted as 2 or more each, sorry)
	COM_SanitizeColorString( va( "%s", clan ), clan, sizeof( clan ), maxchars, COLOR_WHITE );

	Q_strncpyz( ent->r.client->clanname, clan, sizeof( ent->r.client->clanname ) );
}

/*
* think_MoveTypeSwitcher - Used to add a delay to bunnyhop style changes
*/
void think_MoveTypeSwitcher( edict_t *ent ) {
	edict_t *owner;

	if( ent->s.ownerNum > 0 && ent->s.ownerNum <= gs.maxclients ) {
		owner = &game.edicts[ent->s.ownerNum];
		if( owner->r.client ) {
			owner->r.client->movestyle = owner->r.client->movestyle_latched;
			ClientUserinfoChanged( owner, owner->r.client->userinfo );
			G_PrintMsg( owner, "Your movement style has been updated to %i\n", owner->r.client->movestyle );
		}
	}

	G_FreeEdict( ent );
}

/*
* G_UpdatePlayerInfoString
*/
static void G_UpdatePlayerInfoString( int playerNum ) {
	char playerString[MAX_INFO_STRING];
	gclient_t *client;

	assert( playerNum >= 0 && playerNum < gs.maxclients );
	client = &game.clients[playerNum];

	// update client information in cgame
	playerString[0] = 0;

	Info_SetValueForKey( playerString, "name", client->netname );
	Info_SetValueForKey( playerString, "hand", va( "%i", client->hand ) );
	Info_SetValueForKey( playerString, "color",
						 va( "%i %i %i", client->color[0], client->color[1], client->color[2] ) );

	playerString[MAX_CONFIGSTRING_CHARS - 1] = 0;
	trap_ConfigString( CS_PLAYERINFOS + playerNum, playerString );
}

/*
* G_UpdateMMPlayerInfoString
*/
static void G_UpdateMMPlayerInfoString( int playerNum ) {
	char playerString[MAX_INFO_STRING];
	gclient_t *client;

	assert( playerNum >= 0 && playerNum < gs.maxclients );
	client = &game.clients[playerNum];

	if( playerNum >= MAX_MMPLAYERINFOS ) {
		return; // oops
	}

	// update client information in cgame
	playerString[0] = 0;

	if( client->mmflags != 0 ) {
		Info_SetValueForKey( playerString, "f", va( "%x", client->mmflags ) ); // use hex for shorter representation
	}

	playerString[MAX_CONFIGSTRING_CHARS - 1] = 0;
	trap_ConfigString( CS_MMPLAYERINFOS + playerNum, playerString );
}

/*
* ClientUserinfoChanged
* called whenever the player updates a userinfo variable.
*
* The game can override any of the settings in place
* (forcing skins or names, etc) before copying it off.
*/
void ClientUserinfoChanged( edict_t *ent, char *userinfo ) {
	char *s;
	char oldname[MAX_INFO_VALUE];
	gclient_t *cl;

	int rgbcolor, i;

	assert( ent && ent->r.client );
	assert( userinfo && Info_Validate( userinfo ) );

	// check for malformed or illegal info strings
	if( !Info_Validate( userinfo ) ) {
		trap_DropClient( ent, DROP_TYPE_GENERAL, "Error: Invalid userinfo" );
		return;
	}

	cl = ent->r.client;

	// ip
	s = Info_ValueForKey( userinfo, "ip" );
	if( !s ) {
		trap_DropClient( ent, DROP_TYPE_GENERAL, "Error: Server didn't provide client IP" );
		return;
	}

	Q_strncpyz( cl->ip, s, sizeof( cl->ip ) );

	// socket
	s = Info_ValueForKey( userinfo, "socket" );
	if( !s ) {
		trap_DropClient( ent, DROP_TYPE_GENERAL, "Error: Server didn't provide client socket" );
		return;
	}

	Q_strncpyz( cl->socket, s, sizeof( cl->socket ) );

	// color
	s = Info_ValueForKey( userinfo, "color" );
	if( s ) {
		rgbcolor = COM_ReadColorRGBString( s );
	} else {
		rgbcolor = -1;
	}

	if( rgbcolor != -1 ) {
		rgbcolor = COM_ValidatePlayerColor( rgbcolor );
		Vector4Set( cl->color, COLOR_R( rgbcolor ), COLOR_G( rgbcolor ), COLOR_B( rgbcolor ), 255 );
	} else {
		Vector4Set( cl->color, 255, 255, 255, 255 );
	}

	// set name, it's validated and possibly changed first
	Q_strncpyz( oldname, cl->netname, sizeof( oldname ) );
	G_SetName( ent, Info_ValueForKey( userinfo, "name" ) );
	if( oldname[0] && Q_stricmp( oldname, cl->netname ) && !cl->isTV && !CheckFlood( ent, false ) ) {
		G_PrintMsg( NULL, "%s%s is now known as %s%s\n", oldname, S_COLOR_WHITE, cl->netname, S_COLOR_WHITE );
	}
	if( !Info_SetValueForKey( userinfo, "name", cl->netname ) ) {
		trap_DropClient( ent, DROP_TYPE_GENERAL, "Error: Couldn't set userinfo (name)" );
		return;
	}

	// clan tag
	G_SetClan( ent, Info_ValueForKey( userinfo, "clan" ) );

	// handedness
	s = Info_ValueForKey( userinfo, "hand" );
	if( !s ) {
		cl->hand = 2;
	} else {
		cl->hand = bound( atoi( s ), 0, 2 );
	}

	// handicap
	s = Info_ValueForKey( userinfo, "handicap" );
	if( s ) {
		i = atoi( s );

		if( i > 90 || i < 0 ) {
			G_PrintMsg( ent, "Handicap must be defined in the [0-90] range.\n" );
			cl->handicap = 0;
		} else {
			cl->handicap = i;
		}
	}

	s = Info_ValueForKey( userinfo, "cg_movementStyle" );
	if( s ) {
		i = bound( atoi( s ), 0, GS_MAXBUNNIES - 1 );
		if( trap_GetClientState( PLAYERNUM( ent ) ) < CS_SPAWNED ) {
			if( i != cl->movestyle ) {
				cl->movestyle = cl->movestyle_latched = i;
			}
		} else if( cl->movestyle_latched != cl->movestyle ) {
			G_PrintMsg( ent, "A movement style change is already in progress. Please wait.\n" );
		} else if( i != cl->movestyle_latched ) {
			cl->movestyle_latched = i;
			if( cl->movestyle_latched != cl->movestyle ) {
				edict_t *switcher;

				switcher = G_Spawn();
				switcher->think = think_MoveTypeSwitcher;
				switcher->nextThink = level.time + 10000;
				switcher->s.ownerNum = ENTNUM( ent );
				G_PrintMsg( ent, "Movement style will change in 10 seconds.\n" );
			}
		}
	}

	// update the movement features depending on the movestyle
	if( !G_ISGHOSTING( ent ) && g_allow_bunny->integer ) {
		if( cl->movestyle == GS_CLASSICBUNNY ) {
			cl->ps.pmove.stats[PM_STAT_FEATURES] &= ~PMFEAT_FWDBUNNY;
		} else {
			cl->ps.pmove.stats[PM_STAT_FEATURES] |= PMFEAT_FWDBUNNY;
		}
	}

	s = Info_ValueForKey( userinfo, "cg_noAutohop" );
	if( s && s[0] ) {
		if( atoi( s ) != 0 ) {
			cl->ps.pmove.stats[PM_STAT_FEATURES] &= ~PMFEAT_CONTINOUSJUMP;
		} else {
			cl->ps.pmove.stats[PM_STAT_FEATURES] |= PMFEAT_CONTINOUSJUMP;
		}
	}

#ifdef UCMDTIMENUDGE
	s = Info_ValueForKey( userinfo, "cl_ucmdTimeNudge" );
	if( !s ) {
		cl->ucmdTimeNudge = 0;
	} else {
		cl->ucmdTimeNudge = atoi( s );
		clamp( cl->ucmdTimeNudge, -MAX_UCMD_TIMENUDGE, MAX_UCMD_TIMENUDGE );
	}
#endif

	// mm session
	// TODO: remove the key after storing it to gclient_t !
	s = Info_ValueForKey( userinfo, "cl_mm_session" );
	if( !Uuid_FromString( s, &cl->mm_session ) ) {
		cl->mm_session = Uuid_ZeroUuid();
	}

	s = Info_ValueForKey( userinfo, "mmflags" );
	cl->mmflags = ( s == NULL ) ? 0 : strtoul( s, NULL, 10 );

	// tv
	if( cl->isTV ) {
		s = Info_ValueForKey( userinfo, "tv_port" );
		cl->tv.port = s ? atoi( s ) : 0;

		s = Info_ValueForKey( userinfo, "tv_port6" );
		cl->tv.port6 = s ? atoi( s ) : 0;

		s = Info_ValueForKey( userinfo, "max_cl" );
		cl->tv.maxclients = s ? atoi( s ) : 0;

		s = Info_ValueForKey( userinfo, "num_cl" );
		cl->tv.numclients = s ? atoi( s ) : 0;

		s = Info_ValueForKey( userinfo, "chan" );
		cl->tv.channel = s ? atoi( s ) : 0;
	}

	if( !G_ISGHOSTING( ent ) && trap_GetClientState( PLAYERNUM( ent ) ) >= CS_SPAWNED ) {
		G_Client_AssignTeamSkin( ent, userinfo );
	}

	// save off the userinfo in case we want to check something later
	Q_strncpyz( cl->userinfo, userinfo, sizeof( cl->userinfo ) );

	G_UpdatePlayerInfoString( PLAYERNUM( ent ) );
	G_UpdateMMPlayerInfoString( PLAYERNUM( ent ) );

	G_Gametype_ScoreEvent( cl, "userinfochanged", oldname );
}


/*
* ClientConnect
* Called when a player begins connecting to the server.
* The game can refuse entrance to a client by returning false.
* If the client is allowed, the connection process will continue
* and eventually get to ClientBegin()
* Changing levels will NOT cause this to be called again, but
* loadgames will.
*/
bool ClientConnect( edict_t *ent, char *userinfo, bool fakeClient, bool tvClient ) {
	char *value;

	assert( ent );
	assert( userinfo && Info_Validate( userinfo ) );

	// verify that server gave us valid data
	if( !Info_Validate( userinfo ) ) {
		Info_SetValueForKey( userinfo, "rejtype", va( "%i", DROP_TYPE_GENERAL ) );
		Info_SetValueForKey( userinfo, "rejflag", va( "%i", 0 ) );
		Info_SetValueForKey( userinfo, "rejmsg", "Invalid userinfo" );
		return false;
	}

	if( !Info_ValueForKey( userinfo, "ip" ) ) {
		Info_SetValueForKey( userinfo, "rejtype", va( "%i", DROP_TYPE_GENERAL ) );
		Info_SetValueForKey( userinfo, "rejflag", va( "%i", 0 ) );
		Info_SetValueForKey( userinfo, "rejmsg", "Error: Server didn't provide client IP" );
		return false;
	}

	if( !Info_ValueForKey( userinfo, "socket" ) ) {
		Info_SetValueForKey( userinfo, "rejtype", va( "%i", DROP_TYPE_GENERAL ) );
		Info_SetValueForKey( userinfo, "rejflag", va( "%i", 0 ) );
		Info_SetValueForKey( userinfo, "rejmsg", "Error: Server didn't provide client socket" );
		return false;
	}

	// check to see if they are on the banned IP list
	value = Info_ValueForKey( userinfo, "ip" );
	if( SV_FilterPacket( value ) ) {
		Info_SetValueForKey( userinfo, "rejtype", va( "%i", DROP_TYPE_GENERAL ) );
		Info_SetValueForKey( userinfo, "rejflag", va( "%i", 0 ) );
		Info_SetValueForKey( userinfo, "rejmsg", "You're banned from this server" );
		return false;
	}

	// check for a password
	value = Info_ValueForKey( userinfo, "password" );
	if( !fakeClient && ( *password->string && ( !value || strcmp( password->string, value ) ) ) ) {
		Info_SetValueForKey( userinfo, "rejtype", va( "%i", DROP_TYPE_PASSWORD ) );
		Info_SetValueForKey( userinfo, "rejflag", va( "%i", 0 ) );
		if( value && value[0] ) {
			Info_SetValueForKey( userinfo, "rejmsg", "Incorrect password" );
		} else {
			Info_SetValueForKey( userinfo, "rejmsg", "Password required" );
		}
		return false;
	}

	// they can connect

	G_InitEdict( ent );
	ent->s.modelindex = 0;
	ent->r.solid = SOLID_NOT;
	ent->r.client = game.clients + PLAYERNUM( ent );
	ent->r.svflags = ( SVF_NOCLIENT | ( fakeClient ? SVF_FAKECLIENT : 0 ) );
	ent->r.client->Reset();
	ent->r.client->ps.playerNum = PLAYERNUM( ent );
	ent->r.client->connecting = true;
	ent->r.client->isTV = tvClient == true;
	ent->r.client->team = TEAM_SPECTATOR;
	G_Client_UpdateActivity( ent->r.client ); // activity detected

	ClientUserinfoChanged( ent, userinfo );

	if( !fakeClient ) {
		char message[MAX_STRING_CHARS];

		Q_snprintfz( message, sizeof( message ), "%s%s connected", ent->r.client->netname, S_COLOR_WHITE );

		G_PrintMsg( NULL, "%s\n", message );

		G_Printf( "%s%s connected from %s\n", ent->r.client->netname, S_COLOR_WHITE, ent->r.client->ip );
	}

	// let the gametype scripts know this client just connected
	G_Gametype_ScoreEvent( ent->r.client, "connect", NULL );

	G_CallVotes_ResetClient( PLAYERNUM( ent ) );

	return true;
}

/*
* ClientDisconnect
* Called when a player drops from the server.
* Will not be called between levels.
*/
void ClientDisconnect( edict_t *ent, const char *reason ) {
	int team;

	if( !ent->r.client || !ent->r.inuse ) {
		return;
	}

	// always report in RACE mode
	if( GS_RaceGametype()
		|| ( ent->r.client->team != TEAM_SPECTATOR && ( GS_MatchState() == MATCH_STATE_PLAYTIME || GS_MatchState() == MATCH_STATE_POSTMATCH ) ) ) {
		G_AddPlayerReport( ent, GS_MatchState() == MATCH_STATE_POSTMATCH );
	}

	for( team = TEAM_PLAYERS; team < GS_MAX_TEAMS; team++ )
		G_Teams_UnInvitePlayer( team, ent );

	if( !level.gametype.disableObituaries || !( ent->r.svflags & SVF_FAKECLIENT ) ) {
		if( !reason ) {
			G_PrintMsg( NULL, "%s" S_COLOR_WHITE " disconnected\n", ent->r.client->netname );
		} else {
			G_PrintMsg( NULL, "%s" S_COLOR_WHITE " disconnected (%s" S_COLOR_WHITE ")\n", ent->r.client->netname, reason );
		}
	}

	// send effect
	if( ent->s.team > TEAM_SPECTATOR ) {
		G_TeleportEffect( ent, false );
	}

	ent->r.client->team = TEAM_SPECTATOR;
	G_ClientRespawn( ent, true ); // respawn as ghost
	ent->movetype = MOVETYPE_NOCLIP; // allow freefly

	// let the gametype scripts know this client just disconnected
	G_Gametype_ScoreEvent( ent->r.client, "disconnect", NULL );

	ent->r.inuse = false;
	ent->r.svflags = SVF_NOCLIENT;

	ent->r.client->Reset();
	ent->r.client->ps.playerNum = PLAYERNUM( ent );

	trap_ConfigString( CS_PLAYERINFOS + PLAYERNUM( ent ), "" );
	GClip_UnlinkEntity( ent );

	G_Match_CheckReadys();
}

/*
* G_MoveClientToTV
*
* Sends cmd to connect to a non-full TV server with round robin balancing
*/
void G_MoveClientToTV( edict_t *ent ) {
	int i;
	gclient_t *client, *best;
	static int last_tv = 0;
	bool isIPv6;
	char ip[MAX_INFO_VALUE];
	int port;
	const char *p;

	if( !ent->r.client ) {
		return;
	}
	if( ent->r.svflags & SVF_FAKECLIENT ) {
		return;
	}
	if( ent->r.client->isTV ) {
		return;
	}

	best = NULL;
	for( i = 0; i < gs.maxclients; i++ ) {
		client = &game.clients[( last_tv + 1 + i ) % gs.maxclients];
		if( !client->isTV || client->connecting ) {
			// not a TV or not ready yet
			continue;
		}
		if( client->tv.numclients == client->tv.maxclients ) {
			// full
			continue;
		}
		if( !client->tv.channel ) {
			// invalid userinfo/channel number
			continue;
		}
		best = client;
		break;
	}

	if( !best ) {
		G_PrintMsg( ent, "Could not find a free TV server\n" );
		return;
	}

	Q_strncpyz( ip, best->ip, sizeof( ip ) );

	// check IP type
	p = strstr( ip, "::" );
	isIPv6 = p != NULL;

	// strip port number from address string
	p = strrchr( ip, ':' );
	if( p != NULL ) {
		ip[p - ip] = '\0';
	}

	port = isIPv6 ? best->tv.port6 : best->tv.port;

	last_tv = best - game.clients;
	trap_GameCmd( ent, va( "memo tv_moveto \"%s\" %s:%hu#%i", COM_RemoveColorTokens( best->netname ), ip, port, best->tv.channel ) );
}

//==============================================================

/*
* G_PredictedEvent
*/
void G_PredictedEvent( int entNum, int ev, int parm ) {
	edict_t *ent;
	vec3_t upDir = { 0, 0, 1 };

	ent = &game.edicts[entNum];
	switch( ev ) {
		case EV_FALL:
		{
			int dflags, damage;
			dflags = 0;
			damage = parm;

			if( damage ) {
				G_Damage( ent, world, world, vec3_origin, upDir, ent->s.origin, damage, 0, 0, dflags, MOD_FALLING );
			}

			G_AddEvent( ent, ev, damage, true );
		}
		break;

		case EV_SMOOTHREFIREWEAPON: // update the firing
			G_FireWeapon( ent, parm );
			break; // don't send the event

		case EV_FIREWEAPON:
			G_FireWeapon( ent, parm );
			G_AddEvent( ent, ev, parm, true );
			break;

		case EV_WEAPONDROP:
			G_AddEvent( ent, ev, parm, true );
			break;

		case EV_WEAPONACTIVATE:
			ent->s.weapon = parm;
			G_AddEvent( ent, ev, parm, true );
			break;

		default:
			G_AddEvent( ent, ev, parm, true );
			break;
	}
}

/*
* ClientMakePlrkeys
*/
static void ClientMakePlrkeys( gclient_t *client, usercmd_t *ucmd ) {
	client_snapreset_t *clsnap;

	if( !client ) {
		return;
	}

	clsnap = &client->resp.snap;
	clsnap->plrkeys = 0; // clear it first

	if( ucmd->forwardmove > 0 ) {
		clsnap->plrkeys |= ( 1 << KEYICON_FORWARD );
	}
	if( ucmd->forwardmove < 0 ) {
		clsnap->plrkeys |= ( 1 << KEYICON_BACKWARD );
	}
	if( ucmd->sidemove > 0 ) {
		clsnap->plrkeys |= ( 1 << KEYICON_RIGHT );
	}
	if( ucmd->sidemove < 0 ) {
		clsnap->plrkeys |= ( 1 << KEYICON_LEFT );
	}
	if( ucmd->upmove > 0 ) {
		clsnap->plrkeys |= ( 1 << KEYICON_JUMP );
	}
	if( ucmd->upmove < 0 ) {
		clsnap->plrkeys |= ( 1 << KEYICON_CROUCH );
	}
	if( ucmd->buttons & BUTTON_ATTACK ) {
		clsnap->plrkeys |= ( 1 << KEYICON_FIRE );
	}
	if( ucmd->buttons & BUTTON_SPECIAL ) {
		clsnap->plrkeys |= ( 1 << KEYICON_SPECIAL );
	}
}

/*
* ClientMultiviewChanged
* This will be called when client tries to change multiview mode
* Mode change can be disallowed by returning false
*/
bool ClientMultiviewChanged( edict_t *ent, bool multiview ) {
	ent->r.client->multiview = multiview == true;

	return true;
}

/*
* ClientThink
*/
void ClientThink( edict_t *ent, usercmd_t *ucmd, int timeDelta ) {
	gclient_t *client;
	int i, j;
	static pmove_t pm;
	int delta, count;

	client = ent->r.client;

	client->ps.POVnum = ENTNUM( ent );
	client->ps.playerNum = PLAYERNUM( ent );

	// anti-lag
	if( ent->r.svflags & SVF_FAKECLIENT ) {
		client->timeDelta = 0;
	} else {
		int nudge;
		int fixedNudge = ( game.snapFrameTime ) * 0.5; // fixme: find where this nudge comes from.

		// add smoothing to timeDelta between the last few ucmds and a small fine-tuning nudge.
		nudge = fixedNudge + g_antilag_timenudge->integer;
		timeDelta += nudge;
		clamp( timeDelta, -g_antilag_maxtimedelta->integer, 0 );

		// smooth using last valid deltas
		i = client->timeDeltasHead - 6;
		if( i < 0 ) {
			i = 0;
		}
		for( count = 0, delta = 0; i < client->timeDeltasHead; i++ ) {
			if( client->timeDeltas[i & G_MAX_TIME_DELTAS_MASK] < 0 ) {
				delta += client->timeDeltas[i & G_MAX_TIME_DELTAS_MASK];
				count++;
			}
		}

		if( !count ) {
			client->timeDelta = timeDelta;
		} else {
			delta /= count;
			client->timeDelta = ( delta + timeDelta ) * 0.5;
		}

		client->timeDeltas[client->timeDeltasHead & G_MAX_TIME_DELTAS_MASK] = timeDelta;
		client->timeDeltasHead++;

#ifdef UCMDTIMENUDGE
		client->timeDelta += client->pers.ucmdTimeNudge;
#endif
	}

	clamp( client->timeDelta, -g_antilag_maxtimedelta->integer, 0 );

	// update activity if he touched any controls
	if( ucmd->forwardmove != 0 || ucmd->sidemove != 0 || ucmd->upmove != 0 || ( ucmd->buttons & ~BUTTON_BUSYICON ) != 0
		|| client->ucmd.angles[PITCH] != ucmd->angles[PITCH] || client->ucmd.angles[YAW] != ucmd->angles[YAW] ) {
		G_Client_UpdateActivity( client );
	}

	client->ucmd = *ucmd;

	// can exit intermission after two seconds, not counting postmatch
	if( GS_MatchState() == MATCH_STATE_WAITEXIT && ( ucmd->buttons & BUTTON_ATTACK ) && game.serverTime > GS_MatchStartTime() + 2000 ) {
		level.exitNow = true;
	}

	// (is this really needed?:only if not cared enough about ps in the rest of the code)
	// refresh player state position from the entity
	VectorCopy( ent->s.origin, client->ps.pmove.origin );
	VectorCopy( ent->velocity, client->ps.pmove.velocity );
	VectorCopy( ent->s.angles, client->ps.viewangles );

	client->ps.pmove.gravity = level.gravity;

	if( GS_MatchState() >= MATCH_STATE_POSTMATCH || GS_MatchPaused()
		|| ( ent->movetype != MOVETYPE_PLAYER && ent->movetype != MOVETYPE_NOCLIP ) ) {
		client->ps.pmove.pm_type = PM_FREEZE;
	} else if( ent->s.type == ET_GIB ) {
		client->ps.pmove.pm_type = PM_GIB;
	} else if( ent->movetype == MOVETYPE_NOCLIP || client->isTV ) {
		client->ps.pmove.pm_type = PM_SPECTATOR;
	} else {
		client->ps.pmove.pm_type = PM_NORMAL;
	}

	// set up for pmove
	memset( &pm, 0, sizeof( pmove_t ) );
	pm.playerState = &client->ps;

	if( !client->isTV ) {
		pm.cmd = *ucmd;
	}

	// perform a pmove
	Pmove( &pm );

	// save results of pmove
	client->old_pmove = client->ps.pmove;

	// update the entity with the new position
	VectorCopy( client->ps.pmove.origin, ent->s.origin );
	VectorCopy( client->ps.pmove.velocity, ent->velocity );
	VectorCopy( client->ps.viewangles, ent->s.angles );
	ent->viewheight = client->ps.viewheight;
	VectorCopy( pm.mins, ent->r.mins );
	VectorCopy( pm.maxs, ent->r.maxs );

	ent->waterlevel = pm.waterlevel;
	ent->watertype = pm.watertype;
	if( pm.groundentity == -1 ) {
		ent->groundentity = NULL;
	} else {
		G_AwardResetPlayerComboStats( ent );

		ent->groundentity = &game.edicts[pm.groundentity];
		ent->groundentity_linkcount = ent->groundentity->linkcount;
	}

	GClip_LinkEntity( ent );

	GS_AddLaserbeamPoint( &ent->r.client->resp.trail, &ent->r.client->ps, ucmd->serverTimeStamp );

	// Regeneration
	if( ent->r.client->ps.inventory[POWERUP_REGEN] > 0 && ent->health < 200 ) {
		ent->health += ( game.frametime * 0.001f ) * 10.0f;

		// Regen expires if health reaches 200
		if( ent->health >= 199.0f ) {
			ent->r.client->ps.inventory[POWERUP_REGEN]--;
		}
	}

	// fire touch functions
	if( ent->movetype != MOVETYPE_NOCLIP ) {
		edict_t *other;

		// touch other objects
		for( i = 0; i < pm.numtouch; i++ ) {
			other = &game.edicts[pm.touchents[i]];
			for( j = 0; j < i; j++ ) {
				if( &game.edicts[pm.touchents[j]] == other ) {
					break;
				}
			}
			if( j != i ) {
				continue; // duplicated

			}
			// player can't touch projectiles, only projectiles can touch the player
			G_CallTouch( other, ent, NULL, 0 );
		}
	}

	ent->s.weapon = GS_ThinkPlayerWeapon( &client->ps, ucmd->buttons, ucmd->msec, client->timeDelta );

	if( G_IsDead( ent ) ) {
		if( ent->deathTimeStamp + g_respawn_delay_min->integer <= level.time ) {
			client->resp.snap.buttons |= ucmd->buttons;
		}
	} else if( client->ps.pmove.stats[PM_STAT_NOUSERCONTROL] <= 0 ) {
		client->resp.snap.buttons |= ucmd->buttons;
	}

	// trigger the instashield
	if( GS_Instagib() && g_instashield->integer ) {
		if( client->ps.pmove.pm_type == PM_NORMAL && pm.cmd.upmove < 0 &&
			client->resp.instashieldCharge == INSTA_SHIELD_MAX &&
			client->ps.inventory[POWERUP_SHELL] == 0 ) {
			client->ps.inventory[POWERUP_SHELL] = client->resp.instashieldCharge;
			G_Sound( ent, CHAN_AUTO, trap_SoundIndex( GS_FindItemByTag( POWERUP_SHELL )->pickup_sound ), ATTN_NORM );
		}
	}

	//
	if( client->ps.pmove.pm_type == PM_NORMAL ) {
		client->level.stats.had_playtime = true;
	}

	// generating plrkeys (optimized for net communication)
	ClientMakePlrkeys( client, ucmd );
}

/*
* G_ClientThink
* Client frame think, and call to execute its usercommands thinking
*/
void G_ClientThink( edict_t *ent ) {
	if( !ent || !ent->r.client ) {
		return;
	}

	if( trap_GetClientState( PLAYERNUM( ent ) ) < CS_SPAWNED ) {
		return;
	}

	ent->r.client->ps.POVnum = ENTNUM( ent ); // set self

	// load instashield
	if( GS_Instagib() && g_instashield->integer ) {
		if( ent->s.team >= TEAM_PLAYERS && ent->s.team < GS_MAX_TEAMS ) {
			if( ent->r.client->ps.inventory[POWERUP_SHELL] > 0 ) {
				ent->r.client->resp.instashieldCharge -= ( game.frametime * 0.001f ) * 60.0f;
				clamp( ent->r.client->resp.instashieldCharge, 0, INSTA_SHIELD_MAX );
				if( ent->r.client->resp.instashieldCharge == 0 ) {
					ent->r.client->ps.inventory[POWERUP_SHELL] = 0;
				}
			} else {
				ent->r.client->resp.instashieldCharge += ( game.frametime * 0.001f ) * 20.0f;
				clamp( ent->r.client->resp.instashieldCharge, 0, INSTA_SHIELD_MAX );
			}
		}
	}

	// run bots thinking with the rest of clients
	if( ent->r.svflags & SVF_FAKECLIENT ) {
		if( !ent->think && AI_GetType( ent->ai ) == AI_ISBOT ) {
			AI_Think( ent );
		}
	}

	trap_ExecuteClientThinks( PLAYERNUM( ent ) );
}

/*
* G_CheckClientRespawnClick
*/
void G_CheckClientRespawnClick( edict_t *ent ) {
	if( !ent->r.inuse || !ent->r.client || !G_IsDead( ent ) ) {
		return;
	}

	if( GS_MatchState() >= MATCH_STATE_POSTMATCH ) {
		return;
	}

	if( trap_GetClientState( PLAYERNUM( ent ) ) >= CS_SPAWNED ) {
		// if the spawnsystem doesn't require to click
		if( G_SpawnQueue_GetSystem( ent->s.team ) != SPAWNSYSTEM_INSTANT ) {
			int minDelay = g_respawn_delay_min->integer;

			// waves system must wait for at least 500 msecs (to see the death, but very short for selfkilling tactics).
			if( G_SpawnQueue_GetSystem( ent->s.team ) == SPAWNSYSTEM_WAVES ) {
				minDelay = ( g_respawn_delay_min->integer < 500 ) ? 500 : g_respawn_delay_min->integer;
			}

			// hold system must wait for at least 1000 msecs (to see the death properly)
			if( G_SpawnQueue_GetSystem( ent->s.team ) == SPAWNSYSTEM_HOLD ) {
				minDelay = ( g_respawn_delay_min->integer < 1300 ) ? 1300 : g_respawn_delay_min->integer;
			}

			if( level.time >= ent->deathTimeStamp + minDelay ) {
				G_SpawnQueue_AddClient( ent );
			}
		}
		// clicked
		else if( ent->r.client->resp.snap.buttons & BUTTON_ATTACK ) {
			if( level.time > ent->deathTimeStamp + g_respawn_delay_min->integer ) {
				G_SpawnQueue_AddClient( ent );
			}
		}
		// didn't click, but too much time passed
		else if( g_respawn_delay_max->integer && ( level.time > ent->deathTimeStamp + g_respawn_delay_max->integer ) ) {
			G_SpawnQueue_AddClient( ent );
		}
	}
}

#undef PLAYER_MASS
