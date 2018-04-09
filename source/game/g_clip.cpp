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

//
// g_clip.c - entity contact detection. (high level object sorting to reduce interaction tests)
//


//===============================================================================
//
//ENTITY AREA CHECKING
//
//FIXME: this use of "area" is different from the bsp file use
//===============================================================================

#define EDICT_NUM( n ) ( (edict_t *)( game.edicts + n ) )
#define NUM_FOR_EDICT( e ) ( ENTNUM( e ) )

#define AREA_GRID       128
#define AREA_GRIDNODES  ( AREA_GRID * AREA_GRID )
#define AREA_GRIDMINSIZE 64.0f  // minimum areagrid cell size, smaller values
// work better for lots of small objects, higher
// values for large objects

typedef struct
{
	link_t grid[AREA_GRIDNODES];
	link_t outside;
	vec3_t bias;
	vec3_t scale;
	vec3_t mins;
	vec3_t maxs;
	vec3_t size;
	int marknumber;

	// since the areagrid can have multiple references to one entity,
	// we should avoid extensive checking on entities already encountered
	int entmarknumber[MAX_EDICTS];
} areagrid_t;

static areagrid_t g_areagrid;

extern cvar_t *g_antilag;
extern cvar_t *g_antilag_maxtimedelta;

#define CFRAME_UPDATE_BACKUP    64  // copies of entity_state_t to keep buffered (1 second of backup at 62 fps).
#define CFRAME_UPDATE_MASK  ( CFRAME_UPDATE_BACKUP - 1 )

typedef struct c4clipedict_s {
	entity_state_t s;
	entity_shared_t r;
} c4clipedict_t;

//backups of all server frames areas and edicts
typedef struct c4frame_s {
	c4clipedict_t clipEdicts[MAX_EDICTS];   // fixme: there is a g_maxentities cvar. We have to adjust to it
	int numedicts;

	int64_t timestamp;
	int64_t framenum;
} c4frame_t;

c4frame_t sv_collisionframes[CFRAME_UPDATE_BACKUP];
static int64_t sv_collisionFrameNum = 0;

void GClip_BackUpCollisionFrame( void ) {
	c4frame_t *cframe;
	edict_t *svedict;
	int i;

	if( !g_antilag->integer ) {
		return;
	}

	// fixme: should check for any validation here?

	cframe = &sv_collisionframes[sv_collisionFrameNum & CFRAME_UPDATE_MASK];
	cframe->timestamp = game.serverTime;
	cframe->framenum = sv_collisionFrameNum;
	sv_collisionFrameNum++;

	//memset( cframe->clipEdicts, 0, sizeof(cframe->clipEdicts) );

	//backup edicts
	for( i = 0; i < game.numentities; i++ ) {
		svedict = &game.edicts[i];

		cframe->clipEdicts[i].r.inuse = svedict->r.inuse;
		cframe->clipEdicts[i].r.solid = svedict->r.solid;
		if( !svedict->r.inuse || svedict->r.solid == SOLID_NOT
			|| ( svedict->r.solid == SOLID_TRIGGER && !( i >= 1 && i <= gs.maxclients ) ) ) {
			continue;
		}

		cframe->clipEdicts[i].r = svedict->r;
		cframe->clipEdicts[i].s = svedict->s;
	}
	cframe->numedicts = game.numentities;
}

static c4clipedict_t *GClip_GetClipEdictForDeltaTime( int entNum, int deltaTime ) {
	static int index = 0;
	static c4clipedict_t clipEnts[8];
	static c4clipedict_t *clipent;
	static c4clipedict_t clipentNewer; // for interpolation
	c4frame_t *cframe = NULL;
	int64_t backTime, cframenum;
	unsigned bf, i;
	edict_t *ent = game.edicts + entNum;

	// pick one of the 8 slots to prevent overwritings
	clipent = &clipEnts[index];
	index = ( index + 1 ) & 7;

	if( !entNum || deltaTime >= 0 || !g_antilag->integer ) { // current time entity
		clipent->r = ent->r;
		clipent->s = ent->s;
		return clipent;
	}

	if( !ent->r.inuse || ent->r.solid == SOLID_NOT
		|| ( ent->r.solid == SOLID_TRIGGER && !( entNum >= 1 && entNum <= gs.maxclients ) ) ) {
		clipent->r = ent->r;
		clipent->s = ent->s;
		return clipent;
	}

	// clamp delta time inside the backed up limits
	backTime = abs( deltaTime );
	if( g_antilag_maxtimedelta->integer ) {
		if( g_antilag_maxtimedelta->integer < 0 ) {
			trap_Cvar_SetValue( "g_antilag_maxtimedelta", abs( g_antilag_maxtimedelta->integer ) );
		}
		if( backTime > (int64_t)g_antilag_maxtimedelta->integer ) {
			backTime = (int64_t)g_antilag_maxtimedelta->integer;
		}
	}

	// find the first snap with timestamp < than realtime - backtime
	cframenum = sv_collisionFrameNum;
	for( bf = 1; bf < CFRAME_UPDATE_BACKUP && bf < sv_collisionFrameNum; bf++ ) { // never overpass limits
		cframe = &sv_collisionframes[( cframenum - bf ) & CFRAME_UPDATE_MASK];

		// if solid has changed, we can't keep moving backwards
		if( ent->r.solid != cframe->clipEdicts[entNum].r.solid
			|| ent->r.inuse != cframe->clipEdicts[entNum].r.inuse ) {
			bf--;
			if( bf == 0 ) {
				// we can't step back from first
				cframe = NULL;
			} else {
				cframe = &sv_collisionframes[( cframenum - bf ) & CFRAME_UPDATE_MASK];
			}
			break;
		}

		if( game.serverTime >= cframe->timestamp + backTime ) {
			break;
		}
	}

	if( !cframe ) {
		// current time entity
		clipent->r = ent->r;
		clipent->s = ent->s;
		return clipent;
	}

	// setup with older for the data that is not interpolated
	*clipent = cframe->clipEdicts[entNum];

	// if we found an older than desired backtime frame, interpolate to find a more precise position.
	if( game.serverTime > cframe->timestamp + backTime ) {
		float lerpFrac;

		if( bf == 1 ) {
			// interpolate from 1st backed up to current
			lerpFrac = (float)( ( game.serverTime - backTime ) - cframe->timestamp )
					   / (float)( game.serverTime - cframe->timestamp );
			clipentNewer.r = ent->r;
			clipentNewer.s = ent->s;
		} else {
			// interpolate between 2 backed up
			c4frame_t *cframeNewer = &sv_collisionframes[( cframenum - ( bf - 1 ) ) & CFRAME_UPDATE_MASK];
			lerpFrac = (float)( ( game.serverTime - backTime ) - cframe->timestamp )
					   / (float)( cframeNewer->timestamp - cframe->timestamp );
			clipentNewer = cframeNewer->clipEdicts[entNum];
		}

#if 0
		G_Printf( "backTime:%i cframeBackTime:%i backFrames:%i lerfrac:%f\n",
				  backTime, game.serverTime - cframe->timestamp, backframes, lerpFrac );
#endif

		// interpolate
		VectorLerp( clipent->s.origin, lerpFrac, clipentNewer.s.origin, clipent->s.origin );
		VectorLerp( clipent->r.mins, lerpFrac, clipentNewer.r.mins, clipent->r.mins );
		VectorLerp( clipent->r.maxs, lerpFrac, clipentNewer.r.maxs, clipent->r.maxs );
		for( i = 0; i < 3; i++ )
			clipent->s.angles[i] = LerpAngle( clipent->s.angles[i], clipentNewer.s.angles[i], lerpFrac );
	}

#if 0
	G_Printf( "backTime:%i cframeBackTime:%i backFrames:%i\n", backTime,
			  game.serverTime - cframe->timestamp, backframes );
#endif

	// back time entity
	return clipent;
}

// ClearLink is used for new headnodes
static void GClip_ClearLink( link_t *l ) {
	l->prev = l->next = l;
	l->entNum = 0;
}

static void GClip_RemoveLink( link_t *l ) {
	l->next->prev = l->prev;
	l->prev->next = l->next;
	l->entNum = 0;
}

static void GClip_InsertLinkBefore( link_t *l, link_t *before, int entNum ) {
	l->next = before;
	l->prev = before->prev;
	l->prev->next = l;
	l->next->prev = l;
	l->entNum = entNum;
}

/*
* GClip_Init_AreaGrid
*/
static void GClip_Init_AreaGrid( areagrid_t *areagrid, const vec3_t world_mins, const vec3_t world_maxs ) {
	int i;

	// the areagrid_marknumber is not allowed to be 0
	if( areagrid->marknumber < 1 ) {
		areagrid->marknumber = 1;
	}

	// choose either the world box size, or a larger box to ensure the grid isn't too fine
	areagrid->size[0] = max( world_maxs[0] - world_mins[0], AREA_GRID * AREA_GRIDMINSIZE );
	areagrid->size[1] = max( world_maxs[1] - world_mins[1], AREA_GRID * AREA_GRIDMINSIZE );
	areagrid->size[2] = max( world_maxs[2] - world_mins[2], AREA_GRID * AREA_GRIDMINSIZE );

	// figure out the corners of such a box, centered at the center of the world box
	areagrid->mins[0] = ( world_mins[0] + world_maxs[0] - areagrid->size[0] ) * 0.5f;
	areagrid->mins[1] = ( world_mins[1] + world_maxs[1] - areagrid->size[1] ) * 0.5f;
	areagrid->mins[2] = ( world_mins[2] + world_maxs[2] - areagrid->size[2] ) * 0.5f;
	areagrid->maxs[0] = ( world_mins[0] + world_maxs[0] + areagrid->size[0] ) * 0.5f;
	areagrid->maxs[1] = ( world_mins[1] + world_maxs[1] + areagrid->size[1] ) * 0.5f;
	areagrid->maxs[2] = ( world_mins[2] + world_maxs[2] + areagrid->size[2] ) * 0.5f;

	// now calculate the actual useful info from that
	VectorNegate( areagrid->mins, areagrid->bias );
	areagrid->scale[0] = AREA_GRID / areagrid->size[0];
	areagrid->scale[1] = AREA_GRID / areagrid->size[1];
	areagrid->scale[2] = AREA_GRID / areagrid->size[2];

	GClip_ClearLink( &areagrid->outside );
	for( i = 0; i < AREA_GRIDNODES; i++ ) {
		GClip_ClearLink( &areagrid->grid[i] );
	}

	memset( areagrid->entmarknumber, 0, sizeof( areagrid->entmarknumber ) );

	if( developer->integer ) {
		Com_Printf( "areagrid settings: divisions %ix%ix1 : box %f %f %f "
					": %f %f %f size %f %f %f grid %f %f %f (mingrid %f)\n",
					AREA_GRID, AREA_GRID,
					areagrid->mins[0], areagrid->mins[1], areagrid->mins[2],
					areagrid->maxs[0], areagrid->maxs[1], areagrid->maxs[2],
					areagrid->size[0], areagrid->size[1], areagrid->size[2],
					1.0f / areagrid->scale[0], 1.0f / areagrid->scale[1], 1.0f / areagrid->scale[2],
					AREA_GRIDMINSIZE );
	}
}

/*
* GClip_UnlinkEntity_AreaGrid
*/
static void GClip_UnlinkEntity_AreaGrid( edict_t *ent ) {
	for( int i = 0; i < MAX_ENT_AREAS; i++ ) {
		if( !ent->areagrid[i].prev ) {
			break;
		}
		GClip_RemoveLink( &ent->areagrid[i] );
		ent->areagrid[i].prev = ent->areagrid[i].next = NULL;
	}
}

/*
* GClip_LinkEntity_AreaGrid
*/
static void GClip_LinkEntity_AreaGrid( areagrid_t *areagrid, edict_t *ent ) {
	link_t *grid;
	int igrid[3], igridmins[3], igridmaxs[3], gridnum, entitynumber;

	entitynumber = NUM_FOR_EDICT( ent );
	if( entitynumber <= 0 || entitynumber >= game.maxentities || EDICT_NUM( entitynumber ) != ent ) {
		Com_Printf( "GClip_LinkEntity_AreaGrid: invalid edict %p "
					"(edicts is %p, edict compared to prog->edicts is %i)\n",
					(void *)ent, game.edicts, entitynumber );
		return;
	}

	igridmins[0] = (int) floor( ( ent->r.absmin[0] + areagrid->bias[0] ) * areagrid->scale[0] );
	igridmins[1] = (int) floor( ( ent->r.absmin[1] + areagrid->bias[1] ) * areagrid->scale[1] );

	//igridmins[2] = (int) floor( (ent->r.absmin[2] + areagrid->bias[2]) * areagrid->scale[2] );
	igridmaxs[0] = (int) floor( ( ent->r.absmax[0] + areagrid->bias[0] ) * areagrid->scale[0] ) + 1;
	igridmaxs[1] = (int) floor( ( ent->r.absmax[1] + areagrid->bias[1] ) * areagrid->scale[1] ) + 1;

	//igridmaxs[2] = (int) floor( (ent->r.absmax[2] + areagrid->bias[2]) * areagrid->scale[2] ) + 1;
	if( igridmins[0] < 0 || igridmaxs[0] > AREA_GRID
		|| igridmins[1] < 0 || igridmaxs[1] > AREA_GRID
		|| ( ( igridmaxs[0] - igridmins[0] ) * ( igridmaxs[1] - igridmins[1] ) ) > MAX_ENT_AREAS ) {
		// wow, something outside the grid, store it as such
		GClip_InsertLinkBefore( &ent->areagrid[0], &areagrid->outside, entitynumber );
		return;
	}

	gridnum = 0;
	for( igrid[1] = igridmins[1]; igrid[1] < igridmaxs[1]; igrid[1]++ ) {
		grid = areagrid->grid + igrid[1] * AREA_GRID + igridmins[0];
		for( igrid[0] = igridmins[0]; igrid[0] < igridmaxs[0]; igrid[0]++, grid++, gridnum++ )
			GClip_InsertLinkBefore( &ent->areagrid[gridnum], grid, entitynumber );
	}
}

/*
* GClip_EntitiesInBox_AreaGrid
*/
static int GClip_EntitiesInBox_AreaGrid( areagrid_t *areagrid, const vec3_t mins, const vec3_t maxs,
										 int *list, int maxcount, int areatype, int timeDelta ) {
	int numlist;
	link_t *grid;
	link_t *l;
	c4clipedict_t *clipEnt;
	vec3_t paddedmins, paddedmaxs;
	int igrid[3], igridmins[3], igridmaxs[3];

	// LordHavoc: discovered this actually causes its own bugs (dm6 teleporters
	// being too close to info_teleport_destination)
	//VectorSet( paddedmins, mins[0] - 1.0f, mins[1] - 1.0f, mins[2] - 1.0f );
	//VectorSet( paddedmaxs, maxs[0] + 1.0f, maxs[1] + 1.0f, maxs[2] + 1.0f );
	VectorCopy( mins, paddedmins );
	VectorCopy( maxs, paddedmaxs );

	// FIXME: if areagrid_marknumber wraps, all entities need their
	// ent->priv.server->areagridmarknumber reset
	areagrid->marknumber++;

	igridmins[0] = (int) floor( ( paddedmins[0] + areagrid->bias[0] ) * areagrid->scale[0] );
	igridmins[1] = (int) floor( ( paddedmins[1] + areagrid->bias[1] ) * areagrid->scale[1] );

	//igridmins[2] = (int) ( (paddedmins[2] + areagrid->bias[2]) * areagrid->scale[2] );
	igridmaxs[0] = (int) floor( ( paddedmaxs[0] + areagrid->bias[0] ) * areagrid->scale[0] ) + 1;
	igridmaxs[1] = (int) floor( ( paddedmaxs[1] + areagrid->bias[1] ) * areagrid->scale[1] ) + 1;

	//igridmaxs[2] = (int) ( (paddedmaxs[2] + areagrid->bias[2]) * areagrid->scale[2] ) + 1;
	igridmins[0] = max( 0, igridmins[0] );
	igridmins[1] = max( 0, igridmins[1] );

	//igridmins[2] = max( 0, igridmins[2] );
	igridmaxs[0] = min( AREA_GRID, igridmaxs[0] );
	igridmaxs[1] = min( AREA_GRID, igridmaxs[1] );

	//igridmaxs[2] = min( AREA_GRID, igridmaxs[2] );

	// paranoid debugging
	//VectorSet( igridmins, 0, 0, 0 );VectorSet( igridmaxs, AREA_GRID, AREA_GRID, AREA_GRID );

	numlist = 0;

	// add entities not linked into areagrid because they are too big or
	// outside the grid bounds
	if( areagrid->outside.next ) {
		grid = &areagrid->outside;
		for( l = grid->next; l != grid; l = l->next ) {
			clipEnt = GClip_GetClipEdictForDeltaTime( l->entNum, timeDelta );

			if( areagrid->entmarknumber[l->entNum] == areagrid->marknumber ) {
				continue;
			}
			areagrid->entmarknumber[l->entNum] = areagrid->marknumber;

			if( !clipEnt->r.inuse ) {
				continue; // deactivated
			}
			if( areatype == AREA_TRIGGERS && clipEnt->r.solid != SOLID_TRIGGER ) {
				continue;
			}
			if( areatype == AREA_SOLID &&
				( clipEnt->r.solid == SOLID_TRIGGER || clipEnt->r.solid == SOLID_NOT ) ) {
				continue;
			}

			if( BoundsIntersect( paddedmins, paddedmaxs, clipEnt->r.absmin, clipEnt->r.absmax ) ) {
				if( numlist < maxcount ) {
					list[numlist] = l->entNum;
				}
				numlist++;
			}
		}
	}

	// add grid linked entities
	for( igrid[1] = igridmins[1]; igrid[1] < igridmaxs[1]; igrid[1]++ ) {
		grid = areagrid->grid + igrid[1] * AREA_GRID + igridmins[0];

		for( igrid[0] = igridmins[0]; igrid[0] < igridmaxs[0]; igrid[0]++, grid++ ) {
			if( !grid->next ) {
				continue;
			}

			for( l = grid->next; l != grid; l = l->next ) {
				clipEnt = GClip_GetClipEdictForDeltaTime( l->entNum, timeDelta );

				if( areagrid->entmarknumber[l->entNum] == areagrid->marknumber ) {
					continue;
				}
				areagrid->entmarknumber[l->entNum] = areagrid->marknumber;

				if( !clipEnt->r.inuse ) {
					continue; // deactivated
				}
				if( areatype == AREA_TRIGGERS && clipEnt->r.solid != SOLID_TRIGGER ) {
					continue;
				}
				if( areatype == AREA_SOLID &&
					( clipEnt->r.solid == SOLID_TRIGGER || clipEnt->r.solid == SOLID_NOT ) ) {
					continue;
				}

				if( BoundsIntersect( paddedmins, paddedmaxs, clipEnt->r.absmin, clipEnt->r.absmax ) ) {
					if( numlist < maxcount ) {
						list[numlist] = l->entNum;
					}
					numlist++;
				}
			}
		}
	}

	return numlist;
}


/*
* GClip_ClearWorld
* called after the world model has been loaded, before linking any entities
*/
void GClip_ClearWorld( void ) {
	vec3_t world_mins, world_maxs;
	struct cmodel_s *world_model;

	world_model = trap_CM_InlineModel( 0 );
	trap_CM_InlineModelBounds( world_model, world_mins, world_maxs );

	GClip_Init_AreaGrid( &g_areagrid, world_mins, world_maxs );
}

/*
* GClip_UnlinkEntity
* call before removing an entity, and before trying to move one,
* so it doesn't clip against itself
*/
void GClip_UnlinkEntity( edict_t *ent ) {
	if( !ent->linked ) {
		return; // not linked in anywhere
	}
	GClip_UnlinkEntity_AreaGrid( ent );
	ent->linked = false;
}

/*
* GClip_LinkEntity
* Needs to be called any time an entity changes origin, mins, maxs,
* or solid.  Automatically unlinks if needed.
* sets ent->v.absmin and ent->v.absmax
* sets ent->leafnums[] for pvs determination even if the entity
* is not solid
*/
#define MAX_TOTAL_ENT_LEAFS 128
void GClip_LinkEntity( edict_t *ent ) {
	int leafs[MAX_TOTAL_ENT_LEAFS];
	int clusters[MAX_TOTAL_ENT_LEAFS];
	int num_leafs;
	int i, j, k;
	int area;
	int topnode;

	GClip_UnlinkEntity( ent ); // unlink from old position

	if( ent == game.edicts ) {
		return; // don't add the world

	}
	if( !ent->r.inuse ) {
		return;
	}

	// set the size
	VectorSubtract( ent->r.maxs, ent->r.mins, ent->r.size );

	if( ent->r.solid == SOLID_NOT || ( ent->r.svflags & SVF_PROJECTILE ) ) {
		ent->s.solid = 0;
	} else if( ISBRUSHMODEL( ent->s.modelindex ) ) {
		// the only predicted SOLID_TRIGGER entity is ET_PUSH_TRIGGER
		if( ent->r.solid != SOLID_TRIGGER || ent->s.type == ET_PUSH_TRIGGER ) {
			ent->s.solid = SOLID_BMODEL;
		} else {
			ent->s.solid = 0;
		}
	} else {   // encode the size into the entity_state for client prediction
		if( ent->r.solid == SOLID_TRIGGER ) {
			ent->s.solid = 0;
		} else {
			// assume that x/y are equal and symetric
			i = ent->r.maxs[0] / 8;
			clamp( i, 1, 31 );

			// z is not symetric
			j = ( -ent->r.mins[2] ) / 8;
			clamp( j, 1, 31 );

			// and z maxs can be negative...
			k = ( ent->r.maxs[2] + 32 ) / 8;
			clamp( k, 1, 63 );

			ent->s.solid = ( k << 10 ) | ( j << 5 ) | i;
		}
	}

	// set the abs box
	if( ISBRUSHMODEL( ent->s.modelindex ) &&
		( ent->s.angles[0] || ent->s.angles[1] || ent->s.angles[2] ) ) {
		// expand for rotation
		float radius;

		radius = RadiusFromBounds( ent->r.mins, ent->r.maxs );

		for( i = 0; i < 3; i++ ) {
			ent->r.absmin[i] = ent->s.origin[i] - radius;
			ent->r.absmax[i] = ent->s.origin[i] + radius;
		}
	} else {   // axis aligned
		VectorAdd( ent->s.origin, ent->r.mins, ent->r.absmin );
		VectorAdd( ent->s.origin, ent->r.maxs, ent->r.absmax );
	}

	// because movement is clipped an epsilon away from an actual edge,
	// we must fully check even when bounding boxes don't quite touch
	ent->r.absmin[0] -= 1;
	ent->r.absmin[1] -= 1;
	ent->r.absmin[2] -= 1;
	ent->r.absmax[0] += 1;
	ent->r.absmax[1] += 1;
	ent->r.absmax[2] += 1;

	// link to PVS leafs
	ent->r.num_clusters = 0;
	ent->r.areanum = ent->r.areanum2 = -1;

	// get all leafs, including solids
	num_leafs = trap_CM_BoxLeafnums( ent->r.absmin, ent->r.absmax,
									 leafs, MAX_TOTAL_ENT_LEAFS, &topnode );

	// set areas
	for( i = 0; i < num_leafs; i++ ) {
		clusters[i] = trap_CM_LeafCluster( leafs[i] );
		area = trap_CM_LeafArea( leafs[i] );
		if( area > -1 ) {
			// doors may legally straggle two areas,
			// but nothing should ever need more than that
			if( ent->r.areanum > -1 && ent->r.areanum != area ) {
				if( ent->r.areanum2 > -1 && ent->r.areanum2 != area ) {
					if( developer->integer ) {
						G_Printf( "Object %s touching 3 areas at %f %f %f\n",
								  ( ent->classname ? ent->classname : "" ),
								  ent->r.absmin[0], ent->r.absmin[1], ent->r.absmin[2] );
					}
				}
				ent->r.areanum2 = area;
			} else {
				ent->r.areanum = area;
			}
		}
	}

	if( num_leafs >= MAX_TOTAL_ENT_LEAFS ) {
		// assume we missed some leafs, and mark by headnode
		ent->r.num_clusters = -1;
		ent->r.headnode = topnode;
	} else {
		ent->r.num_clusters = 0;
		for( i = 0; i < num_leafs; i++ ) {
			if( clusters[i] == -1 ) {
				continue; // not a visible leaf
			}
			for( j = 0; j < i; j++ )
				if( clusters[j] == clusters[i] ) {
					break;
				}
			if( j == i ) {
				if( ent->r.num_clusters == MAX_ENT_CLUSTERS ) {
					// assume we missed some leafs, and mark by headnode
					ent->r.num_clusters = -1;
					ent->r.headnode = topnode;
					break;
				}
				ent->r.clusternums[ent->r.num_clusters] = clusters[i];
				ent->r.leafnums[ent->r.num_clusters] = leafs[i];
				ent->r.num_clusters++;
			}
		}
	}

	// if first time, make sure old_origin is valid
	if( !ent->linkcount ) {
		ent->olds = ent->s;
	}
	ent->linkcount++;
	ent->linked = true;

	GClip_LinkEntity_AreaGrid( &g_areagrid, ent );
}

/*
* GClip_SetAreaPortalState
*
* Finds an areaportal leaf entity is connected with,
* and also finds two leafs from different areas connected
* with the same entity.
*/
void GClip_SetAreaPortalState( edict_t *ent, bool open ) {
	// entity must touch at least two areas
	if( ent->r.areanum < 0 || ent->r.areanum2 < 0 ) {
		return;
	}

	// change areaportal's state
	trap_CM_SetAreaPortalState( ent->r.areanum, ent->r.areanum2, open );
}


/*
* GClip_AreaEdicts
* fills in a table of edict ids with edicts that have bounding boxes
* that intersect the given area.  It is possible for a non-axial bmodel
* to be returned that doesn't actually intersect the area on an exact
* test.
* returns the number of pointers filled in
* ??? does this always return the world?
*/
int GClip_AreaEdicts( const vec3_t mins, const vec3_t maxs,
					  int *list, int maxcount, int areatype, int timeDelta ) {
	int count;

	count = GClip_EntitiesInBox_AreaGrid( &g_areagrid, mins, maxs,
										  list, maxcount, areatype, timeDelta );

	return min( count, maxcount );
}

/*
* GClip_CollisionModelForEntity
*
* Returns a collision model that can be used for testing or clipping an
* object of mins/maxs size.
*/
static struct cmodel_s *GClip_CollisionModelForEntity( entity_state_t *s, entity_shared_t *r ) {
	struct cmodel_s *model;

	if( ISBRUSHMODEL( s->modelindex ) ) {
		// explicit hulls in the BSP model
		model = trap_CM_InlineModel( s->modelindex );
		if( !model ) {
			G_Error( "MOVETYPE_PUSH with a non bsp model" );
		}

		return model;
	}

	// create a temp hull from bounding box sizes
	if( s->type == ET_PLAYER || s->type == ET_CORPSE ) {
		return trap_CM_OctagonModelForBBox( r->mins, r->maxs );
	} else {
		return trap_CM_ModelForBBox( r->mins, r->maxs );
	}
}


/*
* G_PointContents
* returns the CONTENTS_* value from the world at the given point.
* Quake 2 extends this to also check entities, to allow moving liquids
*/
static int GClip_PointContents( vec3_t p, int timeDelta ) {
	c4clipedict_t *clipEnt;
	int touch[MAX_EDICTS];
	int i, num;
	int contents, c2;
	struct cmodel_s *cmodel;

	// get base contents from world
	contents = trap_CM_TransformedPointContents( p, NULL, NULL, NULL );

	// or in contents from all the other entities
	num = GClip_AreaEdicts( p, p, touch, MAX_EDICTS, AREA_SOLID, timeDelta );

	for( i = 0; i < num; i++ ) {
		clipEnt = GClip_GetClipEdictForDeltaTime( touch[i], timeDelta );

		// might intersect, so do an exact clip
		cmodel = GClip_CollisionModelForEntity( &clipEnt->s, &clipEnt->r );

		c2 = trap_CM_TransformedPointContents( p, cmodel, clipEnt->s.origin, clipEnt->s.angles );
		contents |= c2;
	}

	return contents;
}

int G_PointContents( vec3_t p ) {
	return GClip_PointContents( p, 0 );
}

int G_PointContents4D( vec3_t p, int timeDelta ) {
	return GClip_PointContents( p, timeDelta );
}

//===========================================================================

typedef struct {
	vec3_t boxmins, boxmaxs;    // enclose the test object along entire move
	float *mins, *maxs;         // size of the moving object
	vec3_t mins2, maxs2;        // size when clipping against mosnters
	float *start, *end;
	trace_t *trace;
	int passent;
	int contentmask;
} moveclip_t;

/*
* GClip_ClipMoveToEntities
*/
/*static*/ void GClip_ClipMoveToEntities( moveclip_t *clip, int timeDelta ) {
	int i, num;
	c4clipedict_t *touch;
	int touchlist[MAX_EDICTS];
	trace_t trace;
	struct cmodel_s *cmodel;
	float *angles;

	num = GClip_AreaEdicts( clip->boxmins, clip->boxmaxs, touchlist, MAX_EDICTS, AREA_SOLID, timeDelta );

	// be careful, it is possible to have an entity in this
	// list removed before we get to it (killtriggered)
	for( i = 0; i < num; i++ ) {
		touch = GClip_GetClipEdictForDeltaTime( touchlist[i], timeDelta );
		if( clip->passent >= 0 ) {
			// when they are offseted in time, they can be a different pointer but be the same entity
			if( touch->s.number == clip->passent ) {
				continue;
			}
			if( touch->r.owner && ( touch->r.owner->s.number == clip->passent ) ) {
				continue;
			}
			if( game.edicts[clip->passent].r.owner
				&& ( game.edicts[clip->passent].r.owner->s.number == touch->s.number ) ) {
				continue;
			}

			// wsw : jal : never clipmove against SVF_PROJECTILE entities
			if( touch->r.svflags & SVF_PROJECTILE ) {
				continue;
			}
		}

		if( ( touch->r.svflags & SVF_CORPSE ) && !( clip->contentmask & CONTENTS_CORPSE ) ) {
			continue;
		}

		// might intersect, so do an exact clip
		cmodel = GClip_CollisionModelForEntity( &touch->s, &touch->r );

		if( ISBRUSHMODEL( touch->s.modelindex ) ) {
			angles = touch->s.angles;
		} else {
			angles = vec3_origin; // boxes don't rotate

		}
		trap_CM_TransformedBoxTrace( &trace, clip->start, clip->end,
									 clip->mins, clip->maxs, cmodel, clip->contentmask,
									 touch->s.origin, angles );

		if( trace.allsolid || trace.fraction < clip->trace->fraction ) {
			trace.ent = touch->s.number;
			*( clip->trace ) = trace;
		} else if( trace.startsolid ) {
			clip->trace->startsolid = true;
		}
		if( clip->trace->allsolid ) {
			return;
		}
	}
}


/*
* GClip_TraceBounds
*/
static void GClip_TraceBounds( vec3_t start, vec3_t mins, vec3_t maxs,
							   vec3_t end, vec3_t boxmins, vec3_t boxmaxs ) {
	int i;

	for( i = 0; i < 3; i++ ) {
		if( end[i] > start[i] ) {
			boxmins[i] = start[i] + mins[i] - 1;
			boxmaxs[i] = end[i] + maxs[i] + 1;
		} else {
			boxmins[i] = end[i] + mins[i] - 1;
			boxmaxs[i] = start[i] + maxs[i] + 1;
		}
	}
}

/*
* G_Trace
*
* Moves the given mins/maxs volume through the world from start to end.
*
* Passedict and edicts owned by passedict are explicitly not checked.
* ------------------------------------------------------------------
* mins and maxs are relative

* if the entire move stays in a solid volume, trace.allsolid will be set,
* trace.startsolid will be set, and trace.fraction will be 0

* if the starting point is in a solid, it will be allowed to move out
* to an open area

* passedict is explicitly excluded from clipping checks (normally NULL)
*/
static void GClip_Trace( trace_t *tr, vec3_t start, vec3_t mins, vec3_t maxs,
						 vec3_t end, edict_t *passedict, int contentmask, int timeDelta ) {
	moveclip_t clip;

	if( !tr ) {
		return;
	}

	if( !mins ) {
		mins = vec3_origin;
	}
	if( !maxs ) {
		maxs = vec3_origin;
	}

	if( passedict == world ) {
		memset( tr, 0, sizeof( trace_t ) );
		tr->fraction = 1;
		tr->ent = -1;
	} else {
		// clip to world
		trap_CM_TransformedBoxTrace( tr, start, end, mins, maxs, NULL, contentmask, NULL, NULL );
		tr->ent = tr->fraction < 1.0 ? world->s.number : -1;
		if( tr->fraction == 0 ) {
			return; // blocked by the world
		}
	}

	memset( &clip, 0, sizeof( moveclip_t ) );
	clip.trace = tr;
	clip.contentmask = contentmask;
	clip.start = start;
	clip.end = end;
	clip.mins = mins;
	clip.maxs = maxs;
	clip.passent = passedict ? ENTNUM( passedict ) : -1;

	VectorCopy( mins, clip.mins2 );
	VectorCopy( maxs, clip.maxs2 );

	// create the bounding box of the entire move
	GClip_TraceBounds( start, clip.mins2, clip.maxs2, end, clip.boxmins, clip.boxmaxs );

	// clip to other solid entities
	GClip_ClipMoveToEntities( &clip, timeDelta );
}

void G_Trace( trace_t *tr, vec3_t start, vec3_t mins, vec3_t maxs,
			  vec3_t end, edict_t *passedict, int contentmask ) {
	GClip_Trace( tr, start, mins, maxs, end, passedict, contentmask, 0 );
}

void G_Trace4D( trace_t *tr, vec3_t start, vec3_t mins, vec3_t maxs,
				vec3_t end, edict_t *passedict, int contentmask, int timeDelta ) {
	GClip_Trace( tr, start, mins, maxs, end, passedict, contentmask, timeDelta );
}

//===========================================================================


/*
* GClip_SetBrushModel
*
* Also sets mins and maxs for inline bmodels
*/
void GClip_SetBrushModel( edict_t *ent, const char *name ) {
	struct cmodel_s *cmodel;

	if( !name ) {
		G_Error( "GClip_SetBrushModel: NULL model in '%s'",
				 ent->classname ? ent->classname : "no classname" );
		return;
	}

	if( !name[0] ) {
		ent->s.modelindex = 0;
		return;
	}

	if( name[0] != '*' ) {
		ent->s.modelindex = trap_ModelIndex( name );
		return;
	}

	// if it is an inline model, get the size information for it

	// world model is special
	if( !strcmp( name, "*0" ) ) {
		ent->s.modelindex = 0;
		cmodel = trap_CM_InlineModel( 0 );
		trap_CM_InlineModelBounds( cmodel, ent->r.mins, ent->r.maxs );
		return;
	}

	// brush model
	ent->s.modelindex = trap_ModelIndex( name );
	assert( ent->s.modelindex == (unsigned int)atoi( name + 1 ) );
	cmodel = trap_CM_InlineModel( ent->s.modelindex );
	trap_CM_InlineModelBounds( cmodel, ent->r.mins, ent->r.maxs );
	GClip_LinkEntity( ent );
}

/*
* GClip_EntityContact
*/
bool GClip_EntityContact( vec3_t mins, vec3_t maxs, edict_t *ent ) {
	trace_t tr;
	struct cmodel_s *model;

	if( !mins ) {
		mins = vec3_origin;
	}
	if( !maxs ) {
		maxs = vec3_origin;
	}

	if( ISBRUSHMODEL( ent->s.modelindex ) ) {
		model = trap_CM_InlineModel( ent->s.modelindex );
		if( !model ) {
			G_Error( "MOVETYPE_PUSH with a non bsp model" );
		}

		trap_CM_TransformedBoxTrace( &tr, vec3_origin, vec3_origin, mins, maxs, model,
									 MASK_ALL, ent->s.origin, ent->s.angles );

		return tr.startsolid || tr.allsolid ? true : false;
	}

	return ( BoundsIntersect( mins, maxs, ent->r.absmin, ent->r.absmax ) ) == true;
}


/*
* GClip_TouchTriggers
*/
void GClip_TouchTriggers( edict_t *ent ) {
	int i, num;
	edict_t *hit;
	int touch[MAX_EDICTS];
	vec3_t mins, maxs;

	// dead things don't activate triggers!
	if( ent->r.client && G_IsDead( ent ) ) {
		return;
	}

	VectorAdd( ent->s.origin, ent->r.mins, mins );
	VectorAdd( ent->s.origin, ent->r.maxs, maxs );

	// FIXME: should be s.origin + mins and s.origin + maxs because of absmin and absmax padding?
	num = GClip_AreaEdicts( ent->r.absmin, ent->r.absmax, touch, MAX_EDICTS, AREA_TRIGGERS, 0 );

	// be careful, it is possible to have an entity in this
	// list removed before we get to it (killtriggered)
	for( i = 0; i < num; i++ ) {
		if( !ent->r.inuse ) {
			break;
		}

		hit = &game.edicts[touch[i]];
		if( !hit->r.inuse ) {
			continue;
		}

		if( !hit->touch && !hit->asTouchFunc ) {
			continue;
		}

		if( !hit->item && !GClip_EntityContact( mins, maxs, hit ) ) {
			continue;
		}

		G_CallTouch( hit, ent, NULL, 0 );
	}
}

void G_PMoveTouchTriggers( pmove_t *pm, vec3_t previous_origin ) {
	int i, num;
	edict_t *hit;
	int touch[MAX_EDICTS];
	vec3_t mins, maxs;
	edict_t *ent;

	if( pm->playerState->POVnum <= 0 || (int)pm->playerState->POVnum > gs.maxclients ) {
		return;
	}

	ent = game.edicts + pm->playerState->POVnum;
	if( !ent->r.client || G_IsDead( ent ) ) { // dead things don't activate triggers!
		return;
	}

	// update the entity with the new position
	VectorCopy( pm->playerState->pmove.origin, ent->s.origin );
	VectorCopy( pm->playerState->pmove.velocity, ent->velocity );
	VectorCopy( pm->playerState->viewangles, ent->s.angles );
	ent->viewheight = pm->playerState->viewheight;
	VectorCopy( pm->mins, ent->r.mins );
	VectorCopy( pm->maxs, ent->r.maxs );

	ent->waterlevel = pm->waterlevel;
	ent->watertype = pm->watertype;
	if( pm->groundentity == -1 ) {
		ent->groundentity = NULL;
	} else {
		ent->groundentity = &game.edicts[pm->groundentity];
		ent->groundentity_linkcount = ent->groundentity->linkcount;
	}

	GClip_LinkEntity( ent );

	// expand the search bounds to include the space between the previous and current origin
	for( i = 0; i < 3; i++ ) {
		if( previous_origin[i] < pm->playerState->pmove.origin[i] ) {
			mins[i] = previous_origin[i] + pm->maxs[i];
			if( mins[i] > pm->playerState->pmove.origin[i] + pm->mins[i] ) {
				mins[i] = pm->playerState->pmove.origin[i] + pm->mins[i];
			}
			maxs[i] = pm->playerState->pmove.origin[i] + pm->maxs[i];
		} else {
			mins[i] = pm->playerState->pmove.origin[i] + pm->mins[i];
			maxs[i] = previous_origin[i] + pm->mins[i];
			if( maxs[i] < pm->playerState->pmove.origin[i] + pm->maxs[i] ) {
				maxs[i] = pm->playerState->pmove.origin[i] + pm->maxs[i];
			}
		}
	}

	num = GClip_AreaEdicts( mins, maxs, touch, MAX_EDICTS, AREA_TRIGGERS, 0 );

	// be careful, it is possible to have an entity in this
	// list removed before we get to it (killtriggered)
	for( i = 0; i < num; i++ ) {
		if( !ent->r.inuse ) {
			break;
		}

		hit = &game.edicts[touch[i]];
		if( !hit->r.inuse ) {
			continue;
		}

		if( !hit->touch && !hit->asTouchFunc ) {
			continue;
		}

		if( !hit->item && !GClip_EntityContact( mins, maxs, hit ) ) {
			continue;
		}

		G_CallTouch( hit, ent, NULL, 0 );
	}
}

/*
* GClip_FindInRadius4D
* Returns entities that have their boxes within a spherical area
*/
int GClip_FindInRadius4D( vec3_t org, float rad, int *list, int maxcount, int timeDelta ) {
	int i, num;
	int listnum;
	edict_t *check;
	vec3_t mins, maxs;
	float rad_ = rad * 1.42;
	int touch[MAX_EDICTS];

	VectorSet( mins, org[0] - ( rad_ + 1 ), org[1] - ( rad_ + 1 ), org[2] - ( rad_ + 1 ) );
	VectorSet( maxs, org[0] + ( rad_ + 1 ), org[1] + ( rad_ + 1 ), org[2] + ( rad_ + 1 ) );

	listnum = 0;
	num = GClip_AreaEdicts( mins, maxs, touch, MAX_EDICTS, AREA_ALL, timeDelta );

	for( i = 0; i < num; i++ ) {
		check = EDICT_NUM( touch[i] );

		// make absolute mins and maxs
		if( !BoundsAndSphereIntersect( check->r.absmin, check->r.absmax, org, rad ) ) {
			continue;
		}

		if( listnum < maxcount ) {
			list[listnum] = touch[i];
		}
		listnum++;
	}

	return listnum;
}

/*
* GClip_FindInRadius
*
* Returns entities that have their boxes within a spherical area
*/
int GClip_FindInRadius( vec3_t org, float rad, int *list, int maxcount ) {
	return GClip_FindInRadius4D( org, rad, list, maxcount, 0 );
}

void G_SplashFrac4D( int entNum, vec3_t hitpoint, float maxradius, vec3_t pushdir,
					 float *kickFrac, float *dmgFrac, int timeDelta ) {
	c4clipedict_t *clipEnt;

	clipEnt = GClip_GetClipEdictForDeltaTime( entNum, timeDelta );
	G_SplashFrac( clipEnt->s.origin, clipEnt->r.mins, clipEnt->r.maxs, hitpoint,
				  maxradius, pushdir, kickFrac, dmgFrac );
}

void RS_SplashFrac4D( int entNum, vec3_t hitpoint, float maxradius, vec3_t pushdir, 
					  float *kickFrac, float *dmgFrac, int timeDelta, float splashFrac ) {
	c4clipedict_t *clipEnt;

	clipEnt = GClip_GetClipEdictForDeltaTime( entNum, timeDelta );
	RS_SplashFrac( clipEnt->s.origin, clipEnt->r.mins, clipEnt->r.maxs, hitpoint,
				   maxradius, pushdir, kickFrac, dmgFrac, splashFrac );
}

entity_state_t *G_GetEntityStateForDeltaTime( int entNum, int deltaTime ) {
	c4clipedict_t *clipEnt;

	if( entNum == -1 ) {
		return NULL;
	}

	assert( entNum >= 0 && entNum < MAX_EDICTS );

	clipEnt = GClip_GetClipEdictForDeltaTime( entNum, deltaTime );

	return &clipEnt->s;
}
