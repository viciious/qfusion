#include "MovementPredictionContext.h"
#include "MovementLocal.h"

bool MovementPredictionContext::CanSafelyKeepHighSpeed() {
	if( const bool *cachedValue = canSafelyKeepHighSpeedCachesStack.GetCached() ) {
		return *cachedValue;
	}

	bool result = module->TestWhetherCanSafelyKeepHighSpeed( this );
	canSafelyKeepHighSpeedCachesStack.SetCachedValue( result );
	return result;
}

void MovementPredictionContext::NextReachNumAndTravelTimeToNavTarget( int *reachNum, int *travelTimeToNavTarget ) {
	*reachNum = 0;
	*travelTimeToNavTarget = 0;

	// Do NOT use cached reachability chain for the frame (if any).
	// It might be invalid after movement step, and the route cache does caching itself pretty well.

	const int navTargetAreaNum = NavTargetAasAreaNum();
	if( !navTargetAreaNum ) {
		return;
	}

	const auto *routeCache = bot->RouteCache();

	int fromAreaNums[2];
	int numFromAreas = movementState->entityPhysicsState.PrepareRoutingStartAreas( fromAreaNums );
	if( int travelTime = routeCache->PreferredRouteToGoalArea( fromAreaNums, numFromAreas, navTargetAreaNum, reachNum ) ) {
		*travelTimeToNavTarget = travelTime;
	}
}

BaseMovementAction *MovementPredictionContext::GetCachedActionAndRecordForCurrTime( MovementActionRecord *record_ ) {
	const int64_t levelTime = level.time;
	PredictedMovementAction *prevPredictedAction = nullptr;
	PredictedMovementAction *nextPredictedAction = nullptr;
	for( PredictedMovementAction &predictedAction: predictedMovementActions ) {
		if( predictedAction.timestamp >= levelTime ) {
			nextPredictedAction = &predictedAction;
			break;
		}
		prevPredictedAction = &predictedAction;
	}

	if( !nextPredictedAction ) {
		Debug( "Cannot use predicted movement action: next one (its timestamp is not in the past) cannot be found\n" );
		return nullptr;
	}

	const auto *self = game.edicts + bot->EntNum();

	if( !prevPredictedAction ) {
		// If there were no activated actions, the next state must be recently computed for current level time.
		Assert( nextPredictedAction->timestamp == levelTime );
		// These assertions have already spotted a bug
		Assert( VectorCompare( nextPredictedAction->entityPhysicsState.Origin(), self->s.origin ) );
		Assert( VectorCompare( nextPredictedAction->entityPhysicsState.Velocity(), self->velocity ) );
		// If there is a modified velocity, it will be copied with this record and then applied
		*record_ = nextPredictedAction->record;
		Debug( "Using just computed predicted movement action %s\n", nextPredictedAction->action->Name() );
		return nextPredictedAction->action;
	}

	Assert( prevPredictedAction->timestamp + prevPredictedAction->stepMillis == nextPredictedAction->timestamp );

	// Fail prediction if both previous and next predicted movement states mask mismatch the current movement state
	const auto &actualMovementState = module->movementState;
	if( !actualMovementState.TestActualStatesForExpectedMask( prevPredictedAction->movementStatesMask, bot ) ) {
		if( !actualMovementState.TestActualStatesForExpectedMask( nextPredictedAction->movementStatesMask, bot ) ) {
			return nullptr;
		}
	}

	// Check whether predicted action is valid for an actual bot entity physics state
	float stateLerpFrac = (float)( levelTime - prevPredictedAction->timestamp );
	stateLerpFrac *= 1.0f / ( nextPredictedAction->timestamp - prevPredictedAction->timestamp );
	Assert( stateLerpFrac > 0 && stateLerpFrac <= 1.0f );
	const char *format =
		"Prev predicted action timestamp is " PRId64 ", next predicted action is " PRId64 ", level.time is %" PRId64 "\n";
	Debug( format, prevPredictedAction->timestamp, nextPredictedAction->timestamp, level.time );
	Debug( "Should interpolate entity physics state using fraction %f\n", stateLerpFrac );

	const auto &prevPhysicsState = prevPredictedAction->entityPhysicsState;
	const auto &nextPhysicsState = nextPredictedAction->entityPhysicsState;

	// Prevent cache invalidation on each frame if bot is being hit by a continuous fire weapon and knocked back.
	// Perform misprediction test only on the 3rd frame after a last knockback timestamp.
	if( level.time - bot->lastKnockbackAt > 32 ) {
		vec3_t expectedOrigin;
		VectorLerp( prevPhysicsState.Origin(), stateLerpFrac, nextPhysicsState.Origin(), expectedOrigin );
		float squaredDistanceMismatch = DistanceSquared( self->s.origin, expectedOrigin );
		if( squaredDistanceMismatch > 3.0f * 3.0f ) {
			float distanceMismatch = SQRTFAST( squaredDistanceMismatch );
			const char *format_ = "Cannot use predicted movement action: distance mismatch %f is too high for lerp frac %f\n";
			Debug( format_, distanceMismatch, stateLerpFrac );
			return nullptr;
		}

		float expectedSpeed = ( 1.0f - stateLerpFrac ) * prevPhysicsState.Speed() + stateLerpFrac * nextPhysicsState.Speed();
		float actualSpeed = bot->EntityPhysicsState()->Speed();
		float speedMismatch = fabsf( actualSpeed - expectedSpeed );
		if( speedMismatch > 0.005f * expectedSpeed ) {
			Debug( "Expected speed: %.1f, actual speed: %.1f, speed mismatch: %.1f\n", expectedSpeed, actualSpeed,
				   speedMismatch );
			Debug( "Cannot use predicted movement action: speed mismatch is too high\n" );
			return nullptr;
		}

		if( actualSpeed > 30.0f ) {
			vec3_t expectedVelocity;
			VectorLerp( prevPhysicsState.Velocity(), stateLerpFrac, nextPhysicsState.Velocity(), expectedVelocity );
			Vec3 expectedVelocityDir( expectedVelocity );
			expectedVelocityDir *= 1.0f / expectedSpeed;
			Vec3 actualVelocityDir( self->velocity );
			actualVelocityDir *= 1.0f / actualSpeed;
			float cosine = expectedVelocityDir.Dot( actualVelocityDir );
			static const float MIN_COSINE = cosf( (float) DEG2RAD( 5.0f ) );
			if( cosine < MIN_COSINE ) {
				Debug( "An angle between expected and actual velocities is %f degrees\n",(float) RAD2DEG( acosf( cosine ) ) );
				Debug( "Cannot use predicted movement action:  expected and actual velocity directions differ significantly\n" );
				return nullptr;
			}
		}

		if( !nextPredictedAction->record.botInput.canOverrideLookVec ) {
			Vec3 prevStateAngles( prevPhysicsState.Angles() );
			Vec3 nextStateAngles( nextPhysicsState.Angles() );

			vec3_t expectedAngles;
			for( int i : { YAW, ROLL } )
				expectedAngles[i] = LerpAngle( prevStateAngles.Data()[i], nextStateAngles.Data()[i], stateLerpFrac );

			if( !nextPredictedAction->record.botInput.canOverridePitch ) {
				expectedAngles[PITCH] = LerpAngle( prevStateAngles.Data()[PITCH], nextStateAngles.Data()[PITCH],
												   stateLerpFrac );
			} else {
				expectedAngles[PITCH] = self->s.angles[PITCH];
			}

			vec3_t expectedLookDir;
			AngleVectors( expectedAngles, expectedLookDir, nullptr, nullptr );
			float cosine = bot->EntityPhysicsState()->ForwardDir().Dot( expectedLookDir );
			static const float MIN_COSINE = cosf( (float) DEG2RAD( 5.0f ) );
			if( cosine < MIN_COSINE ) {
				Debug( "An angle between and actual look directions is %f degrees\n", (float) RAD2DEG( acosf( cosine ) ) );
				Debug( "Cannot use predicted movement action: expected and actual look directions differ significantly\n" );
				return nullptr;
			}
		}
	}

	// If next predicted state is likely to be completed next frame, use its input as-is (except the velocity)
	if( nextPredictedAction->timestamp - levelTime <= game.frametime ) {
		*record_ = nextPredictedAction->record;
		// Apply modified velocity only once for an exact timestamp
		if( nextPredictedAction->timestamp != levelTime ) {
			record_->hasModifiedVelocity = false;
		}
		return nextPredictedAction->action;
	}

	float inputLerpFrac = game.frametime / ( (float)( nextPredictedAction->timestamp - levelTime ) );
	Assert( inputLerpFrac > 0 && inputLerpFrac <= 1.0f );
	// If next predicted time is likely to be pending next frame again, interpolate input for a single frame ahead
	*record_ = nextPredictedAction->record;
	// Prevent applying a modified velocity from the new state
	record_->hasModifiedVelocity = false;
	if( !record_->botInput.canOverrideLookVec ) {
		Vec3 actualLookDir( bot->EntityPhysicsState()->ForwardDir() );
		Vec3 intendedLookVec( record_->botInput.IntendedLookDir() );
		VectorLerp( actualLookDir.Data(), inputLerpFrac, intendedLookVec.Data(), intendedLookVec.Data() );
		record_->botInput.SetIntendedLookDir( intendedLookVec );
	}

	auto prevRotationMask = (unsigned)prevPredictedAction->record.botInput.allowedRotationMask;
	auto nextRotationMask = (unsigned)nextPredictedAction->record.botInput.allowedRotationMask;
	record_->botInput.allowedRotationMask = (BotInputRotation)( prevRotationMask & nextRotationMask );

	return nextPredictedAction->action;
}

BaseMovementAction *MovementPredictionContext::GetActionAndRecordForCurrTime( MovementActionRecord *record_ ) {
	auto *action = GetCachedActionAndRecordForCurrTime( record_ );
	if( !action ) {
		BuildPlan();
		action = GetCachedActionAndRecordForCurrTime( record_ );
	}

	//AITools_DrawColorLine(self->s.origin, (Vec3(0, 0, 48) + self->s.origin).Data(), action->DebugColor(), 0);
	return action;
}

void MovementPredictionContext::ShowBuiltPlanPath() const {
	for( unsigned i = 0, j = 1; j < predictedMovementActions.size(); ++i, ++j ) {
		int color;
		switch( i % 3 ) {
			case 0: color = COLOR_RGB( 192, 0, 0 ); break;
			case 1: color = COLOR_RGB( 0, 192, 0 ); break;
			case 2: color = COLOR_RGB( 0, 0, 192 ); break;
		}
		const float *v1 = predictedMovementActions[i].entityPhysicsState.Origin();
		const float *v2 = predictedMovementActions[j].entityPhysicsState.Origin();
		AITools_DrawColorLine( v1, v2, color, 0 );
	}
}

const Ai::ReachChainVector &MovementPredictionContext::NextReachChain() {
	if( const auto *cachedReachChain = reachChainsCachesStack.GetCached() ) {
		return *cachedReachChain;
	}

	Ai::ReachChainVector dummy;
	const Ai::ReachChainVector *oldReachChain = &dummy;
	if( const auto *cachedOldReachChain = reachChainsCachesStack.GetCachedValueBelowTopOfStack() ) {
		oldReachChain = cachedOldReachChain;
	}

	auto *newReachChain = new( reachChainsCachesStack.GetUnsafeBufferForCachedValue() ) Ai::ReachChainVector;
	bot->UpdateReachChain( *oldReachChain, newReachChain, movementState->entityPhysicsState );
	return *newReachChain;
};

static void Intercepted_PredictedEvent( int entNum, int ev, int parm ) {
	game.edicts[entNum].ai->botRef->OnInterceptedPredictedEvent( ev, parm );
}

static void Intercepted_PMoveTouchTriggers( pmove_t *pm, vec3_t previous_origin ) {
	game.edicts[pm->playerState->playerNum + 1].ai->botRef->OnInterceptedPMoveTouchTriggers( pm, previous_origin );
}

static void Intercepted_Trace( trace_t *t, vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end,
							   int ignore, int contentmask, int timeDelta ) {
	trap_CM_TransformedBoxTrace( t, start, end, mins, maxs, nullptr, contentmask, nullptr, nullptr );
}

static int Intercepted_PointContents( vec3_t p, int timeDelta ) {
	return trap_CM_TransformedPointContents( p, nullptr, nullptr, nullptr );
}

void MovementPredictionContext::OnInterceptedPredictedEvent( int ev, int parm ) {
	switch( ev ) {
		case EV_JUMP:
			this->frameEvents.hasJumped = true;
			break;
		case EV_DASH:
			this->frameEvents.hasDashed = true;
			break;
		case EV_WALLJUMP:
			this->frameEvents.hasWalljumped = true;
			break;
		case EV_FALL:
			this->frameEvents.hasTakenFallDamage = true;
			break;
		default: // Shut up an analyzer
			break;
	}
}

void MovementPredictionContext::OnInterceptedPMoveTouchTriggers( pmove_t *pm, vec3_t const previousOrigin ) {
	edict_t *ent = game.edicts + pm->playerState->POVnum;
	// update the entity with the new position
	VectorCopy( pm->playerState->pmove.origin, ent->s.origin );
	VectorCopy( pm->playerState->pmove.velocity, ent->velocity );
	VectorCopy( pm->playerState->viewangles, ent->s.angles );
	ent->viewheight = (int)pm->playerState->viewheight;
	VectorCopy( pm->mins, ent->r.mins );
	VectorCopy( pm->maxs, ent->r.maxs );

	ent->waterlevel = pm->waterlevel;
	ent->watertype = pm->watertype;
	if( pm->groundentity == -1 ) {
		ent->groundentity = NULL;
	} else {
		ent->groundentity = &game.edicts[pm->groundentity];
		ent->groundentity_linkcount = ent->groundentity->linkcount;
	}

	GClip_LinkEntity( ent );

	// expand the search bounds to include the space between the previous and current origin
	vec3_t mins, maxs;
	for( int i = 0; i < 3; i++ ) {
		if( previousOrigin[i] < pm->playerState->pmove.origin[i] ) {
			mins[i] = previousOrigin[i] + pm->maxs[i];
			if( mins[i] > pm->playerState->pmove.origin[i] + pm->mins[i] ) {
				mins[i] = pm->playerState->pmove.origin[i] + pm->mins[i];
			}
			maxs[i] = pm->playerState->pmove.origin[i] + pm->maxs[i];
		} else {
			mins[i] = pm->playerState->pmove.origin[i] + pm->mins[i];
			maxs[i] = previousOrigin[i] + pm->mins[i];
			if( maxs[i] < pm->playerState->pmove.origin[i] + pm->maxs[i] ) {
				maxs[i] = pm->playerState->pmove.origin[i] + pm->maxs[i];
			}
		}
	}

	// Make a local copy of the reference for faster access (avoid accessing shared library relocation table).
	edict_t *const gameEdicts = game.edicts;

	nearbyTriggersCache.EnsureValidForBounds( mins, maxs );

	for( int i = 0; i < nearbyTriggersCache.numJumppadEnts; ++i ) {
		if( GClip_EntityContact( mins, maxs, gameEdicts + nearbyTriggersCache.jumppadEntNums[i] ) ) {
			frameEvents.hasTouchedJumppad = true;
			break;
		}
	}

	for( int i = 0; i < nearbyTriggersCache.numTeleportEnts; ++i ) {
		if( GClip_EntityContact( mins, maxs, gameEdicts + nearbyTriggersCache.teleportEntNums[i] ) ) {
			frameEvents.hasTouchedTeleporter = true;
			break;
		}
	}

	for( int i = 0; i < nearbyTriggersCache.numPlatformEnts; ++i ) {
		if( GClip_EntityContact( mins, maxs, gameEdicts + nearbyTriggersCache.platformEntNums[i] ) ) {
			frameEvents.hasTouchedPlatform = true;
			break;
		}
	}

	if ( nearbyTriggersCache.numOtherEnts <= FrameEvents::MAX_TOUCHED_OTHER_TRIGGERS ) {
		for( int i = 0; i < nearbyTriggersCache.numOtherEnts; ++i ) {
			uint16_t entNum = nearbyTriggersCache.otherEntNums[i];
			if( GClip_EntityContact( mins, maxs, gameEdicts + entNum ) ) {
				frameEvents.otherTouchedTriggerEnts[frameEvents.numOtherTouchedTriggers++] = entNum;
			}
		}
	} else {
		for( int i = 0; i < nearbyTriggersCache.numOtherEnts; ++i ) {
			uint16_t entNum = nearbyTriggersCache.otherEntNums[i];
			if( GClip_EntityContact( mins, maxs, gameEdicts + entNum ) ) {
				frameEvents.otherTouchedTriggerEnts[frameEvents.numOtherTouchedTriggers++] = entNum;
				if( frameEvents.numOtherTouchedTriggers == FrameEvents::MAX_TOUCHED_OTHER_TRIGGERS ) {
					break;
				}
			}
		}
	}
}

void MovementPredictionContext::NearbyTriggersCache::EnsureValidForBounds( const vec3_t absMins,
																			  const vec3_t absMaxs ) {
	int i = 0;
	for( i = 0; i < 3; ++i ) {
		if ( lastComputedForMins[i] + 192.0f > absMins[i] ) {
			break;
		}
		if ( lastComputedForMaxs[i] - 192.0f < absMaxs[i] ) {
			break;
		}
	}

	// If all coords have passed tests
	if ( i == 3 ) {
		return;
	}

	VectorSet( lastComputedForMins, -256, -256, -256 );
	VectorSet( lastComputedForMaxs, +256, +256, +256 );
	VectorAdd( absMins, lastComputedForMins, lastComputedForMins );
	VectorAdd( absMaxs, lastComputedForMaxs, lastComputedForMaxs );

	numTeleportEnts = numJumppadEnts = numPlatformEnts = numOtherEnts = 0;

	constexpr auto maxEnts = 3 * MAX_GROUP_ENTITIES + MAX_OTHER_ENTITIES;
	int entNums[maxEnts];
	const int numEnts = GClip_AreaEdicts( lastComputedForMins, lastComputedForMaxs, entNums, maxEnts, AREA_TRIGGERS, 0 );

	const edict_t *const gameEdicts = game.edicts;
	for( i = 0; i < numEnts; ++i ) {
		const edict_t *ent = gameEdicts + entNums[i];
		if( !ent->r.inuse ) {
			continue;
		}

		const char *classname = ent->classname;
		if( !classname ) {
			continue;
		}

		if( !Q_stricmp( "func_plat", classname ) ) {
			if( numPlatformEnts != MAX_GROUP_ENTITIES ) {
				platformEntNums[numPlatformEnts++] = (uint16_t)entNums[i];
			}
			continue;
		}

		if( !Q_stricmp( "trigger_push", classname ) ) {
			if( numJumppadEnts != MAX_GROUP_ENTITIES ) {
				jumppadEntNums[numJumppadEnts] = (uint16_t)entNums[i];
			}
			continue;
		}

		if( !Q_stricmp( "trigger_teleport", classname ) ) {
			if( numTeleportEnts != MAX_GROUP_ENTITIES ) {
				teleportEntNums[numTeleportEnts++] = (uint16_t)entNums[i];
			}
			continue;
		}

		if( numOtherEnts != MAX_OTHER_ENTITIES ) {
			otherEntNums[numOtherEnts++] = (uint16_t)entNums[i];
		}
	}
}

void MovementPredictionContext::SetupStackForStep() {
	PredictedMovementAction *topOfStack;
	if( topOfStackIndex > 0 ) {
		Assert( predictedMovementActions.size() );
		Assert( botMovementStatesStack.size() == predictedMovementActions.size() );
		Assert( playerStatesStack.size() == predictedMovementActions.size() );
		Assert( pendingWeaponsStack.size() == predictedMovementActions.size() );

		Assert( defaultBotInputsCachesStack.Size() == predictedMovementActions.size() );
		Assert( reachChainsCachesStack.Size() == predictedMovementActions.size() );
		Assert( mayHitWhileRunningCachesStack.Size() == predictedMovementActions.size() );
		Assert( canSafelyKeepHighSpeedCachesStack.Size() == predictedMovementActions.size() );
		Assert( environmentTestResultsStack.size() == predictedMovementActions.size() );

		// topOfStackIndex already points to a needed array element in case of rolling back
		const auto &belowTopOfStack = predictedMovementActions[topOfStackIndex - 1];
		// For case of rolling back to savepoint we have to truncate grew stacks to it
		// The only exception is rolling back to the same top of stack.
		if( this->shouldRollback ) {
			Assert( this->topOfStackIndex <= predictedMovementActions.size() );
			predictedMovementActions.truncate( topOfStackIndex );
			botMovementStatesStack.truncate( topOfStackIndex );
			playerStatesStack.truncate( topOfStackIndex );
			pendingWeaponsStack.truncate( topOfStackIndex );

			defaultBotInputsCachesStack.PopToSize( topOfStackIndex );
			reachChainsCachesStack.PopToSize( topOfStackIndex );
			mayHitWhileRunningCachesStack.PopToSize( topOfStackIndex );
			canSafelyKeepHighSpeedCachesStack.PopToSize( topOfStackIndex );
			environmentTestResultsStack.truncate( topOfStackIndex );
		} else {
			// For case of growing stack topOfStackIndex must point at the first
			// 'illegal' yet free element at top of the stack
			Assert( predictedMovementActions.size() == topOfStackIndex );
		}

		topOfStack = new( predictedMovementActions.unsafe_grow_back() )PredictedMovementAction( belowTopOfStack );

		// Push a copy of previous player state onto top of the stack
		oldPlayerState = &playerStatesStack.back();
		playerStatesStack.push_back( *oldPlayerState );
		currPlayerState = &playerStatesStack.back();
		// Push a copy of previous movement state onto top of the stack
		botMovementStatesStack.push_back( botMovementStatesStack.back() );
		pendingWeaponsStack.push_back( belowTopOfStack.record.pendingWeapon );

		oldStepMillis = belowTopOfStack.stepMillis;
		Assert( belowTopOfStack.timestamp >= level.time );
		Assert( belowTopOfStack.stepMillis > 0 );
		totalMillisAhead = (unsigned)( belowTopOfStack.timestamp - level.time ) + belowTopOfStack.stepMillis;
	} else {
		predictedMovementActions.clear();
		botMovementStatesStack.clear();
		playerStatesStack.clear();
		pendingWeaponsStack.clear();

		defaultBotInputsCachesStack.PopToSize( 0 );
		reachChainsCachesStack.PopToSize( 0 );
		mayHitWhileRunningCachesStack.PopToSize( 0 );
		canSafelyKeepHighSpeedCachesStack.PopToSize( 0 );
		environmentTestResultsStack.clear();

		const edict_t *self = game.edicts + bot->EntNum();

		topOfStack = new( predictedMovementActions.unsafe_grow_back() )PredictedMovementAction;
		// Push the actual bot player state onto top of the stack
		oldPlayerState = &self->r.client->ps;
		playerStatesStack.push_back( *oldPlayerState );
		currPlayerState = &playerStatesStack.back();
		// Push the actual bot movement state onto top of the stack
		botMovementStatesStack.push_back( module->movementState );
		pendingWeaponsStack.push_back( (signed char)oldPlayerState->stats[STAT_PENDING_WEAPON] );

		oldStepMillis = game.frametime;
		totalMillisAhead = 0;
	}
	// Check whether topOfStackIndex really points at the last element of the array
	Assert( predictedMovementActions.size() == topOfStackIndex + 1 );

	movementState = &botMovementStatesStack.back();
	// Provide a predicted movement state for Ai base class
	bot->entityPhysicsState = &movementState->entityPhysicsState;

	// Set the current action record
	this->record = &topOfStack->record;
	this->record->pendingWeapon = pendingWeaponsStack.back();

	Assert( reachChainsCachesStack.Size() + 1 == predictedMovementActions.size() );
	Assert( mayHitWhileRunningCachesStack.Size() + 1 == predictedMovementActions.size() );
	// Check caches size, a cache size must match the stack size after addition of a single placeholder element.
	Assert( defaultBotInputsCachesStack.Size() + 1 == predictedMovementActions.size() );
	// Then put placeholders for non-cached yet values onto top of caches stack
	defaultBotInputsCachesStack.PushDummyNonCachedValue();
	// The different method is used (there is no copy/move constructors for the template type)
	reachChainsCachesStack.UnsafeGrowForNonCachedValue();
	mayHitWhileRunningCachesStack.PushDummyNonCachedValue();
	canSafelyKeepHighSpeedCachesStack.PushDummyNonCachedValue();
	new ( environmentTestResultsStack.unsafe_grow_back() )EnvironmentTraceCache;

	this->shouldRollback = false;

	// Save a movement state BEFORE movement step
	topOfStack->entityPhysicsState = this->movementState->entityPhysicsState;
	topOfStack->movementStatesMask = this->movementState->GetContainedStatesMask();
}

inline BaseMovementAction *MovementPredictionContext::SuggestAnyAction() {
	if( BaseMovementAction *action = this->SuggestSuitableAction() ) {
		return action;
	}

	// If no action has been suggested, use a default/dummy one.
	// We have to check the combat action since it might be disabled due to planning stack overflow.
	if( bot->ShouldAttack() && bot->ShouldKeepXhairOnEnemy() ) {
		const auto &selectedEnemies = bot->GetSelectedEnemies();
		if( selectedEnemies.AreValid() && selectedEnemies.ArePotentiallyHittable() ) {
			if( !module->combatDodgeSemiRandomlyToTargetAction.IsDisabledForPlanning() ) {
				return &module->combatDodgeSemiRandomlyToTargetAction;
			}
		}
	}

	return &module->fallbackMovementAction;
}

BaseMovementAction *MovementPredictionContext::SuggestSuitableAction() {
	Assert( !this->actionSuggestedByAction );

	const auto &entityPhysicsState = this->movementState->entityPhysicsState;

	if( entityPhysicsState.waterLevel > 1 ) {
		return &module->swimMovementAction;
	}

	if( movementState->jumppadMovementState.hasTouchedJumppad ) {
		if( movementState->jumppadMovementState.hasEnteredJumppad ) {
			if( movementState->flyUntilLandingMovementState.IsActive() ) {
				if( movementState->flyUntilLandingMovementState.CheckForLanding( this ) ) {
					return &module->landOnSavedAreasAction;
				}

				return &module->flyUntilLandingAction;
			}
			// Fly until landing movement state has been deactivate,
			// switch to bunnying (and, implicitly, to a dummy action if it fails)
			return &module->walkCarefullyAction;
		}
		return &module->handleTriggeredJumppadAction;
	}

	if( const edict_t *groundEntity = entityPhysicsState.GroundEntity() ) {
		if( groundEntity->use == Use_Plat ) {
			// (prevent blocking if touching platform but not actually triggering it like @ wdm1 GA)
			const auto &mins = groundEntity->r.absmin;
			const auto &maxs = groundEntity->r.absmax;
			if( mins[0] <= entityPhysicsState.Origin()[0] && maxs[0] >= entityPhysicsState.Origin()[0] ) {
				if( mins[1] <= entityPhysicsState.Origin()[1] && maxs[1] >= entityPhysicsState.Origin()[1] ) {
					return &module->ridePlatformAction;
				}
			}
		}
	}

	if( movementState->campingSpotState.IsActive() ) {
		return &module->campASpotMovementAction;
	}

	// The dummy movement action handles escaping using the movement fallback
	if( module->activeMovementFallback ) {
		return &module->fallbackMovementAction;
	}
	return &module->walkCarefullyAction;
}

bool MovementPredictionContext::NextPredictionStep() {
	SetupStackForStep();

	// Reset prediction step millis time.
	// Actions might set their custom step value (otherwise it will be set to a default one).
	this->predictionStepMillis = 0;
#ifdef CHECK_ACTION_SUGGESTION_LOOPS
	Assert( module->movementActions.size() < 32 );
	uint32_t testedActionsMask = 0;
	StaticVector<BaseMovementAction *, 32> testedActionsList;
#endif

	// Get an initial suggested a-priori action
	BaseMovementAction *action;
	if( this->actionSuggestedByAction ) {
		action = this->actionSuggestedByAction;
		this->actionSuggestedByAction = nullptr;
	} else {
		action = this->SuggestSuitableAction();
	}

#ifdef CHECK_ACTION_SUGGESTION_LOOPS
	testedActionsMask |= ( 1 << action->ActionNum() );
	testedActionsList.push_back( action );
#endif

	this->sequenceStopReason = UNSPECIFIED;
	for(;; ) {
		this->cannotApplyAction = false;
		// Prevent reusing record from the switched on the current frame action
		this->record->Clear();
		if( this->activeAction != action ) {
			// If there was an active previous action, stop its application sequence.
			if( this->activeAction ) {
				unsigned stoppedAtFrameIndex = topOfStackIndex;

				// Never pass the UNSPECIFIED reason to the OnApplicationSequenceStopped() call
				if( sequenceStopReason == UNSPECIFIED ) {
					sequenceStopReason = SWITCHED;
				}

				this->activeAction->OnApplicationSequenceStopped( this, sequenceStopReason, stoppedAtFrameIndex );
			}

			sequenceStopReason = UNSPECIFIED;

			this->activeAction = action;
			// Start the action application sequence
			this->activeAction->OnApplicationSequenceStarted( this );
		}

		Debug( "About to call action->PlanPredictionStep() for %s at ToS frame %d\n", action->Name(), topOfStackIndex );
		action->PlanPredictionStep( this );
		// Check for rolling back necessity (an action application chain has lead to an illegal state)
		if( this->shouldRollback ) {
			// Stop an action application sequence manually with a failure.
			this->activeAction->OnApplicationSequenceStopped( this, BaseMovementAction::FAILED, (unsigned)-1 );
			// An action can be suggested again after rolling back on the next prediction step.
			// Force calling action->OnApplicationSequenceStarted() on the next prediction step.
			this->activeAction = nullptr;
			Debug( "Prediction step failed after action->PlanPredictionStep() call for %s\n", action->Name() );
			this->RollbackToSavepoint();
			// Continue planning by returning true (the stack will be restored to a savepoint index)
			return true;
		}

		if( this->cannotApplyAction ) {
			// If current action suggested an alternative, use it
			// Otherwise use the generic suggestion algorithm
			if( this->actionSuggestedByAction ) {
				Debug( "Cannot apply %s, but it has suggested %s\n", action->Name(), this->actionSuggestedByAction->Name() );
				action = this->actionSuggestedByAction;
				this->actionSuggestedByAction = nullptr;
			} else {
				auto *rejectedAction = action;
				action = this->SuggestAnyAction();
				Debug( "Cannot apply %s, using %s suggested by SuggestSuitableAction()\n", rejectedAction->Name(), action->Name() );
			}

#ifdef CHECK_ACTION_SUGGESTION_LOOPS
			if( testedActionsMask & ( 1 << action->ActionNum() ) ) {
				Debug( "List of actions suggested (and tested) this frame #%d:\n", this->topOfStackIndex );
				for( unsigned i = 0; i < testedActionsList.size(); ++i ) {
					if( Q_stricmp( testedActionsList[i]->Name(), action->Name() ) ) {
						Debug( "  %02d: %s\n", i, testedActionsList[i]->Name() );
					} else {
						Debug( ">>%02d: %s\n", i, testedActionsList[i]->Name() );
					}
				}

				AI_FailWith( __FUNCTION__, "An infinite action application loop has been detected\n" );
			}
			testedActionsMask |= ( 1 << action->ActionNum() );
			testedActionsList.push_back( action );
#endif
			// Test next action. Action switch will be handled by the logic above before calling action->PlanPredictionStep().
			continue;
		}

		// Movement prediction is completed
		if( this->isCompleted ) {
			constexpr const char *format = "Movement prediction is completed on %s, ToS frame %d, %d millis ahead\n";
			Debug( format, action->Name(), this->topOfStackIndex, this->totalMillisAhead );
			// Stop an action application sequence manually with a success.
			action->OnApplicationSequenceStopped( this, BaseMovementAction::SUCCEEDED, this->topOfStackIndex );
			// Save the predicted movement action
			this->SaveActionOnStack( action );
			// Stop planning by returning false
			return false;
		}

		// An action can be applied, stop testing suitable actions
		break;
	}

	Assert( action == this->activeAction );

	// If prediction step millis time has not been set, set it to a default value
	if( !this->predictionStepMillis ) {
		this->predictionStepMillis = 48;
	}

	NextMovementStep();

	action->CheckPredictionStepResults( this );
	// If results check has been passed
	if( !this->shouldRollback ) {
		// If movement planning is completed, there is no need to do a next step
		if( this->isCompleted ) {
			constexpr const char *format = "Movement prediction is completed on %s, ToS frame %d, %d millis ahead\n";
			Debug( format, action->Name(), this->topOfStackIndex, this->totalMillisAhead );
			SaveActionOnStack( action );
			// Stop action application sequence manually with a success.
			// Prevent duplicated OnApplicationSequenceStopped() call
			// (it might have been done in action->CheckPredictionStepResults() for this->activeAction)
			if( this->activeAction ) {
				this->activeAction->OnApplicationSequenceStopped( this, BaseMovementAction::SUCCEEDED, topOfStackIndex );
			}
			// Stop planning by returning false
			return false;
		}

		// Check whether next prediction step is possible
		if( this->CanGrowStackForNextStep() ) {
			SaveActionOnStack( action );
			// Continue planning by returning true
			return true;
		}

		// Disable this action for further planning (it has lead to stack overflow)
		action->isDisabledForPlanning = true;
		Debug( "Stack overflow on action %s, this action will be disabled for further planning\n", action->Name() );
		this->SetPendingRollback();
	}

	constexpr const char *format = "Prediction step failed for %s after calling action->CheckPredictionStepResults()\n";
	Debug( format, action->Name() );

	// An active action might have been already reset in action->CheckPredictionStepResults()
	if( this->activeAction ) {
		// Stop action application sequence with a failure manually.
		this->activeAction->OnApplicationSequenceStopped( this, BaseMovementAction::FAILED, (unsigned)-1 );
	}
	// An action can be suggested again after rolling back on the next prediction step.
	// Force calling action->OnApplicationSequenceStarted() on the next prediction step.
	this->activeAction = nullptr;

	this->RollbackToSavepoint();
	// Continue planning by returning true
	return true;
}

void MovementPredictionContext::BuildPlan() {
	for( auto *movementAction: module->movementActions )
		movementAction->BeforePlanning();

	// Intercept these calls implicitly performed by PMove()
	const auto general_PMoveTouchTriggers = module_PMoveTouchTriggers;
	const auto general_PredictedEvent = module_PredictedEvent;

	module_PMoveTouchTriggers = &Intercepted_PMoveTouchTriggers;
	module_PredictedEvent = &Intercepted_PredictedEvent;

	edict_t *const self = game.edicts + bot->EntNum();

	// The entity state might be modified by Intercepted_PMoveTouchTriggers(), so we have to save it
	const Vec3 origin( self->s.origin );
	const Vec3 velocity( self->velocity );
	const Vec3 angles( self->s.angles );
	const int viewHeight = self->viewheight;
	const Vec3 mins( self->r.mins );
	const Vec3 maxs( self->r.maxs );
	const int waterLevel = self->waterlevel;
	const int waterType = self->watertype;
	edict_t *const groundEntity = self->groundentity;
	const int groundEntityLinkCount = self->groundentity_linkcount;

	auto savedPlayerState = self->r.client->ps;
	auto savedPMove = self->r.client->old_pmove;

	Assert( self->ai->botRef->entityPhysicsState == &module->movementState.entityPhysicsState );
	// Save current entity physics state (it will be modified even for a single prediction step)
	const AiEntityPhysicsState currEntityPhysicsState = module->movementState.entityPhysicsState;

	// Remember to reset these values before each planning session
	this->totalMillisAhead = 0;
	this->savepointTopOfStackIndex = 0;
	this->topOfStackIndex = 0;
	this->activeAction = nullptr;
	this->actionSuggestedByAction = nullptr;
	this->sequenceStopReason = UNSPECIFIED;
	this->isCompleted = false;
	this->shouldRollback = false;

#ifndef CHECK_INFINITE_NEXT_STEP_LOOPS
	for(;; ) {
		if( !NextPredictionStep() ) {
			break;
		}
	}
#else
	::nextStepIterationsCounter = 0;
	for(;; ) {
		if( !NextPredictionStep() ) {
			break;
		}
		++nextStepIterationsCounter;
		if( nextStepIterationsCounter < NEXT_STEP_INFINITE_LOOP_THRESHOLD ) {
			continue;
		}
		// An verbose output has been enabled at this stage
		if( nextStepIterationsCounter < NEXT_STEP_INFINITE_LOOP_THRESHOLD + 200 ) {
			continue;
		}
		constexpr const char *message =
			"MovementPredictionContext::BuildPlan(): "
			"an infinite NextPredictionStep() loop has been detected. "
			"Check the server console output of last 200 steps\n";
		G_Error( "%s", message );
	}
#endif

	// The entity might be linked for some predicted state by Intercepted_PMoveTouchTriggers()
	GClip_UnlinkEntity( self );

	// Restore entity state
	origin.CopyTo( self->s.origin );
	velocity.CopyTo( self->velocity );
	angles.CopyTo( self->s.angles );
	self->viewheight = viewHeight;
	mins.CopyTo( self->r.mins );
	maxs.CopyTo( self->r.maxs );
	self->waterlevel = waterLevel;
	self->watertype = waterType;
	self->groundentity = groundEntity;
	self->groundentity_linkcount = groundEntityLinkCount;

	self->r.client->ps = savedPlayerState;
	self->r.client->old_pmove = savedPMove;

	// Set first predicted movement state as the current bot movement state
	module->movementState = botMovementStatesStack[0];
	// Even the first predicted movement state usually has modified physics state, restore it to a saved value
	module->movementState.entityPhysicsState = currEntityPhysicsState;
	// Restore the current entity physics state reference in Ai subclass
	self->ai->botRef->entityPhysicsState = &module->movementState.entityPhysicsState;
	// These assertions helped to find an annoying bug during development
	Assert( VectorCompare( self->s.origin, self->ai->botRef->entityPhysicsState->Origin() ) );
	Assert( VectorCompare( self->velocity, self->ai->botRef->entityPhysicsState->Velocity() ) );

	module_PMoveTouchTriggers = general_PMoveTouchTriggers;
	module_PredictedEvent = general_PredictedEvent;

	for( auto *movementAction: module->movementActions )
		movementAction->AfterPlanning();
}

void MovementPredictionContext::NextMovementStep() {
	auto *botInput = &this->record->botInput;
	auto *entityPhysicsState = &movementState->entityPhysicsState;

	// Make sure we're modify botInput/entityPhysicsState before copying to ucmd

	// Corresponds to Bot::Think();
	module->ApplyPendingTurnToLookAtPoint( botInput, this );
	// Corresponds to module->Frame();
	this->activeAction->ExecActionRecord( this->record, botInput, this );
	// Corresponds to Bot::Think();
	module->ApplyInput( botInput, this );

	// ExecActionRecord() call in SimulateMockBotFrame() might fail or complete the planning execution early.
	// Do not call PMove() in these cases
	if( this->cannotApplyAction || this->isCompleted ) {
		return;
	}

	const edict_t *self = game.edicts + bot->EntNum();

	// Prepare for PMove()
	currPlayerState->POVnum = (unsigned)ENTNUM( self );
	currPlayerState->playerNum = (unsigned)PLAYERNUM( self );

	VectorCopy( entityPhysicsState->Origin(), currPlayerState->pmove.origin );
	VectorCopy( entityPhysicsState->Velocity(), currPlayerState->pmove.velocity );
	Vec3 angles( entityPhysicsState->Angles() );
	angles.CopyTo( currPlayerState->viewangles );

	currPlayerState->pmove.gravity = (int)level.gravity;
	currPlayerState->pmove.pm_type = PM_NORMAL;

	pmove_t pm;
	// TODO: Eliminate this call?
	memset( &pm, 0, sizeof( pmove_t ) );

	pm.playerState = currPlayerState;
	botInput->CopyToUcmd( &pm.cmd );

	for( int i = 0; i < 3; i++ )
		pm.cmd.angles[i] = (short)ANGLE2SHORT( angles.Data()[i] ) - currPlayerState->pmove.delta_angles[i];

	VectorSet( currPlayerState->pmove.delta_angles, 0, 0, 0 );

	// Check for unsigned value wrapping
	Assert( this->predictionStepMillis && this->predictionStepMillis < 100 );
	Assert( this->predictionStepMillis % 16 == 0 );
	pm.cmd.msec = (uint8_t)this->predictionStepMillis;
	pm.cmd.serverTimeStamp = game.serverTime + this->totalMillisAhead;

	this->frameEvents.Clear();

	// The naive solution of supplying a dummy trace function
	// (that yields a zeroed output with fraction = 1) does not work.
	// An actual logic tied to this flag has to be added in Pmove() for each module_Trace() call.
	pm.skipCollision = EnvironmentTraceCache().CanSkipPMoveCollision( this );

	// We currently test collisions only against a solid world on each movement step and the corresponding PMove() call.
	// Touching trigger entities is handled by Intercepted_PMoveTouchTriggers(), also we use AAS sampling for it.
	// Actions that involve touching trigger entities currently are never predicted ahead.
	// If an action really needs to test against entities, a corresponding prediction step flag
	// should be added and this interception of the module_Trace() should be skipped if the flag is set.

	// Save the G_GS_Trace() pointer
	auto oldModuleTrace = module_Trace;
	module_Trace = Intercepted_Trace;

	// Do not test entities contents for same reasons
	// Save the G_PointContents4D() pointer
	auto oldModulePointContents = module_PointContents;
	module_PointContents = Intercepted_PointContents;

	Pmove( &pm );

	// Restore the G_GS_Trace() pointer
	module_Trace = oldModuleTrace;
	// Restore the G_PointContents4D() pointer
	module_PointContents = oldModulePointContents;

	// Update the entity physics state that is going to be used in the next prediction frame
	entityPhysicsState->UpdateFromPMove( &pm );
	// Update the entire movement state that is going to be used in the next prediction frame
	this->movementState->Frame( this->predictionStepMillis );
	this->movementState->TryDeactivateContainedStates( self, this );
}

#ifdef CHECK_INFINITE_NEXT_STEP_LOOPS
int nextStepIterationsCounter;
#endif

void MovementPredictionContext::Debug( const char *format, ... ) const {
#if ( defined( ENABLE_MOVEMENT_DEBUG_OUTPUT ) || defined( CHECK_INFINITE_NEXT_STEP_LOOPS ) )
	// Check if there is an already detected error in this case and perform output only it the condition passes
#if !defined( ENABLE_MOVEMENT_DEBUG_OUTPUT )
	if( ::nextStepIterationsCounter < NEXT_STEP_INFINITE_LOOP_THRESHOLD ) {
		return;
	}
#endif

	char tag[64];
	Q_snprintfz( tag, 64, "^6MovementPredictionContext(%s)", bot->Tag() );

	va_list va;
	va_start( va, format );
	AI_Debugv( tag, format, va );
	va_end( va );
#endif
}



void MovementPredictionContext::SetDefaultBotInput() {
	// Check for cached value first
	if( const BotInput *cachedDefaultBotInput = defaultBotInputsCachesStack.GetCached() ) {
		this->record->botInput = *cachedDefaultBotInput;
		return;
	}

	const auto &entityPhysicsState = movementState->entityPhysicsState;
	auto *botInput = &this->record->botInput;

	botInput->ClearMovementDirections();
	botInput->SetSpecialButton( false );
	botInput->SetWalkButton( false );
	botInput->isUcmdSet = true;

	// If angles are already set (e.g. by pending look at point), do not try to set angles for following AAS reach. chain
	if( botInput->hasAlreadyComputedAngles ) {
		// Save cached value and return
		defaultBotInputsCachesStack.SetCachedValue( *botInput );
		return;
	}

	botInput->isLookDirSet = true;
	botInput->canOverrideLookVec = false;
	botInput->canOverridePitch = true;
	botInput->canOverrideUcmd = false;

	const int navTargetAasAreaNum = NavTargetAasAreaNum();
	const int currAasAreaNum = CurrAasAreaNum();
	// If a current area and a nav target area are defined
	if( currAasAreaNum && navTargetAasAreaNum ) {
		// If bot is not in nav target area
		if( currAasAreaNum != navTargetAasAreaNum ) {
			const int nextReachNum = this->NextReachNum();
			if( nextReachNum ) {
				const auto &nextReach = AiAasWorld::Instance()->Reachabilities()[nextReachNum];
				if( DistanceSquared( entityPhysicsState.Origin(), nextReach.start ) < 12 * 12 ) {
					Vec3 intendedLookVec( nextReach.end );
					intendedLookVec -= nextReach.start;
					intendedLookVec.NormalizeFast();
					intendedLookVec *= 16.0f;
					intendedLookVec += nextReach.start;
					intendedLookVec -= entityPhysicsState.Origin();
					if( entityPhysicsState.GroundEntity() ) {
						intendedLookVec.Z() = 0;
					}
					botInput->SetIntendedLookDir( intendedLookVec );
				} else {
					Vec3 intendedLookVec3( nextReach.start );
					intendedLookVec3 -= entityPhysicsState.Origin();
					botInput->SetIntendedLookDir( intendedLookVec3 );
				}
				// Save a cached value and return
				defaultBotInputsCachesStack.SetCachedValue( *botInput );
				return;
			}
		} else {
			// Look at the nav target
			Vec3 intendedLookVec( NavTargetOrigin() );
			intendedLookVec -= entityPhysicsState.Origin();
			botInput->SetIntendedLookDir( intendedLookVec );
			// Save a cached value and return
			defaultBotInputsCachesStack.SetCachedValue( *botInput );
			return;
		}
	}

	// A valid reachability chain does not exist, use dummy relaxed movement
	if( entityPhysicsState.Speed() > 1 ) {
		// Follow the existing velocity direction
		botInput->SetIntendedLookDir( entityPhysicsState.Velocity() );
	} else {
		// The existing velocity is too low to extract a direction from it, keep looking in the same direction
		botInput->SetAlreadyComputedAngles( entityPhysicsState.Angles() );
	}

	// Allow overriding look angles for aiming (since they are set to dummy ones)
	botInput->canOverrideLookVec = true;
	// Save a cached value and return
	defaultBotInputsCachesStack.SetCachedValue( *botInput );
}

void MovementPredictionContext::CheatingAccelerate( float frac ) {
	if( bot->ShouldMoveCarefully() ) {
		return;
	}

	const auto &entityPhysicsState = this->movementState->entityPhysicsState;
	if( entityPhysicsState.GroundEntity() ) {
		return;
	}

	const float speed = entityPhysicsState.Speed();
	const float speedThreshold = this->GetRunSpeed() - 15.0f;
	// Respect player class speed properties
	const float groundSpeedScale = speedThreshold / ( DEFAULT_PLAYERSPEED_STANDARD - 15.0f );

	// Avoid division by zero and logic errors
	if( speed < speedThreshold ) {
		return;
	}

	if( frac <= 0.0f ) {
		return;
	}

	const float maxSpeedGainPerSecond = 250.0f;
	const float minSpeedGainPerSecond = 75.0f;
	float speedGainPerSecond;
	// If speed = pivotSpeed speedGainPerSecond remains the same
	const float pivotSpeed = 550.0f * groundSpeedScale;
	const float constantAccelSpeed = 900.0f;
	if( speed > pivotSpeed ) {
		speedGainPerSecond = minSpeedGainPerSecond;
		// In this case speedGainPerSecond slowly decreases to minSpeedGainPerSecond
		// on speed = constantAccelSpeed or greater
		if( speed <= constantAccelSpeed ) {
			float speedFrac = BoundedFraction( speed - pivotSpeed, constantAccelSpeed - pivotSpeed );
			Assert( speedFrac >= 0.0f && speedFrac <= 1.0f );
			speedGainPerSecond += ( maxSpeedGainPerSecond - minSpeedGainPerSecond ) * ( 1.0f - speedFrac );
		}
	} else {
		// In this case speedGainPerSecond might be 2x as greater than maxSpeedGainPerSecond
		Assert( speedThreshold < pivotSpeed );
		float speedFrac = BoundedFraction( speed - speedThreshold, pivotSpeed - speedThreshold );
		Assert( speedFrac >= 0.0f && speedFrac <= 1.0f );
		speedGainPerSecond = maxSpeedGainPerSecond * ( 1.0f + ( 1.0f - speedFrac ) );
		// Also modify the frac to ensure the bot accelerates fast to the pivotSpeed in all cases
		// (the real applied frac is frac^(1/4))
		frac = SQRTFAST( frac );
	}

	speedGainPerSecond *= groundSpeedScale;
	// (CheatingAccelerate() is called for several kinds of fallback movement that are assumed to be reliable).
	// Do not lower speed gain per second in this case (fallback movement should be reliable).
	// A caller should set an appropriate frac if CheatingAccelerate() is used for other kinds of movement.

	clamp_high( frac, 1.0f );
	frac = SQRTFAST( frac );

	Vec3 newVelocity( entityPhysicsState.Velocity() );
	// Normalize velocity boost direction
	newVelocity *= 1.0f / speed;
	// Make velocity boost vector
	newVelocity *= ( frac * speedGainPerSecond ) * ( 0.001f * this->oldStepMillis );
	// Add velocity boost to the entity velocity in the given physics state
	newVelocity += entityPhysicsState.Velocity();

	record->SetModifiedVelocity( newVelocity );
}

void MovementPredictionContext::CheatingCorrectVelocity( const vec3_t target ) {
	const auto &entityPhysicsState = this->movementState->entityPhysicsState;

	Vec3 toTargetDir2D( target );
	toTargetDir2D -= entityPhysicsState.Origin();
	toTargetDir2D.Z() = 0;
	toTargetDir2D.NormalizeFast();

	Vec3 velocity2DDir( entityPhysicsState.Velocity() );
	velocity2DDir.Z() = 0;
	velocity2DDir *= 1.0f / entityPhysicsState.Speed2D();

	CheatingCorrectVelocity( velocity2DDir.Dot( toTargetDir2D ), toTargetDir2D );
}

void MovementPredictionContext::CheatingCorrectVelocity( float velocity2DDirDotToTarget2DDir, const Vec3 &toTargetDir2D ) {
	// Respect player class movement limitations
	if( !( this->currPlayerState->pmove.stats[PM_STAT_FEATURES] & PMFEAT_AIRCONTROL ) ) {
		return;
	}

	const auto &entityPhysicsState = this->movementState->entityPhysicsState;

	// Make correction less effective for large angles multiplying it
	// by the dot product to avoid a weird-looking cheating movement
	float controlMultiplier = 0.05f + fabsf( velocity2DDirDotToTarget2DDir ) * 0.05f;
	// Use lots of correction near items
	if( bot->ShouldMoveCarefully() ) {
		controlMultiplier += 0.10f;
	}

	const float speed = entityPhysicsState.Speed();
	if( speed < 100 ) {
		return;
	}

	// Check whether the direction to the target is normalized
	Assert( toTargetDir2D.LengthFast() > 0.99f && toTargetDir2D.LengthFast() < 1.01f );

	Vec3 newVelocity( entityPhysicsState.Velocity() );
	// Normalize current velocity direction
	newVelocity *= 1.0f / speed;
	// Modify velocity direction
	newVelocity += controlMultiplier * toTargetDir2D;
	// Normalize velocity direction again after modification
	newVelocity.Normalize();
	// Restore velocity magnitude
	newVelocity *= speed;

	record->SetModifiedVelocity( newVelocity );
}