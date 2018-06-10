#include "BunnyInterpolatingReachChainAction.h"
#include "MovementLocal.h"
#include "ReachChainInterpolator.h"

// Continue interpolating while a next reach has these travel types
static const int COMPATIBLE_REACH_TYPES[] = {
	TRAVEL_WALK, TRAVEL_WALKOFFLEDGE
};

// Stop interpolating on these reach types but include a reach start in interpolation.
// Note: Jump/Strafejump reach-es should interrupt interpolation,
// otherwise they're prone to falling down as jumping over gaps should be timed precisely.
static const int ALLOWED_REACH_END_REACH_TYPES[] = {
	TRAVEL_JUMP, TRAVEL_STRAFEJUMP, TRAVEL_TELEPORT, TRAVEL_JUMPPAD, TRAVEL_ELEVATOR, TRAVEL_LADDER
};

// We do not want to add this as a method of a ReachChainInterpolator as it is very specific to these movement actions.
// Also we do not want to add extra computations for interpolation step (but this is a minor reason)
static int GetBestConformingToDirArea( const ReachChainInterpolator &interpolator ) {
	const Vec3 pivotDir( interpolator.Result() );

	int bestArea = 0;
	float bestDot = -1.0f;
	for( int i = 0; i < interpolator.dirs.size(); ++i ) {
		float dot = interpolator.dirs[i].Dot( pivotDir );
		if( dot > bestDot ) {
			bestArea = interpolator.dirsAreas[i];
			bestDot = dot;
		}
	}

	return bestArea;
}

void BunnyInterpolatingReachChainAction::PlanPredictionStep( Context *context ) {
	if( !GenericCheckIsActionEnabled( context, &module->bunnyStraighteningReachChainAction ) ) {
		return;
	}

	if( !CheckCommonBunnyingPreconditions( context ) ) {
		return;
	}

	context->record->botInput.isUcmdSet = true;
	ReachChainInterpolator interpolator;
	interpolator.stopAtDistance = 256;
	interpolator.SetCompatibleReachTypes( COMPATIBLE_REACH_TYPES, sizeof( COMPATIBLE_REACH_TYPES ) / sizeof( int ) );
	interpolator.SetAllowedEndReachTypes( ALLOWED_REACH_END_REACH_TYPES, sizeof( ALLOWED_REACH_END_REACH_TYPES ) / sizeof( int ) );
	if( !interpolator.Exec( context ) ) {
		context->SetPendingRollback();
		Debug( "Cannot apply action: cannot interpolate reach chain\n" );
		return;
	}

	// Set this area ONCE at the sequence start.
	// Interpolation happens at every frame, we need to have some well-defined pivot area
	if( this->checkStopAtAreaNums.empty() ) {
		checkStopAtAreaNums.push_back( GetBestConformingToDirArea( interpolator ) );
	}

	context->record->botInput.SetIntendedLookDir( interpolator.Result(), true );

	if( !SetupBunnying( context->record->botInput.IntendedLookDir(), context ) ) {
		context->SetPendingRollback();
		return;
	}
}

BunnyInterpolatingChainAtStartAction::BunnyInterpolatingChainAtStartAction( BotMovementModule *module_ )
	: BunnyTestingMultipleLookDirsAction( module_, NAME, COLOR_RGB( 72, 108, 0 ) ) {
	supportsObstacleAvoidance = false;
	// The constructor cannot be defined in the header due to this bot member access
	suggestedAction = &module->bunnyInterpolatingReachChainAction;
}

void BunnyInterpolatingChainAtStartAction::SaveSuggestedLookDirs( Context *context ) {
	suggestedLookDirs.clear();

	ReachChainInterpolator interpolator;
	interpolator.SetCompatibleReachTypes( COMPATIBLE_REACH_TYPES, sizeof( COMPATIBLE_REACH_TYPES ) / sizeof( int ) );
	interpolator.SetAllowedEndReachTypes( ALLOWED_REACH_END_REACH_TYPES, sizeof( ALLOWED_REACH_END_REACH_TYPES ) / sizeof( int ) );
	for( int i = 0; i < 5; ++i ) {
		interpolator.stopAtDistance = 192.0f + 192.0f * i;
		if( !interpolator.Exec( context ) ) {
			continue;
		}
		Vec3 newDir( interpolator.Result() );
		for( const Vec3 &presentDir: suggestedLookDirs ) {
			// Even slight changes in the direction matter, so avoid rejection unless there is almost exact match
			if( newDir.Dot( presentDir ) > 0.99f ) {
				goto nextAttempt;
			}
		}
		suggestedLookDirs.push_back( newDir );
		dirsBaseAreas.push_back( GetBestConformingToDirArea( interpolator ) );
		if( suggestedLookDirs.size() == 3 ) {
			break;
		}
nextAttempt:;
	}
}
