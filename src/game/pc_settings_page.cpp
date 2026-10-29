// PC Settings as a page of the game's own menus (default.xbe), from Edit Codename's list (Controller Setup, Driving
// Controller, Advanced Options, Multiplayer Options, AV Options, Save Codename) and the main menu's item.
//
// The page is the AV Options page (0x40000031) cloned into the menu file the page builder reads (src/game/
// pc_settings_item.cpp's copy), its page and row widgets given ids of ours: two sliders (the volumes' rows: field of
// view, mouse sensitivity) and four choices (invert mouse, skip intro movies, sharp text, aspect), the Restore Defaults
// button as "More Settings" (the settings window), the rest hidden as AV Options hides its own. The game's widget
// messages drive it as AV Options' handler 0x8D7A0 does (sub_72820(player, id, message, a, b)): a slider 0x27 its
// steps, 0x69, 0x2E its value, 0x40 read; a choice 0x17 cleared, 0x10 a choice added (its text, its value), 0x1E the
// choice of a value, 0x35 its value read; 0x2B 1
// hides; the page 0x4C entered, 0x4B A on an item (kept, and back with 0x5F). The help box (0x10000225) gets each row's
// line as it is focused.
//
// Edit Codename's list is the table 0x17CEC8 (7 entries of 24 bytes: picture, label, help, index, enabled, help when
// disabled). The list's handler 0x8C7F0 sets its range (message 0x27, 0 to 6) as it is made (0x51), on a focus move
// (0x49, 0x54) has 0x7F680 draw the rows' labels, help and picture from the table, and on A reads the index (0x40) and
// goes to that entry's page. It gets a copy with an eighth entry, ours, whose label and help are handles GetText
// (0x6D460) is made to answer; 0x8C7F0's direct call to 0x7F680 bypasses that function's hook, so the focus moves are
// done in 0x8C7F0's hook. Before the list is made its "index" is the widget's own pointer, which the game's
// arithmetic wraps into readable memory and the copy's would not: the copy is used only for a real index.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

#include "app/app.h"
#include "common/nightfire.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace nf {
extern uint32_t fov_percent;
}

namespace {
using kernel::R32;
using kernel::W32;

constexpr uint32_t kPage = 0x4000FC10;
constexpr uint32_t kFov = 0x1000FC11, kMouse = 0x1000FC12, kInvert = 0x1000FC13, kSkip = 0x1000FC14,
                   kSharp = 0x1000FC15, kAspect = 0x1000FC16, kMore = 0x1000FC17;
constexpr uint32_t kHidden[] = {0x1000FC18, 0x1000FC19, 0x1000FC1A};
constexpr uint32_t kHelpBox = 0x10000225;
constexpr uint32_t kSetById = 0x72820, kGoTo = 0x92C40, kSetWidget = 0x72630;
constexpr uint32_t kMenuContext = 0x25F1D0, kContextSize = 0x1D8;
constexpr uint32_t kList = 0x10000100;  // Edit Codename's list widget
constexpr uint32_t kTable = 0x17CEC8, kEntry = 24, kEntries = 7;
constexpr uint32_t kLabelHandle = 0x0000FC01, kHelpHandle = 0x0100FC02;  // no text bank goes that far
constexpr uint32_t kAvPicture = 0x3000125;

uint32_t GuestString(const char* s) {
    uint32_t n = uint32_t(std::strlen(s)) + 1, va = mem::AllocSystem(n);
    std::memcpy(kernel::HostPtr(va), s, n);
    return va;
}

struct Row {
    uint32_t id;
    const char* label;
    const char* help;
};
constexpr Row kRows[] = {
    {kFov, "Field of View", "How wide the view is, on foot and driving. The game's is in the middle."},
    {kMouse, "Mouse Sensitivity", "How far the view turns for the mouse's movement."},
    {kInvert, "Invert Mouse", "Moving the mouse forward looks down."},
    {kSkip, "Skip Intro Movies", "Starts without the EA and MGM logo movies."},
    {kSharp, "Sharp Text", "Draws the text and HUD pictures sharp at high resolutions."},
    {kAspect, "Aspect Ratio", "Auto follows the screen; Fill widens the view to an ultrawide screen."},
    {kMore, "More Settings", "Display, sound, controls and the rest in the settings window."},
};

uint32_t Str(const char* s) {
    static std::vector<std::pair<const char*, uint32_t>> made;
    for (auto& [k, v] : made)
        if (k == s) return v;
    uint32_t va = GuestString(s);
    made.push_back({s, va});
    return va;
}

// a guest call's result is CallGuest's (the context is as it was after)
uint32_t SetById(X86Context& ctx, uint32_t player, uint32_t id, uint32_t msg, uint32_t a, uint32_t b = 0) {
    return kernel::CallGuest(ctx, kSetById, {player, id, msg, a, b});
}

// the settings as the page's controls hold them
uint32_t FovStep() { return uint32_t(std::clamp<int>((int(nf::fov_percent) - 50 + 2) / 5, 0, 20)); }  // 50-150 %
uint32_t MouseStep() {  // 25% to 400%, 100% the middle: 25 * 2^(step / 5)
    double s = std::max(1u, app::Get().mouse_sensitivity) / 25.0;
    return uint32_t(std::clamp<long>(std::lround(5 * std::log2(s)), 0, 20));
}

void Slider(X86Context& ctx, uint32_t player, uint32_t id, uint32_t value) {
    SetById(ctx, player, id, 0x27, 0, 20);
    SetById(ctx, player, id, 0x69, 0);
    SetById(ctx, player, id, 0x2E, value);
}
void Choice(X86Context& ctx, uint32_t player, uint32_t id, std::initializer_list<const char*> items, uint32_t index) {
    SetById(ctx, player, id, 0x17, 0);
    uint32_t value = 0;  // each choice's value (b), which 0x1E selects by and 0x35 gives back
    for (const char* s : items) SetById(ctx, player, id, 0x10, Str(s), value++);
    SetById(ctx, player, id, 0x1E, index);
}

void Enter(X86Context& ctx, uint32_t player) {
    const app::Settings& s = app::Get();
    Slider(ctx, player, kFov, FovStep());
    Slider(ctx, player, kMouse, MouseStep());
    Choice(ctx, player, kInvert, {"Off", "On"}, s.mouse_invert_y ? 1 : 0);
    Choice(ctx, player, kSkip, {"Off", "On"}, nf::skip_intro ? 1 : 0);
    Choice(ctx, player, kSharp, {"Off", "On"}, s.sharp_2d ? 1 : 0);
    Choice(ctx, player, kAspect, {"Auto", "4:3", "16:9", "Fill"}, std::min<uint32_t>(s.aspect, 3));
    for (uint32_t id : kHidden) SetById(ctx, player, id, 0x2B, 1);
    LOGR("nightfire: PC Settings page entered");
}

void Keep(X86Context& ctx, uint32_t player) {
    uint32_t fov = SetById(ctx, player, kFov, 0x40, 0) & 0xFF, mouse = SetById(ctx, player, kMouse, 0x40, 0) & 0xFF;
    uint32_t invert = SetById(ctx, player, kInvert, 0x35, 0) & 0xFF, skip = SetById(ctx, player, kSkip, 0x35, 0) & 0xFF;
    uint32_t sharp = SetById(ctx, player, kSharp, 0x35, 0) & 0xFF;
    uint32_t aspect = SetById(ctx, player, kAspect, 0x35, 0) & 0xFF;
    std::lock_guard<std::recursive_mutex> lock(app::UiLock());
    app::Settings& s = app::Get();
    nf::fov_percent = 50 + std::min(fov, 20u) * 5;
    s.mouse_sensitivity = uint32_t(std::lround(25 * std::pow(2.0, std::min(mouse, 20u) / 5.0)));
    s.mouse_invert_y = invert == 1;
    nf::skip_intro = skip == 1;
    s.sharp_2d = sharp == 1;
    bool display = s.aspect != std::min(aspect, 3u);
    s.aspect = std::min(aspect, 3u);
    app::Save();
    if (display) app::ApplyDisplay();
    LOGR("nightfire: PC Settings kept: field of view %u%%, mouse %u%%, invert %d, skip intro %d, sharp text %d, "
         "aspect %u",
         nf::fov_percent, s.mouse_sensitivity, s.mouse_invert_y, nf::skip_intro, s.sharp_2d, s.aspect);
}

// Edit Codename's list with our entry: the game's seven entries as they are now (it changes Save Codename's label as
// the page opens), then ours
// (and what follows the table, as the game has it: the list reads past its end before it is set up)
constexpr uint32_t kCopied = 64;
uint32_t ListCopy() {
    static uint32_t copy = 0;
    // past the title's memory, where the recompiled code reads it as it is (AllocSystem's pages it does not)
    if (!copy) copy = mem::AllocHostContiguous(kCopied * kEntry, 16);
    std::memcpy(kernel::HostPtr(copy), kernel::HostPtr(kTable), kCopied * kEntry);
    uint32_t e = copy + kEntries * kEntry;
    const uint32_t ours[6] = {kAvPicture, kLabelHandle, kHelpHandle, kEntries, 1, 0};
    for (int i = 0; i < 6; i++) W32(e + uint32_t(i) * 4, ours[i]);
    return copy;
}

bool Ours(uint32_t id) {
    for (const Row& r : kRows)
        if (r.id == id) return true;
    return false;
}

}  // namespace

namespace nf {
// the page's messages, from the dispatcher 0x8E320 (src/game/pc_settings_item.cpp): true when handled (al 1)
bool PcPageMessage(X86Context& ctx, uint32_t player, uint32_t widget, uint32_t msg, uint32_t arg) {
    uint32_t id = R32(widget + 0x18);
    if (id == kPage) {
        if (msg == 0x4C) Enter(ctx, player);
        if (msg == 0x4B) {
            if (arg && R32(arg + 0x18) == kMore) {
                std::lock_guard<std::recursive_mutex> lock(app::UiLock());
                if (!app::OverlayOpen()) app::ToggleOverlay();
                return true;
            }
            Keep(ctx, player);
            uint32_t eax = ctx.eax;
            kernel::CallGuest(ctx, kGoTo, {kMenuContext + (player & 0xFF) * kContextSize, 0x5F, 0, 0});
            ctx.eax = eax;
        }
        return true;
    }
    if (msg == 0x4E && Ours(id))  // focus gained: the row's help line
        for (const Row& r : kRows)
            if (r.id == id) SetById(ctx, player, kHelpBox, 0x18, Str(r.help));
    return false;
}

// the page from anywhere in the front end (the main menu's item)
void OpenPcPage(X86Context& ctx, uint32_t player) {
    uint32_t eax = ctx.eax;
    kernel::CallGuest(ctx, kGoTo, {kMenuContext + (player & 0xFF) * kContextSize, 0x44, kPage, 0});
    ctx.eax = eax;
}

// the ids the menu file's clone of AV Options gets (src/game/pc_settings_item.cpp): AV's, ours, and the labels
uint32_t PcPageId(uint32_t av) {
    switch (av) {
        case 0x40000031: return kPage;
        case 0x10000135: return kFov;
        case 0x10000134: return kMouse;
        case 0x10000136: return kInvert;
        case 0x1000019A: return kSkip;
        case 0x10000198: return kSharp;
        case 0x10000223: return kAspect;
        case 0x100001A3: return kMore;
        case 0x10000137: return kHidden[0];
        case 0x10000138: return kHidden[1];
        case 0x10000222: return kHidden[2];
        default: return av;
    }
}
const char* PcPageLabel(uint32_t id) {
    if (id == kPage) return "PC Settings";
    for (const Row& r : kRows)
        if (r.id == id) return r.label;
    return nullptr;
}
}  // namespace nf

// the list's table (cdecl: the list widget, the table, its count, ...)
X86_EXTERN_FUNC(__imp__sub_000759D0);
X86_FUNC(sub_000759D0) {
    if (R32(ctx.esp + 8) == kTable && R32(ctx.esp + 0xC) == kEntries) {
        W32(ctx.esp + 8, ListCopy());
        W32(ctx.esp + 0xC, kEntries + 1);
        static bool said = false;
        if (!said) said = true, LOGR("nightfire: PC Settings added to Edit Codename's list");
    }
    __imp__sub_000759D0(ctx, base);
}

// the list's focus (cdecl: player, widget, the table, ...): its help line and picture
X86_EXTERN_FUNC(__imp__sub_0007F680);
X86_FUNC(sub_0007F680) {
    // the rows' labels, help and picture from the copy; not before the list is made (its index then the widget's
    // own pointer, which the game's arithmetic wraps into memory the copy's address would not)
    uint32_t widget = R32(ctx.esp + 8);
    if (widget && R32(ctx.esp + 0xC) == kTable) {
        uint32_t index = kernel::CallGuest(ctx, kSetWidget, {widget, 0x40, 0, 0});
        if (index < kCopied) W32(ctx.esp + 0xC, ListCopy());
    }
    __imp__sub_0007F680(ctx, base);
}

// the list's items (cdecl: player, widget, id, message): A on ours opens the page
X86_EXTERN_FUNC(__imp__sub_0008C7F0);
X86_FUNC(sub_0008C7F0) {
    uint32_t player = R32(ctx.esp + 4), widget = R32(ctx.esp + 8), msg = R32(ctx.esp + 0x10);
    uint32_t index = widget ? kernel::CallGuest(ctx, kSetWidget, {widget, 0x40, 0, 0}) : ~0u;
    if ((msg == 0x49 || msg == 0x54) && index < kCopied) {
        // focus moved: the rows' labels, help and picture (the game's call, with the copy: its direct call to 0x7F680
        // does not come through the hook below); not before the list is made (its index the widget's pointer)
        uint32_t eax = ctx.eax;
        kernel::CallGuest(ctx, 0x7F680, {player, widget, ListCopy(), 0x100000FF, 0x100001A5, 0x100001EC, 0x10000109,
                                         msg == 0x49 ? 1u : 0u});
        ctx.eax = eax;
        ctx.eax = 1;
        ctx.esp += 4;
        return;
    }
    if (msg == 0x4B && widget) {
        if ((index & 0xFF) == kEntries) {
            nf::OpenPcPage(ctx, player);
            ctx.eax = 1;
            ctx.esp += 4;
            return;
        }
    }
    __imp__sub_0008C7F0(ctx, base);
    if (msg == 0x51 && widget) {  // the list finished: its range the game's 0 to 6 and ours, 7
        uint32_t eax = ctx.eax;
        kernel::CallGuest(ctx, kSetWidget, {widget, 0x27, 0, kEntries});
        ctx.eax = eax;
    }
}

// GetText (cdecl: the handle, ...): our entry's label and help
X86_EXTERN_FUNC(__imp__sub_0006D460);
X86_FUNC(sub_0006D460) {
    uint32_t h = R32(ctx.esp + 4);
    if (h == kLabelHandle || h == kHelpHandle) {
        ctx.eax = Str(h == kLabelHandle ? "PC Settings" : "Picture, controls and more for this PC.");
        ctx.esp += 4;
        return;
    }
    __imp__sub_0006D460(ctx, base);
}
