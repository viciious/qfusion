#include "NavMeshManager.h"
#include "../ai_precomputed_file_handler.h"
#include "../buffer_builder.h"
#include "../static_vector.h"

#include <Recast.h>
#include <RecastAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>

//
// This is a derivative of the following code:
//

//
// Copyright (c) 2009-2010 Mikko Mononen memon@inside.org
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//

static inline void SwapYZ( float *data ) {
	std::swap( data[1], data[2] );
}

static inline void CopySwappingYZ( const float *from, float *to ) {
	to[0] = from[0];
	to[1] = from[2];
	to[2] = from[1];
}

// TODO: Hints are ignored at this stage.
// Trying to use G_LevelMalloc() for ALLOC_PERM hints leads to enormous required level pool size for building the mesh.
// (Mesh building should be performed only on developer machines).
static inline void *CustomAlloc( size_t size, int actualHint, int tmpAllocHint ) {
	return G_Malloc( size );
}

static inline void CustomFree( void *ptr, int tmpAllocHint ) {
	G_Free( ptr );
}

static void *CustomRecastAlloc( size_t size, rcAllocHint hint ) {
	return CustomAlloc( size, hint, RC_ALLOC_TEMP );
}

static void *CustomDetourAlloc( size_t size, dtAllocHint hint ) {
	return CustomAlloc( size, hint, DT_ALLOC_TEMP );
}

static void CustomRecastFree( void *ptr ) {
	CustomFree( ptr, RC_ALLOC_TEMP );
}

static void CustomDetourFree( void *ptr ) {
	CustomFree( ptr, DT_ALLOC_TEMP );
}

static StaticVector<AiNavMeshManager, 1> instanceHolder;

const AiNavMeshManager *AiNavMeshManager::Instance() {
	return &instanceHolder.front();
}

void AiNavMeshManager::Init( const char *mapName ) {
	if( !instanceHolder.empty() ) {
		AI_FailWith( "AiNavMeshManager::Init()", "An instance is already present\n" );
	}

	rcAllocSetCustom( CustomRecastAlloc, CustomRecastFree );
	dtAllocSetCustom( CustomDetourAlloc, CustomDetourFree );

	( new( instanceHolder.unsafe_grow_back() )AiNavMeshManager )->Load( mapName );
}

void AiNavMeshManager::Shutdown() {
	instanceHolder.clear();
}

AiNavMeshManager::AiNavMeshManager()
	: underlyingNavMesh( nullptr ),
	  polyCenters( nullptr ),
	  polyBounds( nullptr ),
	  dataToSave( nullptr ),
	  dataToSaveSize( 0 ) {
	for( auto &query: querySlots ) {
		query.parent = this;
		query.underlying = nullptr;
	}
}

constexpr const uint32_t PRECOMPUTED_FILE_VERSION = 0x1337B001;

// PrecomputedFileReader/Writer rely on G_LevelMalloc() by default,
// while the rest of code uses G_Malloc for reasons explained above.
// We have to supply our own allocation facilities compatible with the rest of code.

static void *PrecomputedIOAlloc( size_t size ) {
	return G_Malloc( size );
}

static void PrecomputedIOFree( void *ptr ) {
	G_Free( ptr );
}

static const char *MakePrecomputedFilePath( char *buffer, size_t bufferSize, const char *mapName ) {
	Q_snprintfz( buffer, bufferSize, "ai/%s.navmesh", mapName );
	return buffer;
}

AiNavMeshManager::~AiNavMeshManager() {
#ifndef PUBLIC_BUILD
	for( auto &query: querySlots ) {
		if( query.underlying ) {
			AI_FailWith( "~AiNavMeshManager()", "The query for slot %d is still in use", (int)( &query - querySlots ) );
		}
	}
#endif

	if( dataToSave ) {
		char filePath[MAX_QPATH];
		MakePrecomputedFilePath( filePath, sizeof( filePath ), level.mapname );
		constexpr const char *writerTag = "PrecomputedFileWriter@AiNavMeshManager";
		AiPrecomputedFileWriter writer( writerTag, PRECOMPUTED_FILE_VERSION, PrecomputedIOAlloc, PrecomputedIOFree );
		if( writer.BeginWriting( filePath ) ) {
			if( writer.WriteLengthAndData( dataToSave, (uint32_t)dataToSaveSize ) ) {
				G_Printf( "Precomputed nav mesh has been saved successfully to %s\n", filePath );
			} else {
				G_Printf( S_COLOR_RED "Can't write precomputed nav mesh data to file %s\n", filePath );
			}
		} else {
			G_Printf( S_COLOR_RED "Can't write precomputed nav mesh file header to file %s\n", filePath );
		}

		// Do not release an actual memory chunk referred from underlying nav mesh
		// at this moment too, just prevent using of THIS pointer.
		dataToSave = nullptr;
	}

	if( underlyingNavMesh ) {
		dtFreeNavMesh( underlyingNavMesh );
		underlyingNavMesh = nullptr;
	}

	if( polyCenters ) {
		G_LevelFree( polyCenters );
	}

	if( polyBounds ) {
		G_LevelFree( polyBounds );
	}
}

AiNavMeshQuery *AiNavMeshManager::AllocQuery( const gclient_t *client ) const {
	constexpr const char *tag = "AiNavMeshQuery::AllocQuery()";

	if( !client ) {
		AiNavMeshQuery *slotQuery = querySlots + MAX_CLIENTS;
		if( slotQuery->underlying ) {
			AI_FailWith( tag, "A shared query for the world (null client argument) is already present\n" );
		}

		slotQuery->underlying = dtAllocNavMeshQuery();
		if( !slotQuery->underlying ) {
			G_Printf( S_COLOR_YELLOW "%s: Can't allocate a shared query for the world (null client argument)\n", tag );
			return nullptr;
		}

		return slotQuery;
	}

	int clientNum = ENTNUM( client ) - 1;
	AiNavMeshQuery *slotQuery = querySlots + clientNum;
	if( slotQuery->underlying ) {
		AI_FailWith( tag, "A query for client #%d is already present\n", clientNum );
	}

	slotQuery->underlying = dtAllocNavMeshQuery();
	if( !slotQuery->underlying ) {
		G_Printf( S_COLOR_YELLOW "%s: Can't allocate a query for client #%d\n", tag, clientNum );
		return nullptr;
	}

	if( !slotQuery->underlying->init( underlyingNavMesh, 512 ) ) {
		G_Printf( S_COLOR_YELLOW "%s: Can't initialize the query for client #%d\n", tag, clientNum );
		dtFreeNavMeshQuery( slotQuery->underlying );
		slotQuery->underlying = nullptr;
		return nullptr;
	}

	return slotQuery;
}

void AiNavMeshManager::FreeQuery( AiNavMeshQuery *query ) const {
	if( query < querySlots || query > querySlots + MAX_CLIENTS + 1 ) {
		AI_FailWith( "AiNavMeshQuery::FreeQuery()", "Attempt to free a query pointer that is outside of the pool range\n" );
	}

	if( !query->underlying ) {
		AI_FailWith( "AiNavMeshQuery::FreeQuery()", "The query has been already freed or never used\n" );
	}

	dtFreeNavMeshQuery( query->underlying );
	query->underlying = nullptr;
}

static_assert( sizeof( uint32_t ) == sizeof( dtPolyRef ), "dtPolyRef and uint32_t are not compatible" );
static_assert( ( (dtPolyRef)-1 ) > 0, "dtPolyRef and uint32_t are not compatible" );

static const dtQueryFilter DEFAULT_QUERY_FILTER;

void AiNavMeshManager::GetPolyCenter( uint32_t polyRef, vec3_t center ) const {
	auto polyIndex = underlyingNavMesh->decodePolyIdPoly( polyRef );
	CopySwappingYZ( this->polyCenters + polyIndex * 3, center );
}

void AiNavMeshManager::GetPolyBounds( uint32_t polyRef, vec3_t mins, vec3_t maxs ) const {
	auto polyIndex = underlyingNavMesh->decodePolyIdPoly( polyRef );
	float *recastData = this->polyBounds + polyIndex * 6;
	CopySwappingYZ( recastData + 0, mins );
	CopySwappingYZ( recastData + 3, maxs );
}

int AiNavMeshManager::GetPolyVertices( uint32_t polyRef, float *vertices ) const {
	auto polyIndex = underlyingNavMesh->decodePolyIdPoly( polyRef );
	const auto *tile = ( (const dtNavMesh *)underlyingNavMesh )->getTile(0);
	const auto *poly = tile->polys + polyIndex;
	const float *tileVertices = tile->verts;
	const auto numPolyVertices = poly->vertCount;
	const auto *polyVertexOffsets = poly->verts;
	for( auto i = 0; i < numPolyVertices; ++i ) {
		const float *srcVertex = tileVertices + polyVertexOffsets[i] * 3;
		CopySwappingYZ( srcVertex, vertices );
		vertices += 3;
	}
	return numPolyVertices;
}

uint32_t AiNavMeshQuery::FindNearestPoly( const vec3_t absMins, const vec3_t absMaxs, float *closestPoint ) {
	Vec3 halfExtents( absMaxs );
	halfExtents -= absMins;
	halfExtents *= 0.5f;

	Vec3 center( absMins );
	center += halfExtents;

	// Convert to Recast/Detour coordinate system
	SwapYZ( center.Data() );
	SwapYZ( halfExtents.Data() );

	dtPolyRef ref;

	const dtQueryFilter *filter = &DEFAULT_QUERY_FILTER;
	dtStatus status = underlying->findNearestPoly( center.Data(), halfExtents.Data(), filter, &ref, closestPoint );
	// Detour returns a zero ref if no polys intersect the given bounds
	if( !ref ) {
		return 0;
	}

	if( dtStatusFailed( status ) ) {
		return 0;
	}

	if( closestPoint ) {
		SwapYZ( closestPoint );
	}

	return ref;
}

int AiNavMeshQuery::FindPath( const vec3_t startAbsMins, const vec3_t startAbsMaxs,
							   const vec3_t endAbsMins, const vec3_t endAbsMaxs,
							   uint32_t *resultPolys, int maxResultPolys ) {
	vec3_t startPos, endPos;

	uint32_t startPolyRef = FindNearestPoly( startAbsMins, startAbsMaxs, startPos );
	if( !startPolyRef ) {
		return 0;
	}

	uint32_t endPolyRef = FindNearestPoly( endAbsMins, endAbsMaxs, endPos );
	if( !endPolyRef ) {
		return 0;
	}

	// Convert to Recast/Detour coords again
	SwapYZ( startPos );
	SwapYZ( endPos );

	int result;
	const dtQueryFilter *filter = &DEFAULT_QUERY_FILTER;
	dtStatus status = underlying->findPath( startPolyRef, endPolyRef, startPos, endPos,
											filter, resultPolys, &result, maxResultPolys );
	return dtStatusSucceed( status ) ? result : 0;
}

int AiNavMeshQuery::FindPath( uint32_t startPolyRef, uint32_t endPolyRef, uint32_t *resultPolys, int maxResultPolys ) {
	const auto *mesh = underlying->getAttachedNavMesh();
	const float *startPolyCenter = parent->polyCenters + mesh->decodePolyIdPoly( startPolyRef ) * 3;
	const float *endPolyCenter = parent->polyCenters + mesh->decodePolyIdPoly( endPolyRef ) * 3;

	int result;
	const dtQueryFilter *filter = &DEFAULT_QUERY_FILTER;
	dtStatus status = underlying->findPath( startPolyRef, endPolyRef, startPolyCenter, endPolyCenter,
											filter, resultPolys, &result, maxResultPolys );
	return dtStatusSucceed( status ) ? result : 0;
}

int AiNavMeshQuery::FindPolysInRadius( const vec3_t startAbsMins, const vec3_t startAbsMaxs,
									   float radius, uint32_t *resultPolys, int maxResultPolys ) {
	vec3_t startPos;
	uint32_t startPolyRef = FindNearestPoly( startAbsMins, startAbsMaxs, startPos );
	if( !startPolyRef ) {
		return 0;
	}

	int result;
	const dtQueryFilter *filter = &DEFAULT_QUERY_FILTER;
	dtStatus status = underlying->findPolysAroundCircle( startPolyRef, startPos, radius,
														 filter, resultPolys, nullptr,
														 nullptr, &result, maxResultPolys );

	return dtStatusSucceed( status ) ? result : 0;
}

int AiNavMeshQuery::FindPolysInRadius( uint32_t startPolyRef, float radius, uint32_t *resultPolys, int maxResultPolys ) {
	const auto *mesh = underlying->getAttachedNavMesh();
	const float *startPolyCenter = parent->polyCenters + mesh->decodePolyIdPoly( startPolyRef ) * 3;

	int result;
	const dtQueryFilter *filter = &DEFAULT_QUERY_FILTER;
	dtStatus status = underlying->findPolysAroundCircle( startPolyRef, startPolyCenter, radius,
														 filter, resultPolys, nullptr,
														 nullptr, &result, maxResultPolys );
	return dtStatusSucceed( status ) ? result : 0;
}

bool AiNavMeshQuery::TraceWalkability( uint32_t startPolyRef, uint32_t endPolyRef ) {
	const auto *mesh = underlying->getAttachedNavMesh();
	const float *startPolyCenter = parent->polyCenters + mesh->decodePolyIdPoly( startPolyRef ) * 3;
	const float *endPolyCenter = parent->polyCenters + mesh->decodePolyIdPoly( endPolyRef ) * 3;
	return TraceWalkabilityImpl( startPolyRef, startPolyCenter, endPolyCenter );
}

bool AiNavMeshQuery::TraceWalkability( uint32_t startPolyRef, const vec3_t startPos, uint32_t endPolyRef ) {
	const auto *mesh = underlying->getAttachedNavMesh();
	const float *endPolyCenter = parent->polyCenters + mesh->decodePolyIdPoly( endPolyRef ) * 3;
	vec3_t detourStartPos;
	CopySwappingYZ( startPos, detourStartPos );
	return TraceWalkabilityImpl( startPolyRef, detourStartPos, endPolyCenter );
}

bool AiNavMeshQuery::TraceWalkability( uint32_t startPolyRef, const vec3_t endPos ) {
	const auto *mesh = underlying->getAttachedNavMesh();
	const float *startPolyCenter = parent->polyCenters + mesh->decodePolyIdPoly( startPolyRef ) * 3;
	vec3_t detourEndPos;
	CopySwappingYZ( endPos, detourEndPos );
	return TraceWalkabilityImpl( startPolyRef, startPolyCenter, detourEndPos );
}

bool AiNavMeshQuery::TraceWalkability( uint32_t startPolyRef, const vec3_t startPos, const vec3_t endPos ) {
	vec3_t detourStartPos, detourEndPos;
	CopySwappingYZ( startPos, detourStartPos );
	CopySwappingYZ( endPos, detourEndPos );
	return TraceWalkabilityImpl( startPolyRef, detourStartPos, detourEndPos );
}

bool AiNavMeshQuery::TraceWalkabilityImpl( uint32_t startPolyRef, const vec3_t startPos, const vec3_t endPos ) {
	const dtQueryFilter *filter = &DEFAULT_QUERY_FILTER;
	dtRaycastHit hit;
	// Make sure there will be no attempts to fill hit path made
	memset( &hit, 0, sizeof( hit ) );
	dtStatus status = underlying->raycast( startPolyRef, startPos, endPos, filter, 0, &hit );
	// Ignore "buffer too small" errors
	status &= ~DT_BUFFER_TOO_SMALL;
	if( dtStatusFailed( status ) ) {
		return false;
	}

	if( hit.t > 0.999f ) {
		return true;
	}

	// This is a hack for poor quality nav meshes.
	// Get the actual origin where the ray has stopped,
	// skip few units in the trace direction and do a second attempt.

	Vec3 traceDir( endPos );
	traceDir -= startPos;
	traceDir.NormalizeFast();

	Vec3 continueFrom( endPos );
	continueFrom -= startPos;
	continueFrom *= hit.t;
	continueFrom += startPos;
	continueFrom += 4.0f * traceDir;

	// Y is the height axis, give it a greater value
	Vec3 halfExtents( 4, 24, 4 );

	uint32_t continuePolyRef = 0;
	vec3_t dummyPos;
	status = underlying->findNearestPoly( continueFrom.Data(), halfExtents.Data(), filter, &continuePolyRef, dummyPos );
	if( dtStatusFailed( status ) || !continuePolyRef ) {
		return false;
	}

	memset( &hit, 0, sizeof( hit ) );
	status = underlying->raycast( continuePolyRef, continueFrom.Data(), endPos, filter, 0, &hit );
	status &= ~DT_BUFFER_TOO_SMALL;
	if( dtStatusFailed( status ) ) {
		return false;
	}

	return hit.t > 0.999f;
}

struct NavMeshInputTris {
	// Bounds in Recast/Detour coordinate system
	float mins[3], maxs[3];
	// Vertices in Recast/Detour coordinate system
	float *vertices;
	int *tris;
	int numVertices, numTris;

	NavMeshInputTris() {
		memset( this, 0, sizeof( *this ) );
	}

	~NavMeshInputTris() {
		ForceClear();
	}

	void ForceClear();
};

void NavMeshInputTris::ForceClear() {
	numVertices = 0;
	if( vertices ) {
		G_LevelFree( vertices );
		vertices = nullptr;
	}

	numTris = 0;
	if( tris ) {
		G_LevelFree( tris );
		tris = nullptr;
	}

	VectorSet( mins, 0, 0, 0 );
	VectorSet( maxs, 0, 0, 0 );
}

class NavMeshInputTrisSource {
public:
	virtual ~NavMeshInputTrisSource() {}
	virtual bool BuildTris( NavMeshInputTris *tris ) = 0;
};

class NavMeshBuilder {
	// Bounds of the supplied triangles set in Recast/Detour coordinate system
	vec3_t mins, maxs;

	float xzCellSize;
	float yCellSize;

	int gridWidth, gridHeight;

	unsigned char *trisAreaFlags;
	rcHeightfield *heightField;
	rcCompactHeightfield *compactHeightField;
	rcContourSet *contourSet;
	rcPolyMesh *polyMesh;
	rcPolyMeshDetail *polyMeshDetail;
	rcContext context;

	static constexpr auto WALKABLE_HEIGHT = 64;
	static constexpr auto WALKABLE_CLIMB = 18;
	static constexpr auto WALKABLE_RADIUS = 12;   // A bit lower than the actual half-hitbox width
	static constexpr auto WALKABLE_SLOPE = 55;    // A bit higher than the actual slope limit

	bool PrepareHeightField( NavMeshInputTris *tris );
	bool PrepareCompactHeightField();
	bool PrepareContourSet();
	bool PrepareMesh();
	bool PrepareDetailMesh();
	bool CreateNavMeshDataFromIntermediates( unsigned char **data, int *dataSize );
public:
	NavMeshBuilder()
		: xzCellSize( -1.0f ),
		  yCellSize( -1.0f ),
		  gridWidth( -1 ),
		  gridHeight( -1 ),
		  trisAreaFlags( nullptr ),
		  heightField( nullptr ),
		  compactHeightField( nullptr ),
		  contourSet( nullptr ),
		  polyMesh( nullptr ),
		  polyMeshDetail( nullptr ) {
		context.enableLog( false );
		context.enableTimer( false );
	}

	~NavMeshBuilder() {
		ForceClear();
	}

	bool BuildMeshData( unsigned char **data, int *dataSize );
	void ForceClear();
};

void NavMeshBuilder::ForceClear() {
	if( trisAreaFlags ) {
		G_Free( trisAreaFlags );
		trisAreaFlags = nullptr;
	}
	if( heightField ) {
		rcFreeHeightField( heightField );
		heightField = nullptr;
	}
	if( compactHeightField ) {
		rcFreeCompactHeightfield( compactHeightField );
		compactHeightField = nullptr;
	}
	if( contourSet ) {
		rcFreeContourSet( contourSet );
		contourSet = nullptr;
	}
	if( polyMesh ) {
		rcFreePolyMesh( polyMesh );
		polyMesh = nullptr;
	}
	if( polyMeshDetail ) {
		rcFreePolyMeshDetail( polyMeshDetail );
		polyMeshDetail = nullptr;
	}
}

class AasNavMeshInputTrisSource: public NavMeshInputTrisSource {
public:
	bool BuildTris( NavMeshInputTris *tris ) override;
};

bool AasNavMeshInputTrisSource::BuildTris( NavMeshInputTris *tris ) {
	const auto *aasWorld = AiAasWorld::Instance();
	constexpr const char *tag = "AiNavMeshManager::BuildNavMeshInputTris()";

	if( !aasWorld->IsLoaded() ) {
		G_Printf( S_COLOR_RED "%s: AAS world is not loaded\n", tag );
		return false;
	}

	// Put these references in locals/registers for faster access
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasFaceIndex = aasWorld->FaceIndex();
	const int aasFaceIndexSize = aasWorld->FaceIndexSize();
	const auto *aasFaces = aasWorld->Faces();
	const auto *aasEdgeIndex = aasWorld->EdgeIndex();
	const auto *aasEdges = aasWorld->Edges();
	const auto *aasPlanes = aasWorld->Planes();
	const auto *aasVertices = aasWorld->Vertexes();
	const int numAasVertices = aasWorld->NumVertexes();

	float *const trisMins = tris->mins;
	float *const trisMaxs = tris->maxs;
	ClearBounds( trisMins, trisMaxs );

	// A builder for nav mesh tris buffer
	BufferBuilder<int> trisIndicesBuilder( 512 );

	// To prepare vertices input, just copy AAS vertices (they are already indexed) swapping Z and Y components

	// We think there is no need to check the result for nullity as it fails with game error on allocation failure.
	// BufferBuilder methods also rely on this G_LevelMalloc() behavior.
	float *const trisVertices = ( float * )G_LevelMalloc( sizeof( float ) * 3 * aasWorld->NumVertexes() );
	for( int i = 0, j = 0; i < numAasVertices; ++i, j += 3 ) {
		trisVertices[j + 0] = aasVertices[i][0];
		trisVertices[j + 1] = aasVertices[i][2];
		trisVertices[j + 2] = aasVertices[i][1];
	}

	for( int areaNum = 1, numAasAreas = aasWorld->NumAreas(); areaNum < numAasAreas; ++areaNum ) {
		const auto &area = aasAreas[areaNum];
		int faceIndexNum = area.firstface;
		// This is a protection against broken faces that have been witnessed once (?)
		// Note that this limit looks way too extreme but valid areas
		// almost hitting the limit exist (e.g. around flagspot on wctf2).
		if( area.numfaces > 512 ) {
			G_Printf( S_COLOR_YELLOW "%s: Area #%d has %d > 512 faces, skipping the area\n", tag, areaNum, area.numfaces );
			continue;
		}

		const int faceIndexNumBound = faceIndexNum + area.numfaces;
		for(; faceIndexNum < faceIndexNumBound; ++faceIndexNum ) {
			if( faceIndexNum >= aasFaceIndexSize ) {
				G_Printf( S_COLOR_YELLOW "%s: Illegal faceIndexNum: %d, skipping the face\n", tag, faceIndexNum );
				continue;
			}

			const int faceIndex = abs( aasFaceIndex[ faceIndexNum ] );
			const auto &face = aasFaces[faceIndex];
			if( ( face.faceflags & ( FACE_GROUND | FACE_SOLID ) ) != ( FACE_GROUND | FACE_SOLID ) ) {
				continue;
			}

			// Its awkward we still have to check the normal after the previous face flags conditions.
			if( fabs( aasPlanes[face.planenum].normal[2] ) < 0.7f ) {
				continue;
			}

			if( face.numedges < 3 ) {
				G_Printf( S_COLOR_YELLOW "%s: Face #%d has %d < 3 edges, skipping the face\n", tag, faceIndex, face.numedges );
				continue;
			}

			if( face.numedges > 64 ) {
				G_Printf( S_COLOR_YELLOW "%s: Face #%d has %d > 32 edges, skipping the edge\n", tag, faceIndex, face.numedges );
				continue;
			}

			// We know face edges are arranged consequently, and the edges chain is closed.
			// And also, each face is a convex poly.

			int edgeIndexNum = face.firstedge;
			const auto &firstEdge = aasEdges[abs( aasEdgeIndex[edgeIndexNum + 0] )];
			const int firstVertexNum = firstEdge.v[0];
			// Add first edge vertices unconditionally
			trisIndicesBuilder.Add( firstEdge.v[0] );
			trisIndicesBuilder.Add( firstEdge.v[1] );

			AddPointToBounds( aasVertices[firstEdge.v[0]], trisMins, trisMaxs );
			AddPointToBounds( aasVertices[firstEdge.v[1]], trisMins, trisMaxs );

			const auto &secondEdge = aasEdges[abs( aasEdgeIndex[edgeIndexNum + 1] )];
			int lastInChainVertexNum;
			// Now things are more tricky. Edge vertices might be intended to be treated as swapped.
			// Check what vertex matches the last one of the first edge.
			if( VectorCompare( aasVertices[firstEdge.v[1]], aasVertices[secondEdge.v[0]] ) ) {
				lastInChainVertexNum = secondEdge.v[1];
			} else {
				lastInChainVertexNum = secondEdge.v[0];
			}

			AddPointToBounds( aasVertices[lastInChainVertexNum], trisMins, trisMaxs );
			trisIndicesBuilder.Add( lastInChainVertexNum );

			// We have added a triangle corresponding to 2 first edges
			edgeIndexNum += 2;
			// Stop before the last edge that encloses edges chain in a ring,
			// otherwise the last triangle will be degenerate.
			const int edgeIndexNumBound = edgeIndexNum + face.numedges - 1;
			for(; edgeIndexNum < edgeIndexNumBound; ++edgeIndexNum ) {
				// Concat the current edge to the previous one in the hull, adding 2 vertices
				const auto *currEdge = aasEdges + abs( aasEdgeIndex[ edgeIndexNum ] );
				if( VectorCompare( aasVertices[lastInChainVertexNum], aasVertices[currEdge->v[0]] ) ) {
					trisIndicesBuilder.Add( currEdge->v[0] );
					trisIndicesBuilder.Add( currEdge->v[1] );
					lastInChainVertexNum = currEdge->v[1];
				} else {
					trisIndicesBuilder.Add( currEdge->v[1] );
					trisIndicesBuilder.Add( currEdge->v[0] );
					lastInChainVertexNum = currEdge->v[0];
				}

				AddPointToBounds( aasVertices[lastInChainVertexNum], trisMins, trisMaxs );
				// Add the start vertex as the 3rd triangle vertex
				trisIndicesBuilder.Add( firstVertexNum );
			}
		}
	}

	tris->numVertices = numAasVertices;
	tris->vertices = trisVertices;
	assert( !( trisIndicesBuilder.Size() % 3 ) );
	tris->numTris = trisIndicesBuilder.Size() / 3;
	tris->tris = trisIndicesBuilder.FlattenResult();

	// Swap bounds Z and Y as Recast expects
	std::swap( trisMins[1], trisMins[2] );
	std::swap( trisMaxs[1], trisMaxs[2] );

	return true;
}

bool NavMeshBuilder::BuildMeshData( unsigned char **data, int *dataSize ) {
	constexpr const char *tag = "NavMeshBuilder::BuildMeshData()";

	NavMeshInputTris tris;
	AasNavMeshInputTrisSource trisSource;

	if( !trisSource.BuildTris( &tris ) ) {
		G_Printf( S_COLOR_RED "%s: Tris source has failed its execution, there is no tris to process\n", tag );
		return false;
	}

	if( !PrepareHeightField( &tris ) ) {
		return false;
	}

	if( !PrepareCompactHeightField() ) {
		return false;
	}

	if( !PrepareContourSet() ) {
		return false;
	}

	if( !PrepareMesh() ) {
		return false;
	}

	if( !PrepareDetailMesh() ) {
		return false;
	}

	return CreateNavMeshDataFromIntermediates( data, dataSize );
}

bool NavMeshBuilder::PrepareHeightField( NavMeshInputTris *tris ) {
	constexpr const char *tag = "NavMeshBuilder::PrepareHeightField()";

	// Save bounds (we are going to clean tris data early to save allocation space)
	// Copying via this macro is still valid for Recast/Detour coord system
	VectorCopy( tris->mins, this->mins );
	VectorCopy( tris->maxs, this->maxs );

	// Get world bounds
	vec3_t worldMins, worldMaxs, worldSize;
	trap_CM_InlineModelBounds( trap_CM_InlineModel( 0 ), worldMins, worldMaxs );
	// Compute world size and the largest dimension
	VectorSubtract( worldMaxs, worldMins, worldSize );
	float maxDimension = std::max( std::max( worldSize[0], worldSize[1] ), worldSize[2] );

	// We have to use these very low values, otherwise a resulting mesh has lots of holes and bad overall quality
	this->xzCellSize = this->yCellSize = 0.75f + BoundedFraction( maxDimension, 8192.0f );
	rcCalcGridSize( tris->mins, tris->maxs, this->xzCellSize, &this->gridWidth, &this->gridHeight );

	// Allocate and create the height field
	this->heightField = rcAllocHeightfield();
	if( !heightField ) {
		G_Printf( S_COLOR_RED "%s: Cannot allocate an initial Recast height field\n", tag );
		return false;
	}

	if( !rcCreateHeightfield( &context, *heightField, gridWidth,
							  gridHeight, tris->mins, tris->maxs,
							  this->xzCellSize, this->yCellSize ) ) {
		G_Printf( S_COLOR_RED "%s: Cannot create (initialize) the initial Recast height field\n", tag );
		return false;
	}

	// Allocate an array holding marked walkable tris
	// Should not return on failure?
	trisAreaFlags = (unsigned char *)G_Malloc( (size_t)tris->numTris );

	// Mark and rasterize walkable tris
	memset( trisAreaFlags, 0, (size_t)tris->numTris );
	rcMarkWalkableTriangles( &context, WALKABLE_SLOPE, tris->vertices,
							 tris->numVertices, tris->tris,
							 tris->numTris, trisAreaFlags );

	if( !rcRasterizeTriangles( &context, tris->vertices, tris->numVertices,
							   tris->tris, trisAreaFlags,
							   tris->numTris, *heightField ) ) {
		G_Printf( S_COLOR_RED "%s: Cannot rasterize Recast walkable triangles\n", tag );
		return false;
	}

	// Release trisAreaFlags immediately as they are no longer needed
	G_Free( trisAreaFlags );
	trisAreaFlags = nullptr;

	// Filter walkable surfaces.
	// This turned to be really important.

	rcFilterLowHangingWalkableObstacles( &context, WALKABLE_CLIMB, *heightField );
	rcFilterLedgeSpans( &context, WALKABLE_HEIGHT, WALKABLE_CLIMB, *heightField );
	rcFilterWalkableLowHeightSpans( &context, WALKABLE_HEIGHT, *heightField );

	return true;
}

bool NavMeshBuilder::PrepareCompactHeightField() {
	constexpr const char *tag = "NavMeshBuilder::PrepareCompactHeightField()";

	// Alloc and create compact heightfield
	compactHeightField = rcAllocCompactHeightfield();
	if( !compactHeightField ) {
		G_Printf( S_COLOR_RED "%s: Cannot allocate a Recast compact heightfield\n", tag );
		return false;
	}

	if( !rcBuildCompactHeightfield( &context, WALKABLE_HEIGHT, WALKABLE_CLIMB,
									*heightField, *compactHeightField ) ) {
		G_Printf( S_COLOR_RED "%s: Cannot build the Recast compact heightfield\n", tag );
		return false;
	}

	// Erode walkable areas
	if( !rcErodeWalkableArea( &context, 4, *compactHeightField ) ) {
		G_Printf( S_COLOR_RED "%s: Cannot erode walkable Recast area in the compact heightfield\n", tag );
		return false;
	}

	// Partition the height field.
	// This method is neither the fastest nor the best (as Recast docs promise),
	// but it produces better results for the actual input.

	constexpr int BORDER_SIZE = 0;
	constexpr int MIN_REGION_AREA = 24 * 24;
	constexpr int MERGE_REGION_AREA = 48 * 48;

	// Prepare for region partitioning, by calculating distance field along the walkable surface.
	if( !rcBuildDistanceField( &context, *compactHeightField ) ) {
		G_Printf( S_COLOR_RED "%s: Can't build a distance field for the compact height field\n", tag );
		return false;
	}

	// Partition the walkable surface into simple regions without holes.
	if( !rcBuildRegions( &context, *compactHeightField, BORDER_SIZE, MIN_REGION_AREA, MERGE_REGION_AREA ) ) {
		G_Printf( S_COLOR_RED "%s: Can't build regions in the compact height field\n", tag );
		return false;
	}

	// Release this no longer needed object to save space for further allocations

	rcFreeHeightField( heightField );
	heightField = nullptr;

	return true;
}

bool NavMeshBuilder::PrepareContourSet() {
	constexpr const char *tag = "NavMeshBuilder::PrepareContourSet()";

	contourSet = rcAllocContourSet();
	if( !contourSet ) {
		G_Printf( S_COLOR_RED "%s: Can't allocated a Recast contour set\n", tag );
		return false;
	}

	constexpr float MAX_ERROR = 32.0f;
	constexpr int MAX_EDGE_LEN = 128;

	if( !rcBuildContours( &context, *compactHeightField, MAX_ERROR, MAX_EDGE_LEN, *contourSet ) ) {
		G_Printf( S_COLOR_RED "%s: Can't build the Recast contour set\n", tag );
		return false;
	}

	return true;
}

bool NavMeshBuilder::PrepareMesh() {
	constexpr const char *tag = "NavMeshBuilder::PrepareMesh()";

	polyMesh = rcAllocPolyMesh();
	if( !polyMesh ) {
		G_Printf( S_COLOR_RED "%s: Can't allocate a Recast poly mesh\n", tag );
		return false;
	}

	if( !rcBuildPolyMesh( &context, *contourSet, DT_VERTS_PER_POLYGON, *polyMesh ) ) {
		G_Printf( S_COLOR_RED "%s: Can't build the Recast poly mesh from the contour set\n", tag );
		return false;
	}

	// Release this no longer needed object early to save space for further allocations

	rcFreeContourSet( contourSet );
	contourSet = nullptr;

	return true;
}

bool NavMeshBuilder::PrepareDetailMesh() {
	constexpr const char *tag = "NavMeshBuilder::PrepareDetailMesh()";

	polyMeshDetail = rcAllocPolyMeshDetail();
	if( !polyMeshDetail ) {
		G_Printf( S_COLOR_RED "%s: Can't allocate a Recast detail poly mesh\n", tag );
		return false;
	}

	if( !rcBuildPolyMeshDetail( &context, *polyMesh, *compactHeightField, 8, 8, *polyMeshDetail ) ) {
		G_Printf( S_COLOR_RED "%s: Can't build the Recast detail poly mesh from the poly mesh and compact heightfield\n", tag );
		return false;
	}

	// Release this no longer needed object early to save space for further allocations

	rcFreeCompactHeightfield( compactHeightField );
	compactHeightField = nullptr;

	return true;
}

bool NavMeshBuilder::CreateNavMeshDataFromIntermediates( unsigned char **data, int *dataSize ) {
	constexpr const char *tag = "NavMeshBuilder::CreateNavMeshDataFromIntermediates()";

	// We have to set poly flags manually (looks like they are zero by default).
	// Otherwise the default query filter cuts off every poly, expecting at least a single non-zero flag bit.
	// Currently just set all flags bits.
	memset( polyMesh->flags, 0xFF, sizeof( *polyMesh->flags ) * polyMesh->npolys );

	dtNavMeshCreateParams params;
	memset( &params, 0, sizeof( params ));
	params.verts = polyMesh->verts;
	params.vertCount = polyMesh->nverts;
	params.polys = polyMesh->polys;
	params.polyAreas = polyMesh->areas;
	params.polyFlags = polyMesh->flags;
	params.polyCount = polyMesh->npolys;
	params.nvp = polyMesh->nvp;
	params.detailMeshes = polyMeshDetail->meshes;
	params.detailVerts = polyMeshDetail->verts;
	params.detailVertsCount = polyMeshDetail->nverts;
	params.detailTris = polyMeshDetail->tris;
	params.detailTriCount = polyMeshDetail->ntris;

	// We do not set off-mesh connections input as they are unused in navigation and default zero values work fine.

	params.walkableHeight = WALKABLE_HEIGHT;
	params.walkableRadius = WALKABLE_RADIUS;
	params.walkableClimb = WALKABLE_CLIMB;
	VectorCopy( mins, params.bmin );
	VectorCopy( maxs, params.bmax );
	params.cs = xzCellSize;
	params.ch = yCellSize;
	params.buildBvTree = true;

	if( !dtCreateNavMeshData( &params, data, dataSize ) ) {
		G_Printf( S_COLOR_RED "%s: Cannot create Detour nav mesh data\n", tag );
		return false;
	}

	return true;
}

bool AiNavMeshManager::InitNavMeshFromData( unsigned char *data, int dataSize ) {
	constexpr const char *tag = "AiNavMeshManager::InitNavMeshFromData()";

	underlyingNavMesh = dtAllocNavMesh();
	if( !underlyingNavMesh ) {
		G_Printf( S_COLOR_RED "%s: Can't allocate Detour nav mesh\n", tag );
		return false;
	}

	dtStatus status = underlyingNavMesh->init( data, dataSize, DT_TILE_FREE_DATA );
	if( dtStatusFailed( status ) ) {
		G_Printf( S_COLOR_RED "%s: Can't initialize Detour nav mesh with the given data\n", tag );
		if( dtStatusDetail( status, DT_WRONG_MAGIC ) ) {
			G_Printf( S_COLOR_RED "%s: Wrong data magic number\n", tag );
		}
		if( dtStatusDetail( status, DT_WRONG_VERSION ) ) {
			G_Printf( S_COLOR_RED "%s: Wrong data version\n", tag );
		}
		if( dtStatusDetail( status, DT_OUT_OF_MEMORY ) ) {
			G_Printf( S_COLOR_RED "%s: Out of memory\n", tag );
		}
		return false;
	}

	const int maxTiles = underlyingNavMesh->getMaxTiles();
	if( maxTiles != 1 ) {
		G_Printf( S_COLOR_RED "%s: Illegal nav mesh tiles count: %d (must be 1)\n", tag, maxTiles );
		return false;
	}

	// Why a compiler prefers private non-const version, and thus fails?
	const dtMeshTile *tile = ( (const dtNavMesh *) underlyingNavMesh )->getTile( 0 );

	const auto numPolys = (unsigned)tile->header->polyCount;
	const float *vertices = tile->verts;
	const dtPoly *polys = tile->polys;
	// Never returns on failure
	polyCenters = (float *)G_LevelMalloc( 3 * sizeof( float ) * numPolys );
	polyBounds = (float *)G_LevelMalloc( 6 * sizeof( float ) * numPolys );

	for( unsigned i = 0; i < numPolys; ++i ) {
		float *mins = polyBounds + i * 6;
		float *maxs = mins + 3;
		float *center = polyCenters + i * 3;

		ClearBounds( mins, maxs );
		VectorClear( center );

		const dtPoly *poly = polys + i;
		assert( poly->vertCount );
		for( int j = 0; j < poly->vertCount; ++j ) {
			const float *v = vertices + 3 * poly->verts[j];
			VectorAdd( center, v, center );
			AddPointToBounds( v, mins, maxs );
		}

		float scale = 1.0f / poly->vertCount;
		VectorScale( center, scale, center );
	}

	G_Printf( "AiNavMeshLoader: Nav mesh data size: %d bytes, poly count: %d\n", dataSize, tile->header->polyCount );

	return true;
}

bool AiNavMeshManager::Load( const char *mapName ) {
	constexpr const char *tag = "AiNavMeshManager";

	char filePath[MAX_QPATH];
	MakePrecomputedFilePath( filePath, sizeof( filePath ), mapName );

	unsigned char *data = nullptr;
	int dataSize = 0;

	constexpr const char *readerTag = "PrecomputedFileReader@AiNavMeshManager";
	AiPrecomputedFileReader reader( readerTag, PRECOMPUTED_FILE_VERSION, PrecomputedIOAlloc, PrecomputedIOFree );
	const auto loadingStatus = reader.BeginReading( filePath );
	if( loadingStatus == AiPrecomputedFileReader::SUCCESS ) {
		if( reader.ReadLengthAndData( (uint8_t **)&data, (uint32_t *)&dataSize ) ) {
			if( this->InitNavMeshFromData( data, dataSize ) ) {
				G_Printf( "%s: A precomputed mesh data for map %s has been loaded successfully\n", tag, mapName );
				return true;
			} else {
				G_Printf( S_COLOR_RED "%s: Can't load nav mesh data from the read blob\n", tag );
			}
		} else {
			G_Printf( S_COLOR_RED "%s: Can't read nav mesh data from the file\n", tag );
		}
	} else if( loadingStatus == AiPrecomputedFileReader::MISSING ) {
		G_Printf( "%s: Looks like there is no precomputed nav mesh for map %s\n", tag, mapName );
	} else if( loadingStatus == AiPrecomputedFileReader::VERSION_MISMATCH ) {
		G_Printf( "%s: Looks like the precomputed nav mesh for map %s has a different version\n", tag, mapName );
	} else if( loadingStatus == AiPrecomputedFileReader::FAILURE ) {
		G_Printf( S_COLOR_RED "%s: An error has occurred while reading a nav mesh header for map %s\n", tag, mapName );
	}

	G_Printf( "%s: Building nav mesh data for map %s (it might take a while...)\n", tag, mapName );

	NavMeshBuilder builder;
	if( !builder.BuildMeshData( &data, &dataSize ) ) {
		G_Printf( S_COLOR_RED "%s: Can't build nav mesh data for map %s\n", tag, mapName );
		return false;
	}

	// Release allocated memory without waiting for leaving the scope to save space for further allocations
	builder.ForceClear();

	if( !this->InitNavMeshFromData( data, dataSize ) ) {
		G_Printf( S_COLOR_RED "%s: Can't load nav mesh data from the computed blob for map %s\n", tag, mapName );
		return false;
	}

	// Mark the data for saving
	this->dataToSave = data;
	this->dataToSaveSize = dataSize;

	G_Printf( "%s: The nav mesh data has been initialized successfully\n", tag );
	return true;
}