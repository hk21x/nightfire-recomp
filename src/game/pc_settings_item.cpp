// "PC Settings" in the game's own main menu (default.xbe): a fourth item under NightFire / Multiplayer / Codenames
// that opens the settings overlay.
//
// Eurocom's menus are data: one menu file sits in memory as a record stream ([0x25FB08]), and the builder 0x93960
// makes the widgets of a page from it. The main menu is page 0x40000002 (var 0x80000002) in the front end's file;
// its items are widget records (F9) each followed by attribute records (F6, message 0x18 the label). The builder is
// given a copy of the stream with the Codenames item's records cloned as the new item (its own id, a label string
// of ours) and the items moved up to make room. A press of A reaches the per-id dispatcher 0x8E320, where the new
// id opens the overlay; the help box under the menu gets its line from 0x959E0, which knows only the game's items.
// (.local/menu_re.md has the record formats and how they were found.)
#include <cstring>
#include <mutex>
#include <vector>

#include "app/app.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace nf {
bool PcPageMessage(X86Context& ctx, uint32_t player, uint32_t widget, uint32_t msg, uint32_t arg);  // pc_settings_page
void OpenPcPage(X86Context& ctx, uint32_t player);
uint32_t PcPageId(uint32_t av);
const char* PcPageLabel(uint32_t id);
}  // namespace nf

namespace {
using kernel::R32;
using kernel::W32;

constexpr uint32_t kStreamPtr = 0x25FB08;  // the resident menu file the builder reads
constexpr uint32_t kHelpBox = 0x25FC18;    // the main menu's help box widget
constexpr uint32_t kSetWidget = 0x72630;   // cdecl (widget, message, a, b)
constexpr uint32_t kMainMenu = 0x40000002, kFrontVar = 0x80000002;
constexpr uint32_t kNightFire = 0x10000002, kMultiplayer = 0x10000003, kCodenames = 0x10000004;
constexpr uint32_t kPcItem = 0x1000FC01;   // ours: no record or code of the game uses it
constexpr uint32_t kAvPage = 0x40000031, kAvTitle = 0x2BB, kPcPage = 0x4000FC10;  // AV Options, its title text
constexpr uint32_t kOpFocus = 0xFFFFFFF4;  // a page's first focus: +0xC the widget
constexpr uint32_t kOpPage = 0xFFFFFFF0, kOpWidget = 0xFFFFFFF9, kOpAttr = 0xFFFFFFF6, kOpEnd = 0xFFFFFFF2;
constexpr uint8_t kMsgText = 0x18;
constexpr uint32_t kActivate = 0x4B;       // a widget message: A on it
constexpr int16_t kRowY[4] = {230, 262, 294, 326};  // the three items moved up a row, ours below them

// bytes of each record by its opcode (the dword before the fields included); 0 ends the stream
uint32_t RecordSize(uint32_t op) {
    switch (op) {
        case 0xFFFFFFF0: return 0x15;  // page: id, var, ?, platform byte, body size
        case 0xFFFFFFF9: return 0x22;  // widget: id, type, platform, x, y, w, h, ...
        case 0xFFFFFFF8: return 0x24;  // name
        case 0xFFFFFFF7: return 0x0C;  // child
        case 0xFFFFFFF6: return 0x0D;  // attribute: message byte, a, b
        case 0xFFFFFFF5: return 0x12;
        case 0xFFFFFFF4: return 0x11;
        case 0xFFFFFFF3: return 0x0D;
        case 0xFFFFFFFC: return 0x12;
        case 0xFFFFFFFB: return 0x24;
        case 0xFFFFFFFD: return 0x06;
        case 0xFFFFFFFE: return 0x08;
        default: return 0;
    }
}

uint32_t Get32(const std::vector<uint8_t>& b, size_t at) {
    uint32_t v;
    std::memcpy(&v, &b[at], 4);
    return v;
}
void Put32(std::vector<uint8_t>& b, size_t at, uint32_t v) { std::memcpy(&b[at], &v, 4); }
void Put16(std::vector<uint8_t>& b, size_t at, int16_t v) { std::memcpy(&b[at], &v, 2); }

uint32_t GuestString(const char* s) {
    uint32_t n = uint32_t(std::strlen(s)) + 1, va = mem::AllocSystem(n);
    std::memcpy(kernel::HostPtr(va), s, n);
    return va;
}

// the menu file with our item, in guest memory; 0 when the stream has no main menu (another menu file)
uint32_t Patched(uint32_t stream) {
    static uint32_t source = 0, made = 0, source_len = 0;
    // the stream's length: its records to the end opcode
    uint32_t at = 4;  // a header dword first
    for (;;) {
        uint32_t op = R32(stream + at);
        if (op == kOpEnd) {
            at += 4;
            break;
        }
        uint32_t n = RecordSize(op);
        if (!n || at > 0x400000) return 0;  // not a stream this walker knows
        at += n;
    }
    if (stream == source && at == source_len) return made;
    source = stream, source_len = at, made = 0;
    std::vector<uint8_t> b(at);
    std::memcpy(b.data(), kernel::HostPtr(stream), at);
    // the main menu page, its three items' widget records, and where the Codenames item's records end
    size_t page = 0, item[3] = {}, codenames_end = 0;
    for (size_t p = 4; p + 4 <= b.size();) {
        uint32_t op = Get32(b, p), n = RecordSize(op);
        if (!n) break;
        if (op == kOpPage) {
            if (page && !codenames_end && item[2]) codenames_end = p;
            if (Get32(b, p + 4) == kMainMenu && Get32(b, p + 8) == kFrontVar) page = p;
        } else if (op == kOpWidget && page && !codenames_end) {
            uint32_t id = Get32(b, p + 4);
            if (item[2] && id != kCodenames) codenames_end = p;
            if (id == kNightFire) item[0] = p;
            if (id == kMultiplayer) item[1] = p;
            if (id == kCodenames) item[2] = p;
        }
        p += n;
    }
    if (!page || !item[0] || !item[1] || !item[2] || !codenames_end) return 0;
    // our item: the Codenames item's records with our id, row and label
    std::vector<uint8_t> ours(b.begin() + item[2], b.begin() + codenames_end);
    Put32(ours, 4, kPcItem);
    Put16(ours, 0xC, kRowY[3]);
    static const uint32_t label = GuestString("PC Settings");
    for (size_t p = 0; p + 4 <= ours.size();) {
        uint32_t op = Get32(ours, p), n = RecordSize(op);
        if (!n) break;
        if (op == kOpAttr && ours[p + 4] == kMsgText) {
            Put32(ours, p + 5, label);  // a: a string used as it is
            Put32(ours, p + 9, 0);
        }
        p += n;
    }
    for (int i = 0; i < 3; i++) Put16(b, item[i] + 0xC, kRowY[i]);
    Put32(b, page + 0x11, Get32(b, page + 0x11) + uint32_t(ours.size()));  // the page's body size
    b.insert(b.begin() + codenames_end, ours.begin(), ours.end());
    // the PC Settings page: AV Options' page cloned after it, its ids and labels ours (src/game/pc_settings_page.cpp)
    for (size_t p = 4; p + 4 <= b.size();) {
        uint32_t op = Get32(b, p), n = RecordSize(op);
        if (!n) break;
        if (op == kOpPage && Get32(b, p + 4) == kAvPage && Get32(b, p + 8) == kFrontVar && b[p + 0x10] == 0) {
            std::vector<uint8_t> page_copy(b.begin() + p, b.begin() + p + n + Get32(b, p + 0x11));
            uint32_t widget = 0;
            for (size_t q = 0; q + 4 <= page_copy.size();) {
                uint32_t o = Get32(page_copy, q), m = RecordSize(o);
                if (!m) break;
                if (o == kOpPage) Put32(page_copy, q + 4, nf::PcPageId(Get32(page_copy, q + 4)));
                if (o == kOpWidget) {
                    widget = nf::PcPageId(Get32(page_copy, q + 4));
                    Put32(page_copy, q + 4, widget);
                }
                if (o == kOpFocus) Put32(page_copy, q + 0xC, nf::PcPageId(Get32(page_copy, q + 0xC)));
                if (o == kOpAttr && page_copy[q + 4] == kMsgText) {
                    const char* label = Get32(page_copy, q + 9) == kAvTitle ? nf::PcPageLabel(kPcPage)
                                                                             : nf::PcPageLabel(widget);
                    if (label) {
                        Put32(page_copy, q + 5, GuestString(label));
                        Put32(page_copy, q + 9, 0);
                    }
                }
                q += m;
            }
            b.insert(b.begin() + p + page_copy.size(), page_copy.begin(), page_copy.end());
            LOGR("nightfire: PC Settings page made from AV Options' (%zu bytes)", page_copy.size());
            break;
        }
        p += n;
    }
    made = mem::AllocSystem(uint32_t(b.size()));
    std::memcpy(kernel::HostPtr(made), b.data(), b.size());
    LOGR("nightfire: PC Settings added to the main menu (menu file %08X, %u bytes)", stream, at);
    return made;
}
}  // namespace

// the page builder (cdecl, 7 arguments): it reads [0x25FB08] only while it runs
X86_EXTERN_FUNC(__imp__sub_00093960);
X86_FUNC(sub_00093960) {
    uint32_t stream = R32(kStreamPtr), copy = stream ? Patched(stream) : 0;
    if (copy) W32(kStreamPtr, copy);
    __imp__sub_00093960(ctx, base);
    if (copy) W32(kStreamPtr, stream);
}

// the per-id message dispatcher (cdecl: player byte, widget, message, widget, argument; al 1 = handled)
X86_EXTERN_FUNC(__imp__sub_0008E320);
X86_FUNC(sub_0008E320) {
    uint32_t widget = R32(ctx.esp + 8), message = R32(ctx.esp + 0xC);
    if (widget && nf::PcPageMessage(ctx, R32(ctx.esp + 4) & 0xFF, widget, message, R32(ctx.esp + 0x14))) {
        ctx.eax = 1;
        ctx.esp += 4;
        return;
    }
    if (!widget || message != kActivate || R32(widget + 0x18) != kPcItem) {
        __imp__sub_0008E320(ctx, base);
        return;
    }
    nf::OpenPcPage(ctx, R32(ctx.esp + 4) & 0xFF);
    LOGR("nightfire: PC Settings opened from the main menu");
    ctx.eax = 1;
    ctx.esp += 4;  // cdecl: the return address only
}

// the help line of the focused item (cdecl, the page at [esp+4]): ours has no entry in the game's table
X86_EXTERN_FUNC(__imp__sub_000959E0);
X86_FUNC(sub_000959E0) {
    uint32_t page = R32(ctx.esp + 4);
    __imp__sub_000959E0(ctx, base);
    uint32_t focused = page ? R32(page + 0xAC) : 0, box = R32(kHelpBox);
    if (!box || !focused || R32(focused + 0x18) != kPcItem) return;
    static const uint32_t help = GuestString("Picture, controls and more for this PC.");
    uint32_t eax = ctx.eax;
    kernel::CallGuest(ctx, kSetWidget, {box, kMsgText, help, 0});
    ctx.eax = eax;
}
