#ifndef QFUSION_WSWCEFAPP_H
#define QFUSION_WSWCEFAPP_H

#include "include/cef_app.h"
#include "include/cef_client.h"
#include "UiFacade.h"
#include "CefStringBuilder.h"

#include <unordered_map>
#include <atomic>

inline CefString AsCefString( const char *ascii ) {
	CefString cs;
	if( !cs.FromASCII( ascii ) ) {
		abort();
	}
	return cs;
}

template <typename T>
inline CefRefPtr<T> AsCefPtr( T *value ) {
	return CefRefPtr<T>( value );
}

class WswCefApp: public CefApp {
	CefRefPtr<CefBrowserProcessHandler> browserProcessHandler;
	CefRefPtr<CefRenderProcessHandler> renderProcessHandler;
public:
	WswCefApp( int width = -1, int height = -1 );

	void OnBeforeCommandLineProcessing( const CefString& process_type, CefRefPtr<CefCommandLine> command_line ) override;
	void OnRegisterCustomSchemes( CefRawPtr<CefSchemeRegistrar> registrar) override;

	CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
		return browserProcessHandler;
	}

	CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
		return renderProcessHandler;
	}

	IMPLEMENT_REFCOUNTING( WswCefApp );
};












#endif
