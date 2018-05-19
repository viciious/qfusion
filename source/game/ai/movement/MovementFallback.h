#ifndef QFUSION_MOVEMENTFALLBACK_H
#define QFUSION_MOVEMENTFALLBACK_H

#include "../ai_local.h"

class MovementPredictionContext;

class MovementFallback
{
public:
	enum Status {
		COMPLETED,
		PENDING,
		INVALID
	};

protected:
	const edict_t *const self;
	int64_t activatedAt;
	Status status;
	int debugColor;

	void Activate() {
		activatedAt = level.time;
		status = PENDING;
	}

	// A convenient shorthand for returning from TryDeactivate()
	bool DeactivateWithStatus( Status status_ ) {
		assert( status_ != PENDING );
		this->status = status_;
		return true;
	}
public:
	MovementFallback( const edict_t *self_, int debugColor_ )
		: self( self_ ), activatedAt( 0 ), status( COMPLETED ), debugColor( debugColor_ ) {}

	bool IsActive() const { return status == PENDING; }

	int DebugColor()  const { return debugColor; }

	virtual ~MovementFallback() {}

	virtual bool TryDeactivate( MovementPredictionContext *context = nullptr ) = 0;

	virtual void SetupMovement( MovementPredictionContext *context ) = 0;
};


#endif
