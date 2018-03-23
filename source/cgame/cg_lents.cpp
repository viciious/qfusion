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

// cg_lents.c -- client side temporary entities

#include "cg_local.h"

#define MAX_LOCAL_ENTITIES  512

static vec3_t debris_maxs = { 4, 4, 8 };
static vec3_t debris_mins = { -4, -4, 0 };

typedef enum
{
	LE_FREE,
	LE_NO_FADE,
	LE_RGB_FADE,
	LE_ALPHA_FADE,
	LE_SCALE_ALPHA_FADE,
	LE_INVERSESCALE_ALPHA_FADE,
	LE_LASER,

	LE_EXPLOSION_TRACER,
	LE_DASH_SCALE,
	LE_PUFF_SCALE,
	LE_PUFF_SHRINK
} letype_t;

typedef struct lentity_s
{
	struct lentity_s *prev, *next;

	letype_t type;

	entity_t ent;
	vec4_t color;

	unsigned int start;

	float light;
	vec3_t lightcolor;

	vec3_t velocity;
	vec3_t avelocity;
	vec3_t angles;
	vec3_t accel;

	int bounce;     //is activator and bounceability value at once

	int frames;

	cgs_skeleton_t *skel;
	bonepose_t *static_boneposes;
} lentity_t;

lentity_t cg_localents[MAX_LOCAL_ENTITIES];
lentity_t cg_localents_headnode, *cg_free_lents;

/*
* CG_ClearLocalEntities
*/
void CG_ClearLocalEntities( void ) {
	int i;

	memset( cg_localents, 0, sizeof( cg_localents ) );

	// link local entities
	cg_free_lents = cg_localents;
	cg_localents_headnode.prev = &cg_localents_headnode;
	cg_localents_headnode.next = &cg_localents_headnode;
	for( i = 0; i < MAX_LOCAL_ENTITIES - 1; i++ )
		cg_localents[i].next = &cg_localents[i + 1];
}

/*
* CG_AllocLocalEntity
*/
static lentity_t *CG_AllocLocalEntity( letype_t type, float r, float g, float b, float a ) {
	lentity_t *le;

	if( cg_free_lents ) { // take a free decal if possible
		le = cg_free_lents;
		cg_free_lents = le->next;
	} else {              // grab the oldest one otherwise
		le = cg_localents_headnode.prev;
		le->prev->next = le->next;
		le->next->prev = le->prev;
	}

	memset( le, 0, sizeof( *le ) );
	le->type = type;
	le->start = cg.time;
	le->color[0] = r;
	le->color[1] = g;
	le->color[2] = b;
	le->color[3] = a;

	switch( le->type ) {
		case LE_NO_FADE:
			break;
		case LE_RGB_FADE:
			le->ent.shaderRGBA[3] = ( uint8_t )( 255 * a );
			break;
		case LE_SCALE_ALPHA_FADE:
		case LE_INVERSESCALE_ALPHA_FADE:
		case LE_PUFF_SHRINK:
			le->ent.shaderRGBA[0] = ( uint8_t )( 255 * r );
			le->ent.shaderRGBA[1] = ( uint8_t )( 255 * g );
			le->ent.shaderRGBA[2] = ( uint8_t )( 255 * b );
			le->ent.shaderRGBA[3] = ( uint8_t )( 255 * a );
			break;
		case LE_PUFF_SCALE:
			le->ent.shaderRGBA[0] = ( uint8_t )( 255 * r );
			le->ent.shaderRGBA[1] = ( uint8_t )( 255 * g );
			le->ent.shaderRGBA[2] = ( uint8_t )( 255 * b );
			le->ent.shaderRGBA[3] = ( uint8_t )( 255 * a );
			break;
		case LE_ALPHA_FADE:
			le->ent.shaderRGBA[0] = ( uint8_t )( 255 * r );
			le->ent.shaderRGBA[1] = ( uint8_t )( 255 * g );
			le->ent.shaderRGBA[2] = ( uint8_t )( 255 * b );
			break;
		default:
			break;
	}

	// put the decal at the start of the list
	le->prev = &cg_localents_headnode;
	le->next = cg_localents_headnode.next;
	le->next->prev = le;
	le->prev->next = le;

	return le;
}

/*
* CG_FreeLocalEntity
*/
static void CG_FreeLocalEntity( lentity_t *le ) {
	if( le->static_boneposes ) {
		CG_Free( le->static_boneposes );
		le->static_boneposes = NULL;
	}

	// remove from linked active list
	le->prev->next = le->next;
	le->next->prev = le->prev;

	// insert into linked free list
	le->next = cg_free_lents;
	cg_free_lents = le;
}

/*
* CG_AllocModel
*/
static lentity_t *CG_AllocModel( letype_t type, const vec3_t origin, const vec3_t angles, int frames,
								 float r, float g, float b, float a, float light, float lr, float lg, float lb, struct model_s *model, struct shader_s *shader ) {
	lentity_t *le;

	le = CG_AllocLocalEntity( type, r, g, b, a );
	le->frames = frames;
	le->light = light;
	le->lightcolor[0] = lr;
	le->lightcolor[1] = lg;
	le->lightcolor[2] = lb;

	le->ent.rtype = RT_MODEL;
	le->ent.renderfx = RF_NOSHADOW;
	le->ent.model = model;
	le->ent.customShader = shader;
	le->ent.shaderTime = cg.time;
	le->ent.scale = 1.0f;

	VectorCopy( angles, le->angles );
	AnglesToAxis( angles, le->ent.axis );
	VectorCopy( origin, le->ent.origin );

	return le;
}

/*
* CG_AllocSprite
*/
static lentity_t *CG_AllocSprite( letype_t type, const vec3_t origin, float radius, int frames,
								  float r, float g, float b, float a, float light, float lr, float lg, float lb, struct shader_s *shader ) {
	lentity_t *le;

	le = CG_AllocLocalEntity( type, r, g, b, a );
	le->frames = frames;
	le->light = light;
	le->lightcolor[0] = lr;
	le->lightcolor[1] = lg;
	le->lightcolor[2] = lb;

	le->ent.rtype = RT_SPRITE;
	le->ent.renderfx = RF_NOSHADOW;
	le->ent.radius = radius;
	le->ent.customShader = shader;
	le->ent.shaderTime = cg.time;
	le->ent.scale = 1.0f;

	Matrix3_Identity( le->ent.axis );
	VectorCopy( origin, le->ent.origin );

	return le;
}

/*
* CG_AllocLaser
*/
static lentity_t *CG_AllocLaser( const vec3_t start, const vec3_t end, float radius, int frames,
								 float r, float g, float b, float a, struct shader_s *shader ) {
	lentity_t *le;

	le = CG_AllocLocalEntity( LE_LASER, 1, 1, 1, 1 );
	le->frames = frames;

	le->ent.radius = radius;
	le->ent.customShader = shader;
	Vector4Set( le->ent.shaderRGBA, r * 255, g * 255, b * 255, a * 255 );
	VectorCopy( start, le->ent.origin );
	VectorCopy( end, le->ent.origin2 );

	return le;
}

void CG_SpawnSprite( const vec3_t origin, const vec3_t velocity, const vec3_t accel,
					 float radius, int time, int bounce, bool expandEffect, bool shrinkEffect,
					 float r, float g, float b, float a,
					 float light, float lr, float lg, float lb,
					 struct shader_s *shader ) {
	lentity_t *le;
	int numFrames;
	letype_t type = LE_ALPHA_FADE;

	if( !radius || !shader || !origin ) {
		return;
	}

	numFrames = (int)( (float)( time * 1000 ) * 0.01f );
	if( !numFrames ) {
		return;
	}

	if( expandEffect && !shrinkEffect ) {
		type = LE_SCALE_ALPHA_FADE;
	}

	if( !expandEffect && shrinkEffect ) {
		type = LE_INVERSESCALE_ALPHA_FADE;
	}

	le = CG_AllocSprite( type, origin, radius, numFrames,
						 r, g, b, a,
						 light, lr, lg, lb,
						 shader );

	if( velocity != NULL ) {
		VectorCopy( velocity, le->velocity );
	}
	if( accel != NULL ) {
		VectorCopy( accel, le->accel );
	}

	le->bounce = bounce;
	le->ent.rotation = rand() % 360;
}

/*
* CG_ElectroRings
*/
static void CG_ElectroRings( const vec3_t start, const vec3_t end, const vec4_t color ) {
	vec3_t dir, origin;
	int i, numrings;
	float len;
	float timeFrac;
	float space = 15.0f;
	lentity_t *le;
	struct shader_s *s = CG_MediaShader( cgs.media.shaderElectroBeamRing );

	VectorSubtract( end, start, dir );
	len = VectorNormalize( dir );
	if( !len ) {
		return;
	}

	numrings = len / space + 1;
	timeFrac = 0.6f / (float)numrings;
	for( i = 0; i < numrings; i++ ) {
		float t = ( (float)i * timeFrac + 7.5f + ( i * 0.20f ) ) * (float)cg_ebbeam_time->value;
		float l = i * space;

		VectorMA( start, l, dir, origin );
		le = CG_AllocSprite( LE_ALPHA_FADE, origin, 4.25f, t,
							 color[0], color[1], color[2], color[3], 0, 0, 0, 0,
							 s );
		le->ent.rotation = rand() % 360;
	}
}

/*
* CG_ElectroTrail2
*/
void CG_ElectroTrail2( const vec3_t start, const vec3_t end, int team ) {
	vec4_t color = { 1.0f, 1.0f, 1.0f, 1.0f };

	if( cg_ebbeam_time->value < 0.05f ) {
		return;
	}

	if( cg_teamColoredBeams->integer && ( ( team == TEAM_ALPHA ) || ( team == TEAM_BETA ) ) ) {
		CG_TeamColor( team, color );
	}

	if( cg_ebbeam_old->integer ) {
		CG_ElectroPolyBeam( start, end, team );
		CG_ElectroIonsTrail( start, end, color );
	} else {
		CG_ElectroPolyBeam( start, end, team );
		CG_ElectroRings( start, end, color );
		CG_ElectroIonsTrail2( start, end, color );
	}
}

/*
* CG_ImpactSmokePuff
*/
void CG_ImpactSmokePuff( const vec3_t origin, const vec3_t dir, float radius, float alpha, int time, int speed ) {
#define SMOKEPUFF_MAXVIEWDIST 700
	lentity_t *le;
	struct shader_s *shader = CG_MediaShader( cgs.media.shaderSmokePuff );
	vec3_t local_origin, local_dir;

	if( CG_PointContents( origin ) & MASK_WATER ) {
		return;
	}

	if( DistanceFast( origin, cg.view.origin ) * cg.view.fracDistFOV > SMOKEPUFF_MAXVIEWDIST ) {
		return;
	}

	if( !VectorLength( dir ) ) {
		VectorNegate( &cg.view.axis[AXIS_FORWARD], local_dir );
	} else {
		VectorNormalize2( dir, local_dir );
	}

	//offset the origin by half of the radius
	VectorMA( origin, radius * 0.5f, local_dir, local_origin );

	le = CG_AllocSprite( LE_SCALE_ALPHA_FADE, local_origin, radius + crandom(), time,
						 1, 1, 1, alpha, 0, 0, 0, 0, shader );

	le->ent.rotation = rand() % 360;
	VectorScale( local_dir, speed, le->velocity );
}

/*
* CG_BulletExplosion
*/
void CG_BulletExplosion( const vec3_t pos, const vec_t *dir, const trace_t *trace ) {
	lentity_t *le;
	vec3_t angles;
	vec3_t local_dir, end;
	trace_t local_trace;
	const trace_t *tr;

	assert( dir || trace );

	if( dir ) {
		vec3_t local_pos;

		// find what are we hitting
		VectorCopy( pos, local_pos );
		VectorMA( pos, -1.0, dir, end );
		CG_Trace( &local_trace, local_pos, vec3_origin, vec3_origin, end, cg.view.POVent, MASK_SHOT );
		if( local_trace.fraction == 1.0 ) {
			return;
		}

		tr = &local_trace;
		VectorCopy( dir, local_dir );
	} else {
		tr = trace;
		VectorCopy( tr->plane.normal, local_dir );
	}

	VecToAngles( local_dir, angles );

	if( tr->ent > 0 && cg_entities[tr->ent].current.type == ET_PLAYER ) {
		return;
	} else if( tr->surfFlags & SURF_FLESH || ( tr->ent > 0 && cg_entities[tr->ent].current.type == ET_CORPSE ) ) {
		le = CG_AllocModel( LE_ALPHA_FADE, pos, angles, 3, //3 frames for weak
							1, 0, 0, 1, //full white no inducted alpha
							0, 0, 0, 0, //dlight
							CG_MediaModel( cgs.media.modBulletExplode ),
							NULL );
		le->ent.rotation = rand() % 360;
		le->ent.scale = 1.0f;
		if( ISVIEWERENTITY( tr->ent ) ) {
			le->ent.renderfx |= RF_VIEWERMODEL;
		}
	} else if( cg_particles->integer && ( tr->surfFlags & SURF_DUST ) ) {
		// throw particles on dust
		CG_ImpactSmokePuff( tr->endpos, tr->plane.normal, 4, 0.6f, 6, 8 );
	} else {
		le = CG_AllocModel( LE_ALPHA_FADE, pos, angles, 3, //3 frames for weak
							1, 1, 1, 1, //full white no inducted alpha
							0, 0, 0, 0, //dlight
							CG_MediaModel( cgs.media.modBulletExplode ),
							NULL );
		le->ent.rotation = rand() % 360;
		le->ent.scale = 1.0f;
		if( cg_particles->integer ) {
			CG_ImpactSmokePuff( tr->endpos, tr->plane.normal, 2, 0.6f, 6, 8 );
		}

		if( !( tr->surfFlags & SURF_NOMARKS ) ) {
			CG_SpawnDecal( pos, local_dir, random() * 360, 8, 1, 1, 1, 1, 10, 1, false, CG_MediaShader( cgs.media.shaderBulletMark ) );
		}
	}
}

/*
* CG_BubbleTrail
*/
void CG_BubbleTrail( const vec3_t start, const vec3_t end, int dist ) {
	int i;
	float len;
	vec3_t move, vec;
	lentity_t *le;
	struct shader_s *shader;

	VectorCopy( start, move );
	VectorSubtract( end, start, vec );
	len = VectorNormalize( vec );
	if( !len ) {
		return;
	}

	VectorScale( vec, dist, vec );
	shader = CG_MediaShader( cgs.media.shaderWaterBubble );

	for( i = 0; i < len; i += dist ) {
		le = CG_AllocSprite( LE_ALPHA_FADE, move, 3, 10,
							 1, 1, 1, 1,
							 0, 0, 0, 0,
							 shader );
		VectorSet( le->velocity, crandom() * 5, crandom() * 5, crandom() * 5 + 6 );
		VectorAdd( move, vec, move );
	}
}

/*
* CG_PlasmaExplosion
*/
void CG_PlasmaExplosion( const vec3_t pos, const vec3_t dir, int fire_mode, float radius ) {
	lentity_t *le;
	vec3_t angles;
	float model_radius = PLASMA_EXPLOSION_MODEL_RADIUS;

	VecToAngles( dir, angles );

	if( fire_mode == FIRE_MODE_STRONG ) {
		le = CG_AllocModel( LE_ALPHA_FADE, pos, angles, 4,
							1, 1, 1, 1,
							150, 0, 0.75, 0,
							CG_MediaModel( cgs.media.modPlasmaExplosion ),
							NULL );
		le->ent.scale = radius / model_radius;
	} else {

		le = CG_AllocModel( LE_ALPHA_FADE, pos, angles, 4,
							1, 1, 1, 1,
							80, 0, 0.75, 0,
							CG_MediaModel( cgs.media.modPlasmaExplosion ),
							NULL );
		le->ent.scale = radius / model_radius;
	}

	le->ent.rotation = rand() % 360;

	CG_SpawnDecal( pos, dir, 90, 16,
				   1, 1, 1, 1, 4, 1, true,
				   CG_MediaShader( cgs.media.shaderPlasmaMark ) );
}

/*
* CG_BoltExplosionMode
*/
void CG_BoltExplosionMode( const vec3_t pos, const vec3_t dir, int fire_mode, int surfFlags ) {
	lentity_t *le;
	vec3_t angles;

	if( !CG_SpawnDecal( pos, dir, random() * 360, 12,
						1, 1, 1, 1, 10, 1, true, CG_MediaShader( cgs.media.shaderElectroboltMark ) ) ) {
		if( surfFlags & ( SURF_SKY | SURF_NOMARKS | SURF_NOIMPACT ) ) {
			return;
		}
	}

	VecToAngles( dir, angles );

	le = CG_AllocModel( LE_INVERSESCALE_ALPHA_FADE, pos, angles, 6, // 6 is time
						1, 1, 1, 1, //full white no inducted alpha
						250, 0.75, 0.75, 0.75, //white dlight
						CG_MediaModel( cgs.media.modElectroBoltWallHit ), NULL );

	le->ent.rotation = rand() % 360;
	le->ent.scale = ( fire_mode == FIRE_MODE_STRONG ) ? 1.5f : 1.0f;

	// add white energy particles on the impact
	CG_ImpactPuffParticles( pos, dir, 15, 0.75f, 1, 1, 1, 1, NULL );

	trap_S_StartFixedSound( CG_MediaSfx( cgs.media.sfxElectroboltHit ), pos, CHAN_AUTO,
							cg_volume_effects->value, ATTN_STATIC );
}

/*
* CG_InstaExplosionMode
*/
void CG_InstaExplosionMode( const vec3_t pos, const vec3_t dir, int fire_mode, int surfFlags, int owner ) {
	int team = -1;
	vec4_t tcolor = { 0.65f, 0.0f, 0.26f, 1.0f };
	lentity_t *le;
	vec3_t angles;

	if( cg_teamColoredInstaBeams->integer && owner && ( owner < gs.maxclients + 1 ) ) {
		team = cg_entities[owner].current.team;
	}
	if( ( team == TEAM_ALPHA ) || ( team == TEAM_BETA ) ) {
		CG_TeamColor( team, tcolor );
		tcolor[0] *= 0.65f;
		tcolor[1] *= 0.65f;
		tcolor[2] *= 0.65f;
	}

	if( !CG_SpawnDecal( pos, dir, random() * 360, 12,
						tcolor[0], tcolor[1], tcolor[2], 1.0f,
						10, 1, true, CG_MediaShader( cgs.media.shaderInstagunMark ) ) ) {
		if( surfFlags & ( SURF_SKY | SURF_NOMARKS | SURF_NOIMPACT ) ) {
			return;
		}
	}

	VecToAngles( dir, angles );

	le = CG_AllocModel( LE_ALPHA_FADE, pos, angles, 6, // 6 is time
						tcolor[0], tcolor[1], tcolor[2], 1,
						250, 0.65, 0.65, 0.65, //white dlight
						CG_MediaModel( cgs.media.modInstagunWallHit ), NULL );

	le->ent.rotation = rand() % 360;
	le->ent.scale = ( fire_mode == FIRE_MODE_STRONG ) ? 1.5f : 1.0f;

	// add white energy particles on the impact
	CG_ImpactPuffParticles( pos, dir, 15, 0.75f, 1, 1, 1, 1, NULL );

	trap_S_StartFixedSound( CG_MediaSfx( cgs.media.sfxElectroboltHit ), pos, CHAN_AUTO,
							cg_volume_effects->value, ATTN_STATIC );
}

/*
* CG_RocketExplosionMode
*/
void CG_RocketExplosionMode( const vec3_t pos, const vec3_t dir, int fire_mode, float radius ) {
	lentity_t *le;
	vec3_t angles, vec;
	vec3_t origin;
	float expvelocity = 8.0f;

	VecToAngles( dir, angles );

	if( fire_mode == FIRE_MODE_STRONG ) {
		//trap_S_StartSound ( pos, 0, 0, CG_MediaSfx (cgs.media.sfxRocketLauncherStrongHit), cg_volume_effects->value, ATTN_NORM, 0 );
		CG_SpawnDecal( pos, dir, random() * 360, radius * 0.5, 1, 1, 1, 1, 10, 1, false, CG_MediaShader( cgs.media.shaderExplosionMark ) );

	} else {
		//trap_S_StartSound ( pos, 0, 0, CG_MediaSfx (cgs.media.sfxRocketLauncherWeakHit), cg_volume_effects->value, ATTN_NORM, 0 );
		CG_SpawnDecal( pos, dir, random() * 360, radius * 0.25, 1, 1, 1, 1, 10, 1, false, CG_MediaShader( cgs.media.shaderExplosionMark ) );
	}

	// animmap shader of the explosion
	VectorMA( pos, radius * 0.12f, dir, origin );
	le = CG_AllocSprite( LE_ALPHA_FADE, origin, radius * 0.5f, 8,
						 1, 1, 1, 1,
						 radius * 4, 0.8f, 0.6f, 0, // orange dlight
						 CG_MediaShader( cgs.media.shaderRocketExplosion ) );

	VectorSet( vec, crandom() * expvelocity, crandom() * expvelocity, crandom() * expvelocity );
	VectorScale( dir, expvelocity, le->velocity );
	VectorAdd( le->velocity, vec, le->velocity );
	le->ent.rotation = rand() % 360;

	if( cg_explosionsRing->integer ) {
		// explosion ring sprite
		VectorMA( pos, radius * 0.20f, dir, origin );
		le = CG_AllocSprite( LE_ALPHA_FADE, origin, radius, 3,
							 1, 1, 1, 1,
							 0, 0, 0, 0, // no dlight
							 CG_MediaShader( cgs.media.shaderRocketExplosionRing ) );

		le->ent.rotation = rand() % 360;
	}

	if( cg_explosionsDust->integer == 1 ) {
		// dust ring parallel to the contact surface
		CG_ExplosionsDust( pos, dir, radius );
	}

	// Explosion particles
	CG_ParticleExplosionEffect( pos, dir, 1, 0.5, 0, 32 );

	if( fire_mode == FIRE_MODE_STRONG ) {
		trap_S_StartFixedSound( CG_MediaSfx( cgs.media.sfxRocketLauncherStrongHit ), pos, CHAN_AUTO, cg_volume_effects->value, ATTN_DISTANT );
	} else {
		trap_S_StartFixedSound( CG_MediaSfx( cgs.media.sfxRocketLauncherWeakHit ), pos, CHAN_AUTO, cg_volume_effects->value, ATTN_DISTANT );
	}

	//jalfixme: add sound at water?
}

/*
* CG_RocketExplosionMode
*/
void CG_WaveExplosionMode( const vec3_t pos, const vec3_t dir, int fire_mode, float radius ) {
	lentity_t *le;
	vec3_t angles;
	vec3_t origin;

	VecToAngles( dir, angles );

	if( fire_mode == FIRE_MODE_STRONG ) {
		CG_SpawnDecal( pos, dir, random() * 360, radius * 0.5, 1, 1, 1, 1, 10, 1, false, CG_MediaShader( cgs.media.shaderExplosionMark ) );
	} else {
		CG_SpawnDecal( pos, dir, random() * 360, radius * 0.25, 1, 1, 1, 1, 10, 1, false, CG_MediaShader( cgs.media.shaderExplosionMark ) );
	}

	VectorMA( pos, radius * 0.12f, dir, origin );
	le = CG_AllocModel( LE_INVERSESCALE_ALPHA_FADE, origin, vec3_origin, 3,
						1, 1, 1, 1,
						450.0f, 0.9f, 0.9f, 1.0f,
						CG_MediaModel( cgs.media.modWaveExplosion ), NULL );

	le->ent.rotation = rand() % 360;

	VectorMA( pos, radius * 0.20f, dir, origin );
	le = CG_AllocSprite( LE_SCALE_ALPHA_FADE, origin, radius * 0.3f, 3,
						 1, 1, 1, 1,
						 0, 0, 0, 0, // no dlight
						 CG_MediaShader( cgs.media.shaderWaveExplosionRing ) );

	le->ent.rotation = rand() % 360;

	// Explosion particles
	CG_ParticleExplosionEffect( pos, dir, 0.9, 1.0, 1.0, 64, 0.0f );

	if( fire_mode == FIRE_MODE_STRONG ) {
		trap_S_StartFixedSound( CG_MediaSfx( cgs.media.sfxWaveStrongHit ), pos, CHAN_AUTO, cg_volume_effects->value, ATTN_DISTANT );
	} else {
		trap_S_StartFixedSound( CG_MediaSfx( cgs.media.sfxWaveWeakHit ), pos, CHAN_AUTO, cg_volume_effects->value, ATTN_DISTANT );
	}
}

/*
* CG_BladeImpact
*/
void CG_BladeImpact( const vec3_t pos, const vec3_t dir ) {
	lentity_t *le;
	vec3_t angles;
	vec3_t end;
	vec3_t local_pos, local_dir;
	trace_t trace;

	//find what are we hitting
	VectorCopy( pos, local_pos );
	VectorNormalize2( dir, local_dir );
	VectorMA( pos, -1.0, local_dir, end );
	CG_Trace( &trace, local_pos, vec3_origin, vec3_origin, end, cg.view.POVent, MASK_SHOT );
	if( trace.fraction == 1.0 ) {
		return;
	}

	VecToAngles( local_dir, angles );

	if( trace.surfFlags & SURF_FLESH ||
		( trace.ent > 0 && cg_entities[trace.ent].current.type == ET_PLAYER )
		|| ( trace.ent > 0 && cg_entities[trace.ent].current.type == ET_CORPSE ) ) {
		le = CG_AllocModel( LE_ALPHA_FADE, pos, angles, 3, //3 frames for weak
							1, 1, 1, 1, //full white no inducted alpha
							0, 0, 0, 0, //dlight
							CG_MediaModel( cgs.media.modBladeWallHit ), NULL );
		le->ent.rotation = rand() % 360;
		le->ent.scale = 1.0f;

		trap_S_StartFixedSound( CG_MediaSfx( cgs.media.sfxBladeFleshHit[(int)( random() * 3 )] ), pos, CHAN_AUTO,
								cg_volume_effects->value, ATTN_NORM );
	} else if( trace.surfFlags & SURF_DUST ) {
		// throw particles on dust
		CG_ParticleEffect( trace.endpos, trace.plane.normal, 0.30f, 0.30f, 0.25f, 30 );

		//fixme? would need a dust sound
		trap_S_StartFixedSound( CG_MediaSfx( cgs.media.sfxBladeWallHit[(int)( random() * 2 )] ), pos, CHAN_AUTO,
								cg_volume_effects->value, ATTN_NORM );
	} else {
		le = CG_AllocModel( LE_ALPHA_FADE, pos, angles, 3, //3 frames for weak
							1, 1, 1, 1, //full white no inducted alpha
							0, 0, 0, 0, //dlight
							CG_MediaModel( cgs.media.modBladeWallHit ), NULL );
		le->ent.rotation = rand() % 360;
		le->ent.scale = 1.0f;

		CG_ParticleEffect( trace.endpos, trace.plane.normal, 0.30f, 0.30f, 0.25f, 15 );

		trap_S_StartFixedSound( CG_MediaSfx( cgs.media.sfxBladeWallHit[(int)( random() * 2 )] ), pos, CHAN_AUTO,
								cg_volume_effects->value, ATTN_NORM );
		if( !( trace.surfFlags & SURF_NOMARKS ) ) {
			CG_SpawnDecal( pos, dir, random() * 10, 8, 1, 1, 1, 1, 10, 1, false, CG_MediaShader( cgs.media.shaderBladeMark ) );
		}
	}
}

/*
* CG_LasertGunImpact
*/
void CG_LaserGunImpact( const vec3_t pos, const vec3_t dir, float radius, const vec3_t laser_dir, const vec4_t color ) {
	entity_t ent;
	vec3_t ndir;
	vec3_t angles;

	memset( &ent, 0, sizeof( ent ) );
	VectorCopy( pos, ent.origin );
	VectorMA( ent.origin, 2, dir, ent.origin );
	ent.renderfx = RF_FULLBRIGHT | RF_NOSHADOW;
	ent.scale = 1.45f;
	Vector4Set( ent.shaderRGBA, color[0] * 255, color[1] * 255, color[2] * 255, color[3] * 255 );
	ent.model = CG_MediaModel( cgs.media.modLasergunWallExplo );
	VectorNegate( laser_dir, ndir );
	VecToAngles( ndir, angles );
	angles[2] = anglemod( -360.0f * cg.time * 0.001f );

	AnglesToAxis( angles, ent.axis );

	trap_R_AddEntityToScene( &ent );
}

/*
* CG_GunBladeBlastImpact
*/
void CG_GunBladeBlastImpact( const vec3_t pos, const vec3_t dir, float radius ) {
	lentity_t *le;
	lentity_t *le_explo;
	vec3_t angles;
	float model_radius = GUNBLADEBLAST_EXPLOSION_MODEL_RADIUS;

	VecToAngles( dir, angles );

	le = CG_AllocModel( LE_ALPHA_FADE, pos, angles, 2, //3 frames
						1, 1, 1, 1, //full white no inducted alpha
						0, 0, 0, 0, //dlight
						CG_MediaModel( cgs.media.modBladeWallHit ),

	                    //"models/weapon_hits/gunblade/hit_blast.md3"
						NULL );
	le->ent.rotation = rand() % 360;
	le->ent.scale = 1.0f; // this is the small bullet impact


	le_explo = CG_AllocModel( LE_ALPHA_FADE, pos, angles, 2 + ( radius / 16.1f ),
							  1, 1, 1, 1, //full white no inducted alpha
							  0, 0, 0, 0, //dlight
							  CG_MediaModel( cgs.media.modBladeWallExplo ),
							  NULL );
	le_explo->ent.rotation = rand() % 360;
	le_explo->ent.scale = radius / model_radius;

	CG_SpawnDecal( pos, dir, random() * 360, 3 + ( radius * 0.5f ), 1, 1, 1, 1, 10, 1, false, CG_MediaShader( cgs.media.shaderExplosionMark ) );
}

static void CG_ProjectileFireTrail( centity_t *cent ) {
	lentity_t *le;
	float len;
	vec3_t vec;
	int trailTime;
	float radius = 8, alpha = cg_projectileFireTrailAlpha->value;
	struct shader_s *shader;

	if( !cg_projectileFireTrail->integer ) {
		return;
	}

	// didn't move
	VectorSubtract( cent->ent.origin, cent->trailOrigin, vec );
	len = VectorNormalize( vec );
	if( !len ) {
		return;
	}

	if( cent->effects & EF_STRONG_WEAPON ) {
		shader = CG_MediaShader( cgs.media.shaderStrongRocketFireTrailPuff );
	} else {
		shader = CG_MediaShader( cgs.media.shaderWeakRocketFireTrailPuff );
	}

	// density is found by quantity per second
	trailTime = (int)( 1000.0f / cg_projectileFireTrail->value );
	if( trailTime < 1 ) {
		trailTime = 1;
	}

	// we don't add more than one sprite each frame. If frame
	// ratio is too slow, people will prefer having less sprites on screen
	if( cent->localEffects[LOCALEFFECT_ROCKETFIRE_LAST_DROP] + trailTime < cg.time ) {
		cent->localEffects[LOCALEFFECT_ROCKETFIRE_LAST_DROP] = cg.time;

		clamp( alpha, 0.0f, 1.0f );
		le = CG_AllocSprite( LE_INVERSESCALE_ALPHA_FADE, cent->trailOrigin, radius, 4,
							 1.0f, 1.0f, 1.0f, alpha,
							 0, 0, 0, 0,
							 shader );
		VectorSet( le->velocity, -vec[0] * 10 + crandom() * 5, -vec[1] * 10 + crandom() * 5, -vec[2] * 10 + crandom() * 5 );
		le->ent.rotation = rand() % 360;
	}
}

/*
* CG_ProjectileTrail
*/
void CG_ProjectileTrail( centity_t *cent ) {
	lentity_t   *le;
	float len;
	vec3_t vec;
	int contents;
	int trailTime;
	float radius = 6.5f, alpha = 0.35f;
#if 0
	struct shader_s *shader = CG_MediaShader( cgs.media.shaderRocketTrailSmokePuff );
#else
	struct shader_s *shader = CG_MediaShader( cgs.media.shaderSmokePuff );
#endif
	CG_ProjectileFireTrail( cent ); // add fire trail

	if( !cg_projectileTrail->integer ) {
		return;
	}

	// didn't move
	VectorSubtract( cent->ent.origin, cent->trailOrigin, vec );
	len = VectorNormalize( vec );
	if( !len ) {
		return;
	}

	// density is found by quantity per second
	trailTime = (int)( 1000.0f / cg_projectileTrail->value );
	if( trailTime < 1 ) {
		trailTime = 1;
	}

	// we don't add more than one sprite each frame. If frame
	// ratio is too slow, people will prefer having less sprites on screen
	if( cent->localEffects[LOCALEFFECT_ROCKETTRAIL_LAST_DROP] + trailTime < cg.time ) {
		cent->localEffects[LOCALEFFECT_ROCKETTRAIL_LAST_DROP] = cg.time;

		contents = ( CG_PointContents( cent->trailOrigin ) & CG_PointContents( cent->ent.origin ) );
		if( contents & MASK_WATER ) {
			shader = CG_MediaShader( cgs.media.shaderWaterBubble );
			radius = 3 + crandom();
			alpha = 1.0f;
		}

		clamp( alpha, 0.0f, 1.0f );
		le = CG_AllocSprite( LE_PUFF_SHRINK, cent->trailOrigin, radius, 20,
							 1.0f, 1.0f, 1.0f, alpha,
							 0, 0, 0, 0,
							 shader );
		VectorSet( le->velocity, -vec[0] * 5 + crandom() * 5, -vec[1] * 5 + crandom() * 5, -vec[2] * 5 + crandom() * 5 + 3 );
		le->ent.rotation = rand() % 360;
	}
}

void CG_WaveCoronaAndTrail( centity_t *cent, const vec3_t org ) {
	if( cent->localEffects[LOCALEFFECT_WAVECORONA_LAST_DROP] + 4 < cg.time ) {
		cent->localEffects[LOCALEFFECT_WAVECORONA_LAST_DROP] = cg.time;
		lentity_t *le;

		le = CG_AllocSprite( LE_SCALE_ALPHA_FADE, org, 16, 4,
							 1.0f, 1.0f, 1.0f, 0.1f,
							 0, 0, 0, 0,
							 CG_MediaShader( cgs.media.shaderWaveCorona ));

		le->ent.rotation = rand() % 360;
	}

	if( cent->localEffects[LOCALEFFECT_WAVESPARK_LAST_DROP] + 8 < cg.time ) {
		cent->localEffects[LOCALEFFECT_WAVESPARK_LAST_DROP] = cg.time;
		for( int i = 0; i < 2; ++i ) {
			CG_WaveSpark( org );
		}
	}

	if( cent->localEffects[LOCALEFFECT_WAVETRAIL_LAST_DROP] + 16 < cg.time ) {
		cent->localEffects[LOCALEFFECT_WAVETRAIL_LAST_DROP] = cg.time;
		CG_ParticleEffect( org, vec3_origin, 1.0, 1.0, 1.0, 1, 0.0f );
	}
}

/*
* CG_BlasterTrail
*/
void CG_BlasterTrail( centity_t *ent, const vec3_t org ) {
	if( ent->localEffects[LOCALEFFECT_BLASTER_SPARK_LAST_DROP] + 8 < cg.time ) {
		ent->localEffects[LOCALEFFECT_BLASTER_SPARK_LAST_DROP] = cg.time;
		CG_ParticleEffect( org, vec3_origin, 0.9f, 0.6f, 0.0f, 2, 350 );
	}
	if( random() > 0.9f ) {
		CG_ParticleEffect( org, vec3_origin, 0.9f, 0.6f, 0.0f, 1, 750 );
	}
}

/*
* CG_NewBloodTrail
*/
void CG_NewBloodTrail( centity_t *cent ) {
	lentity_t *le;
	float len;
	vec3_t vec;
	int contents;
	int trailTime;
	float radius = 2.5f, alpha = cg_bloodTrailAlpha->value;
	struct shader_s *shader = CG_MediaShader( cgs.media.shaderBloodTrailPuff );

	if( !cg_showBloodTrail->integer ) {
		return;
	}

	if( !cg_bloodTrail->integer ) {
		return;
	}

	// didn't move
	VectorSubtract( cent->ent.origin, cent->trailOrigin, vec );
	len = VectorNormalize( vec );
	if( !len ) {
		return;
	}

	// density is found by quantity per second
	trailTime = (int)( 1000.0f / cg_bloodTrail->value );
	if( trailTime < 1 ) {
		trailTime = 1;
	}

	// we don't add more than one sprite each frame. If frame
	// ratio is too slow, people will prefer having less sprites on screen
	if( cent->localEffects[LOCALEFFECT_BLOODTRAIL_LAST_DROP] + trailTime < cg.time ) {
		cent->localEffects[LOCALEFFECT_BLOODTRAIL_LAST_DROP] = cg.time;

		contents = ( CG_PointContents( cent->trailOrigin ) & CG_PointContents( cent->ent.origin ) );
		if( contents & MASK_WATER ) {
			shader = CG_MediaShader( cgs.media.shaderBloodTrailLiquidPuff );
			radius = 4 + crandom();
			alpha = 0.5f * cg_bloodTrailAlpha->value;
		}

		clamp( alpha, 0.0f, 1.0f );
		le = CG_AllocSprite( LE_SCALE_ALPHA_FADE, cent->trailOrigin, radius, 8,
							 1.0f, 1.0f, 1.0f, alpha,
							 0, 0, 0, 0,
							 shader );
		VectorSet( le->velocity, -vec[0] * 5 + crandom() * 5, -vec[1] * 5 + crandom() * 5, -vec[2] * 5 + crandom() * 5 + 3 );
		le->ent.rotation = rand() % 360;
	}
}

/*
* CG_BloodDamageEffect
*/
void CG_BloodDamageEffect( const vec3_t origin, const vec3_t dir, int damage ) {
	lentity_t *le;
	int count, i;
	float radius = 5.0f, alpha = cg_bloodTrailAlpha->value;
	int time = 8;
	struct shader_s *shader = CG_MediaShader( cgs.media.shaderBloodImpactPuff );
	vec3_t local_dir;

	if( !cg_showBloodTrail->integer ) {
		return;
	}

	if( !cg_bloodTrail->integer ) {
		return;
	}

	count = (int)( damage * 0.25f );
	clamp( count, 1, 10 );

	if( CG_PointContents( origin ) & MASK_WATER ) {
		shader = CG_MediaShader( cgs.media.shaderBloodTrailLiquidPuff );
		radius += ( 1 + crandom() );
		alpha = 0.5f * cg_bloodTrailAlpha->value;
	}

	if( !VectorLength( dir ) ) {
		VectorNegate( &cg.view.axis[AXIS_FORWARD], local_dir );
	} else {
		VectorNormalize2( dir, local_dir );
	}

	for( i = 0; i < count; i++ ) {
		le = CG_AllocSprite( LE_PUFF_SHRINK, origin, radius + crandom(), time,
							 1, 1, 1, alpha, 0, 0, 0, 0, shader );

		le->ent.rotation = rand() % 360;

		// randomize dir
		VectorSet( le->velocity,
				   -local_dir[0] * 5 + crandom() * 5,
				   -local_dir[1] * 5 + crandom() * 5,
				   -local_dir[2] * 5 + crandom() * 5 + 3 );
		VectorMA( local_dir, min( 6, count ), le->velocity, le->velocity );
	}
}

/*
* CG_CartoonHitEffect
*/
void CG_CartoonHitEffect( const vec3_t origin, const vec3_t dir, int damage ) {
	lentity_t *le;
	int time = 6;
	float radius = 11.0f, alpha = cg_bloodTrailAlpha->value;
	struct shader_s *shader = CG_MediaShader( cgs.media.shaderCartoonHit );
	struct shader_s *shader2 = CG_MediaShader( cgs.media.shaderCartoonHit2 );
	struct shader_s *shader3 = CG_MediaShader( cgs.media.shaderCartoonHit3 );
	vec3_t local_origin, local_dir;

	if( !cg_cartoonHitEffect->integer ) {
		return;
	}

	if( damage < 39 ) {
		return;
	}

	if( !VectorLength( dir ) ) {
		VectorNegate( &cg.view.axis[AXIS_FORWARD], local_dir );
	} else {
		VectorNormalize2( dir, local_dir );
	}

	// Move effect a bit up from player
	VectorCopy( origin, local_origin );
	local_origin[2] += 65;

	// small buff
	if( damage < 64 ) {
		if( damage < 50 ) {
			// SPLITZOW!
			radius = 7.0f;
			le = CG_AllocSprite( LE_SCALE_ALPHA_FADE, local_origin, radius, time, 1, 1, 1, alpha, 0, 0, 0, 0, shader2 );
		} else {
			// POW!
			radius = 9.0f;
			le = CG_AllocSprite( LE_SCALE_ALPHA_FADE, local_origin, radius, time, 1, 1, 1, alpha, 0, 0, 0, 0, shader );
		}
	} else {
		// big buff
		// OUCH!
		le = CG_AllocSprite( LE_SCALE_ALPHA_FADE, local_origin, radius, time, 1, 1, 1, alpha, 0, 0, 0, 0, shader3 );
	}

	// randomize dir
	VectorSet( le->velocity,
			   -local_dir[0] * 5 + crandom() * 5,
			   -local_dir[1] * 5 + crandom() * 5,
			   -local_dir[2] * 5 + crandom() * 5 + 3 );
	VectorMA( local_dir, 1, le->velocity, le->velocity );
}

/*
* CG_PModel_SpawnTeleportEffect
*/
void CG_PModel_SpawnTeleportEffect( centity_t *cent ) {
	int j;
	cgs_skeleton_t *skel;
	lentity_t *le;
	vec3_t teleportOrigin;
	vec3_t rgb;

	skel = CG_SkeletonForModel( cent->ent.model );
	if( !skel || !cent->ent.boneposes ) {
		return;
	}

	for( j = LOCALEFFECT_EV_PLAYER_TELEPORT_IN; j <= LOCALEFFECT_EV_PLAYER_TELEPORT_OUT; j++ ) {
		if( cent->localEffects[j] ) {
			cent->localEffects[j] = 0;

			VectorSet( rgb, 0.5, 0.5, 0.5 );
			if( j == LOCALEFFECT_EV_PLAYER_TELEPORT_OUT ) {
				VectorCopy( cent->teleportedFrom, teleportOrigin );
			} else {
				VectorCopy( cent->teleportedTo, teleportOrigin );
				if( ISVIEWERENTITY( cent->current.number ) ) {
					VectorSet( rgb, 0.1, 0.1, 0.1 );
				}
			}
			if( cg_raceGhosts->integer && !ISVIEWERENTITY( cent->current.number ) && GS_RaceGametype() ) {
				VectorScale( rgb, cg_raceGhostsAlpha->value, rgb );
			}

			// spawn a dummy model
			le = CG_AllocModel( LE_RGB_FADE, teleportOrigin, vec3_origin, 10,
								rgb[0], rgb[1], rgb[2], 1, 0, 0, 0, 0, cent->ent.model,
								CG_MediaShader( cgs.media.shaderTeleportShellGfx ) );

			if( cent->skel ) {
				// use static bone pose, no animation
				le->skel = cent->skel;
				le->static_boneposes = ( bonepose_t * )CG_Malloc( sizeof( bonepose_t ) * le->skel->numBones );
				memcpy( le->static_boneposes, cent->ent.boneposes, sizeof( bonepose_t ) * le->skel->numBones );
				le->ent.boneposes = le->static_boneposes;
				le->ent.oldboneposes = le->ent.boneposes;
			}

			le->ent.frame = cent->ent.frame;
			le->ent.oldframe = cent->ent.oldframe;
			le->ent.backlerp = 1.0f;
			Matrix3_Copy( cent->ent.axis, le->ent.axis );
		}
	}
}

/*
* CG_GrenadeExplosionMode
*/
void CG_GrenadeExplosionMode( const vec3_t pos, const vec3_t dir, int fire_mode, float radius ) {
	lentity_t *le;
	vec3_t angles;
	vec3_t decaldir;
	vec3_t origin, vec;
	float expvelocity = 8.0f;

	VectorCopy( dir, decaldir );
	VecToAngles( dir, angles );

	//if( CG_PointContents( pos ) & MASK_WATER )
	//jalfixme: (shouldn't we do the water sound variation?)

	if( fire_mode == FIRE_MODE_STRONG ) {
		CG_SpawnDecal( pos, decaldir, random() * 360, radius * 0.5, 1, 1, 1, 1, 10, 1, false, CG_MediaShader( cgs.media.shaderExplosionMark ) );
	} else {
		CG_SpawnDecal( pos, decaldir, random() * 360, radius * 0.25, 1, 1, 1, 1, 10, 1, false, CG_MediaShader( cgs.media.shaderExplosionMark ) );
	}

	// animmap shader of the explosion
	VectorMA( pos, radius * 0.15f, dir, origin );
	le = CG_AllocSprite( LE_ALPHA_FADE, origin, radius * 0.5f, 8,
						 1, 1, 1, 1,
						 radius * 4, 0.75f, 0.533f, 0, // yellow dlight
						 CG_MediaShader( cgs.media.shaderRocketExplosion ) );

	VectorSet( vec, crandom() * expvelocity, crandom() * expvelocity, crandom() * expvelocity );
	VectorScale( dir, expvelocity, le->velocity );
	VectorAdd( le->velocity, vec, le->velocity );
	le->ent.rotation = rand() % 360;

	// explosion ring sprite
	if( cg_explosionsRing->integer ) {
		VectorMA( pos, radius * 0.25f, dir, origin );
		le = CG_AllocSprite( LE_ALPHA_FADE, origin, radius, 3,
							 1, 1, 1, 1,
							 0, 0, 0, 0, // no dlight
							 CG_MediaShader( cgs.media.shaderRocketExplosionRing ) );

		le->ent.rotation = rand() % 360;
	}

	if( cg_explosionsDust->integer == 1 ) {
		// dust ring parallel to the contact surface
		CG_ExplosionsDust( pos, dir, radius );
	}

	// Explosion particles
	CG_ParticleExplosionEffect( pos, dir, 1, 0.5, 0, 32 );

	if( fire_mode == FIRE_MODE_STRONG ) {
		trap_S_StartFixedSound( CG_MediaSfx( cgs.media.sfxGrenadeStrongExplosion ), pos, CHAN_AUTO, cg_volume_effects->value, ATTN_DISTANT );
	} else {
		trap_S_StartFixedSound( CG_MediaSfx( cgs.media.sfxGrenadeWeakExplosion ), pos, CHAN_AUTO, cg_volume_effects->value, ATTN_DISTANT );
	}
}

/*
* CG_GenericExplosion
*/
void CG_GenericExplosion( const vec3_t pos, const vec3_t dir, int fire_mode, float radius ) {
	lentity_t *le;
	vec3_t angles;
	vec3_t decaldir;
	vec3_t origin, vec;
	float expvelocity = 8.0f;

	VectorCopy( dir, decaldir );
	VecToAngles( dir, angles );

	//if( CG_PointContents( pos ) & MASK_WATER )
	//jalfixme: (shouldn't we do the water sound variation?)

	if( fire_mode == FIRE_MODE_STRONG ) {
		CG_SpawnDecal( pos, decaldir, random() * 360, radius * 0.5, 1, 1, 1, 1, 10, 1, false, CG_MediaShader( cgs.media.shaderExplosionMark ) );
	} else {
		CG_SpawnDecal( pos, decaldir, random() * 360, radius * 0.25, 1, 1, 1, 1, 10, 1, false, CG_MediaShader( cgs.media.shaderExplosionMark ) );
	}

	// animmap shader of the explosion
	VectorMA( pos, radius * 0.15f, dir, origin );
	le = CG_AllocSprite( LE_ALPHA_FADE, origin, radius * 0.5f, 8,
						 1, 1, 1, 1,
						 radius * 4, 0.75f, 0.533f, 0, // yellow dlight
						 CG_MediaShader( cgs.media.shaderRocketExplosion ) );

	VectorSet( vec, crandom() * expvelocity, crandom() * expvelocity, crandom() * expvelocity );
	VectorScale( dir, expvelocity, le->velocity );
	VectorAdd( le->velocity, vec, le->velocity );
	le->ent.rotation = rand() % 360;

	// use the rocket explosion sounds
	if( fire_mode == FIRE_MODE_STRONG ) {
		trap_S_StartFixedSound( CG_MediaSfx( cgs.media.sfxRocketLauncherStrongHit ), pos, CHAN_AUTO, cg_volume_effects->value, ATTN_DISTANT );
	} else {
		trap_S_StartFixedSound( CG_MediaSfx( cgs.media.sfxRocketLauncherWeakHit ), pos, CHAN_AUTO, cg_volume_effects->value, ATTN_DISTANT );
	}
}

/*
* CG_FlagFlareTrail
*/
void CG_FlagTrail( const vec3_t origin, const vec3_t start, const vec3_t end, float r, float g, float b ) {
	lentity_t *le;
	float len, mass = 20;
	vec3_t dir;

	VectorSubtract( end, start, dir );
	len = VectorNormalize( dir );
	if( !len ) {
		return;
	}

	le = CG_AllocSprite( LE_SCALE_ALPHA_FADE, origin, 8, 50 + 50 * random(),
						 r, g, b, 0.7f,
						 0, 0, 0, 0,
						 CG_MediaShader( cgs.media.shaderTeleporterSmokePuff ) );
	VectorSet( le->velocity, -dir[0] * 5 + crandom() * 5, -dir[1] * 5 + crandom() * 5, -dir[2] * 5 + crandom() * 5 + 3 );
	le->ent.rotation = rand() % 360;

	//friction and gravity
	VectorSet( le->accel, -0.2f, -0.2f, -9.8f * mass );
	le->bounce = 50;
}

/*
* CG_Explosion1
*/
void CG_Explosion1( const vec3_t pos ) {
	CG_RocketExplosionMode( pos, vec3_origin, FIRE_MODE_STRONG, 150 );
}

/*
* CG_Explosion2
*/
void CG_Explosion2( const vec3_t pos ) {
	CG_GrenadeExplosionMode( pos, vec3_origin, FIRE_MODE_STRONG, 150 );
}

/*
* CG_GreenLaser
*/
void CG_GreenLaser( const vec3_t start, const vec3_t end ) {
	CG_AllocLaser( start, end, 2.0f, 2.0f, 0.0f, 0.85f, 0.0f, 0.3f, CG_MediaShader( cgs.media.shaderLaser ) );
}

void CG_SpawnTracer( const vec3_t origin, const vec3_t dir, const vec3_t dir_per1, const vec3_t dir_per2 ) {
	lentity_t *tracer;
	vec3_t dir_temp;

	VectorMA( dir, crandom() * 2, dir_per1, dir_temp );
	VectorMA( dir_temp, crandom() * 2, dir_per2, dir_temp );

	VectorScale( dir_temp, VectorNormalize( dir_temp ), dir_temp );
	VectorScale( dir_temp, random() * 400 + 420, dir_temp );

	tracer = CG_AllocSprite( LE_EXPLOSION_TRACER, origin, 30, 7, 1, 1, 1, 1, 0, 0, 0, 0, CG_MediaShader( cgs.media.shaderSmokePuff3 ) );
	VectorCopy( dir_temp, tracer->velocity );
	VectorSet( tracer->accel, -0.2f, -0.2f, -9.8f * 170 );
	tracer->bounce = 50;
	tracer->ent.rotation = cg.time;
}

void CG_Dash( const entity_state_t *state ) {
	lentity_t *le;
	vec3_t pos, dvect, angle = { 0, 0, 0 };

	if( !( cg_cartoonEffects->integer & 4 ) ) {
		return;
	}

	// KoFFiE: Calculate angle based on relative position of the previous origin state of the player entity
	VectorSubtract( state->origin, cg_entities[state->number].prev.origin, dvect );

	// ugly inline define -> Ignore when difference between 2 positions was less than this value.
#define IGNORE_DASH 6.0

	if( ( dvect[0] > -IGNORE_DASH ) && ( dvect[0] < IGNORE_DASH ) &&
		( dvect[1] > -IGNORE_DASH ) && ( dvect[1] < IGNORE_DASH ) ) {
		return;
	}

	VecToAngles( dvect, angle );
	VectorCopy( state->origin, pos );
	angle[1] += 270; // Adjust angle
	pos[2] -= 24; // Adjust the position to ground height

	if( CG_PointContents( pos ) & MASK_WATER ) {
		return; // no smoke under water :)

	}
	le = CG_AllocModel( LE_DASH_SCALE, pos, angle, 7, //5
						1.0, 1.0, 1.0, 0.2f,
						0, 0, 0, 0,
						CG_MediaModel( cgs.media.modDash ),
						NULL
						);
	le->ent.scale = 0.01f;
	le->ent.axis[AXIS_UP + 2] *= 2.0f;
}

void CG_Explosion_Puff( const vec3_t pos, float radius, int frame ) {
	lentity_t *le;
	struct shader_s *shader = CG_MediaShader( cgs.media.shaderSmokePuff1 );
	vec3_t local_pos;

	switch( (int)floor( crandom() * 3.0f ) ) {
		case 0:
			shader = CG_MediaShader( cgs.media.shaderSmokePuff1 );
			break;
		case 1:
			shader = CG_MediaShader( cgs.media.shaderSmokePuff2 );
			break;
		case 2:
			shader = CG_MediaShader( cgs.media.shaderSmokePuff3 );
			break;
	}

	VectorCopy( pos, local_pos );
	local_pos[0] += crandom() * 4;
	local_pos[1] += crandom() * 4;
	local_pos[2] += crandom() * 4;

	le = CG_AllocSprite( LE_PUFF_SCALE, local_pos, radius, frame,
						 1.0f, 1.0f, 1.0f, 1.0f,
						 0, 0, 0, 0,
						 shader );
	le->ent.rotation = rand() % 360;
}

void CG_Explosion_Puff_2( const vec3_t pos, const vec3_t vel, int radius ) {

	lentity_t *le;
	struct shader_s *shader = CG_MediaShader( cgs.media.shaderSmokePuff3 );
	/*
	switch( (int)floor( crandom()*3.0f ) )
	{
	case 0:
	shader = CG_MediaShader( cgs.media.shaderSmokePuff1 );
	break;
	case 1:
	shader = CG_MediaShader( cgs.media.shaderSmokePuff2 );
	break;
	case 2:
	shader = CG_MediaShader( cgs.media.shaderSmokePuff3 );
	break;
	}
	*/
	if( radius == 0 ) {
		radius = (int)floor( 35.0f + crandom() * 5 );
	}

	le = CG_AllocSprite( LE_PUFF_SHRINK, pos, radius, 7,
						 1.0f, 1.0f, 1.0f, 0.2f,
						 0, 0, 0, 0,
						 shader );
	VectorCopy( vel, le->velocity );

	//le->ent.rotation = rand () % 360;
}

void CG_DustCircle( const vec3_t pos, const vec3_t dir, float radius, int count ) {
	vec3_t dir_per1;
	vec3_t dir_per2;
	vec3_t dir_temp = { 0.0f, 0.0f, 0.0f };
	int i;
	float angle;

	if( CG_PointContents( pos ) & MASK_WATER ) {
		return; // no smoke under water :)

	}
	PerpendicularVector( dir_per2, dir );
	CrossProduct( dir, dir_per2, dir_per1 );

	VectorScale( dir_per1, VectorNormalize( dir_per1 ), dir_per1 );
	VectorScale( dir_per2, VectorNormalize( dir_per2 ), dir_per2 );

	for( i = 0; i < count; i++ ) {
		angle = (float)( 6.2831f / count * i );
		VectorSet( dir_temp, 0.0f, 0.0f, 0.0f );
		VectorMA( dir_temp, sin( angle ), dir_per1, dir_temp );
		VectorMA( dir_temp, cos( angle ), dir_per2, dir_temp );

		//VectorScale(dir_temp, VectorNormalize(dir_temp),dir_temp );
		VectorScale( dir_temp, crandom() * 10 + radius, dir_temp );
		CG_Explosion_Puff_2( pos, dir_temp, 10 );
	}
}

void CG_ExplosionsDust( const vec3_t pos, const vec3_t dir, float radius ) {
	const int count = 32; /* Number of sprites used to create the circle */
	lentity_t *le;
	struct shader_s *shader = CG_MediaShader( cgs.media.shaderSmokePuff3 );

	vec3_t dir_per1;
	vec3_t dir_per2;
	vec3_t dir_temp = { 0.0f, 0.0f, 0.0f };
	int i;
	float angle;

	if( CG_PointContents( pos ) & MASK_WATER ) {
		return; // no smoke under water :)

	}
	PerpendicularVector( dir_per2, dir );
	CrossProduct( dir, dir_per2, dir_per1 );

	//VectorScale( dir_per1, VectorNormalize( dir_per1 ), dir_per1 );
	//VectorScale( dir_per2, VectorNormalize( dir_per2 ), dir_per2 );

	// make a circle out of the specified number (int count) of sprites
	for( i = 0; i < count; i++ ) {
		angle = (float)( 6.2831f / count * i );
		VectorSet( dir_temp, 0.0f, 0.0f, 0.0f );
		VectorMA( dir_temp, sin( angle ), dir_per1, dir_temp );
		VectorMA( dir_temp, cos( angle ), dir_per2, dir_temp );

		//VectorScale(dir_temp, VectorNormalize(dir_temp),dir_temp );
		VectorScale( dir_temp, crandom() * 8 + radius + 16.0f, dir_temp );

		// make the sprite smaller & alpha'd
		le = CG_AllocSprite( LE_ALPHA_FADE, pos, 10, 10,
							 1.0f, 1.0f, 1.0f, 1.0f,
							 0, 0, 0, 0,
							 shader );
		VectorCopy( dir_temp, le->velocity );
	}
}

void CG_SmallPileOfGibs( const vec3_t origin, int damage, const vec3_t initialVelocity, int team ) {
	lentity_t *le;
	int i, j, count;
	vec3_t angles, velocity;
	int time;

	if( !cg_gibs->integer ) {
		return;
	}

	time = 50;
	count = 14 + cg_gibs->integer; // 15 models minimum
	clamp( count, 15, 128 );

	for( i = 0; i < count; i++ ) {
		vec4_t color;

		// coloring
		switch( rand() % 3 ) {
			case 0:

				// orange
				Vector4Set( color, 1, 0.5, 0, 1 );
				break;
			case 1:

				// purple
				Vector4Set( color, 1, 0, 1, 1 );
				break;
			case 2:
			default:
				if( ( team == TEAM_ALPHA ) || ( team == TEAM_BETA ) ) {
					// team
					CG_TeamColor( team, color );
					for( j = 0; j < 3; j++ ) {
						color[j] = bound( 60.0f / 255.0f, color[j], 1.0f );
					}
				} else {
					// grey
					Vector4Set( color, 60.0f / 255.0f, 60.0f / 255.0f, 60.0f / 255.0f, 1.0f );
				}
				break;
		}

		le = CG_AllocModel( LE_ALPHA_FADE, origin, vec3_origin, time + time * random(),
							color[0], color[1], color[2], color[3],
							0, 0, 0, 0,
							CG_MediaModel( cgs.media.modIlluminatiGibs ),
							NULL );

		// random rotation and scale variations
		VectorSet( angles, crandom() * 360, crandom() * 360, crandom() * 360 );
		AnglesToAxis( angles, le->ent.axis );
		le->ent.scale = 0.8f - ( random() * 0.25 );
		le->ent.renderfx = RF_FULLBRIGHT | RF_NOSHADOW;

		velocity[0] = crandom() * 0.5;
		velocity[1] = crandom() * 0.5;
		velocity[2] = 0.5 + random() * 0.5; // always have upwards
		VectorNormalize( velocity );
		VectorScale( velocity, min( damage * 10, 300 ), velocity );

		velocity[0] += crandom() * bound( 0, damage, 150 );
		velocity[1] += crandom() * bound( 0, damage, 150 );
		velocity[2] += random() * bound( 0, damage, 250 );

		VectorAdd( initialVelocity, velocity, le->velocity );

		le->avelocity[0] = random() * 1200;
		le->avelocity[1] = random() * 1200;
		le->avelocity[2] = random() * 1200;

		//friction and gravity
		VectorSet( le->accel, -0.2f, -0.2f, -900 );

		le->bounce = 75;
	}
}

/*
* CG_AddLocalEntities
*/
void CG_AddLocalEntities( void ) {
#define FADEINFRAMES 2
	int f;
	lentity_t *le, *next, *hnode;
	entity_t *ent;
	float scale, frac, fade, time, scaleIn, fadeIn;
	float backlerp;
	vec3_t angles;

	time = (float)cg.frameTime * 0.001f;
	backlerp = 1.0f - cg.lerpfrac;

	hnode = &cg_localents_headnode;
	for( le = hnode->next; le != hnode; le = next ) {
		next = le->next;

		frac = ( cg.time - le->start ) * 0.01f;
		f = ( int )floor( frac );
		clamp_low( f, 0 );

		// it's time to DIE
		if( f >= le->frames - 1 ) {
			le->type = LE_FREE;
			CG_FreeLocalEntity( le );
			continue;
		}

		if( le->frames > 1 ) {
			scale = 1.0f - frac / ( le->frames - 1 );
			scale = bound( 0.0f, scale, 1.0f );
			fade = scale * 255.0f;

			// quick fade in, if time enough
			if( le->frames > FADEINFRAMES * 2 ) {
				scaleIn = frac / (float)FADEINFRAMES;
				clamp( scaleIn, 0.0f, 1.0f );
				fadeIn = scaleIn * 255.0f;
			} else {
				fadeIn = 255.0f;
			}
		} else {
			scale = 1.0f;
			fade = 255.0f;
			fadeIn = 255.0f;
		}

		ent = &le->ent;

		if( le->light && scale ) {
			CG_AddLightToScene( ent->origin, le->light * scale, le->lightcolor[0], le->lightcolor[1], le->lightcolor[2] );
		}

		if( le->type == LE_LASER ) {
			CG_QuickPolyBeam( ent->origin, ent->origin2, ent->radius, ent->customShader ); // wsw : jalfixme: missing the color (comes inside ent->skinnum)
			continue;
		}

		if( le->type == LE_DASH_SCALE ) {
			if( f < 1 ) {
				ent->scale = 0.15 * frac;
			} else {
				VecToAngles( &ent->axis[AXIS_RIGHT], angles );
				ent->axis[1 * 3 + 1] += 0.005f * sin( DEG2RAD( angles[YAW] ) ); //length
				ent->axis[1 * 3 + 0] += 0.005f * cos( DEG2RAD( angles[YAW] ) ); //length
				ent->axis[0 * 3 + 1] += 0.008f * cos( DEG2RAD( angles[YAW] ) ); //width
				ent->axis[0 * 3 + 0] -= 0.008f * sin( DEG2RAD( angles[YAW] ) ); //width
				ent->axis[2 * 3 + 2] -= 0.052f;              //height

				if( ent->axis[AXIS_UP + 2] <= 0 ) {
					le->type = LE_FREE;
					CG_FreeLocalEntity( le );
				}
			}
		}
		if( le->type == LE_PUFF_SCALE ) {
			if( le->frames - f < 4 ) {
				ent->scale = 1.0f - 1.0f * ( frac - abs( 4 - le->frames ) ) / 4;
			}
		}
		if( le->type == LE_PUFF_SHRINK ) {
			if( frac < 3 ) {
				ent->scale = 1.0f - 0.2f * frac / 4;
			} else {
				ent->scale = 0.8 - 0.8 * ( frac - 3 ) / 3;
				VectorScale( le->velocity, 0.85f, le->velocity );
			}
		}

		if( le->type == LE_EXPLOSION_TRACER ) {
			if( cg.time - ent->rotation > 10.0f ) {
				ent->rotation = cg.time;
				if( ent->radius - 16 * frac > 4 ) {
					CG_Explosion_Puff( ent->origin, ent->radius - 16 * frac, le->frames - f );
				}
			}
		}

		switch( le->type ) {
			case LE_NO_FADE:
				break;
			case LE_RGB_FADE:
				fade = min( fade, fadeIn );
				ent->shaderRGBA[0] = ( uint8_t )( fade * le->color[0] );
				ent->shaderRGBA[1] = ( uint8_t )( fade * le->color[1] );
				ent->shaderRGBA[2] = ( uint8_t )( fade * le->color[2] );
				break;
			case LE_SCALE_ALPHA_FADE:
				fade = min( fade, fadeIn );
				ent->scale = 1.0f + 1.0f / scale;
				ent->scale = min( ent->scale, 5.0f );
				ent->shaderRGBA[3] = ( uint8_t )( fade * le->color[3] );
				break;
			case LE_INVERSESCALE_ALPHA_FADE:
				fade = min( fade, fadeIn );
				ent->scale = scale + 0.1f;
				clamp( ent->scale, 0.1f, 1.0f );
				ent->shaderRGBA[3] = ( uint8_t )( fade * le->color[3] );
				break;
			case LE_ALPHA_FADE:
				fade = min( fade, fadeIn );
				ent->shaderRGBA[3] = ( uint8_t )( fade * le->color[3] );
				break;
			default:
				break;
		}

		ent->backlerp = backlerp;

		if( le->avelocity[0] || le->avelocity[1] || le->avelocity[2] ) {
			VectorMA( le->angles, time, le->avelocity, le->angles );
			AnglesToAxis( le->angles, le->ent.axis );
		}

		// apply rotational friction
		if( le->bounce ) { // FIXME?
			int i;
			const float adj = 100 * 6 * time; // magic constants here

			for( i = 0; i < 3; i++ ) {
				if( le->avelocity[i] > 0.0f ) {
					le->avelocity[i] -= adj;
					if( le->avelocity[i] < 0.0f ) {
						le->avelocity[i] = 0.0f;
					}
				} else if( le->avelocity[i] < 0.0f ) {
					le->avelocity[i] += adj;
					if( le->avelocity[i] > 0.0f ) {
						le->avelocity[i] = 0.0f;
					}
				}
			}
		}

		if( le->bounce ) {
			trace_t trace;
			vec3_t next_origin;

			VectorMA( ent->origin, time, le->velocity, next_origin );

			CG_Trace( &trace, ent->origin, debris_mins, debris_maxs, next_origin, 0, MASK_SOLID );

			// remove the particle when going out of the map
			if( ( trace.contents & CONTENTS_NODROP ) || ( trace.surfFlags & SURF_SKY ) ) {
				le->frames = 0;
			} else if( trace.fraction != 1.0 ) {   // found solid
				float dot;
				float xyzspeed, orig_xyzspeed;
				float bounce;

				orig_xyzspeed = VectorLength( le->velocity );

				// Reflect velocity
				dot = DotProduct( le->velocity, trace.plane.normal );
				VectorMA( le->velocity, -2.0f * dot, trace.plane.normal, le->velocity );

				//put new origin in the impact point, but move it out a bit along the normal
				VectorMA( trace.endpos, 1, trace.plane.normal, ent->origin );

				// make sure we don't gain speed from bouncing off
				bounce = 2.0f * le->bounce * 0.01f;
				if( bounce < 1.5f ) {
					bounce = 1.5f;
				}
				xyzspeed = orig_xyzspeed / bounce;

				VectorNormalize( le->velocity );
				VectorScale( le->velocity, xyzspeed, le->velocity );

				//the entity has not speed enough. Stop checks
				if( xyzspeed * time < 1.0f ) {
					trace_t traceground;
					vec3_t ground_origin;

					//see if we have ground
					VectorCopy( ent->origin, ground_origin );
					ground_origin[2] += ( debris_mins[2] - 4 );
					CG_Trace( &traceground, ent->origin, debris_mins, debris_maxs, ground_origin, 0, MASK_SOLID );
					if( traceground.fraction != 1.0 ) {
						le->bounce = 0;
						VectorClear( le->velocity );
						VectorClear( le->accel );
						VectorClear( le->avelocity );
						if( le->type == LE_EXPLOSION_TRACER ) {
							// blx
							le->type = LE_FREE;
							CG_FreeLocalEntity( le );
						}
					}
				}

			} else {
				VectorCopy( ent->origin, ent->origin2 );
				VectorCopy( next_origin, ent->origin );
			}
		} else {
			VectorCopy( ent->origin, ent->origin2 );
			VectorMA( ent->origin, time, le->velocity, ent->origin );
		}

		VectorCopy( ent->origin, ent->lightingOrigin );
		VectorMA( le->velocity, time, le->accel, le->velocity );

		CG_AddEntityToScene( ent );
	}
}

/*
* CG_FreeLocalEntities
*/
void CG_FreeLocalEntities( void ) {
	lentity_t *le, *next, *hnode;

	hnode = &cg_localents_headnode;
	for( le = hnode->next; le != hnode; le = next ) {
		next = le->next;

		le->type = LE_FREE;
		CG_FreeLocalEntity( le );
	}

	CG_ClearLocalEntities();
}
