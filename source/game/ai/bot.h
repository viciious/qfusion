#ifndef AI_BOT_H
#define AI_BOT_H

#include "static_vector.h"
#include "bot_perception_manager.h"
#include "bot_planner.h"
#include "ai_base_ai.h"
#include "vec3.h"

#include "bot_movement.h"
#include "combat/WeaponSelector.h"
#include "combat/FireTargetCache.h"
#include "bot_tactical_spots_cache.h"
#include "bot_threat_tracker.h"
#include "bot_roaming_manager.h"
#include "bot_weight_config.h"

#include "bot_goals.h"
#include "bot_actions.h"

class AiSquad;
class AiBaseEnemyPool;

struct AiAlertSpot {
	int id;
	Vec3 origin;
	float radius;
	float regularEnemyInfluenceScale;
	float carrierEnemyInfluenceScale;

	AiAlertSpot( int id_,
				 const Vec3 &origin_,
				 float radius_,
				 float regularEnemyInfluenceScale_ = 1.0f,
				 float carrierEnemyInfluenceScale_ = 1.0f )
		: id( id_ ),
		origin( origin_ ),
		radius( radius_ ),
		regularEnemyInfluenceScale( regularEnemyInfluenceScale_ ),
		carrierEnemyInfluenceScale( carrierEnemyInfluenceScale_ ) {}
};

// This can be represented as an enum but feels better in the following form.
// Many values that affect bot behaviour already are not boolean
// (such as nav targets and special movement states like camping spots),
// and thus controlling a bot by a single flags field already is not possible.
// This struct is likely to be extended by non-boolean values later.
struct SelectedMiscTactics {
	bool willAdvance;
	bool willRetreat;

	bool shouldBeSilent;
	bool shouldMoveCarefully;

	bool shouldAttack;
	bool shouldKeepXhairOnEnemy;

	bool willAttackMelee;
	bool shouldRushHeadless;

	inline SelectedMiscTactics() { Clear(); };

	inline void Clear() {
		willAdvance = false;
		willRetreat = false;

		shouldBeSilent = false;
		shouldMoveCarefully = false;

		shouldAttack = false;
		shouldKeepXhairOnEnemy = false;

		willAttackMelee = false;
		shouldRushHeadless = false;
	}

	inline void PreferAttackRatherThanRun() {
		shouldAttack = true;
		shouldKeepXhairOnEnemy = true;
	}

	inline void PreferRunRatherThanAttack() {
		shouldAttack = true;
		shouldKeepXhairOnEnemy = false;
	}
};

class Bot : public Ai
{
	friend class AiManager;
	friend class BotEvolutionManager;
	friend class AiBaseTeam;
	friend class AiSquadBasedTeam;
	friend class AiObjectiveBasedTeam;
	friend class BotPlanner;
	friend class AiSquad;
	friend class AiBaseEnemyPool;
	friend class BotThreatTracker;
	friend class BotPerceptionManager;
	friend class BotFireTargetCache;
	friend class BotItemsSelector;
	friend class BotWeaponSelector;
	friend class BotRoamingManager;
	friend class TacticalSpotsRegistry;
	friend class BotNavMeshQueryCache;
	friend class BotFallbackMovementPath;
	friend class BotSameFloorClusterAreasCache;
	friend class BotBaseGoal;
	friend class BotGrabItemGoal;
	friend class BotKillEnemyGoal;
	friend class BotRunAwayGoal;
	friend class BotReactToDangerGoal;
	friend class BotReactToThreatGoal;
	friend class BotReactToEnemyLostGoal;
	friend class BotAttackOutOfDespairGoal;
	friend class BotRoamGoal;
	friend class BotTacticalSpotsCache;
	friend class WorldState;
	friend struct BotMovementState;
	friend class BotMovementPredictionContext;
	friend class BotBaseMovementAction;
	friend class BotDummyMovementAction;
	friend class BotMoveOnLadderMovementAction;
	friend class BotHandleTriggeredJumppadMovementAction;
	friend class BotLandOnSavedAreasMovementAction;
	friend class BotRidePlatformMovementAction;
	friend class BotSwimMovementAction;
	friend class BotFlyUntilLandingMovementAction;
	friend class BotCampASpotMovementAction;
	friend class BotWalkCarefullyMovementAction;
	friend class BotGenericRunBunnyingMovementAction;
	friend class BotBunnyStraighteningReachChainMovementAction;
	friend class BotBunnyToBestShortcutAreaMovementAction;
	friend class BotBunnyToBestFloorClusterPointMovementAction;
	friend class BotBunnyInterpolatingReachChainMovementAction;
	friend class BotWalkOrSlideInterpolatingReachChainMovementAction;
	friend class BotCombatDodgeSemiRandomlyToTargetMovementAction;

	friend class BotGenericGroundMovementFallback;
	friend class BotUseWalkableNodeMovementFallback;
	friend class BotUseRampExitMovementFallback;
	friend class BotUseStairsExitMovementFallback;
	friend class BotUseWalkableTriggerMovementFallback;
	friend class BotFallDownMovementFallback;
	friend class BotJumpOverBarrierMovementFallback;

	friend class CachedTravelTimesMatrix;
public:
	static constexpr auto PREFERRED_TRAVEL_FLAGS =
		TFL_WALK | TFL_WALKOFFLEDGE | TFL_JUMP | TFL_STRAFEJUMP | TFL_AIR | TFL_TELEPORT | TFL_JUMPPAD;
	static constexpr auto ALLOWED_TRAVEL_FLAGS =
		PREFERRED_TRAVEL_FLAGS | TFL_WATER | TFL_WATERJUMP | TFL_SWIM | TFL_LADDER | TFL_ELEVATOR | TFL_BARRIERJUMP;

	Bot( edict_t *self_, float skillLevel_ );

	~Bot() override;

	// For backward compatibility with dated code that should be rewritten
	const edict_t *Self() const { return self; }
	edict_t *Self() { return self; }

	// Should be preferred instead of use of Self() that is deprecated and will be removed
	int EntNum() const { return ENTNUM( self ); }

	inline float Skill() const { return skillLevel; }
	inline bool IsReady() const { return level.ready[PLAYERNUM( self )]; }

	void Pain( const edict_t *enemy, float kick, int damage ) {
		if( kick != 0.0f ) {
			lastKnockbackAt = level.time;
		}
		threatTracker.OnPain( enemy, kick, damage );
	}

	void OnEnemyDamaged( const edict_t *enemy, int damage ) {
		threatTracker.OnEnemyDamaged( enemy, damage );
	}

	void OnEnemyOriginGuessed( const edict_t *enemy, unsigned millisSinceLastSeen, const float *guessedOrigin = nullptr ) {
		if( !guessedOrigin ) {
			guessedOrigin = enemy->s.origin;
		}
		threatTracker.OnEnemyOriginGuessed( enemy, millisSinceLastSeen, guessedOrigin );
	}

	void RegisterEvent( const edict_t *ent, int event, int parm ) {
		perceptionManager.RegisterEvent( ent, event, parm );
	}

	inline void OnAttachedToSquad( AiSquad *squad_ ) {
		this->squad = squad_;
		threatTracker.OnAttachedToSquad( squad_ );
		ForcePlanBuilding();
	}

	inline void OnDetachedFromSquad( AiSquad *squad_ ) {
		this->squad = nullptr;
		threatTracker.OnDetachedFromSquad( squad_ );
		ForcePlanBuilding();
	}

	inline bool IsInSquad() const { return squad != nullptr; }

	inline int64_t LastAttackedByTime( const edict_t *attacker ) {
		return threatTracker.LastAttackedByTime( attacker );
	}
	inline int64_t LastTargetTime( const edict_t *target ) {
		return threatTracker.LastTargetTime( target );
	}
	inline void OnEnemyRemoved( const Enemy *enemy ) {
		threatTracker.OnEnemyRemoved( enemy );
	}
	inline void OnHurtByNewThreat( const edict_t *newThreat, const AiFrameAwareUpdatable *threatDetector ) {
		threatTracker.OnHurtByNewThreat( newThreat, threatDetector );
	}

	inline float GetBaseOffensiveness() const { return baseOffensiveness; }

	float GetEffectiveOffensiveness() const;

	inline void SetBaseOffensiveness( float baseOffensiveness_ ) {
		this->baseOffensiveness = baseOffensiveness_;
		clamp( this->baseOffensiveness, 0.0f, 1.0f );
	}

	inline void ClearOverriddenEntityWeights() {
		itemsSelector.ClearOverriddenEntityWeights();
	}

	inline void OverrideEntityWeight( const edict_t *ent, float weight ) {
		itemsSelector.OverrideEntityWeight( ent, weight );
	}

	inline const int *Inventory() const { return self->r.client->ps.inventory; }

	typedef void (*AlertCallback)( void *receiver, Bot *bot, int id, float alertLevel );

	void EnableAutoAlert( const AiAlertSpot &alertSpot, AlertCallback callback, void *receiver );
	void DisableAutoAlert( int id );

	inline int Health() const {
		return self->r.client->ps.stats[STAT_HEALTH];
	}
	inline int Armor() const {
		return self->r.client->ps.stats[STAT_ARMOR];
	}
	inline bool CanAndWouldDropHealth() const {
		return GT_asBotWouldDropHealth( self->r.client );
	}
	inline void DropHealth() {
		GT_asBotDropHealth( self->r.client );
	}
	inline bool CanAndWouldDropArmor() const {
		return GT_asBotWouldDropArmor( self->r.client );
	}
	inline void DropArmor() {
		GT_asBotDropArmor( self->r.client );
	}
	inline float PlayerDefenciveAbilitiesRating() const {
		return GT_asPlayerDefenciveAbilitiesRating( self->r.client );
	}
	inline float PlayerOffenciveAbilitiesRating() const {
		return GT_asPlayerOffensiveAbilitiesRating( self->r.client );
	}

	struct ObjectiveSpotDef {
		int id;
		float navWeight;
		float goalWeight;
		bool isDefenceSpot;

		ObjectiveSpotDef()
			: id( -1 ), navWeight( 0.0f ), goalWeight( 0.0f ), isDefenceSpot( false ) {}

		void Invalidate() { id = -1; }
		bool IsActive() const { return id >= 0; }
		int DefenceSpotId() const { return ( IsActive() && isDefenceSpot ) ? id : -1; }
		int OffenseSpotId() const { return ( IsActive() && !isDefenceSpot ) ? id : -1; }
	};

	ObjectiveSpotDef &GetObjectiveSpot() {
		return objectiveSpotDef;
	}

	inline void ClearDefenceAndOffenceSpots() {
		objectiveSpotDef.Invalidate();
	}

	// TODO: Provide goal weight as well as nav weight?
	inline void SetDefenceSpot( int spotId, float weight ) {
		objectiveSpotDef.id = spotId;
		objectiveSpotDef.navWeight = objectiveSpotDef.goalWeight = weight;
		objectiveSpotDef.isDefenceSpot = false;
	}

	// TODO: Provide goal weight as well as nav weight?
	inline void SetOffenseSpot( int spotId, float weight ) {
		objectiveSpotDef.id = spotId;
		objectiveSpotDef.navWeight = objectiveSpotDef.goalWeight = weight;
		objectiveSpotDef.isDefenceSpot = false;
	}

	inline float Fov() const { return 110.0f + 69.0f * Skill(); }
	inline float FovDotFactor() const { return cosf( (float)DEG2RAD( Fov() / 2 ) ); }

	inline BotBaseGoal *GetGoalByName( const char *name ) { return botPlanner.GetGoalByName( name ); }
	inline BotBaseAction *GetActionByName( const char *name ) { return botPlanner.GetActionByName( name ); }

	inline BotScriptGoal *AllocScriptGoal() { return botPlanner.AllocScriptGoal(); }
	inline BotScriptAction *AllocScriptAction() { return botPlanner.AllocScriptAction(); }

	inline const BotWeightConfig &WeightConfig() const { return weightConfig; }
	inline BotWeightConfig &WeightConfig() { return weightConfig; }

	inline void OnInterceptedPredictedEvent( int ev, int parm ) {
		movementPredictionContext.OnInterceptedPredictedEvent( ev, parm );
	}
	inline void OnInterceptedPMoveTouchTriggers( pmove_t *pm, const vec3_t previousOrigin ) {
		movementPredictionContext.OnInterceptedPMoveTouchTriggers( pm, previousOrigin );
	}

	inline const AiEntityPhysicsState *EntityPhysicsState() const {
		return entityPhysicsState;
	}

	bool TestWhetherCanSafelyKeepHighSpeed( BotMovementPredictionContext *context = nullptr );

	// The movement code should use this method if there really are no
	// feasible ways to continue traveling to the nav target.
	void OnMovementToNavTargetBlocked();
protected:
	virtual void Frame() override;
	virtual void Think() override;

	virtual void PreFrame() override {
		// We should update weapons status each frame since script weapons may be changed each frame.
		// These statuses are used by firing methods, so actual weapon statuses are required.
		UpdateScriptWeaponsStatus();
	}

	virtual void SetFrameAffinity( unsigned modulo, unsigned offset ) override {
		AiFrameAwareUpdatable::SetFrameAffinity( modulo, offset );
		botPlanner.SetFrameAffinity( modulo, offset );
		perceptionManager.SetFrameAffinity( modulo, offset );
		threatTracker.SetFrameAffinity( modulo, offset );
	}

	virtual void OnNavTargetTouchHandled() override {
		selectedNavEntity.InvalidateNextFrame();
	}

	virtual void TouchedOtherEntity( const edict_t *entity ) override;

	virtual Vec3 GetNewViewAngles( const vec3_t oldAngles, const Vec3 &desiredDirection,
								   unsigned frameTime, float angularSpeedMultiplier ) const override;
private:
	inline bool IsPrimaryAimEnemy( const edict_t *enemy ) const {
		return selectedEnemies.IsPrimaryEnemy( enemy );
	}

	BotWeightConfig weightConfig;
	BotPerceptionManager perceptionManager;
	BotThreatTracker threatTracker;
	BotPlanner botPlanner;

	float skillLevel;

	SelectedEnemies selectedEnemies;
	SelectedEnemies lostEnemies;
	SelectedWeapons selectedWeapons;
	SelectedMiscTactics selectedTactics;

	BotWeaponSelector weaponsSelector;

	BotTacticalSpotsCache tacticalSpotsCache;
	BotRoamingManager roamingManager;

	BotFireTargetCache builtinFireTargetCache;
	BotFireTargetCache scriptFireTargetCache;

	BotGrabItemGoal grabItemGoal;
	BotKillEnemyGoal killEnemyGoal;
	BotRunAwayGoal runAwayGoal;
	BotReactToDangerGoal reactToDangerGoal;
	BotReactToThreatGoal reactToThreatGoal;
	BotReactToEnemyLostGoal reactToEnemyLostGoal;
	BotAttackOutOfDespairGoal attackOutOfDespairGoal;
	BotRoamGoal roamGoal;

	BotGenericRunToItemAction genericRunToItemAction;
	BotPickupItemAction pickupItemAction;
	BotWaitForItemAction waitForItemAction;

	BotKillEnemyAction killEnemyAction;
	BotAdvanceToGoodPositionAction advanceToGoodPositionAction;
	BotRetreatToGoodPositionAction retreatToGoodPositionAction;
	BotSteadyCombatAction steadyCombatAction;
	BotGotoAvailableGoodPositionAction gotoAvailableGoodPositionAction;
	BotAttackFromCurrentPositionAction attackFromCurrentPositionAction;

	BotGenericRunAvoidingCombatAction genericRunAvoidingCombatAction;
	BotStartGotoCoverAction startGotoCoverAction;
	BotTakeCoverAction takeCoverAction;

	BotStartGotoRunAwayTeleportAction startGotoRunAwayTeleportAction;
	BotDoRunAwayViaTeleportAction doRunAwayViaTeleportAction;
	BotStartGotoRunAwayJumppadAction startGotoRunAwayJumppadAction;
	BotDoRunAwayViaJumppadAction doRunAwayViaJumppadAction;
	BotStartGotoRunAwayElevatorAction startGotoRunAwayElevatorAction;
	BotDoRunAwayViaElevatorAction doRunAwayViaElevatorAction;
	BotStopRunningAwayAction stopRunningAwayAction;

	BotDodgeToSpotAction dodgeToSpotAction;

	BotTurnToThreatOriginAction turnToThreatOriginAction;

	BotTurnToLostEnemyAction turnToLostEnemyAction;
	BotStartLostEnemyPursuitAction startLostEnemyPursuitAction;
	BotStopLostEnemyPursuitAction stopLostEnemyPursuitAction;

	// Must be initialized before any of movement actions constructors is called
	StaticVector<BotBaseMovementAction *, 16> movementActions;

	BotDummyMovementAction dummyMovementAction;
	BotHandleTriggeredJumppadMovementAction handleTriggeredJumppadMovementAction;
	BotLandOnSavedAreasMovementAction landOnSavedAreasSetMovementAction;
	BotRidePlatformMovementAction ridePlatformMovementAction;
	BotSwimMovementAction swimMovementAction;
	BotFlyUntilLandingMovementAction flyUntilLandingMovementAction;
	BotCampASpotMovementAction campASpotMovementAction;
	BotWalkCarefullyMovementAction walkCarefullyMovementAction;
	BotBunnyStraighteningReachChainMovementAction bunnyStraighteningReachChainMovementAction;
	BotBunnyToBestShortcutAreaMovementAction bunnyToBestShortcutAreaMovementAction;
	BotBunnyToBestFloorClusterPointMovementAction bunnyToBestFloorClusterPointMovementAction;
	BotBunnyInterpolatingReachChainMovementAction bunnyInterpolatingReachChainMovementAction;
	BotWalkOrSlideInterpolatingReachChainMovementAction walkOrSlideInterpolatingReachChainMovementAction;
	BotCombatDodgeSemiRandomlyToTargetMovementAction combatDodgeSemiRandomlyToTargetMovementAction;

	BotMovementState movementState;

	BotMovementPredictionContext movementPredictionContext;

	BotUseWalkableNodeMovementFallback useWalkableNodeMovementFallback;
	BotUseRampExitMovementFallback useRampExitMovementFallback;
	BotUseStairsExitMovementFallback useStairsExitMovementFallback;
	BotUseWalkableTriggerMovementFallback useWalkableTriggerMovementFallback;

	BotJumpToSpotMovementFallback jumpToSpotMovementFallback;
	BotFallDownMovementFallback fallDownMovementFallback;
	BotJumpOverBarrierMovementFallback jumpOverBarrierMovementFallback;

	BotMovementFallback *activeMovementFallback;

	int64_t vsayTimeout;

	AiSquad *squad;

	ObjectiveSpotDef objectiveSpotDef;

	struct AlertSpot : public AiAlertSpot {
		int64_t lastReportedAt;
		float lastReportedScore;
		AlertCallback callback;
		void *receiver;

		AlertSpot( const AiAlertSpot &spot, AlertCallback callback_, void *receiver_ )
			: AiAlertSpot( spot ),
			lastReportedAt( 0 ),
			lastReportedScore( 0.0f ),
			callback( callback_ ),
			receiver( receiver_ ) {};

		inline void Alert( Bot *bot, float score ) {
			callback( receiver, bot, id, score );
			lastReportedAt = level.time;
			lastReportedScore = score;
		}
	};

	static constexpr unsigned MAX_ALERT_SPOTS = 3;
	StaticVector<AlertSpot, MAX_ALERT_SPOTS> alertSpots;

	void CheckAlertSpots( const StaticVector<uint16_t, MAX_CLIENTS> &visibleTargets );

	static constexpr unsigned MAX_SCRIPT_WEAPONS = 3;

	StaticVector<AiScriptWeaponDef, MAX_SCRIPT_WEAPONS> scriptWeaponDefs;
	StaticVector<int, MAX_SCRIPT_WEAPONS> scriptWeaponCooldown;

	int64_t lastTouchedTeleportAt;
	int64_t lastTouchedJumppadAt;
	int64_t lastTouchedElevatorAt;
	int64_t lastKnockbackAt;

	unsigned similarWorldStateInstanceId;

	int64_t lastItemSelectedAt;
	int64_t noItemAvailableSince;

	int64_t lastBlockedNavTargetReportedAt;

	inline bool ShouldUseRoamSpotAsNavTarget() const {
		const auto &selectedNavEntity = GetSelectedNavEntity();
		// Wait for item selection in this case (the selection is just no longer valid).
		if( !selectedNavEntity.IsValid() ) {
			return false;
		}
		// There was a valid item selected
		if( !selectedNavEntity.IsEmpty() ) {
			return false;
		}

		return level.time - noItemAvailableSince > 3000;
	}

	class AimingRandomHolder
	{
		int64_t valuesTimeoutAt[3];
		float values[3];

public:
		inline AimingRandomHolder() {
			std::fill_n( valuesTimeoutAt, 3, 0 );
			std::fill_n( values, 3, 0.5f );
		}
		inline float GetCoordRandom( int coordNum ) {
			if( valuesTimeoutAt[coordNum] <= level.time ) {
				values[coordNum] = random();
				valuesTimeoutAt[coordNum] = level.time + 128 + From0UpToMax( 256, random() );
			}
			return values[coordNum];
		}
	};

	AimingRandomHolder aimingRandomHolder;

	class KeptInFovPoint
	{
		const edict_t *self;
		Vec3 origin;
		unsigned instanceId;
		float viewDot;
		bool isActive;

		float ComputeViewDot( const vec3_t origin_ ) {
			Vec3 selfToOrigin( origin_ );
			selfToOrigin -= self->s.origin;
			selfToOrigin.NormalizeFast();
			vec3_t forward;
			AngleVectors( self->s.angles, forward, nullptr, nullptr );
			return selfToOrigin.Dot( forward );
		}

public:
		KeptInFovPoint( const edict_t *self_ ) :
			self( self_ ), origin( 0, 0, 0 ), instanceId( 0 ), viewDot( -1.0f ), isActive( false ) {}

		void Activate( const Vec3 &origin_, unsigned instanceId_ ) {
			Activate( origin_.Data(), instanceId_ );
		}

		void Activate( const vec3_t origin_, unsigned instanceId_ ) {
			this->origin.Set( origin_ );
			this->instanceId = instanceId_;
			this->isActive = true;
			this->viewDot = ComputeViewDot( origin_ );
		}

		inline void TryDeactivate( const Vec3 &actualOrigin, unsigned instanceId_ ) {
			TryDeactivate( actualOrigin.Data(), instanceId_ );
		}

		inline void TryDeactivate( const vec3_t actualOrigin, unsigned instanceId_ ) {
			if( !this->isActive ) {
				return;
			}

			if( this->instanceId != instanceId_ ) {
				Deactivate();
				return;
			}

			if( this->origin.SquareDistanceTo( actualOrigin ) < 32 * 32 ) {
				return;
			}

			float actualDot = ComputeViewDot( actualOrigin );
			// Do not deactivate if an origin has been changed but the view angles are approximately the same
			if( fabsf( viewDot - actualDot ) > 0.1f ) {
				Deactivate();
				return;
			}
		}

		inline void Update( const Vec3 &actualOrigin, unsigned instanceId_ ) {
			Update( actualOrigin.Data(), instanceId );
		}

		inline void Update( const vec3_t actualOrigin, unsigned instanceId_ ) {
			TryDeactivate( actualOrigin, instanceId_ );

			if( !IsActive() ) {
				Activate( actualOrigin, instanceId_ );
			}
		}

		inline void Deactivate() { isActive = false; }
		inline bool IsActive() const { return isActive; }
		inline const Vec3 &Origin() const {
			assert( isActive );
			return origin;
		}
		inline unsigned InstanceIdOrDefault( unsigned default_ = 0 ) const {
			return isActive ? instanceId : default_;
		}
	};

	KeptInFovPoint keptInFovPoint;
	int64_t nextRotateInputAttemptAt;
	int64_t inputRotationBlockingTimer;
	int64_t lastInputRotationFailureAt;

	const Enemy *lastChosenLostOrHiddenEnemy;
	unsigned lastChosenLostOrHiddenEnemyInstanceId;

	float baseOffensiveness;

	class AiNavMeshQuery *navMeshQuery;

	SelectedNavEntity selectedNavEntity;
	// For tracking picked up items
	const NavEntity *prevSelectedNavEntity;

	BotItemsSelector itemsSelector;

	void UpdateKeptInFovPoint();

	void UpdateScriptWeaponsStatus();

	void MovementFrame( BotInput *input );
	void CheckGroundPlatform();
	bool CanChangeWeapons() const;
	void ChangeWeapons( const SelectedWeapons &selectedWeapons_ );
	void ChangeWeapon( int weapon );
	void FireWeapon( BotInput *input );
	virtual void OnBlockedTimeout() override;
	void SayVoiceMessages();
	void GhostingFrame();
	void ActiveFrame();
	void CallGhostingClientThink( const BotInput &input );
	void CallActiveClientThink( const BotInput &input );

	void OnRespawn();

	void ApplyPendingTurnToLookAtPoint( BotInput *input, BotMovementPredictionContext *context = nullptr ) const;
	void ApplyInput( BotInput *input, BotMovementPredictionContext *context = nullptr );

	void CheckBlockingDueToInputRotation();

	inline void InvertInput( BotInput *input, BotMovementPredictionContext *context = nullptr );
	inline void TurnInputToSide( vec3_t sideDir, int sign, BotInput *input, BotMovementPredictionContext *context = nullptr );

	inline bool TryRotateInput( BotInput *input, BotMovementPredictionContext *context = nullptr );

	// Returns true if current look angle worth pressing attack
	bool CheckShot( const AimParams &aimParams, const BotInput *input,
					const SelectedEnemies &selectedEnemies, const GenericFireDef &fireDef );

	bool TryTraceShot( trace_t *tr, const Vec3 &newLookDir,
					   const AimParams &aimParams,
					   const GenericFireDef &fireDef );

	bool CheckSplashTeamDamage( const vec3_t hitOrigin, const AimParams &aimParams, const GenericFireDef &fireDef );

	// A helper to determine whether continuous-fire weapons should be fired even if there is an obstacle in-front.
	// Should be called if a TryTraceShot() call has set non-unit fraction.
	bool IsShotBlockedBySolidWall( trace_t *tr,
								   float distanceThreshold,
								   const AimParams &aimParams,
								   const GenericFireDef &fireDef );

	void LookAtEnemy( float coordError, const vec3_t fire_origin, vec3_t target, BotInput *input );
	void PressAttack( const GenericFireDef *fireDef, const GenericFireDef *builtinFireDef,
					  const GenericFireDef *scriptFireDef, BotInput *input );

	inline bool HasEnemy() const { return selectedEnemies.AreValid(); }
	/*
	inline bool IsEnemyAStaticSpot() const { return selectedEnemies.IsStaticSpot(); }
	inline const edict_t *EnemyTraceKey() const { return selectedEnemies.TraceKey(); }
	inline const bool IsEnemyOnGround() const { return selectedEnemies.OnGround(); }
	 */
	inline Vec3 EnemyOrigin() const { return selectedEnemies.LastSeenOrigin(); }
	/*
	inline Vec3 EnemyLookDir() const { return selectedEnemies.LookDir(); }
	inline unsigned EnemyFireDelay() const { return selectedEnemies.FireDelay(); }
	inline Vec3 EnemyVelocity() const { return selectedEnemies.LastSeenVelocity(); }
	inline Vec3 EnemyMins() const { return selectedEnemies.Mins(); }
	inline Vec3 EnemyMaxs() const { return selectedEnemies.Maxs(); }*/

	static constexpr unsigned MAX_SAVED_AREAS = BotMovementPredictionContext::MAX_SAVED_LANDING_AREAS;
	StaticVector<int, MAX_SAVED_AREAS> savedLandingAreas;
	StaticVector<int, MAX_SAVED_AREAS> savedPlatformAreas;

	void CheckTargetProximity();

	bool HasJustPickedGoalItem() const;
public:
	// These methods are exposed mostly for script interface
	inline unsigned NextSimilarWorldStateInstanceId() {
		return ++similarWorldStateInstanceId;
	}

	void ForceSetNavEntity( const SelectedNavEntity &selectedNavEntity_ );

	inline void ForcePlanBuilding() {
		basePlanner->ClearGoalAndPlan();
	}
	inline void SetCampingSpot( const AiCampingSpot &campingSpot ) {
		movementState.campingSpotState.Activate( campingSpot );
	}
	inline void ResetCampingSpot() {
		movementState.campingSpotState.Deactivate();
	}
	inline bool HasActiveCampingSpot() const {
		return movementState.campingSpotState.IsActive();
	}
	inline void SetPendingLookAtPoint( const AiPendingLookAtPoint &lookAtPoint, unsigned timeoutPeriod ) {
		movementState.pendingLookAtPointState.Activate( lookAtPoint, timeoutPeriod );
	}
	inline void ResetPendingLookAtPoint() {
		movementState.pendingLookAtPointState.Deactivate();
	}
	inline bool HasPendingLookAtPoint() const {
		return movementState.pendingLookAtPointState.IsActive();
	}
	const SelectedNavEntity &GetSelectedNavEntity() const {
		return selectedNavEntity;
	}

	const SelectedNavEntity &GetOrUpdateSelectedNavEntity();

	const SelectedEnemies &GetSelectedEnemies() const { return selectedEnemies; }

	SelectedMiscTactics &GetMiscTactics() { return selectedTactics; }
	const SelectedMiscTactics &GetMiscTactics() const { return selectedTactics; }

	const AiAasRouteCache *RouteCache() const { return routeCache; }

	const Enemy *TrackedEnemiesHead() const { return threatTracker.TrackedEnemiesHead(); }

	const BotThreatTracker::HurtEvent *ActiveHurtEvent() const { return threatTracker.GetValidHurtEvent(); }

	const Danger *PrimaryDanger() const { return threatTracker.GetValidHazard(); }

	inline bool WillAdvance() const { return selectedTactics.willAdvance; }
	inline bool WillRetreat() const { return selectedTactics.willRetreat; }

	inline bool ShouldBeSilent() const { return selectedTactics.shouldBeSilent; }
	inline bool ShouldMoveCarefully() const { return selectedTactics.shouldMoveCarefully; }

	inline bool ShouldAttack() const { return selectedTactics.shouldAttack; }
	inline bool ShouldKeepXhairOnEnemy() const { return selectedTactics.shouldKeepXhairOnEnemy; }

	inline bool WillAttackMelee() const { return selectedTactics.willAttackMelee; }
	inline bool ShouldRushHeadless() const { return selectedTactics.shouldRushHeadless; }
};

#endif
