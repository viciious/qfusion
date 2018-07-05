#ifndef UI_CEF_V8HANDLER_H
#define UI_CEF_V8HANDLER_H

#include "Ipc.h"

#include "include/cef_v8.h"
#include <unordered_map>

class WswCefV8Handler: public CefV8Handler {
	friend class PendingCallbackRequest;
	friend class PendingRequestLauncher;
	friend class WswCefRenderProcessHandler;
	friend class ExecutingJSMessageHandler;

	WswCefRenderProcessHandler *renderProcessHandler;

	PendingRequestLauncher *requestLaunchersHead;

	ExecutingJSMessageHandler *messageHandlersHead;

	GetCVarRequestLauncher getCVar;
	SetCVarRequestLauncher setCVar;
	ExecuteCmdRequestLauncher executeCmd;
	GetVideoModesRequestLauncher getVideoModes;
	GetDemosAndSubDirsRequestLauncher getDemosAndSubDirs;
	GetDemoMetaDataRequestLauncher getDemoMetaData;
	GetHudsRequestLauncher getHuds;
	GetGametypesRequestLauncher getGametypes;
	GetMapsRequestLauncher getMaps;
	GetLocalizedStringsRequestLauncher getLocalizedStrings;
	GetKeyBindingsRequestLauncher getKeyBindings;
	GetKeyNamesRequestLauncher getKeyNames;

	GameCommandHandler gameCommandHandler;
	MouseSetHandler mouseSetHandler;
	UpdateScreenHandler updateScreenHandler;

	std::unordered_map<int, std::shared_ptr<PendingCallbackRequest>> callbacks;
	// We use an unsigned counter to ensure that the overflow behaviour is defined
	unsigned callId;

	inline int NextCallId() { return (int)( callId++ ); }

	void ProcessAsAwaitedReply( CefRefPtr<CefProcessMessage> &message );

	inline RenderProcessLogger *Logger();
public:
	explicit WswCefV8Handler( WswCefRenderProcessHandler *renderProcessHandler_ )
		: renderProcessHandler( renderProcessHandler_ )
		, requestLaunchersHead( nullptr )
		, messageHandlersHead( nullptr )
		, getCVar( this )
		, setCVar( this )
		, executeCmd( this )
		, getVideoModes( this )
		, getDemosAndSubDirs( this )
		, getDemoMetaData( this )
		, getHuds( this )
		, getGametypes( this )
		, getMaps( this )
		, getLocalizedStrings( this )
		, getKeyBindings( this )
		, getKeyNames( this )
		, gameCommandHandler( this )
		, mouseSetHandler( this )
		, updateScreenHandler( this )
		, callId( 0 ) {}

	bool Execute( const CefString& name,
				  CefRefPtr<CefV8Value> object,
				  const CefV8ValueList& arguments,
				  CefRefPtr<CefV8Value>& retval,
				  CefString& exception ) override;

	bool TryHandle( CefRefPtr<CefBrowser> &browser, CefRefPtr<CefProcessMessage> &message );

	IMPLEMENT_REFCOUNTING( WswCefV8Handler );
};

#endif
