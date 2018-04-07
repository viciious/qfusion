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
#include "g_local.h"
#include "../matchmaker/mm_query.h"
#include "g_gametypes.h"

#include <new>

// A common supertype for query readers/writers
class alignas( 8 )QueryIOHelper {
protected:
	virtual bool CheckTopOfStack( const char *tag, int topOfStack_ ) = 0;

	struct StackedIOHelper {
		virtual ~StackedIOHelper() {}
	};

	static constexpr int STACK_SIZE = 32;

	template<typename Parent, typename ObjectIOHelper, typename ArrayIOHelper>
	class alignas( 8 )StackedHelpersAllocator {
	protected:
		static_assert( sizeof( ObjectIOHelper ) >= sizeof( ArrayIOHelper ), "Redefine LargestEntry" );
		typedef ObjectIOHelper LargestEntry;

		static constexpr auto ENTRY_SIZE =
			sizeof( LargestEntry ) % 8 ? sizeof( LargestEntry ) + 8 - sizeof( LargestEntry ) % 8 : sizeof( LargestEntry );

		alignas( 8 ) uint8_t storage[STACK_SIZE * ENTRY_SIZE];

		QueryIOHelper *parent;
		int topOfStack;

		void *AllocEntry( const char *tag ) {
			if( parent->CheckTopOfStack( tag, topOfStack ) ) {
				return storage + ( topOfStack++ ) * ENTRY_SIZE;
			}
			return nullptr;
		}
	public:
		explicit StackedHelpersAllocator( QueryIOHelper *parent_ ): parent( parent_ ), topOfStack( 0 ) {
			if( ( (uintptr_t)this ) % 8 ) {
				G_Error( "StackedHelpersAllocator(): the object is misaligned!\n" );
			}
		}

		ArrayIOHelper *NewArrayIOHelper( stat_query_api_t *api, stat_query_section_t *section ) {
			return new( AllocEntry( "array" ) )ArrayIOHelper( (Parent *)parent, api, section );
		}

		ObjectIOHelper *NewObjectIOHelper( stat_query_api_t *api, stat_query_section_t *section ) {
			return new( AllocEntry( "object" ) )ObjectIOHelper( (Parent *)parent, api, section );
		}

		void DeleteHelper( StackedIOHelper *helper ) {
			helper->~StackedIOHelper();
			if( (uint8_t *)helper != storage + ( topOfStack - 1 ) * ENTRY_SIZE ) {
				G_Error( "WritersAllocator::DeleteWriter(): Attempt to delete an entry that is not on top of stack\n" );
			}
			topOfStack--;
		}
	};
};

class alignas( 8 )QueryWriter final: public QueryIOHelper {
	friend class CompoundWriter;
	friend class ObjectWriter;
	friend class ArrayWriter;
	friend struct WritersAllocator;

	stat_query_api_t *api;
	stat_query_t *query;

	static constexpr int STACK_SIZE = 32;

	bool CheckTopOfStack( const char *tag, int topOfStack_ ) override {
		if( topOfStack_ < 0 || topOfStack_ >= STACK_SIZE ) {
			const char *kind = topOfStack_ < 0 ? "underflow" : "overflow";
			G_Error( "%s: Objects stack %s, top of stack index is %d\n", tag, kind, topOfStack_ );
		}
		return true;
	}

	void NotifyOfNewArray( const char *name ) {
		auto *section = api->CreateArray( query, TopOfStack().section, name );
		topOfStackIndex++;
		stack[topOfStackIndex] = writersAllocator.NewArrayIOHelper( api, section );
	}

	void NotifyOfNewObject( const char *name ) {
		auto *section = api->CreateSection( query, TopOfStack().section, name );
		topOfStackIndex++;
		stack[topOfStackIndex] = writersAllocator.NewObjectIOHelper( api, section );
	}

	void NotifyOfArrayEnd() {
		writersAllocator.DeleteHelper( &TopOfStack() );
		topOfStackIndex--;
	}

	void NotifyOfObjectEnd() {
		writersAllocator.DeleteHelper( &TopOfStack() );
		topOfStackIndex--;
	}

	class CompoundWriter: public StackedIOHelper {
		friend class QueryWriter;
	protected:
		QueryWriter *const parent;
		stat_query_api_t *const api;
		stat_query_section_t *const section;
	public:
		CompoundWriter( QueryWriter *parent_, stat_query_api_t *api_, stat_query_section_t *section_ )
			: parent( parent_ ), api( api_ ), section( section_ ) {}

		virtual void operator<<( const char *nameOrValue ) = 0;
		virtual void operator<<( int value ) = 0;
		virtual void operator<<( int64_t value ) = 0;
		virtual void operator<<( double value ) = 0;
		virtual void operator<<( const mm_uuid_t &value ) = 0;
		virtual void operator<<( char ch ) = 0;
	};

	class ObjectWriter: public CompoundWriter {
		const char *fieldName;

		const char *CheckFieldName( const char *tag ) {
			if( !fieldName ) {
				G_Error( "QueryWriter::ObjectWriter::operator<<(%s): "
						 "A field name has not been set before supplying a value", tag );
			}
			return fieldName;
		}
	public:
		ObjectWriter( QueryWriter *parent_, stat_query_api_t *api_, stat_query_section_t *section_ )
			: CompoundWriter( parent_, api_, section_ ), fieldName( nullptr ) {}

		void operator<<( const char *nameOrValue ) override {
			if( !fieldName ) {
				// TODO: Check whether it is a valid identifier?
				fieldName = nameOrValue;
			} else {
				api->SetString( section, fieldName, nameOrValue );
				fieldName = nullptr;
			}
		}

		void operator<<( int value ) override {
			api->SetNumber( section, CheckFieldName( "int" ), value );
			fieldName = nullptr;
		}

		void operator<<( int64_t value ) override {
			if( (int64_t)( (double)value ) != value ) {
				G_Error( "ObjectWriter::operator<<(int64_t): The value %"
							 PRIi64 " will be lost in conversion to double", value );
			}
			api->SetNumber( section, CheckFieldName( "int64_t" ), value );
			fieldName = nullptr;
		}

		void operator<<( double value ) override {
			api->SetNumber( section, CheckFieldName( "double" ), value );
			fieldName = nullptr;
		}

		void operator<<( const mm_uuid_t &value ) override {
			char buffer[UUID_BUFFER_SIZE];
			api->SetString( section, CheckFieldName( "const mm_uuid_t &" ), value.ToString( buffer ) );
			fieldName = nullptr;
		}

		void operator<<( char ch ) override {
			if( ch == '{' ) {
				parent->NotifyOfNewObject( CheckFieldName( "{..." ) );
				fieldName = nullptr;
			} else if( ch == '[' ) {
				parent->NotifyOfNewArray( CheckFieldName( "[..." ) );
				fieldName = nullptr;
			} else if( ch == '}' ) {
				parent->NotifyOfObjectEnd();
			} else if( ch == ']' ) {
				G_Error( "ArrayWriter::operator<<('...]'): Unexpected token (an array end token)" );
			} else {
				G_Error( "ArrayWriter::operator<<(char): Illegal character (%d as an integer)", (int)ch );
			}
		}
	};

	class ArrayWriter: public CompoundWriter {
	public:
		ArrayWriter( QueryWriter *parent_, stat_query_api_t *api_, stat_query_section_t *section_ )
			: CompoundWriter( parent_, api_, section_ ) {}

		void operator<<( const char *nameOrValue ) override {
			api->AddArrayString( section, nameOrValue );
		}

		void operator<<( int value ) override {
			api->AddArrayNumber( section, value );
		}

		void operator<<( int64_t value ) override {
			api->AddArrayNumber( section, value );
		}

		void operator<<( double value ) override {
			api->AddArrayNumber( section, value );
		}

		void operator<<( const mm_uuid_t &value ) override {
			char buffer[UUID_BUFFER_SIZE];
			api->AddArrayString( section, value.ToString( buffer ) );
		}

		void operator<<( char ch ) override {
			if( ch == '[' ) {
				parent->NotifyOfNewArray( nullptr );
			} else if( ch == '{' ) {
				parent->NotifyOfNewObject( nullptr );
			} else if( ch == ']' ) {
				parent->NotifyOfArrayEnd();
			} else if( ch == '}' ) {
				G_Error( "ArrayWriter::operator<<('...}'): Unexpected token (an object end token)");
			} else {
				G_Error( "ArrayWriter::operator<<(char): Illegal character (%d as an integer)", (int)ch );
			}
		}
	};

	struct WritersAllocator: public StackedHelpersAllocator<QueryWriter, ObjectWriter, ArrayWriter> {
		explicit WritersAllocator( QueryWriter *parent_ ): StackedHelpersAllocator( parent_ ) {}
	};

	WritersAllocator writersAllocator;

	// Put the root object onto the top of stack
	// Do not require closing it explicitly
	CompoundWriter *stack[32 + 1];
	int topOfStackIndex;

	CompoundWriter &TopOfStack() {
		CheckTopOfStack( "QueryWriter::TopOfStack()", topOfStackIndex );
		return *stack[topOfStackIndex];
	}
public:
	QueryWriter( stat_query_api_t *api_, const char *url )
		: api( api_ ), writersAllocator( this ) {
		query = api->CreateQuery( nullptr, url, false );
		topOfStackIndex = 0;
		stack[topOfStackIndex] = writersAllocator.NewObjectIOHelper( api, api->GetOutRoot( query ) );
	}

	QueryWriter &operator<<( const char *nameOrValue ) {
		TopOfStack() << nameOrValue;
		return *this;
	}

	QueryWriter &operator<<( int value ) {
		TopOfStack() << value;
		return *this;
	}

	QueryWriter &operator<<( int64_t value ) {
		TopOfStack() << value;
		return *this;
	}

	QueryWriter &operator<<( double value ) {
		TopOfStack() << value;
		return *this;
	}

	QueryWriter &operator<<( const mm_uuid_t &value ) {
		TopOfStack() << value;
		return *this;
	}

	QueryWriter &operator<<( char ch ) {
		TopOfStack() << ch;
		return *this;
	}

	void Send() {
		if( topOfStackIndex != 0 ) {
			G_Error( "QueryWriter::Send(): Root object building is incomplete, remaining stack depth is %d\n", topOfStackIndex );
		}

		// Note: Do not call api->Send() directly, let the server code perform an augmentation of the request!
		trap_MM_SendQuery( query );
	}
};

// number of raceruns to send in one batch
#define RACERUN_BATCH_SIZE  256

stat_query_api_t *sq_api;

static void G_Match_SendRaceReport( void );

//====================================================

static clientRating_t *g_ratingAlloc( const char *gametype, float rating, float deviation, mm_uuid_t uuid ) {
	clientRating_t *cr;

	cr = (clientRating_t*)G_Malloc( sizeof( *cr ) );
	if( !cr ) {
		return NULL;
	}

	Q_strncpyz( cr->gametype, gametype, sizeof( cr->gametype ) - 1 );
	cr->rating = rating;
	cr->deviation = deviation;
	cr->next = 0;
	cr->uuid = uuid;

	return cr;
}

static clientRating_t *g_ratingCopy( clientRating_t *other ) {
	return g_ratingAlloc( other->gametype, other->rating, other->deviation, other->uuid );
}

// free the list of clientRatings
static void g_ratingsFree( clientRating_t *list ) {
	clientRating_t *next;

	while( list ) {
		next = list->next;
		G_Free( list );
		list = next;
	}
}

// update the current servers rating
static void g_serverRating( void ) {
	clientRating_t avg;

	if( !game.ratings ) {
		avg.rating = MM_RATING_DEFAULT;
		avg.deviation = MM_DEVIATION_DEFAULT;
	} else {
		Rating_AverageRating( &avg, game.ratings );
	}

	// Com_Printf("g_serverRating: Updated server's skillrating to %f\n", avg.rating );

	trap_Cvar_ForceSet( "sv_skillRating", va( "%.0f", avg.rating ) );
}

/*
* G_TransferRatings
*/
void G_TransferRatings( void ) {
	clientRating_t *cr, *found;
	edict_t *ent;
	gclient_t *client;

	// shuffle the ratings back from game.ratings to clients->ratings and back
	// based on current gametype
	g_ratingsFree( game.ratings );
	game.ratings = 0;

	for( ent = game.edicts + 1; PLAYERNUM( ent ) < gs.maxclients; ent++ ) {
		client = ent->r.client;

		if( !client ) {
			continue;
		}
		if( !ent->r.inuse ) {
			continue;
		}

		// temphack for duplicate client entries
		found = Rating_FindId( game.ratings, client->mm_session );
		if( found ) {
			continue;
		}

		found = Rating_Find( client->ratings, gs.gametypeName );

		// create a new default rating if this doesnt exist
		// DONT USE G_AddDefaultRating cause this will cause double entries in game.ratings
		if( !found ) {
			found = g_ratingAlloc( gs.gametypeName, MM_RATING_DEFAULT, MM_DEVIATION_DEFAULT, client->mm_session );
			if( !found ) {
				continue;   // ??

			}
			found->next = client->ratings;
			client->ratings = found;
		}

		// add it to the games list
		cr = g_ratingCopy( found );
		cr->next = game.ratings;
		game.ratings = cr;
	}

	g_serverRating();
}

// This doesnt update ratings, only inserts new default rating if it doesnt exist
// if gametype is NULL, use current gametype
clientRating_t *G_AddDefaultRating( edict_t *ent, const char *gametype ) {
	clientRating_t *cr;
	gclient_t *client;

	if( gametype == NULL ) {
		gametype = gs.gametypeName;
	}

	client = ent->r.client;
	if( !ent->r.inuse ) {
		return NULL;
	}

	cr = Rating_Find( client->ratings, gametype );
	if( cr == NULL ) {
		cr = g_ratingAlloc( gametype, MM_RATING_DEFAULT, MM_DEVIATION_DEFAULT, ent->r.client->mm_session );
		if( !cr ) {
			return NULL;
		}

		cr->next = client->ratings;
		client->ratings = cr;
	}

	if( !strcmp( gametype, gs.gametypeName ) ) {
		clientRating_t *found;

		// add this rating to current game-ratings
		found = Rating_FindId( game.ratings, client->mm_session );
		if( !found ) {
			found = g_ratingCopy( cr );
			if( found ) {
				found->next = game.ratings;
				game.ratings = found;
			}
		} else {
			// update rating
			found->rating = cr->rating;
			found->deviation = cr->deviation;
		}
		g_serverRating();
	}

	return cr;
}

// this inserts a new one, or updates the ratings if it exists
clientRating_t *G_AddRating( edict_t *ent, const char *gametype, float rating, float deviation ) {
	clientRating_t *cr;
	gclient_t *client;

	if( gametype == NULL ) {
		gametype = gs.gametypeName;
	}

	client = ent->r.client;
	if( !ent->r.inuse ) {
		return NULL;
	}

	cr = Rating_Find( client->ratings, gametype );
	if( cr != NULL ) {
		cr->rating = rating;
		cr->deviation = deviation;
	} else {
		cr = g_ratingAlloc( gametype, rating, deviation, ent->r.client->mm_session );
		if( !cr ) {
			return NULL;
		}

		cr->next = client->ratings;
		client->ratings = cr;
	}

	if( !strcmp( gametype, gs.gametypeName ) ) {
		clientRating_t *found;

		// add this rating to current game-ratings
		found = Rating_FindId( game.ratings, client->mm_session );
		if( !found ) {
			found = g_ratingCopy( cr );
			if( found ) {
				found->next = game.ratings;
				game.ratings = found;
			}
		} else {
			// update values
			found->rating = rating;
			found->deviation = deviation;
		}

		g_serverRating();
	}

	return cr;
}

// removes all references for given entity
void G_RemoveRating( edict_t *ent ) {
	gclient_t *client;
	clientRating_t *cr;

	client = ent->r.client;

	// first from the game
	cr = Rating_DetachId( &game.ratings, client->mm_session );
	if( cr ) {
		G_Free( cr );
	}

	// then the clients own list
	g_ratingsFree( client->ratings );
	client->ratings = 0;

	g_serverRating();
}

// debug purposes
void G_ListRatings_f( void ) {
	clientRating_t *cr;
	gclient_t *cl;
	edict_t *ent;
	char uuid_buffer[UUID_BUFFER_SIZE];

	Com_Printf( "Listing ratings by gametype:\n" );
	for( cr = game.ratings; cr ; cr = cr->next ) {
		Com_Printf( "  %s %s %f %f\n", cr->gametype, cr->uuid.ToString( uuid_buffer ), cr->rating, cr->deviation );
	}

	Com_Printf( "Listing ratings by player\n" );
	for( ent = game.edicts + 1; PLAYERNUM( ent ) < gs.maxclients; ent++ ) {
		cl = ent->r.client;

		if( !ent->r.inuse ) {
			continue;
		}

		Com_Printf( "%s:\n", cl->netname );
		for( cr = cl->ratings; cr ; cr = cr->next ) {
			Com_Printf( "  %s %s %f %f\n", cr->gametype, cr->uuid.ToString( uuid_buffer ), cr->rating, cr->deviation );
		}
	}
}

//==========================================================

// race records from MM
void G_AddRaceRecords( edict_t *ent, int numSectors, int64_t *records ) {
	gclient_t *cl;
	raceRun_t *rr;
	size_t size;

	cl = ent->r.client;

	if( !ent->r.inuse || cl == NULL ) {
		return;
	}

	rr = &cl->level.stats.raceRecords;
	if( rr->times ) {
		G_LevelFree( rr->times );
	}

	size = ( numSectors + 1 ) * sizeof( *rr->times );
	rr->times = ( int64_t * )G_LevelMalloc( size );

	memcpy( rr->times, records, size );
	rr->numSectors = numSectors;
}

// race records to AS (TODO: export this to AS)
int64_t G_GetRaceRecord( edict_t *ent, int sector ) {
	gclient_t *cl;
	raceRun_t *rr;

	cl = ent->r.client;

	if( !ent->r.inuse || cl == NULL ) {
		return 0;
	}

	rr = &cl->level.stats.raceRecords;
	if( !rr->times ) {
		return 0;
	}

	// sector = -1 means final sector
	if( sector < -1 || sector >= rr->numSectors ) {
		return 0;
	}

	if( sector < 0 ) {
		return rr->times[rr->numSectors];   // SAFE!
	}

	// else
	return rr->times[sector];
}

// from AS
raceRun_t *G_NewRaceRun( edict_t *ent, int numSectors ) {
	gclient_t *cl;
	raceRun_t *rr;

	cl = ent->r.client;

	if( !ent->r.inuse || cl == NULL  ) {
		return 0;
	}

	rr = &cl->level.stats.currentRun;
	if( rr->times != NULL ) {
		G_LevelFree( rr->times );
	}

	rr->times = ( int64_t * )G_LevelMalloc( ( numSectors + 1 ) * sizeof( *rr->times ) );
	rr->numSectors = numSectors;
	rr->owner = cl->mm_session;
	if( cl->mm_session.IsValidSessionId() ) {
		rr->nickname[0] = '\0';
	} else {
		Q_strncpyz( rr->nickname, cl->netname, MAX_NAME_BYTES );
	}

	return rr;
}

// from AS
void G_SetRaceTime( edict_t *ent, int sector, int64_t time ) {
	gclient_t *cl;
	raceRun_t *rr;

	cl = ent->r.client;

	if( !ent->r.inuse || cl == NULL ) {
		return;
	}

	rr = &cl->level.stats.currentRun;
	if( sector < -1 || sector >= rr->numSectors ) {
		return;
	}

	// normal sector
	if( sector >= 0 ) {
		rr->times[sector] = time;
	} else if( rr->numSectors > 0 ) {
		raceRun_t *nrr; // new global racerun

		rr->times[rr->numSectors] = time;
		rr->timestamp = trap_Milliseconds();

		// validate the client
		// no bots for race, at all
		if( ent->r.svflags & SVF_FAKECLIENT /* && mm_debug_reportbots->value == 0 */ ) {
			G_Printf( "G_SetRaceTime: not reporting fakeclients\n" );
			return;
		}

		// Note: the test whether client session id has been removed,
		// race runs are reported for non-authenticated players too

		if( !game.raceruns ) {
			game.raceruns = LinearAllocator( sizeof( raceRun_t ), 0, _G_LevelMalloc, _G_LevelFree );
		}

		// push new run
		nrr = ( raceRun_t * )LA_Alloc( game.raceruns );
		memcpy( nrr, rr, sizeof( raceRun_t ) );

		// reuse this one in nrr
		rr->times = 0;

		// see if we have to push intermediate result
		// TODO: We can live with eventual consistency of race records, but it should be kept in mind
		// TODO: Send new race runs every N seconds, or if its likely to be a new record
		if( LA_Size( game.raceruns ) >= RACERUN_BATCH_SIZE ) {
			// Update an actual nickname that is going to be used to identify a run for a non-authenticated player
			if( !cl->mm_session.IsValidSessionId() ) {
				Q_strncpyz( rr->nickname, cl->netname, MAX_NAME_BYTES );
			}
			G_Match_SendReport();

			// double-check this for memory-leaks
			if( game.raceruns != 0 ) {
				LinearAllocator_Free( game.raceruns );
			}
			game.raceruns = 0;
		}
	}

	// Update an actual nickname that is going to be used to identify a run for a non-authenticated player
	if( !cl->mm_session.IsValidSessionId() ) {
		Q_strncpyz( rr->nickname, cl->netname, MAX_NAME_BYTES );
	}
}

void G_ListRaces_f( void ) {
	int i, j, size;
	raceRun_t *run;
	char uuid_buffer[UUID_BUFFER_SIZE];

	if( !game.raceruns || !LA_Size( game.raceruns ) ) {
		G_Printf( "No races to report\n" );
		return;
	}

	G_Printf( S_COLOR_RED "  session    " S_COLOR_YELLOW "times\n" );
	size = LA_Size( game.raceruns );
	for( i = 0; i < size; i++ ) {
		run = (raceRun_t*)LA_Pointer( game.raceruns, i );
		if( run->owner.IsValidSessionId() ) {
			G_Printf( S_COLOR_RED "  %s    " S_COLOR_YELLOW, run->owner.ToString( uuid_buffer ) );
		} else {
			// TODO: Is the nickname actually set at the time of this call?
			G_Printf( S_COLOR_RED "  %s    " S_COLOR_YELLOW, run->nickname );
		}
		for( j = 0; j < run->numSectors; j++ )
			G_Printf( "%" PRIi64 " ", run->times[j] );
		G_Printf( S_COLOR_GREEN "%" PRIi64 "\n", run->times[run->numSectors] );    // SAFE!
	}
}

//==========================================================

// free the collected statistics memory
void G_ClearStatistics( void ) {

}

//==========================================================
//		MM Reporting
//==========================================================


/*
* G_AddPlayerReport
*/
void G_AddPlayerReport( edict_t *ent, bool final ) {
	gclient_t *cl;
	gclient_quit_t *quit;
	int i;
	cvar_t *report_bots;
	char uuid_buffer[UUID_BUFFER_SIZE];

	// TODO: check if MM is enabled

	if( GS_RaceGametype() ) {
		// force sending report when someone disconnects
		G_Match_SendReport();
		return;
	}

	// if( !g_isSupportedGametype( gs.gametypeName ) )
	if( !GS_MMCompatible() ) {
		return;
	}

	cl = ent->r.client;

	if( !ent->r.inuse ) {
		return;
	}

	report_bots = trap_Cvar_Get( "sv_mm_debug_reportbots", "0", CVAR_CHEAT );

	if( ( ent->r.svflags & SVF_FAKECLIENT ) && !report_bots->integer ) {
		return;
	}

	if( cl == NULL || cl->team == TEAM_SPECTATOR ) {
		return;
	}

	mm_uuid_t mm_session = cl->mm_session;
	if( mm_session.IsZero() ) {
		const char *format = "G_AddPlayerReport: Client without session-id (%s" S_COLOR_WHITE ") %s\n\t(%s)\n";
		G_Printf( format, cl->netname, mm_session.ToString( uuid_buffer ), cl->userinfo );
		return;
	}

	// check merge situation
	for( quit = game.quits; quit; quit = quit->next ) {
		if( quit->mm_session == mm_session ) {
			break;
		}

		// ch : for unregistered players, merge stats by name
		if( mm_session.IsFFFs() && quit->mm_session.IsFFFs() ) {
			if( !strcmp( quit->netname, cl->netname ) ) {
				break;
			}
		}
	}

	// debug :
	G_Printf( "G_AddPlayerReport %s" S_COLOR_WHITE ", session %s\n", cl->netname, mm_session.ToString( uuid_buffer ) );

	if( quit ) {
		gameaward_t *award1, *award2;
		loggedFrag_t *frag1, *frag2;
		int j, inSize, outSize;

		// we can merge
		Q_strncpyz( quit->netname, cl->netname, sizeof( quit->netname ) );
		quit->team = cl->team;
		quit->timePlayed += ( level.time - cl->teamstate.timeStamp ) / 1000;
		quit->final = final;

		quit->stats.awards += cl->level.stats.awards;
		quit->stats.score += cl->level.stats.score;

		for( const auto &keyAndValue : cl->level.stats ) {
			quit->stats.AddToEntry( keyAndValue );
		}

		for( i = 0; i < ( AMMO_TOTAL - AMMO_GUNBLADE ); i++ ) {
			quit->stats.accuracy_damage[i] += cl->level.stats.accuracy_damage[i];
			quit->stats.accuracy_frags[i] += cl->level.stats.accuracy_frags[i];
			quit->stats.accuracy_hits[i] += cl->level.stats.accuracy_hits[i];
			quit->stats.accuracy_hits_air[i] += cl->level.stats.accuracy_hits_air[i];
			quit->stats.accuracy_hits_direct[i] += cl->level.stats.accuracy_hits_direct[i];
			quit->stats.accuracy_shots[i] += cl->level.stats.accuracy_shots[i];
		}

		// merge awards
		if( cl->level.stats.awardAllocator ) {
			if( !quit->stats.awardAllocator ) {
				quit->stats.awardAllocator = LinearAllocator( sizeof( gameaward_t ), 0, _G_LevelMalloc, _G_LevelFree );
			}

			inSize = LA_Size( cl->level.stats.awardAllocator );
			outSize = quit->stats.awardAllocator ? LA_Size( quit->stats.awardAllocator ) : 0;
			for( i = 0; i < inSize; i++ ) {
				award1 = ( gameaward_t * )LA_Pointer( cl->level.stats.awardAllocator, i );

				// search for matching one
				for( j = 0; j < outSize; j++ ) {
					award2 = ( gameaward_t * )LA_Pointer( quit->stats.awardAllocator, j );
					if( !strcmp( award1->name, award2->name ) ) {
						award2->count += award1->count;
						break;
					}
				}
				if( j >= outSize ) {
					award2 = ( gameaward_t * )LA_Alloc( quit->stats.awardAllocator );
					award2->name = award1->name;
					award2->count = award1->count;
				}
			}

			// we can free the old awards
			LinearAllocator_Free( cl->level.stats.awardAllocator );
			cl->level.stats.awardAllocator = 0;
		}

		// merge logged frags
		if( cl->level.stats.fragAllocator ) {
			inSize = LA_Size( cl->level.stats.fragAllocator );
			if( !quit->stats.fragAllocator ) {
				quit->stats.fragAllocator = LinearAllocator( sizeof( loggedFrag_t ), 0, _G_LevelMalloc, _G_LevelFree );
			}

			for( i = 0; i < inSize; i++ ) {
				frag1 = ( loggedFrag_t * )LA_Pointer( cl->level.stats.fragAllocator, i );
				frag2 = ( loggedFrag_t * )LA_Alloc( quit->stats.fragAllocator );
				memcpy( frag2, frag1, sizeof( *frag1 ) );
			}

			// we can free the old frags
			LinearAllocator_Free( cl->level.stats.fragAllocator );
			cl->level.stats.fragAllocator = 0;
		}
	} else {
		// create a new quit structure
		quit = new( G_Malloc( sizeof( gclient_quit_t ) ) )gclient_quit_t;

		// fill in the data
		Q_strncpyz( quit->netname, cl->netname, sizeof( quit->netname ) );
		quit->team = cl->team;
		quit->timePlayed = ( level.time - cl->teamstate.timeStamp ) / 1000;
		quit->final = final;
		quit->mm_session = mm_session;
		quit->stats = std::move( cl->level.stats );
		// TODO: Not sure what reasons are
		quit->stats.fragAllocator = NULL;

		// put it to the list
		quit->next = game.quits;
		game.quits = quit;
	}
}

// It is assumed that the writer points to the root object.
static void G_MatchReport_WriteHeaderFields( QueryWriter &writer, int teamGame ) {
	// Note: booleans are transmitted as integers due to underlying api limitations
	writer << "match_id"       << trap_GetConfigString( CS_MATCHUUID );
	writer << "gametype"       << gs.gametypeName;
	writer << "map_name"       << level.mapname;
	writer << "server_name"    << trap_Cvar_String( "sv_hostname" );
	writer << "time_played"    << level.finalMatchDuration / 1000;
	writer << "time_limit"     << GS_MatchDuration() / 1000;
	writer << "score_limit"    << g_scorelimit->integer;
	writer << "is_instagib"    << ( GS_Instagib() ? 1 : 0 );
	writer << "is_team_game"   << ( teamGame ? 1 : 0 );
	writer << "is_race_game"   << ( GS_RaceGametype() ? 1 : 0 );
	writer << "mod_name"       << trap_Cvar_String( "fs_game" );

	// TODO: Write match start time!

	if( g_autorecord->integer ) {
		writer << "demo_filename" << va( "%s%s", level.autorecord_name, game.demoExtension );
	}
}

static void G_MatchReport_AddPlayerAwards( QueryWriter &writer, gclient_quit_t *cl );
static void G_MatchReport_AddPlayerLogFrags( QueryWriter &writer, gclient_quit_t *cl );
static void G_MatchReport_AddPlayerWeapons( QueryWriter &writer, gclient_quit_t *cl, const char **weaponNames );

static void G_Match_SendRegularReport( void ) {
	gclient_quit_t *cl;
	int i, teamGame, duelGame;
	static const char *weapnames[WEAP_TOTAL] = { NULL };

	// Feature: do not report matches with duration less than 1 minute (actually 66 seconds)
	if( level.finalMatchDuration <= SIGNIFICANT_MATCH_DURATION ) {
		return;
	}

	QueryWriter writer( sq_api, "server/matchReport" );

	// ch : race properties through GS_RaceGametype()

	// official duel frag support
	duelGame = GS_TeamBasedGametype() && GS_MaxPlayersInTeam() == 1 ? 1 : 0;

	// ch : fixme do mark duels as teamgames
	if( duelGame ) {
		teamGame = 0;
	} else if( !GS_TeamBasedGametype()) {
		teamGame = 0;
	} else {
		teamGame = 1;
	}

	G_MatchReport_WriteHeaderFields( writer, teamGame );

	// Write team properties (if any)
	if( teamlist[TEAM_ALPHA].numplayers > 0 && teamGame != 0 ) {
		writer << "teams" << '[';
		{
			for( i = TEAM_ALPHA; i <= TEAM_BETA; i++ ) {
				writer << '{';
				{
					writer << "name" << trap_GetConfigString( CS_TEAM_SPECTATOR_NAME + ( i - TEAM_SPECTATOR ));
					// TODO:... What do Statsow controllers expect?
					writer << "index" << i - TEAM_ALPHA;
					writer << "score" << teamlist[i].stats.score;
				}
				writer << '}';
			}
		}
		writer << ']';
	}

	// Provide the weapon indices for the stats server
	// Note that redundant weapons (that were not used) are allowed to be present here
	writer << "weapon_indices" << '{';
	{
		for( int j = 0; j < WEAP_TOTAL; ++j ) {
			weapnames[j] = GS_FindItemByTag( WEAP_GUNBLADE + j )->shortname;
			writer << weapnames[j] << j;
		}
	}
	writer << '}';

	// Write player properties
	writer << "players" << '[';
	for( cl = game.quits; cl; cl = cl->next ) {
		writer << '{';
		{
			writer << "session_id"  << cl->mm_session;
			writer << "name"        << cl->netname;
			writer << "score"       << cl->stats.score;
			writer << "time_played" << cl->timePlayed;
			writer << "is_final"    << ( cl->final ? 1 : 0 );

			writer << "various_stats" << '{';
			{
				for( const auto &keyAndValue: cl->stats ) {
					writer << keyAndValue.first << keyAndValue.second;
				}
			}
			writer << '}';

			if( teamGame != 0 ) {
				writer << "team" << cl->team - TEAM_ALPHA;
			}

			G_MatchReport_AddPlayerAwards( writer, cl );
			G_MatchReport_AddPlayerWeapons( writer, cl, weapnames );
			G_MatchReport_AddPlayerLogFrags( writer, cl );
		}
		writer << '}';
	}
	writer << ']';

	writer.Send();
}

static void G_MatchReport_AddPlayerAwards( QueryWriter &writer, gclient_quit_t *cl ) {
	const auto *stats = &cl->stats;
	if( !stats->awardAllocator || !LA_Size( stats->awardAllocator ) ) {
		return;
	}

	writer << "awards" << '[';
	{
		for( size_t i = 0, size = LA_Size( stats->awardAllocator ); i < size; i++ ) {
			const auto *ga = (gameaward_t *)LA_Pointer( stats->awardAllocator, i );
			writer << '{';
			{
				writer << "name"  << ga->name;
				writer << "count" << ga->count;
			}
			writer << '}';
		}
	}
	writer << ']';
}

static void G_MatchReport_AddPlayerLogFrags( QueryWriter &writer, gclient_quit_t *cl ) {
	const auto *stats = &cl->stats;
	if( !stats->fragAllocator || !LA_Size( stats->fragAllocator ) ) {
		return;
	}

	writer << "log_frags" << '[';
	{
		for( size_t i = 0, size = LA_Size( stats->fragAllocator ); i < size; i++ ) {
			const auto *frag = (loggedFrag_t *)LA_Pointer( stats->fragAllocator, i );
			writer << '{';
			{
				writer << "victim" << frag->victim;
				writer << "weapon" << frag->weapon;
				writer << "time" << frag->time;
			}
			writer << '}';
		}
	}
	writer << ']';
}

// TODO: Should be lifted to gameshared
static inline double ComputeAccuracy( int hits, int shots ) {
	if( !hits ) {
		return 0.0;
	}
	if( hits == shots ) {
		return 100.0;
	}

	// copied from cg_scoreboard.c, but changed the last -1 to 0 (no hits is zero acc, right??)
	return ( min( (int)( floor( ( 100.0f * ( hits ) ) / ( (float)( shots ) ) + 0.5f ) ), 99 ) );
}

static void G_MatchReport_AddPlayerWeapons( QueryWriter &writer, gclient_quit_t *cl, const char **weaponNames ) {
	const auto *stats = &cl->stats;
	int i;

	// first pass calculate the number of weapons, see if we even need this section
	for( i = 0; i < ( AMMO_TOTAL - WEAP_TOTAL ); i++ ) {
		if( stats->accuracy_shots[i] > 0 ) {
			break;
		}
	}

	if( i >= ( AMMO_TOTAL - WEAP_TOTAL ) ) {
		return;
	}

	writer << "weapons" << '[';
	{
		int j;

		// we only loop thru the lower section of weapons since we put both
		// weak and strong shots inside the same weapon
		for( j = 0; j < AMMO_WEAK_GUNBLADE - WEAP_TOTAL; j++ ) {
			const int weak = j + ( AMMO_WEAK_GUNBLADE - WEAP_TOTAL );
			// Don't submit unused weapons
			if( stats->accuracy_shots[j] == 0 && stats->accuracy_shots[weak] == 0 ) {
				continue;
			}

			writer << '{';
			{
				writer << "name" << weaponNames[j];

				writer << "various_stats" << '{';
				{
					// STRONG
					int hits = stats->accuracy_hits[j];
					int shots = stats->accuracy_shots[j];

					writer << "strong_hits"   << hits;
					writer << "strong_shots"  << shots;
					writer << "strong_acc"    << ComputeAccuracy( hits, shots );
					writer << "strong_dmg"    << stats->accuracy_damage[j];
					writer << "strong_frags"  << stats->accuracy_frags[j];

					// WEAK
					hits = stats->accuracy_hits[weak];
					shots = stats->accuracy_shots[weak];

					writer << "weak_hits"   << hits;
					writer << "weak_shots"  << shots;
					writer << "weak_acc"    << ComputeAccuracy( hits, shots );
					writer << "weak_dmg"    << stats->accuracy_damage[weak];
					writer << "weak_frags"  << stats->accuracy_frags[weak];
				}
				writer << '}';
			}
			writer << '}';
		}
	}
	writer << ']';
}

/*
* G_Match_SendReport
*/
void G_Match_SendReport( void ) {
	edict_t *ent;
	gclient_quit_t *qcl, *qnext;
	int numPlayers;

	// TODO: check if MM is enabled

	sq_api = trap_GetStatQueryAPI();
	if( !sq_api ) {
		return;
	}

	if( GS_RaceGametype() ) {
		G_Match_SendRaceReport();
		return;
	}

	// if( g_isSupportedGametype( gs.gametypeName ) )
	if( GS_MMCompatible() ) {
		// merge game.clients with game.quits
		for( ent = game.edicts + 1; PLAYERNUM( ent ) < gs.maxclients; ent++ )
			G_AddPlayerReport( ent, true );

		// check if we have enough players to report (at least 2)
		numPlayers = 0;
		for( qcl = game.quits; qcl ; qcl = qcl->next )
			numPlayers++;

		if( numPlayers > 1 ) {
			G_Match_SendRegularReport();
		}
	}

	// clear the quit-list
	qnext = NULL;
	for( qcl = game.quits; qcl ; qcl = qnext ) {
		qnext = qcl->next;
		G_Free( qcl );
	}
	game.quits = NULL;
}

/*
* G_Match_SendRaceReport
*/
static void G_Match_SendRaceReport( void ) {
	if( !GS_RaceGametype() ) {
		G_Printf( "G_Match_RaceReport.. not race gametype\n" );
		return;
	}

	if( !game.raceruns || !LA_Size( game.raceruns ) ) {
		return;
	}

	QueryWriter writer( sq_api, "server/matchReport" );

	G_MatchReport_WriteHeaderFields( writer, false );

	writer << "race_runs" << '[';
	{
		size_t size = LA_Size( game.raceruns );
		for( size_t i = 0; i < size; i++ ) {
			raceRun_t *prr = (raceRun_t*)LA_Pointer( game.raceruns, i );

			writer << '{';
			{
				// Setting session id and nickname is mutually exclusive
				if( prr->owner.IsValidSessionId() ) {
					writer << "session_id" << prr->owner;
				} else {
					writer << "nickname" << prr->nickname;
				}

				// TODO: This is not a real-world UTC time!
				writer << "timestamp" << prr->timestamp;

				writer << "times" << '[';
				{
					// Accessing the "+1" element is legal (its the final time). Supply it along with other times.
					for( int j = 0; j < prr->numSectors + 1; j++ )
						writer << prr->times[j];
				}
				writer << ']';
			}
			writer << '}';
		}
	}
	writer << ']';

	writer.Send();

	// clear gameruns
	LinearAllocator_Free( game.raceruns );
	game.raceruns = NULL;
}
