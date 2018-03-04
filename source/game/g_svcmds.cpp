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
#include "../qcommon/qcommon.h"

#undef min
#undef max
#include <new>
#include <algorithm>

/*
* Cmd_ConsoleSay_f
*/
static void Cmd_ConsoleSay_f( void ) {
	G_ChatMsg( NULL, NULL, false, "%s", trap_Cmd_Args() );
}


/*
* Cmd_ConsoleKick_f
*/
static void Cmd_ConsoleKick_f( void ) {
	edict_t *ent;

	if( trap_Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: kick <id or name>\n" );
		return;
	}

	ent = G_PlayerForText( trap_Cmd_Argv( 1 ) );
	if( !ent ) {
		Com_Printf( "No such player\n" );
		return;
	}

	trap_DropClient( ent, DROP_TYPE_NORECONNECT, "Kicked" );
}


/*
* Cmd_Match_f
*/
static void Cmd_Match_f( void ) {
	const char *cmd;

	if( trap_Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: match <option: restart|advance|status>\n" );
		return;
	}

	cmd = trap_Cmd_Argv( 1 );
	if( !Q_stricmp( cmd, "restart" ) ) {
		level.exitNow = false;
		level.hardReset = false;
		Q_strncpyz( level.forcemap, level.mapname, sizeof( level.mapname ) );
		G_EndMatch();
	} else if( !Q_stricmp( cmd, "advance" ) ) {
		level.exitNow = false;
		level.hardReset = true;

		//		level.forcemap[0] = 0;
		G_EndMatch();
	} else if( !Q_stricmp( cmd, "status" ) ) {
		trap_Cmd_ExecuteText( EXEC_APPEND, "status" );
	}
}

//==============================================================================
//
// PACKET FILTERING
//
//
// You can add or remove addresses from the filter list with:
//
// addip <ip>
// removeip <ip>
//
// The IP address is specified in dot format, and any unspecified digits will match any value,
// so you can specify an entire class C network with "addip 192.246.40".
//
// IP v6 support has been added too. These formats are supported:
// 1) Vanilla IP v6 notation: up to eight hexadecimal numbers separated by colons.
// any unspecified numbers will match any value (so filter masks are limited to multiples of 16 bits)
// If a single number is specified, you must add a trailing colon to make a distinction with IP v4 filters
// 2) IP v6 with a double-colon shorthand for spanning zeros. A filter will match the specified address exactly.
// 3) Mapped IP v4 filters in format ::ffff::[V4 filter],
// where the V4 filter part must obey V4 filter representation rules mentioned above.
//
// removeip will only remove an address specified exactly the same way.
// You cannot addip a subnet, then removeip a single host.
//
// listip
// Prints the current list of filters.
//
// writeip
// Dumps "addip <ip>" commands to listip.cfg so it can be execed at a later date.
// The filter lists are not saved and restored by default, because I beleive it would cause too much confusion.
//
// filterban <0 or 1>
//
// If 1 (the default), then ip addresses matching the current list will be prohibited from entering the game.
// This is the default setting.
//
// If 0, then only addresses matching the list will be allowed.
// This lets you easily set up a private game, or a game that only allows players from your local network.
//
//==============================================================================

#define MAX_IPFILTERS   1024

// TODO: Lift these utilities to main headers and unify the linked lists codebase

template<typename Node>
static inline Node *Unlink( Node *node, Node **listHeadRef, int linksIndex ) {
	if( auto *next = node->next[linksIndex] ) {
		next->prev[linksIndex] = node->prev[linksIndex];
	}
	if( auto *prev = node->prev[linksIndex] ) {
		prev->next[linksIndex] = node->next[linksIndex];
	} else {
		assert( node == *listHeadRef );
		*listHeadRef = node->next[linksIndex];
	}

	node->prev[linksIndex] = nullptr;
	node->next[linksIndex] = nullptr;
	return node;
}

template<typename Node>
static inline Node *Link( Node *node, Node **listHeadRef, int linksIndex ) {
	if( *listHeadRef ) {
		( *listHeadRef )->prev[linksIndex] = node;
	}
	node->prev[linksIndex] = nullptr;
	node->next[linksIndex] = *listHeadRef;
	*listHeadRef = node;
	return node;
}

class GIPFilter {
	struct GroupHeader;

	struct alignas( 8 )Entry {
		// GIPFilter::NO_TIMEOUT (std::numeric_limits<int64_t>::max()) if not specified
		// This is to reduce branching in timeout tests
		int64_t timeout;

		// Links for used/free and bin lists
		Entry *prev[2];
		Entry *next[2];

		// The corresponding group header
		GroupHeader *group;
		// The bin where the entry is linked to (as a pointer to it to be able to modify its value when unlinking)
		Entry **binRef;

		alignas( 8 )uint8_t prefix[16];

		// A type (address family) of the address
		netadrtype_t type;
	};

	struct V4IPMatcher {
		bool operator()( const Entry *entry, const uint8_t *requestPrefix ) const {
			return ( AsUint32( requestPrefix ) & AsUint32( entry->group->mask ) ) == AsUint32( entry->prefix );
		}
	};

	struct V4PrefixMatcher {
		bool operator()( const Entry *entry, const uint8_t *requestPrefix ) const {
			return AsUint32( entry->prefix ) == AsUint32( requestPrefix );
		}
	};

	static uint32_t AsUint32( const uint8_t *p ) {
		assert( !( ( (uintptr_t)p ) % 8 ) );
		return *( const uint32_t *)p;
	}

	static uint64_t AsUint64( const uint8_t *p ) {
		assert( !( ( (uintptr_t)p ) % 8 ) );
		return *( const uint64_t * )p;
	}

	struct V6IPMatcher {
		bool operator()( const Entry *entry, const uint8_t *requestPrefix ) const;
	};

	struct V6PrefixMatcher {
		bool operator()( const Entry *entry, const uint8_t *requestPrefix ) const;
	};

	struct V4BinIndexComputer {
		unsigned operator()( const uint8_t *octets, const uint8_t *binMask ) const;
	};

	struct V6BinIndexComputer {
		unsigned operator()( const uint8_t *octets, const uint8_t *binMask ) const;
	};

	enum { V4, V6 };
	enum { LIST_LINKS, BIN_LINKS };
	enum { GROUP_LINKS };
	enum { NUM_BINS_PER_GROUP = 283 };
	enum { NUM_V4_GROUPS = 4 };
	enum { NUM_V6_GROUPS = 15 };
	enum { ILLEGAL_GROUP = ~0u };

	// Initialize lazily
	Entry *entries;

	// Using a hash map is trivial, unfortunately it does not work for wildcard filters.
	// (a hash for a given value varies depending of the filter mask).

	// If we limit filter length in bits to values that are multiple of 8,
	// we can group filters by mask length and iterate over these groups.
	// A separate hash bin is used for every mask group
	// A mask group is defined by mask length (mask length in bits is non-zero and is a multiple of 8).
	// The first group corresponds to the all-bits-set mask.
	// Only active (that actually have corresponding filters) are linked in an active groups list.
	// Thus groups that do not have corresponding filters are excluded from sequential testing.
	mutable Entry *v4GroupBins[NUM_V4_GROUPS * NUM_BINS_PER_GROUP];
	// Almost the same for IPv6
	mutable Entry *v6GroupBins[NUM_V6_GROUPS * NUM_BINS_PER_GROUP];

	mutable Entry **binsForAddress[2];

	mutable uint8_t v4GroupMasks[4 * NUM_V4_GROUPS];
	mutable uint8_t v6GroupMasks[16 * NUM_V6_GROUPS];

	mutable uint8_t *groupMasksForAddress[2];

	struct GroupHeader {
		// Prev and next pointers as a pointer arrays to use the generic linked list facilities
		GroupHeader *prev[1], *next[1];
		// A pointer to the array of bin heads for the group
		Entry **bins;
		// An address mask that is unique for the group and defines it
		const uint8_t *mask;
		// Should be positive for active (linked in active groups list) groups
		unsigned numFiltersInUse;
	};

	// A storage for V4 and V6 group headers
	mutable GroupHeader v4GroupHeaders[NUM_V4_GROUPS];
	mutable GroupHeader v6GroupHeaders[NUM_V6_GROUPS];

	// Heads of active groups lists for V4 and V6 families
	mutable GroupHeader *groupListHeads[2];
	// Points to (v4|v6)GroupHeaders, useful for selection by index
	mutable GroupHeader *groupsForAddress[2];

	// A linked list of unused entries ready to be (re-)used
	mutable Entry *freeListHead;
	// A linked list of all used filter entries, useful for iteration over all linked entries
	mutable Entry *usedListHead;

	static GIPFilter *instance;

	void UnlinkAndFreeEntry( Entry *entry );

	// 0 for V4, 1 for V6
	inline unsigned AddressIndexForType( netadrtype_t type ) const;
	inline unsigned AddressLengthForIndex( unsigned index ) const;

	template<typename EntryMatcher, typename IndexComputer>
	const Entry *MatchIP( const uint8_t *prefix_, const GroupHeader *activeGroups ) const;

	GIPFilter();
	~GIPFilter();

	Entry *AllocEntry();
	Entry *AllocAndLinkEntry( unsigned addressIndex, unsigned groupIndex, unsigned binIndex );

	template<typename PrefixMatcher, typename IPMatcher, typename IndexComputer>
	int TryAddEntry( const uint8_t *prefix, netadrtype_t type, unsigned groupIndex, int64_t timeout );

	template<typename IPMatcher>
	void RemoveNarrowerFilters( const Entry *newlyAddedEntry );
public:
	static constexpr auto NO_TIMEOUT = std::numeric_limits<int64_t>::max();

	// Reads prefix octets from a given string.
	// Octet buffer is assumed to have a sufficient capacity for all legal addresses.
	static unsigned ParseFilter( const char *filter, uint8_t *prefix,
								 netadrtype_t *type, const char *terminators = "\0" );

	// Returns a group index for a parsed mask (ILLEGAL_GROUP on failure)
	// A custom terminator characters set is allowed to be specified in addition to always assumed \0
	static unsigned ParseFilterV4( const char *filter, uint8_t *prefix, const char *terminators = "\0" );
	static unsigned ParseFilterV6( const char *filter, uint8_t *prefix, const char *terminators = "\0" );
	static unsigned ParseFilterV4MappedToV6( const char *filter, uint8_t *prefix, const char *terminators = "\0" );

	static bool StringToAddress( const char *s, netadr_t *addr );

	// Sometimes garbage collection on an access is not acceptable but only valid entries are expected to be accessed
	static inline Entry *NextValidEntry( Entry *entry );

	static Entry *FirstValidEntry( Entry *entry );
public:
	class const_iterator;

	// We don't want to expose guts
	class EntryRef {
		friend class GIPFilter;
		friend class GIPFilter::const_iterator;
		Entry *entry;

		explicit EntryRef( Entry *entry_ ): entry( entry_ ) {}
	public:
		size_t PrintTo( char *buffer, size_t bufferSize ) const;
	};

	class const_iterator {
		friend class GIPFilter;
		Entry *entry;
		explicit const_iterator( Entry *entry_ ): entry( entry_ ) {}
	public:
		const_iterator &operator++() {
			entry = NextValidEntry( entry );
			return *this;
		}

		bool operator!=( const const_iterator &that ) const {
			return entry != that.entry;
		}

		EntryRef operator *() const { return EntryRef( entry ); }
	};

	const_iterator begin() const { return const_iterator( FirstValidEntry( usedListHead ) ); }
	const_iterator end() const { return const_iterator( nullptr ); }

	void Clear();

	void SetupGroups( unsigned addressIndex, unsigned groupLength, unsigned numGroups );

	static void Init();
	static void Shutdown();

	static inline GIPFilter *Instance() { return instance; }

	bool Match( const char *ip ) const;

	// Positive = success
	// Zero = internal failure
	// Negative = malformed input
	int AddFilterFromString( const char *ip, int64_t timeout = NO_TIMEOUT );
	int RemoveFilterByString( const char *ip );
};

GIPFilter *GIPFilter::instance = nullptr;

bool GIPFilter::V6IPMatcher::operator()( const Entry *entry, const uint8_t *requestPrefix ) const {
	const uint8_t *groupMask = entry->group->mask;
	for( int i : { 0, 8 } ) {
		if( ( AsUint64( requestPrefix + i ) & AsUint64( groupMask ) ) != AsUint64( entry->prefix + i ) ) {
			return false;
		}
	}

	return true;
}

bool GIPFilter::V6PrefixMatcher::operator()( const Entry *entry, const uint8_t *requestPrefix ) const {
	for( int i : { 0, 8 } ) {
		if( AsUint64( requestPrefix + i ) != AsUint64( entry->prefix + i ) ) {
			return false;
		}
	}

	return true;
}

unsigned GIPFilter::V4BinIndexComputer::operator()( const uint8_t *octets, const uint8_t *binMask ) const {
	assert( !( (uintptr_t)octets % 4 ) );
	assert( !( (uintptr_t)binMask % 4 ) );
	return ( *( const uint32_t *)octets & *(const uint32_t *)binMask ) % NUM_V4_GROUPS;
}

unsigned GIPFilter::V6BinIndexComputer::operator()( const uint8_t *octets, const uint8_t *binMask ) const {
	assert( !( (uintptr_t)octets % 4 ) );
	assert( !( (uintptr_t)binMask % 4 ) );
	const auto *o = (const uint32_t *)octets;
	const auto *m = (const uint32_t *)binMask;
	uint32_t hash = 17;
	hash = hash * 31 + ( o[0] & m[0] );
	hash = hash * 31 + ( o[1] & m[1] );
	hash = hash * 31 + ( o[2] & m[2] );
	hash = hash * 31 + ( o[3] & m[3] );
	return hash % NUM_V6_GROUPS;
}

void GIPFilter::Clear() {
	memset( v4GroupBins, 0, sizeof( v4GroupBins ) );
	memset( v6GroupBins, 0, sizeof( v6GroupBins ) );

	groupsForAddress[V4] = v4GroupHeaders;
	groupsForAddress[V6] = v6GroupHeaders;

	groupMasksForAddress[V4] = v4GroupMasks;
	groupMasksForAddress[V6] = v6GroupMasks;

	binsForAddress[V4] = v4GroupBins;
	binsForAddress[V6] = v6GroupBins;

	SetupGroups( V4, 4, NUM_V4_GROUPS );
	SetupGroups( V6, 16, NUM_V6_GROUPS );

	groupListHeads[V4] = nullptr;
	groupListHeads[V6] = nullptr;

	freeListHead = nullptr;
	usedListHead = nullptr;

	if( !entries ) {
		return;
	}

	for( int i = 0; i < MAX_IPFILTERS; ++i ) {
		entries[i].prev[LIST_LINKS] = entries + i - 1;
		entries[i].next[LIST_LINKS] = entries + i + 1;
	}

	entries[0].prev[LIST_LINKS] = nullptr;
	entries[MAX_IPFILTERS - 1].next[LIST_LINKS] = nullptr;

	freeListHead = entries;
}

void GIPFilter::SetupGroups( unsigned addressIndex, unsigned groupLength, unsigned numGroups ) {
	uint8_t *mask = groupMasksForAddress[addressIndex];
	GroupHeader *header = groupsForAddress[addressIndex];
	Entry **bins = binsForAddress[addressIndex];

	for( unsigned i = 0; i < numGroups; ++i ) {
		header->mask = mask;
		header->numFiltersInUse = 0;
		// Headers are initially not linked
		header->prev[GROUP_LINKS] = nullptr;
		header->next[GROUP_LINKS] = nullptr;
		header->bins = bins + i * NUM_BINS_PER_GROUP;
		header++;

		// Set all bits in first groupLength - i bytes
		memset( mask, 0xFF, groupLength - i );
		mask += groupLength - i;
		// Clear last i bytes
		memset( mask, 0, i );
		mask += i;
	}
}

void GIPFilter::UnlinkAndFreeEntry( Entry *entry ) {
	unsigned addressIndex = AddressIndexForType( entry->type );
	GroupHeader *header = entry->group;

	// Unlink from hash bin
	Unlink( entry, entry->binRef, BIN_LINKS );
	// Unlink from used list
	Unlink( entry, &usedListHead, LIST_LINKS );
	// Link to free list
	Link( entry, &freeListHead, LIST_LINKS );

	// Decrease the number of filters in use for the mask group
	header->numFiltersInUse--;
	// If there is no filters in use left for the group, unlink the group header from the active headers list
	if( !header->numFiltersInUse ) {
		Unlink( header, &groupListHeads[addressIndex], GROUP_LINKS );
	}

	// Prevent use-after-free bugs
	entry->group = nullptr;
	entry->binRef = nullptr;
	entry->type = NA_NOTRANSMIT;
}

inline unsigned GIPFilter::AddressIndexForType( netadrtype_t type ) const {
	assert( type == NA_IP || type == NA_IP6 );
	static_assert( NA_IP == 2, "The values shift has to be recalculated" );
	static_assert( NA_IP6 == 3, "The values shift has to be recalculated" );
	return (unsigned)( type - NA_IP );
}

inline unsigned GIPFilter::AddressLengthForIndex( unsigned index ) const {
	if( index == 0 ) return 4;
	if( index == 1 ) return 16;
	assert( false && "Unreachable" );
	return ~0u;
}

template<typename EntryMatcher, typename IndexComputer>
const GIPFilter::Entry *GIPFilter::MatchIP( const uint8_t *prefix_, const GroupHeader *activeGroups ) const {
	IndexComputer indexComputer;
	EntryMatcher entryMatcher;

	const auto serverTime = game.serverTime;
	// For every active mask length group in active groups list
	for( const GroupHeader *group = activeGroups; group; group = group->next[GROUP_LINKS] ) {
		const Entry *nextEntry = nullptr;
		// Compute the bin index using the group mask
		const unsigned binIndex = indexComputer( prefix_, group->mask );
		// Iterate over list of entries in the corresponding hash bin collecting garbage at the same time
		for( const Entry *entry = group->bins[binIndex]; entry; entry = nextEntry ) {
			// Save the next link to avoid use-after-free
			nextEntry = entry->next[BIN_LINKS];
			// Filters that do not specify timeout use the max numeric type value for the timeout
			if( entry->timeout > serverTime ) {
				if( entryMatcher( entry, prefix_ ) ) {
					return entry;
				}
				continue;
			}
			const_cast<GIPFilter *>( this )->UnlinkAndFreeEntry( const_cast<Entry *>( entry ) );
		}
	}

	return nullptr;
}

GIPFilter::GIPFilter() {
	Clear();
}

GIPFilter::~GIPFilter() {
	if( entries ) {
		G_Free( entries );
	}
}

GIPFilter::Entry *GIPFilter::AllocEntry() {
	if( !entries ) {
		entries = ( Entry *)G_Malloc( sizeof( Entry ) * MAX_IPFILTERS );
		Clear();
	}

	if( freeListHead ) {
		return Unlink( freeListHead, &freeListHead, LIST_LINKS );
	}

	// Try collecting garbage
	const auto serverTime = game.serverTime;
	Entry *nextEntry = nullptr;
	for( Entry *entry = usedListHead; entry; entry = nextEntry ) {
		nextEntry = entry->next[LIST_LINKS];
		if( nextEntry->timeout && nextEntry->timeout < serverTime ) {
			UnlinkAndFreeEntry( entry );
			continue;
		}
	}

	if( freeListHead ) {
		return Unlink( freeListHead, &freeListHead, LIST_LINKS );
	}

	return nullptr;
}

GIPFilter::Entry *GIPFilter::AllocAndLinkEntry( unsigned addressIndex, unsigned groupIndex, unsigned binIndex ) {
	assert( ( addressIndex & ~1 ) == 0 );
	assert( binIndex < NUM_BINS_PER_GROUP );
	if( !addressIndex ) {
		assert( groupIndex < 4 );
	} else {
		assert( groupIndex < 15 );
	}

	Entry *entry = AllocEntry();
	if( !entry ) {
		return nullptr;
	}

	// Link to the used list
	Link( entry, &usedListHead, LIST_LINKS );

	GroupHeader *group = &groupsForAddress[addressIndex][groupIndex];

	// Link to the hash bin
	Link( entry, group->bins + binIndex, BIN_LINKS );

	entry->group = group;
	entry->binRef = group->bins + binIndex;

	group->numFiltersInUse++;
	// If there were no active filters for the group that is described by the group
	if( group->numFiltersInUse == 1 ) {
		// Link the group to the list of active groups
		Link( group, &groupListHeads[addressIndex], GROUP_LINKS );
	}

	return entry;
}

unsigned GIPFilter::ParseFilter( const char *filter, uint8_t *prefix, netadrtype_t *type, const char *terminators ) {
	netadrtype_t resultType;
	unsigned resultGroup;

	// We have to guess a family of the filter
	// Things get complicated once we allow an optional trailing port.
	// If there are termnators specified, assume the address
	// to have IP v6 family if terminators contain the closing bracket.
	// Otherwise assume the address to have IP v6 family if there is a colon character in the input.

	bool isV4 = true;
	if( *terminators ) {
		if( strchr( terminators, ']' ) ) {
			isV4 = false;
		}
	} else if( strchr( filter, ':' ) ) {
		isV4 = false;
	}

	if( isV4 ) {
		resultType = NA_IP;
		resultGroup = ParseFilterV4( filter, prefix, terminators );
	} else {
		resultType = NA_IP6;
		if( !strchr( filter, '.' ) ) {
			resultGroup = ParseFilterV6( filter, prefix, terminators );
		} else {
			resultGroup = ParseFilterV4MappedToV6( filter, prefix, terminators );
		}
	}

	if( resultGroup != ILLEGAL_GROUP ) {
		*type = resultType;
	}

	return resultGroup;
}

unsigned GIPFilter::ParseFilterV4( const char *filter, uint8_t *prefix, const char *terminators ) {
	int i;

	char *endptr = nullptr;
	for( i = 0; i < 4; ++i ) {
		unsigned long parsed = strtoul( filter, &endptr, 10 );
		if( parsed > 0xFF ) {
			return ILLEGAL_GROUP;
		}
		if( endptr == filter ) {
			// Allow a trailing . after a single octet.
			// This is mainly to make a distinction between
			// ::ffff:3 which should be parsed as V6 addres and
			// ::ffff:3. which should be parsed as V4-mapped-to-V6 address
			if( i == 1 ) {
				prefix[1] = prefix[2] = prefix[3] = 0;
				return 3;
			}
			return ILLEGAL_GROUP;
		}

		prefix[i] = (uint8_t)parsed;
		if( !*endptr || strchr( terminators, *endptr ) ) {
			// Advance the index to avoid filling the last octet by zero
			i++;
			break;
		}

		if( *endptr != '.' ) {
			return ILLEGAL_GROUP;
		}

		filter = endptr + 1;
	}

	if( *endptr && !strchr( terminators, *endptr ) ) {
		return ILLEGAL_GROUP;
	}

	// If the prefix is zero
	if( !( *(uint32_t *)prefix ) ) {
		return ILLEGAL_GROUP;
	}

	const unsigned result = 4u - i;

	// Fill trailing prefix bytes
	for(; i < 4; ++i ) {
		prefix[i] = 0;
	}

	return result;
}

unsigned GIPFilter::ParseFilterV6( const char *filter, uint8_t *prefix, const char *terminators ) {
	int j = 0;
	int jBeforeDoubleColon = -1;

	// Parts of prefix written after a shortcut has been met
	uint8_t prefixTail[16];

	uint8_t *writtenPrefix = prefix;

	const char *ptr = filter;
	char *endptr = const_cast<char *>( filter );
	for( int i = 0; i < 8; ++i ) {
		if( !*endptr || strchr( terminators, *endptr ) ) {
			break;
		}
		if( *endptr == ':' ) {
			// Look ahead a character
			const char ch = *( endptr + 1 );
			// Disallow leading ':' except it is a shortcut start
			if( ptr == filter ) {
				if( ch != ':' ) {
					return ILLEGAL_GROUP;
				}
			}

			// Allow trailing ':' in case when there was a single group to make a distiction between V4 and V6 addresses
			if( !ch || strchr( terminators, ch ) ) {
				if( i == 1 ) {
					break;
				}
				return ILLEGAL_GROUP;
			}
			// If the next character defines a group
			if( ( ch >= '0' && ch <= '9' ) || ( ch >= 'a' && ch <= 'f' ) || ( ch >= 'A' && ch <= 'F' ) ) {
				// Skip the colon
				ptr = endptr = endptr + 1;
				// Restart the search for i-th group
				i--;
				continue;
			}
			if( ch != ':' ) {
				return ILLEGAL_GROUP;
			}
			// Double colons shortcut "::" has been matched.
			// Check whether the algorithm already in double colon state
			if( jBeforeDoubleColon >= 0 ) {
				return ILLEGAL_GROUP;
			}
			jBeforeDoubleColon = j;
			j = 0;
			writtenPrefix = prefixTail;
			// Skip the "::" shortcut
			ptr = endptr = endptr + 2;
			// Restart the search for i-th group
			i--;
			continue;
		}

		unsigned long parsed = strtoul( ptr, &endptr, 16 );
		if( parsed > 0xFFFF ) {
			return ILLEGAL_GROUP;
		}
		if( endptr == ptr ) {
			if( !*endptr || strchr( terminators, *endptr ) ) {
				break;
			}
			return ILLEGAL_GROUP;
		}

		// TODO: How do we specify masks with octet granularity?

		writtenPrefix[j] = (uint8_t)( parsed >> 8 );
		j++;

		writtenPrefix[j] = (uint8_t)( parsed & 0xFF );
		j++;

		ptr = endptr;
	}

	// If there has been a :: shortcut met
	if( jBeforeDoubleColon >= 0 ) {
		// There were no octets parsed, disallow zero prefix
		if( j + jBeforeDoubleColon == 0 ) {
			return ILLEGAL_GROUP;
		}
		// Too many octets parsed
		if( j + jBeforeDoubleColon > 15 ) {
			return ILLEGAL_GROUP;
		}

		// Fill 16 - (j + jBeforeDoubleColon) zero octets
		for( int i = 0; i < 16 - ( j + jBeforeDoubleColon ); ++i ) {
			prefix[i + jBeforeDoubleColon] = 0;
		}

		// Copy last jBeforeDoubleColon octets
		for( int i = 0; i < j; ++i ) {
			prefix[i + ( 16 - j )] = prefixTail[i];
		}

		// The all-bits-set mask corresponds to group index 0
		return 0;
	}

	// Check whether the prefix is all-zero
	if( ( (uintptr_t)prefix % 8 ) == 0 ) {
		const uint64_t *qwordPrefix = (uint64_t *)prefix;
		if( !qwordPrefix[0] && !qwordPrefix[1] ) {
			return ILLEGAL_GROUP;
		}
	} else {
		int i = 0;
		for(; i < 16; ++i ) {
			if( prefix[i] ) {
				break;
			}
		}
		if( i == 16 ) {
			return ILLEGAL_GROUP;
		}
	}

	// All-bits-set groups are linked to bin #0
	// All-except-last-octet-set groups are linked to bin #1
	// ...
	// Single-octet-set group is linked to bin #14
	// No-octets-set group is illegal but should be consequently linked to bin #15
	assert( j > 0 );
	unsigned result = 16u - j;

	// Fill trailing prefix bytes
	for(; j < 16; ++j ) {
		prefix[j] = 0;
	}

	return result;
}

unsigned GIPFilter::ParseFilterV4MappedToV6( const char *filter, uint8_t *prefix, const char *terminators ) {
	for( int i = 0; i < 2; ++i ) {
		if( *filter++ != ':' ) {
			return ILLEGAL_GROUP;
		}
	}

	for( int i = 0; i < 4; ++i ) {
		if( *filter != 'f' && *filter != 'F' ) {
			return ILLEGAL_GROUP;
		}
		filter++;
	}

	if( *filter++ != ':' ) {
		return ILLEGAL_GROUP;
	}

	unsigned v4Group = ParseFilterV4( filter, prefix + 12, terminators );
	if( v4Group == ILLEGAL_GROUP ) {
		return v4Group;
	}

	// The value of 12 first bytes is predefined
	prefix[10] = prefix[11] = 0xFF;
	memset( prefix, 0, 10 );
	// Note: zero group corresponds to all-bits-defined mask
	return v4Group;
}

bool GIPFilter::StringToAddress( const char *s, netadr_t *addr ) {
	// Too bad we can't really use stuff from qcommon that is stateful and has extra dependencies
	// Utilize generic prefix/mask parsing facilities

	alignas( 8 )uint8_t prefix[16];

	const char *terminators = ":";
	if( *s == '[' ) {
		terminators = "]";
		s++;
	}

	netadrtype_t type;
	const unsigned parsedGroup = ParseFilter( s, prefix, &type, terminators );
	// All mask bits must be present
	if( parsedGroup != 0 ) {
		return false;
	}

	char *endptr = nullptr;
	if( type == NA_IP ) {
		const char *portSeparator = strrchr( s, ':' );
		// Always expect a port?
		if( !portSeparator ) {
			return false;
		}
		unsigned long parsedPort = strtoul( portSeparator + 1, &endptr, 10 );
		if( portSeparator + 1 == endptr || parsedPort >= ( 1 << 16 ) ) {
			return false;
		}
		addr->address.ipv4.port = (unsigned short)parsedPort;
		memcpy( addr->address.ipv4.ip, prefix, 4 );
	} else {
		const char *portSeparator = strrchr( s, ']' );
		if( !portSeparator ) {
			return false;
		}
		portSeparator++;
		if( *portSeparator != ':' ) {
			return false;
		}
		unsigned long parsedPort = strtoul( portSeparator + 1, &endptr, 10 );
		if( portSeparator + 1 == endptr || parsedPort >= ( 1 << 16 ) ) {
			return false;
		}
		addr->address.ipv6.port = (unsigned short)parsedPort;
		addr->address.ipv6.scope_id = 0;
		memcpy( addr->address.ipv6.ip, prefix, 16 );
	}

	addr->type = type;
	return true;
}

inline GIPFilter::Entry *GIPFilter::NextValidEntry( Entry *entry ) {
	if( !entry ) {
		return nullptr;
	}

	return FirstValidEntry( entry->next[LIST_LINKS] );
}

GIPFilter::Entry *GIPFilter::FirstValidEntry( Entry *entry ) {
	auto serverTime = game.serverTime;
	for( Entry *e = entry; e; e = e->next[LIST_LINKS] ) {
		if( e->timeout > serverTime ) {
			return e;
		}
	}

	return nullptr;
}

int GIPFilter::AddFilterFromString( const char *s, int64_t timeout ) {
	alignas( 8 ) uint8_t prefix[16];

	netadrtype_t addressType;
	const unsigned groupIndex = ParseFilter( s, prefix, &addressType );
	if( groupIndex == ILLEGAL_GROUP ) {
		G_Printf( "IP filter: illegal prefix %s\n", s );
		return -1;
	}

	if( addressType == NA_IP ) {
		return TryAddEntry<V4PrefixMatcher, V4IPMatcher, V4BinIndexComputer>( prefix, addressType, groupIndex, timeout );
	}
	return TryAddEntry<V6PrefixMatcher, V4IPMatcher, V6BinIndexComputer>( prefix, addressType, groupIndex, timeout );
}

template<typename PrefixMatcher, typename IPMatcher, typename IndexComputer>
int GIPFilter::TryAddEntry( const uint8_t *prefix, netadrtype_t addressType, unsigned groupIndex, int64_t timeout ) {
	const unsigned addressIndex = AddressIndexForType( addressType );

	GroupHeader *group = &groupsForAddress[addressIndex][groupIndex];

	if( entries ) {
		if( MatchIP<PrefixMatcher, IndexComputer>( prefix, group ) ) {
			G_Printf( "IP filter: The prefix is already present\n" );
			return -1;
		}
	}

	if( Entry *entry = AllocAndLinkEntry( addressIndex, groupIndex, IndexComputer()( prefix, group->mask ) ) ) {
		entry->type = addressType;
		entry->timeout = timeout;
		memcpy( entry->prefix, prefix, AddressLengthForIndex( addressIndex ) );
		// Remove all filters which prefixes are a subset of prefixes matched by new entry.
		// Use an IP matcher to match entry prefixes using the newly added entry prefix and mask.
		// It would be better to try entries removal before entry allocation attempt to free some,
		// but the capacity exhaustion is very unlikely and it requires rewriting a fair amount of matchers code.
		RemoveNarrowerFilters<IPMatcher>( entry );
		return +1;
	}

	G_Printf( "IP filter: The capacity has been exceeded\n" );
	return 0;
}

template<typename IPMatcher>
void GIPFilter::RemoveNarrowerFilters( const Entry *newlyAddedEntry ) {
	char buffer[MAX_STRING_CHARS];
	IPMatcher matcher;

	Entry *nextEntry = nullptr;
	for( Entry *entry = usedListHead; entry; entry = nextEntry ) {
		nextEntry = entry->next[LIST_LINKS];
		if( entry->type != newlyAddedEntry->type ) {
			continue;
		}
		if( entry == newlyAddedEntry ) {
			continue;
		}
		// Try matching an current entry prefix with the newly added entry prefix and mask
		if( !matcher( newlyAddedEntry, entry->prefix ) ) {
			continue;
		}

		if( EntryRef( entry ).PrintTo( buffer, sizeof( buffer ) ) ) {
			G_Printf( "Removing %s for being narrower than the added filter\n", buffer );
		}
		UnlinkAndFreeEntry( entry );
	}
}

int GIPFilter::RemoveFilterByString( const char *s ) {
	constexpr const char *format = "IP filter: the address %s is not found\n";
	if( !entries ) {
		G_Printf( format, s );
		return 0;
	}

	alignas( 8 ) uint8_t prefix[16];
	netadrtype_t type;

	const unsigned groupIndex = ParseFilter( s, prefix, &type );
	if( groupIndex == ILLEGAL_GROUP ) {
		G_Printf( "IP filter: illegal prefix %s\n", s );
		return -1;
	}

	const GroupHeader *activeHeadersList = groupListHeads[AddressIndexForType( type )];
	if( type == NA_IP ) {
		if( const Entry *entry = MatchIP<V4PrefixMatcher, V4BinIndexComputer>( prefix, activeHeadersList ) ) {
			UnlinkAndFreeEntry( const_cast<Entry *>( entry ) );
			return +1;
		}
	} else {
		if( const Entry *entry = MatchIP<V6PrefixMatcher, V6BinIndexComputer>( prefix, activeHeadersList ) ) {
			UnlinkAndFreeEntry( const_cast<Entry *>( entry ) );
			return +1;
		}
	}

	G_Printf( format, s );
	return -1;
}

/*
* SV_FilterPacket
*/
bool SV_FilterPacket( const char *from ) {
	if( !filterban->integer ) {
		return false;
	}

	return GIPFilter::Instance()->Match( from );
}

void GIPFilter::Init() {
	instance = new( G_Malloc( sizeof( GIPFilter ) ) )GIPFilter;
}

void GIPFilter::Shutdown() {
	instance->~GIPFilter();
	G_Free( instance );
	instance = nullptr;
}

bool GIPFilter::Match( const char *ip ) const {
	if( !entries ) {
		return false;
	}

	netadr_t address;
	if( !StringToAddress( ip, &address ) ) {
		return false;
	}

	const GroupHeader *activeHeadersList = groupListHeads[AddressIndexForType( address.type )];
	if( address.type == NA_IP ) {
		return MatchIP<V4IPMatcher, V4BinIndexComputer>( address.address.ipv4.ip, activeHeadersList ) != nullptr;
	}
	return MatchIP<V6IPMatcher, V6BinIndexComputer>( address.address.ipv6.ip, activeHeadersList ) != nullptr;
}

/*
* SV_ReadIPList
*/
void SV_InitIPList( void ) {
	GIPFilter::Init();

	trap_Cmd_ExecuteText( EXEC_APPEND, "exec listip.cfg silent\n" );
}

/*
* SV_WriteIPList
*/
static void SV_WriteIPList( void ) {
	int file;
	char name[MAX_QPATH];
	char string[MAX_STRING_CHARS];

	Q_strncpyz( name, "listip.cfg", sizeof( name ) );

	//G_Printf( "Writing %s.\n", name );

	if( trap_FS_FOpenFile( name, &file, FS_WRITE ) == -1 ) {
		G_Printf( "Couldn't open %s\n", name );
		return;
	}

	Q_snprintfz( string, sizeof( string ), "set filterban %d\r\n", filterban->integer );
	trap_FS_Write( string, strlen( string ), file );

	for( auto entry: *GIPFilter::Instance() ) {
		char entryBuffer[MAX_STRING_CHARS];
		if( entry.PrintTo( entryBuffer, sizeof( entryBuffer ) ) ) {
			int numChars = Q_snprintfz( string, sizeof( string ), "addip %s\r\n", entryBuffer );
			if( numChars > 0 ) {
				trap_FS_Write( string, (size_t)numChars, file );
			}
		}
	}

	trap_FS_FCloseFile( file );
}

void SV_ShutdownIPList( void ) {
	SV_WriteIPList();

	GIPFilter::Shutdown();
}

size_t GIPFilter::EntryRef::PrintTo( char *buffer, size_t bufferSize ) const {
	const uint8_t *const prefix = entry->prefix;
	const uint8_t *const mask = entry->group->mask;
	size_t result = 0;

	const char *formats[2];
	int numOctets;
	if( entry->type == NA_IP ) {
		formats[0] = formats[1] = "%u.";
		numOctets = 4;
	} else {
		formats[0] = "%02x";
		formats[1] = "%02x:";
		numOctets = 16;
	}

	int i = 0;
	for(; i < numOctets; ++i ) {
		if( !mask[i] ) {
			break;
		}
		int retval = snprintf( buffer + result, bufferSize, formats[i & 1], (unsigned)prefix[i] );
		if( retval < 0 ) {
			return 0;
		}
		result += retval;
		bufferSize -= retval;
	}

	if( numOctets == 4 ) {
		// Remove last dot always
		result--;
	} else {
		// Remove last colon except there is a single group printed
		// This is to make a distinction between V4 and V6 addresses
		// that is noticeable to ParseFilter(), so we can parse own-produced results
		if( i != 2 ) {
			// Check if there is actually a colon.
			// (Might be absent if the mask length is not a multiple of 16 bits, e.g. for V4-mapped-to-V6 addresses).
			if( buffer[result - 1] == ':' ) {
				result--;
			}
		}
	}

	if( entry->timeout != NO_TIMEOUT ) {
		float minutesLeft = ( entry->timeout - level.time ) / ( 60.0f * 1000.0f );
		int retval = snprintf( buffer + result, bufferSize, " %.2f", minutesLeft );
		if( retval < 0 ) {
			return 0;
		}
		result += retval;
	}

	buffer[result] = '\0';
	return result;
}

static void Cmd_PrintIPCmdResult( int result ) {
	if( result <= 0 ) {
		G_Printf("IP filter: %s\n", result < 0 ? "Illegal input" : "Internal fault" );
	}
}

/*
* Cmd_AddIP_f
*/
static void Cmd_AddIP_f( void ) {
	int argc = trap_Cmd_Argc();
	int64_t timeout = GIPFilter::NO_TIMEOUT;

	if( argc < 2 ) {
		G_Printf( "Usage: addip <ip-mask> [time-mins]\n" );
		return;
	}

	if( argc >= 3 ) {
		char *endptr;
		const char *argString = trap_Cmd_Argv( 2 );
		double parsedValue = strtod( argString, &endptr );
		if( *endptr ) {
			G_Printf( "Usage: addip <ip-mask> [time-mins]\n" );
			return;
		}
		timeout = game.serverTime + (int64_t)( parsedValue * 60 * 1000 );
	}

	Cmd_PrintIPCmdResult( GIPFilter::Instance()->AddFilterFromString( trap_Cmd_Argv( 1 ), timeout ) );
}

/*
* Cmd_RemoveIP_f
*/
static void Cmd_RemoveIP_f( void ) {
	if( trap_Cmd_Argc() < 2 ) {
		G_Printf( "Usage: removeip <ip-mask>\n" );
		return;
	}

	Cmd_PrintIPCmdResult( GIPFilter::Instance()->RemoveFilterByString( trap_Cmd_Argv( 1 ) ) );
}

/*
* Cmd_ListIP_f
*/
static void Cmd_ListIP_f( void ) {
	char string[MAX_STRING_CHARS];

	G_Printf( "Filter list:\n" );
	for( auto entry: *GIPFilter::Instance() ) {
		if( entry.PrintTo( string, sizeof( string ) ) ) {
			G_Printf( "%s\n", string );
		}
	}
}

/*
* Cmd_WriteIP_f
*/
static void Cmd_WriteIP_f( void ) {
	SV_WriteIPList();
}

#ifndef PUBLIC_BUILD
/*
* Cmd_Match_IP_f()
*/
static void Cmd_MatchIP_f( void ) {
	// We always expect a port in the command input because we want to simulate an actual address parsing
	constexpr const char *usage = "Usage: matchip <address>:<port>, an IPv6 address must be enclosed in brackets";
	if( trap_Cmd_Argc() < 2 ) {
		G_Printf( "%s\n", usage );
		return;
	}

	if( GIPFilter::Instance()->Match( trap_Cmd_Argv( 1 ) ) ) {
		G_Printf( "Match!\n" );
	} else {
		G_Printf( "No match or malformed input. %s\n", usage );
	}
}
#endif

/*
* Cmd_ListLocations_f
*/
static void Cmd_ListLocations_f( void ) {
	int i;

	for( i = 0; i < MAX_LOCATIONS; i++ ) {
		const char *cs = trap_GetConfigString( CS_LOCATIONS + i );
		if( !cs[0] ) {
			break;
		}
		G_Printf( "%2d %s\n", i, cs );
	}
}

/*
* G_AddCommands
*/
void G_AddServerCommands( void ) {
	if( dedicated->integer ) {
		trap_Cmd_AddCommand( "say", Cmd_ConsoleSay_f );
	}
	trap_Cmd_AddCommand( "kick", Cmd_ConsoleKick_f );

	// match controls
	trap_Cmd_AddCommand( "match", Cmd_Match_f );

	// banning
	trap_Cmd_AddCommand( "addip", Cmd_AddIP_f );
	trap_Cmd_AddCommand( "removeip", Cmd_RemoveIP_f );
	trap_Cmd_AddCommand( "listip", Cmd_ListIP_f );
	trap_Cmd_AddCommand( "writeip", Cmd_WriteIP_f );
#ifndef PUBLIC_BUILD
	trap_Cmd_AddCommand( "matchip", Cmd_MatchIP_f );
#endif

	trap_Cmd_AddCommand( "dumpASapi", G_asDumpAPI_f );

	trap_Cmd_AddCommand( "listratings", G_ListRatings_f );
	trap_Cmd_AddCommand( "listraces", G_ListRaces_f );

	trap_Cmd_AddCommand( "listlocations", Cmd_ListLocations_f );
}

/*
* G_RemoveCommands
*/
void G_RemoveCommands( void ) {
	if( dedicated->integer ) {
		trap_Cmd_RemoveCommand( "say" );
	}
	trap_Cmd_RemoveCommand( "kick" );

	// match controls
	trap_Cmd_RemoveCommand( "match" );

	// banning
	trap_Cmd_RemoveCommand( "addip" );
	trap_Cmd_RemoveCommand( "removeip" );
	trap_Cmd_RemoveCommand( "listip" );
	trap_Cmd_RemoveCommand( "writeip" );
#ifndef PUBLIC_BUILD
	trap_Cmd_RemoveCommand( "matchip" );
#endif

	trap_Cmd_RemoveCommand( "dumpASapi" );

	trap_Cmd_RemoveCommand( "listratings" );
	trap_Cmd_RemoveCommand( "listraces" );

	trap_Cmd_RemoveCommand( "listlocations" );
}
