/*
Copyright (C) 2002-2003 Victor Luchits

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

#include "cg_local.h"

cgs_media_handle_t *sfx_headnode;

/*
* CG_RegisterMediaSfx
*/
static cgs_media_handle_t *CG_RegisterMediaSfx( const char *name, bool precache ) {
	cgs_media_handle_t *mediasfx;

	for( mediasfx = sfx_headnode; mediasfx; mediasfx = mediasfx->next ) {
		if( !Q_stricmp( mediasfx->name, name ) ) {
			return mediasfx;
		}
	}

	mediasfx = ( cgs_media_handle_t * )CG_Malloc( sizeof( cgs_media_handle_t ) );
	mediasfx->name = CG_CopyString( name );
	mediasfx->next = sfx_headnode;
	sfx_headnode = mediasfx;

	if( precache ) {
		mediasfx->data = ( void * )trap_S_RegisterSound( mediasfx->name );
	}

	return mediasfx;
}

/*
* CG_MediaSfx
*/
struct sfx_s *CG_MediaSfx( cgs_media_handle_t *mediasfx ) {
	if( !mediasfx->data ) {
		mediasfx->data = ( void * )trap_S_RegisterSound( mediasfx->name );
	}
	return ( struct sfx_s * )mediasfx->data;
}

/*
* CG_RegisterMediaSounds
*/
void CG_RegisterMediaSounds( void ) {
	int i;

	sfx_headnode = NULL;

	cgs.media.sfxChat = CG_RegisterMediaSfx( S_CHAT, true );

	for( i = 0; i < 2; i++ )
		cgs.media.sfxRic[i] = CG_RegisterMediaSfx( va( "sounds/weapons/ric%i", i + 1 ), true );

	// weapon
	for( i = 0; i < 4; i++ )
		cgs.media.sfxWeaponHit[i] = CG_RegisterMediaSfx( va( S_WEAPON_HITS, i ), true );
	cgs.media.sfxWeaponKill = CG_RegisterMediaSfx( S_WEAPON_KILL, true );
	cgs.media.sfxWeaponHitTeam = CG_RegisterMediaSfx( S_WEAPON_HIT_TEAM, true );
	cgs.media.sfxWeaponUp = CG_RegisterMediaSfx( S_WEAPON_SWITCH, true );
	cgs.media.sfxWeaponUpNoAmmo = CG_RegisterMediaSfx( S_WEAPON_NOAMMO, true );

	cgs.media.sfxWalljumpFailed = CG_RegisterMediaSfx( "sounds/world/ft_walljump_failed", true );

	cgs.media.sfxItemRespawn = CG_RegisterMediaSfx( S_ITEM_RESPAWN, true );
	cgs.media.sfxPlayerRespawn = CG_RegisterMediaSfx( S_PLAYER_RESPAWN, true );
	cgs.media.sfxTeleportIn = CG_RegisterMediaSfx( S_TELEPORT, true );
	cgs.media.sfxTeleportOut = CG_RegisterMediaSfx( S_TELEPORT, true );

	//	cgs.media.sfxJumpPad = CG_RegisterMediaSfx ( S_JUMPPAD, true );
	cgs.media.sfxShellHit = CG_RegisterMediaSfx( S_SHELL_HIT, true );

	// Gunblade sounds (weak is blade):
	for( i = 0; i < 3; i++ ) cgs.media.sfxGunbladeWeakShot[i] = CG_RegisterMediaSfx( va( S_WEAPON_GUNBLADE_W_SHOT_1_to_3, i + 1 ), true );
	for( i = 0; i < 3; i++ ) cgs.media.sfxBladeFleshHit[i] = CG_RegisterMediaSfx( va( S_WEAPON_GUNBLADE_W_HIT_FLESH_1_to_3, i + 1 ), true );
	for( i = 0; i < 2; i++ ) cgs.media.sfxBladeWallHit[i] = CG_RegisterMediaSfx( va( S_WEAPON_GUNBLADE_W_HIT_WALL_1_to_2, i + 1 ), false );
	cgs.media.sfxGunbladeStrongShot = CG_RegisterMediaSfx( S_WEAPON_GUNBLADE_S_SHOT, true );
	for( i = 0; i < 3; i++ ) cgs.media.sfxGunbladeStrongHit[i] = CG_RegisterMediaSfx( va( S_WEAPON_GUNBLADE_S_HIT_1_to_2, i + 1 ), true );

	// Riotgun sounds :
	cgs.media.sfxRiotgunWeakHit = CG_RegisterMediaSfx( S_WEAPON_RIOTGUN_W_HIT, false );
	cgs.media.sfxRiotgunStrongHit = CG_RegisterMediaSfx( S_WEAPON_RIOTGUN_S_HIT, true );

	// Grenade launcher sounds :
	for( i = 0; i < 2; i++ ) cgs.media.sfxGrenadeWeakBounce[i] = CG_RegisterMediaSfx( va( S_WEAPON_GRENADE_W_BOUNCE_1_to_2, i + 1 ), false );
	for( i = 0; i < 2; i++ ) cgs.media.sfxGrenadeStrongBounce[i] = CG_RegisterMediaSfx( va( S_WEAPON_GRENADE_S_BOUNCE_1_to_2, i + 1 ), true );
	cgs.media.sfxGrenadeWeakExplosion = CG_RegisterMediaSfx( S_WEAPON_GRENADE_W_HIT, false );
	cgs.media.sfxGrenadeStrongExplosion = CG_RegisterMediaSfx( S_WEAPON_GRENADE_S_HIT, true );

	// Rocket launcher sounds :
	cgs.media.sfxRocketLauncherWeakHit = CG_RegisterMediaSfx( S_WEAPON_ROCKET_W_HIT, false );
	cgs.media.sfxRocketLauncherStrongHit = CG_RegisterMediaSfx( S_WEAPON_ROCKET_S_HIT, true );

	// Plasmagun sounds :
	cgs.media.sfxPlasmaWeakHit = CG_RegisterMediaSfx( S_WEAPON_PLASMAGUN_W_HIT, false );
	cgs.media.sfxPlasmaStrongHit = CG_RegisterMediaSfx( S_WEAPON_PLASMAGUN_S_HIT, true );

	// Lasergun sounds
	cgs.media.sfxLasergunWeakHum = CG_RegisterMediaSfx( S_WEAPON_LASERGUN_W_HUM, false );
	cgs.media.sfxLasergunWeakQuadHum = CG_RegisterMediaSfx( S_WEAPON_LASERGUN_W_QUAD_HUM, true );
	cgs.media.sfxLasergunWeakStop = CG_RegisterMediaSfx( S_WEAPON_LASERGUN_W_STOP, false );
	cgs.media.sfxLasergunStrongHum = CG_RegisterMediaSfx( S_WEAPON_LASERGUN_S_HUM, true );
	cgs.media.sfxLasergunStrongQuadHum = CG_RegisterMediaSfx( S_WEAPON_LASERGUN_S_QUAD_HUM, true );
	cgs.media.sfxLasergunStrongStop = CG_RegisterMediaSfx( S_WEAPON_LASERGUN_S_STOP, true );
	cgs.media.sfxLasergunHit[0] = CG_RegisterMediaSfx( S_WEAPON_LASERGUN_HIT_0, true );
	cgs.media.sfxLasergunHit[1] = CG_RegisterMediaSfx( S_WEAPON_LASERGUN_HIT_1, true );
	cgs.media.sfxLasergunHit[2] = CG_RegisterMediaSfx( S_WEAPON_LASERGUN_HIT_2, true );

	cgs.media.sfxWaveWeakHit = CG_RegisterMediaSfx( S_WEAPON_SHOCKWAVE_W_HIT, true );
	cgs.media.sfxWaveStrongHit = CG_RegisterMediaSfx( S_WEAPON_SHOCKWAVE_S_HIT, true );

	cgs.media.sfxElectroboltHit = CG_RegisterMediaSfx( S_WEAPON_ELECTROBOLT_HIT, true );

	cgs.media.sfxQuadFireSound = CG_RegisterMediaSfx( S_QUAD_FIRE, true );
}

//======================================================================

cgs_media_handle_t *model_headnode;

/*
* CG_RegisterModel
*/
struct model_s *CG_RegisterModel( const char *name ) {
	struct model_s *model;

	model = trap_R_RegisterModel( name );

	// precache bones
	if( trap_R_SkeletalGetNumBones( model, NULL ) ) {
		CG_SkeletonForModel( model );
	}

	return model;
}

/*
* CG_RegisterMediaModel
*/
static cgs_media_handle_t *CG_RegisterMediaModel( const char *name, bool precache ) {
	cgs_media_handle_t *mediamodel;

	for( mediamodel = model_headnode; mediamodel; mediamodel = mediamodel->next ) {
		if( !Q_stricmp( mediamodel->name, name ) ) {
			return mediamodel;
		}
	}

	mediamodel = ( cgs_media_handle_t * )CG_Malloc( sizeof( cgs_media_handle_t ) );
	mediamodel->name = CG_CopyString( name );
	mediamodel->next = model_headnode;
	model_headnode = mediamodel;

	if( precache ) {
		mediamodel->data = ( void * )CG_RegisterModel( mediamodel->name );
	}

	return mediamodel;
}

/*
* CG_MediaModel
*/
struct model_s *CG_MediaModel( cgs_media_handle_t *mediamodel ) {
	if( !mediamodel ) {
		return NULL;
	}

	if( !mediamodel->data ) {
		mediamodel->data = ( void * )CG_RegisterModel( mediamodel->name );
	}
	return ( struct model_s * )mediamodel->data;
}

/*
* CG_RegisterMediaModels
*/
void CG_RegisterMediaModels( void ) {
	model_headnode = NULL;

	//	cgs.media.modGrenadeExplosion = CG_RegisterMediaModel( PATH_GRENADE_EXPLOSION_MODEL, true );
	cgs.media.modRocketExplosion = CG_RegisterMediaModel( PATH_ROCKET_EXPLOSION_MODEL, true );
	cgs.media.modPlasmaExplosion = CG_RegisterMediaModel( PATH_PLASMA_EXPLOSION_MODEL, true );
	cgs.media.modWaveExplosion = CG_RegisterMediaModel( PATH_SHOCKWAVE_EXPLOSION_MODEL, true );

	//	cgs.media.modBoltExplosion = CG_RegisterMediaModel( "models/weapon_hits/electrobolt/hit_electrobolt.md3", true );
	//	cgs.media.modInstaExplosion = CG_RegisterMediaModel( "models/weapon_hits/instagun/hit_instagun.md3", true );
	//	cgs.media.modTeleportEffect = CG_RegisterMediaModel( "models/misc/telep.md3", false );

	cgs.media.modDash = CG_RegisterMediaModel( "models/effects/dash_burst.md3", true );
	cgs.media.modHeadStun = CG_RegisterMediaModel( "models/effects/head_stun.md3", true );

	cgs.media.modBulletExplode = CG_RegisterMediaModel( PATH_BULLET_EXPLOSION_MODEL, true );
	cgs.media.modBladeWallHit = CG_RegisterMediaModel( PATH_GUNBLADEBLAST_IMPACT_MODEL, true );
	cgs.media.modBladeWallExplo = CG_RegisterMediaModel( PATH_GUNBLADEBLAST_EXPLOSION_MODEL, true );
	cgs.media.modElectroBoltWallHit = CG_RegisterMediaModel( PATH_ELECTROBLAST_IMPACT_MODEL, true );
	cgs.media.modInstagunWallHit = CG_RegisterMediaModel( PATH_INSTABLAST_IMPACT_MODEL, true );
	cgs.media.modLasergunWallExplo = CG_RegisterMediaModel( PATH_LASERGUN_IMPACT_MODEL, true );

	// gibs model
	cgs.media.modIlluminatiGibs = CG_RegisterMediaModel( "models/objects/gibs/illuminati/illuminati1.md3", true );
}

//======================================================================

cgs_media_handle_t *shader_headnode;

/*
* CG_RegisterMediaShader
*/
static cgs_media_handle_t *CG_RegisterMediaShader( const char *name, bool precache ) {
	cgs_media_handle_t *mediashader;

	for( mediashader = shader_headnode; mediashader; mediashader = mediashader->next ) {
		if( !Q_stricmp( mediashader->name, name ) ) {
			return mediashader;
		}
	}

	mediashader = ( cgs_media_handle_t * )CG_Malloc( sizeof( cgs_media_handle_t ) );
	mediashader->name = CG_CopyString( name );
	mediashader->next = shader_headnode;
	shader_headnode = mediashader;

	if( precache ) {
		mediashader->data = ( void * )trap_R_RegisterPic( mediashader->name );
	}

	return mediashader;
}

/*
* CG_MediaShader
*/
struct shader_s *CG_MediaShader( cgs_media_handle_t *mediashader ) {
	if( !mediashader->data ) {
		mediashader->data = ( void * )trap_R_RegisterPic( mediashader->name );
	}
	return ( struct shader_s * )mediashader->data;
}


/*
* CG_RegisterMediaShaders
*/
void CG_RegisterMediaShaders( void ) {
	shader_headnode = NULL;

	cgs.media.shaderParticle = CG_RegisterMediaShader( "particle", true );

	cgs.media.shaderNet = CG_RegisterMediaShader( "gfx/hud/net", true );
	cgs.media.shaderBackTile = CG_RegisterMediaShader( "gfx/ui/backtile", true );
	cgs.media.shaderSelect = CG_RegisterMediaShader( "gfx/hud/select", true );
	cgs.media.shaderChatBalloon = CG_RegisterMediaShader( PATH_BALLONCHAT_ICON, true );
	cgs.media.shaderDownArrow = CG_RegisterMediaShader( "gfx/2d/arrow_down", true );

	cgs.media.shaderPlayerShadow = CG_RegisterMediaShader( "gfx/decals/shadow", true );

	cgs.media.shaderWaterBubble = CG_RegisterMediaShader( "gfx/misc/waterBubble", true );
	cgs.media.shaderSmokePuff = CG_RegisterMediaShader( "gfx/misc/smokepuff", true );

	cgs.media.shaderSmokePuff1 = CG_RegisterMediaShader( "gfx/misc/smokepuff1", true );
	cgs.media.shaderSmokePuff2 = CG_RegisterMediaShader( "gfx/misc/smokepuff2", true );
	cgs.media.shaderSmokePuff3 = CG_RegisterMediaShader( "gfx/misc/smokepuff3", true );

	cgs.media.shaderStrongRocketFireTrailPuff = CG_RegisterMediaShader( "gfx/misc/strong_rocket_fire", true );
	cgs.media.shaderWeakRocketFireTrailPuff = CG_RegisterMediaShader( "gfx/misc/strong_rocket_fire", false );
	cgs.media.shaderTeleporterSmokePuff = CG_RegisterMediaShader( "TeleporterSmokePuff", true );
	cgs.media.shaderGrenadeTrailSmokePuff = CG_RegisterMediaShader( "gfx/grenadetrail_smoke_puf", true );
	cgs.media.shaderRocketTrailSmokePuff = CG_RegisterMediaShader( "gfx/misc/rocketsmokepuff", true );
	cgs.media.shaderBloodTrailPuff = CG_RegisterMediaShader( "gfx/misc/bloodtrail_puff", true );
	cgs.media.shaderBloodTrailLiquidPuff = CG_RegisterMediaShader( "gfx/misc/bloodtrailliquid_puff", true );
	cgs.media.shaderBloodImpactPuff = CG_RegisterMediaShader( "gfx/misc/bloodimpact_puff", true );
	cgs.media.shaderCartoonHit = CG_RegisterMediaShader( "gfx/misc/cartoonhit", true );
	cgs.media.shaderCartoonHit2 = CG_RegisterMediaShader( "gfx/misc/cartoonhit2", true );
	cgs.media.shaderCartoonHit3 = CG_RegisterMediaShader( "gfx/misc/cartoonhit3", true );
	cgs.media.shaderTeamMateIndicator = CG_RegisterMediaShader( "gfx/indicators/teammate_indicator", true );
	cgs.media.shaderTeamCarrierIndicator = CG_RegisterMediaShader( "gfx/indicators/teamcarrier_indicator", true );
	cgs.media.shaderTeleportShellGfx = CG_RegisterMediaShader( "gfx/misc/teleportshell", true );

	cgs.media.shaderAdditiveParticleShine = CG_RegisterMediaShader( "additiveParticleShine", true );

	cgs.media.shaderBladeMark = CG_RegisterMediaShader( "gfx/decals/d_blade_hit", true );
	cgs.media.shaderBulletMark = CG_RegisterMediaShader( "gfx/decals/d_bullet_hit", true );
	cgs.media.shaderExplosionMark = CG_RegisterMediaShader( "gfx/decals/d_explode_hit", true );
	cgs.media.shaderPlasmaMark = CG_RegisterMediaShader( "gfx/decals/d_plasma_hit", true );
	cgs.media.shaderElectroboltMark = CG_RegisterMediaShader( "gfx/decals/d_electrobolt_hit", true );
	cgs.media.shaderInstagunMark = CG_RegisterMediaShader( "gfx/decals/d_instagun_hit", true );

	cgs.media.shaderElectroBeamOld = CG_RegisterMediaShader( "gfx/misc/electro", true );
	cgs.media.shaderElectroBeamOldAlpha = CG_RegisterMediaShader( "gfx/misc/electro_alpha", true );
	cgs.media.shaderElectroBeamOldBeta = CG_RegisterMediaShader( "gfx/misc/electro_beta", true );
	cgs.media.shaderElectroBeamA = CG_RegisterMediaShader( "gfx/misc/electro2a", true );
	cgs.media.shaderElectroBeamAAlpha = CG_RegisterMediaShader( "gfx/misc/electro2a_alpha", true );
	cgs.media.shaderElectroBeamABeta = CG_RegisterMediaShader( "gfx/misc/electro2a_beta", true );
	cgs.media.shaderElectroBeamB = CG_RegisterMediaShader( "gfx/misc/electro2b", true );
	cgs.media.shaderElectroBeamBAlpha = CG_RegisterMediaShader( "gfx/misc/electro2b_alpha", true );
	cgs.media.shaderElectroBeamBBeta = CG_RegisterMediaShader( "gfx/misc/electro2b_beta", true );
	cgs.media.shaderElectroBeamRing = CG_RegisterMediaShader( "gfx/misc/beamring.tga", true );
	cgs.media.shaderWaveCorona = CG_RegisterMediaShader( "gfx/misc/shockwave_corona", true );
	cgs.media.shaderWaveSparks = CG_RegisterMediaShader( "gfx/misc/shockwave_sparks", true );
	cgs.media.shaderInstaBeam = CG_RegisterMediaShader( "gfx/misc/instagun", true );
	cgs.media.shaderLaserGunBeam = CG_RegisterMediaShader( "gfx/misc/laserbeam", true );
	cgs.media.shaderRocketExplosion = CG_RegisterMediaShader( PATH_ROCKET_EXPLOSION_SPRITE, true );
	cgs.media.shaderRocketExplosionRing = CG_RegisterMediaShader( PATH_ROCKET_EXPLOSION_RING_SPRITE, true );
	cgs.media.shaderWaveExplosionRing = CG_RegisterMediaShader( PATH_WAVE_EXPLOSION_RING_SPRITE, true );

	cgs.media.shaderLaser = CG_RegisterMediaShader( "gfx/misc/laser", false );

	// ctf
	cgs.media.shaderFlagFlare = CG_RegisterMediaShader( PATH_FLAG_FLARE_SHADER, false );

	cgs.media.shaderRaceGhostEffect = CG_RegisterMediaShader( "gfx/raceghost", false );

	// Kurim : weapon icons
	cgs.media.shaderWeaponIcon[WEAP_GUNBLADE - 1] = CG_RegisterMediaShader( PATH_GUNBLADE_ICON, true );
	cgs.media.shaderWeaponIcon[WEAP_MACHINEGUN - 1] = CG_RegisterMediaShader( PATH_MACHINEGUN_ICON, true );
	cgs.media.shaderWeaponIcon[WEAP_RIOTGUN - 1] = CG_RegisterMediaShader( PATH_RIOTGUN_ICON, true );
	cgs.media.shaderWeaponIcon[WEAP_GRENADELAUNCHER - 1] = CG_RegisterMediaShader( PATH_GRENADELAUNCHER_ICON, true );
	cgs.media.shaderWeaponIcon[WEAP_ROCKETLAUNCHER - 1] = CG_RegisterMediaShader( PATH_ROCKETLAUNCHER_ICON, true );
	cgs.media.shaderWeaponIcon[WEAP_PLASMAGUN - 1] = CG_RegisterMediaShader( PATH_PLASMAGUN_ICON, true );
	cgs.media.shaderWeaponIcon[WEAP_LASERGUN - 1] = CG_RegisterMediaShader( PATH_LASERGUN_ICON, true );
	cgs.media.shaderWeaponIcon[WEAP_ELECTROBOLT - 1] = CG_RegisterMediaShader( PATH_ELECTROBOLT_ICON, true );
	cgs.media.shaderWeaponIcon[WEAP_SHOCKWAVE - 1] = CG_RegisterMediaShader( PATH_SHOCKWAVE_ICON, true );
	cgs.media.shaderWeaponIcon[WEAP_INSTAGUN - 1] = CG_RegisterMediaShader( PATH_INSTAGUN_ICON, true );

	cgs.media.shaderNoGunWeaponIcon[WEAP_GUNBLADE - 1] = CG_RegisterMediaShader( PATH_NG_GUNBLADE_ICON, true );
	cgs.media.shaderNoGunWeaponIcon[WEAP_MACHINEGUN - 1] = CG_RegisterMediaShader( PATH_NG_MACHINEGUN_ICON, true );
	cgs.media.shaderNoGunWeaponIcon[WEAP_RIOTGUN - 1] = CG_RegisterMediaShader( PATH_NG_RIOTGUN_ICON, true );
	cgs.media.shaderNoGunWeaponIcon[WEAP_GRENADELAUNCHER - 1] = CG_RegisterMediaShader( PATH_NG_GRENADELAUNCHER_ICON, true );
	cgs.media.shaderNoGunWeaponIcon[WEAP_ROCKETLAUNCHER - 1] = CG_RegisterMediaShader( PATH_NG_ROCKETLAUNCHER_ICON, true );
	cgs.media.shaderNoGunWeaponIcon[WEAP_PLASMAGUN - 1] = CG_RegisterMediaShader( PATH_NG_PLASMAGUN_ICON, true );
	cgs.media.shaderNoGunWeaponIcon[WEAP_LASERGUN - 1] = CG_RegisterMediaShader( PATH_NG_LASERGUN_ICON, true );
	cgs.media.shaderNoGunWeaponIcon[WEAP_ELECTROBOLT - 1] = CG_RegisterMediaShader( PATH_NG_ELECTROBOLT_ICON, true );
	cgs.media.shaderNoGunWeaponIcon[WEAP_SHOCKWAVE - 1] = CG_RegisterMediaShader( PATH_NG_SHOCKWAVE_ICON, true );
	cgs.media.shaderNoGunWeaponIcon[WEAP_INSTAGUN - 1] = CG_RegisterMediaShader( PATH_NG_INSTAGUN_ICON, true );

	cgs.media.shaderGunbladeBlastIcon = CG_RegisterMediaShader( PATH_GUNBLADE_BLAST_ICON, true );

	cgs.media.shaderInstagunChargeIcon[0] = CG_RegisterMediaShader( "gfx/hud/icons/weapon/instagun_0", true );
	cgs.media.shaderInstagunChargeIcon[1] = CG_RegisterMediaShader( "gfx/hud/icons/weapon/instagun_1", true );
	cgs.media.shaderInstagunChargeIcon[2] = CG_RegisterMediaShader( "gfx/hud/icons/weapon/instagun_2", true );

	// Kurim : keyicons
	cgs.media.shaderKeyIcon[KEYICON_FORWARD] = CG_RegisterMediaShader( PATH_KEYICON_FORWARD, true );
	cgs.media.shaderKeyIcon[KEYICON_BACKWARD] = CG_RegisterMediaShader( PATH_KEYICON_BACKWARD, true );
	cgs.media.shaderKeyIcon[KEYICON_LEFT] = CG_RegisterMediaShader( PATH_KEYICON_LEFT, true );
	cgs.media.shaderKeyIcon[KEYICON_RIGHT] = CG_RegisterMediaShader( PATH_KEYICON_RIGHT, true );
	cgs.media.shaderKeyIcon[KEYICON_FIRE] = CG_RegisterMediaShader( PATH_KEYICON_FIRE, true );
	cgs.media.shaderKeyIcon[KEYICON_JUMP] = CG_RegisterMediaShader( PATH_KEYICON_JUMP, true );
	cgs.media.shaderKeyIcon[KEYICON_CROUCH] = CG_RegisterMediaShader( PATH_KEYICON_CROUCH, true );
	cgs.media.shaderKeyIcon[KEYICON_SPECIAL] = CG_RegisterMediaShader( PATH_KEYICON_SPECIAL, true );

	cgs.media.shaderSbNums = CG_RegisterMediaShader( "gfx/hud/sbnums", true );
	cgs.media.shaderReadyIcon = CG_RegisterMediaShader( PATH_VSAY_YES_ICON, true );
}

void CG_RegisterLevelMinimap( void ) {
	size_t i;
	int file;
	char *name, minimap[MAX_QPATH];

	cgs.shaderMiniMap = NULL;

	name = cgs.configStrings[CS_MAPNAME];

	for( i = 0; i < NUM_IMAGE_EXTENSIONS; i++ ) {
		Q_snprintfz( minimap, sizeof( minimap ), "minimaps/%s%s", name, IMAGE_EXTENSIONS[i] );
		file = trap_FS_FOpenFile( minimap, NULL, FS_READ );
		if( file != -1 ) {
			cgs.shaderMiniMap = trap_R_RegisterPic( minimap );
			break;
		}
	}
}

/*
* CG_RegisterFonts
*/
void CG_RegisterFonts( void ) {
	cvar_t *con_fontSystemFamily = trap_Cvar_Get( "con_fontSystemFamily", DEFAULT_SYSTEM_FONT_FAMILY, CVAR_ARCHIVE );
	cvar_t *con_fontSystemMonoFamily = trap_Cvar_Get( "con_fontSystemMonoFamily", DEFAULT_SYSTEM_FONT_FAMILY_MONO, CVAR_ARCHIVE );
	cvar_t *con_fontSystemSmallSize = trap_Cvar_Get( "con_fontSystemSmallSize", STR_TOSTR( DEFAULT_SYSTEM_FONT_SMALL_SIZE ), CVAR_ARCHIVE );
	cvar_t *con_fontSystemMediumSize = trap_Cvar_Get( "con_fontSystemMediumSize", STR_TOSTR( DEFAULT_SYSTEM_FONT_MEDIUM_SIZE ), CVAR_ARCHIVE );
	cvar_t *con_fontSystemBigSize = trap_Cvar_Get( "con_fontSystemBigSize", STR_TOSTR( DEFAULT_SYSTEM_FONT_BIG_SIZE ), CVAR_ARCHIVE );

	// register system fonts
	Q_strncpyz( cgs.fontSystemFamily, con_fontSystemFamily->string, sizeof( cgs.fontSystemFamily ) );
	Q_strncpyz( cgs.fontSystemMonoFamily, con_fontSystemMonoFamily->string, sizeof( cgs.fontSystemMonoFamily ) );
	if( con_fontSystemSmallSize->integer <= 0 ) {
		trap_Cvar_Set( con_fontSystemSmallSize->name, con_fontSystemSmallSize->dvalue );
	}
	if( con_fontSystemMediumSize->integer <= 0 ) {
		trap_Cvar_Set( con_fontSystemMediumSize->name, con_fontSystemMediumSize->dvalue );
	}
	if( con_fontSystemBigSize->integer <= 0 ) {
		trap_Cvar_Set( con_fontSystemBigSize->name, con_fontSystemBigSize->dvalue );
	}

	float scale = ( float )( cgs.vidHeight ) / 600.0f;

	cgs.fontSystemSmallSize = ceilf( con_fontSystemSmallSize->integer * scale );
	cgs.fontSystemSmall = trap_SCR_RegisterFont( cgs.fontSystemFamily, QFONT_STYLE_NONE, cgs.fontSystemSmallSize );
	if( !cgs.fontSystemSmall ) {
		Q_strncpyz( cgs.fontSystemFamily, DEFAULT_SYSTEM_FONT_FAMILY, sizeof( cgs.fontSystemFamily ) );
		cgs.fontSystemSmallSize = ceilf( DEFAULT_SYSTEM_FONT_SMALL_SIZE * scale );

		cgs.fontSystemSmall = trap_SCR_RegisterFont( cgs.fontSystemFamily, QFONT_STYLE_NONE, cgs.fontSystemSmallSize );
		if( !cgs.fontSystemSmall ) {
			CG_Error( "Couldn't load default font \"%s\"", cgs.fontSystemFamily );
		}
	}

	cgs.fontSystemMediumSize = ceilf( con_fontSystemMediumSize->integer * scale );
	cgs.fontSystemMedium = trap_SCR_RegisterFont( cgs.fontSystemFamily, QFONT_STYLE_NONE, cgs.fontSystemMediumSize );
	if( !cgs.fontSystemMedium ) {
		cgs.fontSystemMediumSize = ceilf( DEFAULT_SYSTEM_FONT_MEDIUM_SIZE * scale );
		cgs.fontSystemMedium = trap_SCR_RegisterFont( cgs.fontSystemFamily, QFONT_STYLE_NONE, cgs.fontSystemMediumSize );
	}

	cgs.fontSystemBigSize = ceilf( con_fontSystemBigSize->integer * scale );
	cgs.fontSystemBig = trap_SCR_RegisterFont( cgs.fontSystemFamily, QFONT_STYLE_NONE, cgs.fontSystemBigSize );
	if( !cgs.fontSystemBig ) {
		cgs.fontSystemBigSize = ceilf( DEFAULT_SYSTEM_FONT_BIG_SIZE * scale );
		cgs.fontSystemBig = trap_SCR_RegisterFont( cgs.fontSystemFamily, QFONT_STYLE_NONE, cgs.fontSystemBigSize );
	}
}
