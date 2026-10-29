// Keyboard prompts: while the keyboard and mouse play, the button pictures show the keys and mouse buttons instead
// (EoN's way, src/common/strokes.h for the shapes and letters).
//
// Each exe draws its button prompts from one atlas that also holds the PS2 and GameCube versions' pictures:
// default.xbe a 256x128 A8R8G8B8 (the menus' help lines, the pause CONTROLS tab, hints), Driving.xbe a 256x256 P8 of
// the HUD and menus (indices into one of three palettes the game binds with it; the pause CONTROLS tab's icons). When
// one is bound (GameInfo::texture_bound) and still as the disc has it (its bytes' FNV-1a), the Xbox pictures are drawn
// again inside their own boxes: each button as the key it is played with (its first binding; in the front end's
// menus A as Enter and B as Backspace, which select and go back there: nf::MenuKeys; on a mounted gun the triggers
// and reload as the mouse buttons and R: nf::GunStyle), the d-pads as the arrow keys, the left stick as W A S D, the
// right stick as the mouse. default.xbe's is written back into guest memory and drawn four times larger for the
// renderers (gpu::SetReplacement); Driving.xbe's texels become the nearest colours of the palette the icons are
// drawn with (its entry under the A icon green), at its own size (a replacement would not follow the palettes). The
// controller's look puts the disc's bytes back. The HUD binds an atlas once, so it is also redrawn from each poll
// (RefreshButtons) when the look changes.
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

struct Rgba {
    uint8_t r, g, b, a;
};
using Image = std::vector<Rgba>;  // rows

struct Box {
    int x0, y0, x1, y1;  // inclusive
};

constexpr Rgba kClear{0, 0, 0, 0};
constexpr Colour kCapEdge{200, 200, 200, 255}, kDimEdge{75, 75, 75, 255}, kDimText{95, 95, 95, 255};
constexpr Colour kDark{30, 30, 30, 255}, kWhite{255, 255, 255, 255};

// what the pictures show: the disc's, or the keys and mouse buttons bound to each control (in a menu, on a gun)
struct Look {
    bool keys = false, menu = false;
    int gun = -1;
    std::array<std::array<uint16_t, 2>, input::kControls> bound{};
    bool operator==(const Look&) const = default;
};

Look Current() {
    Look l;
    l.keys = input::UsingKeyboard();
    if (!l.keys) return l;
    l.menu = MenuKeys();
    l.gun = kDriving ? GunStyle() : -1;
    for (int c = 0; c < input::kControls; c++) l.bound[size_t(c)] = input::ControlKeys(c);
    if (l.menu) {  // A selects and B goes back with these in the menus (src/common/menu_mouse.cpp)
        l.bound[input::kA] = {0x0D, 0};
        l.bound[input::kB] = {0x08, 0};
    }
    if (l.gun >= 0) {  // a mounted gun: the mouse's buttons fire and zoom, R reloads (src/common/menu_mouse.cpp)
        l.bound[input::kRT] = {0x01, 0};
        l.bound[input::kLT] = {0x02, 0};
        l.bound[l.gun == 0 ? input::kX : input::kA] = {'R', 0};
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

// an atlas's Xbox pictures: where each lies, and how it is inset for its key (the art's empty margins)
struct Picture {
    Box box;
    enum Kind { kKey, kCluster, kMouse } kind;
    int control;  // kKey: the control; kCluster: the arms lit (up 1, down 2, left 4, right 8), the d-pad's keys, or
                  // -1 for the left stick's
    float dx, dy;
};

struct Layout {
    uint32_t format;  // D3DFORMAT
    int w, h;
    bool p8;
    uint64_t disc;  // FNV-1a of its bytes as the disc has them
    std::vector<Picture> pictures;
    Box green;  // P8: a texel the icons' palette shows green (the A button's)
};

// default.xbe's (src/game) and Driving.xbe's (src/driving) atlases, as the disc has them
const Layout kDefaultAtlas{0x06, 256, 128, false, 0xF0D6752BB8C56E3Bull,
                      {{{1, 0, 17, 22}, Picture::kKey, input::kA, 0, 3},
                       {{25, 0, 41, 22}, Picture::kKey, input::kB, 0, 3},
                       {{49, 0, 65, 22}, Picture::kKey, input::kX, 0, 3},
                       {{71, 0, 87, 22}, Picture::kKey, input::kY, 0, 3},
                       {{92, 0, 108, 21}, Picture::kKey, input::kWhite, 0, 2},
                       {{116, 0, 132, 21}, Picture::kKey, input::kBlack, 0, 2},
                       {{0, 23, 25, 45}, Picture::kKey, input::kStart, 0, 4},
                       {{28, 23, 51, 45}, Picture::kKey, input::kBack, 0, 4},
                       {{52, 48, 65, 69}, Picture::kKey, input::kLT, 0, 1},
                       {{70, 48, 83, 69}, Picture::kKey, input::kRT, 0, 1},
                       {{54, 25, 74, 46}, Picture::kCluster, -1, 0, 3},
                       {{77, 25, 97, 45}, Picture::kMouse, 0, 2, 2},
                       {{210, 1, 231, 22}, Picture::kCluster, 15, 0, 3},
                       {{1, 48, 22, 69}, Picture::kCluster, 3, 0, 3},
                       {{26, 48, 47, 69}, Picture::kCluster, 12, 0, 3}},
                      {}};
const Layout kDrivingAtlas{0x0B, 256, 256, true, 0x96C6096C3CB2BF53ull,
                      {{{224, 97, 238, 114}, Picture::kKey, input::kA, 0, 1},
                       {{232, 20, 245, 40}, Picture::kKey, input::kB, 0, 2},
                       {{232, 1, 245, 19}, Picture::kKey, input::kX, 0, 1},
                       {{235, 195, 249, 214}, Picture::kKey, input::kY, 0, 2},
                       {{235, 215, 249, 234}, Picture::kKey, input::kBlack, 0, 2},
                       {{235, 235, 249, 254}, Picture::kKey, input::kWhite, 0, 2},
                       {{162, 58, 184, 75}, Picture::kKey, input::kStart, 0, 2},
                       {{162, 138, 184, 157}, Picture::kKey, input::kBack, 0, 2},
                       {{241, 111, 255, 128}, Picture::kKey, input::kLT, 0, 1},
                       {{241, 158, 255, 172}, Picture::kKey, input::kRT, 0, 0},
                       {{211, 19, 229, 36}, Picture::kCluster, -1, 0, 1},
                       {{211, 0, 229, 15}, Picture::kMouse, 0, 1, 0},
                       {{220, 117, 239, 136}, Picture::kCluster, 3, 0, 1},
                       {{222, 137, 241, 156}, Picture::kCluster, 3, 0, 1},
                       {{221, 160, 240, 178}, Picture::kCluster, 12, 0, 1},
                       {{160, 158, 182, 178}, Picture::kCluster, 12, 0, 1}},
                      {228, 103, 230, 105}};

void DrawKeys(Canvas& c, const Layout& lay, const Look& look) {
    const int dpad[4] = {input::kUp, input::kDown, input::kLeft, input::kRight};
    const int move[4] = {input::kLSUp, input::kLSDown, input::kLSLeft, input::kLSRight};
    for (const Picture& p : lay.pictures) {
        Area a = Inset(p.box, p.dx, p.dy);
        if (p.kind == Picture::kKey) BoundKey(c, a, look.bound[size_t(p.control)]);
        else if (p.kind == Picture::kMouse && kDriving && look.gun < 0) BoundKey(c, a, look.bound[input::kRThumb]);  // look back
        else if (p.kind == Picture::kMouse) Mouse(c, a, 0);
        else if (p.control < 0) Cluster(c, a, look, move, 15);
        else Cluster(c, a, look, dpad, p.control);
    }
}

// a swizzled texel's index: x and y bits interleaved, x first, while both sides have bits left
uint32_t Swizzle(int x, int y, int w, int h) {
    uint32_t o = 0;
    int bit = 0;
    for (int s = 1; s < w || s < h; s <<= 1) {
        if (s < w) o |= uint32_t((x & s) ? 1 : 0) << bit++;
        if (s < h) o |= uint32_t((y & s) ? 1 : 0) << bit++;
    }
    return o;
}

// the atlas onto a square canvas `scale` times its width (nearest: the boxes drawn on are cleared first),
// premultiplied
Canvas ToCanvas(const Image& img, int w, int h, int scale) {
    Canvas c;
    c.scale = scale;
    c.size = std::max(w, h) * scale;
    c.px.assign(size_t(c.size) * c.size, {0, 0, 0, 0});
    for (int y = 0; y < h * scale; y++)
        for (int x = 0; x < w * scale; x++) {
            Rgba t = img[size_t(y / scale) * w + x / scale];
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

int Dist4(Rgba x, Rgba y) {
    return (x.r - y.r) * (x.r - y.r) + (x.g - y.g) * (x.g - y.g) + (x.b - y.b) * (x.b - y.b) + (x.a - y.a) * (x.a - y.a);
}

uint64_t Fnv(const uint8_t* p, size_t n) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

constexpr int kSharp = 4;

// an atlas in memory: its bytes as the disc has them, decoded, what it was last drawn as, the two last drawings
struct Atlas {
    const Layout* lay = nullptr;
    uint32_t data = 0, bytes = 0;
    std::vector<uint8_t> disc;
    Image before;
    std::vector<Rgba> palette;  // P8: the icons' palette
    uint64_t written = 0;       // FNV-1a of its bytes as last written
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

void Decode(Atlas& a) {
    const Layout& l = *a.lay;
    a.before.assign(size_t(l.w) * l.h, kClear);
    for (int y = 0; y < l.h; y++)
        for (int x = 0; x < l.w; x++) {
            uint32_t i = Swizzle(x, y, l.w, l.h);
            if (l.p8) {
                uint8_t k = a.disc[i];
                a.before[size_t(y) * l.w + x] = k < a.palette.size() ? a.palette[k] : kClear;
            } else {
                const uint8_t* p = &a.disc[i * 4];  // B G R A
                a.before[size_t(y) * l.w + x] = {p[2], p[1], p[0], p[3]};
            }
        }
}

// the atlas drawn with the keys: its guest bytes, and (A8R8G8B8) the picture four times larger for the renderers
void Redraw(const Atlas& a, const Look& look, std::vector<uint8_t>& bytes, std::vector<uint32_t>& sharp) {
    const Layout& l = *a.lay;
    Image cleared = a.before;
    for (const Picture& p : l.pictures)
        for (int y = p.box.y0; y <= p.box.y1; y++)
            for (int x = p.box.x0; x <= p.box.x1; x++) cleared[size_t(y) * l.w + x] = kClear;
    Canvas c = ToCanvas(cleared, l.w, l.h, 1);
    DrawKeys(c, l, look);
    bytes = a.disc;
    int clear = -1;  // P8: a fully clear colour for the texels cleared
    for (size_t i = 0; i < a.palette.size() && clear < 0; i++)
        if (a.palette[i].a == 0) clear = int(i);
    for (const Picture& p : l.pictures)
        for (int y = p.box.y0; y <= p.box.y1; y++)
            for (int x = p.box.x0; x <= p.box.x1; x++) {
                Rgba px = Straight(c.px[size_t(y) * c.size + x]);
                uint32_t i = Swizzle(x, y, l.w, l.h);
                if (!l.p8) {
                    bytes[i * 4] = px.b, bytes[i * 4 + 1] = px.g, bytes[i * 4 + 2] = px.r, bytes[i * 4 + 3] = px.a;
                    continue;
                }
                if (px.a < 8 && clear >= 0) {
                    bytes[i] = uint8_t(clear);
                    continue;
                }
                size_t best = 0;
                for (size_t k = 1; k < a.palette.size(); k++)
                    if (Dist4(px, a.palette[k]) < Dist4(px, a.palette[best])) best = k;
                bytes[i] = uint8_t(best);
            }
    sharp.clear();
    if (l.p8) return;
    Canvas big = ToCanvas(cleared, l.w, l.h, kSharp);
    DrawKeys(big, l, look);
    sharp.assign(size_t(l.w * kSharp) * (l.h * kSharp), 0);
    for (int y = 0; y < l.h * kSharp; y++)
        for (int x = 0; x < l.w * kSharp; x++) {
            Rgba p = Straight(big.px[size_t(y) * big.size + x]);
            sharp[size_t(y) * l.w * kSharp + x] =
                uint32_t(p.b) | uint32_t(p.g) << 8 | uint32_t(p.r) << 16 | uint32_t(p.a) << 24;
        }
}

// the atlas drawn for a look (the disc's bytes back for the controller's), in guest memory and for the renderers
void Apply(Atlas& a, const Look& look) {
    auto* bytes = static_cast<uint8_t*>(kernel::HostPtr(a.data));
    if (!look.keys) {
        std::memcpy(bytes, a.disc.data(), a.bytes);
        if (!a.lay->p8) gpu::ClearReplacement(a.data);
    } else {
        auto d = std::find_if(a.cache.begin(), a.cache.end(), [&](const Atlas::Drawing& x) { return x.look == look; });
        if (d == a.cache.end()) {
            Atlas::Drawing drawing{look, {}, {}};
            Redraw(a, look, drawing.bytes, drawing.sharp);
            a.cache.push_back(std::move(drawing));
            if (a.cache.size() > 2) a.cache.erase(a.cache.begin());
            d = a.cache.end() - 1;
        }
        std::memcpy(bytes, d->bytes.data(), a.bytes);
        if (!a.lay->p8)
            gpu::SetReplacement(a.data, a.bytes, uint32_t(a.lay->w * kSharp), uint32_t(a.lay->h * kSharp), d->sharp);
    }
    a.written = Fnv(bytes, a.bytes);
    a.drawn = look;
    LOGR("nightfire: the button pictures (%08X) drawn as %s", a.data,
         !look.keys ? "the disc has them" : look.menu ? "the keys and mouse, for a menu"
                                                      : look.gun >= 0 ? "the keys and mouse, for a gun" : "the keys and mouse");
}

// the atlas still where it was last seen, as last written
bool Holds(const Atlas& a) {
    return mem::IsCommitted(a.data) && mem::IsCommitted(a.data + a.bytes - 1) &&
           Fnv(static_cast<const uint8_t*>(kernel::HostPtr(a.data)), a.bytes) == a.written;
}

}  // namespace

// GameInfo::texture_bound: D3DBaseTexture Data (+4, physical) and Format (+12: format bits 8-15, the width's and
// height's log2 in bits 20-23 and 24-27); D3DPalette Common (+0, its size code in the top two bits) and Data (+4).
// The atlas is kept while it stays where it is, drawn again here and from each poll (RefreshButtons) when what the
// pictures should show changes (the controller or the keyboard last played with, a menu, a gun, a key bound anew).
bool ButtonPictures(uint32_t header, uint32_t palette) {
    std::lock_guard<std::mutex> hold(g_lock);
    const Layout& lay = kDriving ? kDrivingAtlas : kDefaultAtlas;
    uint32_t format = kernel::R32(header + 12);
    if ((format >> 8 & 0xFF) != lay.format || (1 << (format >> 20 & 15)) != lay.w || (1 << (format >> 24 & 15)) != lay.h)
        return false;
    uint32_t data = mem::VirtualFromPhysical(kernel::R32(header + 4)), size = uint32_t(lay.w * lay.h * (lay.p8 ? 1 : 4));
    if (!mem::IsCommitted(data) || !mem::IsCommitted(data + size - 1)) return false;
    auto* bytes = static_cast<uint8_t*>(kernel::HostPtr(data));
    if (g_atlas && g_atlas->data == data && !Holds(*g_atlas)) g_atlas.reset();  // the memory holds something else
    if (!g_atlas || g_atlas->data != data) {
        if (Fnv(bytes, size) != lay.disc) return false;  // not the atlas (or not as the disc has it)
        if (g_atlas && Holds(*g_atlas) && g_atlas->drawn.keys) Apply(*g_atlas, Look{});  // the old copy as it was
        auto a = std::make_unique<Atlas>();
        a->lay = &lay;
        a->data = data;
        a->bytes = size;
        a->disc.assign(bytes, bytes + size);
        a->written = lay.disc;
        g_atlas = std::move(a);
    }
    Atlas& a = *g_atlas;
    if (lay.p8 && a.palette.empty()) {
        // the icons' palette: the one whose colour under the A button is green (BGRA, 256 entries fewer by its size code)
        if (!palette) return false;
        uint32_t colours = 256u >> (kernel::R32(palette) >> 30), pal = mem::VirtualFromPhysical(kernel::R32(palette + 4));
        if (!mem::IsCommitted(pal) || !mem::IsCommitted(pal + colours * 4 - 1)) return false;
        const uint8_t* bgra = static_cast<const uint8_t*>(kernel::HostPtr(pal));
        uint8_t k = a.disc[Swizzle(lay.green.x0, lay.green.y0, lay.w, lay.h)];
        if (k >= colours) return false;
        const uint8_t* e = bgra + k * 4;
        if (!(e[1] > e[2] + 24 && e[1] > e[0] + 24)) return false;  // another palette: not yet
        for (uint32_t i = 0; i < colours; i++) a.palette.push_back({bgra[i * 4 + 2], bgra[i * 4 + 1], bgra[i * 4], bgra[i * 4 + 3]});
    }
    if (a.before.empty()) Decode(a);
    Look look = Current();
    if (look == a.drawn) return false;
    Apply(a, look);
    return true;
}

// each poll of controller 1 (src/common/menu_mouse.cpp): the atlas bound earlier drawn for what it should show now
void RefreshButtons() {
    std::lock_guard<std::mutex> hold(g_lock);
    if (!g_atlas || g_atlas->before.empty()) return;
    if (!Holds(*g_atlas)) {
        g_atlas.reset();
        return;
    }
    Look look = Current();
    if (look != g_atlas->drawn) Apply(*g_atlas, look);
}
}  // namespace nf
