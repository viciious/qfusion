#ifndef QFUSION_AI_ROUTE_CACHE_H
#define QFUSION_AI_ROUTE_CACHE_H

#include "ai_aas_world.h"

//travel flags
#define TFL_INVALID             0x00000001  //traveling temporary not possible
#define TFL_WALK                0x00000002  //walking
#define TFL_CROUCH              0x00000004  //crouching
#define TFL_BARRIERJUMP         0x00000008  //jumping onto a barrier
#define TFL_JUMP                0x00000010  //jumping
#define TFL_LADDER              0x00000020  //climbing a ladder
#define TFL_WALKOFFLEDGE        0x00000080  //walking of a ledge
#define TFL_SWIM                0x00000100  //swimming
#define TFL_WATERJUMP           0x00000200  //jumping out of the water
#define TFL_TELEPORT            0x00000400  //teleporting
#define TFL_ELEVATOR            0x00000800  //elevator
#define TFL_ROCKETJUMP          0x00001000  //rocket jumping
#define TFL_BFGJUMP             0x00002000  //bfg jumping
#define TFL_GRAPPLEHOOK         0x00004000  //grappling hook
#define TFL_DOUBLEJUMP          0x00008000  //double jump
#define TFL_RAMPJUMP            0x00010000  //ramp jump
#define TFL_STRAFEJUMP          0x00020000  //strafe jump
#define TFL_JUMPPAD             0x00040000  //jump pad
#define TFL_AIR                 0x00080000  //travel through air
#define TFL_WATER               0x00100000  //travel through water
#define TFL_SLIME               0x00200000  //travel through slime
#define TFL_LAVA                0x00400000  //travel through lava
#define TFL_DONOTENTER          0x00800000  //travel through donotenter area
#define TFL_FUNCBOB             0x01000000  //func bobbing
#define TFL_FLIGHT              0x02000000  //flight
#define TFL_BRIDGE              0x04000000  //move over a bridge
//
#define TFL_NOTTEAM1            0x08000000  //not team 1
#define TFL_NOTTEAM2            0x10000000  //not team 2

//default travel flags
#define TFL_DEFAULT TFL_WALK | TFL_CROUCH | TFL_BARRIERJUMP | \
	TFL_JUMP | TFL_LADDER | \
	TFL_WALKOFFLEDGE | TFL_SWIM | TFL_WATERJUMP | \
	TFL_TELEPORT | TFL_ELEVATOR | \
	TFL_AIR | TFL_WATER | TFL_JUMPPAD | TFL_FUNCBOB

class AiAasRouteCache
{
	const int *const travelFlags;
	// Used to provide a dummy writable address for several routing calls
	// where we do not want to add extra branching for every call if an out parameter is unused.
	mutable int dummyIntPtr[1];

	static constexpr int CACHETYPE_PORTAL = 0;
	static constexpr int CACHETYPE_AREA = 1;

	//routing cache
	typedef struct aas_routingcache_s {
		int type;                                   //portal or area cache
		int size;                                   //size of the routing cache
		int cluster;                                //cluster the cache is for
		int areanum;                                //area the cache is created for
		vec3_t origin;                              //origin within the area
		float starttraveltime;                      //travel time to start with
		int travelflags;                            //combinations of the travel flags
		struct aas_routingcache_s *prev, *next;
		struct aas_routingcache_s *time_prev, *time_next;
		unsigned char *reachabilities;              //reachabilities used for routing
		unsigned short int traveltimes[1];          //travel time for every area (variable sized)
	} aas_routingcache_t;

	//fields for the routing algorithm
	typedef struct aas_routingupdate_s {
		int cluster;
		int areanum;                                //area number of the update
		unsigned short int tmptraveltime;           //temporary travel time
		unsigned short int *areatraveltimes;        //travel times within the area
	} aas_routingupdate_t;

	//reversed reachability link
	typedef struct aas_reversedlink_s {
		int linknum;                                //the aas_areareachability_t
		int areanum;                                //reachable from this area
		struct aas_reversedlink_s *next;            //next link
	} aas_reversedlink_t;

	//reversed area reachability
	typedef struct aas_reversedreachability_s {
		int numlinks;
		aas_reversedlink_t *first;
	} aas_reversedreachability_t;

	//areas a reachability goes through
	typedef struct aas_reachabilityareas_s {
		int firstarea, numareas;
	} aas_reachabilityareas_t;

	const AiAasWorld &aasWorld;

	bool loaded;

	// These three following buffers are allocated at once, and only the first one should be released.
	// Total size of compound allocated buffer is aasWorld.NumAreas() * (2 * sizeof(int) + 2 * sizeof(bool))
	// A scratchpad for SetDisabledRegions() that is capable to store aasWorld.NumAreas() values
	int *currDisabledAreaNums;
	// A scratchpad for SetDisabledRegions() that is capable to store aasWorld.NumAreas() values
	int *cleanCacheAreaNums;

	// It is sufficient to fit all required info in 2 bits, but we should avoid using bitsets
	// since variable shifts are required for access patterns used by implemented algorithms,
	// and variable shift instructions are usually microcoded.
	struct alignas( 1 )AreaDisabledStatus {
		uint8_t value;

		// We hope a compiler avoids using branches here
		bool OldStatus() const { return (bool)( ( value >> 1 ) & 1 ); }
		bool CurrStatus() const { return (bool)( ( value >> 0 ) & 1 ); }

		// Also we hope a compiler eliminates branches for a known constant status
		void SetOldStatus( bool status ) {
			status ? ( value |= 2 ) : ( value &= ~2 );
		}

		void SetCurrStatus( bool status ) {
			status ? ( value |= 1 ) : ( value &= ~1 );
		}

		// Copies curr status to old status and clears the curr status
		void ShiftCurrToOldStatus() {
			// Clear 6 high bits to avoid confusion
			value &= 3;
			// Promote the curr bit to the old bit position
			value <<= 1;
		}
	};

	AreaDisabledStatus *areasDisabledStatus;

	//index to retrieve travel flag for a travel type
	// Note this is not shared for faster local acccess
	int travelflagfortype[MAX_TRAVELTYPES];
	//travel flags for each area based on contents
	int *areacontentstravelflags;
	//routing update
	aas_routingupdate_t *areaupdate;
	aas_routingupdate_t *portalupdate;
	//reversed reachability links
	aas_reversedreachability_t *reversedreachability;
	signed char *dijkstralabels;
	int maxreachabilityareas;
	//travel times within the areas
	unsigned short ***areatraveltimes;
	//array of size numclusters with cluster cache
	aas_routingcache_t ***clusterareacache;
	aas_routingcache_t **portalcache;
	//cache list sorted on time
	aas_routingcache_t *oldestcache;        // start of cache list sorted on time
	aas_routingcache_t *newestcache;        // end of cache list sorted on time
	//maximum travel time through portal areas
	int *portalmaxtraveltimes;
	//areas the reachabilities go through
	int *reachabilityareaindex;
	aas_reachabilityareas_t *reachabilityareas;

	// We have to waste 8 bytes for the ref count since blocks should be at least 8-byte aligned
	inline static const int64_t RefCountOf( const void *chunk ) { return *( ( (int64_t *)chunk ) - 1 ); }
	inline static int64_t &RefCountOf( void *chunk ) { return *( ( (int64_t *)chunk ) - 1 ); }

	inline static void AddRef( void *chunk ) { RefCountOf( chunk )++; }

	inline void *GetClearedRefCountedMemory( int size ) {
		void *mem = ( (int64_t *)GetClearedMemory( size + 8 ) ) + 1;
		RefCountOf( mem ) = 1;
		return mem;
	}

	inline void FreeRefCountedMemory( void *ptr ) {
		--RefCountOf( ptr );
		if( !RefCountOf( ptr ) ) {
			FreeMemory( ( (int64_t *)ptr ) - 1 );
		}
	}

	// A linked list for bins of relatively large size
	class AreaAndPortalCacheBin *areaAndPortalCacheHead;
	// A table of small size bins addressed by bin size
	class AreaAndPortalCacheBin *areaAndPortalCacheTable[128];

	class ResultCache
	{
public:
		static constexpr unsigned MAX_CACHED_RESULTS = 512;
		// A prime number
		// (we have increased it since bin pointers have been replaced by short integers,
		// but not very much since we would not win in space and thus in CPU cache efficiency)
		static constexpr unsigned NUM_HASH_BINS = 1181;

		struct alignas( 8 )Node {
			uint64_t key;

			// A compact representation of linked list links.
			// A negative index corresponds to a null pointer.
			// Otherwise, an index points to a corresponding element in ResultCache::nodes array
			struct alignas( 2 )Links {
				int16_t prev;
				int16_t next;

				bool HasPrev() { return prev >= 0; }
				bool HasNext() { return next >= 0; }
			};

			enum { BIN_LINKS, LIST_LINKS };

			Links links[2];

			uint16_t reachability;
			uint16_t travelTime;
			uint16_t binIndex;

			// Totally 22 bytes, so only 2 bytes are wasted for 8-byte alignment

			Links &ListLinks() { return links[LIST_LINKS]; }
			const Links &ListLinks() const { return links[LIST_LINKS]; }
			Links &BinLinks() { return links[BIN_LINKS]; }
			const Links &BinLinks() const  { return links[BIN_LINKS]; }
		};

		// Assuming that area nums are limited by 16 bits, all parameters can be composed in a single integer
		static inline uint64_t Key( int fromAreaNum, int toAreaNum, int travelFlags ) {
			assert( fromAreaNum >= 0 && fromAreaNum <= 0xFFFF );
			assert( toAreaNum >= 0 && toAreaNum <= 0xFFFF );
			return ( (uint64_t)travelFlags << 32 ) | ( (uint16_t)fromAreaNum << 16 ) | ( (uint16_t)toAreaNum );
		}

		static inline uint16_t BinIndexForKey( uint64_t key ) {
			// Convert a 64-bit key to 32-bit hash trying to preserve bits entropy.
			// The primary purpose of it is avoiding 64-bit division in modulo computation
			constexpr uint32_t mask32 = 0xFFFFFFFFu;
			uint32_t loPart32 = (uint32_t)( key & mask32 );
			uint32_t hiPart32 = (uint32_t)( ( key >> 32 ) & mask32 );
			uint32_t hash = loPart32 * 17 + hiPart32;
			static_assert( NUM_HASH_BINS < 0xFFFF, "Bin indices are assumed to be short" );
			return (uint16_t)( hash % NUM_HASH_BINS );
		}
private:
		Node nodes[MAX_CACHED_RESULTS];
		// We could keep these links as pointers since they do not require a compact storage,
		// but its better to stay uniform and use common link/unlink methods
		int16_t freeNode;
		int16_t newestUsedNode;
		int16_t oldestUsedNode;

		int16_t bins[NUM_HASH_BINS];

		static inline bool IsValidLink( int16_t link ) { return link >= 0; }
		inline int16_t LinkOf( const Node *node ) { return (int16_t)( node - nodes ); }

		// Constexpr usage leads to "symbol not found" crash on library loading.
		enum { NULL_LINK = -1 };

		inline void LinkToHashBin( uint16_t binIndex, Node *node );
		inline void LinkToUsedList( Node *node );

		inline Node *UnlinkOldestUsedNode();
		inline void UnlinkOldestUsedNodeFromBin();
		inline void UnlinkOldestUsedNodeFromList();

public:
		inline ResultCache() { Clear(); }

		void Clear();

		// The key and bin index must be computed by callers using Key() and BinIndexForKey().
		// This is a bit ugly but encourages efficient usage patterns.
		const Node *GetCachedResultForKey( uint16_t binIndex, uint64_t key ) const;
		Node *AllocAndRegisterForKey( uint16_t binIndex, uint64_t key );
	};

	ResultCache resultCache;

	inline int ClusterAreaNum( int cluster, int areanum );
	void InitTravelFlagFromType();

	void LinkCache( aas_routingcache_t *cache );
	void UnlinkCache( aas_routingcache_t *cache );

	void FreeRoutingCache( aas_routingcache_t *cache );

	void RemoveRoutingCacheInClusterForArea( int areaNum );
	void RemoveRoutingCacheInCluster( int clusternum );
	void RemoveAllPortalsCache();

	int GetAreaContentsTravelFlags( int areanum );

	void *GetClearedMemory( int size );
	void FreeMemory( void *ptr );

	void *AllocAreaAndPortalCacheMemory( int size );
	void FreeAreaAndPortalCacheMemory( void *ptr );

	void FreeAreaAndPortalMemoryPools();

	bool FreeOldestCache();
	aas_routingcache_t *AllocRoutingCache( int numtraveltimes );
	void UnlinkAndFreeRoutingCache( aas_routingcache_t *cache );
	void UnlinkAreaRoutingCache( aas_routingcache_t *cache );
	void UnlinkPortalRoutingCache( aas_routingcache_t *cache );

	void UpdateAreaRoutingCache( aas_routingcache_t *areaCache );
	aas_routingcache_t *GetAreaRoutingCache( int clusternum, int areanum, int travelflags );
	void UpdatePortalRoutingCache( aas_routingcache_t *portalCache );
	aas_routingcache_t *GetPortalRoutingCache( int clusternum, int areanum, int travelflags );
	unsigned short AreaTravelTime( int areanum, const vec3_t start, const vec3_t end );

	struct RoutingRequest {
		int areanum;
		int goalareanum;
		int travelflags;

		inline RoutingRequest( int areaNum_, int goalAreaNum_, int travelFlags_ )
			: areanum( areaNum_ ), goalareanum( goalAreaNum_ ), travelflags( travelFlags_ ) {}
	};

	struct RoutingResult {
		int reachnum;
		int traveltime;
	};

	bool RoutingResultToGoalArea( int fromAreaNum, int toAreaNum, int travelFlags, RoutingResult *result ) const;

	bool RouteToGoalArea( const RoutingRequest &request, RoutingResult *result );
	bool RouteToGoalPortal( const RoutingRequest &request, aas_routingcache_t *portalCache, RoutingResult *result );

	int PortalMaxTravelTime( int portalnum );

	void InitDisabledAreasStatusAndHelpers();
	void InitAreaContentsTravelFlags();
	void InitRoutingUpdate();
	void CreateReversedReachability();
	void InitClusterAreaCache();
	void InitPortalCache();
	void CalculateAreaTravelTimes();
	void InitPortalMaxTravelTimes();
	void InitReachabilityAreas();

	void FreeAllClusterAreaCache();
	void FreeAllPortalCache();

	// Should be used only for shared route cache initialization
	AiAasRouteCache( const AiAasWorld &aasWorld_ );
	// Should be used for creation of new instances based on shared one
	AiAasRouteCache( AiAasRouteCache *parent, const int *newTravelFlags );

	static AiAasRouteCache *shared;

public:
	// AiRoutingCache should be init and shutdown explicitly
	// (a game library is not unloaded when a map changes)
	static void Init( const AiAasWorld &aasWorld );
	static void Shutdown();

	static AiAasRouteCache *Shared() { return shared; }
	static AiAasRouteCache *NewInstance( const int *travelFlags_ );
	static void ReleaseInstance( AiAasRouteCache *instance );

	// A helper for emplace_back() calls on instances of this class
	AiAasRouteCache( AiAasRouteCache &&that );
	~AiAasRouteCache();

	inline int ReachabilityToGoalArea( int fromAreaNum, int toAreaNum, int travelFlags ) const {
		RoutingResult result;
		if( RoutingResultToGoalArea( fromAreaNum, toAreaNum, travelFlags, &result ) ) {
			return result.reachnum;
		}
		return 0;
	}

	inline int TravelTimeToGoalArea( int fromAreaNum,int toAreaNum, int travelFlags ) const {
		RoutingResult result;
		if( RoutingResultToGoalArea( fromAreaNum, toAreaNum, travelFlags, &result ) ) {
			return result.traveltime;
		}
		return 0;
	}

	// Finds a reachability/travel time to goal area testing preferred and allowed travel flags for the owner
	// starting from preferred travel flags for the owner and stopping at first feasible result.
	// Returns non-zero travel time on success and a reachability via the out parameter.
	int PreferredRouteToGoalArea( int fromAreaNum, int toAreaNum, int *reachNum ) const;
	// Tests all specified area nums for each flag before moving to the next one.
	int PreferredRouteToGoalArea( const int *fromAreaNums, int numFromAreas, int toAreaNum, int *reachNum ) const;

	// Finds a reachability/travel time to goal area testing preferred and allowed travel flags for the owner
	// starting from preferred travel flags for the owner and choosing a best result.
	// Returns non-zero travel time on success and a reachability via the out parameter.
	int FastestRouteToGoalArea( int fromAreaNum, int toAreaNum, int *reachNum ) const;
	// Returns best results for each combination of from area / travel flags.
	int FastestRouteToGoalArea( const int *fromAreaNums, int numFromAreas, int toAreaNum, int *reachNum ) const;

	// It's better to add separate prototypes than set out pointers to null by default and use branching on every call.
	// We could also set these parameters to an address of some static variable, but it could lead to extra cache misses
	// since all these variables are likely to be scattered in memory.
	// The underlying calls read flags pointer that is very likely on the same cache line the dummyIntPtr is.

	inline int PreferredRouteToGoalArea( int fromAreaNum, int toAreaNum ) const {
		return PreferredRouteToGoalArea( fromAreaNum, toAreaNum, dummyIntPtr );
	}
	inline int PreferredRouteToGoalArea( const int *fromAreaNums, int numFromAreas, int toAreaNum ) const {
		return PreferredRouteToGoalArea( fromAreaNums, numFromAreas, toAreaNum, dummyIntPtr );
	}
	inline int FastestRouteToGoalArea( int fromAreaNum, int toAreaNum ) const {
		return FastestRouteToGoalArea( fromAreaNum, toAreaNum, dummyIntPtr );
	}
	inline int FastestRouteToGoalArea( const int *fromAreaNums, int numFromAreas, int toAreaNum ) const {
		return FastestRouteToGoalArea( fromAreaNums, numFromAreas, toAreaNum, dummyIntPtr );
	}

	inline bool AreaDisabled( int areaNum ) const {
		return areasDisabledStatus[areaNum].CurrStatus() || ( aasWorld.AreaSettings()[areaNum].areaflags & AREA_DISABLED );
	}

	inline bool AreaTemporarilyDisabled( int areaNum ) const {
		return areasDisabledStatus[areaNum].CurrStatus();
	}

	struct DisableZoneRequest {
		virtual int FillRequestedAreasBuffer( int *areasBuffer, int bufferCapacity ) = 0;
	};

	inline void ClearDisabledZones() {
		SetDisabledZones( nullptr, 0 );
	}

	// Pass an array of object references since they are generic non-POD objects having different size/vtbl
	void SetDisabledZones( DisableZoneRequest **requests, int numRequests );
};

#endif
