#ifndef UI_CEF_COMPOSITOR_H
#define UI_CEF_COMPOSITOR_H

#include "CameraAnimator.h"
#include <string>
#include <include/cef_render_handler.h>

class UiFacade;

class RendererCompositionProxy {
	UiFacade *const parent;

	uint8_t *chromiumBuffer;

	struct shader_s *whiteShader { nullptr };
	struct shader_s *cursorShader { nullptr };
	struct shader_s *chromiumShader { nullptr };

	std::string pendingWorldModel;
	CameraAnimator cameraAnimator;

	bool hasPendingWorldModel { false };
	bool hasStartedWorldModelLoading { false };
	bool hasSucceededWorldModelLoading { false };

	bool hadOwnBackground { false };

	bool blurWorldModel { false };

	void DrawWorldModel( int64_t time, int width, int height, bool blurred = false );

	void CheckAndDrawBackground( int64_t time, int width, int height, bool blurred = false );

	inline void ResetBackground();

	inline void RegisterChromiumBufferShader();
public:
	explicit RendererCompositionProxy( UiFacade *parent_ );

	~RendererCompositionProxy() {
		delete chromiumBuffer;
	}

	void UpdateChromiumBuffer( const CefRenderHandler::RectList &dirtyRects, const void *buffer, int w, int h );

	void Refresh( int64_t time, bool showCursor, bool background );

	void StartShowingWorldModel( const char *name, bool blurred, bool looping, const std::vector<CameraAnimFrame> &frames );
};

#endif
