#pragma once
// NightFire's own settings and the hooks the two executables share (default.xbe: title, menus and the on-foot
// levels; Driving.xbe: EA's front end and the driving levels)
#include <cstdint>

#include "core/input.h"

namespace nf {
extern const bool kDriving;  // Driving.xbe's exe (src/driving/hooks.cpp), default.xbe's false (src/game/hooks.cpp)
extern bool skip_intro;
// a menu has the controller (each exe's hooks.cpp): the mouse then plays A, B and the d-pad (MenuMouse)
bool MenuUp();
void MenuMouse(int port, input::Pad& p);  // GameInfo::pad  // no EA and MGM logo movies at boot (HideFile)
}  // namespace nf
