#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

// CPU-owned copy of a completed PICA D24S8 buffer. Retaining the packed
// layout makes capture a memcpy, with conversion only at queried pixels.
class DepthSnapshot3DS {
  public:
    bool requested = false;
    static constexpr uint16_t Far = 0xFFFC;

    void Reset() {
        requested = false;
        valid = false;
        std::vector<uint32_t>().swap(pixels);
    }

    bool Capture(const void* source, uint32_t width, uint32_t height,
                 uint32_t contentWidth, uint32_t contentHeight, bool rotate) {
        valid = false;
        if (!source || width == 0 || height == 0 || width > 1024 || height > 1024 ||
            width % 8 || height % 8 || contentWidth == 0 || contentHeight == 0 ||
            contentWidth > (rotate ? height : width) || contentHeight > (rotate ? width : height)) {
            return false;
        }
        try {
            pixels.resize(static_cast<size_t>(width) * height);
        } catch (const std::bad_alloc&) {
            return false;
        }
        std::memcpy(pixels.data(), source, pixels.size() * sizeof(uint32_t));
        backingWidth = width;
        backingHeight = height;
        drawWidth = contentWidth;
        drawHeight = contentHeight;
        rotated = rotate;
        valid = true;
        return true;
    }

    uint16_t Sample(float x, float y) const {
        if (!valid || !std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= 400 || y >= 240) {
            return Far;
        }
        // Same containing-pixel convention as GL's integer glReadPixels args.
        const uint32_t px = static_cast<uint32_t>(x * drawWidth / 400.f);
        const uint32_t py = static_cast<uint32_t>(y * drawHeight / 240.f);
        if (px >= drawWidth || py >= drawHeight) return Far;
        // Queries use GL window coordinates (Y up). The tilted projection
        // maps (x,y) to (y,-x); PICA memory then reverses rasterizer Y about
        // the full backing height, including any power-of-two padding.
        const uint32_t tx = rotated ? py : px;
        const uint32_t ty = rotated ? backingHeight - drawWidth + px : backingHeight - 1 - py;
        size_t morton = 0;
        for (unsigned bit = 0; bit < 3; ++bit) {
            morton |= ((tx >> bit) & 1U) << (2 * bit);
            morton |= ((ty >> bit) & 1U) << (2 * bit + 1);
        }
        const size_t offset = ((ty / 8U) * (backingWidth / 8U) + tx / 8U) * 64U + morton;
        // PICA uses reversed depth: zero is far; the upper byte is stencil.
        return static_cast<uint16_t>(((0xFFFFFFU - (pixels[offset] & 0xFFFFFFU)) >> 10U) << 2U);
    }

  private:
    std::vector<uint32_t> pixels;
    uint32_t backingWidth = 0, backingHeight = 0, drawWidth = 0, drawHeight = 0;
    bool valid = false, rotated = false;
};
