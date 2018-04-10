#ifndef QFUSION_CM_TRACE_H
#define QFUSION_CM_TRACE_H

#include "qcommon.h"

struct trace_s;
struct cmodel_state_s;
struct cbrush_s;
struct cface_s;
struct cmodel_s;

// 1/32 epsilon to keep floating point happy
#define DIST_EPSILON    ( 1.0f / 32.0f )
#define RADIUS_EPSILON      1.0f

struct CMTraceContext {
	trace_t *trace;

	vec3_t start, end;
	vec3_t mins, maxs;
	vec3_t startmins, endmins;
	vec3_t startmaxs, endmaxs;
	vec3_t absmins, absmaxs;
	vec3_t extents;
	vec3_t traceDir;
	float boxRadius;

#ifdef CM_USE_SSE
	__m128 xmmAbsmins, xmmAbsmaxs;
	// TODO: Add also xmm copies of trace dir/start once line distance test is vectorized
	__m128 xmmClipBoxLookup[16];
#endif

	int contents;
	bool ispoint;      // optimized case
};

struct CMTraceComputer {
	struct cmodel_state_s *cms;

	CMTraceComputer(): cms( nullptr ) {}

	virtual void SetupCollideContext( CMTraceContext *tlc, trace_t *tr, const vec_t *start, const vec_t *end,
									  const vec_t *mins, const vec_t *maxs, int brushmask );

	virtual void SetupClipContext( CMTraceContext *tlc ) {}

	virtual void CollideBox( CMTraceContext *tlc, void ( CMTraceComputer::*method )( CMTraceContext *, cbrush_s * ),
							 cbrush_s *brushes, int numbrushes, cface_s *markfaces, int nummarkfaces );


	virtual void ClipBoxToLeaf( CMTraceContext *tlc, cbrush_s *brushes,
								int numbrushes, cface_s *markfaces, int nummarkfaces );

	// Lets avoid making these calls virtual, there is a small but definite performance penalty
	// (something around 5-10%s, and this really matter as all newly introduced engine features rely on fast CM raycasting).
	// They still can be "overridden" for specialized implementations
	// just by using explicitly qualified method with the same signature.
	void TestBoxInBrush( CMTraceContext *tlc, cbrush_s *brush );
	void ClipBoxToBrush( CMTraceContext *tlc, cbrush_s *brush );

	void RecursiveHullCheck( CMTraceContext *tlc, int num, float p1f, float p2f, vec3_t p1, vec3_t p2 );

	void Trace( trace_t *tr, const vec3_t start, const vec3_t end, const vec3_t mins,
				const vec3_t maxs, cmodel_s *cmodel, int brushmask );
};

struct CMGenericTraceComputer final: public CMTraceComputer {};

struct CMSse42TraceComputer final: public CMTraceComputer {
	// Don't even bother about making prototypes if there is no attempt to compile SSE code
	// (this should aid calls devirtualization)
#ifdef CM_USE_SSE
	void SetupCollideContext( CMTraceContext *tlc, trace_t *tr, const vec_t *start, const vec_t *end,
							  const vec_t *mins, const vec_t *maxs, int brushmask ) override;

	void SetupClipContext( CMTraceContext *tlc ) override;

	void ClipBoxToLeaf( CMTraceContext *tlc, cbrush_s *brushes, int numbrushes,
						cface_s *markfaces, int nummarkfaces ) override;

	// Overrides a base member by hiding it
	void ClipBoxToBrush( CMTraceContext *tlc, cbrush_s *brush );
#endif
};

#endif //QFUSION_CM_TRACE_H
