// Driving at 60 frames a second on the PAL disc: Driving.xbe's rate is 0x117DA0, the refresh of the video standard
// [0x243540] from the table 0x1A204C (NTSC-M 60, NTSC-J 60, PAL-I 50, PAL-M 60). The game's frame timer takes it
// (0x10AE50 from 0x59920) and its steps are 1 / it (0x95380, 0xB49F0, 0x5CEB0), so on PAL the whole of the driving
// half ran at 50 frames a second while the screen shows 60. The USA disc runs the same code at 60. For the call
// alone the standard reads as NTSC-M, so the rate is 60; everything else (the PAL movies and files) still sees PAL.
#include <cstdint>

#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

X86_EXTERN_FUNC(__imp__sub_00117DA0);
X86_FUNC(sub_00117DA0) {
    constexpr uint32_t kStandard = 0x243540, kPal = 2, kNtsc = 0;
    uint32_t standard = kernel::R32(kStandard);
    if (standard != kPal) return __imp__sub_00117DA0(ctx, base);
    static bool said = false;
    if (!said) {
        said = true;
        LOGR("nightfire: the driving levels at 60 frames a second (the PAL disc's 50 Hz rate read as 60)");
    }
    kernel::W32(kStandard, kNtsc);
    __imp__sub_00117DA0(ctx, base);
    kernel::W32(kStandard, standard);
}
