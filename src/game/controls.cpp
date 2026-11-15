// The keyboard and mouse play as the NightFire controller style whatever style the player chose.
//
// default.xbe reads the pads into each player's controls in 0xDE720 (cdecl: the players, their count; each player
// 0x158 bytes from 0x1FE6D0): a switch on the player's controller style (the u16 at +0xE: NightFire 0, Moonraker 1,
// Octopussy 2, Goldfinger 3, Dr. No 4, Thunderball 5, GoldenEye 6, Classic Bond 7) picks which stick axis is moving,
// strafing, turning and looking and which button does what. The keys are laid out for style 0's controls page. While
// controller 1 is played on the keyboard and mouse its player's style is 0, so the game's prompts and controls page
// name the buttons the keys play (the pictures show the keys: src/common/buttons.cpp); a controller puts the chosen
// style back, and the profile is written with it (0x6ED30 packs a player's controls for the save and the launch data).
#include "app/app.h"
#include "common/nightfire.h"
#include "core/input.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
constexpr uint32_t kControls = 0x1FE6D0, kPlayerSize = 0x158, kStyle = 0xE, kPort = 0x156;
int g_player = -1;     // controller 1's player while its style is held at 0
uint16_t g_chosen = 0;  // its own style meanwhile

void Restore() {
    if (g_player < 0) return;
    kernel::G<uint16_t>(kControls + uint32_t(g_player) * kPlayerSize + kStyle) = g_chosen;
    g_player = -1;
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_000DE720);
X86_FUNC(sub_000DE720) {
    uint32_t players = kernel::R32(ctx.esp + 4), count = kernel::R32(ctx.esp + 8) & 0xFFFF;
    if (!input::UsingKeyboard() || players != kControls || count > 4) {
        Restore();
    } else if (g_player >= 0) {
        uint16_t& style = kernel::G<uint16_t>(kControls + uint32_t(g_player) * kPlayerSize + kStyle);
        if (style) g_chosen = style, style = 0;  // chosen anew in the options meanwhile: that one is the player's
    } else {
        for (uint32_t i = 0; i < count; i++) {
            uint32_t p = kControls + i * kPlayerSize;
            if (kernel::G<uint8_t>(p + kPort) != 0) continue;  // controller 1's player
            g_chosen = kernel::G<uint16_t>(p + kStyle);
            g_player = int(i);
            kernel::G<uint16_t>(p + kStyle) = 0;
            if (g_chosen)
                LOGR("nightfire: controller style %u chosen; the keyboard and mouse play as NightFire's (style 0)",
                     g_chosen);
            break;
        }
    }
    __imp__sub_000DE720(ctx, base);
    nf::MouseAsStick();
}

// a player's controls packed for the profile and the launch data (cdecl: a tag, the player, ...): with its own style
X86_EXTERN_FUNC(__imp__sub_0006ED30);
X86_FUNC(sub_0006ED30) {
    int player = int(kernel::R32(ctx.esp + 8) & 0xFFFF);
    if (player != g_player) return __imp__sub_0006ED30(ctx, base);
    uint16_t& style = kernel::G<uint16_t>(kControls + uint32_t(player) * kPlayerSize + kStyle);
    style = g_chosen;
    __imp__sub_0006ED30(ctx, base);
    style = 0;
}

// The pads' poll 0xE76A0 makes each stick axis -100 to 100, takes a dead zone off it and stretches the rest (then 1.2
// times, held to 100 round). The dead zone is 30 (app::kGame.stick_deadzone), but 60 on an axis of a pad plugged in
// until that axis has been held past 90 each way for five polls (two counters an axis from 0x2FF524, 0xA8 a port,
// set to 5 when plugged in). A stick at a lower sensitivity (the Controls tab) may never reach 90, and its axes stayed
// at 60: while either stick is below 100 the counters are run out, the pad's sticks taken as centred (SDL's are).
X86_EXTERN_FUNC(__imp__sub_000E76A0);
X86_FUNC(sub_000E76A0) {
    const app::Settings& s = app::Get();
    if (s.stick_sensitivity[0] < 100 || s.stick_sensitivity[1] < 100) {
        for (uint32_t port = 0; port < 4; port++)
            for (uint32_t c = 0; c < 8; c++) kernel::W32(0x2FF524 + port * 0xA8 + c * 4, 0);
    }
    __imp__sub_000E76A0(ctx, base);
}
