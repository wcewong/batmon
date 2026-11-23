#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
namespace myfinder {
// Search the right half for the rightmost cluster of high-contrast glyphs, ignoring smooth backgrounds.
inline int GroupLeft(const std::uint32_t* pixels, int width, int height, int maxGap) {
    if (!pixels || width < 8 || height < 4 || maxGap < 1) return -1;
    int left = -1, blank = 0;
    for (int x = width - 3; x >= std::max(2, width / 2); --x) {
        int hits = 0, run = 0, longestRun = 0;
        for (int y = 1; y < height - 1; ++y) {
            auto a = pixels[y * width + x - 2], b = pixels[y * width + x + 2];
            int contrast = 0;
            for (int shift : {0, 8, 16})
                contrast = std::max(contrast, std::abs(int((a >> shift) & 255) - int((b >> shift) & 255)));
            if (contrast >= 40) {
                ++hits;
                longestRun = std::max(longestRun, ++run);
            } else {
                run = 0;
            }
        }
        // A menu button's highlight can extend farther left than its glyph.
        // Its nearly full-height edge should not move the whole strip.
        const bool highlightEdge = height >= 16 && longestRun * 4 >= (height - 2) * 3;
        if (hits >= 2 && !highlightEdge) { left = x - 2; blank = 0; }
        else if (++blank > (left < 0 ? maxGap * 3 : maxGap)) break;
    }
    // Reaching the search boundary is ambiguous, e.g. textured wallpaper.
    return left > width / 2 ? left : -1;
}

enum class Foreground { Unknown, Dark, Light };

// Follow MyFinder's rendered neutral glyphs, not the Windows taskbar theme. The
// dominant luminance is the bar background; only contrasting, nearly neutral
// pixels vote for text polarity, so colored status icons cannot flip the theme.
inline Foreground GlyphForeground(const std::uint32_t* pixels, int width, int height, int edge) {
    if (!pixels || width < 8 || height < 4 || edge < 0 || edge >= width - 2) return Foreground::Unknown;
    int histogram[16]{};
    int total = 0;
    auto luminance = [](std::uint32_t p) { return (77 * int((p >> 16) & 255) +
        150 * int((p >> 8) & 255) + 29 * int(p & 255)) >> 8; };
    int begin = std::max(edge, width / 2);
    for (int y = 1; y < height - 1; ++y) for (int x = begin; x < width - 2; ++x) {
        ++histogram[luminance(pixels[y * width + x]) / 16]; ++total;
    }
    if (!total) return Foreground::Unknown;
    int mode = int(std::max_element(histogram, histogram + 16) - histogram);
    if (histogram[mode] * 4 < total) return Foreground::Unknown; // textured/ambiguous background
    int background = mode * 16 + 8;
    std::int64_t dark = 0, light = 0;
    int candidates = 0;
    for (int y = 1; y < height - 1; ++y) for (int x = begin; x < width - 2; ++x) {
        auto p = pixels[y * width + x];
        int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
        if (std::max({r,g,b}) - std::min({r,g,b}) > 48) continue;
        int difference = luminance(p) - background;
        if (difference >= 40) { light += difference; ++candidates; }
        else if (difference <= -40) { dark -= difference; ++candidates; }
    }
    if (candidates < 4) return Foreground::Unknown;
    if (light > dark * 2) return Foreground::Light;
    if (dark > light * 2) return Foreground::Dark;
    return Foreground::Unknown;
}
}
