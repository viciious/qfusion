#include "SyscallsLocal.h"

void DirectoryWalker::Exec( const char *dir_ ) {
	int totalFiles = api->FS_GetFileList( dir_, extension, nullptr, 0, 0, 0 );
	for( int startAtFile = 0; startAtFile < totalFiles; ) {
		int numFiles = api->FS_GetFileList( dir_, extension, buffer, sizeof( buffer ), startAtFile, totalFiles );
		if( !numFiles ) {
			// Go to next start file on failure
			startAtFile++;
			continue;
		}
		ParseBuffer();
		startAtFile += numFiles;
	}
}

size_t DirectoryWalker::ScanFilename( const char *p, const char **lastDot ) {
	const char *const oldp = p;
	// Scan for the zero byte, marking the first dot as well
	for(;; ) {
		char ch = *p;
		if( !ch ) {
			break;
		}
		if( ch == '.' ) {
			*lastDot = p;
		}
		p++;
	}
	return (size_t)( p - oldp );
}

void DirectoryWalker::ParseBuffer() {
	size_t len = 0;
	// Hop over the last zero byte on every iteration
	for( const char *p = buffer; p - buffer < sizeof( buffer ); p += len + 1 ) {
		const char *lastDot = nullptr;
		len = ScanFilename( p, &lastDot );
		if( !len ) {
			break;
		}
		// Skip hidden files and directory links
		if( *p == '.' ) {
			continue;
		}
		ConsumeEntry( p, len, lastDot );
	}
}