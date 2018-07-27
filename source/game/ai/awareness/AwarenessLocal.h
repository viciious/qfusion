#ifndef QFUSION_AWARENESSLOCAL_H
#define QFUSION_AWARENESSLOCAL_H

#include "EntitiesPvsCache.h"
#include "../static_vector.h"

static inline bool IsGenericProjectileVisible( const edict_t *self, const edict_t *ent ) {
	trace_t trace;
	auto *self_ = const_cast<edict_t *>( self );
	auto *ent_ = const_cast<edict_t *>( ent );
	G_Trace( &trace, self_->s.origin, nullptr, nullptr, ent_->s.origin, self_, MASK_OPAQUE );
	return trace.fraction == 1.0f || trace.ent == ENTNUM( ent );
}

// Try testing both origins and a mid point. Its very coarse but should produce satisfiable results in-game.
static inline bool IsLaserBeamVisible( const edict_t *self, const edict_t *ent ) {
	trace_t trace;
	auto *self_ = const_cast<edict_t *>( self );
	auto *ent_ = const_cast<edict_t *>( ent );
	G_Trace( &trace, self_->s.origin, nullptr, nullptr, ent_->s.origin, self_, MASK_OPAQUE );
	if( trace.fraction == 1.0f || trace.ent == ENTNUM( ent ) ) {
		return true;
	}

	G_Trace( &trace, self_->s.origin, nullptr, nullptr, ent_->s.origin2, self_, MASK_OPAQUE );
	return trace.fraction == 1.0f || trace.ent == ENTNUM( ent );
}

static inline bool IsGenericEntityInPvs( const edict_t *self, const edict_t *ent ) {
	return EntitiesPvsCache::Instance()->AreInPvs( self, ent );
}

static inline bool IsLaserBeamInPvs( const edict_t *self, const edict_t *ent ) {
	return EntitiesPvsCache::Instance()->AreInPvs( self, ent );
}

struct EntAndDistance {
	int entNum;
	float distance;

	EntAndDistance( int entNum_, float distance_ ) : entNum( entNum_ ), distance( distance_ ) {}
	bool operator<( const EntAndDistance &that ) const { return distance < that.distance; }
};

static const unsigned MAX_NONCLIENT_ENTITIES = MAX_EDICTS - MAX_CLIENTS;
typedef StaticVector<uint16_t, MAX_NONCLIENT_ENTITIES> EntNumsVector;
typedef StaticVector<EntAndDistance, MAX_NONCLIENT_ENTITIES> EntsAndDistancesVector;

template<unsigned N, unsigned M, typename PvsFunc, typename VisFunc>
bool VisCheckRawEnts( StaticVector<EntAndDistance, N> &rawEnts,
					  StaticVector<uint16_t, M> &filteredEnts,
					  const edict_t *self, unsigned visEntsLimit,
					  PvsFunc pvsFunc, VisFunc visFunc ) {
	filteredEnts.clear();

	// Do not call inPVS() and G_Visible() inside a single loop for all raw ents.
	// Sort all entities by distance to the bot.
	// Then select not more than visEntsLimit nearest entities in PVS, then call visFunc().
	// It may cause data loss (far entities that may have higher logical priority),
	// but in a common good case (when there are few visible entities) it preserves data,
	// and in the worst case mentioned above it does not act weird from player POV and prevents server hang up.

	std::sort( rawEnts.begin(), rawEnts.end() );

	const edict_t *const gameEdicts = game.edicts;

	StaticVector<uint16_t, M> entsInPvs;
	bool result = true;
	unsigned limit = rawEnts.size();
	if( limit > entsInPvs.capacity() ) {
		limit = entsInPvs.capacity();
		result = false;
	}
	if( limit > visEntsLimit ) {
		limit = visEntsLimit;
		result = false;
	}

	for( unsigned i = 0; i < limit; ++i ) {
		auto entNum = (uint16_t)rawEnts[i].entNum;
		if( pvsFunc( self, gameEdicts + entNum ) ) {
			entsInPvs.push_back( entNum );
		}
	}

	for( auto entNum: entsInPvs ) {
		const edict_t *ent = gameEdicts + entNum;
		if( visFunc( self, ent ) ) {
			filteredEnts.push_back( entNum );
		}
	}

	return result;
}

#endif
