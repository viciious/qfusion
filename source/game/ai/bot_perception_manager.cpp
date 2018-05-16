#include "ai_shutdown_hooks_holder.h"
#include "ai_caching_game_allocator.h"
#include "bot_perception_manager.h"
#include "bot.h"

EntitiesPvsCache EntitiesPvsCache::instance;

bool EntitiesPvsCache::AreInPvs( const edict_t *ent1, const edict_t *ent2 ) const {
	// Prevent signed shift bugs
	const auto entNum1 = (unsigned)ENTNUM( ent1 );
	const auto entNum2 = (unsigned)ENTNUM( ent2 );

	uint32_t *ent1Vis = visStrings[entNum1];
	// An offset of an array cell containing entity bits.
	unsigned ent2ArrayOffset = ( entNum2 * 2 ) / 32;
	// An offset of entity bits inside a 32-bit array cell
	unsigned ent2BitsOffset = ( entNum2 * 2 ) % 32;

	unsigned ent2Bits = ( ent1Vis[ent2ArrayOffset] >> ent2BitsOffset ) & 0x3;
	if( ent2Bits != 0 ) {
		// If 2, return true, if 1, return false. Masking with & 1 should help a compiler to avoid branches here
		return (bool)( ( ent2Bits - 1 ) & 1 );
	}

	bool result = AreInPvsUncached( ent1, ent2 );

	// We assume the PVS relation is symmetrical, so set the result in strings for every entity
	uint32_t *ent2Vis = visStrings[entNum2];
	unsigned ent1ArrayOffset = ( entNum1 * 2 ) / 32;
	unsigned ent1BitsOffset = ( entNum1 * 2 ) % 32;

	// Convert boolean result to a non-zero integer
	unsigned ent1Bits = ent2Bits = (unsigned)result + 1;
	assert( ent1Bits == 1 || ent1Bits == 2 );
	// Convert entity bits (1 or 2) into a mask
	ent1Bits <<= ent1BitsOffset;
	ent2Bits <<= ent2BitsOffset;

	// Clear old bits in array cells
	ent1Vis[ent2ArrayOffset] &= ~ent2Bits;
	ent2Vis[ent1ArrayOffset] &= ~ent1Bits;
	// Set new bits in array cells
	ent1Vis[ent2ArrayOffset] |= ent2Bits;
	ent2Vis[ent1ArrayOffset] |= ent1Bits;

	return result;
}

bool EntitiesPvsCache::AreInPvsUncached( const edict_t *ent1, const edict_t *ent2 ) {
	const int numClusters1 = ent1->r.num_clusters;
	if( numClusters1 < 0 ) {
		return trap_inPVS( ent1->s.origin, ent2->s.origin );
	}
	const int numClusters2 = ent2->r.num_clusters;
	if( numClusters2 < 0 ) {
		return trap_inPVS( ent1->s.origin, ent2->s.origin );
	}

	const int *leafNums1 = ent1->r.leafnums;
	const int *leafNums2 = ent2->r.leafnums;
	for( int i = 0; i < numClusters1; ++i ) {
		for( int j = 0; j < numClusters2; ++j ) {
			if( trap_CM_LeafsInPVS( leafNums1[i], leafNums2[j] ) ) {
				return true;
			}
		}
	}

	return false;
}

static inline bool IsGenericProjectileVisible( const edict_t *self, const edict_t *ent ) {
	trace_t trace;
	edict_t *self_ = const_cast<edict_t *>( self );
	edict_t *ent_ = const_cast<edict_t *>( ent );
	G_Trace( &trace, self_->s.origin, nullptr, nullptr, ent_->s.origin, self_, MASK_OPAQUE );
	return trace.fraction == 1.0f || trace.ent == ENTNUM( ent );
}

// Try testing both origins and a mid point. Its very coarse but should produce satisfiable results in-game.
static inline bool IsLaserBeamVisible( const edict_t *self, const edict_t *ent ) {
	trace_t trace;
	edict_t *self_ = const_cast<edict_t *>( self );
	edict_t *ent_ = const_cast<edict_t *>( ent );
	G_Trace( &trace, self_->s.origin, nullptr, nullptr, ent_->s.origin, self_, MASK_OPAQUE );
	if( trace.fraction == 1.0f || trace.ent == ENTNUM( ent ) ) {
		return true;
	}

	G_Trace( &trace, self_->s.origin, nullptr, nullptr, ent_->s.origin2, self_, MASK_OPAQUE );
	if( trace.fraction == 1.0f || trace.ent == ENTNUM( ent ) ) {
		return true;
	}

	return false;
}

static inline bool IsGenericEntityInPvs( const edict_t *self, const edict_t *ent ) {
	return EntitiesPvsCache::Instance()->AreInPvs( self, ent );
}

static inline bool IsLaserBeamInPvs( const edict_t *self, const edict_t *ent ) {
	return EntitiesPvsCache::Instance()->AreInPvs( self, ent );
}

void EntitiesDetector::Clear() {
	maybeDangerousRockets.clear();
	dangerousRockets.clear();
	maybeVisibleOtherWaves.clear();
	dangerousWaves.clear();
	maybeDangerousPlasmas.clear();
	dangerousPlasmas.clear();
	maybeDangerousBlasts.clear();
	dangerousBlasts.clear();
	maybeDangerousGrenades.clear();
	dangerousGrenades.clear();
	maybeDangerousLasers.clear();
	dangerousLasers.clear();

	maybeVisibleOtherRockets.clear();
	visibleOtherRockets.clear();
	maybeVisibleOtherWaves.clear();
	visibleOtherWaves.clear();
	maybeVisibleOtherPlasmas.clear();
	visibleOtherPlasmas.clear();
	maybeVisibleOtherBlasts.clear();
	visibleOtherBlasts.clear();
	maybeVisibleOtherGrenades.clear();
	visibleOtherGrenades.clear();
	maybeVisibleOtherLasers.clear();
	visibleOtherLasers.clear();
}

void EntitiesDetector::Run() {
	Clear();

	// Note that we always skip own rockets, plasma, etc.
	// Otherwise all own bot shot events yield a danger.
	// There are some cases when an own rocket can hurt but they are either extremely rare or handled by bot fire code.
	// Own grenades are the only exception. We check grenade think time to skip grenades just fired by bot.
	// If a grenade is about to explode and is close to bot, its likely it has bounced of the world and can hurt.

	const edict_t *gameEdicts = game.edicts;
	for( int i = gs.maxclients + 1, end = game.numentities; i < end; ++i ) {
		const edict_t *ent = gameEdicts + i;
		switch( ent->s.type ) {
			case ET_ROCKET:
				TryAddEntity( ent, DETECT_ROCKET_SQ_RADIUS, maybeDangerousRockets, maybeVisibleOtherRockets );
				break;
			case ET_WAVE:
				TryAddEntity( ent, DETECT_WAVE_SQ_RADIUS, maybeDangerousWaves, maybeVisibleOtherWaves );
				break;
			case ET_PLASMA:
				TryAddEntity( ent, DETECT_PLASMA_SQ_RADIUS, maybeDangerousPlasmas, maybeVisibleOtherPlasmas );
				break;
			case ET_BLASTER:
				TryAddEntity( ent, DETECT_GB_BLAST_SQ_RADIUS, maybeDangerousBlasts, maybeVisibleOtherBlasts );
				break;
			case ET_GRENADE:
				TryAddGrenade( ent, maybeDangerousGrenades, maybeVisibleOtherGrenades );
				break;
			case ET_LASERBEAM:
				TryAddEntity( ent, DETECT_LG_BEAM_SQ_RADIUS, maybeDangerousLasers, maybeVisibleOtherLasers );
				break;
			default:
				break;
		}
	}

	constexpr auto isGenInPvs = IsGenericEntityInPvs;
	constexpr auto isLaserInPvs = IsLaserBeamInPvs;
	constexpr auto isGenVisible = IsGenericProjectileVisible;
	constexpr auto isLaserVisible = IsLaserBeamVisible;

	// If all potentially dangerous entities have been processed successfully
	// (no entity has been rejected due to limit/capacity overflow)
	// filter other visible entities of the same kind.

	if( FilterRawEntitiesWithDistances( maybeDangerousRockets, dangerousRockets, 12, isGenInPvs, isGenVisible ) ) {
		FilterRawEntitiesWithDistances( maybeVisibleOtherRockets, visibleOtherRockets, 6, isGenInPvs, isGenVisible );
	}
	if( FilterRawEntitiesWithDistances( maybeDangerousWaves, dangerousWaves, 12, isGenInPvs, isGenVisible ) ) {
		FilterRawEntitiesWithDistances( maybeVisibleOtherWaves, visibleOtherWaves, 6, isGenInPvs, isGenVisible );
	}
	if( FilterRawEntitiesWithDistances( maybeDangerousPlasmas, dangerousPlasmas, 48, isGenInPvs, isGenVisible ) ) {
		FilterRawEntitiesWithDistances( maybeVisibleOtherPlasmas, visibleOtherPlasmas, 12, isGenInPvs, isGenVisible );
	}
	if( FilterRawEntitiesWithDistances( maybeDangerousBlasts, dangerousBlasts, 6, isGenInPvs, isGenVisible ) ) {
		FilterRawEntitiesWithDistances( maybeVisibleOtherBlasts, visibleOtherBlasts, 3, isGenInPvs, isGenVisible );
	}
	if( FilterRawEntitiesWithDistances( maybeDangerousGrenades, dangerousGrenades, 6, isGenInPvs, isGenVisible ) ) {
		FilterRawEntitiesWithDistances( maybeVisibleOtherGrenades, visibleOtherGrenades, 3, isGenInPvs, isGenVisible );
	}
	if( FilterRawEntitiesWithDistances( maybeDangerousLasers, dangerousLasers, 4, isLaserInPvs, isLaserVisible ) ) {
		FilterRawEntitiesWithDistances( maybeVisibleOtherLasers, visibleOtherLasers, 4, isLaserInPvs, isLaserVisible );
	}
}

inline void EntitiesDetector::TryAddEntity( const edict_t *ent,
											float squareDistanceThreshold,
											EntsAndDistancesVector &dangerousEntities,
											EntsAndDistancesVector &otherEntities ) {
	assert( ent->s.type != ET_GRENADE );

	if( ent->s.ownerNum == ENTNUM( self ) ) {
		return;
	}

	if( GS_TeamBasedGametype() && self->s.team == ent->s.team ) {
		if( !g_allow_teamdamage->integer ) {
			return;
		}
	}

	float squareDistance = DistanceSquared( self->s.origin, ent->s.origin );
	if( squareDistance < squareDistanceThreshold ) {
		dangerousEntities.emplace_back( EntAndDistance( ENTNUM( ent ), sqrtf( squareDistance ) ) );
	} else {
		otherEntities.emplace_back( EntAndDistance( ENTNUM( ent ), sqrtf( squareDistance ) ) );
	}
}

inline void EntitiesDetector::TryAddGrenade( const edict_t *ent,
											 EntsAndDistancesVector &dangerousEntities,
											 EntsAndDistancesVector &otherEntities ) {
	assert( ent->s.type == ET_GRENADE );

	if( ent->s.ownerNum == ENTNUM( self ) ) {
		if( !g_allow_selfdamage->integer ) {
			return;
		}
		const auto timeout = GS_GetWeaponDef( WEAP_GRENADELAUNCHER )->firedef.timeout;
		// Ignore own grenades in first 500 millis
		if( level.time - ent->nextThink > timeout - 500 ) {
			return;
		}
	} else {
		if( GS_TeamBasedGametype() && ent->s.team == self->s.team ) {
			if( !g_allow_teamdamage->integer ) {
				return;
			}
		}
	}

	float squareDistance = DistanceSquared( self->s.origin, ent->s.origin );
	if( squareDistance < 300 * 300 ) {
		dangerousEntities.emplace_back( EntAndDistance( ENTNUM( ent ), sqrtf( squareDistance ) ) );
	} else {
		otherEntities.emplace_back( EntAndDistance( ENTNUM( ent ), sqrtf( squareDistance ) ) );
	}
}

class PlasmaBeam
{
	friend class PlasmaBeamsBuilder;

	PlasmaBeam()
		: startProjectile( nullptr ),
		endProjectile( nullptr ),
		owner( nullptr ),
		damage( 0.0f ) {}

public:
	PlasmaBeam( const edict_t *firstProjectile )
		: startProjectile( firstProjectile ),
		endProjectile( firstProjectile ),
		owner( game.edicts + firstProjectile->s.ownerNum ),
		damage( firstProjectile->projectileInfo.maxDamage ) {}

	const edict_t *startProjectile;
	const edict_t *endProjectile;
	const edict_t *owner; // May be null if this beam consists of projectiles of many players

	inline Vec3 start() { return Vec3( startProjectile->s.origin ); }
	inline Vec3 end() { return Vec3( endProjectile->s.origin ); }

	float damage;

	inline void AddProjectile( const edict_t *nextProjectile ) {
		endProjectile = nextProjectile;
		// If the beam is combined from projectiles of many players, the beam owner is unknown
		if( owner != nextProjectile->r.owner ) {
			owner = nullptr;
		}
		damage += nextProjectile->projectileInfo.maxDamage;
	}
};

struct EntAndLineParam {
	int entNum;
	float t;

	inline EntAndLineParam( int entNum_, float t_ ) : entNum( entNum_ ), t( t_ ) {}
	inline bool operator<( const EntAndLineParam &that ) const { return t < that.t; }
};

class SameDirBeamsList
{
	friend class PlasmaBeamsBuilder;
	// All projectiles in this list belong to this line defined as a (point, direction) pair
	Vec3 lineEqnPoint;

	EntAndLineParam *sortedProjectiles;
	unsigned projectilesCount;

	static constexpr float DIST_TO_RAY_THRESHOLD = 200.0f;
	static constexpr float DIR_DOT_THRESHOLD = 0.995f;
	static constexpr float PRJ_PROXIMITY_THRESHOLD = 300.0f;

public:
	bool isAlreadySkipped;
	Vec3 avgDirection;
	PlasmaBeam *plasmaBeams;
	unsigned plasmaBeamsCount;

	SameDirBeamsList( const edict_t *firstEntity, const edict_t *bot );

	inline SameDirBeamsList( SameDirBeamsList &&that )
		: lineEqnPoint( that.lineEqnPoint ),
		sortedProjectiles( that.sortedProjectiles ),
		projectilesCount( that.projectilesCount ),
		isAlreadySkipped( that.isAlreadySkipped ),
		avgDirection( that.avgDirection ),
		plasmaBeams( that.plasmaBeams ),
		plasmaBeamsCount( that.plasmaBeamsCount ) {
		that.sortedProjectiles = nullptr;
		that.plasmaBeams = nullptr;
	}

	~SameDirBeamsList();

	bool TryAddProjectile( const edict_t *projectile );

	void BuildBeams();

	inline float ComputeLineEqnParam( const edict_t *projectile ) {
		const float *origin = projectile->s.origin;

		if( fabsf( avgDirection.X() ) > 0.1f ) {
			return ( origin[0] - lineEqnPoint.X() ) / avgDirection.X();
		}
		if( fabsf( avgDirection.Y() ) > 0.1f ) {
			return ( origin[1] - lineEqnPoint.Y() ) / avgDirection.Y();
		}
		return ( origin[2] - lineEqnPoint.Z() ) / avgDirection.Z();
	}
};

class PlasmaBeamsBuilder
{
	StaticVector<SameDirBeamsList, 1024> sameDirLists;

	static constexpr float SQ_DANGER_RADIUS = 300.0f * 300.0f;

	const edict_t *bot;
	BotPerceptionManager *perceptionManager;

public:
	PlasmaBeamsBuilder( const edict_t *bot_, BotPerceptionManager *perceptionManager_ )
		: bot( bot_ ), perceptionManager( perceptionManager_ ) {}

	void AddProjectile( const edict_t *projectile );
	void FindMostDangerousBeams();
};

CachingGameBufferAllocator<EntAndLineParam, MAX_EDICTS> sortedProjectilesBufferAllocator( "prj" );
CachingGameBufferAllocator<PlasmaBeam, MAX_EDICTS> plasmaBeamsBufferAllocator( "beams" );

SameDirBeamsList::SameDirBeamsList( const edict_t *firstEntity, const edict_t *bot )
	: lineEqnPoint( firstEntity->s.origin ),
	sortedProjectiles( nullptr ),
	projectilesCount( 0 ),
	avgDirection( firstEntity->velocity ),
	plasmaBeams( nullptr ),
	plasmaBeamsCount( 0 ) {
	avgDirection.NormalizeFast();

	// If distance from an infinite line of beam to bot is greater than threshold, skip;
	// Let's compute distance from bot to the beam infinite line;
	Vec3 botOrigin( bot->s.origin );
	float squaredDistanceToBeamLine = ( botOrigin - lineEqnPoint ).Cross( avgDirection ).SquaredLength();
	if( squaredDistanceToBeamLine > DIST_TO_RAY_THRESHOLD * DIST_TO_RAY_THRESHOLD ) {
		isAlreadySkipped = true;
	} else {
		sortedProjectiles = sortedProjectilesBufferAllocator.Alloc();
		plasmaBeams = plasmaBeamsBufferAllocator.Alloc();

		isAlreadySkipped = false;

		sortedProjectiles[projectilesCount++] = EntAndLineParam( ENTNUM( firstEntity ), ComputeLineEqnParam( firstEntity ) );
	}
}

SameDirBeamsList::~SameDirBeamsList() {
	if( isAlreadySkipped ) {
		return;
	}
	// (Do not spam log by messages unless we have allocated memory chunks)
	if( sortedProjectiles ) {
		sortedProjectilesBufferAllocator.Free( sortedProjectiles );
	}
	if( plasmaBeams ) {
		plasmaBeamsBufferAllocator.Free( plasmaBeams );
	}
	sortedProjectiles = nullptr;
	plasmaBeams = nullptr;
}

bool SameDirBeamsList::TryAddProjectile( const edict_t *projectile ) {
	Vec3 direction( projectile->velocity );

	direction.NormalizeFast();

	if( direction.Dot( avgDirection ) < DIR_DOT_THRESHOLD ) {
		return false;
	}

	// Do not process a projectile, but "consume" it anyway...
	if( isAlreadySkipped ) {
		return true;
	}

	// Update average direction
	avgDirection += direction;
	avgDirection.NormalizeFast();

	sortedProjectiles[projectilesCount++] = EntAndLineParam( ENTNUM( projectile ), ComputeLineEqnParam( projectile ) );
	std::push_heap( sortedProjectiles, sortedProjectiles + projectilesCount );

	return true;
}

void SameDirBeamsList::BuildBeams() {
	if( isAlreadySkipped ) {
		return;
	}

	if( projectilesCount == 0 ) {
		AI_FailWith( "SameDirBeamsList::BuildBeams()", "Projectiles count: %d\n", projectilesCount );
	}

	const edict_t *const gameEdicts = game.edicts;

	// Get the projectile that has a maximal `t`
	std::pop_heap( sortedProjectiles, sortedProjectiles + projectilesCount );
	const edict_t *prevProjectile = gameEdicts + sortedProjectiles[--projectilesCount].entNum;

	plasmaBeams[plasmaBeamsCount++] = PlasmaBeam( prevProjectile );

	while( projectilesCount > 0 ) {
		// Get the projectile that has a maximal `t` atm
		std::pop_heap( sortedProjectiles, sortedProjectiles + projectilesCount );
		const edict_t *currProjectile = gameEdicts + sortedProjectiles[--projectilesCount].entNum;

		float prevToCurrLen = ( Vec3( prevProjectile->s.origin ) - currProjectile->s.origin ).SquaredLength();
		if( prevToCurrLen < PRJ_PROXIMITY_THRESHOLD * PRJ_PROXIMITY_THRESHOLD ) {
			// Add the projectile to the last beam
			plasmaBeams[plasmaBeamsCount - 1].AddProjectile( currProjectile );
		} else {
			// Construct new plasma beam at the end of beams array
			plasmaBeams[plasmaBeamsCount++] = PlasmaBeam( currProjectile );
		}
	}
}

void PlasmaBeamsBuilder::AddProjectile( const edict_t *projectile ) {
	for( unsigned i = 0; i < sameDirLists.size(); ++i ) {
		if( sameDirLists[i].TryAddProjectile( projectile ) ) {
			return;
		}
	}
	new ( sameDirLists.unsafe_grow_back() )SameDirBeamsList( projectile, bot );
}

void PlasmaBeamsBuilder::FindMostDangerousBeams() {
	trace_t trace;
	Vec3 botOrigin( bot->s.origin );

	for( unsigned i = 0; i < sameDirLists.size(); ++i ) {
		sameDirLists[i].BuildBeams();
	}

	const auto *weaponDef = GS_GetWeaponDef( WEAP_PLASMAGUN );
	const float splashRadius = 1.2f * 0.5f * ( weaponDef->firedef.splash_radius + weaponDef->firedef_weak.splash_radius );
	float minDamageScore = 0.0f;

	for( const SameDirBeamsList &beamsList: sameDirLists ) {
		if( beamsList.isAlreadySkipped ) {
			continue;
		}

		for( unsigned i = 0; i < beamsList.plasmaBeamsCount; ++i ) {
			PlasmaBeam *beam = beamsList.plasmaBeams + i;

			Vec3 botToBeamStart = beam->start() - botOrigin;
			Vec3 botToBeamEnd = beam->end() - botOrigin;

			if( botToBeamStart.SquaredLength() > SQ_DANGER_RADIUS && botToBeamEnd.SquaredLength() > SQ_DANGER_RADIUS ) {
				continue;
			}

			Vec3 beamStartToEnd = beam->end() - beam->start();

			float dotBotToStartWithDir = botToBeamStart.Dot( beamStartToEnd );
			float dotBotToEndWithDir = botToBeamEnd.Dot( beamStartToEnd );

			// If the beam has entirely passed the bot and is flying away, skip it
			if( dotBotToStartWithDir > 0 && dotBotToEndWithDir > 0 ) {
				continue;
			}

			Vec3 tracedBeamStart = beam->start();
			Vec3 tracedBeamEnd = beam->end();

			// It works for single-projectile beams too
			Vec3 beamDir( beam->startProjectile->velocity );
			beamDir.NormalizeFast();
			tracedBeamEnd += 256.0f * beamDir;

			G_Trace( &trace, tracedBeamStart.Data(), nullptr, nullptr, tracedBeamEnd.Data(), nullptr, MASK_AISOLID );
			if( trace.fraction == 1.0f ) {
				continue;
			}

			// Direct hit
			if( bot == game.edicts + trace.ent ) {
				float damageScore = beam->damage;
				if( damageScore > minDamageScore ) {
					if( perceptionManager->TryAddDanger( damageScore, trace.endpos,
														 beamsList.avgDirection.Data(),
														 beam->owner, 1.5f * splashRadius ) ) {
						minDamageScore = damageScore;
					}
				}
				continue;
			}

			// Splash hit
			float hitVecLen = botOrigin.FastDistanceTo( trace.endpos );
			if( hitVecLen < splashRadius ) {
				float damageScore = beam->damage * ( 1.0f - hitVecLen / splashRadius );
				if( damageScore > minDamageScore ) {
					if( perceptionManager->TryAddDanger( damageScore, trace.endpos,
														 beamsList.avgDirection.Data(),
														 beam->owner, 1.5f * splashRadius ) ) {
						minDamageScore = damageScore;
					}
				}
			}
		}
	}
}

BotPerceptionManager::BotPerceptionManager( edict_t *self_ )
	: entitiesDetector( self_ ),
	self( self_ ),
	primaryDanger( nullptr ),
	dangersPool( "dangersPool" ),
	jumppadUsersTracker( this ) {
	SetupEventHandlers();
}

bool BotPerceptionManager::TryAddDanger( float damageScore, const vec3_t hitPoint, const vec3_t direction,
										 const edict_t *owner, float splashRadius ) {
	if( primaryDanger ) {
		if( primaryDanger->damage >= damageScore ) {
			return false;
		}
	}

	if( Danger *danger = dangersPool.New() ) {
		danger->damage = damageScore;
		danger->hitPoint.Set( hitPoint );
		danger->direction.Set( direction );
		danger->attacker = owner;
		danger->splashRadius = splashRadius;
		if( primaryDanger ) {
			primaryDanger->DeleteSelf();
		}
		primaryDanger = danger;
		return true;
	}

	return false;
}


void BotPerceptionManager::ClearDangers() {
	if( primaryDanger ) {
		primaryDanger->DeleteSelf();
	}

	primaryDanger = nullptr;
}

// TODO: Do not detect dangers that may not be seen by bot, but make bot aware if it can hear the danger
void BotPerceptionManager::Think() {

	RegisterVisibleEnemies();
	ProcessEvents();

	if( primaryDanger && primaryDanger->IsValid() ) {
		return;
	}

	ClearDangers();

	EntitiesDetector entitiesDetector( self );
	entitiesDetector.Run();

	ResetTeammatesVisData();

	if( !entitiesDetector.dangerousRockets.empty() ) {
		FindProjectileDangers( entitiesDetector.dangerousRockets );
		TryGuessingProjectileOwnersOrigins( entitiesDetector.dangerousRockets, 0.0f );
	}

	TryGuessingProjectileOwnersOrigins( entitiesDetector.visibleOtherRockets, 0.0f );

	if( !entitiesDetector.dangerousWaves.empty() ) {
		FindWaveDangers( entitiesDetector.dangerousWaves );
		TryGuessingProjectileOwnersOrigins( entitiesDetector.dangerousWaves, 0.0f );
	}

	if( !entitiesDetector.dangerousBlasts.empty() ) {
		FindProjectileDangers( entitiesDetector.dangerousBlasts );
		TryGuessingProjectileOwnersOrigins( entitiesDetector.dangerousBlasts, 0.0f );
	}

	TryGuessingProjectileOwnersOrigins( entitiesDetector.visibleOtherBlasts, 0.0f );

	if( !entitiesDetector.dangerousGrenades.empty() ) {
		FindProjectileDangers( entitiesDetector.dangerousGrenades );
		TryGuessingProjectileOwnersOrigins( entitiesDetector.dangerousGrenades, 0.0f );
	}

	TryGuessingProjectileOwnersOrigins( entitiesDetector.visibleOtherGrenades, 0.0f );

	if( !entitiesDetector.dangerousPlasmas.empty() ) {
		FindPlasmaDangers( entitiesDetector.dangerousPlasmas );
		TryGuessingProjectileOwnersOrigins( entitiesDetector.dangerousPlasmas, 0.7f );
	}

	TryGuessingProjectileOwnersOrigins( entitiesDetector.visibleOtherPlasmas, 0.7f );

	if( !entitiesDetector.dangerousLasers.empty() ) {
		FindLaserDangers( entitiesDetector.dangerousLasers );
		TryGuessingBeamOwnersOrigins( entitiesDetector.dangerousLasers, 0.0f );
	}

	TryGuessingProjectileOwnersOrigins( entitiesDetector.visibleOtherLasers, 0.0f );

	// Set the primary danger timeout after all
	if( primaryDanger ) {
		primaryDanger->timeoutAt = level.time + Danger::TIMEOUT;
	}
}

void BotPerceptionManager::FindWaveDangers( const EntNumsVector &entNums ) {
	auto *const gameEdicts = game.edicts;
	const auto *weaponDef = GS_GetWeaponDef( WEAP_SHOCKWAVE );
	trace_t trace;
	for( auto entNum: entNums ) {
		edict_t *wave = gameEdicts + entNum;
		float dangerRadius;
		if( wave->style == MOD_SHOCKWAVE_S ) {
			dangerRadius = weaponDef->firedef.splash_radius + 32.0f;
		} else {
			dangerRadius = weaponDef->firedef_weak.splash_radius + 24.0f;
		}

		// We try checking whether the wave passes near the bot inflicting a corona damage.
		// TODO: This code assumes that the bot origin remains the same.
		// This is not so bad because dangers are checked each Think() frame
		// and there is some additional extent applied to the damage radius,
		// but it would be nice to predict an actual trajectory intersection.

		// Compute a distance from wave linear movement line to bot
		Vec3 lineDir( wave->velocity );
		float waveSpeed = lineDir.NormalizeFast();
		Vec3 botToLinePoint( wave->s.origin );
		botToLinePoint -= self->s.origin;
		Vec3 projection( lineDir );
		projection *= botToLinePoint.Dot( lineDir );
		Vec3 perpendicular( botToLinePoint );
		perpendicular -= projection;
		const float squareDistance =  perpendicular.SquaredLength();
		if( squareDistance > dangerRadius * dangerRadius ) {
			continue;
		}

		// We're sure the wave is in PVS and is visible by bot, that's what EntitiesDetector yields
		// Now check whether the wave hits an obstacle on a safe distance.

		Vec3 traceEnd( lineDir );
		traceEnd *= EntitiesDetector::DETECT_WAVE_RADIUS / waveSpeed;
		traceEnd += wave->s.origin;
		G_Trace( &trace, wave->s.origin, nullptr, nullptr, traceEnd.Data(), wave, MASK_SHOT );
		bool isDirectHit = false;
		if( trace.fraction != 1.0f ) {
			if( DistanceSquared( trace.endpos, self->s.origin ) > dangerRadius * dangerRadius ) {
				continue;
			}
			isDirectHit = ( trace.ent == ENTNUM( self ) );
		}
		// Put the likely case first
		float damage = wave->projectileInfo.maxDamage;
		if( !isDirectHit ) {
			float distance = SQRTFAST( squareDistance );
			float damageScore = damage * ( 3.0f - 2.0f * ( distance / dangerRadius ) );
			// Treat the nearest point on the line as a hit point
			// perpendicular = hitPoint - self->s.origin;
			Vec3 hitPoint( perpendicular );
			hitPoint += self->s.origin;
			Vec3 hitDir( perpendicular );
			hitDir *= 1.0f / distance;
			TryAddDanger( damageScore, hitPoint.Data(), hitDir.Data(), gameEdicts + wave->s.ownerNum, dangerRadius );
		} else {
			TryAddDanger( 3.0f * damage, trace.endpos, lineDir.Data(), gameEdicts + wave->s.ownerNum, dangerRadius );
		}
	}
}

void BotPerceptionManager::FindPlasmaDangers( const EntNumsVector &entNums ) {
	PlasmaBeamsBuilder plasmaBeamsBuilder( self, this );
	const edict_t *gameEdicts = game.edicts;

	for( unsigned i = 0; i < entNums.size(); ++i ) {
		plasmaBeamsBuilder.AddProjectile( gameEdicts + entNums[i] );
	}
	plasmaBeamsBuilder.FindMostDangerousBeams();
}

void BotPerceptionManager::FindLaserDangers( const EntNumsVector &entNums ) {
	trace_t trace;
	edict_t *const gameEdicts = game.edicts;
	float maxDamageScore = 0.0f;

	for( unsigned i = 0; i < entNums.size(); ++i ) {
		edict_t *beam = gameEdicts + entNums[i];
		G_Trace( &trace, beam->s.origin, vec3_origin, vec3_origin, beam->s.origin2, beam, MASK_AISOLID );
		if( trace.fraction == 1.0f ) {
			continue;
		}

		if( self != game.edicts + trace.ent ) {
			continue;
		}

		edict_t *owner = game.edicts + beam->s.ownerNum;

		Vec3 direction( beam->s.origin2 );
		direction -= beam->s.origin;
		float squareLen = direction.SquaredLength();
		if( squareLen > 1 ) {
			direction *= 1.0f / sqrtf( squareLen );
		} else {
			// Very rare but really seen case - beam has zero length
			vec3_t forward, right, up;
			AngleVectors( owner->s.angles, forward, right, up );
			direction += forward;
			direction += right;
			direction += up;
			direction.NormalizeFast();
		}

		// Modify potential damage from a beam by its owner accuracy
		float damageScore = 50.0f;
		if( owner->team != self->team && owner->r.client ) {
			const auto &ownerStats = owner->r.client->level.stats;
			if( ownerStats.accuracy_shots[AMMO_LASERS] > 10 ) {
				float extraDamage = 75.0f;
				extraDamage *= ownerStats.accuracy_hits[AMMO_LASERS];
				extraDamage /= ownerStats.accuracy_shots[AMMO_LASERS];
				damageScore += extraDamage;
			}
		}

		if( damageScore > maxDamageScore ) {
			if( TryAddDanger( damageScore, trace.endpos, direction.Data(), owner, 0.0f ) ) {
				maxDamageScore = damageScore;
			}
		}
	}
}

void BotPerceptionManager::FindProjectileDangers( const EntNumsVector &entNums ) {
	trace_t trace;
	float minPrjFraction = 1.0f;
	float minDamageScore = 0.0f;
	Vec3 botOrigin( self->s.origin );
	edict_t *const gameEdicts = game.edicts;

	for( unsigned i = 0; i < entNums.size(); ++i ) {
		edict_t *target = gameEdicts + entNums[i];
		Vec3 end = Vec3( target->s.origin ) + 2.0f * Vec3( target->velocity );
		G_Trace( &trace, target->s.origin, target->r.mins, target->r.maxs, end.Data(), target, MASK_AISOLID );
		if( trace.fraction >= minPrjFraction ) {
			continue;
		}

		minPrjFraction = trace.fraction;
		float hitVecLen = botOrigin.FastDistanceTo( trace.endpos );
		if( hitVecLen >= 1.25f * target->projectileInfo.radius ) {
			continue;
		}

		float damageScore = target->projectileInfo.maxDamage;
		damageScore *= 1.0f - hitVecLen / ( 1.25f * target->projectileInfo.radius );
		if( damageScore <= minDamageScore ) {
			continue;
		}

		// Velocity may be zero for some projectiles (e.g. grenades)
		Vec3 direction( target->velocity );
		float squaredLen = direction.SquaredLength();
		if( squaredLen > 0.1f ) {
			direction *= 1.0f / sqrtf( squaredLen );
		} else {
			direction = Vec3( &axis_identity[AXIS_UP] );
		}
		if( TryAddDanger( damageScore, trace.endpos, direction.Data(),
						  gameEdicts + target->s.ownerNum,
						  1.25f * target->projectileInfo.radius ) ) {
			minDamageScore = damageScore;
		}
	}
}

static bool IsEnemyVisible( const edict_t *self, const edict_t *enemyEnt ) {
	trace_t trace;
	edict_t *const gameEdicts = game.edicts;
	edict_t *ignore = gameEdicts + ENTNUM( self );

	Vec3 traceStart( self->s.origin );
	traceStart.Z() += self->viewheight;
	Vec3 traceEnd( enemyEnt->s.origin );

	G_Trace( &trace, traceStart.Data(), nullptr, nullptr, traceEnd.Data(), ignore, MASK_OPAQUE );
	if( trace.fraction == 1.0f || gameEdicts + trace.ent == enemyEnt ) {
		return true;
	}

	vec3_t dims;
	if( enemyEnt->r.client ) {
		// We're sure clients in-game have quite large and well-formed hitboxes, so no dimensions test is required.
		// However we have a much more important test to do.
		// If this point usually corresponding to an enemy chest/weapon is not
		// considered visible for a bot but is really visible, the bot behavior looks weird.
		// That's why this special test is added.

		// If the view height makes a considerable spatial distinction
		if( abs( enemyEnt->viewheight ) > 8 ) {
			traceEnd.Z() += enemyEnt->viewheight;
			G_Trace( &trace, traceStart.Data(), nullptr, nullptr, traceEnd.Data(), ignore, MASK_OPAQUE );
			if( trace.fraction == 1.0f || gameEdicts + trace.ent == enemyEnt ) {
				return true;
			}
		}

		// We have deferred dimensions computations to a point after trace call.
		for( int i = 0; i < 3; ++i ) {
			dims[i] = enemyEnt->r.maxs[i] - enemyEnt->r.mins[i];
		}
	}
	else {
		for( int i = 0; i < 3; ++i ) {
			dims[i] = enemyEnt->r.maxs[i] - enemyEnt->r.mins[i];
		}
		// Prevent further testing in degenerate case (there might be non-player enemies).
		if( !dims[0] || !dims[1] || !dims[2] ) {
			return false;
		}
		if( std::max( dims[0], std::max( dims[1], dims[2] ) ) < 8 ) {
			return false;
		}
	}

	// Try testing 4 corners of enemy projection onto bot's "view".
	// It is much less expensive that testing all 8 corners of the hitbox.

	Vec3 enemyToBotDir( self->s.origin );
	enemyToBotDir -= enemyEnt->s.origin;
	enemyToBotDir.NormalizeFast();

	vec3_t right, up;
	MakeNormalVectors( enemyToBotDir.Data(), right, up );

	// Add some inner margin to the hitbox (a real model is less than it and the computations are coarse).
	const float sideOffset = ( 0.8f * std::min( dims[0], dims[1] ) ) / 2;
	float zOffset[2] = { enemyEnt->r.maxs[2] - 0.1f * dims[2], enemyEnt->r.mins[2] + 0.1f * dims[2] };
	// Switch the side from left to right
	for( int i = -1; i <= 1; i += 2 ) {
		// Switch Z offset
		for( int j = 0; j < 2; j++ ) {
			// traceEnd = Vec3( enemyEnt->s.origin ) + i * sideOffset * right;
			traceEnd.Set( right );
			traceEnd *= i * sideOffset;
			traceEnd += enemyEnt->s.origin;
			traceEnd.Z() += zOffset[j];
			G_Trace( &trace, traceStart.Data(), nullptr, nullptr, traceEnd.Data(), ignore, MASK_OPAQUE );
			if( trace.fraction == 1.0f || gameEdicts + trace.ent == enemyEnt ) {
				return true;
			}
		}
	}

	return false;
}

void BotPerceptionManager::TryGuessingBeamOwnersOrigins( const EntNumsVector &dangerousEntsNums, float failureChance ) {
	const edict_t *const gameEdicts = game.edicts;
	auto *const bot = self->ai->botRef;
	for( auto entNum: dangerousEntsNums ) {
		if( random() < failureChance ) {
			continue;
		}
		const edict_t *owner = &gameEdicts[gameEdicts[entNum].s.ownerNum];
		if( bot->MayNotBeFeasibleEnemy( owner ) ) {
			continue;
		}
		if( CanDistinguishEnemyShotsFromTeammates( owner ) ) {
			bot->OnEnemyOriginGuessed( owner, 128 );
		}
	}
}

void BotPerceptionManager::TryGuessingProjectileOwnersOrigins( const EntNumsVector &dangerousEntNums, float failureChance ) {
	const edict_t *const gameEdicts = game.edicts;
	const int64_t levelTime = level.time;
	auto *const bot = self->ai->botRef;
	for( auto entNum: dangerousEntNums ) {
		if( random() < failureChance ) {
			continue;
		}
		const edict_t *projectile = &gameEdicts[entNum];
		const edict_t *owner = &gameEdicts[projectile->s.ownerNum];
		if( bot->MayNotBeFeasibleEnemy( owner ) ) {
			continue;
		}

		if( projectile->s.linearMovement ) {
			// This test is expensive, do it after cheaper ones have succeeded.
			if( CanDistinguishEnemyShotsFromTeammates( projectile->s.linearMovementBegin ) ) {
				bot->OnEnemyOriginGuessed( owner, 256, projectile->s.linearMovementBegin );
			}
			return;
		}

		// Can't guess in this case
		if( projectile->s.type != ET_GRENADE ) {
			return;
		}

		unsigned timeout = GS_GetWeaponDef( WEAP_GRENADELAUNCHER )->firedef.timeout;
		// Can't guess in this case
		if( projectile->nextThink < levelTime + timeout / 2 ) {
			continue;
		}

		// This test is expensive, do it after cheaper ones have succeeded.
		if( CanDistinguishEnemyShotsFromTeammates( owner ) ) {
			// Use the exact enemy origin as a guessed one
			bot->OnEnemyOriginGuessed( owner, 384 );
		}
	}
}

void BotPerceptionManager::RegisterVisibleEnemies() {
	if( GS_MatchState() == MATCH_STATE_COUNTDOWN || GS_ShootingDisabled() ) {
		return;
	}

	// Compute look dir before loop
	vec3_t lookDir;
	AngleVectors( self->s.angles, lookDir, nullptr, nullptr );

	const float dotFactor = self->ai->botRef->FovDotFactor();

	// Note: non-client entities also may be candidate targets.
	StaticVector<EntAndDistance, MAX_EDICTS> candidateTargets;

	edict_t *const gameEdicts = game.edicts;
	for( int i = 1; i < game.numentities; ++i ) {
		edict_t *ent = gameEdicts + i;
		if( self->ai->botRef->MayNotBeFeasibleEnemy( ent ) ) {
			continue;
		}

		// Reject targets quickly by fov
		Vec3 toTarget( ent->s.origin );
		toTarget -= self->s.origin;
		float squareDistance = toTarget.SquaredLength();
		if( squareDistance < 1 ) {
			continue;
		}
		if( squareDistance > ent->aiVisibilityDistance * ent->aiVisibilityDistance ) {
			continue;
		}

		float invDistance = Q_RSqrt( squareDistance );
		toTarget *= invDistance;
		if( toTarget.Dot( lookDir ) < dotFactor ) {
			continue;
		}

		// It seams to be more instruction cache-friendly to just add an entity to a plain array
		// and sort it once after the loop instead of pushing an entity in a heap on each iteration
		candidateTargets.emplace_back( EntAndDistance( ENTNUM( ent ), 1.0f / invDistance ) );
	}

	StaticVector<uint16_t, MAX_CLIENTS> visibleTargets;
	entitiesDetector.FilterRawEntitiesWithDistances( candidateTargets, visibleTargets, MAX_CLIENTS,
													 IsGenericEntityInPvs, IsEnemyVisible );

	auto *const threatTracker = &self->ai->botRef->threatTracker;
	for( auto entNum: visibleTargets )
		threatTracker->OnEnemyViewed( gameEdicts + entNum );

	threatTracker->AfterAllEnemiesViewed();

	self->ai->botRef->CheckAlertSpots( visibleTargets );
}

template<unsigned N, unsigned M, typename PvsFunc, typename VisFunc>
bool EntitiesDetector::FilterRawEntitiesWithDistances( StaticVector<EntAndDistance, N> &rawEnts,
													   StaticVector<uint16_t, M> &filteredEnts,
													   unsigned visEntsLimit,
													   PvsFunc pvsFunc, VisFunc visFunc ) {
	filteredEnts.clear();

	// Do not call inPVS() and G_Visible() inside a single loop for all raw ents.
	// Sort all entities by distance to the bot.
	// Then select not more than visEntsLimit nearest entities in PVS, then call visFunc().
	// It may cause data loss (far entities that may have higher logical priority),
	// but in a common good case (when there are few visible entities) it preserves data,
	// and in the worst case mentioned above it does not act weird from player POV and prevents server hang up.

	std::sort( rawEnts.begin(), rawEnts.end() );

	const edict_t *gameEdicts = game.edicts;

	StaticVector<uint16_t, M> entsInPvs;
	bool result = true;
	unsigned limit = rawEnts.size();
	if( limit > entsInPvs.capacity() ) {
		limit = entsInPvs.capacity();
		result = false;
	}
	if( limit > visEntsLimit ) {
		limit = visEntsLimit;
		result = false;
	}

	for( unsigned i = 0; i < limit; ++i ) {
		uint16_t entNum = (uint16_t)rawEnts[i].entNum;
		if( pvsFunc( self, gameEdicts + entNum ) ) {
			entsInPvs.push_back( entNum );
		}
	}

	for( auto entNum: entsInPvs ) {
		const edict_t *ent = gameEdicts + entNum;
		if( visFunc( self, ent ) ) {
			filteredEnts.push_back( entNum );
		}
	}

	return result;
};

void BotPerceptionManager::ResetTeammatesVisData() {
	numTestedTeamMates = 0;
	hasComputedTeammatesVisData = false;
	static_assert( sizeof( *teammatesVisStatus ) == 1, "" );
	static_assert( sizeof( teammatesVisStatus ) == MAX_CLIENTS, "" );
	std::fill_n( teammatesVisStatus, MAX_CLIENTS, -1 );
}

void BotPerceptionManager::ComputeTeammatesVisData( const vec3_t forwardDir, float fovDotFactor ) {
	numTestedTeamMates = 0;
	areAllTeammatesInFov = true;
	const int numMates = teamlist[self->s.team].numplayers;
	const int *mateNums = teamlist[self->s.team].playerIndices;
	const auto *gameEdicts = game.edicts;
	for( int i = 0; i < numMates; ++i ) {
		const edict_t *mate = gameEdicts + mateNums[i];
		if( mate == self || G_ISGHOSTING( mate ) ) {
			continue;
		}
		Vec3 dir( mate->s.origin );
		dir -= self->s.origin;
		distancesToTeammates[i] = dir.NormalizeFast();
		float dot = dir.Dot( forwardDir );
		if( dot < fovDotFactor ) {
			areAllTeammatesInFov = false;
		}
		viewDirDotTeammateDir[numTestedTeamMates] = dot;
		testedTeammatePlayerNums[numTestedTeamMates] = (uint8_t)PLAYERNUM( mate );
		numTestedTeamMates++;
	}
	hasComputedTeammatesVisData = true;
}

bool BotPerceptionManager::CanDistinguishEnemyShotsFromTeammates( const GuessedEnemy &guessedEnemy ) {
	if ( !GS_TeamBasedGametype() ) {
		return true;
	}

	trace_t trace;

	Vec3 toEnemyDir( guessedEnemy.origin );
	toEnemyDir -= self->s.origin;
	const float distanceToEnemy = toEnemyDir.NormalizeFast();

	const auto *gameEdicts = game.edicts;
	const Vec3 forwardDir( self->ai->botRef->entityPhysicsState->ForwardDir() );
	const float fovDotFactor = self->ai->botRef->FovDotFactor();

	// Compute vis data lazily
	if( !hasComputedTeammatesVisData ) {
		ComputeTeammatesVisData( forwardDir.Data(), fovDotFactor );
		hasComputedTeammatesVisData = true;
	}

	const bool canShowMinimap = GS_CanShowMinimap();
	// If the bot can't see the guessed enemy origin
	if( toEnemyDir.Dot( forwardDir ) < fovDotFactor ) {
		if( areAllTeammatesInFov ) {
			return true;
		}
		// Try using a minimap to make the distinction.
		for( unsigned i = 0; i < numTestedTeamMates; ++i ) {
			if( viewDirDotTeammateDir[i] >= fovDotFactor ) {
				continue;
			}
			const edict_t *mate = gameEdicts + testedTeammatePlayerNums[i] + 1;
			if( !canShowMinimap ) {
				return false;
			}

			if( DistanceSquared( mate->s.origin, guessedEnemy.origin ) > 300 * 300 ) {
				continue;
			}

			// A mate is way too close to the guessed origin.
			// Check whether there is a wall that helps to make the distinction.

			if( guessedEnemy.AreInPvsWith( self ) ) {
				SolidWorldTrace( &trace, mate->s.origin, guessedEnemy.origin );
				if( trace.fraction == 1.0f ) {
					return false;
				}
			}
		}
		return true;
	}

	const auto *pvsCache = EntitiesPvsCache::Instance();
	const float viewDotEnemy = forwardDir.Dot( toEnemyDir );
	for( unsigned i = 0; i < numTestedTeamMates; ++i ) {
		const float viewDotTeammate = viewDirDotTeammateDir[i];
		const edict_t *mate = gameEdicts + testedTeammatePlayerNums[i] + 1;
		// The bot can't see or hear the teammate. Try using a minimap to make the distinction.
		if( viewDotTeammate <= fovDotFactor ) {
			// A mate is way too close to the guessed origin.
			if( DistanceSquared( mate->s.origin, guessedEnemy.origin ) < 300 * 300 ) {
				if( !canShowMinimap ) {
					return false;
				}

				// Check whether there is a wall that helps to make the distinction.
				if( guessedEnemy.AreInPvsWith( self ) ) {
					SolidWorldTrace( &trace, mate->s.origin, guessedEnemy.origin );
					if( trace.fraction == 1.0f ) {
						return false;
					}
				}
			}
			continue;
		}

		// In this case it's guaranteed that the enemy cannot be distinguished from a teammate
		if( fabsf( viewDotEnemy - viewDotTeammate ) < 0.9f ) {
			continue;
		}

		// Test teammate visibility status lazily
		if( teammatesVisStatus[i] < 0 ) {
			teammatesVisStatus[i] = 0;
			if( pvsCache->AreInPvs( self, mate ) ) {
				Vec3 viewPoint( self->s.origin );
				viewPoint.Z() += self->viewheight;
				SolidWorldTrace( &trace, viewPoint.Data(), mate->s.origin );
				if( trace.fraction == 1.0f ) {
					teammatesVisStatus[i] = 1;
				}
			}
		}

		// Can't say much if the teammate is not visible.
		// We can test visibility of the guessed origin but its way too expensive.
		// (there might be redundant computations overlapping with the normal bot vision).
		if( teammatesVisStatus[i] < 0 ) {
			return false;
		}

		if( distanceToEnemy > 2048 ) {
			// A teammate probably blocks enemy on the line of sight
			if( distancesToTeammates[i] < distanceToEnemy ) {
				return false;
			}
		}
	}

	return true;
}

bool BotPerceptionManager::GuessedEnemyEnt::AreInPvsWith( const edict_t *botEnt ) const {
	return EntitiesPvsCache::Instance()->AreInPvs( botEnt, ent );
}

bool BotPerceptionManager::GuessedEnemyOrigin::AreInPvsWith( const edict_t *botEnt ) const {
	if( botEnt->r.num_clusters < 0 ) {
		return trap_inPVS( botEnt->s.origin, this->origin );
	}

	// Compute leafs for the own origin lazily
	if( !numLeafs ) {
		vec3_t mins = { -4, -4, -4 };
		vec3_t maxs = { +4, +4, +4 };
		int topnode;
		numLeafs = trap_CM_BoxLeafnums( mins, maxs, leafNums, 4, &topnode );
		// Filter out solid leafs
		for( int i = 0; i < numLeafs; ) {
			if( trap_CM_LeafCluster( leafNums[i] ) >= 0 ) {
				i++;
			} else {
				numLeafs--;
				leafNums[i] = leafNums[numLeafs];
			}
		}
	}

	const int *botLeafNums = botEnt->r.leafnums;
	for( int i = 0, end = botEnt->r.num_clusters; i < end; ++i ) {
		for( int j = 0; j < this->numLeafs; ++j ) {
			if( trap_CM_LeafsInPVS( botLeafNums[i], this->leafNums[j] ) ) {
				return true;
			}
		}
	}

	return false;
}

void BotPerceptionManager::HandleGenericPlayerEntityEvent( const edict_t *player, float distanceThreshold ) {
	if( CanPlayerBeHeardAsEnemy( player, distanceThreshold ) ) {
		PushEnemyEventOrigin( player, player->s.origin );
	}
}

class CachedEventsToPlayersMap {
	struct alignas ( 4 )Entry {
		Int64Align4 computedAt;
		// If we use lesser types the rest of 4 bytes would be lost for alignment anyway
		int32_t playerEntNum;
	};

	Entry entries[MAX_EDICTS];
public:
	CachedEventsToPlayersMap() {
		memset( entries, 0, sizeof( entries ) );
	}

	int PlayerEntNumForEvent( int eventEntNum );

	const edict_t *PlayerEntForEvent( int eventEntNum ) {
		if( int entNum = PlayerEntNumForEvent( eventEntNum ) ) {
			return game.edicts + entNum;
		}
		return nullptr;
	}

	const edict_t *PlayerEntForEvent( const edict_t *event ) {
		assert( event->s.type == ET_EVENT );
		return PlayerEntForEvent( ENTNUM( event ) );
	}
};

static CachedEventsToPlayersMap eventsToPlayersMap;

int CachedEventsToPlayersMap::PlayerEntNumForEvent( int eventEntNum ) {
	const int64_t levelTime = level.time;
	const edict_t *gameEdicts = game.edicts;
	Entry *const entry = &entries[eventEntNum];
	if( entry->computedAt == levelTime ) {
		return entry->playerEntNum;
	}

	const edict_t *event = &gameEdicts[eventEntNum];
	Vec3 mins( event->s.origin );
	Vec3 maxs( event->s.origin );
	mins += playerbox_stand_mins;
	maxs += playerbox_stand_maxs;

	int entNums[16];
	int numEnts = GClip_FindInRadius( const_cast<float *>( event->s.origin ), 16.0f, entNums, 16 );
	for( int i = 0; i < numEnts; ++i ) {
		const edict_t *ent = gameEdicts + entNums[i];
		if( !ent->r.client || G_ISGHOSTING( ent ) ) {
			continue;
		}

		if( !VectorCompare( ent->s.origin, event->s.origin ) ) {
			continue;
		}

		entry->computedAt = levelTime;
		entry->playerEntNum = entNums[i];
		return entNums[i];
	}

	entry->computedAt = levelTime;
	entry->playerEntNum = 0;
	return 0;
}

void BotPerceptionManager::HandleGenericEventAtPlayerOrigin( const edict_t *ent, float distanceThreshold ) {
	if( DistanceSquared( self->s.origin, ent->s.origin ) > distanceThreshold * distanceThreshold ) {
		return;
	}

	const edict_t *player = eventsToPlayersMap.PlayerEntForEvent( ent );
	if( !player ) {
		return;
	}

	if( self->s.team == player->s.team && !GS_TeamBasedGametype() ) {
		return;
	}

	assert( VectorCompare( ent->s.origin, player->s.origin ) );
	PushEnemyEventOrigin( player, player->s.origin );
}

void BotPerceptionManager::HandleGenericImpactEvent( const edict_t *event, float visibleDistanceThreshold ) {
	if( !CanEntityBeHeardAsEnemy( event, visibleDistanceThreshold ) ) {
		return;
	}

	// Throttle plasma impacts
	if( event->s.events[event->numEvents & 1] == EV_PLASMA_EXPLOSION && random() > 0.3f ) {
		return;
	}

	// TODO: Throttle PVS/trace calls
	if( EntitiesPvsCache::Instance()->AreInPvs( self, event ) ) {
		trace_t trace;
		Vec3 viewPoint( self->s.origin );
		viewPoint.Z() += self->viewheight;
		SolidWorldTrace( &trace, viewPoint.Data(), event->s.origin );
		// Not sure if the event entity is on a solid plane.
		// Do not check whether the fraction equals 1.0f
		if( DistanceSquared( trace.endpos, event->s.origin ) < 1.0f * 1.0f ) {
			// We can consider the explosion visible.
			// In this case cheat a bit, get the actual owner origin.
			const edict_t *owner = game.edicts + event->s.ownerNum;
			PushEnemyEventOrigin( owner, owner->s.origin );
			return;
		}
	}

	// If we can't see the impact, use try hear it
	if( DistanceSquared( self->s.origin, event->s.origin ) < 512.0f * 512.0f ) {
		// Let enemy origin be the explosion origin in this case
		PushEnemyEventOrigin( game.edicts + event->s.ownerNum, event->s.origin );
	}
}

void BotPerceptionManager::HandleJumppadEvent( const edict_t *player, float ) {
	// A trajectory of a jumppad user is predictable in most cases.
	// So we track a user and push updates using its real origin without an obvious cheating.

	// If a player uses a jumppad, a player sound is emitted, so we can distinguish enemies from teammates.
	// Just check whether we can hear the sound.
	if( CanPlayerBeHeardAsEnemy( player, 512.0f * ( 1.0f + 2.0f * self->ai->botRef->Skill() ) ) ) {
		jumppadUsersTracker.Register( player );
	}
}

class TeleportTriggersDestCache {
	uint16_t destEntNums[MAX_EDICTS];
public:
	TeleportTriggersDestCache() {
		memset( destEntNums, 0, sizeof( destEntNums ) );
	}

	const float *GetTeleportDest( int entNum );
};

static TeleportTriggersDestCache teleportTriggersDestCache;

const float *TeleportTriggersDestCache::GetTeleportDest( int entNum ) {
	const edict_t *gameEdicts = game.edicts;
	const edict_t *ent = gameEdicts + entNum;
	assert( ent->classname && !Q_stricmp( ent->classname, "trigger_teleport" ) );

	if ( int destEntNum = destEntNums[entNum] ) {
		return gameEdicts[destEntNum].s.origin;
	}

	if( const edict_t *destEnt = G_Find( nullptr, FOFS( targetname ), ent->target ) ) {
		destEntNums[entNum] = (uint16_t)ENTNUM( destEnt );
		return destEnt->s.origin;
	}

	// We do not cache a result of failure. However these broken triggers should not be met often.
	return nullptr;
}

class PlayerTeleOutEventsCache {
	struct Entry {
		Int64Align4 computedAt;
		uint16_t triggerEntNum;
		uint16_t playerEntNum;
	} entries[MAX_EDICTS];
public:
	PlayerTeleOutEventsCache() {
		memset( entries, 0, sizeof( entries ) );
	}

	const edict_t *GetPlayerAndDestOriginByEvent( const edict_t *teleportOutEvent, const float **origin );
};

static PlayerTeleOutEventsCache playerTeleOutEventsCache;

const edict_t *PlayerTeleOutEventsCache::GetPlayerAndDestOriginByEvent( const edict_t *teleportOutEvent, const float **origin ) {
	assert( teleportOutEvent->s.type == ET_EVENT );

	Entry *const entry = &entries[ENTNUM( teleportOutEvent )];
	const int64_t levelTime = level.time;
	const edict_t *gameEdicts = game.edicts;

	if( entry->computedAt == levelTime ) {
		if( entry->triggerEntNum && entry->playerEntNum ) {
			if( const float *dest = teleportTriggersDestCache.GetTeleportDest( entry->triggerEntNum ) ) {
				*origin = dest;
				return gameEdicts + entry->playerEntNum;
			}
		}
		return nullptr;
	}

	int entNums[16];
	int triggerEntNum = 0, playerEntNum = 0;
	int numEnts = GClip_FindInRadius( const_cast<float *>( teleportOutEvent->s.origin ), 64.0f, entNums, 16 );
	for( int i = 0; i < numEnts; ++i ) {
		const edict_t *ent = gameEdicts + entNums[i];
		if( ent->classname && !Q_stricmp( ent->classname, "trigger_teleport" ) ) {
			triggerEntNum = entNums[i];
		} else if ( ent->r.client && VectorCompare( ent->s.origin, teleportOutEvent->s.origin ) ) {
			playerEntNum = entNums[i];
		}
	}

	if( !( triggerEntNum && playerEntNum ) ) {
		// Set both ent nums to zero even if some is not to speed up further calls on cached data
		entry->triggerEntNum = 0;
		entry->playerEntNum = 0;
		entry->computedAt = levelTime;
		return nullptr;
	}

	entry->triggerEntNum = (uint16_t)triggerEntNum;
	entry->playerEntNum = (uint16_t)playerEntNum;
	entry->computedAt = levelTime;

	if( const float *dest = teleportTriggersDestCache.GetTeleportDest( triggerEntNum ) ) {
		*origin = dest;
		return game.edicts + playerEntNum;
	}

	return nullptr;
}

void BotPerceptionManager::HandlePlayerTeleportOutEvent( const edict_t *ent, float ) {
	float distanceThreshold = 512.0f * ( 2.0f + 1.0f * self->ai->botRef->Skill() );
	if( DistanceSquared( self->s.origin, ent->s.origin ) > distanceThreshold * distanceThreshold ) {
		return;
	}

	// This event is spawned when the player has not been teleported yet,
	// so we do not know an actual origin after teleportation and have to look at the trigger destination.

	const float *teleportDest;
	const edict_t *player = playerTeleOutEventsCache.GetPlayerAndDestOriginByEvent( ent, &teleportDest );
	if( !player ) {
		return;
	}

	if( self->s.team == ent->s.team && GS_TeamBasedGametype() ) {
		return;
	}

	PushEnemyEventOrigin( player, teleportDest );
}

void BotPerceptionManager::JumppadUsersTracker::Think() {
	// Report new origins of tracked players.

	Bot *bot = perceptionManager->self->ai->botRef;
	const edict_t *gameEdicts = game.edicts;
	for( int i = 0, end = gs.maxclients; i < end; ++i ) {
		if( !isTrackedUser[i] ) {
			continue;
		}
		const edict_t *ent = gameEdicts + i + 1;
		// Check whether a player cannot be longer a valid entity
		if( bot->MayNotBeFeasibleEnemy( ent ) ) {
			isTrackedUser[i] = false;
			continue;
		}
		bot->OnEnemyOriginGuessed( ent, 128 );
	}
}

void BotPerceptionManager::JumppadUsersTracker::Frame() {
	// Stop tracking landed players.
	// It should be done each frame since a player might continue bunnying after landing,
	// and we might miss all frames when the player is on ground keeping tracking it forever.

	const edict_t *gameEdicts = game.edicts;
	for( int i = 0, end = gs.maxclients; i < end; ++i ) {
		if( !isTrackedUser[i] ) {
			continue;
		}
		if( gameEdicts[i + 1].groundentity ) {
			isTrackedUser[i] = false;
		}
	}
}

void BotPerceptionManager::RegisterEvent( const edict_t *ent, int event, int parm ) {
	( this->*eventHandlers[event])( ent, this->eventHandlingParams[event] );
}

void BotPerceptionManager::SetupEventHandlers() {
	for( int i = 0; i < MAX_EVENTS; ++i ) {
		SetEventHandler( i, &BotPerceptionManager::HandleDummyEvent );
	}

	// Note: radius values are a bit lower than it is expected for a human perception,
	// but otherwise bots behave way too hectic detecting all minor events

	for( int i : { EV_FIREWEAPON, EV_SMOOTHREFIREWEAPON } ) {
		SetEventHandler( i, &BotPerceptionManager::HandleGenericPlayerEntityEvent, 768.0f );
	}
	SetEventHandler( EV_WEAPONACTIVATE, &BotPerceptionManager::HandleGenericPlayerEntityEvent, 512.0f );
	SetEventHandler( EV_NOAMMOCLICK, &BotPerceptionManager::HandleGenericPlayerEntityEvent, 256.0f );

	// TODO: We currently skip weapon beam-like events.
	// This kind of events is extremely expensive to check for visibility.

	for( int i : { EV_DASH, EV_WALLJUMP, EV_WALLJUMP_FAILED, EV_DOUBLEJUMP, EV_JUMP } ) {
		SetEventHandler( i, &BotPerceptionManager::HandleGenericPlayerEntityEvent, 512.0f );
	}

	SetEventHandler( EV_JUMP_PAD, &BotPerceptionManager::HandleJumppadEvent );

	SetEventHandler( EV_FALL, &BotPerceptionManager::HandleGenericPlayerEntityEvent, 512.0f );

	for( int i : { EV_PLAYER_RESPAWN, EV_PLAYER_TELEPORT_IN } ) {
		SetEventHandler( i, &BotPerceptionManager::HandleGenericEventAtPlayerOrigin, 768.0f );
	}

	SetEventHandler( EV_PLAYER_TELEPORT_OUT, &BotPerceptionManager::HandlePlayerTeleportOutEvent );

	for( int i : { EV_GUNBLADEBLAST_IMPACT, EV_PLASMA_EXPLOSION, EV_BOLT_EXPLOSION, EV_INSTA_EXPLOSION } ) {
		SetEventHandler( i, &BotPerceptionManager::HandleGenericImpactEvent, 1024.0f + 256.0f );
	}

	for( int i : { EV_ROCKET_EXPLOSION, EV_GRENADE_EXPLOSION, EV_GRENADE_BOUNCE } ) {
		SetEventHandler( i, &BotPerceptionManager::HandleGenericImpactEvent, 2048.0f );
	}

	// TODO: Track platform users (almost the same as with jumppads)
	// TODO: React to door activation
}

void BotPerceptionManager::ProcessEvents() {
	// We have to validate detected enemies since the events queue
	// has been accumulated during the Think() frames cycle
	// and might contain outdated info (e.g. a player has changed its team).
	const auto *gameEdicts = game.edicts;
	Bot *bot = self->ai->botRef;
	for( const auto &detectedEvent: eventsQueue ) {
		const edict_t *ent = gameEdicts + detectedEvent.enemyEntNum;
		if( bot->MayNotBeFeasibleEnemy( ent ) ) {
			continue;
		}
		bot->OnEnemyOriginGuessed( ent, 96, detectedEvent.origin );
	}

	eventsQueue.clear();
}

