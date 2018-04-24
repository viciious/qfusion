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

class AntiAliasingBitmapCache {
	struct CacheEntry {
		TextureHandle handle;
		int numRefs;
	};

	std::unordered_map<uint64_t, CacheEntry> entries;
	RenderInterface *renderInterface;

	static inline uint64_t MakeKey( const Vector2i &dimensions, float slantAngle );
	static void FillTexture( Colourb *data, const Vector2i &dimensions );
public:
	AntiAliasingBitmapCache() : renderInterface( nullptr ) {}

	~AntiAliasingBitmapCache();

	TextureHandle GetTexture( const Vector2i &dimensions, float slantAngle, RenderInterface *renderInterface_ );
	void ReleaseTexture( const Vector2i &dimensions, float slantAngle );
};

static AntiAliasingBitmapCache *antiAliasingBitmapCache = nullptr;

/*
    Usage in CSS:

        slant-decorator: slanted;
        slant-color: #00FF00;
        slant-angle: +45;
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
			clamp( angle, -60.0f, 60.0f );
			tangent = ::tanf( DEG2RAD( angle ) );
		}
	}

	struct BitmapElementData {
		TextureHandle textureHandle;
		Vector2i bitmapDimensions;
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

		Vector2i dimensions( (int)width, (int)height );
		// Skip the degenerate case
		if( !( dimensions.x * dimensions.y ) ) {
			return (DecoratorDataHandle)0;
		}

		TextureHandle handle = WSWUI::antiAliasingBitmapCache->GetTexture( dimensions, angle, renderInterface );
		if( !handle ) {
			return (DecoratorDataHandle)0;
		}

		auto *data = __new__( BitmapElementData );
		data->bitmapDimensions = dimensions;
		data->textureHandle = handle;
		return (DecoratorDataHandle)data;
	}

	void ReleaseElementData( DecoratorDataHandle element_data ) override {
		if( element_data ) {
			__delete__( (BitmapElementData *)element_data );
		}
	}

	void RenderElement( Element *element, DecoratorDataHandle element_data ) override {
		typedef Rocket::Core::Vertex Vertex;
		typedef Rocket::Core::Vector2f Vector2f;

		const float elementWidth = element->GetClientWidth();
		const float elementHeight = element->GetClientHeight();

		Vector2f topLeft = Vector2f( element->GetAbsoluteLeft() + element->GetClientLeft(),
									 element->GetAbsoluteTop() + element->GetClientTop() );

		Vector2f bottomRight = Vector2f( topLeft.x + elementWidth, topLeft.y + elementHeight );

		// create the renderable vertexes
		Vertex vertices[4];

		vertices[0].colour = this->color;
		vertices[0].tex_coord = Vector2f( 0.0f, 1.0f );

		vertices[1].tex_coord = Vector2f( 1.0f, 1.0f );
		vertices[1].colour = this->color;

		vertices[2].tex_coord = Vector2f( 1.0f, 0.0f );
		vertices[2].colour = this->color;

		vertices[3].tex_coord = Vector2f( 0.0f, 0.0f );
		vertices[3].colour = this->color;

		const float shift = 0.5f * elementHeight * tangent;

		int indices[6] = { 0, 1, 2, 0, 2, 3 };

		Rocket::Core::RenderInterface *renderer = element->GetRenderInterface();

		auto *bitmapElementData = (BitmapElementData *)element_data;
		// Draw anti-aliasing bitmaps first (if they're present).
		if( bitmapElementData ) {
			// Setup vertices for the bitmap rectangle shifted to the left element side.
			// Handle slant shift via renderer translations.

			float side = +1.0f;
			if( tangent < 0 ) {
				// Flip X tex coords for negative degrees.
				// Tex coords do not matter for the main geometry drawing anyway.
				// This is not very nice but nobody is going to use negative angles.
				// Note: The tex coords are also always flipped in Y axis
				// to simplify implementation of Wu algorithm.
				for( Vertex &v: vertices ) {
					v.tex_coord.x = 1.0f - v.tex_coord.x;
				}
				side = -1.0f;
			}

			// Top left
			vertices[0].position = topLeft;

			// Top right
			vertices[1].position = topLeft;
			vertices[1].position.x += bitmapElementData->bitmapDimensions.x;

			// Bottom right
			vertices[2].position = topLeft;
			vertices[2].position.x += bitmapElementData->bitmapDimensions.x;
			vertices[2].position.y += bitmapElementData->bitmapDimensions.y;

			// Bottom left
			vertices[3].position = topLeft;
			vertices[3].position.y += bitmapElementData->bitmapDimensions.y;

			Vector2f leftTranslation( side * ( -shift - side * 0.25f ), 0.0f );
			renderer->RenderGeometry( vertices, 4, indices, 6, bitmapElementData->textureHandle, leftTranslation );
			Vector2f rightTranslation( elementWidth - side * ( shift + side * 0.25f ), 0.0f );
			renderer->RenderGeometry( vertices, 4, indices, 6, bitmapElementData->textureHandle, rightTranslation );
		}

		vertices[0].position = topLeft;
		vertices[0].position.x += shift;

		vertices[1].position = Vector2f( bottomRight.x + shift, topLeft.y );

		vertices[2].position = bottomRight;
		vertices[2].position.x -= shift;

		vertices[3].position = Vector2f( topLeft.x - shift, bottomRight.y );
		renderer->RenderGeometry( vertices, 4, indices, 6, 0, Vector2f( 0.0, 0.0 ) );
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
	*( (float *)&loPart ) = fabsf( slantAngle );
	return ( hiPart << 32 ) | loPart;
}

void AntiAliasingBitmapCache::FillTexture( Colourb *data, const Vector2i &dimensions ) {
	// We are sure the allocated memory chunk size permits this
	memset( data, 0, 2 * sizeof( Colourb ) * dimensions.x * dimensions.y );

	// First draw a line using Wu algorithm, then apply an additional convolution filter
	Colourb *const lineBuffer = data + dimensions.x * dimensions.y;

	float delta;
	int rowShift = dimensions.x;
	if( abs( dimensions.y ) > abs( dimensions.x ) ) {
		delta = dimensions.x / (float)dimensions.y;
		float x = delta;
		for( int y = 1; y < dimensions.y - 1; ++y ) {
			int ix = (int)x;
			uint8_t alpha = (uint8_t)( 255 * ( x - ix ) );
			lineBuffer[ix + y * rowShift] = Colourb( 255, 255, 255, 255 - alpha );
			lineBuffer[ix + 1 + y * rowShift] = Colourb( 255, 255, 255, alpha );
			x += delta;
		}
	} else {
		delta = dimensions.y / (float)dimensions.x;
		float y = 1 * delta;
		for( int x = 1; x < dimensions.x - 1; ++x ) {
			int iy = (int)y;
			uint8_t alpha = (uint8_t)( 255 * ( y - iy ) );
			lineBuffer[x + iy * rowShift] = Colourb( 255, 255, 255 - alpha );
			lineBuffer[x + (iy + 1) * rowShift] = Colourb( 255, 255, 255, alpha );
			y += delta;
		}
	}

	const float kernel[3][3] = {
		{ 1/32.f, 1/8.f, 1/32.f },
		{ 1/8.f, 1/4.f, 1/8.f },
		{ 1/32.f, 1/8.f, 1/32.f }
	};

	// TODO: Do not apply convolution to every inner pixel

	for( int x = 1; x < dimensions.x - 1; ++x ) {
		for( int y = 1; y < dimensions.y - 1; ++y ) {
			float alpha = 0.0f;
			for( int i = -1; i <= 1; ++i ) {
				for( int j = -1; j <= 1; ++j ) {
					alpha += kernel[1 + i][1 + j] * lineBuffer[x + i + ( y + j ) * rowShift].alpha;
				}
			}

			// Deliberately lower AA bitmap alpha value
			alpha *= 0.8f;

			data[x + y * rowShift] = lineBuffer[x + y * rowShift];
			data[x + y * rowShift].alpha = (Rocket::Core::byte)alpha;
		}
	}
}

AntiAliasingBitmapCache::~AntiAliasingBitmapCache() {
	// Accessing this->renderInterface in the loop is valid (if it is null there were no entries)
	for( auto &entry: entries ) {
		renderInterface->ReleaseTexture( entry.second.handle );
	}
}

TextureHandle AntiAliasingBitmapCache::GetTexture( const Vector2i &dimensions,
												   float slantAngle,
												   RenderInterface *renderInterface_ ) {
	// Save the render interface for further operations
	this->renderInterface = renderInterface_;

	// Check for the degenerate case
	assert( dimensions.x * dimensions.y != 0 );

	const uint64_t key = MakeKey( dimensions, slantAngle );
	auto iterator = entries.find( key );
	if( iterator != entries.end() ) {
		( *iterator ).second.numRefs++;
		return ( *iterator ).second.handle;
	}

	auto handle = (TextureHandle)0;

	// Use malloc() and not new[] for more convenient out-of-memory checks.
	// Note: We need 2x amount of memory for convolution buffer
	auto *data = (Colourb *)malloc( 2 * sizeof( Colourb ) * dimensions.x * dimensions.y );
	// Data nullity depends of OS memory over-commit settings... we did our best
	if( data ) {
		FillTexture( data, dimensions );
		// Try generating texture
		if( !renderInterface_->GenerateTexture( handle, (Rocket::Core::byte *)data, dimensions, 4 ) ) {
			// Ensure the handle is zero in case of failure
			handle = (TextureHandle)0;
		}
		free( data );
	}

	// Even if the texture creation has failed, add an entry, otherwise an attempt will be repeated every frame.
	// Add an entry with the initial ref count.
	entries.insert( { key, { handle, 1 } } );

	return handle;
}

void AntiAliasingBitmapCache::ReleaseTexture( const Vector2i &dimensions, float slantAngle ) {
	auto iterator = entries.find( MakeKey( dimensions, slantAngle ) );
	if( iterator != entries.end() ) {
		auto &entry = ( *iterator ).second;
		if( entry.numRefs > 1 ) {
			entry.numRefs--;
		} else {
			if( entry.handle ) {
				renderInterface->ReleaseTexture( entry.handle );
			}
			entries.erase( iterator );
		}
	}
}

}
