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
// Consumes the OCR worker's private snapshot. Samples line backgrounds and word
// colors before changing pixels, paints once, and publishes with the OCR job.
std::shared_ptr<Gdiplus::Bitmap> prepareLoremImage(std::unique_ptr<Gdiplus::Bitmap> snapshot,
                                                   const OcrReport& report, uint32_t seed,
                                                   const std::atomic<bool>* cancelled = nullptr);
// Reads the original screenshot or the published Lorem image. Only the returned
// opaque patch changes; no text generation occurs while editing rectangles.
RedactionResult redact(Gdiplus::Bitmap& source, RECT region, RedactionMode mode, float strength,
                       const OcrReport* ocr, Gdiplus::ARGB fill,
                       const std::function<void(Gdiplus::Graphics&)>& underlay = {},
                       unsigned previewEdge = 0);
} // namespace captura
