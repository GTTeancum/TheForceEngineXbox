// renderState_xbox.cpp
// Xbox stub for TFE_RenderState.
// The software renderer does not use GPU render state.
// All functions are safe no-ops.

#include <TFE_RenderBackend/renderState.h>

namespace TFE_RenderState
{
    void clear()                                                            {}
    void setStateEnable(bool, u32)                                         {}
    void setBlendMode(StateBlendFactor, StateBlendFactor, StateBlendFunc)  {}
    void setDepthFunction(ComparisonFunction)                              {}
    void setStencilFunction(ComparisonFunction, s32, u32)                  {}
    void setStencilOp(StencilOp, StencilOp, StencilOp)                    {}
    void setColorMask(u32)                                                 {}
    void setDepthBias(f32, f32)                                            {}
    void enableClipPlanes(s32)                                             {}
}
