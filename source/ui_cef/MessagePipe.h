#ifndef QFUSION_UIBACKENDMESSAGEPIPE_H
#define QFUSION_UIBACKENDMESSAGEPIPE_H

#include "include/cef_browser.h"

struct MainScreenState;
class UiFacade;

class MessagePipe {
	UiFacade *parent;

	bool isReady { false };
	bool wasReady { false };

	inline CefRefPtr<CefBrowserHost> GetBrowserHost();
	inline CefMouseEvent NewMouseEvent( uint32_t modifiers ) const;

	void KeyUpOrDown( int context, int qKey, int nativeScanCode, int nativeKeyCode, uint32_t modifiers, bool down );

	void SendMouseSet( int context, int mx, int my, bool showCursor );
	void SendForceMenuOff();
	// Currently is not doable in generic way without conversion to std::vector of std::string in the most frequent case
	// Should be rewritten once java-like iterators are introduced.
	void SendExecuteCommand( const std::vector<std::string> &args );
	void SendExecuteCommand( int argc, const char *( getArg )( int ) );

	struct DeferredMessage {
		virtual ~DeferredMessage() {}
		virtual void Send( MessagePipe *pipe ) const = 0;
	};

	struct DeferredMouseSet: public DeferredMessage {
		int context, mx, my;
		bool showCursor;

		DeferredMouseSet( int context_, int mx_, int my_, bool showCursor_ )
			: context( context_ ), mx( mx_ ), my( my_ ), showCursor( showCursor_ ) {}

		void Send( MessagePipe *pipe ) const override {
			pipe->SendMouseSet( context, mx, my, showCursor );
		}
	};

	struct DeferredForceMenuOff: public DeferredMessage {
		void Send( MessagePipe *pipe ) const override {
			pipe->SendForceMenuOff();
		}
	};

	struct DeferredExecuteCommand: public DeferredMessage {
		std::vector<std::string> args;

		explicit DeferredExecuteCommand( std::vector<std::string> &&args_ ): args( args_ ) {}

		void Send( MessagePipe *pipe ) const override {
			pipe->SendExecuteCommand( args );
		}
	};

	std::vector<std::unique_ptr<DeferredMessage>> deferredMessages;

	inline void SendMessage( CefRefPtr<CefProcessMessage> message );
public:
	explicit MessagePipe( UiFacade *parent_ ) : parent( parent_ ) {}

	void KeyUp( int context, int qKey, int nativeScanCode, int nativeKeyCode, uint32_t modifiers ) {
		KeyUpOrDown( context, qKey, nativeScanCode, nativeKeyCode, modifiers, false );
	}
	void KeyDown( int context, int qKey, int nativeScanCode, int nativeKeyCode, uint32_t modifiers ) {
		KeyUpOrDown( context, qKey, nativeScanCode, nativeKeyCode, modifiers, true );
	}

	void CharEvent( int context, int qKey, int character, int nativeKeyCode, int nativeScanCode, uint32_t modifiers );

	void MouseScroll( int context, int scrollY, uint32_t modifiers );
	void MouseClick( int context, int button, int clicksCount, uint32_t modifiers, bool down );
	void MouseMove( int context, uint32_t modifiers );

	void MouseSet( int context, int mx, int my, bool showCursor );
	void ForceMenuOff();
	void ShowQuickMenu( bool show );
	void ExecuteCommand( int argc, const char *( *getArg )( int ) );
	void OnUiPageReady();

	void UpdateScreenState( const MainScreenState &oldState, const MainScreenState &currState );
};

#endif
