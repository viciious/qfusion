#include "HazardsDetector.h"
#include "EntitiesPvsCache.h"



void HazardsDetector::Clear() {
	maybeDangerousRockets.clear();
	dangerousRockets.clear();
	maybeVisibleOtherWaves.clear();
	dangerousWaves.clear();
	maybeDangerousPlasmas.clear();
	dangerousPlasmas.clear();
	maybeDangerousBlasts.clear();
	dangerousBlasts.clear();
	maybeDangerousGrenades.clear();
	dangerousGrenades.clear();
	maybeDangerousLasers.clear();
	dangerousLasers.clear();

	maybeVisibleOtherRockets.clear();
	visibleOtherRockets.clear();
	maybeVisibleOtherWaves.clear();
	visibleOtherWaves.clear();
	maybeVisibleOtherPlasmas.clear();
	visibleOtherPlasmas.clear();
	maybeVisibleOtherBlasts.clear();
	visibleOtherBlasts.clear();
	maybeVisibleOtherGrenades.clear();
	visibleOtherGrenades.clear();
	maybeVisibleOtherLasers.clear();
	visibleOtherLasers.clear();
}

void HazardsDetector::Exec() {
	Clear();

	// Note that we always skip own rockets, plasma, etc.
	// Otherwise all own bot shot events yield a hazard.
	// There are some cases when an own rocket can hurt but they are either extremely rare or handled by bot fire code.
	// Own grenades are the only exception. We check grenade think time to skip grenades just fired by bot.
	// If a grenade is about to explode and is close to bot, its likely it has bounced of the world and can hurt.

	const edict_t *gameEdicts = game.edicts;
	for( int i = gs.maxclients + 1, end = game.numentities; i < end; ++i ) {
		const edict_t *ent = gameEdicts + i;
		switch( ent->s.type ) {
			case ET_ROCKET:
				TryAddEntity( ent, DETECT_ROCKET_SQ_RADIUS, maybeDangerousRockets, maybeVisibleOtherRockets );
				break;
			case ET_WAVE:
				TryAddEntity( ent, DETECT_WAVE_SQ_RADIUS, maybeDangerousWaves, maybeVisibleOtherWaves );
				break;
			case ET_PLASMA:
				TryAddEntity( ent, DETECT_PLASMA_SQ_RADIUS, maybeDangerousPlasmas, maybeVisibleOtherPlasmas );
				break;
			case ET_BLASTER:
				TryAddEntity( ent, DETECT_GB_BLAST_SQ_RADIUS, maybeDangerousBlasts, maybeVisibleOtherBlasts );
				break;
			case ET_GRENADE:
				TryAddGrenade( ent, maybeDangerousGrenades, maybeVisibleOtherGrenades );
				break;
			case ET_LASERBEAM:
				TryAddEntity( ent, DETECT_LG_BEAM_SQ_RADIUS, maybeDangerousLasers, maybeVisibleOtherLasers );
				break;
			default:
				break;
		}
	}

	constexpr auto isGenInPvs = IsGenericEntityInPvs;
	constexpr auto isLaserInPvs = IsLaserBeamInPvs;
	constexpr auto isGenVisible = IsGenericProjectileVisible;
	constexpr auto isLaserVisible = IsLaserBeamVisible;

	// If all potentially dangerous entities have been processed successfully
	// (no entity has been rejected due to limit/capacity overflow)
	// filter other visible entities of the same kind.

	if( VisCheckRawEnts( maybeDangerousRockets, dangerousRockets, self, 12, isGenInPvs, isGenVisible ) ) {
		VisCheckRawEnts( maybeVisibleOtherRockets, visibleOtherRockets, self, 6, isGenInPvs, isGenVisible );
	}
	if( VisCheckRawEnts( maybeDangerousWaves, dangerousWaves, self, 12, isGenInPvs, isGenVisible ) ) {
		VisCheckRawEnts( maybeVisibleOtherWaves, visibleOtherWaves, self, 6, isGenInPvs, isGenVisible );
	}
	if( VisCheckRawEnts( maybeDangerousPlasmas, dangerousPlasmas, self, 48, isGenInPvs, isGenVisible ) ) {
		VisCheckRawEnts( maybeVisibleOtherPlasmas, visibleOtherPlasmas, self, 12, isGenInPvs, isGenVisible );
	}
	if( VisCheckRawEnts( maybeDangerousBlasts, dangerousBlasts, self, 6, isGenInPvs, isGenVisible ) ) {
		VisCheckRawEnts( maybeVisibleOtherBlasts, visibleOtherBlasts, self, 3, isGenInPvs, isGenVisible );
	}
	if( VisCheckRawEnts( maybeDangerousGrenades, dangerousGrenades, self, 6, isGenInPvs, isGenVisible ) ) {
		VisCheckRawEnts( maybeVisibleOtherGrenades, visibleOtherGrenades, self, 3, isGenInPvs, isGenVisible );
	}
	if( VisCheckRawEnts( maybeDangerousLasers, dangerousLasers, self, 4, isLaserInPvs, isLaserVisible ) ) {
		VisCheckRawEnts( maybeVisibleOtherLasers, visibleOtherLasers, self, 4, isLaserInPvs, isLaserVisible );
	}
}

inline void HazardsDetector::TryAddEntity( const edict_t *ent,
										   float squareDistanceThreshold,
										   EntsAndDistancesVector &dangerousEntities,
										   EntsAndDistancesVector &otherEntities ) {
	assert( ent->s.type != ET_GRENADE );

	if( ent->s.ownerNum == ENTNUM( self ) ) {
		return;
	}

	if( GS_TeamBasedGametype() && self->s.team == ent->s.team ) {
		if( !g_allow_teamdamage->integer ) {
			return;
		}
	}

	float squareDistance = DistanceSquared( self->s.origin, ent->s.origin );
	if( squareDistance < squareDistanceThreshold ) {
		dangerousEntities.emplace_back( EntAndDistance( ENTNUM( ent ), sqrtf( squareDistance ) ) );
	} else {
		otherEntities.emplace_back( EntAndDistance( ENTNUM( ent ), sqrtf( squareDistance ) ) );
	}
}

inline void HazardsDetector::TryAddGrenade( const edict_t *ent,
											EntsAndDistancesVector &dangerousEntities,
											EntsAndDistancesVector &otherEntities ) {
	assert( ent->s.type == ET_GRENADE );

	if( ent->s.ownerNum == ENTNUM( self ) ) {
		if( !g_allow_selfdamage->integer ) {
			return;
		}
		const auto timeout = GS_GetWeaponDef( WEAP_GRENADELAUNCHER )->firedef.timeout;
		// Ignore own grenades in first 500 millis
		if( level.time - ent->nextThink > timeout - 500 ) {
			return;
		}
	} else {
		if( GS_TeamBasedGametype() && ent->s.team == self->s.team ) {
			if( !g_allow_teamdamage->integer ) {
				return;
			}
		}
	}

	float squareDistance = DistanceSquared( self->s.origin, ent->s.origin );
	if( squareDistance < 300 * 300 ) {
		dangerousEntities.emplace_back( EntAndDistance( ENTNUM( ent ), sqrtf( squareDistance ) ) );
	} else {
		otherEntities.emplace_back( EntAndDistance( ENTNUM( ent ), sqrtf( squareDistance ) ) );
	}
}