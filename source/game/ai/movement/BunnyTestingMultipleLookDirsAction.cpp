#include "BunnyTestingMultipleLookDirsAction.h"
#include "MovementLocal.h"

bool BotBunnyTestingMultipleLookDirsAction::TraceArcInSolidWorld( const AiEntityPhysicsState &startPhysicsState,
																  const vec3_t from, const vec3_t to ) {
	trace_t trace;
	auto brushMask = MASK_WATER | MASK_SOLID;

	const float velocityZ = startPhysicsState.Velocity()[2];
	if( !startPhysicsState.GroundEntity() && velocityZ < 50.0f ) {
		StaticWorldTrace( &trace, from, to, brushMask );
		return trace.fraction != 1.0f;
	}

	Vec3 midPoint( to );
	midPoint += from;
	midPoint *= 0.5f;

	// Lets figure out deltaZ making an assumption that all forward momentum is converted to the direction to the point one

	const float squareDistanceToMidPoint = SQUARE( from[0] - midPoint.X() ) + SQUARE( from[1] - midPoint.Y() );
	if( squareDistanceToMidPoint < 10 ) {
		StaticWorldTrace( &trace, from, to, brushMask );
		return trace.fraction != 1.0f;
	}

	const float timeToMidPoint = sqrtf( squareDistanceToMidPoint ) / startPhysicsState.Speed2D();
	const float deltaZ = velocityZ * timeToMidPoint - 0.5f * level.gravity * ( timeToMidPoint * timeToMidPoint );

	// Does not worth making an arc
	// Note that we ignore negative deltaZ since the real trajectory differs anyway
	if( deltaZ < 2.0f ) {
		StaticWorldTrace( &trace, from, to, brushMask );
		return trace.fraction != 1.0f;
	}

	midPoint.Z() += deltaZ;

	StaticWorldTrace( &trace, from, midPoint.Data(), brushMask );
	if( trace.fraction != 1.0f ) {
		return false;
	}

	StaticWorldTrace( &trace, midPoint.Data(), to, brushMask );
	if( trace.fraction != 1.0f ) {
		return false;
	}

	return true;
}

void BotBunnyTestingMultipleLookDirsAction::BeforePlanning() {
	GenericRunBunnyingAction::BeforePlanning();
	currSuggestedLookDirNum = 0;
	suggestedLookDirs.clear();
	dirsBaseAreas.clear();

	// Ensure the suggested action has been set in subtype constructor
	Assert( suggestedAction );
}

void BotBunnyTestingMultipleLookDirsAction::OnApplicationSequenceStarted( Context *ctx ) {
	GenericRunBunnyingAction::OnApplicationSequenceStarted( ctx );
	// If there is no dirs tested yet
	if( currSuggestedLookDirNum == 0 ) {
		suggestedLookDirs.clear();
		dirsBaseAreas.clear();
		if( ctx->NavTargetAasAreaNum() ) {
			SaveSuggestedLookDirs( ctx );
		}
	}
}

void BotBunnyTestingMultipleLookDirsAction::OnApplicationSequenceStopped( Context *context,
																		  SequenceStopReason stopReason,
																		  unsigned stoppedAtFrameIndex ) {
	GenericRunBunnyingAction::OnApplicationSequenceStopped( context, stopReason, stoppedAtFrameIndex );
	// If application sequence succeeded
	if( stopReason != FAILED ) {
		if( stopReason != DISABLED ) {
			currSuggestedLookDirNum = 0;
		}
		return;
	}

	// If the action has been disabled due to prediction stack overflow
	if( this->isDisabledForPlanning ) {
		return;
	}

	// If rolling back is available for the current suggested dir
	if( disabledForApplicationFrameIndex != context->savepointTopOfStackIndex ) {
		return;
	}

	// If another suggested look dir exists
	if( currSuggestedLookDirNum + 1 < suggestedLookDirs.size() ) {
		currSuggestedLookDirNum++;
		// Allow the action application after the context rollback to savepoint
		this->disabledForApplicationFrameIndex = std::numeric_limits<unsigned>::max();
		// Ensure this action will be used after rollback
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}
	// Otherwise use the first dir in a new sequence started on some other frame
	currSuggestedLookDirNum = 0;
}

inline float SuggestObstacleAvoidanceCorrectionFraction( const Context *context ) {
	// Might be negative!
	float speedOverRunSpeed = context->movementState->entityPhysicsState.Speed() - context->GetRunSpeed();
	if( speedOverRunSpeed > 500.0f ) {
		return 0.15f;
	}
	return 0.35f - 0.20f * speedOverRunSpeed / 500.0f;
}

void BotBunnyTestingMultipleLookDirsAction::PlanPredictionStep( Context *context ) {
	if( !GenericCheckIsActionEnabled( context, suggestedAction ) ) {
		return;
	}

	// Do this test after GenericCheckIsActionEnabled(), otherwise disabledForApplicationFrameIndex does not get tested
	if( currSuggestedLookDirNum >= suggestedLookDirs.size() ) {
		Debug( "There is no suggested look dirs yet/left\n" );
		context->SetPendingRollback();
		return;
	}

	if( !CheckCommonBunnyingPreconditions( context ) ) {
		return;
	}

	context->record->botInput.SetIntendedLookDir( suggestedLookDirs[currSuggestedLookDirNum], true );

	if( isTryingObstacleAvoidance ) {
		context->TryAvoidJumpableObstacles( SuggestObstacleAvoidanceCorrectionFraction( context ) );
	}

	if( !SetupBunnying( context->record->botInput.IntendedLookDir(), context ) ) {
		context->SetPendingRollback();
		return;
	}
}

inline AreaAndScore *BotBunnyTestingMultipleLookDirsAction::TakeBestCandidateAreas( AreaAndScore *inputBegin,
																					AreaAndScore *inputEnd,
																					unsigned maxAreas ) {
	Assert( inputEnd >= inputBegin );
	const uintptr_t numAreas = inputEnd - inputBegin;
	const uintptr_t numResultAreas = numAreas < maxAreas ? numAreas : maxAreas;

	// Move best area to the array head, repeat it for the array tail
	for( uintptr_t i = 0, end = numResultAreas; i < end; ++i ) {
		// Set the start area as a current best one
		auto &startArea = *( inputBegin + i );
		for( uintptr_t j = i + 1; j < numAreas; ++j ) {
			auto &currArea = *( inputBegin + j );
			// If current area is better (<) than the start one, swap these areas
			if( currArea.score < startArea.score ) {
				std::swap( currArea, startArea );
			}
		}
	}

	return inputBegin + numResultAreas;
}

void BotBunnyTestingMultipleLookDirsAction::SaveCandidateAreaDirs( Context *context,
																   AreaAndScore *candidateAreasBegin,
																   AreaAndScore *candidateAreasEnd ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const int navTargetAreaNum = context->NavTargetAasAreaNum();
	const auto *aasAreas = AiAasWorld::Instance()->Areas();

	AreaAndScore *takenAreasBegin = candidateAreasBegin;
	Assert( maxSuggestedLookDirs <= suggestedLookDirs.capacity() );
	unsigned maxAreas = maxSuggestedLookDirs - suggestedLookDirs.size();
	AreaAndScore *takenAreasEnd = TakeBestCandidateAreas( candidateAreasBegin, candidateAreasEnd, maxAreas );

	for( auto iter = takenAreasBegin; iter < takenAreasEnd; ++iter ) {
		int areaNum = ( *iter ).areaNum;
		void *mem = suggestedLookDirs.unsafe_grow_back();
		dirsBaseAreas.push_back( areaNum );
		if( areaNum != navTargetAreaNum ) {
			Vec3 *toAreaDir = new(mem)Vec3( aasAreas[areaNum].center );
			toAreaDir->Z() = aasAreas[areaNum].mins[2] + 32.0f;
			*toAreaDir -= entityPhysicsState.Origin();
			toAreaDir->Z() *= Z_NO_BEND_SCALE;
			toAreaDir->NormalizeFast();
		} else {
			Vec3 *toTargetDir = new(mem)Vec3( context->NavTargetOrigin() );
			*toTargetDir -= entityPhysicsState.Origin();
			toTargetDir->NormalizeFast();
		}
	}
}