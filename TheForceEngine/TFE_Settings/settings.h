#pragma once
//////////////////////////////////////////////////////////////////////
// The Force Engine Settings
// Xbox port: in-class member initializers replaced with constructors
// (MSVC 2005 / C++03 does not support in-class initializers).
// STL-heavy mod-override structs guarded by #ifndef _XBOX.
//////////////////////////////////////////////////////////////////////

#include <TFE_System/types.h>
#include <TFE_System/iniParser.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_Audio/midiDevice.h>
#include "gameSourceData.h"

#ifndef _XBOX
#include <map>
#include <string>
#include <vector>
using std::string;
#endif

// ---------------------------------------------------------------------------
enum SkyMode
{
    SKYMODE_VANILLA = 0,
    SKYMODE_CYLINDER,
    SKYMODE_COUNT
};

enum ColorMode
{
    COLORMODE_8BIT = 0,
    COLORMODE_8BIT_INTERP,
    COLORMODE_TRUE_COLOR,
    COLORMODE_COUNT,
};

static const char* c_tfeSkyModeStrings[] =
{
    "Vanilla",
    "Cylinder",
};

// ---------------------------------------------------------------------------
// Temporary settings (not serialized).
// ---------------------------------------------------------------------------
struct TFE_Settings_Temp
{
    bool skipLoadDelay;
    bool forceFullscreen;
    bool df_demologging;
    bool exit_after_replay;

    TFE_Settings_Temp()
        : skipLoadDelay(false), forceFullscreen(false)
        , df_demologging(false), exit_after_replay(false) {}
};

// ---------------------------------------------------------------------------
struct TFE_Settings_Window
{
    s32  x, y;
    u32  width, height;
    u32  baseWidth, baseHeight;
    bool fullscreen;

    TFE_Settings_Window()
        : x(0), y(64), width(1280), height(720)
        , baseWidth(1280), baseHeight(720), fullscreen(true) {}
};

// ---------------------------------------------------------------------------
struct TFE_Settings_Graphics
{
    Vec2i gameResolution;
    bool  widescreen;
    bool  asyncFramebuffer;
    bool  gpuColorConvert;
    bool  colorCorrection;
    bool  perspectiveCorrectTexturing;
    bool  extendAjoinLimits;
    bool  vsync;
    bool  showFps;
    bool  fix3doNormalOverflow;
    bool  ignore3doLimits;
    bool  forceGouraudShading;
    bool  overrideLighting;
    bool  useSmoothDeltaTime;
    s32   frameRateLimit;
    f32   brightness;
    f32   contrast;
    f32   saturation;
    f32   gamma;
    s32   fov;
    s32   rendererIndex;
    s32   colorMode;

    // 8-bit options.
    bool ditheredBilinear;

    // True-color options.
    bool useBilinear;
    bool useMipmapping;
    f32  bilinearSharpness;
    f32  anisotropyQuality;

    // Reticle
    bool reticleEnable;
    s32  reticleIndex;
    f32  reticleRed;
    f32  reticleGreen;
    f32  reticleBlue;
    f32  reticleOpacity;
    f32  reticleScale;

    // Bloom
    bool bloomEnabled;
    f32  bloomStrength;
    f32  bloomSpread;

    // Sky
    s32  skyMode;

    TFE_Settings_Graphics()
        : widescreen(false), asyncFramebuffer(true), gpuColorConvert(true)
        , colorCorrection(false), perspectiveCorrectTexturing(false)
        , extendAjoinLimits(true), vsync(true), showFps(false)
        , fix3doNormalOverflow(true), ignore3doLimits(true)
        , forceGouraudShading(false), overrideLighting(false)
        , useSmoothDeltaTime(true), frameRateLimit(240)
        , brightness(1.0f), contrast(1.0f), saturation(1.0f), gamma(1.0f)
        , fov(90), rendererIndex(0), colorMode(COLORMODE_8BIT)
        , ditheredBilinear(false), useBilinear(false), useMipmapping(false)
        , bilinearSharpness(1.0f), anisotropyQuality(1.0f)
        , reticleEnable(false), reticleIndex(6)
        , reticleRed(0.25f), reticleGreen(1.0f), reticleBlue(0.25f)
        , reticleOpacity(1.0f), reticleScale(1.0f)
        , bloomEnabled(false), bloomStrength(0.4f), bloomSpread(0.6f)
        , skyMode(SKYMODE_CYLINDER)
    {
        gameResolution.x = 320;
        gameResolution.z = 200;
    }
};

// ---------------------------------------------------------------------------
struct TFE_Settings_Enhancements
{
    bool enableHdTextures;
    bool enableHdSprites;
    bool enableHdHud;

    TFE_Settings_Enhancements()
        : enableHdTextures(false), enableHdSprites(false), enableHdHud(false) {}
};

// ---------------------------------------------------------------------------
enum TFE_HudScale
{
    TFE_HUDSCALE_PROPORTIONAL = 0,
    TFE_HUDSCALE_SCALED,
};

enum TFE_HudPosition
{
    TFE_HUDPOS_EDGE = 0,
    TFE_HUDPOS_4_3,
};

enum PitchLimit
{
    PITCH_VANILLA = 0,
    PITCH_VANILLA_PLUS,
    PITCH_HIGH,
    PITCH_MAXIMUM,
    PITCH_COUNT
};

enum FontSize
{
    FONT_SMALL,
    FONT_MEDIUM,
    FONT_LARGE,
    FONT_XL
};

static const char* c_tfeHudScaleStrings[] = { "Proportional", "Scaled" };
static const char* c_tfeHudPosStrings[]   = { "Edge", "4:3" };
static const char* c_tfePitchLimit[]      =
{
    "Vanilla  (45 degrees)",
    "Vanilla+ (60 degrees)",
    "High     (75 degrees)",
    "Maximum"
};

struct TFE_Settings_Hud
{
    TFE_HudScale   hudScale;
    TFE_HudPosition hudPos;
    f32 scale;
    s32 pixelOffset[3];

    TFE_Settings_Hud()
        : hudScale(TFE_HUDSCALE_PROPORTIONAL), hudPos(TFE_HUDPOS_EDGE), scale(1.0f)
    {
        pixelOffset[0] = pixelOffset[1] = pixelOffset[2] = 0;
    }
};

// ---------------------------------------------------------------------------
struct TFE_Settings_Sound
{
    f32  masterVolume;
    f32  soundFxVolume;
    f32  musicVolume;
    f32  cutsceneSoundFxVolume;
    f32  cutsceneMusicVolume;
    s32  audioDevice;
    s32  midiOutput;
    s32  midiType;
    bool use16Channels;
    bool disableSoundInMenus;

    TFE_Settings_Sound()
        : masterVolume(1.0f), soundFxVolume(0.75f), musicVolume(1.0f)
        , cutsceneSoundFxVolume(0.9f), cutsceneMusicVolume(1.0f)
        , audioDevice(-1), midiOutput(-1), midiType(MIDI_TYPE_DEFAULT)
        , use16Channels(false), disableSoundInMenus(false) {}
};

// ---------------------------------------------------------------------------
struct TFE_Game
{
    char   game[64];
    GameID id;

    TFE_Game() : id(Game_Dark_Forces)
    {
        strcpy(game, "Dark Forces");
    }
};

struct TFE_GameHeader
{
    char gameName[64];
    char sourcePath[TFE_MAX_PATH];
    char emulatorPath[TFE_MAX_PATH];

    TFE_GameHeader()
    {
        gameName[0]    = 0;
        sourcePath[0]  = 0;
        emulatorPath[0]= 0;
    }
};

// ---------------------------------------------------------------------------
struct TFE_Settings_Game
{
    TFE_GameHeader header[Game_Count];

    s32  df_airControl;
    bool df_bobaFettFacePlayer;
    bool df_smoothVUEs;
    bool df_disableFightMusic;
    bool df_enableAutoaim;
    bool df_showSecretFoundMsg;
    bool df_showSecretCount;
    bool df_centerHudPosition;
    bool df_autorun;
    bool df_crouchToggle;
    bool df_ignoreInfLimit;
    bool df_stepSecondAlt;
    bool df_solidWallFlagFix;
    bool df_enableUnusedItem;
    bool df_jsonAiLogics;
    bool df_enableRecording;
    bool df_enableRecordingAll;
    bool df_enableReplay;
    bool df_showReplayCounter;
    bool df_demologging;
    bool df_autoEndMission;
    bool df_showKeyColors;
    bool df_showMapSecrets;
    bool df_showMapObjects;
    s32  df_recordFrameRate;
    s32  df_playbackFrameRate;
    bool df_showKeyUsed;
    PitchLimit df_pitchLimit;

    TFE_Settings_Game()
        : df_airControl(0), df_bobaFettFacePlayer(false), df_smoothVUEs(false)
        , df_disableFightMusic(false), df_enableAutoaim(true)
        , df_showSecretFoundMsg(false), df_showSecretCount(true)
        , df_centerHudPosition(false), df_autorun(false), df_crouchToggle(false)
        , df_ignoreInfLimit(true), df_stepSecondAlt(false)
        , df_solidWallFlagFix(true), df_enableUnusedItem(true)
        , df_jsonAiLogics(true), df_enableRecording(false)
        , df_enableRecordingAll(false), df_enableReplay(false)
        , df_showReplayCounter(false), df_demologging(false)
        , df_autoEndMission(false), df_showKeyColors(false)
        , df_showMapSecrets(true), df_showMapObjects(true)
        , df_recordFrameRate(4), df_playbackFrameRate(2)
        , df_showKeyUsed(true), df_pitchLimit(PITCH_VANILLA_PLUS) {}
};

// ---------------------------------------------------------------------------
struct TFE_Settings_System
{
    bool gameQuitExitsToMenu;
    bool returnToModLoader;
    f32  gifRecordingFramerate;
    bool showGifPathConfirmation;
    f32  xboxLookSensitivity;
    f32  xboxStickDeadzone;
    f32  xboxLookSensitivityX;
    f32  xboxLookSensitivityY;
    f32  xboxRightStickDeadzone;
    s32  xboxSafeZonePercent;
    s32  xboxSafeZoneWidthPercent;
    s32  xboxSafeZoneHeightPercent;
    s32  xboxSafeZoneOffsetX;
    s32  xboxSafeZoneOffsetY;
    s32  xboxVideoMode;

    TFE_Settings_System()
        : gameQuitExitsToMenu(true), returnToModLoader(true)
        , gifRecordingFramerate(18.0f), showGifPathConfirmation(true)
        , xboxLookSensitivity(1.0f), xboxStickDeadzone(0.09f)
        , xboxLookSensitivityX(1.0f), xboxLookSensitivityY(1.0f)
        , xboxRightStickDeadzone(0.09f)
        , xboxSafeZonePercent(100), xboxSafeZoneWidthPercent(100)
        , xboxSafeZoneHeightPercent(100), xboxSafeZoneOffsetX(0)
        , xboxSafeZoneOffsetY(0), xboxVideoMode(0) {}
};

// ---------------------------------------------------------------------------
// A11y settings - std::string members guarded on Xbox
// ---------------------------------------------------------------------------
struct TFE_Settings_A11y
{
#ifndef _XBOX
    string language;
    string lastFontPath;
#endif
    bool  showCutsceneSubtitles;
    bool  showCutsceneCaptions;
    FontSize cutsceneFontSize;
    RGBA  cutsceneFontColor;
    f32   cutsceneTextBackgroundAlpha;
    bool  showCutsceneTextBorder;
    f32   cutsceneTextSpeed;

    bool  showGameplaySubtitles;
    bool  showGameplayCaptions;
    FontSize gameplayFontSize;
    RGBA  gameplayFontColor;
    int   gameplayMaxTextLines;
    f32   gameplayTextBackgroundAlpha;
    bool  showGameplayTextBorder;
    f32   gameplayTextSpeed;
    s32   gameplayCaptionMinVolume;

    bool captionSystemEnabled()
    {
        return showCutsceneSubtitles || showCutsceneCaptions
            || showGameplaySubtitles || showGameplayCaptions;
    }

    bool enableHeadwave;
    bool disableScreenFlashes;
    bool disablePlayerWeaponLighting;

    TFE_Settings_A11y()
        : showCutsceneSubtitles(false), showCutsceneCaptions(false)
        , cutsceneFontSize(FONT_MEDIUM)
        , cutsceneFontColor(RGBA::fromFloats(1.0f, 1.0f, 1.0f))
        , cutsceneTextBackgroundAlpha(0.75f), showCutsceneTextBorder(true)
        , cutsceneTextSpeed(1.0f)
        , showGameplaySubtitles(false), showGameplayCaptions(false)
        , gameplayFontSize(FONT_MEDIUM)
        , gameplayFontColor(RGBA::fromFloats(1.0f, 1.0f, 1.0f))
        , gameplayMaxTextLines(3), gameplayTextBackgroundAlpha(0.0f)
        , showGameplayTextBorder(false), gameplayTextSpeed(1.0f)
        , gameplayCaptionMinVolume(32)
        , enableHeadwave(true), disableScreenFlashes(false)
        , disablePlayerWeaponLighting(false)
    {
#ifndef _XBOX
        language = "en";
#endif
    }
};

// ---------------------------------------------------------------------------
// Mod override types - excluded from Xbox build (no mod system)
// ---------------------------------------------------------------------------
enum ModSettingOverride
{
    MSO_NOT_SET = 0,
    MSO_TRUE,
    MSO_FALSE,
    MSO_COUNT
};

enum HdAssetType
{
    HD_ASSET_TYPE_BM = 0,
    HD_ASSET_TYPE_FME,
    HD_ASSET_TYPE_WAX,
    HD_ASSET_TYPE_COUNT
};

#ifndef _XBOX

struct ModHdIgnoreList
{
    std::string levName;
    std::vector<std::string> bmIgnoreList;
    std::vector<std::string> fmeIgnoreList;
    std::vector<std::string> waxIgnoreList;
};

static const char* modIntOverrides[] =
{
    "energy","power","plasma","detonator","shell","mine","missile",
    "shields","health","lives","battery","defaultWeapon","fogLevel",
    "floorDamageLow","floorDamageHigh","gasDamage","wallDamage","gravity",
    "projectileGravity","shieldSuperchargeDuration","weaponSuperchargeDuration",
};

static const char* modFloatOverrides[] =
{
    "headlampBatteryConsumption","gogglesBatteryConsumption","maskBatteryConsumption",
};

static const char* modBoolOverrides[] =
{
    "enableMask","enableCleats","enableNightVision","enableHeadlamp",
    "pistol","rifle","autogun","mortar","fusion","concussion","cannon",
    "mask","goggles","cleats","plans","phrik","datatape","nava","dtWeapon",
    "code1","code2","code3","code4","code5",
    "yellowKey","redKey","blueKey","bryarOnly"
};

static const char* modTextureOverrides[] = { "loadScreen" };

struct ModSettingLevelOverride
{
    std::string levName;
    std::map<std::string, int>          intOverrideMap;
    std::map<std::string, float>        floatOverrideMap;
    std::map<std::string, bool>         boolOverrideMap;
    std::map<std::string, TextureData*> textureOverrideMap;
};

struct TFE_ModSettings
{
    ModSettingOverride ignoreInfLimits;
    ModSettingOverride stepSecondAlt;
    ModSettingOverride solidWallFlagFix;
    ModSettingOverride extendAjoinLimits;
    ModSettingOverride ignore3doLimits;
    ModSettingOverride normalFix3do;
    ModSettingOverride enableUnusedItem;
    ModSettingOverride jsonAiLogics;

    std::map<std::string, ModSettingLevelOverride> levelOverrides;
    std::vector<ModHdIgnoreList> ignoreList;

    TFE_ModSettings()
        : ignoreInfLimits(MSO_NOT_SET), stepSecondAlt(MSO_NOT_SET)
        , solidWallFlagFix(MSO_NOT_SET), extendAjoinLimits(MSO_NOT_SET)
        , ignore3doLimits(MSO_NOT_SET), normalFix3do(MSO_NOT_SET)
        , enableUnusedItem(MSO_NOT_SET), jsonAiLogics(MSO_NOT_SET) {}
};

#else // _XBOX - minimal ModSetting stubs

struct ModHdIgnoreList {};

// Minimal stub satisfying mission.cpp / player.cpp call sites.
// getLevelOverrides always returns NULL on Xbox so body is never reached.
struct ModSettingLevelOverrideLevName
{
    char buf[64];
    ModSettingLevelOverrideLevName() { buf[0] = 0; }
    bool empty() const { return buf[0] == 0; }
};

// Dummy map - always returns NULL for TextureData* lookups.
struct ModSettingTextureMap
{
    TextureData* operator[](const char* /*key*/) { return NULL; }
};

struct ModSettingLevelOverride
{
    ModSettingLevelOverrideLevName levName;
    ModSettingTextureMap           textureOverrideMap;
};

struct TFE_ModSettings
{
    ModSettingOverride ignoreInfLimits;
    ModSettingOverride stepSecondAlt;
    ModSettingOverride solidWallFlagFix;
    ModSettingOverride extendAjoinLimits;
    ModSettingOverride ignore3doLimits;
    ModSettingOverride normalFix3do;
    ModSettingOverride enableUnusedItem;
    ModSettingOverride jsonAiLogics;

    TFE_ModSettings()
        : ignoreInfLimits(MSO_NOT_SET), stepSecondAlt(MSO_NOT_SET)
        , solidWallFlagFix(MSO_NOT_SET), extendAjoinLimits(MSO_NOT_SET)
        , ignore3doLimits(MSO_NOT_SET), normalFix3do(MSO_NOT_SET)
        , enableUnusedItem(MSO_NOT_SET), jsonAiLogics(MSO_NOT_SET) {}
};

#endif // _XBOX

// ---------------------------------------------------------------------------
// TFE_Settings namespace
// ---------------------------------------------------------------------------
namespace TFE_Settings
{
    bool init(bool& firstRun);
    void shutdown();

    bool writeToDisk(bool writeDefaultSettings = false);

    TFE_Settings_Window*      getWindowSettings();
    TFE_Settings_Graphics*    getGraphicsSettings();
    TFE_Settings_Enhancements*getEnhancementsSettings();
    TFE_Settings_Hud*         getHudSettings();
    TFE_Settings_Sound*       getSoundSettings();
    TFE_Settings_System*      getSystemSettings();
    TFE_Settings_Temp*        getTempSettings();
    TFE_Game*                 getGame();
    TFE_GameHeader*           getGameHeader(const char* gameName);
    TFE_Settings_Game*        getGameSettings();
    TFE_Settings_A11y*        getA11ySettings();
    TFE_ModSettings*          getModSettings();

    void setLevelName(const char* levelName);
    bool isHdAssetValid(const char* assetName, HdAssetType type);

    bool ignoreInfLimits();
    bool stepSecondAlt();
    bool solidWallFlagFix();
    bool extendAdjoinLimits();
    bool ignore3doLimits();
    bool normalFix3do();
    bool enableUnusedItem();
    bool jsonAiLogics();

#ifndef _XBOX
    ModSettingLevelOverride* getLevelOverrides(string levelName);
#else
    // On Xbox, no mod support - always returns NULL.
    inline ModSettingLevelOverride* getLevelOverrides(const char* /*levelName*/) { return NULL; }
#endif

    bool validatePath(const char* path, const char* sentinel);
    void autodetectGamePaths();
    void clearModSettings();
    void loadCustomModSettings();
    void parseIniFile(const char* buffer, size_t len);
    void writeDarkForcesGameSettings(FileStream& settings);
    void resetGameSettings();
    void resetAllSettings();
}
