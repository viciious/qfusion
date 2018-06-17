#include "MessagePipe.h"
#include "include/cef_browser.h"

void MessagePipe::Keydown( int context, int key ) {
	// Send a specialized message
}

void MessagePipe::Keyup( int context, int key ) {
	// Send a specialized message
}

void MessagePipe::CharEvent( int context, int key ) {
	// Send a specialized message
}

void MessagePipe::MouseMove( int context, int frameTime, int dx, int dy ) {
	// Send a specialized message
}

void MessagePipe::MouseSet( int context, int mx, int my, bool showCursor ) {
	// Send generic message
}

void MessagePipe::ForceMenuOff() {
	// Send generic message
}

void MessagePipe::ShowQuickMenu( bool show ) {
	// Send generic message
}

