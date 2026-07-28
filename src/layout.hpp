#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace alttab {

struct Size {
    double w = 0;
    double h = 0;
};

struct Box {
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;

    [[nodiscard]] bool contains(double px, double py) const;
};

struct LayoutConfig {
    double monitorW   = 1920;
    double monitorH   = 1080;
    double padding    = 48;
    double gap        = 24;
    double maxTileW   = 420;
    double maxTileH   = 280;
    double minTileW   = 120;
    double minTileH   = 80;
};

// Centered horizontal strip. Aspect from natural sizes. Scales down to fit.
std::vector<Box> layoutStrip(const std::vector<Size>& naturalSizes, const LayoutConfig& cfg);

// Monitor-local hit test against last layout boxes.
std::optional<std::size_t> hitTest(const std::vector<Box>& boxes, double px, double py);

} // namespace alttab
