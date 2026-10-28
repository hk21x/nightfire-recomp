// The mouse in the menus: the wheel scrolls (d-pad up and down), the left button selects (A), the right goes back
// (B), and Enter selects too (A's keys are crouch's in play). While a menu is up (nf::MenuUp, each exe's own test),
// the mouse's buttons stop playing what they are bound to in play (fire, aim) and play A and B instead; each notch of
// the wheel is a press of the d-pad (EoN's way). In play each notch cycles the weapon instead: on foot the d-pad's
// right a notch down and left a notch up (the NightFire style's Cycle Weapon), in the car Black and White, on a
// mounted gun its style's; there the mouse's buttons fire and zoom too (GunStyle).
#include <algorithm>
#include <chrono>
#include <cstdint>

#include "common/nightfire.h"
#include "core/input.h"
#include "core/log.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "app/app.h"
#endif

namespace nf {
namespace {
bool KeyHeld(int vk) {
#ifdef _WIN32
    return GetForegroundWindow() == static_cast<HWND>(app::Window()) && (GetAsyncKeyState(vk) & 0x8000);
#else
    (void)vk;
    return false;
#endif
}

// a press of control c (a d-pad bit or an analog button)
void Press(input::Pad& p, int c) {
    if (c < input::kA) p.buttons |= uint16_t(1u << c);
    else p.analog[c - input::kA] = 255;
}
}  // namespace

void MenuMouse(int port, input::Pad& p) {
    constexpr int64_t kPress = 70, kGap = 70;  // ms
    constexpr uint16_t kMouseBits[3] = {0x01, 0x02, 0x04};  // VK_LBUTTON VK_RBUTTON VK_MBUTTON
    if (port != 0) return;
    static int pending = 0;  // notches to play, up positive
    static int64_t pulse = 0;
    static bool was_menu = false;
    int notches = input::TakeWheel();  // taken in play too, so none wait for the next menu
    uint32_t held = input::MouseButtons();
    using namespace std::chrono;
    int64_t now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    bool menu = MenuUp();
    static bool was = false;
    if (menu != was) LOGR("nightfire: %s", menu ? "a menu is up: the mouse plays A, B and the d-pad" : "out of the menus");
    was = menu;
    // the wheel's two controls: the menus scroll, on foot the d-pad cycles the weapon (down the next), in the car Black
    // and White, on a mounted gun its style's (data\\control\\povcfgN.def: White/Black in 4, the d-pad's up and down
    // in 6 and 7, else its left and right)
    int gun = kDriving ? GunStyle() : -1;
    int next = input::kRight, prev = input::kLeft;
    if (menu) next = input::kDown, prev = input::kUp;
    else if (gun == 4) next = input::kBlack, prev = input::kWhite;
    else if (gun == 6 || gun == 7) next = input::kDown, prev = input::kUp;
    else if (kDriving && gun < 0) next = input::kBlack, prev = input::kWhite;
    // on a mounted gun the car's buttons do not apply: the left button fires (RT), the right zooms (LT), R reloads (X
    // in style 0, else A)
    if (gun >= 0 && input::UsingKeyboard()) {
        for (int c : {input::kA, input::kX, input::kLT, input::kRT}) p.analog[c - input::kA] = 0;
        if (held & 1) Press(p, input::kRT);
        if (held & 2) Press(p, input::kLT);
        if (KeyHeld('R')) Press(p, gun == 0 ? input::kX : input::kA);
    }
    if (menu) {
        if (KeyHeld(0x0D)) p.analog[0] = 255;  // Enter: A
        // the buttons out of what they play in play
        for (int c = 0; c < input::kA + 8; c++) {
            auto keys = input::ControlKeys(c);
            bool mouse = false;
            for (int b = 0; b < 3; b++)
                if (held >> b & 1) mouse |= keys[0] == kMouseBits[b] || keys[1] == kMouseBits[b];
            if (!mouse) continue;
            if (c < input::kA) p.buttons = uint16_t(p.buttons & ~(1u << c));
            else p.analog[c - input::kA] = 0;
        }
        if (held & 1) p.analog[0] = 255;  // A
        if (held & 2) p.analog[1] = 255;  // B
    }
    // the wheel: a d-pad press a notch
    if (menu != was_menu) pending = 0;
    was_menu = menu;
    if (notches && !pending) pulse = now;
    pending = std::clamp(pending + notches, -8, 8);
    if (pending) {
        int64_t t = now - pulse;
        if (t < kPress) {
            Press(p, pending > 0 ? prev : next);
        } else if (t >= kPress + kGap) {
            pending += pending > 0 ? -1 : 1;
            pulse = now;
        }
    }
}

}  // namespace nf
