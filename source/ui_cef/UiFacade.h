#ifndef QFUSION_UIFACADE_H
#define QFUSION_UIFACADE_H

#include "MessagePipe.h"

#include <string>
#include <include/cef_render_handler.h>

class CefBrowser;
class WswCefRenderHandler;

struct MainScreenState {
	int clientState;
	int serverState;
	unsigned demoTime;
	std::string demoName;
	bool demoPlaying;
	bool demoPaused;
	bool showCursor;
	bool background;

	bool operator==( const MainScreenState &that ) const {
		return clientState == that.clientState &&
			   serverState == that.serverState &&
			   demoTime == that.demoTime &&
			   demoPlaying == that.demoPlaying &&
			   demoPaused == that.demoPaused &&
			   showCursor == that.showCursor &&
			   background == that.background &&
			   demoName == that.demoName;
	}
};

struct ConnectScreenState {
	std::string serverName;
	std::string rejectMessage;
	std::string downloadFileName;
	int downloadType;
	float downloadPercent;
	float downloadSpeed;
	int connectCount;
	bool background;

	bool operator==( const ConnectScreenState &that ) const {
		return downloadType == that.downloadType &&
			   downloadPercent == that.downloadPercent &&
			   downloadSpeed == that.downloadSpeed &&
			   connectCount == that.connectCount &&
			   background == that.background &&
			   serverName == that.serverName &&
			   rejectMessage == that.rejectMessage &&
			   downloadFileName == that.downloadFileName;
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

	InterleavedStorage<MainScreenState> mainScreenState;
	InterleavedStorage<ConnectScreenState> connectScreenState;

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

	static bool InitCef( int argc, char **argv, void *hInstance );

	void DrawUi();
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

	void Keydown( int context, int key ) {
		messagePipe.Keydown( context, key );
	}

	void Keyup( int context, int key ) {
		messagePipe.Keyup( context, key );
	}

	void CharEvent( int context, int key ) {
		messagePipe.CharEvent( context, key );
	}

	void MouseMove( int context, int frameTime, int dx, int dy ) {
		messagePipe.MouseMove( context, frameTime, dx, dy );
	}

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
