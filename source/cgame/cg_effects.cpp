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

// cg_effects.c -- entity effects parsing and management

#include "cg_local.h"

/*
==============================================================

LIGHT STYLE MANAGEMENT

==============================================================
*/

typedef struct
{
	int length;
	float value[3];
	float map[MAX_QPATH];
} cg_lightStyle_t;

cg_lightStyle_t cg_lightStyle[MAX_LIGHTSTYLES];

/*
* CG_ClearLightStyles
*/
void CG_ClearLightStyles( void ) {
	memset( cg_lightStyle, 0, sizeof( cg_lightStyle ) );
}

/*
* CG_RunLightStyles
*/
void CG_RunLightStyles( void ) {
	int i;
	float f;
	int ofs;
	cg_lightStyle_t *ls;

	f = cg.time / 100.0f;
	ofs = (int)floor( f );
	f = f - ofs;

	for( i = 0, ls = cg_lightStyle; i < MAX_LIGHTSTYLES; i++, ls++ ) {
		if( !ls->length ) {
			ls->value[0] = ls->value[1] = ls->value[2] = 1.0;
			continue;
		}
		if( ls->length == 1 ) {
			ls->value[0] = ls->value[1] = ls->value[2] = ls->map[0];
		} else {
			ls->value[0] = ls->value[1] = ls->value[2] = ( ls->map[ofs % ls->length] * f + ( 1 - f ) * ls->map[( ofs - 1 ) % ls->length] );
		}
	}
}

/*
* CG_SetLightStyle
*/
void CG_SetLightStyle( int i ) {
	int j, k;
	char *s;

	s = cgs.configStrings[i + CS_LIGHTS];

	j = strlen( s );
	if( j >= MAX_QPATH ) {
		CG_Error( "CL_SetLightstyle length = %i", j );
	}
	cg_lightStyle[i].length = j;

	for( k = 0; k < j; k++ )
		cg_lightStyle[i].map[k] = (float)( s[k] - 'a' ) / (float)( 'm' - 'a' );
}

/*
* CG_AddLightStyles
*/
void CG_AddLightStyles( void ) {
	int i;
	cg_lightStyle_t *ls;

	for( i = 0, ls = cg_lightStyle; i < MAX_LIGHTSTYLES; i++, ls++ )
		trap_R_AddLightStyleToScene( i, ls->value[0], ls->value[1], ls->value[2] );
}

/*
==============================================================

DLIGHT MANAGEMENT

==============================================================
*/

typedef struct cdlight_s
{
	vec3_t color;
	vec3_t origin;
	float radius;
} cdlight_t;

static cdlight_t cg_dlights[MAX_DLIGHTS];
static int cg_numDlights;

/*
* CG_ClearDlights
*/
static void CG_ClearDlights( void ) {
	memset( cg_dlights, 0, sizeof( cg_dlights ) );
	cg_numDlights = 0;
}

/*
* CG_AllocDlight
*/
static void CG_AllocDlight( vec3_t origin, float radius, float r, float g, float b ) {
	cdlight_t *dl;

	if( radius <= 0 ) {
		return;
	}
	if( cg_numDlights == MAX_DLIGHTS ) {
		return;
	}

	dl = &cg_dlights[cg_numDlights++];
	dl->radius = radius;
	VectorCopy( origin, dl->origin );
	dl->color[0] = r;
	dl->color[1] = g;
	dl->color[2] = b;
}

void CG_AddLightToScene( vec3_t org, float radius, float r, float g, float b ) {
	CG_AllocDlight( org, radius, r, g, b );
}

/*
* CG_AddDlights
*/
void CG_AddDlights( void ) {
	int i;
	cdlight_t *dl;

	for( i = 0, dl = cg_dlights; i < cg_numDlights; i++, dl++ )
		trap_R_AddLightToScene( dl->origin, dl->radius, dl->color[0], dl->color[1], dl->color[2] );

	cg_numDlights = 0;
}

/*
==============================================================

BLOB SHADOWS MANAGEMENT

==============================================================
*/
#define MAX_CGSHADEBOXES 128

#define MAX_BLOBSHADOW_VERTS 128
#define MAX_BLOBSHADOW_FRAGMENTS 64

typedef struct
{
	vec3_t origin;
	vec3_t mins, maxs;
	int entNum;
	struct shader_s *shader;

	vec4_t verts[MAX_BLOBSHADOW_VERTS];
	vec4_t norms[MAX_BLOBSHADOW_VERTS];
	vec2_t stcoords[MAX_BLOBSHADOW_VERTS];
	byte_vec4_t colors[MAX_BLOBSHADOW_VERTS];
} cgshadebox_t;

cgshadebox_t cg_shadeBoxes[MAX_CGSHADEBOXES];
static int cg_numShadeBoxes = 0;   // cleared each frame

/*
* CG_AddBlobShadow
*
* Ok, to not use decals space we need these arrays to store the
* polygons info. We do not need the linked list nor registration
*/
static void CG_AddBlobShadow( vec3_t origin, vec3_t dir, float orient, float radius,
							  float r, float g, float b, float a, cgshadebox_t *shadeBox ) {
	int i, j, c, nverts;
	vec3_t axis[3];
	byte_vec4_t color;
	fragment_t *fr, fragments[MAX_BLOBSHADOW_FRAGMENTS];
	int numfragments;
	poly_t poly;
	vec4_t verts[MAX_BLOBSHADOW_VERTS];

	if( radius <= 0 || VectorCompare( dir, vec3_origin ) ) {
		return; // invalid

	}

	// calculate orientation matrix
	VectorNormalize2( dir, axis[0] );
	PerpendicularVector( axis[1], axis[0] );
	RotatePointAroundVector( axis[2], axis[0], axis[1], orient );
	CrossProduct( axis[0], axis[2], axis[1] );

	numfragments = trap_R_GetClippedFragments( origin, radius, axis, // clip it
											   MAX_BLOBSHADOW_VERTS, verts, MAX_BLOBSHADOW_FRAGMENTS, fragments );

	// no valid fragments
	if( !numfragments ) {
		return;
	}

	// clamp and scale colors
	if( r < 0 ) {
		r = 0;
	} else if( r > 1 ) {
		r = 255;
	} else {
		r *= 255;
	}
	if( g < 0 ) {
		g = 0;
	} else if( g > 1 ) {
		g = 255;
	} else {
		g *= 255;
	}
	if( b < 0 ) {
		b = 0;
	} else if( b > 1 ) {
		b = 255;
	} else {
		b *= 255;
	}
	if( a < 0 ) {
		a = 0;
	} else if( a > 1 ) {
		a = 255;
	} else {
		a *= 255;
	}

	color[0] = ( uint8_t )( r );
	color[1] = ( uint8_t )( g );
	color[2] = ( uint8_t )( b );
	color[3] = ( uint8_t )( a );
	c = *( int * )color;

	radius = 0.5f / radius;
	VectorScale( axis[1], radius, axis[1] );
	VectorScale( axis[2], radius, axis[2] );

	memset( &poly, 0, sizeof( poly ) );

	for( i = 0, nverts = 0, fr = fragments; i < numfragments; i++, fr++ ) {
		if( nverts + fr->numverts > MAX_BLOBSHADOW_VERTS ) {
			return;
		}
		if( fr->numverts <= 0 ) {
			continue;
		}

		poly.shader = shadeBox->shader;
		poly.verts = &shadeBox->verts[nverts];
		poly.normals = &shadeBox->norms[nverts];
		poly.stcoords = &shadeBox->stcoords[nverts];
		poly.colors = &shadeBox->colors[nverts];
		poly.numverts = fr->numverts;
		poly.fognum = fr->fognum;
		nverts += fr->numverts;

		for( j = 0; j < fr->numverts; j++ ) {
			vec3_t v;

			Vector4Copy( verts[fr->firstvert + j], poly.verts[j] );
			VectorCopy( axis[0], poly.normals[j] ); poly.normals[j][3] = 0;
			VectorSubtract( poly.verts[j], origin, v );
			poly.stcoords[j][0] = DotProduct( v, axis[1] ) + 0.5f;
			poly.stcoords[j][1] = DotProduct( v, axis[2] ) + 0.5f;
			*( int * )poly.colors[j] = c;
		}

		trap_R_AddPolyToScene( &poly );
	}
}

/*
* CG_ClearShadeBoxes
*/
static void CG_ClearShadeBoxes( void ) {
	cg_numShadeBoxes = 0;
	memset( cg_shadeBoxes, 0, sizeof( cg_shadeBoxes ) );
}

/*
* CG_AllocShadeBox
*/
void CG_AllocShadeBox( int entNum, const vec3_t origin, const vec3_t mins, const vec3_t maxs, struct shader_s *shader ) {
	float dist;
	vec3_t dir;
	cgshadebox_t *sb;

	if( !cg_shadows->integer ) {
		return;
	}
	if( cg_numShadeBoxes == MAX_CGSHADEBOXES ) {
		return;
	}

	// Kill if behind the view or if too far away
	VectorSubtract( origin, cg.view.origin, dir );
	dist = VectorNormalize2( dir, dir ) * cg.view.fracDistFOV;
	if( dist > 1024 ) {
		return;
	}

	if( DotProduct( dir, &cg.view.axis[AXIS_FORWARD] ) < 0 ) {
		return;
	}

	sb = &cg_shadeBoxes[cg_numShadeBoxes++];
	VectorCopy( origin, sb->origin );
	VectorCopy( mins, sb->mins );
	VectorCopy( maxs, sb->maxs );
	sb->entNum = entNum;
	sb->shader = shader;
	if( !sb->shader ) {
		sb->shader = CG_MediaShader( cgs.media.shaderPlayerShadow );
	}
}

/*
* CG_AddShadeBoxes - Which in reality means CalcBlobShadows
* Note:	This function should be called after every dynamic light has been added to the rendering list.
* ShadeBoxes exist for the solely reason of waiting until all dlights are sent before doing the shadows.
*/
#define SHADOW_PROJECTION_DISTANCE 96
#define SHADOW_MAX_SIZE 100
#define SHADOW_MIN_SIZE 24

void CG_AddShadeBoxes( void ) {
	// ok, what we have to do here is finding the light direction of each of the shadeboxes origins
	int i;
	cgshadebox_t *sb;
	vec3_t lightdir, end, sborigin;
	trace_t trace;

	if( !cg_shadows->integer ) {
		return;
	}

	for( i = 0, sb = cg_shadeBoxes; i < cg_numShadeBoxes; i++, sb++ ) {
		VectorClear( lightdir );
		trap_R_LightForOrigin( sb->origin, lightdir, NULL, NULL, RadiusFromBounds( sb->mins, sb->maxs ) );

		// move the point we will project close to the bottom of the bbox (so shadow doesn't dance much to the sides)
		VectorSet( sborigin, sb->origin[0], sb->origin[1], sb->origin[2] + sb->mins[2] + 8 );
		VectorMA( sborigin, -SHADOW_PROJECTION_DISTANCE, lightdir, end );

		//CG_DrawTestLine( sb->origin, end ); // lightdir testline
		CG_Trace( &trace, sborigin, vec3_origin, vec3_origin, end, sb->entNum, MASK_OPAQUE );
		if( trace.fraction < 1.0f ) { // we have a shadow
			float blobradius;
			float alpha, maxalpha = 0.95f;
			vec3_t shangles;

			VecToAngles( lightdir, shangles );
			blobradius = SHADOW_MIN_SIZE + trace.fraction * ( SHADOW_MAX_SIZE - SHADOW_MIN_SIZE );

			alpha = ( 1.0f - trace.fraction ) * maxalpha;

			CG_AddBlobShadow( trace.endpos, trace.plane.normal, shangles[YAW], blobradius,
							  1, 1, 1, alpha, sb );
		}
	}

	// clean up the polygons list from old frames
	cg_numShadeBoxes = 0;
}

/*
==============================================================

TEMPORARY (ONE-FRAME) DECALS

==============================================================
*/

#define MAX_TEMPDECALS              32      // in fact, a semi-random multiplier
#define MAX_TEMPDECAL_VERTS         128
#define MAX_TEMPDECAL_FRAGMENTS     64

static unsigned int cg_numDecalVerts = 0;

/*
* CG_ClearFragmentedDecals
*/
void CG_ClearFragmentedDecals( void ) {
	cg_numDecalVerts = 0;
}

/*
* CG_AddFragmentedDecal
*/
void CG_AddFragmentedDecal( vec3_t origin, vec3_t dir, float orient, float radius,
							float r, float g, float b, float a, struct shader_s *shader ) {
	int i, j, c;
	vec3_t axis[3];
	byte_vec4_t color;
	fragment_t *fr, fragments[MAX_TEMPDECAL_FRAGMENTS];
	int numfragments;
	poly_t poly;
	vec4_t verts[MAX_BLOBSHADOW_VERTS];
	static vec4_t t_verts[MAX_TEMPDECAL_VERTS * MAX_TEMPDECALS];
	static vec4_t t_norms[MAX_TEMPDECAL_VERTS * MAX_TEMPDECALS];
	static vec2_t t_stcoords[MAX_TEMPDECAL_VERTS * MAX_TEMPDECALS];
	static byte_vec4_t t_colors[MAX_TEMPDECAL_VERTS * MAX_TEMPDECALS];

	if( radius <= 0 || VectorCompare( dir, vec3_origin ) ) {
		return; // invalid

	}

	// calculate orientation matrix
	VectorNormalize2( dir, axis[0] );
	PerpendicularVector( axis[1], axis[0] );
	RotatePointAroundVector( axis[2], axis[0], axis[1], orient );
	CrossProduct( axis[0], axis[2], axis[1] );

	numfragments = trap_R_GetClippedFragments( origin, radius, axis, // clip it
											   MAX_BLOBSHADOW_VERTS, verts, MAX_TEMPDECAL_FRAGMENTS, fragments );

	// no valid fragments
	if( !numfragments ) {
		return;
	}

	// clamp and scale colors
	if( r < 0 ) {
		r = 0;
	} else if( r > 1 ) {
		r = 255;
	} else {
		r *= 255;
	}
	if( g < 0 ) {
		g = 0;
	} else if( g > 1 ) {
		g = 255;
	} else {
		g *= 255;
	}
	if( b < 0 ) {
		b = 0;
	} else if( b > 1 ) {
		b = 255;
	} else {
		b *= 255;
	}
	if( a < 0 ) {
		a = 0;
	} else if( a > 1 ) {
		a = 255;
	} else {
		a *= 255;
	}

	color[0] = ( uint8_t )( r );
	color[1] = ( uint8_t )( g );
	color[2] = ( uint8_t )( b );
	color[3] = ( uint8_t )( a );
	c = *( int * )color;

	radius = 0.5f / radius;
	VectorScale( axis[1], radius, axis[1] );
	VectorScale( axis[2], radius, axis[2] );

	memset( &poly, 0, sizeof( poly ) );

	for( i = 0, fr = fragments; i < numfragments; i++, fr++ ) {
		if( fr->numverts <= 0 ) {
			continue;
		}
		if( cg_numDecalVerts + (unsigned)fr->numverts > sizeof( t_verts ) / sizeof( t_verts[0] ) ) {
			return;
		}

		poly.shader = shader;
		poly.verts = &t_verts[cg_numDecalVerts];
		poly.normals = &t_norms[cg_numDecalVerts];
		poly.stcoords = &t_stcoords[cg_numDecalVerts];
		poly.colors = &t_colors[cg_numDecalVerts];
		poly.numverts = fr->numverts;
		poly.fognum = fr->fognum;
		cg_numDecalVerts += (unsigned)fr->numverts;

		for( j = 0; j < fr->numverts; j++ ) {
			vec3_t v;

			Vector4Copy( verts[fr->firstvert + j], poly.verts[j] );
			VectorCopy( axis[0], poly.normals[j] ); poly.normals[j][3] = 0;
			VectorSubtract( poly.verts[j], origin, v );
			poly.stcoords[j][0] = DotProduct( v, axis[1] ) + 0.5f;
			poly.stcoords[j][1] = DotProduct( v, axis[2] ) + 0.5f;
			*( int * )poly.colors[j] = c;
		}

		trap_R_AddPolyToScene( &poly );
	}
}

/*
==============================================================

PARTICLE MANAGEMENT

==============================================================
*/

typedef struct particle_s
{
	float time;

	vec3_t org;
	vec3_t vel;
	vec3_t accel;
	vec3_t color;
	float alpha;
	float alphavel;
	float scale;
	bool fog;

	poly_t poly;
	vec4_t pVerts[4];
	vec2_t pStcoords[4];
	byte_vec4_t pColor[4];

	struct shader_s *shader;
} cparticle_t;

#define PARTICLE_GRAVITY    500

#define MAX_PARTICLES       2048

static vec3_t avelocities[NUMVERTEXNORMALS];

cparticle_t particles[MAX_PARTICLES];
int cg_numparticles;

/*
* CG_ClearParticles
*/
static void CG_ClearParticles( void ) {
	int i;
	cparticle_t *p;

	cg_numparticles = 0;
	memset( particles, 0, sizeof( cparticle_t ) * MAX_PARTICLES );

	for( i = 0, p = particles; i < MAX_PARTICLES; i++, p++ ) {
		p->pStcoords[0][0] = 0; p->pStcoords[0][1] = 1;
		p->pStcoords[1][0] = 0; p->pStcoords[1][1] = 0;
		p->pStcoords[2][0] = 1; p->pStcoords[2][1] = 0;
		p->pStcoords[3][0] = 1; p->pStcoords[3][1] = 1;
	}
}

#define CG_InitParticle( p, s, a, r, g, b, h ) \
	( \
		( p )->time = cg.time, \
		( p )->scale = ( s ), \
		( p )->alpha = ( a ), \
		( p )->color[0] = ( r ), \
		( p )->color[1] = ( g ), \
		( p )->color[2] = ( b ), \
		( p )->shader = ( h ), \
		( p )->fog = true \
	)

/*
* CG_ParticleEffect
*
* Wall impact puffs
*/
void CG_ParticleEffect( const vec3_t org, const vec3_t dir, float r, float g, float b, int count, float gravity ) {
	int j;
	cparticle_t *p;
	float d;

	if( !cg_particles->integer ) {
		return;
	}

	// Check for the default argument value
	if( isnan( gravity ) ) {
		gravity = -PARTICLE_GRAVITY;
	}

	if( cg_numparticles + count > MAX_PARTICLES ) {
		count = MAX_PARTICLES - cg_numparticles;
	}
	for( p = &particles[cg_numparticles], cg_numparticles += count; count > 0; count--, p++ ) {
		CG_InitParticle( p, 0.75, 1, r + random() * 0.1, g + random() * 0.1, b + random() * 0.1, NULL );

		d = rand() & 31;
		for( j = 0; j < 3; j++ ) {
			p->org[j] = org[j] + ( ( rand() & 7 ) - 4 ) + d * dir[j];
			p->vel[j] = crandom() * 20;
		}

		p->accel[0] = p->accel[1] = 0;
		p->accel[2] = gravity;
		p->alphavel = -1.0 / ( 0.5 + random() * 0.3 );
	}
}

/*
* CG_ParticleEffect2
*/
void CG_ParticleEffect2( const vec3_t org, const vec3_t dir, float r, float g, float b, int count, float gravity ) {
	int j;
	float d;
	cparticle_t *p;

	if( !cg_particles->integer ) {
		return;
	}

	// Check for the default argument value
	if( isnan( gravity ) ) {
		gravity = -PARTICLE_GRAVITY;
	}

	if( cg_numparticles + count > MAX_PARTICLES ) {
		count = MAX_PARTICLES - cg_numparticles;
	}
	for( p = &particles[cg_numparticles], cg_numparticles += count; count > 0; count--, p++ ) {
		CG_InitParticle( p, 0.75, 1, r, g, b, NULL );

		d = rand() & 7;
		for( j = 0; j < 3; j++ ) {
			p->org[j] = org[j] + ( ( rand() & 7 ) - 4 ) + d * dir[j];
			p->vel[j] = crandom() * 20;
		}

		p->accel[0] = p->accel[1] = 0;
		p->accel[2] = gravity;
		p->alphavel = -1.0 / ( 0.5 + random() * 0.3 );
	}
}

/*
* CG_ParticleExplosionEffect
*/
void CG_ParticleExplosionEffect( const vec3_t org, const vec3_t dir, float r, float g, float b, int count, float gravity ) {
	int j;
	cparticle_t *p;
	float d;

	if( !cg_particles->integer ) {
		return;
	}

	// Check for the default argument value
	if( isnan( gravity ) ) {
		gravity = -PARTICLE_GRAVITY;
	}

	if( cg_numparticles + count > MAX_PARTICLES ) {
		count = MAX_PARTICLES - cg_numparticles;
	}
	for( p = &particles[cg_numparticles], cg_numparticles += count; count > 0; count--, p++ ) {
		CG_InitParticle( p, 0.75, 1, r + random() * 0.1, g + random() * 0.1, b + random() * 0.1, NULL );

		d = rand() & 31;
		for( j = 0; j < 3; j++ ) {
			p->org[j] = org[j] + ( ( rand() & 7 ) - 4 ) + d * dir[j];
			p->vel[j] = crandom() * 400;
		}

		//p->accel[0] = p->accel[1] = 0;
		p->accel[2] = gravity;
		p->alphavel = -1.0 / ( 0.7 + random() * 0.25 );
	}
}

/*
* CG_ElectroWeakTrail
*/
void CG_ElectroWeakTrail( const vec3_t start, const vec3_t end, const vec4_t color ) {
	int j, count;
	vec3_t move, vec;
	float len;
	const float dec = 5;
	cparticle_t *p;
	vec4_t ucolor = { 1.0f, 1.0f, 1.0f, 0.8f };

	if( color ) {
		VectorCopy( color, ucolor );
	}

	// Always draw projectile EB trail. Otherwise it is almost impossible to see the projectile.
#if 0
	if( !cg_particles->integer ) {
		return;
	}
#endif

	VectorCopy( start, move );
	VectorSubtract( end, start, vec );
	len = VectorNormalize( vec );
	VectorScale( vec, dec, vec );

	count = (int)( len / dec ) + 1;
	if( cg_numparticles + count > MAX_PARTICLES ) {
		count = MAX_PARTICLES - cg_numparticles;
	}

	for( p = &particles[cg_numparticles], cg_numparticles += count; count > 0; count--, p++ ) {
		//CG_InitParticle( p, 2.0f, 0.8f, 1.0f, 1.0f, 1.0f, NULL );
		CG_InitParticle( p, 2.0f, ucolor[3], ucolor[0], ucolor[1], ucolor[2], NULL );

		p->alphavel = -1.0 / ( 0.2 + random() * 0.1 );
		for( j = 0; j < 3; j++ ) {
			p->org[j] = move[j] + random();/* + crandom();*/
			p->vel[j] = crandom() * 2;
		}

		VectorClear( p->accel );
		VectorAdd( move, vec, move );
	}
}

/*
* CG_ImpactPuffParticles
* Wall impact puffs
*/
void CG_ImpactPuffParticles( const vec3_t org, const vec3_t dir, int count, float scale, float r, float g, float b, float a, struct shader_s *shader ) {
	int j;
	float d;
	cparticle_t *p;

	if( !cg_particles->integer ) {
		return;
	}

	if( cg_numparticles + count > MAX_PARTICLES ) {
		count = MAX_PARTICLES - cg_numparticles;
	}
	for( p = &particles[cg_numparticles], cg_numparticles += count; count > 0; count--, p++ ) {
		CG_InitParticle( p, scale, a, r, g, b, shader );

		d = rand() & 15;
		for( j = 0; j < 3; j++ ) {
			p->org[j] = org[j] + ( ( rand() & 7 ) - 4 ) + d * dir[j];
			p->vel[j] = dir[j] * 90 + crandom() * 40;
		}

		p->accel[0] = p->accel[1] = 0;
		p->accel[2] = -PARTICLE_GRAVITY;
		p->alphavel = -1.0 / ( 0.5 + random() * 0.3 );
	}
}

/*
* CG_HighVelImpactPuffParticles
* High velocity wall impact puffs
*/
void CG_HighVelImpactPuffParticles( const vec3_t org, const vec3_t dir, int count, float scale, float r, float g, float b, float a, struct shader_s *shader ) {
	int j;
	float d;
	cparticle_t *p;

	if( !cg_particles->integer ) {
		return;
	}

	if( cg_numparticles + count > MAX_PARTICLES ) {
		count = MAX_PARTICLES - cg_numparticles;
	}
	for( p = &particles[cg_numparticles], cg_numparticles += count; count > 0; count--, p++ ) {
		CG_InitParticle( p, scale, a, r, g, b, shader );

		d = rand() & 15;
		for( j = 0; j < 3; j++ ) {
			p->org[j] = org[j] + ( ( rand() & 7 ) - 4 ) + d * dir[j];
			p->vel[j] = dir[j] * 180 + crandom() * 40;
		}

		p->accel[0] = p->accel[1] = 0;
		p->accel[2] = -PARTICLE_GRAVITY * 2;
		p->alphavel = -5.0 / ( 0.5 + random() * 0.3 );
	}
}

/*
* CG_ElectroIonsTrail
*/
void CG_ElectroIonsTrail( const vec3_t start, const vec3_t end, const vec4_t color ) {
#define MAX_BOLT_IONS 48
	int i, count;
	vec3_t move, vec;
	float len;
	float dec2 = 24.0f;
	cparticle_t *p;

	if( !cg_particles->integer ) {
		return;
	}

	VectorSubtract( end, start, vec );
	len = VectorNormalize( vec );
	count = (int)( len / dec2 ) + 1;
	if( count > MAX_BOLT_IONS ) {
		count = MAX_BOLT_IONS;
		dec2 = len / count;
	}

	VectorScale( vec, dec2, vec );
	VectorCopy( start, move );

	if( cg_numparticles + count > MAX_PARTICLES ) {
		count = MAX_PARTICLES - cg_numparticles;
	}
	for( p = &particles[cg_numparticles], cg_numparticles += count; count > 0; count--, p++ ) {
		CG_InitParticle( p, 0.65f, color[3], color[0] + crandom() * 0.1, color[1] + crandom() * 0.1, color[2] + crandom() * 0.1, NULL );

		for( i = 0; i < 3; i++ ) {
			p->org[i] = move[i];
			p->vel[i] = crandom() * 4;
		}
		p->alphavel = -1.0 / ( 0.6 + random() * 0.6 );
		VectorClear( p->accel );
		VectorAdd( move, vec, move );
	}
}

void CG_ElectroIonsTrail2( const vec3_t start, const vec3_t end, const vec4_t color ) {
#define MAX_RING_IONS 96
	int count;
	vec3_t move, vec;
	float len;
	float dec2 = 8.0f;
	cparticle_t *p;

	if( !cg_particles->integer ) {
		return;
	}

	VectorSubtract( end, start, vec );
	len = VectorNormalize( vec );
	count = (int)( len / dec2 ) + 1;
	if( count > MAX_RING_IONS ) {
		count = MAX_RING_IONS;
		dec2 = len / count;
	}

	VectorScale( vec, dec2, vec );
	VectorCopy( start, move );

	if( cg_numparticles + count > MAX_PARTICLES ) {
		count = MAX_PARTICLES - cg_numparticles;
	}

	// Ring rail eb particles
	for( p = &particles[cg_numparticles], cg_numparticles += count; count > 0; count--, p++ ) {
		CG_InitParticle( p, 0.65f, color[3], color[0] + crandom() * 0.1, color[1] + crandom() * 0.1, color[2] + crandom() * 0.1, NULL );

		p->alphavel = -1.0 / ( 0.6 + random() * 0.6 );

		VectorCopy( move, p->org );
		VectorClear( p->accel );
		VectorClear( p->vel );
		VectorAdd( move, vec, move );
	}
}

#define BEAMLENGTH      16

/*
* CG_FlyParticles
*/
static void CG_FlyParticles( const vec3_t origin, int count ) {
	int i, j;
	float angle, sp, sy, cp, cy;
	vec3_t forward, dir;
	float dist, ltime;
	cparticle_t *p;

	if( !cg_particles->integer ) {
		return;
	}

	if( count > NUMVERTEXNORMALS ) {
		count = NUMVERTEXNORMALS;
	}

	if( !avelocities[0][0] ) {
		for( i = 0; i < NUMVERTEXNORMALS; i++ )
			for( j = 0; j < 3; j++ )
				avelocities[i][j] = ( rand() & 255 ) * 0.01;
	}

	i = 0;
	ltime = (float)cg.time / 1000.0;

	count /= 2;
	if( cg_numparticles + count > MAX_PARTICLES ) {
		count = MAX_PARTICLES - cg_numparticles;
	}
	for( p = &particles[cg_numparticles], cg_numparticles += count; count > 0; count--, p++ ) {
		CG_InitParticle( p, 1, 1, 0, 0, 0, NULL );

		angle = ltime * avelocities[i][0];
		sy = sin( angle );
		cy = cos( angle );
		angle = ltime * avelocities[i][1];
		sp = sin( angle );
		cp = cos( angle );

		forward[0] = cp * cy;
		forward[1] = cp * sy;
		forward[2] = -sp;

		dist = sin( ltime + i ) * 64;
		ByteToDir( i, dir );
		p->org[0] = origin[0] + dir[0] * dist + forward[0] * BEAMLENGTH;
		p->org[1] = origin[1] + dir[1] * dist + forward[1] * BEAMLENGTH;
		p->org[2] = origin[2] + dir[2] * dist + forward[2] * BEAMLENGTH;

		VectorClear( p->vel );
		VectorClear( p->accel );
		p->alphavel = -100;

		i += 2;
	}
}

/*
* CG_FlyEffect
*/
void CG_FlyEffect( centity_t *ent, const vec3_t origin ) {
	int n;
	int count;
	int starttime;

	if( !cg_particles->integer ) {
		return;
	}

	if( ent->fly_stoptime < cg.time ) {
		starttime = cg.time;
		ent->fly_stoptime = cg.time + 60000;
	} else {
		starttime = ent->fly_stoptime - 60000;
	}

	n = cg.time - starttime;
	if( n < 20000 ) {
		count = n * 162 / 20000.0;
	} else {
		n = ent->fly_stoptime - cg.time;
		if( n < 20000 ) {
			count = n * 162 / 20000.0;
		} else {
			count = 162;
		}
	}

	CG_FlyParticles( origin, count );
}

/*
* CG_AddParticles
*/
void CG_AddParticles( void ) {
	int i, j, k;
	float alpha;
	float time, time2;
	vec3_t org;
	vec3_t corner;
	byte_vec4_t color;
	int maxparticle, activeparticles;
	float alphaValues[MAX_PARTICLES];
	cparticle_t *p, *free_particles[MAX_PARTICLES];

	if( !cg_numparticles ) {
		return;
	}

	j = 0;
	maxparticle = -1;
	activeparticles = 0;

	for( i = 0, p = particles; i < cg_numparticles; i++, p++ ) {
		time = ( cg.time - p->time ) * 0.001f;
		alpha = alphaValues[i] = p->alpha + time * p->alphavel;

		if( alpha <= 0 ) { // faded out
			free_particles[j++] = p;
			continue;
		}

		maxparticle = i;
		activeparticles++;

		time2 = time * time * 0.5f;

		org[0] = p->org[0] + p->vel[0] * time + p->accel[0] * time2;
		org[1] = p->org[1] + p->vel[1] * time + p->accel[1] * time2;
		org[2] = p->org[2] + p->vel[2] * time + p->accel[2] * time2;

		color[0] = (uint8_t)( bound( 0, p->color[0], 1.0f ) * 255 );
		color[1] = (uint8_t)( bound( 0, p->color[1], 1.0f ) * 255 );
		color[2] = (uint8_t)( bound( 0, p->color[2], 1.0f ) * 255 );
		color[3] = (uint8_t)( bound( 0, alpha, 1.0f ) * 255 );

		corner[0] = org[0];
		corner[1] = org[1] - 0.5f * p->scale;
		corner[2] = org[2] - 0.5f * p->scale;

		Vector4Set( p->pVerts[0], corner[0], corner[1] + p->scale, corner[2] + p->scale, 1 );
		Vector4Set( p->pVerts[1], corner[0], corner[1], corner[2] + p->scale, 1 );
		Vector4Set( p->pVerts[2], corner[0], corner[1], corner[2], 1 );
		Vector4Set( p->pVerts[3], corner[0], corner[1] + p->scale, corner[2], 1 );
		for( k = 0; k < 4; k++ ) {
			Vector4Copy( color, p->pColor[k] );
		}

		p->poly.numverts = 4;
		p->poly.verts = p->pVerts;
		p->poly.stcoords = p->pStcoords;
		p->poly.colors = p->pColor;
		p->poly.fognum = p->fog ? 0 : -1;
		p->poly.shader = ( p->shader == NULL ) ? CG_MediaShader( cgs.media.shaderParticle ) : p->shader;

		trap_R_AddPolyToScene( &p->poly );
	}

	i = 0;
	while( maxparticle >= activeparticles ) {
		*free_particles[i++] = particles[maxparticle--];

		while( maxparticle >= activeparticles ) {
			if( alphaValues[maxparticle] <= 0 ) {
				maxparticle--;
			} else {
				break;
			}
		}
	}

	cg_numparticles = activeparticles;
}

/*
* CG_ClearEffects
*/
void CG_ClearEffects( void ) {
	CG_ClearFragmentedDecals();
	CG_ClearParticles();
	CG_ClearDlights();
	CG_ClearShadeBoxes();
}
