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

#ifdef __cplusplus
extern "C" {
#endif

#if ( defined( i386 ) || defined( __x86_64__ ) || defined( _M_IX86 ) || defined( _M_AMD64 ) || defined( _M_X64 ) )
#define CM_TRY_SIMD
#define CM_USE_SSE
#endif

#define MAX_CM_LEAFS        ( MAX_MAP_LEAFS )

#define CM_SUBDIV_LEVEL     ( 16 )

#define TRACE_NOAXIAL_SAFETY_OFFSET 0.1

// keep 1/8 unit away to keep the position valid before network snapping
// and to avoid various numeric issues
#define SURFACE_CLIP_EPSILON    ( 0.125 )

typedef struct {
	char *name;
	int contents;
	int flags;
} cshaderref_t;

typedef struct {
	cplane_t *plane;
	int children[2];            // negative numbers are leafs
} cnode_t;

#ifdef CM_TRY_SIMD
typedef struct {
	vec4_t normal;
	float dist;
	short type;                 // for fast side tests
	short signbits;             // signx + (signy<<1) + (signz<<1)
} cm_plane_t;
#else
typedef cplane_t cm_plane_t;
#endif

typedef struct {
	cm_plane_t plane;
	int surfFlags;
} cbrushside_t;

#ifdef CM_TRY_SIMD
typedef vec4_t vec_bounds_t;
#else
typedef vec3_t vec_bounds_t;
#endif

typedef struct cbrush_s {
	cbrushside_t *brushsides;

	vec_bounds_t mins, maxs, center;
	float radius;

	int contents;
	int numsides;
} cbrush_t;

typedef struct cface_s {
	cbrush_t *facets;

	vec_bounds_t mins, maxs, center;
	float radius;

	int contents;
	int numfacets;
} cface_t;

typedef struct {
	cbrush_t **markbrushes;
	cface_t **markfaces;

	int contents;
	int cluster;
	int area;
	int nummarkbrushes;
	int nummarkfaces;
} cleaf_t;

typedef struct cmodel_s {
	cface_t **markfaces;
	cbrush_t **markbrushes;

	vec3_t mins, maxs;
	vec3_t cyl_offset;

	int nummarkfaces;
	int nummarkbrushes;

	float cyl_halfheight;
	float cyl_radius;

	bool builtin;
} cmodel_t;

typedef struct {
	int floodnum;               // if two areas have equal floodnums, they are connected
	int floodvalid;
} carea_t;

struct cmodel_state_s {
	int instance_refcount;      // how much users does this cmodel_state_t instance have
	struct mempool_s *mempool;

	const bspFormatDesc_t *cmap_bspFormat;

	char map_name[MAX_CONFIGSTRING_CHARS];
	unsigned int checksum;

	int numbrushsides;
	cbrushside_t *map_brushsides;

	int numshaderrefs;
	cshaderref_t *map_shaderrefs;

	int numplanes;
	cplane_t *map_planes;

	int numnodes;
	cnode_t *map_nodes;

	int numleafs;                   // = 1
	cleaf_t map_leaf_empty;         // allow leaf funcs to be called without a map
	cleaf_t *map_leafs;             // = &map_leaf_empty; instance-local (is not shared)

	int nummarkbrushes;
	cbrush_t **map_markbrushes;     // instance-local (is not shared)

	int numcmodels;
	cmodel_t map_cmodel_empty;
	cmodel_t *map_cmodels;          // = &map_cmodel_empty; instance-local (is not shared)
	vec3_t world_mins, world_maxs;

	int numbrushes;
	cbrush_t *map_brushes;          // instance-local (is not shared)

	int numfaces;
	cface_t *map_faces;             // instance-local (is not shared)

	uint8_t **map_face_brushdata;   // shared between instances contrary to map_faces to avoid duplication for no reasons.

	int nummarkfaces;
	cface_t **map_markfaces;        // instance-local (is not shared)

	vec3_t *map_verts;              // this will be freed
	int numvertexes;

	// each area has a list of portals that lead into other areas
	// when portals are closed, other areas may not be visible or
	// hearable even if the vis info says that it should be
	int numareas;                   // = 1
	carea_t map_area_empty;
	carea_t *map_areas;             // = &map_area_empty;
	int *map_areaportals;

	dvis_t *map_pvs, *map_phs;
	int map_visdatasize;

	uint8_t nullrow[MAX_CM_LEAFS / 8];

	int numentitychars;
	char map_entitystring_empty;
	char *map_entitystring;         // = &map_entitystring_empty;

	int floodvalid;

	uint8_t *cmod_base;

	// cm_trace.c
	cbrushside_t box_brushsides[6];
	cbrush_t box_brush[1];
	cbrush_t *box_markbrushes[1];
	cmodel_t box_cmodel[1];

	cbrushside_t oct_brushsides[10];
	cbrush_t oct_brush[1];
	cbrush_t *oct_markbrushes[1];
	cmodel_t oct_cmodel[1];

	int leaf_count, leaf_maxcount;
	int *leaf_list;
	float *leaf_mins, *leaf_maxs;
	int leaf_topnode;

	struct CMTraceComputer *traceComputer;
};

//=======================================================================

struct CMTraceComputer *CM_GetTraceComputer( cmodel_state_t *cms );

void CM_InitBoxHull( cmodel_state_t *cms );

void CM_InitOctagonHull( cmodel_state_t *cms );

void CM_BoundBrush( cmodel_state_t *cms, cbrush_t *brush );

void CM_FloodAreaConnections( cmodel_state_t *cms );

uint8_t *CM_DecompressVis( const uint8_t *in, int rowsize, uint8_t *decompressed );

static inline void CM_CopyRawToCMPlane( const cplane_t *src, cm_plane_t *dest ) {
	VectorCopy( src->normal, dest->normal );
#ifdef CM_USE_SSE
	dest->normal[3] = 0;
#endif
	dest->dist = src->dist;
	dest->type = src->type;
	dest->signbits = src->signbits;
}

static inline void CM_CopyCMToRawPlane( const cm_plane_t *src, cplane_t *dest ) {
	VectorCopy( src->normal, dest->normal );
	dest->dist = src->dist;
	dest->type = src->type;
	dest->signbits = src->signbits;
}

#ifdef __cplusplus
}
#endif

