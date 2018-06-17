#ifndef QFUSION_UIFACADE_H
#define QFUSION_UIFACADE_H

#include "MessagePipe.h"

#include <string>

class UiFacade {
	const int width;
	const int height;

	struct FrameState {
		int clientState;
		int serverState;
		unsigned demoTime;
		std::string demoFileName;
		bool demoPlaying;
		bool demoPaused;
		bool showCursor;
		bool background;
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
	};

	FrameState frameStateStorage[2];
	ConnectScreenState connectScreenStateStorage[2];


	int demoProtocol;
	std::string demoExtension;
	std::string basePath;

	int interleavedStateIndex;

	MessagePipe messagePipe;

	UiFacade( int width_, int height_, int demoProtocol_, const char *demoExtension_, const char *basePath_ )
		: width( width_ )
		, height( height_ )
		, demoProtocol( demoProtocol_ )
		, demoExtension( demoExtension_ )
		, basePath( basePath_ )
		, interleavedStateIndex( 0 ) {}
public:
	static UiFacade *Create( int argc, char **argv, void *hInstance, int width_, int height_,
							 int demoProtocol_, const char *demoExtension_, const char *basePath_ );

	~UiFacade();

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
