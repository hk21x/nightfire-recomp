#pragma once
// NightFire's own settings and the hooks the two executables share (default.xbe: title, menus and the on-foot
// levels; Driving.xbe: EA's front end and the driving levels)
#include <cstdint>

#include "core/input.h"

namespace nf {
extern const bool kDriving;  // Driving.xbe's exe (src/driving/hooks.cpp), default.xbe's false (src/game/hooks.cpp)
extern bool skip_intro;
extern uint32_t fov_percent;  // the 3D views' field of view, percent of the game's (src/game/view.cpp, src/driving/view.cpp)
float Ultrawide();            // how much wider than 16:9 the frame is shown, 1 on a 16:9 or 4:3 screen
float FovScale();             // fov_percent as a factor, 0.5 to 1.5
// a menu has the controller (each exe's hooks.cpp): the mouse then plays A, B and the d-pad (MenuMouse)
bool MenuUp();
void MenuMouse(int port, input::Pad& p);
void MouseAsStick();
int GunStyle();
bool ButtonPictures(uint32_t header, uint32_t palette);  // GameInfo::texture_bound (src/common/buttons.cpp)
void RefreshButtons();  // each poll
bool MenuKeys();  // the menu up shows Enter and Backspace for A and B (not a level's pause, whose controls page
                  // names the keys played)       // Driving.xbe on a mounted gun: its controller style (0-7); -1 in the car, on foot, in a menu  // default.xbe: after its pad reader (src/game/mouse.cpp)  // GameInfo::pad  // no EA and MGM logo movies at boot (HideFile)
}  // namespace nf
