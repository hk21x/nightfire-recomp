// Keyboard prompts: while the keyboard and mouse play, the button pictures show the keys and mouse buttons instead
// (EoN's way, src/common/strokes.h for the shapes and letters).
//
// default.xbe draws every button prompt (the menus' help lines, the controls page, the tutorial's hints) from one
// 256x128 A8R8G8B8 swizzled atlas that also holds the PS2 and GameCube versions' pictures. When it is bound
// (GameInfo::texture_bound) and still as the disc has it (its bytes' FNV-1a), the Xbox pictures are drawn again inside
// their own boxes: each button as the key it is played with (its first binding; in the front end's menus A as Enter
// and B as Backspace, which select and go back there: nf::MenuKeys), the triggers and the White button as the mouse's buttons, the d-pads
// as the arrow keys, the left stick as W A S D, the right stick as the mouse. The atlas is written back into guest
// memory and drawn four times larger for the renderers (gpu::SetReplacement); the controller's look puts the disc's
// bytes back.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "common/nightfire.h"
#include "core/input.h"
#include "core/log.h"
#include "gpu/texture.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "strokes.h"

namespace nf {
namespace {

constexpr uint32_t kArgb = 0x06;               // D3DFMT_A8R8G8B8, swizzled
constexpr int kW = 256, kH = 128;
constexpr uint32_t kBytes = kW * kH * 4;
constexpr uint64_t kOriginal = 0xF0D6752BB8C56E3Bull;  // FNV-1a of default.xbe's atlas as the disc has it

struct Rgba {
    uint8_t r, g, b, a;
};
using Image = std::vector<Rgba>;  // kW * kH, rows

struct Box {
    int x0, y0, x1, y1;  // inclusive
};
// the Xbox pictures' boxes in the atlas (each one's opaque texels)
constexpr Box kFaceA{1, 0, 17, 22}, kFaceB{25, 0, 41, 22}, kFaceX{49, 0, 65, 22}, kFaceY{71, 0, 87, 22};
constexpr Box kWhiteButton{92, 0, 108, 21}, kBlackButton{116, 0, 132, 21};
constexpr Box kStart{0, 23, 25, 45}, kBack{28, 23, 51, 45};
constexpr Box kLeftStick{54, 25, 74, 46}, kRightStick{77, 25, 97, 45};
constexpr Box kPadAll{210, 1, 231, 22}, kPadUpDown{1, 48, 22, 69}, kPadLeftRight{26, 48, 47, 69};
constexpr Box kLeftTrigger{52, 48, 65, 69}, kRightTrigger{70, 48, 83, 69};
constexpr Box kDrawn[] = {kFaceA, kFaceB, kFaceX, kFaceY, kWhiteButton, kBlackButton, kStart, kBack, kLeftStick,
                          kRightStick, kPadAll, kPadUpDown, kPadLeftRight, kLeftTrigger, kRightTrigger};

constexpr Rgba kClear{0, 0, 0, 0};
constexpr Colour kCapEdge{200, 200, 200, 255}, kDimEdge{75, 75, 75, 255}, kDimText{95, 95, 95, 255};
constexpr Colour kDark{30, 30, 30, 255}, kWhite{255, 255, 255, 255};

// what the pictures show: the disc's, or the keys and mouse buttons bound to each control, in a menu or in play
struct Look {
    bool keys = false, menu = false;
    std::array<std::array<uint16_t, 2>, input::kControls> bound{};
    bool operator==(const Look&) const = default;
};

Look Current() {
    Look l;
    l.keys = input::UsingKeyboard();
    if (!l.keys) return l;
    l.menu = MenuKeys();
    for (int c = 0; c < input::kControls; c++) l.bound[size_t(c)] = input::ControlKeys(c);
    if (l.menu) {  // A selects and B goes back with these in the menus (src/common/menu_mouse.cpp)
        l.bound[input::kA] = {0x0D, 0};
        l.bound[input::kB] = {0x08, 0};
    }
    return l;
}

// Windows virtual keys the pictures name
constexpr uint16_t kVkLeftButton = 0x01, kVkRightButton = 0x02, kVkMiddleButton = 0x04, kVkLeft = 0x25, kVkUp = 0x26,
                   kVkRight = 0x27, kVkDown = 0x28;

bool MouseButton(uint16_t vk) { return vk == kVkLeftButton || vk == kVkRightButton || vk == kVkMiddleButton; }

// a key's name on its cap, empty for the arrows (drawn as heads) and keys it does not know
std::string KeyLabel(uint16_t vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, char(vk));
    if (vk >= 0x70 && vk <= 0x7B) return "F" + std::to_string(vk - 0x70 + 1);
    switch (vk) {
        case 0x08: return "BKSP";
        case 0x09: return "TAB";
        case 0x0D: return "ENTER";
        case 0x10: case 0xA0: case 0xA1: return "SHIFT";
        case 0x11: case 0xA2: case 0xA3: return "CTRL";
        case 0x12: case 0xA4: case 0xA5: return "ALT";
        case 0x1B: return "ESC";
        case 0x20: return "SPACE";
        case 0x2E: return "DEL";
        default: return "";
    }
}

// a key: a dark cap with a light edge and its name, or an arrow's head; dim when not lit
void KeyCap(Canvas& c, Area cap, uint16_t vk, bool lit = true) {
    float w = cap.x1 - cap.x0, h = cap.y1 - cap.y0, r = std::min(w, h) * 0.2f, edge = std::max(0.5f, std::min(w, h) * 0.07f);
    Fill(c, cap, lit ? kCapEdge : kDimEdge, [&](float x, float y) { return BoxDistance(x, y, cap, r, r); });
    Fill(c, cap, kDark, [&](float x, float y) { return BoxDistance(x, y, cap, r, r) + edge; });
    Colour ink = lit ? kWhite : kDimText;
    float cx = (cap.x0 + cap.x1) / 2, cy = (cap.y0 + cap.y1) / 2;
    if (vk >= kVkLeft && vk <= kVkDown) {
        float k = std::min(w, h) * 0.22f;
        std::vector<Pt> head;
        if (vk == kVkUp) head = {{cx, cy - k}, {cx + k, cy + k * 0.7f}, {cx - k, cy + k * 0.7f}};
        if (vk == kVkDown) head = {{cx, cy + k}, {cx - k, cy - k * 0.7f}, {cx + k, cy - k * 0.7f}};
        if (vk == kVkLeft) head = {{cx - k, cy}, {cx + k * 0.7f, cy - k}, {cx + k * 0.7f, cy + k}};
        if (vk == kVkRight) head = {{cx + k, cy}, {cx - k * 0.7f, cy + k}, {cx - k * 0.7f, cy - k}};
        Fill(c, cap, ink, [&](float x, float y) { return PolygonDistance(x, y, head); });
        return;
    }
    std::string label = KeyLabel(vk);
    if (label.empty()) return;
    float height = std::min(h * 0.5f, (w - 2 * edge - std::max(1.0f, w * 0.12f)) / TextWidth(label.c_str(), 1));
    Text(c, label.c_str(), cx, cy, height, std::max(0.45f, height * 0.2f), ink);
}

// a mouse from above, the button pressed lit (none: just the mouse, for looking about)
void Mouse(Canvas& c, Area a, uint16_t vk) {
    float h = (a.y1 - a.y0) * 0.92f, w = std::min(h * 0.64f, (a.x1 - a.x0) * 0.92f);
    float cx = (a.x0 + a.x1) / 2, cy = (a.y0 + a.y1) / 2;
    Area body{cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2};
    float r = w / 2, edge = std::max(0.5f, w * 0.08f), split = body.y0 + h * 0.42f;
    Fill(c, body, kCapEdge, [&](float x, float y) { return BoxDistance(x, y, body, r, r * 0.9f); });
    Fill(c, body, kDark, [&](float x, float y) { return BoxDistance(x, y, body, r, r * 0.9f) + edge; });
    auto inside = [&](float x, float y) { return BoxDistance(x, y, body, r, r * 0.9f) + edge * 1.8f; };
    float gap = edge * 0.8f;
    if (vk == kVkLeftButton)
        Fill(c, body, kWhite, [&](float x, float y) { return std::max({inside(x, y), x - (cx - gap), y - (split - gap)}); });
    if (vk == kVkRightButton)
        Fill(c, body, kWhite, [&](float x, float y) { return std::max({inside(x, y), (cx + gap) - x, y - (split - gap)}); });
    std::vector<Segment> lines = {{{cx, body.y0 + edge}, {cx, split}}, {{body.x0 + edge, split}, {body.x1 - edge, split}}};
    Fill(c, body, kCapEdge,
         [&](float x, float y) { return std::max(StrokeDistance(x, y, lines, edge * 0.5f), -inside(x, y) - edge); });
    Area wheel{cx - w * 0.09f, body.y0 + h * 0.13f, cx + w * 0.09f, body.y0 + h * 0.31f};
    Fill(c, body, vk == kVkMiddleButton ? kWhite : kDimText,
         [&](float x, float y) { return BoxDistance(x, y, wheel, w * 0.09f, w * 0.09f); });
}

// the key a control is played with (its first binding), in the space `a`
void BoundKey(Canvas& c, Area a, const std::array<uint16_t, 2>& keys) {
    uint16_t vk = keys[0] ? keys[0] : keys[1];
    if (MouseButton(vk)) return Mouse(c, a, vk);
    float w = a.x1 - a.x0, h = a.y1 - a.y0, side = std::min(w, h);
    std::string label = KeyLabel(vk);
    float cw = label.size() > 1 ? w : side;  // a long name gets the whole width
    float cx = (a.x0 + a.x1) / 2, cy = (a.y0 + a.y1) / 2;
    KeyCap(c, {cx - cw / 2, cy - side / 2, cx + cw / 2, cy + side / 2}, vk);
}

// four keys as the arrows lie: one above three; those named lit bright
void Cluster(Canvas& c, Area a, const Look& look, const int dirs[4], int lit) {
    float w = a.x1 - a.x0, h = a.y1 - a.y0, k = std::min(w / 3, h / 2), gap = k * 0.08f;
    float cx = (a.x0 + a.x1) / 2, cy = (a.y0 + a.y1) / 2;
    Area cell[4] = {{cx - k / 2, cy - k, cx + k / 2, cy},             // up
                    {cx - k / 2, cy, cx + k / 2, cy + k},             // down
                    {cx - k * 1.5f, cy, cx - k / 2, cy + k},          // left
                    {cx + k / 2, cy, cx + k * 1.5f, cy + k}};         // right
    for (int i = 0; i < 4; i++) {
        const auto& keys = look.bound[size_t(dirs[i])];
        uint16_t vk = keys[0] ? keys[0] : keys[1];
        // two bindings of which one is an arrow: the arrow, which needs no name
        if (keys[1] >= kVkLeft && keys[1] <= kVkDown && !(keys[0] >= kVkLeft && keys[0] <= kVkDown)) vk = keys[1];
        Area g{cell[i].x0 + gap, cell[i].y0 + gap, cell[i].x1 - gap, cell[i].y1 - gap};
        KeyCap(c, g, vk, lit >> i & 1);
    }
}

Area Inset(Box b, float dx, float dy) { return {b.x0 + dx, b.y0 + dy, b.x1 + 1 - dx, b.y1 + 1 - dy}; }

void DrawKeys(Canvas& c, const Look& look) {
    auto key = [&](Box b, float dx, float dy, int control) { BoundKey(c, Inset(b, dx, dy), look.bound[size_t(control)]); };
    key(kFaceA, 0, 3, input::kA);
    key(kFaceB, 0, 3, input::kB);
    key(kFaceX, 0, 3, input::kX);
    key(kFaceY, 0, 3, input::kY);
    key(kWhiteButton, 0, 2, input::kWhite);
    key(kBlackButton, 0, 2, input::kBlack);
    key(kStart, 0, 4, input::kStart);
    key(kBack, 0, 4, input::kBack);
    key(kLeftTrigger, 0, 1, input::kLT);
    key(kRightTrigger, 0, 1, input::kRT);
    const int dpad[4] = {input::kUp, input::kDown, input::kLeft, input::kRight};
    Cluster(c, Inset(kPadAll, 0, 3), look, dpad, 15);
    Cluster(c, Inset(kPadUpDown, 0, 3), look, dpad, 3);
    Cluster(c, Inset(kPadLeftRight, 0, 3), look, dpad, 12);
    const int move[4] = {input::kLSUp, input::kLSDown, input::kLSLeft, input::kLSRight};
    Cluster(c, Inset(kLeftStick, 0, 3), look, move, 15);
    Mouse(c, Inset(kRightStick, 2, 2), 0);
}

// a swizzled texel's index: x and y bits interleaved, x first, while both sides have bits left
uint32_t Swizzle(int x, int y) {
    uint32_t o = 0;
    int bit = 0;
    for (int s = 1; s < kW || s < kH; s <<= 1) {
        if (s < kW) o |= uint32_t((x & s) ? 1 : 0) << bit++;
        if (s < kH) o |= uint32_t((y & s) ? 1 : 0) << bit++;
    }
    return o;
}

Image Decode(const uint8_t* bytes) {
    Image img(size_t(kW) * kH);
    for (int y = 0; y < kH; y++)
        for (int x = 0; x < kW; x++) {
            const uint8_t* p = bytes + Swizzle(x, y) * 4;  // B G R A
            img[size_t(y) * kW + x] = {p[2], p[1], p[0], p[3]};
        }
    return img;
}

// the atlas onto a canvas `scale` times its size (nearest: the boxes drawn on are cleared first), premultiplied
Canvas ToCanvas(const Image& img, int scale) {
    Canvas c;
    c.scale = scale;
    c.size = kW * scale;  // Fill clips to size both ways: the canvas is kept square, its lower half unused
    c.px.assign(size_t(c.size) * c.size, {0, 0, 0, 0});
    for (int y = 0; y < kH * scale; y++)
        for (int x = 0; x < kW * scale; x++) {
            Rgba t = img[size_t(y / scale) * kW + x / scale];
            float a = t.a / 255.0f;
            c.px[size_t(y) * c.size + x] = {t.r / 255.0f * a, t.g / 255.0f * a, t.b / 255.0f * a, a};
        }
    return c;
}

Rgba Straight(const std::array<float, 4>& p) {
    if (p[3] <= 0) return kClear;
    auto ch = [&](float v) { return uint8_t(std::clamp(v / p[3] * 255 + 0.5f, 0.0f, 255.0f)); };
    return {ch(p[0]), ch(p[1]), ch(p[2]), uint8_t(std::clamp(p[3] * 255 + 0.5f, 0.0f, 255.0f))};
}

constexpr int kSharp = 4;

// the atlas drawn with the keys: its guest bytes, and the picture four times larger for the renderers
void Redraw(const Image& before, const Look& look, std::vector<uint8_t>& bytes, std::vector<uint32_t>& sharp) {
    Image cleared = before;
    for (Box b : kDrawn)
        for (int y = b.y0; y <= b.y1; y++)
            for (int x = b.x0; x <= b.x1; x++) cleared[size_t(y) * kW + x] = kClear;
    Canvas c = ToCanvas(cleared, 1);
    DrawKeys(c, look);
    bytes.assign(kBytes, 0);
    for (int y = 0; y < kH; y++)
        for (int x = 0; x < kW; x++) {
            Rgba p = Straight(c.px[size_t(y) * c.size + x]);
            uint8_t* o = &bytes[Swizzle(x, y) * 4];
            o[0] = p.b, o[1] = p.g, o[2] = p.r, o[3] = p.a;
        }
    Canvas big = ToCanvas(cleared, kSharp);
    DrawKeys(big, look);
    sharp.assign(size_t(kW * kSharp) * (kH * kSharp), 0);
    for (int y = 0; y < kH * kSharp; y++)
        for (int x = 0; x < kW * kSharp; x++) {
            Rgba p = Straight(big.px[size_t(y) * big.size + x]);
            sharp[size_t(y) * kW * kSharp + x] = uint32_t(p.b) | uint32_t(p.g) << 8 | uint32_t(p.r) << 16 | uint32_t(p.a) << 24;
        }
}

uint64_t Fnv(const uint8_t* p, size_t n) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

// the atlas in memory: its bytes as the disc has them, what it was last drawn as, the two last drawings
struct Atlas {
    uint32_t data = 0;
    std::vector<uint8_t> disc;
    Image before;
    uint64_t written = 0;  // FNV-1a of its bytes as last written
    Look drawn;
    struct Drawing {
        Look look;
        std::vector<uint8_t> bytes;
        std::vector<uint32_t> sharp;
    };
    std::vector<Drawing> cache;  // the newest last
};
std::unique_ptr<Atlas> g_atlas;
std::mutex g_lock;  // the title's thread binds, the input poll refreshes


// the atlas drawn for a look (the disc's bytes back for the controller's), in guest memory and for the renderers
void Apply(Atlas& a, const Look& look) {
    auto* bytes = static_cast<uint8_t*>(kernel::HostPtr(a.data));
    if (!look.keys) {
        std::memcpy(bytes, a.disc.data(), kBytes);
        gpu::ClearReplacement(a.data);
    } else {
        auto d = std::find_if(a.cache.begin(), a.cache.end(), [&](const Atlas::Drawing& x) { return x.look == look; });
        if (d == a.cache.end()) {
            Atlas::Drawing drawing{look, {}, {}};
            Redraw(a.before, look, drawing.bytes, drawing.sharp);
            a.cache.push_back(std::move(drawing));
            if (a.cache.size() > 2) a.cache.erase(a.cache.begin());
            d = a.cache.end() - 1;
        }
        std::memcpy(bytes, d->bytes.data(), kBytes);
        gpu::SetReplacement(a.data, kBytes, kW * kSharp, kH * kSharp, d->sharp);
    }
    a.written = Fnv(bytes, kBytes);
    a.drawn = look;
    LOGR("nightfire: the button pictures (%08X) drawn as %s", a.data,
         !look.keys ? "the disc has them" : look.menu ? "the keys and mouse, for a menu" : "the keys and mouse");
}

// the atlas still where it was last seen, as last written
bool Holds(const Atlas& a) {
    return mem::IsCommitted(a.data) && mem::IsCommitted(a.data + kBytes - 1) &&
           Fnv(static_cast<const uint8_t*>(kernel::HostPtr(a.data)), kBytes) == a.written;
}

}  // namespace

// GameInfo::texture_bound: D3DBaseTexture Data (+4, physical) and Format (+12: format bits 8-15, the width's and
// height's log2 in bits 20-23 and 24-27). The atlas is kept while it stays bound (the HUD binds it once), drawn again
// here and from each poll (RefreshButtons) when what the pictures should show changes (the controller or the keyboard
// last played with, a menu up or not, a key bound anew).
bool ButtonPictures(uint32_t header, uint32_t) {
    std::lock_guard<std::mutex> hold(g_lock);
    uint32_t format = kernel::R32(header + 12);
    if ((format >> 8 & 0xFF) != kArgb || (format >> 20 & 15) != 8 || (format >> 24 & 15) != 7) return false;
    uint32_t data = mem::VirtualFromPhysical(kernel::R32(header + 4));
    if (!mem::IsCommitted(data) || !mem::IsCommitted(data + kBytes - 1)) return false;
    auto* bytes = static_cast<uint8_t*>(kernel::HostPtr(data));
    if (g_atlas && g_atlas->data == data && !Holds(*g_atlas)) g_atlas.reset();  // the memory holds something else
    if (!g_atlas || g_atlas->data != data) {
        if (Fnv(bytes, kBytes) != kOriginal) return false;  // not the atlas (or not as the disc has it)
        if (g_atlas && Holds(*g_atlas) && g_atlas->drawn.keys) Apply(*g_atlas, Look{});  // the old copy as it was
        g_atlas = std::make_unique<Atlas>();
        g_atlas->data = data;
        g_atlas->disc.assign(bytes, bytes + kBytes);
        g_atlas->before = Decode(bytes);
        g_atlas->written = kOriginal;
    }
    Look look = Current();
    if (look == g_atlas->drawn) return false;
    Apply(*g_atlas, look);
    return true;
}

// each poll of controller 1 (src/common/menu_mouse.cpp): the atlas bound earlier drawn for what it should show now
void RefreshButtons() {
    std::lock_guard<std::mutex> hold(g_lock);
    if (!g_atlas) return;
    if (!Holds(*g_atlas)) {
        g_atlas.reset();
        return;
    }
    Look look = Current();
    if (look != g_atlas->drawn) Apply(*g_atlas, look);
}
}  // namespace nf
