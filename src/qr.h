#pragma once

#include <array>
#include <string>
#include <vector>

namespace captura {
struct QrCode {
    std::wstring text;
    std::wstring url; // Normalized http(s) link when the payload is a web address; else empty.
    std::array<Gdiplus::PointF, 4> corners{}; // Image coordinates, clockwise from top-left.
};

// Decodes every QR code whose pixels lie inside `region` of `image` (clipped to the
// bitmap). Synchronous; reads the bitmap on the calling thread. Corners are in image
// coordinates. Returns codes in detection order.
std::vector<QrCode> scanQrCodes(Gdiplus::Bitmap& image, Gdiplus::Rect region);

// Returns the http(s) address for a payload, adding a scheme to bare `www.` hosts.
// Other schemes are not treated as links, so a click never launches arbitrary handlers.
std::wstring qrUrl(const std::wstring& text);

bool qrContains(const QrCode& code, Gdiplus::PointF point);
Gdiplus::RectF qrBounds(const QrCode& code);
} // namespace captura
