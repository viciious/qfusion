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
#include "../gameshared/q_collision.h"

// commented out to make gcc happy
#if 0
/*
* W_Fire_Lead
* the seed is important to be as pointer for cgame prediction accuracy
*/
static void W_Fire_Lead( edict_t *self, vec3_t start, vec3_t aimdir, vec3_t axis[3], int damage,
						 int knockback, int stun, int hspread, int vspread, int *seed, int dflags,
						 int mod, int timeDelta ) {
	trace_t tr;
	vec3_t dir;
	vec3_t end;
	float r;
	float u;
	vec3_t water_start;
	int content_mask = MASK_SHOT | MASK_WATER;

	G_Trace4D( &tr, self->s.origin, NULL, NULL, start, self, MASK_SHOT, timeDelta );
	if( !( tr.fraction < 1.0 ) ) {
#if 1

		// circle
		double alpha = M_PI * Q_crandom( seed ); // [-PI ..+PI]
		double s = fabs( Q_crandom( seed ) ); // [0..1]
		r = s * cos( alpha ) * hspread;
		u = s * sin( alpha ) * vspread;
#else

		// square
		r = Q_crandom( seed ) * hspread;
		u = Q_crandom( seed ) * vspread;
#endif
		VectorMA( start, 8192, axis[0], end );
		VectorMA( end, r, axis[1], end );
		VectorMA( end, u, axis[2], end );

		if( G_PointContents4D( start, timeDelta ) & MASK_WATER ) {
			VectorCopy( start, water_start );
			content_mask &= ~MASK_WATER;
		}

		G_Trace4D( &tr, start, NULL, NULL, end, self, content_mask, timeDelta );

		// see if we hit water
		if( tr.contents & MASK_WATER ) {
			VectorCopy( tr.endpos, water_start );

			if( !VectorCompare( start, tr.endpos ) ) {
				vec3_t forward, right, up;

				// change bullet's course when it enters water
				VectorSubtract( end, start, dir );
				VecToAngles( dir, dir );
				AngleVectors( dir, forward, right, up );
#if 1

				// circle
				alpha = M_PI * Q_crandom( seed ); // [-PI ..+PI]
				s = fabs( Q_crandom( seed ) ); // [0..1]
				r = s * cos( alpha ) * hspread * 1.5;
				u = s * sin( alpha ) * vspread * 1.5;
#else
				r = Q_crandom( seed ) * hspread * 2;
				u = Q_crandom( seed ) * vspread * 2;
#endif
				VectorMA( water_start, 8192, forward, end );
				VectorMA( end, r, right, end );
				VectorMA( end, u, up, end );
			}

			// re-trace ignoring water this time
			G_Trace4D( &tr, water_start, NULL, NULL, end, self, MASK_SHOT, timeDelta );
		}
	}

	// send gun puff / flash
	if( tr.fraction < 1.0 && tr.ent != -1 ) {
		if( game.edicts[tr.ent].takedamage ) {
			G_Damage( &game.edicts[tr.ent], self, self, aimdir, aimdir, tr.endpos, tr.plane.normal, damage, knockback, stun, dflags, mod );
		} else {
			if( !( tr.surfFlags & SURF_NOIMPACT ) ) {
			}
		}
	}
}
#endif

#define DIRECTAIRTHIT_DAMAGE_BONUS 0
#define DIRECTHIT_DAMAGE_BONUS 0

enum {
	PROJECTILE_TOUCH_NOT = 0,
	PROJECTILE_TOUCH_DIRECTHIT,
	PROJECTILE_TOUCH_DIRECTAIRHIT,
	PROJECTILE_TOUCH_DIRECTSPLASH // treat direct hits as pseudo-splash impacts
};

/*
*
* - We will consider direct impacts as splash when the player is on the ground and the hit very close to the ground
*/
int G_Projectile_HitStyle( edict_t *projectile, edict_t *target ) {
	trace_t trace;
	vec3_t end;
	bool atGround = false;
	edict_t *attacker;
#define AIRHIT_MINHEIGHT 64

	// don't hurt owner for the first second
	if( target == projectile->r.owner && target != world ) {
		if( !g_projectile_touch_owner->integer ||
			( g_projectile_touch_owner->integer && projectile->timeStamp + 1000 > level.time ) ) {
			return PROJECTILE_TOUCH_NOT;
		}
	}

	if( !target->takedamage || ISBRUSHMODEL( target->s.modelindex ) ) {
		return PROJECTILE_TOUCH_DIRECTHIT;
	}

	if( target->waterlevel > 1 ) {
		return PROJECTILE_TOUCH_DIRECTHIT; // water hits are direct but don't count for awards

	}
	attacker = ( projectile->r.owner && projectile->r.owner->r.client ) ? projectile->r.owner : NULL;

	// see if the target is at ground or a less than a step of height
	if( target->groundentity ) {
		atGround = true;
	} else {
		VectorCopy( target->s.origin, end );
		end[2] -= STEPSIZE;

		G_Trace4D( &trace, target->s.origin, target->r.mins, target->r.maxs, end, target, MASK_DEADSOLID, 0 );
		if( ( trace.ent != -1 || trace.startsolid ) && ISWALKABLEPLANE( &trace.plane ) ) {
			atGround = true;
		}
	}

	if( atGround ) {
		// when the player is at ground we will consider a direct hit only when
		// the hit is 16 units above the feet
		if( projectile->s.origin[2] <= 16 + target->s.origin[2] + target->r.mins[2] ) {
			return PROJECTILE_TOUCH_DIRECTSPLASH;
		}
	} else {
		// it's direct hit, but let's see if it's airhit
		VectorCopy( target->s.origin, end );
		end[2] -= AIRHIT_MINHEIGHT;

		G_Trace4D( &trace, target->s.origin, target->r.mins, target->r.maxs, end, target, MASK_DEADSOLID, 0 );
		if( ( trace.ent != -1 || trace.startsolid ) && ISWALKABLEPLANE( &trace.plane ) ) {
			// add directhit and airhit to awards counter
			if( attacker && !GS_IsTeamDamage( &attacker->s, &target->s ) && G_ModToAmmo( projectile->style ) != AMMO_NONE ) {
				projectile->r.owner->r.client->level.stats.accuracy_hits_direct[G_ModToAmmo( projectile->style ) - AMMO_GUNBLADE]++;
				teamlist[projectile->r.owner->s.team].stats.accuracy_hits_direct[G_ModToAmmo( projectile->style ) - AMMO_GUNBLADE]++;

				projectile->r.owner->r.client->level.stats.accuracy_hits_air[G_ModToAmmo( projectile->style ) - AMMO_GUNBLADE]++;
				teamlist[projectile->r.owner->s.team].stats.accuracy_hits_air[G_ModToAmmo( projectile->style ) - AMMO_GUNBLADE]++;
			}

			return PROJECTILE_TOUCH_DIRECTAIRHIT;
		}
	}

	// add directhit to awards counter
	if( attacker && !GS_IsTeamDamage( &attacker->s, &target->s ) && G_ModToAmmo( projectile->style ) != AMMO_NONE ) {
		projectile->r.owner->r.client->level.stats.accuracy_hits_direct[G_ModToAmmo( projectile->style ) - AMMO_GUNBLADE]++;
		teamlist[projectile->r.owner->s.team].stats.accuracy_hits_direct[G_ModToAmmo( projectile->style ) - AMMO_GUNBLADE]++;
	}

	return PROJECTILE_TOUCH_DIRECTHIT;

#undef AIRHIT_MINHEIGHT
}

/*
* W_Touch_Projectile - Generic projectile touch func. Only for replacement in tests
*/
static void W_Touch_Projectile( edict_t *ent, edict_t *other, cplane_t *plane, int surfFlags ) {
	vec3_t dir, normal;
	int hitType;

	if( surfFlags & SURF_NOIMPACT ) {
		G_FreeEdict( ent );
		return;
	}

	hitType = G_Projectile_HitStyle( ent, other );
	if( hitType == PROJECTILE_TOUCH_NOT ) {
		return;
	}

	if( other->takedamage ) {
		VectorNormalize2( ent->velocity, dir );

		if( hitType == PROJECTILE_TOUCH_DIRECTSPLASH ) { // use hybrid direction from splash and projectile
			G_SplashFrac4D( ENTNUM( other ), ent->s.origin, ent->projectileInfo.radius, dir, NULL, NULL, ent->timeDelta );
		} else {
			VectorNormalize2( ent->velocity, dir );
		}

		G_Damage( other, ent, ent->r.owner, dir, ent->velocity, ent->s.origin, ent->projectileInfo.maxDamage, ent->projectileInfo.maxKnockback, ent->projectileInfo.stun, 0, ent->style );
	}

	G_RadiusDamage( ent, ent->r.owner, plane, other, MOD_EXPLOSIVE );

	if( !plane ) {
		VectorSet( normal, 0, 0, 1 );
	} else {
		VectorCopy( plane->normal, normal );
	}

	G_Gametype_ScoreEvent( NULL, "projectilehit", va( "%i %i %f %f %f", ent->s.number, surfFlags, normal[0], normal[1], normal[2] ) );
}

/*
* W_Fire_LinearProjectile - Spawn a generic linear projectile without a model, touch func, sound nor mod
*/
static edict_t *W_Fire_LinearProjectile( edict_t *self, vec3_t start, vec3_t angles, int speed,
										 float damage, int minKnockback, int maxKnockback, int stun, int minDamage, int radius, int timeout, int timeDelta ) {
	edict_t *projectile;
	vec3_t dir;

	projectile = G_Spawn();
	VectorCopy( start, projectile->s.origin );
	VectorCopy( start, projectile->olds.origin );

	VectorCopy( angles, projectile->s.angles );
	AngleVectors( angles, dir, NULL, NULL );
	VectorScale( dir, speed, projectile->velocity );

	projectile->movetype = MOVETYPE_LINEARPROJECTILE;

	projectile->r.solid = SOLID_YES;
	projectile->r.clipmask = ( !GS_RaceGametype() ) ? MASK_SHOT : MASK_SOLID;

	projectile->r.svflags = SVF_PROJECTILE;

	// enable me when drawing exception is added to cgame
	VectorClear( projectile->r.mins );
	VectorClear( projectile->r.maxs );
	projectile->s.modelindex = 0;
	projectile->r.owner = self;
	projectile->s.ownerNum = ENTNUM( self );
	projectile->touch = W_Touch_Projectile; //generic one. Should be replaced after calling this func
	projectile->nextThink = level.time + timeout;
	projectile->think = G_FreeEdict;
	projectile->classname = NULL; // should be replaced after calling this func.
	projectile->style = 0;
	projectile->s.sound = 0;
	projectile->timeStamp = level.time;
	projectile->timeDelta = timeDelta;

	projectile->projectileInfo.minDamage = min( minDamage, damage );
	projectile->projectileInfo.maxDamage = damage;
	projectile->projectileInfo.minKnockback = min( minKnockback, maxKnockback );
	projectile->projectileInfo.maxKnockback = maxKnockback;
	projectile->projectileInfo.stun = stun;
	projectile->projectileInfo.radius = radius;

	GClip_LinkEntity( projectile );

	// update some data required for the transmission
	projectile->s.linearMovement = true;
	VectorCopy( projectile->s.origin, projectile->s.linearMovementBegin );
	VectorCopy( projectile->velocity, projectile->s.linearMovementVelocity );
	projectile->s.linearMovementTimeStamp = game.serverTime;
	projectile->s.team = self->s.team;
	projectile->s.modelindex2 = ( abs( timeDelta ) > 255 ) ? 255 : (unsigned int)abs( timeDelta );
	return projectile;
}

/*
* W_Fire_TossProjectile - Spawn a generic projectile without a model, touch func, sound nor mod
*/
static edict_t *W_Fire_TossProjectile( edict_t *self, vec3_t start, vec3_t angles, int speed,
									   float damage, int minKnockback, int maxKnockback, int stun, int minDamage, int radius, int timeout, int timeDelta ) {
	edict_t *projectile;
	vec3_t dir;

	projectile = G_Spawn();
	VectorCopy( start, projectile->s.origin );
	VectorCopy( start, projectile->olds.origin );

	VectorCopy( angles, projectile->s.angles );
	AngleVectors( angles, dir, NULL, NULL );
	VectorScale( dir, speed, projectile->velocity );

	projectile->movetype = MOVETYPE_BOUNCEGRENADE;

	// make missile fly through players in race
	if( GS_RaceGametype() ) {
		projectile->r.clipmask = MASK_SOLID;
	} else {
		projectile->r.clipmask = MASK_SHOT;
	}

	projectile->r.solid = SOLID_YES;
	projectile->r.svflags = SVF_PROJECTILE;
	VectorClear( projectile->r.mins );
	VectorClear( projectile->r.maxs );

	//projectile->s.modelindex = trap_ModelIndex ("models/objects/projectile/plasmagun/proj_plasmagun2.md3");
	projectile->s.modelindex = 0;
	projectile->r.owner = self;
	projectile->touch = W_Touch_Projectile; //generic one. Should be replaced after calling this func
	projectile->nextThink = level.time + timeout;
	projectile->think = G_FreeEdict;
	projectile->classname = NULL; // should be replaced after calling this func.
	projectile->style = 0;
	projectile->s.sound = 0;
	projectile->timeStamp = level.time;
	projectile->timeDelta = timeDelta;
	projectile->s.team = self->s.team;

	projectile->projectileInfo.minDamage = min( minDamage, damage );
	projectile->projectileInfo.maxDamage = damage;
	projectile->projectileInfo.minKnockback = min( minKnockback, maxKnockback );
	projectile->projectileInfo.maxKnockback = maxKnockback;
	projectile->projectileInfo.stun = stun;
	projectile->projectileInfo.radius = radius;

	GClip_LinkEntity( projectile );

	return projectile;
}


//	------------ the actual weapons --------------


/*
* W_Fire_Blade
*/
void W_Fire_Blade( edict_t *self, int range, vec3_t start, vec3_t angles, float damage, int knockback, int stun, int mod, int timeDelta ) {
	edict_t *event, *other = NULL;
	vec3_t end;
	trace_t trace;
	int mask = MASK_SHOT;
	vec3_t dir;
	int dmgflags = 0;

	if( GS_Instagib() ) {
		damage = 9999;
	}

	AngleVectors( angles, dir, NULL, NULL );
	VectorMA( start, range, dir, end );

	if( GS_RaceGametype() ) {
		mask = MASK_SOLID;
	}

	G_Trace4D( &trace, start, NULL, NULL, end, self, mask, timeDelta );
	if( trace.ent == -1 ) { //didn't touch anything
		return;
	}

	// find out what touched
	other = &game.edicts[trace.ent];
	if( !other->takedamage ) { // it was the world
		// wall impact
		VectorMA( trace.endpos, -0.02, dir, end );
		event = G_SpawnEvent( EV_BLADE_IMPACT, 0, end );
		event->s.ownerNum = ENTNUM( self );
		VectorScale( trace.plane.normal, 1024, event->s.origin2 );
		return;
	}

	// it was a player
	G_Damage( other, self, self, dir, dir, other->s.origin, damage, knockback, stun, dmgflags, mod );
}

/*
* W_Touch_GunbladeBlast
*/
static void W_Touch_GunbladeBlast( edict_t *ent, edict_t *other, cplane_t *plane, int surfFlags ) {
	vec3_t dir;
	int hitType;

	if( surfFlags & SURF_NOIMPACT ) {
		G_FreeEdict( ent );
		return;
	}

	hitType = G_Projectile_HitStyle( ent, other );
	if( hitType == PROJECTILE_TOUCH_NOT ) {
		return;
	}

	if( other->takedamage ) {
		VectorNormalize2( ent->velocity, dir );

		if( hitType == PROJECTILE_TOUCH_DIRECTSPLASH ) { // use hybrid direction from splash and projectile
			G_SplashFrac4D( ENTNUM( other ), ent->s.origin, ent->projectileInfo.radius, dir, NULL, NULL, ent->timeDelta );
		} else {
			VectorNormalize2( ent->velocity, dir );
		}

		G_Damage( other, ent, ent->r.owner, dir, ent->velocity, ent->s.origin, ent->projectileInfo.maxDamage, ent->projectileInfo.maxKnockback, ent->projectileInfo.stun, 0, ent->style );
	}

	G_RadiusDamage( ent, ent->r.owner, plane, other, MOD_GUNBLADE_S );

	// add explosion event
	if( ( !other->takedamage || ISBRUSHMODEL( other->s.modelindex ) ) ) {
		edict_t *event;

		event = G_SpawnEvent( EV_GUNBLADEBLAST_IMPACT, DirToByte( plane ? plane->normal : NULL ), ent->s.origin );
		event->s.weapon = ( ( ent->projectileInfo.radius * 1 / 8 ) > 127 ) ? 127 : ( ent->projectileInfo.radius * 1 / 8 );
		event->s.skinnum = ( ( ent->projectileInfo.maxKnockback * 1 / 8 ) > 255 ) ? 255 : ( ent->projectileInfo.maxKnockback * 1 / 8 );
	}

	// free at next frame
	G_FreeEdict( ent );
}

/*
* W_Fire_GunbladeBlast
*/
edict_t *W_Fire_GunbladeBlast( edict_t *self, vec3_t start, vec3_t angles, float damage, int minKnockback, int maxKnockback, int stun, int minDamage, int radius, int speed, int timeout, int mod, int timeDelta ) {
	edict_t *blast;

	if( GS_Instagib() ) {
		damage = 9999;
	}

	blast = W_Fire_LinearProjectile( self, start, angles, speed, damage, minKnockback, maxKnockback, stun, minDamage, radius, timeout, timeDelta );
	blast->s.modelindex = trap_ModelIndex( PATH_GUNBLADEBLAST_STRONG_MODEL );
	blast->s.type = ET_BLASTER;
	blast->s.effects |= EF_STRONG_WEAPON;
	blast->touch = W_Touch_GunbladeBlast;
	blast->classname = "gunblade_blast";
	blast->style = mod;

	blast->s.sound = trap_SoundIndex( S_WEAPON_PLASMAGUN_S_FLY );
	blast->s.attenuation = ATTN_STATIC;

	return blast;
}

/*
* W_Fire_Bullet
*/
void W_Fire_Bullet( edict_t *self, vec3_t start, vec3_t angles, int seed, int range, int hspread, int vspread,
					float damage, int knockback, int stun, int mod, int timeDelta ) {
	vec3_t dir;
	edict_t *event;
	float r, u;
	double alpha, s;
	trace_t trace;
	int dmgflags = DAMAGE_STUN_CLAMP | DAMAGE_KNOCKBACK_SOFT;

	if( GS_Instagib() ) {
		damage = 9999;
	}

	AngleVectors( angles, dir, NULL, NULL );

	// send the event
	event = G_SpawnEvent( EV_FIRE_BULLET, seed, start );
	event->s.ownerNum = ENTNUM( self );
	VectorScale( dir, 4096, event->s.origin2 ); // DirToByte is too inaccurate
	event->s.weapon = WEAP_MACHINEGUN;
	event->s.firemode = ( mod == MOD_MACHINEGUN_S ) ? FIRE_MODE_STRONG : FIRE_MODE_WEAK;

	// circle shape
	alpha = M_PI * Q_crandom( &seed ); // [-PI ..+PI]
	s = fabs( Q_crandom( &seed ) ); // [0..1]
	r = s * cos( alpha ) * hspread;
	u = s * sin( alpha ) * vspread;

	GS_TraceBullet( &trace, start, dir, r, u, range, ENTNUM( self ), timeDelta );
	if( trace.ent != -1 ) {
		if( game.edicts[trace.ent].takedamage ) {
			G_Damage( &game.edicts[trace.ent], self, self, dir, dir, trace.endpos, damage, knockback, stun, dmgflags, mod );
		} else {
			if( !( trace.surfFlags & SURF_NOIMPACT ) ) {
			}
		}
	}
}

//Sunflower spiral with Fibonacci numbers
static void G_Fire_SunflowerPattern( edict_t *self, vec3_t start, vec3_t dir, int *seed, int count,
									 int hspread, int vspread, int range, float damage, int kick, int stun, int dflags, int mod, int timeDelta ) {
	int i;
	float r;
	float u;
	float fi;
	trace_t trace;

	for( i = 0; i < count; i++ ) {
		fi = i * 2.4; //magic value creating Fibonacci numbers
		r = cos( (float)*seed + fi ) * hspread * sqrt( fi );
		u = sin( (float)*seed + fi ) * vspread * sqrt( fi );

		GS_TraceBullet( &trace, start, dir, r, u, range, ENTNUM( self ), timeDelta );
		if( trace.ent != -1 ) {
			if( game.edicts[trace.ent].takedamage ) {
				G_Damage( &game.edicts[trace.ent], self, self, dir, dir, trace.endpos, damage, kick, stun, dflags, mod );
			} else {
				if( !( trace.surfFlags & SURF_NOIMPACT ) ) {
				}
			}
		}
	}
}

#if 0
static void G_Fire_RandomPattern( edict_t *self, vec3_t start, vec3_t dir, int *seed, int count,
								  int hspread, int vspread, int range, float damage, int kick, int stun, int dflags, int mod, int timeDelta ) {
	int i;
	float r;
	float u;
	trace_t trace;

	for( i = 0; i < count; i++ ) {
		r = Q_crandom( seed ) * hspread;
		u = Q_crandom( seed ) * vspread;

		GS_TraceBullet( &trace, start, dir, r, u, range, ENTNUM( self ), timeDelta );
		if( trace.ent != -1 ) {
			if( game.edicts[trace.ent].takedamage ) {
				G_Damage( &game.edicts[trace.ent], self, self, dir, dir, trace.endpos, damage, kick, stun, dflags, mod );
			} else {
				if( !( trace.surfFlags & SURF_NOIMPACT ) ) {
				}
			}
		}
	}
}
#endif

void W_Fire_Riotgun( edict_t *self, vec3_t start, vec3_t angles, int seed, int range, int hspread, int vspread,
					 int count, float damage, int knockback, int stun, int mod, int timeDelta ) {
	vec3_t dir;
	edict_t *event;
	int dmgflags = 0;

	if( GS_Instagib() ) {
		damage = 9999;
	}

	AngleVectors( angles, dir, NULL, NULL );

	// send the event
	event = G_SpawnEvent( EV_FIRE_RIOTGUN, seed, start );
	event->s.ownerNum = ENTNUM( self );
	VectorScale( dir, 4096, event->s.origin2 ); // DirToByte is too inaccurate
	event->s.weapon = WEAP_RIOTGUN;
	event->s.firemode = ( mod == MOD_RIOTGUN_S ) ? FIRE_MODE_STRONG : FIRE_MODE_WEAK;

	G_Fire_SunflowerPattern( self, start, dir, &seed, count, hspread, vspread,
							 range, damage, knockback, stun, dmgflags, mod, timeDelta );
}

/*
* W_Grenade_ExplodeDir
*/
static void W_Grenade_ExplodeDir( edict_t *ent, vec3_t normal ) {
	vec3_t origin;
	int radius;
	edict_t *event;
	vec3_t up = { 0, 0, 1 };
	vec_t *dir = normal ? normal : up;

	G_RadiusDamage( ent,
					ent->r.owner,
					NULL,
					ent->enemy,
					( ent->s.effects & EF_STRONG_WEAPON ) ? MOD_GRENADE_SPLASH_S : MOD_GRENADE_SPLASH_W
					);

	radius = ( ( ent->projectileInfo.radius * 1 / 8 ) > 127 ) ? 127 : ( ent->projectileInfo.radius * 1 / 8 );
	VectorMA( ent->s.origin, -0.02, ent->velocity, origin );
	event = G_SpawnEvent( EV_GRENADE_EXPLOSION, ( dir ? DirToByte( dir ) : 0 ), ent->s.origin );
	event->s.firemode = ( ent->s.effects & EF_STRONG_WEAPON ) ? FIRE_MODE_STRONG : FIRE_MODE_WEAK;
	event->s.weapon = radius;

	G_FreeEdict( ent );
}

/*
* W_Grenade_Explode
*/
static void W_Grenade_Explode( edict_t *ent ) {
	W_Grenade_ExplodeDir( ent, NULL );
}

/*
* W_Touch_Grenade
*/
static void W_Touch_Grenade( edict_t *ent, edict_t *other, cplane_t *plane, int surfFlags ) {
	int hitType;
	vec3_t dir;

	if( surfFlags & SURF_NOIMPACT ) {
		G_FreeEdict( ent );
		return;
	}

	hitType = G_Projectile_HitStyle( ent, other );
	if( hitType == PROJECTILE_TOUCH_NOT ) {
		return;
	}

	// don't explode on doors and plats that take damage
	if( !other->takedamage || ISBRUSHMODEL( other->s.modelindex ) ) {
		G_AddEvent( ent, EV_GRENADE_BOUNCE, ( ent->s.effects & EF_STRONG_WEAPON ) ? FIRE_MODE_STRONG : FIRE_MODE_WEAK, true );
		return;
	}

	if( other->takedamage ) {
		int directHitDamage = ent->projectileInfo.maxDamage;

		VectorNormalize2( ent->velocity, dir );

		if( hitType == PROJECTILE_TOUCH_DIRECTSPLASH ) { // use hybrid direction from splash and projectile
			G_SplashFrac4D( ENTNUM( other ), ent->s.origin, ent->projectileInfo.radius, dir, NULL, NULL, ent->timeDelta );
		} else {
			VectorNormalize2( ent->velocity, dir );

			// no direct hit bonuses for grenades
			/*
			if( hitType == PROJECTILE_TOUCH_DIRECTAIRHIT )
			    directHitDamage += DIRECTAIRTHIT_DAMAGE_BONUS;
			else if( hitType == PROJECTILE_TOUCH_DIRECTHIT )
			    directHitDamage += DIRECTHIT_DAMAGE_BONUS;
			*/
		}

		G_Damage( other, ent, ent->r.owner, dir, ent->velocity, ent->s.origin, directHitDamage, ent->projectileInfo.maxKnockback, ent->projectileInfo.stun, 0, ent->style );
	}

	ent->enemy = other;
	W_Grenade_ExplodeDir( ent, plane ? plane->normal : NULL );
}

/*
* W_Fire_Grenade
*/
edict_t *W_Fire_Grenade( edict_t *self, vec3_t start, vec3_t angles, int speed, float damage,
						 int minKnockback, int maxKnockback, int stun, int minDamage, float radius,
						 int timeout, int mod, int timeDelta, bool aim_up ) {
	edict_t *grenade;

	if( GS_Instagib() ) {
		damage = 9999;
	}

	if( aim_up ) {
		angles[PITCH] -= 5.0f * cos( DEG2RAD( angles[PITCH] ) ); // aim some degrees upwards from view dir

	}
	grenade = W_Fire_TossProjectile( self, start, angles, speed, damage, minKnockback, maxKnockback, stun, minDamage, radius, timeout, timeDelta );
	VectorClear( grenade->s.angles );
	grenade->style = mod;
	grenade->s.type = ET_GRENADE;
	grenade->movetype = MOVETYPE_BOUNCEGRENADE;
	grenade->touch = W_Touch_Grenade;
	grenade->use = NULL;
	grenade->think = W_Grenade_Explode;
	grenade->classname = "grenade";
	grenade->enemy = NULL;

	if( mod == MOD_GRENADE_S ) {
		grenade->s.modelindex = trap_ModelIndex( PATH_GRENADE_STRONG_MODEL );
		grenade->s.effects |= EF_STRONG_WEAPON;
	} else {
		grenade->s.modelindex = trap_ModelIndex( PATH_GRENADE_WEAK_MODEL );
		grenade->s.effects &= ~EF_STRONG_WEAPON;
	}

	GClip_LinkEntity( grenade );

	return grenade;
}

/*
* W_Touch_Rocket
*/
static void W_Touch_Rocket( edict_t *ent, edict_t *other, cplane_t *plane, int surfFlags ) {
	int mod_splash;
	vec3_t dir;
	int hitType;

	if( surfFlags & SURF_NOIMPACT ) {
		G_FreeEdict( ent );
		return;
	}

	hitType = G_Projectile_HitStyle( ent, other );
	if( hitType == PROJECTILE_TOUCH_NOT ) {
		return;
	}

	if( other->takedamage ) {
		int directHitDamage = ent->projectileInfo.maxDamage;

		VectorNormalize2( ent->velocity, dir );

		if( hitType == PROJECTILE_TOUCH_DIRECTSPLASH ) { // use hybrid direction from splash and projectile

			G_SplashFrac4D( ENTNUM( other ), ent->s.origin, ent->projectileInfo.radius, dir, NULL, NULL, ent->timeDelta );
		} else {
			VectorNormalize2( ent->velocity, dir );

			if( hitType == PROJECTILE_TOUCH_DIRECTAIRHIT ) {
				directHitDamage += DIRECTAIRTHIT_DAMAGE_BONUS;
			} else if( hitType == PROJECTILE_TOUCH_DIRECTHIT ) {
				directHitDamage += DIRECTHIT_DAMAGE_BONUS;
			}
		}

		G_Damage( other, ent, ent->r.owner, dir, ent->velocity, ent->s.origin, directHitDamage, ent->projectileInfo.maxKnockback, ent->projectileInfo.stun, 0, ent->style );
	}

	if( ent->s.effects & EF_STRONG_WEAPON ) {
		mod_splash = MOD_ROCKET_SPLASH_S;
	} else {
		mod_splash = MOD_ROCKET_SPLASH_W;
	}

	G_RadiusDamage( ent, ent->r.owner, plane, other, mod_splash );

	// spawn the explosion
	if( !( surfFlags & SURF_NOIMPACT ) ) {
		edict_t *event;
		vec3_t explosion_origin;

		VectorMA( ent->s.origin, -0.02, ent->velocity, explosion_origin );
		event = G_SpawnEvent( EV_ROCKET_EXPLOSION, DirToByte( plane ? plane->normal : NULL ), explosion_origin );
		event->s.firemode = ( ent->s.effects & EF_STRONG_WEAPON ) ? FIRE_MODE_STRONG : FIRE_MODE_WEAK;
		event->s.weapon = ( ( ent->projectileInfo.radius * 1 / 8 ) > 255 ) ? 255 : ( ent->projectileInfo.radius * 1 / 8 );
	}

	// free the rocket at next frame
	G_FreeEdict( ent );
}

/*
* W_Fire_Rocket
*/
edict_t *W_Fire_Rocket( edict_t *self, vec3_t start, vec3_t angles, int speed, float damage, int minKnockback, int maxKnockback, int stun, int minDamage, int radius, int timeout, int mod, int timeDelta ) {
	edict_t *rocket;

	if( GS_Instagib() ) {
		damage = 9999;
	}

	rocket = W_Fire_LinearProjectile( self, start, angles, speed, damage, minKnockback, maxKnockback, stun, minDamage, radius, timeout, timeDelta );

	rocket->s.type = ET_ROCKET; //rocket trail sfx
	if( mod == MOD_ROCKET_S ) {
		rocket->s.modelindex = trap_ModelIndex( PATH_ROCKET_STRONG_MODEL );
		rocket->s.effects |= EF_STRONG_WEAPON;
		rocket->s.sound = trap_SoundIndex( S_WEAPON_ROCKET_S_FLY );
	} else {
		rocket->s.modelindex = trap_ModelIndex( PATH_ROCKET_WEAK_MODEL );
		rocket->s.effects &= ~EF_STRONG_WEAPON;
		rocket->s.sound = trap_SoundIndex( S_WEAPON_ROCKET_W_FLY );
	}
	rocket->s.attenuation = ATTN_STATIC;
	rocket->touch = W_Touch_Rocket;
	rocket->think = G_FreeEdict;
	rocket->classname = "rocket";
	rocket->style = mod;

	return rocket;
}

static void W_Plasma_Explosion( edict_t *ent, edict_t *ignore, cplane_t *plane, int surfFlags ) {
	edict_t *event;
	int radius = ( ( ent->projectileInfo.radius * 1 / 8 ) > 127 ) ? 127 : ( ent->projectileInfo.radius * 1 / 8 );

	event = G_SpawnEvent( EV_PLASMA_EXPLOSION, DirToByte( plane ? plane->normal : NULL ), ent->s.origin );
	event->s.firemode = ( ent->s.effects & EF_STRONG_WEAPON ) ? FIRE_MODE_STRONG : FIRE_MODE_WEAK;
	event->s.weapon = radius & 127;

	G_RadiusDamage( ent, ent->r.owner, plane, ignore, ent->style );

	G_FreeEdict( ent );
}

/*
* W_Touch_Plasma
*/
static void W_Touch_Plasma( edict_t *ent, edict_t *other, cplane_t *plane, int surfFlags ) {
	int hitType;
	vec3_t dir;

	if( surfFlags & SURF_NOIMPACT ) {
		G_FreeEdict( ent );
		return;
	}

	hitType = G_Projectile_HitStyle( ent, other );
	if( hitType == PROJECTILE_TOUCH_NOT ) {
		return;
	}

	if( other->takedamage ) {
		VectorNormalize2( ent->velocity, dir );

		if( hitType == PROJECTILE_TOUCH_DIRECTSPLASH ) { // use hybrid direction from splash and projectile
			G_SplashFrac4D( ENTNUM( other ), ent->s.origin, ent->projectileInfo.radius, dir, NULL, NULL, ent->timeDelta );
		} else {
			VectorNormalize2( ent->velocity, dir );
		}

		G_Damage( other, ent, ent->r.owner, dir, ent->velocity, ent->s.origin, ent->projectileInfo.maxDamage, ent->projectileInfo.maxKnockback, ent->projectileInfo.stun, DAMAGE_KNOCKBACK_SOFT, ent->style );
	}

	W_Plasma_Explosion( ent, other, plane, surfFlags );
}

/*
* W_Plasma_Backtrace
*/
void W_Plasma_Backtrace( edict_t *ent, const vec3_t start ) {
	trace_t tr;
	vec3_t oldorigin;
	vec3_t mins = { -2, -2, -2 }, maxs = { 2, 2, 2 };

	if( GS_RaceGametype() ) {
		return;
	}

	VectorCopy( ent->s.origin, oldorigin );
	VectorCopy( start, ent->s.origin );

	do {
		G_Trace4D( &tr, ent->s.origin, mins, maxs, oldorigin, ent, ( CONTENTS_BODY | CONTENTS_CORPSE ), ent->timeDelta );

		VectorCopy( tr.endpos, ent->s.origin );

		if( tr.ent == -1 ) {
			break;
		}
		if( tr.allsolid || tr.startsolid ) {
			W_Touch_Plasma( ent, &game.edicts[tr.ent], NULL, 0 );
		} else if( tr.fraction != 1.0 ) {
			W_Touch_Plasma( ent, &game.edicts[tr.ent], &tr.plane, tr.surfFlags );
		} else {
			break;
		}
	} while( ent->r.inuse && ent->s.type == ET_PLASMA && !VectorCompare( ent->s.origin, oldorigin ) );

	if( ent->r.inuse && ent->s.type == ET_PLASMA ) {
		VectorCopy( oldorigin, ent->s.origin );
	}
}

/*
* W_Think_Plasma
*/
static void W_Think_Plasma( edict_t *ent ) {
	vec3_t start;

	if( ent->timeout < level.time ) {
		G_FreeEdict( ent );
		return;
	}

	if( ent->r.inuse ) {
		ent->nextThink = level.time + 1;
	}

	VectorMA( ent->s.origin, -( game.frametime * 0.001 ), ent->velocity, start );

	W_Plasma_Backtrace( ent, start );
}

/*
* W_AutoTouch_Plasma
*/
static void W_AutoTouch_Plasma( edict_t *ent, edict_t *other, cplane_t *plane, int surfFlags ) {
	W_Think_Plasma( ent );
	if( !ent->r.inuse || ent->s.type != ET_PLASMA ) {
		return;
	}

	W_Touch_Plasma( ent, other, plane, surfFlags );
}

/*
* W_Fire_Plasma
*/
edict_t *W_Fire_Plasma( edict_t *self, vec3_t start, vec3_t angles, float damage, int minKnockback, int maxKnockback, int stun, int minDamage, int radius, int speed, int timeout, int mod, int timeDelta ) {
	edict_t *plasma;

	if( GS_Instagib() ) {
		damage = 9999;
	}

	plasma = W_Fire_LinearProjectile( self, start, angles, speed, damage, minKnockback, maxKnockback, stun, minDamage, radius, timeout, timeDelta );
	plasma->s.type = ET_PLASMA;
	plasma->classname = "plasma";
	plasma->style = mod;

	plasma->think = W_Think_Plasma;
	plasma->touch = W_AutoTouch_Plasma;
	plasma->nextThink = level.time + 1;
	plasma->timeout = level.time + timeout;

	if( mod == MOD_PLASMA_S ) {
		plasma->s.modelindex = trap_ModelIndex( PATH_PLASMA_STRONG_MODEL );
		plasma->s.sound = trap_SoundIndex( S_WEAPON_PLASMAGUN_S_FLY );
		plasma->s.effects |= EF_STRONG_WEAPON;
	} else {
		plasma->s.modelindex = trap_ModelIndex( PATH_PLASMA_WEAK_MODEL );
		plasma->s.sound = trap_SoundIndex( S_WEAPON_PLASMAGUN_W_FLY );
		plasma->s.effects &= ~EF_STRONG_WEAPON;
	}
	plasma->s.attenuation = ATTN_STATIC;
	return plasma;
}

/*
* W_Touch_Bolt
*/
static void W_Touch_Bolt( edict_t *self, edict_t *other, cplane_t *plane, int surfFlags ) {
	edict_t *event;
	bool missed = true;
	int hitType;

	if( surfFlags & SURF_NOIMPACT ) {
		G_FreeEdict( self );
		return;
	}

	if( other == self->enemy ) {
		return;
	}

	hitType = G_Projectile_HitStyle( self, other );
	if( hitType == PROJECTILE_TOUCH_NOT ) {
		return;
	}

	if( other->takedamage ) {
		vec3_t invdir;
		G_Damage( other, self, self->r.owner, self->velocity, self->velocity, self->s.origin, self->projectileInfo.maxDamage, self->projectileInfo.maxKnockback, self->projectileInfo.stun, 0, MOD_ELECTROBOLT_W );
		VectorNormalize2( self->velocity, invdir );
		VectorScale( invdir, -1, invdir );
		event = G_SpawnEvent( EV_BOLT_EXPLOSION, DirToByte( invdir ), self->s.origin );
		event->s.firemode = FIRE_MODE_WEAK;
		if( other->r.client ) {
			missed = false;
		}
	} else if( !( surfFlags & SURF_NOIMPACT ) ) {
		// add explosion event
		event = G_SpawnEvent( EV_BOLT_EXPLOSION, DirToByte( plane ? plane->normal : NULL ), self->s.origin );
		event->s.firemode = FIRE_MODE_WEAK;
	}

	if( missed && self->r.client ) {
		G_AwardPlayerMissedElectrobolt( self->r.owner, MOD_ELECTROBOLT_W ); // hit something that isnt a player

	}
	G_FreeEdict( self );
}

/*
* W_Fire_Electrobolt_Combined
*/
void W_Fire_Electrobolt_Combined( edict_t *self, vec3_t start, vec3_t angles, float maxdamage, float mindamage, float maxknockback, float minknockback, int stun, int range, int mod, int timeDelta ) {
	vec3_t from, end, dir;
	trace_t tr;
	edict_t *ignore, *event, *hit, *damaged;
	int hit_movetype;
	int mask;
	bool missed = true;
	int dmgflags = 0;
	int fireMode;

#ifdef ELECTROBOLT_TEST
	fireMode = FIRE_MODE_WEAK;
#else
	fireMode = FIRE_MODE_STRONG;
#endif

	if( GS_Instagib() ) {
		maxdamage = mindamage = 9999;
	}

	AngleVectors( angles, dir, NULL, NULL );
	VectorMA( start, range, dir, end );
	VectorCopy( start, from );

	ignore = self;
	hit = damaged = NULL;

	mask = MASK_SHOT;
	if( GS_RaceGametype() ) {
		mask = MASK_SOLID;
	}

	clamp_high( mindamage, maxdamage );
	clamp_high( minknockback, maxknockback );

	tr.ent = -1;
	while( ignore ) {
		G_Trace4D( &tr, from, NULL, NULL, end, ignore, mask, timeDelta );

		VectorCopy( tr.endpos, from );
		ignore = NULL;

		if( tr.ent == -1 ) {
			break;
		}

		// some entity was touched
		hit = &game.edicts[tr.ent];
		hit_movetype = hit->movetype; // backup the original movetype as the entity may "die"
		if( hit == world ) { // stop dead if hit the world
			break;
		}

		// allow trail to go through BBOX entities (players, gibs, etc)
		if( !ISBRUSHMODEL( hit->s.modelindex ) ) {
			ignore = hit;
		}

		if( ( hit != self ) && ( hit->takedamage ) ) {
			float frac, damage, knockback;

			frac = DistanceFast( tr.endpos, start ) / (float)range;
			clamp( frac, 0.0f, 1.0f );

			damage = maxdamage - ( ( maxdamage - mindamage ) * frac );
			knockback = maxknockback - ( ( maxknockback - minknockback ) * frac );

			G_Damage( hit, self, self, dir, dir, tr.endpos, damage, knockback, stun, dmgflags, mod );

			// spawn a impact event on each damaged ent
			event = G_SpawnEvent( EV_BOLT_EXPLOSION, DirToByte( tr.plane.normal ), tr.endpos );
			event->s.firemode = fireMode;
			if( hit->r.client ) {
				missed = false;
			}

			damaged = hit;
		}

		if( hit_movetype == MOVETYPE_NONE || hit_movetype == MOVETYPE_PUSH ) {
			damaged = NULL;
			break;
		}
	}

	if( missed && self->r.client ) {
		G_AwardPlayerMissedElectrobolt( self, mod );
	}

	// send the weapon fire effect
	event = G_SpawnEvent( EV_ELECTROTRAIL, ENTNUM( self ), start );
	VectorScale( dir, 1024, event->s.origin2 );
	event->s.firemode = fireMode;

	if( !GS_Instagib() && tr.ent == -1 ) {  // didn't touch anything, not even a wall
		edict_t *bolt;
		gs_weapon_definition_t *weapondef = GS_GetWeaponDef( self->s.weapon );

		// fire a weak EB from the end position
		bolt = W_Fire_Electrobolt_Weak( self, end, angles, weapondef->firedef_weak.speed, mindamage, minknockback, minknockback, stun, weapondef->firedef_weak.timeout, mod, timeDelta );
		bolt->enemy = damaged;
	}
}

void W_Fire_Electrobolt_FullInstant( edict_t *self, vec3_t start, vec3_t angles, float maxdamage, float mindamage, int maxknockback, int minknockback, int stun, int range, int minDamageRange, int mod, int timeDelta ) {
	vec3_t from, end, dir;
	trace_t tr;
	edict_t *ignore, *event, *hit;
	int hit_movetype;
	int mask;
	bool missed = true;
	int dmgflags = 0;

#define FULL_DAMAGE_RANGE g_projectile_prestep->value

	if( GS_Instagib() ) {
		maxdamage = mindamage = 9999;
	}

	AngleVectors( angles, dir, NULL, NULL );
	VectorMA( start, range, dir, end );
	VectorCopy( start, from );

	ignore = self;
	hit = NULL;

	mask = MASK_SHOT;
	if( GS_RaceGametype() ) {
		mask = MASK_SOLID;
	}

	clamp_high( mindamage, maxdamage );
	clamp_high( minknockback, maxknockback );
	clamp_high( minDamageRange, range );

	if( minDamageRange <= FULL_DAMAGE_RANGE ) {
		minDamageRange = FULL_DAMAGE_RANGE + 1;
	}

	if( range <= FULL_DAMAGE_RANGE + 1 ) {
		range = FULL_DAMAGE_RANGE + 1;
	}

	tr.ent = -1;
	while( ignore ) {
		G_Trace4D( &tr, from, NULL, NULL, end, ignore, mask, timeDelta );

		VectorCopy( tr.endpos, from );
		ignore = NULL;

		if( tr.ent == -1 ) {
			break;
		}

		// some entity was touched
		hit = &game.edicts[tr.ent];
		hit_movetype = hit->movetype; // backup the original movetype as the entity may "die"
		if( hit == world ) { // stop dead if hit the world
			break;
		}

		// allow trail to go through BBOX entities (players, gibs, etc)
		if( !ISBRUSHMODEL( hit->s.modelindex ) ) {
			ignore = hit;
		}

		if( ( hit != self ) && ( hit->takedamage ) ) {
			float frac, damage, knockback, dist;

			dist = DistanceFast( tr.endpos, start );
			if( dist <= FULL_DAMAGE_RANGE ) {
				frac = 0.0f;
			} else {
				frac = ( dist - FULL_DAMAGE_RANGE ) / (float)( minDamageRange - FULL_DAMAGE_RANGE );
				clamp( frac, 0.0f, 1.0f );
			}

			damage = maxdamage - ( ( maxdamage - mindamage ) * frac );
			knockback = maxknockback - ( ( maxknockback - minknockback ) * frac );

			G_Damage( hit, self, self, dir, dir, tr.endpos, damage, knockback, stun, dmgflags, mod );

			// spawn a impact event on each damaged ent
			event = G_SpawnEvent( EV_BOLT_EXPLOSION, DirToByte( tr.plane.normal ), tr.endpos );
			event->s.firemode = FIRE_MODE_STRONG;
			if( hit->r.client ) {
				missed = false;
			}
		}

		if( hit_movetype == MOVETYPE_NONE || hit_movetype == MOVETYPE_PUSH ) {
			break;
		}
	}

	if( missed && self->r.client ) {
		G_AwardPlayerMissedElectrobolt( self, mod );
	}

	// send the weapon fire effect
	event = G_SpawnEvent( EV_ELECTROTRAIL, ENTNUM( self ), start );
	VectorScale( dir, 1024, event->s.origin2 );
	event->s.firemode = FIRE_MODE_STRONG;

#undef FULL_DAMAGE_RANGE
}

/*
* W_Fire_Electrobolt_Weak
*/
edict_t *W_Fire_Electrobolt_Weak( edict_t *self, vec3_t start, vec3_t angles, float speed, float damage, int minKnockback, int maxKnockback, int stun, int timeout, int mod, int timeDelta ) {
	edict_t *bolt;

	if( GS_Instagib() ) {
		damage = 9999;
	}

	// projectile, weak mode
	bolt = W_Fire_LinearProjectile( self, start, angles, speed, damage, minKnockback, maxKnockback, stun, 0, 0, timeout, timeDelta );
	bolt->s.modelindex = trap_ModelIndex( PATH_ELECTROBOLT_WEAK_MODEL );
	bolt->s.type = ET_ELECTRO_WEAK; //add particle trail and light
	bolt->s.ownerNum = ENTNUM( self );
	bolt->touch = W_Touch_Bolt;
	bolt->classname = "bolt";
	bolt->style = mod;
	bolt->s.effects &= ~EF_STRONG_WEAPON;

	return bolt;
}

static void W_Think_Wave( edict_t *ent ) {
	if( ent->timeout < level.time ) {
		G_FreeEdict( ent );
		return;
	}

	if( ent->r.inuse ) {
		ent->nextThink = level.time + 1;
	}

	// Check whether the corona damage has not been activated yet
	const firedef_t *firedef;
	if( ent->style == MOD_SHOCKWAVE_S ) {
		firedef = &GS_GetWeaponDef( WEAP_SHOCKWAVE )->firedef;
	} else {
		firedef = &GS_GetWeaponDef( WEAP_SHOCKWAVE )->firedef_weak;
	}

	if( level.time - ent->timeStamp < ( ( 1000 * firedef->splash_radius ) / firedef->speed ) + 64 ) {
		return;
	}

	// TODO: We're falling into "continuous collision detection" problem here.
	// We're not sure what is the proper way to resolve ones.
    // If we check entities in a transformed box covered by the wave, how do we check visibility?
	// Neither antilag nor projectile offset are applied for same reasons.

	int entNums[64];
	int numEnts = GClip_FindInRadius( ent->s.origin, ent->projectileInfo.radius, entNums, 64 );
	clamp_high( numEnts, 64 );

	// Let us assume if a target stands still and the ingoing wave projectile flies
	// has flown away almost hitting it, the target should get the same damage as a direct hit.
	// Thus the projectile affects the target for Ta = ( 2 * radius ) / speed
	// Consequently DPS = ( direct hit damage ) / Ta = ( direct hit damage ) * speed / ( 2 * radius )
	// All time units are assumed to be in seconds

	float dps = ent->projectileInfo.maxDamage * VectorLengthFast( ent->velocity ) / ( 2.0f * ent->projectileInfo.radius );
	float frameTimeSeconds = 0.001f * game.frametime;

	const float stun = g_shockwave_fly_stun_scale->value * ent->projectileInfo.stun * frameTimeSeconds;
	// We have discovered that theoretical best dps is not substantial enough for a player to fear it
	// since players are not standing still waiting to absorb all potential damage.
	// Thus we add an extra scale to the damage
	const float damage = g_shockwave_fly_damage_scale->value * dps * frameTimeSeconds;
	// Same applies to knockback. Moreover we want to keep explosion knockback on GB level
	// and highly increase the knockback for area damage to be able to push enemy back.
	const float knockback = g_shockwave_fly_knockback_scale->value * ent->projectileInfo.maxKnockback * frameTimeSeconds;

	trace_t trace;
	edict_t *attacker = game.edicts + ent->s.ownerNum;

	// For each entity in radius
	for( int i = 0; i < numEnts; ++i ) {
		edict_t *other = game.edicts + entNums[i];
		// If the entity is not damageable
		if( !other->takedamage ) {
			continue;
		}

		float *origin = ent->s.origin;
		edict_t *ignore = ent;
		// Try tracing from wave origin to other entity origin.
		for(;; ) {
			G_Trace( &trace, origin, NULL, NULL, other->s.origin, ignore, MASK_PLAYERSOLID );
			// Stop if there is no hit occured
			if( trace.fraction == 1.0f ) {
				break;
			}
			// Stop if a solid world is hit
			if( trace.ent <= 0 ) {
				break;
			}

			if( game.edicts + trace.ent == other ) {
				vec3_t dir;
				VectorSubtract( other->s.origin, ent->s.origin, dir );
				int mod = ( ent->style == MOD_SHOCKWAVE_S ) ? MOD_SHOCKWAVE_CORONA_S : MOD_SHOCKWAVE_CORONA_W;
				G_Damage( other, ent, attacker, dir, dir, vec3_origin, damage, knockback, stun, DAMAGE_RADIUS, mod );
				break;
			}

			// Stop if a solid and not-damageable entity is hit
			if( !game.edicts[trace.ent].takedamage ) {
				break;
			}

			// Continue tracing from the orgin of an entity the trace has hit ignoring it
			ignore = &game.edicts[trace.ent];
			origin = game.edicts[trace.ent].s.origin;
		}
	}
}

static void W_Touch_Wave( edict_t *ent, edict_t *other, cplane_t *plane, int surfFlags ) {
	vec3_t dir;
	int hitType;

	if( surfFlags & SURF_NOIMPACT ) {
		G_FreeEdict( ent );
		return;
	}

	hitType = G_Projectile_HitStyle( ent, other );
	if( hitType == PROJECTILE_TOUCH_NOT ) {
		return;
	}

	if( other->takedamage ) {
		VectorNormalize2( ent->velocity, dir );

		if( hitType == PROJECTILE_TOUCH_DIRECTSPLASH ) { // use hybrid direction from splash and projectile
			G_SplashFrac4D( ENTNUM( other ), ent->s.origin, ent->projectileInfo.radius, dir, NULL, NULL, ent->timeDelta );
		} else {
			VectorNormalize2( ent->velocity, dir );
		}

		G_Damage( other, ent, ent->r.owner, dir, ent->velocity, ent->s.origin, ent->projectileInfo.maxDamage, ent->projectileInfo.maxKnockback, ent->projectileInfo.stun, 0, ent->style );
	}

	G_RadiusDamage( ent, ent->r.owner, plane, other, ( ent->style == MOD_SHOCKWAVE_S ) ? MOD_SHOCKWAVE_SPLASH_S : MOD_SHOCKWAVE_SPLASH_W );

	if( !( surfFlags & SURF_NOIMPACT ) ) {
		edict_t *event;
		vec3_t explosion_origin;

		VectorMA( ent->s.origin, -0.02, ent->velocity, explosion_origin );
		event = G_SpawnEvent( EV_WAVE_EXPLOSION, DirToByte( plane ? plane->normal : NULL ), explosion_origin );
		event->s.firemode = ( ent->s.effects & EF_STRONG_WEAPON ) ? FIRE_MODE_STRONG : FIRE_MODE_WEAK;
		event->s.weapon = ( ( ent->projectileInfo.radius * 1 / 8 ) > 255 ) ? 255 : ( ent->projectileInfo.radius * 1 / 8 );
	}

	// free at next frame
	G_FreeEdict( ent );
}

edict_t *W_Fire_Shockwave( edict_t *self, vec3_t start, vec3_t angles, int speed, float damage, int minKnockback, int maxKnockback, int stun, int minDamage, int radius, int timeout, int mod, int timeDelta ) {
	edict_t *wave;

	if( GS_Instagib() ) {
		damage = 9999;
	}

	wave = W_Fire_LinearProjectile( self, start, angles, speed, damage, minKnockback, maxKnockback, stun, minDamage, radius, timeout, timeDelta );
	if( mod == MOD_SHOCKWAVE_S ) {
		wave->s.modelindex = trap_ModelIndex( PATH_WAVE_STRONG_MODEL );
		wave->s.sound = trap_SoundIndex( S_WEAPON_SHOCKWAVE_S_FLY );
		wave->s.effects |= EF_STRONG_WEAPON;
	} else {
		wave->s.modelindex = trap_ModelIndex( PATH_WAVE_WEAK_MODEL );
		wave->s.sound = trap_SoundIndex( S_WEAPON_SHOCKWAVE_W_FLY );
		wave->s.effects &= ~EF_STRONG_WEAPON;
	}

	wave->s.type = ET_WAVE;
	wave->classname = "wave";
	wave->style = mod;
	wave->s.ownerNum = ENTNUM( self );

	wave->think = W_Think_Wave;
	wave->touch = W_Touch_Wave;
	wave->timeout = level.time + timeout;
	wave->nextThink = level.time + 1;

	return wave;
}

/*
* W_Fire_Instagun_Strong
*/
void W_Fire_Instagun( edict_t *self, vec3_t start, vec3_t angles, float damage, int knockback,
					  int stun, int radius, int range, int mod, int timeDelta ) {
	vec3_t from, end, dir;
	trace_t tr;
	edict_t *ignore, *event, *hit;
	int hit_movetype;
	int mask;
	bool missed = true;
	int dmgflags = 0;

	if( GS_Instagib() ) {
		damage = 9999;
	}

	AngleVectors( angles, dir, NULL, NULL );
	VectorMA( start, range, dir, end );
	VectorCopy( start, from );
	ignore = self;
	mask = MASK_SHOT;
	if( GS_RaceGametype() ) {
		mask = MASK_SOLID;
	}
	tr.ent = -1;
	while( ignore ) {
		G_Trace4D( &tr, from, NULL, NULL, end, ignore, mask, timeDelta );
		VectorCopy( tr.endpos, from );
		ignore = NULL;
		if( tr.ent == -1 ) {
			break;
		}

		// allow trail to go through SOLID_BBOX entities (players, gibs, etc)
		hit = &game.edicts[tr.ent];
		hit_movetype = hit->movetype; // backup the original movetype as the entity may "die"

		if( !ISBRUSHMODEL( hit->s.modelindex ) ) {
			ignore = hit;
		}

		if( ( hit != self ) && ( hit->takedamage ) ) {
			G_Damage( hit, self, self, dir, dir, tr.endpos, damage, knockback, stun, dmgflags, mod );

			// spawn a impact event on each damaged ent
			event = G_SpawnEvent( EV_INSTA_EXPLOSION, DirToByte( tr.plane.normal ), tr.endpos );
			event->s.ownerNum = ENTNUM( self );
			event->s.firemode = FIRE_MODE_STRONG;
			if( hit->r.client ) {
				missed = false;
			}
		}

		// some entity was touched
		if( hit == world
			|| hit_movetype == MOVETYPE_NONE
			|| hit_movetype == MOVETYPE_PUSH ) {
			if( g_instajump->integer && self && self->r.client ) {
				// create a temporary inflictor entity
				edict_t *inflictor;

				inflictor = G_Spawn();
				inflictor->s.solid = SOLID_NOT;
				inflictor->timeDelta = 0;
				VectorCopy( tr.endpos, inflictor->s.origin );
				inflictor->s.ownerNum = ENTNUM( self );
				inflictor->projectileInfo.maxDamage = 0;
				inflictor->projectileInfo.minDamage = 0;
				inflictor->projectileInfo.maxKnockback = knockback;
				inflictor->projectileInfo.minKnockback = 1;
				inflictor->projectileInfo.stun = 0;
				inflictor->projectileInfo.radius = radius;

				G_RadiusDamage( inflictor, self, &tr.plane, NULL, mod );

				G_FreeEdict( inflictor );
			}
			break;
		}
	}

	if( missed && self->r.client ) {
		G_AwardPlayerMissedElectrobolt( self, mod );
	}

	// send the weapon fire effect
	event = G_SpawnEvent( EV_INSTATRAIL, ENTNUM( self ), start );
	VectorScale( dir, 1024, event->s.origin2 );
}

/*
* G_HideLaser
*/
void G_HideLaser( edict_t *ent ) {
	ent->s.modelindex = 0;
	ent->s.sound = 0;
	ent->r.svflags = SVF_NOCLIENT;

	// give it 100 msecs before freeing itself, so we can relink it if we start firing again
	ent->think = G_FreeEdict;
	ent->nextThink = level.time + 100;
}

/*
* G_Laser_Think
*/
static void G_Laser_Think( edict_t *ent ) {
	edict_t *owner;

	if( ent->s.ownerNum < 1 || ent->s.ownerNum > gs.maxclients ) {
		G_FreeEdict( ent );
		return;
	}

	owner = &game.edicts[ent->s.ownerNum];

	if( G_ISGHOSTING( owner ) || owner->s.weapon != WEAP_LASERGUN ||
		trap_GetClientState( PLAYERNUM( owner ) ) < CS_SPAWNED ||
		( owner->r.client->ps.weaponState != WEAPON_STATE_REFIRESTRONG
		  && owner->r.client->ps.weaponState != WEAPON_STATE_REFIRE ) ) {
		G_HideLaser( ent );
		return;
	}

	ent->nextThink = level.time + 1;
}

static float laser_damage;
static int laser_knockback;
static int laser_stun;
static int laser_attackerNum;
static int laser_mod;
static int laser_missed;

static void _LaserImpact( trace_t *trace, vec3_t dir ) {
	edict_t *attacker;

	if( !trace || trace->ent <= 0 ) {
		return;
	}
	if( trace->ent == laser_attackerNum ) {
		return; // should not be possible theoretically but happened at least once in practice

	}
	attacker = &game.edicts[laser_attackerNum];

	if( game.edicts[trace->ent].takedamage ) {
		G_Damage( &game.edicts[trace->ent], attacker, attacker, dir, dir, trace->endpos, laser_damage, laser_knockback, laser_stun, DAMAGE_STUN_CLAMP | DAMAGE_KNOCKBACK_SOFT, laser_mod );
		laser_missed = false;
	}
}

static edict_t *_FindOrSpawnLaser( edict_t *owner, int entType, bool *newLaser ) {
	int i, ownerNum;
	edict_t *e, *laser;

	// first of all, see if we already have a beam entity for this laser
	*newLaser = false;
	laser = NULL;
	ownerNum = ENTNUM( owner );
	for( i = gs.maxclients + 1; i < game.maxentities; i++ ) {
		e = &game.edicts[i];
		if( !e->r.inuse ) {
			continue;
		}

		if( e->s.ownerNum == ownerNum && ( e->s.type == ET_LASERBEAM || e->s.type == ET_CURVELASERBEAM ) ) {
			laser = e;
			break;
		}
	}

	// if no ent was found we have to create one
	if( !laser || laser->s.type != entType || !laser->s.modelindex ) {
		if( !laser ) {
			*newLaser = true;
			laser = G_Spawn();
		}

		laser->s.type = entType;
		laser->s.ownerNum = ownerNum;
		laser->movetype = MOVETYPE_NONE;
		laser->r.solid = SOLID_NOT;
		laser->s.modelindex = 255; // needs to have some value so it isn't filtered by the server culling
		laser->r.svflags &= ~SVF_NOCLIENT;
		// The only real utility of this flag is to prevent aggressive entity culling by anticheat.
		// The netcode/entity system needs an overhaul, use this hack as a temporary workaround.
		laser->r.svflags |= SVF_TRANSMITORIGIN2;
	}

	return laser;
}

/*
* W_Fire_Lasergun
*/
edict_t *W_Fire_Lasergun( edict_t *self, vec3_t start, vec3_t angles, float damage, int knockback, int stun, int range, int mod, int timeDelta ) {
	edict_t *laser;
	bool newLaser;
	trace_t tr;
	vec3_t dir;

	if( GS_Instagib() ) {
		damage = 9999;
	}

	laser = _FindOrSpawnLaser( self, ET_LASERBEAM, &newLaser );
	if( newLaser ) {
		// the quad start sound is added from the server
		if( self->r.client && self->r.client->ps.inventory[POWERUP_QUAD] > 0 ) {
			G_Sound( self, CHAN_AUTO, trap_SoundIndex( S_QUAD_FIRE ), ATTN_NORM );
		}
	}

	laser_damage = damage;
	laser_knockback = knockback;
	laser_stun = stun;
	laser_attackerNum = ENTNUM( self );
	laser_mod = mod;
	laser_missed = true;

	GS_TraceLaserBeam( &tr, start, angles, range, ENTNUM( self ), timeDelta, _LaserImpact );

	laser->r.svflags |= SVF_FORCEOWNER;
	VectorCopy( start, laser->s.origin );
	AngleVectors( angles, dir, NULL, NULL );
	VectorMA( laser->s.origin, range, dir, laser->s.origin2 );

	laser->think = G_Laser_Think;
	laser->nextThink = level.time + 100;

	if( laser_missed && self->r.client ) {
		G_AwardPlayerMissedLasergun( self, mod );
	}

	// calculate laser's mins and maxs for linkEntity
	G_SetBoundsForSpanEntity( laser, 8 );

	GClip_LinkEntity( laser );

	return laser;
}

/*
* W_Fire_Lasergun_Weak
*/
edict_t *W_Fire_Lasergun_Weak( edict_t *self, vec3_t start, vec3_t end, float damage, int knockback, int stun, int range, int mod, int timeDelta ) {
	edict_t *laser;
	bool newLaser;
	trace_t trace;

	if( GS_Instagib() ) {
		damage = 9999;
	}

	laser = _FindOrSpawnLaser( self, ET_CURVELASERBEAM, &newLaser );
	if( newLaser ) {
		// the quad start sound is added from the server
		if( self->r.client && self->r.client->ps.inventory[POWERUP_QUAD] > 0 ) {
			G_Sound( self, CHAN_AUTO, trap_SoundIndex( S_QUAD_FIRE ), ATTN_NORM );
		}
	}

	laser_damage = damage;
	laser_knockback = knockback;
	laser_stun = stun;
	laser_attackerNum = ENTNUM( self );
	laser_mod = mod;
	laser_missed = true;

	GS_TraceCurveLaserBeam( &trace, start, self->s.angles, end, ENTNUM( self ), timeDelta, _LaserImpact );

	laser->r.svflags |= SVF_FORCEOWNER;
	VectorCopy( start, laser->s.origin );
	VectorCopy( end, laser->s.origin2 );

	laser->think = G_Laser_Think;
	laser->nextThink = level.time + 100;

	if( laser_missed && self->r.client ) {
		G_AwardPlayerMissedLasergun( self, mod );
	}

	// calculate laser's mins and maxs for linkEntity
	G_SetBoundsForSpanEntity( laser, 8 );

	GClip_LinkEntity( laser );

	return laser;
}
