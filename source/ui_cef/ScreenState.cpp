#include "ScreenState.h"

static ReusableItemsAllocator<MainScreenState, 2> screenStateAllocator;
static ReusableItemsAllocator<ConnectionState, 2> connectionStateAllocator;
static ReusableItemsAllocator<DemoPlaybackState, 2> demoPlaybackStateAllocator;

ConnectionState* ConnectionState::NewPooledObject() {
	return AllocatorChild::CheckShouldDelete( connectionStateAllocator.New() );
}

void ConnectionState::OnBeforeAllocatorFreeCall() {
	serverName.resize( 0 );
	rejectMessage.resize( 0 );
	downloadFileName.resize( 0 );
	downloadType = 0;
	downloadPercent = 0.0f;
	downloadSpeed = 0.0f;
	connectCount = 0;
}

bool ConnectionState::Equals( ConnectionState *that ) const {
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

DemoPlaybackState *DemoPlaybackState::NewPooledObject() {
	return AllocatorChild::CheckShouldDelete( demoPlaybackStateAllocator.New() );
}

void DemoPlaybackState::OnBeforeAllocatorFreeCall() {
	demoName.resize( 0 );
	time = 0;
	paused = false;
}

bool DemoPlaybackState::Equals( const DemoPlaybackState *that ) const {
	if( !that ) {
		return false;
	}
	return time == that->time && paused == that->paused && demoName == that->demoName;
}

MainScreenState *MainScreenState::NewPooledObject() {
	return screenStateAllocator.New();
}

void MainScreenState::OnBeforeAllocatorFreeCall() {
	if( connectionState ) {
		connectionState->DeleteSelf();
		connectionState = nullptr;
	}
	if( demoPlaybackState ) {
		demoPlaybackState->DeleteSelf();
		demoPlaybackState = nullptr;
	}
	clientState = 0;
	serverState = 0;
	showCursor = false;
	background = false;
}

bool MainScreenState::operator==( const MainScreenState &that ) const {
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