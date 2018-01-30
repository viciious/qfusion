#include "ai_base_ai.h"
#include "ai_base_brain.h"
#include "ai_ground_trace_cache.h"

Ai::Ai( edict_t *self_,
		AiBaseBrain *aiBaseBrain_,
		AiAasRouteCache *routeCache_,
		AiEntityPhysicsState *entityPhysicsState_,
		int allowedAasTravelFlags_,
		int preferredAasTravelFlags_,
		float yawSpeed,
		float pitchSpeed )
	: EdictRef( self_ ),
	aiBaseBrain( aiBaseBrain_ ),
	routeCache( routeCache_ ),
	aasWorld( AiAasWorld::Instance() ),
	entityPhysicsState( entityPhysicsState_ ),
	travelFlagsRange( travelFlags, 2 ),
	blockedTimeoutAt( level.time + 15000 ) {
	travelFlags[0] = preferredAasTravelFlags_;
	travelFlags[1] = allowedAasTravelFlags_;
	angularViewSpeed[YAW] = yawSpeed;
	angularViewSpeed[PITCH] = pitchSpeed;
	angularViewSpeed[ROLL] = 999999.0f;
}

void Ai::SetFrameAffinity( unsigned modulo, unsigned offset ) {
	frameAffinityModulo = modulo;
	frameAffinityOffset = offset;
	aiBaseBrain->SetFrameAffinity( modulo, offset );
}

// These getters cannot be defined in headers due to incomplete AiBaseBrain class definition

int Ai::NavTargetAasAreaNum() const {
	return aiBaseBrain->NavTargetAasAreaNum();
}

Vec3 Ai::NavTargetOrigin() const {
	return aiBaseBrain->NavTargetOrigin();
}

float Ai::NavTargetRadius() const {
	return aiBaseBrain->NavTargetRadius();
}

bool Ai::IsNavTargetBasedOnEntity( const edict_t *ent ) const {
	return aiBaseBrain->IsNavTargetBasedOnEntity( ent );
}

void Ai::ResetNavigation() {
	blockedTimeoutAt = level.time + BLOCKED_TIMEOUT;
}

void Ai::UpdateReachChain( const ReachChainVector &oldReachChain,
						   ReachChainVector *currReachChain,
						   const AiEntityPhysicsState &state ) const {
	currReachChain->clear();
	if( !aiBaseBrain->HasNavTarget() ) {
		return;
	}

	const aas_reachability_t *reachabilities = AiAasWorld::Instance()->Reachabilities();
	const int goalAreaNum = NavTargetAasAreaNum();
	// First skip reaches to reached area
	unsigned i = 0;
	for( i = 0; i < oldReachChain.size(); ++i ) {
		if( reachabilities[oldReachChain[i].ReachNum()].areanum == state.CurrAasAreaNum() ) {
			break;
		}
	}
	// Copy remaining reachabilities
	for( unsigned j = i + 1; j < oldReachChain.size(); ++j )
		currReachChain->push_back( oldReachChain[j] );

	int areaNum;
	if( currReachChain->empty() ) {
		areaNum = state.currAasAreaNum;
	} else {
		areaNum = reachabilities[currReachChain->back().ReachNum()].areanum;
	}

	int reachNum, travelTime;
	while( areaNum != goalAreaNum && currReachChain->size() != currReachChain->capacity() ) {
		// We hope we'll be pushed in some other area during movement, and goal area will become reachable. Leave as is.
		if( !( travelTime = routeCache->PreferredRouteToGoalArea( areaNum, goalAreaNum, &reachNum ) ) ) {
			break;
		}
		areaNum = reachabilities[reachNum].areanum;
		currReachChain->emplace_back( ReachAndTravelTime( reachNum, (short)travelTime ) );
	}
}

int Ai::CheckTravelTimeMillis( const Vec3& from, const Vec3 &to, bool allowUnreachable ) {

	// We try to use the same checks the TacticalSpotsRegistry performs to find spots.
	// If a spot is not reachable, it is an bug,
	// because a reachability must have been checked by the spots registry first in a few preceeding calls.

	int fromAreaNum;
	constexpr float squareDistanceError = WorldState::OriginVar::MAX_ROUNDING_SQUARE_DISTANCE_ERROR;
	if( ( from - self->s.origin ).SquaredLength() < squareDistanceError ) {
		fromAreaNum = aasWorld->FindAreaNum( self );
	} else {
		fromAreaNum = aasWorld->FindAreaNum( from );
	}

	if( !fromAreaNum ) {
		if( allowUnreachable ) {
			return 0;
		}

		FailWith( "CheckTravelTimeMillis(): Can't find `from` AAS area" );
	}

	const int toAreaNum = aasWorld->FindAreaNum( to.Data() );
	if( !toAreaNum ) {
		if( allowUnreachable ) {
			return 0;
		}

		FailWith( "CheckTravelTimeMillis(): Can't find `to` AAS area" );
	}

	if( int aasTravelTime = routeCache->PreferredRouteToGoalArea( fromAreaNum, toAreaNum ) ) {
		return 10U * aasTravelTime;
	}

	if( allowUnreachable ) {
		return 0;
	}

	FailWith( "CheckTravelTimeMillis(): Can't find travel time %d->%d\n", fromAreaNum, toAreaNum );
}

void Ai::OnNavTargetSet( NavTarget *navTarget ) {
	// TODO??
	/*
	if (!entityPhysicsState.currAasAreaNum)
	{
	    currAasAreaNum = aasWorld->FindAreaNum(self);
	    if (!currAasAreaNum)
	    {
	        Debug("Still can't find curr aas area num");
	    }
	}

	nextReaches.clear();
	UpdateReachCache(currAasAreaNum);
	*/
}

void Ai::TouchedEntity( edict_t *ent ) {
	if( aiBaseBrain->HandleNavTargetTouch( ent ) ) {
		// Clear goal area num to ensure bot will not repeatedly try to reach that area even if he has no goals.
		// Usually it gets overwritten in this or next frame, when bot picks up next goal,
		// but sometimes there are no other goals to pick up.
		OnNavTargetTouchHandled();
		return;
	}
	TouchedOtherEntity( ent );
}

void Ai::Frame() {
	// Call super method first
	AiFrameAwareUpdatable::Frame();

	if( !G_ISGHOSTING( self ) ) {
		entityPhysicsState->UpdateFromEntity( self );
	}

	// Call brain Update() (Frame() and, maybe Think())
	aiBaseBrain->Update();

	if( level.spawnedTimeStamp + 5000 > game.realtime || !level.canSpawnEntities ) {
		self->nextThink = level.time + game.snapFrameTime;
		return;
	}
}

void Ai::Think() {
	if( !G_ISGHOSTING( self ) ) {
		// TODO: Check whether we are camping/holding a spot
		bool checkBlocked = true;
		if( !self->groundentity ) {
			checkBlocked = false;
		} else if( self->groundentity->use == Use_Plat && VectorLengthSquared( self->groundentity->velocity ) > 10 * 10 ) {
			checkBlocked = false;
		}

		if( checkBlocked && VectorLengthSquared( self->velocity ) > 30 * 30 ) {
			blockedTimeoutAt = level.time + BLOCKED_TIMEOUT;
		}

		// if completely stuck somewhere
		if( blockedTimeoutAt < level.time ) {
			OnBlockedTimeout();
			return;
		}
	}
}

void AiEntityPhysicsState::UpdateAreaNums() {
	const AiAasWorld *aasWorld = AiAasWorld::Instance();
	this->currAasAreaNum = ( decltype( this->currAasAreaNum ) )aasWorld->FindAreaNum( Origin() );
	// Use a computation shortcut when entity is on ground
	if( this->groundEntNum >= 0 ) {
		this->droppedToFloorOriginOffset = ( decltype( this->droppedToFloorOriginOffset ) )( -playerbox_stand_mins[2] );
		this->droppedToFloorOriginOffset += 4.0f;
		SetHeightOverGround( 0 );
		Vec3 droppedOrigin( Origin() );
		droppedOrigin.Z() -= this->droppedToFloorOriginOffset;
		this->droppedToFloorAasAreaNum = ( decltype( this->droppedToFloorAasAreaNum ) )aasWorld->FindAreaNum( droppedOrigin );
		return;
	}

	// Use a computation shortcut when the current area is grounded
	if( aasWorld->AreaSettings()[this->currAasAreaNum].areaflags & AREA_GROUNDED ) {
		float areaMinsZ = aasWorld->Areas()[this->currAasAreaNum].mins[2];
		float selfZ = Self()->s.origin[2];
		float heightOverGround_ = selfZ - areaMinsZ + playerbox_stand_maxs[2];
		clamp_high( heightOverGround_, GROUND_TRACE_DEPTH );
		SetHeightOverGround( heightOverGround_ );
		this->droppedToFloorOriginOffset = ( decltype( this->droppedToFloorOriginOffset ) )( heightOverGround_ - 4.0f );
		this->droppedToFloorAasAreaNum = this->currAasAreaNum;
		return;
	}

	// Try drop an origin from air to floor
	trace_t trace;
	edict_t *ent = const_cast<edict_t *>( Self() );
	Vec3 traceEnd( Origin() );
	traceEnd.Z() -= GROUND_TRACE_DEPTH;
	G_Trace( &trace, this->origin, ent->r.mins, ent->r.maxs, traceEnd.Data(), ent, MASK_PLAYERSOLID );
	// Check not only whether there is a hit but test whether is it really a ground (and not a wall or obstacle)
	if( trace.fraction != 1.0f && Origin()[2] - trace.endpos[2] > -playerbox_stand_mins[2] ) {
		float heightOverGround_ = trace.fraction * GROUND_TRACE_DEPTH + playerbox_stand_mins[2];
		this->droppedToFloorOriginOffset = ( decltype( this->droppedToFloorOriginOffset ) )( -playerbox_stand_mins[2] );
		this->droppedToFloorOriginOffset -= heightOverGround_ - 4.0f;
		SetHeightOverGround( heightOverGround_ );
		Vec3 droppedOrigin( Origin() );
		droppedOrigin.Z() -= this->droppedToFloorOriginOffset;
		this->droppedToFloorAasAreaNum = ( decltype( this->droppedToFloorAasAreaNum ) )aasWorld->FindAreaNum( droppedOrigin );
		return;
	}

	this->droppedToFloorOriginOffset = 0;
	SetHeightOverGround( std::numeric_limits<float>::infinity() );
	this->droppedToFloorAasAreaNum = this->currAasAreaNum;
}

float Ai::GetChangedAngle( float oldAngle, float desiredAngle, unsigned frameTime,
						   float angularSpeedMultiplier, int angleIndex ) const {
	if( oldAngle == desiredAngle ) {
		return oldAngle;
	}

	float maxAngularMove = angularSpeedMultiplier * angularViewSpeed[angleIndex] * ( 1e-3f * frameTime );
	float angularMove = AngleNormalize180( desiredAngle - oldAngle );
	if( angularMove < -maxAngularMove ) {
		angularMove = -maxAngularMove;
	} else if( angularMove > maxAngularMove ) {
		angularMove = maxAngularMove;
	}

	return AngleNormalize180( oldAngle + angularMove );
}

Vec3 Ai::GetNewViewAngles( const vec3_t oldAngles, const Vec3 &desiredDirection,
						   unsigned frameTime, float angularSpeedMultiplier ) const {
	// Based on turret script code

	// For those trying to learn working with angles
	// Vec3.x is the PITCH angle (up and down rotation)
	// Vec3.y is the YAW angle (left and right rotation)
	// Vec3.z is the ROLL angle (left and right inclination)

	vec3_t newAngles, desiredAngles;
	VecToAngles( desiredDirection.Data(), desiredAngles );

	// Normalize180 all angles so they can be compared
	for( int i = 0; i < 3; ++i ) {
		newAngles[i] = AngleNormalize180( oldAngles[i] );
		desiredAngles[i] = AngleNormalize180( desiredAngles[i] );
	}

	// Rotate the entity angles to the desired angles
	if( !VectorCompare( newAngles, desiredAngles ) ) {
		for( auto angleNum: { YAW, PITCH } ) {
			newAngles[angleNum] = GetChangedAngle( newAngles[angleNum], desiredAngles[angleNum],
												   frameTime, angularSpeedMultiplier, angleNum );
		}
	}

	return Vec3( newAngles );
}
