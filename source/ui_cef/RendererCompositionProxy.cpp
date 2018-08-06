#include "UiFacade.h"
#include "Api.h"
#include "CefClient.h"

void RendererCompositionProxy::StartShowingWorldModel( const char *name, bool blurred, bool looping,
													   const std::vector<CameraAnimFrame> &frames ) {
	if( looping ) {
		cameraAnimator.ResetWithLoop( frames.data(), frames.data() + frames.size() );
	} else {
		cameraAnimator.ResetWithSequence( frames.data(), frames.data() + frames.size() );
	}

	blurWorldModel = blurred;

	pendingWorldModel.assign( name );
	hasPendingWorldModel = true;
	hasStartedWorldModelLoading = false;
	hasSucceededWorldModelLoading = false;
}

RendererCompositionProxy::RendererCompositionProxy( UiFacade *parent_ ): parent( parent_ ) {
	chromiumBuffer = new uint8_t[parent->width * parent->height * 4];
}

inline void RendererCompositionProxy::RegisterChromiumBufferShader() {
	chromiumShader = api->R_RegisterRawPic( "chromiumBufferShader", parent->width, parent->height, chromiumBuffer, 4 );
}

inline void RendererCompositionProxy::ResetBackground() {
	pendingWorldModel.clear();
	hasPendingWorldModel = false;
	hasStartedWorldModelLoading = false;
	hasSucceededWorldModelLoading = false;
}

void RendererCompositionProxy::UpdateChromiumBuffer( const CefRenderHandler::RectList &dirtyRects,
													 const void *buffer, int w, int h ) {
	assert( this->parent->width == w );
	assert( this->parent->height == h );

	// Note: we currently HAVE to maintain a local copy of the buffer...
	// TODO: Avoid that and supply data directly to GPU if the shader is not invalidated

	const auto *in = (const uint8_t *)buffer;

	// Check whether we can copy the entire screen buffer or a continuous region of a screen buffer
	if( dirtyRects.size() == 1 ) {
		const auto &rect = dirtyRects[0];
		if( rect.width == w ) {
			assert( rect.x == 0 );
			const size_t screenRowSize = 4u * w;
			const ptrdiff_t startOffset = screenRowSize * rect.y;
			const size_t bytesToCopy = screenRowSize * rect.height;
			memcpy( this->chromiumBuffer + startOffset, in + startOffset, bytesToCopy );
			return;
		}
	}

	const size_t screenRowSize = 4u * w;
	for( const auto &rect: dirtyRects ) {
		ptrdiff_t rectRowOffset = screenRowSize * rect.y + 4 * rect.x;
		const size_t rowSize = 4u * rect.width;
		// There are rect.height rows in the copied rect
		for( int i = 0; i < rect.height; ++i ) {
			// TODO: Start prefetching next row?
			memcpy( this->chromiumBuffer + rectRowOffset, in + rectRowOffset, rowSize );
			rectRowOffset += screenRowSize;
		}
	}
}

void RendererCompositionProxy::Refresh( int64_t time, bool showCursor, bool background ) {
	const int width = parent->width;
	const int height = parent->height;

	// Ok it seems we have to touch these shaders every frame as they are invalidated on map loading
	whiteShader = api->R_RegisterPic( "$whiteimage" );
	cursorShader = api->R_RegisterPic( "gfx/ui/cursor.tga" );

	if( background ) {
		CheckAndDrawBackground( time, width, height, blurWorldModel );
		hadOwnBackground = true;
	} else if( hadOwnBackground ) {
		ResetBackground();
	}

	// TODO: Avoid this if the shader has not been invalidated and there were no updates!
	RegisterChromiumBufferShader();

	vec4_t color = { 1.0f, 1.0f, 1.0f, 1.0f };
	api->R_DrawStretchPic( 0, 0, width, height, 0.0f, 0.0f, 1.0f, 1.0f, color, chromiumShader );

	if( showCursor ) {
		int cursorX = parent->mouseXY[0], cursorY = parent->mouseXY[1];
		api->R_DrawStretchPic( cursorX, cursorY, 32, 32, 0.0f, 0.0f, 1.0f, 1.0f, color, cursorShader );
	}
}

void RendererCompositionProxy::CheckAndDrawBackground( int64_t time, int width, int height, bool blurred ) {
	if( hasPendingWorldModel ) {
		if( !hasStartedWorldModelLoading ) {
			api->R_RegisterWorldModel( pendingWorldModel.c_str() );
			hasStartedWorldModelLoading = true;
		} else {
			if( api->R_RegisterModel( pendingWorldModel.c_str() ) ) {
				hasSucceededWorldModelLoading = true;
			}
			hasPendingWorldModel = false;
		}
	}

	if( hasSucceededWorldModelLoading ) {
		cameraAnimator.Refresh( time );
		DrawWorldModel( time, width, height, blurWorldModel );
	} else {
		// Draw a fullscreen black quad... we are unsure if the renderer clears default framebuffer
		api->R_DrawStretchPic( 0, 0, width, height, 0.0f, 0.0f, 1.0f, 1.0f, colorBlack, whiteShader );
	}
}

void RendererCompositionProxy::DrawWorldModel( int64_t time, int width, int height, bool blurred ) {
	refdef_t rdf;
	memset( &rdf, 0, sizeof( rdf ) );
	rdf.areabits = nullptr;
	rdf.x = 0;
	rdf.y = 0;
	rdf.width = width;
	rdf.height = height;

	VectorCopy( cameraAnimator.Origin(), rdf.vieworg );
	Matrix3_Copy( cameraAnimator.Axis(), rdf.viewaxis );
	rdf.fov_x = cameraAnimator.Fov();
	rdf.fov_y = CalcFov( rdf.fov_x, width, height );
	AdjustFov( &rdf.fov_x, &rdf.fov_y, rdf.width, rdf.height, false );
	rdf.time = time;

	rdf.scissor_x = 0;
	rdf.scissor_y = 0;
	rdf.scissor_width = width;
	rdf.scissor_height = height;

	api->R_ClearScene();
	api->R_RenderScene( &rdf );
	if( blurred ) {
		api->R_BlurScreen();
	}
}