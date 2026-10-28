// The mouse aims on foot as in a PC shooter: its movement turns and pitches Bond's view directly, not through the
// right stick (GameInfo::mouse_direct in default.xbe's exe; the driving levels keep the stick).
//
// The player's movement update 0xA9CF0 (cdecl: the actor, its object, a time scale) reads the turn, move and strafe
// controls (0x6CA20 slots 0-2) and writes the frame's turn at actor +0x28: radians added to the view's heading at the
// next step (turning right is negative), through an acceleration filter (0xB7420, +0x88C) and the stick's value
// squared, which is what made the mouse feel like a stick. The look update 0xB7610 adds the look control to the
// pitch at +0x838 (a quarter turn up is 1, the camera's angle pi/2 times it) and marks the frame looked (+0xF0 = 1:
// otherwise the view eases back level). After 0xA9CF0 for player 1 the mouse's movement since the last
// frame is added to both: so many radians a count at 100% sensitivity, less zoomed in (the camera's field of view
// over the game's 60 degrees). A frame where the game did not write the turn (a cutscene, another state) gets none.
// Headless (no mouse): NF_MOUSE_TEST=dx,dy,from,to plays that many counts a frame for frames from..to.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common/nightfire.h"
#include "core/input.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
constexpr uint32_t kTurn = 0x28, kPitch = 0x838, kLooked = 0xF0, kPlayerIndex = 0x8DE;
constexpr uint32_t kCameras = 0x1F661C, kCamFov = 0x104;
constexpr float kRadiansPerCount = 0.0008f;  // at 100% sensitivity: 36 degrees an inch at 800 counts an inch
constexpr float kGameFov = 1.0471976f;       // the cameras' 60 degrees (0x25190's callers)
constexpr float kHalfPi = 1.5707964f;          // the pitch is kept over a quarter turn (the camera's is pi/2 times it)
constexpr uint32_t kUnset = 0x7FC0DEAD;      // a NaN: the turn as it was before the update, if it stays

float GetF(uint32_t va) {
    float f;
    std::memcpy(&f, &kernel::G<uint32_t>(va), 4);
    return f;
}
void SetF(uint32_t va, float f) { std::memcpy(&kernel::G<uint32_t>(va), &f, 4); }

// the mouse's counts since the last call, times the sensitivity
bool Counts(float& x, float& y) {
    static const struct Test {
        float dx = 0, dy = 0;
        uint32_t from = 0, to = 0;
    } test = [] {
        Test t;
        if (const char* e = std::getenv("NF_MOUSE_TEST"))
            std::sscanf(e, "%f,%f,%u,%u", &t.dx, &t.dy, &t.from, &t.to);
        return t;
    }();
    static uint32_t frame = 0;
    frame++;
    if (test.to) {
        bool on = frame >= test.from && frame <= test.to;
        x = on ? test.dx : 0, y = on ? test.dy : 0;
        return true;
    }
    static auto last = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    float dt = std::clamp(std::chrono::duration<float>(now - last).count(), 0.001f, 0.1f);
    last = now;
    if (!input::UsingKeyboard() || !input::TakeMouse(x, y)) return false;  // a controller in use: its own look
    x *= 2400 * dt, y *= 2400 * dt;  // TakeMouse's rate (2400 counts a second at 100% is 1) back to counts
    return true;
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_000A9CF0);
X86_FUNC(sub_000A9CF0) {
    uint32_t actor = kernel::R32(ctx.esp + 4);
    bool mine = actor && kernel::G<int8_t>(actor + kPlayerIndex) == 0;
    uint32_t was = mine ? kernel::R32(actor + kTurn) : 0;
    if (mine) kernel::W32(actor + kTurn, kUnset);
    __imp__sub_000A9CF0(ctx, base);
    if (!mine) return;
    if (kernel::R32(actor + kTurn) == kUnset) {
        kernel::W32(actor + kTurn, was);
        return;
    }
    float x, y;
    if (!Counts(x, y)) return;
    kernel::G<uint8_t>(actor + kLooked) = 1;  // looking, as 0xB7610 marks the stick's: else the view eases back level
    if (x == 0 && y == 0) return;
    uint32_t cam = kernel::R32(kCameras);
    float zoom = cam ? std::clamp(GetF(cam + kCamFov) / kGameFov, 0.05f, 2.0f) : 1.0f;
    float k = kRadiansPerCount * zoom;
    SetF(actor + kTurn, GetF(actor + kTurn) - x * k);
    SetF(actor + kPitch, std::clamp(GetF(actor + kPitch) + y * k / kHalfPi, -1.0f, 1.0f));
    static bool said = false;
    if (!said) {
        said = true;
        LOGR("nightfire: the mouse turns and pitches the view directly (%.4f radians a count at 100%%)", kRadiansPerCount);
    }
}
