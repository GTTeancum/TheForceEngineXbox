#pragma once
// input_xbox.h
// Xbox XInput polling interface.
// Include in main_xbox.cpp and call once per frame.

namespace TFE_InputXbox
{
    void init();
    void pollInput();
    void shutdown();
}
