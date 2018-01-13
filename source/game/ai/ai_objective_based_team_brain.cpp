#include "ai_ground_trace_cache.h"
#include "ai_objective_based_team_brain.h"
#include "ai_aas_route_cache.h"
#include "bot.h"

template <typename Container, typename T>
inline int AiObjectiveBasedTeamBrain::AddItem( const char *name, Container &c, T &&item ) {
	if( item.id < 0 ) {
		G_Printf( S_COLOR_YELLOW "%s has illegal id %d < 0", name, item.id );
		return -1;
	}
	for( unsigned i = 0, end = c.size(); i < end; ++i ) {
		if( c[i].id == item.id ) {
			G_Printf( S_COLOR_YELLOW "%s (id=%d) is already present\n", name, item.id );
			return -1;
		}
	}
	// Check for duplicates first, check capacity only after that.
	if( c.size() == c.capacity() ) {
		G_Printf( S_COLOR_YELLOW "Can't add %s (id=%d): too many %s's\n", name, item.id, name );
		return -1;
	}
	c.push_back( item );
	return (int)( c.size() - 1 );
};

template <typename Container, typename OnRemoved>
inline int AiObjectiveBasedTeamBrain::RemoveItem( const char *name, Container &c, int id, OnRemoved onRemoved ) {
	for( unsigned i = 0, end = c.size(); i < end; ++i ) {
		if( c[i].id == id ) {
			onRemoved( &c[i] );
			c.erase( c.begin() + i );
			ResetAllBotsOrders();
			return (int)i;
		}
	}
	G_Printf( S_COLOR_YELLOW "%s (id=%d) cannot be found\n", name, id );
	return -1;
}

void AiObjectiveBasedTeamBrain::AddDefenceSpot( const AiDefenceSpot &spot ) {
	int slot = AddItem( "DefenceSpot", defenceSpots, DefenceSpot( spot ) );
	if( slot >= 0 ) {
		EnableDefenceSpotAutoAlert( &defenceSpots[slot] );
	}
}

void AiObjectiveBasedTeamBrain::RemoveDefenceSpot( int id ) {
	RemoveItem( "DefenceSpot", defenceSpots, id, [&]( DefenceSpot *s ) {
		OnDefenceSpotRemoved( s );
	} );
}

void AiObjectiveBasedTeamBrain::AddOffenseSpot( const AiOffenseSpot &spot ) {
	AddItem( "OffenceSpot", offenseSpots, OffenseSpot( spot ) );
}

void AiObjectiveBasedTeamBrain::RemoveOffenseSpot( int id ) {
	RemoveItem( "OffenceSpot", offenseSpots, id, [&]( OffenseSpot *s ) {
		OnOffenseSpotRemoved( s );
	} );
}

void AiObjectiveBasedTeamBrain::ClearExternalEntityWeights( const edict_t *ent ) {
	// TODO: AiBaseTeamBrain should maintain a list of its bots
	for( int i = 0; i < gs.maxclients; ++i ) {
		edict_t *player = PLAYERENT( i );
		if( !player->r.inuse || !player->ai || !player->ai->botRef ) {
			continue;
		}
		if( player->r.client->team != this->team ) {
			continue;
		}
		player->ai->botRef->OverrideEntityWeight( ent, 0.0f );
	}
}

void AiObjectiveBasedTeamBrain::SetDefenceSpotAlert( int id, float alertLevel, unsigned timeoutPeriod ) {
	for( unsigned i = 0; i < defenceSpots.size(); ++i ) {
		if( defenceSpots[i].id == id ) {
			clamp( alertLevel, 0.0f, 1.0f );
			defenceSpots[i].alertLevel = alertLevel;
			defenceSpots[i].alertTimeoutAt = level.time + timeoutPeriod;
			return;
		}
	}
	G_Printf( S_COLOR_YELLOW "Can't find a DefenceSpot (id=%d)\n", id );
}

AiAlertSpot AiObjectiveBasedTeamBrain::DefenceSpot::ToAlertSpot() const {
	AiAlertSpot result( id, Vec3( entity->s.origin ), radius );
	result.regularEnemyInfluenceScale = regularEnemyAlertScale;
	result.carrierEnemyInfluenceScale = carrierEnemyAlertScale;
	return result;
}

void AiObjectiveBasedTeamBrain::EnableDefenceSpotAutoAlert( DefenceSpot *defenceSpot ) {
	AiAlertSpot alertSpot( defenceSpot->ToAlertSpot() );
	// TODO: Track a list of all bots in AiBaseTeamBrain
	for( int i = 1; i <= gs.maxclients; ++i ) {
		edict_t *ent = game.edicts + i;
		if( !ent->ai || !ent->ai->botRef ) {
			continue;
		}
		// If an entity is an AI, it is a client.
		if( ent->r.client->team != this->team ) {
			continue;
		}
		ent->ai->botRef->EnableAutoAlert( alertSpot, AlertCallback, this );
	}
	defenceSpot->usesAutoAlert = true;
}

void AiObjectiveBasedTeamBrain::DisableDefenceSpotAutoAlert( DefenceSpot *defenceSpot ) {
	for( int i = 1; i <= gs.maxclients; ++i ) {
		edict_t *ent = game.edicts + i;
		if( !ent->ai || !ent->ai->botRef ) {
			continue;
		}
		if( ent->r.client->team != this->team ) {
			continue;
		}
		ent->ai->botRef->DisableAutoAlert( defenceSpot->id );
	}
	defenceSpot->usesAutoAlert = false;
}

void AiObjectiveBasedTeamBrain::AlertCallback( void *receiver, Bot *bot, int id, float alertLevel ) {
	( (AiObjectiveBasedTeamBrain*)receiver )->OnAlertReported( bot, id, alertLevel );
}

void AiObjectiveBasedTeamBrain::OnAlertReported( Bot *bot, int id, float alertLevel ) {
	for( unsigned i = 0; i < defenceSpots.size(); ++i ) {
		if( defenceSpots[i].id == id ) {
			// Several bots in team may not realize real alert level
			// (in alert reporting "fair" bot vision is used, and bot may have missed other attackers)

			float oldAlertLevel = defenceSpots[i].alertLevel;
			// If reported alert level is greater than the current one, always override the current level
			if( defenceSpots[i].alertLevel <= alertLevel ) {
				defenceSpots[i].alertLevel = alertLevel;
			}
			// Otherwise override the current level only when last report is dated and has almost expired
			else if( defenceSpots[i].alertTimeoutAt < level.time + 150 ) {
				defenceSpots[i].alertLevel = alertLevel;
			}

			// Keep alert state if an alert is present
			// Note: bots may (and usually do) report zero alert level.
			if( alertLevel > 0 ) {
				defenceSpots[i].alertTimeoutAt = level.time + 1000;
			}

			if( oldAlertLevel + 0.3f < alertLevel ) {
				// TODO: Precache
				int locationTag = G_MapLocationTAGForOrigin( defenceSpots[i].entity->s.origin );
				if( !locationTag ) {
					G_Say_Team( bot->Self(), S_COLOR_RED "An enemy is incoming!!!", false );
				} else {
					char location[MAX_CONFIGSTRING_CHARS];
					G_MapLocationNameForTAG( locationTag, location, MAX_CONFIGSTRING_CHARS );
					char *msg = va( S_COLOR_RED "An enemy is @ %s" S_COLOR_RED "!!!", location );
					G_Say_Team( bot->Self(), msg, false );
				}
			}

			return;
		}
	}
	// Since alert reports are not scriptable, the native code should abort on error.
	FailWith( "OnAlertReported(): Can't find a DefenceSpot (id=%d)\n", id );
}

void AiObjectiveBasedTeamBrain::OnBotAdded( Bot *bot ) {
	AiSquadBasedTeamBrain::OnBotAdded( bot );

	for( auto &spot: defenceSpots )
		if( spot.usesAutoAlert ) {
			bot->EnableAutoAlert( spot.ToAlertSpot(), AlertCallback, this );
		}
}

void AiObjectiveBasedTeamBrain::OnBotRemoved( Bot *bot ) {
	AiSquadBasedTeamBrain::OnBotRemoved( bot );

	ResetBotOrders( bot );

	for( const auto &spot: defenceSpots )
		if( spot.usesAutoAlert ) {
			bot->DisableAutoAlert( spot.id );
		}
}

void AiObjectiveBasedTeamBrain::Think() {
	// Call super method first, it contains an obligatory logic
	AiSquadBasedTeamBrain::Think();

	Candidates candidates;
	FindAllCandidates( candidates );

	// First reset all candidates statuses to default values
	for( auto &botAndScore: candidates )
		ResetBotOrders( botAndScore.bot->ai->botRef );

	AssignDefenders( candidates );
	AssignAttackers( candidates );

	for( unsigned i = 0; i < defenceSpots.size(); ++i )
		UpdateDefendersStatus( i );

	for( unsigned spotNum = 0; spotNum < offenseSpots.size(); ++spotNum )
		UpdateAttackersStatus( spotNum );

	// Other candidates should support a carrier
	if( const edict_t *carrier = FindCarrier() ) {
		SetSupportCarrierOrders( carrier, candidates );
	}
}

void AiObjectiveBasedTeamBrain::ResetBotOrders( Bot *bot ) {
	bot->ClearDefenceAndOffenceSpots();
	for( const auto &defenceSpot: defenceSpots )
		bot->OverrideEntityWeight( defenceSpot.entity, 0.0f );
	for( const auto &offenceSpot: offenseSpots )
		bot->OverrideEntityWeight( offenceSpot.entity, 0.0f );
	bot->SetBaseOffensiveness( 0.5f );
	for( int i = 1; i <= gs.maxclients; ++i )
		bot->OverrideEntityWeight( game.edicts + i, 0.0f );
}

void AiObjectiveBasedTeamBrain::ResetAllBotsOrders() {
	for( int i = 0; i <= gs.maxclients; ++i ) {
		edict_t *ent = game.edicts + i;
		if( !ent->r.inuse || !ent->ai || !ent->ai->botRef ) {
			continue;
		}
		ResetBotOrders( ent->ai->botRef );
	}
}

void AiObjectiveBasedTeamBrain::FindAllCandidates( Candidates &candidates ) {
	for( int i = 0; i <= gs.maxclients; ++i ) {
		edict_t *ent = game.edicts + i;
		if( !ent->r.inuse || !ent->ai || !ent->ai->botRef ) {
			continue;
		}
		// If an entity is an AI, it is a client too.
		if( G_ISGHOSTING( ent ) ) {
			continue;
		}
		if( ent->r.client->team != this->team ) {
			continue;
		}

		candidates.push_back( BotAndScore( ent ) );
	}
}

void AiObjectiveBasedTeamBrain::AssignDefenders( Candidates &candidates ) {
	for( unsigned i = 0; i < defenceSpots.size(); ++i )
		defenders[i].clear();

	for( auto &defenceSpot: defenceSpots ) {
		if( defenceSpot.alertTimeoutAt <= level.time ) {
			defenceSpot.alertLevel = 0.0f;
		}

		defenceSpot.weight = defenceSpot.alertLevel;
	}

	auto cmp = []( const DefenceSpot &a, const DefenceSpot &b ) {
				   return a.weight > b.weight;
			   };
	std::sort( defenceSpots.begin(), defenceSpots.end(), cmp );

	// Compute raw score of bots as defenders
	ComputeDefenceRawScore( candidates );

	unsigned extraDefendersLeft = candidates.size();
	for( const auto &spot: defenceSpots ) {
		if( extraDefendersLeft < spot.minDefenders ) {
			extraDefendersLeft = 0;
			break;
		}
		extraDefendersLeft -= spot.minDefenders;
	}

	for( unsigned spotNum = 0; spotNum < defenceSpots.size(); ++spotNum ) {
		if( candidates.empty() ) {
			break;
		}

		// Compute effective bot defender scores for i-th defence spot
		ComputeDefenceScore( candidates, spotNum );
		// Sort candidates so best candidates are last
		std::sort( candidates.begin(), candidates.end() );

		DefenceSpot &spot = defenceSpots[spotNum];
		unsigned totalDefenders = std::min( candidates.size(), spot.minDefenders );
		if( extraDefendersLeft > 0 ) {
			unsigned candidatesLeft = candidates.size() - totalDefenders;
			unsigned extraDefenders = (unsigned)( candidatesLeft * spot.weight );
			if( extraDefenders > spot.maxDefenders - totalDefenders ) {
				extraDefenders = spot.maxDefenders - totalDefenders;
			}
			totalDefenders += extraDefenders;
		}
		for( unsigned j = 0; j < totalDefenders; ++j ) {
			defenders[spotNum].push_back( candidates.back().bot );
			candidates.pop_back();
		}
	}
}

void AiObjectiveBasedTeamBrain::ComputeDefenceRawScore( Candidates &candidates ) {
	const float armorProtection = g_armor_protection->value;
	const float armorDegradation = g_armor_degradation->value;
	for( auto &botAndScore: candidates ) {
		// Be offensive having powerups
		if( HasPowerups( botAndScore.bot ) ) {
			botAndScore.rawScore = 0.001f;
		}

		float resistanceScore = DamageToKill( botAndScore.bot, armorProtection, armorDegradation );
		float weaponScore = 0.0f;
		const int *inventory = botAndScore.bot->r.client->ps.inventory;
		for( int weapon = WEAP_GUNBLADE + 1; weapon < WEAP_TOTAL; ++weapon ) {
			if( !inventory[weapon] ) {
				continue;
			}

			const auto *weaponDef = GS_GetWeaponDef( weapon );

			if( weaponDef->firedef.ammo_id != AMMO_NONE && weaponDef->firedef.ammo_max ) {
				weaponScore += inventory[weaponDef->firedef.ammo_id] / weaponDef->firedef.ammo_max;
			} else {
				weaponScore += 1.0f;
			}

			if( weaponDef->firedef_weak.ammo_id != AMMO_NONE && weaponDef->firedef_weak.ammo_max ) {
				weaponScore += inventory[weaponDef->firedef_weak.ammo_id] / weaponDef->firedef_weak.ammo_max;
			} else {
				weaponScore += 1.0f;
			}

			// TODO: Modify by weapon tier
		}
		weaponScore /= ( WEAP_TOTAL - WEAP_GUNBLADE - 1 );
		weaponScore = 1.0f / Q_RSqrt( weaponScore + 0.001f );

		botAndScore.rawScore = resistanceScore * weaponScore *
							   botAndScore.bot->ai->botRef->PlayerDefenciveAbilitiesRating();
	}
}

void AiObjectiveBasedTeamBrain::ComputeDefenceScore( Candidates &candidates, int spotNum ) {
	const float *origin = defenceSpots[spotNum].entity->s.origin;
	for( auto &botAndScore: candidates ) {
		float squareDistance = DistanceSquared( botAndScore.bot->s.origin, origin );
		float inverseDistance = Q_RSqrt( squareDistance + 0.001f );
		botAndScore.effectiveScore = botAndScore.rawScore * inverseDistance;
	}
}

void AiObjectiveBasedTeamBrain::AssignAttackers( Candidates &candidates ) {
	for( unsigned i = 0; i < offenseSpots.size(); ++i )
		attackers[i].clear();

	for( unsigned i = 0; i < offenseSpots.size(); ++i )
		offenseSpots[i].weight = 1.0f / offenseSpots.size();

	auto cmp = []( const OffenseSpot &a, const OffenseSpot &b ) {
				   return a.weight < b.weight;
			   };
	std::sort( offenseSpots.begin(), offenseSpots.end(), cmp );

	ComputeOffenceRawScore( candidates );

	unsigned extraAttackersLeft = candidates.size();
	for( const auto &spot: offenseSpots ) {
		if( extraAttackersLeft < spot.minAttackers ) {
			extraAttackersLeft = 0;
			break;
		}
		extraAttackersLeft -= spot.minAttackers;
	}

	for( unsigned spotNum = 0; spotNum < offenseSpots.size(); ++spotNum ) {
		if( candidates.empty() ) {
			break;
		}
		// Compute effective bot defender scores for i-th defence spot
		ComputeOffenceScore( candidates, spotNum );
		// Sort candidates so best candidates are last
		std::sort( candidates.begin(), candidates.end() );

		const OffenseSpot &spot = offenseSpots[spotNum];
		unsigned totalAttackers = std::min( candidates.size(), spot.minAttackers );
		if( extraAttackersLeft > 0 ) {
			unsigned candidatesLeft = candidates.size() - spot.minAttackers;
			unsigned extraAttackers = (unsigned)( offenseSpots[spotNum].weight * candidatesLeft );
			if( extraAttackers > spot.maxAttackers - totalAttackers ) {
				extraAttackers = spot.maxAttackers - totalAttackers;
			}
			totalAttackers += extraAttackers;
		}
		for( unsigned j = 0; j < totalAttackers; ++j ) {
			attackers[spotNum].push_back( candidates.back().bot );
			candidates.pop_back();
		}
	}
}

void AiObjectiveBasedTeamBrain::ComputeOffenceRawScore( Candidates &candidates ) {
	for( auto &botAndScore: candidates ) {
		float score = DamageToKill( botAndScore.bot, g_armor_protection->value, g_armor_degradation->value );
		if( HasShell( botAndScore.bot ) ) {
			score *= 4.0f;
		}
		if( HasQuad( botAndScore.bot ) ) {
			score *= 4.0f;
		}
		score *= botAndScore.bot->ai->botRef->PlayerOffenciveAbilitiesRating();
		botAndScore.rawScore = score;
	}
}

void AiObjectiveBasedTeamBrain::ComputeOffenceScore( Candidates &candidates, int spotNum ) {
	const float *origin = offenseSpots[spotNum].entity->s.origin;
	for( auto &botAndScore: candidates ) {
		float squareDistance = DistanceSquared( botAndScore.bot->s.origin, origin );
		float inverseDistance = Q_RSqrt( squareDistance + 0.001f );
		botAndScore.effectiveScore = botAndScore.rawScore * inverseDistance;
	}
}

void AiObjectiveBasedTeamBrain::UpdateDefendersStatus( unsigned defenceSpotNum ) {
	const DefenceSpot &spot = defenceSpots[defenceSpotNum];
	const float *spotOrigin = defenceSpots[defenceSpotNum].entity->s.origin;
	for( unsigned i = 0; i < defenders[defenceSpotNum].size(); ++i ) {
		edict_t *bot = defenders[defenceSpotNum][i];
		bot->ai->botRef->SetDefenceSpotId( spot.id );
		float distance = 1.0f / Q_RSqrt( 0.001f + DistanceSquared( bot->s.origin, spotOrigin ) );
		float distanceFactor = 1.0f;
		if( distance < spot.radius ) {
			if( distance < 0.33f * spot.radius ) {
				distanceFactor = 0.0f;
			} else {
				distanceFactor = distance / spot.radius;
			}
		}
		bot->ai->botRef->OverrideEntityWeight( spot.entity, 12.0f * distanceFactor );
		bot->ai->botRef->SetBaseOffensiveness( 1.0f - distanceFactor );
	}
}

void AiObjectiveBasedTeamBrain::UpdateAttackersStatus( unsigned offenceSpotNum ) {
	const edict_t *spotEnt = offenseSpots[offenceSpotNum].entity;
	for( unsigned i = 0; i < attackers[offenceSpotNum].size(); ++i ) {
		edict_t *bot = attackers[offenceSpotNum][i];
		bot->ai->botRef->SetOffenseSpotId( offenseSpots[offenceSpotNum].id );
		// If bot is not in squad, set an offence spot weight to a value of an ordinary valuable item.
		// Thus bots will not attack alone and will grab some items instead in order to prepare to attack.
		if( bot->ai->botRef->IsInSquad() ) {
			bot->ai->botRef->OverrideEntityWeight( spotEnt, 9.0f );
		} else {
			bot->ai->botRef->OverrideEntityWeight( spotEnt, 3.0f );
		}
		bot->ai->botRef->SetBaseOffensiveness( 0.0f );
	}
}

const edict_t *AiObjectiveBasedTeamBrain::FindCarrier() const {
	for( int i = 1; i <= gs.maxclients; ++i ) {
		edict_t *ent = game.edicts + i;
		if( !ent->r.inuse || !ent->r.client ) {
			continue;
		}
		if( ent->r.client->team != this->team ) {
			continue;
		}
		if( IsCarrier( ent ) ) {
			return ent;
		}
	}
	return nullptr;
}

void AiObjectiveBasedTeamBrain::SetSupportCarrierOrders( const edict_t *carrier, Candidates &candidates ) {
	float *carrierOrigin = const_cast<float *>( carrier->s.origin );
	auto *groundTraceCache = AiGroundTraceCache::Instance();
	auto *aasWorld = AiAasWorld::Instance();
	auto *routeCache = AiAasRouteCache::Shared();

	Vec3 groundedCarrierOrigin( carrierOrigin );
	groundTraceCache->TryDropToFloor( carrier, 64.0f, groundedCarrierOrigin.Data() );

	const int carrierAreaNum = aasWorld->FindAreaNum( carrierOrigin );
	if( !carrierAreaNum ) {
		for( const auto &botAndScore: candidates ) {
			if( botAndScore.bot == carrier ) {
				continue;
			}
			float *botOrigin = botAndScore.bot->s.origin;
			float squareDistance = DistanceSquared( botOrigin, carrierOrigin );
			// The carrier is too far, hurry up to support it
			if( squareDistance > 768.0f * 768.0f ) {
				botAndScore.bot->ai->botRef->OverrideEntityWeight( carrier, 9.0f );
			} else {
				botAndScore.bot->ai->botRef->OverrideEntityWeight( carrier, 4.5f );
			}
		}
		return;
	}

	const auto *pvsCache = EntitiesPvsCache::Instance();
	for( const auto &botAndScore: candidates ) {
		if( botAndScore.bot == carrier ) {
			continue;
		}
		float *botOrigin = botAndScore.bot->s.origin;
		float squareDistance = DistanceSquared( botOrigin, carrierOrigin );
		// The carrier is too far, hurry up to support it
		if( squareDistance > 768.0f * 768.0f ) {
			botAndScore.bot->ai->botRef->OverrideEntityWeight( carrier, 9.0f );
			continue;
		}

		if( !pvsCache->AreInPvs( botAndScore.bot, carrier ) ) {
			continue;
		}

		trace_t trace;
		G_Trace( &trace, carrierOrigin, nullptr, nullptr, botAndScore.bot->s.origin, botAndScore.bot, MASK_AISOLID );
		// The carrier is not visible, hurry up to support it
		if( trace.fraction != 1.0f && carrier != game.edicts + trace.ent ) {
			botAndScore.bot->ai->botRef->OverrideEntityWeight( carrier, 4.5f );
			continue;
		}
		Vec3 groundedBotOrigin( botOrigin );
		if( !groundTraceCache->TryDropToFloor( botAndScore.bot, 64.0f, groundedBotOrigin.Data() ) ) {
			botAndScore.bot->ai->botRef->OverrideEntityWeight( carrier, 4.5f );
			continue;
		}
		int botAreaNum = aasWorld->FindAreaNum( groundedBotOrigin );
		if( !botAreaNum ) {
			botAndScore.bot->ai->botRef->OverrideEntityWeight( carrier, 4.5f );
			continue;
		}
		int travelTime = routeCache->TravelTimeToGoalArea( botAreaNum, carrierAreaNum, Bot::ALLOWED_TRAVEL_FLAGS );
		// A carrier is not reachable in a short period of time
		// AAS travel time is given in seconds^-2 and lowest feasible value is 1
		if( !travelTime || travelTime > 250 ) {
			botAndScore.bot->ai->botRef->OverrideEntityWeight( carrier, 4.5f );
			continue;
		}

		// Decrease carrier weight if bot is already close to it
		float distance = 1.0f / Q_RSqrt( squareDistance );
		float distanceFactor = distance / 768.0f;
		if( distanceFactor < 0.25f ) {
			distanceFactor = 0.0f;
		}
		botAndScore.bot->ai->botRef->OverrideEntityWeight( carrier, 4.5f * distanceFactor );
	}
}
