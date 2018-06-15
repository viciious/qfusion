#ifndef AI_BOT_H
#define AI_BOT_H

#include "static_vector.h"
#include "awareness/PerceptionManager.h"
#include "planning/BotPlanner.h"
#include "ai_base_ai.h"
#include "vec3.h"

#include "movement/MovementModule.h"
#include "combat/WeaponsUsageModule.h"
#include "planning/TacticalSpotsCache.h"
#include "awareness/ThreatTracker.h"
#include "planning/RoamingManager.h"
#include "bot_weight_config.h"

#include "planning/Goals.h"
#include "planning/Actions.h"

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
	friend class BotWeaponsUsageModule;
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

	friend class BotMovementModule;
	friend class MovementPredictionContext;
	// TODO: Remove this and refactor "kept in fov point" handling
	friend class FallbackMovementAction;
	friend class CorrectWeaponJumpAction;

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

	const player_state_t *PlayerState() const { return &self->r.client->ps; }
	player_state_t *PlayerState() { return &self->r.client->ps; }

	const float *Origin() const { return self->s.origin; }
	const float *Velocity() const { return self->velocity; }

	inline float Skill() const { return skillLevel; }
	inline bool IsReady() const { return level.ready[PLAYERNUM( self )]; }

	void OnPain( const edict_t *enemy, float kick, int damage ) {
		if( enemy != self ) {
			threatTracker.OnPain( enemy, kick, damage );
		}
	}

	void OnKnockback( edict_t *attacker, const vec3_t basedir, int kick, int dflags ) {
		if( kick ) {
			lastKnockbackAt = level.time;
			VectorCopy( basedir, lastKnockbackBaseDir );
			if( attacker == self ) {
				lastOwnKnockbackKick = kick;
				lastOwnKnockbackAt = level.time;
			}
		}
	}

	void OnEnemyDamaged( const edict_t *enemy, int damage ) {
		if( enemy != self ) {
			threatTracker.OnEnemyDamaged( enemy, damage );
		}
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
		movementModule.OnInterceptedPredictedEvent( ev, parm );
	}

	inline void OnInterceptedPMoveTouchTriggers( pmove_t *pm, const vec3_t previousOrigin ) {
		movementModule.OnInterceptedPMoveTouchTriggers( pm, previousOrigin );
	}

	inline const AiEntityPhysicsState *EntityPhysicsState() const {
		return entityPhysicsState;
	}

	// The movement code should use this method if there really are no
	// feasible ways to continue traveling to the nav target.
	void OnMovementToNavTargetBlocked();
protected:
	virtual void Frame() override;
	virtual void Think() override;

	virtual void PreFrame() override {
		// We should update weapons status each frame since script weapons may be changed each frame.
		// These statuses are used by firing methods, so actual weapon statuses are required.
		weaponsUsageModule.UpdateScriptWeaponsStatus();
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
	SelectedMiscTactics selectedTactics;

	BotWeaponsUsageModule weaponsUsageModule;

	BotTacticalSpotsCache tacticalSpotsCache;
	BotRoamingManager roamingManager;

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
	BotAttackAdvancingToTargetAction attackAdvancingToTargetAction;

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

	BotMovementModule movementModule;

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

	int64_t lastTouchedTeleportAt;
	int64_t lastTouchedJumppadAt;
	int64_t lastTouchedElevatorAt;
	int64_t lastKnockbackAt;
	int64_t lastOwnKnockbackAt;
	int lastOwnKnockbackKick;
	vec3_t lastKnockbackBaseDir;

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

	const Enemy *lastChosenLostOrHiddenEnemy;
	unsigned lastChosenLostOrHiddenEnemyInstanceId;

	float baseOffensiveness;

	class AiNavMeshQuery *navMeshQuery;

	SelectedNavEntity selectedNavEntity;
	// For tracking picked up items
	const NavEntity *prevSelectedNavEntity;

	BotItemsSelector itemsSelector;

	void UpdateKeptInFovPoint();

	bool CanChangeWeapons() const {
		return movementModule.CanChangeWeapons();
	}

	void ChangeWeapons( const SelectedWeapons &selectedWeapons_ );
	virtual void OnBlockedTimeout() override;
	void GhostingFrame();
	void ActiveFrame();
	void CallGhostingClientThink( const BotInput &input );
	void CallActiveClientThink( const BotInput &input );

	void OnRespawn();

	void CheckTargetProximity();

	bool HasJustPickedGoalItem() const;
public:
	// These methods are exposed mostly for script interface
	inline unsigned NextSimilarWorldStateInstanceId() {
		return ++similarWorldStateInstanceId;
	}

	int64_t LastTriggerTouchTime() const {
		return std::max( lastTouchedJumppadAt, std::max( lastTouchedTeleportAt, lastTouchedElevatorAt ) );
	}

	int64_t LastKnockbackAt() const { return lastKnockbackAt; }

	void ForceSetNavEntity( const SelectedNavEntity &selectedNavEntity_ );

	inline void ForcePlanBuilding() {
		basePlanner->ClearGoalAndPlan();
	}

	inline void SetCampingSpot( const AiCampingSpot &campingSpot ) {
		movementModule.SetCampingSpot( campingSpot );
	}
	inline void ResetCampingSpot() {
		movementModule.ResetCampingSpot();
	}
	inline bool HasActiveCampingSpot() const {
		return movementModule.HasActiveCampingSpot();
	}
	inline void SetPendingLookAtPoint( const AiPendingLookAtPoint &lookAtPoint, unsigned timeoutPeriod ) {
		return movementModule.SetPendingLookAtPoint( lookAtPoint, timeoutPeriod );
	}
	inline void ResetPendingLookAtPoint() {
		movementModule.ResetPendingLookAtPoint();
	}
	inline bool HasPendingLookAtPoint() const {
		return movementModule.HasPendingLookAtPoint();
	}

	inline bool CanInterruptMovement() const {
		return movementModule.CanInterruptMovement();
	}

	const SelectedNavEntity &GetSelectedNavEntity() const {
		return selectedNavEntity;
	}

	bool NavTargetWorthRushing() const;

	bool NavTargetWorthWeaponJumping() const {
		// TODO: Implement more sophisticated logic for this and another methods
		return NavTargetWorthRushing();
	}

	// Returns a number of weapons the logic allows to be used for weapon jumping.
	// The buffer is assumed to be capable to store all implemented weapons.
	int GetWeaponsForWeaponJumping( int *weaponNumsBuffer );

	const SelectedNavEntity &GetOrUpdateSelectedNavEntity();

	const SelectedEnemies &GetSelectedEnemies() const { return selectedEnemies; }

	const Danger *PrimaryHazard() const { return perceptionManager.PrimaryDanger(); }

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
