#include "BaseEnemyPool.h"
#include "../bot.h"

constexpr float MAX_ENEMY_WEIGHT = 5.0f;

float DamageToKill( const edict_t *ent, float armorProtection, float armorDegradation ) {
	if( !ent || !ent->takedamage ) {
		return std::numeric_limits<float>::infinity();
	}

	if( !ent->r.client ) {
		return ent->health;
	}

	float health = ent->r.client->ps.stats[STAT_HEALTH];
	float armor = ent->r.client->ps.stats[STAT_ARMOR];

	return DamageToKill( health, armor, armorProtection, armorDegradation );
}

float DamageToKill( float health, float armor, float armorProtection, float armorDegradation ) {
	if( !armor ) {
		return health;
	}
	if( armorProtection == 1.0f ) {
		return std::numeric_limits<float>::infinity();
	}

	if( armorDegradation != 0 ) {
		float damageToWipeArmor = armor / armorDegradation;
		float healthDamageToWipeArmor = damageToWipeArmor * ( 1.0f - armorProtection );

		if( healthDamageToWipeArmor < health ) {
			return damageToWipeArmor + ( health - healthDamageToWipeArmor );
		}

		return health / ( 1.0f - armorProtection );
	}

	return health / ( 1.0f - armorProtection );
}

void Enemy::Clear() {
	ent = nullptr;
	weight = 0.0f;
	avgPositiveWeight = 0.0f;
	maxPositiveWeight = 0.0f;
	positiveWeightsCount = 0;
	registeredAt = 0;
	lastSeenSnapshots.clear();
	lastSeenAt = 0;

	checkForWeaponHitFlags = 0;
	checkForWeaponHitKillDamage = 0.0f;
	numBoxLeafNums = 0;
	lookDirComputedAt = 0;
	boxLeafNumsComputedAt = 0;
	weaponHitFlagsComputedAt = 0;

	listLinks[TRACKED_LIST_INDEX].Clear();
	listLinks[ACTIVE_LIST_INDEX].Clear();
}

void Enemy::OnViewed( const float *specifiedOrigin ) {
	if( lastSeenSnapshots.size() == MAX_TRACKED_SNAPSHOTS ) {
		lastSeenSnapshots.pop_front();
	}

	// Put the likely case first
	const float *origin = !specifiedOrigin ? ent->s.origin : specifiedOrigin;
	// Set members for faster access
	VectorCopy( origin, lastSeenOrigin.Data() );
	VectorCopy( ent->velocity, lastSeenVelocity.Data() );
	lastSeenAt = level.time;
	// Store in a queue then for history
	lastSeenSnapshots.emplace_back( Snapshot( ent->s.origin, ent->velocity, level.time ) );
	assert( IsInTrackedList() );
}

Vec3 Enemy::LookDir() const {
	const auto levelTime = level.time;
	if( lookDirComputedAt == levelTime ) {
		return Vec3( lookDir );
	}

	lookDirComputedAt = levelTime;
	if( ent->ai && ent->ai->botRef ) {
		ent->ai->botRef->EntityPhysicsState()->ForwardDir().CopyTo( lookDir );
	} else {
		AngleVectors( ent->s.angles, lookDir, nullptr, nullptr );
	}

	return Vec3( lookDir );
}

enum { RAIL = 1, SHAFT = 2, ROCKET = 4 };

int Enemy::GetCheckForWeaponHitFlags( float damageToKillTarget ) const {
	auto levelTime = level.time;

	if( weaponHitFlagsComputedAt == levelTime && checkForWeaponHitKillDamage == damageToKillTarget ) {
		return checkForWeaponHitFlags;
	}

	weaponHitFlagsComputedAt = levelTime;
	checkForWeaponHitKillDamage = damageToKillTarget;

	return ( checkForWeaponHitFlags = ComputeCheckForWeaponHitFlags( damageToKillTarget ) );
}

int Enemy::ComputeCheckForWeaponHitFlags( float damageToKillTarget ) const {
	if( !ent->r.client ) {
		return 0;
	}

	int flags = 0;
	if( damageToKillTarget < 150 ) {
		if( RocketsReadyToFireCount() || WavesReadyToFireCount()) {
			flags |= ROCKET;
		}
	}

	if( InstasReadyToFireCount() ) {
		flags |= RAIL;
	} else if( ( HasQuad() || damageToKillTarget < 140 ) && BoltsReadyToFireCount() ) {
		flags |= RAIL;
	}

	if( LasersReadyToFireCount() && damageToKillTarget < 80 ) {
		flags |= SHAFT;
	} else if( BulletsReadyToFireCount() && ( ( HasQuad() || damageToKillTarget < 30 ) ) ) {
		flags |= SHAFT;
	}

	return flags;
}

bool Enemy::MightBlockArea( float damageToKillTarget, int areaNum, int reachNum, const AiAasWorld *aasWorld ) const {
	assert( IsValid() );

	int weaponHitFlags = GetCheckForWeaponHitFlags( damageToKillTarget );
	const auto &reach = aasWorld->Reachabilities()[reachNum];
	int travelType = reach.traveltype;
	// Don't check for rail-like shots except the bot is vulnerable in air
	if( travelType != TRAVEL_JUMPPAD && travelType != TRAVEL_JUMP && travelType != TRAVEL_STRAFEJUMP ) {
		// If there is no falling or its insignificant
		if( travelType != TRAVEL_WALKOFFLEDGE || DistanceSquared( reach.start, reach.end ) < 72 * 72 ) {
			weaponHitFlags &= ~RAIL;
		}
	}

	float baseRadius = 256.0f;
	if( HasQuad() ) {
		baseRadius = 768.0f;
	}

	const auto &area = aasWorld->Areas()[areaNum];
	Vec3 enemyOrigin( LastSeenOrigin());
	float squareDistance = enemyOrigin.DistanceTo( area.center );
	if( squareDistance > baseRadius * baseRadius ) {
		if( !weaponHitFlags ) {
			return false;
		}
		if( !( weaponHitFlags & RAIL ) ) {
			if( squareDistance > 1000 * 1000 ) {
				return false;
			}
		}
		if( weaponHitFlags == ROCKET ) {
			if( enemyOrigin.Z() < area.mins[2] ) {
				return false;
			}
		}

		Vec3 toAreaDir( area.center );
		toAreaDir -= enemyOrigin;
		toAreaDir *= 1.0f / sqrtf( squareDistance );
		float dot = toAreaDir.Dot( LookDir());
		if( dot < 0.3f ) {
			if( const auto *client = ent->r.client ) {
				if( !client->ps.stats[PM_STAT_ZOOMTIME] ) {
					return false;
				}
			}
			if( squareDistance > 1500 * 1500 ) {
				return false;
			}
		}
		if( dot < 0 ) {
			return false;
		}
	}

	if( !IsAreaInPVS( areaNum, aasWorld ) ) {
		return false;
	}

	trace_t trace;
	SolidWorldTrace( &trace, area.center, enemyOrigin.Data() );
	return trace.fraction == 1.0f;
}

int Enemy::GetBoxLeafNums( int **leafNums ) const {
	auto levelTime = level.time;
	if( boxLeafNumsComputedAt == levelTime ) {
		*leafNums = boxLeafNums;
		return numBoxLeafNums;
	}

	boxLeafNumsComputedAt = levelTime;

	// We can't reuse entity leaf nums that were set on linking it to area grid since the origin differs
	Vec3 enemyBoxMins( playerbox_stand_mins );
	Vec3 enemyBoxMaxs( playerbox_stand_maxs );
	Vec3 origin( LastSeenOrigin() );
	enemyBoxMins += origin;
	enemyBoxMaxs += origin;

	int topNode;
	numBoxLeafNums = trap_CM_BoxLeafnums( enemyBoxMins.Data(), enemyBoxMaxs.Data(), boxLeafNums, 8, &topNode );
	clamp_high( numBoxLeafNums, 8 );

	*leafNums = boxLeafNums;
	return numBoxLeafNums;
}

bool Enemy::IsAreaInPVS( int areaNum, const AiAasWorld *aasWorld ) const {
	const int *areaLeafNums = aasWorld->AreaMapLeafsList( areaNum ) + 1;

	int *enemyLeafNums;
	const int numEnemyLeafs = GetBoxLeafNums( &enemyLeafNums );

	for( int i = 0; i < numEnemyLeafs; ++i ) {
		for( int j = 0; j < areaLeafNums[-1]; ++j ) {
			if( trap_CM_LeafsInPVS( enemyLeafNums[i], areaLeafNums[j] ) ) {
				return true;
			}
		}
	}

	return false;
}

AiBaseEnemyPool::AiBaseEnemyPool( float avgSkill_ )
	: avgSkill( avgSkill_ ),
	numTrackedEnemies( 0 ),
	maxTrackedAttackers( From1UpToMax( MAX_TRACKED_ATTACKERS, avgSkill_ ) ),
	maxTrackedTargets( From1UpToMax( MAX_TRACKED_TARGETS, avgSkill_ ) ),
	maxActiveEnemies( From1UpToMax( MAX_ACTIVE_ENEMIES, avgSkill_ ) ),
	reactionTime( 320 - From0UpToMax( 300, avgSkill_ ) ),
	prevThinkLevelTime( 0 ),
	hasQuad( false ),
	hasShell( false ),
	damageToBeKilled( 0.0f ) {
	// Initialize table slots
	for( Enemy &enemy: entityToEnemyTable ) {
		enemy.Clear();
		enemy.parent = this;
		enemy.entNum = (int)( &enemy - entityToEnemyTable );
	}

	for( unsigned i = 0; i < maxTrackedAttackers; ++i )
		attackers.push_back( AttackStats() );

	for( unsigned i = 0; i < maxTrackedTargets; ++i )
		targets.push_back( AttackStats() );
}

void AiBaseEnemyPool::Frame() {

	const int64_t levelTime = level.time;

	for( AttackStats &attackerStats: attackers ) {
		attackerStats.Frame();
		if( attackerStats.LastActivityAt() + ATTACKER_TIMEOUT < levelTime ) {
			attackerStats.Clear();
		}
	}

	for( AttackStats &targetStats: targets ) {
		targetStats.Frame();
		if( targetStats.LastActivityAt() + TARGET_TIMEOUT < levelTime ) {
			targetStats.Clear();
		}
	}

	// If we could see enemy entering teleportation a last Think() frame, update its tracked origin by the actual one.
	for( Enemy *enemy = TrackedEnemiesHead(); enemy; enemy = enemy->NextInTrackedList() ) {
		const edict_t *ent = enemy->ent;
		if( !ent ) {
			continue;
		}
		// If the enemy cannot be longer valid
		if( G_ISGHOSTING( ent ) ) {
			continue;
		}
		if( !ent->s.teleported ) {
			continue;
		}
		if( levelTime - enemy->lastSeenAt >= 64 ) {
			continue;
		}
		enemy->OnViewed();
	}
}

void AiBaseEnemyPool::PreThink() {
	hasQuad = CheckHasQuad();
	hasShell = CheckHasShell();
	damageToBeKilled = ComputeDamageToBeKilled();
}

void AiBaseEnemyPool::Think() {
	const int64_t levelTime = level.time;

	// We have to introduce an intermediate variable and save the next link on each iteration
	// first because the current variable gets unlinked and next link becomes invalid.
	Enemy *nextEnemy;
	for( Enemy *enemy = TrackedEnemiesHead(); enemy; enemy = nextEnemy ) {
		assert( enemy->ent );
		nextEnemy = enemy->NextInTrackedList();
		// Remove not seen yet enemies
		if( levelTime - enemy->LastSeenAt() > NOT_SEEN_UNLINK_TIMEOUT ) {
			Debug( "has not seen %s for %d ms, should forget this enemy\n", enemy->Nick(), NOT_SEEN_UNLINK_TIMEOUT );
			RemoveEnemy( enemy );
			continue;
		}
		if( G_ISGHOSTING( enemy->ent ) ) {
			Debug( "should forget %s (this enemy is ghosting)\n", enemy->Nick() );
			RemoveEnemy( enemy );
			continue;
		}
		// Do not forget, just skip
		if( enemy->ent->flags & ( FL_NOTARGET | FL_BUSY ) ) {
			continue;
		}
		// Skip during reaction time
		if( enemy->registeredAt + reactionTime > levelTime ) {
			continue;
		}

		UpdateEnemyWeight( enemy );
	}
}

float AiBaseEnemyPool::ComputeRawEnemyWeight( const edict_t *enemy ) {
	if( !enemy || G_ISGHOSTING( enemy ) ) {
		return 0.0;
	}

	float weight = 0.5f;
	if( !enemy->r.client ) {
		weight = enemy->aiIntrinsicEnemyWeight;
		if( weight <= 0.0f ) {
			return 0.0f;
		}
	}

	if( int64_t time = LastAttackedByTime( enemy ) ) {
		weight += 1.55f * ( 1.0f - BoundedFraction( level.time - time, ATTACKER_TIMEOUT ) );
		// TODO: Add weight for poor attackers (by total damage / attack attepts ratio)
	}

	if( int64_t time = LastTargetTime( enemy ) ) {
		weight += 1.55f * ( 1.0f - BoundedFraction( level.time - time, TARGET_TIMEOUT ) );
		// TODO: Add weight for targets that are well hit by bot
	}

	if( ::IsCarrier( enemy ) ) {
		weight += 2.0f;
	}

	constexpr float maxDamageToKill = 350.0f;

	float damageToKill = DamageToKill( enemy );
	if( hasQuad ) {
		damageToKill /= 4;
	}
	if( ::HasShell( enemy ) ) {
		damageToKill *= 4;
	}

	// abs(damageToBeKilled - damageToKill) / maxDamageToKill may be > 1
	weight += ( damageToBeKilled - damageToKill ) / maxDamageToKill;

	if( weight > 0 ) {
		if( hasQuad ) {
			weight *= 1.5f;
		}
		if( hasShell ) {
			weight += 0.5f;
		}
		if( hasQuad && hasShell ) {
			weight *= 1.5f;
		}
	}

	return std::min( std::max( 0.0f, weight ), MAX_ENEMY_WEIGHT );
}

void AiBaseEnemyPool::OnPain( const edict_t *bot, const edict_t *enemy, float kick, int damage ) {
	int attackerSlot = EnqueueAttacker( enemy, damage );
	if( attackerSlot < 0 ) {
		return;
	}

	bool newThreat = true;
	if( bot->ai->botRef->IsPrimaryAimEnemy( enemy ) ) {
		newThreat = false;
		int currEnemySlot = -1;
		for( int i = 0, end = attackers.size(); i < end; ++i ) {
			if( bot->ai->botRef->IsPrimaryAimEnemy( attackers[i].ent ) ) {
				currEnemySlot = i;
				break;
			}
		}
		// If current enemy did not inflict any damage
		// or new attacker hits harder than current one, there is a new threat
		if( currEnemySlot < 0 || attackers[currEnemySlot].totalDamage < attackers[attackerSlot].totalDamage ) {
			newThreat = true;
		}
	}

	if( newThreat ) {
		OnHurtByNewThreat( enemy );
	}
}

int64_t AiBaseEnemyPool::LastAttackedByTime( const edict_t *ent ) const {
	for( const AttackStats &attackStats: attackers )
		if( ent && attackStats.ent == ent ) {
			return attackStats.LastActivityAt();
		}

	return 0;
}

int64_t AiBaseEnemyPool::LastTargetTime( const edict_t *ent ) const {
	for( const AttackStats &targetStats: targets )
		if( ent && targetStats.ent == ent ) {
			return targetStats.LastActivityAt();
		}

	return 0;
}

float AiBaseEnemyPool::TotalDamageInflictedBy( const edict_t *ent ) const {
	for( const AttackStats &attackStats: attackers )
		if( ent && attackStats.ent == ent ) {
			return attackStats.totalDamage;
		}

	return 0;
}

int AiBaseEnemyPool::EnqueueAttacker( const edict_t *attacker, int damage ) {
	if( !attacker ) {
		return -1;
	}

	int freeSlot = -1;
	for( unsigned i = 0; i < attackers.size(); ++i ) {
		if( attackers[i].ent == attacker ) {
			attackers[i].OnDamage( damage );
			return i;
		} else if( !attackers[i].ent && freeSlot < 0 ) {
			freeSlot = i;
		}
	}
	if( freeSlot >= 0 ) {
		attackers[freeSlot].Clear();
		attackers[freeSlot].ent = attacker;
		attackers[freeSlot].OnDamage( damage );
		return freeSlot;
	}
	float maxEvictionScore = 0.0f;
	for( unsigned i = 0; i < attackers.size(); ++i ) {
		float timeFactor = ( level.time - attackers[i].LastActivityAt() ) / (float)ATTACKER_TIMEOUT;
		float damageFactor = 1.0f - BoundedFraction( attackers[i].totalDamage, 500.0f );
		// Always > 0, so we always evict some attacker
		float evictionScore = 0.1f + timeFactor * damageFactor;
		if( maxEvictionScore < evictionScore ) {
			maxEvictionScore = evictionScore;
			freeSlot = i;
		}
	}
	attackers[freeSlot].Clear();
	attackers[freeSlot].ent = attacker;
	attackers[freeSlot].OnDamage( damage );
	return freeSlot;
}

void AiBaseEnemyPool::EnqueueTarget( const edict_t *target ) {
	if( !target ) {
		return;
	}

	int freeSlot = -1;
	for( unsigned i = 0; i < targets.size(); ++i ) {
		if( targets[i].ent == target ) {
			targets[i].Touch();
			return;
		} else if( !targets[i].ent && freeSlot < 0 ) {
			freeSlot = i;
		}
	}
	if( freeSlot >= 0 ) {
		targets[freeSlot].Clear();
		targets[freeSlot].ent = target;
		targets[freeSlot].Touch();
		return;
	}
	float maxEvictionScore = 0.0f;
	for( unsigned i = 0; i < targets.size(); ++i ) {
		float timeFactor = ( level.time - targets[i].LastActivityAt() ) / (float)TARGET_TIMEOUT;
		// Do not evict enemies that bot hit hard
		float damageScale = this->HasQuad() ? 4.0f : 1.0f;
		float damageFactor = 1.0f - BoundedFraction( targets[i].totalDamage, 300.0f * damageScale );
		// Always > 0, so we always evict some target
		float evictionScore = 0.1f + timeFactor * damageFactor;
		if( maxEvictionScore < evictionScore ) {
			maxEvictionScore = evictionScore;
			freeSlot = i;
		}
	}
	targets[freeSlot].Clear();
	targets[freeSlot].ent = target;
	targets[freeSlot].Touch();
}

void AiBaseEnemyPool::OnEnemyDamaged( const edict_t *bot, const edict_t *target, int damage ) {
	if( !target ) {
		return;
	}

	for( unsigned i = 0; i < targets.size(); ++i ) {
		if( targets[i].ent == target ) {
			targets[i].OnDamage( damage );
			return;
		}
	}
}

bool AiBaseEnemyPool::WillAssignAimEnemy() const {
	for( const Enemy *enemy = TrackedEnemiesHead(); enemy; enemy = enemy->NextInTrackedList() ) {
		if( !enemy->ent ) {
			continue;
		}
		if( enemy->LastSeenAt() == level.time ) {
			// Check whether we may react
			for( const auto &snapshot: enemy->lastSeenSnapshots ) {
				if( snapshot.Timestamp() + reactionTime <= level.time ) {
					return true;
				}
			}
		}
	}
	return false;
}

void AiBaseEnemyPool::UpdateEnemyWeight( Enemy *enemy ) {
	// Explicitly limit effective reaction time to a time quantum between Think() calls
	// This method gets called before all enemies are viewed.
	// For seen enemy registration actual weights of known enemies are mandatory
	// (enemies may get evicted based on their weights and weight of a just seen enemy).
	if( level.time - enemy->LastSeenAt() > std::max( 64u, reactionTime ) ) {
		enemy->weight = 0;
		return;
	}

	enemy->weight = ComputeRawEnemyWeight( enemy->ent );
	if( enemy->weight > enemy->maxPositiveWeight ) {
		enemy->maxPositiveWeight = enemy->weight;
	}
	if( enemy->weight > 0 ) {
		enemy->avgPositiveWeight = enemy->avgPositiveWeight * enemy->positiveWeightsCount + enemy->weight;
		enemy->positiveWeightsCount++;
		enemy->avgPositiveWeight /= enemy->positiveWeightsCount;
	}
}

const Enemy *AiBaseEnemyPool::ChooseVisibleEnemy( const edict_t *challenger ) {
	Vec3 botOrigin( challenger->s.origin );
	vec3_t forward;
	AngleVectors( challenger->s.angles, forward, nullptr, nullptr );

	bool isEntityTested[MAX_EDICTS];
	memset( isEntityTested, 0, sizeof( isEntityTested ) );

	// Until these bounds distance factor scales linearly
	constexpr float distanceBounds = 3500.0f;

	StaticVector<EntAndScore, MAX_EDICTS> candidates;
	for( Enemy *enemy = TrackedEnemiesHead(); enemy; enemy = enemy->NextInTrackedList() ) {
		isEntityTested[enemy->entNum] = true;

		if( !enemy->ent ) {
			continue;
		}
		// Not seen in this frame enemies have zero weight;
		if( !enemy->weight ) {
			continue;
		}

		Vec3 botToEnemy = botOrigin - enemy->ent->s.origin;
		float distance = botToEnemy.LengthFast();
		botToEnemy *= 1.0f / distance;
		// For far enemies distance factor is lower
		float distanceFactor = 1.0f - 0.7f * BoundedFraction( distance, distanceBounds );
		// Should affect the score only a bit (otherwise bot will miss a dangerous enemy that he is not looking at).
		float directionFactor = 0.7f + 0.3f * botToEnemy.Dot( forward );

		float weight = enemy->weight + GetAdditionalEnemyWeight( challenger, enemy->ent );
		float currScore = weight * distanceFactor * directionFactor;
		candidates.push_back( EntAndScore( ENTNUM( enemy->ent ), currScore ) );
	}

	if( candidates.empty() ) {
		OnBotEnemyAssigned( challenger, nullptr );
		return nullptr;
	}

	// Its better to sort once instead of pushing into a heap inside the loop above
	std::sort( candidates.begin(), candidates.end() );

	// Now candidates should be merged in a list of active enemies
	StaticVector<EntAndScore, MAX_ACTIVE_ENEMIES * 2> mergedActiveEnemies;
	// Best candidates are first (EnemyAndScore::operator<() yields this result)
	// Choose not more than maxActiveEnemies candidates
	// that have a score not than twice less than the current best one
	float bestCurrentActiveScore = candidates.front().score;
	for( unsigned i = 0, end = std::min( candidates.size(), maxActiveEnemies ); i < end; ++i ) {
		if( candidates[i].score < 0.5f * bestCurrentActiveScore ) {
			break;
		}
		mergedActiveEnemies.push_back( candidates[i] );
	}

	// Add current active enemies to merged ones
	for( Enemy *enemy = ActiveEnemiesHead(); enemy; enemy = enemy->NextInActiveList() ) {
		if( !isEntityTested[enemy->entNum] ) {
			mergedActiveEnemies.push_back( EntAndScore( enemy->entNum, enemy->scoreAsActiveEnemy ) );
		}
	}

	// Sort merged enemies
	std::sort( mergedActiveEnemies.begin(), mergedActiveEnemies.end() );

	// Clear all links in active enemies list
	listHeads[ACTIVE_LIST_INDEX] = nullptr;
	for( Enemy *enemy = TrackedEnemiesHead(); enemy; enemy = enemy->NextInTrackedList() ) {
		enemy->listLinks[ACTIVE_LIST_INDEX].Clear();
	}

	// Select not more than maxActiveEnemies mergedActiveEnemies as a current activeEnemies
	// Note: start from the worst-score enemy.
	// Enemies are linked to the list head, so the best enemy should be linked last
	// (and be the first one at the start of iteration via NextInActiveList() calls)
	for( int i = (int)std::min( mergedActiveEnemies.size(), maxActiveEnemies ) - 1; i >= 0; --i ) {
		const auto &entAndScore = mergedActiveEnemies[i];
		Enemy *enemy = entityToEnemyTable + entAndScore.entNum;
		enemy->scoreAsActiveEnemy = entAndScore.score;
		LinkToActiveList( enemy );
		EnqueueTarget( enemy->ent );
	}

	OnBotEnemyAssigned( challenger, ActiveEnemiesHead() );
	return ActiveEnemiesHead();
}

const Enemy *AiBaseEnemyPool::ChooseLostOrHiddenEnemy( const edict_t *challenger, unsigned timeout ) {
	if( AvgSkill() < 0.33f ) {
		return nullptr;
	}

	vec3_t forward;
	AngleVectors( challenger->s.angles, forward, nullptr, nullptr );

	// ChooseLostOrHiddenEnemy(const edict_t *challenger, unsigned timeout = (unsigned)-1)
	if( timeout > NOT_SEEN_SUGGEST_TIMEOUT ) {
		timeout = NOT_SEEN_SUGGEST_TIMEOUT;
	}

	float bestScore = 0.0f;
	const Enemy *bestEnemy = nullptr;
	for( Enemy *enemy = TrackedEnemiesHead(); enemy; enemy = enemy->NextInTrackedList() ) {
		if( !enemy->IsValid() ) {
			continue;
		}

		// If it has been weighted for selection (and thus was considered visible)
		if( enemy->weight ) {
			continue;
		}

		Vec3 botToSpotDirection = enemy->LastSeenOrigin() - challenger->s.origin;
		float directionFactor = 0.5f;
		float distanceFactor = 1.0f;
		float squareDistance = botToSpotDirection.SquaredLength();
		if( squareDistance > 1 ) {
			float distance = 1.0f / Q_RSqrt( squareDistance );
			botToSpotDirection *= 1.0f / distance;
			directionFactor = 0.3f + 0.7f * botToSpotDirection.Dot( forward );
			distanceFactor = 1.0f - 0.9f * BoundedFraction( distance, 2000.0f );
		}
		float timeFactor = 1.0f - BoundedFraction( level.time - enemy->LastSeenAt(), timeout );

		float currScore = ( 0.5f * ( enemy->maxPositiveWeight + enemy->avgPositiveWeight ) );
		currScore *= directionFactor * distanceFactor * timeFactor;
		if( currScore > bestScore ) {
			bestScore = currScore;
			bestEnemy = enemy;
		}
	}

	return bestEnemy;
}

void AiBaseEnemyPool::OnEnemyViewed( const edict_t *ent ) {
	if( !ent ) {
		return;
	}

	Enemy *enemy = entityToEnemyTable + ENTNUM( ent );
	if( enemy->IsValid() ) {
		enemy->OnViewed();
	} else {
		enemy->InitAndLink( ent );
	}
}

void AiBaseEnemyPool::OnEnemyOriginGuessed( const edict_t *ent,
											unsigned minMillisSinceLastSeen,
											const float *guessedOrigin ) {
	if( !ent ) {
		return;
	}

	Enemy *enemy = entityToEnemyTable + ENTNUM( ent );
	if( enemy->IsValid() ) {
		// If there is already an Enemy record containing an entity,
		// check whether this record timed out enough to be overwritten.
		// This condition prevents overwriting
		if( enemy->lastSeenAt + minMillisSinceLastSeen <= level.time ) {
			enemy->OnViewed( guessedOrigin );
		}
	} else {
		enemy->InitAndLink( ent );
	}
}

void AiBaseEnemyPool::Forget( const edict_t *ent ) {
	if( !ent ) {
		return;
	}

	RemoveEnemy( entityToEnemyTable + ENTNUM( ent ) );
}

void AiBaseEnemyPool::RemoveEnemy( Enemy *enemy ) {
	// Call overridden method that should contain domain-specific logic
	OnEnemyRemoved( enemy );

	if( enemy->IsInActiveList() ) {
		UnlinkFromActiveList( enemy );
	}

	assert( enemy->IsInTrackedList() );
	UnlinkFromTrackedList( enemy );

	enemy->Clear();
	--numTrackedEnemies;
}

void Enemy::InitAndLink( const edict_t *ent, const float *specifiedOrigin ) {
	this->ent = ent;
	this->registeredAt = level.time;
	this->weight = 0.0f;
	this->avgPositiveWeight = 0.0f;
	this->maxPositiveWeight = 0.0f;
	this->positiveWeightsCount = 0;
	parent->LinkToTrackedList( this );
	parent->numTrackedEnemies++;
	this->OnViewed( specifiedOrigin );
}