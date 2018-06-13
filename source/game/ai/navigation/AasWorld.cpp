#include "AasWorld.h"
#include "../buffer_builder.h"
#include "../static_vector.h"
#include "../ai_local.h"
#include "../../../qalgo/md5.h"
#include "../../../qalgo/base64.h"

#undef min
#undef max
#include <memory>
#include <tuple>

// Static member definition
AiAasWorld *AiAasWorld::instance = nullptr;

AiAasWorld::AiAasWorld( AiAasWorld &&that ) {
	memcpy( this, &that, sizeof( AiAasWorld ) );
	that.loaded = false;
}

bool AiAasWorld::Init( const char *mapname ) {
	if( instance ) {
		AI_FailWith( "AiAasWorld::Init()", "An instance is already present\n" );
	}
	instance = (AiAasWorld *)G_Malloc( sizeof( AiAasWorld ) );
	new(instance) AiAasWorld;
	// Try to initialize the instance
	if( !instance->Load( mapname ) ) {
		return false;
	}
	instance->PostLoad();
	return true;
}

void AiAasWorld::Shutdown() {
	// This may be called on first map load when an instance has never been instantiated
	if( instance ) {
		instance->~AiAasWorld();
		G_Free( instance );
		// Allow the pointer to be reused, otherwise an assertion will fail on a next Init() call
		instance = nullptr;
	}
}

void AiAasWorld::Frame() {
}

int AiAasWorld::PointAreaNum( const vec3_t point ) const {
	if( !loaded ) {
		return 0;
	}

	//start with node 1 because node zero is a dummy used for solid leafs
	int nodenum = 1;

	while( nodenum > 0 ) {
		aas_node_t *node = &nodes[nodenum];
		aas_plane_t *plane = &planes[node->planenum];
		vec_t dist = DotProduct( point, plane->normal ) - plane->dist;
		if( dist > 0 ) {
			nodenum = node->children[0];
		} else {
			nodenum = node->children[1];
		}
	}
	return -nodenum;
}

int AiAasWorld::FindAreaNum( const vec3_t mins, const vec3_t maxs ) const {
	const vec_t *bounds[2] = { maxs, mins };
	// Test all AABB vertices
	vec3_t origin = { 0, 0, 0 };

	for( int i = 0; i < 8; ++i ) {
		origin[0] = bounds[( i >> 0 ) & 1][0];
		origin[1] = bounds[( i >> 1 ) & 1][1];
		origin[2] = bounds[( i >> 2 ) & 1][2];
		int areaNum = PointAreaNum( origin );
		if( areaNum ) {
			return areaNum;
		}
	}
	return 0;
}

int AiAasWorld::FindAreaNum( const vec3_t origin ) const {
	int areaNum = PointAreaNum( const_cast<float*>( origin ) );

	if( areaNum ) {
		return areaNum;
	}

	vec3_t mins = { -8, -8, 0 };
	VectorAdd( mins, origin, mins );
	vec3_t maxs = { +8, +8, 16 };
	VectorAdd( maxs, origin, maxs );
	return FindAreaNum( mins, maxs );
}

int AiAasWorld::FindAreaNum( const edict_t *ent ) const {
	// Reject degenerate case
	if( ent->r.absmin[0] == ent->r.absmax[0] &&
		ent->r.absmin[1] == ent->r.absmax[1] &&
		ent->r.absmin[2] == ent->r.absmax[2] ) {
		return FindAreaNum( ent->s.origin );
	}

	Vec3 testedOrigin( ent->s.origin );
	int areaNum = PointAreaNum( testedOrigin.Data() );
	if( areaNum ) {
		return areaNum;
	}

	return FindAreaNum( ent->r.absmin, ent->r.absmax );
}

typedef struct aas_tracestack_s {
	vec3_t start;       //start point of the piece of line to trace
	vec3_t end;         //end point of the piece of line to trace
	int planenum;       //last plane used as splitter
	int nodenum;        //node found after splitting with planenum
} aas_tracestack_t;

int AiAasWorld::TraceAreas( const vec3_t start, const vec3_t end, int *areas_, vec3_t *points, int maxareas ) const {
	if( !loaded ) {
		return 0;
	}

	vec3_t cur_start, cur_end, cur_mid;
	aas_tracestack_t tracestack[127];
	aas_tracestack_t *tstack_p;

	int numAreas = 0;
	areas_[0] = 0;

	tstack_p = tracestack;
	//we start with the whole line on the stack
	VectorCopy( start, tstack_p->start );
	VectorCopy( end, tstack_p->end );
	tstack_p->planenum = 0;
	//start with node 1 because node zero is a dummy for a solid leaf
	tstack_p->nodenum = 1;      //starting at the root of the tree
	tstack_p++;

	while( 1 ) {
		//pop up the stack
		tstack_p--;
		//if the trace stack is empty (ended up with a piece of the
		//line to be traced in an area)
		if( tstack_p < tracestack ) {
			return numAreas;
		}

		//number of the current node to test the line against
		int nodenum = tstack_p->nodenum;
		//if it is an area
		if( nodenum < 0 ) {
			areas_[numAreas] = -nodenum;
			if( points ) {
				VectorCopy( tstack_p->start, points[numAreas] );
			}
			numAreas++;
			if( numAreas >= maxareas ) {
				return numAreas;
			}
			continue;
		}
		//if it is a solid leaf
		if( !nodenum ) {
			continue;
		}

		//the node to test against
		aas_node_t *aasnode = &nodes[nodenum];
		//start point of current line to test against node
		VectorCopy( tstack_p->start, cur_start );
		//end point of the current line to test against node
		VectorCopy( tstack_p->end, cur_end );
		//the current node plane
		aas_plane_t *plane = &planes[aasnode->planenum];

		float front = DotProduct( cur_start, plane->normal ) - plane->dist;
		float back = DotProduct( cur_end, plane->normal ) - plane->dist;

		//if the whole to be traced line is totally at the front of this node
		//only go down the tree with the front child
		if( front > 0 && back > 0 ) {
			//keep the current start and end point on the stack
			//and go down the tree with the front child
			tstack_p->nodenum = aasnode->children[0];
			tstack_p++;
			if( tstack_p >= &tracestack[127] ) {
				G_Printf( S_COLOR_RED "AiAasWorld::TraceAreas(): stack overflow\n" );
				return numAreas;
			}
		}
		//if the whole to be traced line is totally at the back of this node
		//only go down the tree with the back child
		else if( front <= 0 && back <= 0 ) {
			//keep the current start and end point on the stack
			//and go down the tree with the back child
			tstack_p->nodenum = aasnode->children[1];
			tstack_p++;
			if( tstack_p >= &tracestack[127] ) {
				G_Printf( S_COLOR_RED "AiAasWorld::TraceAreas(): stack overflow\n" );
				return numAreas;
			}
		}
		//go down the tree both at the front and back of the node
		else {
			int tmpplanenum = tstack_p->planenum;
			//calculate the hitpoint with the node (split point of the line)
			//put the crosspoint TRACEPLANE_EPSILON pixels on the near side
			float frac = front / ( front - back );
			clamp( frac, 0.0f, 1.0f );
			//frac = front / (front-back);
			//
			cur_mid[0] = cur_start[0] + ( cur_end[0] - cur_start[0] ) * frac;
			cur_mid[1] = cur_start[1] + ( cur_end[1] - cur_start[1] ) * frac;
			cur_mid[2] = cur_start[2] + ( cur_end[2] - cur_start[2] ) * frac;

//			AAS_DrawPlaneCross(cur_mid, plane->normal, plane->dist, plane->type, LINECOLOR_RED);
			//side the front part of the line is on
			int side = front < 0;
			//first put the end part of the line on the stack (back side)
			VectorCopy( cur_mid, tstack_p->start );
			//not necesary to store because still on stack
			//VectorCopy(cur_end, tstack_p->end);
			tstack_p->planenum = aasnode->planenum;
			tstack_p->nodenum = aasnode->children[!side];
			tstack_p++;
			if( tstack_p >= &tracestack[127] ) {
				G_Printf( S_COLOR_RED "AiAasWorld::TraceAreas(): stack overflow\n" );
				return numAreas;
			}
			//now put the part near the start of the line on the stack so we will
			//continue with thats part first. This way we'll find the first
			//hit of the bbox
			VectorCopy( cur_start, tstack_p->start );
			VectorCopy( cur_mid, tstack_p->end );
			tstack_p->planenum = tmpplanenum;
			tstack_p->nodenum = aasnode->children[side];
			tstack_p++;
			if( tstack_p >= &tracestack[127] ) {
				G_Printf( S_COLOR_RED "AiAasWorld::TraceAreas(): stack overflow\n" );
				return numAreas;
			}
		}
	}
}

int AiAasWorld::BBoxAreasNonConst( const vec3_t absmins, const vec3_t absmaxs, int *areas_, int maxareas ) {
	if( !loaded ) {
		return 0;
	}

	aas_link_t *linkedareas = LinkEntity( absmins, absmaxs, -1 );
	int num = 0;

	for( aas_link_t *link = linkedareas; link; link = link->next_area ) {
		areas_[num] = link->areanum;
		num++;
		if( num >= maxareas ) {
			break;
		}
	}
	UnlinkFromAreas( linkedareas );
	return num;
}

typedef struct {
	int nodenum;        //node found after splitting
} aas_linkstack_t;

AiAasWorld::aas_link_t *AiAasWorld::LinkEntity( const vec3_t absmins, const vec3_t absmaxs, int entnum ) {
	aas_linkstack_t linkstack[256];
	aas_linkstack_t *lstack_p;

	aas_link_t *linkedAreas = nullptr;

	//
	lstack_p = linkstack;
	//we start with the whole line on the stack
	//start with node 1 because node zero is a dummy used for solid leafs
	lstack_p->nodenum = 1;      //starting at the root of the tree
	lstack_p++;

	while( 1 ) {
		//pop up the stack
		lstack_p--;
		//if the trace stack is empty (ended up with a piece of the
		//line to be traced in an area)
		if( lstack_p < linkstack ) {
			break;
		}

		//number of the current node to test the line against
		int nodenum = lstack_p->nodenum;
		//if it is an area
		if( nodenum < 0 ) {
			//NOTE: the entity might have already been linked into this area
			// because several node children can point to the same area
			aas_link_t *link = arealinkedentities[-nodenum];

			for(; link; link = link->next_ent ) {
				if( link->entnum == entnum ) {
					break;
				}
			}
			if( link ) {
				continue;
			}

			link = AllocLink();
			if( !link ) {
				return linkedAreas;
			}

			link->entnum = entnum;
			link->areanum = -nodenum;
			//put the link into the double linked area list of the entity
			link->prev_area = nullptr;
			link->next_area = linkedAreas;
			if( linkedAreas ) {
				linkedAreas->prev_area = link;
			}
			linkedAreas = link;
			//put the link into the double linked entity list of the area
			link->prev_ent = nullptr;
			link->next_ent = arealinkedentities[-nodenum];
			if( arealinkedentities[-nodenum] ) {
				arealinkedentities[-nodenum]->prev_ent = link;
			}
			arealinkedentities[-nodenum] = link;
			continue;
		}

		//if solid leaf
		if( !nodenum ) {
			continue;
		}

		//the node to test against
		aas_node_t *aasnode = &nodes[nodenum];
		//the current node plane
		aas_plane_t *plane = &planes[aasnode->planenum];
		//get the side(s) the box is situated relative to the plane
		int side = BoxOnPlaneSide2( absmins, absmaxs, plane );
		//if on the front side of the node
		if( side & 1 ) {
			lstack_p->nodenum = aasnode->children[0];
			lstack_p++;
		}
		if( lstack_p >= &linkstack[255] ) {
			G_Printf( S_COLOR_RED "AiAasWorld::LinkEntity(): stack overflow\n" );
			break;
		}
		//if on the back side of the node
		if( side & 2 ) {
			lstack_p->nodenum = aasnode->children[1];
			lstack_p++;
		}
		if( lstack_p >= &linkstack[255] ) {
			G_Printf( S_COLOR_RED "AiAasWorld::LinkEntity(): stack overflow\n" );
			break;
		}
	}
	return linkedAreas;
}

void AiAasWorld::UnlinkFromAreas( aas_link_t *linkedAreas ) {
	aas_link_t *link, *nextlink;

	for( link = linkedAreas; link; link = nextlink ) {
		//next area the entity is linked in
		nextlink = link->next_area;
		//remove the entity from the linked list of this area
		if( link->prev_ent ) {
			link->prev_ent->next_ent = link->next_ent;
		} else {
			arealinkedentities[link->areanum] = link->next_ent;
		}
		if( link->next_ent ) {
			link->next_ent->prev_ent = link->prev_ent;
		}
		//deallocate the link structure
		DeAllocLink( link );
	}
}

AiAasWorld::aas_link_t *AiAasWorld::AllocLink() {
	aas_link_t *link = freelinks;

	if( !link ) {
		G_Printf( S_COLOR_RED "empty aas link heap\n" );
		return nullptr;
	}
	if( freelinks ) {
		freelinks = freelinks->next_ent;
	}
	if( freelinks ) {
		freelinks->prev_ent = nullptr;
	}
	numaaslinks--;
	return link;
}

void AiAasWorld::DeAllocLink( aas_link_t *link ) {
	if( freelinks ) {
		freelinks->prev_ent = link;
	}
	link->prev_ent = nullptr;
	link->next_ent = freelinks;
	link->prev_area = nullptr;
	link->next_area = nullptr;
	freelinks = link;
	numaaslinks++;
}

void AiAasWorld::InitLinkHeap() {
	int max_aaslinks = linkheapsize;

	//if there's no link heap present
	if( !linkheap ) {
		max_aaslinks = 6144;
		linkheapsize = max_aaslinks;
		linkheap = (aas_link_t *) G_LevelMalloc( max_aaslinks * sizeof( aas_link_t ) );
	}
	//link the links on the heap
	linkheap[0].prev_ent = nullptr;
	linkheap[0].next_ent = &linkheap[1];

	for( int i = 1; i < max_aaslinks - 1; i++ ) {
		linkheap[i].prev_ent = &linkheap[i - 1];
		linkheap[i].next_ent = &linkheap[i + 1];
	}
	linkheap[max_aaslinks - 1].prev_ent = &linkheap[max_aaslinks - 2];
	linkheap[max_aaslinks - 1].next_ent = nullptr;
	//pointer to the first free link
	freelinks = &linkheap[0];
	//
	numaaslinks = max_aaslinks;
}

void AiAasWorld::InitLinkedEntities() {
	arealinkedentities = (aas_link_t **) G_LevelMalloc( numareas * sizeof( aas_link_t * ) );
}

void AiAasWorld::FreeLinkHeap() {
	if( linkheap ) {
		G_LevelFree( linkheap );
	}
	linkheap = nullptr;
	linkheapsize = 0;
}

void AiAasWorld::FreeLinkedEntities() {
	if( arealinkedentities ) {
		G_LevelFree( arealinkedentities );
	}
	arealinkedentities = nullptr;
}

void AiAasWorld::ComputeExtraAreaData() {
	for( int areaNum = 1; areaNum < numareas; ++areaNum ) {
		TrySetAreaLedgeFlags( areaNum );
		TrySetAreaWallFlags( areaNum );
		TrySetAreaJunkFlags( areaNum );
		TrySetAreaRampFlags( areaNum );
	}

	// Call after all other flags have been set
	TrySetAreaSkipCollisionFlags();

	ComputeLogicalAreaClusters();
	ComputeFace2DProjVertices();
	ComputeAreasLeafsLists();

	// These computations expect (are going to expect) that logical clusters are valid
	for( int areaNum = 1; areaNum < numareas; ++areaNum ) {
		TrySetAreaNoFallFlags( areaNum );
	}
}

void AiAasWorld::TrySetAreaLedgeFlags( int areaNum ) {
	int reachNum = areasettings[areaNum].firstreachablearea;
	int endReachNum = areasettings[areaNum].firstreachablearea + areasettings[areaNum].numreachableareas;

	for(; reachNum != endReachNum; ++reachNum ) {
		if( reachability[reachNum].traveltype == TRAVEL_WALKOFFLEDGE ) {
			areasettings[areaNum].areaflags |= AREA_LEDGE;
			break;
		}
	}
}

void AiAasWorld::TrySetAreaWallFlags( int areaNum ) {
	int faceIndexNum = areas[areaNum].firstface;
	int endFaceIndexNum = areas[areaNum].firstface + areas[areaNum].numfaces;
	const float *zAxis = &axis_identity[AXIS_UP];

	for(; faceIndexNum != endFaceIndexNum; ++faceIndexNum ) {
		int faceIndex = faceindex[faceIndexNum];
		int areaBehindFace;
		const aas_face_t *face;
		if( faceIndex >= 0 ) {
			face = &faces[faceIndex];
			areaBehindFace = face->backarea;
		} else   {
			face = &faces[-faceIndex];
			areaBehindFace = face->frontarea;
		}

		// There is no solid but some other area behind the face
		if( areaBehindFace ) {
			continue;
		}

		const aas_plane_t *facePlane = &planes[face->planenum];
		// Do not treat bounding ceilings and ground as a wall
		if( fabsf( DotProduct( zAxis, facePlane->normal ) ) < 0.3f ) {
			areasettings[areaNum].areaflags |= AREA_WALL;
			break;
		}
	}
}

void AiAasWorld::TrySetAreaJunkFlags( int areaNum ) {
	const aas_area_t &area = areas[areaNum];
	int junkFactor = 0;

	// Changed to test only 2D dimensions, otherwise there will be way too many bogus ramp flags set
	for( int i = 0; i < 2; ++i ) {
		if( area.maxs[i] - area.mins[i] < 24.0f ) {
			++junkFactor;
		}
	}
	if( junkFactor > 1 ) {
		areasettings[areaNum].areaflags |= AREA_JUNK;
	}
}

void AiAasWorld::TrySetAreaRampFlags( int areaNum ) {
	// Since we extend the trace end a bit below the area,
	// this test is added to avoid classifying non-grounded areas as having a ramp
	if( !( AreaSettings()[areaNum].areaflags & AREA_GROUNDED ) ) {
		return;
	}
	// Skip junk areas as well
	if( AreaSettings()[areaNum].areaflags & AREA_JUNK ) {
		return;
	}

	// AAS does not make a distinction for areas having an inclined floor.
	// This leads to a poor bot behaviour since bots threat areas of these kind as obstacles.
	// Moreover if an "area" (which should not be a single area) has both flat and inclined floor parts,
	// the inclined part is still ignored.

	// There is an obvious approach of testing ground faces of the area but it does not work for several reasons
	// (some faces are falsely marked as FACE_GROUND).

	const auto &area = areas[areaNum];
	// Since an area might contain both flat and inclined part, we cannot just test a trace going through the center
	float stepX = 0.2f * ( area.maxs[0] - area.mins[0] );
	float stepY = 0.2f * ( area.maxs[1] - area.mins[1] );

	static const float zNormalThreshold = cosf( DEG2RAD( 2.0f ) );

	trace_t trace;
	for( int i = -2; i <= 2; ++i ) {
		for( int j = -2; j <= 2; ++j ) {
			Vec3 start( area.center );
			Vec3 end( area.center );
			start.X() += stepX * i;
			start.Y() += stepY * j;
			end.X() += stepX * i;
			end.Y() += stepY * j;

			// These margins added are absolutely required in order to produce satisfiable results
			start.Z() = area.maxs[2] + 16.0f;
			end.Z() = area.mins[2] - 16.0f;

			G_Trace( &trace, start.Data(), nullptr, nullptr, end.Data(), nullptr, MASK_PLAYERSOLID );
			if( trace.fraction == 1.0f || trace.startsolid ) {
				continue;
			}

			if( !ISWALKABLEPLANE( &trace.plane ) ) {
				continue;
			}

			if( trace.plane.normal[2] > zNormalThreshold ) {
				continue;
			}

			// Check whether we're still in the same area
			if( trace.endpos[2] < area.mins[2] || trace.endpos[2] > area.maxs[2] ) {
				continue;
			}

			// TODO: This does not really work for some weird reasons so we have to live with false positives
			// Area bounds extend the actual area geometry,
			// so a point might be within the bounds but outside the area hull
			//Vec3 testedPoint( trace.endpos );
			//testedPoint.Z() += 1.0f;
			//if( PointAreaNum( testedPoint.Data() ) != areaNum ) {
			//	continue;
			//}

			areasettings[areaNum].areaflags |= AREA_INCLINED_FLOOR;
			if( trace.plane.normal[2] <= 1.0f - SLIDEMOVE_PLANEINTERACT_EPSILON ) {
				areasettings[areaNum].areaflags |= AREA_SLIDABLE_RAMP;
				// All flags that could be set are present
				return;
			}
		}
	}
}

void AiAasWorld::TrySetAreaNoFallFlags( int areaNum ) {
	// First, try to cut off extremely expensive computations at the end of this method
	const auto &areaSettings = areasettings[areaNum];
	if( areaSettings.areaflags & ( AREA_JUNK | AREA_LIQUID | AREA_DISABLED ) ) {
		return;
	}

	constexpr auto badContents = AREACONTENTS_LAVA | AREACONTENTS_SLIME | AREACONTENTS_DONOTENTER;
	// We also include triggers/movers as undesired to prevent trigger activation without intention
	constexpr auto triggerContents = AREACONTENTS_JUMPPAD | AREACONTENTS_TELEPORTER | AREACONTENTS_MOVER;
	constexpr auto undesiredContents = badContents | triggerContents;

	if( areaSettings.contents & undesiredContents ) {
		return;
	}

	// Inspect all outgoing reachabilities
	int reachNum = areaSettings.firstreachablearea;
	for(; reachNum < areaSettings.firstreachablearea + areaSettings.numreachableareas; ++reachNum ) {
		const auto &reach = reachability[reachNum];
		const auto &reachAreaSettings = areasettings[reach.areanum];
		if( reachAreaSettings.areaflags & ( AREA_LIQUID | AREA_DISABLED ) ) {
			return;
		}
		if( reachAreaSettings.contents & undesiredContents ) {
			return;
		}
		const int travelType = reach.traveltype & TRAVELTYPE_MASK;
		if( travelType == TRAVEL_JUMPPAD || travelType == TRAVEL_TELEPORT || travelType == TRAVEL_ELEVATOR ) {
			return;
		}
		if( travelType == TRAVEL_WALKOFFLEDGE ) {
			// Some reach. of this kind are really short and should not be considered as falling
			if( DistanceSquared( reach.start, reach.end ) > 32 * 32 ) {
				return;
			}
		}
		// Note: TRAVEL_SWIM reach.-es are cut off by the reachable area contents test
	}

	// TODO: This is way too optimistic

	areasettings[areaNum].areaflags |= AREA_NOFALL;
}

void AiAasWorld::TrySetAreaSkipCollisionFlags() {
	trace_t trace;

	const float extents[3] = { 32, 16 };
	int flagsToSet[3] = { AREA_SKIP_COLLISION_48, AREA_SKIP_COLLISION_32, AREA_SKIP_COLLISION_16 };
	// Leftmost flags also imply all rightmost flags presence
	for( int i = 0; i < 2; ++i ) {
		for( int j = i + 1; j < 3; ++j ) {
			flagsToSet[i] |= flagsToSet[j];
		}
	}

	for( int i = 1; i < numareas; ++i ) {
		int *const areaFlags = &areasettings[i].areaflags;
		// If it is already known that the area is bounded by a solid wall or is an inclined floor area
		if( *areaFlags & ( AREA_WALL | AREA_INCLINED_FLOOR ) ) {
			continue;
		}

		auto &area = areas[i];
		for( int j = 0; j < 3; ++j ) {
			const float extent = extents[j];
			// Now make a bounding box not lesser than the area bounds or player bounds

			// Set some side extent, except for the bottom side
			Vec3 mins( -extent, -extent, 0 );
			Vec3 maxs( +extent, +extent, playerbox_stand_maxs[2] );
			mins += area.mins;
			maxs += area.maxs;
			// Convert bounds to relative
			maxs -= area.center;
			mins -= area.center;

			// Ensure that the bounds are not less than playerbox + necessary extent
			for( int k = 0; k < 2; ++k ) {
				float maxSideMins = playerbox_stand_mins[k] - extent;
				if( mins.Data()[k] > maxSideMins ) {
					mins.Data()[k] = maxSideMins;
				}
				float minSideMaxs = playerbox_stand_maxs[k] + extent;
				if( maxs.Data()[k] < minSideMaxs ) {
					maxs.Data()[k] = minSideMaxs;
				}
			}

			float maxMinsZ = playerbox_stand_mins[2];
			if( mins.Z() > maxMinsZ ) {
				mins.Z() = maxMinsZ;
			}

			float minMaxsZ = playerbox_stand_maxs[2] * 2;
			if( maxs.Z() < minMaxsZ ) {
				maxs.Z() = minMaxsZ;
			}

			// Add an offset from ground if necessary (otherwise a trace is likely to start in solid)
			if( *areaFlags & AREA_GROUNDED ) {
				mins.Z() += 1.0f;
			}

			G_Trace( &trace, area.center, mins.Data(), maxs.Data(), area.center, nullptr, MASK_PLAYERSOLID );
			if( trace.fraction == 1.0f && !trace.startsolid ) {
				*areaFlags |= flagsToSet[j];
				goto nextArea;
			}
		}
nextArea:;
	}
}

int AiAasWorld::BoxOnPlaneSide2( const vec3_t absmins, const vec3_t absmaxs, const aas_plane_t *p ) {
	vec3_t corners[2];

	for( int i = 0; i < 3; i++ ) {
		if( p->normal[i] < 0 ) {
			corners[0][i] = absmins[i];
			corners[1][i] = absmaxs[i];
		} else   {
			corners[1][i] = absmins[i];
			corners[0][i] = absmaxs[i];
		} //end else
	}
	float dist1 = DotProduct( p->normal, corners[0] ) - p->dist;
	float dist2 = DotProduct( p->normal, corners[1] ) - p->dist;
	int sides = 0;
	if( dist1 >= 0 ) {
		sides = 1;
	}
	if( dist2 < 0 ) {
		sides |= 2;
	}

	return sides;
}

static void AAS_DData( unsigned char *data, int size ) {
	for( int i = 0; i < size; i++ ) {
		data[i] ^= (unsigned char) i * 119;
	}
}

#define AAS_LUMPS                   14
#define AASLUMP_BBOXES              0
#define AASLUMP_VERTEXES            1
#define AASLUMP_PLANES              2
#define AASLUMP_EDGES               3
#define AASLUMP_EDGEINDEX           4
#define AASLUMP_FACES               5
#define AASLUMP_FACEINDEX           6
#define AASLUMP_AREAS               7
#define AASLUMP_AREASETTINGS        8
#define AASLUMP_REACHABILITY        9
#define AASLUMP_NODES               10
#define AASLUMP_PORTALS             11
#define AASLUMP_PORTALINDEX         12
#define AASLUMP_CLUSTERS            13

class AasFileReader
{
	int fp;
	int lastoffset;

	//header lump
	typedef struct {
		int fileofs;
		int filelen;
	} aas_lump_t;

	//aas file header
	typedef struct aas_header_s {
		int ident;
		int version;
		int bspchecksum;
		//data entries
		aas_lump_t lumps[AAS_LUMPS];
	} aas_header_t;

	aas_header_t header;
	int fileSize;

	char *LoadLump( int lumpNum, int size );

public:
	AasFileReader( const char *mapname );

	~AasFileReader() {
		if( fp ) {
			trap_FS_FCloseFile( fp );
		}
	}

	inline bool IsValid() { return fp != 0; }

	template<typename T>
	inline std::tuple<T*, int> LoadLump( int lumpNum ) {
		int oldOffset = lastoffset;
		char *rawData = LoadLump( lumpNum, sizeof( T ) );
		int length = lastoffset - oldOffset;

		return std::make_tuple( (T*)rawData, length / sizeof( T ) );
	};

	bool ComputeChecksum( char **base64Digest );
};

#define AASID                       ( ( 'S' << 24 ) + ( 'A' << 16 ) + ( 'A' << 8 ) + 'E' )
#define AASVERSION_OLD              4
#define AASVERSION                  5

AasFileReader::AasFileReader( const char *mapname )
	: lastoffset( 0 ) {
	// Shut up an analyzer
	memset( &header, 0, sizeof( header ) );

	char filename[MAX_QPATH];
	Q_snprintfz( filename, MAX_QPATH, "maps/%s.aas", mapname );

	fileSize = trap_FS_FOpenFile( filename, &fp, FS_READ );
	if( !fp || fileSize <= 0 ) {
		G_Printf( S_COLOR_RED "can't open %s\n", filename );
		return;
	}

	//read the header
	trap_FS_Read( &header, sizeof( aas_header_t ), fp );
	lastoffset = sizeof( aas_header_t );
	//check header identification
	header.ident = LittleLong( header.ident );
	if( header.ident != AASID ) {
		G_Printf( S_COLOR_RED "%s is not an AAS file\n", filename );
		return;
	}

	//check the version
	header.version = LittleLong( header.version );
	if( header.version != AASVERSION_OLD && header.version != AASVERSION ) {
		G_Printf( S_COLOR_RED "aas file %s is version %i, not %i\n", filename, header.version, AASVERSION );
		return;
	}
	if( header.version == AASVERSION ) {
		AAS_DData( (unsigned char *) &header + 8, sizeof( aas_header_t ) - 8 );
	}
}

char *AasFileReader::LoadLump( int lumpNum, int size ) {
	int offset = LittleLong( header.lumps[lumpNum].fileofs );
	int length = LittleLong( header.lumps[lumpNum].filelen );

	if( !length ) {
		//just alloc a dummy
		return (char *) G_LevelMalloc( size + 1 );
	}
	//seek to the data
	if( offset != lastoffset ) {
		G_Printf( S_COLOR_YELLOW "AAS file not sequentially read\n" );
		if( trap_FS_Seek( fp, offset, FS_SEEK_SET ) ) {
			G_Printf( S_COLOR_RED "can't seek to aas lump\n" );
			return nullptr;
		}
	}
	//allocate memory
	char *buf = (char *) G_LevelMalloc( length + 1 );
	//read the data
	if( length ) {
		trap_FS_Read( buf, length, fp );
		lastoffset += length;
	}
	return buf;
}

bool AasFileReader::ComputeChecksum( char **base64Digest ) {
	if( trap_FS_Seek( fp, 0, FS_SEEK_SET ) < 0 ) {
		return false;
	}

	// TODO: Read the entire AAS data at start and then use the read chunk for loading of AAS lumps
	char *mem = (char *)G_LevelMalloc( (unsigned)fileSize );
	if( trap_FS_Read( mem, (unsigned)fileSize, fp ) <= 0 ) {
		G_LevelFree( mem );
		return false;
	}

	// Compute a binary MD5 digest of the file data first
	md5_byte_t binaryDigest[16];
	md5_digest( mem, fileSize, binaryDigest );

	// Get a base64-encoded digest in a temporary buffer allocated via malloc()
	size_t base64Length;
	char *tmpBase64Chars = ( char * )base64_encode( binaryDigest, 16, &base64Length );

	// Free the level data
	G_LevelFree( mem );

	// Copy the base64-encoded digest to the game memory storage to avoid further confusion
	*base64Digest = ( char * )G_LevelMalloc( base64Length + 1 );
	// Include the last zero byte in copied chars
	memcpy( *base64Digest, tmpBase64Chars, base64Length + 1 );

	free( tmpBase64Chars );

	return true;
}

bool AiAasWorld::Load( const char *mapname ) {
	AasFileReader reader( mapname );

	if( !reader.IsValid() ) {
		return false;
	}

	std::tie( bboxes, numbboxes ) = reader.LoadLump<aas_bbox_t>( AASLUMP_BBOXES );
	if( numbboxes && !bboxes ) {
		return false;
	}

	std::tie( vertexes, numvertexes ) = reader.LoadLump<aas_vertex_t>( AASLUMP_VERTEXES );
	if( numvertexes && !vertexes ) {
		return false;
	}

	std::tie( planes, numplanes ) = reader.LoadLump<aas_plane_t>( AASLUMP_PLANES );
	if( numplanes && !planes ) {
		return false;
	}

	std::tie( edges, numedges ) = reader.LoadLump<aas_edge_t>( AASLUMP_EDGES );
	if( numedges && !edges ) {
		return false;
	}

	std::tie( edgeindex, edgeindexsize ) = reader.LoadLump<int>( AASLUMP_EDGEINDEX );
	if( edgeindexsize && !edgeindex ) {
		return false;
	}

	std::tie( faces, numfaces ) = reader.LoadLump<aas_face_t>( AASLUMP_FACES );
	if( numfaces && !faces ) {
		return false;
	}

	std::tie( faceindex, faceindexsize ) = reader.LoadLump<int>( AASLUMP_FACEINDEX );
	if( faceindexsize && !faceindex ) {
		return false;
	}

	std::tie( areas, numareas ) = reader.LoadLump<aas_area_t>( AASLUMP_AREAS );
	if( numareas && !areas ) {
		return false;
	}

	std::tie( areasettings, numareasettings ) = reader.LoadLump<aas_areasettings_t>( AASLUMP_AREASETTINGS );
	if( numareasettings && !areasettings ) {
		return false;
	}

	std::tie( reachability, reachabilitysize ) = reader.LoadLump<aas_reachability_t>( AASLUMP_REACHABILITY );
	if( reachabilitysize && !reachability ) {
		return false;
	}

	std::tie( nodes, numnodes ) = reader.LoadLump<aas_node_t>( AASLUMP_NODES );
	if( numnodes && !nodes ) {
		return false;
	}

	std::tie( portals, numportals ) = reader.LoadLump<aas_portal_t>( AASLUMP_PORTALS );
	if( numportals && !portals ) {
		return false;
	}

	std::tie( portalindex, portalindexsize ) = reader.LoadLump<int>( AASLUMP_PORTALINDEX );
	if( portalindexsize && !portalindex ) {
		return false;
	}

	std::tie( clusters, numclusters ) = reader.LoadLump<aas_cluster_t>( AASLUMP_CLUSTERS );
	if( numclusters && !clusters ) {
		return false;
	}

	checksum = nullptr;
	if( !reader.ComputeChecksum( &checksum ) ) {
		return false;
	}

	SwapData();

	loaded = true;
	return true;
}

AiAasWorld::~AiAasWorld() {
	if( !loaded ) {
		return;
	}

	if( checksum ) {
		G_LevelFree( checksum );
	}

	FreeLinkedEntities();
	FreeLinkHeap();

	// These items may be absent for some stripped AAS files, so check each one.
	if( bboxes ) {
		G_LevelFree( bboxes );
	}
	if( vertexes ) {
		G_LevelFree( vertexes );
	}
	if( planes ) {
		G_LevelFree( planes );
	}
	if( edges ) {
		G_LevelFree( edges );
	}
	if( edgeindex ) {
		G_LevelFree( edgeindex );
	}
	if( faces ) {
		G_LevelFree( faces );
	}
	if( faceindex ) {
		G_LevelFree( faceindex );
	}
	if( areas ) {
		G_LevelFree( areas );
	}
	if( areasettings ) {
		G_LevelFree( areasettings );
	}
	if( reachability ) {
		G_LevelFree( reachability );
	}
	if( nodes ) {
		G_LevelFree( nodes );
	}
	if( portals ) {
		G_LevelFree( portals );
	}
	if( portalindex ) {
		G_LevelFree( portalindex );
	}
	if( clusters ) {
		G_LevelFree( clusters );
	}

	if( areaFloorClusterNums ) {
		G_LevelFree( areaFloorClusterNums );
	}
	if( areaStairsClusterNums ) {
		G_LevelFree( areaStairsClusterNums );
	}
	if( floorClusterDataOffsets ) {
		G_LevelFree( floorClusterDataOffsets );
	}
	if( stairsClusterDataOffsets ) {
		G_LevelFree( stairsClusterDataOffsets );
	}
	if( floorClusterData ) {
		G_LevelFree( floorClusterData );
	}
	if( stairsClusterData ) {
		G_LevelFree( stairsClusterData );
	}

	if( face2DProjVertexNums ) {
		G_LevelFree( face2DProjVertexNums );
	}

	if( areaMapLeafListOffsets ) {
		G_LevelFree( areaMapLeafListOffsets );
	}
	if( areaMapLeafsData ) {
		G_LevelFree( areaMapLeafsData );
	}
}

void AiAasWorld::SwapData() {
	//bounding boxes
	for( int i = 0; i < numbboxes; i++ ) {
		bboxes[i].presencetype = LittleLong( bboxes[i].presencetype );
		bboxes[i].flags = LittleLong( bboxes[i].flags );

		for( int j = 0; j < 3; j++ ) {
			bboxes[i].mins[j] = LittleLong( bboxes[i].mins[j] );
			bboxes[i].maxs[j] = LittleLong( bboxes[i].maxs[j] );
		}
		// Note: we do not care about dimensions shift described below
		// because these AAS bboxes are unused and should be removed.
	}

	// We have to shift all vertices/bounding boxes by this value,
	// as the entire bot code expects area mins to match ground,
	// and values loaded as-is are -shifts[2] units above the ground.
	// This behavior is observed not only on maps compiled by the Qfusion-compatible BSPC, but on vanilla Q3 maps as well.
	// XY-shifts are also observed, but are not so painful as the Z one is.
	// Also XY shifts seem to vary (?) from map to map and even in the same map.
	const vec3_t shifts = { 0, 0, -24.0f + 0.25f };

	//vertexes
	for( int i = 0; i < numvertexes; i++ ) {
		for( int j = 0; j < 3; j++ )
			vertexes[i][j] = LittleFloat( vertexes[i][j] ) + shifts[j];
	}

	//planes
	for( int i = 0; i < numplanes; i++ ) {
		for( int j = 0; j < 3; j++ )
			planes[i].normal[j] = LittleFloat( planes[i].normal[j] );
		planes[i].dist = LittleFloat( planes[i].dist );
		planes[i].type = LittleLong( planes[i].type );
	}

	//edges
	for( int i = 0; i < numedges; i++ ) {
		edges[i].v[0] = LittleLong( edges[i].v[0] );
		edges[i].v[1] = LittleLong( edges[i].v[1] );
	}

	//edgeindex
	for( int i = 0; i < edgeindexsize; i++ ) {
		edgeindex[i] = LittleLong( edgeindex[i] );
	}

	//faces
	for( int i = 0; i < numfaces; i++ ) {
		faces[i].planenum = LittleLong( faces[i].planenum );
		faces[i].faceflags = LittleLong( faces[i].faceflags );
		faces[i].numedges = LittleLong( faces[i].numedges );
		faces[i].firstedge = LittleLong( faces[i].firstedge );
		faces[i].frontarea = LittleLong( faces[i].frontarea );
		faces[i].backarea = LittleLong( faces[i].backarea );
	}

	//face index
	for( int i = 0; i < faceindexsize; i++ ) {
		faceindex[i] = LittleLong( faceindex[i] );
	}

	//convex areas
	for( int i = 0; i < numareas; i++ ) {
		areas[i].areanum = LittleLong( areas[i].areanum );
		areas[i].numfaces = LittleLong( areas[i].numfaces );
		areas[i].firstface = LittleLong( areas[i].firstface );

		for( int j = 0; j < 3; j++ ) {
			areas[i].mins[j] = LittleFloat( areas[i].mins[j] ) + shifts[j];
			areas[i].maxs[j] = LittleFloat( areas[i].maxs[j] ) + shifts[j];
			areas[i].center[j] = LittleFloat( areas[i].center[j] ) + shifts[j];
		}
	}

	//area settings
	for( int i = 0; i < numareasettings; i++ ) {
		areasettings[i].contents = LittleLong( areasettings[i].contents );
		areasettings[i].areaflags = LittleLong( areasettings[i].areaflags );
		areasettings[i].presencetype = LittleLong( areasettings[i].presencetype );
		areasettings[i].cluster = LittleLong( areasettings[i].cluster );
		areasettings[i].clusterareanum = LittleLong( areasettings[i].clusterareanum );
		areasettings[i].numreachableareas = LittleLong( areasettings[i].numreachableareas );
		areasettings[i].firstreachablearea = LittleLong( areasettings[i].firstreachablearea );
	}

	//area reachability
	for( int i = 0; i < reachabilitysize; i++ ) {
		reachability[i].areanum = LittleLong( reachability[i].areanum );
		reachability[i].facenum = LittleLong( reachability[i].facenum );
		reachability[i].edgenum = LittleLong( reachability[i].edgenum );

		for( int j = 0; j < 3; j++ ) {
			reachability[i].start[j] = LittleFloat( reachability[i].start[j] ) + shifts[j];
			reachability[i].end[j] = LittleFloat( reachability[i].end[j] ) + shifts[j];
		}

		reachability[i].traveltype = LittleLong( reachability[i].traveltype );
		reachability[i].traveltime = LittleShort( reachability[i].traveltime );
	}

	//nodes
	for( int i = 0; i < numnodes; i++ ) {
		nodes[i].planenum = LittleLong( nodes[i].planenum );
		nodes[i].children[0] = LittleLong( nodes[i].children[0] );
		nodes[i].children[1] = LittleLong( nodes[i].children[1] );
	}

	//cluster portals
	for( int i = 0; i < numportals; i++ ) {
		portals[i].areanum = LittleLong( portals[i].areanum );
		portals[i].frontcluster = LittleLong( portals[i].frontcluster );
		portals[i].backcluster = LittleLong( portals[i].backcluster );
		portals[i].clusterareanum[0] = LittleLong( portals[i].clusterareanum[0] );
		portals[i].clusterareanum[1] = LittleLong( portals[i].clusterareanum[1] );
	}

	//cluster portal index
	for( int i = 0; i < portalindexsize; i++ ) {
		portalindex[i] = LittleLong( portalindex[i] );
	}

	//cluster
	for( int i = 0; i < numclusters; i++ ) {
		clusters[i].numareas = LittleLong( clusters[i].numareas );
		clusters[i].numreachabilityareas = LittleLong( clusters[i].numreachabilityareas );
		clusters[i].numportals = LittleLong( clusters[i].numportals );
		clusters[i].firstportal = LittleLong( clusters[i].firstportal );
	}
}

// ClassfiyFunc operator() invocation must yield these results:
// -1: the area should be marked as flooded and skipped
//  0: the area should be skipped without marking as flooded
//  1: the area should be marked as flooded and put in the results list
template<typename ClassifyFunc>
class AreasClusterBuilder {
protected:
	ClassifyFunc classifyFunc;

	bool *isFlooded;
	uint16_t *resultsBase;
	uint16_t *resultsPtr;

	const AiAasWorld *aasWorld;

	vec3_t floodedRegionMins;
	vec3_t floodedRegionMaxs;

public:
	AreasClusterBuilder( bool *isFloodedBuffer, uint16_t *resultsBuffer, AiAasWorld *aasWorld_ )
		: isFlooded( isFloodedBuffer ), resultsBase( resultsBuffer ), aasWorld( aasWorld_ ) {}

	void FloodAreasRecursive( int areaNum );

	void PrepareToFlood() {
		memset( isFlooded, 0, sizeof( bool ) * aasWorld->NumAreas() );
		resultsPtr = &resultsBase[0];
		ClearBounds( floodedRegionMins, floodedRegionMaxs );
	}

	const uint16_t *ResultAreas() const { return resultsBase; }
	int ResultSize() const { return (int)( resultsPtr - resultsBase ); }
};

template <typename ClassifyFunc>
void AreasClusterBuilder<ClassifyFunc>::FloodAreasRecursive( int areaNum ) {
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const auto *aasReach = aasWorld->Reachabilities();

	// TODO: Rewrite to stack-based non-recursive version

	*resultsPtr++ = (uint16_t)areaNum;
	isFlooded[areaNum] = true;

	const auto &currArea = aasAreas[areaNum];
	AddPointToBounds( currArea.mins, floodedRegionMins, floodedRegionMaxs );
	AddPointToBounds( currArea.maxs, floodedRegionMins, floodedRegionMaxs );

	const auto &currAreaSettings = aasAreaSettings[areaNum];
	int reachNum = currAreaSettings.firstreachablearea;
	const int maxReachNum = reachNum + currAreaSettings.numreachableareas;
	for( ; reachNum < maxReachNum; ++reachNum ) {
		const auto &reach = aasReach[reachNum];
		if( isFlooded[reach.areanum] ) {
			continue;
		}

		int classifyResult = classifyFunc( currArea, reach, aasAreas[reach.areanum], aasAreaSettings[reach.areanum] );
		if( classifyResult < 0 ) {
			isFlooded[reach.areanum] = true;
			continue;
		}

		if( classifyResult > 0 ) {
			FloodAreasRecursive( reach.areanum );
		}
	}
}

struct ClassifyFloorArea
{
	int operator()( const aas_area_t &currArea,
					const aas_reachability_t &reach,
					const aas_area_t &reachArea,
					const aas_areasettings_t &reachAreaSetttings ) {
		if( reach.traveltype != TRAVEL_WALK ) {
			// Do not disable the area for further search,
			// it might be reached by walking through some intermediate area
			return 0;
		}

		if( fabsf( reachArea.mins[2] - currArea.mins[2] ) > 1.0f ) {
			// Disable the area for further search
			return -1;
		}

		if( !LooksLikeAFloorArea( reachAreaSetttings ) ) {
			// Disable the area for further search
			return -1;
		}

		return 1;
	}

	bool LooksLikeAFloorArea( const aas_areasettings_t &areaSettings ) {
		if( !( areaSettings.areaflags & AREA_GROUNDED ) ) {
			return false;
		}
		if( areaSettings.areaflags & AREA_INCLINED_FLOOR ) {
			return false;
		}
		return true;
	}
};

class FloorClusterBuilder : public AreasClusterBuilder<ClassifyFloorArea> {
	bool IsFloodedRegionDegenerate() const;
public:
	FloorClusterBuilder( bool *isFloodedBuffer, uint16_t *resultsBuffer, AiAasWorld *aasWorld_ )
		: AreasClusterBuilder( isFloodedBuffer, resultsBuffer, aasWorld_ ) {}

	bool Build( int startAreaNum );
};

bool FloorClusterBuilder::IsFloodedRegionDegenerate() const {
	float dimsSum = 0.0f;
	for( int i = 0; i < 2; ++i ) {
		float dims = floodedRegionMaxs[i] - floodedRegionMins[i];
		if( dims < 48.0f ) {
			return true;
		}
		dimsSum += dims;
	}

	// If there are only few single area, apply greater restrictions
	switch( ResultSize() ) {
		case 1: return dimsSum < 256.0f + 32.0f;
		case 2: return dimsSum < 192.0f + 32.0f;
		case 3: return dimsSum < 144.0f + 32.0f;
		default: return dimsSum < 144.0f;
	}
}

bool FloorClusterBuilder::Build( int startAreaNum ) {
	if( !classifyFunc.LooksLikeAFloorArea( aasWorld->AreaSettings()[startAreaNum] ) ) {
		return false;
	}

	PrepareToFlood();

	FloodAreasRecursive( startAreaNum );

	return ResultSize() && !IsFloodedRegionDegenerate();
}

struct ClassifyStairsArea {
	int operator()( const aas_area_t &currArea,
					const aas_reachability_t &reach,
					const aas_area_t &reachArea,
					const aas_areasettings_t &reachAreaSettings ) {
		if( reach.traveltype != TRAVEL_WALK && reach.traveltype != TRAVEL_WALKOFFLEDGE && reach.traveltype != TRAVEL_JUMP ) {
			// Do not disable the area for further search,
			// it might be reached by walking through some intermediate area
			return 0;
		}

		// Check whether there is a feasible height difference with the current area
		float relativeHeight = fabsf( reachArea.mins[2] - currArea.mins[2] );
		if( relativeHeight < 4 || relativeHeight > -playerbox_stand_mins[2] ) {
			// Disable the area for further search
			return -1;
		}

		// HACK: TODO: Refactor this (operator()) method params
		const auto *aasWorld = AiAasWorld::Instance();
		if( aasWorld->FloorClusterNum( &currArea - aasWorld->Areas() ) ) {
			// The area is already in a floor cluster
			return -1;
		}

		if( !LooksLikeAStairsArea( reachArea, reachAreaSettings ) ) {
			// Disable the area for further search
			return -1;
		}

		return 1;
	}

	bool LooksLikeAStairsArea( const aas_area_t &area, const aas_areasettings_t &areaSettings ) {
		if( !( areaSettings.areaflags & AREA_GROUNDED ) ) {
			return false;
		}
		if( areaSettings.areaflags & AREA_INCLINED_FLOOR ) {
			return false;
		}

		// TODO: There should be more strict tests... A substantial amount of false positives is noticed.

		// Check whether the area top projection looks like a stretched rectangle
		float dx = area.maxs[0] - area.mins[0];
		float dy = area.maxs[1] - area.mins[1];

		return dx / dy > 4.0f || dy / dx > 4.0f;
	}
};

class StairsClusterBuilder: public AreasClusterBuilder<ClassifyStairsArea>
{
	int firstAreaIndex;
	int lastAreaIndex;
	vec2_t averageDimensions;

	inline bool ConformsToDimensions( const aas_area_t &area, float conformanceRatio ) {
		for( int j = 0; j < 2; ++j ) {
			float dimension = area.maxs[j] - area.mins[j];
			float avg = averageDimensions[j];
			if( dimension < ( 1.0f / conformanceRatio ) * avg || dimension > conformanceRatio * avg ) {
				return false;
			}
		}
		return true;
	}
public:
	StaticVector<AreaAndScore, 128> areasAndHeights;

	StairsClusterBuilder( bool *isFloodedBuffer, uint16_t *resultsBuffer, AiAasWorld *aasWorld_ )
		: AreasClusterBuilder( isFloodedBuffer, resultsBuffer, aasWorld_ ), firstAreaIndex(0), lastAreaIndex(0) {}

	bool Build( int startAreaNum );

	const AreaAndScore *begin() const { return &areasAndHeights.front() + firstAreaIndex; }
	const AreaAndScore *end() const { return &areasAndHeights.front() + lastAreaIndex + 1; }
};

bool StairsClusterBuilder::Build( int startAreaNum ) {
	// We do not check intentionally whether the start area belongs to stairs itself and is not just adjacent to stairs.
	// (whether the area top projection dimensions ratio looks like a stair step)
	// (A bot might get blocked on such stairs entrance/exit areas)

	PrepareToFlood();

	const auto *aasAreas = aasWorld->Areas();
	const auto *aasAreaSettings = aasWorld->AreaSettings();

	if( !classifyFunc.LooksLikeAStairsArea( aasAreas[startAreaNum], aasAreaSettings[startAreaNum ] ) ) {
		return false;
	}

	FloodAreasRecursive( startAreaNum );

	const int numAreas = ResultSize();
	if( numAreas < 3 ) {
		return false;
	}

	if( numAreas > 128 ) {
		G_Printf( S_COLOR_YELLOW "Warning: StairsClusterBuilder::Build(): too many stairs-like areas in cluster\n" );
		return false;
	}

	const auto *areaNums = ResultAreas();
	areasAndHeights.clear();
	Vector2Set( averageDimensions, 0, 0 );
	for( int i = 0; i < numAreas; ++i) {
		const int areaNum = areaNums[i];
		// Negate the "score" so lowest areas (having the highest "score") are first after sorting
		const auto &area = aasAreas[areaNum];
		new( areasAndHeights.unsafe_grow_back() )AreaAndScore( areaNum, -area.mins[2] );
		for( int j = 0; j < 2; ++j ) {
			averageDimensions[j] += area.maxs[j] - area.mins[j];
		}
	}

	for( int j = 0; j < 2; ++j ) {
		averageDimensions[j] *= 1.0f / areasAndHeights.size();
	}

	std::sort( areasAndHeights.begin(), areasAndHeights.end() );

	// Chop first/last areas if they do not conform to average dimensions
	// This prevents inclusion of huge entrance/exit areas to the cluster
	// Ideally some size filter should be applied to cluster areas too,
	// but it has shown to produce bad results rejecting many feasible clusters.

	this->firstAreaIndex = 0;
	if( !ConformsToDimensions( aasAreas[areasAndHeights[this->firstAreaIndex].areaNum], 1.25f ) ) {
		this->firstAreaIndex++;
	}

	this->lastAreaIndex = areasAndHeights.size() - 1;
	if( !ConformsToDimensions( aasAreas[areasAndHeights[this->lastAreaIndex].areaNum], 1.25f ) ) {
		this->lastAreaIndex--;
	}

	if( end() - begin() < 3 ) {
		return false;
	}

	// Check monotone height increase ("score" decrease)
	float prevScore = areasAndHeights[firstAreaIndex].score;
	for( int i = firstAreaIndex + 1; i < lastAreaIndex; ++i ) {
		float currScore = areasAndHeights[i].score;
		if( fabsf( currScore - prevScore ) <= 1.0f ) {
			return false;
		}
		assert( currScore < prevScore );
		prevScore = currScore;
	}

	// Now add protection against Greek/Cyrillic Gamma-like stairs
	// that include an intermediate platform (like wbomb1 water stairs)
	// Check whether an addition of an area does not lead to unexpected 2D area growth.
	// (this kind of stairs should be split in two or more clusters)
	// This test should split curved stairs like on wdm4 as well.

	vec3_t boundsMins, boundsMaxs;
	ClearBounds( boundsMins, boundsMaxs );
	AddPointToBounds( aasAreas[areasAndHeights[firstAreaIndex].areaNum].mins, boundsMins, boundsMaxs );
	AddPointToBounds( aasAreas[areasAndHeights[firstAreaIndex].areaNum].maxs, boundsMins, boundsMaxs );

	float oldTotal2DArea = ( boundsMaxs[0] - boundsMins[0] ) * ( boundsMaxs[1] - boundsMins[1] );
	const float areaStepGrowthThreshold = 1.25f * averageDimensions[0] * averageDimensions[1];
	for( int areaIndex = firstAreaIndex + 1; areaIndex < lastAreaIndex; ++areaIndex ) {
		const auto &currAasArea = aasAreas[areasAndHeights[areaIndex].areaNum];
		AddPointToBounds( currAasArea.mins, boundsMins, boundsMaxs );
		AddPointToBounds( currAasArea.maxs, boundsMins, boundsMaxs );
		const float newTotal2DArea = ( boundsMaxs[0] - boundsMins[0] ) * ( boundsMaxs[1] - boundsMins[1] );
		// If there was a significant total 2D area growth
		if( newTotal2DArea - oldTotal2DArea > areaStepGrowthThreshold ) {
			lastAreaIndex = areaIndex - 1;
			break;
		}
		oldTotal2DArea = newTotal2DArea;
	}

	if( end() - begin() < 3 ) {
		return false;
	}

	// Check connectivity between adjacent stair steps, it should not be broken after sorting for real stairs

	// Let us assume we have a not stair-like environment of this kind as an algorithm input:
	//   I
	//   I I
	// I I I
	// 1 2 3

	// After sorting it looks like this:
	//     I
	//   I I
	// I I I
	// 1 3 2

	// The connectivity between steps 1<->2, 2<->3 is broken
	// (there are no mutual walk reachabilities connecting some of steps of these false stairs)

	const auto *aasReach = aasWorld->Reachabilities();
	for( int i = firstAreaIndex; i < lastAreaIndex - 1; ++i ) {
		const int prevAreaNum = areasAndHeights[i + 0].areaNum;
		const int currAreaNum = areasAndHeights[i + 1].areaNum;
		const auto &currAreaSettings = aasAreaSettings[currAreaNum];
		int currReachNum = currAreaSettings.firstreachablearea;
		const int maxReachNum = currReachNum + currAreaSettings.numreachableareas;
		for(; currReachNum < maxReachNum; ++currReachNum ) {
			const auto &reach = aasReach[currReachNum];
			// We have dropped condition on travel type of the reachability as showing unsatisfiable results
			if( reach.areanum == prevAreaNum ) {
				break;
			}
		}

		if( currReachNum == maxReachNum ) {
			return false;
		}
	}

	return true;
}

void AiAasWorld::ComputeLogicalAreaClusters() {
	auto isFloodedBuffer = (bool *)G_LevelMalloc( sizeof( bool ) * this->NumAreas() );
	auto floodResultsBuffer = (uint16_t *)G_LevelMalloc( sizeof( uint16_t ) * this->NumAreas() );

	FloorClusterBuilder floorClusterBuilder( isFloodedBuffer, floodResultsBuffer, this );
	StairsClusterBuilder stairsClusterBuilder( isFloodedBuffer, floodResultsBuffer, this );

	this->areaFloorClusterNums = (uint16_t *)G_LevelMalloc( sizeof( uint16_t ) * this->NumAreas() );
	memset( this->areaFloorClusterNums, 0, sizeof( uint16_t ) * this->NumAreas() );
	this->areaStairsClusterNums = (uint16_t *)G_LevelMalloc( sizeof( uint16_t ) * this->NumAreas() );
	memset( this->areaStairsClusterNums, 0, sizeof( uint16_t ) * this->NumAreas() );

	BufferBuilder<uint16_t> floorData( 256 );
	BufferBuilder<int> floorDataOffsets( 32 );
	BufferBuilder<uint16_t> stairsData( 128 );
	BufferBuilder<int> stairsDataOffsets( 16 );

	// Add dummy clusters at index 0 in order to conform to the rest of AAS code
	numFloorClusters = 1;
	floorDataOffsets.Add( 0 );
	floorData.Add( 0 );

	for( int i = 1; i < this->NumAreas(); ++i ) {
		// If an area is already marked
		if( areaFloorClusterNums[i] ) {
			continue;
		}

		if( !floorClusterBuilder.Build( i ) ) {
			continue;
		}

		// Important: Mark all areas in the built cluster
		int numClusterAreas = floorClusterBuilder.ResultSize();
		const auto *clusterAreaNums = floorClusterBuilder.ResultAreas();
		for( int j = 0; j < numClusterAreas; ++j ) {
			areaFloorClusterNums[clusterAreaNums[j]] = (uint16_t)numFloorClusters;
		}

		numFloorClusters++;
		floorDataOffsets.Add( floorData.Size() );
		floorData.Add( (uint16_t)numClusterAreas );
		floorData.Add( clusterAreaNums, numClusterAreas );
	}

	assert( numFloorClusters == floorDataOffsets.Size() );
	this->floorClusterDataOffsets = floorDataOffsets.FlattenResult();
	// Clear as no longer needed immediately for same reasons
	floorDataOffsets.Clear();
	this->floorClusterData = floorData.FlattenResult();
	floorData.Clear();

	numStairsClusters = 1;
	stairsDataOffsets.Add( 0 );
	stairsData.Add( 0 );

	for( int i = 0; i < this->NumAreas(); ++i ) {
		// If an area is already marked
		if( areaFloorClusterNums[i] || areaStairsClusterNums[i] ) {
			continue;
		}

		if( !stairsClusterBuilder.Build( i ) ) {
			continue;
		}

		// Important: Mark all areas in the built cluster
		for( auto iter = stairsClusterBuilder.begin(), end = stairsClusterBuilder.end(); iter != end; ++iter ) {
			areaStairsClusterNums[iter->areaNum] = (uint16_t)numStairsClusters;
		}

		numStairsClusters++;
		// Add the current stairs data size to the offsets array
		stairsDataOffsets.Add( stairsData.Size() );
		// Add the actual stairs data length for the current cluster
		stairsData.Add( (uint16_t)( stairsClusterBuilder.end() - stairsClusterBuilder.begin() ) );
		// Save areas preserving sorting by height
		for( auto iter = stairsClusterBuilder.begin(), end = stairsClusterBuilder.end(); iter != end; ++iter ) {
			stairsData.Add( (uint16_t)( iter->areaNum ) );
		}
	}

	// Clear as no longer needed to provide free space for further allocations
	G_LevelFree( isFloodedBuffer );
	G_LevelFree( floodResultsBuffer );

	assert( numStairsClusters == stairsDataOffsets.Size() );
	this->stairsClusterDataOffsets = stairsDataOffsets.FlattenResult();
	stairsDataOffsets.Clear();
	this->stairsClusterData = stairsData.FlattenResult();

	constexpr auto *format =
		"AiAasWorld: %d floor clusters, %d stairs clusters "
		"(including dummy zero ones) have been detected\n";
	G_Printf( format, numFloorClusters, numStairsClusters );
}

void AiAasWorld::ComputeFace2DProjVertices() {
	face2DProjVertexNums = (int *)G_LevelMalloc( sizeof( int ) * 2 * this->NumFaces() );
	int *vertexNumsPtr = face2DProjVertexNums;

	// Skip 2 vertices for the dummy zero face
	vertexNumsPtr += 2;

	const auto *faces = this->faces;
	const auto *edgeIndex = this->edgeindex;
	const auto *edges = this->edges;
	const auto *vertices = this->vertexes;

	for( int i = 1; i < numfaces; ++i ) {
		const auto &face = faces[i];
		int edgeIndexNum = face.firstedge;
		const int endEdgeIndexNum = edgeIndexNum + face.numedges;
		// Put dummy values by default. Make sure they're distinct.
		int n1 = 0, n2 = 1;
		for(; edgeIndexNum != endEdgeIndexNum; ++edgeIndexNum ) {
			const auto &edge = edges[abs( edgeIndex[edgeIndexNum] )];
			int ev1 = edge.v[0];
			int ev2 = edge.v[1];
			Vec3 dir( vertices[ev1] );
			dir -= vertices[ev2];
			dir.NormalizeFast();
			if( fabsf( dir.Z() ) > 0.001f ) {
				continue;
			}
			n1 = ev1;
			n2 = ev2;
			break;
		}
		*vertexNumsPtr++ = n1;
		*vertexNumsPtr++ = n2;
	}
}

void AiAasWorld::ComputeAreasLeafsLists() {
	BufferBuilder<int> leafListsData( 512 );
	BufferBuilder<int> listOffsets( 128 );

	// Add a dummy list for the dummy zero area
	leafListsData.Add( 0 );
	listOffsets.Add( 0 );

	int tmpLeafNums[256];
	int topNode;
	for( int i = 1, end = this->NumAreas(); i < end; ++i ) {
		int numLeafs = trap_CM_BoxLeafnums( areas[i].mins, areas[i].maxs, tmpLeafNums, 256, &topNode );
		listOffsets.Add( (int)listOffsets.Size() );
		leafListsData.Add( tmpLeafNums, std::min( 256, numLeafs ) );
	}

	this->areaMapLeafListOffsets = listOffsets.FlattenResult();
	// Clear early to free some allocation space for flattening of the next result
	listOffsets.Clear();
	this->areaMapLeafsData = leafListsData.FlattenResult();
}