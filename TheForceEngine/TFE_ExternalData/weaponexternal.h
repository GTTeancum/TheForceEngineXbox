#pragma once
#include <TFE_System/types.h>
#include <TFE_DarkForces/player.h>

///////////////////////////////////////////
// TFE Externalised Weapon data
///////////////////////////////////////////

namespace TFE_ExternalData
{
	enum
	{
		WEAPON_NUM_TEXTURES = 16,
		WEAPON_NUM_ANIMFRAMES = 16,
	};

	struct ExternalProjectile
	{
#ifndef _XBOX
		const char* type = nullptr;

		// Projectile object
		const char* assetType = "spirit";
		const char* asset = "";
		bool fullBright = false;
		bool zeroWidth = false;
		bool autoAim = false;
		bool movable = false;

		// Projectile logic
		const char* updateFunc = "";
#else
		const char* type;

		// Projectile object
		const char* assetType;
		const char* asset;
		bool fullBright;
		bool zeroWidth;
		bool autoAim;
		bool movable;

		// Projectile logic
		const char* updateFunc;
#endif
		u32 damage;
		u32 falloffAmount;
		u32 nextFalloffTick;
		u32 damageFalloffDelta;
		u32 minDamage;
		u32 force;
		u32 speed;
		u32 horzBounciness;
		u32 vertBounciness;
		s32 bounceCount;
		u32 reflectVariation;
		u32 duration;
		s32 homingAngularSpeed;
#ifndef _XBOX
		const char* flightSound = "";
		const char* reflectSound = "";
		const char* cameraPassSound = "";
		s32 reflectEffectId = -1;
		s32 hitEffectId = -1;
		bool explodeOnTimeout = false;
#else
		const char* flightSound;
		const char* reflectSound;
		const char* cameraPassSound;
		s32 reflectEffectId;
		s32 hitEffectId;
		bool explodeOnTimeout;
#endif
	};

	struct ExternalEffect
	{
#ifndef _XBOX
		const char* type = nullptr;
		const char* wax = "";
#else
		const char* type;
		const char* wax;
#endif
		s32 force;
		s32 damage;
		s32 explosiveRange;
		s32 wakeupRange;
#ifndef _XBOX
		const char* soundEffect = "";
#else
		const char* soundEffect;
#endif
		s32 soundPriority;
	};

	// Maps to TFE_DarkForces::WeaponAnimFrame
	struct WeaponAnimFrame
	{
#ifndef _XBOX
		s32 texture = 0;
		s32 light = 0;
		u32 durationSupercharge = 0;
		u32 durationNormal = 0;
#else
		s32 texture;
		s32 light;
		u32 durationSupercharge;
		u32 durationNormal;
#endif
	};
	
	struct ExternalWeapon
	{
#ifndef _XBOX
		const char* name = nullptr;
		s32 frameCount = 1;
		const char* textures[WEAPON_NUM_TEXTURES] = { "default.bm" };
		s32 xPos[WEAPON_NUM_TEXTURES] = { 0 };
		s32 yPos[WEAPON_NUM_TEXTURES] = { 0 };
		s32* ammo = &TFE_DarkForces::s_playerInfo.ammoEnergy;
		s32* secondaryAmmo = nullptr;
		s32 wakeupRange = 0;
		s32 variation = 0;

		s32 primaryFireConsumption = 1;
		s32 secondaryFireConsumption = 1;

		s32 numAnimFrames = 1;
#else
		const char* name;
		s32 frameCount;
		const char* textures[WEAPON_NUM_TEXTURES];
		s32 xPos[WEAPON_NUM_TEXTURES];
		s32 yPos[WEAPON_NUM_TEXTURES];
		s32* ammo;
		s32* secondaryAmmo;
		s32 wakeupRange;
		s32 variation;

		s32 primaryFireConsumption;
		s32 secondaryFireConsumption;

		s32 numAnimFrames;
#endif
		WeaponAnimFrame animFrames[WEAPON_NUM_ANIMFRAMES];
#ifndef _XBOX
		s32 numSecondaryAnimFrames = 1;
#else
		s32 numSecondaryAnimFrames;
#endif
		WeaponAnimFrame animFramesSecondary[WEAPON_NUM_ANIMFRAMES];
	};

	struct ExternalGasmask
	{
#ifndef _XBOX
		const char* texture = "gmask.bm";
		s32 xPos = 105;
		s32 yPos = 141;
#else
		const char* texture;
		s32 xPos;
		s32 yPos;
#endif
	};

	ExternalProjectile* getExternalProjectiles();
	ExternalEffect* getExternalEffects();
	ExternalWeapon* getExternalWeapons();
	ExternalGasmask* getExternalGasmask();
	void clearExternalProjectiles();
	void clearExternalEffects();
	void clearExternalWeapons();
	void loadExternalProjectiles();
	void parseExternalProjectiles(char* data, bool fromMod);
	bool validateExternalProjectiles();
	void loadExternalEffects();
	void parseExternalEffects(char* data, bool fromMod);
	bool validateExternalEffects();
	void loadExternalWeapons();
	void parseExternalWeapons(char* data, bool fromMod);
	bool validateExternalWeapons();
}