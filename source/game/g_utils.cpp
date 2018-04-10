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

// g_utils.c -- misc utility functions for game module

#include "g_local.h"

/*
==============================================================================

ZONE MEMORY ALLOCATION

There is never any space between memblocks, and there will never be two
contiguous free memblocks.

The rover can be left pointing at a non-empty block.

Ported over from Quake 1 and Quake 3.
==============================================================================
*/

#define TAG_FREE    0
#define TAG_LEVEL   1

#define ZONEID      0x1d4a11
#define MINFRAGMENT 64

typedef struct memblock_s
{
	int size;               // including the header and possibly tiny fragments
	int tag;                // a tag of 0 is a free block
	struct memblock_s       *next, *prev;
	int id;                 // should be ZONEID
} memblock_t;

typedef struct
{
	int size;           // total bytes malloced, including header
	int count, used;
	memblock_t blocklist;       // start / end cap for linked list
	memblock_t  *rover;
} memzone_t;

static memzone_t *levelzone;

/*
* G_Z_ClearZone
*/
static void G_Z_ClearZone( memzone_t *zone, int size ) {
	memblock_t  *block;

	// set the entire zone to one free block
	zone->blocklist.next = zone->blocklist.prev = block =
													  (memblock_t *)( (uint8_t *)zone + sizeof( memzone_t ) );
	zone->blocklist.tag = 1;    // in use block
	zone->blocklist.id = 0;
	zone->blocklist.size = 0;
	zone->size = size;
	zone->rover = block;
	zone->used = 0;
	zone->count = 0;

	block->prev = block->next = &zone->blocklist;
	block->tag = 0;         // free block
	block->id = ZONEID;
	block->size = size - sizeof( memzone_t );
}

/*
* G_Z_Free
*/
static void G_Z_Free( void *ptr, const char *filename, int fileline ) {
	memblock_t *block, *other;
	memzone_t *zone;

	if( !ptr ) {
		G_Error( "G_Z_Free: NULL pointer" );
	}

	block = (memblock_t *) ( (uint8_t *)ptr - sizeof( memblock_t ) );
	if( block->id != ZONEID ) {
		G_Error( "G_Z_Free: freed a pointer without ZONEID (file %s at line %i)", filename, fileline );
	}
	if( block->tag == 0 ) {
		G_Error( "G_Z_Free: freed a freed pointer (file %s at line %i)", filename, fileline );
	}

	// check the memory trash tester
	if( *(int *)( (uint8_t *)block + block->size - 4 ) != ZONEID ) {
		G_Error( "G_Z_Free: memory block wrote past end" );
	}

	zone = levelzone;
	zone->used -= block->size;
	zone->count--;

	block->tag = 0;     // mark as free

	other = block->prev;
	if( !other->tag ) {
		// merge with previous free block
		other->size += block->size;
		other->next = block->next;
		other->next->prev = other;
		if( block == zone->rover ) {
			zone->rover = other;
		}
		block = other;
	}

	other = block->next;
	if( !other->tag ) {
		// merge the next free block onto the end
		block->size += other->size;
		block->next = other->next;
		block->next->prev = block;
		if( other == zone->rover ) {
			zone->rover = block;
		}
	}
}

/*
* G_Z_TagMalloc
*/
static void *G_Z_TagMalloc( int size, int tag, const char *filename, int fileline ) {
	int extra;
	memblock_t *start, *rover, *newb, *base;
	memzone_t *zone;

	if( !tag ) {
		G_Error( "G_Z_TagMalloc: tried to use a 0 tag (file %s at line %i)", filename, fileline );
	}

	//
	// scan through the block list looking for the first free block
	// of sufficient size
	//
	size += sizeof( memblock_t ); // account for size of block header
	size += 4;                  // space for memory trash tester
	size = ( size + 3 ) & ~3;     // align to 32-bit boundary

	zone = levelzone;
	base = rover = zone->rover;
	start = base->prev;

	do {
		if( rover == start ) {  // scaned all the way around the list
			return NULL;
		}
		if( rover->tag ) {
			base = rover = rover->next;
		} else {
			rover = rover->next;
		}
	} while( base->tag || base->size < size );

	//
	// found a block big enough
	//
	extra = base->size - size;
	if( extra > MINFRAGMENT ) {
		// there will be a free fragment after the allocated block
		newb = (memblock_t *) ( (uint8_t *)base + size );
		newb->size = extra;
		newb->tag = 0;          // free block
		newb->prev = base;
		newb->id = ZONEID;
		newb->next = base->next;
		newb->next->prev = newb;
		base->next = newb;
		base->size = size;
	}

	base->tag = tag;                // no longer a free block
	zone->rover = base->next;   // next allocation will start looking here
	zone->used += base->size;
	zone->count++;
	base->id = ZONEID;

	// marker for memory trash testing
	*(int *)( (uint8_t *)base + base->size - 4 ) = ZONEID;

	return (void *) ( (uint8_t *)base + sizeof( memblock_t ) );
}

/*
* G_Z_Malloc
*/
static void *G_Z_Malloc( int size, const char *filename, int fileline ) {
	void    *buf;

	buf = G_Z_TagMalloc( size, TAG_LEVEL, filename, fileline );
	if( !buf ) {
		G_Error( "G_Z_Malloc: failed on allocation of %i bytes", size );
	}
	memset( buf, 0, size );

	return buf;
}

//==============================================================================

/*
* G_LevelInitPool
*/
void G_LevelInitPool( size_t size ) {
	G_LevelFreePool();

	levelzone = ( memzone_t * )G_Malloc( size );
	G_Z_ClearZone( levelzone, size );
}

/*
* G_LevelFreePool
*/
void G_LevelFreePool( void ) {
	if( levelzone ) {
		G_Free( levelzone );
		levelzone = NULL;
	}
}

/*
* G_LevelMalloc
*/
void *_G_LevelMalloc( size_t size, const char *filename, int fileline ) {
	return G_Z_Malloc( size, filename, fileline );
}

/*
* G_LevelFree
*/
void _G_LevelFree( void *data, const char *filename, int fileline ) {
	G_Z_Free( data, filename, fileline );
}

/*
* G_LevelCopyString
*/
char *_G_LevelCopyString( const char *in, const char *filename, int fileline ) {
	char *out;

	out = ( char * )_G_LevelMalloc( strlen( in ) + 1, filename, fileline );
	strcpy( out, in );
	return out;
}

/*
* G_LevelGarbageCollect
*/
void G_LevelGarbageCollect( void ) {
	//G_Z_Print( levelzone );
}

//==============================================================================

#define STRINGPOOL_SIZE         1024 * 1024
#define STRINGPOOL_HASH_SIZE    32

typedef struct g_poolstring_s {
	char *buf;
	struct g_poolstring_s *hash_next;
} g_poolstring_t;

static uint8_t *g_stringpool;
static size_t g_stringpool_offset;
static g_poolstring_t *g_stringpool_hash[STRINGPOOL_HASH_SIZE];

/*
* G_StringPoolInit
*
* Preallocates a memory region to permanently store level strings
*/
void G_StringPoolInit( void ) {
	memset( g_stringpool_hash, 0, sizeof( g_stringpool_hash ) );

	g_stringpool = ( uint8_t * )G_LevelMalloc( STRINGPOOL_SIZE );
	g_stringpool_offset = 0;
}

/*
* G_StringPoolHashKey
*/
static unsigned int G_StringPoolHashKey( const char *string ) {
	int i;
	unsigned int v;
	unsigned int c;

	v = 0;
	for( i = 0; string[i]; i++ ) {
		c = string[i];
		v = ( v + i ) * 37 + c;
	}

	return v % STRINGPOOL_HASH_SIZE;
}

/*
* G_RegisterLevelString
*
* Registers a unique string which is guaranteed to exist until the level reloads
*/
const char *_G_RegisterLevelString( const char *string, const char *filename, int fileline ) {
	size_t size;
	g_poolstring_t *ps;
	unsigned int hashkey;

	if( !string ) {
		return NULL;
	}
	if( !*string ) {
		return "";
	}

	size = strlen( string ) + 1;
	if( sizeof( *ps ) + size > STRINGPOOL_SIZE ) {
		G_Error( "G_RegisterLevelString: out of memory (str:%s at %s:%i)\n", string, filename, fileline );
		return NULL;
	}

	// find a matching registered string
	hashkey = G_StringPoolHashKey( string );
	for( ps = g_stringpool_hash[hashkey]; ps; ps = ps->hash_next ) {
		if( !strcmp( ps->buf, string ) ) {
			return ps->buf;
		}
	}

	// no match, register a new one
	ps = ( g_poolstring_t * )( g_stringpool + g_stringpool_offset );
	g_stringpool_offset += sizeof( *ps );

	ps->buf = ( char * )( g_stringpool + g_stringpool_offset );
	ps->hash_next = g_stringpool_hash[hashkey];
	g_stringpool_hash[hashkey] = ps;

	memcpy( ps->buf, string, size );
	g_stringpool_offset += size;

	return ps->buf;
}

//==============================================================================

/*
* G_AllocCreateNamesList
*/
char *G_AllocCreateNamesList( const char *path, const char *extension, const char separator ) {
	char separators[2];
	char name[MAX_CONFIGSTRING_CHARS];
	char buffer[MAX_STRING_CHARS], *s, *list;
	int numfiles, i, j, found, length, fulllength;

	if( !extension || !path ) {
		return NULL;
	}

	if( extension[0] != '.' || strlen( extension ) < 2 ) {
		return NULL;
	}

	if( ( numfiles = trap_FS_GetFileList( path, extension, NULL, 0, 0, 0 ) ) == 0 ) {
		return NULL;
	}

	separators[0] = separator;
	separators[1] = 0;

	//
	// do a first pass just for finding the full len of the list
	//

	i = 0;
	found = 0;
	length = 0;
	fulllength = 0;
	do {
		if( ( j = trap_FS_GetFileList( path, extension, buffer, sizeof( buffer ), i, numfiles ) ) == 0 ) {
			// can happen if the filename is too long to fit into the buffer or we're done
			i++;
			continue;
		}

		i += j;
		for( s = buffer; j > 0; j--, s += length + 1 ) {
			length = strlen( s );

			if( strlen( path ) + 1 + length >= MAX_CONFIGSTRING_CHARS ) {
				Com_Printf( "Warning: G_AllocCreateNamesList :file name too long: %s\n", s );
				continue;
			}

			Q_strncpyz( name, s, sizeof( name ) );
			COM_StripExtension( name );

			fulllength += strlen( name ) + 1;
			found++;
		}
	} while( i < numfiles );

	if( !found ) {
		return NULL;
	}

	//
	// Allocate a string for the full list and do a second pass to copy them in there
	//

	fulllength += 1;
	list = ( char * )G_Malloc( fulllength );

	i = 0;
	length = 0;
	do {
		if( ( j = trap_FS_GetFileList( path, extension, buffer, sizeof( buffer ), i, numfiles ) ) == 0 ) {
			// can happen if the filename is too long to fit into the buffer or we're done
			i++;
			continue;
		}

		i += j;
		for( s = buffer; j > 0; j--, s += length + 1 ) {
			length = strlen( s );

			if( strlen( path ) + 1 + length >= MAX_CONFIGSTRING_CHARS ) {
				continue;
			}

			Q_strncpyz( name, s, sizeof( name ) );
			COM_StripExtension( name );

			Q_strncatz( list, name, fulllength );
			Q_strncatz( list, separators, fulllength );
		}
	} while( i < numfiles );

	return list;
}

void G_ProjectSource( vec3_t point, vec3_t distance, vec3_t forward, vec3_t right, vec3_t result ) {
	result[0] = point[0] + forward[0] * distance[0] + right[0] * distance[1];
	result[1] = point[1] + forward[1] * distance[0] + right[1] * distance[1];
	result[2] = point[2] + forward[2] * distance[0] + right[2] * distance[1] + distance[2];
}


/*
* G_Find
*
* Searches all active entities for the next one that holds
* the matching string at fieldofs (use the FOFS() macro) in the structure.
*
* Searches beginning at the edict after from, or the beginning if NULL
* NULL will be returned if the end of the list is reached.
*
*/
edict_t *G_Find( edict_t *from, size_t fieldofs, const char *match ) {
	char *s;

	if( !from ) {
		from = world;
	} else {
		from++;
	}

	for(; from <= &game.edicts[game.numentities - 1]; from++ ) {
		if( !from->r.inuse ) {
			continue;
		}
		s = *(char **) ( (uint8_t *)from + fieldofs );
		if( !s ) {
			continue;
		}
		if( !Q_stricmp( s, match ) ) {
			return from;
		}
	}

	return NULL;
}

/*
* G_PickTarget
*
* Searches all active entities for the next one that holds
* the matching string at fieldofs (use the FOFS() macro) in the structure.
*
* Searches beginning at the edict after from, or the beginning if NULL
* NULL will be returned if the end of the list is reached.
*
*/
#define MAXCHOICES  8

edict_t *G_PickTarget( const char *targetname ) {
	edict_t *ent = NULL;
	int num_choices = 0;
	edict_t *choice[MAXCHOICES];

	if( !targetname ) {
		G_Printf( "G_PickTarget called with NULL targetname\n" );
		return NULL;
	}

	while( 1 ) {
		ent = G_Find( ent, FOFS( targetname ), targetname );
		if( !ent ) {
			break;
		}
		choice[num_choices++] = ent;
		if( num_choices == MAXCHOICES ) {
			break;
		}
	}

	if( !num_choices ) {
		G_Printf( "G_PickTarget: target %s not found\n", targetname );
		return NULL;
	}

	return choice[rand() % num_choices];
}



static void Think_Delay( edict_t *ent ) {
	G_UseTargets( ent, ent->activator );
	G_FreeEdict( ent );
}

/*
* G_UseTargets
*
* the global "activator" should be set to the entity that initiated the firing.
*
* If self.delay is set, a DelayedUse entity will be created that will actually
* do the SUB_UseTargets after that many seconds have passed.
*
* Centerprints any self.message to the activator.
*
* Search for (string)targetname in all entities that
* match (string)self.target and call their .use function
*
*/
void G_UseTargets( edict_t *ent, edict_t *activator ) {
	edict_t *t;

	//
	// check for a delay
	//
	if( ent->delay ) {
		// create a temp object to fire at a later time
		t = G_Spawn();
		t->classname = "delayed_use";
		t->nextThink = level.time + 1000 * ent->delay;
		t->think = Think_Delay;
		t->activator = activator;
		if( !activator ) {
			G_Printf( "Think_Delay with no activator\n" );
		}
		t->message = ent->message;
		t->target = ent->target;
		t->killtarget = ent->killtarget;
		return;
	}


	//
	// print the message
	//
	if( ent->message ) {
		G_CenterPrintMsg( activator, "%s", ent->message );

		if( ent->noise_index ) {
			G_Sound( activator, CHAN_AUTO, ent->noise_index, ATTN_NORM );
		} else {
			G_Sound( activator, CHAN_AUTO, trap_SoundIndex( S_WORLD_MESSAGE ), ATTN_NORM );
		}
	}

	//
	// set the help message
	//
	if( ent->helpmessage && ent->mapmessage_index <= MAX_HELPMESSAGES ) {
		G_SetPlayerHelpMessage( activator, ent->mapmessage_index );

		if( !ent->message ) {
			if( ent->noise_index ) {
				G_Sound( activator, CHAN_AUTO, ent->noise_index, ATTN_NORM );
			} else {
				G_Sound( activator, CHAN_AUTO, trap_SoundIndex( S_WORLD_MESSAGE ), ATTN_NORM );
			}
		}
	}

	//
	// kill killtargets
	//
	if( ent->killtarget ) {
		t = NULL;
		while( ( t = G_Find( t, FOFS( targetname ), ent->killtarget ) ) ) {
			G_FreeEdict( t );
			if( !ent->r.inuse ) {
				G_Printf( "entity was removed while using killtargets\n" );
				return;
			}
		}
	}

	//	G_Printf ("TARGET: activating %s\n", ent->target);

	//
	// fire targets
	//
	if( ent->target ) {
		t = NULL;
		while( ( t = G_Find( t, FOFS( targetname ), ent->target ) ) ) {
			if( t == ent ) {
				G_Printf( "WARNING: Entity used itself.\n" );
			} else {
				G_CallUse( t, ent, activator );
			}
			if( !ent->r.inuse ) {
				G_Printf( "entity was removed while using targets\n" );
				return;
			}
		}
	}
}


vec3_t VEC_UP       = { 0, -1, 0 };
vec3_t MOVEDIR_UP   = { 0, 0, 1 };
vec3_t VEC_DOWN     = { 0, -2, 0 };
vec3_t MOVEDIR_DOWN = { 0, 0, -1 };

void G_SetMovedir( vec3_t angles, vec3_t movedir ) {
	if( VectorCompare( angles, VEC_UP ) ) {
		VectorCopy( MOVEDIR_UP, movedir );
	} else if( VectorCompare( angles, VEC_DOWN ) ) {
		VectorCopy( MOVEDIR_DOWN, movedir );
	} else {
		AngleVectors( angles, movedir, NULL, NULL );
	}

	VectorClear( angles );
}


float vectoyaw( vec3_t vec ) {
	float yaw;

	if( vec[PITCH] == 0 ) {
		yaw = 0;
		if( vec[YAW] > 0 ) {
			yaw = 90;
		} else if( vec[YAW] < 0 ) {
			yaw = -90;
		}
	} else {
		yaw = RAD2DEG( atan2( vec[YAW], vec[PITCH] ) );
		if( yaw < 0 ) {
			yaw += 360;
		}
	}

	return yaw;
}


char *_G_CopyString( const char *in, const char *filename, int fileline ) {
	char *out;

	out = ( char * )trap_MemAlloc( strlen( in ) + 1, filename, fileline );
	strcpy( out, in );
	return out;
}

/*
* G_FreeEdict
*
* Marks the edict as free
*/
void G_FreeEdict( edict_t *ed ) {
	bool evt = ISEVENTENTITY( &ed->s );

	GClip_UnlinkEntity( ed );   // unlink from world

	AI_RemoveNavEntity( ed );
	G_FreeAI( ed );

	G_asReleaseEntityBehaviors( ed );

	memset( ed, 0, sizeof( *ed ) );
	ed->r.inuse = false;
	ed->s.number = ENTNUM( ed );
	ed->r.svflags = SVF_NOCLIENT;
	ed->scriptSpawned = false;

	if( !evt && ( level.spawnedTimeStamp != game.realtime ) ) {
		ed->freetime = game.realtime; // ET_EVENT or ET_SOUND don't need to wait to be reused
	}
}

/*
* G_InitEdict
*/
void G_InitEdict( edict_t *e ) {
	e->r.inuse = true;
	e->classname = NULL;
	e->gravity = 1.0;
	e->timeDelta = 0;
	e->deadflag = DEAD_NO;
	e->timeStamp = 0;
	e->scriptSpawned = false;

	memset( &e->s, 0, sizeof( entity_state_t ) );
	e->s.attenuation = ATTN_NORM;
	e->s.number = ENTNUM( e );

	G_asResetEntityBehaviors( e );

	// Reset AI intrinsic properties
	e->aiIntrinsicEnemyWeight = 0.0f;
	e->aiVisibilityDistance = 999999.9f;

	// mark all entities to not be sent by default
	e->r.svflags = SVF_NOCLIENT | (e->r.svflags & SVF_FAKECLIENT);

	// clear the old state data
	memset( &e->olds, 0, sizeof( e->olds ) );
	memset( &e->snap, 0, sizeof( e->snap ) );

	//wsw clean up the backpack counts
	memset( e->invpak, 0, sizeof( e->invpak ) );
}

/*
* G_Spawn
*
* Either finds a free edict, or allocates a new one.
* Try to avoid reusing an entity that was recently freed, because it
* can cause the client to think the entity morphed into something else
* instead of being removed and recreated, which can cause interpolated
* angles and bad trails.
*/
edict_t *G_Spawn( void ) {
	int i;
	edict_t *e, *freed;

	if( !level.canSpawnEntities ) {
		G_Printf( "WARNING: Spawning entity before map entities have been spawned\n" );
	}

	freed = NULL;
	e = &game.edicts[gs.maxclients + 1];
	for( i = gs.maxclients + 1; i < game.numentities; i++, e++ ) {
		if( e->r.inuse ) {
			continue;
		}

		// the first couple seconds of server time can involve a lot of
		// freeing and allocating, so relax the replacement policy
		if( e->freetime < level.spawnedTimeStamp + 2000 || game.realtime > e->freetime + 500 ) {
			G_InitEdict( e );
			return e;
		}

		// this is going to be our second chance to spawn an entity in case all free
		// entities have been freed only recently
		if( !freed ) {
			freed = e;
		}
	}

	if( i == game.maxentities ) {
		if( freed ) {
			G_InitEdict( freed );
			return freed;
		}
		G_Error( "G_Spawn: no free edicts" );
	}

	game.numentities++;

	trap_LocateEntities( game.edicts, sizeof( game.edicts[0] ), game.numentities, game.maxentities );

	G_InitEdict( e );

	return e;
}

/*
* G_AddEvent
*/
void G_AddEvent( edict_t *ent, int event, int parm, bool highPriority ) {
	if( !ent || ent == world || !ent->r.inuse ) {
		return;
	}
	if( !event ) {
		return;
	}

	int eventNum = ent->numEvents & 1;
	if( ent->eventPriority[eventNum] && !ent->eventPriority[( eventNum + 1 ) & 1] ) {
		eventNum = ( eventNum + 1 ) & 1; // prefer overwriting low priority events
	} else if( !highPriority && ent->eventPriority[eventNum] ) {
		return; // no low priority event to overwrite
	} else {
		ent->numEvents++; // numEvents is only used to vary the overwritten event

	}
	ent->s.events[eventNum] = event;
	ent->s.eventParms[eventNum] = parm & 0xFF;
	ent->eventPriority[eventNum] = highPriority;

	AI_RegisterEvent( ent, event, parm );
}

/*
* G_SpawnEvent
*/
edict_t *G_SpawnEvent( int event, int parm, vec3_t origin ) {
	edict_t *ent;

	ent = G_Spawn();
	ent->s.type = ET_EVENT;
	ent->r.solid = SOLID_NOT;
	ent->r.svflags &= ~SVF_NOCLIENT;
	if( origin != NULL ) {
		VectorCopy( origin, ent->s.origin );
	}
	G_AddEvent( ent, event, parm, true );

	GClip_LinkEntity( ent );

	return ent;
}

/*
* G_MorphEntityIntoEvent
*/
void G_MorphEntityIntoEvent( edict_t *ent, int event, int parm ) {
	ent->s.type = ET_EVENT;
	ent->r.solid = SOLID_NOT;
	ent->r.svflags &= ~SVF_PROJECTILE; // FIXME: Medar: should be remove all or remove this one elsewhere?
	ent->s.linearMovement = false;
	G_AddEvent( ent, event, parm, true );

	GClip_LinkEntity( ent );
}

/*
* G_InitMover
*/
void G_InitMover( edict_t *ent ) {
	ent->r.solid = SOLID_YES;
	ent->movetype = MOVETYPE_PUSH;
	ent->r.svflags &= ~SVF_NOCLIENT;

	GClip_SetBrushModel( ent, ent->model );
	G_PureModel( ent->model );

	if( ent->model2 ) {
		ent->s.modelindex2 = trap_ModelIndex( ent->model2 );
		G_PureModel( ent->model2 );
	}

	if( ent->light || !VectorCompare( ent->color, vec3_origin ) ) {
		int r, g, b, i;

		if( !ent->light ) {
			i = 100;
		} else {
			i = ent->light;
		}

		i /= 4;
		i = min( i, 255 );

		r = ent->color[0];
		if( r <= 1.0 ) {
			r *= 255;
		}
		clamp( r, 0, 255 );

		g = ent->color[1];
		if( g <= 1.0 ) {
			g *= 255;
		}
		clamp( g, 0, 255 );

		b = ent->color[2];
		if( b <= 1.0 ) {
			b *= 255;
		}
		clamp( b, 0, 255 );

		ent->s.light = COLOR_RGBA( r, g, b, i );
	}
}

/*
* G_CallThink
*/
void G_CallThink( edict_t *ent ) {
	if( ent->think ) {
		ent->think( ent );
	} else if( ent->scriptSpawned && ent->asThinkFunc ) {
		G_asCallMapEntityThink( ent );
	} else if( developer->integer ) {
		G_Printf( "NULL ent->think in %s\n", ent->classname ? ent->classname : va( "'no classname. Entity type is %i", ent->s.type ) );
	}
}

/*
* G_CallTouch
*/
void G_CallTouch( edict_t *self, edict_t *other, cplane_t *plane, int surfFlags ) {
	bool touched = false;

	if( self == other ) {
		return;
	}

	if( self->touch ) {
		touched = true;
		self->touch( self, other, plane, surfFlags );
	} else if( self->scriptSpawned && self->asTouchFunc ) {
		touched = true;
		G_asCallMapEntityTouch( self, other, plane, surfFlags );
	}

	if( touched && other->ai ) {
		AI_TouchedEntity( other, self );
	}
}

/*
* G_CallUse
*/
void G_CallUse( edict_t *self, edict_t *other, edict_t *activator ) {
	if( self->use ) {
		self->use( self, other, activator );
	} else if( self->scriptSpawned && self->asUseFunc ) {
		G_asCallMapEntityUse( self, other, activator );
	}
}

/*
* G_CallStop
*/
void G_CallStop( edict_t *self ) {
	if( self->stop ) {
		self->stop( self );
	} else if( self->scriptSpawned && self->asStopFunc ) {
		G_asCallMapEntityStop( self );
	}
}

/*
* G_CallPain
*/
void G_CallPain( edict_t *ent, edict_t *attacker, float kick, float damage ) {
	if( ent->ai ) {
		AI_Pain( ent, attacker, kick, damage );
	}

	if( ent->pain ) {
		ent->pain( ent, attacker, kick, damage );
	} else if( ent->scriptSpawned && ent->asPainFunc ) {
		G_asCallMapEntityPain( ent, attacker, kick, damage );
	}
}

/*
* G_CallDie
*/
void G_CallDie( edict_t *ent, edict_t *inflictor, edict_t *attacker, int damage, const vec3_t point ) {
	if( ent->die ) {
		ent->die( ent, inflictor, attacker, damage, point );
	} else if( ent->scriptSpawned && ent->asDieFunc ) {
		G_asCallMapEntityDie( ent, inflictor, attacker, damage, point );
	}
}


/*
* G_PlayerGender
* server doesn't know the model gender, so all are neutrals in console prints.
*/
int G_PlayerGender( edict_t *player ) {
	return GENDER_NEUTRAL;
}

/*
* G_PrintMsg
*
* NULL sends to all the message to all clients
*/
void G_PrintMsg( edict_t *ent, const char *format, ... ) {
	char msg[MAX_STRING_CHARS];
	va_list argptr;
	char *s, *p;

	va_start( argptr, format );
	Q_vsnprintfz( msg, sizeof( msg ), format, argptr );
	va_end( argptr );

	// double quotes are bad
	p = msg;
	while( ( p = strchr( p, '\"' ) ) != NULL )
		*p = '\'';

	s = va( "pr \"%s\"", msg );

	if( !ent ) {
		// mirror at server console
		if( dedicated->integer ) {
			G_Printf( "%s", msg );
		}
		trap_GameCmd( NULL, s );
	} else {
		if( ent->r.inuse && ent->r.client ) {
			trap_GameCmd( ent, s );
		}
	}
}

void G_PrintChasersf( edict_t *self, const char *format, ... ) {
	char msg[1024];
	va_list argptr;
	edict_t *ent;

	if( !self ) {
		return;
	}

	va_start( argptr, format );
	Q_vsnprintfz( msg, sizeof( msg ), format, argptr );
	va_end( argptr );

	for( ent = game.edicts + 1; PLAYERNUM( ent ) < gs.maxclients; ent++ ) {
		if( ent->r.client->resp.chase.active && ent->r.client->resp.chase.target == ENTNUM( self ) ) {
			G_PrintMsg( ent, "%s", msg );
		}
	}
}

/*
* G_ChatMsg
*
* NULL sends the message to all clients
*/
void G_ChatMsg( edict_t *ent, edict_t *who, bool teamonly, const char *format, ... ) {
	char msg[1024];
	va_list argptr;
	char *s, *p;

	va_start( argptr, format );
	Q_vsnprintfz( msg, sizeof( msg ), format, argptr );
	va_end( argptr );

	// double quotes are bad
	p = msg;
	while( ( p = strchr( p, '\"' ) ) != NULL )
		*p = '\'';

	s = va( "%s %i \"%s\"", ( who && teamonly ? "tch" : "ch" ), ( who ? ENTNUM( who ) : 0 ), msg );

	if( !ent ) {
		// mirror at server console
		if( dedicated->integer ) {
			if( !who ) {
				G_Printf( S_COLOR_GREEN "console: %s\n", msg );     // admin console
			} else if( !who->r.client ) {
				;   // wtf?
			} else if( teamonly ) {
				G_Printf( S_COLOR_YELLOW "[%s]" S_COLOR_WHITE "%s" S_COLOR_YELLOW ": %s\n",
						  who->r.client->ps.stats[STAT_TEAM] == TEAM_SPECTATOR ? "SPEC" : "TEAM", who->r.client->netname, msg );
			} else {
				G_Printf( "%s" S_COLOR_GREEN ": %s\n", who->r.client->netname, msg );
			}
		}

		if( who && teamonly ) {
			int i;

			for( i = 0; i < gs.maxclients; i++ ) {
				ent = game.edicts + 1 + i;

				if( ent->r.inuse && ent->r.client && trap_GetClientState( i ) >= CS_CONNECTED ) {
					if( ent->s.team == who->s.team ) {
						trap_GameCmd( ent, s );
					}
				}
			}
		} else {
			trap_GameCmd( NULL, s );
		}
	} else {
		if( ent->r.inuse && ent->r.client && trap_GetClientState( PLAYERNUM( ent ) ) >= CS_CONNECTED ) {
			if( !who || !teamonly || ent->s.team == who->s.team ) {
				trap_GameCmd( ent, s );
			}
		}
	}
}

/*
* G_CenterPrintMsg
*
* NULL sends to all the message to all clients
*/
void G_CenterPrintMsg( edict_t *ent, const char *format, ... ) {
	char msg[1024];
	char cmd[MAX_STRING_CHARS];
	va_list argptr;
	char *p;
	edict_t *other;

	va_start( argptr, format );
	Q_vsnprintfz( msg, sizeof( msg ), format, argptr );
	va_end( argptr );

	// double quotes are bad
	p = msg;
	while( ( p = strchr( p, '\"' ) ) != NULL )
		*p = '\'';

	Q_snprintfz( cmd, sizeof( cmd ), "cp \"%s\"", msg );
	trap_GameCmd( ent, cmd );

	if( ent != NULL ) {
		// add it to every player who's chasing this player
		for( other = game.edicts + 1; PLAYERNUM( other ) < gs.maxclients; other++ ) {
			if( !other->r.client || !other->r.inuse || !other->r.client->resp.chase.active ) {
				continue;
			}

			if( other->r.client->resp.chase.target == ENTNUM( ent ) ) {
				trap_GameCmd( other, cmd );
			}
		}
	}
}

/*
* G_CenterPrintFormatMsg
*
* MUST be passed NULL as the last variadic argument
*
* NULL sends to all the message to all clients
*/
void G_CenterPrintFormatMsg( edict_t *ent, int numVargs, const char *format, ... ) {
	int i;
	char cmd[MAX_STRING_CHARS];
	char arg_fmt[MAX_TOKEN_CHARS];
	va_list argptr;
	char *p, *arg_p;
	bool overflow = false;
	edict_t *other;

	if( !numVargs ) {
		// can't transmit formatted message with no arguments or
		// no strings to replace the placeholders
		return;
	}

	Q_strncpyz( cmd, "cpf ", sizeof( cmd ) );

	// double quotes are bad
	Q_strncpyz( arg_fmt, format, sizeof( arg_fmt ) );
	arg_p = arg_fmt;

	va_start( argptr, format );

	for( i = 0; i <= numVargs; i++ ) {
		size_t cmd_len;
		size_t arg_len;

		// double quotes are bad
		p = arg_p;
		if( !p ) {
			overflow = true;
			break;
		}

		while( ( p = strchr( p, '\"' ) ) != NULL )
			*p = '\'';

		cmd_len = strlen( cmd );
		arg_len = strlen( arg_p );
		if( arg_len > MAX_TOKEN_CHARS ) {
			overflow = true;
			break;
		}

		if( cmd_len + arg_len + 3 >= sizeof( cmd ) ) {
			overflow = true;
			break;
		}

		cmd[cmd_len + 0] = ' ';
		cmd[cmd_len + 1] = '"';
		memcpy( &cmd[cmd_len + 2], arg_p, arg_len );
		cmd[cmd_len + 2 + arg_len] = '"';
		cmd[cmd_len + 3 + arg_len] = '\0';

		arg_p = va_arg( argptr, char * );
	}

	va_end( argptr );

	if( overflow ) {
		// couldn't fit it all into the cmd buffer
		return;
	}

	trap_GameCmd( ent, cmd );

	if( ent != NULL ) {
		// add it to every player who's chasing this player
		for( other = game.edicts + 1; PLAYERNUM( other ) < gs.maxclients; other++ ) {
			if( !other->r.client || !other->r.inuse || !other->r.client->resp.chase.active ) {
				continue;
			}

			if( other->r.client->resp.chase.target == ENTNUM( ent ) ) {
				trap_GameCmd( other, cmd );
			}
		}
	}
}


/*
* G_Obituary
*
* Prints death message to all clients
*/
void G_Obituary( edict_t *victim, edict_t *attacker, int mod ) {
	if( victim && attacker ) {
		trap_GameCmd( NULL, va( "obry %i %i %i", (int)(victim - game.edicts), (int)(attacker - game.edicts), mod ) );
	}
}

/*
* G_UpdatePlayerMatchMsg
*
* Sends correct match msg to one client
* Must be called whenever client's team, ready status or chase mode changes
*/
void G_UpdatePlayerMatchMsg( edict_t *ent, bool force ) {
	matchmessage_t newmm;

	if( GS_MatchWaiting() ) {
		newmm = MATCHMESSAGE_WAITING_FOR_PLAYERS;
	} else if( GS_MatchState() > MATCH_STATE_PLAYTIME ) {
		newmm = MATCHMESSAGE_NONE;
	} else if( ent->s.team == TEAM_SPECTATOR ) {
		if( GS_HasChallengers() ) { // He is in the queue
			newmm = ( ent->r.client->queueTimeStamp ? MATCHMESSAGE_CHALLENGERS_QUEUE : MATCHMESSAGE_ENTER_CHALLENGERS_QUEUE );
		} else {
			newmm = ( ent->r.client->resp.chase.active ? MATCHMESSAGE_NONE : MATCHMESSAGE_SPECTATOR_MODES );
		}
	} else {
		if( GS_MatchState() == MATCH_STATE_WARMUP ) {
			newmm = ( !level.ready[PLAYERNUM( ent )] ? MATCHMESSAGE_GET_READY : MATCHMESSAGE_NONE );
		} else {
			newmm = MATCHMESSAGE_NONE;
		}
	}

	if( newmm != ent->r.client->level.matchmessage || force ) {
		ent->r.client->level.matchmessage = newmm;
		trap_GameCmd( ent, va( "mm %i", newmm ) );
	}
}

/*
* G_UpdatePlayerMatchMsg
*
* Sends correct match msg to every client
* Must be called whenever match state changes
*/
void G_UpdatePlayersMatchMsgs( void ) {
	int i;
	edict_t *cl_ent;

	for( i = 0; i < gs.maxclients; i++ ) {
		cl_ent = game.edicts + 1 + i;
		if( !cl_ent->r.inuse ) {
			continue;
		}
		G_UpdatePlayerMatchMsg( cl_ent );
	}
}

//==================================================
// MAP MESSAGES
//==================================================

/*
* G_RegisterHelpMessage
*/
unsigned G_RegisterHelpMessage( const char *str ) {
	unsigned i;

	if( !str || !*str ) {
		return 0;
	}

	for( i = 0; i < MAX_HELPMESSAGES; i++ ) {
		const char *cs = trap_GetConfigString( CS_HELPMESSAGES + i );
		if( !cs[0] ) {
			break;
		}
		if( !strcmp( cs, str ) ) {
			return i + 1;
		}
	}

	if( i < MAX_HELPMESSAGES ) {
		trap_ConfigString( CS_HELPMESSAGES + i, str );
	}
	return i + 1;
}

/*
* G_SetPlayerHelpMessage
*/
void G_SetPlayerHelpMessage( edict_t *ent, unsigned index, bool force ) {
	if( index > MAX_HELPMESSAGES ) {
		return;
	}
	if( !ent || !ent->r.client ) {
		return;
	}

	if( index != ent->r.client->level.helpmessage || force ) {
		ent->r.client->level.helpmessage = index;
		trap_GameCmd( ent, va( "mapmsg %i", index ) );
	}
}

//==================================================
// SOUNDS
//==================================================

/*
* _G_SpawnSound
*/
static edict_t *_G_SpawnSound( int channel, int soundindex, float attenuation ) {
	edict_t *ent;

	if( attenuation <= 0.0f ) {
		attenuation = ATTN_NONE;
	}

	ent = G_Spawn();
	ent->r.svflags &= ~SVF_NOCLIENT;
	ent->r.svflags |= SVF_SOUNDCULL;
	ent->s.type = ET_SOUNDEVENT;
	ent->s.attenuation = attenuation;
	ent->s.channel = channel;
	ent->s.sound = soundindex;

	return ent;
}

/*
* G_Sound
*/
edict_t *G_Sound( edict_t *owner, int channel, int soundindex, float attenuation ) {
	edict_t *ent;

	if( !soundindex ) {
		return NULL;
	}

	if( owner == NULL || owner == world ) {
		attenuation = ATTN_NONE;
	} else if( ISEVENTENTITY( &owner->s ) ) {
		return NULL; // event entities can't be owner of sound entities

	}
	ent = _G_SpawnSound( channel, soundindex, attenuation );
	if( attenuation != ATTN_NONE ) {
		assert( owner );
		ent->s.ownerNum = owner->s.number;

		if( owner->s.solid != SOLID_BMODEL ) {
			VectorCopy( owner->s.origin, ent->s.origin );
		} else {
			VectorAdd( owner->r.mins, owner->r.maxs, ent->s.origin );
			VectorMA( owner->s.origin, 0.5f, ent->s.origin, ent->s.origin );
		}
	} else {
		ent->r.svflags |= SVF_BROADCAST;
	}

	GClip_LinkEntity( ent );
	return ent;
}

/*
* G_PositionedSound
*/
edict_t *G_PositionedSound( vec3_t origin, int channel, int soundindex, float attenuation ) {
	edict_t *ent;

	if( !soundindex ) {
		return NULL;
	}

	if( origin == NULL ) {
		attenuation = ATTN_NONE;
	}

	ent = _G_SpawnSound( channel, soundindex, attenuation );
	if( attenuation != ATTN_NONE ) {
		assert( origin );
		ent->s.channel |= CHAN_FIXED;
		VectorCopy( origin, ent->s.origin );
	} else {
		ent->r.svflags |= SVF_BROADCAST;
	}

	GClip_LinkEntity( ent );
	return ent;
}

/*
* G_GlobalSound
*/
void G_GlobalSound( int channel, int soundindex ) {
	G_PositionedSound( NULL, channel, soundindex, ATTN_NONE );
}

/*
* G_LocalSound
*/
void G_LocalSound( edict_t *owner, int channel, int soundindex ) {
	edict_t *ent;

	if( !soundindex ) {
		return;
	}
	if( ISEVENTENTITY( &owner->s ) ) {
		return; // event entities can't be owner of sound entities
	}

	ent = _G_SpawnSound( channel, soundindex, ATTN_NONE );
	ent->s.ownerNum = ENTNUM( owner );
	ent->r.svflags |= SVF_ONLYOWNER | SVF_BROADCAST;

	GClip_LinkEntity( ent );
}

//==============================================================================
//
//Kill box
//
//==============================================================================

/*
* KillBox
*
* Kills all entities that would touch the proposed new positioning
* of ent.  Ent should be unlinked before calling this!
*/
bool KillBox( edict_t *ent ) {
	trace_t tr;
	bool telefragged = false;

	while( 1 ) {
		G_Trace( &tr, ent->s.origin, ent->r.mins, ent->r.maxs, ent->s.origin, world, MASK_PLAYERSOLID );
		if( ( tr.fraction == 1.0f && !tr.startsolid ) || tr.ent < 0 ) {
			return telefragged;
		}

		if( tr.ent == ENTNUM( world ) ) {
			return telefragged; // found the world (but a player could be in there too). suicide?

		}
		// nail it
		G_Damage( &game.edicts[tr.ent], ent, ent, vec3_origin, vec3_origin, ent->s.origin, 100000, 0, 0, DAMAGE_NO_PROTECTION, MOD_TELEFRAG );
		telefragged = true;

		// if we didn't kill it, fail
		if( game.edicts[tr.ent].r.solid ) {
			return telefragged;
		}
	}

	return telefragged; // all clear
}

/*
* LookAtKillerYAW
* returns the YAW angle to look at our killer
*/
float LookAtKillerYAW( edict_t *self, edict_t *inflictor, edict_t *attacker ) {
	vec3_t dir;
	float killer_yaw;

	if( attacker && attacker != world && attacker != self ) {
		VectorSubtract( attacker->s.origin, self->s.origin, dir );
	} else if( inflictor && inflictor != world && inflictor != self ) {
		VectorSubtract( inflictor->s.origin, self->s.origin, dir );
	} else {
		killer_yaw = self->s.angles[YAW];
		return killer_yaw;
	}

	if( dir[0] ) {
		killer_yaw = RAD2DEG( atan2( dir[1], dir[0] ) );
	} else {
		killer_yaw = 0;
		if( dir[1] > 0 ) {
			killer_yaw = 90;
		} else if( dir[1] < 0 ) {
			killer_yaw = -90;
		}
	}
	if( killer_yaw < 0 ) {
		killer_yaw += 360;
	}

	return killer_yaw;
}

//==============================================================================
//
//		Warsow: more miscelanea tools
//
//==============================================================================

/*
* G_SpawnTeleportEffect
*/
static void G_SpawnTeleportEffect( edict_t *ent, bool respawn, bool in ) {
	edict_t *event;

	if( !ent || !ent->r.client ) {
		return;
	}

	if( trap_GetClientState( PLAYERNUM( ent ) ) < CS_SPAWNED || ent->r.solid == SOLID_NOT ) {
		return;
	}

	// add a teleportation effect
	event = G_SpawnEvent( respawn ? EV_PLAYER_RESPAWN : ( in ? EV_PLAYER_TELEPORT_IN : EV_PLAYER_TELEPORT_OUT ), 0, ent->s.origin );
	event->s.ownerNum = ENTNUM( ent );
}

void G_TeleportEffect( edict_t *ent, bool in ) {
	G_SpawnTeleportEffect( ent, false, in );
}

void G_RespawnEffect( edict_t *ent ) {
	G_SpawnTeleportEffect( ent, true, false );
}

/*
* G_SolidMaskForEnt
*/
int G_SolidMaskForEnt( edict_t *ent ) {
	int solidmask;
	if( AI_GetType( ent->ai ) == AI_ISMONSTER ) {
		solidmask = MASK_MONSTERSOLID;
	} else {
		solidmask = ent->r.clipmask ? ent->r.clipmask : MASK_SOLID;
	}

	return solidmask;
}

/*
* G_CheckEntGround
*/
void G_CheckGround( edict_t *ent ) {
	vec3_t point;
	trace_t trace;

	if( ent->flags & ( FL_SWIM | FL_FLY ) ) {
		ent->groundentity = NULL;
		ent->groundentity_linkcount = 0;
		return;
	}

	if( ent->r.client && ent->velocity[2] > 180 ) {
		ent->groundentity = NULL;
		ent->groundentity_linkcount = 0;
		return;
	}

	// if the hull point one-quarter unit down is solid the entity is on ground
	point[0] = ent->s.origin[0];
	point[1] = ent->s.origin[1];
	point[2] = ent->s.origin[2] - 0.25;

	G_Trace( &trace, ent->s.origin, ent->r.mins, ent->r.maxs, point, ent, G_SolidMaskForEnt( ent ) );

	// check steepness
	if( !ISWALKABLEPLANE( &trace.plane ) && !trace.startsolid ) {
		ent->groundentity = NULL;
		ent->groundentity_linkcount = 0;
		return;
	}

	if( ( ent->velocity[2] > 1 && !ent->r.client ) && !trace.startsolid ) {
		ent->groundentity = NULL;
		ent->groundentity_linkcount = 0;
		return;
	}

	if( !trace.startsolid && !trace.allsolid ) {
		//VectorCopy( trace.endpos, ent->s.origin );
		ent->groundentity = &game.edicts[trace.ent];
		ent->groundentity_linkcount = ent->groundentity->linkcount;
		if( ent->velocity[2] < 0 ) {
			ent->velocity[2] = 0;
		}
	}
}

/*
* G_CategorizePosition
*/
void G_CategorizePosition( edict_t *ent ) {
	vec3_t point;
	int cont;

	//
	// get waterlevel
	//
	point[0] = ent->s.origin[0];
	point[1] = ent->s.origin[1];
	point[2] = ent->s.origin[2] + ent->r.mins[2] + 1;
	cont = G_PointContents( point );

	if( !( cont & MASK_WATER ) ) {
		ent->waterlevel = 0;
		ent->watertype = 0;
		return;
	}

	ent->watertype = cont;
	ent->waterlevel = 1;
	point[2] += 26;
	cont = G_PointContents( point );
	if( !( cont & MASK_WATER ) ) {
		return;
	}

	ent->waterlevel = 2;
	point[2] += 22;
	cont = G_PointContents( point );
	if( cont & MASK_WATER ) {
		ent->waterlevel = 3;
	}
}

/*
* G_DropToFloor
*/
void G_DropToFloor( edict_t *ent ) {
	vec3_t end;
	trace_t trace;

	ent->s.origin[2] += 1;
	VectorCopy( ent->s.origin, end );
	end[2] -= 256;

	G_Trace( &trace, ent->s.origin, ent->r.mins, ent->r.maxs, end, ent, G_SolidMaskForEnt( ent ) );

	if( trace.fraction == 1 || trace.allsolid ) {
		return;
	}

	VectorCopy( trace.endpos, ent->s.origin );

	GClip_LinkEntity( ent );
	G_CheckGround( ent );
	G_CategorizePosition( ent );
}

/*
* G_DropSpawnpointToFloor
*/
void G_DropSpawnpointToFloor( edict_t *ent ) {
	vec3_t start, end;
	trace_t trace;

	VectorCopy( ent->s.origin, start );
	start[2] += 16;
	VectorCopy( ent->s.origin, end );
	end[2] -= 16000;

	G_Trace( &trace, start, playerbox_stand_mins, playerbox_stand_maxs, end, ent, MASK_PLAYERSOLID );
	if( trace.startsolid || trace.allsolid ) {
		G_Printf( "Warning: %s %s spawns inside solid. Inhibited\n", ent->classname, vtos( ent->s.origin ) );
		G_FreeEdict( ent );
		return;
	}

	if( ent->spawnflags & 1 ) { //  floating items flag, we test that they are not inside solid too
		return;
	}

	if( trace.fraction < 1.0f ) {
		VectorMA( trace.endpos, 1.0f, trace.plane.normal, ent->s.origin );
	}
}

/*
* G_CheckBottom
*
* Returns false if any part of the bottom of the entity is off an edge that
* is not a staircase.
*
*/
int c_yes, c_no;
bool G_CheckBottom( edict_t *ent ) {
	vec3_t mins, maxs, start, stop;
	trace_t trace;
	int x, y;
	float mid, bottom;

	VectorAdd( ent->s.origin, ent->r.mins, mins );
	VectorAdd( ent->s.origin, ent->r.maxs, maxs );

	// if all of the points under the corners are solid world, don't bother
	// with the tougher checks
	// the corners must be within 16 of the midpoint
	start[2] = mins[2] - 1;
	for( x = 0; x <= 1; x++ )
		for( y = 0; y <= 1; y++ ) {
			start[0] = x ? maxs[0] : mins[0];
			start[1] = y ? maxs[1] : mins[1];
			if( G_PointContents( start ) != CONTENTS_SOLID ) {
				goto realcheck;
			}
		}

	c_yes++;
	return true;       // we got out easy

realcheck:
	c_no++;

	//
	// check it for real...
	//
	start[2] = mins[2];

	// the midpoint must be within 16 of the bottom
	start[0] = stop[0] = ( mins[0] + maxs[0] ) * 0.5;
	start[1] = stop[1] = ( mins[1] + maxs[1] ) * 0.5;
	stop[2] = start[2] - 2 * STEPSIZE;
	G_Trace( &trace, start, vec3_origin, vec3_origin, stop, ent, G_SolidMaskForEnt( ent ) );

	if( trace.fraction == 1.0 ) {
		return false;
	}
	mid = bottom = trace.endpos[2];

	// the corners must be within 16 of the midpoint
	for( x = 0; x <= 1; x++ ) {
		for( y = 0; y <= 1; y++ ) {
			start[0] = stop[0] = x ? maxs[0] : mins[0];
			start[1] = stop[1] = y ? maxs[1] : mins[1];

			G_Trace( &trace, start, vec3_origin, vec3_origin, stop, ent, G_SolidMaskForEnt( ent ) );

			if( trace.fraction != 1.0 && trace.endpos[2] > bottom ) {
				bottom = trace.endpos[2];
			}
			if( trace.fraction == 1.0 || mid - trace.endpos[2] > STEPSIZE ) {
				return false;
			}
		}
	}

	c_yes++;
	return true;
}

/*
* G_SetBoundsForSpanEntity
*
* Set origin and origin2 and then call this before linkEntity
* for laser entities for proper clipping against world leafs/clusters.
*/
void G_SetBoundsForSpanEntity( edict_t *ent, vec_t size ) {
	vec3_t sizeVec;

	VectorSet( sizeVec, size, size, size );
	ClearBounds( ent->r.absmin, ent->r.absmax );
	AddPointToBounds( ent->s.origin, ent->r.absmin, ent->r.absmax );
	AddPointToBounds( ent->s.origin2, ent->r.absmin, ent->r.absmax );
	VectorSubtract( ent->r.absmin, sizeVec, ent->r.absmin );
	VectorAdd( ent->r.absmax, sizeVec, ent->r.absmax );
	VectorSubtract( ent->r.absmin, ent->s.origin, ent->r.mins );
	VectorSubtract( ent->r.absmax, ent->s.origin, ent->r.maxs );
}

/*
* G_ReleaseClientPSEvent
*/
void G_ReleaseClientPSEvent( gclient_t *client ) {
	int i;

	if( client ) {
		for( i = 0; i < 2; i++ ) {
			if( client->resp.eventsCurrent < client->resp.eventsHead ) {
				client->ps.event[i] = client->resp.events[client->resp.eventsCurrent & MAX_CLIENT_EVENTS_MASK] & 127;
				client->ps.eventParm[i] = ( client->resp.events[client->resp.eventsCurrent & MAX_CLIENT_EVENTS_MASK] >> 8 ) & 0xFF;
				client->resp.eventsCurrent++;
			} else {
				client->ps.event[i] = PSEV_NONE;
				client->ps.eventParm[i] = 0;
			}
		}
	}
}

/*
* G_AddPlayerStateEvent
* This event is only sent to this client inside its player_state_t.
*/
void G_AddPlayerStateEvent( gclient_t *client, int event, int parm ) {
	int eventdata;
	if( client ) {
		if( !event || event > PSEV_MAX_EVENTS || parm > 0xFF ) {
			return;
		}
		if( client ) {
			eventdata = ( ( event & 0xFF ) | ( parm & 0xFF ) << 8 );
			client->resp.events[client->resp.eventsHead & MAX_CLIENT_EVENTS_MASK] = eventdata;
			client->resp.eventsHead++;
		}
	}
}

/*
* G_ClearPlayerStateEvents
*/
void G_ClearPlayerStateEvents( gclient_t *client ) {
	if( client ) {
		memset( client->resp.events, PSEV_NONE, sizeof( client->resp.events ) );
		client->resp.eventsCurrent = client->resp.eventsHead = 0;
	}
}

/*
* G_PlayerForText
* Returns player matching given text. It can be either number of the player or player's name.
*/
edict_t *G_PlayerForText( const char *text ) {
	int pnum;

	if( !text || !text[0] ) {
		return NULL;
	}

	pnum = atoi( text );

	if( !Q_stricmp( text, va( "%i", pnum ) ) && pnum >= 0 && pnum < gs.maxclients && game.edicts[pnum + 1].r.inuse ) {
		return &game.edicts[atoi( text ) + 1];
	} else {
		int i;
		edict_t *e;
		char colorless[MAX_INFO_VALUE];

		Q_strncpyz( colorless, COM_RemoveColorTokens( text ), sizeof( colorless ) );

		// check if it's a known player name
		for( i = 0, e = game.edicts + 1; i < gs.maxclients; i++, e++ ) {
			if( !e->r.inuse ) {
				continue;
			}

			if( !Q_stricmp( colorless, COM_RemoveColorTokens( e->r.client->netname ) ) ) {
				return e;
			}
		}

		// nothing found
		return NULL;
	}
}

/*
* G_AnnouncerSound - sends inmediatly. queue client side (excepting at player's ps events queue)
*/
void G_AnnouncerSound( edict_t *targ, int soundindex, int team, bool queued, edict_t *ignore ) {
	int psev = queued ? PSEV_ANNOUNCER_QUEUED : PSEV_ANNOUNCER;
	int playerTeam;

	if( targ ) { // only for a given player
		if( !targ->r.client || trap_GetClientState( PLAYERNUM( targ ) ) < CS_SPAWNED ) {
			return;
		}

		if( targ == ignore ) {
			return;
		}

		G_AddPlayerStateEvent( targ->r.client, psev, soundindex );
	} else {   // add it to all players
		edict_t *ent;

		for( ent = game.edicts + 1; PLAYERNUM( ent ) < gs.maxclients; ent++ ) {
			if( !ent->r.inuse || trap_GetClientState( PLAYERNUM( ent ) ) < CS_SPAWNED ) {
				continue;
			}

			if( ent == ignore ) {
				continue;
			}

			// team filter
			if( team >= TEAM_SPECTATOR && team < GS_MAX_TEAMS ) {
				playerTeam = ent->s.team;

				// if in chasecam, assume the player is in the chased player team
				if( playerTeam == TEAM_SPECTATOR && ent->r.client->resp.chase.active
					&& ent->r.client->resp.chase.target > 0 ) {
					playerTeam = game.edicts[ent->r.client->resp.chase.target].s.team;
				}

				if( playerTeam != team ) {
					continue;
				}
			}

			G_AddPlayerStateEvent( ent->r.client, psev, soundindex );
		}
	}
}

/*
* G_PureSound
*/
void G_PureSound( const char *sound ) {
	assert( sound && sound[0] && strlen( sound ) < MAX_CONFIGSTRING_CHARS );

	if( sound[0] == '*' ) {
		// sexed sounds
		// jal : this isn't correct. Sexed sounds don't have the full path because
		// the path depends on the model, so how can they be pure anyway?
		trap_PureSound( sound + 1 );
	} else {
		trap_PureSound( sound );
	}
}

/*
* G_PureModel
*/
void G_PureModel( const char *model ) {
	assert( model && model[0] && strlen( model ) < MAX_CONFIGSTRING_CHARS );

	trap_PureModel( model );
}

/*
* G_PrecacheWeapondef
*/
void G_PrecacheWeapondef( int weapon ) {	
	assert( weapon < ( MAX_WEAPONDEFS / 4 ) );

	bool race, strong;
	for( int index = weapon; index < MAX_WEAPONDEFS; index += MAX_WEAPONDEFS / 4 )
	{
		// see CG_OverrideWeapondef, uses same operations
		weapon = index % ( MAX_WEAPONDEFS / 4 );
		race = index > ( MAX_WEAPONDEFS / 2 );
		strong = ( index % ( MAX_WEAPONDEFS / 2 ) ) > ( MAX_WEAPONDEFS / 4 );

		const auto *weaponDef = GS_GetWeaponDefExt( weapon, race );
		const auto *firedef = strong ? &weaponDef->firedef : &weaponDef->firedef_weak;
		
		char cstring[MAX_CONFIGSTRING_CHARS];

		Q_snprintfz( cstring, sizeof( cstring ), "%i %i %u %u %u %u %u %i %i %i",
					 firedef->usage_count,
					 firedef->projectile_count,
					 firedef->weaponup_time,
					 firedef->weapondown_time,
					 firedef->reload_time,
					 firedef->cooldown_time,
					 firedef->timeout,
					 firedef->speed,
					 firedef->spread,
					 firedef->v_spread
					 );

		trap_ConfigString( CS_WEAPONDEFS + index, cstring );
	}
}

#ifdef WEAPONDEFS_FROM_DISK

#define WEAPONDEF_NUMPARMS 20
static bool G_ParseFiredefFile( uint8_t *buf, int weapon, firedef_t *firedef ) {
	char *ptr, *token;
	int count = 0;
	float parm[WEAPONDEF_NUMPARMS];

	// jal: this is quite ugly.
	ptr = ( char * )buf;
	while( ptr ) {
		token = COM_ParseExt( &ptr, true );
		if( !token[0] ) {
			break;
		}

		//ignore spacing tokens
		if( !Q_stricmp( token, "," ) ||
			!Q_stricmp( token, "{" ) ||
			!Q_stricmp( token, "}" ) ) {
			continue;
		}

		//some token sanity checks
		if( token[strlen( token ) - 1] == ',' ) {
			token[strlen( token ) - 1] = 0;
		}
		if( token[strlen( token ) - 1] == '}' ) {
			token[strlen( token ) - 1] = 0;
		}
		//(I don't fix these ones, but show the error)
		if( token[0] == ',' ) {
			G_Printf( "ERROR in script. Comma must be followed by space or newline\n" );
			return false;
		}
		if( token[0] == '{' || token[0] == '}' ) {
			G_Printf( "ERROR in script. Scorches must be followed by space or newline\n" );
			return false;
		}

		if( count > WEAPONDEF_NUMPARMS ) {
			return false;
		}

		if( !Q_stricmp( token, "instant" ) ) {
			parm[count] = 0;
		} else {
			parm[count] = atof( token );
		}

		if( parm[count] < 0 ) {
			return false;
		}

		count++;
	}

	// incomplete or wrong file
	if( count != WEAPONDEF_NUMPARMS ) {
		G_Printf( "ERROR in weapondef. Incorrect count of parameters\n" );
		return false;
	}

	// validate

	count = 0;
	// put the data into the firedef
	firedef->usage_count = (int)parm[count++];
	firedef->projectile_count = (int)parm[count++];

	firedef->weaponup_time = (unsigned int)parm[count++];
	firedef->weapondown_time = (unsigned int)parm[count++];
	firedef->reload_time = (unsigned int)parm[count++];
	firedef->cooldown_time = (unsigned int)parm[count++];
	firedef->timeout = (unsigned int)parm[count++];
	firedef->smooth_refire = (int)parm[count++];

	firedef->damage = (float)parm[count++];
	firedef->selfdamage = parm[count++];
	firedef->knockback = (int)parm[count++];
	firedef->stun = (int)parm[count++];
	firedef->splash_radius = (int)parm[count++];
	firedef->mindamage = (int)parm[count++];
	firedef->minknockback = (int)parm[count++];

	firedef->speed = (int)parm[count++];
	firedef->spread = (int)parm[count++];
	firedef->v_spread = (int)parm[count++];

	firedef->ammo_pickup = (int)parm[count++];
	firedef->ammo_max = (int)parm[count++];
	firedef->ammo_low = (int)parm[count++];

	if( firedef->weaponup_time < 50 ) {
		firedef->weaponup_time = 50;
	}
	if( firedef->weapondown_time < 50 ) {
		firedef->weapondown_time = 50;
	}

	return true;
}

static bool G_LoadFiredefFromFile( int weapon, firedef_t *firedef ) {
	int length, filenum;
	uint8_t *data;
	char filename[MAX_QPATH];

	if( !firedef ) {
		return false;
	}

	Q_snprintfz( filename, sizeof( filename ), "weapondefs/%s %s.def", GS_FindItemByTag( weapon )->shortname,
				 ( firedef->fire_mode == FIRE_MODE_STRONG ) ? "strong" : "weak" );

	Q_strlwr( filename );

	length = trap_FS_FOpenFile( filename, &filenum, FS_READ );

	if( length == -1 ) {
		G_Printf( "Couldn't find script: %s.\n", filename );
		return false;
	}

	if( !length ) {
		G_Printf( "Found empty script: %s.\n", filename );
		trap_FS_FCloseFile( filenum );
		return false;
	}

	//load the script data into memory
	data = G_Malloc( length + 1 );
	trap_FS_Read( data, length, filenum );
	trap_FS_FCloseFile( filenum );

	if( !data[0] ) {
		G_Printf( "Found empty script: %s.\n", filename );
		G_Free( data );
		return false;
	}

	//parse the file updating the firedef
	if( !G_ParseFiredefFile( data, weapon, firedef ) ) {
		G_Printf( "'InitWeapons': Error in definition file %s\n", filename );
		G_Free( data );
		return false;
	}

	G_Free( data );
	return true;
}
#endif // WEAPONDEFS_FROM_DISK

void G_LoadFiredefsFromDisk( void ) {
#ifdef WEAPONDEFS_FROM_DISK
	int i;
	gs_weapon_definition_t *weapondef;

	for( i = WEAP_GUNBLADE; i < WEAP_TOTAL; i++ ) {
		weapondef = GS_GetWeaponDef( i );
		if( !weapondef ) {
			continue;
		}

		G_LoadFiredefFromFile( i, &weapondef->firedef_weak );
		G_LoadFiredefFromFile( i, &weapondef->firedef );
	}
#endif
}

//======================================================================
//	LOCATIONS
//======================================================================

void G_MapLocations_Init( void ) {
	G_RegisterMapLocationName( "someplace" ); // location zero is unknown
}

int G_RegisterMapLocationName( const char *name ) {
	int tag;
	char temp[MAX_CONFIGSTRING_CHARS];

	tag = 0;
	if( !name ) {
		return 0;
	}

	Q_strncpyz( temp, name, sizeof( temp ) );

	tag = G_MapLocationTAGForName( temp );
	if( tag > 0 ) {
		return tag;
	}
	if( level.numLocations == MAX_LOCATIONS ) {
		return 0;
	}

	tag = level.numLocations++;
	trap_ConfigString( CS_LOCATIONS + tag, temp );

	return tag;
}

int G_MapLocationTAGForName( const char *name ) {
	int i;
	char temp[MAX_CONFIGSTRING_CHARS];

	if( !level.numLocations ) {
		return -1;
	}

	Q_strncpyz( temp, name, sizeof( temp ) );

	for( i = 0; i < level.numLocations; i++ ) {
		if( !Q_stricmp( temp, trap_GetConfigString( CS_LOCATIONS + i ) ) ) {
			return i;
		}
	}

	return 0;
}

int G_MapLocationTAGForOrigin( const vec3_t origin ) {
	edict_t *what = NULL;
	edict_t *hot = NULL;
	float hotdist = 3.0f * 8192.0f * 8192.0f;
	vec3_t v;

	while( ( what = G_Find( what, FOFS( classname ), "target_location" ) ) != NULL ) {
		VectorSubtract( what->s.origin, origin, v );

		if( VectorLengthFast( v ) > hotdist ) {
			continue;
		}

		if( !trap_inPVS( what->s.origin, origin ) ) {
			continue;
		}

		hot = what;
		hotdist = VectorLengthFast( v );
	}

	if( !hot || !hot->message ) {
		return 0;
	}
	return hot->style;
}

void G_MapLocationNameForTAG( int tag, char *buf, size_t buflen ) {
	if( tag < 0 || tag >= level.numLocations ) {
		tag = 0;
	}
	Q_strncpyz( buf, trap_GetConfigString( CS_LOCATIONS + tag ), buflen );
}
