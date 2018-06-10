#include "WeaponJumpActions.h"
#include "BestJumpableSpotDetector.h"
#include "MovementLocal.h"
#include "../ai_manager.h"

class WeaponJumpableSpotDetector: public BestJumpableSpotDetector {
public:
	SpotAndScore *spots;
	// Addressed by spot tags
	const float *zOffsets;
	int numSpots;
	float push;
	vec3_t origin;

	void GetCandidateSpots( SpotAndScore **begin, SpotAndScore **end ) override;
	void GetVelocityForJumpingToSpot( vec3_t velocity, const SpotAndScore *spot ) override;
};

class WeaponJumpWeaponsTester {
	WeaponJumpableSpotDetector detector;

	typedef WeaponJumpableSpotDetector::SpotAndScore SpotAndScore;
	StaticVector<SpotAndScore, 64> spots;

	float zOffsets[64];

	const int *suggestedWeapons;
	int numWeapons;

	const int *areaNums;
	const int *travelTimes;
	const vec3_t *targets;
	int numAreas;

	void SetupForWeapon( int weaponNum );
public:
	void SetWeapons( const int *suggestedWeapons_, int numWeapons_ ) {
		this->suggestedWeapons = suggestedWeapons_;
		this->numWeapons = numWeapons_;
	}

	void SetSpotData( const int *areaNums_, const int *travelTimes_, const vec3_t *targets_, int numAreas_ ) {
		this->areaNums = areaNums_;
		this->travelTimes = travelTimes_;
		this->targets = targets_;
		this->numAreas = numAreas_;
	}

	bool Exec( MovementPredictionContext *context, ScheduleWeaponJumpAction *action );
};

static WeaponJumpWeaponsTester weaponJumpWeaponsTester;

static void PrepareAnglesAndWeapon( Context *context ) {
	const auto &weaponJumpState = context->movementState->weaponJumpMovementState;
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	Vec3 lookDir( weaponJumpState.FireTarget() );
	lookDir -= entityPhysicsState.Origin();
	lookDir.Z() = 0;
	float squareLen = lookDir.SquaredLength();
	if( squareLen > 1 ) {
		lookDir *= -1.0f / squareLen;
		// It will be normalized again by BotInput code
		lookDir.Z() = -1.0f;
	} else {
		// A rare but possible case
		lookDir.Set( 0, 0, -1 );
	}

	context->record->botInput.SetIntendedLookDir( lookDir );
	context->record->botInput.SetTurnSpeedMultiplier( 15.0f );
	context->record->botInput.isUcmdSet = true;
	context->record->pendingWeapon = weaponJumpState.weapon;
}

uint32_t ScheduleWeaponJumpAction::areasMask[( 1 << 16 ) / 8];
int ScheduleWeaponJumpAction::dummyTravelTimes[ScheduleWeaponJumpAction::MAX_AREAS];

void ScheduleWeaponJumpAction::PlanPredictionStep( Context *context ) {
	if( !GenericCheckIsActionEnabled( context, &DefaultWalkAction() ) ) {
		return;
	}

	Assert( context->topOfStackIndex == 0, "This action can be applied only for the current state" );

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	// Isn't this very restrictive?
	if( !entityPhysicsState.GroundEntity() ) {
		Debug( "The bot is not on ground\n" );
		this->DisableWithAlternative( context, &DefaultWalkAction() );
		return;
	}

	if( entityPhysicsState.Speed2D() > 2 * context->GetDashSpeed() ) {
		Debug( "Weapon-jumps from ground while having a significant speed are not allowed\n" );
		this->DisableWithAlternative( context, &DefaultWalkAction() );
		return;
	}

	// Disallow rocketjumps for easy bots and also prevent holding cpu quota all the time
	if( bot->Skill() <= 0.33f ) {
		Debug( "The action has been rejected by a bot skill test\n" );
		this->DisableWithAlternative( context, &DefaultWalkAction() );
		return;
	}

	if( context->IsInNavTargetArea() ) {
		Debug( "The bot is already in the target area\n" );
		this->DisableWithAlternative( context, &DefaultWalkAction() );
		return;
	}

	if( !module->weaponJumpAttemptsRateLimiter.TryAcquire() ) {
		Debug( "A weapon jumping attempt is disallowed by the rate limiter\n" );
		this->DisableWithAlternative( context, &DefaultWalkAction() );
		return;
	}

	// Avoid jumping to target if the bot is in the same floor cluster and is fairly close.
	// This test rejects a case when there is a gap that requires walking around, but is rather good for all other cases.
	if( const int groundedAreaNum = context->CurrGroundedAasAreaNum() ) {
		const auto *aasWorld = AiAasWorld::Instance();
		if( int currClusterNum = aasWorld->FloorClusterNum( groundedAreaNum ) ) {
			if( int targetClusterNum = aasWorld->FloorClusterNum( context->NavTargetAasAreaNum() ) ) {
				if( targetClusterNum == currClusterNum ) {
					if( context->NavTargetOrigin().SquareDistanceTo( entityPhysicsState.Origin() ) < SQUARE( 144 ) ) {
						Debug( "The bot is in the target floor cluster and is fairly close to target\n" );
						this->DisableWithAlternative( context, &DefaultWalkAction() );
						return;
					}
				}
			}
		}
	}

	const bool worthWeaponJumping = bot->NavTargetWorthWeaponJumping();
	const bool worthRushing = bot->NavTargetWorthRushing();

	int suitableWeapons[WEAP_TOTAL];
	int numSuitableWeapons = bot->GetWeaponsForWeaponJumping( suitableWeapons );
	if( !numSuitableWeapons ) {
		Debug( "There is no suitable weapon-jump weapons for the current bot state\n" );
		this->DisableWithAlternative( context, &DefaultWalkAction() );
		return;
	}

	if( !( context->CurrGroundedAasAreaNum() && context->NavTargetAasAreaNum() ) ) {
		this->DisableWithAlternative( context, &DefaultWalkAction() );
		return;
	}

	// Do it after all other tests have been passed and only routing/trajectory prediction tests are left.
	// Otherwise the bot would often lock the quotum fruitlessly.
	if( !AiManager::Instance()->TryGetExpensiveComputationQuota( bot ) ) {
		Debug( "Cannot acquire an expensive computations CPU quota\n" );
		this->DisableWithAlternative( context, &DefaultWalkAction() );
		return;
	}

	ClearAreasMask();
	PrecacheBotLeafs( context );

	if( worthWeaponJumping && TryJumpDirectlyToTarget( context, suitableWeapons, numSuitableWeapons ) ) {
		context->isCompleted = true;
		return;
	}

	if( worthRushing && TryShortcutReachChain( context, suitableWeapons, numSuitableWeapons ) ) {
		context->isCompleted = true;
		return;
	}

	Debug( "No method/target was suitable for weapon-jumping, disabling the action for further planning\n" );
	this->DisableWithAlternative( context, &DefaultWalkAction() );
}

inline void ScheduleWeaponJumpAction::ClearAreasMask() {
	memset( areasMask, 0, sizeof( areasMask ) );
}

inline bool ScheduleWeaponJumpAction::TryMarkAreaInMask( int areaNum ) {
	int wordNum = areaNum / 32;
	uint32_t bitMask = 1u << ( areaNum % 32 );
	// True if was not marked
	bool result = ( areasMask[wordNum] & bitMask ) == 0;
	areasMask[wordNum] |= bitMask;
	return result;
}

inline void ScheduleWeaponJumpAction::PrecacheBotLeafs( Context *context ) {
	const float *botOrigin = context->movementState->entityPhysicsState.Origin();
	Vec3 mins( playerbox_stand_mins );
	Vec3 maxs( playerbox_stand_maxs );
	mins += botOrigin;
	maxs += botOrigin;
	int topNode;
	numBotLeafs = trap_CM_BoxLeafnums( mins.Data(), maxs.Data(), botLeafNums, sizeof( botLeafNums ), &topNode );
}

inline const int *ScheduleWeaponJumpAction::GetTravelTimesForReachChainShortcut() {
	if( !dummyTravelTimes[0] ) {
		for( int i = 0; i < MAX_AREAS; ++i ) {
			// Make sure every travel time is a feasible AAS time (>0)
			dummyTravelTimes[i] = i + 1;
		}
	}

	return dummyTravelTimes;
}

int ScheduleWeaponJumpAction::GetCandidatesForReachChainShortcut( Context *context, int *areaNums ) {
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasReach = aasWorld->Reachabilities();
	const auto *aasAreas = aasWorld->Areas();
	const auto *routeCache = bot->RouteCache();
	const float *botOrigin = context->movementState->entityPhysicsState.Origin();
	const int targetAreaNum = context->NavTargetAasAreaNum();
	int currAreaNum = context->CurrGroundedAasAreaNum();

	float squareDistanceThreshold = SQUARE( 256 + 64 );

	int numAreas = 0;
	int numHops = 0;
	int reachNum;
	while( routeCache->PreferredRouteToGoalArea( currAreaNum, targetAreaNum, &reachNum ) ) {
		if( !reachNum ) {
			break;
		}
		// Prevent looping that still occurs
		if( ( numHops++ ) >= MAX_AREAS ) {
			break;
		}

		const auto &nextReach = aasReach[reachNum];
		const auto travelType = nextReach.traveltype & TRAVELTYPE_MASK;
		if( travelType != TRAVEL_WALK && travelType != TRAVEL_WALKOFFLEDGE ) {
			if( !( travelType == TRAVEL_JUMP && DistanceSquared( nextReach.start, nextReach.end ) < SQUARE( 64 ) ) ) {
				squareDistanceThreshold = SQUARE( 96 );
			}
		}

		int nextAreaNum = nextReach.areanum;
		currAreaNum = nextAreaNum;
		// If an area has been already marked (by the "jump to target" call)
		if( !TryMarkAreaInMask( nextAreaNum ) ) {
			continue;
		}

		if( DistanceSquared( aasAreas[nextAreaNum].center, botOrigin ) < squareDistanceThreshold ) {
			continue;
		}

		areaNums[numAreas++] = nextAreaNum;
		if( numAreas >= MAX_AREAS ) {
			break;
		}
	}

	return numAreas;
}

void ScheduleWeaponJumpAction::PrepareJumpTargets( Context *context, const int *areaNums, vec3_t *targets, int numAreas ) {
	const auto *aasAreas = AiAasWorld::Instance()->Areas();
	// Aim at the height of a player (jump targets will be lower)
	const float offset = -playerbox_stand_mins[2] + playerbox_stand_maxs[2];
	for( int i = 0; i < numAreas; ++i ) {
		const auto &area = aasAreas[areaNums[i]];
		targets[i][0] = area.center[0];
		targets[i][1] = area.center[1];
		targets[i][2] = area.mins[2] + offset;
	}
}

bool ScheduleWeaponJumpAction::TryJumpDirectlyToTarget( Context *context, const int *suitableWeapons, int numWeapons ) {
	int areaNums[MAX_AREAS];
	int travelTimes[MAX_AREAS];
	vec3_t jumpTargets[MAX_AREAS];

	const int numRawCandidateAreas = GetCandidatesForJumpingToTarget( context, areaNums );
	const int numFilteredRawAreas = FilterRawCandidateAreas( context, areaNums, numRawCandidateAreas );
	int numPassedReachTestAreas = ReachTestNearbyTargetAreas( context, areaNums, travelTimes, numFilteredRawAreas );
	PrepareJumpTargets( context, areaNums, jumpTargets, numPassedReachTestAreas );

	::weaponJumpWeaponsTester.SetSpotData( areaNums, travelTimes, jumpTargets, numPassedReachTestAreas );
	::weaponJumpWeaponsTester.SetWeapons( suitableWeapons, numWeapons );
	return ::weaponJumpWeaponsTester.Exec( context, this );
}

int ScheduleWeaponJumpAction::GetCandidatesForJumpingToTarget( Context *context, int *areaNums ) {
	const auto *aasWorld = AiAasWorld::Instance();
	const Vec3 navTargetOrigin( context->NavTargetOrigin() );

	if( int floorClusterNum = aasWorld->FloorClusterNum( context->NavTargetAasAreaNum() ) ) {
		const auto *clusterAreaNums = aasWorld->FloorClusterData( floorClusterNum ) + 1;
		if( clusterAreaNums[-1] <= MAX_AREAS ) {
			// We have saved lots of cycles on avoiding BBoxAreas() call anyway.
			static_assert( sizeof( int ) > sizeof( *clusterAreaNums ), "Avoid copying/conversions in this case" );
			for( int i = 0; i < clusterAreaNums[-1]; ++i ) {
				areaNums[i] = clusterAreaNums[i];
			}
			return clusterAreaNums[-1];
		}

		// Having much more areas in cluster than MAX_AREAS is not rare.
		// Make sure we won't miss closest to target areas.

		StaticVector<AreaAndScore, MAX_AREAS> heap;
		float farthestPresentDistance = 0.0f;

		const auto *aasAreas = aasWorld->Areas();
		for( int i = 0; i < clusterAreaNums[-1]; ++i ) {
			const int areaNum = clusterAreaNums[i];
			const float squareDistance = navTargetOrigin.SquareDistanceTo( aasAreas[areaNum].center );
			if( heap.size() == heap.capacity() ) {
				if( squareDistance >= farthestPresentDistance ) {
					continue;
				}
				// Evict a worst area
				std::pop_heap( heap.begin(), heap.end() );
				heap.pop_back();
			} else {
				farthestPresentDistance = std::max( squareDistance, farthestPresentDistance );
			}
			// Note: AreaAndScore::operator< puts best by score areas first,
			// so far areas get a huge negative score and are evicted first in the max-heap.
			heap.emplace_back( AreaAndScore( areaNum, -squareDistance ) );
			std::push_heap( heap.begin(), heap.end() );
		}

		for( unsigned i = 0; i < heap.size(); ++i ) {
			areaNums[i] = heap[i].areaNum;
		}

		return (int)heap.size();
	}

	Vec3 mins( -384, -384, -32 );
	Vec3 maxs( +384, +384, +32 );
	mins += context->NavTargetOrigin();
	maxs += context->NavTargetOrigin();
	return aasWorld->BBoxAreas( mins, maxs, areaNums, 64 );
}

inline bool ScheduleWeaponJumpAction::IsAreaInPvs( const int *areaLeafsList ) const {
	for( int i = 0; i < areaLeafsList[-1]; ++i ) {
		for( int j = 0; j < numBotLeafs; ++j ) {
			if( trap_CM_LeafsInPVS( areaLeafsList[i], botLeafNums[j] ) ) {
				return true;
			}
		}
	}
	return false;
}

int ScheduleWeaponJumpAction::FilterRawCandidateAreas( Context *context, int *areaNums, int numRawAreas ) {
	int *const filteredAreas = areaNums;
	int numFilteredAreas = 0;

	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasAreaSettings = aasWorld->AreaSettings();

	const float *botOrigin = context->movementState->entityPhysicsState.Origin();

	for( int i = 0; i < numRawAreas; ++i ) {
		const int areaNum = areaNums[i];
		const auto &settings = aasAreaSettings[areaNum];
		if( settings.contents & ( AREACONTENTS_LAVA | AREACONTENTS_SLIME | AREACONTENTS_WATER | AREACONTENTS_DONOTENTER ) ) {
			continue;
		}
		if( settings.areaflags & AREA_JUNK ) {
			continue;
		}
		const auto &area = aasAreas[areaNum];
		// There will be further distance tests, but this coarse and broad one helps to save expensive routing calls
		const float squareDistance = DistanceSquared( botOrigin, area.center );
		// Note: the lower limit might seem insufficient but sometimes short vertical weapon-jumps are needed
		if( squareDistance < SQUARE( 40 ) || squareDistance > SQUARE( 1024 + 512 ) ) {
			continue;
		}
		// Since we precompute leaf nums for every area, this PVS test is very cheap
		if( !IsAreaInPvs( aasWorld->AreaMapLeafsList( areaNum ) ) ) {
			continue;
		}
		if( int stairsClusterNum = aasWorld->StairsClusterNum( areaNum ) ) {
			const auto *stairsClusterAreaNums = aasWorld->StairsClusterData( stairsClusterNum ) + 1;
			// If the area is not a boundary (uppper/lower) cluster area, skip it. Do not jump to stairs areas.
			if( areaNum != stairsClusterAreaNums[0] && areaNum != stairsClusterAreaNums[stairsClusterAreaNums[-1]] ) {
				continue;
			}
		}

		filteredAreas[numFilteredAreas++] = areaNum;
	}

	return numFilteredAreas;
}

int ScheduleWeaponJumpAction::ReachTestNearbyTargetAreas( Context *context, int *areaNums, int *travelTimes, int numAreas ) {
	int *const passedTestAreas = areaNums;
	int numPassedTestAreas = 0;

	int botAreaNums[2];
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const int numBotAreas = entityPhysicsState.PrepareRoutingStartAreas( botAreaNums );
	const auto *routeCache = bot->RouteCache();
	const int targetAreaNum = context->NavTargetAasAreaNum();
	const int currTravelTimeToTarget = context->TravelTimeToNavTarget();
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasAreas = aasWorld->Areas();

	int reachNum;
	int travelTimeFromAreaToTarget;
	// Travel times from the bot to a spot and back
	int directTravelTime, reverseTravelTime;
	float deltaHeight;
	vec3_t areaPoint;

	trace_t trace;
	for( int i = 0; i < numAreas; ++i ) {
		const int testedAreaNum = areaNums[i];
		// Always add the target area
		if( testedAreaNum == targetAreaNum ) {
			travelTimeFromAreaToTarget = 1;
			goto testPassed;
		}

		travelTimeFromAreaToTarget = routeCache->PreferredRouteToGoalArea( testedAreaNum, targetAreaNum );
		if( !travelTimeFromAreaToTarget || travelTimeFromAreaToTarget >= currTravelTimeToTarget ) {
			continue;
		}

		deltaHeight = aasAreas[testedAreaNum].mins[2] + 16.0f - entityPhysicsState.Origin()[2];
		if( deltaHeight > 0 ) {
			float squareDistance = Distance2DSquared( aasAreas[testedAreaNum].center, entityPhysicsState.Origin() );
			if( squareDistance / ( deltaHeight * deltaHeight ) < SQUARE( 3.0f / 2.0f ) ) {
				goto testPassed;
			}
		}

		directTravelTime = routeCache->PreferredRouteToGoalArea( botAreaNums, numBotAreas, targetAreaNum, &reachNum );
		// If the area is not reachable from the bot origin, add it without other tests (it seems to be a royal shortcut!)
		if( !directTravelTime ) {
			goto testPassed;
		}

		// We do not check a travel time from bot to area due to
		// a triangle inequality of partial travel times and the final travel time

		VectorCopy( aasAreas[testedAreaNum].center, areaPoint );
		areaPoint[2] = aasAreas[testedAreaNum].mins[2] + 40;
		SolidWorldTrace( &trace, aasAreas[testedAreaNum].center, entityPhysicsState.Origin() );
		if( trace.fraction != 1.0f ) {
			if( deltaHeight < 0 ) {
				continue;
			}
			goto testPassed;
		}

		// The most expensive part, left the last (this route is unlikely to be cached).
		// Check the travel time back from spot to the bot origin.
		reverseTravelTime = TravelTimeWalkingOrFallingShort( routeCache, testedAreaNum, botAreaNums[numBotAreas] );
		// Its very likely there is a falling involve
		if( !reverseTravelTime ) {
			goto testPassed;
		}

		// Make sure that direct travel time is much greater that the reverse time
		if( directTravelTime - reverseTravelTime < 200 ) {
			continue;
		}
testPassed:;
		passedTestAreas[numPassedTestAreas] = testedAreaNum;
		travelTimes[numPassedTestAreas] = travelTimeFromAreaToTarget;
		++numPassedTestAreas;
	}

	return numPassedTestAreas;
}

bool ScheduleWeaponJumpAction::TryShortcutReachChain( Context *context, const int *suitableWeapons, int numWeapons ) {
	int areaNums[MAX_AREAS];
	vec3_t jumpTargets[MAX_AREAS];

	int numRawAreas = GetCandidatesForReachChainShortcut( context, areaNums );
	int numFilteredAreas = FilterRawCandidateAreas( context, areaNums, numRawAreas );
	PrepareJumpTargets( context, areaNums, jumpTargets, numFilteredAreas );

	::weaponJumpWeaponsTester.SetWeapons( suitableWeapons, numWeapons );
	::weaponJumpWeaponsTester.SetSpotData( areaNums, GetTravelTimesForReachChainShortcut(), jumpTargets, numFilteredAreas );
	return ::weaponJumpWeaponsTester.Exec( context, this );
}

void TryTriggerWeaponJumpAction::PlanPredictionStep( Context *context ) {
	if( !GenericCheckIsActionEnabled( context, &DefaultWalkAction() ) ) {
		return;
	}

	Assert( context->topOfStackIndex == 0, "This action can be applied only for the current state" );

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	auto *weaponJumpState = &context->movementState->weaponJumpMovementState;

	PrepareAnglesAndWeapon( context );

	if( weaponJumpState->OriginAtStart().SquareDistanceTo( entityPhysicsState.Origin() ) > SQUARE( 8 ) ) {
		Debug( "The bot origin has been changed. Deactivating the weapon jump state (should be replanned next frame)." );
		weaponJumpState->Deactivate();
		// Keep the applied input, its very likely that the action will be activated again next frame.
		context->isCompleted = true;
		return;
	}

	if( entityPhysicsState.GroundEntity() ) {
		if( entityPhysicsState.ForwardDir().Z() < -0.8f ) {
			if( context->oldPlayerState->stats[STAT_WEAPON] == weaponJumpState->weapon ) {
				context->record->botInput.SetAttackButton( true );
				context->record->botInput.SetUpMovement( 1 );
				weaponJumpState->hasTriggeredWeaponJump = true;
			}
		}
	}

	context->isCompleted = true;
}

void CorrectWeaponJumpAction::PlanPredictionStep( Context *context ) {
	if( !GenericCheckIsActionEnabled( context, &DefaultWalkAction() ) ) {
		return;
	}

	Assert( context->topOfStackIndex == 0, "This action can be applied only for the current state" );

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	auto *const weaponJumpState = &context->movementState->weaponJumpMovementState;
	auto *const record = context->record;

	Vec3 velocity( entityPhysicsState.Velocity() );
	const float speed = velocity.Normalize();
	Vec3 newVelocity( weaponJumpState->FireTarget() );
	newVelocity -= entityPhysicsState.Origin();
	newVelocity.NormalizeFast();

	bool weaponJumpFailed = false;
	if( level.time - bot->lastOwnKnockbackAt > 16 || bot->lastOwnKnockbackKick < 25 ) {
		weaponJumpFailed = true;
	} else if( speed < context->GetDashSpeed() || velocity.Dot( newVelocity ) < 0.3 ) {
		weaponJumpFailed = true;
	}

	if( weaponJumpFailed ) {
		Debug( "The weapon jump attempt has failed, deactivating weapon jump state\n" );
		weaponJumpState->Deactivate();
		this->DisableWithAlternative( context, &DefaultWalkAction() );
		return;
	}

	newVelocity *= speed;

	record->SetModifiedVelocity( newVelocity );
	record->botInput.SetIntendedLookDir( newVelocity );
	record->botInput.SetTurnSpeedMultiplier( 15.0f );
	record->botInput.isUcmdSet = true;
	weaponJumpState->hasCorrectedWeaponJump = true;
	context->isCompleted = true;
}

void WeaponJumpableSpotDetector::GetCandidateSpots( SpotAndScore **begin, SpotAndScore **end ) {
	*begin = spots;
	*end = spots + numSpots;
}

void WeaponJumpableSpotDetector::GetVelocityForJumpingToSpot( vec3_t velocity, const SpotAndScore *spot ) {
	Vec3 dir( spot->origin );
	dir.Z() += zOffsets[spot->tag];
	dir -= origin;
	dir.NormalizeFast();

	dir.CopyTo( velocity );
	VectorScale( velocity, push, velocity );
}

void WeaponJumpWeaponsTester::SetupForWeapon( int weaponNum ) {
	// TODO: Support weak ammo?
	const float knockback = GS_GetWeaponDef( weaponNum )->firedef.knockback;
	// TODO: We assume the default player mass
	const float push = 1000.0f * ( (float)knockback / 200.0f );
	const float *origin = detector.origin;

	spots.clear();

	for( int i = 0; i < numAreas; ++i ) {
		const float square2DDistance = Distance2DSquared( origin, targets[i] );
		if( square2DDistance < 1 ) {
			continue;
		}

		// This is not an exact solution (the time is not really correct) but compensates gravity fairly good
		const float distance = sqrtf( square2DDistance );
		const float tana = ( targets[i][2] - origin[2] ) / distance;
		const float cosa = sqrtf( 1.0f / ( 1.0f + tana * tana ) );
		const float t = distance / ( push * cosa );
		zOffsets[i] = 0.5f * level.gravity * t * t;

		// Closest to the target spots get greater score
		spots.emplace_back( SpotAndScore( targets[i], -travelTimes[i], areaNums[i], i ) );
		std::push_heap( spots.begin(), spots.end() );
	}

	detector.spots = spots.begin();
	detector.numSpots = spots.size();
	detector.zOffsets = zOffsets;
	detector.push = push;
}

void ScheduleWeaponJumpAction::SaveLandingAreas( Context *context, int areaNum ) {
	module->savedLandingAreas.clear();
	module->savedLandingAreas.push_back( areaNum );

	// Try add some nearby areas.
	// Using areas reachable from the target area, add a nearby area if the target area is reachable from it too.
	// Would be fine if we have reused initial raw bbox/cluster areas, but this complicates interface.
	const auto &aasWorld = AiAasWorld::Instance();
	const auto *aasReach = aasWorld->Reachabilities();
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const auto &targetAreaSettings = aasAreaSettings[areaNum];
	const int endReachNum = targetAreaSettings.firstreachablearea + targetAreaSettings.numreachableareas;
	for( int reachNum = targetAreaSettings.firstreachablearea; reachNum < endReachNum; ++reachNum ) {
		const auto &reach = aasReach[reachNum];
		if( ( reach.traveltype & TRAVELTYPE_MASK ) != TRAVEL_WALK ) {
			continue;
		}
		// TODO: Utilize router reverse reach.
		const auto &reachAreaSettings = aasAreaSettings[reach.areanum];
		if( reachAreaSettings.areaflags & AREA_JUNK ) {
			continue;
		}
		const int endRevReachNum = reachAreaSettings.firstreachablearea + reachAreaSettings.numreachableareas;
		for( int revReachNum = reachAreaSettings.firstreachablearea; revReachNum < endRevReachNum; ++revReachNum ) {
			const auto &revReach = aasReach[revReachNum];
			if( reach.areanum != areaNum ) {
				continue;
			}
			int revTravelType = revReach.traveltype & TRAVELTYPE_MASK;
			if( revTravelType != TRAVEL_WALK && revTravelType != TRAVEL_WALKOFFLEDGE ) {
				// If we have found a reach. that points back to an area, it is unique (?) or should be unique.
				// Avoid fruitless further tests for this reverse reach.
				goto nextDirectReach;
			}
			module->savedLandingAreas.push_back( reach.areanum );
			if( module->savedLandingAreas.size() == module->savedLandingAreas.capacity() ) {
				return;
			}
		}
nextDirectReach:;
	}
}

bool WeaponJumpWeaponsTester::Exec( MovementPredictionContext *context, ScheduleWeaponJumpAction *action ) {
	const float *botOrigin = context->movementState->entityPhysicsState.Origin();
	auto *weaponJumpState = &context->movementState->weaponJumpMovementState;

	detector.SetStepMillis( 192 );
	detector.SetNumSteps( 16 );
	detector.SetColliderBounds( playerbox_stand_mins, playerbox_stand_maxs );
	VectorCopy( context->movementState->entityPhysicsState.Origin(), detector.origin );
	// Try avoiding collisions with the ground at start
	detector.origin[2] += 4.0f;

	for( int i = 0; i < numWeapons; ++i ) {
		const int weaponNum = suggestedWeapons[i];
		SetupForWeapon( weaponNum );

		unsigned millis;
		const auto *spot = detector.Exec( botOrigin, &millis );
		if( !spot ) {
			continue;
		}

		Vec3 fireTarget( spot->origin );
		fireTarget.Z() += zOffsets[spot->tag];
		Vec3 jumpTarget( spot->origin );
		Vec3 originAtStart( botOrigin );
		weaponJumpState->Activate( jumpTarget, fireTarget, originAtStart, millis, weaponNum );

		PrepareAnglesAndWeapon( context );
		action->SaveLandingAreas( context, spot->areaNum );
		return true;
	}

	return false;
}