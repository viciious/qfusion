#include "BunnyToBestClusterPointAction.h"
#include "MovementLocal.h"

void BunnyToBestFloorClusterPointAction::PlanPredictionStep( Context *context ) {
	if( !GenericCheckIsActionEnabled( context, &self->ai->botRef->bunnyInterpolatingReachChainAction ) ) {
		return;
	}

	if( !CheckCommonBunnyingPreconditions( context ) ) {
		return;
	}

	if( !hasSpotOrigin ) {
		if( !context->sameFloorClusterAreasCache.GetClosestToTargetPoint( context, spotOrigin ) ) {
			context->SetPendingRollback();
			return;
		}
	}

	Vec3 intendedLookDir( spotOrigin );
	intendedLookDir -= context->movementState->entityPhysicsState.Origin();
	float distanceToSpot = intendedLookDir.NormalizeFast();

	context->record->botInput.SetIntendedLookDir( intendedLookDir, true );

	// Set initially to a default value
	float maxAccelDotThreshold = 1.0f;
	// If the distance to the spot is large enough (so we are unlikely to miss it without any space for correction)
	if( distanceToSpot > 128.0f ) {
		// Use the most possible allowed acceleration once the velocity and target dirs dot product exceeds this value
		maxAccelDotThreshold = distanceToSpot > 192.0f ? 0.5f : 0.7f;
	}

	if( !SetupBunnying( intendedLookDir, context, maxAccelDotThreshold ) ) {
		context->SetPendingRollback();
		return;
	}
}