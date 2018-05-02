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

// g_combat.c

#include "g_local.h"

/*
*
*/
int G_ModToAmmo( int mod ) {
	if( mod == MOD_GUNBLADE_W ) {
		return AMMO_WEAK_GUNBLADE;
	} else if( mod == MOD_GUNBLADE_S ) {
		return AMMO_GUNBLADE;
	} else if( mod == MOD_MACHINEGUN_W ) {
		return AMMO_WEAK_BULLETS;
	} else if( mod == MOD_MACHINEGUN_S ) {
		return AMMO_BULLETS;
	} else if( mod == MOD_RIOTGUN_W ) {
		return AMMO_WEAK_SHELLS;
	} else if( mod == MOD_RIOTGUN_S ) {
		return AMMO_SHELLS;
	} else if( mod == MOD_GRENADE_W || mod == MOD_GRENADE_SPLASH_W ) {
		return AMMO_WEAK_GRENADES;
	} else if( mod == MOD_GRENADE_S || mod == MOD_GRENADE_SPLASH_S ) {
		return AMMO_GRENADES;
	} else if( mod == MOD_ROCKET_W || mod == MOD_ROCKET_SPLASH_W ) {
		return AMMO_WEAK_ROCKETS;
	} else if( mod == MOD_ROCKET_S || mod == MOD_ROCKET_SPLASH_S ) {
		return AMMO_ROCKETS;
	} else if( mod == MOD_PLASMA_W || mod == MOD_PLASMA_SPLASH_W ) {
		return AMMO_WEAK_PLASMA;
	} else if( mod == MOD_PLASMA_S || mod == MOD_PLASMA_SPLASH_S ) {
		return AMMO_PLASMA;
	} else if( mod == MOD_ELECTROBOLT_W ) {
		return AMMO_WEAK_BOLTS;
	} else if( mod == MOD_ELECTROBOLT_S ) {
		return AMMO_BOLTS;
	} else if( mod == MOD_INSTAGUN_W ) {
		return AMMO_WEAK_INSTAS;
	} else if( mod == MOD_INSTAGUN_S ) {
		return AMMO_INSTAS;
	} else if( mod == MOD_LASERGUN_W ) {
		return AMMO_WEAK_LASERS;
	} else if( mod == MOD_LASERGUN_S ) {
		return AMMO_LASERS;
	} else {
		return AMMO_NONE;
	}
}

/*
* G_CanSplashDamage
*/
#define SPLASH_DAMAGE_TRACE_FRAC_EPSILON 1.0 / 32.0f
static bool G_CanSplashDamage( edict_t *targ, edict_t *inflictor, cplane_t *plane ) {
	vec3_t dest, origin;
	trace_t trace;
	int solidmask = MASK_SOLID;

	if( !targ ) {
		return false;
	}

	if( !plane ) {
		VectorCopy( inflictor->s.origin, origin );
	}

	// bmodels need special checking because their origin is 0,0,0
	if( targ->movetype == MOVETYPE_PUSH ) {
		// NOT FOR PLAYERS only for entities that can push the players
		VectorAdd( targ->r.absmin, targ->r.absmax, dest );
		VectorScale( dest, 0.5, dest );
		G_Trace4D( &trace, origin, vec3_origin, vec3_origin, dest, inflictor, solidmask, inflictor->timeDelta );
		if( trace.fraction >= 1.0 - SPLASH_DAMAGE_TRACE_FRAC_EPSILON || trace.ent == ENTNUM( targ ) ) {
			return true;
		}

		return false;
	}

	if( plane ) {
		// up by 9 units to account for stairs
		VectorMA( inflictor->s.origin, 9, plane->normal, origin );
	}

	// This is for players
	G_Trace4D( &trace, origin, vec3_origin, vec3_origin, targ->s.origin, inflictor, solidmask, inflictor->timeDelta );
	if( trace.fraction >= 1.0 - SPLASH_DAMAGE_TRACE_FRAC_EPSILON || trace.ent == ENTNUM( targ ) ) {
		return true;
	}

	VectorCopy( targ->s.origin, dest );
	dest[0] += 15.0;
	dest[1] += 15.0;
	G_Trace4D( &trace, origin, vec3_origin, vec3_origin, dest, inflictor, solidmask, inflictor->timeDelta );
	if( trace.fraction >= 1.0 - SPLASH_DAMAGE_TRACE_FRAC_EPSILON || trace.ent == ENTNUM( targ ) ) {
		return true;
	}

	VectorCopy( targ->s.origin, dest );
	dest[0] += 15.0;
	dest[1] -= 15.0;
	G_Trace4D( &trace, origin, vec3_origin, vec3_origin, dest, inflictor, solidmask, inflictor->timeDelta );
	if( trace.fraction >= 1.0 - SPLASH_DAMAGE_TRACE_FRAC_EPSILON || trace.ent == ENTNUM( targ ) ) {
		return true;
	}

	VectorCopy( targ->s.origin, dest );
	dest[0] -= 15.0;
	dest[1] += 15.0;
	G_Trace4D( &trace, origin, vec3_origin, vec3_origin, dest, inflictor, solidmask, inflictor->timeDelta );
	if( trace.fraction >= 1.0 - SPLASH_DAMAGE_TRACE_FRAC_EPSILON || trace.ent == ENTNUM( targ ) ) {
		return true;
	}

	VectorCopy( targ->s.origin, dest );
	dest[0] -= 15.0;
	dest[1] -= 15.0;
	G_Trace4D( &trace, origin, vec3_origin, vec3_origin, dest, inflictor, solidmask, inflictor->timeDelta );
	if( trace.fraction >= 1.0 - SPLASH_DAMAGE_TRACE_FRAC_EPSILON || trace.ent == ENTNUM( targ ) ) {
		return true;
	}
/*
    VectorCopy( targ->s.origin, dest );
    origin[2] += 9;
    G_Trace4D( &trace, origin, vec3_origin, vec3_origin, targ->s.origin, inflictor, solidmask, inflictor->timeDelta );
    if( trace.fraction >= 1.0-SPLASH_DAMAGE_TRACE_FRAC_EPSILON || trace.ent == ENTNUM( targ ) )
        return true;
*/

	return false;
}

/*
* G_Killed
*/
void G_Killed( edict_t *targ, edict_t *inflictor, edict_t *attacker, int damage, const vec3_t point, int mod ) {
	if( targ->health < -999 ) {
		targ->health = -999;
	}

	if( targ->deadflag == DEAD_DEAD ) {
		return;
	}

	targ->deadflag = DEAD_DEAD;
	targ->enemy = attacker;

	if( targ->r.client ) {
		if( attacker && targ != attacker ) {
			if( GS_IsTeamDamage( &targ->s, &attacker->s ) ) {
				attacker->snap.teamkill = true;
			} else {
				attacker->snap.kill = true;
			}
		}

		// count stats
		if( GS_MatchState() == MATCH_STATE_PLAYTIME ) {
			targ->r.client->level.stats.AddDeath();
			teamlist[targ->s.team].stats.AddDeath();

			if( !attacker || !attacker->r.client || attacker == targ || attacker == world ) {
				targ->r.client->level.stats.AddSuicide();
				teamlist[targ->s.team].stats.AddSuicide();
			} else {
				if( GS_IsTeamDamage( &targ->s, &attacker->s ) ) {
					attacker->r.client->level.stats.AddTeamFrag();
					teamlist[attacker->s.team].stats.AddTeamFrag();
				} else {
					attacker->r.client->level.stats.AddFrag();
					teamlist[attacker->s.team].stats.AddFrag();
					G_AwardPlayerKilled( targ, inflictor, attacker, mod );
				}
			}
		}
	}

	G_Gametype_ScoreEvent( attacker ? attacker->r.client : NULL, "kill", va( "%i %i %i %i", targ->s.number, ( inflictor == world ) ? -1 : ENTNUM( inflictor ), ENTNUM( attacker ), mod ) );

	G_CallDie( targ, inflictor, attacker, damage, point );
}

/*
* G_CheckArmor
*/
static float G_CheckArmor( edict_t *ent, float damage, int dflags ) {
	gclient_t *client = ent->r.client;
	float maxsave, save, armordamage;

	if( !client ) {
		return 0.0f;
	}

	if( dflags & DAMAGE_NO_ARMOR || dflags & DAMAGE_NO_PROTECTION ) {
		return 0.0f;
	}

	maxsave = min( damage, client->resp.armor / g_armor_degradation->value );

	if( maxsave <= 0.0f ) {
		return 0.0f;
	}

	armordamage = maxsave * g_armor_degradation->value;
	save = maxsave * g_armor_protection->value;

	client->resp.armor -= armordamage;
	if( ARMOR_TO_INT( client->resp.armor ) <= 0 ) {
		client->resp.armor = 0.0f;
	}
	client->ps.stats[STAT_ARMOR] = ARMOR_TO_INT( client->resp.armor );

	return save;
}

/*
* G_BlendFrameDamage
*/
static void G_BlendFrameDamage( edict_t *ent, float damage, float *old_damage, const vec3_t point, const vec3_t basedir, vec3_t old_point, vec3_t old_dir ) {
	vec3_t offset;
	float frac;
	vec3_t dir;
	int i;

	if( !point ) {
		VectorSet( offset, 0, 0, ent->viewheight );
	} else {
		VectorSubtract( point, ent->s.origin, offset );
	}

	VectorNormalize2( basedir, dir );

	if( *old_damage == 0 ) {
		VectorCopy( offset, old_point );
		VectorCopy( dir, old_dir );
		*old_damage = damage;
		return;
	}

	frac = damage / ( damage + *old_damage );
	for( i = 0; i < 3; i++ ) {
		old_point[i] = ( old_point[i] * ( 1.0f - frac ) ) + offset[i] * frac;
		old_dir[i] = ( old_dir[i] * ( 1.0f - frac ) ) + dir[i] * frac;
	}
	*old_damage += damage;
}

#define MIN_KNOCKBACK_SPEED 2.5

/*
* G_KnockBackPush
*/
static void G_KnockBackPush( edict_t *targ, edict_t *attacker, const vec3_t basedir, int knockback, int dflags ) {
	float mass = 75.0f;
	float push;
	vec3_t dir;

	if( targ->flags & FL_NO_KNOCKBACK ) {
		knockback = 0;
	}

	knockback *= g_knockback_scale->value;

	if( knockback < 1 ) {
		return;
	}

	if( ( targ->movetype == MOVETYPE_NONE ) ||
		( targ->movetype == MOVETYPE_PUSH ) ||
		( targ->movetype == MOVETYPE_STOP ) ||
		( targ->movetype == MOVETYPE_BOUNCE ) ) {
		return;
	}

	if( targ->mass > 75 ) {
		mass = targ->mass;
	}

	push = 1000.0f * ( (float)knockback / mass );
	if( push < MIN_KNOCKBACK_SPEED ) {
		return;
	}

	VectorNormalize2( basedir, dir );

	if( targ->r.client && targ != attacker && !( dflags & DAMAGE_KNOCKBACK_SOFT ) ) {
		targ->r.client->ps.pmove.stats[PM_STAT_KNOCKBACK] = 3 * knockback;
		clamp( targ->r.client->ps.pmove.stats[PM_STAT_KNOCKBACK], 100, 250 );
	}

	VectorMA( targ->velocity, push, dir, targ->velocity );
}

/*
* G_Damage
* targ		entity that is being damaged
* inflictor	entity that is causing the damage
* attacker	entity that caused the inflictor to damage targ
* example: targ=enemy, inflictor=rocket, attacker=player
*
* dir			direction of the attack
* point		point at which the damage is being inflicted
* normal		normal vector from that point
* damage		amount of damage being inflicted
* knockback	force to be applied against targ as a result of the damage
*
* dflags		these flags are used to control how T_Damage works
*/
void G_Damage( edict_t *targ, edict_t *inflictor, edict_t *attacker, const vec3_t pushdir, const vec3_t dmgdir, const vec3_t point, float damage, float knockback, float stun, int dflags, int mod ) {
	gclient_t *client;
	float take;
	float save;
	float asave;
	bool statDmg;

	if( !targ || !targ->takedamage ) {
		return;
	}

	if( !attacker ) {
		attacker = world;
		mod = MOD_TRIGGER_HURT;
	}

	meansOfDeath = mod;

	client = targ->r.client;

	// Cgg - race mode: players don't interact with one another
	if( GS_RaceGametype() ) {
		if( attacker->r.client && targ->r.client && attacker != targ ) {
			return;
		}
	}

	// push
	if( !( dflags & DAMAGE_NO_KNOCKBACK ) ) {
		G_KnockBackPush( targ, attacker, pushdir, knockback, dflags );
	}

	// stun
	if( g_allow_stun->integer && !( dflags & ( DAMAGE_NO_STUN | FL_GODMODE ) )
		&& (int)stun > 0 && targ->r.client && targ->r.client->resp.takeStun &&
		!GS_IsTeamDamage( &targ->s, &attacker->s ) && ( targ != attacker ) ) {
		if( dflags & DAMAGE_STUN_CLAMP ) {
			if( targ->r.client->ps.pmove.stats[PM_STAT_STUN] < (int)stun ) {
				targ->r.client->ps.pmove.stats[PM_STAT_STUN] = (int)stun;
			}
		} else {
			targ->r.client->ps.pmove.stats[PM_STAT_STUN] += (int)stun;
		}

		clamp( targ->r.client->ps.pmove.stats[PM_STAT_STUN], 0, MAX_STUN_TIME );
	}

	// dont count self-damage cause it just adds the same to both stats
	statDmg = ( attacker != targ ) && ( mod != MOD_TELEFRAG );

	// apply handicap on the damage given
	if( statDmg && attacker->r.client && !GS_Instagib() ) {
		// handicap is a percentage value
		if( attacker->r.client->handicap != 0 ) {
			damage *= 1.0 - ( attacker->r.client->handicap * 0.01f );
		}
	}

	take = damage;
	save = 0;

	// check for cases where damage is protected
	if( !( dflags & DAMAGE_NO_PROTECTION ) ) {
		// check for godmode
		if( targ->flags & FL_GODMODE ) {
			take = 0;
			save = damage;
		}
		// never damage in timeout
		else if( GS_MatchPaused() ) {
			take = save = 0;
		}
		// ca has self splash damage disabled
		else if( ( dflags & DAMAGE_RADIUS ) && attacker == targ && !GS_SelfDamage() ) {
			take = save = 0;
		}
		// don't get damage from players in race
		else if( ( GS_RaceGametype() ) && attacker->r.client && targ->r.client &&
				 ( attacker->r.client != targ->r.client ) ) {
			take = save = 0;
		}
		// team damage avoidance
		else if( GS_IsTeamDamage( &targ->s, &attacker->s ) && !G_Gametype_CanTeamDamage( dflags ) ) {
			take = save = 0;
		}
		// apply warShell powerup protection
		else if( targ->r.client && targ->r.client->ps.inventory[POWERUP_SHELL] > 0 ) {
			// warshell offers full protection in instagib
			if( GS_Instagib() ) {
				take = 0;
				save = damage;
			} else {
				take = ( damage * 0.25f );
				save = damage - take;
			}

			// todo : add protection sound
		}
	}

	asave = G_CheckArmor( targ, take, dflags );
	take -= asave;

	//treat cheat/powerup savings the same as armor
	asave += save;

	// APPLY THE DAMAGES

	if( !take && !asave ) {
		return;
	}

	// do the damage
	if( take <= 0 ) {
		return;
	}

	// adding damage given/received to stats
	if( statDmg && attacker->r.client && !targ->deadflag && targ->movetype != MOVETYPE_PUSH && targ->s.type != ET_CORPSE ) {
		attacker->r.client->level.stats.AddDamageGiven( take + asave );
		teamlist[attacker->s.team].stats.AddDamageGiven( take + asave );
		if( GS_IsTeamDamage( &targ->s, &attacker->s ) ) {
			attacker->r.client->level.stats.AddTeamDamageGiven( take + asave );
			teamlist[attacker->s.team].stats.AddTeamDamageGiven( take + asave );
		}
	}

	G_Gametype_ScoreEvent( attacker->r.client, "dmg", va( "%i %f %i", targ->s.number, damage, attacker->s.number ) );

	if( attacker->ai )
		AI_DamagedEntity( attacker, targ, (int)damage );

	if( statDmg && client ) {
		client->level.stats.AddDamageTaken( take + asave );
		teamlist[targ->s.team].stats.AddDamageTaken( take + asave );
		if( GS_IsTeamDamage( &targ->s, &attacker->s ) ) {
			client->level.stats.AddTeamDamageTaken( take + asave );
			teamlist[targ->s.team].stats.AddTeamDamageTaken( take + asave );
		}
	}

	// accumulate received damage for snapshot effects
	{
		vec3_t dorigin;

		if( inflictor == world && mod == MOD_FALLING ) { // it's fall damage
			targ->snap.damage_fall += take + save;
		}

		if( point[0] != 0.0f || point[1] != 0.0f || point[2] != 0.0f ) {
			VectorCopy( point, dorigin );
		} else {
			VectorSet( dorigin,
					   targ->s.origin[0],
					   targ->s.origin[1],
					   targ->s.origin[2] + targ->viewheight );
		}

		G_BlendFrameDamage( targ, take, &targ->snap.damage_taken, dorigin, dmgdir, targ->snap.damage_at, targ->snap.damage_dir );
		G_BlendFrameDamage( targ, save, &targ->snap.damage_saved, dorigin, dmgdir, targ->snap.damage_at, targ->snap.damage_dir );

		if( targ->r.client ) {
			if( mod != MOD_FALLING && mod != MOD_TELEFRAG && mod != MOD_SUICIDE ) {
				if( inflictor == world || attacker == world ) {
					// for world inflicted damage use always 'frontal'
					G_ClientAddDamageIndicatorImpact( targ->r.client, take + save, NULL );
				} else if( dflags & DAMAGE_RADIUS ) {
					// for splash hits the direction is from the inflictor origin
					G_ClientAddDamageIndicatorImpact( targ->r.client, take + save, pushdir );
				} else {   // for direct hits the direction is the projectile direction
					G_ClientAddDamageIndicatorImpact( targ->r.client, take + save, dmgdir );
				}
			}
		}
	}

	targ->health = targ->health - take;

	// add damage done to stats
	if( !GS_IsTeamDamage( &targ->s, &attacker->s ) && statDmg && G_ModToAmmo( mod ) != AMMO_NONE && client && attacker->r.client ) {
		attacker->r.client->level.stats.accuracy_hits[G_ModToAmmo( mod ) - AMMO_GUNBLADE]++;
		attacker->r.client->level.stats.accuracy_damage[G_ModToAmmo( mod ) - AMMO_GUNBLADE] += damage;
		teamlist[attacker->s.team].stats.accuracy_hits[G_ModToAmmo( mod ) - AMMO_GUNBLADE]++;
		teamlist[attacker->s.team].stats.accuracy_damage[G_ModToAmmo( mod ) - AMMO_GUNBLADE] += damage;

		G_AwardPlayerHit( targ, attacker, mod );
	}

	// accumulate given damage for hit sounds
	if( ( take || asave ) && targ != attacker && client && !targ->deadflag ) {
		if( attacker ) {
			if( GS_IsTeamDamage( &targ->s, &attacker->s ) ) {
				attacker->snap.damageteam_given += take + asave; // we want to know how good our hit was, so saved also matters
			} else {
				attacker->snap.damage_given += take + asave;
			}
		}
	}

	if( G_IsDead( targ ) ) {
		if( client ) {
			targ->flags |= FL_NO_KNOCKBACK;
		}
		G_Killed( targ, inflictor, attacker, HEALTH_TO_INT( take ), point, mod );
	} else {
		G_CallPain( targ, attacker, knockback, take );
	}
}

/*
* G_SplashFrac
*/
void G_SplashFrac( const vec3_t origin, const vec3_t mins, const vec3_t maxs, const vec3_t point, float maxradius, vec3_t pushdir, float *kickFrac, float *dmgFrac ) {
#define VERTICALBIAS 0.65f // 0...1
#define CAPSULEDISTANCE

//#define SPLASH_HDIST_CLAMP 0
	vec3_t boxcenter = { 0, 0, 0 };
	vec3_t hitpoint;
	float distance;
	int i;
	float innerradius;
	float refdistance;

	if( maxradius <= 0 ) {
		if( kickFrac ) {
			*kickFrac = 0;
		}
		if( dmgFrac ) {
			*dmgFrac = 0;
		}
		if( pushdir ) {
			VectorClear( pushdir );
		}
		return;
	}

	VectorCopy( point, hitpoint );

	innerradius = ( maxs[0] + maxs[1] - mins[0] - mins[1] ) * 0.25;

#ifdef CAPSULEDISTANCE

	// Find the distance to the closest point in the capsule contained in the player bbox
	// modify the origin so the inner sphere acts as a capsule
	VectorCopy( origin, boxcenter );
	boxcenter[2] = hitpoint[2];
	clamp( boxcenter[2], ( origin[2] + mins[2] ) + innerradius, ( origin[2] + maxs[2] ) - innerradius );
#else

	// find center of the box
	for( i = 0; i < 3; i++ )
		boxcenter[i] = origin[i] + ( 0.5f * ( maxs[i] + mins[i] ) );
#endif

	// find push intensity
	distance = Distance( boxcenter, hitpoint );

	if( distance >= maxradius ) {
		if( kickFrac ) {
			*kickFrac = 0;
		}
		if( dmgFrac ) {
			*dmgFrac = 0;
		}
		if( pushdir ) {
			VectorClear( pushdir );
		}
		return;
	}

	refdistance = innerradius;
	if( refdistance >= maxradius ) {
		if( kickFrac ) {
			*kickFrac = 0;
		}
		if( dmgFrac ) {
			*dmgFrac = 0;
		}
		if( pushdir ) {
			VectorClear( pushdir );
		}
		return;
	}

	maxradius -= refdistance;
	distance -= refdistance;
	if( distance < 0 ) {
		distance = 0;
	}

	distance = maxradius - distance;
	clamp( distance, 0, maxradius );

	if( dmgFrac ) {
		// soft sin curve
		*dmgFrac = sin( DEG2RAD( ( distance / maxradius ) * 80 ) );
		clamp( *dmgFrac, 0.0f, 1.0f );
	}

	if( kickFrac ) {
		// linear kick fraction
		float kick = ( distance / maxradius );

		kick *= kick;

		//kick = maxradius / distance;
		clamp( kick, 0, 1 );

		// half linear half exponential
		//*kickFrac =  ( kick + ( kick * kick ) ) * 0.5f;

		// linear
		*kickFrac = kick;

		clamp( *kickFrac, 0.0f, 1.0f );
	}

	//if( dmgFrac && kickFrac )
	//	G_Printf( "SPLASH: dmgFrac %.2f kickFrac %.2f\n", *dmgFrac, *kickFrac );

	// find push direction

	if( pushdir ) {
#ifdef CAPSULEDISTANCE

		// find real center of the box again
		for( i = 0; i < 3; i++ )
			boxcenter[i] = origin[i] + ( 0.5f * ( maxs[i] + mins[i] ) );
#endif

#ifdef VERTICALBIAS

		// move the center up for the push direction
		if( origin[2] + maxs[2] > boxcenter[2] ) {
			boxcenter[2] += VERTICALBIAS * ( ( origin[2] + maxs[2] ) - boxcenter[2] );
		}
#endif // VERTICALBIAS

#ifdef SPLASH_HDIST_CLAMP

		// if pushed from below, hack the hitpoint to limit the side push direction
		if( hitpoint[2] < boxcenter[2] && SPLASH_HDIST_CLAMP >= 0 ) {
			// do not allow the hitpoint to be further away
			// than SPLASH_HDIST_CLAMP in the horizontal axis
			vec3_t vec;

			vec[0] = hitpoint[0];
			vec[1] = hitpoint[1];
			vec[2] = boxcenter[2];

			if( Distance( boxcenter, vec ) > SPLASH_HDIST_CLAMP ) {
				VectorSubtract( vec, boxcenter, pushdir );
				VectorNormalize( pushdir );
				VectorMA( boxcenter, SPLASH_HDIST_CLAMP, pushdir, hitpoint );
				hitpoint[2] = point[2]; // restore the original hitpoint height
			}
		}
#endif // SPLASH_HDIST_CLAMP

		VectorSubtract( boxcenter, hitpoint, pushdir );
		VectorNormalize( pushdir );
	}

#undef VERTICALBIAS
#undef CAPSULEDISTANCE
#undef SPLASH_HDIST_CLAMP
}

/*
* G_RadiusDamage
*/
void G_RadiusDamage( edict_t *inflictor, edict_t *attacker, cplane_t *plane, edict_t *ignore, int mod ) {
	int i, numtouch;
	int touch[MAX_EDICTS];
	edict_t *ent = NULL;
	float dmgFrac, kickFrac, damage, knockback, stun;
	vec3_t pushDir;
	int timeDelta;

	float maxdamage, mindamage, maxknockback, minknockback, maxstun, minstun, radius;

	assert( inflictor );

	maxdamage = inflictor->projectileInfo.maxDamage;
	mindamage = inflictor->projectileInfo.minDamage;
	maxknockback = inflictor->projectileInfo.maxKnockback;
	minknockback = inflictor->projectileInfo.minKnockback;
	maxstun = inflictor->projectileInfo.stun;
	minstun = 1;
	radius = inflictor->projectileInfo.radius;

	if( radius <= 1.0f || ( maxdamage <= 0.0f && maxknockback <= 0.0f ) ) {
		return;
	}

	clamp_high( mindamage, maxdamage );
	clamp_high( minknockback, maxknockback );
	clamp_high( minstun, maxstun );

	numtouch = GClip_FindInRadius4D( inflictor->s.origin, radius, touch, MAX_EDICTS, inflictor->timeDelta );
	for( i = 0; i < numtouch; i++ ) {
		ent = game.edicts + touch[i];
		if( ent == ignore || !ent->takedamage ) {
			continue;
		}

		if( ent == attacker && ent->r.client ) {
			timeDelta = 0;
		} else {
			timeDelta = inflictor->timeDelta;
		}

		G_SplashFrac4D( ENTNUM( ent ), inflictor->s.origin, radius, pushDir, &kickFrac, &dmgFrac, timeDelta );

		damage = max( 0, mindamage + ( ( maxdamage - mindamage ) * dmgFrac ) );
		stun = max( 0, minstun + ( ( maxstun - minstun ) * dmgFrac ) );
		knockback = max( 0, minknockback + ( ( maxknockback - minknockback ) * kickFrac ) );

		// weapon jumps hack : when knockback on self, use strong weapon definition
		if( ent == attacker && ent->r.client ) {
			gs_weapon_definition_t *weapondef = NULL;
			if( inflictor->s.type == ET_ROCKET ) {
				weapondef = GS_GetWeaponDef( WEAP_ROCKETLAUNCHER );
			} else if( inflictor->s.type == ET_GRENADE ) {
				weapondef = GS_GetWeaponDef( WEAP_GRENADELAUNCHER );
			} else if( inflictor->s.type == ET_PLASMA ) {
				weapondef = GS_GetWeaponDef( WEAP_PLASMAGUN );
			} else if( inflictor->s.type == ET_BLASTER ) {
				weapondef = GS_GetWeaponDef( WEAP_GUNBLADE );
			}

			if( weapondef ) {
				G_SplashFrac4D( ENTNUM( ent ), inflictor->s.origin, radius, pushDir, &kickFrac, NULL, 0 );

				minknockback = weapondef->firedef.minknockback;
				maxknockback = weapondef->firedef.knockback;
				clamp_high( minknockback, maxknockback );
				knockback = ( minknockback + ( (float)( maxknockback - minknockback ) * kickFrac ) ) * g_self_knockback->value;
				damage *= weapondef->firedef.selfdamage;
			}
		}

		if( knockback < 1.0f ) {
			knockback = 0.0f;
		}

		if( stun < 1.0f ) {
			stun = 0.0f;
		}

		if( damage <= 0.0f && knockback <= 0.0f && stun <= 0.0f ) {
			continue;
		}

		if( G_CanSplashDamage( ent, inflictor, plane ) ) {
			G_Damage( ent, inflictor, attacker, pushDir, inflictor->velocity, inflictor->s.origin, damage, knockback, stun, DAMAGE_RADIUS, mod );
		}
	}
}
