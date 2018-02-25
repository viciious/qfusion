#ifndef QFUSION_BUFFER_BUILDER_H
#define QFUSION_BUFFER_BUILDER_H

#include "ai_local.h"

// A container that avoids allocations of the entire data on capacity overflow.
template<typename T>
class BufferBuilder {

	struct alignas ( 8 )Chunk {
		Chunk *next;
		T *data;
		unsigned size;
		unsigned capacity;

		explicit Chunk( T *data_, unsigned capacity_ ) {
			data = data_;
			size = 0;
			capacity = capacity_;
			next = nullptr;
		}

		static Chunk *New( unsigned chunkSize ) {
			// Allocate the chunk along with its data in the single block of memory
			static_assert( sizeof( Chunk ) % 8 == 0, "" );
			static_assert( alignof( Chunk ) % 8 == 0, "" );
			auto *mem = (uint8_t *)malloc( sizeof( Chunk ) + sizeof( T ) * chunkSize );
			return new( mem )Chunk( (T *)( mem + sizeof( Chunk ) ), chunkSize );
		}

		static void Delete( Chunk *chunk ) {
			free( chunk );
		}

		bool IsFull() const {
			return size == capacity;
		}

		void Add( const T &elem ) {
			assert( !IsFull() );
			data[size++] = elem;
		}

		unsigned Add( const T *elems, unsigned numElems ) {
			unsigned elemsToCopy = numElems;
			if( numElems > capacity - size ) {
				elemsToCopy = capacity - size;
			}

			memcpy( data + size, elems, sizeof( T ) * elemsToCopy );
			size += elemsToCopy;
			return elemsToCopy;
		}
	};

	Chunk *first;
	Chunk *last;
	unsigned size;
	unsigned chunkSize;

public:
	explicit BufferBuilder( unsigned chunkSize_ ) : size( 0 ), chunkSize( chunkSize_ ) {
		first = last = Chunk::New( chunkSize_ );
	}

	~BufferBuilder() {
		Clear();
	}

	void Clear();

	unsigned Size() const { return size; }

	void Add( const T &elem );
	void Add( const T *elems, int numElems );

	// Constructs a single continuous array from chunks
	T *FlattenResult() const;
};

template <typename T>
void BufferBuilder<T>::Clear() {
	Chunk *chunk, *nextChunk;
	for( chunk = first; chunk; chunk = nextChunk ) {
		nextChunk = chunk->next;
		Chunk::Delete( chunk );
	}

	first = last = nullptr;
	size = 0;
}

template <typename T>
void BufferBuilder<T>::Add( const T &elem ) {
	if( last->IsFull() ) {
		Chunk *newChunk = Chunk::New( chunkSize );
		last->next = newChunk;
		last = newChunk;
	}

	last->Add( elem );
	size++;
}

template<typename T>
T *BufferBuilder<T>::FlattenResult() const {
	auto *result = (T *)G_LevelMalloc( sizeof( T ) * size );
	auto *resultPtr = result;

	for( const Chunk *chunk = first; chunk; chunk = chunk->next ) {
		memcpy( resultPtr, chunk->data, sizeof( T ) * chunk->size );
		resultPtr += chunk->size;
	}

	return result;
}

template <typename T>
void BufferBuilder<T>::Add( const T *elems, int numElems ) {
	auto remaining = (unsigned)numElems;
	for(;; ) {
		unsigned added = last->Add( elems, remaining );
		elems += added;
		size += added;
		if( added == remaining ) {
			return;
		}
		remaining -= added;
		Chunk *newChunk = Chunk::New( chunkSize );
		last->next = newChunk;
		last = newChunk;
	}
}

#endif
