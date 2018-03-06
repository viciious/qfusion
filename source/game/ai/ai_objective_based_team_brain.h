#ifndef QFUSION_AI_OBJECTIVE_BASED_TEAM_BRAIN_H
#define QFUSION_AI_OBJECTIVE_BASED_TEAM_BRAIN_H

#include "ai_squad_based_team_brain.h"

// Visible for script
struct AiDefenceSpot {
	int id;
	const edict_t *entity;
	float radius;
	bool usesAutoAlert;
	unsigned minDefenders;
	unsigned maxDefenders;
	float regularEnemyAlertScale;
	float carrierEnemyAlertScale;
};

// Visible for script
struct AiOffenseSpot {
	int id;
	const edict_t *entity;
	unsigned minAttackers;
	unsigned maxAttackers;
};

class AiObjectiveBasedTeamBrain : public AiSquadBasedTeamBrain
{
	static constexpr unsigned MAX_SPOT_ATTACKERS = 16;
	static constexpr unsigned MAX_SPOT_DEFENDERS = 16;

	// Extended definition based on one visible for script
	struct DefenceSpot : public AiDefenceSpot {
		float weight;
		float alertLevel;
		int64_t alertTimeoutAt;

		DefenceSpot( const AiDefenceSpot &spot )
			: AiDefenceSpot( spot ),
			weight( 0.0f ),
			alertLevel( 0.0f ),
			alertTimeoutAt( 0 ) {
			clamp_low( radius, 16.0f );
			clamp( regularEnemyAlertScale, 0.0f, 1.0f );
			clamp( carrierEnemyAlertScale, 0.0f, 1.0f );
			clamp_high( minDefenders, MAX_SPOT_DEFENDERS );
			clamp( maxDefenders, 1, MAX_SPOT_DEFENDERS );
			if( minDefenders > maxDefenders ) {
				minDefenders = maxDefenders;
			}
		}

		struct AiAlertSpot ToAlertSpot() const;
	};

	// Extended definition based on one visible for script
	struct OffenseSpot : public AiOffenseSpot {
		float weight;

		OffenseSpot( const AiOffenseSpot &spot )
			: AiOffenseSpot( spot ), weight( 0.0f ) {
			clamp_high( minAttackers, MAX_SPOT_ATTACKERS );
			clamp( maxAttackers, 1, MAX_SPOT_ATTACKERS );
			if( minAttackers > maxAttackers ) {
				minAttackers = maxAttackers;
			}
		}
	};

	// This value is chosen having the domination GT in mind
	static constexpr unsigned MAX_DEFENCE_SPOTS = 3;
	static constexpr unsigned MAX_OFFENSE_SPOTS = 3;

	StaticVector<edict_t *, MAX_SPOT_ATTACKERS> attackers[MAX_OFFENSE_SPOTS];
	StaticVector<edict_t *, MAX_SPOT_DEFENDERS> defenders[MAX_DEFENCE_SPOTS];

	StaticVector<DefenceSpot, MAX_DEFENCE_SPOTS> defenceSpots;
	StaticVector<OffenseSpot, MAX_OFFENSE_SPOTS> offenseSpots;

	template <typename Container, typename T>
	inline int AddItem( const char *name, Container &c, T&& item );

	template <typename Container, typename OnRemoved>
	inline int RemoveItem( const char *name, Container &c, int id, OnRemoved onRemoved );

	inline void OnDefenceSpotRemoved( DefenceSpot *defenceSpot ) {
		if( defenceSpot->usesAutoAlert ) {
			DisableDefenceSpotAutoAlert( defenceSpot );
		}
	}

	inline void OnOffenseSpotRemoved( OffenseSpot *offenseSpot ) {}

	struct BotAndScore {
		edict_t *bot;
		float rawScore;
		float effectiveScore;
		BotAndScore( edict_t *bot_ ) : bot( bot_ ), rawScore( 0 ), effectiveScore( 0 ) {}
		bool operator<( const BotAndScore &that ) const {
			return this->effectiveScore < that.effectiveScore;
		}
	};

	typedef StaticVector<BotAndScore, MAX_CLIENTS> Candidates;

	void ResetBotOrders( Bot *bot );
	void ResetAllBotsOrders();

	void FindAllCandidates( Candidates &candidates );
	void AssignDefenders( Candidates &candidates );
	void ComputeDefenceRawScore( Candidates &candidates );
	void ComputeDefenceScore( Candidates &candidates, int spotNum );
	void AssignAttackers( Candidates &candidates );
	void ComputeOffenceRawScore( Candidates &candidates );
	void ComputeOffenceScore( Candidates &candidates, int spotNum );

	void UpdateDefendersStatus( unsigned defenceSpotNum );
	void UpdateAttackersStatus( unsigned offenceSpotNum );

	void EnableDefenceSpotAutoAlert( DefenceSpot *defenceSpot );
	void DisableDefenceSpotAutoAlert( DefenceSpot *defenceSpot );

	static void AlertCallback( void *receiver, Bot *bot, int id, float alertLevel );

	void OnAlertReported( Bot *bot, int id, float alertLevel );

	template<typename Container >
	const edict_t *GetUnderlyingEntity( const Container &container, int spotId ) const;
public:
	AiObjectiveBasedTeamBrain( int team_ ) : AiSquadBasedTeamBrain( team_ ) {}
	virtual ~AiObjectiveBasedTeamBrain() override {}

	void AddDefenceSpot( const AiDefenceSpot &spot );
	void RemoveDefenceSpot( int id );

	void SetDefenceSpotAlert( int id, float alertLevel, unsigned timeoutPeriod );

	void AddOffenseSpot( const AiOffenseSpot &spot );
	void RemoveOffenseSpot( int id );

	// Returns null if there is no such spot / the spot is no longer valid
	// Note: this signature is actually user friendly contrary to the first impression and typed alternatives.
	const edict_t *GetSpotUnderlyingEntity( int spotId, bool isDefenceSpot ) const;

	virtual void OnBotAdded( Bot *bot ) override;
	virtual void OnBotRemoved( Bot *bot ) override;

	virtual void Think() override;
};

#endif
