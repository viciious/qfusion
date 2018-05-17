#include "ThreatTracker.h"
#include "../teamplay/SquadBasedTeam.h"
#include "../bot.h"

BotThreatTracker::BotThreatTracker( edict_t *self_, Bot *bot_, float skill_ )
	: activeEnemyPool( &ownEnemyPool )
	, squad( nullptr )
	, self( self_ )
	, selectedEnemies( bot_->selectedEnemies )
	, lostEnemies( bot_->lostEnemies )
	, targetChoicePeriod( (unsigned)( 1500 - 500 * skill_ ) )
	, reactionTime( 320u - (unsigned)( 300 * skill_ ) )
	, ownEnemyPool( self_, this, skill_ )
	, selectedHazard( nullptr )
	, triggeredPlanningHazard( nullptr ) {}

void BotThreatTracker::OnAttachedToSquad( AiSquad *squad_ ) {
	this->squad = squad_;
	this->activeEnemyPool = squad_->EnemyPool();
}

void BotThreatTracker::OnDetachedFromSquad( AiSquad *squad_ ) {
	if( squad_ != this->squad ) {
		if( this->squad ) {
			FailWith( "OnDetachedFromSquad(%p): Was attached to squad %p\n", squad_, this->squad );
		} else {
			FailWith( "OnDetachedFromSquad(%p): Was not attached to a squad\n", squad_ );
		}
	}
	this->squad = nullptr;
	this->activeEnemyPool = &ownEnemyPool;
}

void BotThreatTracker::OnEnemyViewed( const edict_t *enemy ) {
	ownEnemyPool.OnEnemyViewed( enemy );
	if( squad ) {
		squad->OnBotViewedEnemy( self, enemy );
	}
}

void BotThreatTracker::OnEnemyOriginGuessed( const edict_t *enemy, unsigned minMillisSinceLastSeen, const float *guessedOrigin ) {
	ownEnemyPool.OnEnemyOriginGuessed( enemy, minMillisSinceLastSeen, guessedOrigin );
	if( squad ) {
		squad->OnBotGuessedEnemyOrigin( self, enemy, minMillisSinceLastSeen, guessedOrigin );
	}
}

void BotThreatTracker::OnPain( const edict_t *enemy, float kick, int damage ) {
	ownEnemyPool.OnPain( self, enemy, kick, damage );
	if( squad ) {
		squad->OnBotPain( self, enemy, kick, damage );
	}
}

void BotThreatTracker::OnEnemyDamaged( const edict_t *target, int damage ) {
	ownEnemyPool.OnEnemyDamaged( self, target, damage );
	if( squad ) {
		squad->OnBotDamagedEnemy( self, target, damage );
	}
}

const Enemy *BotThreatTracker::ChooseLostOrHiddenEnemy( unsigned timeout ) {
	return activeEnemyPool->ChooseLostOrHiddenEnemy( self, timeout );
}

void BotThreatTracker::Frame() {
	AiFrameAwareUpdatable::Frame();

	ownEnemyPool.Update();
}

void BotThreatTracker::Think() {
	AiFrameAwareUpdatable::Think();

	if( selectedEnemies.AreValid() ) {
		if( level.time - selectedEnemies.LastSeenAt() > std::min( 64u, reactionTime ) ) {
			selectedEnemies.Invalidate();
			UpdateSelectedEnemies();
			UpdateBlockedAreasStatus();
		}
	} else {
		UpdateSelectedEnemies();
		UpdateBlockedAreasStatus();
	}

	CheckNewActiveHazard();
}

void BotThreatTracker::UpdateSelectedEnemies() {
	selectedEnemies.Invalidate();
	lostEnemies.Invalidate();

	float visibleEnemyWeight = 0.0f;

	if( const Enemy *visibleEnemy = activeEnemyPool->ChooseVisibleEnemy( self ) ) {
		// A compiler prefers a non-const version here, and therefore fails on non-const version of method being private
		const auto *activeEnemiesHead = ( (const AiBaseEnemyPool *)activeEnemyPool )->ActiveEnemiesHead();
		selectedEnemies.Set( visibleEnemy, targetChoicePeriod, activeEnemiesHead );
		visibleEnemyWeight = 0.5f * ( visibleEnemy->AvgWeight() + visibleEnemy->MaxWeight() );
	}

	if( const Enemy *lostEnemy = activeEnemyPool->ChooseLostOrHiddenEnemy( self ) ) {
		float lostEnemyWeight = 0.5f * ( lostEnemy->AvgWeight() + lostEnemy->MaxWeight() );
		// If there is a lost or hidden enemy of higher weight, store it
		if( lostEnemyWeight > visibleEnemyWeight ) {
			// Provide a pair of iterators to the Set call:
			// lostEnemies.activeEnemies must contain the lostEnemy.
			const Enemy *enemies[] = { lostEnemy };
			lostEnemies.Set( lostEnemy, targetChoicePeriod, enemies, enemies + 1 );
		}
	}
}

bool BotThreatTracker::HurtEvent::IsValidFor( const edict_t *self_ ) const {
	if( level.time - lastHitTimestamp > 350 ) {
		return false;
	}

	// Check whether the inflictor entity is no longer valid

	if( !inflictor->r.inuse ) {
		return false;
	}

	if( !inflictor->r.client && inflictor->aiIntrinsicEnemyWeight <= 0 ) {
		return false;
	}

	if( G_ISGHOSTING( inflictor ) ) {
		return false;
	}

	// It is not cheap to call so do it after all other tests have passed
	vec3_t lookDir;
	AngleVectors( self_->s.angles, lookDir, nullptr, nullptr );
	Vec3 toThreat( inflictor->s.origin );
	toThreat -= self_->s.origin;
	toThreat.NormalizeFast();
	return toThreat.Dot( lookDir ) < self_->ai->botRef->FovDotFactor();
}

void BotThreatTracker::CheckNewActiveHazard() {
	if( self->ai->botRef->Skill() <= 0.33f ) {
		return;
	}

	const Danger *hazard = self->ai->botRef->perceptionManager.PrimaryDanger();
	if( !hazard ) {
		return;
	}

	// Trying to do urgent replanning based on more sophisticated formulae was a bad idea.
	// The bot has inertia and cannot change dodge direction so fast,
	// and it just lead to no actual dodging performed since the actual mean dodge vector is about zero.

	selectedHazard = *hazard;
	if( !triggeredPlanningHazard.IsValid() ) {
		triggeredPlanningHazard = selectedHazard;
		self->ai->botRef->ForcePlanBuilding();
	}
}

void BotThreatTracker::OnHurtByNewThreat( const edict_t *newThreat, const AiFrameAwareUpdatable *threatDetector ) {
	// Reject threats detected by bot brain if there is active squad.
	// Otherwise there may be two calls for a single or different threats
	// detected by squad and the bot brain enemy pool itself.
	if( self->ai->botRef->IsInSquad() && threatDetector == &this->ownEnemyPool ) {
		return;
	}

	bool hadValidThreat = hurtEvent.IsValidFor( self );
	float totalInflictedDamage = activeEnemyPool->TotalDamageInflictedBy( newThreat );
	if( hadValidThreat ) {
		// The active threat is more dangerous than a new one
		if( hurtEvent.totalDamage > totalInflictedDamage ) {
			return;
		}
		// The active threat has the same inflictor
		if( hurtEvent.inflictor == newThreat ) {
			// Just update the existing threat
			hurtEvent.totalDamage = totalInflictedDamage;
			hurtEvent.lastHitTimestamp = level.time;
			return;
		}
	}

	vec3_t botLookDir;
	AngleVectors( self->s.angles, botLookDir, nullptr, nullptr );
	Vec3 toEnemyDir = Vec3( newThreat->s.origin ) - self->s.origin;
	float squareDistance = toEnemyDir.SquaredLength();
	if( squareDistance < 1 ) {
		return;
	}

	float distance = 1.0f / Q_RSqrt( squareDistance );
	toEnemyDir *= 1.0f / distance;
	if( toEnemyDir.Dot( botLookDir ) >= 0 ) {
		return;
	}

	// Try to guess enemy origin
	toEnemyDir.X() += -0.25f + 0.50f * random();
	toEnemyDir.Y() += -0.10f + 0.20f * random();
	toEnemyDir.NormalizeFast();
	hurtEvent.inflictor = newThreat;
	hurtEvent.lastHitTimestamp = level.time;
	hurtEvent.possibleOrigin = distance * toEnemyDir + self->s.origin;
	hurtEvent.totalDamage = totalInflictedDamage;
	// Force replanning on new threat
	if( !hadValidThreat ) {
		self->ai->botRef->ForcePlanBuilding();
	}
}

void BotThreatTracker::OnEnemyRemoved( const Enemy *enemy ) {
	if( !selectedEnemies.AreValid() ) {
		return;
	}
	if( !selectedEnemies.Contain( enemy ) ) {
		return;
	}
	selectedEnemies.Invalidate();
	self->ai->botRef->ForcePlanBuilding();
}

void BotThreatTracker::UpdateBlockedAreasStatus() {
	// Disabled at this moment as the "old"-style blocking that does a raycast for every area in path is used.
	// Refer to the git history of "bot_brain.cpp" for the removed code.
}