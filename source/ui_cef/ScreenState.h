#ifndef UI_CEF_SCREENSTATE_H
#define UI_CEF_SCREENSTATE_H

#include "Allocator.h"

#include <string>
#include <stdexcept>
#include <algorithm>
#include <stddef.h>

struct ConnectionState: public AllocatorChild {
	enum {
		SERVER_NAME_ATTACHMENT = 1,
		REJECT_MESSAGE_ATTACHMENT = 2,
		DOWNLOAD_FILENAME_ATTACHMENT = 4
	};

	void OnBeforeAllocatorFreeCall() override;

	std::string serverName;
	std::string rejectMessage;
	std::string downloadFileName;
	int downloadType;
	float downloadPercent;
	float downloadSpeed;
	int connectCount;

	explicit ConnectionState( RawAllocator *parent_ ): AllocatorChild( parent_ ) {}

	bool Equals( ConnectionState *that ) const;

	static ConnectionState *NewPooledObject();
};

struct DemoPlaybackState: public AllocatorChild {
	std::string demoName;
	unsigned time;
	bool paused;

	void OnBeforeAllocatorFreeCall() override;

	bool Equals( const DemoPlaybackState *that ) const;

	explicit DemoPlaybackState( RawAllocator *parent_ ): AllocatorChild( parent_ ) {}

	static DemoPlaybackState *NewPooledObject();
};

struct MainScreenState: public AllocatorChild {
	enum {
		CONNECTION_ATTACHMENT = 1,
		DEMO_PLAYBACK_ATTACHMENT = 2
	};

	void OnBeforeAllocatorFreeCall() override;

	explicit MainScreenState( RawAllocator *parent_ ): AllocatorChild( parent_ ) {}

	ConnectionState *connectionState;
	DemoPlaybackState *demoPlaybackState;
	int clientState;
	int serverState;
	bool showCursor;
	bool background;

	bool operator==( const MainScreenState &that ) const;

	static MainScreenState *NewPooledObject();
};

// Constructs items on demand in place of a raw bytes storage.
// An item constructor is assumed to accept this allocator as a single argument.
// Constructed items are kept during the entire lifecycle of this allocator object.
// Item cleanup is delegated to OnBeforeAllocatorFreeCall() item method.
template <typename T, size_t N>
class alignas( 8 )ReusableItemsAllocator: public RawAllocator {
	enum { ALIGNED_ITEM_SIZE = ( sizeof( T ) % 8 ? sizeof( T ) + 8 - sizeof( T ) % 8 : sizeof( T ) ) };
	alignas( 8 ) uint8_t buffer[N * ALIGNED_ITEM_SIZE];

	bool inUse[N];
	bool constructed[N];

	// The current implementation is a stub only to provide a minimal support
	// for allocators that are not very performance demanding.
	// Two linked lists, free item headers and constructed item headers should be maintained.
	// Templates for linking/unlinking items should be unified over the code base.
	static_assert( N < 8, "Rewrite to use an underlying freelist" );

	// There's no reliable other way to interrupt an execution regardless of process, even if we try avoiding exceptions
#ifndef _MSC_VER
	void FailWith( const char *message ) __attribute__( ( noreturn ) ) {
		throw std::logic_error( message );
	}
#else
	__declspec( noreturn ) void FailWith( const char *message ) {
		throw std::logic_error( message );
	}
#endif
public:
	ReusableItemsAllocator() {
		std::fill_n( inUse, N, false );
		std::fill_n( constructed, N, false );
	}

	~ReusableItemsAllocator() {
		for( size_t i = 0; i < N; ++i ) {
			if( constructed[i] ) {
				( (T *)( buffer + ALIGNED_ITEM_SIZE * i ) )->~T();
			}
		}
	}

	T *New() { return (T*)Alloc(); }

	void *Alloc() override {
		auto iter = std::find( inUse, inUse + N, false );
		if( iter == inUse + N ) {
			FailWith( "There's no free items left" );
		}
		auto index = iter - inUse;
		void *mem = buffer + ALIGNED_ITEM_SIZE * index;
		if( !constructed[index] ) {
			constructed[index] = true;
			new( mem )T( this );
		}
		inUse[index] = true;
		return (T *)mem;
	}

	void Free( void *p ) override {
		auto index = ( T *)p - ( T *)buffer;
#ifndef PUBLIC_BUILD
		if( index < 0 || index >= N ) {
			FailWith( "The pointer does not seem to belong to this allocator" );
		}
		if( ( (uint8_t *)p - buffer ) % ALIGNED_ITEM_SIZE ) {
			FailWith( "The pointer is not aligned on item boundaries" );
		}
		if( !constructed[index] ) {
			FailWith( "The item has not been constructed" );
		}
		if( !inUse[index] ) {
			FailWith( "The item is not in use or has been already freed" );
		}
#endif
		inUse[index] = false;
	}
};

#endif
