#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include "redaction.h"

namespace captura {
namespace {
using namespace Gdiplus;
Rect wordRegion(const RectF& r, int padding) {
    int left = int(std::floor(r.X)) - padding, top = int(std::floor(r.Y)) - padding;
    return {left, top, int(std::ceil(r.GetRight())) + padding - left,
            int(std::ceil(r.GetBottom())) + padding - top};
}
struct Raster {
    Bitmap& bitmap;
    BitmapData data{};
    bool locked;
    explicit Raster(Bitmap& image, unsigned mode) : bitmap(image) {
        Rect area(0, 0, image.GetWidth(), image.GetHeight());
        locked = image.LockBits(&area, mode, PixelFormat32bppARGB, &data) == Ok;
    }
    ~Raster() {
        if (locked)
            bitmap.UnlockBits(&data);
    }
    ARGB& at(int x, int y) {
        return reinterpret_cast<ARGB*>(static_cast<BYTE*>(data.Scan0) +
                                       ptrdiff_t(y) * data.Stride)[x];
    }
};
struct Bucket {
    uint64_t red = 0, green = 0, blue = 0;
    unsigned count = 0;
};
using Histogram = std::unordered_map<unsigned, Bucket>;
void countColor(Histogram& counts, ARGB color) {
    unsigned r = (color >> 16) & 255, g = (color >> 8) & 255, b = color & 255;
    auto& bucket = counts[((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)];
    bucket.red += r;
    bucket.green += g;
    bucket.blue += b;
    ++bucket.count;
}
ARGB dominant(const Histogram& counts, ARGB fallback) {
    const Bucket* best = nullptr;
    unsigned bestKey = 0;
    for (const auto& entry : counts)
        if (!best || entry.second.count > best->count ||
            (entry.second.count == best->count && entry.first < bestKey)) {
            best = &entry.second;
            bestKey = entry.first;
        }
    return best ? 0xff000000 | (ARGB(best->red / best->count) << 16) |
                      (ARGB(best->green / best->count) << 8) | ARGB(best->blue / best->count)
                : fallback;
}
int colorDistance(ARGB a, ARGB b) {
    int r = int((a >> 16) & 255) - int((b >> 16) & 255),
        g = int((a >> 8) & 255) - int((b >> 8) & 255), blue = int(a & 255) - int(b & 255);
    return r * r + g * g + blue * blue;
}
struct Replacement {
    RectF bounds;
    ARGB background, color;
    std::wstring text;
};
Replacement replacement(Raster& pixels, const OcrWord& word, const Rect& context, uint32_t& seed) {
    Rect ink = wordRegion(word.bounds, 0), ring = wordRegion(word.bounds, 4);
    Histogram background, foreground;
    auto visit = [&](Rect area, auto sample) {
        int left = std::max(context.X, area.X), top = std::max(context.Y, area.Y),
            right = std::min(context.GetRight(), area.GetRight()),
            bottom = std::min(context.GetBottom(), area.GetBottom());
        for (int y = top; y < bottom; ++y)
            for (int x = left; x < right; ++x)
                sample(x, y, pixels.at(x - context.X, y - context.Y));
    };
    visit(ring, [&](int x, int y, ARGB color) {
        if (!ink.Contains(x, y))
            countColor(background, color);
    });
    ARGB bg = dominant(background, 0xffffffff);
    int strongest = 0;
    visit(ink,
          [&](int, int, ARGB color) { strongest = std::max(strongest, colorDistance(color, bg)); });
    int threshold = std::max(8 * 8, strongest / 3);
    visit(ink, [&](int, int, ARGB color) {
        if (colorDistance(color, bg) > threshold)
            countColor(foreground, color);
    });
    ARGB fg = dominant(foreground, colorDistance(bg, 0xff000000) > colorDistance(bg, 0xffffffff)
                                       ? 0xff000000
                                       : 0xffffffff);
    static constexpr const wchar_t* lorem[] = {
        L"Lorem",  L"ipsum", L"dolor",  L"sit",     L"amet",   L"consectetur", L"adipiscing",
        L"elit",   L"sed",   L"do",     L"eiusmod", L"tempor", L"incididunt",  L"ut",
        L"labore", L"et",    L"dolore", L"magna",   L"aliqua"};
    seed = seed * 1664525u + 1013904223u;
    unsigned index = seed % std::size(lorem);
    if (_wcsicmp(lorem[index], word.text.c_str()) == 0)
        index = (index + 1) % std::size(lorem);
    return {word.bounds, bg, fg, lorem[index]};
}
bool pixelate(Bitmap& image, int block) {
    Raster pixels(image, ImageLockModeRead | ImageLockModeWrite);
    if (!pixels.locked)
        return false;
    int width = int(image.GetWidth()), height = int(image.GetHeight());
    for (int top = 0; top < height; top += block)
        for (int left = 0; left < width; left += block) {
            int right = std::min(width, left + block), bottom = std::min(height, top + block);
            uint64_t r = 0, g = 0, b = 0, count = uint64_t(right - left) * (bottom - top);
            for (int y = top; y < bottom; ++y)
                for (int x = left; x < right; ++x) {
                    ARGB c = pixels.at(x, y);
                    r += (c >> 16) & 255;
                    g += (c >> 8) & 255;
                    b += c & 255;
                }
            ARGB average =
                0xff000000 | (ARGB(r / count) << 16) | (ARGB(g / count) << 8) | ARGB(b / count);
            for (int y = top; y < bottom; ++y)
                for (int x = left; x < right; ++x)
                    pixels.at(x, y) = average;
        }
    return true;
}
bool applyEffect(Bitmap& patch, bool gaussian, float strength) {
    if (!gaussian)
        return pixelate(patch, std::max(1, int(std::round(strength))));
    Blur blur;
    BlurParams parameters{std::clamp(strength, 0.5f, 255.f), FALSE};
    RECT roi{0, 0, LONG(patch.GetWidth()), LONG(patch.GetHeight())};
    if (blur.SetParameters(&parameters) != Ok || patch.ApplyEffect(&blur, &roi) != Ok)
        return false;
    Raster pixels(patch, ImageLockModeRead | ImageLockModeWrite);
    if (!pixels.locked)
        return false;
    // Opaque replacement prevents the original from showing through blur edges.
    for (unsigned y = 0; y < patch.GetHeight(); ++y)
        for (unsigned x = 0; x < patch.GetWidth(); ++x)
            pixels.at(x, y) |= 0xff000000;
    return true;
}
} // namespace

RedactionResult redact(Gdiplus::Bitmap& source, RECT area, RedactionMode mode, float strength,
                       const OcrReport* ocr, uint32_t seed, Gdiplus::ARGB fill,
                       const std::function<void(Gdiplus::Graphics&)>& underlay,
                       unsigned previewEdge) {
    using namespace Gdiplus;
    bool lorem = mode == RedactionMode::LoremGaussian || mode == RedactionMode::LoremPixelate;
    if (lorem && (!ocr || (ocr->state != OcrState::Ready && ocr->state != OcrState::Empty)))
        return {}; // Never silently fall back to blurring original text.
    int width = area.right - area.left, height = area.bottom - area.top;
    if (width <= 0 || height <= 0)
        return {};
    bool gaussian = mode == RedactionMode::Gaussian || mode == RedactionMode::LoremGaussian;
    float scale = previewEdge && std::max(width, height) > int(previewEdge)
                      ? previewEdge / float(std::max(width, height))
                      : 1.f;
    if (!lorem && scale < 1) {
        // Rasterize ordinary interactive effects directly at preview resolution.
        // No full-size temporary patch or full-size convolution per cursor event.
        auto preview =
            std::make_shared<Bitmap>(std::max(1, int(width * scale)),
                                     std::max(1, int(height * scale)), PixelFormat32bppARGB);
        if (preview->GetLastStatus() != Ok)
            return {};
        {
            Graphics g(preview.get());
            g.Clear(Color(fill));
            g.TranslateTransform(-area.left * scale, -area.top * scale);
            g.ScaleTransform(scale, scale);
            g.SetInterpolationMode(InterpolationModeBilinear);
            g.SetCompositingMode(CompositingModeSourceCopy);
            g.DrawImage(&source, 0, 0, source.GetWidth(), source.GetHeight());
            g.SetCompositingMode(CompositingModeSourceOver);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            if (underlay)
                underlay(g);
        }
        if (!applyEffect(*preview, gaussian, strength * scale))
            return {};
        return {std::move(preview), 0};
    }
    Rect context(area.left, area.top, width, height);
    RectF selected(float(area.left), float(area.top), float(width), float(height));
    std::vector<const OcrWord*> words;
    if (lorem)
        for (const auto& line : ocr->lines)
            for (const auto& word : line.words)
                if (word.bounds.IntersectsWith(selected)) {
                    words.push_back(&word);
                    Rect extra = wordRegion(word.bounds, 6), united;
                    Rect::Union(united, context, extra);
                    context = united;
                }
    Bitmap backdrop(context.Width, context.Height, PixelFormat32bppARGB);
    if (backdrop.GetLastStatus() != Ok)
        return {};
    {
        Graphics g(&backdrop);
        g.Clear(Color(fill));
        g.TranslateTransform(float(-context.X), float(-context.Y));
        g.SetCompositingMode(CompositingModeSourceCopy);
        g.DrawImage(&source, 0, 0, source.GetWidth(), source.GetHeight());
        g.SetCompositingMode(CompositingModeSourceOver);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        if (underlay)
            underlay(g);
    }
    std::vector<Replacement> substitutes;
    if (!words.empty()) {
        Raster pixels(backdrop, ImageLockModeRead);
        if (!pixels.locked)
            return {};
        for (const auto* word : words)
            substitutes.push_back(replacement(pixels, *word, context, seed));
    }
    std::shared_ptr<Bitmap> patch(backdrop.Clone(area.left - context.X, area.top - context.Y, width,
                                                 height, PixelFormat32bppARGB));
    if (!patch || patch->GetLastStatus() != Ok)
        return {};
    if (!substitutes.empty()) {
        Graphics g(patch.get());
        g.TranslateTransform(float(-area.left), float(-area.top));
        // Erase every detected glyph before drawing any replacement glyphs.
        for (const auto& word : substitutes) {
            SolidBrush bg(Color(word.background));
            Rect erase = wordRegion(word.bounds, 2);
            g.FillRectangle(&bg, erase);
        }
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        FontFamily family(L"Segoe UI");
        for (const auto& word : substitutes) {
            GraphicsPath glyphs;
            glyphs.AddString(word.text.c_str(), int(word.text.size()), &family, FontStyleRegular,
                             32, PointF(0, 0), StringFormat::GenericTypographic());
            RectF ink;
            glyphs.GetBounds(&ink);
            if (ink.Width <= 0 || ink.Height <= 0)
                continue;
            REAL sy = word.bounds.Height / ink.Height;
            REAL sx = std::min(sy, word.bounds.Width / ink.Width);
            Matrix transform(sx, 0, 0, sy, word.bounds.X - ink.X * sx, word.bounds.Y - ink.Y * sy);
            glyphs.Transform(&transform);
            SolidBrush foreground(Color(word.color));
            g.FillPath(&foreground, &glyphs);
        }
    }
    if (scale < 1) {
        auto smaller =
            std::make_shared<Bitmap>(std::max(1, int(width * scale)),
                                     std::max(1, int(height * scale)), PixelFormat32bppARGB);
        if (smaller->GetLastStatus() != Ok)
            return {};
        {
            Graphics g(smaller.get());
            g.SetCompositingMode(CompositingModeSourceCopy);
            g.SetInterpolationMode(InterpolationModeBilinear);
            g.DrawImage(patch.get(), 0, 0, smaller->GetWidth(), smaller->GetHeight());
        }
        patch = std::move(smaller);
    }
    if (!applyEffect(*patch, gaussian, strength * scale))
        return {};
    return {std::move(patch), unsigned(substitutes.size())};
}
} // namespace captura
