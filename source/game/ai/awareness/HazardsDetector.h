#ifndef QFUSION_HAZARDSDETECTOR_H
#define QFUSION_HAZARDSDETECTOR_H

#include "../ai_local.h"
#include "../static_vector.h"

#include "AwarenessLocal.h"

class HazardsDetector {
	friend class BotAwarenessModule;
	friend class HazardsSelector;

	static constexpr float DETECT_ROCKET_SQ_RADIUS = 650 * 650;
	static constexpr float DETECT_WAVE_RADIUS = 500;
	static constexpr float DETECT_WAVE_SQ_RADIUS = DETECT_WAVE_RADIUS * DETECT_WAVE_RADIUS;
	static constexpr float DETECT_PLASMA_SQ_RADIUS = 650 * 650;
	static constexpr float DETECT_GB_BLAST_SQ_RADIUS = 700 * 700;
	static constexpr float DETECT_GRENADE_SQ_RADIUS = 450 * 450;
	static constexpr float DETECT_LG_BEAM_SQ_RADIUS = 1000 * 1000;

	// There is a way to compute it in compile-time but it looks ugly
	static constexpr float MAX_RADIUS = 1000.0f;
	static_assert( MAX_RADIUS * MAX_RADIUS >= DETECT_ROCKET_SQ_RADIUS, "" );
	static_assert( MAX_RADIUS * MAX_RADIUS >= DETECT_WAVE_SQ_RADIUS, "" );
	static_assert( MAX_RADIUS * MAX_RADIUS >= DETECT_PLASMA_SQ_RADIUS, "" );
	static_assert( MAX_RADIUS * MAX_RADIUS >= DETECT_GB_BLAST_SQ_RADIUS, "" );
	static_assert( MAX_RADIUS * MAX_RADIUS >= DETECT_GRENADE_SQ_RADIUS, "" );
	static_assert( MAX_RADIUS * MAX_RADIUS >= DETECT_LG_BEAM_SQ_RADIUS, "" );

	void Clear();

	static const auto MAX_NONCLIENT_ENTITIES = MAX_EDICTS - MAX_CLIENTS;
	typedef StaticVector<EntAndDistance, MAX_NONCLIENT_ENTITIES> EntsAndDistancesVector;
	typedef StaticVector<uint16_t, MAX_NONCLIENT_ENTITIES> EntNumsVector;

	inline void TryAddEntity( const edict_t *ent,
							  float squareDistanceThreshold,
							  EntsAndDistancesVector &dangerousEntities,
							  EntsAndDistancesVector &otherEntities );
	inline void TryAddGrenade( const edict_t *ent,
							   EntsAndDistancesVector &dangerousEntities,
							   EntsAndDistancesVector &otherEntities );

	const edict_t *const self;

	EntsAndDistancesVector maybeDangerousRockets;
	EntNumsVector dangerousRockets;
	EntsAndDistancesVector maybeDangerousWaves;
	EntNumsVector dangerousWaves;
	EntsAndDistancesVector maybeDangerousPlasmas;
	EntNumsVector dangerousPlasmas;
	EntsAndDistancesVector maybeDangerousBlasts;
	EntNumsVector dangerousBlasts;
	EntsAndDistancesVector maybeDangerousGrenades;
	EntNumsVector dangerousGrenades;
	EntsAndDistancesVector maybeDangerousLasers;
	EntNumsVector dangerousLasers;

	EntsAndDistancesVector maybeVisibleOtherRockets;
	EntNumsVector visibleOtherRockets;
	EntsAndDistancesVector maybeVisibleOtherWaves;
	EntNumsVector visibleOtherWaves;
	EntsAndDistancesVector maybeVisibleOtherPlasmas;
	EntNumsVector visibleOtherPlasmas;
	EntsAndDistancesVector maybeVisibleOtherBlasts;
	EntNumsVector visibleOtherBlasts;
	EntsAndDistancesVector maybeVisibleOtherGrenades;
	EntNumsVector visibleOtherGrenades;
	EntsAndDistancesVector maybeVisibleOtherLasers;
	EntNumsVector visibleOtherLasers;

	explicit HazardsDetector( const edict_t *self_ ) : self( self_ ) {}

	void Exec();
};

#endif
