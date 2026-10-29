// hide_hud in the driving missions: the HUD's draw 0xE1970 (fastcall, the HUD at [0x23F44C] in ecx) draws, in play
// ([0x1B7055] set, +0x314 0, no menu: +0x2B0 0), the meters, radar and weapon (0xD81D0 and those beside it) and the
// objective and hint messages (0xE3180, 0xE39B0); with a menu up, the pause screens (0xD93D0, 0xDD340). In play it
// is not called while the HUD is hidden; the menus and the mission's end draw as the game has them.
#include <cstdint>

#include "common/nightfire.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

X86_EXTERN_FUNC(__imp__sub_000E1970);
X86_FUNC(sub_000E1970) {
    constexpr uint32_t kInPlay = 0x1B7055, kMenuUp = 0x2B0, kEnding = 0x314;
    const uint32_t hud = ctx.ecx;
    if (nf::hide_hud && kernel::G<uint8_t>(kInPlay) && !kernel::G<uint8_t>(hud + kMenuUp) && !kernel::R32(hud + kEnding)) {
        ctx.esp += 4;  // the return address only
        return;
    }
    __imp__sub_000E1970(ctx, base);
}
