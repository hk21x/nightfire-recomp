// Clean text: the fonts drawn again eight times larger with smooth edges, for the renderers (gpu::SetReplacement).
//
// Both exes' fonts are pixel art with next to no soft edge (default.xbe's 512x64 A8R8G8B8, swizzled; Driving.xbe's
// 256x108 LIN_A8), so enlarged by interpolation (the runtime's sharp 2D) their curves and diagonals keep a stepped
// rim. Here a glyph's shape is rebuilt instead: the mask (alpha half or more) is enlarged four times by Scale2x twice,
// which keeps corners and straight strokes and turns 45 degree stairs into diagonals; a Gaussian half a source texel
// wide over that irons out the stairs of shallower slopes (Scale2x alone left bumps on them: Harry's screenshot)
// without closing a glyph's one-texel gaps; the field is taken twice larger again and cut at its middle a pixel
// wide, which gives the edge its antialiasing. The colour is the nearest opaque texel's. The title's picture wins over the
// runtime's sharpened one; the guest bytes stay as they are (a font changed in memory drops it, and it is made again
// at the next bind).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/nightfire.h"
#include "core/log.h"
#include "gpu/texture.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace nf {
namespace {

constexpr int kScale = 8;      // Scale2x twice, then the field twice
constexpr float kSigma = 2.0f;  // the Gaussian's, in Scale2x's texels (half a source texel)
constexpr int kReach = 6;       // texels each side it takes

struct Font {
    uint32_t format, w, h;
};
// default.xbe's and Driving.xbe's (src/game, src/driving)
constexpr Font kFonts[] = {{0x06, 512, 64}, {0x1F, 256, 108}};

// Scale2x (EPX) on a mask: each texel four, a corner taking its two neighbours' value where they agree and the
// others differ
std::vector<uint8_t> Scale2x(const std::vector<uint8_t>& m, int w, int h) {
    auto at = [&](int x, int y) { return m[size_t(std::clamp(y, 0, h - 1)) * w + size_t(std::clamp(x, 0, w - 1))]; };
    std::vector<uint8_t> out(size_t(w) * h * 4);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t p = at(x, y), a = at(x, y - 1), b = at(x + 1, y), c = at(x - 1, y), d = at(x, y + 1);
            uint8_t e0 = p, e1 = p, e2 = p, e3 = p;
            if (c == a && c != d && a != b) e0 = a;
            if (a == b && a != c && b != d) e1 = b;
            if (d == c && d != b && c != a) e2 = c;
            if (b == d && b != a && d != c) e3 = d;
            size_t o = size_t(y * 2) * (w * 2) + size_t(x * 2);
            out[o] = e0, out[o + 1] = e1, out[o + size_t(w * 2)] = e2, out[o + size_t(w * 2) + 1] = e3;
        }
    return out;
}

// a 0/1 mask blurred by the Gaussian (separable; outside the mask is clear)
std::vector<float> Blur(const std::vector<uint8_t>& m, int w, int h) {
    float k[2 * kReach + 1], ks = 0;
    for (int i = -kReach; i <= kReach; i++) ks += k[i + kReach] = std::exp(-float(i * i) / (2 * kSigma * kSigma));
    for (float& v : k) v /= ks;
    std::vector<float> rows(size_t(w) * h), out(size_t(w) * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float s = 0;
            for (int i = -kReach; i <= kReach; i++)
                if (x + i >= 0 && x + i < w) s += k[i + kReach] * m[size_t(y) * w + size_t(x + i)];
            rows[size_t(y) * w + x] = s;
        }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float s = 0;
            for (int j = -kReach; j <= kReach; j++)
                if (y + j >= 0 && y + j < h) s += k[j + kReach] * rows[size_t(y + j) * w + x];
            out[size_t(y) * w + x] = s;
        }
    return out;
}

// a field `s` times larger (bilinear) cut at 0.5 a pixel wide: the edge's coverage
std::vector<uint8_t> Cut(const std::vector<float>& f, int w, int h, int s) {
    auto at = [&](int x, int y) { return f[size_t(std::clamp(y, 0, h - 1)) * w + size_t(std::clamp(x, 0, w - 1))]; };
    const int ow = w * s, oh = h * s;
    auto field = [&](int x, int y) {
        float u = (x + 0.5f) / s - 0.5f, v = (y + 0.5f) / s - 0.5f;
        int x0 = int(std::floor(u)), y0 = int(std::floor(v));
        float fx = u - x0, fy = v - y0;
        return (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) +
               (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
    };
    std::vector<uint8_t> out(size_t(ow) * oh);
    for (int y = 0; y < oh; y++)
        for (int x = 0; x < ow; x++) {
            float c = field(x, y), gx = (field(x + 1, y) - field(x - 1, y)) / 2;
            float gy = (field(x, y + 1) - field(x, y - 1)) / 2;
            float g = std::sqrt(gx * gx + gy * gy);
            float a = g > 1e-4f ? (c - 0.5f) / g + 0.5f : (c >= 0.5f ? 1.0f : 0.0f);
            out[size_t(y) * ow + x] = uint8_t(std::clamp(a, 0.0f, 1.0f) * 255 + 0.5f);
        }
    return out;
}

// the font's picture kScale times larger (B8G8R8A8 rows)
std::vector<uint32_t> Rebuild(const std::vector<uint32_t>& bgra, int w, int h) {
    std::vector<uint8_t> mask(size_t(w) * h);
    for (size_t i = 0; i < mask.size(); i++) mask[i] = (bgra[i] >> 24) >= 128;
    std::vector<uint8_t> m4 = Scale2x(Scale2x(mask, w, h), w * 2, h * 2);
    std::vector<uint8_t> alpha = Cut(Blur(m4, w * 4, h * 4), w * 4, h * 4, kScale / 4);
    // each texel's colour: its own where it shows, else an opaque neighbour's (the glyph's), else white
    std::vector<uint32_t> colour(size_t(w) * h, 0xFFFFFF);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t best = 0, c = 0xFFFFFF;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    uint32_t p = bgra[size_t(ny) * w + nx], a = p >> 24;
                    if (a > best || (a == best && dx == 0 && dy == 0)) best = a, c = p & 0xFFFFFF;
                }
            colour[size_t(y) * w + x] = c;
        }
    const int ow = w * kScale, oh = h * kScale;
    std::vector<uint32_t> out(size_t(ow) * oh);
    for (int y = 0; y < oh; y++)
        for (int x = 0; x < ow; x++)
            out[size_t(y) * ow + x] =
                colour[size_t(y / kScale) * w + x / kScale] | uint32_t(alpha[size_t(y) * ow + x]) << 24;
    return out;
}

std::mutex g_lock;
std::unordered_map<uint32_t, uint64_t> g_made;  // texture data -> the guest bytes' FNV-1a it was made from

uint64_t Fnv(const uint8_t* p, size_t n) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

}  // namespace

// GameInfo::texture_bound (after the button pictures): a font bound without its picture gets one
void FontPicture(uint32_t header) {
    gpu::TextureDesc d;
    if (!gpu::Describe(header, d)) return;
    bool font = false;
    for (const Font& f : kFonts) font |= d.format == f.format && d.width == f.w && d.height == f.h;
    if (!font) return;
    if (auto r = gpu::FindReplacement(d.data); r && !r->sharpened) return;  // made already, still holds
    std::lock_guard<std::mutex> hold(g_lock);
    uint32_t bytes = gpu::LevelSize(d, 0);
    if (!mem::IsCommitted(d.data) || !mem::IsCommitted(d.data + bytes - 1)) return;
    const auto* guest = static_cast<const uint8_t*>(kernel::HostPtr(d.data));
    uint64_t hash = Fnv(guest, bytes);
    auto it = g_made.find(d.data);
    if (it != g_made.end() && it->second == hash) return;  // made from these bytes already
    g_made[d.data] = hash;
    std::vector<uint32_t> px;
    if (!gpu::DecodeBgra(d, 0, guest, px)) return;
    std::vector<uint32_t> big = Rebuild(px, int(d.width), int(d.height));
    // NF_FONT_DUMP=folder: each font as built (font_<texture>.tga), to look at
    if (const char* dir = std::getenv("NF_FONT_DUMP"))
        if (FILE* f = std::fopen((std::string(dir) + "/font_" + std::to_string(d.data) + ".tga").c_str(), "wb")) {
            uint32_t w = d.width * kScale, h = d.height * kScale;
            uint8_t head[18] = {0, 0, 2};
            head[12] = uint8_t(w), head[13] = uint8_t(w >> 8), head[14] = uint8_t(h), head[15] = uint8_t(h >> 8);
            head[16] = 32, head[17] = 0x28;
            std::fwrite(head, 1, sizeof head, f);
            std::fwrite(big.data(), 4, big.size(), f);
            std::fclose(f);
        }
    gpu::SetReplacement(d.data, bytes, d.width * kScale, d.height * kScale, std::move(big));
    LOGR("nightfire: the font %08X (%ux%u) drawn again %u times larger with smooth edges", d.data, d.width, d.height,
         kScale);
}
}  // namespace nf
