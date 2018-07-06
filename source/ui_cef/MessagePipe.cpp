#include "MessagePipe.h"
#include "UiFacade.h"
#include "CefApp.h"
#include "Ipc.h"

#include "include/cef_browser.h"
#include "Api.h"
#include <memory>
#include "../gameshared/q_keycodes.h"

inline void MessagePipe::SendMessage( CefRefPtr<CefProcessMessage> message ) {
	CefBrowser *browser = parent->GetBrowser();
	assert( browser );
	assert( isReady );
	browser->SendProcessMessage( PID_RENDERER, message );
}

uint32_t MessagePipe::GetInputModifiers() const {
	uint32_t result = 0;
	if( api->Key_IsDown( K_LCTRL ) || api->Key_IsDown( K_RCTRL ) ) {
		result |= EVENTFLAG_CONTROL_DOWN;
	}
	if( api->Key_IsDown( K_LALT ) || api->Key_IsDown( K_RALT ) ) {
		result |= EVENTFLAG_ALT_DOWN;
	}
	if( api->Key_IsDown( K_LSHIFT ) || api->Key_IsDown( K_RSHIFT ) ) {
		result |= EVENTFLAG_SHIFT_DOWN;
	}
	if( api->Key_IsDown( K_CAPSLOCK ) ) {
		result |= EVENTFLAG_CAPS_LOCK_ON;
	}
	if( api->Key_IsDown( K_MOUSE1 ) ) {
		result |= EVENTFLAG_LEFT_MOUSE_BUTTON;
	}
	if( api->Key_IsDown( K_MOUSE2 ) ) {
		result |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
	}
	if( api->Key_IsDown( K_MOUSE3 ) ) {
		result |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
	}
	return result;
}

void MessagePipe::SendMouseScrollOrButtonEvent( int context, int key, bool down ) {
	CefMouseEvent event;
	event.x = mouseX;
	event.y = mouseY;
	event.modifiers = GetInputModifiers();

	if( key == K_MWHEELUP || key == K_MWHEELDOWN ) {
		parent->GetBrowser()->GetHost()->SendMouseWheelEvent( event, 0, key == K_MWHEELUP ? 10 : -10 );
		return;
	}

	int clickCount = 1;
	CefBrowserHost::MouseButtonType type;
	if( key == K_MOUSE1 ) {
		type = MBT_LEFT;
	} else if( key == K_MOUSE2 ) {
		type = MBT_RIGHT;
	} else if( key == K_MOUSE3 ) {
		type = MBT_MIDDLE;
	} else if( key == K_MOUSE1DBLCLK ) {
		type = MBT_LEFT;
		clickCount = 2;
	} else {
		return;
	}
	parent->GetBrowser()->GetHost()->SendMouseClickEvent( event, type, !down, clickCount );
}

void MessagePipe::FillKeyEvent( CefKeyEvent *event, int qKey ) {
	event->modifiers = GetInputModifiers();
	event->native_key_code = qKey;
	event->windows_key_code = qKey;
	event->focus_on_editable_field = false;
	event->character = qKey;
	event->unmodified_character = qKey;
}

void MessagePipe::Keydown( int context, int key ) {
	if( !isReady ) {
		return;
	}

	if( key >= K_MOUSE1 ) {
		SendMouseScrollOrButtonEvent( context, key, true );
		return;
	}

	CefKeyEvent event;
	FillKeyEvent( &event, key );
	event.type = KEYEVENT_RAWKEYDOWN;
	parent->GetBrowser()->GetHost()->SendKeyEvent( event );
	event.type = KEYEVENT_CHAR;
	parent->GetBrowser()->GetHost()->SendKeyEvent( event );
}

void MessagePipe::Keyup( int context, int key ) {
	if( !isReady ) {
		return;
	}

	if( key >= K_MOUSE1 ) {
		SendMouseScrollOrButtonEvent( context, key, true );
		return;
	}

	CefKeyEvent event;
	FillKeyEvent( &event, key );
	event.type = KEYEVENT_KEYUP;
	parent->GetBrowser()->GetHost()->SendKeyEvent( event );
}

void MessagePipe::CharEvent( int context, int key ) {
	if( !isReady ) {
		return;
	}

	CefKeyEvent event;
	event.type = KEYEVENT_CHAR;
	FillKeyEvent( &event, key );
	parent->GetBrowser()->GetHost()->SendKeyEvent( event );
}

void MessagePipe::MouseMove( int context, int frameTime, int dx, int dy ) {
	mouseX += dx;
	clamp( mouseX, 0, parent->width );
	mouseY += dy;
	clamp( mouseY, 0, parent->height );

	if( !isReady ) {
		return;
	}

	CefMouseEvent event;
	event.x = mouseX;
	event.y = mouseY;
	event.modifiers = GetInputModifiers();
	parent->GetBrowser()->GetHost()->SendMouseMoveEvent( event, false );
}

void MessagePipe::MouseSet( int context, int mx, int my, bool showCursor ) {
	mouseX = mx;
	assert( mouseX >= 0 && mouseX < parent->width );
	mouseY = my;
	assert( mouseY >= 0 && mouseY < parent->height );

	if( isReady ) {
		SendMouseSet( context, mx, my, showCursor );
		return;
	}

	deferredMessages.emplace_back( std::make_unique<DeferredMouseSet>( context, mx, my, showCursor ) );
}

void MessagePipe::ForceMenuOff() {
	if( isReady ) {
		SendForceMenuOff();
		return;
	}

	deferredMessages.emplace_back( std::make_unique<DeferredForceMenuOff>() );
}

void MessagePipe::ShowQuickMenu( bool show ) {
	// We do not support quick menus, do we?
}

void MessagePipe::ExecuteCommand( int argc, const char *( *getArg )( int ) ) {
	if( isReady ) {
		SendExecuteCommand( argc, getArg );
		return;
	}

	std::vector<std::string> args;
	for( int i = 0; i < argc; ++i ) {
		args.emplace_back( std::string( getArg( i ) ) );
	}

	deferredMessages.emplace_back( std::make_unique<DeferredExecuteCommand>( std::move( args ) ) );
}

void MessagePipe::SendMouseSet( int context, int mx, int my, bool showCursor ) {
	auto message( CefProcessMessage::Create( ExecutingJSMessageHandler::mouseSet ) );
	auto messageArgs( message->GetArgumentList() );
	messageArgs->SetInt( 0, context );
	messageArgs->SetInt( 1, mx );
	messageArgs->SetInt( 2, my );
	messageArgs->SetBool( 3, showCursor );
	SendMessage( message );
}

void MessagePipe::SendForceMenuOff() {
	SendMessage( CefProcessMessage::Create( "forceMenuOff" ) );
}

void MessagePipe::SendExecuteCommand( const std::vector<std::string> &args ) {
	auto message( CefProcessMessage::Create( ExecutingJSMessageHandler::gameCommand ) );
	auto messageArgs( message->GetArgumentList() );
	for( size_t i = 0; i < args.size(); ++i ) {
		messageArgs->SetString( i, args[i] );
	}

	SendMessage( message );
}

void MessagePipe::SendExecuteCommand( int argc, const char *( *getArg )( int ) ) {
	auto message( CefProcessMessage::Create( ExecutingJSMessageHandler::gameCommand ) );
	auto messageArgs( message->GetArgumentList() );
	for( int i = 0; i < argc; ++i ) {
		messageArgs->SetString( (size_t)i, getArg( i ) );
	}

	SendMessage( message );
}

void MessagePipe::OnUiPageReady() {
	isReady = true;

	for( auto &message: deferredMessages ) {
		message->Send( this );
	}

	deferredMessages.clear();
	deferredMessages.shrink_to_fit();

	parent->GetBrowser()->GetHost()->SendFocusEvent( true );
}

void MessagePipe::UpdateScreenState( const MainScreenState &prevState, const MainScreenState &currState ) {
	if( !isReady ) {
		return;
	}

	// Skip updates if there was at least a single update and the new state is the same
	if( wasReady && prevState == currState ) {
		return;
	}

	wasReady = true;

	auto message( CefProcessMessage::Create( ExecutingJSMessageHandler::updateScreen ) );
	auto args( message->GetArgumentList() );

	// Write the main part, no reasons to use a delta encoding for it

	size_t argNum = 0;
	args->SetInt( argNum++, currState.clientState );
	args->SetInt( argNum++, currState.serverState );
	args->SetBool( argNum++, currState.showCursor );
	args->SetBool( argNum++, currState.background );

	if( !currState.connectionState && !currState.demoPlaybackState ) {
		SendMessage( message );
		return;
	}

	// Write attachments, either demo playback state or connection (process of connection) state
	if( const auto &dps = currState.demoPlaybackState ) {
		args->SetInt( argNum++, MainScreenState::DEMO_PLAYBACK_ATTACHMENT );
		args->SetInt( argNum++, (int)dps->time );
		args->SetBool( argNum++, dps->paused );
		// Send a demo name only if it is needed
		if( !prevState.demoPlaybackState || ( dps->demoName != prevState.demoPlaybackState->demoName ) ) {
			args->SetString( argNum++, dps->demoName );
		}
		SendMessage( message );
		return;
	}

	args->SetInt( argNum++, MainScreenState::CONNECTION_ATTACHMENT );
	auto *currConnState = currState.connectionState;
	assert( currConnState );
	auto *prevConnState = prevState.connectionState;

	// Write shared fields (download numbers are always written to simplify parsing of transmitted result)
	args->SetInt( argNum++, currConnState->connectCount );
	args->SetInt( argNum++, currConnState->downloadType );
	args->SetDouble( argNum++, currConnState->downloadSpeed );
	args->SetDouble( argNum++, currConnState->downloadPercent );

	int flags = 0;
	if( !prevConnState ) {
		if( !currConnState->serverName.empty() ) {
			flags |= ConnectionState::SERVER_NAME_ATTACHMENT;
		}
		if( !currConnState->rejectMessage.empty() ) {
			flags |= ConnectionState::REJECT_MESSAGE_ATTACHMENT;
		}
		if( !currConnState->downloadFileName.empty() ) {
			flags |= ConnectionState::DOWNLOAD_FILENAME_ATTACHMENT;
		}
	} else {
		if( currConnState->serverName != prevConnState->serverName ) {
			flags |= ConnectionState::SERVER_NAME_ATTACHMENT;
		}
		if( currConnState->rejectMessage != prevConnState->rejectMessage ) {
			flags |= ConnectionState::REJECT_MESSAGE_ATTACHMENT;
		}
		if( currConnState->downloadFileName != prevConnState->downloadFileName ) {
			flags |= ConnectionState::DOWNLOAD_FILENAME_ATTACHMENT;
		}
	}

	args->SetInt( argNum++, flags );
	if( flags & ConnectionState::SERVER_NAME_ATTACHMENT ) {
		args->SetString( argNum++, currConnState->serverName );
	}
	if( flags & ConnectionState::REJECT_MESSAGE_ATTACHMENT ) {
		args->SetString( argNum++, currConnState->rejectMessage );
	}
	if( flags & ConnectionState::DOWNLOAD_FILENAME_ATTACHMENT ) {
		args->SetString( argNum++, currConnState->downloadFileName );
	}
}
