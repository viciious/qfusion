#ifndef UI_CEF_LOGGER_H
#define UI_CEF_LOGGER_H

#include "include/cef_browser.h"

class Logger {
protected:
	virtual void SendLogMessage( cef_log_severity_t severity, const char *format, va_list va ) = 0;
public:
	virtual ~Logger() {}

#ifndef _MSC_VER
	void Debug( const char *format, ... ) __attribute__( ( format( printf, 2, 3 ) ) );
	void Info( const char *format, ... ) __attribute__( ( format( printf, 2, 3 ) ) );
	void Warning( const char *format, ... ) __attribute__( ( format( printf, 2, 3 ) ) );
	void Error( const char *format, ... ) __attribute__( ( format( printf, 2, 3 ) ) );
#else
	void Debug( _Printf_format_string_ const char *format, ... );
	void Info( _Printf_format_string_ const char *format, ... );
	void Warning( _Printf_format_string_ const char *format, ... );
	void Error( _Printf_format_string_ const char *format, ... );
#endif
};


#endif
