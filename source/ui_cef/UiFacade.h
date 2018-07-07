#ifndef QFUSION_UIFACADE_H
#define QFUSION_UIFACADE_H

#include "MessagePipe.h"

#include <string>
#include <include/cef_render_handler.h>

class CefBrowser;
class WswCefRenderHandler;

struct ConnectionState {
	enum {
		SERVER_NAME_ATTACHMENT = 1,
		REJECT_MESSAGE_ATTACHMENT = 2,
		DOWNLOAD_FILENAME_ATTACHMENT = 4
	};

	std::string serverName;
	std::string rejectMessage;
	std::string downloadFileName;
	int downloadType;
	float downloadPercent;
	float downloadSpeed;
	int connectCount;

	bool Equals( ConnectionState *that ) {
		if( !that ) {
			return false;
		}
		// Put cheap tests first
		if( connectCount != that->connectCount || downloadType != that->downloadType ) {
			return false;
		}
		if( downloadPercent != that->downloadPercent || downloadSpeed != that->downloadSpeed ) {
			return false;
		}
		return serverName == that->serverName &&
			   rejectMessage == that->rejectMessage &&
			   downloadFileName == that->downloadFileName;
	}
};

struct DemoPlaybackState {
	std::string demoName;
	unsigned time;
	bool paused;

	bool Equals( const DemoPlaybackState *that ) const {
		if( !that ) {
			return false;
		}
		return time == that->time && paused == that->paused && demoName == that->demoName;
	}
};

struct MainScreenState {
	enum {
		CONNECTION_ATTACHMENT = 1,
		DEMO_PLAYBACK_ATTACHMENT = 2
	};

	ConnectionState *connectionState;
	DemoPlaybackState *demoPlaybackState;
	int clientState;
	int serverState;
	bool showCursor;
	bool background;

	bool operator==( const MainScreenState &that ) const {
		// Put cheap tests first
		if( clientState != that.clientState || serverState != that.serverState ) {
			return false;
		}
		if( showCursor != that.showCursor || background != that.background ) {
			return false;
		}

		if( connectionState && !connectionState->Equals( that.connectionState ) ) {
			return false;
		}
		if( demoPlaybackState && !demoPlaybackState->Equals( that.demoPlaybackState ) ) {
			return false;
		}
		return true;
	}
};

class UiFacade {
	friend class MessagePipe;

	CefRefPtr<CefBrowser> browser;
	WswCefRenderHandler *renderHandler;

	const int width;
	const int height;

	template <typename T>
	class InterleavedStorage {
		T values[2];
		uint8_t currIndex, prevIndex;

	public:
		T &Curr() { return values[currIndex]; }
		T &Prev() { return values[prevIndex]; }
		const T &Curr() const { return values[currIndex]; }
		const T &Prev() const { return values[prevIndex]; }

		void Flip() {
			prevIndex = currIndex;
			currIndex = (uint8_t)( ( prevIndex + 1 ) & 1 );
		}
	};

	struct StateStorage {
		MainScreenState mainState;
		ConnectionState connectionState;
		DemoPlaybackState demoPlaybackState;
	};

	InterleavedStorage<StateStorage> interleavedStateStorage;
	bool hasFlippedStateThisFrame { false };

	int demoProtocol;
	std::string demoExtension;
	std::string basePath;

	MessagePipe messagePipe;

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

	void DrawUi();

	int64_t lastRefreshAt;

	struct shader_s *cursorShader;

	int64_t lastScrollAt;
	int numScrollsInARow;
	int lastScrollDirection;

	int mouseXY[2] { 0, 0 };

	uint32_t GetInputModifiers() const;
	bool ProcessAsMouseKey( int context, int qKey, bool down );
	void AddToScroll( int context, int direction );

	struct cvar_s *menu_sensitivity { nullptr };
	struct cvar_s *menu_mouseAccel { nullptr };
public:
	static bool Init( int argc, char **argv, void *hInstance, int width_, int height_,
					  int demoProtocol_, const char *demoExtension_, const char *basePath_ );

	static void Shutdown() {
		delete instance;
		instance = nullptr;
	}

	static UiFacade *Instance() { return instance; }

	void RegisterRenderHandler( CefRenderHandler *handler_ );
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
};

#endif
