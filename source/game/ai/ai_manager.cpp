#include "ai_manager.h"
#include "ai_base_planner.h"
#include "ai_base_team.h"
#include "bot_evolution_manager.h"
#include "ai_shutdown_hooks_holder.h"
#include "bot.h"
#include "tactical_spots_registry.h"

// Class static variable declaration
AiManager *AiManager::instance = nullptr;

// Actual instance location in memory
static StaticVector<AiManager, 1> instanceHolder;

void AiManager::Init( const char *gametype, const char *mapname ) {
	if( instance ) {
		AI_FailWith( "AiManager::Init()", "An instance is already present\n" );
	}

	AiBaseTeam::Init();

	new( instanceHolder.unsafe_grow_back() )AiManager( gametype, mapname );
	instance = &instanceHolder.front();

	BotEvolutionManager::Init();
}

void AiManager::Shutdown() {
	BotEvolutionManager::Shutdown();

	AiBaseTeam::Shutdown();

	if( instance ) {
		instance = nullptr;
	}

	instanceHolder.clear();
}

template <typename T, unsigned N>
T* AiManager::StringValueMap<T, N>::Get( const char *key ) {
	for( std::pair<const char *, T> &pair: keyValuePairs ) {
		if( !Q_stricmp( key, pair.first ) ) {
			return &pair.second;
		}
	}

	return nullptr;
}

template <typename T, unsigned N>
bool AiManager::StringValueMap<T, N>::Insert( const char *key, T &&value ) {
	for( std::pair<const char *, T> &pair: keyValuePairs )
		if( !Q_stricmp( key, pair.first ) ) {
			return false;
		}

	// Caller logic should not allow this. The following code is only an assertion for debug builds.
#ifdef _DEBUG
	if( IsFull() ) {
		AI_FailWith( "AiManager::StringValueMap::Insert()", "Capacity has been exceeded\n" );
	}
#endif

	keyValuePairs.emplace_back( std::make_pair( key, std::move( value ) ) );
	return true;
}

#define REGISTER_BUILTIN_GOAL( goal ) this->RegisterBuiltinGoal(#goal )
#define REGISTER_BUILTIN_ACTION( action ) this->RegisterBuiltinAction(#action )

AiManager::AiManager( const char *gametype, const char *mapname )
	: last( nullptr ), cpuQuotaOwner( nullptr ), cpuQuotaGivenAt( 0 ) {
	std::fill_n( teams, MAX_CLIENTS, TEAM_SPECTATOR );

	REGISTER_BUILTIN_GOAL( BotGrabItemGoal );
	REGISTER_BUILTIN_GOAL( BotKillEnemyGoal );
	REGISTER_BUILTIN_GOAL( BotRunAwayGoal );
	REGISTER_BUILTIN_GOAL( BotReactToDangerGoal );
	REGISTER_BUILTIN_GOAL( BotReactToThreatGoal );
	REGISTER_BUILTIN_GOAL( BotReactToEnemyLostGoal );
	REGISTER_BUILTIN_GOAL( BotAttackOutOfDespairGoal );
	REGISTER_BUILTIN_GOAL( BotRoamGoal );

	// Do not clear built-in goals later
	registeredGoals.MarkClearLimit();

	REGISTER_BUILTIN_ACTION( BotGenericRunToItemAction );
	REGISTER_BUILTIN_ACTION( BotPickupItemAction );
	REGISTER_BUILTIN_ACTION( BotWaitForItemAction );

	REGISTER_BUILTIN_ACTION( BotKillEnemyAction );
	REGISTER_BUILTIN_ACTION( BotAdvanceToGoodPositionAction );
	REGISTER_BUILTIN_ACTION( BotRetreatToGoodPositionAction );
	REGISTER_BUILTIN_ACTION( BotSteadyCombatAction );
	REGISTER_BUILTIN_ACTION( BotGotoAvailableGoodPositionAction );
	REGISTER_BUILTIN_ACTION( BotAttackFromCurrentPositionAction );

	REGISTER_BUILTIN_ACTION( BotGenericRunAvoidingCombatAction );
	REGISTER_BUILTIN_ACTION( BotStartGotoCoverAction );
	REGISTER_BUILTIN_ACTION( BotTakeCoverAction );
	REGISTER_BUILTIN_ACTION( BotStartGotoRunAwayTeleportAction );
	REGISTER_BUILTIN_ACTION( BotDoRunAwayViaTeleportAction );
	REGISTER_BUILTIN_ACTION( BotStartGotoRunAwayJumppadAction );
	REGISTER_BUILTIN_ACTION( BotDoRunAwayViaJumppadAction );
	REGISTER_BUILTIN_ACTION( BotStartGotoRunAwayElevatorAction );
	REGISTER_BUILTIN_ACTION( BotDoRunAwayViaElevatorAction );
	REGISTER_BUILTIN_ACTION( BotStopRunningAwayAction );

	REGISTER_BUILTIN_ACTION( BotDodgeToSpotAction );

	REGISTER_BUILTIN_ACTION( BotTurnToThreatOriginAction );

	REGISTER_BUILTIN_ACTION( BotTurnToLostEnemyAction );
	REGISTER_BUILTIN_ACTION( BotStartLostEnemyPursuitAction );
	REGISTER_BUILTIN_ACTION( BotStopLostEnemyPursuitAction );

	// Do not clear builtin actions later
	registeredActions.MarkClearLimit();
}

void AiManager::NavEntityReachedBy( const NavEntity *navEntity, const Ai *grabber ) {
	if( !navEntity ) {
		return;
	}

	// find all bots which have this node as goal and tell them their goal is reached
	for( ai_handle_t *ai = last; ai; ai = ai->prev ) {
		if( ai->type == AI_INACTIVE ) {
			continue;
		}

		ai->aiRef->OnNavEntityReachedBy( navEntity, grabber );
	}
}

void AiManager::NavEntityReachedSignal( const edict_t *ent ) {
	if( !last ) {
		return;
	}

	// find all bots which have this node as goal and tell them their goal is reached
	for( ai_handle_t *ai = last; ai; ai = ai->prev ) {
		if( ai->type == AI_INACTIVE ) {
			continue;
		}

		ai->aiRef->OnEntityReachedSignal( ent );
	}
}

void AiManager::OnBotJoinedTeam( edict_t *ent, int team ) {
	const int entNum = ENTNUM( ent );
	const int oldTeam = teams[entNum];
	if( oldTeam != team ) {
		if( oldTeam != TEAM_SPECTATOR ) {
			AiBaseTeam::GetTeamForNum( oldTeam )->RemoveBot( ent->ai->botRef );
		}
		if( team != TEAM_SPECTATOR ) {
			AiBaseTeam::GetTeamForNum( team )->AddBot( ent->ai->botRef );
		}
		teams[entNum] = team;
	}
}

void AiManager::OnBotDropped( edict_t *ent ) {
	const int entNum = ENTNUM( ent );
	const int oldTeam = teams[entNum];
	if( oldTeam != TEAM_SPECTATOR ) {
		AiBaseTeam::GetTeamForNum( oldTeam )->RemoveBot( ent->ai->botRef );
	}
	teams[entNum] = TEAM_SPECTATOR;
}

void AiManager::LinkAi( ai_handle_t *ai ) {
	ai->next = nullptr;
	if( last != nullptr ) {
		ai->prev = last;
		last->next = ai;
	} else {
		ai->prev = nullptr;
	}
	last = ai;
}

void AiManager::UnlinkAi( ai_handle_t *ai ) {
	ai_handle_t *next = ai->next;
	ai_handle_t *prev = ai->prev;

	ai->next = nullptr;
	ai->prev = nullptr;
	// If the cell is not the last in chain
	if( next ) {
		next->prev = prev;
		if( prev ) {
			prev->next = next;
		}
	} else {
		// A cell before the last in chain should become the last one
		if( prev ) {
			prev->next = nullptr;
			last = prev;
		} else {
			last = nullptr;
		}
	}

	// All links related to the unlinked AI become invalid.
	// Reset CPU quota cycling state to prevent use-after-free.
	if( ai == cpuQuotaOwner ) {
		cpuQuotaOwner = nullptr;
	}
}

void AiManager::RegisterEvent( const edict_t *ent, int event, int parm ) {
	for( ai_handle_t *ai = last; ai; ai = ai->prev ) {
		if( ai->botRef ) {
			ai->botRef->RegisterEvent( ent, event, parm );
		}
	}
}

static struct { const char *name; const char *model; } botCharacters[] = {
	{ "Viciious",   "bigvic" },
	{ "Sid",        "bigvic" },
	{ "Pervert",    "bigvic" },
	{ "Sick",       "bigvic" },
	{ "Punk",       "bigvic" },

	{ "Black Sis",  "monada" },
	{ "Monada",     "monada" },
	{ "Afrodita",   "monada" },
	{ "Goddess",    "monada" },
	{ "Athena",     "monada" },

	{ "Silver",     "silverclas" },
	{ "Cathy",      "silverclaw" },
	{ "MishiMishi", "silverclaw" },
	{ "Lobita",     "silverclaw" },
	{ "SisterClaw", "silverclaw" },

	{ "Padpork",    "padpork" },
	{ "Jason",      "padpork" },
	{ "Lord Hog",   "padpork" },
	{ "Porkalator", "padpork" },
	{ "Babe",       "padpork" },

	{ "YYZ2112",    "bobot" },
	{ "01011001",   "bobot" },
	{ "Sector",     "bobot" },
	{ "%APPDATA%",  "bobot" },
	{ "P.E.#1",     "bobot" },
};

static constexpr int BOT_CHARACTERS_COUNT = sizeof( botCharacters ) / sizeof( botCharacters[0] );

void AiManager::CreateUserInfo( char *buffer, size_t bufferSize ) {
	// Try to avoid bad distribution, otherwise some bots are selected too often. Weights are prime numbers
	int characterIndex = ( (int)( 3 * random() + 11 * random() +  97 * random() + 997 * random() ) ) % BOT_CHARACTERS_COUNT;

	memset( buffer, 0, bufferSize );

	Info_SetValueForKey( buffer, "name", botCharacters[characterIndex].name );
	Info_SetValueForKey( buffer, "model", botCharacters[characterIndex].model );
	Info_SetValueForKey( buffer, "skin", "default" );
	Info_SetValueForKey( buffer, "hand", va( "%i", (int)( random() * 2.5 ) ) );
	const char *color = va( "%i %i %i", (uint8_t)( random() * 255 ), (uint8_t)( random() * 255 ), (uint8_t)( random() * 255 ) );
	Info_SetValueForKey( buffer, "color", color );
}

edict_t * AiManager::ConnectFakeClient() {
	char userInfo[MAX_INFO_STRING];
	static char fakeSocketType[] = "loopback";
	static char fakeIP[] = "127.0.0.1";
	CreateUserInfo( userInfo, sizeof( userInfo ) );
	int entNum = trap_FakeClientConnect( userInfo, fakeSocketType, fakeIP );
	if( entNum < 1 ) {
		Com_Printf( "AI: Can't spawn the fake client\n" );
		return nullptr;
	}
	return game.edicts + entNum;
}

void AiManager::RespawnBot( edict_t *self ) {
	if( AI_GetType( self->ai ) != AI_ISBOT ) {
		return;
	}

	BotEvolutionManager::Instance()->OnBotRespawned( self );

	self->ai->botRef->OnRespawn();

	VectorClear( self->r.client->ps.pmove.delta_angles );
	self->r.client->level.last_activity = level.time;
}

static float MakeRandomBotSkillByServerSkillLevel() {
	float skillLevel = trap_Cvar_Value( "sv_skilllevel" ); // 0 = easy, 2 = hard
	skillLevel += random(); // so we have a float between 0 and 3 meaning the server skill
	skillLevel /= 3.0f;
	if( skillLevel < 0.1f ) {
		skillLevel = 0.1f;
	} else if( skillLevel > 1.0f ) { // Won't happen?
		skillLevel = 1.0f;
	}
	return skillLevel;
}

static void BOT_JoinPlayers( edict_t *self ) {
	G_Teams_JoinAnyTeam( self, true );
	self->think = nullptr;
}

bool AiManager::CheckCanSpawnBots() {
	if( level.spawnedTimeStamp + 5000 > game.realtime || !level.canSpawnEntities ) {
		return false;
	}

	if( !AiAasWorld::Instance()->IsLoaded() || !TacticalSpotsRegistry::Instance()->IsLoaded() ) {
		Com_Printf( "AI: Can't spawn bots without a valid navigation file\n" );
		if( g_numbots->integer ) {
			trap_Cvar_Set( "g_numbots", "0" );
		}

		return false;
	}

	return true;
}

void AiManager::SetupClientBot( edict_t *ent ) {
	// We have to determine skill level early, since G_SpawnAI calls Bot constructor that requires it as a constant
	float skillLevel;

	// Always use the same skill for bots that are subject of evolution
	if( ai_evolution->integer ) {
		skillLevel = 0.75f;
	} else {
		skillLevel = MakeRandomBotSkillByServerSkillLevel();
	}

	// This also does an increment of game.numBots
	G_SpawnAI( ent, skillLevel );

	//init this bot
	ent->think = nullptr;
	ent->nextThink = level.time + 1;
	ent->ai->type = AI_ISBOT;
	ent->classname = "bot";
	ent->die = player_die;

	G_Printf( "%s skill %i\n", ent->r.client->netname, (int) ( skillLevel * 100 ) );
}

void AiManager::SetupBotTeam( edict_t *ent, const char *teamName ) {
	int team = GS_Teams_TeamFromName( teamName );
	if( team != -1 && team > TEAM_PLAYERS ) {
		// Join specified team immediately
		G_Teams_JoinTeam( ent, team );
	} else {
		//stay as spectator, give random time for joining
		ent->think = BOT_JoinPlayers;
		ent->nextThink = level.time + 500 + (unsigned)( random() * 2000 );
	}
}

void AiManager::SpawnBot( const char *teamName ) {
	if( CheckCanSpawnBots() ) {
		if( edict_t *ent = ConnectFakeClient() ) {
			SetupClientBot( ent );
			RespawnBot( ent );
			SetupBotTeam( ent, teamName );
			SetupBotGoalsAndActions( ent );
			BotEvolutionManager::Instance()->OnBotConnected( ent );
			game.numBots++;
		}
	}
}

void AiManager::RemoveBot( const char *name ) {
	// Do not iterate over the linked list of bots since it is implicitly modified by these calls
	for( edict_t *ent = game.edicts + gs.maxclients; PLAYERNUM( ent ) >= 0; ent-- ) {
		if( !Q_stricmp( ent->r.client->netname, name ) ) {
			trap_DropClient( ent, DROP_TYPE_GENERAL, nullptr );
			OnBotDropped( ent );
			G_FreeAI( ent );
			game.numBots--;
			return;
		}
	}
	G_Printf( "BOT: %s not found\n", name );
}

void AiManager::AfterLevelScriptShutdown() {
	// Do not iterate over the linked list of bots since it is implicitly modified by these calls
	for( edict_t *ent = game.edicts + gs.maxclients; PLAYERNUM( ent ) >= 0; ent-- ) {
		if( !ent->r.inuse || AI_GetType( ent->ai ) != AI_ISBOT ) {
			continue;
		}

		trap_DropClient( ent, DROP_TYPE_GENERAL, nullptr );
		OnBotDropped( ent );
		G_FreeAI( ent );
		game.numBots--;
	}
}

void AiManager::BeforeLevelScriptShutdown() {
	BotEvolutionManager::Instance()->SaveEvolutionResults();
}

// We have to sanitize all input values since these methods are exported to scripts

void AiManager::RegisterScriptGoal( const char *goalName, void *factoryObject, unsigned updatePeriod ) {
	if( registeredGoals.IsFull() ) {
		Debug( S_COLOR_RED "RegisterScriptGoal(): can't register the %s goal (too many goals)\n", goalName );
		return;
	}

	GoalProps goalProps( goalName, factoryObject, updatePeriod );
	// Ensure map key valid lifetime, use GoalProps::name
	if( !registeredGoals.Insert( goalProps.name, std::move( goalProps ) ) ) {
		Debug( S_COLOR_RED "RegisterScriptGoal(): goal %s is already registered\n", goalName );
	}
}

void AiManager::RegisterScriptAction( const char *actionName, void *factoryObject ) {
	if( registeredActions.IsFull() ) {
		Debug( S_COLOR_RED "RegisterScriptAction(): can't register the %s action (too many actions)\n", actionName );
		return;
	}

	ActionProps actionProps( actionName, factoryObject );
	// Ensure map key valid lifetime, user ActionProps::name
	if( !registeredActions.Insert( actionProps.name, std::move( actionProps ) ) ) {
		Debug( S_COLOR_RED "RegisterScriptAction(): action %s is already registered\n", actionName );
	}
}

void AiManager::AddApplicableAction( const char *goalName, const char *actionName ) {
	ActionProps *actionProps = registeredActions.Get( actionName );
	if( !actionProps ) {
		Debug( S_COLOR_RED "AddApplicableAction(): action %s has not been registered\n", actionName );
		return;
	}

	GoalProps *goalProps = registeredGoals.Get( goalName );
	if( !goalProps ) {
		Debug( S_COLOR_RED "AddApplicableAction(): goal %s has not been registered\n", goalName );
		return;
	}

	if( !actionProps->factoryObject && !goalProps->factoryObject ) {
		const char *format = S_COLOR_RED
							 "AddApplicableAction(): both goal %s and action %s are builtin "
							 "(builtin action/goal relations are hardcoded for performance)\n";
		Debug( format, goalName, actionName );
		return;
	}

	if( goalProps->numApplicableActions == MAX_ACTIONS ) {
		Debug( S_COLOR_RED "AddApplicableAction(): too many actions have already been registered\n" );
		return;
	}

	// Ensure that applicable action name has valid lifetime, use ActionProps::name
	goalProps->applicableActions[goalProps->numApplicableActions++] = actionProps->name;
}

void AiManager::RegisterBuiltinGoal( const char *goalName ) {
	if( registeredGoals.IsFull() ) {
		AI_FailWith( "AiManager::RegisterBuiltinGoal()", "Too many registered goals" );
	}

	GoalProps goalProps( goalName, nullptr, 0 );
	// Ensure map key valid lifetime, user GoalProps::name
	if( !registeredGoals.Insert( goalProps.name, std::move( goalProps ) ) ) {
		AI_FailWith( "AiManager::RegisterBuiltinGoal()", "The goal %s is already registered", goalName );
	}
}

void AiManager::RegisterBuiltinAction( const char *actionName ) {
	if( registeredActions.IsFull() ) {
		AI_FailWith( "AiManager::RegisterBuiltinAction()", "Too many registered actions" );
	}

	ActionProps actionProps( actionName, nullptr );
	// Ensure map key valid lifetime, use ActionProps::name.
	if( !registeredActions.Insert( actionProps.name, std::move( actionProps ) ) ) {
		AI_FailWith( "AiManager::RegisterBuiltinAction()", "The action %s is already registered", actionName );
	}
}

void AiManager::SetupBotGoalsAndActions( edict_t *ent ) {
#ifdef _DEBUG
	// Make sure all builtin goals and actions have been registered
	bool wereErrors = false;
	for( const auto *goal: ent->ai->botRef->botPlanner.goals ) {
		if( !registeredGoals.Get( goal->Name() ) ) {
			Debug( S_COLOR_RED "Builtin goal %s has not been registered\n", goal->Name() );
			wereErrors = true;
		}
	}
	for( const auto *action: ent->ai->botRef->botPlanner.actions ) {
		if( !registeredActions.Get( action->Name() ) ) {
			Debug( S_COLOR_RED "Builtin action %s has not been registered\n", action->Name() );
			wereErrors = true;
		}
	}
	if( wereErrors ) {
		AI_FailWith( "AiManager::SetupBotGoalsAndActions()", "There were errors\n" );
	}
#endif

	for( auto &goalPropsAndName: registeredGoals ) {
		GoalProps &goalProps = goalPropsAndName.second;
		// If the goal is builtin
		BotBaseGoal *goal;
		if( !goalProps.factoryObject ) {
			goal = ent->ai->botRef->GetGoalByName( goalProps.name );
		} else {
			// Allocate a persistent memory chunk but not initialize it.
			// GENERIC_asInstantiateGoal() expects a persistent memory address for a native object reference.
			// BotScriptGoal constructor expects a persistent script object address too.
			// We defer BotScriptGoal constructor call to break this loop.
			// GENERIC_asInstantiateGoal() script counterpart be aware that the native object is not constructed yet.
			// That's why an additional entity argument is provided to access the owner instead of using scriptGoal fields.
			BotScriptGoal *scriptGoal = ent->ai->botRef->AllocScriptGoal();
			void *goalObject = GENERIC_asInstantiateGoal( goalProps.factoryObject, ent, scriptGoal );
			goal = new(scriptGoal)BotScriptGoal( ent->ai->aiRef, goalProps.name, goalProps.updatePeriod, goalObject );
		}

		for( unsigned i = 0; i < goalProps.numApplicableActions; ++i ) {
			const char *actionName = goalProps.applicableActions[i];
			ActionProps *actionProps = registeredActions.Get( actionName );
			BotBaseAction *action;
			// If the action is builtin
			if( !actionProps->factoryObject ) {
				action = ent->ai->botRef->GetActionByName( actionProps->name );
			} else {
				// See the explanation above related to a script goal, this is a similar case
				BotScriptAction *scriptAction = ent->ai->botRef->AllocScriptAction();
				void *actionObject = GENERIC_asInstantiateAction( actionProps->factoryObject, ent, scriptAction );
				action = new(scriptAction)BotScriptAction( ent->ai->aiRef, goalProps.name, actionObject );
			}
			goal->AddExtraApplicableAction( action );
		}
	}
}

void AiManager::Frame() {
	UpdateCpuQuotaOwner();

	if( !GS_TeamBasedGametype() ) {
		AiBaseTeam::GetTeamForNum( TEAM_PLAYERS )->Update();
		return;
	}

	for( int team = TEAM_ALPHA; team < GS_MAX_TEAMS; ++team ) {
		AiBaseTeam::GetTeamForNum( team )->Update();
	}
}

void AiManager::FindHubAreas() {
	const auto *aasWorld = AiAasWorld::Instance();
	if( !aasWorld->IsLoaded() ) {
		return;
	}

	StaticVector<AreaAndScore, sizeof( hubAreas ) / sizeof( *hubAreas )> bestAreasHeap;
	for( int i = 1; i < aasWorld->NumAreas(); ++i ) {
		const auto &areaSettings = aasWorld->AreaSettings()[i];
		if( !( areaSettings.areaflags & AREA_GROUNDED ) ) {
			continue;
		}
		if( areaSettings.areaflags & AREA_DISABLED ) {
			continue;
		}
		if( areaSettings.contents & ( AREACONTENTS_DONOTENTER | AREACONTENTS_LAVA | AREACONTENTS_SLIME | AREACONTENTS_WATER ) ) {
			continue;
		}

		// Reject degenerate areas, pass only relatively large areas
		const auto &area = aasWorld->Areas()[i];
		if( area.maxs[0] - area.mins[0] < 56.0f ) {
			continue;
		}
		if( area.maxs[1] - area.mins[1] < 56.0f ) {
			continue;
		}

		// Count as useful only several kinds of reachabilities
		int usefulReachCount = 0;
		int reachNum = areaSettings.firstreachablearea;
		int lastReachNum = areaSettings.firstreachablearea + areaSettings.numreachableareas - 1;
		while( reachNum <= lastReachNum ) {
			const auto &reach = aasWorld->Reachabilities()[reachNum];
			if( reach.traveltype == TRAVEL_WALK || reach.traveltype == TRAVEL_WALKOFFLEDGE ) {
				usefulReachCount++;
			}
			++reachNum;
		}

		// Reject early to avoid more expensive call to push_heap()
		if( !usefulReachCount ) {
			continue;
		}

		bestAreasHeap.push_back( AreaAndScore( i, usefulReachCount ) );
		std::push_heap( bestAreasHeap.begin(), bestAreasHeap.end() );

		// bestAreasHeap size should be always less than its capacity:
		// 1) to ensure that there is a free room for next area;
		// 2) to ensure that hubAreas capacity will not be exceeded.
		if( bestAreasHeap.size() == bestAreasHeap.capacity() ) {
			std::pop_heap( bestAreasHeap.begin(), bestAreasHeap.end() );
			bestAreasHeap.pop_back();
		}
	}

	std::sort( bestAreasHeap.begin(), bestAreasHeap.end() );

	for( int i = 0; i < bestAreasHeap.size(); ++i ) {
		this->hubAreas[i] = bestAreasHeap[i].areaNum;
	}

	this->numHubAreas = (int)bestAreasHeap.size();
}

bool AiManager::IsAreaReachableFromHubAreas( int targetArea, float *score ) const {
	if( !targetArea ) {
		return false;
	}

	if( !this->numHubAreas ) {
		const_cast<AiManager *>( this )->FindHubAreas();
	}

	const auto *routeCache = AiAasRouteCache::Shared();
	int numReach = 0;
	float scoreSum = 0.0f;
	for( int i = 0; i < numHubAreas; ++i ) {
		if( routeCache->ReachabilityToGoalArea( hubAreas[i], targetArea, Bot::ALLOWED_TRAVEL_FLAGS ) ) {
			numReach++;
			// Give first (and best) areas greater score
			scoreSum += ( numHubAreas - i ) / (float)numHubAreas;
			// That's enough, stop wasting CPU cycles
			if( numReach == 4 ) {
				if( score ) {
					*score = scoreSum;
				}
				return true;
			}
		}
	}

	if ( score ) {
		*score = scoreSum;
	}

	return numReach > 0;
}

void AiManager::UpdateCpuQuotaOwner() {
	if( !cpuQuotaOwner ) {
		cpuQuotaOwner = last;
		return;
	}

	const auto *const oldQuotaOwner = cpuQuotaOwner;
	// Start from the next AI in list
	cpuQuotaOwner = cpuQuotaOwner->prev;
	// Scan all bots that are after the current owner in the list
	while( cpuQuotaOwner ) {
		// Stop on the first bot that is in-game
		if( !cpuQuotaOwner->aiRef->IsGhosting() ) {
			break;
		}
		cpuQuotaOwner = cpuQuotaOwner->prev;
	}

	// If the scan has not reached the list end
	if( cpuQuotaOwner ) {
		return;
	}

	// Rewind to the list head
	cpuQuotaOwner = last;

	// Scan all bots that is before the current owner in the list
	// Keep the current owner if there is no in-game bots before
	while( cpuQuotaOwner && cpuQuotaOwner != oldQuotaOwner ) {
		// Stop of the first bot that is in game
		if( !cpuQuotaOwner->aiRef->IsGhosting() ) {
			break;
		}
		cpuQuotaOwner = cpuQuotaOwner->prev;
	}

	// If the loop execution has not been interrupted by break,
	// quota owner remains the same as before this call.
	// This means a bot always gets a quota if there is no other active bots in game.
}

bool AiManager::TryGetExpensiveComputationQuota( const edict_t *ent ) {
	if( !ent->ai ) {
		return false;
	}

	if( ent->ai != cpuQuotaOwner ) {
		return false;
	}

	// Allow expensive computations only once per frame
	if( cpuQuotaGivenAt == level.time ) {
		return false;
	}

	// Mark it
	cpuQuotaGivenAt = level.time;
	return true;
}