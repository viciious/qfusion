#include "Api.h"

#include "CefApp.h"

#include "include/cef_base.h"
#include "include/wrapper/cef_helpers.h"

int main( int argc, char **argv ) {
	CefMainArgs args( argc, argv );
	CefRefPtr<CefApp> app( new WswCefApp );
	return CefExecuteProcess( args, app.get(), nullptr );
}
