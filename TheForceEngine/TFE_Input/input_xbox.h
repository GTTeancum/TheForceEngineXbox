#pragma once
// input_xbox.h
// Xbox XInput polling interface.
// Include in main_xbox.cpp and call once per frame.

namespace TFE_InputXbox
{
    void init();
    void pollInput();
    void shutdown();
    void setLookSensitivity(float value);
    float getLookSensitivity();
    void setLookSensitivityX(float value);
    void setLookSensitivityY(float value);
    float getLookSensitivityX();
    float getLookSensitivityY();
    void setStickDeadzone(float value);
    float getStickDeadzone();
    void setRightStickDeadzone(float value);
    float getRightStickDeadzone();
}
