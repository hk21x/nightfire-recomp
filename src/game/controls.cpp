// The keyboard and mouse play as the NightFire controller style whatever style the player chose.
//
// default.xbe reads the pads into each player's controls in 0xDE720 (cdecl: the players, their count; each player
// 0x158 bytes from +0x14): a switch on the player's controller style (the u16 at +0xE, the options' order: NightFire,
// Moonraker, Octopussy, ...) picks which stick axis is moving, strafing, turning and looking. The keys (WASD on the
// left stick) and the mouse (the right stick) are laid out for style 0, NightFire's: the left stick moves and strafes,
// the right turns and looks. Octopussy (2) and Thunderball (5) swap the sticks' x axes, so the mouse strafed and A and
// D turned. While controller 1 is played on the keyboard and mouse its player is read as style 0 for the call; a
// controller keeps the chosen style.

#include "core/input.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
constexpr uint32_t kPlayerSize = 0x158, kStyle = 0xE, kPort = 0x156;
}  // namespace

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
