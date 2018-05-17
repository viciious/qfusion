#include "BaseTeam.h"
#include "SquadBasedTeam.h"
#include "../ai_shutdown_hooks_holder.h"
#include "../bot.h"

AiBaseTeam *AiBaseTeam::teamsForNums[GS_MAX_TEAMS - 1];

AiBaseTeam::AiBaseTeam( int teamNum_ )
	: teamNum( teamNum_ ) {
	svFps = -1;
	svSkill = -1;
	teamAffinityModulo = -1;
	teamAffinityOffset = -1;

	memset( botAffinityModulo, 0, sizeof( botAffinityModulo ) );
	memset( botAffinityOffsets, 0, sizeof( botAffinityOffsets ) );
}

void AiBaseTeam::Debug( const char *format, ... ) {
	// Cut it early to help optimizer to eliminate AI_Debugv call
#ifdef _DEBUG
	va_list va;
	va_start( va, format );
	AI_Debugv( GS_TeamName( teamNum ), format, va );
	va_end( va );
#endif
}

void AiBaseTeam::CheckTeamNum( int teamNum ) {
#ifndef PUBLIC_BUILD
	if( teamNum < TEAM_PLAYERS || teamNum >= GS_MAX_TEAMS ) {
		AI_FailWith( "AiBaseTeam", "GetTeamForNum(): Illegal team num %d\n", teamNum );
	}
#endif
}

AiBaseTeam **AiBaseTeam::TeamRefForNum( int teamNum ) {
	CheckTeamNum( teamNum );
	return &teamsForNums[teamNum - TEAM_PLAYERS];
}

AiBaseTeam *AiBaseTeam::GetTeamForNum( int teamNum ) {
	CheckTeamNum( teamNum );
	AiBaseTeam **teamRef = TeamRefForNum( teamNum );
	if( !*teamRef ) {
		AI_FailWith( "AiBaseTeam", "GetTeamForNum(): A team for num %d is not instantiated atm\n", teamNum );
	}
	return *teamRef;
}

AiBaseTeam *AiBaseTeam::GetTeamForNum( int teamNum, const std::type_info &desiredType ) {
	CheckTeamNum( teamNum );
	AiBaseTeam **teamRef = TeamRefForNum( teamNum );
	if( !*teamRef ) {
		AI_FailWith( "AiBaseTeam::GetTeamForNum(,)", "A team for num %d has not been instantiated yet\n", teamNum );
	}

	// Check whether the desired type is related to AiBaseTeam at all.
	// If types A and B are involved in (transitive) inheritance relation, this statement holds: A <: B or B <: A
	if( typeid( AiBaseTeam ) != desiredType ) {
		if( !typeid( AiBaseTeam ).before( desiredType ) && !desiredType.before( typeid( AiBaseTeam ) ) ) {
			AI_FailWith( "AiBaseTeam::GetTeamForNum(,)", "Desired type %s is unrelated to AiBaseTeam\n", desiredType.name() );
		}
	}

	// If the existing type is not an ancestor (a parent) of the desired type
	// (matches it exactly or is a descendant of it)
	if( !typeid( **teamRef ).before( desiredType ) ) {
		return *teamRef;
	}

	// Destroy the existing AI team for the team slot
	AiBaseTeam *oldTeam = *teamRef;
	// Delegate further ops to the descendant factory
	*teamRef = AiSquadBasedTeam::InstantiateTeam( teamNum, desiredType );
	// Move an additional state (if any) from the old team
	( *teamRef )->TransferStateFrom( oldTeam );
	// Destroy the old team
	oldTeam->~AiBaseTeam();
	G_Free( oldTeam );

	return *teamRef;
}

unsigned AiBaseTeam::AffinityModulo() const {
	if( teamAffinityModulo == -1 ) {
		InitTeamAffinity();
	}
	return (unsigned)teamAffinityModulo;
}

unsigned AiBaseTeam::TeamAffinityOffset() const {
	if( teamAffinityOffset == -1 ) {
		InitTeamAffinity();
	}
	return (unsigned)teamAffinityOffset;
}

void AiBaseTeam::InitTeamAffinity() const {
	// We round frame time to integer milliseconds
	int frameTime = 1000 / ServerFps();
	// 4 for 60 fps or more, 1 for 16 fps or less
	teamAffinityModulo = std::min( 4, std::max( 1, 64 / frameTime ) );
	if( teamNum == TEAM_PLAYERS ) {
		teamAffinityOffset = 0;
		const_cast<AiBaseTeam*>( this )->SetFrameAffinity( teamAffinityModulo, teamAffinityOffset );
		return;
	}

	static_assert( TEAM_ALPHA == 2 && TEAM_BETA == 3, "Modify affinity offset computations" );
	switch( teamAffinityModulo ) {
		// The Alpha AI team thinks on frame 0, the Beta AI team thinks on frame 2
		case 4: teamAffinityOffset = ( teamNum - 2 ) * 2; break;
		// Both Alpha and Beta AI teams think on frame 0
		case 3: teamAffinityOffset = 0; break;
		// The Alpha AI team thinks on frame 0, the Beta AI team thinks on frame 1
		case 2: teamAffinityOffset = teamNum - 2; break;
		// All AI teams think in the same frame
		case 1: teamAffinityOffset = 0; break;
	}
	// Initialize superclass fields
	const_cast<AiBaseTeam*>( this )->SetFrameAffinity( teamAffinityModulo, teamAffinityOffset );
}

void AiBaseTeam::AddBot( Bot *bot ) {
	Debug( "new bot %s has been added\n", bot->Nick() );

	AcquireBotFrameAffinity( ENTNUM( bot->self ) );
	// Call subtype method (if any)
	OnBotAdded( bot );
}

void AiBaseTeam::RemoveBot( Bot *bot ) {
	Debug( "bot %s has been removed\n", bot->Nick() );

	ReleaseBotFrameAffinity( ENTNUM( bot->self ) );
	// Call subtype method (if any)
	OnBotRemoved( bot );
}

void AiBaseTeam::AcquireBotFrameAffinity( int entNum ) {
	// Always specify offset as zero for affinity module that = 1 (any % 1 == 0)
	if( AffinityModulo() == 1 ) {
		SetBotFrameAffinity( entNum, AffinityModulo(), 0 );
		return;
	}

	if( GS_TeamBasedGametype() ) {
		unsigned modulo = AffinityModulo(); // Precompute
		static_assert( MAX_AFFINITY_OFFSET == 4, "Only two teams are supported" );
		switch( modulo ) {
			case 4:
				// If the think cycle consist of 4 frames:
				// the Alpha AI team thinks on frame 0, bots of team Alpha think on frame 1,
				// the Beta AI team thinks on frame 2, bots of team Beta think on frame 3
				SetBotFrameAffinity( entNum, modulo, 2 * ( (unsigned)teamNum - TEAM_ALPHA ) + 1 );
				break;
			case 3:
				// If the think cycle consist of 3 frames:
				// AI teams think on frame 0, bots of team Alpha think on frame 1, bots of team Beta think on frame 2
				SetBotFrameAffinity( entNum, modulo, 1 + (unsigned)teamNum - TEAM_ALPHA );
				break;
			case 2:
				// If the think cycle consist of 2 frames:
				// the Alpha AI team and team Alpha bots think on frame 0,
				// the Beta AI team and team Beta bots think on frame 1
				SetBotFrameAffinity( entNum, modulo, (unsigned)teamNum - TEAM_ALPHA );
				break;
		}
	} else {
		// Select less used offset (thus, less loaded frames)
		unsigned chosenOffset = 0;
		for( unsigned i = 1; i < MAX_AFFINITY_OFFSET; ++i ) {
			if( affinityOffsetsInUse[chosenOffset] > affinityOffsetsInUse[i] ) {
				chosenOffset = i;
			}
		}
		affinityOffsetsInUse[chosenOffset]++;
		SetBotFrameAffinity( entNum, AffinityModulo(), chosenOffset );
	}
}

void AiBaseTeam::ReleaseBotFrameAffinity( int entNum ) {
	unsigned offset = botAffinityOffsets[entNum];
	botAffinityOffsets[entNum] = 0;
	botAffinityModulo[entNum] = 0;
	affinityOffsetsInUse[offset]--;
}

void AiBaseTeam::SetBotFrameAffinity( int entNum, unsigned modulo, unsigned offset ) {
	botAffinityModulo[entNum] = (unsigned char)modulo;
	botAffinityOffsets[entNum] = (unsigned char)offset;
	game.edicts[entNum].ai->botRef->SetFrameAffinity( modulo, offset );
}

void AiBaseTeam::Init() {
#ifndef PUBLIC_BUILD
	for( int team = TEAM_PLAYERS; team < GS_MAX_TEAMS; ++team ) {
		if( *TeamRefForNum( team ) ) {
			AI_FailWith( "AiBaseTeam::Init()", "A team for num %d is already present / was not released", team );
		}
	}
#endif

	if( GS_TeamBasedGametype() ) {
		for( int team = TEAM_ALPHA; team < GS_MAX_TEAMS; ++team ) {
			CreateTeam( team );
		}
	} else {
		CreateTeam( TEAM_PLAYERS );
	}
}

void AiBaseTeam::Shutdown() {
	// Destroy all current teams (if any)
	for( int team = TEAM_PLAYERS; team < GS_MAX_TEAMS; ++team ) {
		ReleaseTeam( team );
	}
}

void AiBaseTeam::CreateTeam( int teamNum ) {
	AiBaseTeam **teamRef = TeamRefForNum( teamNum );
	// If there was an existing
	if( *teamRef ) {
		ReleaseTeam( teamNum );
	}
	// Set team pointer
	*teamRef = InstantiateTeam( teamNum );
	// TODO: Should we nofify bots? They should not use a cached team reference and always use GetTeamForNum()
}

void AiBaseTeam::ReleaseTeam( int teamNum ) {
	// Get the static cell that maybe holds the address of the team
	AiBaseTeam **teamToRef = TeamRefForNum( teamNum );
	// If there is no team address in this memory cell
	if( !*teamToRef ) {
		return;
	}

	// Destruct the team
	( *teamToRef )->~AiBaseTeam();
	// Free team memory
	G_Free( *teamToRef );
	// Nullify the static memory cell holding no longer valid address
	*teamToRef = nullptr;
}

AiBaseTeam *AiBaseTeam::InstantiateTeam( int teamNum ) {
	// Delegate construction to AiSquadBasedTeam
	if( GS_TeamBasedGametype() && !GS_InvidualGameType() ) {
		return AiSquadBasedTeam::InstantiateTeam( teamNum );
	}

	void *mem = G_Malloc( sizeof( AiBaseTeam ) );
	return new(mem)AiBaseTeam( teamNum );
}
