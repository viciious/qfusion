#ifndef QFUSION_AI_NAV_MESH_MANAGER_H
#define QFUSION_AI_NAV_MESH_MANAGER_H

#include "../ai_local.h"

class AiNavMeshQuery
{
	friend class AiNavMeshManager;
	class AiNavMeshManager *parent;
	class dtNavMeshQuery *underlying;

	// Assumes that passed vectors are in Recast/Detour coord system
	bool TraceWalkabilityImpl( uint32_t startPolyRef, const vec3_t startPos, const vec3_t endPos );
public:
	uint32_t FindNearestPoly( const vec3_t absMins, const vec3_t absMaxs, float *closestPoint = nullptr );

	int FindPath( const vec3_t startAbsMins, const vec3_t startAbsMaxs,
				  const vec3_t endAbsMins, const vec3_t endAbsMaxs,
				  uint32_t *resultPolys, int maxResultPolys );

	int FindPath( uint32_t startPolyRef, uint32_t endPolyRef, uint32_t *resultPolys, int maxResultPolys );

	int FindPolysInRadius( const vec3_t startAbsMins, const vec3_t startAbsMaxs, float radius,
						   uint32_t *resultPolys, int maxResultPolys );

	int FindPolysInRadius( uint32_t startPolyRef, float radius, uint32_t *resultPolys, int maxResultPolys );

	bool TraceWalkability( uint32_t startPolyRef, uint32_t endPolyRef );
	bool TraceWalkability( uint32_t startPolyRef, const vec3_t startPos, uint32_t endPolyRef );
	bool TraceWalkability( uint32_t startPolyRef, const vec3_t endPos );
	bool TraceWalkability( uint32_t startPolyRef, const vec3_t startPos, const vec3_t endPos );
};

class AiNavMeshManager
{
	friend class AiNavMeshQuery;

	class dtNavMesh *underlyingNavMesh;
	float *polyCenters;
	float *polyBounds;

	// A data to save (might be null and is usually null).
	// We have decided to defer saving just computed data until map shutdown
	// to follow the existing TacticalSpotsRegistry behavior.
	// This data is not really large, should be several hundreds of KiBs.
	unsigned char *dataToSave;
	int dataToSaveSize;

	bool Load( const char *mapName );
	bool InitNavMeshFromData( unsigned char *data, int dataSize );

	// Add a slot for a world too
	mutable AiNavMeshQuery querySlots[MAX_CLIENTS + 1];
public:
	// Only a single query is allowed for a client, and a single shared query is allowed for a world
	AiNavMeshQuery *AllocQuery( const gclient_t *client ) const;
	void FreeQuery( AiNavMeshQuery *query ) const;

	static const AiNavMeshManager *Instance();
	AiNavMeshManager();
	~AiNavMeshManager();
	static void Init( const char *mapName );
	static void Shutdown();

	void GetPolyCenter( uint32_t polyRef, vec3_t center ) const;
	void GetPolyBounds( uint32_t polyRef, vec3_t mins, vec3_t maxs ) const;
	// Assumes that the output buffer has at least 3 * MAX_POLY_VERTICES capacity
	int GetPolyVertices( uint32_t polyRef, float *vertices ) const;
};



#endif
