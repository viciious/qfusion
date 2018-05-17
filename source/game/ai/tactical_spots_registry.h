#ifndef QFUSION_TACTICAL_SPOTS_DETECTOR_H
#define QFUSION_TACTICAL_SPOTS_DETECTOR_H

#include "ai_local.h"
#include "navigation/AasRouteCache.h"
#include "static_vector.h"
#include "bot.h"

class TacticalSpotsRegistry
{
	friend class BotRoamingManager;
	friend class TacticalSpotsBuilder;
	// These types need forward declaration before methods where they are used.

public:
	class OriginParams
	{
		friend class TacticalSpotsRegistry;

		const edict_t *originEntity;
		vec3_t origin;
		float searchRadius;
		AiAasRouteCache *routeCache;
		int originAreaNum;
		int preferredTravelFlags;
		int allowedTravelFlags;

public:
		OriginParams( const edict_t *originEntity_, float searchRadius_, AiAasRouteCache *routeCache_ )
			: originEntity( originEntity_ ), searchRadius( searchRadius_ ), routeCache( routeCache_ ) {
			VectorCopy( originEntity_->s.origin, this->origin );
			const AiAasWorld *aasWorld = AiAasWorld::Instance();
			originAreaNum = aasWorld->IsLoaded() ? aasWorld->FindAreaNum( originEntity ) : 0;
			preferredTravelFlags = Bot::PREFERRED_TRAVEL_FLAGS;
			allowedTravelFlags = Bot::ALLOWED_TRAVEL_FLAGS;
		}

		OriginParams( const vec3_t origin_, float searchRadius_, AiAasRouteCache *routeCache_ )
			: originEntity( nullptr ), searchRadius( searchRadius_ ), routeCache( routeCache_ ) {
			VectorCopy( origin_, this->origin );
			const AiAasWorld *aasWorld = AiAasWorld::Instance();
			originAreaNum = aasWorld->IsLoaded() ? aasWorld->FindAreaNum( origin ) : 0;
			preferredTravelFlags = Bot::PREFERRED_TRAVEL_FLAGS;
			allowedTravelFlags = Bot::ALLOWED_TRAVEL_FLAGS;
		}

		OriginParams( const vec3_t origin_, const edict_t *originEntity_, float searchRadius_, AiAasRouteCache *routeCache_ )
			: originEntity( originEntity_ ), searchRadius( searchRadius_ ), routeCache( routeCache_ ) {
			VectorCopy( origin_, this->origin );
			const AiAasWorld *aasWorld = AiAasWorld::Instance();
			originAreaNum = aasWorld->IsLoaded() ? aasWorld->FindAreaNum( originEntity ) : 0;
			preferredTravelFlags = Bot::PREFERRED_TRAVEL_FLAGS;
			allowedTravelFlags = Bot::ALLOWED_TRAVEL_FLAGS;
		}

		inline Vec3 MinBBoxBounds( float minHeightAdvantage = 0.0f ) const {
			return Vec3( -searchRadius, -searchRadius, minHeightAdvantage ) + origin;
		}

		inline Vec3 MaxBBoxBounds() const {
			return Vec3( +searchRadius, +searchRadius, +searchRadius ) + origin;
		}
	};

	class CommonProblemParams
	{
		friend class TacticalSpotsRegistry;

protected:
		float minHeightAdvantageOverOrigin;
		float originWeightFalloffDistanceRatio;
		float originDistanceInfluence;
		float travelTimeInfluence;
		float heightOverOriginInfluence;
		int maxFeasibleTravelTimeMillis;
		float spotProximityThreshold;
		bool checkToAndBackReachability;

public:
		CommonProblemParams()
			: minHeightAdvantageOverOrigin( 0.0f ),
			originWeightFalloffDistanceRatio( 0.0f ),
			originDistanceInfluence( 0.9f ),
			travelTimeInfluence( 0.9f ),
			heightOverOriginInfluence( 0.9f ),
			maxFeasibleTravelTimeMillis( 5000 ),
			spotProximityThreshold( 64.0f ),
			checkToAndBackReachability( false ) {}

		inline void SetCheckToAndBackReachability( bool checkToAndBack ) {
			this->checkToAndBackReachability = checkToAndBack;
		}

		inline void SetOriginWeightFalloffDistanceRatio( float ratio ) {
			originWeightFalloffDistanceRatio = Clamp( ratio );
		}

		inline void SetMinHeightAdvantageOverOrigin( float minHeight ) {
			minHeightAdvantageOverOrigin = minHeight;
		}

		inline void SetMaxFeasibleTravelTimeMillis( int millis ) {
			maxFeasibleTravelTimeMillis = std::max( 1, millis );
		}

		inline void SetOriginDistanceInfluence( float influence ) { originDistanceInfluence = Clamp( influence ); }

		inline void SetTravelTimeInfluence( float influence ) { travelTimeInfluence = Clamp( influence ); }

		inline void SetHeightOverOriginInfluence( float influence ) { heightOverOriginInfluence = Clamp( influence ); }

		inline void SetSpotProximityThreshold( float radius ) { spotProximityThreshold = std::max( 0.0f, radius ); }
	};

	class AdvantageProblemParams : public CommonProblemParams
	{
		friend class TacticalSpotsRegistry;

		const edict_t *keepVisibleEntity;
		vec3_t keepVisibleOrigin;
		float minSpotDistanceToEntity;
		float maxSpotDistanceToEntity;
		float entityDistanceInfluence;
		float entityWeightFalloffDistanceRatio;
		float minHeightAdvantageOverEntity;
		float heightOverEntityInfluence;

public:
		AdvantageProblemParams( const edict_t *keepVisibleEntity_ )
			: keepVisibleEntity( keepVisibleEntity_ ),
			minSpotDistanceToEntity( 0 ),
			maxSpotDistanceToEntity( 999999.0f ),
			entityDistanceInfluence( 0.5f ),
			entityWeightFalloffDistanceRatio( 0 ),
			minHeightAdvantageOverEntity( -999999.0f ),
			heightOverEntityInfluence( 0.5f ) {
			VectorCopy( keepVisibleEntity->s.origin, this->keepVisibleOrigin );
		}

		AdvantageProblemParams( const vec3_t keepVisibleOrigin_ )
			: keepVisibleEntity( nullptr ),
			minSpotDistanceToEntity( 0 ),
			maxSpotDistanceToEntity( 999999.0f ),
			entityDistanceInfluence( 0.5f ),
			entityWeightFalloffDistanceRatio( 0 ),
			minHeightAdvantageOverEntity( -999999.0f ),
			heightOverEntityInfluence( 0.5f ) {
			VectorCopy( keepVisibleOrigin_, this->keepVisibleOrigin );
		}

		inline void SetMinSpotDistanceToEntity( float distance ) { minSpotDistanceToEntity = distance; }
		inline void SetMaxSpotDistanceToEntity( float distance ) { maxSpotDistanceToEntity = distance; }
		inline void SetEntityDistanceInfluence( float influence ) { entityDistanceInfluence = influence; }
		inline void SetEntityWeightFalloffDistanceRatio( float ratio ) { entityWeightFalloffDistanceRatio = Clamp( ratio ); }
		inline void SetMinHeightAdvantageOverEntity( float height ) { minHeightAdvantageOverEntity = height; }
		inline void SetHeightOverEntityInfluence( float influence ) { heightOverEntityInfluence = Clamp( influence ); }
	};

	class CoverProblemParams : public CommonProblemParams
	{
		friend class TacticalSpotsRegistry;

		const edict_t *attackerEntity;
		vec3_t attackerOrigin;
		float harmfulRayThickness;

public:
		CoverProblemParams( const edict_t *attackerEntity_, float harmfulRayThickness_ )
			: attackerEntity( attackerEntity_ ), harmfulRayThickness( harmfulRayThickness_ ) {
			VectorCopy( attackerEntity_->s.origin, this->attackerOrigin );
		}

		CoverProblemParams( const vec3_t attackerOrigin_, float harmfulRayThickness_ )
			: attackerEntity( nullptr ), harmfulRayThickness( harmfulRayThickness_ ) {
			VectorCopy( attackerOrigin_, this->attackerOrigin );
		}
	};

	class DodgeDangerProblemParams : public CommonProblemParams
	{
		friend class TacticalSpotsRegistry;

		const Vec3 &dangerHitPoint;
		const Vec3 &dangerDirection;
		const bool avoidSplashDamage;

public:
		DodgeDangerProblemParams( const Vec3 &dangerHitPoint_, const Vec3 &dangerDirection_, bool avoidSplashDamage_ )
			: dangerHitPoint( dangerHitPoint_ ), dangerDirection( dangerDirection_ ), avoidSplashDamage( avoidSplashDamage_ ) {}
	};

	struct TacticalSpot {
		vec3_t origin;
		vec3_t absMins;
		vec3_t absMaxs;
		int aasAreaNum;
	};

private:
	static constexpr uint16_t MAX_SPOTS_PER_QUERY = 768;
	static constexpr uint16_t MIN_GRID_CELL_SIDE = 512;
	static constexpr uint16_t MAX_GRID_DIMENSION = 32;

	// i-th element contains a spot for i=spotNum
	TacticalSpot *spots;
	// For i-th spot element # i * numSpots + j contains a mutual visibility between spots i-th and j-th spot:
	// 0 if spot origins and bounds are completely invisible for each other
	// ...
	// 255 if spot origins and bounds are completely visible for each other
	uint8_t *spotVisibilityTable;
	// For i-th spot element # i * numSpots + j contains AAS travel time to j-th spot.
	// If the value is zero, j-th spot is not reachable from i-th one (we conform to AAS time encoding).
	// Non-zero value is a travel time in seconds^-2 (we conform to AAS time encoding).
	// Non-zero does not guarantee the spot is reachable for some picked bot
	// (these values are calculated using shared AI route cache and bots have individual one for blocked paths handling).
	uint16_t *spotTravelTimeTable;

	unsigned numSpots;

	bool needsSavingPrecomputedData;

	class SpotsGridBuilder;

	class BaseSpotsGrid
	{
		friend class TacticalSpotsRegistry::SpotsGridBuilder;
	protected:
		TacticalSpot *spots;
		unsigned numSpots;

		vec3_t worldMins;
		vec3_t worldMaxs;
		unsigned gridCellSize[3];
		unsigned gridNumCells[3];

		inline unsigned PointGridCellNum( const vec3_t point ) const;
		void SetupGridParams();

	public:
		BaseSpotsGrid( const BaseSpotsGrid &that ) = delete;
		BaseSpotsGrid &operator=( const BaseSpotsGrid &that ) = delete;
		BaseSpotsGrid( BaseSpotsGrid &&that ) = delete;
		BaseSpotsGrid &operator=( BaseSpotsGrid &&that ) = delete;

		BaseSpotsGrid() : spots( nullptr ), numSpots( 0 ) {}
		virtual ~BaseSpotsGrid() {}

		inline unsigned NumGridCells() const { return gridNumCells[0] * gridNumCells[1] * gridNumCells[2]; }

		const float *WorldMins() const { return &worldMins[0]; }
		const float *WorldMaxs() const { return &worldMaxs[0]; }

		void AttachSpots( TacticalSpot *spots_, unsigned numSpots_ ) {
			this->spots = spots_;
			this->numSpots = numSpots_;
		}

		virtual uint16_t FindSpotsInRadius( const OriginParams &originParams,
											uint16_t *spotNums,
											uint16_t *insideSpotNum ) const;

		virtual uint16_t *GetCellSpotsList( unsigned gridCellNum, uint16_t *numCellSpots ) const = 0;
	};

	class PrecomputedSpotsGrid final: public BaseSpotsGrid
	{
		friend class TacticalSpotsRegistry::SpotsGridBuilder;

		// i-th element contains an offset of a grid cell spot nums list for i=cellNum
		uint32_t *gridListOffsets;
		// Contains packed lists of grid cell spot nums.
		// Each list starts by number of spot nums followed by spot nums.
		uint16_t *gridSpotsLists;
	public:
		PrecomputedSpotsGrid() : gridListOffsets( nullptr ), gridSpotsLists( nullptr ) {}

		~PrecomputedSpotsGrid() override;

		bool IsLoaded() const { return gridListOffsets != nullptr; }
		bool Load( class AiPrecomputedFileReader &reader );
		void Save( class AiPrecomputedFileWriter &writer );

		uint16_t FindSpotsInRadius( const OriginParams &originParams,
									uint16_t *spotNums,
									uint16_t *insideSpotNum ) const override;

		uint16_t *GetCellSpotsList( unsigned gridCellNum, uint16_t *numCellSpots ) const override;
	};

	class SpotsGridBuilder final: public BaseSpotsGrid
	{
		// Contains a list of spot nums for the grid cell
		struct GridSpotsArray {
			uint16_t *data;
			uint16_t internalBuffer[8];
			uint16_t size;
			uint16_t capacity;

			GridSpotsArray() : data( internalBuffer ), size( 0 ), capacity( 8 ) {}

			~GridSpotsArray() {
				if( data != internalBuffer ) {
					G_LevelFree( data );
				}
			}

			void AddSpot( uint16_t spotNum );
		};

		// A sparse storage for grid cell spots lists used for grid building.
		// Each array element corresponds to a grid cell, and might be null.
		// Built cells spots list get compactified while being copied to a PrecomputedSpotsGrid.
		GridSpotsArray **gridSpotsArrays;
	public:
		SpotsGridBuilder();

		~SpotsGridBuilder() override;

		uint16_t *GetCellSpotsList( unsigned gridCellNum, uint16_t *numCellSpots ) const override;

		void AddSpot( const vec3_t origin, uint16_t spotNum );
		void AddSpotToGridList( unsigned gridCellNum, uint16_t spotNum );
		void CopyTo( PrecomputedSpotsGrid *precomputedGrid );
	};

	PrecomputedSpotsGrid spotsGrid;

	struct SpotAndScore {
		float score;
		uint16_t spotNum;

		SpotAndScore( uint16_t spotNum_, float score_ ) : score( score_ ), spotNum( spotNum_ ) {}
		bool operator<( const SpotAndScore &that ) const { return score > that.score; }
	};

	typedef StaticVector<SpotAndScore, 384> CandidateSpots;
	typedef StaticVector<SpotAndScore, 256> ReachCheckedSpots;
	typedef StaticVector<SpotAndScore, 128> TraceCheckedSpots;

	static TacticalSpotsRegistry *instance;

public:
	inline TacticalSpotsRegistry()
		: spots( nullptr ),
		spotVisibilityTable( nullptr ),
		spotTravelTimeTable( nullptr ),
		numSpots( 0 ),
		needsSavingPrecomputedData( false ) {}

	~TacticalSpotsRegistry();

	bool Load( const char *mapname );

private:
	bool TryLoadPrecomputedData( const char *mapname );
	void SavePrecomputedData( const char *mapname );

	uint16_t FindSpotsInRadius( const OriginParams &originParams, uint16_t *spotNums, uint16_t *insideSpotNum ) const {
		return spotsGrid.FindSpotsInRadius( originParams, spotNums, insideSpotNum );
	}

	void SelectCandidateSpots( const OriginParams &originParams, const CommonProblemParams &problemParams,
							   const uint16_t *spotNums, uint16_t numSpots_, CandidateSpots &result ) const;

	void SelectCandidateSpots( const OriginParams &originParams, const AdvantageProblemParams &problemParams,
							   const uint16_t *spotNums, uint16_t numSpots_, CandidateSpots &result ) const;

	void CheckSpotsReachFromOrigin( const OriginParams &originParams,
									const CommonProblemParams &problemParams,
									const CandidateSpots &candidateSpots,
									uint16_t insideSpotNum,
									ReachCheckedSpots &result ) const;

	void CheckSpotsReachFromOriginAndBack( const OriginParams &originParams,
										   const CommonProblemParams &problemParams,
										   const CandidateSpots &candidateSpots,
										   uint16_t insideSpotNum,
										   ReachCheckedSpots &result ) const;

	int CopyResults( const SpotAndScore *spotsBegin,
					 const SpotAndScore *spotsEnd,
					 const CommonProblemParams &problemParams,
					 vec3_t *spotOrigins, int maxSpots ) const;

	// Specific for positional advantage spots
	void CheckSpotsVisibleOriginTrace( const OriginParams &originParams,
									   const AdvantageProblemParams &params,
									   const ReachCheckedSpots &candidateSpots,
									   TraceCheckedSpots &result ) const;

	void SortByVisAndOtherFactors( const OriginParams &originParams, const AdvantageProblemParams &problemParams,
								   TraceCheckedSpots &spots ) const;

	// Specific for cover spots
	void SelectSpotsForCover( const OriginParams &originParams,
							  const CoverProblemParams &problemParams,
							  const ReachCheckedSpots &candidateSpots,
							  TraceCheckedSpots &result ) const;

	bool LooksLikeACoverSpot( uint16_t spotNum, const OriginParams &originParams,
							  const CoverProblemParams &problemParams ) const;

	Vec3 MakeDodgeDangerDir( const OriginParams &originParams,
							 const DodgeDangerProblemParams &problemParams,
							 bool *mightNegateDodgeDir ) const;

	void SelectPotentialDodgeSpots( const OriginParams &originParams,
									const DodgeDangerProblemParams &problemParams,
									const uint16_t *spotNums,
									uint16_t numSpots_,
									CandidateSpots &result ) const;

public:
	// TacticalSpotsRegistry should be init and shut down explicitly
	// (a game library is not unloaded when a map changes)
	static bool Init( const char *mapname );
	static void Shutdown();

	inline bool IsLoaded() const { return spots != nullptr && numSpots > 0; }

	static inline const TacticalSpotsRegistry *Instance() {
		return ( instance && instance->IsLoaded() ) ? instance : nullptr;
	}

	int FindPositionalAdvantageSpots( const OriginParams &originParams, const AdvantageProblemParams &problemParams,
									  vec3_t *spotOrigins, int maxSpots ) const;

	int FindCoverSpots( const OriginParams &originParams, const CoverProblemParams &problemParams,
						vec3_t *spotOrigins, int maxSpots ) const;

	int FindEvadeDangerSpots( const OriginParams &originParams, const DodgeDangerProblemParams &problemParams,
							  vec3_t *spotOrigins, int maxSpots ) const;

	// Returns an AAS travel time from the spot to the target
	int FindClosestToTargetWalkableSpot( const OriginParams &originParams, int targetAreaNum, vec3_t spotOrigin ) const;

	static inline void GetSpotsWalkabilityTraceBounds( vec3_t mins, vec3_t maxs ) {
		// This step size is rather huge but produces satisfiable results espectially on inclined surfaces
		VectorSet( mins, -2, -2, AI_STEPSIZE + 4 );
		VectorSet( maxs, +2, +2, +2 );
		VectorAdd( mins, playerbox_stand_mins, mins );
		VectorAdd( maxs, playerbox_stand_maxs, maxs );
	}

	bool FindShortSideStepDodgeSpot( const OriginParams &originParams,
									 const vec3_t keepVisibleOrigin,
									 vec3_t spotOrigin ) const;
};

#endif
