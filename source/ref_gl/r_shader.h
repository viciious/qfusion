/*
Copyright (C) 2013 Victor Luchits

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
#ifndef R_SHADER_H
#define R_SHADER_H

#define MAX_SHADERS                 2048
#define MAX_SHADER_PASSES           8
#define MAX_SHADER_DEFORMVS         8
#define MAX_SHADER_IMAGES           16
#define MAX_SHADER_TCMODS           8

// shader types (by superlightstyle)
typedef enum {
	SHADER_TYPE_DELUXEMAP       = 0,
	SHADER_TYPE_VERTEX          = 1,
	SHADER_TYPE_BSP_MIN         = SHADER_TYPE_DELUXEMAP,
	SHADER_TYPE_BSP_MAX         = SHADER_TYPE_VERTEX,
	SHADER_TYPE_DIFFUSE         = 2,
	SHADER_TYPE_2D              = 3,
	SHADER_TYPE_2D_RAW          = 4,
	SHADER_TYPE_CORONA          = 5,
	SHADER_TYPE_OPAQUE_ENV      = 6,
	SHADER_TYPE_VIDEO           = 7,
	SHADER_TYPE_SKYBOX          = 8,
	SHADER_TYPE_FOG             = 9,
	SHADER_TYPE_2D_LINEAR       = 10,
} shaderType_e;

#define NUM_SHADER_TYPES_BSP ( SHADER_TYPE_BSP_MAX - SHADER_TYPE_BSP_MIN + 1 )

// shader flags
enum {
	SHADER_DEPTHWRITE               = 1 << 0,
	SHADER_SKY                      = 1 << 1,
	SHADER_CULL_FRONT               = 1 << 2,
	SHADER_CULL_BACK                = 1 << 3,
	SHADER_POLYGONOFFSET            = 1 << 4,
	SHADER_ENTITY_MERGABLE          = 1 << 5,
	SHADER_AUTOSPRITE               = 1 << 6,
	SHADER_LIGHTMAP                 = 1 << 7,
	SHADER_PORTAL                   = 1 << 8,
	SHADER_PORTAL_CAPTURE           = 1 << 9,
	SHADER_PORTAL_CAPTURE2          = 1 << 10,
	SHADER_NO_TEX_FILTERING         = 1 << 11,
	SHADER_ALLDETAIL                = 1 << 12,
	SHADER_NODRAWFLAT               = 1 << 13,
	SHADER_SOFT_PARTICLE            = 1 << 14,
	SHADER_FORCE_OUTLINE_WORLD      = 1 << 15,
	SHADER_STENCILTEST              = 1 << 16
};

// sorting
enum {
	SHADER_SORT_NONE                = 0,
	SHADER_SORT_PORTAL              = 1,
	SHADER_SORT_SKY                 = 2,
	SHADER_SORT_OPAQUE              = 3,
	SHADER_SORT_DECAL               = 4,
	SHADER_SORT_ALPHATEST           = 5,
	SHADER_SORT_BANNER              = 6,
	SHADER_SORT_FOG                 = 7,
	SHADER_SORT_UNDERWATER          = 8,
	SHADER_SORT_ADDITIVE            = 9,
	SHADER_SORT_NEAREST             = 14,
	SHADER_SORT_WEAPON              = 15, // optional phase: depth write but no color write
	SHADER_SORT_WEAPON2             = 16,
};

// shaderpass flags
#define SHADERPASS_MARK_BEGIN       0x20000 // same as GLSTATE_MARK_END
enum {
	SHADERPASS_LIGHTMAP             = SHADERPASS_MARK_BEGIN,
	SHADERPASS_DETAIL               = SHADERPASS_MARK_BEGIN << 1,
	SHADERPASS_PORTALMAP            = SHADERPASS_MARK_BEGIN << 2,
	SHADERPASS_GREYSCALE            = SHADERPASS_MARK_BEGIN << 3,
	SHADERPASS_SKYBOXSIDE           = SHADERPASS_MARK_BEGIN << 4,

	SHADERPASS_AFUNC_GT0            = SHADERPASS_MARK_BEGIN << 5,
	SHADERPASS_AFUNC_LT128          = SHADERPASS_MARK_BEGIN << 6,
	SHADERPASS_AFUNC_GE128          = SHADERPASS_AFUNC_GT0 | SHADERPASS_AFUNC_LT128,
	SHADERPASS_NOSRGB               = SHADERPASS_MARK_BEGIN << 7,
};

#define SHADERPASS_ALPHAFUNC ( SHADERPASS_AFUNC_GT0 | SHADERPASS_AFUNC_LT128 | SHADERPASS_AFUNC_GE128 )

// transform functions
enum {
	SHADER_FUNC_NONE,
	SHADER_FUNC_SIN,
	SHADER_FUNC_TRIANGLE,
	SHADER_FUNC_SQUARE,
	SHADER_FUNC_SAWTOOTH,
	SHADER_FUNC_INVERSESAWTOOTH,
	SHADER_FUNC_NOISE,
	SHADER_FUNC_CONSTANT,
	SHADER_FUNC_RAMP,

	MAX_SHADER_FUNCS
};

// RGB colors generation
enum {
	RGB_GEN_UNKNOWN,
	RGB_GEN_IDENTITY,
	RGB_GEN_CONST,
	RGB_GEN_WAVE,
	RGB_GEN_ENTITYWAVE,
	RGB_GEN_ONE_MINUS_ENTITY,
	RGB_GEN_VERTEX,
	RGB_GEN_ONE_MINUS_VERTEX,
	RGB_GEN_LIGHTING_DIFFUSE,
	RGB_GEN_EXACT_VERTEX,
	RGB_GEN_FOG,
	RGB_GEN_CUSTOMWAVE,
	RGB_GEN_OUTLINE,
	RGB_GEN_ENVIRONMENT
};

// alpha channel generation
enum {
	ALPHA_GEN_UNKNOWN,
	ALPHA_GEN_IDENTITY,
	ALPHA_GEN_CONST,
	ALPHA_GEN_VERTEX,
	ALPHA_GEN_ONE_MINUS_VERTEX,
	ALPHA_GEN_ENTITY,
	ALPHA_GEN_WAVE,
	ALPHA_GEN_OUTLINE
};

// texture coordinates generation
enum {
	TC_GEN_NONE,
	TC_GEN_BASE,
	TC_GEN_LIGHTMAP,
	TC_GEN_ENVIRONMENT,
	TC_GEN_VECTOR,
	TC_GEN_REFLECTION,
	TC_GEN_FOG,
	TC_GEN_REFLECTION_CELSHADE,
	TC_GEN_SVECTORS,
	TC_GEN_PROJECTION,
	TC_GEN_SURROUND
};

// tcmod functions
enum {
	TC_MOD_NONE,
	TC_MOD_SCALE,
	TC_MOD_SCROLL,
	TC_MOD_ROTATE,
	TC_MOD_TRANSFORM,
	TC_MOD_TURB,
	TC_MOD_STRETCH
};

// vertices deformation
enum {
	DEFORMV_NONE,
	DEFORMV_WAVE,
	DEFORMV_BULGE,
	DEFORMV_MOVE,
	DEFORMV_AUTOSPRITE,
	DEFORMV_AUTOSPRITE2,
	DEFORMV_AUTOPARTICLE,
	DEFORMV_OUTLINE
};

typedef struct {
	unsigned int type;                  // SHADER_FUNC enum
	float args[4];                      // offset, amplitude, phase_offset, rate
} shaderfunc_t;

typedef struct {
	unsigned int type;
	float args[6];
} tcmod_t;

typedef struct {
	unsigned int type;
	float               *args;
	shaderfunc_t func;
} colorgen_t;

typedef struct {
	unsigned int type;
	float args[4];
	shaderfunc_t func;
} deformv_t;

// Per-pass rendering state information
typedef struct {
	unsigned int flags;

	colorgen_t rgbgen;
	colorgen_t alphagen;

	unsigned int tcgen;
	vec_t               *tcgenVec;

	unsigned int numtcmods;
	tcmod_t             *tcmods;

	unsigned int cin;

	unsigned int program_type;

	image_t             *images[MAX_SHADER_IMAGES]; // texture refs

	float anim_fps;                                 // animation frames per sec
	unsigned int anim_numframes;
} shaderpass_t;

// Shader information
typedef struct shader_s {
	char                *name;
	unsigned int id;
	int registrationSequence;
	shaderType_e type;

	unsigned int flags;
	vattribmask_t vattribs;
	unsigned int sort;
	int imagetags;                                  // usage tags of the images - currently only depend
	                                                // on type, but if one shader can be requesed with
	                                                // different tags, functions like R_TouchShader
	                                                // should merge the existing and the requested tags

	unsigned int numpasses;
	shaderpass_t        *passes;

	unsigned int numdeforms;
	deformv_t           *deforms;
	char                *deformsKey;

	uint8_t fog_color[4];
	float fog_dist, fog_clearDist;

	unsigned int cin;

	float glossIntensity;
	float glossExponent;
	float offsetmappingScale;

	float portalDistance;

	float skyHeight;
	image_t             *skyboxImages[6];

	struct shader_s     *prev, *next;
} shader_t;

#define     Shader_UseTextureFog( s ) ( ( ( s )->sort <= SHADER_SORT_FOG && \
										  ( ( s )->flags & SHADER_DEPTHWRITE ) ) || ( s )->fog_dist || ( s )->type == SHADER_TYPE_FOG )

#define     Shader_ReadDepth( s ) ( ( s )->flags & SHADER_SOFT_PARTICLE )

void        R_InitShaders( void );
void        R_ShutdownShaders( void );

void        R_UploadCinematicShader( const shader_t *shader );

void        R_PrintShaderList( const char *mask, bool ( *filter )( const char *filter, const char *value ) );
void        R_PrintShaderCache( const char *name );

shader_t    *R_ShaderById( unsigned int id );

shader_t    *R_LoadShader( const char *name, shaderType_e type, bool forceDefault, const char *text );

shader_t    *R_RegisterShader( const char *name, shaderType_e type );
shader_t    *R_RegisterPic( const char *name );
shader_t    *R_RegisterRawPic( const char *name, int width, int height, uint8_t *data, int samples );
shader_t    *R_RegisterRawAlphaMask( const char *name, int width, int height, uint8_t *data );
shader_t    *R_RegisterLevelshot( const char *name, shader_t *defaultShader, bool *matchesDefault );
shader_t    *R_RegisterSkin( const char *name );
shader_t    *R_RegisterVideo( const char *name );
shader_t    *R_RegisterLinearPic( const char *name );

unsigned    R_PackShaderOrder( const shader_t *shader );

void        R_TouchShader( shader_t *s );
void        R_TouchShadersByName( const char *name );
void        R_FreeUnusedShadersByType( const shaderType_e *types, unsigned int numTypes );
void        R_FreeUnusedShaders( void );

void        R_RemapShader( const char *from, const char *to, int timeOffset );

void        R_GetShaderDimensions( const shader_t *shader, int *width, int *height );

void        R_ReplaceRawSubPic( shader_t *shader, int x, int y, int width, int height, uint8_t *data );

#endif // R_SHADER_H
