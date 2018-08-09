#include "UiFacade.h"
#include "Api.h"
#include "CefClient.h"

#include <memory>

void RendererCompositionProxy::StartShowingWorldModel( const char *name, bool blurred, bool looping,
													   const std::vector<CameraAnimFrame> &frames ) {
	if( looping ) {
		worldCameraAnimator.ResetWithLoop( frames.data(), frames.data() + frames.size() );
	} else {
		worldCameraAnimator.ResetWithSequence( frames.data(), frames.data() + frames.size() );
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

int RendererCompositionProxy::StartDrawingModel( const ModelDrawParams &params ) {
	const auto zIndex = params.ZIndex();

	std::string modelName( params.Model() );
	auto *model = api->R_RegisterModel( modelName.c_str() );
	if( !model ) {
		parent->Logger()->Error( "No such model %s", modelName.c_str() );
		return 0;
	}

	std::string skinName( params.Skin() );
	auto *skin = api->R_RegisterSkinFile( skinName.c_str() );
	if( !skin ) {
		// Just print a warning... a default skin will be used (???)
		parent->Logger()->Warning( "No such skin %s", skinName.c_str() );
	}

	if( !zIndicesSet.TrySet( zIndex ) ) {
		parent->Logger()->Error( "Can't reserve a z-index for a model: z-index %d is already in use", (int)zIndex );
		return 0;
	}


	DrawnAliasModel *newDrawnModel;
	try {
		newDrawnModel = new DrawnAliasModel( this, params, model, skin );
	} catch( ... ) {
		zIndicesSet.Unset( zIndex );
		throw;
	}

	return drawnItemsRegistry.LinkNewItem( newDrawnModel );
}

int RendererCompositionProxy::StartDrawingImage( const ImageDrawParams &params ) {
	const auto zIndex = params.ZIndex();

	std::string shaderName( params.Shader() );
	auto *shader = api->R_RegisterPic( shaderName.c_str() );
	if( !shader ) {
		parent->Logger()->Error( "No such shader %s", shaderName.c_str() );
		return 0;
	}

	if( !zIndicesSet.TrySet( params.ZIndex() ) ) {
		parent->Logger()->Error( "Can't reserve a zIndex for an image: z-index %d is already in use", (int)zIndex );
		return 0;
	}

	Drawn2DImage *newDrawnImage;
	try {
		newDrawnImage = new Drawn2DImage( this, params, shader );
	} catch( ... ) {
		zIndicesSet.Unset( zIndex );
		throw;
	}

	return drawnItemsRegistry.LinkNewItem( newDrawnImage );
}

bool RendererCompositionProxy::StopDrawingItem( int drawnItemHandle, const std::type_info &itemTypeInfo ) {
	if( NativelyDrawnItem *item = drawnItemsRegistry.TryUnlinkItem( drawnItemHandle ) ) {
		// Check whether it really was an item of the needed type.
		// Handles are assumed to be unqiue for all drawn items (models or images).
		// We're trying to catch this contract violation.
		assert( typeid( item ) == itemTypeInfo );
		delete item;
		return true;
	}

	return false;
}

RendererCompositionProxy::DrawnItemsRegistry::~DrawnItemsRegistry() {
	NativelyDrawnItem *nextItem;
	for( auto *item = globalItemsListHead; item; item = nextItem ) {
		nextItem = item->NextInGlobalList();
		delete item;
	}
}

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// TODO... This is from game/g_svcmds.cpp, lift it to project top-level

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

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// TODO...

int RendererCompositionProxy::DrawnItemsRegistry::LinkNewItem( RendererCompositionProxy::NativelyDrawnItem *item ) {
	// The z-index must be already set
	assert( item->zIndex );
	const int handle = NextHandle();
	item->handle = handle;

	const unsigned hashBinIndex = HashBinIndex( handle );

	// Link the item to the global list (so it could be deallocated in destructor)
	Link( item, &globalItemsListHead, NativelyDrawnItem::GLOBAL_LIST );
	// Link the item to its hash bin
	Link( item, &hashBins[hashBinIndex], NativelyDrawnItem::BIN_LIST );

	if( item->zIndex > 0 ) {
		positiveZItemsList.AddItem( item );
	} else {
		negativeZItemsList.AddItem( item );
	}

	return handle;
}

RendererCompositionProxy::NativelyDrawnItem *RendererCompositionProxy::DrawnItemsRegistry::TryUnlinkItem( int itemHandle ) {
	if( !itemHandle ) {
		return nullptr;
	}

	const unsigned hashBinIndex = HashBinIndex( itemHandle );
	for( NativelyDrawnItem *item = hashBins[hashBinIndex]; item; item = item->NextInBinList() ) {
		if( item->handle == itemHandle ) {
			// Unlink from hash bin
			Unlink( item, &hashBins[hashBinIndex], NativelyDrawnItem::BIN_LIST );
			// Unlink from allocated list
			Unlink( item, &globalItemsListHead, NativelyDrawnItem::GLOBAL_LIST );
			// Remove from the corresponding half-space sorted list
			if( item->zIndex > 0 ) {
				positiveZItemsList.RemoveItem( item );
			} else {
				negativeZItemsList.RemoveItem( item );
			}
		}
	}

	return nullptr;
}

void RendererCompositionProxy::DrawnItemsRegistry::HalfSpaceDrawList::DrawItems( int64_t time ) {
	if( invalidated ) {
		BuildSortedList();
		invalidated = false;
	}

	// Draw items sorted by z-index
	for( SortedDrawnItemRef itemRef: sortedItems ) {
		itemRef.item->DrawSelf( time );
	}
}

// TODO: See remarks in the header
void RendererCompositionProxy::DrawnItemsRegistry::HalfSpaceDrawList::AddItem( NativelyDrawnItem *item ) {
	Link( item, &drawnItemsSetHead, DRAWN_LIST );
	invalidated = true;
}

// TODO: See remarks in the header
void RendererCompositionProxy::DrawnItemsRegistry::HalfSpaceDrawList::RemoveItem( NativelyDrawnItem *item ) {
	Unlink( item, &drawnItemsSetHead, DRAWN_LIST );
	invalidated = true;
}

void RendererCompositionProxy::DrawnItemsRegistry::HalfSpaceDrawList::BuildSortedList() {
	sortedItems.clear();
	sortedItems.reserve( (unsigned)numItems );

	// Heap sort feels more natural in this case but is really less efficient
	for( auto item = drawnItemsSetHead; item; item = item->next[DRAWN_LIST] ) {
		sortedItems.emplace_back( SortedDrawnItemRef( item ) );
	}

	std::sort( sortedItems.begin(), sortedItems.end() );
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

	// Draw items that are intended to be put behind the Chromium buffer
	drawnItemsRegistry.DrawNegativeZItems( time );

	// TODO: Avoid this if the shader has not been invalidated and there were no updates!
	RegisterChromiumBufferShader();

	// TODO: Avoid drawning of UI overlay at all if there is nothing to show!
	// The only reliable way of doing that is adding syscalls...
	vec4_t color = { 1.0f, 1.0f, 1.0f, 1.0f };
	api->R_DrawStretchPic( 0, 0, width, height, 0.0f, 0.0f, 1.0f, 1.0f, color, chromiumShader );

	// Draw items that are intended to be put in front of the Chromium buffer
	drawnItemsRegistry.DrawPositiveZItems( time );

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
		worldCameraAnimator.Refresh( time );
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

	VectorCopy( worldCameraAnimator.Origin(), rdf.vieworg );
	Matrix3_Copy( worldCameraAnimator.Axis(), rdf.viewaxis );
	rdf.fov_x = worldCameraAnimator.Fov();
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

RendererCompositionProxy::NativelyDrawnItem::NativelyDrawnItem( RendererCompositionProxy *parent_,
																const ItemDrawParams &drawParams )
	: parent( parent_ ), zIndex( drawParams.ZIndex() ) {
	const float *topLeft = drawParams.TopLeft();
	Vector2Copy( topLeft, this->viewportTopLeft );
	const float *dimensions = drawParams.Dimensions();
	Vector2Copy( dimensions, this->viewportDimensions );
}

RendererCompositionProxy::DrawnAliasModel::DrawnAliasModel( RendererCompositionProxy *parent_,
															const ModelDrawParams &drawParams,
															model_s *validatedModel,
															skinfile_s *validatedSkin )
	: NativelyDrawnItem( parent_, drawParams ) {
	memset( &entity, 0, sizeof( entity ) );
	const int color = drawParams.ColorRgba();
	Vector4Set( entity.color, COLOR_R( color ), COLOR_G( color ), COLOR_B( color ), COLOR_A( color ) );
	entity.model = validatedModel;
	entity.customSkin = validatedSkin;
	// TODO... check whether it's a skeletal model
}

void RendererCompositionProxy::DrawnAliasModel::DrawSelf( int64_t time ) {
	// TODO: This is a placeholder drawn!!!
	int x = viewportTopLeft[0];
	int y = viewportTopLeft[1];
	int w = viewportDimensions[0];
	int h = viewportDimensions[1];
	vec4_t color = { entity.color[0] / 255.0f, entity.color[1] / 255.0f, entity.color[2] / 255.0f, entity.color[3] / 255.0f };
	api->R_DrawStretchPic( x, y, w, h, 0.0, 0.0f, 1.0f, 1.0f, color, api->R_RegisterPic( "$whiteimage") );
}

RendererCompositionProxy::Drawn2DImage::Drawn2DImage( RendererCompositionProxy *parent_,
													  const ImageDrawParams &drawParams,
													  struct shader_s *validatedShader )
	: NativelyDrawnItem( parent_, drawParams ), shader( validatedShader ) {}

void RendererCompositionProxy::Drawn2DImage::DrawSelf( int64_t time ) {
	int x = viewportTopLeft[0];
	int y = viewportTopLeft[1];
	int w = viewportDimensions[0];
	int h = viewportDimensions[1];
	api->R_DrawStretchPic( x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, colorWhite, shader );
}