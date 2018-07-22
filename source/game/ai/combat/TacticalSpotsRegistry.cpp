#include "TacticalSpotsRegistry.h"
#include "../ai_precomputed_file_handler.h"
#include "../bot.h"

typedef TacticalSpotsRegistry::SpotsQueryVector SpotsQueryVector;

TacticalSpotsRegistry *TacticalSpotsRegistry::instance = nullptr;
// An actual storage for an instance
static StaticVector<TacticalSpotsRegistry, 1> instanceHolder;

bool TacticalSpotsRegistry::Init( const char *mapname ) {
	if( instance ) {
		AI_FailWith( "TacticalSpotsRegistry::Init()", "The instance has been already initialized\n" );
	}
	if( instanceHolder.size() ) {
		AI_FailWith( "TacticalSpotsRegistry::Init()", "The instance holder must be empty at this moment\n" );
	}

	instance = new( instanceHolder.unsafe_grow_back() )TacticalSpotsRegistry;
	return instance->Load( mapname );
}

class TacticalSpotsBuilder {
	typedef TacticalSpotsRegistry::TacticalSpot TacticalSpot;

	AreaAndScore *candidateAreas { nullptr };
	int numCandidateAreas { 0 };
	int candidateAreasCapacity { 0 };

	Vec3 *candidatePoints { nullptr };
	int numCandidatePoints { 0 };
	int candidatePointsCapacity { 0 };

	TacticalSpot *spots { nullptr };
	int numSpots { 0 };
	int spotsCapacity { 0 };

	uint8_t *spotVisibilityTable { nullptr };
	uint16_t *spotTravelTimeTable { nullptr };

	TacticalSpotsRegistry::SpotsGridBuilder gridBuilder;

	bool TestAas();

	template< typename T >
	void AddItem( const T &item, T **items, int *numItems, int *itemsCapacity ) {
		void *mem = AllocItem( items, numItems, itemsCapacity );
		new( mem )T( item );
	}

	template<typename T>
	T *AllocItem( T **items, int *numItems, int *itemsCapacity );

	void FindCandidateAreas();
	void AddCandidateArea( int areaNum, float score ) {
		AddItem( AreaAndScore( areaNum, score ), &candidateAreas, &numCandidateAreas, &candidateAreasCapacity );
	}

	void FindCandidatePoints();

	void AddAreaFacePoints( int areaNum );

	void AddCandidatePoint( const Vec3 &point ) {
		AddItem( point, &candidatePoints, &numCandidatePoints, &candidatePointsCapacity );
	}

	void TryAddSpotFromPoint( const Vec3 &point );
	int TestPointForGoodAreaNum( const vec3_t point );
	bool IsAGoodSpotPosition( const SpotsQueryVector &nearbySpots, vec3_t spot, int *numVisNearbySpots, int *areaNum );

	TacticalSpot *AllocSpot() {
		return AllocItem( &spots, &numSpots, &spotsCapacity );
	}

	void PickTacticalSpots();
	void ComputeMutualSpotsVisibility();
	void ComputeMutualSpotsReachability();
public:
	explicit TacticalSpotsBuilder( TacticalSpotsRegistry *registry ): gridBuilder( registry ) {}

	~TacticalSpotsBuilder();

	bool Build();

	void CopyTo( TacticalSpotsRegistry *registry );
};

bool TacticalSpotsRegistry::Load( const char *mapname ) {
	if( TryLoadPrecomputedData( mapname ) ) {
		return true;
	}

	TacticalSpotsBuilder builder( this );
	if( !builder.Build() ) {
		return false;
	}

	builder.CopyTo( this );
	return true;
}

constexpr const uint32_t PRECOMPUTED_DATA_VERSION = 0x1337A001;

static const char *MakePrecomputedFilePath( char *buffer, size_t bufferSize, const char *mapName ) {
	Q_snprintfz( buffer, bufferSize, "ai/%s.spots", mapName );
	return buffer;
}

bool TacticalSpotsRegistry::TryLoadPrecomputedData( const char *mapName ) {
	char filePath[MAX_QPATH];
	MakePrecomputedFilePath( filePath, sizeof( filePath ), mapName );

	constexpr const char *function = "TacticalSpotsRegistry::TryLoadPrecomputedData()";

	AiPrecomputedFileReader reader( "PrecomputedFileReader@TacticalSpotsRegistry", PRECOMPUTED_DATA_VERSION );

	uint32_t dataLength;
	uint8_t *data;

	if( reader.BeginReading( filePath ) != AiPrecomputedFileReader::SUCCESS ) {
		return false;
	}

	// Read spots
	if( !reader.ReadLengthAndData( &data, &dataLength ) ) {
		return false;
	}

	// We do not need to add cleanup guards for any piece of data read below
	// (the pointers are saved in members and get freed in the class destructor)

	spots = (TacticalSpot *)data;
	numSpots = dataLength / sizeof( TacticalSpot );

	// Read spots travel time table
	if( !reader.ReadLengthAndData( &data, &dataLength ) ) {
		return false;
	}

	spotTravelTimeTable = (uint16_t *)data;
	if( dataLength / sizeof( uint16_t ) != numSpots * numSpots ) {
		G_Printf( S_COLOR_RED "%s: Travel time table size does not match the number of spots\n", function );
		return false;
	}

	// Read spots visibility table
	if( !reader.ReadLengthAndData( &data, &dataLength ) ) {
		return false;
	}

	spotVisibilityTable = (uint8_t *)data;
	if( dataLength / sizeof( uint8_t ) != numSpots * numSpots ) {
		G_Printf( S_COLOR_RED "%s: Spots visibility table size does not match the number of spots\n", function );
		return false;
	}

	// Byte swap and validate tactical spots
	const int numAasAreas = AiAasWorld::Instance()->NumAreas();
	for( unsigned i = 0; i < numSpots; ++i ) {
		auto &spot = spots[i];
		spot.aasAreaNum = LittleLong( spot.aasAreaNum );
		if( spot.aasAreaNum <= 0 || spot.aasAreaNum >= numAasAreas ) {
			G_Printf( S_COLOR_RED "%s: Bogus spot %d area num %d\n", function, i, spot.aasAreaNum );
			return false;
		}

		for( unsigned j = 0; j < 3; ++j ) {
			spot.origin[j] = LittleFloat( spot.origin[j] );
			spot.absMins[j] = LittleFloat( spot.absMins[j] );
			spot.absMaxs[j] = LittleFloat( spot.absMaxs[j] );
		}
	}

	// Byte swap and travel times
	for( unsigned i = 0; i < numSpots; ++i ) {
		spotTravelTimeTable[i] = LittleShort( spotTravelTimeTable[i] );
	}

	// Spot visibility does not need neither byte swap nor validation being just an unsigned byte
	static_assert( sizeof( *spotVisibilityTable ) == 1, "" );

	spotsGrid.AttachSpots( spots, numSpots );
	if( !spotsGrid.Load( reader ) ) {
		return false;
	}

	return true;
}

bool TacticalSpotsRegistry::PrecomputedSpotsGrid::Load( AiPrecomputedFileReader &reader ) {
	SetupGridParams();
	const unsigned numGridCells = NumGridCells();

	uint32_t dataLength;
	uint8_t *data;
	// Read grid list offsets
	if( !reader.ReadLengthAndData( &data, &dataLength ) ) {
		return false;
	}

	constexpr const char *function = "TacticalSpotsRegistry::PrecomputedSpotsGrid::Load()";

	gridListOffsets = (uint32_t *)data;
	if( dataLength / sizeof( uint32_t ) != numGridCells ) {
		G_Printf( S_COLOR_RED "%s: Grid spot list offsets size does not match the number of cells\n", function );
		return false;
	}

	// Read grid spot lists
	if( !reader.ReadLengthAndData( &data, &dataLength ) ) {
		return false;
	}

	gridSpotsLists = (uint16_t *)data;
	const unsigned gridListsArraySize = dataLength / sizeof( uint16_t );
	if( gridListsArraySize != numGridCells + numSpots ) {
		G_Printf( S_COLOR_RED "%s: Grid spot lists array size does not match numbers of cells and spots\n", function );
		return false;
	}

	// Byte swap and validate offsets
	for( unsigned i = 0; i < numGridCells; ++i ) {
		gridListOffsets[i] = LittleLong( gridListOffsets[i] );
		if( gridListOffsets[i] >= gridListsArraySize ) {
			G_Printf( S_COLOR_RED "%s: Bogus grid list offset %d for cell %d\n", function, gridListOffsets[i], i );
			return false;
		}
	}

	// Byte swap and validate lists
	for( unsigned i = 0; i < numGridCells; ++i ) {
		uint16_t *gridSpotsList = gridSpotsLists + gridListOffsets[i];
		// Byte swap and validate list spots number
		uint16_t numListSpots = gridSpotsList[0] = LittleShort( gridSpotsList[0] );
		if( gridListOffsets[i] + numListSpots > gridListsArraySize ) {
			G_Printf( S_COLOR_RED "%s: Bogus grid list num spots %d for cell %d", function, numListSpots, i );
			return false;
		}

		// Byte swap grid spot list nums
		for( uint16_t j = 0; j < numListSpots; ++j ) {
			gridSpotsList[j + 1] = LittleShort( gridSpotsList[j + 1] );
		}
	}

	return true;
}

void TacticalSpotsRegistry::SavePrecomputedData( const char *mapName ) {
	char fileName[MAX_QPATH];
	MakePrecomputedFilePath( fileName, sizeof( fileName ), mapName );

	AiPrecomputedFileWriter writer( "PrecomputedFileWriter@TacticalSpotsRegistry", PRECOMPUTED_DATA_VERSION );
	if( !writer.BeginWriting( fileName ) ) {
		return;
	}

	// Byte swap spots
	static_assert( sizeof( spots->aasAreaNum ) == 4, "LittleLong() is not applicable" );
	for( unsigned i = 0; i < numSpots; ++i ) {
		auto &spot = spots[i];
		spot.aasAreaNum = LittleLong( spot.aasAreaNum );
		for( int j = 0; j < 3; ++j ) {
			spot.origin[j] = LittleFloat( spot.origin[j] );
			spot.absMins[j] = LittleFloat( spot.origin[j] );
			spot.absMaxs[j] = LittleFloat( spot.origin[j] );
		}
	}

	uint32_t dataLength = numSpots * sizeof( TacticalSpot );
	if( !writer.WriteLengthAndData( (const uint8_t *)spots, dataLength ) ) {
		return;
	}

	// Prevent using byte-swapped spots
	G_LevelFree( spots );
	spots = nullptr;

	// Byte swap travel times
	static_assert( sizeof( *spotTravelTimeTable ) == 2, "LittleShort() is not applicable" );
	for( unsigned i = 0, end = numSpots * numSpots; i < end; ++i )
		spotTravelTimeTable[i] = LittleShort( spotTravelTimeTable[i] );

	dataLength = numSpots * numSpots * sizeof( *spotTravelTimeTable );
	if( !writer.WriteLengthAndData( (const uint8_t *)spotTravelTimeTable, dataLength ) ) {
		return;
	}

	// Prevent using byte-swapped travel times table
	G_LevelFree( spotTravelTimeTable );
	spotTravelTimeTable = nullptr;

	static_assert( sizeof( *spotVisibilityTable ) == 1, "Byte swapping is required" );
	dataLength = numSpots * numSpots * sizeof( *spotVisibilityTable );
	if( !writer.WriteLengthAndData( (const uint8_t *)spotVisibilityTable, dataLength ) ) {
		return;
	}

	// Release the data for conformance with the rest of the saved data
	G_LevelFree( spotVisibilityTable );
	spotVisibilityTable = nullptr;

	spotsGrid.Save( writer );

	G_Printf( "The precomputed tactical spots data has been saved successfully to %s\n", fileName );
}

void TacticalSpotsRegistry::PrecomputedSpotsGrid::Save( AiPrecomputedFileWriter &writer ) {
	uint32_t dataLength;

	// Byte swap grid list offsets and grid spots lists
	static_assert( sizeof( *gridListOffsets ) == 4, "LittleLong() is not applicable" );
	for( unsigned i = 0, end = NumGridCells(); i < end; ++i ) {
		unsigned listOffset = gridListOffsets[i];
		gridListOffsets[i] = LittleLong( gridListOffsets[i] );
		uint16_t *list = gridSpotsLists + listOffset;
		unsigned numListSpots = *list;
		// Byte-swap the list data (number of lists spots and the spots nums)
		for( unsigned j = 0; j < numListSpots + 1; ++j )
			list[j] = LittleShort( list[j] );
	}

	dataLength = NumGridCells() * sizeof( *gridListOffsets );
	if( !writer.WriteLengthAndData( (const uint8_t *)gridListOffsets, dataLength ) ) {
		return;
	}

	// Prevent using byte-swapped grid list offsets
	G_LevelFree( gridListOffsets );
	gridListOffsets = nullptr;

	dataLength = sizeof( uint16_t ) * ( NumGridCells() + numSpots );
	if( !writer.WriteLengthAndData( (const uint8_t *)gridSpotsLists, dataLength ) ) {
		return;
	}

	// Prevent using byte-swapped grid spots lists
	G_LevelFree( gridSpotsLists );
	gridSpotsLists = nullptr;
}

void TacticalSpotsRegistry::Shutdown() {
	if( !instance ) {
		if( instanceHolder.size() ) {
			AI_FailWith("TacticalSpotsRegistry::Shutdown()", "The instance holder must be empty at this moment\n" );
		}
		// Calling Shutdown() without a preceding initialization is legal.
		return;
	}
	instanceHolder.pop_back();
	instance = nullptr;
}

TacticalSpotsRegistry::~TacticalSpotsRegistry() {
	if( needsSavingPrecomputedData ) {
		SavePrecomputedData( level.mapname );
		needsSavingPrecomputedData = false;
	}

	numSpots = 0;
	if( spots ) {
		G_LevelFree( spots );
	}
	if( spotVisibilityTable ) {
		G_LevelFree( spotVisibilityTable );
	}
	if( spotTravelTimeTable ) {
		G_LevelFree( spotTravelTimeTable );
	}
}

void TacticalSpotsBuilder::ComputeMutualSpotsVisibility() {
	G_Printf( "Computing mutual tactical spots visibility (it might take a while)...\n" );

	unsigned uNumSpots = (unsigned)numSpots;
	spotVisibilityTable = (unsigned char *)G_LevelMalloc( uNumSpots * uNumSpots );

	float *mins = vec3_origin;
	float *maxs = vec3_origin;

	trace_t trace;
	for( unsigned i = 0; i < uNumSpots; ++i ) {
		// Consider each spot visible to itself
		spotVisibilityTable[i * numSpots + i] = 255;

		TacticalSpot &currSpot = spots[i];
		vec3_t currSpotBounds[2];
		VectorCopy( currSpot.absMins, currSpotBounds[0] );
		VectorCopy( currSpot.absMaxs, currSpotBounds[1] );

		// Mutual visibility for spots [0, i) has been already computed
		for( unsigned j = i + 1; j < uNumSpots; ++j ) {
			TacticalSpot &testedSpot = spots[j];
			if( !trap_inPVS( currSpot.origin, testedSpot.origin ) ) {
				spotVisibilityTable[j * numSpots + i] = 0;
				spotVisibilityTable[i * numSpots + j] = 0;
				continue;
			}

			unsigned char visibility = 0;
			vec3_t testedSpotBounds[2];
			VectorCopy( testedSpot.absMins, testedSpotBounds[0] );
			VectorCopy( testedSpot.absMaxs, testedSpotBounds[1] );

			// Do not test against any entities using G_Trace() as these tests are expensive and pointless in this case.
			// Test only against the solid world.
			SolidWorldTrace( &trace, currSpot.origin, testedSpot.origin, mins, maxs );
			bool areOriginsMutualVisible = ( trace.fraction == 1.0f );

			for( unsigned n = 0; n < 8; ++n ) {
				float from[] =
				{
					currSpotBounds[( n >> 2 ) & 1][0],
					currSpotBounds[( n >> 1 ) & 1][1],
					currSpotBounds[( n >> 0 ) & 1][2]
				};
				for( unsigned m = 0; m < 8; ++m ) {
					float to[] =
					{
						testedSpotBounds[( m >> 2 ) & 1][0],
						testedSpotBounds[( m >> 1 ) & 1][1],
						testedSpotBounds[( m >> 0 ) & 1][2]
					};
					SolidWorldTrace( &trace, from, to, mins, maxs );
					// If all 64 traces succeed, the visibility is a half of the maximal score
					visibility += 2 * (unsigned char)trace.fraction;
				}
			}

			// Prevent marking of the most significant bit
			if( visibility == 128 ) {
				visibility = 127;
			}

			// Mutual origins visibility counts is a half of the maximal score.
			// Also, if the most significant bit of visibility bits is set, spot origins are mutually visible
			if( areOriginsMutualVisible ) {
				visibility |= 128;
			}

			spotVisibilityTable[i * numSpots + j] = visibility;
			spotVisibilityTable[j * numSpots + i] = visibility;
		}
	}
}

void TacticalSpotsBuilder::ComputeMutualSpotsReachability() {
	G_Printf( "Computing mutual tactical spots reachability (it might take a while)...\n" );

	unsigned uNumSpots = (unsigned)numSpots;
	spotTravelTimeTable = (unsigned short *)G_LevelMalloc( sizeof( unsigned short ) * uNumSpots * uNumSpots );
	const int flags = Bot::ALLOWED_TRAVEL_FLAGS;
	AiAasRouteCache *routeCache = AiAasRouteCache::Shared();
	// Note: spots reachabilities are not reversible
	// (for spots two A and B reachabilies A->B and B->A might differ, including being invalid, non-existent)
	// Thus we have to find a reachability for each possible pair of spots
	for( unsigned i = 0; i < uNumSpots; ++i ) {
		const int currAreaNum = spots[i].aasAreaNum;
		for( unsigned j = 0; j < i; ++j ) {
			const int testedAreaNum = spots[j].aasAreaNum;
			const int travelTime = routeCache->TravelTimeToGoalArea( currAreaNum, testedAreaNum, flags );
			// AAS uses short for travel time computation. If one changes it, this assertion might be triggered.
			assert( travelTime <= std::numeric_limits<unsigned short>::max() );
			spotTravelTimeTable[i * numSpots + j] = (unsigned short)travelTime;
		}
		// Set the lowest feasible travel time value for traveling from the curr spot to the curr spot itself.
		spotTravelTimeTable[i * numSpots + i] = 1;
		for( unsigned j = i + 1; j < uNumSpots; ++j ) {
			const int testedAreaNum = spots[j].aasAreaNum;
			const int travelTime = routeCache->TravelTimeToGoalArea( currAreaNum, testedAreaNum, flags );
			assert( travelTime <= std::numeric_limits<unsigned short>::max() );
			spotTravelTimeTable[i * numSpots + j] = (unsigned short)travelTime;
		}
	}
}

TacticalSpotsBuilder::~TacticalSpotsBuilder() {
	if( candidateAreas ) {
		G_LevelFree( candidateAreas );
	}
	if ( candidatePoints ) {
		G_LevelFree( candidatePoints );
	}
	if( spots ) {
		G_LevelFree( spots );
	}
	if( spotVisibilityTable ) {
		G_LevelFree( spotVisibilityTable );
	}
	if( spotTravelTimeTable ) {
		G_LevelFree( spotTravelTimeTable );
	}
}

bool TacticalSpotsBuilder::Build() {
	if( !TestAas() ) {
		return false;
	}

	PickTacticalSpots();
	ComputeMutualSpotsVisibility();
	ComputeMutualSpotsReachability();
	return true;
}

bool TacticalSpotsBuilder::TestAas() {
	const auto *aasWorld = AiAasWorld::Instance();
	if( !aasWorld->IsLoaded() ) {
		G_Printf( S_COLOR_RED "TacticalSpotsBuilder::Build(): AAS world is not loaded\n" );
		return false;
	}

	if( aasWorld->NumAreas() > 256 && aasWorld->NumFaces() < 256 ) {
		G_Printf( S_COLOR_RED "TacticalSpotsBuilder::Build(): Looks like the AAS file is stripped\n" );
		return false;
	}

	return true;
}

void TacticalSpotsBuilder::PickTacticalSpots() {
	G_Printf( "Picking tactical spots (it might take a while)...\n" );
	FindCandidatePoints();
	for( int i = 0; i < numCandidatePoints; ++i ) {
		TryAddSpotFromPoint( candidatePoints[i] );
	}
}

inline bool LooksLikeAGoodArea( const aas_areasettings_t &areaSettings, int badContents ) {
	if( !( areaSettings.areaflags & AREA_GROUNDED ) ) {
		return false;
	}
	if( areaSettings.areaflags & ( AREA_JUNK | AREA_DISABLED ) ) {
		return false;
	}
	if( areaSettings.contents & badContents ) {
		return false;
	}
	return true;
}

void TacticalSpotsBuilder::FindCandidateAreas() {
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasReach = aasWorld->Reachabilities();
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const int numAasAreas = aasWorld->NumAreas();

	const auto badContents = AREACONTENTS_WATER | AREACONTENTS_LAVA | AREACONTENTS_SLIME | AREACONTENTS_DONOTENTER;
	for( int i = 1; i < numAasAreas; ++i ) {
		if( !LooksLikeAGoodArea( aasAreaSettings[i], badContents ) ) {
			continue;
		}

		float score = 0.0f;
		int reachNum = aasAreaSettings[i].firstreachablearea;
		const int endReachNum = reachNum + aasAreaSettings[i].numreachableareas;
		// Check reachabilities from this area to other areas.
		// An area score depends of how many reachabilities are outgoing from this area
		// and how good these reachabilities are for fleeing away safely.
		for(; reachNum != endReachNum; ++reachNum ) {
			const auto &reach = aasReach[reachNum];
			const int travelType = reach.traveltype & TRAVELTYPE_MASK;
			if( travelType == TRAVEL_WALK ) {
				score += 1.0f;
				continue;
			}
			if( travelType == TRAVEL_TELEPORT ) {
				// This is a royal opportunity to escape, give it a greatest score
				score += 10.0f;
				continue;
			}
			if( travelType == TRAVEL_WALKOFFLEDGE ) {
				if( DistanceSquared( reach.start, reach.end ) > 32 * 32 ) {
					// Give a minimal score in this case... we should not favor ledges
					score += 0.1f;
				} else {
					// Just a short step down
					score += 0.5;
				}
				continue;
			}
		}

		if( score ) {
			AddCandidateArea( i, score );
		}
	}

	// Sort areas so best (with higher connectivity score) are first
	std::sort( candidateAreas, candidateAreas + numCandidateAreas );
}

void TacticalSpotsBuilder::FindCandidatePoints() {
	FindCandidateAreas();

	const auto *aasAreas = AiAasWorld::Instance()->Areas();

	// Add areas centers first.
	// Note: area centers gained priority over boundaries once we started sorting areas by score.
	// Make sure we really select an point in a high-priority area and not in some boundary one.
	// The old approach that favoured area face points used to favor ledges too (that should be avoided).
	for( int i = 0; i < numCandidateAreas; i++ ) {
		const auto &area = aasAreas[i];
		Vec3 areaPoint( area.center );
		areaPoint.Z() = area.mins[2];
		AddCandidatePoint( areaPoint );
	}

	for( int i = 0; i < numCandidateAreas; i++ ) {
		AddAreaFacePoints( candidateAreas[i].areaNum );
	}

	// Try adding intermediate points for large areas
	for( int i = 0; i < numCandidateAreas; i++ ) {
		const auto &area = aasAreas[i];
		const int step = 108;
		const int xSteps = (int)( area.maxs[0] - area.mins[0] ) / step;
		const int ySteps = (int)( area.maxs[1] - area.mins[0] ) / step;
		if( xSteps < 2 || ySteps < 2 ) {
			continue;
		}

		Vec3 areaPoint( area.center );
		areaPoint.Z() = area.mins[2];
		for( int xi = 0; xi < xSteps; ++xi ) {
			for( int yi = 0; yi < ySteps; ++yi ) {
				areaPoint.X() = area.mins[0] + xi * step;
				areaPoint.Y() = area.mins[1] + yi * step;
				AddCandidatePoint( areaPoint );
			}
		}
	}
}

void TacticalSpotsBuilder::AddAreaFacePoints( int areaNum ) {
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasFaceIndex = aasWorld->FaceIndex();
	const auto *aasFaces = aasWorld->Faces();
	const auto *aasEdgeIndex = aasWorld->EdgeIndex();
	const auto *aasEdges = aasWorld->Edges();
	const auto *aasPlanes = aasWorld->Planes();
	const auto *aasVertices = aasWorld->Vertexes();

	const float maxZ = gridBuilder.WorldMaxs()[2] + 999.0f;

	const auto &area = aasWorld->Areas()[areaNum];
	for( int faceIndexNum = area.firstface; faceIndexNum < area.firstface + area.numfaces; ++faceIndexNum ) {
		const auto &face = aasFaces[ abs( aasFaceIndex[faceIndexNum] ) ];
		// Skip boundaries with solid (some of areas split by face is solid)
		if( !( face.frontarea & face.backarea ) ) {
			continue;
		}
		// Reject non-vertical faces
		const auto &plane = aasPlanes[face.planenum];
		if( fabsf( plane.normal[2] ) > 0.1f ) {
			continue;
		}
		// Find the lowest face edge
		float minZ = maxZ;
		int lowestEdgeNum = -1;
		for ( int edgeIndexNum = face.firstedge; edgeIndexNum < face.firstedge + face.numedges; ++edgeIndexNum ) {
			const int edgeNum = abs( aasEdgeIndex[edgeIndexNum ] );
			const auto &edge = aasEdges[ edgeNum ];
			const float *v1 = aasVertices[edge.v[0]];
			const float *v2 = aasVertices[edge.v[1]];
			// Reject vertical edges
			if( fabsf( v1[2] - v2[2] ) > 1.0f ) {
				continue;
			}
			// Reject edges that are above the lowest known one
			if( v1[2] >= minZ ) {
				continue;
			}

			minZ = v1[2];
			lowestEdgeNum = edgeNum;
		}

		if( lowestEdgeNum < 0 ) {
			continue;
		}

		const auto &edge = aasEdges[ lowestEdgeNum ];
		Vec3 edgePoint( aasVertices[edge.v[0]] );
		edgePoint += aasVertices[edge.v[1]];
		edgePoint *= 0.5f;
		AddCandidatePoint( edgePoint );
	}
}

static const vec3_t spotMins = { -24, -24, 0 };
static const vec3_t spotMaxs = { +24, +24, 64 };

static const vec3_t testedMins = { -36, -36, 0 };
static const vec3_t testedMaxs = { +36, +36, 72 };

void TacticalSpotsBuilder::TryAddSpotFromPoint( const Vec3 &point ) {
	TacticalSpotsRegistry::OriginParams originParams( point.Data(), 1024.0f, AiAasRouteCache::Shared() );

	uint16_t insideSpotNum;
	const SpotsQueryVector &nearbySpots = gridBuilder.FindSpotsInRadius( originParams, &insideSpotNum );

	int bestNumVisNearbySpots = -1;
	int bestSpotAreaNum = 0;
	Vec3 bestSpotOrigin( point );
	for( int i = -3; i < 3; ++i ) {
		for( int j = -3; j < 3; ++j ) {
			Vec3 spotOrigin( point );
			spotOrigin.X() += i * 24.0f;
			spotOrigin.Y() += j * 24.0f;
			// Usually spots are picked on ground, add an offset to avoid being qualified as starting in solid
			spotOrigin.Z() += 1.0f;
			int numVisNearbySpots, areaNum;
			if( IsAGoodSpotPosition( nearbySpots, spotOrigin.Data(), &numVisNearbySpots, &areaNum ) ) {
				if( numVisNearbySpots > bestNumVisNearbySpots ) {
					bestNumVisNearbySpots = numVisNearbySpots;
					bestSpotOrigin = spotOrigin;
					bestSpotAreaNum = areaNum;
				}
			}
		}
	}

	if( bestNumVisNearbySpots < 0 ) {
		return;
	}

	if( numSpots >= TacticalSpotsRegistry::MAX_SPOTS ) {
		AI_FailWith( "TacticalSpotsBuilder::Build()", "Too many spots (> MAX_SPOTS) have been produced" );
	}

	TacticalSpot *newSpot = AllocSpot();
	bestSpotOrigin.CopyTo( newSpot->origin );
	newSpot->aasAreaNum = bestSpotAreaNum;
	VectorCopy( spotMins, newSpot->absMins );
	VectorCopy( spotMaxs, newSpot->absMaxs );

	gridBuilder.AttachSpots( spots, (unsigned)numSpots );
	gridBuilder.AddSpot( bestSpotOrigin.Data(), (uint16_t)( numSpots - 1 ) );
}

static constexpr float SPOT_SQUARE_PROXIMITY_THRESHOLD = 108 * 108;

int TacticalSpotsBuilder::TestPointForGoodAreaNum( const vec3_t point ) {
	const auto *aasWorld = AiAasWorld::Instance();
	int areaNum = aasWorld->FindAreaNum( point );
	if( !areaNum ) {
		return 0;
	}

	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const auto &areaSettings = aasAreaSettings[areaNum];
	constexpr auto badContents = AREACONTENTS_LAVA | AREACONTENTS_SLIME | AREACONTENTS_DONOTENTER;
	if( !LooksLikeAGoodArea( areaSettings, badContents ) ) {
		return 0;
	}

	// Check whether there are "good" (walk/teleport) reachabilities to the area and from the area
	int numGoodReaches = 0;
	const int endReachNum = areaSettings.firstreachablearea + areaSettings.numreachableareas;
	const auto *aasReach = aasWorld->Reachabilities();
	for( int reachNum = areaSettings.firstreachablearea; reachNum != endReachNum; ++reachNum ) {
		const auto &reach = aasReach[reachNum];
		if( reach.traveltype != TRAVEL_WALK && reach.traveltype != TRAVEL_TELEPORT ) {
			continue;
		}
		const auto thatAreaSettings = aasAreaSettings[reach.areanum];
		if( !LooksLikeAGoodArea( thatAreaSettings, badContents ) ) {
			continue;
		}
		const int thatEndReachNum = thatAreaSettings.firstreachablearea + thatAreaSettings.numreachableareas;
		for( int thatReachNum = thatAreaSettings.firstreachablearea; thatReachNum != thatEndReachNum; ++thatReachNum ) {
			const auto revReach = aasReach[thatReachNum];
			if( revReach.areanum != areaNum ) {
				continue;
			}
			if( revReach.traveltype != TRAVEL_WALK && reach.traveltype != TRAVEL_TELEPORT ) {
				continue;
			}
			numGoodReaches++;
			if( numGoodReaches > 1 ) {
				return areaNum;
			}
		}
	}

	return 0;
}

bool TacticalSpotsBuilder::IsAGoodSpotPosition( const SpotsQueryVector &nearbySpots,
												vec3_t point,
												int *numVisNearbySpots,
												int *areaNum ) {
	trace_t trace;
	// Test whether there is a solid inside extended spot bounds
	SolidWorldTrace( &trace, point, point, testedMins, testedMaxs );
	if( trace.fraction != 1.0f || trace.startsolid ) {
		return false;
	}

	// Check whether there is really a ground below
	Vec3 groundedPoint( point );
	groundedPoint.Z() -= 32.0f;
	SolidWorldTrace( &trace, point, groundedPoint.Data() );
	if( trace.fraction == 1.0f || trace.startsolid || trace.allsolid ) {
		return false;
	}
	if( trace.contents & ( CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_DONOTENTER ) ) {
		return false;
	}
	if( !ISWALKABLEPLANE( &trace.plane ) ) {
		return false;
	}

	// Important: set point origin to an origin of a player standing here
	// This is an assumption of TacticalSpotsRegistry::FindClosestToTargetWalkableSpot()
	VectorCopy( trace.endpos, point );
	point[2] += -playerbox_stand_mins[2] + 1 + 4;

	// Also very important: round coordinates to 4 units as WorldState vars do.
	// Avoid being surprised by noticeable values mismatch.
	for( int i = 0; i < 3; ++i ) {
		point[i] = 4 * ( ( (int)point[i] ) / 4 );
	}

	// Operate on these local vars. Do not modify output params unless the result is successful.
	int numVisNearbySpots_ = 0;
	int areaNum_ = 0;

	if( !( areaNum_ = TestPointForGoodAreaNum( point ) ) ) {
		return false;
	}

	vec3_t traceMins, traceMaxs;
	TacticalSpotsRegistry::GetSpotsWalkabilityTraceBounds( traceMins, traceMaxs );

	// Test whether there are way too close visible spots.
	// Also compute overall spots visibility.
	for( auto spotNum: nearbySpots ) {
		const auto &spot = spots[spotNum];
		SolidWorldTrace( &trace, point, spot.origin, traceMins, traceMaxs );
		if( trace.fraction == 1.0f && !trace.startsolid ) {
			if( DistanceSquared( point, spot.origin ) < SPOT_SQUARE_PROXIMITY_THRESHOLD ) {
				return false;
			}
			numVisNearbySpots_++;
		}
	}

	*areaNum = areaNum_;
	*numVisNearbySpots = numVisNearbySpots_;
	return true;
}

template <typename T>
T *TacticalSpotsBuilder::AllocItem( T **items, int *numItems, int *itemsCapacity ) {
	if( *numItems < *itemsCapacity ) {
		return ( *items ) + ( *numItems )++;
	}
	if( *itemsCapacity < 1024 ) {
		*itemsCapacity = ( *itemsCapacity + 16 ) * 2;
	} else {
		*itemsCapacity = ( 3 * ( *itemsCapacity ) ) / 2;
	}
	T *newData = (T *)G_LevelMalloc( sizeof( T ) * ( *itemsCapacity ) );
	if( *items ) {
		memcpy( newData, *items, sizeof( T ) * ( *numItems ) );
		G_LevelFree( *items );
	}
	*items = newData;
	return ( *items ) + ( *numItems )++;
}

void TacticalSpotsBuilder::CopyTo( TacticalSpotsRegistry *registry ) {
	this->gridBuilder.CopyTo( &registry->spotsGrid );

	registry->spots = this->spots;
	this->spots = nullptr;

	registry->numSpots = (unsigned)this->numSpots;
	this->numSpots = 0;

	registry->spotVisibilityTable = this->spotVisibilityTable;
	this->spotVisibilityTable = nullptr;

	registry->spotTravelTimeTable = this->spotTravelTimeTable;
	this->spotTravelTimeTable = nullptr;

	registry->needsSavingPrecomputedData = true;
}

inline unsigned TacticalSpotsRegistry::BaseSpotsGrid::PointGridCellNum( const vec3_t point ) const {
	vec3_t offset;
	VectorSubtract( point, worldMins, offset );

	unsigned i = (unsigned)( offset[0] / gridCellSize[0] );
	unsigned j = (unsigned)( offset[1] / gridCellSize[1] );
	unsigned k = (unsigned)( offset[2] / gridCellSize[2] );

	return i * ( gridNumCells[1] * gridNumCells[2] ) + j * gridNumCells[2] + k;
}

void TacticalSpotsRegistry::BaseSpotsGrid::SetupGridParams() {
	// Get world bounds
	trap_CM_InlineModelBounds( trap_CM_InlineModel( 0 ), worldMins, worldMaxs );

	vec3_t worldDims;
	VectorSubtract( worldMaxs, worldMins, worldDims );

	for( int i = 0; i < 3; ++i ) {
		unsigned roundedDimension = (unsigned)worldDims[i];
		if( roundedDimension > MIN_GRID_CELL_SIDE * MAX_GRID_DIMENSION ) {
			gridCellSize[i] = roundedDimension / MAX_GRID_DIMENSION;
			gridNumCells[i] = MAX_GRID_DIMENSION;
		} else {
			gridCellSize[i] = MIN_GRID_CELL_SIDE;
			gridNumCells[i] = ( roundedDimension / MIN_GRID_CELL_SIDE ) + 1;
		}
	}
}

const SpotsQueryVector &TacticalSpotsRegistry::BaseSpotsGrid::FindSpotsInRadius( const OriginParams &originParams,
																				 uint16_t *insideSpotNum ) const {

	vec3_t boxMins, boxMaxs;
	VectorCopy( originParams.origin, boxMins );
	VectorCopy( originParams.origin, boxMaxs );
	const float radius = originParams.searchRadius;
	vec3_t radiusBounds = { radius, radius, radius };
	VectorSubtract( boxMins, radiusBounds, boxMins );
	VectorAdd( boxMaxs, radiusBounds, boxMaxs );

	// Find loop bounds for each dimension
	unsigned minCellDimIndex[3];
	unsigned maxCellDimIndex[3];
	for( int i = 0; i < 3; ++i ) {
		// Clamp box bounds by world bounds
		clamp( boxMins[i], worldMins[i], worldMaxs[i] );
		clamp( boxMaxs[i], worldMins[i], worldMaxs[i] );

		// Convert box bounds to relative
		boxMins[i] -= worldMins[i];
		boxMaxs[i] -= worldMins[i];

		minCellDimIndex[i] = (unsigned)( boxMins[i] / gridCellSize[i] );
		maxCellDimIndex[i] = (unsigned)( boxMaxs[i] / gridCellSize[i] );
	}

	*insideSpotNum = std::numeric_limits<uint16_t>::max();

	SpotsQueryVector &result = parent->temporariesAllocator.GetCleanQueryVector();

	// Copy to locals for faster access
	const Vec3 searchOrigin( originParams.origin );
	const float squareRadius = originParams.searchRadius * originParams.searchRadius;
	// For each index for X dimension in the query bounding box
	for( unsigned i = minCellDimIndex[0]; i <= maxCellDimIndex[0]; ++i ) {
		unsigned indexIOffset = i * ( gridNumCells[1] * gridNumCells[2] );
		// For each index for Y dimension in the query bounding box
		for( unsigned j = minCellDimIndex[1]; j <= maxCellDimIndex[1]; ++j ) {
			unsigned indexJOffset = j * gridNumCells[2];
			// For each index for Z dimension in the query bounding box
			for( unsigned k = minCellDimIndex[2]; k <= maxCellDimIndex[2]; ++k ) {
				// The cell is at this offset from the beginning of a linear cells array
				unsigned cellIndex = indexIOffset + indexJOffset + k;
				uint16_t numGridSpots;
				uint16_t *spotsList = GetCellSpotsList( cellIndex, &numGridSpots );
				// For each spot number fetch a spot and test against the problem params
				for( uint16_t spotNumIndex = 0; spotNumIndex < numGridSpots; ++spotNumIndex ) {
					uint16_t spotNum = spotsList[spotNumIndex];
					const TacticalSpot &spot = spots[spotNum];
					if( DistanceSquared( spot.origin, searchOrigin.Data() ) < squareRadius ) {
						result.push_back( spotNum );
						// Test whether search origin is inside the spot
						if( searchOrigin.X() < spot.absMins[0] || searchOrigin.X() > spot.absMaxs[0] ) {
							continue;
						}
						if( searchOrigin.Y() < spot.absMins[1] || searchOrigin.Y() > spot.absMaxs[1] ) {
							continue;
						}
						if( searchOrigin.Z() < spot.absMins[2] || searchOrigin.Z() > spot.absMaxs[2] ) {
							continue;
						}
						// Spots should not overlap. But if spots overlap, last matching spot will be returned
						*insideSpotNum = spotNum;
					}
				}
			}
		}
	}

	return result;
}

TacticalSpotsRegistry::PrecomputedSpotsGrid::~PrecomputedSpotsGrid() {
	if( gridListOffsets ) {
		G_LevelFree( gridListOffsets );
	}
	if( gridSpotsLists ) {
		G_LevelFree( gridSpotsLists );
	}
}

const SpotsQueryVector &TacticalSpotsRegistry::PrecomputedSpotsGrid::FindSpotsInRadius( const OriginParams &originParams,
																						uint16_t *insideSpotNum ) const {
	if( !IsLoaded() ) {
		AI_FailWith( "PrecomputedSpotsGrid::FindSpotsInRadius()", "The grid has not been loaded\n" );
	}

	// We hope a compiler is able to substitute the parent implementation here
	// and use devirtualized overridden GetCellsSpotsLists() calls.
	return BaseSpotsGrid::FindSpotsInRadius( originParams, insideSpotNum );
}

uint16_t *TacticalSpotsRegistry::PrecomputedSpotsGrid::GetCellSpotsList( unsigned gridCellNum,
																		 uint16_t *numCellSpots ) const {
	unsigned gridListOffset = gridListOffsets[gridCellNum];
	uint16_t *spotsList = gridSpotsLists + gridListOffset;
	// Spots list head contains the count of spots (spot numbers)
	*numCellSpots = spotsList[0];
	// The spots data follow the head.
	return spotsList + 1;
}

TacticalSpotsRegistry::SpotsGridBuilder::SpotsGridBuilder( TacticalSpotsRegistry *parent_ )
	: BaseSpotsGrid( parent_ ) {
	SetupGridParams();

	gridSpotsArrays = ( GridSpotsArray ** )( G_LevelMalloc( NumGridCells() * sizeof( GridSpotsArray * ) ) );
}

TacticalSpotsRegistry::SpotsGridBuilder::~SpotsGridBuilder() {
	if( gridSpotsArrays ) {
		for( unsigned i = 0, end = NumGridCells(); i < end; ++i ) {
			if( gridSpotsArrays[i] ) {
				gridSpotsArrays[i]->~GridSpotsArray();
				G_LevelFree( gridSpotsArrays[i] );
			}
		}
		G_LevelFree( gridSpotsArrays );
	}
}

uint16_t *TacticalSpotsRegistry::SpotsGridBuilder::GetCellSpotsList( unsigned gridCellNum,
																	 uint16_t *numCellSpots ) const {
	if( GridSpotsArray *array = gridSpotsArrays[gridCellNum] ) {
		*numCellSpots = array->size;
		return array->data;
	}
	*numCellSpots = 0;
	return nullptr;
}

void TacticalSpotsRegistry::SpotsGridBuilder::AddSpot( const vec3_t origin, uint16_t spotNum ) {
	unsigned gridCellNum = PointGridCellNum( origin );
	AddSpotToGridList( gridCellNum, spotNum );
}

void TacticalSpotsRegistry::SpotsGridBuilder::AddSpotToGridList( unsigned gridCellNum, uint16_t spotNum ) {
	GridSpotsArray *array = gridSpotsArrays[gridCellNum];
	// Put the likely case first
	if( array ) {
		array->AddSpot( spotNum );
		return;
	}

	array = new( G_LevelMalloc( sizeof( GridSpotsArray ) ) )GridSpotsArray;
	array->AddSpot( spotNum );
	gridSpotsArrays[gridCellNum] = array;
}

void TacticalSpotsRegistry::SpotsGridBuilder::GridSpotsArray::AddSpot( uint16_t spotNum ) {
	if( this->size < this->capacity ) {
		this->data[this->size++] = spotNum;
		return;
	}

	uint16_t *newData = (uint16_t *)G_LevelMalloc( sizeof( uint16_t ) * ( this->capacity + 16 ) );
	memcpy( newData, this->data, sizeof( uint16_t ) * this->size );
	if( this->data != internalBuffer ) {
		G_LevelFree( this->data );
	}

	if( this->capacity > MAX_SPOTS_PER_QUERY ) {
		AI_FailWith( "GridSpotsArray::AddSpot()", "Too many spots per a grid cell" );
	}

	this->data = newData;
	this->size = this->capacity;
	this->capacity += 16;
	this->data[this->size++] = spotNum;
}

void TacticalSpotsRegistry::SpotsGridBuilder::CopyTo( PrecomputedSpotsGrid *precomputedGrid ) {
	if( !this->spots ) {
		AI_FailWith( "SpotsGridBuilder::CopyTo()", "Spots have not been attached\n" );
	}

	VectorCopy( this->worldMins, precomputedGrid->worldMins );
	VectorCopy( this->worldMaxs, precomputedGrid->worldMaxs );
	VectorCopy( this->gridNumCells, precomputedGrid->gridNumCells );
	VectorCopy( this->gridCellSize, precomputedGrid->gridCellSize );

	// Should not really happen if it is used as intended,
	// but calling CopyTo() with an initialized grid as an argument is legal
	if( precomputedGrid->gridListOffsets ) {
		G_LevelFree( precomputedGrid->gridListOffsets );
	}
	if( precomputedGrid->gridSpotsLists ) {
		G_LevelFree( precomputedGrid->gridSpotsLists );
	}

	precomputedGrid->numSpots = this->numSpots;
	precomputedGrid->spots = this->spots;

	unsigned totalNumCells = NumGridCells();
	precomputedGrid->gridListOffsets = (uint32_t *)G_LevelMalloc( sizeof( uint32_t ) * totalNumCells );
	precomputedGrid->gridSpotsLists = (uint16_t *)G_LevelMalloc( sizeof( uint16_t ) * ( totalNumCells + numSpots ) );

	uint16_t *listPtr = precomputedGrid->gridSpotsLists;
	// For each cell of all possible cells
	for( unsigned cellNum = 0; cellNum < totalNumCells; ++cellNum ) {
		// Store offset of the cell spots list
		precomputedGrid->gridListOffsets[cellNum] = (unsigned)( listPtr - precomputedGrid->gridSpotsLists );
		// Use a reference to cell spots list head that contains number of spots in the cell
		uint16_t *listSize = &listPtr[0];
		*listSize = 0;
		// Skip list head
		++listPtr;
		if( GridSpotsArray *spotsArray = gridSpotsArrays[cellNum] ) {
			if( spotsArray->size > MAX_SPOTS_PER_QUERY ) {
				AI_FailWith( "SpotsGridBuilder::CopyTo()", "Too many spots in cell %d/%d\n", cellNum, totalNumCells );
			}
			*listSize = spotsArray->size;
			memcpy( listPtr, spotsArray->data, sizeof( spotsArray->data[0] ) * spotsArray->size );
			listPtr += *listSize;
		}
		if( (unsigned)(listPtr - precomputedGrid->gridSpotsLists) > totalNumCells + numSpots ) {
			AI_FailWith( "SpotsGridBuilder::CopyTo()", "List ptr went out of bounds\n" );
		}
	}
}

typedef TacticalSpotsRegistry::SpotsAndScoreVector SpotsAndScoreVector;

SpotsAndScoreVector &TacticalSpotsRegistry::TemporariesAllocator::GetNextCleanSpotsAndScoreVector() {
	if( freeHead ) {
		auto &result = freeHead->data;
		auto *nextFree = freeHead->next;
		freeHead->next = usedHead;
		usedHead = freeHead;
		freeHead = nextFree;
		result.clear();
		return result;
	}

	auto *newEntry = new( G_Malloc( sizeof( SpotsAndScoreCacheEntry ) ) )SpotsAndScoreCacheEntry;
	newEntry->next = usedHead;
	usedHead = newEntry;
	return usedHead->data;
}

void TacticalSpotsRegistry::TemporariesAllocator::Release() {
	// Copy from used to free list... todo... figure out how to do it in a single step
	for( auto *entry = usedHead; entry; entry = entry->next ) {
		entry->next = freeHead;
		freeHead = entry;
	}

	usedHead = nullptr;
	// TODO: do a cleanup of spots query vector/excluded spots mask?
}

TacticalSpotsRegistry::TemporariesAllocator::~TemporariesAllocator() {
	if( usedHead ) {
		AI_FailWith( "TacticalSpotsRegistry::TemporariesAllocator::~()", "A user has not released resources" );
	}

	SpotsAndScoreCacheEntry *nextEntry;
	for( auto *entry = usedHead; entry; entry = nextEntry ) {
		nextEntry = entry->next;
		entry->~SpotsAndScoreCacheEntry();
		G_Free( entry );
	}

	query->~StaticVector();
	G_Free( query );
	G_Free( excludedSpotsMask );
}