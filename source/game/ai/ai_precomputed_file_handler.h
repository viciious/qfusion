#ifndef QFUSION_PRECOMPUTED_FILE_HANDLER_H
#define QFUSION_PRECOMPUTED_FILE_HANDLER_H

#include "ai_local.h"

class AiPrecomputedFileHandler {
public:
	typedef void *( *AllocFn )( size_t  );
	typedef void ( *FreeFn )( void * );
protected:
	const char *tag;
	AllocFn allocFn;
	FreeFn freeFn;
	uint8_t *data;

	uint32_t expectedVersion;
	int fp;
	int dataSize;

	bool useAasChecksum;
	bool useMapChecksum;

public:
	AiPrecomputedFileHandler( const char *tag_, uint32_t expectedVersion_,
						  AllocFn allocFn_ = nullptr, FreeFn freeFn_ = nullptr )
		: tag( tag_ ),
		  allocFn( allocFn_ ),
		  freeFn( freeFn_ ),
		  data( nullptr ),
		  expectedVersion( expectedVersion_ ),
		  fp( -1 ),
		  dataSize( 0 ),
		  useAasChecksum( true ),
		  useMapChecksum( true ) {}

	virtual ~AiPrecomputedFileHandler();
};

class AiPrecomputedFileReader: public virtual AiPrecomputedFileHandler {
public:
	enum LoadingStatus {
		MISSING,
		FAILURE,
		VERSION_MISMATCH,
		SUCCESS
	};
private:
	LoadingStatus ExpectFileString( const char *expected, const char *message );
public:
	AiPrecomputedFileReader( const char *tag_, uint32_t expectedVersion_, AllocFn allocFn_ = nullptr, FreeFn freeFn_ = nullptr )
		: AiPrecomputedFileHandler( tag_, expectedVersion_, allocFn_, freeFn_ ) {}

	LoadingStatus BeginReading( const char *filePath );

	bool ReadLengthAndData( uint8_t **data, uint32_t *dataLength );
};

class AiPrecomputedFileWriter: public virtual AiPrecomputedFileHandler {
	char *filePath;
	bool failedOnWrite;
public:
	AiPrecomputedFileWriter( const char *tag_, uint32_t expectedVersion_, AllocFn allocFn_ = nullptr, FreeFn freeFn_ = nullptr )
		: AiPrecomputedFileHandler( tag_, expectedVersion_, allocFn_, freeFn_ ),
		  filePath( nullptr ),
		  failedOnWrite( false ) {}

	~AiPrecomputedFileWriter() override;

	bool BeginWriting( const char *filePath_ );

	bool WriteString( const char *string );
	bool WriteLengthAndData( const uint8_t *data, uint32_t dataLength );
};

#endif
