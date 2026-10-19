#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <memory>
#include <string>
#include <vector>

#include "qr.h"
#include "quirc/quirc.h"

namespace captura {
namespace {
// quirc needs a quiet zone around the symbol and at least a few pixels per module.
// Users often drag tightly around a small on-screen code, so the region is padded with
// white and upscaled by nearest neighbor before detection.
constexpr int Margin = 16, MinimumSide = 400, MaximumSide = 4096;

std::wstring decodePayload(const quirc_data& data) {
    const char* bytes = reinterpret_cast<const char*>(data.payload);
    int length = data.payload_len;
    if (length <= 0)
        return {};
    auto convert = [&](UINT codePage, DWORD flags) {
        int size = MultiByteToWideChar(codePage, flags, bytes, length, nullptr, 0);
        std::wstring text(static_cast<size_t>(std::max(size, 0)), L'\0');
        if (size > 0)
            MultiByteToWideChar(codePage, flags, bytes, length, text.data(), size);
        return text;
    };
    if (data.data_type == QUIRC_DATA_TYPE_KANJI)
        return convert(932, 0);
    UINT codePage = 0;
    switch (data.eci) {
    case QUIRC_ECI_ISO_8859_1:
        codePage = 28591;
        break;
    case QUIRC_ECI_IBM437:
        codePage = 437;
        break;
    case QUIRC_ECI_ISO_8859_2:
        codePage = 28592;
        break;
    case QUIRC_ECI_ISO_8859_5:
        codePage = 28595;
        break;
    case QUIRC_ECI_ISO_8859_7:
        codePage = 28597;
        break;
    case QUIRC_ECI_ISO_8859_9:
        codePage = 28599;
        break;
    case QUIRC_ECI_WINDOWS_874:
        codePage = 874;
        break;
    case QUIRC_ECI_ISO_8859_15:
        codePage = 28605;
        break;
    case QUIRC_ECI_SHIFT_JIS:
        codePage = 932;
        break;
    case QUIRC_ECI_UTF_8:
        codePage = CP_UTF8;
        break;
    }
    if (codePage)
        return convert(codePage, 0);
    // Most generators emit UTF-8 without an ECI header; the standard default is Latin-1.
    std::wstring utf8 = convert(CP_UTF8, MB_ERR_INVALID_CHARS);
    return utf8.empty() ? convert(28591, 0) : utf8;
}
} // namespace

std::vector<QrCode> scanQrCodes(Gdiplus::Bitmap& image, Gdiplus::Rect region) {
    std::vector<QrCode> codes;
    Gdiplus::Rect bounds(0, 0, static_cast<INT>(image.GetWidth()),
                         static_cast<INT>(image.GetHeight()));
    Gdiplus::Rect roi;
    if (!Gdiplus::Rect::Intersect(roi, region, bounds) || roi.Width < 1 || roi.Height < 1)
        return codes;
    int scale = std::max(1, (MinimumSide + std::max(roi.Width, roi.Height) - 1) /
                                std::max(roi.Width, roi.Height));
    while (scale > 1 && std::max(roi.Width, roi.Height) * scale > MaximumSide)
        --scale;
    int width = roi.Width * scale + 2 * Margin, height = roi.Height * scale + 2 * Margin;

    struct Recognizer {
        quirc* q = quirc_new();
        ~Recognizer() {
            if (q)
                quirc_destroy(q);
        }
    } recognizer;
    if (!recognizer.q || quirc_resize(recognizer.q, width, height) < 0)
        return codes;
    uint8_t* gray = quirc_begin(recognizer.q, nullptr, nullptr);
    std::fill(gray, gray + static_cast<size_t>(width) * height, uint8_t{255});

    Gdiplus::BitmapData pixels{};
    if (image.LockBits(&roi, Gdiplus::ImageLockModeRead, PixelFormat32bppRGB, &pixels) !=
        Gdiplus::Ok)
        return codes;
    for (int y = 0; y < roi.Height; ++y) {
        const uint8_t* row = static_cast<const uint8_t*>(pixels.Scan0) + y * pixels.Stride;
        uint8_t* line = gray + static_cast<size_t>(Margin + y * scale) * width + Margin;
        for (int x = 0; x < roi.Width; ++x) {
            const uint8_t* bgr = row + x * 4;
            uint8_t luma = static_cast<uint8_t>((bgr[2] * 77 + bgr[1] * 151 + bgr[0] * 28) >> 8);
            for (int dx = 0; dx < scale; ++dx)
                line[x * scale + dx] = luma;
        }
        for (int dy = 1; dy < scale; ++dy)
            memcpy(line + static_cast<size_t>(dy) * width, line,
                   static_cast<size_t>(roi.Width) * scale);
    }
    image.UnlockBits(&pixels);
    quirc_end(recognizer.q);

    int count = quirc_count(recognizer.q);
    auto code = std::make_unique<quirc_code>();
    auto data = std::make_unique<quirc_data>();
    for (int index = 0; index < count; ++index) {
        quirc_extract(recognizer.q, index, code.get());
        quirc_decode_error_t status = quirc_decode(code.get(), data.get());
        if (status == QUIRC_ERROR_DATA_ECC) {
            quirc_flip(code.get());
            status = quirc_decode(code.get(), data.get());
        }
        if (status != QUIRC_SUCCESS)
            continue;
        QrCode found;
        found.text = decodePayload(*data);
        found.url = qrUrl(found.text);
        for (int corner = 0; corner < 4; ++corner)
            found.corners[corner] = {
                roi.X + static_cast<Gdiplus::REAL>(code->corners[corner].x - Margin) / scale,
                roi.Y + static_cast<Gdiplus::REAL>(code->corners[corner].y - Margin) / scale};
        codes.push_back(std::move(found));
    }
    return codes;
}

std::wstring qrUrl(const std::wstring& text) {
    size_t start = 0, end = text.size();
    while (start < end && std::iswspace(text[start]))
        ++start;
    while (end > start && std::iswspace(text[end - 1]))
        --end;
    std::wstring trimmed = text.substr(start, end - start);
    if (trimmed.empty() || trimmed.find_first_of(L" \t\r\n") != std::wstring::npos)
        return {};
    auto startsWith = [&](const wchar_t* prefix) {
        size_t length = wcslen(prefix);
        return trimmed.size() > length && _wcsnicmp(trimmed.c_str(), prefix, length) == 0;
    };
    if (startsWith(L"http://") || startsWith(L"https://"))
        return trimmed;
    if (startsWith(L"www."))
        return L"https://" + trimmed;
    return {};
}

bool qrContains(const QrCode& code, Gdiplus::PointF point) {
    // Corners form a convex quadrilateral; the point is inside when every edge turns the
    // same way around it.
    bool positive = false, negative = false;
    for (int i = 0; i < 4; ++i) {
        const Gdiplus::PointF &a = code.corners[i], &b = code.corners[(i + 1) % 4];
        Gdiplus::REAL cross = (b.X - a.X) * (point.Y - a.Y) - (b.Y - a.Y) * (point.X - a.X);
        positive |= cross > 0;
        negative |= cross < 0;
    }
    return !(positive && negative);
}

Gdiplus::RectF qrBounds(const QrCode& code) {
    Gdiplus::REAL left = code.corners[0].X, top = code.corners[0].Y, right = left, bottom = top;
    for (const auto& corner : code.corners) {
        left = std::min(left, corner.X);
        right = std::max(right, corner.X);
        top = std::min(top, corner.Y);
        bottom = std::max(bottom, corner.Y);
    }
    return {left, top, right - left, bottom - top};
}
} // namespace captura
