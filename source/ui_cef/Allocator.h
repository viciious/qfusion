#ifndef UI_CEF_ALLOCATOR_H
#define UI_CEF_ALLOCATOR_H

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

class RawAllocator;

class AllocatorChild {
	RawAllocator *const parent;
protected:
	// Sometimes we want to reuse items and calling a destructor leads to undesired destruction
	// of nested/inherited non-POD members we prefer to keep/just clear.
	// In this case this method should be overridden and member cleanup code should be put in it.
	virtual void OnBeforeAllocatorFreeCall() {
		// Make sure this method is not called for stack-allocated or default heap-allocated object
		assert( parent );
		this->~AllocatorChild();
	}
public:
	// Allows a construction of objects without parent
	// (If an object is allocated statically/on stack/using a default heap)
	AllocatorChild(): parent( nullptr ) {}

	explicit AllocatorChild( RawAllocator *parent_ ): parent( parent_ ) {}

	virtual ~AllocatorChild() = default;

	inline void DeleteSelf();

	// Very useful for catching bugs early by assertions
	inline bool ShouldDeleteSelf() const { return parent != nullptr; }

	// Useful for fluent-style checks
	template <typename T>
	static inline T *CheckShouldDelete( T *object ) {
		assert( object->ShouldDeleteSelf() );
		return object;
	}
};

class RawAllocator {
public:
	virtual ~RawAllocator() = default;
	// Useful for assertions in runtime-polymorphic calls. A zero size means the size is not predefined.
	virtual size_t AllocationSize() const { return 0; }
	// Allocates a storage, usually a placement new should be called next
	virtual void *Alloc() = 0;
	// Frees a storage, a destructor call should be preceding to this
	virtual void Free( void *p ) = 0;
};

inline void AllocatorChild::DeleteSelf() {
	// Make sure this method is not called for stack-allocated or default heap-allocated object
	assert( parent );
	OnBeforeAllocatorFreeCall();
	parent->Free( this );
}

#endif
