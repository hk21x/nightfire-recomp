// Ultrawide and the field of view in the driving levels (EA's engine, as EoN's driving.xbe).
//
// Every view is built by 0xE46D0 (thiscall: horizontal field of view in degrees, aspect, near, far), called only from
// 0x96EB0, which takes the angle from the camera (+0xB4, the half angle) times the game's multiplier [[0x1EBFF4]+0x58]
// (1, set by scripts), 1.25 times that and 16:9 with the widescreen flag [[0x1EBFF4]+0x4C], else 4:3. The x scale is
// cot(fov/2), the y scale that times the aspect. Ultrawide widens the horizontal angle by the frame's width over 16:9
// in tangent and the aspect with it: the vertical angle is the game's, the cull data the view keeps follows from the
// same arguments, and nothing else culls by the view's sides (gdb watchpoints, a 2x wider test frame drew to its
// edges). The field of view scales the multiplier while 0x96EB0 runs, and while the HUD's markers are placed
// (0xCE2B0 reads it too), so a script's value survives.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "common/nightfire.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
constexpr uint32_t kGame = 0x1EBFF4, kFovFactor = 0x58;
constexpr float kPi = 3.14159265f;

float GetF(uint32_t va) {
    float f;
    std::memcpy(&f, &kernel::G<uint32_t>(va), 4);
    return f;
}
void SetF(uint32_t va, float f) { std::memcpy(&kernel::G<uint32_t>(va), &f, 4); }

// the game's field of view multiplier scaled while call runs
template <class Fn>
void Scaled(Fn call) {
    uint32_t g = kernel::R32(kGame);
    float s = nf::FovScale();
    if (!g || s == 1.0f) return call();
    float was = GetF(g + kFovFactor);
    SetF(g + kFovFactor, was * s);
    call();
    SetF(g + kFovFactor, was);
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_000E46D0);
X86_FUNC(sub_000E46D0) {
    float k = nf::Ultrawide(), fov = GetF(ctx.esp + 4), aspect = GetF(ctx.esp + 8);
    if (k > 1.0f + 1e-3f) {
        fov = 2 * std::atan(k * std::tan(fov * kPi / 360)) * 180 / kPi;
        SetF(ctx.esp + 8, aspect * k);
    }
    SetF(ctx.esp + 4, std::min(fov, 170.0f));
    __imp__sub_000E46D0(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_00096EB0);
X86_FUNC(sub_00096EB0) {  // a view's angle and aspect
    Scaled([&] { __imp__sub_00096EB0(ctx, base); });
}

X86_EXTERN_FUNC(__imp__sub_000CE2B0);
X86_FUNC(sub_000CE2B0) {  // a HUD marker's place on the screen
    Scaled([&] { __imp__sub_000CE2B0(ctx, base); });
}
