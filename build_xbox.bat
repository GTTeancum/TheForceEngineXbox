@echo off
REM ============================================================================
REM  build_xbox.bat - Build TheForceEngine Xbox port via XDK 5558 + VC71.
REM
REM  Mirrors OpenJKDF2's working build setup 1:1. Direct cl.exe / link.exe
REM  invocation - no vcproj. XDK 5849 is fallback for headers 5558 lacks
REM  (stdint.h, winsock2.h).
REM
REM  Toolchain rationale (from OpenJKDF2 build_xbox.bat):
REM    XDK 5558 has Xbox-correct D3DPT_*/D3DRS_*/D3DTSS_*/D3DFMT_* enum
REM    values in D3D8Types.h, and ships a full d3d8.lib (2.1 MB, 214
REM    D3DDevice exports) vs XDK 5849's 3.5 KB LTCG stub.
REM
REM  Usage: build_xbox.bat [clean]
REM ============================================================================

setlocal enabledelayedexpansion
cd /d "%~dp0"

REM -- Paths ------------------------------------------------------------------
set XDK_ROOT=C:\XDK_5558\XDK\xbox
set CC=%XDK_ROOT%\bin\vc71\CL.Exe
set LINK=%XDK_ROOT%\bin\vc71\Link.Exe
set IMAGEBLD=%XDK_ROOT%\bin\imagebld.exe
set SRCDIR=%~dp0TheForceEngine
set OBJDIR=%~dp0build\xbox\release\obj
set OUTDIR=%~dp0build\xbox\release
set OUTEXE=%OUTDIR%\default.exe
set BUILDLOG=%~dp0build_xbox.log
set PYTHON=python

if not exist "%CC%"       echo ERROR: missing %CC%       & exit /b 1
if not exist "%LINK%"     echo ERROR: missing %LINK%     & exit /b 1
if not exist "%IMAGEBLD%" echo ERROR: missing %IMAGEBLD% & exit /b 1

REM -- Clean ------------------------------------------------------------------
if "%1"=="clean" (
    echo Cleaning...
    if exist "%OBJDIR%" rd /s /q "%OBJDIR%"
    if exist "%OUTEXE%" del /q "%OUTEXE%"
    if exist "%BUILDLOG%" del /q "%BUILDLOG%"
    echo Done.
    goto :eof
)

REM -- Output dirs ------------------------------------------------------------
if not exist "%OBJDIR%" mkdir "%OBJDIR%"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"
if exist "%BUILDLOG%" del /q "%BUILDLOG%"

REM -- Compiler flags ---------------------------------------------------------
REM /MT static multi-threaded CRT; /O2 speed; /W2 warning level 2.
REM /FI force-includes xbox_compat.h (XDK macro killers + C++03 shims).
REM Include order: TFE root -> XDK 5558 -> XDK 5849 fallback.
set CFLAGS=/nologo /MT /O2 /W2
set CFLAGS=%CFLAGS% /I"%SRCDIR%" /I"%XDK_ROOT%\include" /I"C:\XDK\xbox\include"
set CFLAGS=%CFLAGS% /D_XBOX=1 /DWIN32 /DNDEBUG
set CFLAGS=%CFLAGS% /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_WARNINGS
set CFLAGS=%CFLAGS% /D_ITERATOR_DEBUG_LEVEL=0 /D_HAS_ITERATOR_DEBUGGING=0 /D_SECURE_SCL=0
set CFLAGS=%CFLAGS% /FI"%SRCDIR%\xbox_compat.h"
set CFLAGS=%CFLAGS% /wd4996 /wd4244 /wd4267 /wd4305 /wd4013 /wd4133 /wd4114 /wd4028

set ERRORS=0
set COMPILED=0
set TOTAL=0

echo.
echo ============================================================
echo  TheForceEngine Xbox build (VC71 + XDK 5558)
echo  Compiler: %CC%
echo  Output:   %OUTEXE%
echo ============================================================
echo.

REM -- Compile C files (compile as C with /Tc) --------------------------------
echo Compiling C files...
for %%F in (
    TheForceEngine\TFE_System\cJSON.c
) do (
    set /a TOTAL+=1
    set "SRC=%%F"
    for %%N in (%%~nF) do set "OBJ=%OBJDIR%\%%N.obj"
    echo   [C ] %%F
    "%CC%" /c /Tc "%~dp0%%F" %CFLAGS% /Fo"!OBJ!" >> "%BUILDLOG%" 2>&1
    if errorlevel 1 (
        echo       *** FAILED: %%F
        set /a ERRORS+=1
    ) else (
        set /a COMPILED+=1
    )
)

REM -- Compile C++ files (and .c files compiled as C++ via /Tp) ---------------
echo.
echo Compiling C++ files...
for %%F in (
    TheForceEngine\TFE_Archive\archive.cpp
    TheForceEngine\TFE_Archive\gobArchive.cpp
    TheForceEngine\TFE_Archive\gobMemoryArchive.cpp
    TheForceEngine\TFE_Archive\labArchive.cpp
    TheForceEngine\TFE_Archive\lfdArchive.cpp
    TheForceEngine\TFE_Archive\zipArchive.cpp
    TheForceEngine\TFE_Archive\zip\zip.c
    TheForceEngine\TFE_Archive\zstdCompression.cpp
    TheForceEngine\TFE_Asset\assetSystem.cpp
    TheForceEngine\TFE_Asset\colormapAsset.cpp
    TheForceEngine\TFE_Asset\dfKeywords.cpp
    TheForceEngine\TFE_Asset\fontAsset.cpp
    TheForceEngine\TFE_Asset\gameMessages.cpp
    TheForceEngine\TFE_Asset\gifWriter.cpp
    TheForceEngine\TFE_Asset\gmidAsset.cpp
    TheForceEngine\TFE_Asset\imageAsset.cpp
    TheForceEngine\TFE_Asset\levelList.cpp
    TheForceEngine\TFE_Asset\modelAsset_jedi.cpp
    TheForceEngine\TFE_Asset\paletteAsset.cpp
    TheForceEngine\TFE_Asset\spriteAsset.cpp
    TheForceEngine\TFE_Asset\spriteAsset_Jedi.cpp
    TheForceEngine\TFE_Asset\textureAsset.cpp
    TheForceEngine\TFE_Asset\vocAsset.cpp
    TheForceEngine\TFE_Asset\vueAsset.cpp
    TheForceEngine\TFE_Audio\MidiSynth\fm4Opl3Device.cpp
    TheForceEngine\TFE_Audio\MidiSynth\opl3.c
    TheForceEngine\TFE_Audio\MidiSynth\soundFontDevice.cpp
    TheForceEngine\TFE_Audio\audioDevice_xbox.cpp
    TheForceEngine\TFE_Audio\audioFilters.cpp
    TheForceEngine\TFE_Audio\audioSystem.cpp
    TheForceEngine\TFE_Audio\midiPlayer_xbox.cpp
    TheForceEngine\TFE_DarkForces\Actor\actor.cpp
    TheForceEngine\TFE_DarkForces\Actor\actorSerialization.cpp
    TheForceEngine\TFE_DarkForces\Actor\animTables.cpp
    TheForceEngine\TFE_DarkForces\Actor\bobaFett.cpp
    TheForceEngine\TFE_DarkForces\Actor\dragon.cpp
    TheForceEngine\TFE_DarkForces\Actor\enemies.cpp
    TheForceEngine\TFE_DarkForces\Actor\exploders.cpp
    TheForceEngine\TFE_DarkForces\Actor\flyers.cpp
    TheForceEngine\TFE_DarkForces\Actor\mousebot.cpp
    TheForceEngine\TFE_DarkForces\Actor\phaseOne.cpp
    TheForceEngine\TFE_DarkForces\Actor\phaseThree.cpp
    TheForceEngine\TFE_DarkForces\Actor\phaseTwo.cpp
    TheForceEngine\TFE_DarkForces\Actor\scenery.cpp
    TheForceEngine\TFE_DarkForces\Actor\sewer.cpp
    TheForceEngine\TFE_DarkForces\Actor\troopers.cpp
    TheForceEngine\TFE_DarkForces\Actor\turret.cpp
    TheForceEngine\TFE_DarkForces\Actor\welder.cpp
    TheForceEngine\TFE_DarkForces\GameUI\agentMenu.cpp
    TheForceEngine\TFE_DarkForces\GameUI\delt.cpp
    TheForceEngine\TFE_DarkForces\GameUI\editBox.cpp
    TheForceEngine\TFE_DarkForces\GameUI\escapeMenu.cpp
    TheForceEngine\TFE_DarkForces\GameUI\menu.cpp
    TheForceEngine\TFE_DarkForces\GameUI\missionBriefing.cpp
    TheForceEngine\TFE_DarkForces\GameUI\pda.cpp
    TheForceEngine\TFE_DarkForces\GameUI\uiDraw.cpp
    TheForceEngine\TFE_DarkForces\Landru\cutscene.cpp
    TheForceEngine\TFE_DarkForces\Landru\cutsceneList.cpp
    TheForceEngine\TFE_DarkForces\Landru\cutscene_film.cpp
    TheForceEngine\TFE_DarkForces\Landru\cutscene_player.cpp
    TheForceEngine\TFE_DarkForces\Landru\lactor.cpp
    TheForceEngine\TFE_DarkForces\Landru\lactorAnim.cpp
    TheForceEngine\TFE_DarkForces\Landru\lactorCust.cpp
    TheForceEngine\TFE_DarkForces\Landru\lactorDelt.cpp
    TheForceEngine\TFE_DarkForces\Landru\lcanvas.cpp
    TheForceEngine\TFE_DarkForces\Landru\ldraw.cpp
    TheForceEngine\TFE_DarkForces\Landru\lfade.cpp
    TheForceEngine\TFE_DarkForces\Landru\lfont.cpp
    TheForceEngine\TFE_DarkForces\Landru\lmusic.cpp
    TheForceEngine\TFE_DarkForces\Landru\lpalette.cpp
    TheForceEngine\TFE_DarkForces\Landru\lrect.cpp
    TheForceEngine\TFE_DarkForces\Landru\lsound.cpp
    TheForceEngine\TFE_DarkForces\Landru\ltimer.cpp
    TheForceEngine\TFE_DarkForces\Landru\lview.cpp
    TheForceEngine\TFE_DarkForces\Landru\textCrawl.cpp
    TheForceEngine\TFE_DarkForces\agent.cpp
    TheForceEngine\TFE_DarkForces\animLogic.cpp
    TheForceEngine\TFE_DarkForces\automap.cpp
    TheForceEngine\TFE_DarkForces\briefingList.cpp
    TheForceEngine\TFE_DarkForces\cheats.cpp
    TheForceEngine\TFE_DarkForces\config.cpp
    TheForceEngine\TFE_DarkForces\darkForcesMain.cpp
    TheForceEngine\TFE_DarkForces\gameMessage.cpp
    TheForceEngine\TFE_DarkForces\gameMusic.cpp
    TheForceEngine\TFE_DarkForces\generator.cpp
    TheForceEngine\TFE_DarkForces\hitEffect.cpp
    TheForceEngine\TFE_DarkForces\hud.cpp
    TheForceEngine\TFE_DarkForces\item.cpp
    TheForceEngine\TFE_DarkForces\logic.cpp
    TheForceEngine\TFE_DarkForces\mission.cpp
    TheForceEngine\TFE_DarkForces\pickup.cpp
    TheForceEngine\TFE_DarkForces\player.cpp
    TheForceEngine\TFE_DarkForces\playerCollision.cpp
    TheForceEngine\TFE_DarkForces\projectile.cpp
    TheForceEngine\TFE_DarkForces\random.cpp
    TheForceEngine\TFE_DarkForces\sound.cpp
    TheForceEngine\TFE_DarkForces\time.cpp
    TheForceEngine\TFE_DarkForces\updateLogic.cpp
    TheForceEngine\TFE_DarkForces\util.cpp
    TheForceEngine\TFE_DarkForces\vueLogic.cpp
    TheForceEngine\TFE_DarkForces\weapon.cpp
    TheForceEngine\TFE_DarkForces\weaponFireFunc.cpp
    TheForceEngine\TFE_FileSystem\filestream_xbox.cpp
    TheForceEngine\TFE_FileSystem\fileutil_xbox.cpp
    TheForceEngine\TFE_FileSystem\filewriterAsync_xbox.cpp
    TheForceEngine\TFE_FileSystem\memorystream.cpp
    TheForceEngine\TFE_FileSystem\paths_xbox.cpp
    TheForceEngine\TFE_Game\igame.cpp
    TheForceEngine\TFE_Game\reticle_xbox.cpp
    TheForceEngine\TFE_Game\saveSystem.cpp
    TheForceEngine\TFE_Input\input.cpp
    TheForceEngine\TFE_Input\inputMapping.cpp
    TheForceEngine\TFE_Input\input_xbox.cpp
    TheForceEngine\TFE_Input\replay_xbox.cpp
    TheForceEngine\TFE_Jedi\Collision\collision.cpp
    TheForceEngine\TFE_Jedi\IMuse\imConst.cpp
    TheForceEngine\TFE_Jedi\IMuse\imDigitalSound.cpp
    TheForceEngine\TFE_Jedi\IMuse\imList.cpp
    TheForceEngine\TFE_Jedi\IMuse\imMidiCmd.cpp
    TheForceEngine\TFE_Jedi\IMuse\imMidiPlayer.cpp
    TheForceEngine\TFE_Jedi\IMuse\imSoundFader.cpp
    TheForceEngine\TFE_Jedi\IMuse\imTrigger.cpp
    TheForceEngine\TFE_Jedi\IMuse\imuse.cpp
    TheForceEngine\TFE_Jedi\IMuse\midiData.cpp
    TheForceEngine\TFE_Jedi\InfSystem\infState.cpp
    TheForceEngine\TFE_Jedi\InfSystem\infSystem.cpp
    TheForceEngine\TFE_Jedi\InfSystem\message.cpp
    TheForceEngine\TFE_Jedi\Level\level.cpp
    TheForceEngine\TFE_Jedi\Level\levelBin.cpp
    TheForceEngine\TFE_Jedi\Level\levelData.cpp
    TheForceEngine\TFE_Jedi\Level\levelTextures.cpp
    TheForceEngine\TFE_Jedi\Level\rfont.cpp
    TheForceEngine\TFE_Jedi\Level\robjData.cpp
    TheForceEngine\TFE_Jedi\Level\robject.cpp
    TheForceEngine\TFE_Jedi\Level\roffscreenBuffer.cpp
    TheForceEngine\TFE_Jedi\Level\rsector.cpp
    TheForceEngine\TFE_Jedi\Level\rtexture.cpp
    TheForceEngine\TFE_Jedi\Level\rwall.cpp
    TheForceEngine\TFE_Jedi\Math\core_math.cpp
    TheForceEngine\TFE_Jedi\Math\cosTable.cpp
    TheForceEngine\TFE_Jedi\Memory\allocator.cpp
    TheForceEngine\TFE_Jedi\Memory\list.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Fixed\rclassicFixed.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Fixed\rclassicFixedSharedState.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Fixed\redgePairFixed.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Fixed\rflatFixed.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Fixed\rlightingFixed.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Fixed\robj3d_fixed\robj3dFixed.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Fixed\robj3d_fixed\robj3dFixed_Clipping.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Fixed\robj3d_fixed\robj3dFixed_Culling.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Fixed\robj3d_fixed\robj3dFixed_PolygonDraw.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Fixed\robj3d_fixed\robj3dFixed_PolygonSetup.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Fixed\robj3d_fixed\robj3dFixed_TransformAndLighting.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Fixed\rsectorFixed.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Fixed\rwallFixed.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Float\rclassicFloat.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Float\rclassicFloatSharedState.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Float\redgePairFloat.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Float\rflatFloat.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Float\rlightingFloat.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Float\robj3d_float\robj3dFloat.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Float\robj3d_float\robj3dFloat_Clipping.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Float\robj3d_float\robj3dFloat_Culling.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Float\robj3d_float\robj3dFloat_PolygonDraw.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Float\robj3d_float\robj3dFloat_PolygonSetup.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Float\robj3d_float\robj3dFloat_TransformAndLighting.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Float\rsectorFloat.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Float\rwallFloat.cpp
    TheForceEngine\TFE_Jedi\Renderer\RClassic_Float\screenDraw.cpp
    TheForceEngine\TFE_Jedi\Renderer\jediRenderer.cpp
    TheForceEngine\TFE_Jedi\Renderer\rcommon.cpp
    TheForceEngine\TFE_Jedi\Renderer\rscanline.cpp
    TheForceEngine\TFE_Jedi\Renderer\rsectorRender.cpp
    TheForceEngine\TFE_Jedi\Renderer\screenDraw.cpp
    TheForceEngine\TFE_Jedi\Renderer\virtualFramebuffer.cpp
    TheForceEngine\TFE_Jedi\Serialization\serialization.cpp
    TheForceEngine\TFE_Jedi\Task\task.cpp
    TheForceEngine\TFE_Memory\chunkedArray.cpp
    TheForceEngine\TFE_Memory\memoryRegion.cpp
    TheForceEngine\TFE_Polygon\clipper.cpp
    TheForceEngine\TFE_Polygon\polygon.cpp
    TheForceEngine\TFE_ExternalData\dfLogics.cpp
    TheForceEngine\TFE_ExternalData\logicTables.cpp
    TheForceEngine\TFE_ExternalData\pickupExternal.cpp
    TheForceEngine\TFE_ExternalData\weaponExternal.cpp
    TheForceEngine\TFE_RenderBackend\renderBackend_xbox.cpp
    TheForceEngine\TFE_RenderBackend\renderState_xbox.cpp
    TheForceEngine\TFE_RenderShared\texturePacker_xbox.cpp
    TheForceEngine\TFE_Settings\settings.cpp
    TheForceEngine\TFE_Settings\windows\registry.cpp
    TheForceEngine\TFE_System\frameLimiter.cpp
    TheForceEngine\TFE_System\iniParser.cpp
    TheForceEngine\TFE_System\math.cpp
    TheForceEngine\TFE_System\memoryPool.cpp
    TheForceEngine\TFE_System\parser.cpp
    TheForceEngine\TFE_System\system_xbox.cpp
    TheForceEngine\TFE_System\tfeMessage.cpp
    TheForceEngine\TFE_System\utf8.cpp
    TheForceEngine\main_xbox.cpp
    TheForceEngine\xbox_link_stubs.cpp
) do (
    set /a TOTAL+=1
    set "SRC=%%F"
    for %%N in (%%~nF) do set "OBJ=%OBJDIR%\%%N.obj"
    echo   [C++] %%F
    "%CC%" /c /Tp "%~dp0%%F" %CFLAGS% /Fo"!OBJ!" >> "%BUILDLOG%" 2>&1
    if errorlevel 1 (
        echo       *** FAILED: %%F
        set /a ERRORS+=1
    ) else (
        set /a COMPILED+=1
    )
)

echo.
echo  Compiled: !COMPILED!/!TOTAL!   Errors: !ERRORS!
echo  Full build log: %BUILDLOG%

if !ERRORS! GTR 0 (
    echo.
    echo  BUILD FAILED at compile stage.
    echo  See %BUILDLOG% for full output.
    exit /b 1
)

REM -- Link via response file -------------------------------------------------
REM Lib list matches OpenJKDF2 exactly. xacteng/xnet excluded (TFE doesn't
REM use XACT or networking). libc.lib is VC71's static CRT (not libcmt.lib).
set RSPFILE=%OBJDIR%\link.rsp
> "%RSPFILE%" echo /nologo
>> "%RSPFILE%" echo /OUT:"%OUTEXE%"
>> "%RSPFILE%" echo /MAP:"%OUTEXE%.map"
>> "%RSPFILE%" echo /LIBPATH:"%XDK_ROOT%\lib"
>> "%RSPFILE%" echo /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup /FIXED:NO
REM xbox_link_stubs.cpp redefines screenDraw symbols that screenDraw.cpp
REM also provides (VC8 build tolerated as LNK4006 warning, VC71 emits
REM LNK2005 error). /FORCE:MULTIPLE preserves the VC8 behavior.
>> "%RSPFILE%" echo /FORCE:MULTIPLE /IGNORE:4254 /IGNORE:4006 /IGNORE:4088 /IGNORE:4078
REM Block the dynamic CRT and any single-threaded variants. With /MT the
REM compiler emits /DEFAULTLIB:LIBCMT + /DEFAULTLIB:LIBCPMT directives,
REM which the linker resolves out of XDK 5558's lib dir (first in LIBPATH).
>> "%RSPFILE%" echo /NODEFAULTLIB:MSVCRT.lib /NODEFAULTLIB:MSVCRTD.lib /NODEFAULTLIB:LIBC.lib /NODEFAULTLIB:LIBCD.lib /NODEFAULTLIB:LIBCP.lib /NODEFAULTLIB:LIBCPD.lib
>> "%RSPFILE%" echo d3d8.lib d3dx8.lib dsound.lib xboxkrnl.lib xgraphics.lib xonline.lib xapilib.lib
for %%O in (%OBJDIR%\*.obj) do >> "%RSPFILE%" echo "%%O"

REM VC71 link.exe doesn't play well with enabledelayedexpansion - delegate
REM to a child batch that runs without it (OpenJKDF2 pattern).
set _POSTBAT=%OBJDIR%\do_post.bat
> "%_POSTBAT%" echo @echo off
>> "%_POSTBAT%" echo echo Linking...
>> "%_POSTBAT%" echo "%LINK%" @"%RSPFILE%" ^>^> "%BUILDLOG%" 2^>^&1
>> "%_POSTBAT%" echo if errorlevel 1 exit /b 1
>> "%_POSTBAT%" echo echo Patching XBE...
>> "%_POSTBAT%" echo %PYTHON% "%~dp0patchxbe.py" "%OUTEXE%" "%OUTDIR%\default.xbe" ^>^> "%BUILDLOG%" 2^>^&1
>> "%_POSTBAT%" echo if errorlevel 1 exit /b 1

endlocal
call "%~dp0build\xbox\release\obj\do_post.bat"
if errorlevel 1 (
    echo.
    echo  LINK or PATCHXBE FAILED.
    echo  See build_xbox.log for details.
    exit /b 1
)

if not exist "%~dp0build\xbox\release\default.xbe" (
    echo  ERROR: default.xbe was not produced.
    exit /b 1
)

echo.
echo ============================================================
echo  BUILD SUCCEEDED
echo  EXE: %~dp0build\xbox\release\default.exe
echo  XBE: %~dp0build\xbox\release\default.xbe
echo  Log: %~dp0build_xbox.log
echo ============================================================
exit /b 0
