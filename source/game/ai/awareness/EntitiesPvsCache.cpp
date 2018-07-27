#include "EntitiesPvsCache.h"

EntitiesPvsCache EntitiesPvsCache::instance;

bool EntitiesPvsCache::AreInPvs( const edict_t *ent1, const edict_t *ent2 ) const {
	// Prevent undefined behaviour of signed shifts
	const auto entNum1 = (unsigned)ENTNUM( ent1 );
	const auto entNum2 = (unsigned)ENTNUM( ent2 );

	uint32_t *ent1Vis = visStrings[entNum1];
	// An offset of an array cell containing entity bits.
	unsigned ent2ArrayOffset = ( entNum2 * 2 ) / 32;
	// An offset of entity bits inside a 32-bit array cell
	unsigned ent2BitsOffset = ( entNum2 * 2 ) % 32;

	unsigned ent2Bits = ( ent1Vis[ent2ArrayOffset] >> ent2BitsOffset ) & 0x3;
	if( ent2Bits != 0 ) {
		// If 2, return true, if 1, return false. Masking with & 1 should help a compiler to avoid branches here
		return (bool)( ( ent2Bits - 1 ) & 1 );
	}

	bool result = AreInPvsUncached( ent1, ent2 );

	// We assume the PVS relation is symmetrical, so set the result in strings for every entity
	uint32_t *ent2Vis = visStrings[entNum2];
	unsigned ent1ArrayOffset = ( entNum1 * 2 ) / 32;
	unsigned ent1BitsOffset = ( entNum1 * 2 ) % 32;

	// Convert boolean result to a non-zero integer
	unsigned ent1Bits = ent2Bits = (unsigned)result + 1;
	assert( ent1Bits == 1 || ent1Bits == 2 );
	// Convert entity bits (1 or 2) into a mask
	ent1Bits <<= ent1BitsOffset;
	ent2Bits <<= ent2BitsOffset;

	// Clear old bits in array cells
	ent1Vis[ent2ArrayOffset] &= ~ent2Bits;
	ent2Vis[ent1ArrayOffset] &= ~ent1Bits;
	// Set new bits in array cells
	ent1Vis[ent2ArrayOffset] |= ent2Bits;
	ent2Vis[ent1ArrayOffset] |= ent1Bits;

	return result;
}

bool EntitiesPvsCache::AreInPvsUncached( const edict_t *ent1, const edict_t *ent2 ) {
	const int numClusters1 = ent1->r.num_clusters;
	if( numClusters1 < 0 ) {
		return trap_inPVS( ent1->s.origin, ent2->s.origin );
	}
	const int numClusters2 = ent2->r.num_clusters;
	if( numClusters2 < 0 ) {
		return trap_inPVS( ent1->s.origin, ent2->s.origin );
	}

	const int *leafNums1 = ent1->r.leafnums;
	const int *leafNums2 = ent2->r.leafnums;
	for( int i = 0; i < numClusters1; ++i ) {
		for( int j = 0; j < numClusters2; ++j ) {
			if( trap_CM_LeafsInPVS( leafNums1[i], leafNums2[j] ) ) {
				return true;
			}
		}
	}

	return false;
}