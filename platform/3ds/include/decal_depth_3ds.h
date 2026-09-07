#pragma once

#include <algorithm>
#include <cmath>

// Two D24 units for a flat polygon, plus twice its maximum depth slope:
// the reversed-Z equivalent of the desktop renderer's glPolygonOffset(-2,-2).
constexpr float kDecalDepthUnits3DS = 2.0f / 16777215.0f;

inline float DecalDepthBias3DS(const float* a, const float* b, const float* c,
                              int width, int height) {
    if (width <= 0 || height <= 0 || a[3] <= 0 || b[3] <= 0 || c[3] <= 0) {
        return kDecalDepthUnits3DS;
    }
    const float* vertices[] = {a, b, c};
    double x[3], y[3], z[3];
    for (int i = 0; i < 3; ++i) {
        const double w = vertices[i][3];
        x[i] = vertices[i][0] / w * width * 0.5;
        y[i] = vertices[i][1] / w * height * 0.5;
        z[i] = std::clamp(0.5 - vertices[i][2] / w * 0.4999, 0.0, 1.0);
    }
    const double dx1=x[1]-x[0], dx2=x[2]-x[0];
    const double dy1=y[1]-y[0], dy2=y[2]-y[0];
    const double dz1=z[1]-z[0], dz2=z[2]-z[0];
    const double area=dx1*dy2-dx2*dy1;
    if (area == 0 || !std::isfinite(area)) return kDecalDepthUnits3DS;
    const double slope=std::max(std::abs((dz1*dy2-dz2*dy1)/area),
                                std::abs((dx1*dz2-dx2*dz1)/area));
    return std::isfinite(slope) ? static_cast<float>(std::min(1.0, 2*slope+kDecalDepthUnits3DS))
                              : kDecalDepthUnits3DS;
}
