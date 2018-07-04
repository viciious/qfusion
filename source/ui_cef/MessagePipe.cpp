#include "MessagePipe.h"
#include "UiFacade.h"
#include "CefApp.h"
#include "Ipc.h"

#include "include/cef_browser.h"
#include <memory>

inline void MessagePipe::SendMessage( CefRefPtr<CefProcessMessage> message ) {
	CefBrowser *browser = parent->GetBrowser();
	assert( browser );
	assert( isReady );
	browser->SendProcessMessage( PID_RENDERER, message );
}

void MessagePipe::Keydown( int context, int key ) {
	if( !isReady ) {
		printf( "Keydown: Is not ready, ignoring\n" );
	}
}

void MessagePipe::Keyup( int context, int key ) {
	if( !isReady ) {
		printf( "Keyup: Is not ready, ignoring\n" );
	}
	// Send a specialized message
}

void MessagePipe::CharEvent( int context, int key ) {
	if( !isReady ) {
		printf( "CharEvent: Is not ready, ignoring\n" );
	}
}

void MessagePipe::MouseMove( int context, int frameTime, int dx, int dy ) {
	if( !isReady ) {
		printf( "MouseMove: Is not ready, ignoring\n" );
	}
}

void MessagePipe::MouseSet( int context, int mx, int my, bool showCursor ) {
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
}

void MessagePipe::UpdateMainScreenState( const MainScreenState &prevState, const MainScreenState &currState ) {
	if( !isReady ) {
		return;
	}

	// No delta encoding for now, just checking equality
	if( wasReady && prevState == currState ) {
		return;
	}

	wasReady = true;

	auto message( CefProcessMessage::Create( ExecutingJSMessageHandler::updateMainScreen ) );
	auto args( message->GetArgumentList() );

	args->SetInt( 0, currState.clientState );
	args->SetInt( 1, currState.serverState );
	args->SetInt( 2, currState.demoTime );
	args->SetString( 3, currState.demoName );
	args->SetBool( 4, currState.demoPlaying );
	args->SetBool( 5, currState.demoPaused );
	args->SetBool( 6, currState.showCursor );
	args->SetBool( 7, currState.background );

	SendMessage( message );
}

void MessagePipe::UpdateConnectScreenState( const ConnectScreenState &prevState, const ConnectScreenState &currState ) {
	if( !isReady ) {
		return;
	}

	// No delta encoding for now, just checking equality
	if( wasReady && prevState == currState ) {
		return;
	}

	wasReady = true;

	auto message( CefProcessMessage::Create( ExecutingJSMessageHandler::updateConnectScreen ) );
	auto args( message->GetArgumentList() );

	args->SetString( 0, currState.serverName );
	args->SetString( 1, currState.rejectMessage );
	args->SetString( 2, currState.downloadFileName );
	args->SetInt( 3, currState.downloadType );
	args->SetDouble( 4, currState.downloadPercent );
	args->SetDouble( 5, currState.downloadSpeed );
	args->SetInt( 6, currState.connectCount );
	args->SetBool( 7, currState.background );

	SendMessage( message );
}
