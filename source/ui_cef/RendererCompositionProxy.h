#ifndef UI_CEF_COMPOSITOR_H
#define UI_CEF_COMPOSITOR_H

#include "CameraAnimator.h"
#include <string>
#include <include/cef_render_handler.h>

#include "../cgame/ref.h"

class UiFacade;

class ItemDrawParams;
class ModelDrawParams;
class ImageDrawParams;

struct model_s;
struct skinfile_s;

class RendererCompositionProxy {
	UiFacade *const parent;

	uint8_t *chromiumBuffer;

	struct shader_s *whiteShader { nullptr };
	struct shader_s *cursorShader { nullptr };
	struct shader_s *chromiumShader { nullptr };

	std::string pendingWorldModel;
	CameraAnimator worldCameraAnimator;

	bool hasPendingWorldModel { false };
	bool hasStartedWorldModelLoading { false };
	bool hasSucceededWorldModelLoading { false };

	bool hadOwnBackground { false };

	bool blurWorldModel { false };

	void DrawWorldModel( int64_t time, int width, int height, bool blurred = false );

	void CheckAndDrawBackground( int64_t time, int width, int height, bool blurred = false );

	inline void ResetBackground();

	inline void RegisterChromiumBufferShader();

	struct NativelyDrawnItem {
		RendererCompositionProxy *const parent;
		const int zIndex;

		NativelyDrawnItem( RendererCompositionProxy *parent_, const ItemDrawParams &drawParams );

		enum { GLOBAL_LIST, BIN_LIST, DRAWN_LIST };
		NativelyDrawnItem *prev[3] = { nullptr, nullptr, nullptr };
		NativelyDrawnItem *next[3] = { nullptr, nullptr, nullptr };

		int handle { 0 };                // illegal value by default

		int viewportTopLeft[2];
		int viewportDimensions[2];

		NativelyDrawnItem *NextInBinList() { return next[BIN_LIST]; }
		NativelyDrawnItem *NextInGlobalList() { return next[GLOBAL_LIST]; }

		virtual ~NativelyDrawnItem() = default;
		virtual void DrawSelf( int64_t time ) = 0;
	};

	class DrawnAliasModel: public NativelyDrawnItem {
		entity_t entity;
		CameraAnimator animator;
	public:
		DrawnAliasModel( RendererCompositionProxy *parent_,
						 const ModelDrawParams &drawParams,
						 model_s *validatedModel,
						 skinfile_s *validatedSkin );

		void DrawSelf( int64_t time ) override;
	};

	class Drawn2DImage: public NativelyDrawnItem {
		shader_s *const shader;
	public:
		Drawn2DImage( RendererCompositionProxy *parent_,
					  const ImageDrawParams &drawParams,
					  struct shader_s *validatedShader );

		void DrawSelf( int64_t time ) override;
	};

	struct ZIndicesSet {
		uint32_t words[std::numeric_limits<uint16_t>::max() / 32];

		ZIndicesSet() {
			memset( words, 0, sizeof( words ) );
		}

		/**
		 * This call has "compare and swap"-like semantics
		 */
		inline bool TrySet( int16_t index ) {
			uint16_t u = *(uint16_t *)&index;
			int wordIndex = u / 32;
			uint32_t bitMask = 1u << ( u % 32u );
			bool result = !( words[wordIndex] & bitMask );
			words[wordIndex] |= bitMask;
			return result;
		}

		void Unset( int16_t index ) {
			uint16_t u = *(uint16_t *)&index;
			words[u / 32] &= ~( 1u << ( u % 32u ) );
		}
	} zIndicesSet;

	struct SortedDrawnItemRef {
		NativelyDrawnItem *item;
		explicit SortedDrawnItemRef( NativelyDrawnItem *item_ ): item( item_ ) {}
		bool operator<( const SortedDrawnItemRef &that ) const { return item->zIndex < that.item->zIndex; }
	};

	struct DrawnItemsRegistry {
		enum { NUM_HASH_BINS = 107 };
		/**
		 * We maintain a hash table for efficient lookups of items by a drawn item handle
		 */
		NativelyDrawnItem *hashBins[NUM_HASH_BINS];

		struct HalfSpaceDrawList {
			/**
			 * A sorted list of refs to items that will be drawn in this order
			 */
			std::vector<SortedDrawnItemRef> sortedItems;
			/**
			 * All drawn items corresponding to sortedItems array are kept here
			 * to preserve items if the draw list has been invalidated
			 */
			NativelyDrawnItem *drawnItemsSetHead { nullptr };
			/**
			 * A counter of items, useful for reserving memory
			 */
			int numItems { 0 };
			/**
			 * Setting this flag leads to sortedItems array invalidation and rebuilding
			 * @note: we can optimize single/small additions/removals given the sortedItems
			 * ordering property, but this does not justify the required code complexity
			 * (we would to have maintain a valid index in sortedItems list for every item)
			 * and invalidating the entire sortedItems list is more efficient for bulky additions/removals.
			 */
			bool invalidated { false };

			static constexpr auto DRAWN_LIST = NativelyDrawnItem::DRAWN_LIST;

			// TODO: Move back from the source to this header once Link()/Unlink() utilities are lifted to project root
			void AddItem( NativelyDrawnItem *item );
			void RemoveItem( NativelyDrawnItem *item );

			void BuildSortedList();
			void DrawItems( int64_t time );
		};

		// Maintain lists of items for positive/negative z-index half-spaces separately
		// so item addition/removal do not affect other list
		HalfSpaceDrawList negativeZItemsList;
		HalfSpaceDrawList positiveZItemsList;

		NativelyDrawnItem *globalItemsListHead { nullptr };

		unsigned nextHandle { 0 };

		DrawnItemsRegistry() {
			memset( hashBins, 0, sizeof( hashBins ) );
		}

		~DrawnItemsRegistry();

		int NextHandle() {
			unsigned u = ++nextHandle;
			// Make sure we never return a zero handle.
			if( !u ) {
				u = ++nextHandle;
			}
			// Negative int handles are perfectly valid.
			// We operate on singed handles since they are prevalent in CEF/JS.
			return (int)u;
		}

		static unsigned HashBinIndex( int handle ) {
			return ( *( unsigned *)&handle ) % NUM_HASH_BINS;
		}

		int LinkNewItem( NativelyDrawnItem *item );
		NativelyDrawnItem *TryUnlinkItem( int itemHandle );

		void DrawNegativeZItems( int64_t time ) { negativeZItemsList.DrawItems( time ); }
		void DrawPositiveZItems( int64_t time ) { positiveZItemsList.DrawItems( time ); };
	};

	DrawnItemsRegistry drawnItemsRegistry;

	bool StopDrawingItem( int drawnItemHandle, const std::type_info &itemTypeInfo );
public:
	explicit RendererCompositionProxy( UiFacade *parent_ );

	~RendererCompositionProxy() {
		delete chromiumBuffer;
	}

	void UpdateChromiumBuffer( const CefRenderHandler::RectList &dirtyRects, const void *buffer, int w, int h );

	void Refresh( int64_t time, bool showCursor, bool background );

	void StartShowingWorldModel( const char *name, bool blurred, bool looping, const std::vector<CameraAnimFrame> &frames );

	int StartDrawingModel( const ModelDrawParams &params );
	int StartDrawingImage( const ImageDrawParams &params );

	bool StopDrawingModel( int drawnModelHandle ) {
		return StopDrawingItem( drawnModelHandle, typeid( DrawnAliasModel ) );
	}

	bool StopDrawingImage( int drawnImageHandle ) {
		return StopDrawingItem( drawnImageHandle, typeid( Drawn2DImage ) );
	}
};

#endif
