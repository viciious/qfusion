#include "ai_precomputed_file_handler.h"

AiPrecomputedFileHandler::~AiPrecomputedFileHandler() {
	if( data ) {
		// We could use proxy functions that wrap default calls,
		// but it is useful to defer macro expansion to this call site to get an actual memory leak trace.
		if( freeFn ) {
			freeFn( data );
		} else {
			G_LevelFree( data );
		}
	}
	if( fp >= 0 ) {
		trap_FS_FCloseFile( fp );
	}
}

bool AiPrecomputedFileWriter::WriteString( const char *string ) {
	uint32_t length = (uint32_t)strlen( string ) + 1;
	return WriteLengthAndData( (const uint8_t *)string, length );
}

bool AiPrecomputedFileWriter::WriteLengthAndData( const uint8_t *data, uint32_t dataLength ) {
	if( trap_FS_Write( &dataLength, 4, fp ) <= 0 ) {
		failedOnWrite = true;
		return false;
	}

	if( trap_FS_Write( data, dataLength, fp ) <= 0 ) {
		failedOnWrite = true;
		return false;
	}

	return true;
}

bool AiPrecomputedFileReader::ReadLengthAndData( uint8_t **data, uint32_t *dataLength ) {
	uint32_t length;
	if( trap_FS_Read( &length, 4, fp ) <= 0 ) {
		G_Printf( S_COLOR_RED "%s: Can't read a chunk length\n", tag );
		return false;
	}

	length = LittleLong( length );
	uint8_t *mem;
	if( allocFn ) {
		mem = (uint8_t *)allocFn( length );
	} else {
		mem = (uint8_t *)G_LevelMalloc( length );
	}

	if( !mem ) {
		G_Printf( S_COLOR_RED "%s: Can't allocate %d bytes for the chunk\n", tag, (int)length );
		return false;
	}

	if( trap_FS_Read( mem, length, fp ) != length ) {
		G_Printf( S_COLOR_RED "%s: Can't read %d chunk bytes\n", tag, (int)length );
		if( freeFn ) {
			freeFn( mem );
		} else {
			G_LevelFree( mem );
		}
		return false;
	}

	*data = mem;
	*dataLength = length;
	return true;
}

AiPrecomputedFileReader::LoadingStatus AiPrecomputedFileReader::ExpectFileString( const char *expected,
																				  const char *message ) {
	uint32_t dataLength;
	uint8_t *data;

	if( !ReadLengthAndData( &data, &dataLength ) ) {
		return FAILURE;
	}

	if( !dataLength ) {
		return ( expected[0] == 0 ) ? SUCCESS : VERSION_MISMATCH;
	}

	data[dataLength - 1] = 0;

	LoadingStatus result = SUCCESS;
	if( Q_stricmp( expected, ( const char *)data ) ) {
		G_Printf( "%s: actual string is `%s`, expected string is `%s`", message, data, expected );
		result = VERSION_MISMATCH;
	}

	if( freeFn ) {
		freeFn( data );
	} else {
		G_LevelFree( data );
	}

	return result;
}

AiPrecomputedFileReader::LoadingStatus AiPrecomputedFileReader::BeginReading( const char *filePath ) {
	if( trap_FS_FOpenFile( filePath, &fp, FS_READ ) < 0 ) {
		G_Printf( S_COLOR_YELLOW "%s: Can't open file `%s` for reading\n", tag, filePath );
		return MISSING;
	}

	uint32_t version;
	if( trap_FS_Read( &version, 4, fp ) != 4 ) {
		G_Printf( S_COLOR_YELLOW "%s: Can't read the format version from the file\n", tag );
		return FAILURE;
	}

	version = LittleLong( version );
	if( version != expectedVersion ) {
		G_Printf( S_COLOR_YELLOW "%s: Expected and actual file format versions differ\n", tag );
		return VERSION_MISMATCH;
	}

	LoadingStatus status;
	if( useAasChecksum ) {
		const auto *aasWorld = AiAasWorld::Instance();
		if( !aasWorld->IsLoaded() ) {
			G_Printf( S_COLOR_RED "%s: Can't get checksum for non-loaded AAS world\n", tag );
			return FAILURE;
		}
		if( ( status = ExpectFileString( aasWorld->Checksum(), "AAS checksum mismatch" ) ) != SUCCESS ) {
			return status;
		}
	}

	if( useMapChecksum ) {
		const char *mapChecksum = trap_GetConfigString( CS_MAPCHECKSUM );
		if( ( status = ExpectFileString( mapChecksum, "Map checksum mismatch" ) ) != SUCCESS ) {
			return status;
		}
	}

	return SUCCESS;
}

AiPrecomputedFileWriter::~AiPrecomputedFileWriter() {
	if( fp < 0 || !failedOnWrite ) {
		return;
	}

	// Avoid handling the file in the parent destructor
	// in case when we have to remove the file
	// (close the handle first, then remove the file)

	trap_FS_FCloseFile( fp );
	if( filePath ) {
		trap_FS_RemoveFile( filePath );
		if( freeFn ) {
			freeFn( filePath );
		} else {
			G_LevelFree( filePath );
		}
	}

	fp = -1;
}

bool AiPrecomputedFileWriter::BeginWriting( const char *filePath_ ) {
	// Try open file for writing
	if( trap_FS_FOpenFile( filePath_, &fp, FS_WRITE ) < 0 ) {
		G_Printf( S_COLOR_RED "%s: Can't open file %s for writing\n", tag, filePath_ );
		return false;
	}

	// Make a copy of the file path to be able to remove it by path in case of failure
	size_t pathLen = strlen( filePath_ );
	if( allocFn ) {
		this->filePath = (char *)allocFn( pathLen + 1 );
	} else {
		this->filePath = (char *)G_LevelMalloc( pathLen + 1 );
	}
	if( !this->filePath ) {
		G_Printf( S_COLOR_RED "%s: Can't allocate a buffer for storing a file path copy\n", tag );
		return false;
	}
	memcpy( this->filePath, filePath_, pathLen + 1 );

	uint32_t version = LittleLong( expectedVersion );
	if( !trap_FS_Write( &version, 4, fp ) ) {
		G_Printf( S_COLOR_RED "%s: Can't write version to file\n", tag );
		return false;
	}

	if( useAasChecksum ) {
		const auto *aasWorld = AiAasWorld::Instance();
		if( !aasWorld->IsLoaded() ) {
			G_Printf( S_COLOR_RED "%s: Can't get checksum for non-loaded AAS world\n", tag );
			return false;
		}
		if( !WriteString( aasWorld->Checksum() ) ) {
			G_Printf( S_COLOR_RED "%s: Can't write AAS checksum to file\n", tag );
			return false;
		}
	}

	if( useMapChecksum ) {
		if( !WriteString( trap_GetConfigString( CS_MAPCHECKSUM ) ) ) {
			G_Printf( S_COLOR_RED "%s: Can't write map checksum to file\n", tag );
			return false;
		}
	}

	return true;
}
