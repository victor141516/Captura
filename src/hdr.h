#pragma once

#include <cstdint>
#include <string>

namespace captura {
struct HdrReport {
    unsigned hdrOutputs = 0, convertedOutputs = 0, regions = 0, mappedRegions = 0;
    double milliseconds = 0;
    std::wstring error;
};

// GDI screen capture converts HDR content with scRGB 1.0 (80 nits) as white, so anything
// brighter clips per channel: highlights burn and hues shift, while SDR windows look right.
// This recaptures every HDR monitor of `desktop` (virtual-screen coordinates) as FP16 scRGB
// through DXGI desktop duplication and overwrites the matching area of `bgra` (top-down
// 32 bpp, row pitch `stride`), relative to the monitor's SDR white level. Each visible
// top-level window is converted separately: regions within SDR white are encoded linearly,
// reproducing SDR windows; brighter regions are tone-mapped (BT.2390 roll-off on max RGB,
// hue preserving) so their peak lands on SDR white. Failures leave the GDI pixels untouched.
HdrReport convertHdrDesktop(uint8_t* bgra, int stride, RECT desktop);

// Pure conversion steps, exposed for tests.
float halfToFloat(uint16_t value);
uint16_t floatToHalf(float value);
// BT.2390 EETF in PQ space, from [0, sourcePeak] to [0, targetPeak] nits.
float hdrToneMapNits(float nits, float sourcePeakNits, float targetPeakNits);
// Converts the pixels of `rects` (coordinates shared by both buffers) from scRGB FP16 RGBA
// (`pitch` in pixels) to sRGB BGRA. The region peak is capped at `displayPeakNits` when that
// exceeds SDR white. Returns whether the region exceeded SDR white and was tone-mapped.
bool toneMapRegion(const uint16_t* rgba, int pitch, const RECT* rects, size_t count,
                   float sdrWhiteNits, float displayPeakNits, uint8_t* bgra, int stride);
} // namespace captura
