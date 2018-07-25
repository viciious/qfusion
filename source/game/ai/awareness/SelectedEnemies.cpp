#include "SelectedEnemies.h"
#include "../bot.h"

bool SelectedEnemies::AreValid() const {
	for( const auto *enemy: activeEnemies ) {
		if( !enemy->IsValid() ) {
			return false;
		}
	}

	return timeoutAt > level.time;
}

void SelectedEnemies::Set( const TrackedEnemy *primaryEnemy_,
						   unsigned timeoutPeriod,
						   const TrackedEnemy **activeEnemiesBegin,
						   const TrackedEnemy **activeEnemiesEnd ) {
	this->primaryEnemy = primaryEnemy_;
	this->timeoutAt = level.time + timeoutPeriod;

#ifndef _DEBUG
	if( !activeEnemies.empty() ) {
		AI_FailWith( "SelectedEnemies::Set()", "activeEnemies.size() %d > 0", activeEnemies.size() );
	}
#endif

	for( const auto **enemy = activeEnemiesBegin; enemy != activeEnemiesEnd; ++enemy ) {
		this->activeEnemies.push_back( *enemy );
	}
}

void SelectedEnemies::Set( const TrackedEnemy *primaryEnemy_,
						   unsigned timeoutPeriod,
						   const TrackedEnemy *firstActiveEnemy ) {
	this->primaryEnemy = primaryEnemy_;
	this->timeoutAt = level.time + timeoutPeriod;

#ifndef _DEBUG
	if( !activeEnemies.empty() ) {
		AI_FailWith( "SelectedEnemies::Set()", "activeEnemies.size() %d > 0", activeEnemies.size() );
	}
#endif

	for( const auto *enemy = firstActiveEnemy; enemy; enemy = enemy->NextInActiveList() ) {
		this->activeEnemies.push_back( enemy );
	}
}

Vec3 SelectedEnemies::ClosestEnemyOrigin( const vec3_t relativelyTo ) const {
	const TrackedEnemy *closestEnemy = nullptr;
	float minSquareDistance = std::numeric_limits<float>::max();
	for( const auto *enemy: activeEnemies ) {
		float squareDistance = enemy->LastSeenOrigin().SquareDistanceTo( relativelyTo );
		if( minSquareDistance > squareDistance ) {
			minSquareDistance = squareDistance;
			closestEnemy = enemy;
		}
	}

	assert( closestEnemy );
	return closestEnemy->LastSeenOrigin();
}

float SelectedEnemies::DamageToKill() const {
	CheckValid( __FUNCTION__ );

	float result = 0.0f;
	for( const auto *enemy: activeEnemies ) {
		float damageToKill = ::DamageToKill( enemy->ent, g_armor_protection->value, g_armor_degradation->value );
		if( enemy->HasShell() ) {
			damageToKill *= 4.0f;
		}
		result += damageToKill;
	}

	return result;
}

unsigned SelectedEnemies::FireDelay() const {
	unsigned minDelay = std::numeric_limits<unsigned>::max();

	for( const auto *enemy: activeEnemies ) {
		if( !enemy->IsValid() ) {
			return std::numeric_limits<unsigned>::max();
		}

		if( !enemy->ent->r.client ) {
			return 0;
		}

		auto delay = (unsigned)enemy->ent->r.client->ps.stats[STAT_WEAPON_TIME];
		if( delay < minDelay ) {
			minDelay = delay;
		}
	}

	return minDelay;
}

bool SelectedEnemies::HaveQuad() const {
	CheckValid( __FUNCTION__ );

	for( const auto *enemy: activeEnemies ) {
		if( enemy->HasQuad() ) {
			return true;
		}
	}

	return false;
}

bool SelectedEnemies::HaveCarrier() const {
	CheckValid( __FUNCTION__ );

	for( const auto *enemy: activeEnemies ) {
		if( enemy->IsCarrier() ) {
			return true;
		}
	}

	return false;
}

bool SelectedEnemies::Contain( const TrackedEnemy *enemy ) const {
	CheckValid( __FUNCTION__ );

	for( const auto *activeEnemy: activeEnemies ) {
		if( activeEnemy == enemy ) {
			return true;
		}
	}

	return false;
}

float SelectedEnemies::MaxThreatFactor() const {
	if( maxThreatFactorComputedAt == level.time ) {
		return maxThreatFactor;
	}

	if( activeEnemies.empty() ) {
		return 0.0f;
	}

	maxThreatFactor = 0;
	for( int i = 0, end = (int)activeEnemies.size(); i < end; ++i ) {
		float factor = GetThreatFactor( i );
		if( factor > maxThreatFactor ) {
			maxThreatFactor = factor;
		}
	}

	return maxThreatFactor;
}

float SelectedEnemies::GetThreatFactor( int enemyNum ) const {
	if( threatFactorsComputedAt[enemyNum] == level.time ) {
		return threatFactors[enemyNum];
	}

	threatFactorsComputedAt[enemyNum] = level.time;
	threatFactors[enemyNum] = ComputeThreatFactor( enemyNum );
	return threatFactors[enemyNum];
}

float SelectedEnemies::ComputeThreatFactor( int enemyNum ) const {
	const auto *enemy = activeEnemies[enemyNum];
	float entFactor = ComputeThreatFactor( enemy->ent, enemyNum );
	if( level.time - activeEnemies[enemyNum]->LastAttackedByTime() < 1000 ) {
		entFactor = sqrtf( entFactor );
	}

	return entFactor;
}

float SelectedEnemies::ComputeThreatFactor( const edict_t *ent, int enemyNum ) const {
	if( !ent ) {
		return 0.0f;
	}

	// Try cutting off further expensive calls by doing this cheap test first
	if( const auto *client = ent->r.client ) {
		// Can't shoot soon.
		if( client->ps.stats[STAT_WEAPON_TIME] > 800 ) {
			return 0.0f;
		}
	}

	Vec3 enemyToBotDir( self->s.origin );
	enemyToBotDir -= ent->s.origin;
	enemyToBotDir.NormalizeFast();

	float dot;

	if( ent->ai && ent->ai->botRef ) {
		dot = enemyToBotDir.Dot( ent->ai->botRef->EntityPhysicsState()->ForwardDir() );
		if( dot < self->ai->botRef->FovDotFactor() ) {
			return 0.0f;
		}
	} else {
		vec3_t enemyLookDir;
		AngleVectors( ent->s.angles, enemyLookDir, nullptr, nullptr );
		dot = enemyToBotDir.Dot( enemyLookDir );
		// There is no threat if the bot is not in fov for a client (but not for a turret for example)
		if ( ent->r.client && dot < 0.2f ) {
			return 0.0f;
		}
	}

	if( !EntitiesPvsCache::Instance()->AreInPvs( ent, self ) ) {
		return 0.0f;
	}

	if( ent->s.effects & ( EF_QUAD | EF_CARRIER ) ) {
		return 1.0f;
	}

	if( const auto *hazard = self->ai->botRef->PrimaryHazard() ) {
		if( hazard->attacker == ent ) {
			return 0.5f + 0.5f * BoundedFraction( hazard->damage, 75 );
		}
	}

	// Its guaranteed that the enemy cannot hit
	if( dot < 0.7f ) {
		return 0.5f * dot;
	}

	if( enemyNum >= 0 ) {
		if( !GetCanHit( enemyNum ) ) {
			dot *= 0.5f;
		}
	} else if( !TestCanHit( ent ) ) {
		dot *= 0.5f;
	}

	return sqrtf( dot );
}

float SelectedEnemies::TotalInflictedDamage() const {
	CheckValid( __FUNCTION__ );

	float damage = 0;
	for( const auto *activeEnemy: activeEnemies )
		damage += activeEnemy->TotalInflictedDamage();

	return damage;
}

float SelectedEnemies::MaxDotProductOfBotViewAndDirToEnemy() const {
	vec3_t botViewDir;

	float maxDot = -1.0f;
	for( const auto *enemy: activeEnemies ) {
		Vec3 toEnemyDir( enemy->LastSeenOrigin() );
		toEnemyDir -= self->s.origin;
		toEnemyDir.NormalizeFast();
		float dot = toEnemyDir.Dot( botViewDir );
		if( dot > maxDot ) {
			maxDot = dot;
		}
	}
	return maxDot;
}

float SelectedEnemies::MaxDotProductOfEnemyViewAndDirToBot() const {
	float maxDot = -1.0f;
	for( const auto *enemy: activeEnemies ) {
		Vec3 toBotDir( self->s.origin );
		toBotDir -= enemy->LastSeenOrigin();
		toBotDir.NormalizeFast();
		float dot = toBotDir.Dot( enemy->LookDir() );
		if( dot > maxDot ) {
			maxDot = dot;
		}
	}
	return maxDot;
}

bool SelectedEnemies::ArePotentiallyHittable() const {
	CheckValid( __FUNCTION__ );

	if( arePotentiallyHittableComputedAt == level.time ) {
		return arePotentiallyHittable;
	}

	trace_t trace;
	Vec3 viewPoint( self->s.origin );
	viewPoint.Z() += self->viewheight;
	const auto *pvsCache = EntitiesPvsCache::Instance();
	for( const auto *enemy: activeEnemies ) {
		const auto *enemyEnt = enemy->ent;
		if( !enemyEnt ) {
			continue;
		}
		if( !pvsCache->AreInPvs( self, enemyEnt ) ) {
			continue;
		}
		SolidWorldTrace( &trace, viewPoint.Data(), enemyEnt->s.origin );
		if( trace.fraction == 1.0f ) {
			arePotentiallyHittableComputedAt = level.time;
			arePotentiallyHittable = true;
			return true;
		}
	}

	arePotentiallyHittableComputedAt = level.time;
	arePotentiallyHittable = false;
	return false;
}

bool SelectedEnemies::CanHit() const {
	CheckValid( __FUNCTION__ );

	if( canEnemiesHitComputedAt == level.time ) {
		return canEnemiesHit;
	}

	for( int i = 0, end = (int)activeEnemies.size(); i < end; ++i ) {
		if( GetCanHit( i ) ) {
			canEnemiesHitComputedAt = level.time;
			canEnemiesHit = true;
			return true;
		}
	}

	canEnemiesHitComputedAt = level.time;
	canEnemiesHit = false;
	return false;
}

bool SelectedEnemies::GetCanHit( int enemyNum ) const {
	if( canEnemyHitComputedAt[enemyNum] == level.time ) {
		return canEnemyHit[enemyNum];
	}

	canEnemyHitComputedAt[enemyNum] = level.time;
	canEnemyHit[enemyNum] = TestCanHit( activeEnemies[enemyNum]->ent );
	return canEnemyHit[enemyNum];
}

bool SelectedEnemies::TestCanHit( const edict_t *enemy ) const {
	if( !enemy ) {
		return false;
	}

	Vec3 enemyToBot( self->s.origin );
	enemyToBot -= enemy->s.origin;
	enemyToBot.NormalizeFast();

	if( enemy->ai && enemy->ai->botRef ) {
		if( enemyToBot.Dot( self->ai->botRef->EntityPhysicsState()->ForwardDir() ) < self->ai->botRef->FovDotFactor() ) {
			return false;
		}
	} else if ( enemy->r.client ) {
		vec3_t forwardDir;
		AngleVectors( enemy->s.origin, forwardDir, nullptr, nullptr );
		if( enemyToBot.Dot( forwardDir ) < 0 ) {
			return false;
		}
	}

	if( !EntitiesPvsCache::Instance()->AreInPvs( enemy, self ) ) {
		return false;
	}

	auto *targetEnt = const_cast<edict_t *>( self );
	trace_t trace;
	auto *enemyEnt = const_cast<edict_t *>( enemy );
	Vec3 traceStart( enemyEnt->s.origin );
	traceStart.Z() += enemyEnt->viewheight;

	G_Trace( &trace, traceStart.Data(), nullptr, nullptr, targetEnt->s.origin, enemyEnt, MASK_AISOLID );
	if( trace.fraction != 1.0f && game.edicts + trace.ent == targetEnt ) {
		return true;
	}

	// If there is a distinct chest point (we call it chest since it is usually on chest position)
	if( abs( targetEnt->viewheight ) > 8 ) {
		Vec3 targetPoint( targetEnt->s.origin );
		targetPoint.Z() += targetEnt->viewheight;
		G_Trace( &trace, traceStart.Data(), nullptr, nullptr, targetPoint.Data(), enemyEnt, MASK_AISOLID );
		if( trace.fraction != 1.0f && game.edicts + trace.ent == targetEnt ) {
			return true;
		}
	}

	// Don't waste cycles on further tests (as it used to be).
	// This test is for getting a coarse info anyway.

	return false;
}

bool SelectedEnemies::HaveGoodSniperRangeWeapons() const {
	CheckValid( __FUNCTION__ );
	for( const auto *activeEnemy: activeEnemies ) {
		if( activeEnemy->BoltsReadyToFireCount() ) {
			return true;
		}
		if( activeEnemy->BulletsReadyToFireCount() ) {
			return true;
		}
		if( activeEnemy->InstasReadyToFireCount() ) {
			return true;
		}
	}
	return false;
}

bool SelectedEnemies::HaveGoodFarRangeWeapons() const {
	CheckValid( __FUNCTION__ );
	for( const auto *activeEnemy: activeEnemies ) {
		if( activeEnemy->BoltsReadyToFireCount() ) {
			return true;
		}
		if( activeEnemy->BulletsReadyToFireCount() ) {
			return true;
		}
		if( activeEnemy->PlasmasReadyToFireCount() ) {
			return true;
		}
		if( activeEnemy->InstasReadyToFireCount() ) {
			return true;
		}
	}
	return false;
}

bool SelectedEnemies::HaveGoodMiddleRangeWeapons() const {
	CheckValid( __FUNCTION__ );
	for( const auto *activeEnemy: activeEnemies ) {
		if( activeEnemy->RocketsReadyToFireCount() ) {
			return true;
		}
		if( activeEnemy->LasersReadyToFireCount() ) {
			return true;
		}
		if( activeEnemy->PlasmasReadyToFireCount() ) {
			return true;
		}
		if( activeEnemy->WavesReadyToFireCount() ) {
			return true;
		}
		if( activeEnemy->BulletsReadyToFireCount() ) {
			return true;
		}
		if( activeEnemy->ShellsReadyToFireCount() ) {
			return true;
		}
		if( activeEnemy->InstasReadyToFireCount() ) {
			return true;
		}
	}
	return false;
}

bool SelectedEnemies::HaveGoodCloseRangeWeapons() const {
	CheckValid( __FUNCTION__ );
	for( const auto *activeEnemy: activeEnemies ) {
		if( activeEnemy->RocketsReadyToFireCount() ) {
			return true;
		}
		if( activeEnemy->PlasmasReadyToFireCount() ) {
			return true;
		}
		if( activeEnemy->WavesReadyToFireCount() ) {
			return true;
		}
		if( activeEnemy->ShellsReadyToFireCount() ) {
			return true;
		}
	}
	return false;
}