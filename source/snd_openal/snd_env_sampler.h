#ifndef QFUSION_SND_ENV_SAMPLER_H
#define QFUSION_SND_ENV_SAMPLER_H

#include "../gameshared/q_collision.h"

struct src_s;

void ENV_Init();
void ENV_Shutdown();
void ENV_EndRegistration();

void ENV_UpdateListener( const vec3_t origin, const vec3_t velocity, const mat3_t axes );

void ENV_RegisterSource( struct src_s *src );

void ENV_UnregisterSource( struct src_s *src );

#endif
