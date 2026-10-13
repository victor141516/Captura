#pragma once
#include <functional>
#include <memory>
#include "ocr.h"

namespace captura {
enum class RedactionMode { Gaussian, Pixelate, LoremGaussian, LoremPixelate };
struct RedactionResult {
    std::shared_ptr<Gdiplus::Bitmap> bitmap;
    unsigned replacedWords = 0;
};
// Only the returned opaque patch changes. Text/color sampling may read context
// outside the patch, but never writes to the immutable screenshot.
RedactionResult redact(Gdiplus::Bitmap& source, RECT region, RedactionMode mode, float strength,
                       const OcrReport* ocr, uint32_t seed, Gdiplus::ARGB fill,
                       const std::function<void(Gdiplus::Graphics&)>& underlay = {},
                       unsigned previewEdge = 0);
} // namespace captura
