#include "ai_aas_route_cache.h"
#include "static_vector.h"
#include "ai_local.h"

#undef min
#undef max
#include <stdlib.h>
#include <algorithm>
#include <limits>

// Static member definition
AiAasRouteCache *AiAasRouteCache::shared = nullptr;

void AiAasRouteCache::Init( const AiAasWorld &aasWorld ) {
	if( shared ) {
		AI_FailWith( "AiAasRouteCache::Init()", "The shared instance is already present\n" );
	}
	// AiAasRouteCache is quite large, so it should be allocated on heap
	shared = (AiAasRouteCache *)G_Malloc( sizeof( AiAasRouteCache ) );
	new(shared) AiAasRouteCache( *AiAasWorld::Instance() );
}

void AiAasRouteCache::Shutdown() {
	// This may be called on first map load when an instance has never been instantiated
	if( shared ) {
		shared->~AiAasRouteCache();
		G_Free( shared );
		// Allow the pointer to be reused, otherwise an assertion will fail on a next Init() call
		shared = nullptr;
	}
}

AiAasRouteCache *AiAasRouteCache::NewInstance() {
	return new( G_Malloc( sizeof( AiAasRouteCache ) ) )AiAasRouteCache( Shared() );
}

void AiAasRouteCache::ReleaseInstance( AiAasRouteCache *instance ) {
	if( instance == Shared() ) {
		AI_FailWith( "AiAasRouteCache::ReleaseInstance()", "Attempt to release the shared instance\n" );
	}

	instance->~AiAasRouteCache();
	G_Free( instance );
}

AiAasRouteCache::AiAasRouteCache( const AiAasWorld &aasWorld_ )
	: aasWorld( aasWorld_ ), loaded( false ) {
	InitDisabledAreasStatusAndHelpers();
	//
	InitTravelFlagFromType();
	//
	InitAreaContentsTravelFlags();
	//initialize the routing update fields
	InitRoutingUpdate();
	//create reversed reachability links used by the routing update algorithm
	CreateReversedReachability();
	//initialize the cluster cache
	InitClusterAreaCache();
	//initialize portal cache
	InitPortalCache();
	//initialize the area travel times
	CalculateAreaTravelTimes();
	//calculate the maximum travel times through portals
	InitPortalMaxTravelTimes();
	//get the areas reachabilities go through
	InitReachabilityAreas();
	loaded = true;
}

AiAasRouteCache::AiAasRouteCache( AiAasRouteCache &&that )
	: aasWorld( that.aasWorld ), loaded( true ) {
	currDisabledAreaNums = that.currDisabledAreaNums;
	cleanCacheAreaNums = that.cleanCacheAreaNums;
	areasDisabledStatus = that.areasDisabledStatus;

	memcpy( travelflagfortype, that.travelflagfortype, sizeof( travelflagfortype ) );

	areacontentstravelflags = that.areacontentstravelflags;

	areaupdate = that.areaupdate;
	portalupdate = that.portalupdate;
	dijkstralabels = that.dijkstralabels;

	reversedreachability = that.reversedreachability;

	clusterareacache = that.clusterareacache;
	portalcache = that.portalcache;

	oldestcache = that.oldestcache;
	newestcache = that.newestcache;

	areatraveltimes = that.areatraveltimes;
	portalmaxtraveltimes = that.portalmaxtraveltimes;

	reachabilityareas = that.reachabilityareas;
	reachabilityareaindex = that.reachabilityareaindex;

	maxreachabilityareas = that.maxreachabilityareas;

	that.loaded = false;
}

AiAasRouteCache::AiAasRouteCache( AiAasRouteCache *parent )
	: aasWorld( parent->aasWorld ), loaded( true ) {
	InitDisabledAreasStatusAndHelpers();

	memcpy( travelflagfortype, parent->travelflagfortype, sizeof( travelflagfortype ) );

	areacontentstravelflags = parent->areacontentstravelflags;
	AddRef( areacontentstravelflags );

	InitRoutingUpdate();

	reversedreachability = parent->reversedreachability;
	AddRef( reversedreachability );

	InitClusterAreaCache();
	InitPortalCache();

	areatraveltimes = parent->areatraveltimes;
	AddRef( areatraveltimes );
	portalmaxtraveltimes = parent->portalmaxtraveltimes;
	AddRef( portalmaxtraveltimes );

	reachabilityareas = parent->reachabilityareas;
	AddRef( reachabilityareas );
	reachabilityareaindex = parent->reachabilityareaindex;
	AddRef( reachabilityareaindex );
}

AiAasRouteCache::~AiAasRouteCache() {
	if( !loaded ) {
		return;
	}

	// free all the existing cluster area cache
	FreeAllClusterAreaCache();
	// free all the existing portal cache
	FreeAllPortalCache();
	// free cached travel times within areas
	FreeRefCountedMemory( areatraveltimes );
	// free cached maximum travel time through cluster portals
	FreeRefCountedMemory( portalmaxtraveltimes );
	// free reversed reachability links
	FreeRefCountedMemory( reversedreachability );
	// free routing algorithm memory
	FreeMemory( areaupdate );
	FreeMemory( portalupdate );
	FreeMemory( dijkstralabels );
	// free lists with areas the reachabilities go through
	FreeRefCountedMemory( reachabilityareas );
	// free the reachability area index
	FreeRefCountedMemory( reachabilityareaindex );
	// free area contents travel flags look up table
	FreeRefCountedMemory( areacontentstravelflags );
	// other arrays related to disabled areas are allocated in this buffer too
	FreeMemory( currDisabledAreaNums );
	// free cached memory chunk pools
	FreeAreaAndPortalMemoryPools();
}

inline int AiAasRouteCache::ClusterAreaNum( int cluster, int areanum ) {
	int areacluster = aasWorld.AreaSettings()[areanum].cluster;
	if( areacluster > 0 ) {
		return aasWorld.AreaSettings()[areanum].clusterareanum;
	}

	int side = aasWorld.Portals()[-areacluster].frontcluster != cluster;
	return aasWorld.Portals()[-areacluster].clusterareanum[side];
}

void AiAasRouteCache::InitTravelFlagFromType() {
	for( int i = 0; i < MAX_TRAVELTYPES; i++ ) {
		travelflagfortype[i] = TFL_INVALID;
	}
	travelflagfortype[TRAVEL_INVALID] = TFL_INVALID;
	travelflagfortype[TRAVEL_WALK] = TFL_WALK;
	travelflagfortype[TRAVEL_CROUCH] = TFL_CROUCH;
	travelflagfortype[TRAVEL_BARRIERJUMP] = TFL_BARRIERJUMP;
	travelflagfortype[TRAVEL_JUMP] = TFL_JUMP;
	travelflagfortype[TRAVEL_LADDER] = TFL_LADDER;
	travelflagfortype[TRAVEL_WALKOFFLEDGE] = TFL_WALKOFFLEDGE;
	travelflagfortype[TRAVEL_SWIM] = TFL_SWIM;
	travelflagfortype[TRAVEL_WATERJUMP] = TFL_WATERJUMP;
	travelflagfortype[TRAVEL_TELEPORT] = TFL_TELEPORT;
	travelflagfortype[TRAVEL_ELEVATOR] = TFL_ELEVATOR;
	travelflagfortype[TRAVEL_ROCKETJUMP] = TFL_ROCKETJUMP;
	travelflagfortype[TRAVEL_BFGJUMP] = TFL_BFGJUMP;
	travelflagfortype[TRAVEL_GRAPPLEHOOK] = TFL_GRAPPLEHOOK;
	travelflagfortype[TRAVEL_DOUBLEJUMP] = TFL_DOUBLEJUMP;
	travelflagfortype[TRAVEL_RAMPJUMP] = TFL_RAMPJUMP;
	travelflagfortype[TRAVEL_STRAFEJUMP] = TFL_STRAFEJUMP;
	travelflagfortype[TRAVEL_JUMPPAD] = TFL_JUMPPAD;
	travelflagfortype[TRAVEL_FUNCBOB] = TFL_FUNCBOB;
}

void AiAasRouteCache::UnlinkCache( aas_routingcache_t *cache ) {
	if( cache->time_next ) {
		cache->time_next->time_prev = cache->time_prev;
	} else {
		newestcache = cache->time_prev;
	}
	if( cache->time_prev ) {
		cache->time_prev->time_next = cache->time_next;
	} else {
		oldestcache = cache->time_next;
	}
	cache->time_next = nullptr;
	cache->time_prev = nullptr;
}

void AiAasRouteCache::LinkCache( aas_routingcache_t *cache ) {
	if( newestcache ) {
		newestcache->time_next = cache;
		cache->time_prev = newestcache;
	} else {
		oldestcache = cache;
		cache->time_prev = nullptr;
	}
	cache->time_next = nullptr;
	newestcache = cache;
}

void AiAasRouteCache::FreeRoutingCache( aas_routingcache_t *cache ) {
	UnlinkCache( cache );
	FreeAreaAndPortalCacheMemory( cache );
}

void AiAasRouteCache::RemoveRoutingCacheInClusterForArea( int areaNum ) {
	// TODO: aasWorld ref chasing is not cache-friendly
	int clusterNum = aasWorld.AreaSettings()[areaNum].cluster;
	if( clusterNum > 0 ) {
		//remove all the cache in the cluster the area is in
		RemoveRoutingCacheInCluster( clusterNum );
	} else {
		// if this is a portal remove all cache in both the front and back cluster
		RemoveRoutingCacheInCluster( aasWorld.Portals()[-clusterNum].frontcluster );
		RemoveRoutingCacheInCluster( aasWorld.Portals()[-clusterNum].backcluster );
	}
}

void AiAasRouteCache::RemoveRoutingCacheInCluster( int clusternum ) {
	if( !clusterareacache ) {
		return;
	}

	const aas_cluster_t *cluster = &aasWorld.Clusters()[clusternum];
	for( int i = 0; i < cluster->numareas; i++ ) {
		aas_routingcache_t *cache, *nextcache;
		for( cache = clusterareacache[clusternum][i]; cache; cache = nextcache ) {
			nextcache = cache->next;
			FreeRoutingCache( cache );
		}
		clusterareacache[clusternum][i] = nullptr;
	}
}

void AiAasRouteCache::RemoveAllPortalsCache() {
	for( int i = 0, end = aasWorld.NumAreas(); i < end; i++ ) {
		aas_routingcache_t *cache, *nextcache;
		for( cache = portalcache[i]; cache; cache = nextcache ) {
			nextcache = cache->next;
			FreeRoutingCache( cache );
		}
		portalcache[i] = nullptr;
	}
}

void AiAasRouteCache::SetDisabledZones( DisableZoneRequest **requests, int numRequests ) {
	// Copy the reference to a local var for faster access
	AreaDisabledStatus *areasDisabledStatus = this->areasDisabledStatus;

	// First, save old area statuses and set new ones as non-blocked
	for( int i = 0, end = aasWorld.NumAreas(); i < end; ++i ) {
		areasDisabledStatus[i].ShiftCurrToOldStatus();
	}

	// Select all disabled area nums
	int numDisabledAreas = 0;
	int capacityLeft = aasWorld.NumAreas();
	for( int i = 0; i < numRequests; ++i ) {
		int numAreas = requests[i]->FillRequestedAreasBuffer( currDisabledAreaNums + numDisabledAreas, capacityLeft );
		numDisabledAreas += numAreas;
		capacityLeft -= numAreas;
	}

	// For each selected area mark area as disabled
	for( int i = 0; i < numDisabledAreas; ++i ) {
		areasDisabledStatus[currDisabledAreaNums[i]].SetCurrStatus( true );
	}

	// For each area compare its old and new status
	int totalClearCacheAreas = 0;
	for( int i = 0, end = aasWorld.NumAreas(); i < end; ++i ) {
		const auto &status = areasDisabledStatus[i];
		if( status.OldStatus() != status.CurrStatus() ) {
			cleanCacheAreaNums[totalClearCacheAreas++] = i;
		}
	}

	if( totalClearCacheAreas ) {
		for( int i = 0; i < totalClearCacheAreas; ++i ) {
			RemoveRoutingCacheInClusterForArea( cleanCacheAreaNums[i] );
		}
		RemoveAllPortalsCache();
	}

	resultCache.Clear();
}

int AiAasRouteCache::GetAreaContentsTravelFlags( int areanum ) {
	int contents = aasWorld.AreaSettings()[areanum].contents;
	int tfl = 0;
	if( contents & AREACONTENTS_WATER ) {
		tfl |= TFL_WATER;
	} else if( contents & AREACONTENTS_SLIME ) {
		tfl |= TFL_SLIME;
	} else if( contents & AREACONTENTS_LAVA ) {
		tfl |= TFL_LAVA;
	} else {
		tfl |= TFL_AIR;
	}
	if( contents & AREACONTENTS_DONOTENTER ) {
		tfl |= TFL_DONOTENTER;
	}
	if( contents & AREACONTENTS_NOTTEAM1 ) {
		tfl |= TFL_NOTTEAM1;
	}
	if( contents & AREACONTENTS_NOTTEAM2 ) {
		tfl |= TFL_NOTTEAM2;
	}
	if( aasWorld.AreaSettings()[areanum].areaflags & AREA_BRIDGE ) {
		tfl |= TFL_BRIDGE;
	}
	return tfl;
}

void AiAasRouteCache::InitDisabledAreasStatusAndHelpers() {
	static_assert( sizeof( AreaDisabledStatus ) == 1, "" );
	static_assert( alignof( AreaDisabledStatus ) == 1, "" );
	int size = aasWorld.NumAreas() * ( 2 * sizeof( int ) + sizeof( AreaDisabledStatus ) );
	char *ptr = (char *)GetClearedMemory( size );
	currDisabledAreaNums = (int *)ptr;
	cleanCacheAreaNums = ( (int *)ptr ) + aasWorld.NumAreas();
	areasDisabledStatus = (AreaDisabledStatus *)( ( (int *)ptr ) + aasWorld.NumAreas() );
}

void AiAasRouteCache::InitAreaContentsTravelFlags( void ) {
	areacontentstravelflags = (int *) GetClearedRefCountedMemory( aasWorld.NumAreas() * sizeof( int ) );
	for( int i = 0; i < aasWorld.NumAreas(); i++ ) {
		areacontentstravelflags[i] = GetAreaContentsTravelFlags( i );
	}
}

void AiAasRouteCache::CreateReversedReachability( void ) {
	//allocate memory for the reversed reachability links
	char *ptr = (char *) GetClearedRefCountedMemory( aasWorld.NumAreas() * sizeof( aas_reversedreachability_t ) +
													 aasWorld.NumReachabilities() * sizeof( aas_reversedlink_t ) );
	//
	reversedreachability = (aas_reversedreachability_t *) ptr;
	//pointer to the memory for the reversed links
	ptr += aasWorld.NumAreas() * sizeof( aas_reversedreachability_t );
	//check all reachabilities of all areas
	for( int i = 1; i < aasWorld.NumAreas(); i++ ) {
		//settings of the area
		const aas_areasettings_t *settings = &aasWorld.AreaSettings()[i];
		//
		if( settings->numreachableareas >= 128 ) {
			G_Printf( S_COLOR_YELLOW "area %d has more than 128 reachabilities\n", i );
		}
		//create reversed links for the reachabilities
		for( int n = 0; n < settings->numreachableareas && n < 128; n++ ) {
			//reachability link
			const aas_reachability_t *reach = &aasWorld.Reachabilities()[settings->firstreachablearea + n];
			//
			aas_reversedlink_t *revlink = (aas_reversedlink_t *) ptr;
			ptr += sizeof( aas_reversedlink_t );
			//
			revlink->areanum = i;
			revlink->linknum = settings->firstreachablearea + n;
			revlink->next = reversedreachability[reach->areanum].first;
			reversedreachability[reach->areanum].first = revlink;
			reversedreachability[reach->areanum].numlinks++;
		}
	}
}

//travel time in hundreths of a second = distance * 100 / speed
constexpr auto DISTANCEFACTOR_CROUCH = 1.3f; //crouch speed = 100
constexpr auto DISTANCEFACTOR_SWIM = 1.0f;   //should be 0.66, swim speed = 150
constexpr auto DISTANCEFACTOR_WALK = 0.16f;  //Qfusion: corrected for real bot traveling speed

unsigned short AiAasRouteCache::AreaTravelTime( int areanum, const vec3_t start, const vec3_t end ) {
	vec3_t dir;
	VectorSubtract( start, end, dir );
	float dist = VectorLength( dir );
	//if crouch only area
	if( aasWorld.AreaCrouch( areanum ) ) {
		dist *= DISTANCEFACTOR_CROUCH;
	} else if( aasWorld.AreaSwim( areanum ) ) {
		dist *= DISTANCEFACTOR_SWIM;
	} else {
		dist *= DISTANCEFACTOR_WALK;
	}
	//
	int intdist = (int) dist;
	//make sure the distance isn't zero
	if( intdist <= 0 ) {
		intdist = 1;
	}
	return intdist;
}

#define PAD( base, alignment ) ( ( ( base ) + ( alignment ) - 1 ) & ~( ( alignment ) - 1 ) )

void AiAasRouteCache::CalculateAreaTravelTimes( void ) {
	//get the total size of all the area travel times
	int size = aasWorld.NumAreas() * sizeof( unsigned short ** );
	for( int i = 0; i < aasWorld.NumAreas(); i++ ) {
		aas_reversedreachability_t *revreach = &reversedreachability[i];
		//settings of the area
		const aas_areasettings_t *settings = &aasWorld.AreaSettings()[i];
		//
		size += settings->numreachableareas * sizeof( unsigned short * );
		//
		size += settings->numreachableareas *  PAD( revreach->numlinks, sizeof( long ) ) * sizeof( unsigned short );
	}
	//allocate memory for the area travel times
	char *ptr = (char *) GetClearedRefCountedMemory( size );
	areatraveltimes = (unsigned short ***) ptr;
	ptr += aasWorld.NumAreas() * sizeof( unsigned short ** );
	//calcluate the travel times for all the areas
	for( int i = 0; i < aasWorld.NumAreas(); i++ ) {
		//reversed reachabilities of this area
		aas_reversedreachability_s *revreach = &reversedreachability[i];
		//settings of the area
		const aas_areasettings_t *settings = &aasWorld.AreaSettings()[i];
		//
		areatraveltimes[i] = (unsigned short **) ptr;
		ptr += settings->numreachableareas * sizeof( unsigned short * );
		//
		for( int l = 0; l < settings->numreachableareas; l++ ) {
			areatraveltimes[i][l] = (unsigned short *) ptr;
			ptr += PAD( revreach->numlinks, sizeof( long ) ) * sizeof( unsigned short );
			//reachability link
			const aas_reachability_t *reach = &aasWorld.Reachabilities()[settings->firstreachablearea + l];
			//
			int n = 0;
			aas_reversedlink_t *revlink = revreach->first;
			for(; revlink; revlink = revlink->next, n++ ) {
				vec3_t end;
				VectorCopy( aasWorld.Reachabilities()[revlink->linknum].end, end );
				//
				areatraveltimes[i][l][n] = AreaTravelTime( i, end, reach->start );
			}
		}
	}
}

int AiAasRouteCache::PortalMaxTravelTime( int portalnum ) {
	const aas_portal_t *portal = &aasWorld.Portals()[portalnum];
	//reversed reachabilities of this portal area
	const aas_reversedreachability_t *revreach = &reversedreachability[portal->areanum];
	//settings of the portal area
	const aas_areasettings_t *settings = &aasWorld.AreaSettings()[portal->areanum];
	//
	int maxt = 0;
	for( int l = 0; l < settings->numreachableareas; l++ ) {
		int n = 0;
		aas_reversedlink_t *revlink = revreach->first;
		for(; revlink; revlink = revlink->next, n++ ) {
			int t = areatraveltimes[portal->areanum][l][n];
			if( t > maxt ) {
				maxt = t;
			}
		}
	}
	return maxt;
}

void AiAasRouteCache::InitPortalMaxTravelTimes( void ) {
	portalmaxtraveltimes = (int *) GetClearedRefCountedMemory( aasWorld.NumPortals() * sizeof( int ) );
	for( int i = 0; i < aasWorld.NumPortals(); i++ ) {
		portalmaxtraveltimes[i] = PortalMaxTravelTime( i );
	}
}

class FreelistPool
{
public:
	struct BlockHeader {
		BlockHeader *prev;
		BlockHeader *next;
	};

private:
	// Freelist head
	BlockHeader headBlock;
	// Freelist free item
	BlockHeader *freeBlock;
	// Actual blocks data
	uint8_t *buffer;

	// An actual blocks data size and a maximal count of blocks.
	const size_t blockSize, maxBlocks;
	size_t blocksInUse;

public:
	FreelistPool( void *buffer_, size_t bufferSize, size_t blockSize_ );
	virtual ~FreelistPool() {}

	void *Alloc( int size );
	void Free( void *ptr );

	inline bool MayOwn( const void *ptr ) const {
		const uint8_t *comparablePtr = (const uint8_t *)ptr;
		const uint8_t *bufferBoundary = buffer + ( blockSize + sizeof( BlockHeader ) ) * ( maxBlocks );
		return buffer <= comparablePtr && comparablePtr <= bufferBoundary;
	}

	inline size_t Size() const { return blocksInUse; }
	inline size_t Capacity() const { return maxBlocks; }
};

class AreaAndPortalCacheBin
{
	FreelistPool *freelistPool;

	void *usedSingleBlock;
	void *freeSingleBlock;

	unsigned numBlocks;
	unsigned blockSize;

	// Sets the reference in the chunk to its owner so the block can destruct self
	inline void *SetSelfAsTag( void *block ) {
		// Check the block alignment
		assert( !( (uintptr_t)block % 8 ) );
		// We have to always return 8-byte aligned blocks to follow the malloc contract
		// regardless of address size, so 8 bytes are wasted anyway
		uint64_t *u = (uint64_t *)block;
		u[0] = (uintptr_t)this;
		return u + 1;
	}

	// Don't call directly, use FreeTaggedBlock() instead.
	// A real raw block pointer is expected (that is legal to be fed to G_Free),
	// and Alloc() returns pointers shifted by 8 (a tag is prepended)
	void Free( void *ptr );
public:
	AreaAndPortalCacheBin *next;

	AreaAndPortalCacheBin( unsigned chunkSize_, unsigned numChunks_ )
		: freelistPool( nullptr ),
		  usedSingleBlock( nullptr ),
		  freeSingleBlock( nullptr ),
		  numBlocks( numChunks_ ),
		  blockSize( chunkSize_ ),
		  next( nullptr ) {}

	~AreaAndPortalCacheBin() {
		if( usedSingleBlock ) {
			G_Free( usedSingleBlock );
		}
		if( freeSingleBlock ) {
			G_Free( freeSingleBlock );
		}
		if( freelistPool ) {
			freelistPool->~FreelistPool();
			G_Free( freelistPool );
		}
	}

	void *Alloc( int size );

	bool FitsSize( int size ) const {
		// Use a strict comparison and do not try to reuse a bin for blocks of different size.
		// There are usually very few size values.
		// Moreover bins for small sizes are addressed by the size.
		// If we try reusing the same bin for different sizes, the cache gets evicted way too often.
		// (these small bins usually correspond to cluster cache).
		return size == (int)blockSize;
	}

	bool NeedsCleanup() const {
		if( !freelistPool ) {
			return false;
		}
		// Raising the threshold leads to pool exhaustion on some maps
		return freelistPool->Size() / (float)freelistPool->Capacity() > 0.66f;
	}

	static void FreeTaggedBlock( void *ptr ) {
		// Check alignment of the supplied pointer
		assert( !( (uintptr_t)ptr % 8 ) );
		// Real block starts 8 bytes below
		uint64_t *realBlock = ( (uint64_t *)ptr ) - 1;
		// Extract the bin stored as a tag
		auto *bin = (AreaAndPortalCacheBin *)( (uintptr_t)realBlock[0] );
		// Free the real block registered by the bin
		bin->Free( realBlock );
	}
};

FreelistPool::FreelistPool( void *buffer_, size_t bufferSize, size_t blockSize_ )
	: buffer( (uint8_t *)buffer_ ),
	blockSize( blockSize_ ),
	maxBlocks( bufferSize / ( blockSize_ + sizeof( BlockHeader ) ) ),
	blocksInUse( 0 ) {
#ifndef PUBLIC_BUILD
	if( ( (uintptr_t)buffer ) & 7 ) {
		AI_FailWith( "FreelistPool::FreelistPool()", "Illegal buffer pointer (should be at least 8-byte-aligned)\n" );
	}
#endif

	freeBlock = nullptr;
	if( maxBlocks ) {
		// We can't use array access on BlockHeader * pointer since real chunk size is not a sizeof(Chunk).
		// Next chunk has this offset in bytes from previous one:
		size_t stride = blockSize + sizeof( BlockHeader );
		uint8_t *nextBlockPtr = this->buffer + stride;
		auto *currBlock = (BlockHeader *)this->buffer;
		freeBlock = currBlock;
		for( unsigned i = 0; i < maxBlocks - 1; ++i ) {
			auto *nextChunk = (BlockHeader *)( nextBlockPtr );
			currBlock->prev = nullptr;
			currBlock->next = nextChunk;
			currBlock = nextChunk;
			nextBlockPtr += stride;
		}
		currBlock->prev = nullptr;
		currBlock->next = nullptr;
	}
	headBlock.next = &headBlock;
	headBlock.prev = &headBlock;
}

void *FreelistPool::Alloc( int size ) {
#ifndef PUBLIC_BUILD
	if( (unsigned)size > blockSize ) {
		constexpr const char *format = "Attempt to allocate more bytes %d than the block size %d\n";
		AI_FailWith( "FreelistPool::Alloc()", format, size, (int)blockSize );
	}

	if( !freeBlock ) {
		AI_FailWith( "FreelistPool::Alloc()", "There are no free blocks left\n" );
	}
#endif

	BlockHeader *block = freeBlock;
	freeBlock = block->next;

	block->prev = &headBlock;
	block->next = headBlock.next;
	block->next->prev = block;
	block->prev->next = block;

	++blocksInUse;
	// Return a pointer to a datum after the chunk header
	return block + 1;
}

void FreelistPool::Free( void *ptr ) {
	BlockHeader *block = ( (BlockHeader *)ptr ) - 1;
	if( block->prev ) {
		block->prev->next = block->next;
	}
	if( block->next ) {
		block->next->prev = block->prev;
	}
	block->next = freeBlock;
	freeBlock = block;
	--blocksInUse;
}

void *AreaAndPortalCacheBin::Alloc( int size ) {
#ifndef PUBLIC_BUILD
	if( size > (int)blockSize ) {
		const char *message = "Don't call Alloc() if the cache is a-priory incapable of allocating chunk of specified size";
		AI_FailWith("AreaAndPortalCacheBin::Alloc()", "%s", message );
	}
#endif
	constexpr auto TAG_SIZE = 8;

	// Once the freelist pool for many chunks has been initialized, it handles all allocation requests.
	if( freelistPool ) {
		// Allocate 8 extra bytes for the tag
		if( freelistPool->Size() != freelistPool->Capacity() ) {
			return SetSelfAsTag( freelistPool->Alloc( size + TAG_SIZE ) );
		} else {
			// The pool capacity has been exhausted. Fall back to using G_Malloc()
			// This is not a desired behavior but we should not crash in these extreme cases.
			return SetSelfAsTag( G_Malloc( (size_t)( size + TAG_SIZE ) ) );
		}
	}

	// Check if there is a free unpooled block saved for further allocations.
	// Check then whether no unpooled blocks were used at all, and allocate a new one.
	if( freeSingleBlock ) {
		assert( !usedSingleBlock );
		usedSingleBlock = freeSingleBlock;
		freeSingleBlock = nullptr;
		return SetSelfAsTag( usedSingleBlock );
	} else if( !usedSingleBlock ) {
		// Allocate 8 extra bytes for the tag
		usedSingleBlock = G_Malloc( (size_t)( size + TAG_SIZE ) );
		return SetSelfAsTag( usedSingleBlock );
	}

	// Too many blocks is going to be used.
	// Allocate a freelist pool in a single continuous memory block, and use it for further allocations.

	// Each block needs some space for the header and 8 extra bytes for the block tag
	size_t bufferSize = ( blockSize + sizeof( FreelistPool::BlockHeader ) + TAG_SIZE ) * numBlocks;
	size_t alignmentBytes = 0;
	if( sizeof( FreelistPool ) % 8 ) {
		alignmentBytes = 8 - sizeof( FreelistPool ) % 8;
	}

	size_t memSize = sizeof( FreelistPool ) + alignmentBytes + bufferSize;
	uint8_t *memBlock = (uint8_t *)G_Malloc( memSize );
	memset( memBlock, 0, memSize );
	uint8_t *poolBuffer = memBlock + sizeof( FreelistPool ) + alignmentBytes;
	// Note: It is important to tell the pool about the extra space occupied by block tags
	freelistPool = new( memBlock )FreelistPool( poolBuffer, bufferSize, blockSize + TAG_SIZE );
	return SetSelfAsTag( freelistPool->Alloc( size + TAG_SIZE ) );
}

void AreaAndPortalCacheBin::Free( void *ptr ) {
	if( ptr != usedSingleBlock ) {
		// Check whether it has been allocated by the freelist pool
		// or has been allocated via G_Malloc() if the pool capacity has been exceeded.
		if( freelistPool->MayOwn( ptr ) ) {
			freelistPool->Free( ptr );
		} else {
			G_Free( ptr );
		}
		return;
	}

	assert( !freeSingleBlock );
	// If the freelist pool is not allocated yet at the moment of this Free() call,
	// its likely there is no further necessity in the pool.
	// Save the single block for further use.
	if( !freelistPool ) {
		freeSingleBlock = usedSingleBlock;
	} else {
		// Free the single block, as further allocations are handled by the freelist pool.
		G_Free( usedSingleBlock );
	}

	usedSingleBlock = nullptr;
}

void AiAasRouteCache::ResultCache::Clear() {
	nodes[0].ListLinks().prev = NULL_LINK;
	nodes[0].ListLinks().next = 1;

	for( unsigned i = 1; i < MAX_CACHED_RESULTS - 1; ++i ) {
		nodes[i].ListLinks().prev = i - 1;
		nodes[i].ListLinks().next = i + 1;
	}

	nodes[MAX_CACHED_RESULTS - 1].ListLinks().prev = MAX_CACHED_RESULTS - 2;
	nodes[MAX_CACHED_RESULTS - 1].ListLinks().next = NULL_LINK;

	freeNode = 0;
	newestUsedNode = NULL_LINK;
	oldestUsedNode = NULL_LINK;

	std::fill_n( bins, NUM_HASH_BINS, NULL_LINK );
}

inline void AiAasRouteCache::ResultCache::LinkToHashBin( uint16_t binIndex, Node *node ) {
	node->binIndex = binIndex;
	// Link the result node to its hash bin
	if( IsValidLink( bins[binIndex] ) ) {
		node->BinLinks().next = bins[binIndex];
		Node *oldBinHead = nodes + bins[binIndex];
		oldBinHead->BinLinks().prev = LinkOf( node );
	}
	node->BinLinks().next = NULL_LINK;
	bins[binIndex] = LinkOf( node );
}

inline void AiAasRouteCache::ResultCache::LinkToUsedList( Node *node ) {
	// Newest used nodes are always linked to the `next` link.
	// Thus, newest used node must always have a negative `next` link
	if( IsValidLink( newestUsedNode ) ) {
		Node *oldUsedHead = nodes + newestUsedNode;
#ifndef _DEBUG
		if( oldUsedHead->ListLinks().HasNext() ) {
			AI_FailWith( "AiAasRouteCache::ResultCache::LinkToUsedList()", "newest used node has a next link" );
		}
#endif

		oldUsedHead->ListLinks().next = LinkOf( node );
		node->ListLinks().prev = newestUsedNode;
	} else {
		node->ListLinks().prev = NULL_LINK;
	}

	newestUsedNode = LinkOf( node );
	node->ListLinks().next = NULL_LINK;

	// If there is no oldestUsedNode, set the node as it.
	if( oldestUsedNode < 0 ) {
		oldestUsedNode = LinkOf( node );
	}
}

inline void AiAasRouteCache::ResultCache::UnlinkOldestUsedNodeFromBin() {
	// Unlink last used node from bin
	Node *oldestNode = nodes + oldestUsedNode;
	if( oldestNode->BinLinks().HasNext() ) {
		Node *nextNode = nodes + oldestNode->BinLinks().next;
		nextNode->ListLinks().prev = oldestNode->BinLinks().prev;
	}

	if( oldestNode->BinLinks().HasPrev() ) {
		Node *prevNode = nodes + oldestNode->BinLinks().prev;
		prevNode->BinLinks().next = oldestNode->BinLinks().next;
		return;
	}

#ifndef _DEBUG
	// If node.prevInBin is null, the node must be a bin head
	if( bins[oldestNode->binIndex] != oldestUsedNode ) {
		AI_FailWith( "AiAasRouteCache::ResultCache::UnlinkOldestUsedNodeFromBin()", "The node is not a bin head" );
	}
#endif
	bins[oldestNode->binIndex] = oldestNode->BinLinks().next;
}

inline void AiAasRouteCache::ResultCache::UnlinkOldestUsedNodeFromList() {
#ifndef _DEBUG
	// Oldest used node must always have a negative `prev` link
	if( nodes[oldestUsedNode].ListLinks().HasPrev() ) {
		AI_FailWith( "AiAasRouteCache::ResultCache::UnlinkOldestUsedNodeFromBin()", "Oldest used node has a prev link" );
	}
#endif

	if( nodes[oldestUsedNode].ListLinks().HasNext() ) {
		oldestUsedNode = nodes[oldestUsedNode].ListLinks().next;
		nodes[oldestUsedNode].ListLinks().prev = NULL_LINK;
	} else {
		oldestUsedNode = NULL_LINK;
	}
}

inline AiAasRouteCache::ResultCache::Node *AiAasRouteCache::ResultCache::UnlinkOldestUsedNode() {
	Node *result = nodes + oldestUsedNode;
	UnlinkOldestUsedNodeFromBin();
	UnlinkOldestUsedNodeFromList();
	return result;
}

const AiAasRouteCache::ResultCache::Node *
AiAasRouteCache::ResultCache::GetCachedResultForKey( uint16_t binIndex, uint64_t key ) const {
	int16_t nodeIndex = bins[binIndex];
	while( nodeIndex >= 0 ) {
		const Node *node = nodes + nodeIndex;
		if( node->key == key ) {
			return node;
		}
		nodeIndex = node->BinLinks().next;
	}

	return nullptr;
}

AiAasRouteCache::ResultCache::Node *
AiAasRouteCache::ResultCache::AllocAndRegisterForKey( uint16_t binIndex, uint64_t key ) {
	Node *result;
	if( freeNode >= 0 ) {
		// Unlink the node from free list
		result = nodes + freeNode;
		freeNode = result->ListLinks().next;
		LinkToHashBin( binIndex, result );
		LinkToUsedList( result );
	} else {
		result = UnlinkOldestUsedNode();
		LinkToHashBin( binIndex, result );
		LinkToUsedList( result );
	}

	result->key = key;
	return result;
}

void *AiAasRouteCache::GetClearedMemory( int size ) {
	return G_Malloc( size );
}

void AiAasRouteCache::FreeMemory( void *ptr ) {
	G_Free( ptr );
}

void *AiAasRouteCache::AllocAreaAndPortalCacheMemory( int size ) {
	// Lowering this value leads to pool exhaustion on some maps
	constexpr auto NUM_CHUNKS = 512;

	// Check whether the corresponding bin chunk size is small enough
	// to allow the bin to be addressed directly by size.
	if( size < sizeof( areaAndPortalCacheTable ) / sizeof( *areaAndPortalCacheTable ) ) {
		if( auto *bin = areaAndPortalCacheTable[size] ) {
			assert( bin->FitsSize( size ) );
			if( bin->NeedsCleanup() ) {
				FreeOldestCache();
			}
			return bin->Alloc( size );
		}

		// Create a new bin for the size
		void *mem = G_Malloc( sizeof( AreaAndPortalCacheBin ) );
		memset( mem, 0, sizeof( AreaAndPortalCacheBin ) );
		auto *newBin = new( mem )AreaAndPortalCacheBin( (unsigned)size, NUM_CHUNKS );
		areaAndPortalCacheTable[size] = newBin;
		return newBin->Alloc( size );
	}

	// Check whether there are bins able to handle the request in the common bins list
	for( AreaAndPortalCacheBin *bin = areaAndPortalCacheHead; bin; bin = bin->next ) {
		if( bin->FitsSize( size ) ) {
			if( bin->NeedsCleanup() ) {
				FreeOldestCache();
			}
			return bin->Alloc( size );
		}
	}

	// Create a new bin for the size
	void *mem = G_Malloc( sizeof( AreaAndPortalCacheBin ) );
	memset( mem, 0, sizeof( AreaAndPortalCacheBin ) );
	auto *newBin = new( mem )AreaAndPortalCacheBin( (unsigned)size, NUM_CHUNKS );

	// Link it to the bins list head
	newBin->next = areaAndPortalCacheHead;
	areaAndPortalCacheHead = newBin;

	return newBin->Alloc( size );
}

void AiAasRouteCache::FreeAreaAndPortalCacheMemory( void *ptr ) {
	// The chunk stores its owner as a tag
	AreaAndPortalCacheBin::FreeTaggedBlock( ptr );
}

void AiAasRouteCache::FreeAreaAndPortalMemoryPools() {
	auto *bin = areaAndPortalCacheHead;
	while( bin ) {
		// Don't trigger "use after free"
		auto *nextBin = bin->next;
		G_Free( bin );
		bin = nextBin;
	}

	int binsTableCapacity = sizeof( areaAndPortalCacheTable ) / sizeof( areaAndPortalCacheTable[0] );
	for( int i = 0; i < binsTableCapacity; ++i ) {
		if( areaAndPortalCacheTable[i] ) {
			G_Free( areaAndPortalCacheTable[i] );
		}
	}
}

bool AiAasRouteCache::FreeOldestCache() {
	for( aas_routingcache_t *cache = oldestcache; cache; cache = cache->time_next ) {
		// never free area cache leading towards a portal
		if( cache->type == CACHETYPE_AREA && aasWorld.AreaSettings()[cache->areanum].cluster < 0 ) {
			continue;
		}

		UnlinkAndFreeRoutingCache( cache );
		return true;
	}

	return false;
}

void AiAasRouteCache::UnlinkAndFreeRoutingCache( aas_routingcache_t *cache ) {
	if( cache->type == CACHETYPE_AREA ) {
		UnlinkAreaRoutingCache( cache );
	} else {
		UnlinkPortalRoutingCache( cache );
	}

	FreeRoutingCache( cache );
}

void AiAasRouteCache::UnlinkAreaRoutingCache( aas_routingcache_t *cache ) {
	//number of the area in the cluster
	int clusterareanum = ClusterAreaNum( cache->cluster, cache->areanum );
	// unlink from cluster area cache
	if( cache->prev ) {
		cache->prev->next = cache->next;
	} else {
		clusterareacache[cache->cluster][clusterareanum] = cache->next;
	}
	if( cache->next ) {
		cache->next->prev = cache->prev;
	}
}

void AiAasRouteCache::UnlinkPortalRoutingCache( aas_routingcache_t *cache ) {
	// unlink from portal cache
	if( cache->prev ) {
		cache->prev->next = cache->next;
	} else {
		portalcache[cache->areanum] = cache->next;
	}
	if( cache->next ) {
		cache->next->prev = cache->prev;
	}
}

AiAasRouteCache::aas_routingcache_t *AiAasRouteCache::AllocRoutingCache( int numtraveltimes ) {
	int size = sizeof( aas_routingcache_t )
			   + numtraveltimes * sizeof( unsigned short int )
			   + numtraveltimes * sizeof( unsigned char );

	aas_routingcache_t *cache = (aas_routingcache_t *)AllocAreaAndPortalCacheMemory( size );
	cache->reachabilities = (unsigned char *) cache + sizeof( aas_routingcache_t )
							+ numtraveltimes * sizeof( unsigned short int );
	cache->size = size;
	return cache;
}

void AiAasRouteCache::FreeAllClusterAreaCache() {
	//free all cluster cache if existing
	if( !clusterareacache ) {
		return;
	}

	//free caches
	for( int i = 0; i < aasWorld.NumClusters(); i++ ) {
		const aas_cluster_t *cluster = &aasWorld.Clusters()[i];
		for( int j = 0; j < cluster->numareas; j++ ) {
			aas_routingcache_t *cache, *nextcache;
			for( cache = clusterareacache[i][j]; cache; cache = nextcache ) {
				nextcache = cache->next;
				FreeRoutingCache( cache );
			}
			clusterareacache[i][j] = nullptr;
		}
	}
	//free the cluster cache array
	FreeMemory( clusterareacache );
	clusterareacache = nullptr;
}

void AiAasRouteCache::InitClusterAreaCache( void ) {
	int i, size;
	for( size = 0, i = 0; i < aasWorld.NumClusters(); i++ ) {
		size += aasWorld.Clusters()[i].numareas;
	}
	//two dimensional array with pointers for every cluster to routing cache
	//for every area in that cluster
	int numBytes = aasWorld.NumClusters() * sizeof( aas_routingcache_t ** ) + size * sizeof( aas_routingcache_t * );
	char *ptr = (char *) GetClearedMemory( numBytes );
	clusterareacache = (aas_routingcache_t ***) ptr;
	ptr += aasWorld.NumClusters() * sizeof( aas_routingcache_t ** );
	for( i = 0; i < aasWorld.NumClusters(); i++ ) {
		clusterareacache[i] = (aas_routingcache_t **) ptr;
		ptr += aasWorld.Clusters()[i].numareas * sizeof( aas_routingcache_t * );
	}
}

void AiAasRouteCache::FreeAllPortalCache( void ) {
	//free all portal cache if existing
	if( !portalcache ) {
		return;
	}
	//free portal caches
	for( int i = 0; i < aasWorld.NumAreas(); i++ ) {
		aas_routingcache_t *cache, *nextcache;
		for( cache = portalcache[i]; cache; cache = nextcache ) {
			nextcache = cache->next;
			FreeRoutingCache( cache );
		}
		portalcache[i] = nullptr;
	}
	FreeMemory( portalcache );
	portalcache = nullptr;
}

void AiAasRouteCache::InitPortalCache( void ) {
	portalcache = (aas_routingcache_t **) GetClearedMemory( aasWorld.NumAreas() * sizeof( aas_routingcache_t * ) );
}

void AiAasRouteCache::InitRoutingUpdate( void ) {
	maxreachabilityareas = 0;
	for( int i = 0; i < aasWorld.NumClusters(); i++ ) {
		if( aasWorld.Clusters()[i].numreachabilityareas > maxreachabilityareas ) {
			maxreachabilityareas = aasWorld.Clusters()[i].numreachabilityareas;
		}
	}
	//allocate memory for the routing update fields
	areaupdate = (aas_routingupdate_t *) GetClearedMemory( maxreachabilityareas * sizeof( aas_routingupdate_t ) );
	//allocate memory for the portal update fields
	portalupdate = (aas_routingupdate_t *) GetClearedMemory( ( aasWorld.NumPortals() + 1 ) * sizeof( aas_routingupdate_t ) );
	//allocate memory for the Dijkstra's algorithm labels
	dijkstralabels = (signed char *) GetClearedMemory( maxreachabilityareas );

	oldestcache = nullptr;
	newestcache = nullptr;
}

constexpr auto MAX_REACHABILITYPASSAREAS = 32;

void AiAasRouteCache::InitReachabilityAreas() {
	int areas[MAX_REACHABILITYPASSAREAS];
	vec3_t start, end;

	reachabilityareas = (aas_reachabilityareas_t *)
						GetClearedRefCountedMemory( aasWorld.NumReachabilities() * sizeof( aas_reachabilityareas_t ) );
	reachabilityareaindex = (int *)
							GetClearedRefCountedMemory( aasWorld.NumReachabilities() * MAX_REACHABILITYPASSAREAS * sizeof( int ) );

	int numreachareas = 0;
	for( int i = 0; i < aasWorld.NumReachabilities(); i++ ) {
		const aas_reachability_t *reach = &aasWorld.Reachabilities()[i];
		int numareas = 0;
		switch( reach->traveltype & TRAVELTYPE_MASK ) {
			//trace areas from start to end
			case TRAVEL_BARRIERJUMP:
			case TRAVEL_WATERJUMP:
				VectorCopy( reach->start, end );
				end[2] = reach->end[2];
				numareas = aasWorld.TraceAreas( reach->start, end, areas, nullptr, MAX_REACHABILITYPASSAREAS );
				break;
			case TRAVEL_WALKOFFLEDGE:
				VectorCopy( reach->end, start );
				start[2] = reach->start[2];
				numareas = aasWorld.TraceAreas( start, reach->end, areas, nullptr, MAX_REACHABILITYPASSAREAS );
				break;
			case TRAVEL_GRAPPLEHOOK:
				numareas = aasWorld.TraceAreas( reach->start, reach->end, areas, nullptr, MAX_REACHABILITYPASSAREAS );
				break;

			//trace arch
			case TRAVEL_JUMP: break;
			case TRAVEL_ROCKETJUMP: break;
			case TRAVEL_BFGJUMP: break;
			case TRAVEL_JUMPPAD: break;

			//trace from reach->start to entity center, along entity movement
			//and from entity center to reach->end
			case TRAVEL_ELEVATOR: break;
			case TRAVEL_FUNCBOB: break;

			//no areas in between
			case TRAVEL_WALK: break;
			case TRAVEL_CROUCH: break;
			case TRAVEL_LADDER: break;
			case TRAVEL_SWIM: break;
			case TRAVEL_TELEPORT: break;
			default: break;
		} //end switch
		reachabilityareas[i].firstarea = numreachareas;
		reachabilityareas[i].numareas = numareas;
		for( int j = 0; j < numareas; j++ ) {
			reachabilityareaindex[numreachareas++] = areas[j];
		}
	}
}

struct RoutingUpdateRef
{
	int index;
	unsigned short tmpTravelTime;

	inline RoutingUpdateRef( int index_, unsigned short tmpTravelTime_ )
		: index( index_ ), tmpTravelTime( tmpTravelTime_ ) {}

	inline bool operator<( const RoutingUpdateRef &that ) const {
		return tmpTravelTime > that.tmpTravelTime;
	}
};

// Dijkstra's algorithm labels
constexpr signed char UNREACHED = 0;
constexpr signed char LABELED = -1;
constexpr signed char SCANNED = +1;

void AiAasRouteCache::UpdateAreaRoutingCache( aas_routingcache_t *areaCache ) {
	//NOTE: not more than 128 reachabilities per area allowed
	unsigned short startareatraveltimes[128];
	// Copied from the member to stack for faster access
	int travelFlagForType[MAX_TRAVELTYPES];

	//number of reachability areas within this cluster
	const int numreachabilityareas = aasWorld.Clusters()[areaCache->cluster].numreachabilityareas;
	const int badtravelflags = ~areaCache->travelflags;

	int clusterareanum = ClusterAreaNum( areaCache->cluster, areaCache->areanum );
	if( clusterareanum >= numreachabilityareas ) {
		return;
	}

	memset( startareatraveltimes, 0, sizeof( startareatraveltimes ) );
	// Copy to stack for faster access
	memcpy( travelFlagForType, this->travelflagfortype, sizeof( this->travelflagfortype ) );

	// Precache all references to avoid pointer chasing in loop
	const aas_areasettings_t *areaSettings = aasWorld.AreaSettings();
	const aas_reachability_t *reachabilities = aasWorld.Reachabilities();
	const aas_reversedreachability_t *reversedReachability = this->reversedreachability;
	const aas_portal_t *portals = aasWorld.Portals();
	aas_routingupdate_t *routingUpdate = this->areaupdate;
	const int *areaContentsTravelFlags = this->areacontentstravelflags;
	const auto *areaDisabledStatus = this->areasDisabledStatus;
	unsigned short ***areaTravelTimes = this->areatraveltimes;

	signed char *dijkstraAreaLabels = this->dijkstralabels;
	memset( dijkstraAreaLabels, UNREACHED, maxreachabilityareas );

	aas_routingupdate_t *curupdate = &routingUpdate[clusterareanum];
	curupdate->areanum = areaCache->areanum;
	curupdate->areatraveltimes = startareatraveltimes;
	curupdate->tmptraveltime = areaCache->starttraveltime;
	areaCache->traveltimes[clusterareanum] = areaCache->starttraveltime;
	dijkstraAreaLabels[clusterareanum] = LABELED;

	StaticVector<RoutingUpdateRef, 1024> updateHeap;
	updateHeap.push_back( RoutingUpdateRef( clusterareanum, curupdate->tmptraveltime ) );

	//while there are updates in the current list
	while( !updateHeap.empty() ) {
		std::pop_heap( updateHeap.begin(), updateHeap.end() );
		RoutingUpdateRef currUpdateRef = updateHeap.back();
		curupdate = &routingUpdate[currUpdateRef.index];
		dijkstraAreaLabels[currUpdateRef.index] = SCANNED;
		updateHeap.pop_back();

		//check all reversed reachability links
		const aas_reversedreachability_t *revreach = &reversedReachability[curupdate->areanum];
		//
		int i = 0;
		aas_reversedlink_t *revlink = revreach->first;
		for(; revlink; revlink = revlink->next, i++ ) {
			int linknum = revlink->linknum;
			const aas_reachability_t *reach = &reachabilities[linknum];
			//if there is used an undesired travel type
			if( travelFlagForType[reach->traveltype & TRAVELTYPE_MASK] & badtravelflags ) {
				continue;
			}
			//if not allowed to enter the next area
			if( areaDisabledStatus[reach->areanum].CurrStatus() ) {
				continue;
			}
			// Respect global flags too
			if( areaSettings[reach->areanum].areaflags & AREA_DISABLED ) {
				continue;
			}
			//if the next area has a not allowed travel flag
			if( areaContentsTravelFlags[reach->areanum] & badtravelflags ) {
				continue;
			}
			//number of the area the reversed reachability leads to
			int nextareanum = revlink->areanum;
			//get the cluster number of the area
			int cluster = areaSettings[nextareanum].cluster;
			//don't leave the cluster
			if( cluster > 0 && cluster != areaCache->cluster ) {
				continue;
			}

			// Here goes the inlined ClusterAreaNum() body
			const int areacluster = areaSettings[nextareanum].cluster;
			if( areacluster > 0 ) {
				clusterareanum = areaSettings[nextareanum].clusterareanum;
			} else {
				int side = portals[-areacluster].frontcluster != cluster;
				clusterareanum = portals[-areacluster].clusterareanum[side];
			}
			if( clusterareanum >= numreachabilityareas ) {
				continue;
			}

			if( dijkstraAreaLabels[clusterareanum] == SCANNED ) {
				continue;
			}

			//time already travelled plus the traveltime through
			//the current area plus the travel time from the reachability
			unsigned short t = curupdate->tmptraveltime + curupdate->areatraveltimes[i] + reach->traveltime;

			if( !areaCache->traveltimes[clusterareanum] || areaCache->traveltimes[clusterareanum] > t ) {
				int firstnextareareach = areaSettings[nextareanum].firstreachablearea;
				areaCache->traveltimes[clusterareanum] = t;
				areaCache->reachabilities[clusterareanum] = linknum - firstnextareareach;
				aas_routingupdate_t *nextupdate = &routingUpdate[clusterareanum];
				nextupdate->areanum = nextareanum;
				nextupdate->tmptraveltime = t;
				nextupdate->areatraveltimes = areaTravelTimes[nextareanum][linknum - firstnextareareach];
				if( dijkstraAreaLabels[clusterareanum] == UNREACHED ) {
					dijkstraAreaLabels[clusterareanum] = LABELED;
					updateHeap.push_back( RoutingUpdateRef( clusterareanum, t ) );
					std::push_heap( updateHeap.begin(), updateHeap.end() );
				}
			}
		}
	}
}

AiAasRouteCache::aas_routingcache_t *AiAasRouteCache::GetAreaRoutingCache( int clusternum, int areanum, int travelflags ) {
	//number of the area in the cluster
	int clusterareanum = ClusterAreaNum( clusternum, areanum );
	//find the cache without undesired travel flags
	aas_routingcache_t *cache = clusterareacache[clusternum][clusterareanum];
	for(; cache; cache = cache->next ) {
		//if there aren't used any undesired travel types for the cache
		if( cache->travelflags == travelflags ) {
			break;
		}
	}
	//if there was no cache
	if( !cache ) {
		cache = AllocRoutingCache( aasWorld.Clusters()[clusternum].numreachabilityareas );
		cache->cluster = clusternum;
		cache->areanum = areanum;
		VectorCopy( aasWorld.Areas()[areanum].center, cache->origin );
		cache->starttraveltime = 1;
		cache->travelflags = travelflags;
		cache->prev = nullptr;
		// Warning! Do not precache this reference at the beginning!
		// AllocRoutingCache() calls might modify the member!
		auto *oldCacheHead = clusterareacache[clusternum][clusterareanum];
		cache->next = oldCacheHead;
		if( oldCacheHead ) {
			oldCacheHead->prev = cache;
		}
		clusterareacache[clusternum][clusterareanum] = cache;
		UpdateAreaRoutingCache( cache );
	} else {
		UnlinkCache( cache );
	}
	//the cache has been accessed
	cache->type = CACHETYPE_AREA;
	LinkCache( cache );
	return cache;
}

void AiAasRouteCache::UpdatePortalRoutingCache( aas_routingcache_t *portalCache ) {
	// Precache these references to avoid pointer chasing in loop
	const aas_areasettings_t *areaSettings = aasWorld.AreaSettings();
	const aas_portalindex_t *portalIndex = aasWorld.PortalIndex();
	const aas_cluster_t *clusters = aasWorld.Clusters();
	const aas_portal_t *portals = aasWorld.Portals();
	aas_routingupdate_t *routingUpdate = this->portalupdate;
	int *portalMaxTravelTimes = this->portalmaxtraveltimes;

	aas_routingupdate_t *curupdate = &routingUpdate[aasWorld.NumPortals()];

	signed char *dijkstraPortalLabels = this->dijkstralabels;
	memset( dijkstraPortalLabels, UNREACHED, (size_t)( aasWorld.NumPortals() + 1 ) );

	curupdate->cluster = portalCache->cluster;
	curupdate->areanum = portalCache->areanum;
	curupdate->tmptraveltime = portalCache->starttraveltime;
	dijkstraPortalLabels[aasWorld.NumPortals()] = LABELED;
	//if the start area is a cluster portal, store the travel time for that portal
	int clusternum = areaSettings[portalCache->areanum].cluster;
	if( clusternum < 0 ) {
		portalCache->traveltimes[-clusternum] = portalCache->starttraveltime;
	}

	StaticVector<RoutingUpdateRef, 1024> updateHeap;
	updateHeap.push_back( RoutingUpdateRef( aasWorld.NumPortals(), portalCache->starttraveltime ) );

	//while there are updates in the current list
	while( !updateHeap.empty() ) {
		std::pop_heap( updateHeap.begin(), updateHeap.end() );
		curupdate = &portalupdate[updateHeap.back().index];
		dijkstraPortalLabels[updateHeap.back().index] = SCANNED;
		updateHeap.pop_back();

		// Fix invalid access to cluster 0
		if( !curupdate->cluster ) {
			continue;
		}

		const aas_cluster_t *cluster = &clusters[curupdate->cluster];
		//
		aas_routingcache_t *cache = GetAreaRoutingCache( curupdate->cluster, curupdate->areanum, portalCache->travelflags );
		//take all portals of the cluster
		for( int i = 0; i < cluster->numportals; i++ ) {
			int portalnum = portalIndex[cluster->firstportal + i];
			if( dijkstraPortalLabels[portalnum] == SCANNED ) {
				continue;
			}
			const aas_portal_t *portal = &portals[portalnum];
			//if this is the portal of the current update continue
			if( portal->areanum == curupdate->areanum ) {
				continue;
			}
			// Here goes the inlined ClusterAreaNum() body
			int clusterareanum;
			const int areacluster = aasWorld.AreaSettings()[portal->areanum].cluster;
			if( areacluster > 0 ) {
				clusterareanum = aasWorld.AreaSettings()[portal->areanum].clusterareanum;
			} else {
				int side = portals[-areacluster].frontcluster != curupdate->cluster;
				clusterareanum = portals[-areacluster].clusterareanum[side];
			}
			if( clusterareanum >= cluster->numreachabilityareas ) {
				continue;
			}
			//
			unsigned short t = cache->traveltimes[clusterareanum];
			if( !t ) {
				continue;
			}
			t += curupdate->tmptraveltime;

			if( !portalCache->traveltimes[portalnum] || portalCache->traveltimes[portalnum] > t ) {
				portalCache->traveltimes[portalnum] = t;
				aas_routingupdate_t *nextupdate = &routingUpdate[portalnum];
				if( portal->frontcluster == curupdate->cluster ) {
					nextupdate->cluster = portal->backcluster;
				} else {
					nextupdate->cluster = portal->frontcluster;
				}
				nextupdate->areanum = portal->areanum;
				//add travel time through the actual portal area for the next update
				nextupdate->tmptraveltime = t + portalMaxTravelTimes[portalnum];
				if( dijkstraPortalLabels[portalnum] == UNREACHED ) {
					dijkstraPortalLabels[portalnum] = LABELED;
					updateHeap.push_back( RoutingUpdateRef( portalnum, nextupdate->tmptraveltime ) );
					std::push_heap( updateHeap.begin(), updateHeap.end() );
				}
			}
		}
	}
}

AiAasRouteCache::aas_routingcache_t *AiAasRouteCache::GetPortalRoutingCache( int clusternum, int areanum, int travelflags ) {
	aas_routingcache_t *cache;
	//find the cached portal routing if existing
	for( cache = portalcache[areanum]; cache; cache = cache->next ) {
		if( cache->travelflags == travelflags ) {
			break;
		}
	}
	//if the portal routing isn't cached
	if( !cache ) {
		cache = AllocRoutingCache( aasWorld.NumPortals() );
		cache->cluster = clusternum;
		cache->areanum = areanum;
		VectorCopy( aasWorld.Areas()[areanum].center, cache->origin );
		cache->starttraveltime = 1;
		cache->travelflags = travelflags;
		//add the cache to the cache list
		cache->prev = nullptr;
		// Warning! Do not precache this reference at the beginning!
		// AllocRoutingCache() calls might modify the member!
		auto *oldCacheHead = portalcache[areanum];
		cache->next = oldCacheHead;
		if( oldCacheHead ) {
			oldCacheHead->prev = cache;
		}
		portalcache[areanum] = cache;
		//update the cache
		UpdatePortalRoutingCache( cache );
	} else {
		UnlinkCache( cache );
	}
	//the cache has been accessed
	cache->type = CACHETYPE_PORTAL;
	LinkCache( cache );
	return cache;
}

bool AiAasRouteCache::RoutingResultToGoalArea( int fromAreaNum, int toAreaNum,
											   int travelFlags, RoutingResult *result ) const {
	if( fromAreaNum == toAreaNum ) {
		result->traveltime = 1;
		result->reachnum = 0;
		return true;
	}

	if( fromAreaNum <= 0 || fromAreaNum >= aasWorld.NumAreas() ) {
		return false;
	}

	if( toAreaNum <= 0 || toAreaNum >= aasWorld.NumAreas() ) {
		return false;
	}

	if( aasWorld.AreaDoNotEnter( fromAreaNum ) || aasWorld.AreaDoNotEnter( toAreaNum ) ) {
		travelFlags |= TFL_DONOTENTER;
	}

	AiAasRouteCache *nonConstThis = const_cast<AiAasRouteCache *>( this );

	const uint64_t key = ResultCache::Key( fromAreaNum, toAreaNum, travelFlags );
	const uint16_t binIndex = ResultCache::BinIndexForKey( key );
	if( auto *cacheNode = resultCache.GetCachedResultForKey( binIndex, key ) ) {
		result->reachnum = cacheNode->reachability;
		result->traveltime = cacheNode->travelTime;
		return cacheNode->reachability != 0;
	}

	auto *cacheNode = nonConstThis->resultCache.AllocAndRegisterForKey( binIndex, key );
	RoutingRequest request( fromAreaNum, toAreaNum, travelFlags );
	if( nonConstThis->RouteToGoalArea( request, result ) ) {
		assert( result->reachnum >= 0 && result->reachnum <= 0xFFFF );
		cacheNode->reachability = (uint16_t)result->reachnum;
		assert( result->traveltime >= 0 && result->traveltime <= 0xFFFF );
		cacheNode->travelTime = (uint16_t)result->traveltime;
		return true;
	}
	cacheNode->reachability = 0;
	cacheNode->travelTime = 0;
	return false;
}

bool AiAasRouteCache::RouteToGoalArea( const RoutingRequest &request, RoutingResult *result ) {
	int clusternum = aasWorld.AreaSettings()[request.areanum].cluster;
	int goalclusternum = aasWorld.AreaSettings()[request.goalareanum].cluster;
	//check if the area is a portal of the goal area cluster
	if( clusternum < 0 && goalclusternum > 0 ) {
		const aas_portal_t *portal = &aasWorld.Portals()[-clusternum];
		if( portal->frontcluster == goalclusternum || portal->backcluster == goalclusternum ) {
			clusternum = goalclusternum;
		}
	}
	//check if the goalarea is a portal of the area cluster
	else if( clusternum > 0 && goalclusternum < 0 ) {
		const aas_portal_t *portal = &aasWorld.Portals()[-goalclusternum];
		if( portal->frontcluster == clusternum || portal->backcluster == clusternum ) {
			goalclusternum = clusternum;
		}
	}
	// Fix invalid access to cluster 0
	else if( !clusternum || !goalclusternum ) {
		return false;
	}

	//if both areas are in the same cluster
	//NOTE: there might be a shorter route via another cluster!!! but we don't care
	if( clusternum > 0 && goalclusternum > 0 && clusternum == goalclusternum ) {
		aas_routingcache_t *areacache = GetAreaRoutingCache( clusternum, request.goalareanum, request.travelflags );
		//the number of the area in the cluster
		int clusterareanum = ClusterAreaNum( clusternum, request.areanum );
		//the cluster the area is in
		const aas_cluster_t *cluster = &aasWorld.Clusters()[clusternum];
		//if the area is NOT a reachability area
		if( clusterareanum >= cluster->numreachabilityareas ) {
			return false;
		}
		//if it is possible to travel to the goal area through this cluster
		if( areacache->traveltimes[clusterareanum] != 0 ) {
			result->reachnum = aasWorld.AreaSettings()[request.areanum].firstreachablearea + areacache->reachabilities[clusterareanum];
			result->traveltime = areacache->traveltimes[clusterareanum];
			return true;
		}
	}
	goalclusternum = aasWorld.AreaSettings()[request.goalareanum].cluster;
	//if the goal area is a portal
	if( goalclusternum < 0 ) {
		//just assume the goal area is part of the front cluster
		const aas_portal_t *portal = &aasWorld.Portals()[-goalclusternum];
		goalclusternum = portal->frontcluster;
	}
	//get the portal routing cache
	aas_routingcache_t *portalCache = GetPortalRoutingCache( goalclusternum, request.goalareanum, request.travelflags );
	return RouteToGoalPortal( request, portalCache, result );
}

bool AiAasRouteCache::RouteToGoalPortal( const RoutingRequest &request, aas_routingcache_t *portalCache, RoutingResult *result ) {
	int clusternum = aasWorld.AreaSettings()[request.areanum].cluster;
	//if the area is a cluster portal, read directly from the portal cache
	if( clusternum < 0 ) {
		result->traveltime = portalCache->traveltimes[-clusternum];
		result->reachnum = aasWorld.AreaSettings()[request.areanum].firstreachablearea + portalCache->reachabilities[-clusternum];
		return true;
	}
	unsigned short besttime = 0;
	int bestreachnum = -1;
	//the cluster the area is in
	const aas_cluster_t *cluster = &aasWorld.Clusters()[clusternum];
	//find the portal of the area cluster leading towards the goal area
	for( int i = 0; i < cluster->numportals; i++ ) {
		int portalnum = aasWorld.PortalIndex()[cluster->firstportal + i];
		//if the goal area isn't reachable from the portal
		if( !portalCache->traveltimes[portalnum] ) {
			continue;
		}
		//
		const aas_portal_t *portal = &aasWorld.Portals()[portalnum];
		//get the cache of the portal area
		aas_routingcache_t *areacache = GetAreaRoutingCache( clusternum, portal->areanum, request.travelflags );
		//current area inside the current cluster
		int clusterareanum = ClusterAreaNum( clusternum, request.areanum );
		//if the area is NOT a reachability area
		if( clusterareanum >= cluster->numreachabilityareas ) {
			continue;
		}
		//if the portal is NOT reachable from this area
		if( !areacache->traveltimes[clusterareanum] ) {
			continue;
		}
		//total travel time is the travel time the portal area is from
		//the goal area plus the travel time towards the portal area
		unsigned short t = portalCache->traveltimes[portalnum] + areacache->traveltimes[clusterareanum];
		//FIXME: add the exact travel time through the actual portal area
		//NOTE: for now we just add the largest travel time through the portal area
		//		because we can't directly calculate the exact travel time
		//		to be more specific we don't know which reachability was used to travel
		//		into the portal area
		t += portalmaxtraveltimes[portalnum];
		// Qfusion: always fetch the reachnum even if origin is not present.
		int reachnum = aasWorld.AreaSettings()[request.areanum].firstreachablearea + areacache->reachabilities[clusterareanum];
		//if the time is better than the one already found
		if( !besttime || t < besttime ) {
			bestreachnum = reachnum;
			besttime = t;
		}
	}

	if( bestreachnum < 0 ) {
		return false;
	}

	result->reachnum = bestreachnum;
	result->traveltime = besttime;
	return true;
}
