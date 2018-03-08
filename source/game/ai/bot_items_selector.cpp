#include "bot_items_selector.h"
#include "bot.h"
#include "ai_objective_based_team.h"

void BotItemsSelector::UpdateInternalItemAndGoalWeights() {
	memset( internalEntityWeights, 0, sizeof( internalEntityWeights ) );
	memset( internalPickupGoalWeights, 0, sizeof( internalPickupGoalWeights ) );

	// Compute it once, not on each loop step
	bool onlyGotGB = true;
	for( int weapon = WEAP_GUNBLADE + 1; weapon < WEAP_TOTAL; ++weapon ) {
		if( Inventory()[weapon] ) {
			onlyGotGB = false;
			break;
		}
	}

	const edict_t *objectiveSpotEntity = nullptr;
	const auto &spotDef = self->ai->botRef->objectiveSpotDef;
	if( spotDef.IsActive() ) {
		const auto *team = AiBaseTeam::GetTeamForNum( self->s.team );
		if( const auto *objectiveBasedTeam = dynamic_cast<const AiObjectiveBasedTeam *>( team ) ) {
			objectiveSpotEntity = objectiveBasedTeam->GetSpotUnderlyingEntity( spotDef.id, spotDef.isDefenceSpot );
		}
	}

	const auto levelTime = level.time;
	auto *navEntitiesRegistry = NavEntitiesRegistry::Instance();
	for( auto it = navEntitiesRegistry->begin(), end = navEntitiesRegistry->end(); it != end; ++it ) {
		const NavEntity *goalEnt = *it;
		// Picking clients as goal entities is currently disabled
		if( goalEnt->IsClient() ) {
			continue;
		}

		// Do not even try to compute a weight for the disabled item
		if( disabledForSelectionUntil[goalEnt->Id()] >= levelTime ) {
			internalEntityWeights[goalEnt->Id()] = 0;
			internalPickupGoalWeights[goalEnt->Id()] = 0;
			continue;
		}

		if( goalEnt->Item() ) {
			ItemAndGoalWeights weights = ComputeItemWeights( goalEnt->Item(), onlyGotGB );
			internalEntityWeights[goalEnt->Id()] = weights.itemWeight;
			internalPickupGoalWeights[goalEnt->Id()] = weights.goalWeight;
			continue;
		}

		if( goalEnt->IsBasedOnEntity( objectiveSpotEntity ) ) {
			internalEntityWeights[goalEnt->Id()] = spotDef.navWeight;
			internalPickupGoalWeights[goalEnt->Id()] = spotDef.goalWeight;
			continue;
		}
	}
}

BotItemsSelector::ItemAndGoalWeights BotItemsSelector::ComputeItemWeights( const gsitem_t *item, bool onlyGotGB ) const {
	switch( item->type ) {
		case IT_WEAPON: return ComputeWeaponWeights( item, onlyGotGB );
		case IT_AMMO: return ComputeAmmoWeights( item );
		case IT_HEALTH: return ComputeHealthWeights( item );
		case IT_ARMOR: return ComputeArmorWeights( item );
		case IT_POWERUP: return ComputePowerupWeights( item );
	}

	// Collect ammo packs too.
	// Checking an actual pack contents might sound better, but:
	// 1) It complicates the item selection code that is likely to be reworked anyway.
	// 2) It adds some degree of cheating (a bot knows exact pack contents in this case)
	if( item->tag == AMMO_PACK || item->tag == AMMO_PACK_STRONG || item->tag == AMMO_PACK_WEAK ) {
		// These weights are relatively large for this kind of item,
		// but we guess ammo packs are valuable in gametypes where they might be dropped.
		return ItemAndGoalWeights( 0.75f, 0.75f );
	}

	return ItemAndGoalWeights( 0, 0 );
}

BotItemsSelector::ItemAndGoalWeights BotItemsSelector::ComputeWeaponWeights( const gsitem_t *item, bool onlyGotGB ) const {
	if( Inventory()[item->tag] ) {
		// TODO: Precache
		const gsitem_t *ammo = GS_FindItemByTag( item->ammo_tag );
		if( Inventory()[ammo->tag] >= ammo->inventory_max ) {
			return ItemAndGoalWeights( 0, 0 );
		}

		float ammoQuantityFactor = 1.0f - Inventory()[ammo->tag] / (float)ammo->inventory_max;
		if( ammoQuantityFactor > 0 ) {
			ammoQuantityFactor = 1.0f / Q_RSqrt( ammoQuantityFactor );
		}

		switch( item->tag ) {
			case WEAP_ELECTROBOLT:
				return ItemAndGoalWeights( ammoQuantityFactor, 0.5f * ammoQuantityFactor );
			case WEAP_LASERGUN:
				return ItemAndGoalWeights( ammoQuantityFactor * 1.1f, 0.6f * ammoQuantityFactor );
			case WEAP_PLASMAGUN:
				return ItemAndGoalWeights( ammoQuantityFactor * 1.1f, 0.6f * ammoQuantityFactor );
			case WEAP_ROCKETLAUNCHER:
				return ItemAndGoalWeights( ammoQuantityFactor, 0.5f * ammoQuantityFactor );
			default:
				return ItemAndGoalWeights( 0.75f * ammoQuantityFactor, 0.75f * ammoQuantityFactor );
		}
	}

	// We may consider plasmagun in a bot's hand as a top tier weapon too
	const int topTierWeapons[4] = { WEAP_ELECTROBOLT, WEAP_LASERGUN, WEAP_ROCKETLAUNCHER, WEAP_PLASMAGUN };

	// TODO: Precompute
	float topTierWeaponGreed = 0.0f;
	for( int i = 0; i < 4; ++i ) {
		if( !Inventory()[topTierWeapons[i]] ) {
			topTierWeaponGreed += 1.0f;
		}
	}

	for( int i = 0; i < 4; ++i ) {
		if( topTierWeapons[i] == item->tag ) {
			float weight = ( onlyGotGB ? 2.0f : 0.9f ) + ( topTierWeaponGreed - 1.0f ) / 3.0f;
			return ItemAndGoalWeights( weight, weight );
		}
	}

	return onlyGotGB ? ItemAndGoalWeights( 1.5f, 2.0f ) : ItemAndGoalWeights( 0.75f, 0.75f );
}

BotItemsSelector::ItemAndGoalWeights BotItemsSelector::ComputeAmmoWeights( const gsitem_t *item ) const {
	if( Inventory()[item->tag] < item->inventory_max ) {
		float quantityFactor = 1.0f - Inventory()[item->tag] / (float)item->inventory_max;
		if( quantityFactor > 0 ) {
			quantityFactor = 1.0f / Q_RSqrt( quantityFactor );
		}

		for( int weapon = WEAP_GUNBLADE; weapon < WEAP_TOTAL; weapon++ ) {
			// TODO: Preache
			const gsitem_t *weaponItem = GS_FindItemByTag( weapon );
			if( weaponItem->ammo_tag == item->tag ) {
				if( Inventory()[weaponItem->tag] ) {
					switch( weaponItem->tag ) {
						case WEAP_ELECTROBOLT:
							return ItemAndGoalWeights( quantityFactor, quantityFactor );
						case WEAP_LASERGUN:
							return ItemAndGoalWeights( quantityFactor * 1.1f, quantityFactor );
						case WEAP_PLASMAGUN:
							return ItemAndGoalWeights( quantityFactor * 1.1f, quantityFactor );
						case WEAP_ROCKETLAUNCHER:
							return ItemAndGoalWeights( quantityFactor, quantityFactor );
						default:
							return ItemAndGoalWeights( 0.5f * quantityFactor, quantityFactor );
					}
				}
				return ItemAndGoalWeights( quantityFactor * 0.33f, quantityFactor * 0.5f );
			}
		}
	}
	return ItemAndGoalWeights( 0.0, 0.0f );
}

BotItemsSelector::ItemAndGoalWeights BotItemsSelector::ComputeHealthWeights( const gsitem_t *item ) const {
	if( item->tag == HEALTH_MEGA || item->tag == HEALTH_ULTRA ) {
		return ItemAndGoalWeights( 2.5f, 1.5f );
	}

	// Always set low goal weight for small health bubbles
	if( item->tag == HEALTH_SMALL ) {
		return ItemAndGoalWeights( 0.2f + 0.3f * ( 1.0f - self->health / (float) self->max_health ), 0.05f );
	}

	float healthFactor = std::max( 0.0f, 1.0f - self->health / (float)self->max_health );
	return ItemAndGoalWeights( healthFactor, healthFactor );
}

BotItemsSelector::ItemAndGoalWeights BotItemsSelector::ComputeArmorWeights( const gsitem_t *item ) const {
	float currArmor = self->r.client->resp.armor;
	switch( item->tag ) {
		case ARMOR_RA:
			return currArmor < 150.0f ? ItemAndGoalWeights( 2.0f, 1.0f ) : ItemAndGoalWeights( 0, 0 );
		case ARMOR_YA:
			return currArmor < 125.0f ? ItemAndGoalWeights( 1.7f, 1.0f ) : ItemAndGoalWeights( 0, 0 );
		case ARMOR_GA:
			return currArmor < 100.0f ? ItemAndGoalWeights( 1.4f, 1.0f ) : ItemAndGoalWeights( 0, 0 );
		case ARMOR_SHARD:
		{
			// Always set low goal weight for shards
			if( currArmor < 25 || currArmor >= 150 ) {
				return ItemAndGoalWeights( 0.4f, 0.10f );
			}
			return ItemAndGoalWeights( 0.25f, 0.05f );
		}
	}
	return ItemAndGoalWeights( 0, 0 );
}

BotItemsSelector::ItemAndGoalWeights BotItemsSelector::ComputePowerupWeights( const gsitem_t *item ) const {
	return ItemAndGoalWeights( 3.5f, 2.00f );
}

class EnemyPathBlockingDetector {
	const edict_t *const self;
	const AiAasWorld *const aasWorld;
	const AiAasRouteCache *const routeCache;

	// The capacity should not exceeded unless in some really bizarre setups.
	// Extra enemies are just not taken into account in these rare cases.
	StaticVector<const Enemy *, 16> potentialBlockers;

	float damageToKillBot;

	int startAreaNums[2];
	int numStartAreas;

	bool IsAPotentialBlocker( const Enemy *enemy, float damageToKillBot, int botBestWeaponTier ) const;

	bool GetInitialRoutingParams( const NavEntity *navEntity, int *travelFlags, int *fromAreaNum ) const;
public:
	EnemyPathBlockingDetector( const edict_t *self_ );

	bool IsPathToNavEntityBlocked( const NavEntity *navEntity ) const;
};

constexpr float MOVE_TIME_WEIGHT = 1.0f;
constexpr float WAIT_TIME_WEIGHT = 3.5f;

struct NavEntityAndWeight {
	const NavEntity *goal;
	float weight;
	inline NavEntityAndWeight( const NavEntity *goal_, float weight_ ) : goal( goal_ ), weight( weight_ ) {}
	// For sorting in descending by weight order operator < is negated
	inline bool operator<( const NavEntityAndWeight &that ) const { return weight > that.weight; }
};

SelectedNavEntity BotItemsSelector::SuggestGoalNavEntity( const SelectedNavEntity &currSelectedNavEntity ) {
	UpdateInternalItemAndGoalWeights();

	StaticVector<NavEntityAndWeight, MAX_NAVENTS> rawWeightCandidates;
	const auto levelTime = level.time;
	auto *navEntitiesRegistry = NavEntitiesRegistry::Instance();
	for( auto it = navEntitiesRegistry->begin(), end = navEntitiesRegistry->end(); it != end; ++it ) {
		const NavEntity *navEnt = *it;
		if( navEnt->IsDisabled() ) {
			continue;
		}

		// We cannot just set a zero internal weight for a temporarily disabled nav entity
		// (it might be overridden by an external weight, and we should not modify external weights
		// as script users expect them remaining the same unless explicitly changed via script API)
		if( disabledForSelectionUntil[navEnt->Id()] >= levelTime ) {
			continue;
		}

		// Since movable goals have been introduced (and clients qualify as movable goals), prevent picking itself as a goal.
		if( navEnt->Id() == ENTNUM( self ) ) {
			continue;
		}

		if( navEnt->Item() && !G_Gametype_CanPickUpItem( navEnt->Item() ) ) {
			continue;
		}

		// Reject an entity quickly if it looks like blocked by an enemy that is close to the entity.
		// Note than passing this test does not guarantee that entire path to the entity is not blocked by enemies.
		if( self->ai->botRef->routeCache->AreaDisabled( navEnt->AasAreaNum() ) ) {
			continue;
		}

		// This is a coarse and cheap test, helps to reject recently picked armors and powerups
		int64_t spawnTime = navEnt->SpawnTime();
		// A feasible spawn time (non-zero) always >= level.time.
		if( !spawnTime || spawnTime - level.time > 15000 ) {
			continue;
		}

		float weight = GetEntityWeight( navEnt->Id() );
		if( weight > 0 ) {
			rawWeightCandidates.push_back( NavEntityAndWeight( navEnt, weight ) );
		}
	}

	// Make sure the candidates list is not empty and thus we can access the best candidate
	if( rawWeightCandidates.empty() ) {
		Debug( "Can't find a feasible long-term goal nav. entity\n" );
		return SelectedNavEntity( nullptr, std::numeric_limits<float>::max(), 0.0f, level.time + 200 );
	}

	// Sort all pre-selected candidates by their raw weights
	std::sort( rawWeightCandidates.begin(), rawWeightCandidates.end() );

	int fromAreaNums[2] = { 0, 0 };
	const auto &entityPhysicsState = self->ai->botRef->EntityPhysicsState();
	const int numFromAreas = entityPhysicsState->PrepareRoutingStartAreas( fromAreaNums );
	const auto *routeCache = self->ai->botRef->routeCache;

	// Pick the best raw weight nav entity.
	// This nav entity is not necessarily the best final nav entity
	// by the final weight that is influenced by routing cost,
	// but the best raw weight means the high importance of it.
	// The picked entity must be reachable from the current location
	auto rawCandidatesIter = rawWeightCandidates.begin();
	const auto rawCandidatesEnd = rawWeightCandidates.end();
	const NavEntity *rawBestNavEnt = ( *rawCandidatesIter ).goal;
	const int rawBestAreaNum = rawBestNavEnt->AasAreaNum();
	unsigned botToBestRawEntMoveDuration = 0;
	for(;; ) {
		botToBestRawEntMoveDuration = 10U * routeCache->PreferredRouteToGoalArea( fromAreaNums, numFromAreas, rawBestAreaNum );
		if( botToBestRawEntMoveDuration ) {
			break;
		}
		++rawCandidatesIter;
		if( rawCandidatesIter == rawCandidatesEnd ) {
			Debug( "Can't find a feasible long-term goal nav. entity\n" );
			return SelectedNavEntity( nullptr, std::numeric_limits<float>::max(), 0.0f, level.time + 200 );
		}
		rawBestNavEnt = ( *rawCandidatesEnd ).goal;
	}

	const EnemyPathBlockingDetector pathBlockingDetector( self );

	// Try checking whether the bot is in some floor cluster to give a greater weight for items in the same cluster
	int currFloorClusterNum = 0;
	const auto *aasFloorClusterNums = AiAasWorld::Instance()->AreaFloorClusterNums();
	if( aasFloorClusterNums[entityPhysicsState->CurrAasAreaNum()] ) {
		currFloorClusterNum = aasFloorClusterNums[entityPhysicsState->CurrAasAreaNum()];
	} else if( aasFloorClusterNums[entityPhysicsState->DroppedToFloorAasAreaNum()] ) {
		currFloorClusterNum = aasFloorClusterNums[entityPhysicsState->DroppedToFloorAasAreaNum()];
	}

	const NavEntity *currGoalNavEntity = currSelectedNavEntity.navEntity;
	float currGoalEntWeight = 0.0f;
	float currGoalEntCost = 0.0f;
	const NavEntity *bestNavEnt = nullptr;
	float bestWeight = 0.000001f;
	float bestNavEntCost = 0.0f;

	const auto startCandidatesIter = rawCandidatesIter;
	// Start from the first (and best) reachable nav entity.
	// (This entity cannot be selected right now as there are additional tests).
	// Test no more than 16 next entities to prevent performance drops.
	for(; rawCandidatesIter - startCandidatesIter < 16 && rawCandidatesIter != rawCandidatesEnd; ++rawCandidatesIter ) {
		const NavEntity *navEnt = ( *rawCandidatesIter ).goal;
		float weight = ( *rawCandidatesIter ).weight;

		const unsigned botToCandidateMoveDuration =
			routeCache->PreferredRouteToGoalArea( fromAreaNums, numFromAreas, navEnt->AasAreaNum() ) * 10U;

		// AAS functions return 0 as a "none" value, 1 as a lowest feasible value
		if( !botToCandidateMoveDuration ) {
			continue;
		}

		if( navEnt->IsDroppedEntity() ) {
			// Do not pick an entity that is likely to dispose before it may be reached
			if( navEnt->Timeout() <= level.time + botToCandidateMoveDuration ) {
				continue;
			}
		}

		int64_t spawnTime = navEnt->SpawnTime();
		// The entity is not spawned and respawn time is unknown
		if( !spawnTime ) {
			continue;
		}

		// Entity origin may be reached at this time
		int64_t reachTime = level.time + botToCandidateMoveDuration;
		unsigned waitDuration = 1;
		if( reachTime < spawnTime ) {
			waitDuration = (unsigned)( spawnTime - reachTime );
		}

		if( waitDuration > navEnt->MaxWaitDuration() ) {
			continue;
		}

		bool isShortRangeReachable = false;
		// If the bot is inside a floor cluster
		if( currFloorClusterNum ) {
			// Increase weight for nav entities in the same floor cluster if the entity is fairly close to it,
			// are spawned, is visible and is reachable by just by walking (to cut off entities behind gaps).
			// Note: do not try making weights depend of velocity/view dir as it is prone to jitter.
			if( currFloorClusterNum == aasFloorClusterNums[navEnt->AasAreaNum()] ) {
				if( IsShortRangeReachable( navEnt, fromAreaNums, numFromAreas ) ) {
					isShortRangeReachable = true;
					weight *= 2.0f;
				}
			}
		}

		// Check the travel time from the nav entity to the best raw weight nav entity
		const unsigned candidateToRawBestEntMoveDuration =
			routeCache->PreferredRouteToGoalArea( navEnt->AasAreaNum(), rawBestNavEnt->AasAreaNum() ) * 10U;

		// If the best raw weight nav entity is not reachable from the entity
		if( !candidateToRawBestEntMoveDuration ) {
			continue;
		}

		// Take into account not only travel time to candidate, but also travel time from candidate to the raw best nav ent.
		// If moving to the current candidate leads to increased travel time to the best raw nav entity.
		// compared to the travel time to it from the current origin, consider it as a penalty.
		// No penalty is applied for nav entities that are closer to the best raw nav entity than the bot.
		// This bots are forced to advance to "best" map regions to complete objectives (capture flags, etc)
		// while still grabbing stuff that is short-range reachable.
		unsigned moveDurationPenalty = 0;
		// Don't apply penalty for short-range reachable nav entities
		if( !isShortRangeReachable ) {
			if( candidateToRawBestEntMoveDuration > botToBestRawEntMoveDuration ) {
				moveDurationPenalty = candidateToRawBestEntMoveDuration - botToBestRawEntMoveDuration;
			}
		}

		const float moveCost = MOVE_TIME_WEIGHT * ( botToCandidateMoveDuration + moveDurationPenalty );
		const float cost = moveCost + WAIT_TIME_WEIGHT * waitDuration;

		weight = ( 1000 * weight ) / ( 0.001f + cost * navEnt->CostInfluence() );

		// Store current weight of the current goal entity
		if( currGoalNavEntity == navEnt ) {
			currGoalEntWeight = weight;
			// Waiting time is handled by the planner for wait actions separately.
			currGoalEntCost = moveCost;
		}

		if( weight > bestWeight ) {
			bestNavEnt = navEnt;
			bestWeight = weight;
			// Waiting time is handled by the planner for wait actions separately.
			bestNavEntCost = moveCost;
		}
	}

	if( !bestNavEnt ) {
		Debug( "Can't find a feasible long-term goal nav. entity\n" );
		return SelectedNavEntity( nullptr, std::numeric_limits<float>::max(), 0.0f, level.time + 200 );
	}

	// If it is time to pick a new goal (not just re-evaluate current one), do not be too sticky to the current goal
	const float currToBestWeightThreshold = currGoalNavEntity != nullptr ? 0.6f : 0.8f;

	if( currGoalNavEntity && currGoalNavEntity == bestNavEnt ) {
		constexpr const char *format = "current goal entity %s is kept as still having best weight %.3f\n";
		Debug( format, currGoalNavEntity->Name(), bestWeight );
		return SelectedNavEntity( bestNavEnt, bestNavEntCost, GetGoalWeight( bestNavEnt->Id() ), level.time + 4000 );
	} else if( currGoalEntWeight > 0 && currGoalEntWeight / bestWeight > currToBestWeightThreshold ) {
		constexpr const char *format =
			"current goal entity %s is kept as having weight %.3f good enough to not consider picking another one\n";
		// If currGoalEntWeight > 0, currLongTermGoalEnt is guaranteed to be non-null
		Debug( format, currGoalNavEntity->Name(), currGoalEntWeight );
		return SelectedNavEntity( currGoalNavEntity, currGoalEntCost, GetGoalWeight( bestNavEnt->Id() ), level.time + 2500 );
	} else {
		if( currGoalNavEntity ) {
			const char *format = "suggested %s weighted %.3f as a long-term goal instead of %s weighted now as %.3f\n";
			Debug( format, bestNavEnt->Name(), bestWeight, currGoalNavEntity->Name(), currGoalEntWeight );
		} else {
			Debug( "suggested %s weighted %.3f as a new long-term goal\n", bestNavEnt->Name(), bestWeight );
		}
		return SelectedNavEntity( bestNavEnt, bestNavEntCost, GetGoalWeight( bestNavEnt->Id() ), level.time + 2500 );
	}
}

bool BotItemsSelector::IsShortRangeReachable( const NavEntity *navEnt, const int *fromAreaNums, int numFromAreas ) const {
	if( navEnt->Origin().SquareDistanceTo( self->s.origin ) > 256.0f * 256.0f ) {
		return false;
	}

	if( !navEnt->IsSpawnedAtm() ) {
		return false;
	}

	const auto *ent = game.edicts + navEnt->Id();
	if( !EntitiesPvsCache::Instance()->AreInPvs( self, ent ) ) {
		return false;
	}

	Vec3 viewOrigin( self->s.origin );
	viewOrigin.Z() += self->viewheight;
	trace_t trace;

	SolidWorldTrace( &trace, viewOrigin.Data(), ent->s.origin );
	if( trace.fraction != 1.0f ) {
		return false;
	}

	const int travelFlags = TFL_WALK | TFL_AIR;
	const auto *routeCache = self->ai->botRef->routeCache;
	for( int i = 0; i < numFromAreas; ++i ) {
		if( routeCache->TravelTimeToGoalArea( fromAreaNums[i], navEnt->AasAreaNum(), travelFlags ) ) {
			return true;
		}
	}

	return false;
}

EnemyPathBlockingDetector::EnemyPathBlockingDetector( const edict_t *self_ )
	: self( self_ ), aasWorld( AiAasWorld::Instance() ), routeCache( self->ai->botRef->RouteCache() ) {
	numStartAreas = self_->ai->botRef->EntityPhysicsState()->PrepareRoutingStartAreas( startAreaNums );

	if( self_->ai->botRef->ShouldRushHeadless() ) {
		return;
	}

	damageToKillBot = DamageToKill( self, g_armor_protection->value, g_armor_degradation->value );
	if( HasShell( self ) ) {
		damageToKillBot *= 4.0f;
	}

	// We modify "damage to kill" in order to take quad bearing into account
	if( HasQuad( self ) && damageToKillBot > 50.0f ) {
		damageToKillBot *= 2.0f;
	}

	const int botBestWeaponTier = FindBestWeaponTier( self_->r.client );

	const Enemy *enemy = self->ai->botRef->TrackedEnemiesHead();
	for(; enemy; enemy = enemy->NextInTrackedList() ) {
		if( !IsAPotentialBlocker( enemy, damageToKillBot, botBestWeaponTier ) ) {
			continue;
		}
		potentialBlockers.push_back( enemy );
		if( potentialBlockers.size() == potentialBlockers.capacity() ) {
			break;
		}
	}
}

bool EnemyPathBlockingDetector::IsAPotentialBlocker( const Enemy *enemy,
													 float damageToKillBot,
													 int botBestWeaponTier ) const {
	if( !enemy->IsValid() ) {
		return false;
	}

	int enemyWeaponTier;
	if( const auto *client = enemy->ent->r.client ) {
		enemyWeaponTier = FindBestWeaponTier( client );
		if( enemyWeaponTier < 1 && !HasPowerups( enemy->ent ) ) {
			return false;
		}
	} else {
		// Try guessing...
		enemyWeaponTier = (int)( 1.0f + BoundedFraction( enemy->ent->aiIntrinsicEnemyWeight, 3.0f ) );
	}

	float damageToKillEnemy = DamageToKill( enemy->ent, g_armor_protection->value, g_armor_degradation->value );

	if( HasShell( enemy->ent ) ) {
		damageToKillEnemy *= 4.0f;
	}

	// We modify "damage to kill" in order to take quad bearing into account
	if( HasQuad( enemy->ent ) && damageToKillEnemy > 50 ) {
		damageToKillEnemy *= 2.0f;
	}

	const float offensiveness = self->ai->botRef->GetEffectiveOffensiveness();

	if( damageToKillBot < 50 && damageToKillEnemy < 50 ) {
		// Just check weapons. Note: GB has 0 tier, GL has 1 tier, the rest of weapons have a greater tier
		return ( std::min( 1, enemyWeaponTier ) / (float)std::min( 1, botBestWeaponTier ) ) > 0.7f + 0.8f * offensiveness;
	}

	const auto &selectedEnemies = self->ai->botRef->GetSelectedEnemies();
	// Don't block if is in squad, except they have a quad runner
	if( self->ai->botRef->IsInSquad() ) {
		if( !( selectedEnemies.AreValid() && selectedEnemies.HaveQuad() ) ) {
			return false;
		}
	}

	float ratioThreshold = 1.25f;
	if( selectedEnemies.AreValid() ) {
		// If the bot is outnumbered
		if( selectedEnemies.AreThreatening() && selectedEnemies.Contain( enemy ) ) {
			ratioThreshold *= 1.25f;
		}
	}

	if( selectedEnemies.AreValid() && selectedEnemies.AreThreatening() && selectedEnemies.Contain( enemy ) ) {
		if( selectedEnemies.end() - selectedEnemies.begin() > 1 ) {
			ratioThreshold *= 1.25f;
		}
	}

	ratioThreshold -= ( botBestWeaponTier - enemyWeaponTier ) * 0.25f;
	if( damageToKillEnemy / damageToKillBot < ratioThreshold ) {
		return false;
	}

	return damageToKillEnemy / damageToKillBot > 1.0f + 2.0f * ( offensiveness * offensiveness );
}

// Discovers what params were used for actual route building
// by doing a search again for all possible params
// This is not as inefficient as it sounds as the route cache
// nowadays guarantees O(1) and really fast retrieval for recent results.
bool EnemyPathBlockingDetector::GetInitialRoutingParams( const NavEntity *navEntity,
														 int *travelFlags,
														 int *fromAreaNum ) const {
	const int goalAreaNum = navEntity->AasAreaNum();
	// Should be identical to what is used for goal estimation
	int chosenTravelTime = routeCache->PreferredRouteToGoalArea( startAreaNums, numStartAreas, goalAreaNum );
	for( int i = 0; i < numStartAreas; ++i ) {
		for( int flags : self->ai->botRef->TravelFlags() ) {
			if( chosenTravelTime == routeCache->TravelTimeToGoalArea( startAreaNums[i], goalAreaNum, flags ) ) {
				*travelFlags = flags;
				*fromAreaNum = startAreaNums[i];
				return true;
			}
		}
	}

	return false;
}

bool EnemyPathBlockingDetector::IsPathToNavEntityBlocked( const NavEntity *navEntity ) const {
	if( self->ai->botRef->ShouldRushHeadless() ) {
		return false;
	}

	int travelFlags, currAreaNum;
	if( !GetInitialRoutingParams( navEntity, &travelFlags, &currAreaNum ) ) {
		return true;
	}

	const auto *aasAreas = aasWorld->Areas();
	const auto *aasReach = aasWorld->Reachabilities();
	const int goalAreaNum = navEntity->AasAreaNum();

	int numReachHops = 0;
	int reachNum = 0;
	while( currAreaNum != goalAreaNum ) {
		// Don't check for blocking areas that are fairly close to the bot,
		// so the bot does not get blocked in his current position
		if( DistanceSquared( aasAreas[currAreaNum].center, self->s.origin ) > 192 * 192 ) {
			for( const Enemy *enemy: potentialBlockers ) {
				if( enemy->MightBlockArea( damageToKillBot, currAreaNum, reachNum, aasWorld ) ) {
					return true;
				}
			}
		}

		numReachHops++;
		// Stop at some point
		if( numReachHops == 48 ) {
			break;
		}

		reachNum = routeCache->ReachabilityToGoalArea( currAreaNum, goalAreaNum, travelFlags );
		// Break at unreachable, consider the path valid
		if( !reachNum ) {
			break;
		}
		currAreaNum = aasReach[reachNum].areanum;
	}

	return false;
}
