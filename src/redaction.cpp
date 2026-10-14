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
    struct InkSpan {
        float left, right;
        ARGB color;
    };
    RectF bounds;
    ARGB background;
    std::wstring text;
    std::vector<InkSpan> spans;
};
struct TextRun {
    OcrWord line;
    std::vector<const OcrWord*> words;
};
std::vector<TextRun> textRuns(const OcrReport& report) {
    struct Row {
        RectF bounds;
        std::vector<const OcrWord*> words;
    };
    std::vector<const OcrWord*> words;
    for (const auto& line : report.lines)
        for (const auto& word : line.words)
            if (!word.text.empty() && word.bounds.Width > 0 && word.bounds.Height > 0)
                words.push_back(&word);
    std::stable_sort(words.begin(), words.end(), [](const OcrWord* a, const OcrWord* b) {
        return a->bounds.Y + a->bounds.Height / 2 < b->bounds.Y + b->bounds.Height / 2;
    });
    auto unite = [](RectF a, RectF b) {
        float left = std::min(a.X, b.X), top = std::min(a.Y, b.Y);
        return RectF(left, top, std::max(a.GetRight(), b.GetRight()) - left,
                     std::max(a.GetBottom(), b.GetBottom()) - top);
    };
    std::vector<Row> rows;
    for (const auto* word : words) {
        const auto& b = word->bounds;
        Row* match = nullptr;
        float closest = 1e30f;
        for (auto& row : rows) {
            const auto& a = row.bounds;
            float overlap = std::min(a.GetBottom(), b.GetBottom()) - std::max(a.Y, b.Y);
            float distance = std::abs(a.Y + a.Height / 2 - b.Y - b.Height / 2);
            if (overlap >= std::min(a.Height, b.Height) * 0.45f &&
                distance <= std::max(a.Height, b.Height) * 0.6f && distance < closest) {
                match = &row;
                closest = distance;
            }
        }
        if (match) {
            match->bounds = unite(match->bounds, b);
            match->words.push_back(word);
        } else
            rows.push_back({b, {word}});
    }
    std::vector<TextRun> runs;
    for (auto& row : rows) {
        std::stable_sort(
            row.words.begin(), row.words.end(),
            [](const OcrWord* a, const OcrWord* b) { return a->bounds.X < b->bounds.X; });
        TextRun segment;
        auto& run = segment.line;
        for (const auto* word : row.words) {
            // Split separate columns or labels, but keep normal interword spaces.
            if (!run.text.empty() && word->bounds.X - run.bounds.GetRight() >
                                         1.5f * std::max(run.bounds.Height, word->bounds.Height)) {
                runs.push_back(std::move(segment));
                segment = {};
            }
            if (run.text.empty())
                run = *word;
            else {
                run.text += L' ';
                run.text += word->text;
                run.bounds = unite(run.bounds, word->bounds);
            }
            segment.words.push_back(word);
        }
        if (!run.text.empty())
            runs.push_back(std::move(segment));
    }
    std::stable_sort(runs.begin(), runs.end(), [](const TextRun& left, const TextRun& right) {
        const auto& a = left.line;
        const auto& b = right.line;
        return a.bounds.Y < b.bounds.Y || (a.bounds.Y == b.bounds.Y && a.bounds.X < b.bounds.X);
    });
    return runs;
}
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
ARGB textColor(Raster& pixels, const RectF& bounds, const Rect& context, ARGB bg) {
    Rect ink = wordRegion(bounds, 0);
    auto visit = [&](auto sample) {
        for (int y = std::max(context.Y, ink.Y); y < std::min(context.GetBottom(), ink.GetBottom());
             ++y)
            for (int x = std::max(context.X, ink.X);
                 x < std::min(context.GetRight(), ink.GetRight()); ++x)
                sample(pixels.at(x - context.X, y - context.Y));
    };
    int strongest = 0;
    ARGB furthest = bg;
    visit([&](ARGB color) {
        int distance = colorDistance(color, bg);
        if (distance > strongest) {
            strongest = distance;
            furthest = color | 0xff000000;
        }
    });
    Histogram foreground;
    // Relative contrast also recognizes faint colored glyphs. Never invent a
    // black/white foreground when no samples reach a fixed contrast threshold.
    int threshold = std::max(1, strongest / 3);
    visit([&](ARGB color) {
        if (colorDistance(color, bg) > threshold)
            countColor(foreground, color);
    });
    ARGB estimate = dominant(foreground, furthest);
    auto key = [](ARGB color) {
        return (((color >> 16) & 255) >> 3) << 10 | (((color >> 8) & 255) >> 3) << 5 |
               ((color & 255) >> 3);
    };
    // Within the winning color cluster, prefer solid glyph cores over blended
    // antialiased edges, which would lighten blue or darken white lettering.
    strongest = -1;
    visit([&](ARGB color) {
        int distance = colorDistance(color, bg);
        if (key(color) == key(estimate) && distance > strongest) {
            strongest = distance;
            furthest = color | 0xff000000;
        }
    });
    return furthest;
}
Replacement replacement(Raster& pixels, const TextRun& run, const Rect& context) {
    const auto& word = run.line;
    Rect ink = wordRegion(word.bounds, 0), ring = wordRegion(word.bounds, 4);
    Histogram background;
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
    ARGB fg = textColor(pixels, word.bounds, context, bg);
    Replacement result{word.bounds, bg, word.text, {}};
    float left = word.bounds.X - 2;
    for (size_t n = 0; n < run.words.size(); ++n) {
        const auto& current = run.words[n]->bounds;
        float right = n + 1 < run.words.size()
                          ? (current.GetRight() + run.words[n + 1]->bounds.X) / 2
                          : word.bounds.GetRight() + 2;
        ARGB color = textColor(pixels, current, context, bg);
        if (colorDistance(color, fg) <= 3 * 8 * 8)
            color = fg;
        if (!result.spans.empty() && result.spans.back().color == color)
            result.spans.back().right = right;
        else
            result.spans.push_back({left, right, color});
        left = right;
    }
    return result;
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
    const auto runs = textRuns(report);
    std::vector<Replacement> substitutes;
    {
        // RGB snapshots avoid a second full-screen conversion buffer. Estimate
        // every line from original pixels before any neighboring text is erased.
        INT format = snapshot->GetPixelFormat() == PixelFormat32bppRGB ? PixelFormat32bppRGB
                                                                       : PixelFormat32bppARGB;
        Raster pixels(*snapshot, ImageLockModeRead, format);
        if (!pixels.locked)
            return {};
        for (const auto& run : runs) {
            if (stopped())
                return {};
            substitutes.push_back(replacement(pixels, run, context));
        }
    }
    if (!substitutes.empty()) {
        Graphics g(snapshot.get());
        if (g.GetLastStatus() != Ok)
            return {};
        // Erase complete runs before drawing any replacement lines.
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
            RectF ink;
            float scale = 0, best = -1;
            // Choose similarly long Lorem content that naturally fits the line.
            // Letter shapes always use one uniform scale, never independent
            // horizontal/vertical stretching to force an exact bounding box.
            for (int attempt = 0; attempt < 6; ++attempt) {
                if (stopped())
                    return {};
                auto text = loremText(word.text, seed);
                GraphicsPath candidate;
                if (candidate.AddString(text.c_str(), int(text.size()), &family, FontStyleRegular,
                                        32, PointF(0, 0), StringFormat::GenericTypographic()) != Ok)
                    return {};
                RectF box;
                candidate.GetBounds(&box);
                if (box.Width <= 0 || box.Height <= 0)
                    continue;
                float fit =
                    std::min(word.bounds.Width / box.Width, word.bounds.Height / box.Height);
                float coverage =
                    (box.Width * fit / word.bounds.Width) * (box.Height * fit / word.bounds.Height);
                if (coverage > best) {
                    glyphs.Reset();
                    if (glyphs.AddPath(&candidate, FALSE) != Ok)
                        return {};
                    ink = box;
                    scale = fit;
                    best = coverage;
                }
                if (best >= 0.97f)
                    break;
            }
            if (scale <= 0)
                continue;
            float top = word.bounds.Y + (word.bounds.Height - ink.Height * scale) / 2;
            Matrix transform(scale, 0, 0, scale, word.bounds.X - ink.X * scale,
                             top - ink.Y * scale);
            glyphs.Transform(&transform);
            std::vector<float> gaps;
            if (word.spans.size() > 1) {
                int count = glyphs.GetPointCount();
                std::vector<PointF> points(count);
                std::vector<BYTE> types(count);
                std::vector<std::pair<float, float>> contours;
                if (glyphs.GetPathPoints(points.data(), count) == Ok &&
                    glyphs.GetPathTypes(types.data(), count) == Ok) {
                    for (int n = 0; n < count; ++n) {
                        if ((types[n] & PathPointTypePathTypeMask) == PathPointTypeStart)
                            contours.push_back({points[n].X, points[n].X});
                        else if (!contours.empty()) {
                            contours.back().first = std::min(contours.back().first, points[n].X);
                            contours.back().second = std::max(contours.back().second, points[n].X);
                        }
                    }
                    std::sort(contours.begin(), contours.end());
                    float right = word.bounds.X;
                    for (const auto& contour : contours) {
                        if (contour.first - right > word.bounds.Height * 0.16f)
                            gaps.push_back((right + contour.first) / 2);
                        right = std::max(right, contour.second);
                    }
                }
            }
            // Color clips reuse the complete line path and its one transform.
            // They do not reposition, resize, or regenerate individual words.
            float left = word.spans.front().left;
            for (size_t n = 0; n < word.spans.size(); ++n) {
                const auto& span = word.spans[n];
                float right = span.right;
                // Prefer a nearby space rather than changing color mid-glyph.
                if (n + 1 < word.spans.size()) {
                    float closest = word.bounds.Height;
                    for (float gap : gaps)
                        if (gap > left + 1 && gap < word.spans[n + 1].right - 1 &&
                            std::abs(gap - span.right) < closest) {
                            closest = std::abs(gap - span.right);
                            right = gap;
                        }
                }
                auto saved = g.Save();
                g.SetClip(RectF(left, word.bounds.Y - 2, right - left, word.bounds.Height + 4),
                          CombineModeIntersect);
                SolidBrush foreground(Color(span.color));
                Status status = g.FillPath(&foreground, &glyphs);
                g.Restore(saved);
                if (status != Ok)
                    return {};
                left = right;
            }
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
