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
// cmodel_trace.c

#include "qcommon.h"
#include "cm_local.h"
#include "cm_trace.h"

static inline void CM_SetBuiltinBrushBounds( vec_bounds_t mins, vec_bounds_t maxs ) {
	for( int i = 0; i < sizeof( vec_bounds_t ) / sizeof( vec_t ); ++i ) {
		mins[i] = +999999;
		maxs[i] = -999999;
	}
}

static CMGenericTraceComputer genericTraceComputer;
static CMSse42TraceComputer sse42TraceComputer;

static CMTraceComputer *selectedTraceComputer = nullptr;

struct CMTraceComputer *CM_GetTraceComputer( cmodel_state_t *cms ) {
	// This is mostly to avoid annoying console spam on every map loading
	if( selectedTraceComputer ) {
		// Just set the appropriate cms pointer
		// (the selected computer once it's selected remains the same during the entire executable lifetime).
		// Warning: If different cms instances (e.g. cloned ones)
		// are used simultaneously, this is plain wrong in environment of that kind.
		// Instantiate separate trace computer instances in such cases.
		selectedTraceComputer->cms = cms;
		return selectedTraceComputer;
	}

	if( COM_CPUFeatures() & QF_CPU_FEATURE_SSE42 ) {
		Com_Printf( "SSE4.2 instructions are supported. An optimized collision code will be used\n" );
		selectedTraceComputer = &sse42TraceComputer;
	} else {
		Com_Printf( "SSE4.2 instructions support has not been found. A generic collision code will be used\n" );
		selectedTraceComputer = &genericTraceComputer;
	}

	selectedTraceComputer->cms = cms;
	return selectedTraceComputer;
}

/*
* CM_InitBoxHull
*
* Set up the planes so that the six floats of a bounding box
* can just be stored out and get a proper clipping hull structure.
*/
extern "C" void CM_InitBoxHull( cmodel_state_t *cms ) {
	cms->box_brush->numsides = 6;
	cms->box_brush->brushsides = cms->box_brushsides;
	cms->box_brush->contents = CONTENTS_BODY;

	// Make sure CM_CollideBox() will not reject the brush by its bounds
	CM_SetBuiltinBrushBounds( cms->box_brush->maxs, cms->box_brush->mins );

	cms->box_markbrushes[0] = cms->box_brush;

	cms->box_cmodel->builtin = true;
	cms->box_cmodel->nummarkfaces = 0;
	cms->box_cmodel->markfaces = NULL;
	cms->box_cmodel->markbrushes = cms->box_markbrushes;
	cms->box_cmodel->nummarkbrushes = 1;

	for( int i = 0; i < 6; i++ ) {
		// brush sides
		cbrushside_t *s = cms->box_brushsides + i;

		// planes
		cplane_t tmp, *p = &tmp;
		VectorClear( p->normal );

		if( ( i & 1 ) ) {
			p->type = PLANE_NONAXIAL;
			p->normal[i >> 1] = -1;
			p->signbits = ( 1 << ( i >> 1 ) );
		} else {
			p->type = i >> 1;
			p->normal[i >> 1] = 1;
			p->signbits = 0;
		}

		p->dist = 0;
		CM_CopyRawToCMPlane( p, &s->plane );
		s->surfFlags = 0;
	}
}

/*
* CM_InitOctagonHull
*
* Set up the planes so that the six floats of a bounding box
* can just be stored out and get a proper clipping hull structure.
*/
extern "C" void CM_InitOctagonHull( cmodel_state_t *cms ) {
	const vec3_t oct_dirs[4] = {
		{  1,  1, 0 },
		{ -1,  1, 0 },
		{ -1, -1, 0 },
		{  1, -1, 0 }
	};

	cms->oct_brush->numsides = 10;
	cms->oct_brush->brushsides = cms->oct_brushsides;
	cms->oct_brush->contents = CONTENTS_BODY;

	// Make sure CM_CollideBox() will not reject the brush by its bounds
	CM_SetBuiltinBrushBounds( cms->oct_brush->maxs, cms->oct_brush->mins );

	cms->oct_markbrushes[0] = cms->oct_brush;

	cms->oct_cmodel->builtin = true;
	cms->oct_cmodel->nummarkfaces = 0;
	cms->oct_cmodel->markfaces = NULL;
	cms->oct_cmodel->markbrushes = cms->oct_markbrushes;
	cms->oct_cmodel->nummarkbrushes = 1;

	// axial planes
	for( int i = 0; i < 6; i++ ) {
		// brush sides
		cbrushside_t *s = cms->oct_brushsides + i;

		// planes
		cplane_t tmp, *p = &tmp;
		VectorClear( p->normal );

		if( ( i & 1 ) ) {
			p->type = PLANE_NONAXIAL;
			p->normal[i >> 1] = -1;
			p->signbits = ( 1 << ( i >> 1 ) );
		} else {
			p->type = i >> 1;
			p->normal[i >> 1] = 1;
			p->signbits = 0;
		}

		p->dist = 0;
		CM_CopyRawToCMPlane( p, &s->plane );
		s->surfFlags = 0;
	}

	// non-axial planes
	for( int i = 6; i < 10; i++ ) {
		// brush sides
		cbrushside_t *s = cms->oct_brushsides + i;

		// planes
		cplane_t tmp, *p = &tmp;
		VectorCopy( oct_dirs[i - 6], p->normal );

		p->type = PLANE_NONAXIAL;
		p->signbits = SignbitsForPlane( p );

		p->dist = 0;
		CM_CopyRawToCMPlane( p, &s->plane );
		s->surfFlags = 0;
	}
}

/*
* CM_ModelForBBox
*
* To keep everything totally uniform, bounding boxes are turned into inline models
*/
extern "C" cmodel_t *CM_ModelForBBox( cmodel_state_t *cms, vec3_t mins, vec3_t maxs ) {
	cbrushside_t *sides = cms->box_brush->brushsides;
	sides[0].plane.dist = maxs[0];
	sides[1].plane.dist = -mins[0];
	sides[2].plane.dist = maxs[1];
	sides[3].plane.dist = -mins[1];
	sides[4].plane.dist = maxs[2];
	sides[5].plane.dist = -mins[2];

	VectorCopy( mins, cms->box_cmodel->mins );
	VectorCopy( maxs, cms->box_cmodel->maxs );

	return cms->box_cmodel;
}

/*
* CM_OctagonModelForBBox
*
* Same as CM_ModelForBBox with 4 additional planes at corners.
* Internally offset to be symmetric on all sides.
*/
extern "C" cmodel_t *CM_OctagonModelForBBox( cmodel_state_t *cms, vec3_t mins, vec3_t maxs ) {
	int i;
	float a, b, d, t;
	float sina, cosa;
	vec3_t offset, size[2];

	for( i = 0; i < 3; i++ ) {
		offset[i] = ( mins[i] + maxs[i] ) * 0.5;
		size[0][i] = mins[i] - offset[i];
		size[1][i] = maxs[i] - offset[i];
	}

	VectorCopy( offset, cms->oct_cmodel->cyl_offset );
	VectorCopy( size[0], cms->oct_cmodel->mins );
	VectorCopy( size[1], cms->oct_cmodel->maxs );

	cbrushside_t *sides = cms->oct_brush->brushsides;
	sides[0].plane.dist = size[1][0];
	sides[1].plane.dist = -size[0][0];
	sides[2].plane.dist = size[1][1];
	sides[3].plane.dist = -size[0][1];
	sides[4].plane.dist = size[1][2];
	sides[5].plane.dist = -size[0][2];

	a = size[1][0]; // halfx
	b = size[1][1]; // halfy
	d = sqrt( a * a + b * b ); // hypothenuse

	cosa = a / d;
	sina = b / d;

	// swap sin and cos, which is the same thing as adding pi/2 radians to the original angle
	t = sina;
	sina = cosa;
	cosa = t;

	// elleptical radius
	d = a * b / sqrt( a * a * cosa * cosa + b * b * sina * sina );
	//d = a * b / sqrt( a * a  + b * b ); // produces a rectangle, inscribed at middle points

	// the following should match normals and signbits set in CM_InitOctagonHull

	VectorSet( sides[6].plane.normal, cosa, sina, 0 );
	sides[6].plane.dist = d;

	VectorSet( sides[7].plane.normal, -cosa, sina, 0 );
	sides[7].plane.dist = d;

	VectorSet( sides[8].plane.normal, -cosa, -sina, 0 );
	sides[8].plane.dist = d;

	VectorSet( sides[9].plane.normal, cosa, -sina, 0 );
	sides[9].plane.dist = d;

	return cms->oct_cmodel;
}

/*
===============================================================================

BOX TRACING

===============================================================================
*/

void CMTraceComputer::ClipBoxToBrush( CMTraceContext *tlc, cbrush_t *brush ) {
	cm_plane_t *clipplane;
	cbrushside_t *side, *leadside;
	float d1, d2, f;

	if( !brush->numsides ) {
		return;
	}

	float enterfrac = -1;
	float leavefrac = 1;
	clipplane = NULL;

	bool getout = false;
	bool startout = false;
	leadside = NULL;
	side = brush->brushsides;

	const float *startmins = tlc->startmins;
	const float *endmins = tlc->endmins;

	const float *endmaxs = tlc->endmaxs;
	const float *startmaxs = tlc->startmaxs;

	for( int i = 0, end = brush->numsides; i < end; i++, side++ ) {
		cm_plane_t *p = &side->plane;
		int type = p->type;
		float dist = p->dist;

		// push the plane out apropriately for mins/maxs
		if( type < 3 ) {
			d1 = startmins[type] - dist;
			d2 = endmins[type] - dist;
		} else {
			// It has been proven that using a switch is cheaper than using a LUT like in SIMD approach
			const float *normal = p->normal;
			switch( p->signbits & 7 ) {
				case 0:
					d1 = normal[0] * startmins[0] + normal[1] * startmins[1] + normal[2] * startmins[2] - dist;
					d2 = normal[0] * endmins[0] + normal[1] * endmins[1] + normal[2] * endmins[2] - dist;
					break;
				case 1:
					d1 = normal[0] * startmaxs[0] + normal[1] * startmins[1] + normal[2] * startmins[2] - dist;
					d2 = normal[0] * endmaxs[0] + normal[1] * endmins[1] + normal[2] * endmins[2] - dist;
					break;
				case 2:
					d1 = normal[0] * startmins[0] + normal[1] * startmaxs[1] + normal[2] * startmins[2] - dist;
					d2 = normal[0] * endmins[0] + normal[1] * endmaxs[1] + normal[2] * endmins[2] - dist;
					break;
				case 3:
					d1 = normal[0] * startmaxs[0] + normal[1] * startmaxs[1] + normal[2] * startmins[2] - dist;
					d2 = normal[0] * endmaxs[0] + normal[1] * endmaxs[1] + normal[2] * endmins[2] - dist;
					break;
				case 4:
					d1 = normal[0] * startmins[0] + normal[1] * startmins[1] + normal[2] * startmaxs[2] - dist;
					d2 = normal[0] * endmins[0] + normal[1] * endmins[1] + normal[2] * endmaxs[2] - dist;
					break;
				case 5:
					d1 = normal[0] * startmaxs[0] + normal[1] * startmins[1] + normal[2] * startmaxs[2] - dist;
					d2 = normal[0] * endmaxs[0] + normal[1] * endmins[1] + normal[2] * endmaxs[2] - dist;
					break;
				case 6:
					d1 = normal[0] * startmins[0] + normal[1] * startmaxs[1] + normal[2] * startmaxs[2] - dist;
					d2 = normal[0] * endmins[0] + normal[1] * endmaxs[1] + normal[2] * endmaxs[2] - dist;
					break;
				case 7:
					d1 = normal[0] * startmaxs[0] + normal[1] * startmaxs[1] + normal[2] * startmaxs[2] - dist;
					d2 = normal[0] * endmaxs[0] + normal[1] * endmaxs[1] + normal[2] * endmaxs[2] - dist;
					break;
				default:
					d1 = d2 = 0; // shut up compiler
					assert( 0 );
					break;
			}
		}

		if( d2 > 0 ) {
			getout = true; // endpoint is not in solid
		}
		if( d1 > 0 ) {
			startout = true;
		}

		// if completely in front of face, no intersection
		if( d1 > 0 && d2 >= d1 ) {
			return;
		}
		if( d1 <= 0 && d2 <= 0 ) {
			continue;
		}
		// crosses face
		f = d1 - d2;
		if( f > 0 ) {   // enter
			f = ( d1 - DIST_EPSILON ) / f;
			if( f > enterfrac ) {
				enterfrac = f;
				clipplane = p;
				leadside = side;
			}
		} else if( f < 0 ) {   // leave
			f = ( d1 + DIST_EPSILON ) / f;
			if( f < leavefrac ) {
				leavefrac = f;
			}
		}
	}

	if( !startout ) {
		// original point was inside brush
		tlc->trace->startsolid = true;
		tlc->trace->contents = brush->contents;
		if( !getout ) {
			tlc->trace->allsolid = true;
			tlc->trace->fraction = 0;
		}
		return;
	}
	if( enterfrac - ( 1.0f / 1024.0f ) <= leavefrac ) {
		if( enterfrac > -1 && enterfrac < tlc->trace->fraction ) {
			if( enterfrac < 0 ) {
				enterfrac = 0;
			}
			tlc->trace->fraction = enterfrac;
			CM_CopyCMToRawPlane( clipplane, &tlc->trace->plane );
			tlc->trace->surfFlags = leadside->surfFlags;
			tlc->trace->contents = brush->contents;
		}
	}
}

void CMTraceComputer::TestBoxInBrush( CMTraceContext *tlc, cbrush_t *brush ) {
	int i;
	cm_plane_t *p;
	cbrushside_t *side;

	if( !brush->numsides ) {
		return;
	}

	const float *startmins = tlc->startmins;
	const float *startmaxs = tlc->startmaxs;

	side = brush->brushsides;
	for( i = 0; i < brush->numsides; i++, side++ ) {
		p = &side->plane;
		int type = p->type;

		// push the plane out appropriately for mins/maxs
		// if completely in front of face, no intersection
		if( type < 3 ) {
			if( startmins[type] > p->dist ) {
				return;
			}
		} else {
			const float *normal = p->normal;
			switch( p->signbits & 7 ) {
				case 0:
					if( normal[0] * startmins[0] + normal[1] * startmins[1] + normal[2] * startmins[2] > p->dist ) {
						return;
					}
					break;
				case 1:
					if( normal[0] * startmaxs[0] + normal[1] * startmins[1] + normal[2] * startmins[2] > p->dist ) {
						return;
					}
					break;
				case 2:
					if( normal[0] * startmins[0] + normal[1] * startmaxs[1] + normal[2] * startmins[2] > p->dist ) {
						return;
					}
					break;
				case 3:
					if( normal[0] * startmaxs[0] + normal[1] * startmaxs[1] + normal[2] * startmins[2] > p->dist ) {
						return;
					}
					break;
				case 4:
					if( normal[0] * startmins[0] + normal[1] * startmins[1] + normal[2] * startmaxs[2] > p->dist ) {
						return;
					}
					break;
				case 5:
					if( normal[0] * startmaxs[0] + normal[1] * startmins[1] + normal[2] * startmaxs[2] > p->dist ) {
						return;
					}
					break;
				case 6:
					if( normal[0] * startmins[0] + normal[1] * startmaxs[1] + normal[2] * startmaxs[2] > p->dist ) {
						return;
					}
					break;
				case 7:
					if( normal[0] * startmaxs[0] + normal[1] * startmaxs[1] + normal[2] * startmaxs[2] > p->dist ) {
						return;
					}
					break;
				default:
					assert( 0 );
					return;
			}
		}
	}

	// inside this brush
	tlc->trace->startsolid = tlc->trace->allsolid = true;
	tlc->trace->fraction = 0;
	tlc->trace->contents = brush->contents;
}

static inline bool CM_MightCollide( const vec_bounds_t shapeMins, const vec_bounds_t shapeMaxs, const CMTraceContext *tlc ) {
	return BoundsIntersect( shapeMins, shapeMaxs, tlc->absmins, tlc->absmaxs );
}

void CMTraceComputer::CollideBox( CMTraceContext *tlc, void ( CMTraceComputer::*method )( CMTraceContext *, cbrush_t * ),
								  cbrush_t **markbrushes, int nummarkbrushes,
								  cface_t **markfaces, int nummarkfaces ) {
	int i, j;
	cbrush_t *b;
	cface_t *patch;
	cbrush_t *facet;

	// Save the exact address to avoid pointer chasing in loops
	const float *fraction = &tlc->trace->fraction;

	// trace line against all brushes
	for( i = 0; i < nummarkbrushes; i++ ) {
		b = markbrushes[i];
		if( !( b->contents & tlc->contents ) ) {
			continue;
		}
		if( !CM_MightCollide( b->mins, b->maxs, tlc ) ) {
			continue;
		}
		( this->*method )( tlc, b );
		if( !*fraction ) {
			return;
		}
	}

	// trace line against all patches
	for( i = 0; i < nummarkfaces; i++ ) {
		patch = markfaces[i];
		if( !( patch->contents & tlc->contents ) ) {
			continue;
		}
		if( !CM_MightCollide( patch->mins, patch->maxs, tlc ) ) {
			continue;
		}
		facet = patch->facets;
		for( j = 0; j < patch->numfacets; j++, facet++ ) {
			if( !CM_MightCollide( facet->mins, facet->maxs, tlc ) ) {
				continue;
			}
			( this->*method )( tlc, facet );
			if( !*fraction ) {
				return;
			}
		}
	}
}

static inline bool CM_MightCollideInLeaf( const vec_bounds_t shapeMins,
										  const vec_bounds_t shapeMaxs,
										  const vec_bounds_t shapeCenter,
										  float shapeRadius,
										  const CMTraceContext *tlc ) {
	if( !CM_MightCollide( shapeMins, shapeMaxs, tlc ) ) {
		return false;
	}

	// TODO: Vectorize this part. This task is not completed for various reasons.

	vec3_t centerToStart;
	vec3_t proj, perp;

	VectorSubtract( tlc->start, shapeCenter, centerToStart );
	float projMagnitude = DotProduct( centerToStart, tlc->traceDir );
	VectorScale( tlc->traceDir, projMagnitude, proj );
	VectorSubtract( centerToStart, proj, perp );
	float distanceThreshold = shapeRadius + tlc->boxRadius;
	return VectorLengthSquared( perp ) <= distanceThreshold * distanceThreshold;
}

void CMTraceComputer::ClipBoxToLeaf( CMTraceContext *tlc, cbrush_t **markbrushes,
									 int nummarkbrushes, cface_t **markfaces, int nummarkfaces ) {
	int i, j;
	cbrush_t *b;
	cface_t *patch;
	cbrush_t *facet;

	// Saving the method reference should reduce virtual calls cost by avoid vtable lookup
	auto method = &CMTraceComputer::ClipBoxToBrush;

	// Save the exact address to avoid pointer chasing in loops
	const float *fraction = &tlc->trace->fraction;

	// trace line against all brushes
	for( i = 0; i < nummarkbrushes; i++ ) {
		b = markbrushes[i];
		if( !( b->contents & tlc->contents ) ) {
			continue;
		}
		if( !CM_MightCollideInLeaf( b->mins, b->maxs, b->center, b->radius, tlc ) ) {
			continue;
		}
		( this->*method )( tlc, b );
		if( !*fraction ) {
			return;
		}
	}

	// trace line against all patches
	for( i = 0; i < nummarkfaces; i++ ) {
		patch = markfaces[i];
		if( !( patch->contents & tlc->contents ) ) {
			continue;
		}
		if( !CM_MightCollideInLeaf( patch->mins, patch->maxs, patch->center, patch->radius, tlc ) ) {
			continue;
		}
		facet = patch->facets;
		for( j = 0; j < patch->numfacets; j++, facet++ ) {
			if( !CM_MightCollideInLeaf( facet->mins, facet->maxs, facet->center, facet->radius, tlc ) ) {
				continue;
			}
			( this->*method )( tlc, facet );
			if( !*fraction ) {
				return;
			}
		}
	}
}

void CMTraceComputer::RecursiveHullCheck( CMTraceContext *tlc, int num, float p1f, float p2f, vec3_t p1, vec3_t p2 ) {
	cnode_t *node;
	cplane_t *plane;
	int side;
	float t1, t2, offset;
	float frac, frac2;
	float idist, midf;
	vec3_t mid;

loc0:
	if( tlc->trace->fraction <= p1f ) {
		return; // already hit something nearer
	}
	// if < 0, we are in a leaf node
	if( num < 0 ) {
		cleaf_t *leaf;

		leaf = &cms->map_leafs[-1 - num];
		if( leaf->contents & tlc->contents ) {
			ClipBoxToLeaf( tlc, leaf->markbrushes, leaf->nummarkbrushes, leaf->markfaces, leaf->nummarkfaces );
		}
		return;
	}

	//
	// find the point distances to the seperating plane
	// and the offset for the size of the box
	//
	node = cms->map_nodes + num;
	plane = node->plane;

	if( plane->type < 3 ) {
		t1 = p1[plane->type] - plane->dist;
		t2 = p2[plane->type] - plane->dist;
		offset = tlc->extents[plane->type];
	} else {
		t1 = DotProduct( plane->normal, p1 ) - plane->dist;
		t2 = DotProduct( plane->normal, p2 ) - plane->dist;
		if( tlc->ispoint ) {
			offset = 0;
		} else {
			offset = fabsf( tlc->extents[0] * plane->normal[0] ) +
					 fabsf( tlc->extents[1] * plane->normal[1] ) +
					 fabsf( tlc->extents[2] * plane->normal[2] );
		}
	}

	// see which sides we need to consider
	if( t1 >= offset && t2 >= offset ) {
		num = node->children[0];
		goto loc0;
	}
	if( t1 < -offset && t2 < -offset ) {
		num = node->children[1];
		goto loc0;
	}

	// put the crosspoint DIST_EPSILON pixels on the near side
	if( t1 < t2 ) {
		idist = 1.0 / ( t1 - t2 );
		side = 1;
		frac2 = ( t1 + offset + DIST_EPSILON ) * idist;
		frac = ( t1 - offset + DIST_EPSILON ) * idist;
	} else if( t1 > t2 ) {
		idist = 1.0 / ( t1 - t2 );
		side = 0;
		frac2 = ( t1 - offset - DIST_EPSILON ) * idist;
		frac = ( t1 + offset + DIST_EPSILON ) * idist;
	} else {
		side = 0;
		frac = 1;
		frac2 = 0;
	}

	// move up to the node
	clamp( frac, 0, 1 );
	midf = p1f + ( p2f - p1f ) * frac;
	VectorLerp( p1, frac, p2, mid );

	RecursiveHullCheck( tlc, node->children[side], p1f, midf, p1, mid );

	// go past the node
	clamp( frac2, 0, 1 );
	midf = p1f + ( p2f - p1f ) * frac2;
	VectorLerp( p1, frac2, p2, mid );

	RecursiveHullCheck( tlc, node->children[side ^ 1], midf, p2f, mid, p2 );
}

//======================================================================



void CMTraceComputer::SetupCollideContext( CMTraceContext *tlc, trace_t *tr, const vec_t *start,
										   const vec3_t end, const vec3_t mins, const vec3_t maxs, int brushmask ) {
	tlc->trace = tr;
	tlc->contents = brushmask;
	VectorCopy( start, tlc->start );
	VectorCopy( end, tlc->end );
	VectorCopy( mins, tlc->mins );
	VectorCopy( maxs, tlc->maxs );

	// build a bounding box of the entire move
	ClearBounds( tlc->absmins, tlc->absmaxs );

	VectorAdd( start, tlc->mins, tlc->startmins );
	AddPointToBounds( tlc->startmins, tlc->absmins, tlc->absmaxs );

	VectorAdd( start, tlc->maxs, tlc->startmaxs );
	AddPointToBounds( tlc->startmaxs, tlc->absmins, tlc->absmaxs );

	VectorAdd( end, tlc->mins, tlc->endmins );
	AddPointToBounds( tlc->endmins, tlc->absmins, tlc->absmaxs );

	VectorAdd( end, tlc->maxs, tlc->endmaxs );
	AddPointToBounds( tlc->endmaxs, tlc->absmins, tlc->absmaxs );
}



void CMTraceComputer::Trace( trace_t *tr, const vec3_t start, const vec3_t end,
							 const vec3_t mins, const vec3_t maxs, cmodel_t *cmodel, int brushmask ) {
	ATTRIBUTE_ALIGNED( 16 ) CMTraceContext tlc;

	// fill in a default trace
	memset( tr, 0, sizeof( *tr ) );
	tr->fraction = 1;
	if( !cms->numnodes ) { // map not loaded
		return;
	}

	SetupCollideContext( &tlc, tr, start, end, mins, maxs, brushmask );

	//
	// check for position test special case
	//
	if( VectorCompare( start, end ) ) {
		auto func = &CMTraceComputer::TestBoxInBrush;
		if( cmodel != cms->map_cmodels ) {
			if( BoundsIntersect( cmodel->mins, cmodel->maxs, tlc.absmins, tlc.absmaxs ) ) {
				CollideBox( &tlc, func, cmodel->markbrushes, cmodel->nummarkbrushes, cmodel->markfaces, cmodel->nummarkfaces );
			}
		} else {
			vec3_t boxmins, boxmaxs;
			for( int i = 0; i < 3; i++ ) {
				boxmins[i] = start[i] + mins[i] - 1;
				boxmaxs[i] = start[i] + maxs[i] + 1;
			}

			int leafs[1024];
			int topnode;
			int numleafs = CM_BoxLeafnums( cms, boxmins, boxmaxs, leafs, 1024, &topnode );
			for( int i = 0; i < numleafs; i++ ) {
				cleaf_t *leaf = &cms->map_leafs[leafs[i]];
					if( leaf->contents & tlc.contents ) {
						CollideBox( &tlc, func, leaf->markbrushes, leaf->nummarkbrushes, leaf->markfaces, leaf->nummarkfaces );
						if( tr->allsolid ) {
							break;
						}
					}
			}
		}

		VectorCopy( start, tr->endpos );
		return;
	}

	//
	// check for point special case
	//
	if( VectorCompare( mins, vec3_origin ) && VectorCompare( maxs, vec3_origin ) ) {
		tlc.ispoint = true;
		VectorClear( tlc.extents );
	} else {
		tlc.ispoint = false;
		VectorSet( tlc.extents,
				   -mins[0] > maxs[0] ? -mins[0] : maxs[0],
				   -mins[1] > maxs[1] ? -mins[1] : maxs[1],
				   -mins[2] > maxs[2] ? -mins[2] : maxs[2] );
	}

	// TODO: Why do we have to prepare all these vars for all cases, otherwise platforms/movers are malfunctioning?
	SetupClipContext( &tlc );

	VectorSubtract( end, start, tlc.traceDir );
	VectorNormalize( tlc.traceDir );
	float squareDiameter = DistanceSquared( mins, maxs );
	if( squareDiameter >= 2.0f ) {
		tlc.boxRadius = 0.5f * sqrtf( squareDiameter ) + 8.0f;
	} else {
		tlc.boxRadius = 8.0f;
	}

	//
	// general sweeping through world
	//
	if( cmodel == cms->map_cmodels ) {
		RecursiveHullCheck( &tlc, 0, 0, 1, const_cast<float *>( start ), const_cast<float *>( end ) );
	} else if( BoundsIntersect( cmodel->mins, cmodel->maxs, tlc.absmins, tlc.absmaxs ) ) {
		auto func = &CMTraceComputer::ClipBoxToBrush;
		CollideBox( &tlc, func, cmodel->markbrushes, cmodel->nummarkbrushes, cmodel->markfaces, cmodel->nummarkfaces );
	}

	if( tr->fraction == 1 ) {
		VectorCopy( end, tr->endpos );
	} else {
		VectorLerp( start, tr->fraction, end, tr->endpos );
#ifdef TRACE_NOAXIAL
		if( PlaneTypeForNormal( tr->plane.normal ) == PLANE_NONAXIAL ) {
			VectorMA( tr->endpos, TRACE_NOAXIAL_SAFETY_OFFSET, tr->plane.normal, tr->endpos );
		}
#endif
	}
}

/*
* CM_TransformedBoxTrace
*
* Handles offseting and rotation of the end points for moving and
* rotating entities
*/
extern "C" void CM_TransformedBoxTrace( cmodel_state_t *cms, trace_t *tr, vec3_t start, vec3_t end,
										vec3_t mins, vec3_t maxs, cmodel_t *cmodel,
										int brushmask, vec3_t origin, vec3_t angles ) {
	vec3_t start_l, end_l;
	vec3_t a, temp;
	mat3_t axis;
	bool rotated;

	if( !tr ) {
		return;
	}

	if( !cmodel || cmodel == cms->map_cmodels ) {
		cmodel = cms->map_cmodels;
		origin = vec3_origin;
		angles = vec3_origin;
	} else {
		if( !origin ) {
			origin = vec3_origin;
		}
		if( !angles ) {
			angles = vec3_origin;
		}
	}

	// cylinder offset
	if( cmodel == cms->oct_cmodel ) {
		VectorSubtract( start, cmodel->cyl_offset, start_l );
		VectorSubtract( end, cmodel->cyl_offset, end_l );
	} else {
		VectorCopy( start, start_l );
		VectorCopy( end, end_l );
	}

	// subtract origin offset
	VectorSubtract( start_l, origin, start_l );
	VectorSubtract( end_l, origin, end_l );

	// ch : here we could try back-rotate the vector for aabb to get
	// 'cylinder-like' shape, ie width of the aabb is constant for all directions
	// in this case, the orientation of vector would be ( normalize(origin-start), cross(x,z), up )

	// rotate start and end into the models frame of reference
	if( ( angles[0] || angles[1] || angles[2] )
#ifndef CM_ALLOW_ROTATED_BBOXES
		&& !cmodel->builtin
#endif
		) {
		rotated = true;
	} else {
		rotated = false;
	}

	if( rotated ) {
		AnglesToAxis( angles, axis );

		VectorCopy( start_l, temp );
		Matrix3_TransformVector( axis, temp, start_l );

		VectorCopy( end_l, temp );
		Matrix3_TransformVector( axis, temp, end_l );
	}

	// sweep the box through the model
	cms->traceComputer->Trace( tr, start_l, end_l, mins, maxs, cmodel, brushmask );

	if( rotated && tr->fraction != 1.0 ) {
		VectorNegate( angles, a );
		AnglesToAxis( a, axis );

		VectorCopy( tr->plane.normal, temp );
		Matrix3_TransformVector( axis, temp, tr->plane.normal );
	}

	if( tr->fraction == 1 ) {
		VectorCopy( end, tr->endpos );
	} else {
		VectorLerp( start, tr->fraction, end, tr->endpos );
#ifdef TRACE_NOAXIAL
		if( PlaneTypeForNormal( tr->plane.normal ) == PLANE_NONAXIAL ) {
			VectorMA( tr->endpos, TRACE_NOAXIAL_SAFETY_OFFSET, tr->plane.normal, tr->endpos );
		}
#endif
	}
}
