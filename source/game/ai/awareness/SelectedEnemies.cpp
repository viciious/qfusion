#include "SelectedEnemies.h"
#include "EntitiesPvsCache.h"
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
		auto delay = (unsigned)enemy->FireDelay();
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
		if( !GetCanHit( enemyNum, GetEnemyViewDirDotToBotDirValues()[enemyNum] ) ) {
			dot *= 0.5f;
		}
	} else if( !TestCanHit( ent, GetEnemyViewDirDotToBotDirValues()[enemyNum] ) ) {
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

bool SelectedEnemies::ArePotentiallyHittable() const {
	CheckValid( __FUNCTION__ );

	if( arePotentiallyHittableComputedAt == level.time ) {
		return arePotentiallyHittable;
	}

	const auto *viewDots = GetBotViewDirDotToEnemyDirValues();

	trace_t trace;
	Vec3 viewPoint( self->s.origin );
	viewPoint.Z() += self->viewheight;
	const auto *pvsCache = EntitiesPvsCache::Instance();
	for( unsigned i = 0; i < activeEnemies.size(); ++i ) {
		const auto *enemy = activeEnemies[i];
		const auto *enemyEnt = enemy->ent;
		if( !enemyEnt ) {
			continue;
		}
		if( viewDots[i] < 0.7f ) {
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

	const auto *viewDots = GetEnemyViewDirDotToBotDirValues();

	if( canEnemiesHitComputedAt == level.time ) {
		return canEnemiesHit;
	}

	for( int i = 0, end = (int)activeEnemies.size(); i < end; ++i ) {
		if( GetCanHit( i, viewDots[i] ) ) {
			canEnemiesHitComputedAt = level.time;
			canEnemiesHit = true;
			return true;
		}
	}

	canEnemiesHitComputedAt = level.time;
	canEnemiesHit = false;
	return false;
}

bool SelectedEnemies::GetCanHit( int enemyNum, float viewDot ) const {
	if( canEnemyHitComputedAt[enemyNum] == level.time ) {
		return canEnemyHit[enemyNum];
	}

	canEnemyHitComputedAt[enemyNum] = level.time;
	canEnemyHit[enemyNum] = TestCanHit( activeEnemies[enemyNum]->ent, viewDot );
	return canEnemyHit[enemyNum];
}

bool SelectedEnemies::TestCanHit( const edict_t *enemy, float viewDot ) const {
	if( !enemy ) {
		return false;
	}

	if( viewDot < 0.7f ) {
		return false;
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

const float *SelectedEnemies::GetBotViewDirDotToEnemyDirValues() const {
	const auto levelTime = level.time;
	if( levelTime == botViewDirDotToEnemyDirComputedAt ) {
		return botViewDirDotToEnemyDir;
	}

	Vec3 botViewDir( self->ai->botRef->EntityPhysicsState()->ForwardDir() );
	for( unsigned i = 0; i < activeEnemies.size(); ++i ) {
		Vec3 botToEnemyDir( activeEnemies[i]->LastSeenOrigin() );
		botToEnemyDir -= self->s.origin;
		botToEnemyDir.Z() -= playerbox_stand_viewheight;
		botToEnemyDir.NormalizeFast();
		botViewDirDotToEnemyDir[i] = botViewDir.Dot( botToEnemyDir );
	}

	botViewDirDotToEnemyDirComputedAt = levelTime;
	return botViewDirDotToEnemyDir;
}

const float *SelectedEnemies::GetEnemyViewDirDotToBotDirValues() const {
	const auto levelTime = level.time;
	if( levelTime == enemyViewDirDotToBotDirComputedAt ) {
		return enemyViewDirDotToBotDir;
	}

	for( unsigned i = 0; i < activeEnemies.size(); ++i ) {
		Vec3 enemyToBotDir( self->s.origin );
		enemyToBotDir -= activeEnemies[i]->LastSeenOrigin();
		enemyToBotDir.Z() -= playerbox_stand_viewheight;
		enemyToBotDir.NormalizeFast();
		enemyViewDirDotToBotDir[i] = activeEnemies[i]->LookDir().Dot( enemyToBotDir );
	}

	enemyViewDirDotToBotDirComputedAt = level.time;
	return enemyViewDirDotToBotDir;
}

bool SelectedEnemies::TestAboutToHitEBorIG( int64_t levelTime ) const {
	trace_t trace;
	const auto *pvsCache = EntitiesPvsCache::Instance();
	const auto *const viewDots = GetEnemyViewDirDotToBotDirValues();
	for( int i = 0; i < activeEnemies.size(); ++i ) {
		const auto *const enemy = activeEnemies[i];
		if( !enemy->IsShootableCurrOrPendingWeapon( WEAP_ELECTROBOLT ) ) {
			if( !enemy->IsShootableCurrOrPendingWeapon( WEAP_INSTAGUN ) ) {
				continue;
			}
		}

		// We can dodge at the last movement, so wait until there is 1/3 of a second to make a shot
		if( enemy->FireDelay() > 333 ) {
			continue;
		}

		// Just check and trust but do not force computations
		if( canEnemyHitComputedAt[i] == levelTime && canEnemyHit[i] ) {
			return true;
		}

		// Is not going to put crosshair right now
		// TODO: Check past view dots and derive direction?
		if( viewDots[i] < 0.7f ) {
			continue;
		}

		if( !pvsCache->AreInPvs( self, enemy->ent ) ) {
			continue;
		}

		Vec3 traceStart( enemy->LastSeenOrigin() );
		traceStart.Z() += playerbox_stand_viewheight;
		SolidWorldTrace( &trace, traceStart.Data(), self->s.origin );
		if( trace.fraction == 1.0f ) {
			return true;
		}
	}

	return false;
}

bool SelectedEnemies::TestAboutToHitLGorPG( int64_t levelTime ) const {
	trace_t trace;
	const auto *pvsCache = EntitiesPvsCache::Instance();
	const auto *const viewDots = GetEnemyViewDirDotToBotDirValues();
	constexpr float squareDistanceThreshold = WorldState::MIDDLE_RANGE_MAX * WorldState::MIDDLE_RANGE_MAX;
	for( int i = 0; i < activeEnemies.size(); ++i ) {
		const auto *const enemy = activeEnemies[i];
		// Skip enemies that are out of LG range. (Consider PG to be inefficient outside of this range too)
		if( enemy->LastSeenOrigin().SquareDistanceTo( self->s.origin ) > squareDistanceThreshold ) {
			continue;
		}

		if( !enemy->IsShootableCurrOrPendingWeapon( WEAP_LASERGUN ) ) {
			if( !enemy->IsShootableCurrOrPendingWeapon( WEAP_PLASMAGUN ) ) {
				continue;
			}
		}

		// We can start dodging at the last moment, are not going to be hit hard
		if( enemy->FireDelay() > 333 ) {
			continue;
		}

		// Just check and trust but do not force computations
		if( canEnemyHitComputedAt[i] == levelTime && canEnemyHit[i] ) {
			return true;
		}

		// Is not going to put crosshair right now
		if( viewDots[i] < 0.7f ) {
			continue;
		}

		// TODO: Check past view dots and derive direction?

		if( !pvsCache->AreInPvs( self, enemy->ent ) ) {
			continue;
		}

		Vec3 traceStart( enemy->LastSeenOrigin() );
		traceStart.Z() += playerbox_stand_viewheight;
		SolidWorldTrace( &trace, traceStart.Data(), self->s.origin );
		if( trace.fraction == 1.0f ) {
			return true;
		}
	}
	return false;
}

bool SelectedEnemies::TestAboutToHitRLorSW( int64_t levelTime ) const {
	trace_t trace;
	const auto *pvsCache = EntitiesPvsCache::Instance();
	const auto *const viewDots = GetEnemyViewDirDotToBotDirValues();
	for( int i = 0; i < activeEnemies.size(); ++i ) {
		const auto *const enemy = activeEnemies[i];

		float distanceThreshold = 512.0f;
		// Ideally should check the bot environment too
		const float deltaZ = enemy->LastSeenOrigin().Z() - self->s.origin[2];
		if( deltaZ > 16.0f ) {
			distanceThreshold += 2.0f * BoundedFraction( deltaZ, 128.0f );
		} else if( deltaZ < -16.0f ) {
			distanceThreshold -= BoundedFraction( deltaZ, 128.0f );
		}

		const float squareDistance = enemy->LastSeenOrigin().SquareDistanceTo( self->s.origin );
		if( squareDistance > distanceThreshold * distanceThreshold ) {
			continue;
		}

		if( !enemy->IsShootableCurrOrPendingWeapon( WEAP_ROCKETLAUNCHER ) ) {
			if( !enemy->IsShootableCurrOrPendingWeapon( WEAP_SHOCKWAVE ) ) {
				continue;
			}
		}

		const float distance = SQRTFAST( squareDistance );
		const float distanceFraction = BoundedFraction( distance, distanceThreshold );
		// Do not wait for an actual shot on a short distance.
		// Its impossible to dodge on a short distance due to damage splash.
		// If the distance is close to zero 750 millis of reloading left must be used for making a dodge.
		if( enemy->FireDelay() > 750 - ( ( 750 - 333 ) * distanceFraction ) ) {
			continue;
		}

		// Just check and trust but do not force computations
		if( canEnemyHitComputedAt[i] == levelTime && canEnemyHit[i] ) {
			return true;
		}

		// Is not going to put crosshair right now
		if( viewDots[i] < 0.3f + 0.4f * distanceFraction ) {
			continue;
		}

		if( !pvsCache->AreInPvs( self, enemy->ent ) ) {
			continue;
		}

		// TODO: Check view dot and derive direction?
		Vec3 enemyViewOrigin( enemy->LastSeenOrigin() );
		enemyViewOrigin.Z() += playerbox_stand_viewheight;
		SolidWorldTrace( &trace, enemyViewOrigin.Data(), self->s.origin );
		if( trace.fraction == 1.0f ) {
			return true;
		}

		// A coarse environment test, check whether there are hittable environment elements
		// around the bot that are visible for the enemy
		for( int x = -1; x <= 1; x += 2 ) {
			for( int y = -1; y <= 1; y += 2 ) {
				Vec3 sidePoint( self->s.origin );
				sidePoint.X() += 64.0f * x;
				sidePoint.Y() += 64.0f * y;
				SolidWorldTrace( &trace, self->s.origin, sidePoint.Data() );
				if( trace.fraction == 1.0f || ( trace.surfFlags & SURF_NOIMPACT ) ) {
					continue;
				}
				const Vec3 oldImpact( trace.endpos );
				// Notice the order: we trace a ray from enemy to impact point to avoid having to offset start point
				SolidWorldTrace( &trace, enemyViewOrigin.Data(), oldImpact.Data() );
				if( trace.fraction == 1.0f || oldImpact.SquareDistanceTo( trace.endpos ) < 8 * 8 ) {
					return true;
				}
			}
		}
	}

	return false;
}