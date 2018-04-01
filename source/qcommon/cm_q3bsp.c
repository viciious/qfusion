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
// cm_q3bsp.c -- Q3 BSP model loading

#include "qcommon.h"
#include "cm_local.h"
#include "patch.h"

#define MAX_FACET_PLANES 32

static inline float CM_AddSphericalBounds( vec_bounds_t mins, vec_bounds_t maxs, vec_bounds_t center ) {
#ifdef CM_USE_SSE
	mins[3] = 0.0f;
	maxs[3] = 1.0f;
	center[3] = 0.0f;
#endif

	vec3_t dimensions;
	VectorSubtract( maxs, mins, dimensions );
	VectorMA( mins, 0.5f, dimensions, center );

	float squareDiameter = VectorLengthSquared( dimensions );
	if( squareDiameter < 1.0f ) {
		return 8.0f;
	}

	return 0.5f * sqrtf( squareDiameter ) + 8.0f;
}

/*
* CM_CreateFacetFromPoints
*/
static int CM_CreateFacetFromPoints( cmodel_state_t *cms, cbrush_t *facet, vec3_t *verts, int numverts, cshaderref_t *shaderref, cplane_t *brushplanes ) {
	int i, j;
	int axis, dir;
	vec3_t normal;
	float d, dist;
	cplane_t mainplane;
	vec3_t vec, vec2;
	int numbrushplanes;

	// set default values for brush
	facet->numsides = 0;
	facet->brushsides = NULL;
	facet->contents = shaderref->contents;

	// these bounds are default for the facet, and are not valid
	// however only bogus facets that are not collidable anyway would use these bounds
	ClearBounds( facet->mins, facet->maxs );

	// calculate plane for this triangle
	PlaneFromPoints( verts, &mainplane );
	if( ComparePlanes( mainplane.normal, mainplane.dist, vec3_origin, 0 ) ) {
		return 0;
	}

	// test a quad case
	if( numverts > 3 ) {
		d = DotProduct( verts[3], mainplane.normal ) - mainplane.dist;
		if( d < -0.1 || d > 0.1 ) {
			return 0;
		}

		if( 0 ) {
			vec3_t v[3];
			cplane_t plane;

			// try different combinations of planes
			for( i = 1; i < 4; i++ ) {
				VectorCopy( verts[i], v[0] );
				VectorCopy( verts[( i + 1 ) % 4], v[1] );
				VectorCopy( verts[( i + 2 ) % 4], v[2] );
				PlaneFromPoints( v, &plane );

				if( fabs( DotProduct( mainplane.normal, plane.normal ) ) < 0.9 ) {
					return 0;
				}
			}
		}
	}

	numbrushplanes = 0;

	// add front plane
	SnapPlane( mainplane.normal, &mainplane.dist );
	VectorCopy( mainplane.normal, brushplanes[numbrushplanes].normal );
	brushplanes[numbrushplanes].dist = mainplane.dist; numbrushplanes++;

	// calculate mins & maxs
	for( i = 0; i < numverts; i++ )
		AddPointToBounds( verts[i], facet->mins, facet->maxs );

	// add the axial planes
	for( axis = 0; axis < 3; axis++ ) {
		for( dir = -1; dir <= 1; dir += 2 ) {
			for( i = 0; i < numbrushplanes; i++ ) {
				if( brushplanes[i].normal[axis] == dir ) {
					break;
				}
			}

			if( i == numbrushplanes ) {
				VectorClear( normal );
				normal[axis] = dir;
				if( dir == 1 ) {
					dist = facet->maxs[axis];
				} else {
					dist = -facet->mins[axis];
				}

				VectorCopy( normal, brushplanes[numbrushplanes].normal );
				brushplanes[numbrushplanes].dist = dist; numbrushplanes++;
			}
		}
	}

	// add the edge bevels
	for( i = 0; i < numverts; i++ ) {
		j = ( i + 1 ) % numverts;

		VectorSubtract( verts[i], verts[j], vec );
		if( VectorNormalize( vec ) < 0.5 ) {
			continue;
		}

		SnapVector( vec );
		for( j = 0; j < 3; j++ ) {
			if( vec[j] == 1 || vec[j] == -1 ) {
				break; // axial
			}
		}
		if( j != 3 ) {
			continue; // only test non-axial edges

		}
		// try the six possible slanted axials from this edge
		for( axis = 0; axis < 3; axis++ ) {
			for( dir = -1; dir <= 1; dir += 2 ) {
				// construct a plane
				VectorClear( vec2 );
				vec2[axis] = dir;
				CrossProduct( vec, vec2, normal );
				if( VectorNormalize( normal ) < 0.5 ) {
					continue;
				}
				dist = DotProduct( verts[i], normal );

				for( j = 0; j < numbrushplanes; j++ ) {
					// if this plane has already been used, skip it
					if( ComparePlanes( brushplanes[j].normal, brushplanes[j].dist, normal, dist ) ) {
						break;
					}
				}
				if( j != numbrushplanes ) {
					continue;
				}

				// if all other points are behind this plane, it is a proper edge bevel
				for( j = 0; j < numverts; j++ ) {
					if( j != i ) {
						d = DotProduct( verts[j], normal ) - dist;
						if( d > 0.1 ) {
							break; // point in front: this plane isn't part of the outer hull
						}
					}
				}
				if( j != numverts ) {
					continue;
				}

				// add this plane
				VectorCopy( normal, brushplanes[numbrushplanes].normal );
				brushplanes[numbrushplanes].dist = dist; numbrushplanes++;
				if( numbrushplanes == MAX_FACET_PLANES ) {
					break;
				}
			}
		}
	}

	// spread facet mins/maxs by a unit
	for( i = 0; i < 3; i++ ) {
		facet->mins[i] -= 1.0f;
		facet->maxs[i] += 1.0f;
	}

	facet->radius = CM_AddSphericalBounds( facet->mins, facet->maxs, facet->center );

	return ( facet->numsides = numbrushplanes );
}

/*
* CM_CreatePatch
*/
static void CM_CreatePatch( cmodel_state_t *cms, cface_t *patch, cshaderref_t *shaderref, vec3_t *verts, int *patch_cp ) {
	int step[2], size[2], flat[2];
	vec3_t *patchpoints;
	int i, j, k,u, v;
	int numsides, totalsides;
	cbrush_t *facets, *facet;
	vec3_t *points;
	vec3_t tverts[4];
	uint8_t *data;
	cplane_t *brushplanes;

	// find the degree of subdivision in the u and v directions
	Patch_GetFlatness( CM_SUBDIV_LEVEL, ( vec_t * )verts[0], 3, patch_cp, flat );

	step[0] = 1 << flat[0];
	step[1] = 1 << flat[1];
	size[0] = ( patch_cp[0] >> 1 ) * step[0] + 1;
	size[1] = ( patch_cp[1] >> 1 ) * step[1] + 1;
	if( size[0] <= 0 || size[1] <= 0 ) {
		return;
	}

	patchpoints = Mem_TempMalloc( size[0] * size[1] * sizeof( vec3_t ) );
	Patch_Evaluate( vec_t, 3, verts[0], patch_cp, step, patchpoints[0], 0 );
	Patch_RemoveLinearColumnsRows( patchpoints[0], 3, &size[0], &size[1], 0, NULL, NULL );

	data = Mem_Alloc( cms->mempool, size[0] * size[1] * sizeof( vec3_t ) +
					  ( size[0] - 1 ) * ( size[1] - 1 ) * 2 * ( sizeof( cbrush_t ) + 32 * sizeof( cplane_t ) ) );

	points = ( vec3_t * )data; data += size[0] * size[1] * sizeof( vec3_t );
	facets = ( cbrush_t * )data; data += ( size[0] - 1 ) * ( size[1] - 1 ) * 2 * sizeof( cbrush_t );
	brushplanes = ( cplane_t * )data; data += ( size[0] - 1 ) * ( size[1] - 1 ) * 2 * MAX_FACET_PLANES * sizeof( cplane_t );

	// fill in
	memcpy( points, patchpoints, size[0] * size[1] * sizeof( vec3_t ) );
	Mem_TempFree( patchpoints );

	totalsides = 0;
	patch->numfacets = 0;
	patch->facets = NULL;
	ClearBounds( patch->mins, patch->maxs );

	// create a set of facets
	for( v = 0; v < size[1] - 1; v++ ) {
		for( u = 0; u < size[0] - 1; u++ ) {
			i = v * size[0] + u;
			VectorCopy( points[i], tverts[0] );
			VectorCopy( points[i + size[0]], tverts[1] );
			VectorCopy( points[i + size[0] + 1], tverts[2] );
			VectorCopy( points[i + 1], tverts[3] );

			for( i = 0; i < 4; i++ )
				AddPointToBounds( tverts[i], patch->mins, patch->maxs );

			// try to create one facet from a quad
			numsides = CM_CreateFacetFromPoints( cms, &facets[patch->numfacets], tverts, 4, shaderref, brushplanes + totalsides );
			if( !numsides ) { // create two facets from triangles
				VectorCopy( tverts[3], tverts[2] );
				numsides = CM_CreateFacetFromPoints( cms, &facets[patch->numfacets], tverts, 3, shaderref, brushplanes + totalsides );
				if( numsides ) {
					totalsides += numsides;
					patch->numfacets++;
				}

				VectorCopy( tverts[2], tverts[0] );
				VectorCopy( points[v * size[0] + u + size[0] + 1], tverts[2] );
				numsides = CM_CreateFacetFromPoints( cms, &facets[patch->numfacets], tverts, 3, shaderref, brushplanes + totalsides );
			}

			if( numsides ) {
				totalsides += numsides;
				patch->numfacets++;
			}
		}
	}

	if( patch->numfacets ) {
		uint8_t *fdata;

		fdata = Mem_Alloc( cms->mempool, patch->numfacets * sizeof( cbrush_t ) + totalsides * ( sizeof( cbrushside_t ) + sizeof( cplane_t ) ) );
		cms->map_face_brushdata[patch - cms->map_faces] = fdata;

		patch->facets = ( cbrush_t * )fdata; fdata += patch->numfacets * sizeof( cbrush_t );
		memcpy( patch->facets, facets, patch->numfacets * sizeof( cbrush_t ) );
		for( i = 0, k = 0, facet = patch->facets; i < patch->numfacets; i++, facet++ ) {
			cplane_t *planes;
			cbrushside_t *s;

			facet->brushsides = ( cbrushside_t * )fdata; fdata += facet->numsides * sizeof( cbrushside_t );
			planes = ( cplane_t * )fdata; fdata += facet->numsides * sizeof( cplane_t );

			for( j = 0, s = facet->brushsides; j < facet->numsides; j++, s++ ) {
				planes[j] = brushplanes[k++];
				SnapPlane( planes[j].normal, &planes[j].dist );
				CategorizePlane( &planes[j] );
				CM_CopyRawToCMPlane( &planes[j], &s->plane );
				s->surfFlags = shaderref->flags;
			}
		}

		patch->contents = shaderref->contents;

		for( i = 0; i < 3; i++ ) {
			// spread the mins / maxs by a pixel
			patch->mins[i] -= 1;
			patch->maxs[i] += 1;
		}

		patch->radius = CM_AddSphericalBounds( patch->mins, patch->maxs, patch->center );
	}

	Mem_Free( points );
}

/*
===============================================================================

MAP LOADING

===============================================================================
*/

/*
* CMod_LoadSurfaces
*/
static void CMod_LoadSurfaces( cmodel_state_t *cms, lump_t *l ) {
	int i;
	int count;
	char *buffer;
	size_t len, bufLen, bufSize;
	dshaderref_t *in;
	cshaderref_t *out;

	in = ( void * )( cms->cmod_base + l->fileofs );
	if( l->filelen % sizeof( *in ) ) {
		Com_Error( ERR_DROP, "CMod_LoadSurfaces: funny lump size" );
	}
	count = l->filelen / sizeof( *in );
	if( count < 1 ) {
		Com_Error( ERR_DROP, "CMod_LoadSurfaces: map with no shaders" );
	}

	out = cms->map_shaderrefs = Mem_Alloc( cms->mempool, count * sizeof( *out ) );
	cms->numshaderrefs = count;

	buffer = NULL;
	bufLen = bufSize = 0;

	for( i = 0; i < count; i++, in++, out++, bufLen += len + 1 ) {
		len = strlen( in->name );
		if( bufLen + len >= bufSize ) {
			bufSize = bufLen + len + 128;
			if( buffer ) {
				buffer = Mem_Realloc( buffer, bufSize );
			} else {
				buffer = Mem_Alloc( cms->mempool, bufSize );
			}
		}

		// Vic: ZOMG, this is so nasty, perfectly valid in C though
		out->name = ( char * )( ( void * )bufLen );
		strcpy( buffer + bufLen, in->name );
		out->flags = LittleLong( in->flags );
		out->contents = LittleLong( in->contents );
	}

	for( i = 0; i < count; i++ )
		cms->map_shaderrefs[i].name = buffer + ( size_t )( ( void * )cms->map_shaderrefs[i].name );

	// For non-FBSP maps (i.e. Q3, RTCW), unset FBSP-specific surface flags
	if( strcmp( cms->cmap_bspFormat->header, QFBSPHEADER ) ) {
		for( i = 0; i < count; i++ )
			cms->map_shaderrefs[i].flags &= SURF_FBSP_START - 1;
	}
}

/*
* CMod_LoadVertexes
*/
static void CMod_LoadVertexes( cmodel_state_t *cms, lump_t *l ) {
	int i;
	int count;
	dvertex_t *in;
	vec3_t *out;

	in = ( void * )( cms->cmod_base + l->fileofs );
	if( l->filelen % sizeof( *in ) ) {
		Com_Error( ERR_DROP, "CMOD_LoadVertexes: funny lump size" );
	}
	count = l->filelen / sizeof( *in );
	if( count < 1 ) {
		Com_Error( ERR_DROP, "Map with no vertexes" );
	}

	out = cms->map_verts = Mem_Alloc( cms->mempool, count * sizeof( *out ) );
	cms->numvertexes = count;

	for( i = 0; i < count; i++, in++ ) {
		out[i][0] = LittleFloat( in->point[0] );
		out[i][1] = LittleFloat( in->point[1] );
		out[i][2] = LittleFloat( in->point[2] );
	}
}

/*
* CMod_LoadVertexes_RBSP
*/
static void CMod_LoadVertexes_RBSP( cmodel_state_t *cms, lump_t *l ) {
	int i;
	int count;
	rdvertex_t *in;
	vec3_t *out;

	in = ( void * )( cms->cmod_base + l->fileofs );
	if( l->filelen % sizeof( *in ) ) {
		Com_Error( ERR_DROP, "CMod_LoadVertexes_RBSP: funny lump size" );
	}
	count = l->filelen / sizeof( *in );
	if( count < 1 ) {
		Com_Error( ERR_DROP, "Map with no vertexes" );
	}

	out = cms->map_verts = Mem_Alloc( cms->mempool, count * sizeof( *out ) );
	cms->numvertexes = count;

	for( i = 0; i < count; i++, in++ ) {
		out[i][0] = LittleFloat( in->point[0] );
		out[i][1] = LittleFloat( in->point[1] );
		out[i][2] = LittleFloat( in->point[2] );
	}
}

/*
* CMod_LoadFace
*/
static inline void CMod_LoadFace( cmodel_state_t *cms, cface_t *out, int shadernum, int firstvert, int numverts, int *patch_cp ) {
	cshaderref_t *shaderref;

	shadernum = LittleLong( shadernum );
	if( shadernum < 0 || shadernum >= cms->numshaderrefs ) {
		return;
	}

	shaderref = &cms->map_shaderrefs[shadernum];
	if( !shaderref->contents || ( shaderref->flags & SURF_NONSOLID ) ) {
		return;
	}

	patch_cp[0] = LittleLong( patch_cp[0] );
	patch_cp[1] = LittleLong( patch_cp[1] );
	if( patch_cp[0] <= 0 || patch_cp[1] <= 0 ) {
		return;
	}

	firstvert = LittleLong( firstvert );
	if( numverts <= 0 || firstvert < 0 || firstvert >= cms->numvertexes ) {
		return;
	}

	CM_CreatePatch( cms, out, shaderref, cms->map_verts + firstvert, patch_cp );
}

/*
* CMod_LoadFaces
*/
static void CMod_LoadFaces( cmodel_state_t *cms, lump_t *l ) {
	int i, count;
	dface_t *in;
	cface_t *out;

	in = ( void * )( cms->cmod_base + l->fileofs );
	if( l->filelen % sizeof( *in ) ) {
		Com_Error( ERR_DROP, "CMod_LoadFaces: funny lump size" );
	}
	count = l->filelen / sizeof( *in );
	if( count < 1 ) {
		Com_Error( ERR_DROP, "Map with no faces" );
	}

	out = cms->map_faces = Mem_Alloc( cms->mempool, count * sizeof( *out ) );
	cms->map_face_brushdata = Mem_Alloc( cms->mempool, count * sizeof( *cms->map_face_brushdata ) );
	cms->numfaces = count;

	for( i = 0; i < count; i++, in++, out++ ) {
		out->contents = 0;
		out->numfacets = 0;
		out->facets = NULL;
		cms->map_face_brushdata[i] = NULL;
		if( LittleLong( in->facetype ) != FACETYPE_PATCH ) {
			continue;
		}
		CMod_LoadFace( cms, out, in->shadernum, in->firstvert, in->numverts, in->patch_cp );
	}
}

/*
* CMod_LoadFaces_RBSP
*/
static void CMod_LoadFaces_RBSP( cmodel_state_t *cms, lump_t *l ) {
	int i, count;
	rdface_t *in;
	cface_t *out;

	in = ( void * )( cms->cmod_base + l->fileofs );
	if( l->filelen % sizeof( *in ) ) {
		Com_Error( ERR_DROP, "CMod_LoadFaces_RBSP: funny lump size" );
	}
	count = l->filelen / sizeof( *in );
	if( count < 1 ) {
		Com_Error( ERR_DROP, "Map with no faces" );
	}

	out = cms->map_faces = Mem_Alloc( cms->mempool, count * sizeof( *out ) );
	cms->map_face_brushdata = Mem_Alloc( cms->mempool, count * sizeof( *cms->map_face_brushdata ) );
	cms->numfaces = count;

	for( i = 0; i < count; i++, in++, out++ ) {
		out->contents = 0;
		out->numfacets = 0;
		out->facets = NULL;
		cms->map_face_brushdata[0] = NULL;
		if( LittleLong( in->facetype ) != FACETYPE_PATCH ) {
			continue;
		}
		CMod_LoadFace( cms, out, in->shadernum, in->firstvert, in->numverts, in->patch_cp );
	}
}

/*
* CMod_LoadSubmodels
*/
static void CMod_LoadSubmodels( cmodel_state_t *cms, lump_t *l ) {
	int i, j;
	int count;
	dmodel_t *in;
	cmodel_t *out;

	in = ( void * )( cms->cmod_base + l->fileofs );
	if( l->filelen % sizeof( *in ) ) {
		Com_Error( ERR_DROP, "CMod_LoadSubmodels: funny lump size" );
	}
	count = l->filelen / sizeof( *in );
	if( count < 1 ) {
		Com_Error( ERR_DROP, "Map with no models" );
	}

	out = cms->map_cmodels = Mem_Alloc( cms->mempool, count * sizeof( *out ) );
	cms->numcmodels = count;

	for( i = 0; i < count; i++, in++, out++ ) {
		out->nummarkfaces = LittleLong( in->numfaces );
		out->markfaces = Mem_Alloc( cms->mempool, out->nummarkfaces * sizeof( cface_t * ) );
		out->nummarkbrushes = LittleLong( in->numbrushes );
		out->markbrushes = Mem_Alloc( cms->mempool, out->nummarkbrushes * sizeof( cbrush_t * ) );

		for( j = 0; j < out->nummarkfaces; j++ )
			out->markfaces[j] = cms->map_faces + LittleLong( in->firstface ) + j;
		for( j = 0; j < out->nummarkbrushes; j++ )
			out->markbrushes[j] = cms->map_brushes + LittleLong( in->firstbrush ) + j;

		for( j = 0; j < 3; j++ ) {
			// spread the mins / maxs by a pixel
			out->mins[j] = LittleFloat( in->mins[j] ) - 1;
			out->maxs[j] = LittleFloat( in->maxs[j] ) + 1;
		}
	}
}

/*
* CMod_LoadNodes
*/
static void CMod_LoadNodes( cmodel_state_t *cms, lump_t *l ) {
	int i;
	int count;
	dnode_t *in;
	cnode_t *out;

	in = ( void * )( cms->cmod_base + l->fileofs );
	if( l->filelen % sizeof( *in ) ) {
		Com_Error( ERR_DROP, "CMod_LoadNodes: funny lump size" );
	}
	count = l->filelen / sizeof( *in );
	if( count < 1 ) {
		Com_Error( ERR_DROP, "Map has no nodes" );
	}

	out = cms->map_nodes = Mem_Alloc( cms->mempool, count * sizeof( *out ) );
	cms->numnodes = count;

	for( i = 0; i < 3; i++ ) {
		cms->world_mins[i] = LittleFloat( in->mins[i] );
		cms->world_maxs[i] = LittleFloat( in->maxs[i] );
	}

	for( i = 0; i < count; i++, out++, in++ ) {
		out->plane = cms->map_planes + LittleLong( in->planenum );
		out->children[0] = LittleLong( in->children[0] );
		out->children[1] = LittleLong( in->children[1] );
	}
}

/*
* CMod_LoadMarkFaces
*/
static void CMod_LoadMarkFaces( cmodel_state_t *cms, lump_t *l ) {
	int i, j;
	int count;
	cface_t **out;
	int *in;

	in = ( void * )( cms->cmod_base + l->fileofs );
	if( l->filelen % sizeof( *in ) ) {
		Com_Error( ERR_DROP, "CMod_LoadMarkFaces: funny lump size" );
	}
	count = l->filelen / sizeof( *in );
	if( count < 1 ) {
		Com_Error( ERR_DROP, "Map with no leaffaces" );
	}

	out = cms->map_markfaces = Mem_Alloc( cms->mempool, count * sizeof( *out ) );
	cms->nummarkfaces = count;

	for( i = 0; i < count; i++ ) {
		j = LittleLong( in[i] );
		if( j < 0 || j >= cms->numfaces ) {
			Com_Error( ERR_DROP, "CMod_LoadMarkFaces: bad surface number" );
		}
		out[i] = cms->map_faces + j;
	}
}

/*
* CMod_LoadLeafs
*/
static void CMod_LoadLeafs( cmodel_state_t *cms, lump_t *l ) {
	int i, j, k;
	int count;
	cleaf_t *out;
	dleaf_t *in;

	in = ( void * )( cms->cmod_base + l->fileofs );
	if( l->filelen % sizeof( *in ) ) {
		Com_Error( ERR_DROP, "CMod_LoadLeafs: funny lump size" );
	}
	count = l->filelen / sizeof( *in );
	if( count < 1 ) {
		Com_Error( ERR_DROP, "Map with no leafs" );
	}

	out = cms->map_leafs = Mem_Alloc( cms->mempool, count * sizeof( *out ) );
	cms->numleafs = count;

	for( i = 0; i < count; i++, in++, out++ ) {
		out->contents = 0;
		out->cluster = LittleLong( in->cluster );
		out->area = LittleLong( in->area );
		out->markbrushes = cms->map_markbrushes + LittleLong( in->firstleafbrush );
		out->nummarkbrushes = LittleLong( in->numleafbrushes );
		out->markfaces = cms->map_markfaces + LittleLong( in->firstleafface );
		out->nummarkfaces = LittleLong( in->numleaffaces );

		// OR brushes' contents
		for( j = 0; j < out->nummarkbrushes; j++ )
			out->contents |= out->markbrushes[j]->contents;

		// exclude markfaces that have no facets
		// so we don't perform this check at runtime
		for( j = 0; j < out->nummarkfaces; ) {
			k = j;
			if( !out->markfaces[j]->facets ) {
				for(; ( ++j < out->nummarkfaces ) && !out->markfaces[j]->facets; ) ;
				if( j < out->nummarkfaces ) {
					memmove( &out->markfaces[k], &out->markfaces[j], ( out->nummarkfaces - j ) * sizeof( *out->markfaces ) );
				}
				out->nummarkfaces -= j - k;

			}
			j = k + 1;
		}

		// OR patches' contents
		for( j = 0; j < out->nummarkfaces; j++ )
			out->contents |= out->markfaces[j]->contents;

		if( out->area >= cms->numareas ) {
			cms->numareas = out->area + 1;
		}
	}
}

/*
* CMod_LoadPlanes
*/
static void CMod_LoadPlanes( cmodel_state_t *cms, lump_t *l ) {
	int i, j;
	int count;
	cplane_t *out;
	dplane_t *in;

	in = ( void * )( cms->cmod_base + l->fileofs );
	if( l->filelen % sizeof( *in ) ) {
		Com_Error( ERR_DROP, "CMod_LoadPlanes: funny lump size" );
	}
	count = l->filelen / sizeof( *in );
	if( count < 1 ) {
		Com_Error( ERR_DROP, "Map with no planes" );
	}

	out = cms->map_planes = Mem_Alloc( cms->mempool, count * sizeof( *out ) );
	cms->numplanes = count;

	for( i = 0; i < count; i++, in++, out++ ) {
		out->signbits = 0;
		out->type = PLANE_NONAXIAL;

		for( j = 0; j < 3; j++ ) {
			out->normal[j] = LittleFloat( in->normal[j] );
			if( out->normal[j] < 0 ) {
				out->signbits |= ( 1 << j );
			}
			if( out->normal[j] == 1.0f ) {
				out->type = j;
			}
		}

		out->dist = LittleFloat( in->dist );
	}
}

/*
* CMod_LoadMarkBrushes
*/
static void CMod_LoadMarkBrushes( cmodel_state_t *cms, lump_t *l ) {
	int i;
	int count;
	cbrush_t **out;
	int *in;

	in = ( void * )( cms->cmod_base + l->fileofs );
	if( l->filelen % sizeof( *in ) ) {
		Com_Error( ERR_DROP, "CMod_LoadMarkBrushes: funny lump size" );
	}
	count = l->filelen / sizeof( *in );
	if( count < 1 ) {
		Com_Error( ERR_DROP, "Map with no leafbrushes" );
	}

	out = cms->map_markbrushes = Mem_Alloc( cms->mempool, count * sizeof( *out ) );
	cms->nummarkbrushes = count;

	for( i = 0; i < count; i++, in++ )
		out[i] = cms->map_brushes + LittleLong( *in );
}

/*
* CMod_LoadBrushSides
*/
static void CMod_LoadBrushSides( cmodel_state_t *cms, lump_t *l ) {
	int i, j;
	int count;
	cbrushside_t *out;
	dbrushside_t *in;

	in = ( void * )( cms->cmod_base + l->fileofs );
	if( l->filelen % sizeof( *in ) ) {
		Com_Error( ERR_DROP, "CMod_LoadBrushSides: funny lump size" );
	}
	count = l->filelen / sizeof( *in );
	if( count < 1 ) {
		Com_Error( ERR_DROP, "Map with no brushsides" );
	}

	out = cms->map_brushsides = Mem_Alloc( cms->mempool, count * sizeof( *out ) );
	cms->numbrushsides = count;

	for( i = 0; i < count; i++, in++, out++ ) {
		CM_CopyRawToCMPlane( cms->map_planes + LittleLong( in->planenum ), &out->plane );
		j = LittleLong( in->shadernum );
		if( j >= cms->numshaderrefs ) {
			Com_Error( ERR_DROP, "Bad brushside texinfo" );
		}
		out->surfFlags = cms->map_shaderrefs[j].flags;
	}
}

/*
* CMod_LoadBrushSides_RBSP
*/
static void CMod_LoadBrushSides_RBSP( cmodel_state_t *cms, lump_t *l ) {
	int i, j;
	int count;
	cbrushside_t *out;
	rdbrushside_t *in;

	in = ( void * )( cms->cmod_base + l->fileofs );
	if( l->filelen % sizeof( *in ) ) {
		Com_Error( ERR_DROP, "CMod_LoadBrushSides_RBSP: funny lump size" );
	}
	count = l->filelen / sizeof( *in );
	if( count < 1 ) {
		Com_Error( ERR_DROP, "Map with no brushsides" );
	}

	out = cms->map_brushsides = Mem_Alloc( cms->mempool, count * sizeof( *out ) );
	cms->numbrushsides = count;

	for( i = 0; i < count; i++, in++, out++ ) {
		CM_CopyRawToCMPlane( cms->map_planes + LittleLong( in->planenum ), &out->plane );
		j = LittleLong( in->shadernum );
		if( j >= cms->numshaderrefs ) {
			Com_Error( ERR_DROP, "Bad brushside texinfo" );
		}
		out->surfFlags = cms->map_shaderrefs[j].flags;
	}
}

/*
* CMod_LoadBrushes
*/
static void CMod_LoadBrushes( cmodel_state_t *cms, lump_t *l ) {
	int i;
	int count;
	dbrush_t *in;
	cbrush_t *out;
	int shaderref;

	in = ( void * )( cms->cmod_base + l->fileofs );
	if( l->filelen % sizeof( *in ) ) {
		Com_Error( ERR_DROP, "CMod_LoadBrushes: funny lump size" );
	}
	count = l->filelen / sizeof( *in );
	if( count < 1 ) {
		Com_Error( ERR_DROP, "Map with no brushes" );
	}

	out = cms->map_brushes = Mem_Alloc( cms->mempool, count * sizeof( *out ) );
	cms->numbrushes = count;

	for( i = 0; i < count; i++, out++, in++ ) {
		shaderref = LittleLong( in->shadernum );
		out->contents = cms->map_shaderrefs[shaderref].contents;
		out->numsides = LittleLong( in->numsides );
		out->brushsides = cms->map_brushsides + LittleLong( in->firstside );
		CM_BoundBrush( cms, out );
	}
}

/*
* CMod_LoadVisibility
*/
static void CMod_LoadVisibility( cmodel_state_t *cms, lump_t *l ) {
	cms->map_visdatasize = l->filelen;
	if( !cms->map_visdatasize ) {
		cms->map_pvs = NULL;
		return;
	}

	cms->map_pvs = Mem_Alloc( cms->mempool, cms->map_visdatasize );
	memcpy( cms->map_pvs, cms->cmod_base + l->fileofs, cms->map_visdatasize );

	cms->map_pvs->numclusters = LittleLong( cms->map_pvs->numclusters );
	cms->map_pvs->rowsize = LittleLong( cms->map_pvs->rowsize );
}

/*
* CMod_LoadEntityString
*/
static void CMod_LoadEntityString( cmodel_state_t *cms, lump_t *l ) {
	cms->numentitychars = l->filelen;
	if( !l->filelen ) {
		return;
	}

	cms->map_entitystring = Mem_Alloc( cms->mempool, cms->numentitychars );
	memcpy( cms->map_entitystring, cms->cmod_base + l->fileofs, l->filelen );
}

/*
* CM_LoadQ3BrushModel
*/
void CM_LoadQ3BrushModel( cmodel_state_t *cms, void *parent, void *buf, bspFormatDesc_t *format ) {
	int i;
	dheader_t header;

	cms->cmap_bspFormat = format;

	header = *(dheader_t *)buf;
	for( i = 0; i < sizeof( dheader_t ) / 4; i++ )
		( (int *)&header )[i] = LittleLong( ( (int *)&header )[i] );
	cms->cmod_base = ( uint8_t * )buf;

	// load into heap
	CMod_LoadSurfaces( cms, &header.lumps[LUMP_SHADERREFS] );
	CMod_LoadPlanes( cms, &header.lumps[LUMP_PLANES] );
	if( cms->cmap_bspFormat->flags & BSP_RAVEN ) {
		CMod_LoadBrushSides_RBSP( cms, &header.lumps[LUMP_BRUSHSIDES] );
	} else {
		CMod_LoadBrushSides( cms, &header.lumps[LUMP_BRUSHSIDES] );
	}
	CMod_LoadBrushes( cms, &header.lumps[LUMP_BRUSHES] );
	CMod_LoadMarkBrushes( cms, &header.lumps[LUMP_LEAFBRUSHES] );
	if( cms->cmap_bspFormat->flags & BSP_RAVEN ) {
		CMod_LoadVertexes_RBSP( cms, &header.lumps[LUMP_VERTEXES] );
		CMod_LoadFaces_RBSP( cms, &header.lumps[LUMP_FACES] );
	} else {
		CMod_LoadVertexes( cms, &header.lumps[LUMP_VERTEXES] );
		CMod_LoadFaces( cms, &header.lumps[LUMP_FACES] );
	}
	CMod_LoadMarkFaces( cms, &header.lumps[LUMP_LEAFFACES] );
	CMod_LoadLeafs( cms, &header.lumps[LUMP_LEAFS] );
	CMod_LoadNodes( cms, &header.lumps[LUMP_NODES] );
	CMod_LoadSubmodels( cms, &header.lumps[LUMP_MODELS] );
	CMod_LoadVisibility( cms, &header.lumps[LUMP_VISIBILITY] );
	CMod_LoadEntityString( cms, &header.lumps[LUMP_ENTITIES] );

	FS_FreeFile( buf );

	if( cms->numvertexes ) {
		Mem_Free( cms->map_verts );
	}
}

void CM_BoundBrush( cmodel_state_t *cms, cbrush_t *brush ) {
	int i;

	for( i = 0; i < 3; i++ ) {
		brush->mins[i] = -brush->brushsides[i * 2 + 0].plane.dist;
		brush->maxs[i] = +brush->brushsides[i * 2 + 1].plane.dist;
	}

	brush->radius = CM_AddSphericalBounds( brush->mins, brush->maxs, brush->center );
}
