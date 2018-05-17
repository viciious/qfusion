#include "TacticalSpotsRegistry.h"
#include "../ai_precomputed_file_handler.h"
#include "../bot.h"

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

	int *candidateAreas;
	int numCandidateAreas;
	int candidateAreasCapacity;

	Vec3 *candidatePoints;
	int numCandidatePoints;
	int candidatePointsCapacity;

	TacticalSpot *spots;
	int numSpots;
	int spotsCapacity;

	uint8_t *spotVisibilityTable;
	uint16_t *spotTravelTimeTable;

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
	void AddCandidateArea( int areaNum ) {
		AddItem( areaNum, &candidateAreas, &numCandidateAreas, &candidateAreasCapacity );
	}

	void FindCandidatePoints();
	void AddAreaFacePoints( int areaNum );
	void AddCandidatePoint( const Vec3 &point ) {
		AddItem( point, &candidatePoints, &numCandidatePoints, &candidatePointsCapacity );
	}

	void TryAddSpotFromPoint( const Vec3 &point );
	int TestPointForGoodAreaNum( const vec3_t point );
	bool IsGoodSpotPosition( vec3_t point,
							 const uint16_t *nearbySpotNums,
							 uint16_t numNearbySpots,
							 int *numVisNearbySpots,
							 int *areaNum );

	TacticalSpot *AllocSpot() {
		return AllocItem( &spots, &numSpots, &spotsCapacity );
	}

	void PickTacticalSpots();
	void ComputeMutualSpotsVisibility();
	void ComputeMutualSpotsReachability();
public:
	TacticalSpotsBuilder();
	~TacticalSpotsBuilder();

	bool Build();

	void CopyTo( TacticalSpotsRegistry *registry );
};

bool TacticalSpotsRegistry::Load( const char *mapname ) {
	if( TryLoadPrecomputedData( mapname ) ) {
		return true;
	}

	TacticalSpotsBuilder builder;
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

TacticalSpotsBuilder::TacticalSpotsBuilder()
	: candidateAreas( nullptr ),
	numCandidateAreas( 0 ),
	candidateAreasCapacity( 0 ),
	candidatePoints( nullptr ),
	numCandidatePoints( 0 ),
	candidatePointsCapacity( 0 ),
	spots( nullptr ),
	numSpots( 0 ),
	spotsCapacity( 0 ),
	spotVisibilityTable( nullptr ),
	spotTravelTimeTable( nullptr ) {}

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
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const int numAasAreas = aasWorld->NumAreas();

	const auto badContents = AREACONTENTS_WATER | AREACONTENTS_LAVA | AREACONTENTS_SLIME | AREACONTENTS_DONOTENTER;
	for( int i = 1; i < numAasAreas; ++i ) {
		if( LooksLikeAGoodArea( aasAreaSettings[i], badContents ) ) {
			AddCandidateArea( i );
		}
	}
}

void TacticalSpotsBuilder::FindCandidatePoints() {
	FindCandidateAreas();

	const auto *aasAreas = AiAasWorld::Instance()->Areas();
	// Add boundary area points first, they should have a priority over centers
	for( int i = 0; i < numCandidateAreas; i++ ) {
		AddAreaFacePoints( candidateAreas[i] );
	}

	// Add areas centers and (maybe) intermediate points
	for( int i = 0; i < numCandidateAreas; i++ ) {
		const auto &area = aasAreas[i];
		Vec3 areaPoint( area.center );
		areaPoint.Z() = area.mins[2];
		AddCandidatePoint( areaPoint );
		// Add intermediate points for large areas
		const int step = 108;
		const int xSteps = (int)( area.maxs[0] - area.mins[0] ) / step;
		const int ySteps = (int)( area.maxs[1] - area.mins[0] ) / step;
		if( xSteps < 2 || ySteps < 2 ) {
			continue;
		}
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
	uint16_t nearbySpotNums[TacticalSpotsRegistry::MAX_SPOTS_PER_QUERY];
	uint16_t insideSpotNum;
	TacticalSpotsRegistry::OriginParams originParams( point.Data(), 1024.0f, AiAasRouteCache::Shared() );
	const uint16_t numNearbySpots = gridBuilder.FindSpotsInRadius( originParams, nearbySpotNums, &insideSpotNum );

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
			if( IsGoodSpotPosition( spotOrigin.Data(), nearbySpotNums, numNearbySpots, &numVisNearbySpots, &areaNum ) ) {
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

	if( numSpots >= std::numeric_limits<uint16_t>::max() ) {
		AI_FailWith( "TacticalSpotsBuilder::Build()", "Too many spots (>2^16) have been produced" );
	}

	TacticalSpot *newSpot = AllocSpot();
	bestSpotOrigin.CopyTo( newSpot->origin );
	newSpot->aasAreaNum = bestSpotAreaNum;
	VectorCopy( spotMins, newSpot->absMins );
	VectorCopy( spotMaxs, newSpot->absMaxs );

	gridBuilder.AttachSpots( spots, (unsigned)numSpots );
	gridBuilder.AddSpot( bestSpotOrigin.Data(), (uint16_t)( numSpots - 1 ) );
}

static constexpr float SPOT_SQUARE_PROXIMITY_THRESHOLD = ( 128 + 32 ) * ( 128 + 32 );

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

bool TacticalSpotsBuilder::IsGoodSpotPosition( vec3_t point,
											   const uint16_t *nearbySpotNums,
											   uint16_t numNearbySpots,
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
	for( unsigned i = 0; i < numNearbySpots; ++i ) {
		const auto &spot = spots[nearbySpotNums[i]];
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

uint16_t TacticalSpotsRegistry::BaseSpotsGrid::FindSpotsInRadius( const OriginParams &originParams,
																  unsigned short *spotNums,
																  unsigned short *insideSpotNum ) const {

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

	// Copy to locals for faster access
	const Vec3 searchOrigin( originParams.origin );
	const float squareRadius = originParams.searchRadius * originParams.searchRadius;
	uint16_t numSpotsInRadius = 0;
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
						if( numSpotsInRadius >= MAX_SPOTS_PER_QUERY ) {
							AI_FailWith( "FindSpotsInRadius()", "Too many spots in query result\n" );
						}
						spotNums[numSpotsInRadius++] = spotNum;
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

	return numSpotsInRadius;
}

TacticalSpotsRegistry::PrecomputedSpotsGrid::~PrecomputedSpotsGrid() {
	if( gridListOffsets ) {
		G_LevelFree( gridListOffsets );
	}
	if( gridSpotsLists ) {
		G_LevelFree( gridSpotsLists );
	}
}

uint16_t TacticalSpotsRegistry::PrecomputedSpotsGrid::FindSpotsInRadius( const OriginParams &originParams,
																		 uint16_t *spotNums,
																		 uint16_t *insideSpotNum ) const {
	if( !IsLoaded() ) {
		AI_FailWith( "PrecomputedSpotsGrid::FindSpotsInRadius()", "The grid has not been loaded\n" );
	}

	// We hope a compiler is able to substitute the parent implementation here
	// and use devirtualized overridden GetCellsSpotsLists() calls.
	return BaseSpotsGrid::FindSpotsInRadius( originParams, spotNums, insideSpotNum );
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

TacticalSpotsRegistry::SpotsGridBuilder::SpotsGridBuilder() {
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

int TacticalSpotsRegistry::CopyResults( const SpotAndScore *spotsBegin,
										const SpotAndScore *spotsEnd,
										const CommonProblemParams &problemParams,
										vec3_t *spotOrigins, int maxSpots ) const {
	const unsigned resultsSize = (unsigned)( spotsEnd - spotsBegin );
	if( maxSpots == 0 || resultsSize == 0 ) {
		return 0;
	}

	// Its a common case so give it an optimized branch
	if( maxSpots == 1 ) {
		VectorCopy( spots[spotsBegin->spotNum].origin, spotOrigins[0] );
		return 1;
	}

	const float spotProximityThreshold = problemParams.spotProximityThreshold;

	bool isSpotExcluded[CandidateSpots::capacity()];
	memset( isSpotExcluded, 0, sizeof( bool ) * CandidateSpots::capacity() );

	int numSpots_ = 0;
	unsigned keptSpotIndex = 0;
	for(;; ) {
		if( keptSpotIndex >= resultsSize ) {
			return numSpots_;
		}
		if( numSpots_ >= maxSpots ) {
			return numSpots_;
		}

		// Spots are sorted by score.
		// So first spot not marked as excluded yet has higher priority and should be kept.

		const TacticalSpot &keptSpot = spots[spotsBegin[keptSpotIndex].spotNum];
		VectorCopy( keptSpot.origin, spotOrigins[numSpots_] );
		++numSpots_;

		// Exclude all next (i.e. lower score) spots that are too close to the kept spot.

		unsigned testedSpotIndex = keptSpotIndex + 1;
		keptSpotIndex = 999999;
		for(; testedSpotIndex < resultsSize; testedSpotIndex++ ) {
			// Skip already excluded areas
			if( isSpotExcluded[testedSpotIndex] ) {
				continue;
			}

			const TacticalSpot &testedSpot = spots[spotsBegin[testedSpotIndex].spotNum];
			if( DistanceSquared( keptSpot.origin, testedSpot.origin ) < spotProximityThreshold * spotProximityThreshold ) {
				isSpotExcluded[testedSpotIndex] = true;
			} else if( keptSpotIndex > testedSpotIndex ) {
				keptSpotIndex = testedSpotIndex;
			}
		}
	}
}

inline float ComputeDistanceFactor( float distance, float weightFalloffDistanceRatio, float searchRadius ) {
	float weightFalloffRadius = weightFalloffDistanceRatio * searchRadius;
	if( distance < weightFalloffRadius ) {
		return distance / weightFalloffRadius;
	}

	return 1.0f - ( ( distance - weightFalloffRadius ) / ( 0.000001f + searchRadius - weightFalloffRadius ) );
}

inline float ComputeDistanceFactor( const vec3_t v1, const vec3_t v2, float weightFalloffDistanceRatio, float searchRadius ) {
	float squareDistance = DistanceSquared( v1, v2 );
	float distance = 1.0f;
	if( squareDistance >= 1.0f ) {
		distance = 1.0f / Q_RSqrt( squareDistance );
	}

	return ComputeDistanceFactor( distance, weightFalloffDistanceRatio, searchRadius );
}

// Units of travelTime and maxFeasibleTravelTime must match!
inline float ComputeTravelTimeFactor( int travelTime, float maxFeasibleTravelTime ) {
	float factor = 1.0f - BoundedFraction( travelTime, maxFeasibleTravelTime );
	return 1.0f / Q_RSqrt( 0.0001f + factor );
}

inline float ApplyFactor( float value, float factor, float factorInfluence ) {
	float keptPart = value * ( 1.0f - factorInfluence );
	float modifiedPart = value * factor * factorInfluence;
	return keptPart + modifiedPart;
}

int TacticalSpotsRegistry::FindPositionalAdvantageSpots( const OriginParams &originParams,
														 const AdvantageProblemParams &problemParams,
														 vec3_t *spotOrigins, int maxSpots ) const {
	uint16_t boundsSpots[MAX_SPOTS_PER_QUERY];
	uint16_t insideSpotNum;
	uint16_t numSpotsInBounds = FindSpotsInRadius( originParams, boundsSpots, &insideSpotNum );

	CandidateSpots candidateSpots;
	SelectCandidateSpots( originParams, problemParams, boundsSpots, numSpotsInBounds, candidateSpots );

	ReachCheckedSpots reachCheckedSpots;
	if( problemParams.checkToAndBackReachability ) {
		CheckSpotsReachFromOriginAndBack( originParams, problemParams, candidateSpots,  insideSpotNum, reachCheckedSpots );
	} else {
		CheckSpotsReachFromOrigin( originParams, problemParams, candidateSpots, insideSpotNum, reachCheckedSpots );
	}

	TraceCheckedSpots traceCheckedSpots;
	CheckSpotsVisibleOriginTrace( originParams, problemParams, reachCheckedSpots, traceCheckedSpots );

	SortByVisAndOtherFactors( originParams, problemParams, traceCheckedSpots );

	return CopyResults( traceCheckedSpots.begin(), traceCheckedSpots.end(), problemParams, spotOrigins, maxSpots );
}

void TacticalSpotsRegistry::SelectCandidateSpots( const OriginParams &originParams,
												  const CommonProblemParams &problemParams,
												  const uint16_t *spotNums,
												  uint16_t numSpots_, CandidateSpots &result ) const {
	const float minHeightAdvantageOverOrigin = problemParams.minHeightAdvantageOverOrigin;
	const float heightOverOriginInfluence = problemParams.heightOverOriginInfluence;
	const float searchRadius = originParams.searchRadius;
	const float originZ = originParams.origin[2];
	// Copy to stack for faster access
	Vec3 origin( originParams.origin );

	for( unsigned i = 0; i < numSpots_ && result.size() < result.capacity(); ++i ) {
		const TacticalSpot &spot = spots[spotNums[i]];

		float heightOverOrigin = spot.absMins[2] - originZ;
		if( heightOverOrigin < minHeightAdvantageOverOrigin ) {
			continue;
		}

		float squareDistanceToOrigin = DistanceSquared( origin.Data(), spot.origin );
		if( squareDistanceToOrigin > searchRadius * searchRadius ) {
			continue;
		}

		float score = 1.0f;
		float factor = BoundedFraction( heightOverOrigin - minHeightAdvantageOverOrigin, searchRadius );
		score = ApplyFactor( score, factor, heightOverOriginInfluence );

		result.push_back( SpotAndScore( spotNums[i], score ) );
	}

	// Sort result so best score areas are first
	std::sort( result.begin(), result.end() );
}

void TacticalSpotsRegistry::SelectCandidateSpots( const OriginParams &originParams,
												  const AdvantageProblemParams &problemParams,
												  const uint16_t *spotNums,
												  uint16_t numSpots_, CandidateSpots &result ) const {
	const float minHeightAdvantageOverOrigin = problemParams.minHeightAdvantageOverOrigin;
	const float minHeightAdvantageOverEntity = problemParams.minHeightAdvantageOverEntity;
	const float heightOverOriginInfluence = problemParams.heightOverOriginInfluence;
	const float heightOverEntityInfluence = problemParams.heightOverEntityInfluence;
	const float minSquareDistanceToEntity = problemParams.minSpotDistanceToEntity * problemParams.minSpotDistanceToEntity;
	const float maxSquareDistanceToEntity = problemParams.maxSpotDistanceToEntity * problemParams.maxSpotDistanceToEntity;
	const float searchRadius = originParams.searchRadius;
	const float originZ = originParams.origin[2];
	const float entityZ = problemParams.keepVisibleOrigin[2];
	// Copy to stack for faster access
	Vec3 origin( originParams.origin );
	Vec3 entityOrigin( problemParams.keepVisibleOrigin );

	for( unsigned i = 0; i < numSpots_ && result.size() < result.capacity(); ++i ) {
		const TacticalSpot &spot = spots[spotNums[i]];

		float heightOverOrigin = spot.absMins[2] - originZ;
		if( heightOverOrigin < minHeightAdvantageOverOrigin ) {
			continue;
		}

		float heightOverEntity = spot.absMins[2] - entityZ;
		if( heightOverEntity < minHeightAdvantageOverEntity ) {
			continue;
		}

		float squareDistanceToOrigin = DistanceSquared( origin.Data(), spot.origin );
		if( squareDistanceToOrigin > searchRadius * searchRadius ) {
			continue;
		}

		float squareDistanceToEntity = DistanceSquared( entityOrigin.Data(), spot.origin );
		if( squareDistanceToEntity < minSquareDistanceToEntity ) {
			continue;
		}
		if( squareDistanceToEntity > maxSquareDistanceToEntity ) {
			continue;
		}

		float score = 1.0f;
		float factor;
		factor = BoundedFraction( heightOverOrigin - minHeightAdvantageOverOrigin, searchRadius );
		score = ApplyFactor( score, factor, heightOverOriginInfluence );
		factor = BoundedFraction( heightOverEntity - minHeightAdvantageOverEntity, searchRadius );
		score = ApplyFactor( score, factor, heightOverEntityInfluence );

		result.push_back( SpotAndScore( spotNums[i], score ) );
	}

	// Sort result so best score areas are first
	std::sort( result.begin(), result.end() );
}

void TacticalSpotsRegistry::CheckSpotsReachFromOrigin( const OriginParams &originParams,
													   const CommonProblemParams &problemParams,
													   const CandidateSpots &candidateSpots,
													   uint16_t insideSpotNum,
													   ReachCheckedSpots &result ) const {
	AiAasRouteCache *routeCache = originParams.routeCache;
	const int originAreaNum = originParams.originAreaNum;
	const float *origin = originParams.origin;
	const float searchRadius = originParams.searchRadius;
	// AAS uses travel time in centiseconds
	const int maxFeasibleTravelTimeCentis = problemParams.maxFeasibleTravelTimeMillis / 10;
	const float weightFalloffDistanceRatio = problemParams.originWeightFalloffDistanceRatio;
	const float distanceInfluence = problemParams.originDistanceInfluence;
	const float travelTimeInfluence = problemParams.travelTimeInfluence;

	// Do not more than result.capacity() iterations.
	// Some feasible areas in candidateAreas tai that pass test may be skipped,
	// but this is intended to reduce performance drops (do not more than result.capacity() pathfinder calls).
	if( insideSpotNum < MAX_SPOTS_PER_QUERY ) {
		const auto *travelTimeTable = this->spotTravelTimeTable;
		const auto tableRowOffset = insideSpotNum * this->numSpots;
		for( unsigned i = 0, end = std::min( candidateSpots.size(), result.capacity() ); i < end; ++i ) {
			const SpotAndScore &spotAndScore = candidateSpots[i];
			const TacticalSpot &spot = spots[spotAndScore.spotNum];
			// If zero, the spotNum spot is not reachable from insideSpotNum
			int tableTravelTime = travelTimeTable[tableRowOffset + spotAndScore.spotNum];
			if( !tableTravelTime || tableTravelTime > maxFeasibleTravelTimeCentis ) {
				continue;
			}

			// Get an actual travel time (non-zero table value does not guarantee reachability)
			int travelTime = routeCache->TravelTimeToGoalArea( originAreaNum, spot.aasAreaNum, Bot::ALLOWED_TRAVEL_FLAGS );
			if( !travelTime || travelTime > maxFeasibleTravelTimeCentis ) {
				continue;
			}

			float travelTimeFactor = 1.0f - ComputeTravelTimeFactor( travelTime, maxFeasibleTravelTimeCentis );
			float distanceFactor = ComputeDistanceFactor( spot.origin, origin, weightFalloffDistanceRatio, searchRadius );
			float newScore = spotAndScore.score;
			newScore = ApplyFactor( newScore, distanceFactor, distanceInfluence );
			newScore = ApplyFactor( newScore, travelTimeFactor, travelTimeInfluence );
			result.push_back( SpotAndScore( spotAndScore.spotNum, newScore ) );
		}
	} else {
		for( unsigned i = 0, end = std::min( candidateSpots.size(), result.capacity() ); i < end; ++i ) {
			const SpotAndScore &spotAndScore = candidateSpots[i];
			const TacticalSpot &spot = spots[spotAndScore.spotNum];
			int travelTime = routeCache->TravelTimeToGoalArea( originAreaNum, spot.aasAreaNum, Bot::ALLOWED_TRAVEL_FLAGS );
			if( !travelTime || travelTime > maxFeasibleTravelTimeCentis ) {
				continue;
			}

			float travelTimeFactor = 1.0f - ComputeTravelTimeFactor( travelTime, maxFeasibleTravelTimeCentis );
			float distanceFactor = ComputeDistanceFactor( spot.origin, origin, weightFalloffDistanceRatio, searchRadius );
			float newScore = spotAndScore.score;
			newScore = ApplyFactor( newScore, distanceFactor, distanceInfluence );
			newScore = ApplyFactor( newScore, travelTimeFactor, travelTimeInfluence );
			result.push_back( SpotAndScore( spotAndScore.spotNum, newScore ) );
		}
	}

	// Sort result so best score areas are first
	std::sort( result.begin(), result.end() );
}

void TacticalSpotsRegistry::CheckSpotsReachFromOriginAndBack( const OriginParams &originParams,
															  const CommonProblemParams &problemParams,
															  const CandidateSpots &candidateSpots,
															  uint16_t insideSpotNum,
															  ReachCheckedSpots &result ) const {
	AiAasRouteCache *routeCache = originParams.routeCache;
	const int originAreaNum = originParams.originAreaNum;
	const float *origin = originParams.origin;
	const float searchRadius = originParams.searchRadius;
	// AAS uses time in centiseconds
	const int maxFeasibleTravelTimeCentis = problemParams.maxFeasibleTravelTimeMillis / 10;
	const float weightFalloffDistanceRatio = problemParams.originWeightFalloffDistanceRatio;
	const float distanceInfluence = problemParams.originDistanceInfluence;
	const float travelTimeInfluence = problemParams.travelTimeInfluence;

	// Do not more than result.capacity() iterations.
	// Some feasible areas in candidateAreas tai that pass test may be skipped,
	// but it is intended to reduce performance drops (do not more than 2 * result.capacity() pathfinder calls).
	if( insideSpotNum < MAX_SPOTS_PER_QUERY ) {
		const auto *travelTimeTable = this->spotTravelTimeTable;
		const auto numSpots_ = this->numSpots;
		for( unsigned i = 0, end = std::min( candidateSpots.size(), result.capacity() ); i < end; ++i ) {
			const SpotAndScore &spotAndScore = candidateSpots[i];
			const TacticalSpot &spot = spots[spotAndScore.spotNum];

			// If the table element i * numSpots_ + j is zero, j-th spot is not reachable from i-th one.
			int tableToTravelTime = travelTimeTable[insideSpotNum * numSpots_ + spotAndScore.spotNum];
			if( !tableToTravelTime ) {
				continue;
			}
			int tableBackTravelTime = travelTimeTable[spotAndScore.spotNum * numSpots_ + insideSpotNum];
			if( !tableBackTravelTime ) {
				continue;
			}
			if( tableToTravelTime + tableBackTravelTime > maxFeasibleTravelTimeCentis ) {
				continue;
			}

			// Get an actual travel time (non-zero table values do not guarantee reachability)
			int toTravelTime = routeCache->TravelTimeToGoalArea( originAreaNum, spot.aasAreaNum, Bot::ALLOWED_TRAVEL_FLAGS );
			// If `to` travel time is apriori greater than maximum allowed one (and thus the sum would be), reject early.
			if( !toTravelTime || toTravelTime > maxFeasibleTravelTimeCentis ) {
				continue;
			}
			int backTimeTravelTime = routeCache->TravelTimeToGoalArea( spot.aasAreaNum, originAreaNum, Bot::ALLOWED_TRAVEL_FLAGS );
			if( !backTimeTravelTime || toTravelTime + backTimeTravelTime > maxFeasibleTravelTimeCentis ) {
				continue;
			}

			int totalTravelTimeCentis = toTravelTime + backTimeTravelTime;
			float travelTimeFactor = ComputeTravelTimeFactor( totalTravelTimeCentis, maxFeasibleTravelTimeCentis );
			float distanceFactor = ComputeDistanceFactor( spot.origin, origin, weightFalloffDistanceRatio, searchRadius );
			float newScore = spotAndScore.score;
			newScore = ApplyFactor( newScore, distanceFactor, distanceInfluence );
			newScore = ApplyFactor( newScore, travelTimeFactor, travelTimeInfluence );
			result.push_back( SpotAndScore( spotAndScore.spotNum, newScore ) );
		}
	} else {
		for( unsigned i = 0, end = std::min( candidateSpots.size(), result.capacity() ); i < end; ++i ) {
			const SpotAndScore &spotAndScore = candidateSpots[i];
			const TacticalSpot &spot = spots[spotAndScore.spotNum];
			int toSpotTime = routeCache->TravelTimeToGoalArea( originAreaNum, spot.aasAreaNum, Bot::ALLOWED_TRAVEL_FLAGS );
			if( !toSpotTime ) {
				continue;
			}
			int toEntityTime = routeCache->TravelTimeToGoalArea( spot.aasAreaNum, originAreaNum, Bot::ALLOWED_TRAVEL_FLAGS );
			if( !toEntityTime ) {
				continue;
			}

			int totalTravelTimeCentis = 10 * ( toSpotTime + toEntityTime );
			float travelTimeFactor = ComputeTravelTimeFactor( totalTravelTimeCentis, maxFeasibleTravelTimeCentis );
			float distanceFactor = ComputeDistanceFactor( spot.origin, origin, weightFalloffDistanceRatio, searchRadius );
			float newScore = spotAndScore.score;
			newScore = ApplyFactor( newScore, distanceFactor, distanceInfluence );
			newScore = ApplyFactor( newScore, travelTimeFactor, travelTimeInfluence );
			result.push_back( SpotAndScore( spotAndScore.spotNum, newScore ) );
		}
	}

	// Sort result so best score areas are first
	std::sort( result.begin(), result.end() );
}

void TacticalSpotsRegistry::CheckSpotsVisibleOriginTrace( const OriginParams &originParams,
														  const AdvantageProblemParams &problemParams,
														  const ReachCheckedSpots &candidateSpots,
														  TraceCheckedSpots &result ) const {
	edict_t *passent = const_cast<edict_t*>( originParams.originEntity );
	edict_t *keepVisibleEntity = const_cast<edict_t *>( problemParams.keepVisibleEntity );
	Vec3 entityOrigin( problemParams.keepVisibleOrigin );
	// If not only origin but an entity too is supplied
	if( keepVisibleEntity ) {
		// Its a good idea to add some offset from the ground
		entityOrigin.Z() += 0.66f * keepVisibleEntity->r.maxs[2];
	}
	// Copy to locals for faster access
	const edict_t *gameEdicts = game.edicts;

	trace_t trace;
	// Do not more than result.capacity() iterations
	// (do not do more than result.capacity() traces even if it may cause loose of feasible areas).
	for( unsigned i = 0, end = std::min( candidateSpots.size(), result.capacity() ); i < end; ++i ) {
		const SpotAndScore &spotAndScore = candidateSpots[i];
		G_Trace( &trace, spots[spotAndScore.spotNum].origin, nullptr, nullptr, entityOrigin.Data(), passent, MASK_AISOLID );
		if( trace.fraction == 1.0f || gameEdicts + trace.ent == keepVisibleEntity ) {
			result.push_back( spotAndScore );
		}
	}
}

void TacticalSpotsRegistry::SortByVisAndOtherFactors( const OriginParams &originParams,
													  const AdvantageProblemParams &problemParams,
													  TraceCheckedSpots &result ) const {
	const Vec3 origin( originParams.origin );
	const Vec3 entityOrigin( problemParams.keepVisibleOrigin );
	const float originZ = originParams.origin[2];
	const float entityZ = problemParams.keepVisibleOrigin[2];
	const float searchRadius = originParams.searchRadius;
	const float minHeightAdvantageOverOrigin = problemParams.minHeightAdvantageOverOrigin;
	const float heightOverOriginInfluence = problemParams.heightOverOriginInfluence;
	const float minHeightAdvantageOverEntity = problemParams.minHeightAdvantageOverEntity;
	const float heightOverEntityInfluence = problemParams.heightOverEntityInfluence;
	const float originWeightFalloffDistanceRatio = problemParams.originWeightFalloffDistanceRatio;
	const float originDistanceInfluence = problemParams.originDistanceInfluence;
	const float entityWeightFalloffDistanceRatio = problemParams.entityWeightFalloffDistanceRatio;
	const float entityDistanceInfluence = problemParams.entityDistanceInfluence;
	const float minSpotDistanceToEntity = problemParams.minSpotDistanceToEntity;
	const float entityDistanceRange = problemParams.maxSpotDistanceToEntity - problemParams.minSpotDistanceToEntity;

	const unsigned resultSpotsSize = result.size();
	if( resultSpotsSize <= 1 ) {
		return;
	}

	for( unsigned i = 0; i < resultSpotsSize; ++i ) {
		unsigned visibilitySum = 0;
		unsigned testedSpotNum = result[i].spotNum;
		// Get address of the visibility table row
		unsigned char *spotVisibilityForSpotNum = spotVisibilityTable + testedSpotNum * numSpots;

		for( unsigned j = 0; j < i; ++j )
			visibilitySum += spotVisibilityForSpotNum[j];

		// Skip i-th index

		for( unsigned j = i + 1; j < resultSpotsSize; ++j )
			visibilitySum += spotVisibilityForSpotNum[j];

		const TacticalSpot &testedSpot = spots[testedSpotNum];
		float score = result[i].score;

		// The maximum possible visibility score for a pair of spots is 255
		float visFactor = visibilitySum / ( ( result.size() - 1 ) * 255.0f );
		visFactor = 1.0f / Q_RSqrt( visFactor );
		score *= visFactor;

		float heightOverOrigin = testedSpot.absMins[2] - originZ - minHeightAdvantageOverOrigin;
		float heightOverOriginFactor = BoundedFraction( heightOverOrigin, searchRadius - minHeightAdvantageOverOrigin );
		score = ApplyFactor( score, heightOverOriginFactor, heightOverOriginInfluence );

		float heightOverEntity = testedSpot.absMins[2] - entityZ - minHeightAdvantageOverEntity;
		float heightOverEntityFactor = BoundedFraction( heightOverEntity, searchRadius - minHeightAdvantageOverEntity );
		score = ApplyFactor( score, heightOverEntityFactor, heightOverEntityInfluence );

		float originDistance = 1.0f / Q_RSqrt( 0.001f + DistanceSquared( testedSpot.origin, origin.Data() ) );
		float originDistanceFactor = ComputeDistanceFactor( originDistance, originWeightFalloffDistanceRatio, searchRadius );
		score = ApplyFactor( score, originDistanceFactor, originDistanceInfluence );

		float entityDistance = 1.0f / Q_RSqrt( 0.001f + DistanceSquared( testedSpot.origin, entityOrigin.Data() ) );
		entityDistance -= minSpotDistanceToEntity;
		float entityDistanceFactor = ComputeDistanceFactor( entityDistance,
															entityWeightFalloffDistanceRatio,
															entityDistanceRange );
		score = ApplyFactor( score, entityDistanceFactor, entityDistanceInfluence );

		result[i].score = score;
	}

	// Sort results so best score spots are first
	std::stable_sort( result.begin(), result.end() );
}

int TacticalSpotsRegistry::FindCoverSpots( const OriginParams &originParams,
										   const CoverProblemParams &problemParams,
										   vec3_t *spotOrigins, int maxSpots ) const {
	uint16_t boundsSpots[MAX_SPOTS_PER_QUERY];
	uint16_t insideSpotNum;
	uint16_t numSpotsInBounds = FindSpotsInRadius( originParams, boundsSpots, &insideSpotNum );

	CandidateSpots candidateSpots;
	SelectCandidateSpots( originParams, problemParams, boundsSpots, numSpotsInBounds, candidateSpots );

	ReachCheckedSpots reachCheckedSpots;
	if( problemParams.checkToAndBackReachability ) {
		CheckSpotsReachFromOriginAndBack( originParams, problemParams, candidateSpots, insideSpotNum, reachCheckedSpots );
	} else {
		CheckSpotsReachFromOrigin( originParams, problemParams, candidateSpots, insideSpotNum, reachCheckedSpots );
	}

	TraceCheckedSpots coverSpots;
	SelectSpotsForCover( originParams, problemParams, reachCheckedSpots, coverSpots );

	return CopyResults( coverSpots.begin(), coverSpots.end(), problemParams, spotOrigins, maxSpots );
}

void TacticalSpotsRegistry::SelectSpotsForCover( const OriginParams &originParams,
												 const CoverProblemParams &problemParams,
												 const ReachCheckedSpots &candidateSpots,
												 TraceCheckedSpots &result ) const {
	// Do not do more than result.capacity() iterations
	for( unsigned i = 0, end = std::min( candidateSpots.size(), result.capacity() ); i < end; ++i ) {
		const SpotAndScore &spotAndScore = candidateSpots[i];
		if( LooksLikeACoverSpot( spotAndScore.spotNum, originParams, problemParams ) ) {
			result.push_back( spotAndScore );
		}
	}
	;
}

bool TacticalSpotsRegistry::LooksLikeACoverSpot( uint16_t spotNum, const OriginParams &originParams,
												 const CoverProblemParams &problemParams ) const {
	const TacticalSpot &spot = spots[spotNum];

	edict_t *passent = const_cast<edict_t *>( problemParams.attackerEntity );
	float *attackerOrigin = const_cast<float *>( problemParams.attackerOrigin );
	float *spotOrigin = const_cast<float *>( spot.origin );
	const edict_t *doNotHitEntity = originParams.originEntity;

	trace_t trace;
	G_Trace( &trace, attackerOrigin, nullptr, nullptr, spotOrigin, passent, MASK_AISOLID );
	if( trace.fraction == 1.0f ) {
		return false;
	}

	float harmfulRayThickness = problemParams.harmfulRayThickness;

	vec3_t bounds[2] =
	{
		{ -harmfulRayThickness, -harmfulRayThickness, -harmfulRayThickness },
		{ +harmfulRayThickness, +harmfulRayThickness, +harmfulRayThickness }
	};

	// Convert bounds from relative to absolute
	VectorAdd( bounds[0], spot.origin, bounds[0] );
	VectorAdd( bounds[1], spot.origin, bounds[1] );

	for( int i = 0; i < 8; ++i ) {
		vec3_t traceEnd;
		traceEnd[0] = bounds[( i >> 2 ) & 1][0];
		traceEnd[1] = bounds[( i >> 1 ) & 1][1];
		traceEnd[2] = bounds[( i >> 0 ) & 1][2];
		G_Trace( &trace, attackerOrigin, nullptr, nullptr, traceEnd, passent, MASK_AISOLID );
		if( trace.fraction == 1.0f || game.edicts + trace.ent == doNotHitEntity ) {
			return false;
		}
	}

	return true;
}

int TacticalSpotsRegistry::FindEvadeDangerSpots( const OriginParams &originParams,
												 const DodgeDangerProblemParams &problemParams,
												 vec3_t *spotOrigins, int maxSpots ) const {
	uint16_t boundsSpots[MAX_SPOTS_PER_QUERY];
	uint16_t insideSpotNum;
	uint16_t numSpotsInBounds = FindSpotsInRadius( originParams, boundsSpots, &insideSpotNum );

	CandidateSpots candidateSpots;
	SelectPotentialDodgeSpots( originParams, problemParams, boundsSpots, numSpotsInBounds, candidateSpots );

	// Try preferring spots that conform well to the existing velocity direction
	if( const edict_t *ent = originParams.originEntity ) {
		// Make sure that the current entity params match problem params
		if( VectorCompare( ent->s.origin, originParams.origin ) ) {
			float squareSpeed = VectorLengthSquared( ent->velocity );
			if( squareSpeed > DEFAULT_PLAYERSPEED * DEFAULT_PLAYERSPEED ) {
				Vec3 velocityDir( ent->velocity );
				velocityDir *= 1.0f / sqrtf( squareSpeed );
				for( auto &spotAndScore: candidateSpots ) {
					Vec3 toSpotDir( spots[spotAndScore.spotNum].origin );
					toSpotDir -= ent->s.origin;
					toSpotDir.NormalizeFast();
					spotAndScore.score *= 1.0f + velocityDir.Dot( toSpotDir );
				}
			}
		}
	}

	ReachCheckedSpots reachCheckedSpots;
	if( problemParams.checkToAndBackReachability ) {
		CheckSpotsReachFromOriginAndBack( originParams, problemParams, candidateSpots, insideSpotNum, reachCheckedSpots );
	} else {
		CheckSpotsReachFromOrigin( originParams, problemParams, candidateSpots, insideSpotNum, reachCheckedSpots );
	}

	return CopyResults( reachCheckedSpots.begin(), reachCheckedSpots.end(), problemParams, spotOrigins, maxSpots );
}

void TacticalSpotsRegistry::SelectPotentialDodgeSpots( const OriginParams &originParams,
													   const DodgeDangerProblemParams &problemParams,
													   const uint16_t *spotNums,
													   uint16_t numSpots_,
													   CandidateSpots &result ) const {
	bool mightNegateDodgeDir = false;
	Vec3 dodgeDir = MakeDodgeDangerDir( originParams, problemParams, &mightNegateDodgeDir );

	const float searchRadius = originParams.searchRadius;
	const float minHeightAdvantageOverOrigin = problemParams.minHeightAdvantageOverOrigin;
	const float heightOverOriginInfluence = problemParams.heightOverOriginInfluence;
	const float originZ = originParams.origin[2];
	// Copy to stack for faster access
	Vec3 origin( originParams.origin );

	if( mightNegateDodgeDir ) {
		for( unsigned i = 0; i < numSpots_ && result.size() < result.capacity(); ++i ) {
			const TacticalSpot &spot = spots[spotNums[i]];

			float heightOverOrigin = spot.absMins[2] - originZ;
			if( heightOverOrigin < minHeightAdvantageOverOrigin ) {
				continue;
			}

			Vec3 toSpotDir( spot.origin );
			toSpotDir -= origin;
			float squaredDistanceToSpot = toSpotDir.SquaredLength();
			if( squaredDistanceToSpot < 1 ) {
				continue;
			}

			toSpotDir *= Q_RSqrt( squaredDistanceToSpot );
			float dot = toSpotDir.Dot( dodgeDir );
			if( dot < 0.2f ) {
				continue;
			}

			heightOverOrigin -= minHeightAdvantageOverOrigin;
			float heightOverOriginFactor = BoundedFraction( heightOverOrigin, searchRadius - minHeightAdvantageOverOrigin );
			float score = ApplyFactor( dot, heightOverOriginFactor, heightOverOriginInfluence );

			result.push_back( SpotAndScore( spotNums[i], score ) );
		}
	} else {
		for( unsigned i = 0; i < numSpots_ && result.size() < result.capacity(); ++i ) {
			const TacticalSpot &spot = spots[spotNums[i]];

			float heightOverOrigin = spot.absMins[2] - originZ;
			if( heightOverOrigin < minHeightAdvantageOverOrigin ) {
				continue;
			}

			Vec3 toSpotDir( spot.origin );
			toSpotDir -= origin;
			float squaredDistanceToSpot = toSpotDir.SquaredLength();
			if( squaredDistanceToSpot < 1 ) {
				continue;
			}

			toSpotDir *= Q_RSqrt( squaredDistanceToSpot );
			float absDot = fabsf( toSpotDir.Dot( dodgeDir ) );
			if( absDot < 0.2f ) {
				continue;
			}

			heightOverOrigin -= minHeightAdvantageOverOrigin;
			float heightOverOriginFactor = BoundedFraction( heightOverOrigin, searchRadius - minHeightAdvantageOverOrigin );
			float score = ApplyFactor( absDot, heightOverOriginFactor, heightOverOriginInfluence );

			result.push_back( SpotAndScore( spotNums[i], score ) );
		}
	}

	// Sort result so best score areas are first
	std::sort( result.begin(), result.end() );
}

Vec3 TacticalSpotsRegistry::MakeDodgeDangerDir( const OriginParams &originParams,
												const DodgeDangerProblemParams &problemParams,
												bool *mightNegateDodgeDir ) const {
	*mightNegateDodgeDir = false;
	if( problemParams.avoidSplashDamage ) {
		Vec3 result( 0, 0, 0 );
		Vec3 originToHitDir = problemParams.dangerHitPoint - originParams.origin;
		float degrees = originParams.originEntity ? -originParams.originEntity->s.angles[YAW] : -90;
		RotatePointAroundVector( result.Data(), &axis_identity[AXIS_UP], originToHitDir.Data(), degrees );
		result.NormalizeFast();

		if( fabs( result.X() ) < 0.3 ) {
			result.X() = 0;
		}
		if( fabs( result.Y() ) < 0.3 ) {
			result.Y() = 0;
		}
		result.Z() = 0;
		result.X() *= -1.0f;
		result.Y() *= -1.0f;
		return result;
	}

	Vec3 selfToHitPoint = problemParams.dangerHitPoint - originParams.origin;
	selfToHitPoint.Z() = 0;
	// If bot is not hit in its center, try pick a direction that is opposite to a vector from bot center to hit point
	if( selfToHitPoint.SquaredLength() > 4 * 4 ) {
		selfToHitPoint.NormalizeFast();
		// Check whether this direction really helps to dodge the danger
		// (the less is the abs. value of the dot product, the closer is the chosen direction to a perpendicular one)
		if( fabs( selfToHitPoint.Dot( originParams.origin ) ) < 0.5f ) {
			if( fabs( selfToHitPoint.X() ) < 0.3 ) {
				selfToHitPoint.X() = 0;
			}
			if( fabs( selfToHitPoint.Y() ) < 0.3 ) {
				selfToHitPoint.Y() = 0;
			}
			return -selfToHitPoint;
		}
	}

	*mightNegateDodgeDir = true;
	// Otherwise just pick a direction that is perpendicular to the danger direction
	float maxCrossSqLen = 0.0f;
	Vec3 result( 0, 1, 0 );
	for( int i = 0; i < 3; ++i ) {
		Vec3 cross = problemParams.dangerDirection.Cross( &axis_identity[i * 3] );
		cross.Z() = 0;
		float crossSqLen = cross.SquaredLength();
		if( crossSqLen > maxCrossSqLen ) {
			maxCrossSqLen = crossSqLen;
			float invLen = Q_RSqrt( crossSqLen );
			result.X() = cross.X() * invLen;
			result.Y() = cross.Y() * invLen;
		}
	}
	return result;
}

int TacticalSpotsRegistry::FindClosestToTargetWalkableSpot( const OriginParams &originParams,
															int targetAreaNum,
															vec3_t spotOrigin ) const {
	uint16_t spotNums[MAX_SPOTS_PER_QUERY];
	uint16_t insideSpotNum;
	uint16_t numSpots_ = FindSpotsInRadius( originParams, spotNums, &insideSpotNum );

	const auto *routeCache = originParams.routeCache;
	const float *origin = originParams.origin;
	const int originAreaNum = originParams.originAreaNum;
	const int allowedFlags = originParams.allowedTravelFlags;
	const int preferredFlags = originParams.preferredTravelFlags;
	constexpr int fallbackTravelFlags = BotGenericGroundMovementFallback::TRAVEL_FLAGS;

	// Assume that max allowed travel time to spot does not exceed travel time required to cover the search radius
	const int maxTravelTimeToSpotWalking = (int)( originParams.searchRadius * 3.0f );

	trace_t trace;

	const TacticalSpot *bestSpot = nullptr;
	int bestTravelTimeFromSpotToTarget = std::numeric_limits<int>::max();
	int travelTimeToSpotWalking, travelTimeFromSpotToTarget;

	// Test for coarse walkability from a feet point
	vec3_t traceMins, traceMaxs;
	GetSpotsWalkabilityTraceBounds( traceMins, traceMaxs );

	for( uint16_t i = 0; i < numSpots_; ++i ) {
		const uint16_t spotNum = spotNums[i];
		const auto &spot = spots[spotNum];

		// Cut off computations early in this case
		if( spot.aasAreaNum == originAreaNum ) {
			continue;
		}

		// Reject spots that are in another area but close
		// It might occur if the spot is on boundary edge with a current area
		if( DistanceSquared( spot.origin, originParams.origin ) < 32.0f * 32.0f ) {
			continue;
		}

		travelTimeToSpotWalking = routeCache->TravelTimeToGoalArea( originAreaNum, spot.aasAreaNum, fallbackTravelFlags );
		if( !travelTimeToSpotWalking ) {
			continue;
		}

		// Due to triangle inequality travelTimeToSpotWalking + travelTimeFromSpotToTarget >= currTravelTimeToTarget.
		// We can reject spots that does not look like short-range reachable by walking though
		if( travelTimeToSpotWalking > maxTravelTimeToSpotWalking ) {
			continue;
		}

		// Do a double test to match generic routing behaviour.
		travelTimeFromSpotToTarget = routeCache->TravelTimeToGoalArea( spot.aasAreaNum, targetAreaNum, preferredFlags );
		if( !travelTimeFromSpotToTarget ) {
			travelTimeFromSpotToTarget = routeCache->TravelTimeToGoalArea( spot.aasAreaNum, targetAreaNum, allowedFlags );
			if( !travelTimeFromSpotToTarget ) {
				continue;
			}
		}

		if( travelTimeFromSpotToTarget >= bestTravelTimeFromSpotToTarget ) {
			continue;
		}

		float *start = const_cast<float *>( origin );
		float *end = const_cast<float *>( spot.origin );
		edict_t *skip = const_cast<edict_t *>( originParams.originEntity );
		// We have to test against entities and not only solid world in this case
		// (results of the method are critical for escaping from blocked state)
		G_Trace( &trace, start, traceMins, traceMaxs, end, skip, MASK_AISOLID );
		if( trace.fraction != 1.0f ) {
			continue;
		}

		bestTravelTimeFromSpotToTarget = travelTimeFromSpotToTarget;
		bestSpot = &spot;
	}

	if( bestSpot ) {
		VectorCopy( bestSpot->origin, spotOrigin );
		return bestTravelTimeFromSpotToTarget;
	}

	return 0;
}

bool TacticalSpotsRegistry::FindShortSideStepDodgeSpot( const OriginParams &originParams,
														const vec3_t keepVisibleOrigin,
														vec3_t spotOrigin ) const {
	trace_t trace;
	Vec3 tmpVec3( 0, 0, 0 );
	Vec3 *droppedToFloorOrigin = nullptr;
	if( auto originEntity = originParams.originEntity ) {
		if( originEntity->groundentity ) {
			tmpVec3.Set( originEntity->s.origin );
			tmpVec3.Z() += originEntity->r.mins[2];
			droppedToFloorOrigin = &tmpVec3;
		} else if( originEntity->ai && originEntity->ai->botRef ) {
			const auto &entityPhysicsState = originEntity->ai->botRef->EntityPhysicsState();
			if( entityPhysicsState->HeightOverGround() < 32.0f ) {
				tmpVec3.Set( entityPhysicsState->Origin() );
				tmpVec3.Z() += playerbox_stand_mins[2];
				tmpVec3.Z() -= entityPhysicsState->HeightOverGround();
				droppedToFloorOrigin = &tmpVec3;
			}
		} else {
			auto *ent = const_cast<edict_t *>( originEntity );
			Vec3 end( originEntity->s.origin );
			end.Z() += originEntity->r.mins[2] - 32.0f;
			G_Trace( &trace, ent->s.origin, ent->r.mins, ent->r.maxs, end.Data(), ent, MASK_SOLID );
			if( trace.fraction != 1.0f && ISWALKABLEPLANE( &trace.plane ) ) {
				tmpVec3.Set( trace.endpos );
				droppedToFloorOrigin = &tmpVec3;
			}
		}
	} else {
		Vec3 end( originParams.origin );
		end.Z() -= 48.0f;
		G_Trace( &trace, const_cast<float *>( originParams.origin ), nullptr, nullptr, end.Data(), nullptr, MASK_SOLID );
		if( trace.fraction != 1.0f && ISWALKABLEPLANE( &trace.plane ) ) {
			tmpVec3.Set( trace.endpos );
			droppedToFloorOrigin = &tmpVec3;
		}
	}

	if( !droppedToFloorOrigin ) {
		return false;
	}

	Vec3 testedOrigin( *droppedToFloorOrigin );
	// Make sure it is at least 1 unit above the ground
	testedOrigin.Z() += ( -playerbox_stand_mins[2] ) + 1.0f;

	float bestScore = -1.0f;
	vec3_t bestDir;

	Vec3 keepVisible2DDir( keepVisibleOrigin );
	keepVisible2DDir -= originParams.origin;
	keepVisible2DDir.Z() = 0;
	keepVisible2DDir.Normalize();

	edict_t *const ignore = originParams.originEntity ? const_cast<edict_t *>( originParams.originEntity ) : nullptr;
	const float dx = playerbox_stand_maxs[0] - playerbox_stand_mins[0];
	const float dy = playerbox_stand_maxs[1] - playerbox_stand_mins[1];
	for( int i = -1; i <= 1; ++i ) {
		for( int j = -1; j <= 1; ++j ) {
			// Disallow diagonal moves (can't be checked so easily by just adding an offset)
			// Disallow staying at the current origin as well.
			if( ( i && j ) || !( i || j ) ) {
				continue;
			}

			testedOrigin.X() = droppedToFloorOrigin->X() + i * dx;
			testedOrigin.Y() = droppedToFloorOrigin->Y() + j * dy;
			float *mins = playerbox_stand_mins;
			float *maxs = playerbox_stand_maxs;
			G_Trace( &trace, testedOrigin.Data(), mins, maxs, testedOrigin.Data(), ignore, MASK_SOLID );
			if( trace.fraction != 1.0f || trace.startsolid ) {
				continue;
			}

			// Check ground below
			Vec3 groundTraceEnd( testedOrigin );
			groundTraceEnd.Z() -= 72.0f;
			G_Trace( &trace, testedOrigin.Data(), nullptr, nullptr, groundTraceEnd.Data(), ignore, MASK_SOLID );
			if( trace.fraction == 1.0f || trace.allsolid ) {
				continue;
			}
			if( !ISWALKABLEPLANE( &trace.plane ) ) {
				continue;
			}
			if( trace.contents & ( CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_NODROP | CONTENTS_DONOTENTER ) ) {
				continue;
			}

			Vec3 spot2DDir( testedOrigin.Data() );
			spot2DDir -= originParams.origin;
			spot2DDir.Z() = 0;
			spot2DDir.Normalize();

			// Give side spots a greater score, but make sure the score is always positive for each spot
			float score = ( 1.0f - fabsf( spot2DDir.Dot( keepVisible2DDir ) ) ) + 0.1f;
			if( score > bestScore ) {
				bestScore = score;
				spot2DDir.CopyTo( bestDir );
			}
		}
	}

	if( bestScore > 0 ) {
		VectorCopy( bestDir, spotOrigin );
		VectorScale( spotOrigin, sqrtf( dx * dx + dy * dy ), spotOrigin );
		VectorAdd( spotOrigin, droppedToFloorOrigin->Data(), spotOrigin );
		return true;
	}

	return false;
}