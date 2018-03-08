#ifndef QFUSION_AI_BASE_TEAM_BRAIN_H
#define QFUSION_AI_BASE_TEAM_BRAIN_H

#include "ai_frame_aware_updatable.h"

class AiBaseTeam : public AiFrameAwareUpdatable
{
	friend class Bot;  // Bots should be able to notify its team in destructor when they get dropped immediately
	friend class AiManager;

	// We can't initialize these vars in constructor, because game exports may be not yet intialized.
	// These values are set to -1 in constructor and computed on demand
	mutable int svFps;
	mutable int svSkill;

	// These vars are used instead of AiFrameAwareUpdatable for lazy intiailization
	mutable int teamAffinityModulo;
	mutable int teamAffinityOffset;
	static constexpr int MAX_AFFINITY_OFFSET = 4;
	// This array contains count of bots that use corresponding offset for each possible affinity offset
	unsigned affinityOffsetsInUse[MAX_AFFINITY_OFFSET];

	// These arrays store copies of bot affinities to be able to access them even if the bot reference has been lost
	unsigned char botAffinityModulo[MAX_CLIENTS];
	unsigned char botAffinityOffsets[MAX_CLIENTS];

	unsigned AffinityModulo() const;
	unsigned TeamAffinityOffset() const;

	void InitTeamAffinity() const;  // Callers are const ones, and only mutable vars are modified

	static void RegisterTeam( int teamNum, AiBaseTeam *team );
	static void UnregisterTeam( int teamNum );

	// A factory method for team creation.
	// Instantiates appropriate kind of team for a current gametype.
	static AiBaseTeam *InstantiateTeam( int teamNum, const char *gametype );

protected:
	AiBaseTeam( int teamNum_ );
	virtual ~AiBaseTeam() override {}

	const int teamNum;

	void AddBot( class Bot *bot );
	void RemoveBot( class Bot *bot );
	virtual void OnBotAdded( class Bot *bot ) {};
	virtual void OnBotRemoved( class Bot *bot ) {};

	void AcquireBotFrameAffinity( int entNum );
	void ReleaseBotFrameAffinity( int entNum );
	void SetBotFrameAffinity( int bot, unsigned modulo, unsigned offset );

	inline int GetCachedCVar( int *cached, const char *name ) const {
		if( *cached == -1 ) {
			*cached = (int)trap_Cvar_Value( name );
		}
		return *cached;
	}

	inline int ServerFps() const { return GetCachedCVar( &svFps, "sv_fps" ); }
	inline int ServerSkill() const { return GetCachedCVar( &svSkill, "sv_skilllevel" ); }

	void Debug( const char *format, ... );

	static void CheckTeamNum( int teamNum );
	static AiBaseTeam **TeamRefForNum( int teamNum );
public:
	static void OnGametypeChanged( const char *gametype );
	static AiBaseTeam *GetTeamForNum( int teamNum );
};

#endif
