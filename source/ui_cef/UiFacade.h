#ifndef QFUSION_UIFACADE_H
#define QFUSION_UIFACADE_H

#include "Logger.h"
#include "MessagePipe.h"
#include "Ipc.h"
#include "RendererCompositionProxy.h"

#include "../gameshared/q_math.h"
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <string>
#include <include/cef_render_handler.h>

class CefBrowser;
class WswCefRenderHandler;

class BrowserProcessLogger: public Logger {
	void SendLogMessage( cef_log_severity_t severity, const char *format, va_list va ) override;
};

class UiFacade {
	friend class MessagePipe;
	friend class WswCefRenderHandler;
	friend class RendererCompositionProxy;

	CefRefPtr<CefBrowser> browser;
	WswCefRenderHandler *renderHandler;

	const int width;
	const int height;

	MainScreenState *thisFrameScreenState { nullptr };

	int demoProtocol;
	std::string demoExtension;
	std::string basePath;

	MessagePipe messagePipe;

	RendererCompositionProxy rendererCompositionProxy;

	UiFacade( int width_, int height_, int demoProtocol_, const char *demoExtension_, const char *basePath_ );
	~UiFacade();

	static void MenuOpenHandler() { Instance()->MenuCommand(); }
	static void MenuCloseHandler() { Instance()->MenuCommand(); }
	static void MenuForceHandler() { Instance()->MenuCommand(); }
	static void MenuModalHandler() { Instance()->MenuCommand(); }

	void MenuCommand();

	static UiFacade *instance;

	CefBrowser *GetBrowser() { return browser.get(); }

	static bool InitCef( int argc, char **argv, void *hInstance, int width, int height );

	int64_t lastRefreshAt;
	int64_t lastScrollAt;
	int numScrollsInARow;
	int lastScrollDirection;

	int mouseXY[2] { 0, 0 };

	uint32_t GetInputModifiers() const;
	bool ProcessAsMouseKey( int context, int qKey, bool down );
	void AddToScroll( int context, int direction );

	struct cvar_s *menu_sensitivity { nullptr };
	struct cvar_s *menu_mouseAccel { nullptr };

	BrowserProcessLogger logger;
public:
	static bool Init( int argc, char **argv, void *hInstance, int width_, int height_,
					  int demoProtocol_, const char *demoExtension_, const char *basePath_ );

	static void Shutdown() {
		delete instance;
		instance = nullptr;
	}

	static UiFacade *Instance() { return instance; }

	Logger *Logger() { return &logger; }

	void RegisterBrowser( CefRefPtr<CefBrowser> browser_ );
	void UnregisterBrowser( CefRefPtr<CefBrowser> browser_ );

	void OnUiPageReady() {
		messagePipe.OnUiPageReady();
	}

	int Width() const { return width; }
	int Height() const { return height; }

	void Refresh( int64_t time, int clientState, int serverState,
				  bool demoPlaying, const char *demoName, bool demoPaused,
				  unsigned int demoTime, bool backGround, bool showCursor );

	void UpdateConnectScreen( const char *serverName, const char *rejectMessage,
							  int downloadType, const char *downloadFilename,
							  float downloadPercent, int downloadSpeed,
							  int connectCount, bool backGround );

	void Keydown( int context, int qKey );

	void Keyup( int context, int qKey );

	void CharEvent( int context, int qKey );

	void MouseMove( int context, int frameTime, int dx, int dy );

	void MouseSet( int context, int mx, int my, bool showCursor ) {
		messagePipe.MouseSet( context, mx, my, showCursor );
	}

	void ForceMenuOff() {
		messagePipe.ForceMenuOff();
	}

	void ShowQuickMenu( bool show ) {
		messagePipe.ShowQuickMenu( show );
	}

	void StartShowingWorldModel( const char *name, bool blurred, bool looping, const std::vector<CameraAnimFrame> &frames ) {
		rendererCompositionProxy.StartShowingWorldModel( name, blurred, looping, frames );
	}
};

#endif
