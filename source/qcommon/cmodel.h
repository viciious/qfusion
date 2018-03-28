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

typedef struct cmodel_state_s cmodel_state_t;

struct cmodel_s *CM_LoadMap( cmodel_state_t *cms, const char *name, bool clientload, unsigned *checksum );
struct cmodel_s *CM_InlineModel( cmodel_state_t *cms, int num ); // 1, 2, etc
char *CM_LoadMapMessage( char *name, char *message, int size );

int CM_NumClusters( cmodel_state_t *cms );
int CM_NumAreas( cmodel_state_t *cms );
int CM_NumInlineModels( cmodel_state_t *cms );
char *CM_EntityString( cmodel_state_t *cms );
int CM_EntityStringLen( cmodel_state_t *cms );
const char *CM_ShaderrefName( cmodel_state_t *cms, int ref );

// creates a clipping hull for an arbitrary bounding box
struct cmodel_s *CM_ModelForBBox( cmodel_state_t *cms, vec3_t mins, vec3_t maxs );
struct cmodel_s *CM_OctagonModelForBBox( cmodel_state_t *cms, vec3_t mins, vec3_t maxs );

void CM_InlineModelBounds( cmodel_state_t *cms, struct cmodel_s *cmodel, vec3_t mins, vec3_t maxs );

// returns an ORed contents mask
int CM_TransformedPointContents( cmodel_state_t *cms, vec3_t p, struct cmodel_s *cmodel, vec3_t origin, vec3_t angles );

void CM_TransformedBoxTrace( cmodel_state_t *cms, trace_t *tr, vec3_t start, vec3_t end, vec3_t mins, vec3_t maxs,
							 struct cmodel_s *cmodel, int brushmask, vec3_t origin, vec3_t angles );

int CM_ClusterRowSize( cmodel_state_t *cms );
int CM_AreaRowSize( cmodel_state_t *cms );
int CM_PointLeafnum( cmodel_state_t *cms, const vec3_t p );

// call with topnode set to the headnode, returns with topnode
// set to the first node that splits the box
int CM_BoxLeafnums( cmodel_state_t *cms, vec3_t mins, vec3_t maxs, int *list, int listsize, int *topnode );

int CM_LeafCluster( cmodel_state_t *cms, int leafnum );
int CM_LeafArea( cmodel_state_t *cms, int leafnum );

void CM_SetAreaPortalState( cmodel_state_t *cms, int area1, int area2, bool open );
bool CM_AreasConnected( cmodel_state_t *cms, int area1, int area2 );

int CM_WriteAreaBits( cmodel_state_t *cms, uint8_t *buffer );
void CM_ReadAreaBits( cmodel_state_t *cms, uint8_t *buffer );
bool CM_HeadnodeVisible( cmodel_state_t *cms, int headnode, uint8_t *visbits );

void CM_WritePortalState( cmodel_state_t *cms, int file );
void CM_ReadPortalState( cmodel_state_t *cms, int file );

void CM_MergePVS( cmodel_state_t *cms, const vec3_t org, uint8_t *out );
void CM_MergePHS( cmodel_state_t *cms, int cluster, uint8_t *out );
int CM_MergeVisSets( cmodel_state_t *cms, const vec3_t org, uint8_t *pvs, uint8_t *areabits );

bool CM_InPVS( cmodel_state_t *cms, const vec3_t p1, const vec3_t p2 );

bool CM_LeafsInPVS( cmodel_state_t *cms, int leafnum1, int leafnum2 );

//
cmodel_state_t *CM_New( void *mempool );

void CM_AddReference( cmodel_state_t *cms );
void CM_ReleaseReference( cmodel_state_t *cms );

//
void CM_Init( void );
void CM_Shutdown( void );

#ifdef __cplusplus
}
#endif