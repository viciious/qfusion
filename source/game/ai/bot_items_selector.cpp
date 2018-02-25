#include "bot_items_selector.h"
#include "bot.h"

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
		unsigned spawnTime = navEnt->SpawnTime();
		// A feasible spawn time (non-zero) always >= level.time.
		if( !spawnTime || level.time - spawnTime > 15000 ) {
			continue;
		}

		float weight = GetEntityWeight( navEnt->Id() );
		if( weight > 0 ) {
			rawWeightCandidates.push_back( NavEntityAndWeight( navEnt, weight ) );
		}
	}

	// Sort all pre-selected candidates by their raw weights
	std::sort( rawWeightCandidates.begin(), rawWeightCandidates.end() );

	// Try checking whether the bot is in some floor cluster to give a greater weight for items in the same cluster
	int currFloorClusterNum = 0;
	const auto &entityPhysicsState = self->ai->botRef->EntityPhysicsState();
	const auto *aasFloorClusterNums = AiAasWorld::Instance()->AreaFloorClusterNums();
	if( aasFloorClusterNums[entityPhysicsState->CurrAasAreaNum()] ) {
		currFloorClusterNum = aasFloorClusterNums[entityPhysicsState->CurrAasAreaNum()];
	} else if( aasFloorClusterNums[entityPhysicsState->DroppedToFloorAasAreaNum()] ) {
		currFloorClusterNum = aasFloorClusterNums[entityPhysicsState->DroppedToFloorAasAreaNum()];
	}

	int fromAreaNums[2] = { 0, 0 };
	const int numFromAreas = entityPhysicsState->PrepareRoutingStartAreas( fromAreaNums );
	const auto *routeCache = self->ai->botRef->routeCache;

	const NavEntity *currGoalNavEntity = currSelectedNavEntity.navEntity;
	float currGoalEntWeight = 0.0f;
	float currGoalEntCost = 0.0f;
	const NavEntity *bestNavEnt = nullptr;
	float bestWeight = 0.000001f;
	float bestNavEntCost = 0.0f;
	// Test not more than 16 best pre-selected by raw weight candidates.
	// (We try to avoid too many expensive FindTravelTimeToGoalArea() calls,
	// thats why we start from the best item to avoid wasting these calls for low-priority items)
	for( unsigned i = 0, end = std::min( rawWeightCandidates.size(), 16U ); i < end; ++i ) {
		const NavEntity *navEnt = rawWeightCandidates[i].goal;
		float weight = rawWeightCandidates[i].weight;

		unsigned moveDuration = 1;
		unsigned waitDuration = 1;

		if( self->ai->botRef->CurrAreaNum() != navEnt->AasAreaNum() ) {
			// This call returns an AAS travel time (and optionally a next reachability via out parameter)
			moveDuration = routeCache->PreferredRouteToGoalArea( fromAreaNums, numFromAreas, navEnt->AasAreaNum() ) * 10U;
			// AAS functions return 0 as a "none" value, 1 as a lowest feasible value
			if( !moveDuration ) {
				continue;
			}

			if( navEnt->IsDroppedEntity() ) {
				// Do not pick an entity that is likely to dispose before it may be reached
				if( navEnt->Timeout() <= level.time + moveDuration ) {
					continue;
				}
			}
		}

		unsigned spawnTime = navEnt->SpawnTime();
		// The entity is not spawned and respawn time is unknown
		if( !spawnTime ) {
			continue;
		}

		// Entity origin may be reached at this time
		unsigned reachTime = level.time + moveDuration;
		if( reachTime < spawnTime ) {
			waitDuration = spawnTime - reachTime;
		}

		if( waitDuration > navEnt->MaxWaitDuration() ) {
			continue;
		}

		float moveCost = MOVE_TIME_WEIGHT * moveDuration * navEnt->CostInfluence();
		float cost = 0.0001f + moveCost + WAIT_TIME_WEIGHT * waitDuration * navEnt->CostInfluence();

		weight = ( 1000 * weight ) / cost;

		// If the bot is inside a floor cluster
		if( currFloorClusterNum ) {
			// Greatly increase weight for items in the same floor cluster
			if( currFloorClusterNum == aasFloorClusterNums[navEnt->AasAreaNum()] ) {
				weight *= 4.0f;
			}
		}

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
