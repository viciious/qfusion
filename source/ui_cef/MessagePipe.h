#ifndef QFUSION_UIBACKENDMESSAGEPIPE_H
#define QFUSION_UIBACKENDMESSAGEPIPE_H

class MessagePipe {
public:
	void Keydown( int context, int key );
	void Keyup( int context, int key );
	void CharEvent( int context, int key );
	void MouseMove( int context, int frameTime, int dx, int dy );
	void MouseSet( int context, int mx, int my, bool showCursor );
	void ForceMenuOff();
	void ShowQuickMenu( bool show );
};

#endif
