// The keyboard and mouse play as the NightFire controller style whatever style the player chose.
//
// default.xbe reads the pads into each player's controls in 0xDE720 (cdecl: the players, their count; each player
// 0x158 bytes from +0x14): a switch on the player's controller style (the u16 at +0xE, the options' order: NightFire,
// Moonraker, Octopussy, ...) picks which stick axis is moving, strafing, turning and looking. The keys (WASD on the
// left stick) and the mouse (the right stick) are laid out for style 0, NightFire's: the left stick moves and strafes,
// the right turns and looks. Octopussy (2) and Thunderball (5) swap the sticks' x axes, so the mouse strafed and A and
// D turned. While controller 1 is played on the keyboard and mouse its player is read as style 0 for the call; a
// controller keeps the chosen style.
//
// The mouse plays the right stick, and the pads' poll 0xE76A0 (every port into 0x2FF49C + port * 0xA8: the
// XInputGetState report at +4, its sticks as floats at +0x1C..+0x28, -100 to 100 by 0x1620E0) takes a dead zone off
// each axis and stretches the rest (0xE79CF..), so slow mouse moves did nothing. While the keyboard and mouse play,
// controller 1's right stick floats are made again from the report's shorts without it; the runtime starts a mouse
// move at 20% deflection to get past a game's dead zone, which is taken off here too, so the turn follows the mouse
// from its slowest.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "core/input.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
constexpr uint32_t kPlayerSize = 0x158, kStyle = 0xE, kPort = 0x156;
constexpr uint32_t kPads = 0x2FF49C, kRawRX = 0x16, kRawRY = 0x18, kRX = 0x24, kRY = 0x28;
constexpr float kMouseStart = 0.2f;  // input.cpp's kDeadZone: where a mouse move's deflection starts

float Linear(int16_t raw) {
    float a = std::fabs(raw / 32767.0f);
    return a <= kMouseStart ? 0.0f : std::copysign(std::min((a - kMouseStart) / (1 - kMouseStart), 1.0f), float(raw));
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_000E76A0);
X86_FUNC(sub_000E76A0) {  // the pads' poll
    __imp__sub_000E76A0(ctx, base);
    if (!input::UsingKeyboard() || !kernel::R32(kPads)) return;  // controller 1 not open
    float x = 100 * Linear(kernel::G<int16_t>(kPads + kRawRX)), y = 100 * Linear(kernel::G<int16_t>(kPads + kRawRY));
    std::memcpy(&kernel::G<uint32_t>(kPads + kRX), &x, 4);
    std::memcpy(&kernel::G<uint32_t>(kPads + kRY), &y, 4);
}

X86_EXTERN_FUNC(__imp__sub_000DE720);
X86_FUNC(sub_000DE720) {
    uint32_t players = kernel::R32(ctx.esp + 4), count = kernel::R32(ctx.esp + 8) & 0xFFFF;
    uint32_t style_at = 0, style = 0;
    if (input::UsingKeyboard() && players && count <= 4)
        for (uint32_t i = 0; i < count; i++) {
            uint32_t p = players + i * kPlayerSize;
            if (kernel::G<uint8_t>(p + kPort) != 0) continue;  // controller 1's player
            style = kernel::G<uint16_t>(p + kStyle);
            if (style) style_at = p + kStyle;
            break;
        }
    static uint32_t said = 0;
    if (style_at && style != said) {
        said = style;
        LOGR("nightfire: controller style %u chosen; the keyboard and mouse play as NightFire's (style 0)", style);
    }
    if (style_at) kernel::G<uint16_t>(style_at) = 0;
    __imp__sub_000DE720(ctx, base);
    if (style_at) kernel::G<uint16_t>(style_at) = uint16_t(style);
}
