#ifndef QFUSION_PLANNINGLOCAL_H
#define QFUSION_PLANNINGLOCAL_H

#include "../ai_local.h"

inline float LgRange() {
	return GS_GetWeaponDef( WEAP_LASERGUN )->firedef.timeout;
}

#endif
