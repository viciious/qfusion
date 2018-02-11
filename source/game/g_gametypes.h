/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#ifndef __G_GAMETYPE_H__
#define __G_GAMETYPE_H__

#include <utility>

#include "../matchmaker/mm_rating.h"

//g_gametypes.c
extern cvar_t *g_warmup_timelimit;
extern cvar_t *g_postmatch_timelimit;
extern cvar_t *g_countdown_time;
extern cvar_t *g_match_extendedtime;
extern cvar_t *g_votable_gametypes;
extern cvar_t *g_gametype; // only for use in function that deal with changing gametype, use GS_Gametype()
extern cvar_t *g_gametype_generic;
extern cvar_t *g_gametypes_list;


#define G_CHALLENGERS_MIN_JOINTEAM_MAPTIME  9000 // must wait 10 seconds before joining
#define GAMETYPE_PROJECT_EXTENSION          ".gt"
#define CHAR_GAMETYPE_SEPARATOR             ';'

#define MAX_RACE_CHECKPOINTS    32

typedef struct gameaward_s {
	// ch : size of this?
	const char *name;
	int count;
	// struct gameaward_s *next;
} gameaward_t;

typedef struct {
	mm_uuid_t attacker; // session-id
	mm_uuid_t victim;   // session-id
	int weapon;         // weapon used
	int64_t time;		// server timestamp
} loggedFrag_t;

typedef struct {
	char nickname[32];  // not set if session id is valid
	mm_uuid_t owner;	// session-id
	int64_t timestamp;	// milliseconds
	int numSectors;
	int64_t *times;		// unsigned int * numSectors+1, where last is final time
} raceRun_t;

class GVariousStats {
	struct Node {
		Node *nextInList;
		Node *nextInBin;
		char *key;
		int64_t value;
		uint32_t keyHash;
		uint32_t keyLength;
	};

	Node *listHead;
	// Initialize bins lazily since some objects are global and are constructed before game imports are set up.
	mutable Node **bins;
	unsigned numHashBins;

	const Node *GetNode( unsigned binIndex, const char *key, uint32_t hash, uint32_t length ) const;
	void LinkNewNode( unsigned binIndex, const char *key, uint32_t hash, uint32_t length, int64_t value );
protected:
	void MoveFields( GVariousStats &&that ) {
		this->numHashBins = that.numHashBins;
		this->bins = that.bins, that.bins = nullptr;
		this->listHead = that.listHead, that.listHead = nullptr;
	}
public:
	explicit GVariousStats( unsigned numHashBins_ )
		: listHead( nullptr ), bins( nullptr ), numHashBins( numHashBins_ ) {}

	~GVariousStats();

	GVariousStats( const GVariousStats & ) = delete;
	GVariousStats &operator=( const GVariousStats & ) = delete;

	GVariousStats( GVariousStats &&that ) {
		MoveFields( std::move( that ) );
	}

	GVariousStats &operator=( GVariousStats &&that ) {
		Clear();
		MoveFields( std::move( that ) );
		return *this;
	}

	void Clear();

	int64_t GetEntry( const char *key, int64_t defaultValue = 0 ) const;
	void SetEntry( const char *key, int64_t value );
	void AddToEntry( const char *key, int64_t delta );

	template<typename T>
	inline void AddToEntry( const std::pair<const char *, T> &keyAndValue ) {
		AddToEntry( keyAndValue.first, keyAndValue.second );
	}

	class const_iterator {
		friend class GVariousStats;

		const Node *currNode;
		explicit const_iterator( const Node *node ): currNode( node ) {}
	public:
		bool operator==( const const_iterator &that ) const {
			return currNode == that.currNode;
		}

		bool operator!=( const const_iterator &that ) const { return !( *this == that ); }

		const_iterator &operator++() {
			currNode = currNode->nextInList;
			return *this;
		}

		const_iterator operator++(int) {
			const_iterator result( currNode );
			currNode = currNode->nextInList;
			return result;
		}

		const std::pair<const char *, int64_t> operator*() const {
			return std::make_pair( currNode->key, currNode->value );
		};
	};

	const_iterator begin() const { return const_iterator( listHead ); }
	const_iterator end() const { return const_iterator( nullptr ); }
};

template <typename T, size_t N> inline void MoveArray( T ( &dest )[N], T ( &src )[N] ) {
	memcpy( dest, src, N );
	memset( src, 0, N );
}

typedef struct score_stats_s: public GVariousStats {
	score_stats_s(): GVariousStats( 271 ) {
		Clear();
	}

	~score_stats_s() {
		ReleaseAllocators();
	}

	int score;
	int awards;

	void Clear() {
		GVariousStats::Clear();

		score = 0;
		awards = 0;

		memset( accuracy_shots, 0, sizeof( accuracy_shots ) );
		memset( accuracy_hits, 0, sizeof( accuracy_hits ) );
		memset( accuracy_hits_direct, 0, sizeof( accuracy_hits_direct ) );
		memset( accuracy_hits_air, 0, sizeof( accuracy_hits_air ) );
		memset( accuracy_damage, 0, sizeof( accuracy_damage ) );
		memset( accuracy_frags, 0, sizeof( accuracy_frags ) );

		had_playtime = false;
		memset( &currentRun, 0, sizeof( currentRun ) );
		memset( &raceRecords, 0, sizeof( raceRecords ) );

		ReleaseAllocators();
	}

	// These getters serve an utilty. We might think of precomputing handles (key/length)
	// for strings and use ones instead of plain string for faster access.

	void AddDeath() { AddToEntry( "deaths", 1 ); }
	void AddFrag() { AddToEntry( "frags", 1 ); }
	void AddSuicide() { AddToEntry( "suicides", 1 ); }
	void AddTeamFrag() { AddToEntry( "team_frags", 1 ); }
	void AddRound() { AddToEntry( "rounds", 1 ); }

	int accuracy_shots[AMMO_TOTAL - AMMO_GUNBLADE];
	int accuracy_hits[AMMO_TOTAL - AMMO_GUNBLADE];
	int accuracy_hits_direct[AMMO_TOTAL - AMMO_GUNBLADE];
	int accuracy_hits_air[AMMO_TOTAL - AMMO_GUNBLADE];
	int accuracy_damage[AMMO_TOTAL - AMMO_GUNBLADE];
	int accuracy_frags[AMMO_TOTAL - AMMO_GUNBLADE];

	void AddDamageGiven( float damage ) {
		AddToEntry( "damage_given", (int)damage );
	}
	void AddDamageTaken( float damage ) {
		AddToEntry( "damage_taken", (int)damage );
	}
	void AddTeamDamageGiven( float damage ) {
		AddToEntry( "team_damage_given", (int)damage );
	}
	void AddTeamDamageTaken( float damage ) {
		AddToEntry( "team_damage_taken", (int)damage );
	}

	bool had_playtime;

	// loggedFrag_t
	linear_allocator_t *fragAllocator;

	// gameaward_t
	linear_allocator_t *awardAllocator;
	// gameaward_t *gameawards;

	raceRun_t currentRun;
	raceRun_t raceRecords;

	score_stats_s( const score_stats_s &that ) = delete;
	score_stats_s &operator=( const score_stats_s &that ) = delete;

	score_stats_s( score_stats_s &&that )
		: GVariousStats( std::move( that ) ) {
		MoveFields( std::move( that ) );
	}

	score_stats_s &operator=( score_stats_s &&that ) {
		Clear();
		MoveFields( std::move( that ) );
		return *this;
	}
private:
	void MoveFields( score_stats_s &&that ) {
		GVariousStats::MoveFields( std::move( that ) );

		this->score = that.score, that.score = 0;
		this->awards = that.awards, that.awards = 0;

		MoveArray( this->accuracy_shots, that.accuracy_shots );
		MoveArray( this->accuracy_hits, that.accuracy_hits );
		MoveArray( this->accuracy_hits_direct, that.accuracy_hits_direct );
		MoveArray( this->accuracy_hits_air, that.accuracy_hits_air );
		MoveArray( this->accuracy_damage, that.accuracy_damage );
		MoveArray( this->accuracy_frags, that.accuracy_frags );

		this->had_playtime = that.had_playtime, that.had_playtime = false;

		this->currentRun = that.currentRun;
		memset( &that.currentRun, 0, sizeof( that.currentRun ) );
		this->raceRecords = that.raceRecords;
		memset( &that.raceRecords, 0, sizeof( that.raceRecords ) );

		this->awardAllocator = that.awardAllocator, that.awardAllocator = nullptr;
		this->fragAllocator = that.fragAllocator, that.fragAllocator = nullptr;
	}

	void ReleaseAllocators() {
		if( fragAllocator ) {
			LinearAllocator_Free( fragAllocator );
			fragAllocator = nullptr;
		}
		if( awardAllocator ) {
			LinearAllocator_Free( awardAllocator );
			awardAllocator = nullptr;
		}
	}
} score_stats_t;

// this is only really used to create the script objects
typedef struct {
	bool dummy;
} match_t;

typedef struct {
	match_t match;

	void *initFunc;
	void *spawnFunc;
	void *matchStateStartedFunc;
	void *matchStateFinishedFunc;
	void *thinkRulesFunc;
	void *playerRespawnFunc;
	void *scoreEventFunc;
	void *scoreboardMessageFunc;
	void *selectSpawnPointFunc;
	void *clientCommandFunc;
	void *shutdownFunc;

	int spawnableItemsMask;
	int respawnableItemsMask;
	int dropableItemsMask;
	int pickableItemsMask;

	bool isTeamBased;
	bool isRace;
	bool isTutorial;
	bool inverseScore;
	bool hasChallengersQueue;
	bool hasChallengersRoulette;
	int maxPlayersPerTeam;

	// default item respawn time
	int ammo_respawn;
	int armor_respawn;
	int weapon_respawn;
	int health_respawn;
	int powerup_respawn;
	int megahealth_respawn;
	int ultrahealth_respawn;

	// few default settings
	bool readyAnnouncementEnabled;
	bool scoreAnnouncementEnabled;
	bool countdownEnabled;
	bool matchAbortDisabled;
	bool shootingDisabled;
	bool infiniteAmmo;
	bool canForceModels;
	bool canShowMinimap;
	bool teamOnlyMinimap;
	bool customDeadBodyCam;
	bool removeInactivePlayers;
	bool disableObituaries;

	int spawnpointRadius;

	bool mmCompatible;

	int numBots;
	bool dummyBots;

	int forceTeamHumans;
	int forceTeamBots;
} gametype_descriptor_t;

typedef struct g_teamlist_s {
	int playerIndices[MAX_CLIENTS];
	int numplayers;
	score_stats_t stats;
	int ping;
	bool locked;
	int invited[MAX_CLIENTS];
	bool has_coach;

	void Clear() {
		memset( playerIndices, 0, sizeof( playerIndices ) );
		numplayers = 0;
		stats.Clear();
		ping = 0;
		locked = false;;
		memset( invited, 0, sizeof( invited ) );
		has_coach = false;
	}
} g_teamlist_t;

extern g_teamlist_t teamlist[GS_MAX_TEAMS];

//clock
extern char clockstring[16];

//
//	matches management
//
bool G_Match_Tied( void );
bool G_Match_CheckExtendPlayTime( void );
void G_Match_RemoveProjectiles( edict_t *owner );
void G_Match_CleanUpPlayerStats( edict_t *ent );
void G_Match_FreeBodyQueue( void );
void G_Match_LaunchState( int matchState );

//
//	teams
//
void G_Teams_Init( void );
void G_Teams_UpdateTeamInfoMessages( void );

void G_Teams_ExecuteChallengersQueue( void );
void G_Teams_AdvanceChallengersQueue( void );

void G_Match_Autorecord_Start( void );
void G_Match_Autorecord_AltStart( void );
void G_Match_Autorecord_Stop( void );
void G_Match_Autorecord_Cancel( void );
bool G_Match_ScorelimitHit( void );
bool G_Match_SuddenDeathFinished( void );
bool G_Match_TimelimitHit( void );

//coach
void G_Teams_Coach( edict_t *ent );
void G_Teams_CoachLockTeam( edict_t *ent );
void G_Teams_CoachUnLockTeam( edict_t *ent );
void G_Teams_CoachRemovePlayer( edict_t *ent );

bool G_Gametype_Exists( const char *name );
void G_Gametype_GENERIC_ScoreboardMessage( void );
void G_Gametype_GENERIC_ClientRespawn( edict_t *self, int old_team, int new_team );

#endif //  __G_GAMETYPE_H__
