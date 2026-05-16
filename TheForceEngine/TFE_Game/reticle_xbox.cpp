// reticle_xbox.cpp
// Xbox stub for the reticle system.
// TFE_PostProcess is excluded on Xbox; reticle overlay is not rendered.
// All call sites (mission.cpp, escapeMenu.cpp, pda.cpp, darkForcesMain.cpp)
// are safe to call these no-ops.

#include <TFE_Game/reticle.h>
#include <TFE_System/system.h>

static bool s_reticleEnabled = false;
static u32  s_reticleShape   = 0;
static f32  s_reticleColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
static f32  s_reticleScale   = 1.0f;

bool reticle_init()
{
    s_reticleEnabled = false;
    TFE_System::logWrite(LOG_MSG, "Reticle", "Reticle stub initialised (no post-process on Xbox)");
    return true;
}

void reticle_destroy()    { TFE_XboxLogf("Reticle", "destroy"); }
void reticle_enable(bool enable) { s_reticleEnabled = enable; TFE_XboxLogf("Reticle", "enable %d", enable ? 1 : 0); }
void reticle_setShape(u32 index) { s_reticleShape = index; TFE_XboxLogf("Reticle", "setShape %u", index); }
void reticle_setColor(const f32* color)
{
    if (color)
    {
        s_reticleColor[0] = color[0];
        s_reticleColor[1] = color[1];
        s_reticleColor[2] = color[2];
        s_reticleColor[3] = color[3];
        // %f avoided - MSVC 2005 vsprintf float formatting hangs on Xbox.
        TFE_XboxLogf("Reticle", "setColor %d %d %d %d (x1000)",
            (int)(color[0]*1000.0f), (int)(color[1]*1000.0f),
            (int)(color[2]*1000.0f), (int)(color[3]*1000.0f));
    }
}
void reticle_setScale(f32 scale) { s_reticleScale = scale; TFE_XboxLogf("Reticle", "setScale %d(x1000)", (int)(scale*1000.0f)); }
bool reticle_enabled()           { return s_reticleEnabled; }
u32  reticle_getShape()          { return s_reticleShape; }
u32  reticle_getShapeCount()     { return 0; }
void reticle_getColor(f32* color)
{
    if (color)
    {
        color[0] = s_reticleColor[0];
        color[1] = s_reticleColor[1];
        color[2] = s_reticleColor[2];
        color[3] = s_reticleColor[3];
    }
}
f32 reticle_getScale() { return s_reticleScale; }
