// Clean text: the fonts drawn again eight times larger with smooth edges, for the renderers (gpu::SetReplacement).
//
// Both exes' fonts are pixel art with next to no soft edge (default.xbe's 512x64 A8R8G8B8, swizzled; Driving.xbe's
// 256x108 LIN_A8), so enlarged by interpolation (the runtime's sharp 2D) their curves and diagonals keep a stepped
// rim. Here a glyph's shape is rebuilt instead: the mask (alpha half or more) is enlarged twice by Scale2x, which keeps
// straight strokes straight and turns each stair into a diagonal, then twice again through a smooth field (Catmull-Rom
// over the mask) cut at its middle a pixel wide, which rounds what is left and gives the edge its antialiasing. The
// colour is the nearest opaque texel's. The title's picture wins over the runtime's sharpened one; the guest bytes stay
// as they are (a font changed in memory drops it, and it is made again at the next bind).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "common/nightfire.h"
#include "core/log.h"
#include "gpu/texture.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace nf {
namespace {

constexpr int kScale = 8;  // Scale2x twice, then the smooth field twice

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

// a 0/1 mask enlarged `s` times with antialiased edges: a Catmull-Rom field over it, cut at 0.5 a pixel wide
std::vector<uint8_t> Smooth(const std::vector<uint8_t>& m, int w, int h, int s) {
    auto at = [&](int x, int y) { return float(m[size_t(std::clamp(y, 0, h - 1)) * w + size_t(std::clamp(x, 0, w - 1))]); };
    auto cubic = [](float t, float* k) {
        float t2 = t * t, t3 = t2 * t;
        k[0] = -0.5f * t3 + t2 - 0.5f * t;
        k[1] = 1.5f * t3 - 2.5f * t2 + 1;
        k[2] = -1.5f * t3 + 2 * t2 + 0.5f * t;
        k[3] = 0.5f * t3 - 0.5f * t2;
    };
    const int ow = w * s, oh = h * s;
    std::vector<float> f(size_t(ow + 2) * size_t(oh + 2));
    for (int y = -1; y <= oh; y++) {
        float v = (y + 0.5f) / s - 0.5f;
        int y0 = int(std::floor(v));
        float ky[4];
        cubic(v - y0, ky);
        for (int x = -1; x <= ow; x++) {
            float u = (x + 0.5f) / s - 0.5f;
            int x0 = int(std::floor(u));
            float kx[4];
            cubic(u - x0, kx);
            float sum = 0;
            for (int j = 0; j < 4; j++) {
                float row = 0;
                for (int i = 0; i < 4; i++) row += kx[i] * at(x0 - 1 + i, y0 - 1 + j);
                sum += ky[j] * row;
            }
            f[size_t(y + 1) * size_t(ow + 2) + size_t(x + 1)] = sum;
        }
    }
    std::vector<uint8_t> out(size_t(ow) * oh);
    for (int y = 0; y < oh; y++)
        for (int x = 0; x < ow; x++) {
            auto F = [&](int dx, int dy) { return f[size_t(y + 1 + dy) * size_t(ow + 2) + size_t(x + 1 + dx)]; };
            float c = F(0, 0), gx = (F(1, 0) - F(-1, 0)) / 2, gy = (F(0, 1) - F(0, -1)) / 2;
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
    std::vector<uint8_t> m2 = Scale2x(Scale2x(mask, w, h), w * 2, h * 2);
    std::vector<uint8_t> alpha = Smooth(m2, w * 4, h * 4, kScale / 4);
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
            out[size_t(y) * ow + x] = colour[size_t(y / kScale) * w + x / kScale] | uint32_t(alpha[size_t(y) * ow + x]) << 24;
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
    if (it != g_made.end() && it->second == hash) return;  // dropped by the renderer since: not again for the same bytes
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
