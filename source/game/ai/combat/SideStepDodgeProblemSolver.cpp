#include "SideStepDodgeProblemSolver.h"

bool SideStepDodgeProblemSolver::FindSingle( vec_t *spotOrigin ) {
	trace_t trace;
	Vec3 tmpVec3( 0, 0, 0 );
	Vec3 *droppedToFloorOrigin = nullptr;
	if( auto originEntity = originParams.originEntity ) {
		if( originEntity->groundentity ) {
			tmpVec3.Set( originEntity->s.origin );
			tmpVec3.Z() += originEntity->r.mins[2];
			droppedToFloorOrigin = &tmpVec3;
		} else if( originEntity->ai && originEntity->ai->botRef ) {
			const auto &entityPhysicsState = originEntity->ai->botRef->EntityPhysicsState();
			if( entityPhysicsState->HeightOverGround() < 32.0f ) {
				tmpVec3.Set( entityPhysicsState->Origin() );
				tmpVec3.Z() += playerbox_stand_mins[2];
				tmpVec3.Z() -= entityPhysicsState->HeightOverGround();
				droppedToFloorOrigin = &tmpVec3;
			}
		} else {
			auto *ent = const_cast<edict_t *>( originEntity );
			Vec3 end( originEntity->s.origin );
			end.Z() += originEntity->r.mins[2] - 32.0f;
			G_Trace( &trace, ent->s.origin, ent->r.mins, ent->r.maxs, end.Data(), ent, MASK_SOLID );
			if( trace.fraction != 1.0f && ISWALKABLEPLANE( &trace.plane ) ) {
				tmpVec3.Set( trace.endpos );
				droppedToFloorOrigin = &tmpVec3;
			}
		}
	} else {
		Vec3 end( originParams.origin );
		end.Z() -= 48.0f;
		G_Trace( &trace, const_cast<float *>( originParams.origin ), nullptr, nullptr, end.Data(), nullptr, MASK_SOLID );
		if( trace.fraction != 1.0f && ISWALKABLEPLANE( &trace.plane ) ) {
			tmpVec3.Set( trace.endpos );
			droppedToFloorOrigin = &tmpVec3;
		}
	}

	if( !droppedToFloorOrigin ) {
		return false;
	}

	Vec3 testedOrigin( *droppedToFloorOrigin );
	// Make sure the tested box will be at least 1 unit above the ground
	testedOrigin.Z() += ( -playerbox_stand_mins[2] ) + 1.0f;

	float bestScore = -1.0f;
	vec3_t bestDir;

	Vec3 keepVisible2DDir( problemParams.keepVisibleOrigin );
	keepVisible2DDir -= originParams.origin;
	keepVisible2DDir.Z() = 0;
	keepVisible2DDir.Normalize();

	edict_t *const ignore = originParams.originEntity ? const_cast<edict_t *>( originParams.originEntity ) : nullptr;
	const float dx = playerbox_stand_maxs[0] - playerbox_stand_mins[0];
	const float dy = playerbox_stand_maxs[1] - playerbox_stand_mins[1];
	for( int i = -1; i <= 1; ++i ) {
		for( int j = -1; j <= 1; ++j ) {
			// Disallow diagonal moves (can't be checked so easily by just adding an offset)
			// Disallow staying at the current origin as well.
			if( ( i && j ) || !( i || j ) ) {
				continue;
			}

			testedOrigin.X() = droppedToFloorOrigin->X() + i * dx;
			testedOrigin.Y() = droppedToFloorOrigin->Y() + j * dy;

			// Check actual visibility of the origin specified in problem params
			G_Trace( &trace, testedOrigin.Data(), nullptr, nullptr, problemParams.keepVisibleOrigin, ignore, MASK_SOLID );
			if( trace.fraction != 1.0f || trace.startsolid ) {
				continue;
			}

			float *mins = playerbox_stand_mins;
			float *maxs = playerbox_stand_maxs;
			// Test whether the box intersects solid
			G_Trace( &trace, testedOrigin.Data(), mins, maxs, testedOrigin.Data(), ignore, MASK_SOLID );
			if( trace.fraction != 1.0f || trace.startsolid ) {
				continue;
			}

			// Check ground below
			Vec3 groundTraceEnd( testedOrigin );
			groundTraceEnd.Z() -= 72.0f;
			G_Trace( &trace, testedOrigin.Data(), nullptr, nullptr, groundTraceEnd.Data(), ignore, MASK_SOLID );
			if( trace.fraction == 1.0f || trace.allsolid ) {
				continue;
			}
			if( !ISWALKABLEPLANE( &trace.plane ) ) {
				continue;
			}
			if( trace.contents & ( CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_NODROP | CONTENTS_DONOTENTER ) ) {
				continue;
			}

			Vec3 spot2DDir( testedOrigin.Data() );
			spot2DDir -= originParams.origin;
			spot2DDir.Z() = 0;
			spot2DDir.Normalize();

			// Give side spots a greater score, but make sure the score is always positive for each spot
			float score = ( 1.0f - fabsf( spot2DDir.Dot( keepVisible2DDir ) ) ) + 0.1f;
			if( score > bestScore ) {
				bestScore = score;
				spot2DDir.CopyTo( bestDir );
			}
		}
	}

	if( bestScore > 0 ) {
		VectorCopy( bestDir, spotOrigin );
		VectorScale( spotOrigin, sqrtf( dx * dx + dy * dy ), spotOrigin );
		VectorAdd( spotOrigin, droppedToFloorOrigin->Data(), spotOrigin );
		return true;
	}

	return false;
}