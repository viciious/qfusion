#include "AwarenessModule.h"
#include "../teamplay/SquadBasedTeam.h"
#include "../bot.h"

BotAwarenessModule::BotAwarenessModule( edict_t *self_, Bot *bot_, float skill_ )
	: self( self_ )
	, selectedEnemies( bot_->selectedEnemies )
	, lostEnemies( bot_->lostEnemies )
	, targetChoicePeriod( (unsigned)( 1500 - 500 * skill_ ) )
	, reactionTime( 320u - (unsigned)( 300 * skill_ ) )
	, hazardsDetector( self_ )
	, hazardsSelector( self_ )
	, eventsTracker( self_ )
	, ownEnemiesTracker( self_, this, skill_ ) {}

void BotAwarenessModule::OnAttachedToSquad( AiSquad *squad_ ) {
	this->squad = squad_;
	this->activeEnemiesTracker = squad_->EnemiesTracker();
}

void BotAwarenessModule::OnDetachedFromSquad( AiSquad *squad_ ) {
	if( squad_ != this->squad ) {
		if( this->squad ) {
			FailWith( "OnDetachedFromSquad(%p): Was attached to squad %p\n", squad_, this->squad );
		} else {
			FailWith( "OnDetachedFromSquad(%p): Was not attached to a squad\n", squad_ );
		}
	}
	this->squad = nullptr;
	this->activeEnemiesTracker = &ownEnemiesTracker;
}

void BotAwarenessModule::OnEnemyViewed( const edict_t *enemy ) {
	ownEnemiesTracker.OnEnemyViewed( enemy );
	if( squad ) {
		squad->OnBotViewedEnemy( self, enemy );
	}
}

void BotAwarenessModule::OnEnemyOriginGuessed( const edict_t *enemy, unsigned minMillisSinceLastSeen, const float *guessedOrigin ) {
	ownEnemiesTracker.OnEnemyOriginGuessed( enemy, minMillisSinceLastSeen, guessedOrigin );
	if( squad ) {
		squad->OnBotGuessedEnemyOrigin( self, enemy, minMillisSinceLastSeen, guessedOrigin );
	}
}

void BotAwarenessModule::OnPain( const edict_t *enemy, float kick, int damage ) {
	ownEnemiesTracker.OnPain( self, enemy, kick, damage );
	if( squad ) {
		squad->OnBotPain( self, enemy, kick, damage );
	}
}

void BotAwarenessModule::OnEnemyDamaged( const edict_t *target, int damage ) {
	ownEnemiesTracker.OnEnemyDamaged( self, target, damage );
	if( squad ) {
		squad->OnBotDamagedEnemy( self, target, damage );
	}
}

const TrackedEnemy *BotAwarenessModule::ChooseLostOrHiddenEnemy( unsigned timeout ) {
	return activeEnemiesTracker->ChooseLostOrHiddenEnemy( self, timeout );
}

void BotAwarenessModule::Frame() {
	AiFrameAwareUpdatable::Frame();

	ownEnemiesTracker.Update();
	eventsTracker.Update();
}

void BotAwarenessModule::Think() {
	AiFrameAwareUpdatable::Think();

	RegisterVisibleEnemies();
	CheckForNewHazards();

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

	TryTriggerPlanningForNewHazard();
}

void BotAwarenessModule::UpdateSelectedEnemies() {
	selectedEnemies.Invalidate();
	lostEnemies.Invalidate();

	float visibleEnemyWeight = 0.0f;

	if( const TrackedEnemy *visibleEnemy = activeEnemiesTracker->ChooseVisibleEnemy( self ) ) {
		// A compiler prefers a non-const version here, and therefore fails on non-const version of method being private
		const auto *activeEnemiesHead = ( (const AiEnemiesTracker *)activeEnemiesTracker )->ActiveEnemiesHead();
		selectedEnemies.Set( visibleEnemy, targetChoicePeriod, activeEnemiesHead );
		visibleEnemyWeight = 0.5f * ( visibleEnemy->AvgWeight() + visibleEnemy->MaxWeight() );
	}

	if( const TrackedEnemy *lostEnemy = activeEnemiesTracker->ChooseLostOrHiddenEnemy( self ) ) {
		float lostEnemyWeight = 0.5f * ( lostEnemy->AvgWeight() + lostEnemy->MaxWeight() );
		// If there is a lost or hidden enemy of higher weight, store it
		if( lostEnemyWeight > visibleEnemyWeight ) {
			// Provide a pair of iterators to the Set call:
			// lostEnemies.activeEnemies must contain the lostEnemy.
			const TrackedEnemy *enemies[] = { lostEnemy };
			lostEnemies.Set( lostEnemy, targetChoicePeriod, enemies, enemies + 1 );
		}
	}
}

bool BotAwarenessModule::HurtEvent::IsValidFor( const edict_t *self_ ) const {
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

void BotAwarenessModule::TryTriggerPlanningForNewHazard() {
	if( self->ai->botRef->Skill() <= 0.33f ) {
		return;
	}

	const Hazard *hazard = hazardsSelector.PrimaryHazard();
	if( !hazard ) {
		return;
	}

	// Trying to do urgent replanning based on more sophisticated formulae was a bad idea.
	// The bot has inertia and cannot change dodge direction so fast,
	// and it just lead to no actual dodging performed since the actual mean dodge vector is about zero.

	if( !triggeredPlanningHazard.IsValid() ) {
		triggeredPlanningHazard = *hazard;
		self->ai->botRef->ForcePlanBuilding();
	}
}

void BotAwarenessModule::OnHurtByNewThreat( const edict_t *newThreat, const AiFrameAwareUpdatable *threatDetector ) {
	// Reject threats detected by bot brain if there is active squad.
	// Otherwise there may be two calls for a single or different threats
	// detected by squad and the bot brain enemy pool itself.
	if( self->ai->botRef->IsInSquad() && threatDetector == &this->ownEnemiesTracker ) {
		return;
	}

	bool hadValidThreat = hurtEvent.IsValidFor( self );
	float totalInflictedDamage = activeEnemiesTracker->TotalDamageInflictedBy( newThreat );
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

void BotAwarenessModule::OnEnemyRemoved( const TrackedEnemy *enemy ) {
	if( !selectedEnemies.AreValid() ) {
		return;
	}
	if( !selectedEnemies.Contain( enemy ) ) {
		return;
	}
	selectedEnemies.Invalidate();
	self->ai->botRef->ForcePlanBuilding();
}

void BotAwarenessModule::UpdateBlockedAreasStatus() {
	// Disabled at this moment as the "old"-style blocking that does a raycast for every area in path is used.
	// Refer to the git history of "bot_brain.cpp" for the removed code.
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

void BotAwarenessModule::RegisterVisibleEnemies() {
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
	VisCheckRawEnts( candidateTargets, visibleTargets, self, MAX_CLIENTS, IsGenericEntityInPvs, IsEnemyVisible );

	for( auto entNum: visibleTargets ) {
		OnEnemyViewed( gameEdicts + entNum );
	}

	self->ai->botRef->CheckAlertSpots( visibleTargets );
}

void BotAwarenessModule::CheckForNewHazards() {
	// This call returns a value if the primary hazard is valid
	if( PrimaryHazard() != nullptr ) {
		return;
	}

	eventsTracker.ResetTeammatesVisData();
	hazardsSelector.BeginUpdate();

	hazardsDetector.Exec();

	EntNumsVector *v;

	if( !( v = &hazardsDetector.dangerousRockets )->empty() ) {
		hazardsSelector.FindProjectileHazards( *v );
		eventsTracker.TryGuessingProjectileOwnersOrigins( *v );
	}

	eventsTracker.TryGuessingProjectileOwnersOrigins( hazardsDetector.visibleOtherRockets );

	if( !( v = &hazardsDetector.dangerousWaves )->empty() ) {
		hazardsSelector.FindWaveHazards( *v );
		eventsTracker.TryGuessingProjectileOwnersOrigins( *v );
	}

	eventsTracker.TryGuessingProjectileOwnersOrigins( hazardsDetector.visibleOtherWaves );

	if( !( v = &hazardsDetector.dangerousBlasts )->empty() ) {
		hazardsSelector.FindProjectileHazards( *v );
		eventsTracker.TryGuessingProjectileOwnersOrigins( *v );
	}

	eventsTracker.TryGuessingProjectileOwnersOrigins( hazardsDetector.visibleOtherBlasts );

	if( !( v = &hazardsDetector.dangerousGrenades )->empty() ) {
		hazardsSelector.FindProjectileHazards( *v );
		eventsTracker.TryGuessingProjectileOwnersOrigins( *v );
	}

	eventsTracker.TryGuessingProjectileOwnersOrigins( hazardsDetector.visibleOtherGrenades );

	// The detection is quite expensive, allow intentional failing at it (and thus rejecting quickly)
	constexpr float pgDetectionFailureChance = 0.75f;
	if( !( v = &hazardsDetector.dangerousPlasmas )->empty() ) {
		hazardsSelector.FindPlasmaHazards( *v );
		eventsTracker.TryGuessingProjectileOwnersOrigins( *v, pgDetectionFailureChance );
	}

	eventsTracker.TryGuessingProjectileOwnersOrigins( hazardsDetector.visibleOtherPlasmas, pgDetectionFailureChance );

	if( !( v = &hazardsDetector.dangerousLasers )->empty() ) {
		hazardsSelector.FindLaserHazards( *v );
		eventsTracker.TryGuessingBeamOwnersOrigins( *v );
	}

	eventsTracker.TryGuessingProjectileOwnersOrigins( hazardsDetector.visibleOtherLasers );

	hazardsSelector.EndUpdate();
}