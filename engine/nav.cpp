#include "nav.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <queue>
#include <stdexcept>

namespace obf {

NavGrid nav_parse(const std::vector<uint8_t>& d, float world_size) {
    if (d.size() < 32) throw std::runtime_error("path map: truncated header");
    uint32_t h[4];
    std::memcpy(h, d.data(), 16);
    if (h[0] != h[1] || h[3] != 0 || h[0] > 8 || h[2] > 10 || h[0] + h[2] > 13) throw std::runtime_error("path map: unsupported header");
    const uint32_t tiles = 1u << h[0], cells = 1u << h[2], words = cells * cells / 32;
    NavGrid g;
    g.n = static_cast<int>(tiles * cells);
    g.cell = world_size / static_cast<float>(g.n);
    if (!(g.cell > 0) || !std::isfinite(g.cell)) throw std::runtime_error("path map: bad world size");
    g.blocked.assign(static_cast<size_t>(g.n) * g.n, 0);
    size_t o = 32, n = static_cast<size_t>(g.n);
    for (uint32_t t = 0; t < tiles * tiles; ++t) {
        if (o + 4 > d.size()) throw std::runtime_error("path map: truncated");
        uint32_t marker;
        std::memcpy(&marker, d.data() + o, 4);
        o += 4;
        size_t tx = t % tiles, tz = t / tiles;
        if (marker == 0xFFFFFFFFu) {  // cells x cells bits, rows of cells / 32 words, bit 0 = lowest x
            if (o + words * 4 > d.size()) throw std::runtime_error("path map: truncated tile");
            for (uint32_t r = 0; r < cells; ++r)
                for (uint32_t wi = 0; wi < cells / 32; ++wi) {
                    uint32_t w;
                    std::memcpy(&w, d.data() + o + (r * (cells / 32) + wi) * 4, 4);
                    for (uint32_t bit = 0; bit < 32 && w; ++bit)
                        if (w >> bit & 1u) g.blocked[(tz * cells + r) * n + tx * cells + wi * 32 + bit] = 1;
                }
            o += words * 4;
        } else if (marker == 1) {  // whole tile blocked
            for (uint32_t r = 0; r < cells; ++r) std::fill_n(&g.blocked[(tz * cells + r) * n + tx * cells], cells, 1);
        } else if (marker != 0) {
            throw std::runtime_error("path map: unknown tile marker");
        }
    }
    if (o != d.size()) throw std::runtime_error("path map: trailing data");
    return g;
}

NavGrid nav_coarse(const NavGrid& g, int f) {
    NavGrid c;
    if (f < 1 || g.n % f) throw std::runtime_error("nav_coarse: factor must divide the grid");
    c.n = g.n / f, c.cell = g.cell * static_cast<float>(f);
    c.blocked.assign(static_cast<size_t>(c.n) * c.n, 0);
    for (int z = 0; z < c.n; ++z)
        for (int x = 0; x < c.n; ++x) {
            int s = 0;
            for (int j = 0; j < f; ++j)
                for (int k = 0; k < f; ++k) s += g.blocked[static_cast<size_t>(z * f + j) * g.n + x * f + k];
            c.blocked[static_cast<size_t>(z) * c.n + x] = s * 2 > f * f;
        }
    return c;
}

namespace {

bool line_free(const NavGrid& g, int x0, int z0, int x1, int z1) {  // cells along the segment, both ends included
    int dx = std::abs(x1 - x0), dz = std::abs(z1 - z0), sx = x0 < x1 ? 1 : -1, sz = z0 < z1 ? 1 : -1, err = dx - dz;
    for (;;) {
        if (g.at(x0, z0)) return false;
        if (x0 == x1 && z0 == z1) return true;
        int e2 = 2 * err;
        if (e2 > -dz && e2 < dx) {  // diagonal step: both side cells must be free (no corner cutting)
            if (g.at(x0 + sx, z0) || g.at(x0, z0 + sz)) return false;
        }
        if (e2 > -dz) err -= dz, x0 += sx;
        if (e2 < dx) err += dx, z0 += sz;
    }
}

bool nearest_free(const NavGrid& g, int& x, int& z) {
    for (int r = 0; r <= 8; ++r)
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx)
                if ((std::abs(dx) == r || std::abs(dz) == r) && !g.at(x + dx, z + dz)) return x += dx, z += dz, true;
    return false;
}

}  // namespace

std::vector<float> nav_path(const NavGrid& g, float ax, float az, float bx, float bz, int max_expand) {
    if (g.n <= 0 || !std::isfinite(ax + az + bx + bz)) return {};
    auto cellof = [&](float v) { return std::clamp(static_cast<int>(std::floor(v / g.cell)), 0, g.n - 1); };
    int sx = cellof(ax), sz = cellof(az), gx = cellof(bx), gz = cellof(bz);
    if (!nearest_free(g, sx, sz) || !nearest_free(g, gx, gz)) return {};
    const size_t n = static_cast<size_t>(g.n), start = static_cast<size_t>(sz) * n + static_cast<size_t>(sx),
                 goal = static_cast<size_t>(gz) * n + static_cast<size_t>(gx);
    std::vector<float> cost(n * n, 1e30f);
    std::vector<int> from(n * n, -1);
    using Item = std::pair<float, size_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    auto h = [&](size_t i) {
        float dx = std::fabs(static_cast<float>(static_cast<int>(i % n) - gx)), dz = std::fabs(static_cast<float>(static_cast<int>(i / n) - gz));
        return std::max(dx, dz) + 0.41421356f * std::min(dx, dz);  // octile distance
    };
    cost[start] = 0, open.push({h(start), start});
    int expanded = 0;
    while (!open.empty() && expanded < max_expand) {
        auto [f, i] = open.top();
        open.pop();
        if (i == goal) break;
        if (f > cost[i] + h(i) + 1e-3f) continue;  // stale entry
        ++expanded;
        int x = static_cast<int>(i % n), z = static_cast<int>(i / n);
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                if ((!dx && !dz) || g.at(x + dx, z + dz) || (dx && dz && (g.at(x + dx, z) || g.at(x, z + dz)))) continue;
                size_t j = static_cast<size_t>(z + dz) * n + static_cast<size_t>(x + dx);
                float c = cost[i] + (dx && dz ? 1.41421356f : 1.0f);
                if (c < cost[j]) cost[j] = c, from[j] = static_cast<int>(i), open.push({c + h(j), j});
            }
    }
    if (from[goal] < 0 && goal != start) return {};
    std::vector<size_t> cells;
    for (size_t i = goal; ; i = static_cast<size_t>(from[i])) {
        cells.push_back(i);
        if (i == start) break;
    }
    std::reverse(cells.begin(), cells.end());
    std::vector<float> out;  // string pulling: keep a waypoint only where the straight line from the last one breaks
    size_t anchor = 0;
    auto push = [&](size_t i) { out.insert(out.end(), {(static_cast<float>(i % n) + 0.5f) * g.cell, (static_cast<float>(i / n) + 0.5f) * g.cell}); };
    for (size_t k = 2; k < cells.size(); ++k) {
        size_t a = cells[anchor], c = cells[k];
        if (!line_free(g, static_cast<int>(a % n), static_cast<int>(a / n), static_cast<int>(c % n), static_cast<int>(c / n)))
            push(cells[k - 1]), anchor = k - 1;
    }
    push(cells.back());
    return out;
}

}  // namespace obf
