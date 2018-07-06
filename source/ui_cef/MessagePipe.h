#ifndef QFUSION_UIBACKENDMESSAGEPIPE_H
#define QFUSION_UIBACKENDMESSAGEPIPE_H

#include "include/cef_browser.h"

struct MainScreenState;
class UiFacade;

class MessagePipe {
	UiFacade *parent;

	int mouseX { 0 };
	int mouseY { 0 };

	bool isReady { false };
	bool wasReady { false };

	uint32_t GetInputModifiers() const;
	void SendMouseScrollOrButtonEvent( int context, int qKey, bool down );
	void FillKeyEvent( CefKeyEvent *event, int qKey );

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

	void Keydown( int context, int key );
	void Keyup( int context, int key );
	void CharEvent( int context, int key );
	void MouseMove( int context, int frameTime, int dx, int dy );
	void MouseSet( int context, int mx, int my, bool showCursor );
	void ForceMenuOff();
	void ShowQuickMenu( bool show );
	void ExecuteCommand( int argc, const char *( *getArg )( int ) );
	void OnUiPageReady();

	void UpdateScreenState( const MainScreenState &oldState, const MainScreenState &currState );
};

#endif
