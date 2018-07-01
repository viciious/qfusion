#ifndef QFUSION_CEFSTRINGBUILDER_H
#define QFUSION_CEFSTRINGBUILDER_H

#include "include/cef_base.h"

#include <iostream>
#include <sstream>

// A helper to avoid messing with std::string(stream) <-> CefString conversions
// that allows transparent optimization without touching call sites.
// Note: Instances of this class are used only in the separate UI process so far.
class CefStringBuilder {
	// The current implementation is a minimal one that satisfies the interface.
	// This should ideally use a linked list of cef_char_t[] chunks.
	std::stringstream underlying;
	// Currently just to ensure correctness of ReleaseOwnership()/Result() calls
	bool moved;
	// A hack to avoid always resetting/copying a buffer, see sarcastic comments in ChopLast()
	bool chopped;

	void CheckValid() {
		assert( !moved );
	}
public:
	CefStringBuilder()
		: moved( false ), chopped( false ) {
		underlying.imbue( std::locale::classic() );
	}

	explicit CefStringBuilder( size_t sizeHint )
		: moved( false ), chopped( false ) {
		underlying.imbue( std::locale::classic() );
	}

	CefStringBuilder &operator<<( const char *s ) {
		CheckValid();
		if( *s ) {
			chopped = false;
			underlying << s;
		}
		return *this;
	}

	CefStringBuilder &operator<<( const CefString &s ) {
		CheckValid();
		if( s.length() ) {
			chopped = false;
			underlying << s.ToString();
		}
		return *this;
	}

	CefStringBuilder &operator<<( const std::string &s ) {
		CheckValid();
		if( s.length() ) {
			chopped = false;
			underlying << s;
		}
		return *this;
	}

	CefStringBuilder &operator<<( bool value ) {
		CheckValid();
		chopped = false;
		underlying << ( value ? "true" : "false" );
		return *this;
	}

	CefStringBuilder &operator<<( char value ) {
		CheckValid();
		chopped = false;
		underlying << value;
		return *this;
	}

#define DEFINE_DEFAULT_STREAM_OPERATOR( Type )           \
	CefStringBuilder &operator<<( const Type &value ) {  \
		CheckValid();                                    \
		chopped = false;                                 \
		underlying << value;                             \
		return *this;                                    \
	}

	DEFINE_DEFAULT_STREAM_OPERATOR( int )
	DEFINE_DEFAULT_STREAM_OPERATOR( unsigned );
	DEFINE_DEFAULT_STREAM_OPERATOR( float );
	DEFINE_DEFAULT_STREAM_OPERATOR( double );
	DEFINE_DEFAULT_STREAM_OPERATOR( int64_t );
	DEFINE_DEFAULT_STREAM_OPERATOR( uint64_t );
#undef DEFINE_DEFAULT_STREAM_OPERATOR

	void ChopLast() {
		CheckValid();
		// This "standard library" is crooked but unfortunately we have to use it as it already widely used in CEF.
		// Even Java StringBuilder has far more convenient (and efficient) API.

		// If the stream is empty
		if( !underlying.tellp() ) {
			return;
		}

		// This call resets a write pointer without actual last characters removal
		underlying.seekp( -1, std::ios_base::end );
		// Set this flag, reset on every non-zero length write and check while getting a result
		chopped = true;
	}

	void Clear() {
		moved = false;
		chopped = false;
		underlying.str( std::string() );
	}

	// Should be called once and may use optimizations due to this fact
	CefString ReleaseOwnership() {
		moved = true;
		std::string result( underlying.str() );
		if( !result.empty() && chopped ) {
			result.pop_back();
		}
		return CefString( result );
	}

	// Can be called multiple times
	CefString Result() {
		std::string result( underlying.str() );
		if( !result.empty() && chopped ) {
			result.pop_back();
			chopped = false;
			underlying.str( result );
		}
		return CefString( result );
	}
};


// A helper for building arrays and objects string representation from containers
class AggregateBuildHelper {
protected:
	CefStringBuilder &underlying;
	const char openingChar;
	const char closingChar;

	virtual void PrintArgsAsBody( CefRefPtr<CefListValue> &args, size_t startArg, size_t numArgs ) = 0;
public:
	AggregateBuildHelper( CefStringBuilder &underlying_, char openingChar_, char closingChar_ )
		: underlying( underlying_ ), openingChar( openingChar_ ), closingChar( closingChar_ ) {}

	typedef std::function<void( CefStringBuilder &, CefRefPtr<CefListValue> &, size_t )> ArgPrinter;

	static inline ArgPrinter QuotedStringPrinter( char quoteChar = '"' ) {
		return [=]( CefStringBuilder &sb, CefRefPtr<CefListValue> &args, size_t argNum ) {
			sb << quoteChar << args->GetString( argNum ) << quoteChar;
		};
	}

	CefStringBuilder &PrintArgs( CefRefPtr<CefListValue> &args, size_t startArg, size_t numArgs ) {
		if( numArgs ) {
			underlying << openingChar << ' ';
			PrintArgsAsBody( args, startArg, numArgs );
			underlying << ' ' << closingChar;
		} else {
			underlying << openingChar << closingChar;
		}
		return underlying;
	}
};

class ArrayBuildHelper: public AggregateBuildHelper {
	const ArgPrinter &argPrinter;

	void PrintArgsAsBody( CefRefPtr<CefListValue> &args, size_t startArg, size_t numArgs ) override {
		for( size_t argNum = startArg; argNum < startArg + numArgs; ++argNum ) {
			argPrinter( underlying, args, argNum );
			underlying << ',';
		}
		underlying.ChopLast();
	}
public:
	explicit ArrayBuildHelper( CefStringBuilder &underlying_, const ArgPrinter &argPrinter_ = QuotedStringPrinter( '"' ) )
		: AggregateBuildHelper( underlying_, '[', ']' ), argPrinter( argPrinter_ ) {}
};

class ObjectBuildHelper: public AggregateBuildHelper {
	const ArgPrinter &keyPrinter;
	const ArgPrinter &valuePrinter;

	void PrintArgsAsBody( CefRefPtr<CefListValue> &args, size_t startArg, size_t numArgs ) override {
		assert( !( numArgs % 2 ) );
		for( size_t argNum = startArg; argNum < startArg + numArgs; argNum += 2 ) {
			keyPrinter( underlying, args, argNum + 0 );
			underlying << ':';
			valuePrinter( underlying, args, argNum + 1 );
			underlying << ',';
		}
		underlying.ChopLast();
	}
public:
	explicit ObjectBuildHelper( CefStringBuilder &underlying_,
								const ArgPrinter &keyPrinter_ = QuotedStringPrinter(),
								const ArgPrinter &valuePrinter_ = QuotedStringPrinter() )
		: AggregateBuildHelper( underlying_, '{', '}' )
		, keyPrinter( keyPrinter_ )
		, valuePrinter( valuePrinter_ ) {}

};

#endif
