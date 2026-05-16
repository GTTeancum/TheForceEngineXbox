#pragma once
#include <TFE_System/types.h>
#include <TFE_Jedi/Math/fixedPoint.h>

///////////////////////////////////////////
// TFE Externalised Pickup data
///////////////////////////////////////////

namespace TFE_ExternalData
{
	struct MaxAmounts
	{
#ifndef _XBOX
		s32 ammoEnergyMax = 500;
		s32 ammoPowerMax = 500;
		s32 ammoShellMax = 50;
		s32 ammoPlasmaMax = 400;
		s32 ammoDetonatorMax = 50;
		s32 ammoMineMax = 30;
		s32 ammoMissileMax = 20;
		s32 shieldsMax = 200;
		s32 batteryPowerMax = 2 * ONE_16;
		s32 healthMax = 100;
#else
		s32 ammoEnergyMax;
		s32 ammoPowerMax;
		s32 ammoShellMax;
		s32 ammoPlasmaMax;
		s32 ammoDetonatorMax;
		s32 ammoMineMax;
		s32 ammoMissileMax;
		s32 shieldsMax;
		s32 batteryPowerMax;
		s32 healthMax;
#endif
	};

	struct ExternalPickup
	{
#ifndef _XBOX
		const char* name = nullptr;
#else
		const char* name;
#endif
		s32 type;
#ifndef _XBOX
		s32 weaponIndex = -1;
		JBool* playerItem = nullptr;
		s32* playerAmmo = nullptr;
		s32 amount = 0;
		s32 message1 = -1;
		s32 message2 = -1;
		bool fullBright = false;
		bool noRemove = false;
		const char* asset = "";
#else
		s32 weaponIndex;
		JBool* playerItem;
		s32* playerAmmo;
		s32 amount;
		s32 message1;
		s32 message2;
		bool fullBright;
		bool noRemove;
		const char* asset;
#endif
	};

	
	MaxAmounts* getMaxAmounts();
	ExternalPickup* getExternalPickups();
	void clearExternalPickups();
	void loadExternalPickups();
	void parseExternalPickups(char* data, bool fromMod);
	bool validateExternalPickups();
}