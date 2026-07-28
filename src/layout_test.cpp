#include "layout.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

int g_fails = 0;

void expect(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << "\n";
        ++g_fails;
    }
}

bool near(double a, double b, double eps = 0.5) {
    return std::abs(a - b) <= eps;
}

} // namespace

int main() {
    using namespace alttab;

    // Empty
    expect(layoutStrip({}, {}).empty(), "empty in → empty out");

    // Single tile centered
    {
        LayoutConfig cfg{.monitorW = 1000, .monitorH = 800, .padding = 50, .gap = 20, .maxTileW = 400, .maxTileH = 300};
        const auto   boxes = layoutStrip({{800, 600}}, cfg);
        expect(boxes.size() == 1, "single size");
        if (!boxes.empty()) {
            expect(near(boxes[0].w, 400), "single w capped");
            expect(near(boxes[0].x + boxes[0].w * 0.5, 500), "single cx");
            expect(near(boxes[0].y + boxes[0].h * 0.5, 400), "single cy");
        }
    }

    // Three tiles fit in a row with gaps
    {
        LayoutConfig cfg{.monitorW = 1200, .monitorH = 800, .padding = 40, .gap = 20, .maxTileW = 300, .maxTileH = 200};
        const auto   boxes = layoutStrip({{16, 9}, {16, 9}, {16, 9}}, cfg);
        expect(boxes.size() == 3, "three tiles");
        if (boxes.size() == 3) {
            expect(boxes[0].x < boxes[1].x, "order L→R");
            expect(boxes[1].x < boxes[2].x, "order L→R 2");
            // No overlap
            expect(boxes[0].x + boxes[0].w + cfg.gap <= boxes[1].x + 0.5, "gap 0-1");
            // Inside monitor
            for (const auto& b : boxes) {
                expect(b.x >= 0 && b.y >= 0, "non-neg");
                expect(b.x + b.w <= cfg.monitorW + 0.5, "in width");
                expect(b.y + b.h <= cfg.monitorH + 0.5, "in height");
            }
        }
    }

    // Many tiles scale down
    {
        LayoutConfig cfg{.monitorW = 800, .monitorH = 600, .padding = 20, .gap = 8, .maxTileW = 400, .maxTileH = 300};
        std::vector<Size> sizes(12, Size{1920, 1080});
        const auto        boxes = layoutStrip(sizes, cfg);
        expect(boxes.size() == 12, "twelve");
        if (!boxes.empty()) {
            expect(boxes.front().x >= cfg.padding - 1.0, "left pad-ish");
            expect(boxes.back().x + boxes.back().w <= cfg.monitorW - cfg.padding + 1.0, "right pad-ish");
        }
    }

    // Hit test
    {
        std::vector<Box> boxes{{10, 10, 50, 50}, {80, 10, 50, 50}};
        expect(hitTest(boxes, 20, 20) == 0, "hit 0");
        expect(hitTest(boxes, 90, 30) == 1, "hit 1");
        expect(!hitTest(boxes, 0, 0).has_value(), "miss");
    }

    if (g_fails) {
        std::cerr << g_fails << " failure(s)\n";
        return 1;
    }

    std::cout << "layout_test ok\n";
    return 0;
}
