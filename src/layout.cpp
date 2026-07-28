#include "layout.hpp"

#include <algorithm>
#include <cmath>

namespace alttab {

bool Box::contains(double px, double py) const {
    return px >= x && py >= y && px < x + w && py < y + h;
}

std::vector<Box> layoutStrip(const std::vector<Size>& naturalSizes, const LayoutConfig& cfg) {
    std::vector<Box> out;
    if (naturalSizes.empty() || cfg.monitorW <= 0 || cfg.monitorH <= 0)
        return out;

    const double innerW = std::max(1.0, cfg.monitorW - 2.0 * cfg.padding);
    const double innerH = std::max(1.0, cfg.monitorH - 2.0 * cfg.padding);
    const auto   n      = naturalSizes.size();

    // Ideal cell from caps, then uniform scale so row+gaps fit inner area.
    double tileH = std::min(cfg.maxTileH, innerH);
    double tileW = cfg.maxTileW;
    double gap   = cfg.gap;

    if (n > 0) {
        const double maxWFromRow = (innerW - gap * static_cast<double>(n - 1)) / static_cast<double>(n);
        tileW                    = std::min(tileW, std::max(1.0, maxWFromRow));
    }

    // Prefer mins when they still fit; else scale whole row uniformly (tiles+gaps).
    if (n > 0) {
        double wantW = std::max(cfg.minTileW, tileW);
        double wantH = std::max(cfg.minTileH, tileH);
        double wantG = gap;

        double rowW = wantW * static_cast<double>(n) + wantG * static_cast<double>(n - 1);
        double scale = 1.0;
        if (rowW > innerW)
            scale = std::min(scale, innerW / rowW);
        if (wantH > innerH)
            scale = std::min(scale, innerH / wantH);

        tileW = wantW * scale;
        tileH = wantH * scale;
        gap   = wantG * scale;
    }

    out.reserve(n);

    // Per-window aspect inside the uniform cell (letterbox content conceptually;
    // box is the full cell — render path scales window into it).
    const double rowW   = tileW * static_cast<double>(n) + gap * static_cast<double>(n > 0 ? n - 1 : 0);
    const double startX = (cfg.monitorW - rowW) * 0.5;
    const double startY = (cfg.monitorH - tileH) * 0.5;

    for (std::size_t i = 0; i < n; ++i) {
        const auto& nat = naturalSizes[i];
        double      w   = tileW;
        double      h   = tileH;

        if (nat.w > 1.0 && nat.h > 1.0) {
            const double aspect = nat.w / nat.h;
            const double cellA  = tileW / tileH;
            if (aspect > cellA) {
                w = tileW;
                h = tileW / aspect;
            } else {
                h = tileH;
                w = tileH * aspect;
            }
        }

        const double cellX = startX + static_cast<double>(i) * (tileW + gap);
        const double cellY = startY;
        out.push_back(Box{
            .x = cellX + (tileW - w) * 0.5,
            .y = cellY + (tileH - h) * 0.5,
            .w = std::max(1.0, w),
            .h = std::max(1.0, h),
        });
    }

    return out;
}

std::optional<std::size_t> hitTest(const std::vector<Box>& boxes, double px, double py) {
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        if (boxes[i].contains(px, py))
            return i;
    }
    return std::nullopt;
}

} // namespace alttab
