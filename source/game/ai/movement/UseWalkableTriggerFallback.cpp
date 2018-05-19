#include "UseWalkableTriggerFallback.h"
#include "MovementLocal.h"
#include "../combat/TacticalSpotsRegistry.h"

void UseWalkableTriggerFallback::GetSteeringTarget( vec3_t target ) {
	// Triggers do not have s.origin set.
	// Get the bounds center
	VectorSubtract( trigger->r.absmax, trigger->r.absmin, target );
	VectorScale( target, 0.5f, target );
	// Add bounds center to abs mins
	VectorAdd( trigger->r.absmin, target, target );
}

bool UseWalkableTriggerFallback::TryDeactivate( Context *context ) {
	if( GenericGroundMovementFallback::TryDeactivate( context ) ) {
		return true;
	}

	if( GenericGroundMovementFallback::ShouldSkipTests( context ) ) {
		return false;
	}

	if( level.time - activatedAt > 750 ) {
		status = INVALID;
		return true;
	}

	vec3_t targetOrigin;
	GetSteeringTarget( targetOrigin );
	if( !TestActualWalkability( triggerAreaNumsCache.GetAreaNum( ENTNUM( trigger ) ), targetOrigin, context ) ) {
		status = INVALID;
		return true;
	}

	return false;
}

MovementFallback *FallbackMovementAction::TryFindWalkableTriggerFallback( Context *context ) {
	if( const edict_t *trigger = FindClosestToTargetTrigger( context ) ) {
		auto *fallback = &self->ai->botRef->useWalkableTriggerFallback;
		fallback->Activate( trigger );
		return fallback;
	}

	return nullptr;
}

const edict_t *FallbackMovementAction::FindClosestToTargetTrigger( Context *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	int fromAreaNums[2];
	int numFromAreas = entityPhysicsState.PrepareRoutingStartAreas( fromAreaNums );
	int goalAreaNum = context->NavTargetAasAreaNum();
	ClosestTriggerProblemParams problemParams( entityPhysicsState.Origin(), fromAreaNums, numFromAreas, goalAreaNum );
	// self->r.abs* values may be shifted having values remaining from a failed prediction step
	Vec3 absMins( problemParams.Origin() );
	Vec3 absMaxs( problemParams.Origin() );
	absMins += playerbox_stand_mins;
	absMaxs += playerbox_stand_maxs;
	context->nearbyTriggersCache.EnsureValidForBounds( absMins.Data(), absMaxs.Data() );
	return FindClosestToTargetTrigger( problemParams, context->nearbyTriggersCache );
}

typedef Context::NearbyTriggersCache NearbyTriggersCache;

const edict_t *FallbackMovementAction::FindClosestToTargetTrigger( const ClosestTriggerProblemParams &problemParams,
																   const NearbyTriggersCache &triggersCache ) {
	const int allowedTravelFlags = self->ai->botRef->AllowedTravelFlags();

	const auto triggerTravelFlags = &triggersCache.triggerTravelFlags[0];
	const auto triggerEntNums = &triggersCache.triggerEntNums[0];
	const auto triggerNumEnts = &triggersCache.triggerNumEnts[0];

	const edict_t *bestEnt = nullptr;
	int bestTravelTime = std::numeric_limits<int>::max();
	for( int i = 0; i < 3; ++i ) {
		if( allowedTravelFlags & triggerTravelFlags[i] ) {
			int travelTime;
			const edict_t *ent = FindClosestToTargetTrigger( problemParams, triggerEntNums[i], *triggerNumEnts[i], &travelTime );
			if( travelTime && travelTime < bestTravelTime ) {
				bestEnt = ent;
			}
		}
	}

	return bestTravelTime < std::numeric_limits<int>::max() ? bestEnt : nullptr;
}

const edict_t *FallbackMovementAction::FindClosestToTargetTrigger( const ClosestTriggerProblemParams &problemParams,
																   const uint16_t *triggerEntNums,
																   int numTriggerEnts,
																   int *travelTime ) {
	float *origin = const_cast<float *>( problemParams.Origin() );
	const int *fromAreaNums = problemParams.FromAreaNums();
	const int numFromAreas = problemParams.numFromAreas;
	const int toAreaNum = problemParams.goalAreaNum;
	const edict_t *gameEdicts = game.edicts;
	const auto *routeCache = self->ai->botRef->routeCache;

	int bestTravelTimeFromTrigger = std::numeric_limits<int>::max();
	int bestTriggerIndex = -1;
	int bestTriggerAreaNum = 0;

	trace_t trace;
	vec3_t traceMins, traceMaxs;
	TacticalSpotsRegistry::GetSpotsWalkabilityTraceBounds( traceMins, traceMaxs );

	for( int i = 0; i < numTriggerEnts; ++i ) {
		const edict_t *ent = gameEdicts + triggerEntNums[i];

		// Check whether the trigger is reachable by walking in 2 seconds (200 AAS time units)
		const int entAreaNum = triggerAreaNumsCache.GetAreaNum( triggerEntNums[i] );

		int travelTimeToTrigger = 0;
		for( int j = 0; j < numFromAreas; ++j ) {
			const auto travelFlags = GenericGroundMovementFallback::TRAVEL_FLAGS;
			travelTimeToTrigger = routeCache->TravelTimeToGoalArea( fromAreaNums[j], entAreaNum, travelFlags );
			if( travelTimeToTrigger && travelTimeToTrigger < 200 ) {
				break;
			}
			travelTimeToTrigger = 0;
		}

		if( !travelTimeToTrigger ) {
			continue;
		}

		// Find a travel time from trigger for regular bot movement

		const int travelTimeFromTrigger = routeCache->FastestRouteToGoalArea( entAreaNum, toAreaNum );
		if( !travelTimeFromTrigger || travelTimeFromTrigger >= bestTravelTimeFromTrigger ) {
			continue;
		}

		// All trigger seem to have absent s.origin, but the precomputed area is valid.
		// AiAasWorld::FindAreaNum() hides this issue by testing entity bounds too and producing a feasible result
		Vec3 entOrigin( ent->r.absmin );
		entOrigin += ent->r.absmax;
		entOrigin *= 0.5f;

		// We have to test against entities and not only solid world
		// since this is a fallback action and any failure is critical
		G_Trace( &trace, origin, traceMins, traceMaxs, entOrigin.Data(), self, MASK_PLAYERSOLID | CONTENTS_TRIGGER );
		// We might hit a solid world aiming at the trigger origin.
		// Check hit distance too, not only fraction for equality to 1.0f.
		if( trace.fraction != 1.0f && trace.ent != triggerEntNums[i] ) {
			continue;
		}

		bestTriggerIndex = i;
		bestTravelTimeFromTrigger = travelTimeFromTrigger;
		bestTriggerAreaNum = entAreaNum;
	}

	if( bestTriggerIndex >= 0 ) {
		*travelTime = bestTravelTimeFromTrigger;
		return gameEdicts + triggerEntNums[bestTriggerIndex];
	}

	return nullptr;
}