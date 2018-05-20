#include "BunnyInterpolatingReachChainAction.h"
#include "MovementLocal.h"
#include "ReachChainInterpolator.h"

void BunnyInterpolatingReachChainAction::PlanPredictionStep( Context *context ) {
	if( !GenericCheckIsActionEnabled( context, &module->bunnyStraighteningReachChainAction ) ) {
		return;
	}

	if( !CheckCommonBunnyingPreconditions( context ) ) {
		return;
	}

	context->record->botInput.isUcmdSet = true;
	// Continue interpolating while a next reach has these travel types
	const int compatibleReachTypes[4] = { TRAVEL_WALK, TRAVEL_WALKOFFLEDGE, TRAVEL_JUMP, TRAVEL_STRAFEJUMP };
	// Stop interpolating on these reach types but include a reach start in interpolation
	const int allowedEndReachTypes[4] = { TRAVEL_TELEPORT, TRAVEL_JUMPPAD, TRAVEL_ELEVATOR, TRAVEL_LADDER };
	ReachChainInterpolator interpolator;
	interpolator.stopAtDistance = 256;
	interpolator.SetCompatibleReachTypes( compatibleReachTypes, sizeof( compatibleReachTypes ) / sizeof( int ) );
	interpolator.SetAllowedEndReachTypes( allowedEndReachTypes, sizeof( allowedEndReachTypes ) / sizeof( int ) );
	if( !interpolator.Exec( context ) ) {
		context->SetPendingRollback();
		Debug( "Cannot apply action: cannot interpolate reach chain\n" );
		return;
	}

	context->record->botInput.SetIntendedLookDir( interpolator.Result(), true );

	if( !SetupBunnying( context->record->botInput.IntendedLookDir(), context ) ) {
		context->SetPendingRollback();
		return;
	}
}