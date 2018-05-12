#include "ui_precompiled.h"
#include "kernel/ui_common.h"
#include "kernel/ui_main.h"

namespace WSWUI {

typedef Rocket::Core::Element Element;
typedef Rocket::Core::Decorator Decorator;
typedef Rocket::Core::DecoratorDataHandle DecoratorDataHandle;
typedef Rocket::Core::DecoratorInstancer DecoratorInstancer;
typedef Rocket::Core::PropertyDictionary PropertyDictionary;
typedef Rocket::Core::Colourb Colourb;
typedef Rocket::Core::RenderInterface RenderInterface;
typedef Rocket::Core::TextureHandle TextureHandle;
typedef Rocket::Core::Vertex Vertex;
typedef Rocket::Core::Vector2f Vector2f;

class AntiAliasingBitmapCache {
	struct CacheEntry {
		// We might think about storing both sides data in a single texture
		// but we have to fight (sub)pixel offsets due to rounding in this case
		TextureHandle handles[2];
		int numRefs;
	};

	std::unordered_map<uint64_t, CacheEntry> entries;
	RenderInterface *renderInterface;

	static inline uint64_t MakeKey( const Vector2i &dimensions, float slantAngle );
	static void FillTextures( Colourb *data, const Vector2i &dimensions );
public:
	AntiAliasingBitmapCache() : renderInterface( nullptr ) {}

	~AntiAliasingBitmapCache();

	TextureHandle *GetTextures( const Vector2i &dimensions, float slantAngle, RenderInterface *renderInterface_ );
	void ReleaseTextures( const Vector2i &dimensions, float slantAngle );
};

static AntiAliasingBitmapCache *antiAliasingBitmapCache = nullptr;

/*
    Usage in CSS:

        slant-decorator: slanted;
        slant-color: #00FF00;
        slant-angle: 45;
*/
class SlantedDecorator final: public Decorator {
	Colourb color;
	float angle;
	float tangent;

	static const float DEFAULT_TANGENT;
public:
	static constexpr float DEFAULT_DEGREES = 20;

	explicit SlantedDecorator( const PropertyDictionary &properties ) {
		color = properties.GetProperty( "color" )->Get<Colourb>();
		angle = properties.GetProperty( "angle" )->Get<float>();
		if( angle == DEFAULT_DEGREES ) {
			tangent = DEFAULT_TANGENT;
			return;
		} else {
			// Values outside this range do not really make sense
			clamp( angle, 0.0f, 45.0f );
			tangent = ::tanf( DEG2RAD( angle ) );
		}
	}

	struct BitmapElementData {
		TextureHandle textureHandles[2];
		Vector2i bitmapDimensions;

		BitmapElementData( TextureHandle *textureHandles_, const Vector2i &bitmapDimensions_ )
			: bitmapDimensions( bitmapDimensions_ ) {
			Vector2Copy( textureHandles_, textureHandles );
		}
	};

	DecoratorDataHandle GenerateElementData( Element *element ) override {
		RenderInterface *renderInterface = element->GetRenderInterface();
		// Do not use anti-aliasing via bitmaps on hi-DPI screens
		if( renderInterface->GetPixelsPerInch() >= 200 ) {
			return (DecoratorDataHandle)0;
		}

		// Height is the same as the element height
		float height = element->GetClientHeight();
		// Width is affected by slant angle
		float width = fabsf( height * tangent );

		Vector2i dimensions( (int)( width + 0.5f ), (int)( height + 0.5f ) );
		// Skip the degenerate case
		if( !( dimensions.x * dimensions.y ) ) {
			return (DecoratorDataHandle)0;
		}

		TextureHandle *handles = WSWUI::antiAliasingBitmapCache->GetTextures( dimensions, angle, renderInterface );
		if( !handles ) {
			return (DecoratorDataHandle)0;
		}

		return (DecoratorDataHandle)( __new__( BitmapElementData )( handles, dimensions ) );
	}

	void ReleaseElementData( DecoratorDataHandle element_data ) override {
		if( element_data ) {
			__delete__( (BitmapElementData *)element_data );
		}
	}

	void RenderElement( Element *element, DecoratorDataHandle element_data ) override {
		if( auto *bitmapElementData = (BitmapElementData *)element_data ) {
			RenderUsingAABitmap( element, bitmapElementData );
		} else {
			RenderUsingRawGeometry( element );
		}
	}

	void RenderUsingRawGeometry( Element *element ) {
		const float elementWidth = element->GetClientWidth();
		const float elementHeight = element->GetClientHeight();

		Vector2f topLeft = Vector2f( element->GetAbsoluteLeft() + element->GetClientLeft(),
									 element->GetAbsoluteTop() + element->GetClientTop() );

		Vector2f bottomRight = Vector2f( topLeft.x + elementWidth, topLeft.y + elementHeight );

		Vertex vertices[4];

		const float shift = 0.5f * elementHeight * tangent;

		vertices[0].colour = this->color;
		vertices[0].tex_coord = Vector2f( 0.0f, 1.0f );
		vertices[0].position = topLeft;
		vertices[0].position.x += shift;

		vertices[1].position = Vector2f( bottomRight.x + shift, topLeft.y );
		vertices[1].tex_coord = Vector2f( 1.0f, 1.0f );
		vertices[1].colour = this->color;

		vertices[2].position = bottomRight;
		vertices[2].position.x -= shift;
		vertices[2].tex_coord = Vector2f( 1.0f, 0.0f );
		vertices[2].colour = this->color;

		vertices[3].position = Vector2f( topLeft.x - shift, bottomRight.y );
		vertices[3].tex_coord = Vector2f( 0.0f, 0.0f );
		vertices[3].colour = this->color;

		int indices[6] = { 0, 1, 2, 0, 2, 3 };
		Rocket::Core::RenderInterface *renderer = element->GetRenderInterface();

		renderer->RenderGeometry( vertices, 4, indices, 6, 0, Vector2f( 0.0, 0.0 ) );
	}

	void RenderUsingAABitmap( Element *element, BitmapElementData *elementData ) {
		const float elementWidth = element->GetClientWidth();
		const float elementHeight = element->GetClientHeight();

		Vector2f topLeft = Vector2f( element->GetAbsoluteLeft() + element->GetClientLeft(),
									 element->GetAbsoluteTop() + element->GetClientTop() );

		Vector2f bottomRight = Vector2f( topLeft.x + elementWidth, topLeft.y + elementHeight );

		const float shift = 0.5f * elementHeight * tangent;

		Vertex vertices[4];

		vertices[0].tex_coord = Vector2f( 0.0f, 1.0f );
		vertices[0].colour = this->color;

		vertices[1].tex_coord = Vector2f( 1.0f, 1.0f );
		vertices[1].colour = this->color;

		vertices[2].tex_coord = Vector2f( 1.0f, 0.0f );
		vertices[2].colour = this->color;

		vertices[3].tex_coord = Vector2f( 0.0f, 0.0f );
		vertices[3].colour = this->color;

		vertices[0].position = Vector2f( topLeft.x + shift, topLeft.y );
		vertices[1].position = Vector2f( bottomRight.x - shift, topLeft.y );
		vertices[2].position = Vector2f( bottomRight.x - shift, bottomRight.y );
		vertices[3].position = Vector2f( topLeft.x + shift, bottomRight.y );

		int indices[6] = { 0, 1, 2, 0, 2, 3 };
		Rocket::Core::RenderInterface *renderer = element->GetRenderInterface();
		renderer->RenderGeometry( vertices, 4, indices, 6, 0, Vector2f( 0.0, 0.0 ) );

		vertices[0].position = Vector2f( topLeft.x - shift, topLeft.y );
		vertices[1].position = Vector2f( topLeft.x + shift, topLeft.y );
		vertices[2].position = Vector2f( topLeft.x + shift, bottomRight.y );
		vertices[3].position = Vector2f( topLeft.x - shift, bottomRight.y );
		renderer->RenderGeometry( vertices, 4, indices, 6, elementData->textureHandles[0], Vector2f( 0.0f, 0.0f ) );
		renderer->RenderGeometry( vertices, 4, indices, 6, elementData->textureHandles[1], Vector2f( elementWidth, 0.0f ) );
	}
};

const float SlantedDecorator::DEFAULT_TANGENT = ::tanf( DEG2RAD( SlantedDecorator::DEFAULT_DEGREES ) );

class SlantedDecoratorInstancer: public DecoratorInstancer {
public:
	SlantedDecoratorInstancer() {
		RegisterProperty( "color", "#ffff" ).AddParser( "color" );
		RegisterProperty( "angle", va( "%f", SlantedDecorator::DEFAULT_DEGREES ) ).AddParser( "number" );
		// Tie the global cache lifetime to the instancer lifetime (which is itself implicitly global)
		assert( !WSWUI::antiAliasingBitmapCache );
		WSWUI::antiAliasingBitmapCache = __new__( AntiAliasingBitmapCache );
	}

	Decorator* InstanceDecorator( const String& name, const PropertyDictionary& _properties ) override {
		return __new__( SlantedDecorator )( _properties );
	}

	void ReleaseDecorator( Decorator* decorator ) override {
		__delete__( decorator );
	}

	void Release() override {
		__delete__( WSWUI::antiAliasingBitmapCache );
		WSWUI::antiAliasingBitmapCache = nullptr;
		__delete__( this );
	}
};

DecoratorInstancer *GetSlantedDecoratorInstancer() {
	return __new__( SlantedDecoratorInstancer );
}

inline uint64_t AntiAliasingBitmapCache::MakeKey( const Vector2i &dimensions, float slantAngle ) {
	assert( dimensions.x > 0 && dimensions.x < ( 1 << 16 ) );
	assert( dimensions.y > 0 && dimensions.y < ( 1 << 16 ) );
	uint64_t hiPart = (uint64_t)( ( dimensions.x << 16 ) | dimensions.y );
	uint64_t loPart = 0;
	assert( slantAngle >= 0 );
	*( (float *)&loPart ) = slantAngle;
	return ( hiPart << 32 ) | loPart;
}

static void ScanRightLine( Colourb *data, int rowShift, float x, int y ) {
	const int ix = (int)x;
	ptrdiff_t rowDataOffset = y * rowShift;

	for( int i = 0; i < ix + 1; ++i ) {
		data[i + rowDataOffset] = Colourb( 255, 255, 255, 255 );
	}

	data[ix + 1 + rowDataOffset] = Colourb( 255, 255, 255, (uint8_t)( 255 * ( x - ix ) ) );
}

static void ScanLeftLine( Colourb *data, int rowShift, float x, int y ) {
	const int ix = (int)x;
	ptrdiff_t rowDataOffset = y * rowShift;

	data[ix + rowDataOffset] = Colourb( 255, 255, 255, (uint8_t)( 255 - 255 * ( x - ix ) ) );
	for( int i = ix + 1; i < rowShift; ++i ) {
		data[i + rowDataOffset] = Colourb( 255, 255, 255, 255 );
	}
}

void AntiAliasingBitmapCache::FillTextures( Colourb *data, const Vector2i &dimensions ) {
	// Dimensions are supplied for a single side, both sides are combined in a single texture
	size_t numPixels = ( dimensions.x + 1 ) * ( dimensions.y + 1 ) * 2u;
	memset( data, 0, sizeof( Colourb ) * numPixels );
	Colourb *left = data;
	Colourb *right = data + numPixels / 2;

	// We do not support negative / > 45 degrees angles anymore
	assert( dimensions.x <= dimensions.y );

	// A derivation of Wu algorithm.
	// Not only plot two pixels with varied opacity,
	// but fill a scanned line segment and plot a single pixel then.

	int rowShift = dimensions.x;
	float delta = dimensions.x / (float)dimensions.y;
	float x = delta;
	ScanLeftLine( left, rowShift, x, 0 );
	ScanRightLine( right, rowShift, x, 0 );
	for( int y = 1; y < dimensions.y - 1; ++y ) {
		ScanLeftLine( left, rowShift, x, y );
		ScanRightLine( right, rowShift, x, y );
		x += delta;
	}
	ScanLeftLine( left, rowShift, x, dimensions.y - 1 );
	ScanRightLine( right, rowShift, x, dimensions.y - 1 );
}

AntiAliasingBitmapCache::~AntiAliasingBitmapCache() {
	// Accessing this->renderInterface in the loop is valid (if it is null there were no entries)
	for( auto &entry: entries ) {
		for( int i = 0; i < 2; ++i ) {
			renderInterface->ReleaseTexture( entry.second.handles[i] );
		}
	}
}

TextureHandle *AntiAliasingBitmapCache::GetTextures( const Vector2i &dimensions,
													 float slantAngle,
													 RenderInterface *renderInterface_ ) {
	// Save the render interface for further operations
	this->renderInterface = renderInterface_;

	// Check for the degenerate case
	assert( dimensions.x * dimensions.y != 0 );
	// Check sloppiness
	assert( abs( dimensions.x ) <= abs( dimensions.y ) );

	const uint64_t key = MakeKey( dimensions, slantAngle );
	auto iterator = entries.find( key );
	if( iterator != entries.end() ) {
		( *iterator ).second.numRefs++;
		return ( *iterator ).second.handles;
	}

	TextureHandle handles[2] = { 0, 0 };

	// Make sure we can legally access pixels that are +1 X/Y out of dimensions bounds
	auto *buffer = (Colourb *)malloc( 2 * sizeof( Colourb ) * ( dimensions.x + 1 ) * ( dimensions.y + 1 ) );
	// Data nullity depends of OS memory over-commit settings... we did our best
	if( buffer ) {
		FillTextures( buffer, dimensions );
		// Try generating texture
		for( int i = 0; i < 2; ++i ) {
			Colourb *textureData = buffer + i * ( dimensions.x + 1 ) * ( dimensions.y + 1 );
			if( !renderInterface_->GenerateTexture( handles[i], (Rocket::Core::byte *)textureData, dimensions, 4 ) ) {
				for( int j = 0; j < i; ++j ) {
					renderInterface_->ReleaseTexture( handles[j] );
				}
				// Ensure handles are zero in this case
				handles[0] = handles[1] = 0;
			}
		}
		free( buffer );
	}

	// Even if the texture creation has failed, add an entry, otherwise an attempt will be repeated every frame.
	// Add an entry with the initial ref count.
	entries.insert( { key, { { handles[0], handles[1] }, 1 } } );
	// Note: return an address of a persistent due to UI lifetime data
	return entries[key].handles;
}

void AntiAliasingBitmapCache::ReleaseTextures( const Vector2i &dimensions, float slantAngle ) {
	auto iterator = entries.find( MakeKey( dimensions, slantAngle ) );
	if( iterator != entries.end() ) {
		auto &entry = ( *iterator ).second;
		if( entry.numRefs > 1 ) {
			entry.numRefs--;
		} else {
			for( int i = 0; i < 2; ++i ) {
				if( entry.handles[i] ) {
					renderInterface->ReleaseTexture( entry.handles[i] );
				}
			}
			entries.erase( iterator );
		}
	}
}

}
