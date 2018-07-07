#include "Logger.h"

#define IMPLEMENT_LOGGER_METHOD( Name, SEVERITY )                \
void Logger::Name( const char *format, ... ) {                   \
	va_list va;                                                  \
	va_start( va, format );                                      \
	SendLogMessage( SEVERITY, format, va );                      \
	va_end( va );                                                \
}

IMPLEMENT_LOGGER_METHOD( Debug, LOGSEVERITY_VERBOSE )
IMPLEMENT_LOGGER_METHOD( Info, LOGSEVERITY_INFO )
IMPLEMENT_LOGGER_METHOD( Warning, LOGSEVERITY_WARNING )
IMPLEMENT_LOGGER_METHOD( Error, LOGSEVERITY_ERROR )
