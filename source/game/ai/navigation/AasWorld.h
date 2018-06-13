#ifndef QFUSION_AI_AAS_WORLD_H
#define QFUSION_AI_AAS_WORLD_H

#include "../../../gameshared/q_math.h"
#include "../../../gameshared/q_shared.h"
#include "../vec3.h"

//presence types
#define PRESENCE_NONE               1
#define PRESENCE_NORMAL             2
#define PRESENCE_CROUCH             4

//travel types
#define MAX_TRAVELTYPES             32
#define TRAVEL_INVALID              1       //temporary not possible
#define TRAVEL_WALK                 2       //walking
#define TRAVEL_CROUCH               3       //crouching
#define TRAVEL_BARRIERJUMP          4       //jumping onto a barrier
#define TRAVEL_JUMP                 5       //jumping
#define TRAVEL_LADDER               6       //climbing a ladder
#define TRAVEL_WALKOFFLEDGE         7       //walking of a ledge
#define TRAVEL_SWIM                 8       //swimming
#define TRAVEL_WATERJUMP            9       //jump out of the water
#define TRAVEL_TELEPORT             10      //teleportation
#define TRAVEL_ELEVATOR             11      //travel by elevator
#define TRAVEL_ROCKETJUMP           12      //rocket jumping required for travel
#define TRAVEL_BFGJUMP              13      //bfg jumping required for travel
#define TRAVEL_GRAPPLEHOOK          14      //grappling hook required for travel
#define TRAVEL_DOUBLEJUMP           15      //double jump
#define TRAVEL_RAMPJUMP             16      //ramp jump
#define TRAVEL_STRAFEJUMP           17      //strafe jump
#define TRAVEL_JUMPPAD              18      //jump pad
#define TRAVEL_FUNCBOB              19      //func bob

//additional travel flags
#define TRAVELTYPE_MASK             0xFFFFFF
#define TRAVELFLAG_NOTTEAM1         ( 1 << 24 )
#define TRAVELFLAG_NOTTEAM2         ( 2 << 24 )

//face flags
#define FACE_SOLID                  1       //just solid at the other side
#define FACE_LADDER                 2       //ladder
#define FACE_GROUND                 4       //standing on ground when in this face
#define FACE_GAP                    8       //gap in the ground
#define FACE_LIQUID                 16      //face seperating two areas with liquid
#define FACE_LIQUIDSURFACE          32      //face seperating liquid and air
#define FACE_BRIDGE                 64      //can walk over this face if bridge is closed

//area contents
#define AREACONTENTS_WATER              1
#define AREACONTENTS_LAVA               2
#define AREACONTENTS_SLIME              4
#define AREACONTENTS_CLUSTERPORTAL      8
#define AREACONTENTS_TELEPORTAL         16
#define AREACONTENTS_ROUTEPORTAL        32
#define AREACONTENTS_TELEPORTER         64
#define AREACONTENTS_JUMPPAD            128
#define AREACONTENTS_DONOTENTER         256
#define AREACONTENTS_VIEWPORTAL         512
#define AREACONTENTS_MOVER              1024
#define AREACONTENTS_NOTTEAM1           2048
#define AREACONTENTS_NOTTEAM2           4096
//number of model of the mover inside this area
#define AREACONTENTS_MODELNUMSHIFT      24
#define AREACONTENTS_MAXMODELNUM        0xFF
#define AREACONTENTS_MODELNUM           ( AREACONTENTS_MAXMODELNUM << AREACONTENTS_MODELNUMSHIFT )

//area flags
#define AREA_GROUNDED               1       // a bot can stand on the ground
#define AREA_LADDER                 2       // an area contains one or more ladder faces
#define AREA_LIQUID                 4       // an area contains a liquid
#define AREA_DISABLED               8       // an area is disabled for routing when set
#define AREA_BRIDGE                 16      // an area is on top of a bridge

// These flags are specific to this engine and are set on world loading based on various computations
#define AREA_LEDGE            ( 1 << 10 )  // an area looks like a ledge. This flag is set on world loading.
#define AREA_WALL             ( 1 << 11 )  // an area has bounding solid walls.
#define AREA_JUNK             ( 1 << 12 )  // an area does not look like useful.
#define AREA_INCLINED_FLOOR   ( 1 << 13 )  // an area has an inclined floor (AAS treats these areas as having a flat one)
#define AREA_SLIDABLE_RAMP    ( 1 << 14 )  // an area is a slidable ramp (AREA_INCLINED_FLOOR is implied and set too)

// if a bot is in any point of an area and is in air above ground,
// it's legal to skip collision in PMove() for specified suffix units around/above.
#define AREA_SKIP_COLLISION_16   ( 1 << 20 )
#define AREA_SKIP_COLLISION_32   ( 1 << 21 )
#define AREA_SKIP_COLLISION_48   ( 1 << 22 )

// a movement in an area is safe from falling/entering a hazard
// (this is currently not a 100% guarantee but an optimistic estimation)
#define AREA_NOFALL              ( 1 << 25 )

//========== bounding box =========

//bounding box
typedef struct aas_bbox_s {
	int presencetype;
	int flags;
	vec3_t mins, maxs;
} aas_bbox_t;

//============ settings ===========

//reachability to another area
typedef struct aas_reachability_s {
	int areanum;                        //number of the reachable area
	int facenum;                        //number of the face towards the other area
	int edgenum;                        //number of the edge towards the other area
	vec3_t start;                       //start point of inter area movement
	vec3_t end;                         //end point of inter area movement
	int traveltype;                 //type of travel required to get to the area
	unsigned short int traveltime;//travel time of the inter area movement
} aas_reachability_t;

//area settings
typedef struct aas_areasettings_s {
	//could also add all kind of statistic fields
	int contents;                       //contents of the area
	int areaflags;                      //several area flags
	int presencetype;                   //how a bot can be present in this area
	int cluster;                        //cluster the area belongs to, if negative it's a portal
	int clusterareanum;             //number of the area in the cluster
	int numreachableareas;          //number of reachable areas from this one
	int firstreachablearea;         //first reachable area in the reachable area index
} aas_areasettings_t;

//cluster portal
typedef struct aas_portal_s {
	int areanum;                        //area that is the actual portal
	int frontcluster;                   //cluster at front of portal
	int backcluster;                    //cluster at back of portal
	int clusterareanum[2];          //number of the area in the front and back cluster
} aas_portal_t;

//cluster portal index
typedef int aas_portalindex_t;

//cluster
typedef struct aas_cluster_s {
	int numareas;                       //number of areas in the cluster
	int numreachabilityareas;           //number of areas with reachabilities
	int numportals;                     //number of cluster portals
	int firstportal;                    //first cluster portal in the index
} aas_cluster_t;

//============ 3d definition ============

typedef vec3_t aas_vertex_t;

//just a plane in the third dimension
typedef struct aas_plane_s {
	vec3_t normal;                      //normal vector of the plane
	float dist;                         //distance of the plane (normal vector * distance = point in plane)
	int type;
} aas_plane_t;

//edge
typedef struct aas_edge_s {
	int v[2];                           //numbers of the vertexes of this edge
} aas_edge_t;

//edge index, negative if vertexes are reversed
typedef int aas_edgeindex_t;

//a face bounds an area, often it will also seperate two areas
typedef struct aas_face_s {
	int planenum;                       //number of the plane this face is in
	int faceflags;                      //face flags (no use to create face settings for just this field)
	int numedges;                       //number of edges in the boundary of the face
	int firstedge;                      //first edge in the edge index
	int frontarea;                      //area at the front of this face
	int backarea;                       //area at the back of this face
} aas_face_t;

//face index, stores a negative index if backside of face
typedef int aas_faceindex_t;

//area with a boundary of faces
typedef struct aas_area_s {
	int areanum;                        //number of this area
	//3d definition
	int numfaces;                       //number of faces used for the boundary of the area
	int firstface;                      //first face in the face index used for the boundary of the area
	vec3_t mins;                        //mins of the area
	vec3_t maxs;                        //maxs of the area
	vec3_t center;                      //'center' of the area
} aas_area_t;

//nodes of the bsp tree
typedef struct aas_node_s {
	int planenum;
	int children[2];                    //child nodes of this node, or areas as leaves when negative
	//when a child is zero it's a solid leaf
} aas_node_t;

class AiAasWorld
{
	bool loaded = false;
	// Should be released by G_LevelFree();
	char *checksum = nullptr;

	//bounding boxes
	int numbboxes;
	aas_bbox_t *bboxes;
	//vertexes
	int numvertexes;
	aas_vertex_t *vertexes;
	//planes
	int numplanes;
	aas_plane_t *planes;
	//edges
	int numedges;
	aas_edge_t *edges;
	//edge index
	int edgeindexsize;
	aas_edgeindex_t *edgeindex;
	//faces
	int numfaces;
	aas_face_t *faces;
	//face index
	int faceindexsize;
	aas_faceindex_t *faceindex;
	//convex areas
	int numareas;
	aas_area_t *areas;
	//convex area settings
	int numareasettings;
	aas_areasettings_t *areasettings;
	//reachablity list
	int reachabilitysize;
	aas_reachability_t *reachability;
	//nodes of the bsp tree
	int numnodes;
	aas_node_t *nodes;
	//cluster portals
	int numportals;
	aas_portal_t *portals;
	//cluster portal index
	int portalindexsize;
	aas_portalindex_t *portalindex;
	//clusters
	int numclusters;
	aas_cluster_t *clusters;

	//structure to link entities to areas and areas to entities
	typedef struct aas_link_s {
		int entnum;
		int areanum;
		struct aas_link_s *next_ent, *prev_ent;
		struct aas_link_s *next_area, *prev_area;
	} aas_link_t;

	//enities linked in the areas
	aas_link_t *linkheap;                       //heap with link structures
	int linkheapsize;                           //size of the link heap
	aas_link_t *freelinks;                      //first free link
	aas_link_t **arealinkedentities;            //entities linked into areas
	int numaaslinks;

	uint16_t *areaFloorClusterNums;            // A number of a floor cluster for an area, 0 = not in a floor cluster
	uint16_t *areaStairsClusterNums;           // A number of a stairs cluster for an area, 0 = not in a stairs cluster

	int numFloorClusters;
	int numStairsClusters;

	int *floorClusterDataOffsets;    // An element #i contains an offset of cluster elements sequence in the joint data
	int *stairsClusterDataOffsets;   // An element #i contains an offset of cluster elements sequence in the joint data

	uint16_t *floorClusterData;    // Contains floor clusters element sequences, each one is prepended by the length
	uint16_t *stairsClusterData;    // Contains stairs clusters element sequences, each one is prepended by the length

	int *face2DProjVertexNums;     // Elements #i*2, #i*2+1 contain numbers of vertices of a 2d face proj for face #i

	int *areaMapLeafListOffsets;    // An element #i contains an offset of leafs list data in the joint data
	int *areaMapLeafsData;          // Contains area map (collision/vis) leafs lists, each one is prepended by the length

	static AiAasWorld *instance;

	AiAasWorld() {
		memset( this, 0, sizeof( AiAasWorld ) );
	}

	bool Load( const char *mapname );

	void PostLoad() {
		InitLinkHeap();
		InitLinkedEntities();
		ComputeExtraAreaData();
	}

	void SwapData();

	void InitLinkHeap();
	void InitLinkedEntities();
	// Computes extra Qfusion area flags based on loaded world data
	void ComputeExtraAreaData();
	// Computes extra Qfusion area floor and stairs clusters
	void ComputeLogicalAreaClusters();
    // Computes vertices of top 2D face projections
	void ComputeFace2DProjVertices();
	// Computes map (collision/vis) leafs for areas
	void ComputeAreasLeafsLists();

	void TrySetAreaLedgeFlags( int areaNum );
	void TrySetAreaWallFlags( int areaNum );
	void TrySetAreaJunkFlags( int areaNum );
	void TrySetAreaRampFlags( int areaNum );

	void TrySetAreaNoFallFlags( int areaNum );

	// Should be called after all other flags are computed
	void TrySetAreaSkipCollisionFlags();

	void FreeLinkHeap();
	void FreeLinkedEntities();

	aas_link_t *LinkEntity( const vec3_t absmins, const vec3_t absmaxs, int entnum );
	void UnlinkFromAreas( aas_link_t *linkedAreas );
	aas_link_t *AllocLink();
	void DeAllocLink( aas_link_t *link );

	int BoxOnPlaneSide2( const vec3_t absmins, const vec3_t absmaxs, const aas_plane_t *p );

	int FindAreaNum( const vec3_t mins, const vec3_t maxs ) const;
	int BBoxAreasNonConst( const vec3_t absmins, const vec3_t absmaxs, int *areas_, int maxareas );

public:
	AiAasWorld( AiAasWorld &&that );
	~AiAasWorld();

	// AiAasWorld should be init and shut down explicitly
	// (a game library is not unloaded when a map changes)
	static bool Init( const char *mapname );
	static void Shutdown();
	static AiAasWorld *Instance() { return instance; }

	inline bool IsLoaded() const { return loaded; }
	inline const char *Checksum() const { return loaded ? (const char *)checksum : ""; }

	void Frame();

	inline int TraceAreas( const Vec3 &start, const Vec3 &end, int *areas_, int maxareas ) const {
		return TraceAreas( start.Data(), end.Data(), areas_, nullptr, maxareas );
	}
	inline int TraceAreas( const vec3_t start, const vec3_t end, int *areas_, int maxareas ) const {
		return TraceAreas( start, end, areas_, nullptr, maxareas );
	}

	//stores the areas the trace went through and returns the number of passed areas
	int TraceAreas( const vec3_t start, const vec3_t end, int *areas_, vec3_t *points, int maxareas ) const;

	inline int BBoxAreas( const Vec3 &absmins, const Vec3 &absmaxs, int *areas_, int maxareas ) const {
		return BBoxAreas( absmins.Data(), absmaxs.Data(), areas_, maxareas );
	}

	//returns the areas the bounding box is in
	int BBoxAreas( const vec3_t absmins, const vec3_t absmaxs, int *areas_, int maxareas ) const {
		return ( const_cast<AiAasWorld*>( this ) )->BBoxAreasNonConst( absmins, absmaxs, areas_, maxareas );
	}

	//returns the area the point is in
	int PointAreaNum( const vec3_t point ) const;

	// If an area is not found, tries to adjust the origin a bit
	inline int FindAreaNum( const Vec3 &origin ) const {
		return FindAreaNum( origin.Data() );
	}

	// If an area is not found, tries to adjust the origin a bit
	int FindAreaNum( const vec3_t origin ) const;
	// Tries to find some area the ent is in
	int FindAreaNum( const struct edict_s *ent ) const;

	//returns true if the area is crouch only
	inline bool AreaCrouch( int areanum ) const {
		return areasettings[areanum].presencetype != PRESENCE_NORMAL;
	}
	//returns true if a player can swim in this area
	inline bool AreaSwim( int areanum ) const {
		return ( areasettings[areanum].areaflags & AREA_LIQUID ) != 0;
	}
	//returns true if the area is filled with a liquid
	inline bool AreaLiquid( int areanum ) const {
		return ( areasettings[areanum].areaflags & AREA_LIQUID ) != 0;
	}
	//returns true if the area contains lava
	inline bool AreaLava( int areanum ) const {
		return ( areasettings[areanum].contents & AREACONTENTS_LAVA ) != 0;
	}
	//returns true if the area contains slime
	inline bool AreaSlime( int areanum ) const {
		return ( areasettings[areanum].contents & AREACONTENTS_SLIME ) != 0;
	}
	//returns true if the area has one or more ground faces
	inline bool AreaGrounded( int areanum ) const {
		return ( areasettings[areanum].areaflags & AREA_GROUNDED ) != 0;
	}
	//returns true if the area has one or more ladder faces
	inline bool AreaLadder( int areanum ) const {
		return ( areasettings[areanum].areaflags & AREA_LADDER ) != 0;
	}
	//returns true if the area is a jump pad
	inline bool AreaJumpPad( int areanum ) const {
		return ( areasettings[areanum].contents & AREACONTENTS_JUMPPAD ) != 0;
	}
	inline bool AreaTeleporter( int areanum ) const {
		return ( areasettings[areanum].contents & AREACONTENTS_TELEPORTER ) != 0;
	}
	//returns true if the area is donotenter
	inline bool AreaDoNotEnter( int areanum ) const {
		return ( areasettings[areanum].contents & AREACONTENTS_DONOTENTER ) != 0;
	}

	//bounding boxes
	inline int NumBBoxes() const { return numbboxes; }
	inline const aas_bbox_t *BBoxes() const { return bboxes; }
	//vertexes
	inline int NumVertexes() const { return numvertexes; }
	inline const aas_vertex_t *Vertexes() const { return vertexes; }
	//planes
	inline int NumPlanes() const { return numplanes; }
	inline const aas_plane_t *Planes() const { return planes; }
	//edges
	inline int NumEdges() const { return numedges; }
	inline const aas_edge_t *Edges() const { return edges; }
	//edge index
	inline int EdgeIndexSize() const { return edgeindexsize; }
	inline const aas_edgeindex_t *EdgeIndex() const { return edgeindex; }
	//faces
	inline int NumFaces() const { return numfaces; }
	inline const aas_face_t *Faces() const { return faces; }
	//face index
	inline int FaceIndexSize() const { return faceindexsize; };
	inline const aas_faceindex_t *FaceIndex() const { return faceindex; }
	//convex areas
	inline int NumAreas() const { return numareas; }
	inline const aas_area_t *Areas() const { return areas; }
	//convex area settings
	inline int NumAreaSettings() const { return numareasettings; }
	inline const aas_areasettings_t *AreaSettings() const { return areasettings; }
	//reachablity list
	inline int NumReachabilities() const { return reachabilitysize; }
	inline const aas_reachability_t *Reachabilities() const { return reachability; }
	//nodes of the bsp tree
	inline int NumNodes() const { return numnodes; }
	inline const aas_node_t *Nodes() const { return nodes; }
	//cluster portals
	inline int NumPortals() const { return numportals; }
	inline const aas_portal_t *Portals() const { return portals; }
	//cluster portal index
	inline int PortalIndexSize() const { return portalindexsize; }
	inline const aas_portalindex_t *PortalIndex() const { return portalindex; }
	//clusters
	inline int NumClusters() const { return numclusters; }
	inline const aas_cluster_t *Clusters() const { return clusters; }

	inline int NumFloorClusters() const { return numFloorClusters; }
	inline int NumStairsClusters() const { return numStairsClusters; }

	// A feasible cluster num in non-zero
	inline uint16_t FloorClusterNum( int areaNum ) const {
		return loaded ? areaFloorClusterNums[areaNum] : (uint16_t)0;
	}

	inline uint16_t *AreaFloorClusterNums() const { return areaFloorClusterNums; }

	// A feasible cluster num is non-zero
	inline uint16_t StairsClusterNum( int areaNum ) const {
		return loaded ? areaStairsClusterNums[areaNum] : (uint16_t)0;
	}

	inline uint16_t *AreaStairsClusterNums() const { return areaStairsClusterNums; }

	// In order to be conform with the rest of AAS code the zero cluster is dummy
	inline const uint16_t *FloorClusterData( int floorClusterNum ) const {
		assert( floorClusterNum >= 0 && floorClusterNum < numFloorClusters );
		return floorClusterData + floorClusterDataOffsets[floorClusterNum];
	}

	// In order to be conform with the rest of AAS code the zero cluster is dummy
	inline const uint16_t *StairsClusterData( int stairsClusterNum ) const {
		assert( stairsClusterNum >= 0 && stairsClusterNum < numStairsClusters );
		return stairsClusterData + stairsClusterDataOffsets[stairsClusterNum];
	}

	inline int *Face2DProjVertexNums() const { return face2DProjVertexNums; }

	inline const int *AreaMapLeafsList( int areaNum ) const {
		assert( areaNum >= 0 && areaNum < numareas );
		return areaMapLeafsData + areaMapLeafListOffsets[areaNum];
	}
};

#endif
