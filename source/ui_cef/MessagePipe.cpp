#include "MessagePipe.h"
#include "UiFacade.h"
#include "CefApp.h"
#include "Ipc.h"
#include "ScreenState.h"

#include "include/cef_browser.h"
#include "Api.h"
#include <memory>
#include "../gameshared/q_keycodes.h"

void MessagePipe::SendProcessMessage( CefRefPtr<CefProcessMessage> message ) {
	CefBrowser *browser = parent->GetBrowser();
	assert( browser );
	assert( isReady );
	browser->SendProcessMessage( PID_RENDERER, message );
}

inline CefRefPtr<CefBrowserHost> MessagePipe::GetBrowserHost() {
	return parent->GetBrowser()->GetHost();
}

CefMouseEvent MessagePipe::NewMouseEvent( uint32_t modifiers ) const {
	CefMouseEvent event;
	event.x = parent->mouseXY[0];
	event.y = parent->mouseXY[1];
	event.modifiers = modifiers;
	return event;
}

void MessagePipe::MouseMove( int context, uint32_t modifiers ) {
	if( !isReady ) {
		return;
	}

	GetBrowserHost()->SendMouseMoveEvent( NewMouseEvent( modifiers ), false );
}

void MessagePipe::MouseScroll( int context, int scrollY, uint32_t modifiers ) {
	if( !isReady ) {
		return;
	}

	GetBrowserHost()->SendMouseWheelEvent( NewMouseEvent( modifiers ), 0, scrollY );
}

void MessagePipe::MouseClick( int context, int button, int clicksCount, uint32_t modifiers, bool down ) {
	if( !isReady ) {
		return;
	}

	assert( button >= 1 && button <= 3 );
	// Triple clicks are allowed...
	assert( clicksCount >= 1 && clicksCount <= 3 );

	cef_mouse_button_type_t buttonTypes[] = { MBT_LEFT, MBT_RIGHT, MBT_MIDDLE };
	GetBrowserHost()->SendMouseClickEvent( NewMouseEvent( modifiers ), buttonTypes[button - 1], !down, clicksCount );
}

void MessagePipe::KeyUpOrDown( int context, int qKey, int nativeScanCode,
							   int nativeKeyCode, uint32_t modifiers, bool down ) {
	CefKeyEvent event;
	event.modifiers = modifiers;
	event.windows_key_code = qKey;
	event.native_key_code = qKey;
	event.character = 0;
	event.unmodified_character = 0;
	event.focus_on_editable_field = false;
	event.is_system_key = false;

	if( !down ) {
		event.type = KEYEVENT_KEYUP;
		GetBrowserHost()->SendKeyEvent( event );
		return;
	}

	event.type = KEYEVENT_RAWKEYDOWN;
	GetBrowserHost()->SendKeyEvent( event );
}

void MessagePipe::CharEvent( int context, int qKey, int character,
							 int nativeKeyCode, int nativeScanCode, uint32_t modifiers ) {
	CefKeyEvent event;
	event.modifiers = modifiers;
	event.windows_key_code = qKey;
	event.native_key_code = qKey;
	event.character = character;
	event.unmodified_character = character;
	event.is_system_key = false;
	event.focus_on_editable_field = false;
	event.type = KEYEVENT_CHAR;

	GetBrowserHost()->SendKeyEvent( event );
}

void MessagePipe::MouseSet( int context, int mx, int my, bool showCursor ) {
	if( isReady ) {
		mouseSetSender.AcquireAndSend( MouseSetMessage::NewPooledObject( context, mx, my, showCursor ) );
		return;
	}

	// Allocate the message using the default heap and not the allocator (since its capacity is limited)
	auto messagePtr = std::make_unique<MouseSetMessage>( context, mx, my, showCursor, nullptr );
	deferredMessages.emplace_back( std::make_pair( std::move( messagePtr ), &mouseSetSender ) );
}

void MessagePipe::ForceMenuOff() {
	/*
	if( isReady ) {
		SendForceMenuOff();
		return;
	}

	deferredMessages.emplace_back( std::make_unique<DeferredForceMenuOff>() );
	 */
	// TODO: Execute command?
}

void MessagePipe::ShowQuickMenu( bool show ) {
	// We do not support quick menus, do we?
}

void MessagePipe::ExecuteCommand( int argc, const char *( *getArg )( int ) ) {
	if( isReady ) {
		auto argGetter = [=]( int argNum ) {
			CefString s;
			s.FromASCII( getArg( argNum ) );
			return s;
		};
		gameCommandSender.AcquireAndSend( ProxyingGameCommandMessage::NewPooledObject( argc, std::move( argGetter ) ) );
		return;
	}

	std::vector<std::string> args;
	for( int i = 0; i < argc; ++i ) {
		args.emplace_back( std::string( getArg( i ) ) );
	}

	// Allocate the message using a default heap...
	auto messagePtr( std::make_unique<DeferredGameCommandMessage>( std::move( args ), nullptr ) );
	deferredMessages.emplace_back( std::make_pair( std::move( messagePtr ), &gameCommandSender ) );
}

void MessagePipe::OnUiPageReady() {
	isReady = true;

	for( auto &messageAndSender: deferredMessages ) {
		// Lets put full types here for clarity
		SimplexMessageSender *sender = messageAndSender.second;
		// Release the ownership of the message
		SimplexMessage *message = messageAndSender.first.release();
		// The message is going to be deleted in this call (even if it has been allocated in the default heap)
		sender->AcquireAndSend( message );
	}

	deferredMessages.clear();
	deferredMessages.shrink_to_fit();

	GetBrowserHost()->SendFocusEvent( true );
}

void MessagePipe::ConsumeScreenState( MainScreenState *currScreenState ) {
	if( !isReady ) {
		// This is important, either delete the state or transfer an ownership to the sender
		currScreenState->DeleteSelf();
		return;
	}

	auto *updateMessage = UpdateScreenMessage::NewPooledObject( currScreenState );
	updateScreenSender.AcquireAndSend( AllocatorChild::CheckShouldDelete( updateMessage ) );
}
