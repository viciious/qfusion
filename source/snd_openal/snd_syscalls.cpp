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

#include "snd_local.h"

sound_import_t SOUND_IMPORT;

#ifdef __cplusplus
extern "C" {
#endif

/*
* GetCGameAPI
*
* Returns a pointer to the structure with all entry points
*/
QF_DLL_EXPORT sound_export_t *GetSoundAPI( sound_import_t *import ) {
	static sound_export_t globals;

	SOUND_IMPORT = *import;

	globals.API = S_API;

	globals.Init = SF_Init;
	globals.Shutdown = SF_Shutdown;

	globals.BeginRegistration = SF_BeginRegistration;
	globals.EndRegistration = SF_EndRegistration;

	globals.StopAllSounds = SF_StopAllSounds;

	globals.Clear = SF_Clear;
	globals.Update = SF_Update;
	globals.Activate = SF_Activate;

	globals.SetAttenuationModel = SF_SetAttenuationModel;
	globals.SetEntitySpatialization = SF_SetEntitySpatialization;

	globals.RegisterSound = SF_RegisterSound;
	globals.StartFixedSound = SF_StartFixedSound;
	globals.StartRelativeSound = SF_StartRelativeSound;
	globals.StartGlobalSound = SF_StartGlobalSound;
	globals.StartLocalSoundByName = SF_StartLocalSoundByName;
	globals.StartLocalSound = SF_StartLocalSound;
	globals.AddLoopSound = SF_AddLoopSound;

	globals.RawSamples = SF_RawSamples;
	globals.PositionedRawSamples = SF_PositionedRawSamples;
	globals.GetRawSamplesLength = S_GetRawSamplesLength;
	globals.GetPositionedRawSamplesLength = S_GetPositionedRawSamplesLength;

	globals.StartBackgroundTrack = SF_StartBackgroundTrack;
	globals.StopBackgroundTrack = SF_StopBackgroundTrack;
	globals.LockBackgroundTrack = SF_LockBackgroundTrack;

	globals.BeginAviDemo = SF_BeginAviDemo;
	globals.StopAviDemo = SF_StopAviDemo;

	return &globals;
}

#ifdef __cplusplus
}
#endif