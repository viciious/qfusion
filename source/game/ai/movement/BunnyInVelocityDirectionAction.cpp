#include "MovementLocal.h"

void BunnyInVelocityDirectionAction::PlanPredictionStep( Context *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( !GenericCheckIsActionEnabled( context, &module->bunnyInterpolatingReachChainAction ) ) {
		return;
	}

	if( !CheckCommonBunnyingPreconditions( context ) ) {
		return;
	}

	if( entityPhysicsState.Speed2D() < context->GetRunSpeed() ) {
		Debug( "The 2D speed is way too low" );
		context->SetPendingRollback();
		return;
	}

	int currAreaNums[2] = { 0, 0 };
	int numCurrAreas = entityPhysicsState.PrepareRoutingStartAreas( currAreaNums );
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	for( int i = 0; i < numCurrAreas; ++i ) {
		if( !( aasAreaSettings[currAreaNums[i]].areaflags & AREA_NOFALL ) ) {
			Debug( "One of the current areas is not an AREA_NOFALL area" );
			context->SetPendingRollback();
			return;
		}
	}

	Vec3 intendedLookVec( entityPhysicsState.Velocity() );
	intendedLookVec.Z() = 0;

	if( !SetupBunnying( intendedLookVec, context ) ) {
		context->SetPendingRollback();
		return;
	}

	if( context->topOfStackIndex || !checkStopAtAreaNums.empty() ) {
		return;
	}

	Vec3 intendedLookDir( intendedLookVec );
	intendedLookDir *= 1.0f / entityPhysicsState.Speed2D();

	// Save checkStopAtAreaNums

	const auto *aasReach = AiAasWorld::Instance()->Reachabilities();
	for( auto &reachAndTravelTime: module->visibleNextReachCache.GetVisibleReachVector( context ) ) {
		const auto &reach = aasReach[reachAndTravelTime.ReachNum()];
		float squareDistance = Distance2DSquared( reach.start, entityPhysicsState.Origin() );
		if( squareDistance < SQUARE( 108 ) ) {
			continue;
		}

		Vec3 toReachDir( reach.start );
		toReachDir -= entityPhysicsState.Origin();
		toReachDir.Z() = 0;
		toReachDir *= 1.0f / sqrtf( squareDistance );
		if( intendedLookDir.Dot( toReachDir ) < 0.7f ) {
			continue;
		}

		checkStopAtAreaNums.push_back( reach.areanum );
		if( checkStopAtAreaNums.size() == checkStopAtAreaNums.capacity() ) {
			break;
		}
	}
}
