/*
Copyright (C) 1997-2001 Id Software, Inc.

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

// gs_weapondefs.c	-	hard coded weapon definitions

#include "q_arch.h"
#include "q_math.h"
#include "q_shared.h"
#include "q_comref.h"
#include "q_collision.h"
#include "gs_public.h"

#define INSTANT 0

#define WEAPONDOWN_FRAMETIME 50
#define WEAPONUP_FRAMETIME 50
#define DEFAULT_BULLET_SPREAD 350

gs_weapon_definition_t gs_weaponDefs[] =
{
	{
		"no weapon",
		WEAP_NONE,
		{
			FIRE_MODE_STRONG,               // fire mode
			AMMO_NONE,                      // ammo tag
			0,                              // ammo usage per shot
			0,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			0,                              // reload frametime
			0,                              // cooldown frametime
			0,                              // projectile timeout
			false,                          // smooth refire

			//damages
			0,                              // damage
			0,                              // selfdamage ratio
			0,                              // knockback
			0,                              // stun
			0,                              // splash radius
			0,                              // splash fraction
			0,                              // splash minimum damage
			0,                              // splash minimum knockback

			//projectile def
			0,                              // speed
			0,                              // spread
			0,                              // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep


			//ammo
			0,                              // weapon pickup amount
			0,                              // pickup amount
			0,                              // max amount
			0                               // low ammo threshold
		},

		{
			FIRE_MODE_WEAK,                 // fire mode
			AMMO_NONE,                      // ammo tag
			0,                              // ammo usage per shot
			0,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			0,                              // reload frametime
			0,                              // cooldown frametime
			0,                              // projectile timeout
			false,                          // smooth refire

			//damages
			0,                              // damage
			0,                              // selfdamage ratio
			0,                              // knockback
			0,                              // stun
			0,                              // splash radius
			0,                              // splash fraction
			0,                              // splash minimum damage
			0,                              // splash minimum knockback

			//projectile def
			0,                              // speed
			0,                              // spread
			0,                              // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			0,                              // weapon pickup amount
			0,                              // pickup amount
			0,                              // max amount
			0                               // low ammo threshold
		},
	},

	{
		"Gunblade",
		WEAP_GUNBLADE,
		{
			FIRE_MODE_STRONG,
			AMMO_GUNBLADE,
			1,                              // ammo usage per shot
			1,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			600,                            // reload frametime
			0,                              // cooldown frametime
			5000,                           // projectile timeout
			false,                          // smooth refire

			//damages
			35,                             // damage
			1.0,                            // selfdamage ratio
			90,                             // knockback
			0,                              // stun
			80,                             // splash radius
			0,                              // splash fraction
			8,                              // splash minimum damage
			10,                             // splash minimum knockback

			//projectile def
			3000,                           // speed
			0,                              // spread
			0,                              // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			0,                              // weapon pickup amount
			0,                              // pickup amount
			1,                              // max amount
			0                               // low ammo threshold
		},

		{
			FIRE_MODE_WEAK,
			AMMO_WEAK_GUNBLADE,
			0,                              // ammo usage per shot
			0,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			600,                            // reload frametime
			0,                              // cooldown frametime
			64,                             // projectile timeout  / projectile range for instant weapons
			false,                          // smooth refire

			//damages
			50,                             // damage
			0,                              // selfdamage ratio
			50,                             // knockback
			0,                              // stun
			0,                              // splash radius
			0,                              // splash fraction
			0,                              // splash minimum damage
			0,                              // splash minimum knockback

			//projectile def
			0,                              // speed
			0,                              // spread
			0,                              // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			0,                              // weapon pickup amount
			0,                              // pickup amount
			0,                              // max amount
			0                               // low ammo threshold
		},
	},

	{
		"Machinegun",
		WEAP_MACHINEGUN,
		{
			FIRE_MODE_STRONG,
			AMMO_BULLETS,
			1,                              // ammo usage per shot
			1,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			100,                            // reload frametime
			0,                              // cooldown frametime
			6000,                           // projectile timeout
			false,                          // smooth refire

			//damages
			10,                             // damage
			0,                              // selfdamage ratio
			10,                             // knockback
			50,                             // stun
			0,                              // splash radius
			0,                              // splash fraction
			0,                              // splash minimum damage
			0,                              // splash minimum knockback

			//projectile def
			INSTANT,                        // speed
			10,                             // spread
			10,                             // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			50,                             // weapon pickup amount
			50,                             // pickup amount
			100,                            // max amount
			20                              // low ammo threshold
		},

		{
			FIRE_MODE_WEAK,
			AMMO_WEAK_BULLETS,
			1,                              // ammo usage per shot
			1,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			100,                            // reload frametime
			0,                              // cooldown frametime
			6000,                           // projectile timeout
			false,                          // smooth refire

			//damages
			10,                             // damage
			0,                              // selfdamage ratio
			10,                             // knockback
			50,                             // stun
			0,                              // splash radius
			0,                              // splash fraction
			0,                              // splash minimum damage
			0,                              // splash minimum knockback

			//projectile def
			INSTANT,                        // speed
			10,                             // spread
			10,                             // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			0,                              // weapon pickup amount
			0,                              // pickup amount
			0,                              // max amount
			0                               // low ammo threshold
		},
	},

	{
		"Riotgun",
		WEAP_RIOTGUN,
		{
			FIRE_MODE_STRONG,
			AMMO_SHELLS,
			1,                              // ammo usage per shot
			20,                             // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			900,                            // reload frametime
			0,                              // cooldown frametime
			8192,                           // projectile timeout / projectile range for instant weapons
			false,                          // smooth refire

			//damages
			5,                              // damage
			0,                              // selfdamage ratio (rg cant selfdamage)
			7,                              // knockback
			85,                             // stun
			0,                              // splash radius
			0,                              // splash fraction
			0,                              // splash minimum damage
			0,                              // splash minimum knockback

			//projectile def
			INSTANT,                        // speed
			160,                            // spread
			90,                             // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			10,                             // weapon pickup amount
			10,                             // pickup amount
			20,                             // max amount
			3                               // low ammo threshold
		},

		{
			FIRE_MODE_WEAK,
			AMMO_WEAK_SHELLS,
			1,                              // ammo usage per shot
			25,                             // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			900,                            // reload frametime
			0,                              // cooldown frametime
			8192,                           // projectile timeout / projectile range for instant weapons
			false,                          // smooth refire

			//damages
			4,                              // damage
			0,                              // selfdamage ratio (rg cant selfdamage)
			5,                              // knockback
			85,                             // stun
			0,                              // splash radius
			0,                              // splash fraction
			0,                              // splash minimum damage
			0,                              // splash minimum knockback

			//projectile def
			INSTANT,                        // speed
			160,                            // spread
			90,                             // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			0,                              // weapon pickup amount
			0,                              // pickup amount
			0,                              // max amount
			0                               // low ammo threshold
		},
	},

	{
		"Grenade Launcher",
		WEAP_GRENADELAUNCHER,
		{
			FIRE_MODE_STRONG,
			AMMO_GRENADES,
			1,                              // ammo usage per shot
			1,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			800,                            // reload frametime
			0,                              // cooldown frametime
			1250,                           // projectile timeout
			false,                          // smooth refire

			//damages
			80,                             // damage
			1.00,                           // selfdamage ratio
			100,                                // knockback
			1250,                           // stun
			125,                            // splash radius
			0,                              // splash fraction
			15,                             // splash minimum damage
			35,                             // splash minimum knockback

			//projectile def
			1000,                           // speed
			0,                              // spread
			0,                              // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			10,                             // weapon pickup amount
			10,                             // pickup amount
			20,                             // max amount
			3                               // low ammo threshold
		},

		{
			FIRE_MODE_WEAK,
			AMMO_WEAK_GRENADES,
			1,                              // ammo usage per shot
			1,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			800,                            // reload frametime
			0,                              // cooldown frametime
			1250,                           // projectile timeout
			false,                          // smooth refire

			//damages
			80,                             // damage
			1.00,                           // selfdamage ratio
			100,                                // knockback
			1250,                           // stun
			135,                            // splash radius
			0,                              // splash fraction
			15,                             // splash minimum damage
			35,                             // splash minimum knockback

			//projectile def
			1000,                           // speed
			0,                              // spread
			0,                              // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			0,                              // weapon pickup amount
			0,                              // pickup amount
			0,                              // max amount
			0                               // low ammo threshold
		},
	},

	{
		"Rocket Launcher",
		WEAP_ROCKETLAUNCHER,
		{
			FIRE_MODE_STRONG,
			AMMO_ROCKETS,
			1,                              // ammo usage per shot
			1,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			950,                            // reload frametime
			0,                              // cooldown frametime
			10000,                          // projectile timeout
			false,                          // smooth refire

			//damages
			80,                             // damage
			1.00,                           // selfdamage ratio
			100,                                // knockback
			1250,                           // stun
			125,                            // splash radius
			0,                              // splash fraction
			15,                             // splash minimum damage
			35,                             // splash minimum knockback

			//projectile def
			1150,                           // speed
			0,                              // spread
			0,                              // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			5,                              // weapon pickup amount
			10,                             // pickup amount
			20,                             // max amount
			3                               // low ammo threshold
		},

		{
			FIRE_MODE_WEAK,
			AMMO_WEAK_ROCKETS,
			1,                              // ammo usage per shot
			1,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			950,                            // reload frametime
			0,                              // cooldown frametime
			10000,                          // projectile timeout
			false,                          // smooth refire

			//damages
			80,                             // damage
			1.00,                           // selfdamage ratio
			100,                                // knockback
			1250,                           // stun
			135,                            // splash radius
			0,                              // splash fraction
			15,                             // splash minimum damage
			35,                             // splash minimum knockback

			//projectile def
			1150,                           // speed
			0,                              // spread
			0,                              // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			0,                              // weapon pickup amount
			0,                              // pickup amount
			0,                              // max amount
			0                               // low ammo threshold
		},
	},

	{
		"Plasmagun",
		WEAP_PLASMAGUN,
		{
			FIRE_MODE_STRONG,
			AMMO_PLASMA,
			1,                              // ammo usage per shot
			1,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			100,                            // reload frametime
			0,                              // cooldown frametime
			5000,                           // projectile timeout
			false,                          // smooth refire

			//damages
			15,                             // damage
			0.5,                            // selfdamage ratio
			20,                             // knockback
			200,                            // stun
			45,                             // splash radius
			0,                              // splash fraction
			5,                              // splash minimum damage
			1,                              // splash minimum knockback

			//projectile def
			2500,                           // speed
			0,                              // spread
			0,                              // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			50,                             // weapon pickup amount
			100,                            // pickup amount
			150,                            // max amount
			20                              // low ammo threshold
		},

		{
			FIRE_MODE_WEAK,
			AMMO_WEAK_PLASMA,
			1,                              // ammo usage per shot
			1,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			100,                            // reload frametime
			0,                              // cooldown frametime
			5000,                           // projectile timeout
			false,                          // smooth refire

			//damages
			15,                             // damage
			0.5,                            // selfdamage ratio
			20,                             // knockback
			200,                            // stun
			45,                             // splash radius
			0,                              // splash fraction
			5,                              // splash minimum damage
			1,                              // splash minimum knockback

			//projectile def
			2500,                           // speed
			0,                              // spread
			0,                              // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			0,                              // weapon pickup amount
			0,                              // pickup amount
			0,                              // max amount
			0                               // low ammo threshold
		},
	},

	{
		"Lasergun",
		WEAP_LASERGUN,
		{
			FIRE_MODE_STRONG,
			AMMO_LASERS,
			1,                              // ammo usage per shot
			1,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			50,                             // reload frametime
			0,                              // cooldown frametime
			850,                            // projectile timeout / projectile range for instant weapons
			true,                           // smooth refire

			//damages
			7,                              // damage
			0,                              // selfdamage ratio (lg cant damage)
			14,                             // knockback
			300,                            // stun
			0,                              // splash radius
			0,                              // splash fraction
			0,                              // splash minimum damage
			0,                              // splash minimum knockback

			//projectile def
			INSTANT,                        // speed
			0,                              // spread
			0,                              // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			50,                             // weapon pickup amount
			100,                            // pickup amount
			150,                            // max amount
			20                              // low ammo threshold
		},

		{
			FIRE_MODE_WEAK,
			AMMO_WEAK_LASERS,
			1,                              // ammo usage per shot
			1,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			50,                             // reload frametime
			0,                              // cooldown frametime
			850,                            // projectile timeout / projectile range for instant weapons
			true,                           // smooth refire

			//damages
			7,                              // damage
			0,                              // selfdamage ratio (lg cant damage)
			14,                             // knockback
			300,                            // stun
			0,                              // splash radius
			0,                              // splash fraction
			0,                              // splash minimum damage
			0,                              // splash minimum knockback

			//projectile def
			INSTANT,                        // speed
			0,                              // spread
			0,                              // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			0,                              // weapon pickup amount
			0,                              // pickup amount
			150,                            // max amount
			20                              // low ammo threshold
		},
	},

	{
		"Electrobolt",
		WEAP_ELECTROBOLT,
		{
			FIRE_MODE_STRONG,
			AMMO_BOLTS,
			1,                              // ammo usage per shot
			1,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			1250,                           // reload frametime
			0,                              // cooldown frametime
			900,                            // min damage range
			false,                          // smooth refire

			//damages
			75,                             // damage
			0,                              // selfdamage ratio
			80,                             // knockback
			1000,                           // stun
			0,                              // splash radius
			0,                              // splash fraction
			75,                             // minimum damage
			35,                             // minimum knockback

			//projectile def
			INSTANT,                        // speed
			0,                              // spread
			0,                              // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			5,                              // weapon pickup amount
			10,                             // pickup amount
			10,                             // max amount
			3                               // low ammo threshold
		},

		{
			FIRE_MODE_WEAK,
			AMMO_WEAK_BOLTS,
			1,                              // ammo usage per shot
			1,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			1250,                           // reload frametime
			0,                              // cooldown frametime
			900,                            // min damage range
			false,                          // smooth refire

			//damages
			75,                             // damage
			0,                              // selfdamage ratio
			80,                             // knockback (let it match instant one)
			1000,                           // stun
			0,                              // splash radius
			0,                              // splash fraction
			75,                             // minimum damage
			35,                             // minimum knockback

			//projectile def
			8000,                           // speed (old was 6000 but there was a forum whine on it)
			0,                              // spread
			0,                              // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			0,                              // weapon pickup amount
			0,                              // pickup amount
			10,                             // max amount
			3                               // low ammo threshold
		},
	},

	{
		"Shockwave",
		WEAP_SHOCKWAVE,
		{
			FIRE_MODE_STRONG,
			AMMO_WAVES,
			1,                              // ammo usage per shot
			1,                              // projectiles fired each shot

			//timings (in msecs)
			300,                            // weapon up frametime (a noticeable delay)
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			950,                            // reload frametime (same as RL)
			0,                              // cooldown frametime
			10000,                          // projectile timeout
			false,                          // smooth refire

			//damages
			55,                             // damage
			1.0,                            // selfdamage ratio
			90,                             // knockback (as gunblade)
			500,                            // stun
			90,                             // splash radius
			0,                              // splash fraction
			55,                             // minimum damage
			35,                             // minimum knockback

			//projectile def
			775,                            // speed
			0,                              // spread
			0,                              // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			5,                              // weapon pickup amount
			10,                             // pickup amount
			10,                             // max amount
			3                               // low ammo threshold
		},

		{
			FIRE_MODE_WEAK,
			AMMO_WEAK_WAVES,
			1,                              // ammo usage per shot
			1,                              // projectiles fired each shot

			//timings (in msecs)
			300,                            // weapon up frametime (a noticeable delay)
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			950,                            // reload frametime (same as RL)
			0,                              // cooldown frametime
			10000,                          // projectile timeout
			false,                          // smooth refire

			//damages
			55,                             // damage
			0,                              // selfdamage ratio
			90,                             // knockback
			500,                            // stun
			90,                             // splash radius
			0,                              // splash fraction
			55,                             // minimum damage
			35,                             // minimum knockback

			//projectile def
			775,                            // speed
			0,                              // spread
			0,                              // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			0,                              // weapon pickup amount
			0,                              // pickup amount
			0,                              // max amount
			0                               // low ammo threshold
		},
	},

	{
		"Instagun",
		WEAP_INSTAGUN,
		{
			FIRE_MODE_STRONG,
			AMMO_INSTAS,
			1,                              // ammo usage per shot
			1,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			1300,                           // reload frametime
			0,                              // cooldown frametime
			8024,                           // range
			false,                          // smooth refire

			//damages
			200,                            // damage
			0.1,                            // selfdamage ratio (ig cant damage)
			95,                             // knockback
			1000,                           // stun
			80,                             // splash radius
			0,                              // splash fraction
			0,                              // splash minimum damage
			0,                              // splash minimum knockback

			//projectile def
			INSTANT,                        // speed
			0,                              // spread
			0,                              // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			5,                              // weapon pickup amount
			5,                              // pickup amount
			5,                              // max amount
			0                               // low ammo threshold
		},

		{
			FIRE_MODE_WEAK,
			AMMO_WEAK_INSTAS,
			1,                              // ammo usage per shot
			1,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			1300,                           // reload frametime
			0,                              // cooldown frametime
			8024,                           // range
			false,                          // smooth refire

			//damages
			200,                            // damage
			0.1,                            // selfdamage ratio (ig cant damage)
			95,                             // knockback
			1000,                           // stun
			80,                             // splash radius
			0,                              // splash fraction
			0,                              // splash minimum damage
			0,                              // splash minimum knockback

			//projectile def
			INSTANT,                        // speed
			0,                              // spread
			0,                              // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			5,                              // weapon pickup amount
			5,                              // pickup amount
			15,                             // max amount
			0                               // low ammo threshold
		},
	},
};

gs_weapon_definition_t gs_raceWeaponDefs[] =
{
	{NULL},

	{
		"Gunblade",
		WEAP_GUNBLADE,
		{
			FIRE_MODE_STRONG,
			AMMO_GUNBLADE,
			1,                              // ammo usage per shot
			1,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			600,                            // reload frametime
			0,                              // cooldown frametime
			5000,                           // projectile timeout
			false,                          // smooth refire

			//damages
			35,                             // damage
			1.0,                            // selfdamage ratio
			60,                             // knockback (race specific)
			0,                              // stun
			80,                             // splash radius
			1.3,                            // splash fraction (race specific)
			8,                              // splash minimum damage
			10,                             // splash minimum knockback

			//projectile def
			3000,                           // speed
			0,                              // spread
			0,                              // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			0,                              // weapon pickup amount
			0,                              // pickup amount
			1,                              // max amount
			0                               // low ammo threshold
		},

		{
			FIRE_MODE_WEAK,
			AMMO_WEAK_GUNBLADE,
			0,                              // ammo usage per shot
			0,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			600,                            // reload frametime
			0,                              // cooldown frametime
			64,                             // projectile timeout  / projectile range for instant weapons
			false,                          // smooth refire

			//damages
			50,                             // damage
			0,                              // selfdamage ratio
			50,                             // knockback
			0,                              // stun
			0,                              // splash radius
			0,                              // splash fraction
			0,                              // splash minimum damage
			0,                              // splash minimum knockback

			//projectile def
			0,                              // speed
			0,                              // spread
			0,                              // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			0,                              // weapon pickup amount
			0,                              // pickup amount
			0,                              // max amount
			0                               // low ammo threshold
		},
	},

	{NULL},

	{NULL},

	{
		"Grenade Launcher",
		WEAP_GRENADELAUNCHER,
		{
			FIRE_MODE_STRONG,
			AMMO_GRENADES,
			1,                              // ammo usage per shot
			1,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			800,                            // reload frametime
			0,                              // cooldown frametime
			1650,                           // projectile timeout (race specific)
			false,                          // smooth refire

			//damages
			80,                             // damage
			1.00,                           // selfdamage ratio
			120,                                // knockback (race specific)
			1250,                           // stun
			170,                            // splash radius (race specific)
			2.5,                            // splash fraction (race specific)
			15,                             // splash minimum damage
			1,                              // splash minimum knockback (race specific)

			//projectile def
			800,                            // speed (race specific)
			0,                              // spread
			0,                              // v_spread
			1.25,                           // friction (race specific)
			1.22,                           // gravity (race specific)
			24,                             // prestep (race specific)

			//ammo
			10,                             // weapon pickup amount
			10,                             // pickup amount
			20,                             // max amount
			3                               // low ammo threshold
		},

		{
			FIRE_MODE_WEAK,
			AMMO_WEAK_GRENADES,
			1,                              // ammo usage per shot
			1,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			800,                            // reload frametime
			0,                              // cooldown frametime
			1250,                           // projectile timeout
			false,                          // smooth refire

			//damages
			80,                             // damage
			1.00,                           // selfdamage ratio
			100,                                // knockback
			1250,                           // stun
			135,                            // splash radius
			0,                              // splash fraction
			15,                             // splash minimum damage
			35,                             // splash minimum knockback

			//projectile def
			1000,                           // speed
			0,                              // spread
			0,                              // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			0,                              // weapon pickup amount
			0,                              // pickup amount
			0,                              // max amount
			0                               // low ammo threshold
		},
	},

	{
		"Rocket Launcher",
		WEAP_ROCKETLAUNCHER,
		{
			FIRE_MODE_STRONG,
			AMMO_ROCKETS,
			1,                              // ammo usage per shot
			1,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			850,                            // reload frametime (race specific)
			0,                              // cooldown frametime
			100000,                          // projectile timeout (race specific)
			false,                          // smooth refire

			//damages
			80,                             // damage
			1.00,                           // selfdamage ratio
			108,                                // knockback (race specific)
			1250,                           // stun
			120,                            // splash radius (race specific)
			1,                              // splash fraction (race specific)
			15,                             // splash minimum damage
			1,                              // splash minimum knockback (race specific)

			//projectile def
			950,                            // speed (race specific)
			0,                              // spread
			0,                              // v_spread
			0,                              // friction
			0,                              // gravity
			10,                             // prestep (race specific)

			//ammo
			5,                              // weapon pickup amount
			10,                             // pickup amount
			20,                             // max amount
			3                               // low ammo threshold
		},

		{
			FIRE_MODE_WEAK,
			AMMO_WEAK_ROCKETS,
			1,                              // ammo usage per shot
			1,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			850,                            // reload frametime (race specific)
			0,                              // cooldown frametime
			100000,                          // projectile timeout (race specific)
			false,                          // smooth refire

			//damages
			80,                             // damage
			1.00,                           // selfdamage ratio
			100,                                // knockback
			1250,                           // stun
			135,                            // splash radius
			0,                              // splash fraction
			15,                             // splash minimum damage
			35,                             // splash minimum knockback

			//projectile def
			1150,                           // speed
			0,                              // spread
			0,                              // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			0,                              // weapon pickup amount
			0,                              // pickup amount
			0,                              // max amount
			0                               // low ammo threshold
		},
	},

	{
		"Plasmagun",
		WEAP_PLASMAGUN,
		{
			FIRE_MODE_STRONG,
			AMMO_PLASMA,
			1,                              // ammo usage per shot
			1,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			100,                            // reload frametime
			0,                              // cooldown frametime
			5000,                           // projectile timeout
			false,                          // smooth refire

			//damages
			15,                             // damage
			0.5,                            // selfdamage ratio
			24,                             // knockback (race specific)
			200,                            // stun
			40,                             // splash radius (race specific)
			1,                              // splash fraction (race specific)
			5,                              // splash minimum damage (race specific)
			1,                              // splash minimum knockback

			//projectile def
			1700,                           // speed (race specific)
			0,                              // spread
			0,                              // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep (race specific)

			//ammo
			50,                             // weapon pickup amount
			100,                            // pickup amount
			150,                            // max amount
			20                              // low ammo threshold
		},

		{
			FIRE_MODE_WEAK,
			AMMO_WEAK_PLASMA,
			1,                              // ammo usage per shot
			1,                              // projectiles fired each shot

			//timings (in msecs)
			WEAPONUP_FRAMETIME,             // weapon up frametime
			WEAPONDOWN_FRAMETIME,           // weapon down frametime
			100,                            // reload frametime
			0,                              // cooldown frametime
			5000,                           // projectile timeout
			false,                          // smooth refire

			//damages
			15,                             // damage
			0.5,                            // selfdamage ratio
			20,                             // knockback
			200,                            // stun
			45,                             // splash radius
			0,                              // splash fraction
			5,                              // splash minimum damage
			1,                              // splash minimum knockback

			//projectile def
			2500,                           // speed
			0,                              // spread
			0,                              // v_spread
			0,                              // friction
			0,                              // gravity
			0,                              // prestep

			//ammo
			0,                              // weapon pickup amount
			0,                              // pickup amount
			0,                              // max amount
			0                               // low ammo threshold
		},
	},

	{NULL},

	{NULL},

	{NULL},

	{NULL},
};

#define GS_NUMWEAPONDEFS ( sizeof( gs_weaponDefs ) / sizeof( gs_weapon_definition_t ) )

/*
* GS_GetWeaponDef
*/
gs_weapon_definition_t *GS_GetWeaponDef( int weapon ) {
	assert( GS_NUMWEAPONDEFS == WEAP_TOTAL );
	assert( weapon >= 0 && weapon < WEAP_TOTAL );
	if( GS_RaceGametype() && gs_raceWeaponDefs[weapon].name != NULL ){
		return &gs_raceWeaponDefs[weapon];
	} else {
		return &gs_weaponDefs[weapon];
	}
}

/*
* GS_GetWeaponDefExt
*/
gs_weapon_definition_t *GS_GetWeaponDefExt( int weapon, bool race ) {
	if( race && gs_raceWeaponDefs[weapon].name != NULL ) {
		return &gs_raceWeaponDefs[weapon];
	} else {
		return &gs_weaponDefs[weapon];
	}
}

/*
* GS_InitWeapons
*/
void GS_InitWeapons( void ) {
	int i;
	gsitem_t *item;
	gs_weapon_definition_t *weapondef;

	for( i = WEAP_GUNBLADE; i < WEAP_TOTAL; i++ ) {
		item = GS_FindItemByTag( i );
		weapondef = GS_GetWeaponDef( i );

		assert( item && weapondef );

		// hack : use the firedef pickup counts on items
		if( item->weakammo_tag && GS_FindItemByTag( item->weakammo_tag ) ) {
			GS_FindItemByTag( item->weakammo_tag )->quantity = weapondef->firedef_weak.ammo_pickup;
			GS_FindItemByTag( item->weakammo_tag )->inventory_max = weapondef->firedef_weak.ammo_max;
		}

		if( item->ammo_tag && GS_FindItemByTag( item->ammo_tag ) ) {
			GS_FindItemByTag( item->ammo_tag )->quantity = weapondef->firedef.ammo_pickup;
			GS_FindItemByTag( item->ammo_tag )->inventory_max = weapondef->firedef.ammo_max;
		}
	}
}
