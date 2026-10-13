#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <emmintrin.h>
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
    explicit Raster(Bitmap& image, unsigned mode, INT format = PixelFormat32bppARGB)
        : bitmap(image) {
        Rect area(0, 0, image.GetWidth(), image.GetHeight());
        locked = image.LockBits(&area, mode, format, &data) == Ok;
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
std::wstring loremText(const std::wstring& original, uint32_t& seed) {
    static constexpr const wchar_t* words[] = {L"a",
                                               L"et",
                                               L"ex",
                                               L"eu",
                                               L"id",
                                               L"in",
                                               L"ut",
                                               L"sit",
                                               L"sed",
                                               L"non",
                                               L"est",
                                               L"amet",
                                               L"elit",
                                               L"enim",
                                               L"Lorem",
                                               L"ipsum",
                                               L"dolor",
                                               L"magna",
                                               L"aliqua",
                                               L"tempor",
                                               L"labore",
                                               L"dolore",
                                               L"eiusmod",
                                               L"aliquam",
                                               L"voluptas",
                                               L"officiis",
                                               L"voluptate",
                                               L"consequat",
                                               L"adipiscing",
                                               L"incididunt",
                                               L"consectetur",
                                               L"exercitation",
                                               L"reprehenderit",
                                               L"necessitatibus"};
    bool upper = false, lower = false;
    for (wchar_t c : original) {
        upper |= std::iswupper(c) != 0;
        lower |= std::iswlower(c) != 0;
    }
    const size_t length = std::clamp(original.size(), size_t(1), size_t(512));
    std::wstring text;
    for (int attempt = 0; attempt < 8; ++attempt) {
        text.clear();
        seed = seed * 1664525u + 1013904223u;
        unsigned matches = 0;
        if (length > 1)
            for (const auto* word : words)
                if (wcslen(word) == length && _wcsicmp(word, original.c_str()) != 0)
                    ++matches;
        if (matches) {
            unsigned choice = (seed >> 16) % matches;
            for (const auto* word : words)
                if (wcslen(word) == length && _wcsicmp(word, original.c_str()) != 0) {
                    if (choice-- == 0) {
                        text = word;
                        break;
                    }
                }
        }
        while (text.size() < length) {
            if (!text.empty())
                text += L' ';
            size_t remaining = length - text.size();
            seed = seed * 1664525u + 1013904223u;
            if (remaining == 1) {
                // Tiny OCR words still need a different, equally short glyph.
                constexpr wchar_t initials[] = L"aeilmnstu";
                text += initials[seed % (std::size(initials) - 1)];
                break;
            }
            unsigned start = seed % std::size(words);
            for (unsigned offset = 0; offset < std::size(words); ++offset) {
                const wchar_t* word = words[(start + offset) % std::size(words)];
                size_t size = wcslen(word);
                // Avoid a trailing blank: any remainder must fit a space and
                // another word. Long originals use complete Lorem phrases.
                if (size <= remaining && remaining - size != 1) {
                    text += word;
                    break;
                }
            }
        }
        for (wchar_t& c : text)
            c = wchar_t(upper && !lower ? std::towupper(c) : std::towlower(c));
        if (upper && lower && !original.empty() && std::iswupper(original.front()))
            text.front() = wchar_t(std::towupper(text.front()));
        if (_wcsicmp(text.c_str(), original.c_str()) != 0)
            return text;
    }
    text.front() = std::towlower(text.front()) == L'l' ? L'i' : L'l';
    if (upper && !lower)
        text.front() = wchar_t(std::towupper(text.front()));
    return text;
}
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
    return {word.bounds, bg, fg, loremText(word.text, seed)};
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
bool gaussianBlur(Bitmap& patch, float strength) {
    Raster pixels(patch, ImageLockModeRead | ImageLockModeWrite);
    if (!pixels.locked)
        return false;
    const int width = int(patch.GetWidth()), height = int(patch.GetHeight());
    // One continuous sigma mapping for both axes and all preview resolutions.
    // GDI+ Blur is directional at small radii on some Windows builds.
    const float sigma = std::clamp(strength, 0.01f, 255.f) * 0.45f;
    const int radius = std::max(1, int(std::ceil(3.f * sigma)));
    std::vector<float> weights(size_t(radius) + 1);
    double total = 0;
    for (int k = 0; k <= radius; ++k) {
        weights[k] = float(std::exp(-double(k * k) / (2.0 * sigma * sigma)));
        total += weights[k] * (k ? 2 : 1);
    }
    for (float& weight : weights)
        weight = float(weight / total);
    // SSE2 is part of the x64 baseline. Keep fractional channel values between
    // passes and round only once, avoiding directional quantization artifacts.
    std::vector<__m128> horizontal(size_t(width) * height);
    std::vector<__m128> row(size_t(width) + 2 * radius), sum(width);
    const __m128i zero = _mm_setzero_si128();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            __m128i packed = _mm_cvtsi32_si128(int(pixels.at(x, y)));
            row[size_t(x) + radius] =
                _mm_cvtepi32_ps(_mm_unpacklo_epi16(_mm_unpacklo_epi8(packed, zero), zero));
        }
        for (int k = 0; k < radius; ++k) {
            row[k] = row[radius];
            row[size_t(width) + radius + k] = row[size_t(width) + radius - 1];
        }
        __m128* output = horizontal.data() + size_t(y) * width;
        const __m128 center = _mm_set1_ps(weights[0]);
        for (int x = 0; x < width; ++x)
            output[x] = _mm_mul_ps(row[size_t(x) + radius], center);
        for (int k = 1; k <= radius; ++k) {
            const __m128 weight = _mm_set1_ps(weights[k]);
            for (int x = 0; x < width; ++x)
                output[x] = _mm_add_ps(output[x],
                                       _mm_mul_ps(weight, _mm_add_ps(row[size_t(x) + radius - k],
                                                                     row[size_t(x) + radius + k])));
        }
    }
    for (int y = 0; y < height; ++y) {
        const __m128* center = horizontal.data() + size_t(y) * width;
        const __m128 weight0 = _mm_set1_ps(weights[0]);
        for (int x = 0; x < width; ++x)
            sum[x] = _mm_mul_ps(center[x], weight0);
        for (int k = 1; k <= radius; ++k) {
            const __m128* before = horizontal.data() + size_t(std::max(0, y - k)) * width;
            const __m128* after = horizontal.data() + size_t(std::min(height - 1, y + k)) * width;
            const __m128 weight = _mm_set1_ps(weights[k]);
            for (int x = 0; x < width; ++x)
                sum[x] = _mm_add_ps(sum[x], _mm_mul_ps(weight, _mm_add_ps(before[x], after[x])));
        }
        for (int x = 0; x < width; ++x) {
            __m128i channels = _mm_cvttps_epi32(_mm_add_ps(sum[x], _mm_set1_ps(0.5f)));
            __m128i packed = _mm_packus_epi16(_mm_packs_epi32(channels, zero), zero);
            // Opaque replacement prevents the original from showing through edges.
            pixels.at(x, y) = ARGB(_mm_cvtsi128_si32(packed)) | 0xff000000;
        }
    }
    return true;
}
} // namespace

std::shared_ptr<Gdiplus::Bitmap> prepareLoremImage(std::unique_ptr<Gdiplus::Bitmap> snapshot,
                                                   const OcrReport& report, uint32_t seed,
                                                   const std::atomic<bool>* cancelled) {
    using namespace Gdiplus;
    if (!snapshot || snapshot->GetLastStatus() != Ok ||
        (report.state != OcrState::Ready && report.state != OcrState::Empty))
        return {};
    auto stopped = [&] { return cancelled && cancelled->load(); };
    Rect context(0, 0, snapshot->GetWidth(), snapshot->GetHeight());
    std::vector<Replacement> substitutes;
    {
        // RGB snapshots avoid a second full-screen conversion buffer. Estimate
        // every word from original pixels before any neighboring text is erased.
        INT format = snapshot->GetPixelFormat() == PixelFormat32bppRGB ? PixelFormat32bppRGB
                                                                       : PixelFormat32bppARGB;
        Raster pixels(*snapshot, ImageLockModeRead, format);
        if (!pixels.locked)
            return {};
        for (const auto& line : report.lines)
            for (const auto& word : line.words) {
                if (stopped())
                    return {};
                substitutes.push_back(replacement(pixels, word, context, seed));
            }
    }
    if (!substitutes.empty()) {
        Graphics g(snapshot.get());
        if (g.GetLastStatus() != Ok)
            return {};
        // Erase every detected glyph before drawing any replacement glyphs.
        for (const auto& word : substitutes) {
            if (stopped())
                return {};
            SolidBrush bg(Color(word.background));
            Rect erase = wordRegion(word.bounds, 2);
            if (g.FillRectangle(&bg, erase) != Ok)
                return {};
        }
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        FontFamily family(L"Segoe UI");
        for (const auto& word : substitutes) {
            if (stopped())
                return {};
            GraphicsPath glyphs;
            if (glyphs.AddString(word.text.c_str(), int(word.text.size()), &family,
                                 FontStyleRegular, 32, PointF(0, 0),
                                 StringFormat::GenericTypographic()) != Ok)
                return {};
            RectF ink;
            glyphs.GetBounds(&ink);
            if (ink.Width <= 0 || ink.Height <= 0)
                continue;
            REAL sy = word.bounds.Height / ink.Height;
            // Match the original visual span as well as its character count.
            // Short substitutions must not leave most of a long word blank.
            REAL sx = word.bounds.Width / ink.Width;
            Matrix transform(sx, 0, 0, sy, word.bounds.X - ink.X * sx, word.bounds.Y - ink.Y * sy);
            glyphs.Transform(&transform);
            SolidBrush foreground(Color(word.color));
            if (g.FillPath(&foreground, &glyphs) != Ok)
                return {};
        }
    }
    return stopped() ? std::shared_ptr<Bitmap>{} : std::shared_ptr<Bitmap>(std::move(snapshot));
}

RedactionResult redact(Gdiplus::Bitmap& source, RECT area, RedactionMode mode, float strength,
                       const OcrReport* ocr, Gdiplus::ARGB fill,
                       const std::function<void(Gdiplus::Graphics&)>& underlay,
                       unsigned previewEdge) {
    using namespace Gdiplus;
    bool lorem = mode == RedactionMode::LoremGaussian || mode == RedactionMode::LoremPixelate;
    Bitmap* base = lorem ? (ocr ? ocr->loremImage.get() : nullptr) : &source;
    if (!base)
        return {}; // Never fall back to original text if preparation failed.
    if (lorem && ((ocr->state != OcrState::Ready && ocr->state != OcrState::Empty) ||
                  base->GetWidth() != source.GetWidth() || base->GetHeight() != source.GetHeight()))
        return {};
    int width = area.right - area.left, height = area.bottom - area.top;
    if (width <= 0 || height <= 0)
        return {};
    RectF selected(float(area.left), float(area.top), float(width), float(height));
    unsigned words = 0;
    if (lorem)
        for (const auto& line : ocr->lines)
            for (const auto& word : line.words)
                words += word.bounds.IntersectsWith(selected);
    float scale = previewEdge && std::max(width, height) > int(previewEdge)
                      ? previewEdge / float(std::max(width, height))
                      : 1.f;
    auto patch = std::make_shared<Bitmap>(std::max(1, int(width * scale)),
                                          std::max(1, int(height * scale)), PixelFormat32bppARGB);
    if (patch->GetLastStatus() != Ok)
        return {};
    {
        Graphics g(patch.get());
        g.Clear(Color(fill));
        g.TranslateTransform(-area.left * scale, -area.top * scale);
        g.ScaleTransform(scale, scale);
        g.SetInterpolationMode(InterpolationModeBilinear);
        g.SetCompositingMode(CompositingModeSourceCopy);
        g.DrawImage(base, 0, 0, base->GetWidth(), base->GetHeight());
        g.SetCompositingMode(CompositingModeSourceOver);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        if (underlay)
            underlay(g);
    }
    bool gaussian = mode == RedactionMode::Gaussian || mode == RedactionMode::LoremGaussian;
    if (!(gaussian ? gaussianBlur(*patch, strength * scale)
                   : pixelate(*patch, std::max(1, int(std::round(strength * scale))))))
        return {};
    return {std::move(patch), words};
}
} // namespace captura
