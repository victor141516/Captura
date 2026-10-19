// Captura: native Win32/GDI+; only Windows system DLLs, static C++ runtime.
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <objidl.h>
#include <objbase.h>
#include <gdiplus.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdio>
#include <new>

#include "ui_text.h"
#include "resource.h"
#include "capture_shortcut.h"
#include "ocr.h"
#include "qr.h"
#include "redaction.h"
#include "startup.h"

using namespace Gdiplus;
constexpr wchar_t AppClass[] = L"Captura.Native.Editor.1";
constexpr wchar_t OverlayClass[] = L"Captura.Native.Overlay.1";
const wchar_t* mainClassName = AppClass;
constexpr UINT TrayMessage = WM_APP + 1, ActivateMessage = WM_APP + 2;
constexpr UINT OcrMessage = WM_APP + 3;
constexpr UINT_PTR ZoomRefineTimer = 2;
constexpr REAL Pi = 3.14159265358979f;
const Color Ink(255, 35, 43, 55), Muted(255, 103, 114, 130), Blue(255, 0, 110, 235);
const std::array<ARGB, 11> Swatches = {0xffef3e42, 0xfff28c28, 0xffffd23f, 0xff16a765,
                                       0xff087cf0, 0xff8752d6, 0xffec70a5, 0xff202833,
                                       0xffffffff, 0xff758294, 0xff00bcd4};
enum class Tool {
    Select,
    Arrow,
    Text,
    Rectangle,
    Line,
    Counter,
    Brush,
    TextSelect,
    Redact,
    QrScan
};
constexpr int ToolCount = 10;
struct Style {
    ARGB color = 0xffef3e42, fill = 0xffef3e42;
    bool filled = false;
    REAL size = 12;
    int arrow = 0;
    int redaction = 1;
};
struct RedactionCache {
    const Bitmap* source = nullptr;
    const captura::OcrReport* ocr = nullptr;
    RECT region{};
    REAL strength = 0;
    int mode = -1;
    ARGB fill = 0;
    uint64_t underlay = 0;
    bool preview = false;
    captura::RedactionResult result;
};
struct Item {
    Tool tool = Tool::Arrow;
    PointF a{0, 0}, b{0, 0};
    Style style;
    std::wstring text;
    int number = 1;
    bool pointer = false;
    PointF bend{0, 0};
    bool bent = false;
    bool cropped = false;
    std::vector<PointF> stroke;
    RectF strokeBounds;
    mutable std::shared_ptr<RedactionCache> redactionCache;
};
struct ImageCanvas {
    RECT bounds{}, manualFrame{};
    ARGB color = 0xffffffff;
};
struct Snapshot {
    std::vector<Item> items;
    int selected = -1;
    ImageCanvas canvas;
};
struct FrozenWindow {
    RECT rect;
};
struct Button {
    RectF rect;
    int id;
    bool enabled = true;
};
struct Layout {
    REAL width = 0, height = 0, scale = 1, side = 0;
    RectF canvas;
    REAL imageX = 0, imageY = 0, zoom = 1;
};
struct AppState {
    HINSTANCE instance = nullptr;
    HWND main = nullptr, overlay = nullptr, edit = nullptr, tooltip = nullptr;
    HWND shortcutDialog = nullptr;
    HICON icon = nullptr;
    HFONT editFont = nullptr;
    ULONG_PTR gdiplus = 0;
    HANDLE mutex = nullptr;
    std::unique_ptr<Bitmap> image, frozen;
    RECT desktop{}, selection{};
    std::vector<FrozenWindow> windows;
    std::vector<Item> items;
    std::vector<Snapshot> undo, redo;
    std::vector<Button> buttons;
    ImageCanvas canvas;
    bool pinnedCanvas = false;
    PointF pinnedOrigin{};
    REAL pinnedZoom = 1;
    bool resizingCanvas = false;
    int canvasHandle = -1, hoverCanvas = -1;
    RECT resizeStart{}, gestureCanvas{};
    PointF canvasGrab{};
    std::array<Style, ToolCount> defaults;
    int selected = -1, hover = -1, dragHandle = -1, editIndex = -1;
    Tool tool = Tool::Arrow;
    bool dragging = false, creating = false, panning = false, background = false, sizing = false;
    bool windowMode = false, selecting = false, capturePending = false, captureRestore = false;
    bool hotkey = false, tray = false, test = false, fillTarget = false, managed = false;
    bool zooming = false;
    std::shared_ptr<captura::OcrJob> ocr;
    bool selectingText = false, hasTextSelection = false;
    PointF textStart{};
    RectF textArea{};
    std::wstring textFeedback;
    bool scanningQr = false, hasQrArea = false;
    PointF qrStart{};
    RectF qrArea{};
    std::vector<captura::QrCode> qrCodes;
    std::wstring qrFeedback;
    int pressedQr = -1;
#ifdef CAPTURA_TEST
    bool shortcutPreview = false;
    bool testAutoOcr = false;
    std::wstring lifecycleDirectory;
    bool lifecycleNoTray = false;
    unsigned lifecycleActivations = 0;
#endif
    PointF dragStart{}, pan{}, initialPan{};
    Item initialItem;
    REAL userZoom = 0;
    Layout layout;
    CaptureShortcut captureShortcut;
    int hotkeyId = 1;
    std::wstring settingsPath;
    std::wstring status;
    COLORREF customColors[16]{};
    ARGB sharedColor = 0xffef3e42;
    std::wstring editOriginal;
    ImageCanvas editCanvas;
    DWORD editSelectionStart = MAXDWORD, editSelectionEnd = MAXDWORD;
    int hoverButton = -1, pressedButton = -1;
    bool trackingMouse = false;
    std::array<bool, 512> tooltipAdded{};
} app;
captura::OcrService ocrService;
#ifdef CAPTURA_TEST
void recordLifecycle() {
    if (app.lifecycleDirectory.empty())
        return;
    FILE* file = nullptr;
    _wfopen_s(&file, (app.lifecycleDirectory + L"\\state.txt").c_str(), L"wb");
    if (file) {
        fprintf(file, "background=%d\nmanaged=%d\nvisible=%d\ntray=%d\nhotkey=%d\nactivations=%u\n",
                app.background, app.managed, IsWindowVisible(app.main) != FALSE, app.tray,
                app.hotkey, app.lifecycleActivations);
        fclose(file);
    }
}
#endif

const captura::OcrReport* ocrReport() {
    return app.ocr && app.ocr->complete.load(std::memory_order_acquire) ? &app.ocr->report
                                                                        : nullptr;
}
bool ocrReady() {
    const auto* report = ocrReport();
    return report && report->state == captura::OcrState::Ready;
}
bool loremReady() {
    const auto* report = ocrReport();
    return report && report->loremImage &&
           (report->state == captura::OcrState::Ready || report->state == captura::OcrState::Empty);
}
void startOcr() {
    if (app.ocr)
        app.ocr->cancelled = true;
    app.ocr.reset();
    app.selectingText = app.hasTextSelection = false;
    app.textFeedback.clear();
#ifdef CAPTURA_TEST
    if (app.test && !app.testAutoOcr)
        return;
#endif
    if (app.image)
        app.ocr = ocrService.start(*app.image, app.main, OcrMessage);
}
void resetQr() {
    app.scanningQr = app.hasQrArea = false;
    app.pressedQr = -1;
    app.qrCodes.clear();
    app.qrFeedback.clear();
}

std::wstring captureHint() {
    return captureShortcutName(app.captureShortcut) + L" to capture";
}
std::wstring helpText() {
    std::wstring text = ui::kHelp;
    const std::wstring token = L"{capture-shortcut}";
    text.replace(text.find(token), token.size(), captureShortcutName(app.captureShortcut));
    return text;
}

// Reuse the paint buffers instead of allocating a screen-sized bitmap for
// every mouse movement. Their GDI resources are released at window shutdown.
struct PaintBuffer {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ original = nullptr;
    int width = 0, height = 0;
    void reset() {
        if (dc && original)
            SelectObject(dc, original);
        if (bitmap)
            DeleteObject(bitmap);
        if (dc)
            DeleteDC(dc);
        dc = nullptr;
        bitmap = nullptr;
        original = nullptr;
        width = height = 0;
    }
    bool ensure(HDC target, int w, int h) {
        if (dc && width == w && height == h)
            return true;
        reset();
        dc = CreateCompatibleDC(target);
        if (dc)
            bitmap = CreateCompatibleBitmap(target, w, h);
        if (!dc || !bitmap) {
            reset();
            return false;
        }
        original = SelectObject(dc, bitmap);
        width = w;
        height = h;
        return true;
    }
    ~PaintBuffer() { reset(); }
} editorBuffer, overlayBuffer;

struct OverlayCache {
    PaintBuffer original, dark;
    const Bitmap* source = nullptr;
    unsigned builds = 0;
    RECT hint{};
    void reset() {
        original.reset();
        dark.reset();
        source = nullptr;
        hint = {};
    }
} overlayCache;

struct CaptureViewCache {
    std::unique_ptr<Bitmap> bitmap;
    std::unique_ptr<CachedBitmap> native;
    const Bitmap* source = nullptr;
    Layout layout;
    RECT extent{};
    PointF phase{};
    InterpolationMode interpolation = InterpolationModeInvalid;
    unsigned builds = 0;
    void reset() {
        native.reset();
        bitmap.reset();
        source = nullptr;
        extent = {};
    }
} captureView;

struct NativeSourceRaster {
    std::unique_ptr<ARGB[]> pixels;
    BITMAPINFO info{};
    int width = 0, height = 0;
    const Bitmap* source = nullptr;
    unsigned builds = 0;
    void reset() {
        pixels.reset();
        source = nullptr;
        width = height = 0;
    }
    bool ensure(Bitmap* image) {
        if (!image)
            return false;
        if (source == image && pixels)
            return true;
        reset();
        width = static_cast<int>(image->GetWidth());
        height = static_cast<int>(image->GetHeight());
        size_t count = static_cast<size_t>(width) * height;
        pixels.reset(new (std::nothrow) ARGB[count]);
        BitmapData data{};
        Rect whole(0, 0, width, height);
        if (!pixels ||
            image->LockBits(&whole, ImageLockModeRead, PixelFormat32bppRGB, &data) != Ok) {
            reset();
            return false;
        }
        for (int y = 0; y < height; y++)
            memcpy(pixels.get() + static_cast<size_t>(y) * width,
                   static_cast<const BYTE*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride,
                   static_cast<size_t>(width) * 4);
        image->UnlockBits(&data);
        info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        source = image;
        ++builds;
        return true;
    }
} sourceRaster;

PaintBuffer dragBackground;
PaintBuffer navigationBackground;
unsigned long long sceneRevision = 1, dragRevision = 0;
unsigned long long navigationRevision = 0;
Layout navigationLayout;
int navigationHover = -2, navigationPressed = -2;
int cachedSelection = -1;
Layout dragLayout;
bool sameView(const Layout& a, const Layout& b) {
    return a.width == b.width && a.height == b.height && a.scale == b.scale &&
           a.imageX == b.imageX && a.imageY == b.imageY && a.zoom == b.zoom;
}

REAL distance(PointF a, PointF b) {
    return std::hypot(a.X - b.X, a.Y - b.Y);
}
PointF add(PointF a, PointF b) {
    return {a.X + b.X, a.Y + b.Y};
}
PointF sub(PointF a, PointF b) {
    return {a.X - b.X, a.Y - b.Y};
}
void moveItem(Item& item, PointF delta) {
    item.a = add(item.a, delta);
    item.b = add(item.b, delta);
    if (item.bent)
        item.bend = add(item.bend, delta);
    for (auto& p : item.stroke)
        p = add(p, delta);
    if (item.tool == Tool::Brush) {
        item.strokeBounds.X += delta.X;
        item.strokeBounds.Y += delta.Y;
    }
}
RectF pointBounds(const std::vector<PointF>& points) {
    if (points.empty())
        return {};
    REAL left = points[0].X, top = points[0].Y, right = left, bottom = top;
    for (auto p : points) {
        left = std::min(left, p.X);
        top = std::min(top, p.Y);
        right = std::max(right, p.X);
        bottom = std::max(bottom, p.Y);
    }
    return {left, top, right - left, bottom - top};
}
RectF normalized(PointF a, PointF b) {
    return {std::min(a.X, b.X), std::min(a.Y, b.Y), std::abs(a.X - b.X), std::abs(a.Y - b.Y)};
}
bool contains(RectF r, PointF p) {
    return p.X >= r.X && p.Y >= r.Y && p.X <= r.GetRight() && p.Y <= r.GetBottom();
}
bool validIndex(int i) {
    return i >= 0 && i < static_cast<int>(app.items.size());
}
RECT canvasBounds() {
    if (!IsRectEmpty(&app.canvas.bounds))
        return app.canvas.bounds;
    return app.image ? RECT{0, 0, static_cast<LONG>(app.image->GetWidth()),
                            static_cast<LONG>(app.image->GetHeight())}
                     : RECT{};
}
#ifdef CAPTURA_TEST
bool testShiftPressed = false, testCtrlPressed = false;
#endif
bool keyPressed(int key) {
#ifdef CAPTURA_TEST
    if (app.test)
        return (key == VK_SHIFT && testShiftPressed) || (key == VK_CONTROL && testCtrlPressed);
#endif
    return !app.test && (GetKeyState(key) & 0x8000) != 0;
}
void invalidate() {
    ++sceneRevision;
    if (app.main)
        InvalidateRect(app.main, nullptr, FALSE);
}
void report(const wchar_t* s) {
    app.status = s;
    invalidate();
}
void error(HWND owner, const wchar_t* s) {
    MessageBoxW(owner, s, ui::kAppName, MB_OK | MB_ICONERROR);
}
void checkpoint() {
    app.undo.push_back({app.items, app.selected, app.canvas});
    if (app.undo.size() > 80)
        app.undo.erase(app.undo.begin());
    app.redo.clear();
}
void undo(bool redo) {
    auto& from = redo ? app.redo : app.undo;
    auto& to = redo ? app.undo : app.redo;
    if (from.empty())
        return;
    to.push_back({app.items, app.selected, app.canvas});
    Snapshot s = std::move(from.back());
    from.pop_back();
    bool resized = !EqualRect(&app.canvas.bounds, &s.canvas.bounds);
    app.items = std::move(s.items);
    app.selected = s.selected;
    app.canvas = s.canvas;
    app.pinnedCanvas = false;
    if (resized) {
        app.userZoom = 0;
        app.pan = {0, 0};
    }
    invalidate();
}
Style& activeStyle() {
    return validIndex(app.selected) ? app.items[app.selected].style
                                    : app.defaults[static_cast<int>(app.tool)];
}
Tool activeTool() {
    return validIndex(app.selected) ? app.items[app.selected].tool : app.tool;
}
ARGB contrastingText(ARGB color) {
    auto linear = [](REAL value) {
        value /= 255;
        return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
    };
    REAL luminance = 0.2126f * linear(static_cast<REAL>((color >> 16) & 255)) +
                     0.7152f * linear(static_cast<REAL>((color >> 8) & 255)) +
                     0.0722f * linear(static_cast<REAL>(color & 255));
    return (luminance + 0.05f) / 0.05f >= 1.05f / (luminance + 0.05f) ? 0xff000000 : 0xffffffff;
}
void shareColor(ARGB chosen) {
    app.sharedColor = chosen;
    for (int i = 0; i < ToolCount; i++)
        app.defaults[i].color = chosen;
    Style& text = app.defaults[static_cast<int>(Tool::Text)];
    text.fill = chosen;
    if (text.filled)
        text.color = contrastingText(chosen);
}
void drawText(Graphics& g, const std::wstring& s, RectF r, REAL size, Color c = Ink,
              bool bold = false, bool center = false) {
    Font font(L"Segoe UI", size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    SolidBrush brush(c);
    StringFormat fmt;
    fmt.SetLineAlignment(StringAlignmentCenter);
    fmt.SetAlignment(center ? StringAlignmentCenter : StringAlignmentNear);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    g.DrawString(s.c_str(), -1, &font, r, &fmt, &brush);
}
void glyphPath(GraphicsPath& path, const std::wstring& text, REAL size,
               INT fontStyle = FontStyleRegular) {
    FontFamily family(L"Segoe UI");
    StringFormat format(StringFormat::GenericTypographic());
    path.AddString(text.c_str(), static_cast<INT>(text.size()), &family, fontStyle, size,
                   PointF(0, 0), &format);
}
REAL lineSpacing(REAL size) {
    FontFamily family(L"Segoe UI");
    return size * family.GetLineSpacing(FontStyleRegular) / family.GetEmHeight(FontStyleRegular);
}
REAL textPadding(REAL size) {
    return std::max(6.f, size * 0.32f);
}
RectF inkBounds(GraphicsPath& path, const Item& item) {
    RectF ink;
    GraphicsPath capital;
    glyphPath(capital, L"A", item.style.size);
    RectF cap;
    capital.GetBounds(&cap);
    int rows = 1;
    for (wchar_t c : item.text)
        if (c == L'\n')
            rows++;
    if (path.GetPointCount())
        path.GetBounds(&ink);
    else
        ink = {0, cap.Y, std::max(8.f, item.style.size * 0.45f),
               cap.Height + (rows - 1) * lineSpacing(item.style.size)};
    if (!item.text.empty() && (item.text.front() == L'\r' || item.text.front() == L'\n')) {
        REAL bottom = ink.GetBottom();
        ink.Y = std::min(ink.Y, cap.Y);
        ink.Height = bottom - ink.Y;
    }
    if (!item.text.empty() && item.text.back() == L'\n')
        ink.Height = std::max(ink.Height,
                              cap.GetBottom() + (rows - 1) * lineSpacing(item.style.size) - ink.Y);
    return ink;
}
RectF textRect(const Item& item) {
    GraphicsPath path;
    glyphPath(path, item.text, item.style.size);
    RectF ink = inkBounds(path, item);
    REAL padding = textPadding(item.style.size);
    return {item.a.X, item.a.Y, ink.Width + 2 * padding, ink.Height + 2 * padding};
}
void roundedRect(GraphicsPath& path, RectF r, REAL radius) {
    REAL d = 2 * std::min({radius, r.Width / 2, r.Height / 2});
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
}
REAL counterRadius(const Item& item) {
    return std::max(12.f, item.style.size * 0.8f);
}
PointF counterTip(const Item& item) {
    REAL r = counterRadius(item), length = distance(item.a, item.b);
    PointF direction = length > 0.01f
                           ? PointF((item.b.X - item.a.X) / length, (item.b.Y - item.a.Y) / length)
                           : PointF(1, 0);
    REAL reach = std::clamp(length, 1.60f * r, 3 * r);
    return add(item.a, {direction.X * reach, direction.Y * reach});
}
struct CounterTangents {
    PointF lower, upper;
    REAL angle, spread;
};
CounterTangents counterTangents(const Item& item) {
    PointF tip = counterTip(item);
    REAL r = counterRadius(item), length = distance(item.a, tip);
    REAL angle = std::atan2(tip.Y - item.a.Y, tip.X - item.a.X),
         spread = std::acos(std::clamp(r / length, 0.f, 1.f));
    // (T-C) dot (P-T) = 0: the segment to P is tangent to the circle at T.
    return {{item.a.X + r * std::cos(angle + spread), item.a.Y + r * std::sin(angle + spread)},
            {item.a.X + r * std::cos(angle - spread), item.a.Y + r * std::sin(angle - spread)},
            angle,
            spread};
}
void counterPath(GraphicsPath& path, const Item& item) {
    REAL r = counterRadius(item);
    if (!item.pointer) {
        path.AddEllipse(item.a.X - r, item.a.Y - r, 2 * r, 2 * r);
        return;
    }
    PointF tip = counterTip(item);
    CounterTangents tangent = counterTangents(item);
    // Keep the long rear arc and add the two external tangent segments.
    // This convex outline contains the entire original circular disk.
    path.AddArc(item.a.X - r, item.a.Y - r, 2 * r, 2 * r,
                (tangent.angle + tangent.spread) * 180 / Pi, 360 - 2 * tangent.spread * 180 / Pi);
    path.AddLine(tangent.upper, tip);
    path.AddLine(tip, tangent.lower);
    path.CloseFigure();
}
PointF curveMiddle(const Item& item) {
    return item.bent ? item.bend
                     : PointF((item.a.X + item.b.X) * 0.5f, (item.a.Y + item.b.Y) * 0.5f);
}
PointF curveControl(const Item& item) {
    PointF middle = curveMiddle(item);
    return {2 * middle.X - (item.a.X + item.b.X) * 0.5f,
            2 * middle.Y - (item.a.Y + item.b.Y) * 0.5f};
}
PointF curvePoint(const Item& item, REAL t) {
    PointF c = curveControl(item);
    REAL u = 1 - t;
    return {u * u * item.a.X + 2 * u * t * c.X + t * t * item.b.X,
            u * u * item.a.Y + 2 * u * t * c.Y + t * t * item.b.Y};
}
void arrowHead(Graphics& g, PointF tip, PointF from, Color color, REAL head) {
    REAL angle = std::atan2(tip.Y - from.Y, tip.X - from.X);
    PointF v[3] = {
        tip,
        {tip.X - head * std::cos(angle - 0.45f), tip.Y - head * std::sin(angle - 0.45f)},
        {tip.X - head * std::cos(angle + 0.45f), tip.Y - head * std::sin(angle + 0.45f)}};
    SolidBrush b(color);
    g.FillPolygon(&b, v, 3);
}
void drawArrowItem(Graphics& g, const Item& item) {
    if (distance(item.a, item.b) < 0.1f)
        return;
    std::array<REAL, 65> lengths{};
    PointF previous = item.a;
    for (int k = 1; k <= 64; k++) {
        PointF p = curvePoint(item, k / 64.f);
        lengths[k] = lengths[k - 1] + distance(previous, p);
        previous = p;
    }
    REAL total = lengths[64], head = std::min(std::max(10.f, item.style.size * 3.8f),
                                              total * (item.style.arrow == 1 ? 0.42f : 0.65f));
    auto parameter = [&](REAL length) {
        for (int k = 1; k <= 64; k++)
            if (lengths[k] >= length)
                return ((k - 1) +
                        (length - lengths[k - 1]) / std::max(0.001f, lengths[k] - lengths[k - 1])) /
                       64.f;
        return 1.f;
    };
    REAL t0 = item.style.arrow == 1 ? parameter(head * 0.65f) : 0,
         t1 = parameter(total - head * 0.65f);
    PointF a = curvePoint(item, t0), b = curvePoint(item, t1), control = curveControl(item);
    PointF derivative = {(1 - t0) * (control.X - item.a.X) + t0 * (item.b.X - control.X),
                         (1 - t0) * (control.Y - item.a.Y) + t0 * (item.b.Y - control.Y)};
    PointF trimmedControl = add(a, {derivative.X * (t1 - t0), derivative.Y * (t1 - t0)});
    Pen pen(Color(item.style.color), std::min(item.style.size, head * 0.5f));
    pen.SetStartCap(item.style.arrow == 1 ? LineCapFlat : LineCapRound);
    pen.SetEndCap(LineCapFlat);
    g.DrawBezier(&pen, a,
                 add(a, {(trimmedControl.X - a.X) * 2 / 3, (trimmedControl.Y - a.Y) * 2 / 3}),
                 add(b, {(trimmedControl.X - b.X) * 2 / 3, (trimmedControl.Y - b.Y) * 2 / 3}), b);
    PointF endFrom = distance(control, item.b) > 0.1f ? control : item.a,
           startFrom = distance(control, item.a) > 0.1f ? control : item.b;
    arrowHead(g, item.b, endFrom, Color(item.style.color), head);
    if (item.style.arrow == 1)
        arrowHead(g, item.a, startFrom, Color(item.style.color), head);
}
void drawArrow(Graphics& g, PointF a, PointF b, const Style& style) {
    Item item;
    item.a = a;
    item.b = b;
    item.style = style;
    drawArrowItem(g, item);
}

REAL textAdvance(Graphics& g, const std::wstring& text, REAL size) {
    if (text.empty())
        return 0;
    Font font(L"Segoe UI", size, FontStyleRegular, UnitPixel);
    StringFormat format(StringFormat::GenericTypographic());
    format.SetFormatFlags(StringFormatFlagsMeasureTrailingSpaces | StringFormatFlagsNoWrap);
    RectF measured;
    g.MeasureString(text.c_str(), static_cast<INT>(text.size()), &font, PointF(0, 0), &format,
                    &measured);
    return measured.Width;
}
void inlineSelection(Graphics& g, const Item& item, RectF ink) {
    DWORD first = 0, last = 0;
    SendMessageW(app.edit, EM_GETSEL, reinterpret_cast<WPARAM>(&first),
                 reinterpret_cast<LPARAM>(&last));
    size_t begin = 0;
    int row = 0;
    REAL padding = textPadding(item.style.size), spacing = lineSpacing(item.style.size);
    GraphicsPath capital;
    glyphPath(capital, L"A", item.style.size);
    RectF cap;
    capital.GetBounds(&cap);
    REAL originX = item.a.X + padding - ink.X, originY = item.a.Y + padding - ink.Y;
    SolidBrush highlight(Color(90, 0, 110, 235));
    Pen caret(Color(item.style.color), std::max(1.f, 1 / app.layout.zoom));
    do {
        size_t nl = item.text.find(L'\n', begin),
               end = nl == std::wstring::npos ? item.text.size() : nl;
        if (end > begin && item.text[end - 1] == L'\r')
            end--;
        size_t a = std::clamp(static_cast<size_t>(first), begin, end),
               b = std::clamp(static_cast<size_t>(last), begin, end);
        REAL x1 = originX + textAdvance(g, item.text.substr(begin, a - begin), item.style.size),
             x2 = originX + textAdvance(g, item.text.substr(begin, b - begin), item.style.size);
        REAL top = originY + row * spacing + cap.Y;
        if (b > a)
            g.FillRectangle(&highlight, x1, top, std::max(1.f, x2 - x1), cap.Height);
        if (first == last && first >= begin && first <= end)
            g.DrawLine(&caret, x1, top, x1, top + cap.Height);
        if (nl == std::wstring::npos)
            break;
        begin = nl + 1;
        row++;
    } while (begin <= item.text.size());
}
RECT redactionRegion(const Item& item) {
    RectF r = normalized(item.a, item.b);
    return {LONG(std::floor(r.X)), LONG(std::floor(r.Y)), LONG(std::ceil(r.GetRight())),
            LONG(std::ceil(r.GetBottom()))};
}
bool hiddenOcrWord(const captura::OcrWord& word) {
    for (const auto& item : app.items)
        if (item.tool == Tool::Redact && word.bounds.IntersectsWith(normalized(item.a, item.b)))
            return true;
    return false;
}
void drawItem(Graphics& g, const Item& item, bool interactive = false);
uint64_t underlayHash(int before) {
    uint64_t hash = 14695981039346656037ull;
    auto bytes = [&](const void* value, size_t size) {
        auto* data = static_cast<const BYTE*>(value);
        for (size_t n = 0; n < size; ++n)
            hash = (hash ^ data[n]) * 1099511628211ull;
    };
    for (int index = 0; index < before; ++index) {
        const auto& item = app.items[index];
        bytes(&item.tool, sizeof(item.tool));
        bytes(&item.a, sizeof(item.a));
        bytes(&item.b, sizeof(item.b));
        bytes(&item.style.color, sizeof(item.style.color));
        bytes(&item.style.fill, sizeof(item.style.fill));
        bytes(&item.style.filled, sizeof(item.style.filled));
        bytes(&item.style.size, sizeof(item.style.size));
        bytes(&item.style.arrow, sizeof(item.style.arrow));
        bytes(&item.style.redaction, sizeof(item.style.redaction));
        bytes(&item.bend, sizeof(item.bend));
        bytes(&item.bent, sizeof(item.bent));
        bytes(&item.number, sizeof(item.number));
        bytes(&item.pointer, sizeof(item.pointer));
        bytes(item.text.data(), item.text.size() * sizeof(wchar_t));
        bytes(item.stroke.data(), item.stroke.size() * sizeof(PointF));
    }
    return hash;
}
void drawRedaction(Graphics& g, const Item& item, bool interactive) {
    if (!app.image)
        return;
    RECT r = redactionRegion(item);
    if (IsRectEmpty(&r))
        return;
    RectF bounds(float(r.left), float(r.top), float(r.right - r.left), float(r.bottom - r.top));
    bool selected = validIndex(app.selected) && &item == &app.items[app.selected];
    if (interactive && selected && app.creating) {
        SolidBrush tint(Color(45, 0, 110, 235));
        Pen outline(Blue, 1.5f / app.layout.zoom);
        g.FillRectangle(&tint, bounds);
        g.DrawRectangle(&outline, bounds);
        return;
    }
    int index = 0;
    for (; index < int(app.items.size()); ++index)
        if (&app.items[index] == &item)
            break;
    if (index == int(app.items.size()))
        index = 0;
    uint64_t hash = underlayHash(index);
    const auto* ocr = item.style.redaction >= 2 ? ocrReport() : nullptr;
    const Bitmap* source =
        item.style.redaction >= 2 ? (ocr ? ocr->loremImage.get() : nullptr) : app.image.get();
    auto cache = item.redactionCache;
    bool preview = interactive && (app.dragging || app.sizing) &&
                   (selected || (cache && (cache->underlay != hash || cache->preview)));
    if (!cache || cache->source != source || !EqualRect(&cache->region, &r) ||
        cache->mode != item.style.redaction || cache->strength != item.style.size ||
        cache->ocr != ocr || cache->preview != preview || cache->underlay != hash ||
        cache->fill != app.canvas.color) {
        cache = std::make_shared<RedactionCache>();
        cache->source = source;
        cache->region = r;
        cache->mode = item.style.redaction;
        cache->strength = item.style.size;
        cache->ocr = ocr;
        cache->preview = preview;
        cache->underlay = hash;
        cache->fill = app.canvas.color;
        try {
            cache->result = captura::redact(
                *app.image, r, static_cast<captura::RedactionMode>(cache->mode), cache->strength,
                ocr, app.canvas.color,
                [index, lorem = cache->mode >= 2](Graphics& below) {
                    for (int k = 0; k < index; ++k)
                        if (!lorem || app.items[k].tool != Tool::Redact)
                            drawItem(below, app.items[k]);
                },
                preview ? 384 : 0);
        } catch (...) {
            cache->result = {};
        }
        item.redactionCache = cache;
    }
    GraphicsState state = g.Save();
    g.SetClip(bounds, CombineModeIntersect);
    if (cache->result.bitmap) {
        g.SetCompositingMode(CompositingModeSourceCopy);
        g.SetInterpolationMode(InterpolationModeNearestNeighbor);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        auto& patch = *cache->result.bitmap;
        g.DrawImage(&patch, bounds, 0, 0, float(patch.GetWidth()), float(patch.GetHeight()),
                    UnitPixel);
    } else {
        // An unavailable text replacement or failed effect must never expose
        // original information through an apparently applied privacy rectangle.
        SolidBrush opaque(Color(255, 117, 130, 148));
        g.FillRectangle(&opaque, bounds);
    }
    g.Restore(state);
}
void drawItem(Graphics& g, const Item& i, bool interactive) {
    const Style& s = i.style;
    Pen pen(Color(s.color), s.size);
    pen.SetLineJoin(LineJoinRound);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    SolidBrush foreground(Color(s.color)), fill(Color(s.fill));
    switch (i.tool) {
    case Tool::Redact:
        drawRedaction(g, i, interactive);
        break;
    case Tool::Arrow:
        drawArrowItem(g, i);
        break;
    case Tool::Line:
        if (i.bent) {
            PointF c = curveControl(i);
            g.DrawBezier(&pen, i.a, add(i.a, {(c.X - i.a.X) * 2 / 3, (c.Y - i.a.Y) * 2 / 3}),
                         add(i.b, {(c.X - i.b.X) * 2 / 3, (c.Y - i.b.Y) * 2 / 3}), i.b);
        } else
            g.DrawLine(&pen, i.a, i.b);
        break;
    case Tool::Brush:
        if (i.stroke.size() > 1)
            g.DrawLines(&pen, i.stroke.data(), static_cast<INT>(i.stroke.size()));
        else
            g.FillEllipse(&foreground, i.a.X - s.size / 2, i.a.Y - s.size / 2, s.size, s.size);
        break;
    case Tool::Rectangle: {
        RectF r = normalized(i.a, i.b);
        if (s.filled)
            g.FillRectangle(&fill, r);
        g.DrawRectangle(&pen, r);
        break;
    }
    case Tool::Text: {
        GraphicsPath letters;
        glyphPath(letters, i.text, s.size);
        RectF ink = inkBounds(letters, i);
        REAL padding = textPadding(s.size);
        RectF r(i.a.X, i.a.Y, ink.Width + 2 * padding, ink.Height + 2 * padding);
        if (s.filled) {
            GraphicsPath background;
            roundedRect(background, r, s.size * 0.22f + 3);
            g.FillPath(&fill, &background);
        }
        bool editing = validIndex(app.editIndex) && app.edit && &i == &app.items[app.editIndex];
        if (editing)
            inlineSelection(g, i, ink);
        Matrix move;
        move.Translate(i.a.X + padding - ink.X, i.a.Y + padding - ink.Y);
        letters.Transform(&move);
        g.FillPath(&foreground, &letters);
        break;
    }
    case Tool::Counter: {
        REAL r = counterRadius(i);
        GraphicsPath body;
        counterPath(body, i);
        SolidBrush circle(Color(s.filled ? s.fill : s.color));
        g.FillPath(&circle, &body);
        if (s.filled) {
            Pen outline(Color(s.color), std::max(1.f, s.size / 12));
            g.DrawPath(&outline, &body);
        }
        SolidBrush number(Color(contrastingText(s.filled ? s.fill : s.color)));
        GraphicsPath glyphs;
        glyphPath(glyphs, std::to_wstring(i.number), r * 1.18f, FontStyleBold);
        RectF bounds;
        glyphs.GetBounds(&bounds);
        Matrix centered;
        centered.Translate(i.a.X - bounds.X - bounds.Width / 2,
                           i.a.Y - bounds.Y - bounds.Height / 2);
        glyphs.Transform(&centered);
        g.FillPath(&number, &glyphs);
        break;
    }
    default:
        break;
    }
}
std::vector<PointF> handles(const Item& i) {
    if (i.tool == Tool::Arrow || i.tool == Tool::Line)
        return {i.a, i.b, curveMiddle(i)};
    if (i.tool == Tool::Brush) {
        if (i.stroke.size() < 2)
            return {i.a};
        RectF r = i.strokeBounds;
        return {
            {r.X, r.Y}, {r.GetRight(), r.Y}, {r.GetRight(), r.GetBottom()}, {r.X, r.GetBottom()}};
    }
    if (i.tool == Tool::Rectangle || i.tool == Tool::Redact) {
        RectF r = normalized(i.a, i.b);
        return {
            {r.X, r.Y}, {r.GetRight(), r.Y}, {r.GetRight(), r.GetBottom()}, {r.X, r.GetBottom()}};
    }
    if (i.tool == Tool::Text) {
        RectF r = textRect(i);
        return {{r.X, r.Y}, {r.GetRight(), r.GetBottom()}};
    }
    REAL r = counterRadius(i);
    return {i.a, i.pointer ? counterTip(i) : PointF(i.a.X + r, i.a.Y)};
}
REAL segmentDistance(PointF p, PointF a, PointF b) {
    PointF d = sub(b, a);
    REAL len = d.X * d.X + d.Y * d.Y;
    REAL t = len ? std::clamp(((p.X - a.X) * d.X + (p.Y - a.Y) * d.Y) / len, 0.f, 1.f) : 0;
    return distance(p, {a.X + t * d.X, a.Y + t * d.Y});
}
bool hitItem(const Item& i, PointF p, REAL tolerance) {
    if (i.tool == Tool::Redact)
        return contains(normalized(i.a, i.b), p);
    if (i.tool == Tool::Brush) {
        REAL radius = std::max(tolerance, i.style.size / 2);
        if (i.stroke.size() < 2)
            return distance(i.a, p) <= radius;
        for (size_t k = 1; k < i.stroke.size(); k++)
            if (segmentDistance(p, i.stroke[k - 1], i.stroke[k]) <= radius)
                return true;
        return false;
    }
    if (i.tool == Tool::Text)
        return contains(textRect(i), p);
    if (i.tool == Tool::Counter) {
        GraphicsPath body;
        counterPath(body, i);
        Pen edge(Color(255, 0, 0, 0), tolerance * 2);
        return body.IsVisible(p) || body.IsOutlineVisible(p, &edge);
    }
    if (i.tool == Tool::Rectangle) {
        RectF r = normalized(i.a, i.b);
        RectF outer(r.X - tolerance, r.Y - tolerance, r.Width + 2 * tolerance,
                    r.Height + 2 * tolerance);
        if (!contains(outer, p))
            return false;
        return i.style.filled ||
               std::min({std::abs(p.X - r.X), std::abs(p.X - r.GetRight()), std::abs(p.Y - r.Y),
                         std::abs(p.Y - r.GetBottom())}) <= tolerance;
    }
    if ((i.tool == Tool::Arrow || i.tool == Tool::Line) && i.bent) {
        PointF previous = i.a;
        for (int k = 1; k <= 48; k++) {
            PointF q = curvePoint(i, k / 48.f);
            if (segmentDistance(p, previous, q) <= tolerance)
                return true;
            previous = q;
        }
        return false;
    }
    return segmentDistance(p, i.a, i.b) <= std::max(tolerance, i.style.size * 2);
}
int hitTest(PointF p) {
    for (int i = static_cast<int>(app.items.size()) - 1; i >= 0; i--)
        if (hitItem(app.items[i], p, 7 / app.layout.zoom))
            return i;
    return -1;
}
struct EdgeColorCache {
    const Bitmap* source = nullptr;
    unsigned ready = 0, builds = 0;
    std::array<ARGB, 16> colors{};
    void reset() {
        source = nullptr;
        ready = 0;
    }
} edgeColors;
ARGB dominantEdgeColor(unsigned sides) {
    if (!app.image || !sides)
        return 0xffffffff;
    if (edgeColors.source != app.image.get()) {
        edgeColors.reset();
        edgeColors.source = app.image.get();
    }
    if (edgeColors.ready & (1u << sides))
        return edgeColors.colors[sides];
    int width = static_cast<int>(app.image->GetWidth()),
        height = static_cast<int>(app.image->GetHeight());
    BitmapData data{};
    Rect whole(0, 0, width, height);
    if (app.image->LockBits(&whole, ImageLockModeRead, PixelFormat32bppARGB, &data) != Ok)
        return app.canvas.color;
    std::unordered_map<ARGB, unsigned> counts;
    counts.reserve(std::min(65536, 20 * (width + height)));
    ARGB result = 0xffffffff;
    unsigned maximum = 0;
    auto sample = [&](const ARGB* row, int begin, int end) {
        for (int x = begin; x < end; x++) {
            ARGB value = row[x] | 0xff000000;
            unsigned count = ++counts[value];
            if (count > maximum) {
                maximum = count;
                result = value;
            }
        }
    };
    for (int y = 0; y < height; y++) {
        const ARGB* row = reinterpret_cast<const ARGB*>(static_cast<const BYTE*>(data.Scan0) +
                                                        static_cast<ptrdiff_t>(y) * data.Stride);
        if (((sides & 4) && y < 10) || ((sides & 8) && y >= height - 10))
            sample(row, 0, width);
        else {
            int left = (sides & 1) ? std::min(10, width) : 0;
            if (left)
                sample(row, 0, left);
            if (sides & 2)
                sample(row, std::max(left, width - 10), width);
        }
    }
    app.image->UnlockBits(&data);
    edgeColors.colors[sides] = result;
    edgeColors.ready |= 1u << sides;
    ++edgeColors.builds;
    return result;
}
RectF itemVisualBounds(const Item& item) {
    RectF bounds = normalized(item.a, item.b);
    REAL padding = item.style.size / 2 + 1;
    if (item.tool == Tool::Redact)
        padding = 0;
    auto include = [&](PointF p) {
        REAL left = std::min(bounds.X, p.X), top = std::min(bounds.Y, p.Y);
        bounds = {left, top, std::max(bounds.GetRight(), p.X) - left,
                  std::max(bounds.GetBottom(), p.Y) - top};
    };
    if (item.tool == Tool::Brush)
        bounds = item.strokeBounds;
    else if (item.tool == Tool::Text) {
        bounds = textRect(item);
        padding = 1;
    } else if (item.tool == Tool::Counter) {
        REAL radius = counterRadius(item);
        bounds = {item.a.X - radius, item.a.Y - radius, 2 * radius, 2 * radius};
        if (item.pointer)
            include(counterTip(item));
        padding = 1;
    } else if (item.tool == Tool::Arrow || item.tool == Tool::Line) {
        if (item.bent) {
            PointF control = curveControl(item);
            REAL dx = item.a.X - 2 * control.X + item.b.X, dy = item.a.Y - 2 * control.Y + item.b.Y;
            if (std::abs(dx) > 0.0001f) {
                REAL t = (item.a.X - control.X) / dx;
                if (t > 0 && t < 1)
                    include(curvePoint(item, t));
            }
            if (std::abs(dy) > 0.0001f) {
                REAL t = (item.a.Y - control.Y) / dy;
                if (t > 0 && t < 1)
                    include(curvePoint(item, t));
            }
        }
        if (item.tool == Tool::Arrow) {
            REAL total = 0;
            PointF previous = item.a;
            for (int k = 1; k <= 64; k++) {
                PointF p = curvePoint(item, k / 64.f);
                total += distance(previous, p);
                previous = p;
            }
            REAL head = std::min(std::max(10.f, item.style.size * 3.8f),
                                 total * (item.style.arrow == 1 ? 0.42f : 0.65f));
            PointF control = curveControl(item);
            auto includeHead = [&](PointF tip, PointF from) {
                REAL angle = std::atan2(tip.Y - from.Y, tip.X - from.X);
                for (REAL offset : {-0.45f, 0.45f})
                    include({tip.X - head * std::cos(angle + offset),
                             tip.Y - head * std::sin(angle + offset)});
            };
            includeHead(item.b, distance(control, item.b) > 0.1f ? control : item.a);
            if (item.style.arrow == 1)
                includeHead(item.a, distance(control, item.a) > 0.1f ? control : item.b);
        }
    }
    return {bounds.X - padding, bounds.Y - padding, bounds.Width + 2 * padding,
            bounds.Height + 2 * padding};
}
bool growCanvas(const Item& item) {
    if (!app.image)
        return false;
    if ((item.tool == Tool::Arrow || item.tool == Tool::Line || item.tool == Tool::Rectangle) &&
        distance(item.a, item.b) < 2)
        return false;
    RECT bounds = canvasBounds(), next = bounds;
    RectF ink = itemVisualBounds(item);
    LONG step = std::max(16L, static_cast<LONG>(std::ceil(24 / std::max(0.02f, app.layout.zoom))));
    if (ink.X < bounds.left)
        next.left -= static_cast<LONG>(std::ceil((bounds.left - ink.X) / step)) * step;
    if (ink.Y < bounds.top)
        next.top -= static_cast<LONG>(std::ceil((bounds.top - ink.Y) / step)) * step;
    if (ink.GetRight() > bounds.right)
        next.right += static_cast<LONG>(std::ceil((ink.GetRight() - bounds.right) / step)) * step;
    if (ink.GetBottom() > bounds.bottom)
        next.bottom +=
            static_cast<LONG>(std::ceil((ink.GetBottom() - bounds.bottom) / step)) * step;
    if (EqualRect(&next, &bounds))
        return false;
    long long width = static_cast<long long>(next.right) - next.left,
              height = static_cast<long long>(next.bottom) - next.top;
    if (width > 32768 || height > 32768 || width * height > 64 * 1024 * 1024)
        return false;
    if (!app.pinnedCanvas) {
        app.pinnedOrigin = {app.layout.imageX, app.layout.imageY};
        app.pinnedZoom = app.layout.zoom;
        app.pinnedCanvas = true;
    }
    unsigned sides = (next.left < 0 ? 1u : 0u) |
                     (next.right > static_cast<LONG>(app.image->GetWidth()) ? 2u : 0u) |
                     (next.top < 0 ? 4u : 0u) |
                     (next.bottom > static_cast<LONG>(app.image->GetHeight()) ? 8u : 0u);
    app.canvas.bounds = next;
    app.canvas.color = dominantEdgeColor(sides);
    invalidate();
    return true;
}
bool trimCanvasToItems() {
    if (!app.image)
        return false;
    RECT original{0, 0, static_cast<LONG>(app.image->GetWidth()),
                  static_cast<LONG>(app.image->GetHeight())};
    bool manual = !IsRectEmpty(&app.canvas.manualFrame);
    RECT previous = canvasBounds(), needed = manual ? app.canvas.manualFrame : original;
    for (size_t index = 0; index < app.items.size(); index++) {
        RectF ink = itemVisualBounds(app.items[index]);
        RECT visible{static_cast<LONG>(std::floor(ink.X)), static_cast<LONG>(std::floor(ink.Y)),
                     static_cast<LONG>(std::ceil(ink.GetRight())),
                     static_cast<LONG>(std::ceil(ink.GetBottom()))};
        if (manual && static_cast<int>(index) != app.selected) {
            RECT clipped{};
            const RECT& allowed =
                app.items[index].cropped ? app.canvas.manualFrame : app.gestureCanvas;
            if (!IntersectRect(&clipped, &visible, &allowed))
                continue;
            visible = clipped;
        }
        needed.left = std::min(needed.left, visible.left);
        needed.top = std::min(needed.top, visible.top);
        needed.right = std::max(needed.right, visible.right);
        needed.bottom = std::max(needed.bottom, visible.bottom);
    }
    // Only remove temporary margins. Keep the chosen frame and visible
    // annotation bounds, including strokes and arrowheads.
    RECT next{};
    IntersectRect(&next, &previous, &needed);
    if (EqualRect(&previous, &next))
        return false;
    unsigned sides = (next.left < 0 ? 1u : 0u) | (next.right > original.right ? 2u : 0u) |
                     (next.top < 0 ? 4u : 0u) | (next.bottom > original.bottom ? 8u : 0u);
    app.canvas.bounds = manual || sides ? next : RECT{};
    app.canvas.color = sides ? dominantEdgeColor(sides) : 0xffffffff;
    invalidate();
    return true;
}
Layout calculateLayout(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    Layout l;
    l.width = static_cast<REAL>(rc.right);
    l.height = static_cast<REAL>(rc.bottom);
    l.scale =
        std::max(0.1f, std::min({GetDpiForWindow(hwnd) / 96.f, l.width / 760.f, l.height / 500.f}));
    l.side = std::max(0.f, l.width - 220 * l.scale);
    l.canvas = {0, 56 * l.scale, l.side, std::max(1.f, l.height - 86 * l.scale)};
    if (app.image) {
        RECT bounds = canvasBounds();
        REAL width = static_cast<REAL>(bounds.right - bounds.left),
             height = static_cast<REAL>(bounds.bottom - bounds.top);
        REAL fit = std::min((l.canvas.Width - 40 * l.scale) / width,
                            (l.canvas.Height - 40 * l.scale) / height);
        l.zoom = app.userZoom > 0 ? app.userZoom : std::min(1.f, std::max(0.02f, fit));
        l.imageX =
            l.canvas.X + (l.canvas.Width - width * l.zoom) / 2 - bounds.left * l.zoom + app.pan.X;
        l.imageY =
            l.canvas.Y + (l.canvas.Height - height * l.zoom) / 2 - bounds.top * l.zoom + app.pan.Y;
        if (app.pinnedCanvas) {
            l.zoom = app.pinnedZoom;
            l.imageX = app.pinnedOrigin.X;
            l.imageY = app.pinnedOrigin.Y;
        }
    }
    return l;
}
PointF boundedPan(PointF pan, const Layout& view, RECT frame) {
    // A small frame stays wholly inside the viewport. A larger one can scroll
    // through all its content, stopping when the opposite edge reaches the viewport.
    REAL x = std::abs(view.canvas.Width - (frame.right - frame.left) * view.zoom) / 2,
         y = std::abs(view.canvas.Height - (frame.bottom - frame.top) * view.zoom) / 2;
    return {std::clamp(pan.X, -x, x), std::clamp(pan.Y, -y, y)};
}
void constrainPan() {
    if (app.image && !app.pinnedCanvas)
        app.pan = boundedPan(app.pan, calculateLayout(app.main), canvasBounds());
}
void presentNavigation() {
    if (!app.main)
        return;
    constrainPan();
    Layout layout = calculateLayout(app.main);
    RECT dirty{static_cast<LONG>(std::floor(layout.canvas.X)),
               static_cast<LONG>(std::floor(layout.canvas.Y)),
               static_cast<LONG>(std::ceil(layout.canvas.GetRight())),
               static_cast<LONG>(std::ceil(layout.canvas.GetBottom()))};
    InvalidateRect(app.main, &dirty, FALSE);
    // Mouse moves are coalesced by Windows. Present the latest position instead
    // of waiting behind a continuous input stream for a low-priority WM_PAINT.
    if (!app.test && IsWindowVisible(app.main))
        UpdateWindow(app.main);
}
void centerExpandedCanvas() {
    if (!app.pinnedCanvas)
        return;
    app.pinnedCanvas = false;
    app.userZoom = 0;
    app.pan = {0, 0};
    app.layout = calculateLayout(app.main);
}
PointF toImage(PointF p, bool clamp = true) {
    PointF v = {(p.X - app.layout.imageX) / app.layout.zoom,
                (p.Y - app.layout.imageY) / app.layout.zoom};
    if (clamp && app.image) {
        RECT bounds = canvasBounds();
        v.X = std::clamp(v.X, static_cast<REAL>(bounds.left), static_cast<REAL>(bounds.right));
        v.Y = std::clamp(v.Y, static_cast<REAL>(bounds.top), static_cast<REAL>(bounds.bottom));
    }
    return v;
}
PointF toScreen(PointF p) {
    return {app.layout.imageX + p.X * app.layout.zoom, app.layout.imageY + p.Y * app.layout.zoom};
}
bool onImage(PointF p) {
    RECT bounds = canvasBounds();
    return app.image && contains(app.layout.canvas, p) &&
           contains({app.layout.imageX + bounds.left * app.layout.zoom,
                     app.layout.imageY + bounds.top * app.layout.zoom,
                     (bounds.right - bounds.left) * app.layout.zoom,
                     (bounds.bottom - bounds.top) * app.layout.zoom},
                    p);
}
std::array<PointF, 4> canvasCorners(RECT bounds) {
    return {PointF(static_cast<REAL>(bounds.left), static_cast<REAL>(bounds.top)),
            PointF(static_cast<REAL>(bounds.right), static_cast<REAL>(bounds.top)),
            PointF(static_cast<REAL>(bounds.right), static_cast<REAL>(bounds.bottom)),
            PointF(static_cast<REAL>(bounds.left), static_cast<REAL>(bounds.bottom))};
}
int canvasHandleAt(PointF screen) {
    if (!app.image || !contains(app.layout.canvas, screen))
        return -1;
    REAL radius = 9 * app.layout.scale, best = radius * 1.5f;
    int result = -1;
    auto corners = canvasCorners(canvasBounds());
    for (int i = 0; i < 4; i++) {
        PointF p = toScreen(corners[i]);
        REAL d = distance(p, screen);
        if (std::abs(p.X - screen.X) <= radius && std::abs(p.Y - screen.Y) <= radius && d < best) {
            best = d;
            result = i;
        }
    }
    return result;
}
void hoverCanvasHandle(int handle) {
    if (app.hoverCanvas == handle)
        return;
    app.hoverCanvas = handle;
    invalidate();
}
void resizeCanvas(PointF screen) {
    PointF moving = sub(toImage(screen, false), app.canvasGrab);
    PointF fixed = canvasCorners(app.resizeStart)[(app.canvasHandle + 2) % 4];
    bool left = app.canvasHandle == 0 || app.canvasHandle == 3,
         top = app.canvasHandle == 0 || app.canvasHandle == 1;
    LONG x = static_cast<LONG>(std::round(moving.X)), y = static_cast<LONG>(std::round(moving.Y));
    RECT next{left ? std::min(x, static_cast<LONG>(fixed.X) - 2) : static_cast<LONG>(fixed.X),
              top ? std::min(y, static_cast<LONG>(fixed.Y) - 2) : static_cast<LONG>(fixed.Y),
              left ? static_cast<LONG>(fixed.X) : std::max(x, static_cast<LONG>(fixed.X) + 2),
              top ? static_cast<LONG>(fixed.Y) : std::max(y, static_cast<LONG>(fixed.Y) + 2)};
    long long width = static_cast<long long>(next.right) - next.left,
              height = static_cast<long long>(next.bottom) - next.top;
    if (width > 32768 || height > 32768 || width * height > 64 * 1024 * 1024)
        return;
    app.canvas.bounds = next;
    app.canvas.manualFrame = next;
    unsigned sides = (next.left < 0 ? 1u : 0u) |
                     (next.right > static_cast<LONG>(app.image->GetWidth()) ? 2u : 0u) |
                     (next.top < 0 ? 4u : 0u) |
                     (next.bottom > static_cast<LONG>(app.image->GetHeight()) ? 8u : 0u);
    app.canvas.color = sides ? dominantEdgeColor(sides) : 0xffffffff;
    invalidate();
    if (!app.test && IsWindowVisible(app.main))
        UpdateWindow(app.main);
}
// All toolbar glyphs use the same 24-unit vector grid and rounded 2-unit
// stroke. They do not depend on fonts, bitmap assets or external libraries.
GraphicsState iconTransform(Graphics& g, RectF r) {
    GraphicsState saved = g.Save();
    g.TranslateTransform(r.X, r.Y);
    g.ScaleTransform(r.Width / 24, r.Height / 24);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeDefault);
    return saved;
}
void iconPen(Pen& pen) {
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);
}
void toolIcon(Graphics& g, Tool tool, RectF r, Color color) {
    GraphicsState saved = iconTransform(g, r);
    Pen pen(color, 2);
    iconPen(pen);
    if (tool == Tool::Select) {
        // Exactly symmetric about the diagonal through its tip and tail.
        PointF outline[] = {{3, 3},         {8, 20},        {11.5f, 14.5f}, {17.5f, 20.5f},
                            {20.5f, 17.5f}, {14.5f, 11.5f}, {20, 8}};
        g.DrawPolygon(&pen, outline, 7);
    } else if (tool == Tool::Arrow) {
        g.DrawLine(&pen, 4.f, 20.f, 20.f, 4.f);
        PointF head[] = {{12, 4}, {20, 4}, {20, 12}};
        g.DrawLines(&pen, head, 3);
    } else if (tool == Tool::Text) {
        g.DrawLine(&pen, 4.f, 5.f, 20.f, 5.f);
        g.DrawLine(&pen, 12.f, 5.f, 12.f, 20.f);
    } else if (tool == Tool::Rectangle) {
        GraphicsPath shape;
        roundedRect(shape, {3, 5, 18, 14}, 2);
        g.DrawPath(&pen, &shape);
    } else if (tool == Tool::Line) {
        g.DrawLine(&pen, 4.f, 20.f, 20.f, 4.f);
    } else if (tool == Tool::Counter) {
        g.DrawEllipse(&pen, 3.f, 3.f, 18.f, 18.f);
        PointF digit[] = {{10, 9}, {12, 7}, {12, 17}};
        g.DrawLines(&pen, digit, 3);
        g.DrawLine(&pen, 9.f, 17.f, 15.f, 17.f);
    } else if (tool == Tool::TextSelect) {
        PointF left[] = {{7, 3}, {3, 3}, {3, 8}}, right[] = {{17, 21}, {21, 21}, {21, 16}};
        g.DrawLines(&pen, left, 3);
        g.DrawLines(&pen, right, 3);
        g.DrawLine(&pen, 8.f, 7.f, 16.f, 7.f);
        g.DrawLine(&pen, 12.f, 7.f, 12.f, 17.f);
        g.DrawLine(&pen, 9.f, 17.f, 15.f, 17.f);
    } else if (tool == Tool::Redact) {
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column) {
                REAL width = 3.f + (row + column) % 2;
                SolidBrush tile(color);
                g.FillRectangle(&tile, 3.f + column * 7, 3.f + row * 7, width, width);
            }
    } else if (tool == Tool::Brush) {
        GraphicsPath handle;
        handle.AddLine(11.f, 11.f, 18.f, 4.f);
        handle.AddBezier(18.f, 4.f, 20.f, 2.f, 23.f, 5.f, 21.f, 7.f);
        handle.AddLine(21.f, 7.f, 14.f, 14.f);
        handle.CloseFigure();
        g.DrawPath(&pen, &handle);
        GraphicsPath bristles;
        bristles.AddBezier(11.f, 11.f, 8.f, 11.f, 7.f, 13.f, 7.f, 16.f);
        bristles.AddBezier(7.f, 16.f, 7.f, 18.f, 5.f, 19.f, 3.f, 19.f);
        bristles.AddBezier(3.f, 19.f, 7.f, 22.f, 13.f, 21.f, 15.f, 17.f);
        bristles.AddBezier(15.f, 17.f, 16.f, 15.f, 15.f, 15.f, 14.f, 14.f);
        g.DrawPath(&pen, &bristles);
    } else if (tool == Tool::QrScan) {
        // Three finder patterns plus a few data modules.
        SolidBrush tile(color);
        for (PointF origin : {PointF{3, 3}, PointF{14, 3}, PointF{3, 14}}) {
            g.DrawRectangle(&pen, origin.X + 1, origin.Y + 1, 5.f, 5.f);
            g.FillRectangle(&tile, origin.X + 3, origin.Y + 3, 1.f, 1.f);
        }
        for (PointF cell : {PointF{14, 14}, PointF{18, 14}, PointF{16, 16}, PointF{14, 18},
                            PointF{18, 18}, PointF{20, 16}, PointF{16, 20}, PointF{20, 20}})
            g.FillRectangle(&tile, cell.X, cell.Y, 2.f, 2.f);
    }
    g.Restore(saved);
}
void button(Graphics& g, RectF r, int id, const wchar_t* label, bool selected = false,
            bool primary = false, bool enabled = true) {
    app.buttons.push_back({r, id, enabled});
    bool hovered = enabled && app.hoverButton == id, pressed = hovered && app.pressedButton == id;
    Color background =
        primary
            ? (pressed ? Color(255, 0, 77, 176) : (hovered ? Color(255, 0, 94, 211) : Blue))
            : (pressed ? Color(255, 190, 216, 249)
                       : (selected
                              ? Color(255, 220, 235, 255)
                              : (hovered ? Color(255, 235, 241, 249) : Color(255, 255, 255, 255))));
    if (!enabled)
        background = Color(255, 245, 247, 250);
    SolidBrush bg(background);
    Color border = selected || hovered ? Blue : Color(255, 217, 223, 232);
    if (id >= 100 && id < 100 + ToolCount && !selected && !hovered)
        border = Color(0, 0, 0, 0);
    Pen outline(border, (selected || pressed ? 1.5f : 1.f) * app.layout.scale);
    GraphicsPath shape;
    roundedRect(shape, r, 4 * app.layout.scale);
    g.FillPath(&bg, &shape);
    g.DrawPath(&outline, &shape);
    drawText(g, label, r, 12 * app.layout.scale,
             enabled ? (primary ? Color(255, 255, 255, 255) : Ink) : Muted, false, true);
}
const wchar_t* tooltipFor(int id) {
    if (id >= 260 && id < 264)
        return ui::kRedactionModes[id - 260];
    if (id >= 100 && id < 100 + ToolCount)
        return ui::kTooltips[id - 100];
    switch (id) {
    case 2:
        return ui::kUndo;
    case 3:
        return ui::kRedo;
    case 4:
        return ui::kCopy;
    case 5:
        return ui::kSave;
    case 200:
        return ui::kOneHead;
    case 201:
        return ui::kTwoHeads;
    case 210:
        return ui::kColor;
    case 211:
        return ui::kBackgroundTip;
    case 213:
        return ui::kMoreColors;
    case 230:
        return ui::kDecreaseNumber;
    case 231:
        return ui::kIncreaseNumber;
    case 232:
        return ui::kRemovePointer;
    case 240:
        return ui::kDelete;
    case 254:
        return ui::kHelpTip;
    default:
        return nullptr;
    }
}
void initTooltips() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES | ICC_LINK_CLASS};
    InitCommonControlsEx(&controls);
    app.tooltip =
        CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
                        CW_USEDEFAULT, CW_USEDEFAULT, app.main, nullptr, app.instance, nullptr);
    if (app.tooltip) {
        SendMessageW(app.tooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 350);
        SendMessageW(app.tooltip, TTM_SETDELAYTIME, TTDT_RESHOW, 80);
        SendMessageW(app.tooltip, TTM_SETMAXTIPWIDTH, 0, 320);
    }
}
void syncTooltips() {
    if (!app.tooltip)
        return;
    std::array<bool, 512> visible{};
    for (const Button& button : app.buttons) {
        const wchar_t* label = tooltipFor(button.id);
        if (!label)
            continue;
        visible[button.id] = true;
        TOOLINFOW tool{};
        tool.cbSize = sizeof(tool);
        tool.hwnd = app.main;
        tool.uId = button.id;
        tool.uFlags = TTF_SUBCLASS;
        tool.rect = {static_cast<LONG>(std::floor(button.rect.X)),
                     static_cast<LONG>(std::floor(button.rect.Y)),
                     static_cast<LONG>(std::ceil(button.rect.GetRight())),
                     static_cast<LONG>(std::ceil(button.rect.GetBottom()))};
        tool.lpszText = const_cast<wchar_t*>(label);
        if (app.tooltipAdded[button.id])
            SendMessageW(app.tooltip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
        else
            app.tooltipAdded[button.id] = SendMessageW(app.tooltip, TTM_ADDTOOLW, 0,
                                                       reinterpret_cast<LPARAM>(&tool)) != FALSE;
    }
    for (size_t id = 0; id < visible.size(); id++)
        if (app.tooltipAdded[id] && !visible[id]) {
            TOOLINFOW tool{};
            tool.cbSize = sizeof(tool);
            tool.hwnd = app.main;
            tool.uId = id;
            SendMessageW(app.tooltip, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
            app.tooltipAdded[id] = false;
        }
}
int buttonAt(PointF point, bool enabledOnly = false) {
    for (const Button& button : app.buttons)
        if (contains(button.rect, point) && (!enabledOnly || button.enabled))
            return button.id;
    return -1;
}
void invalidateButton(int id) {
    if (id < 0)
        return;
    ++sceneRevision;
    for (const Button& button : app.buttons)
        if (button.id == id) {
            RECT rect{static_cast<LONG>(std::floor(button.rect.X - 2)),
                      static_cast<LONG>(std::floor(button.rect.Y - 2)),
                      static_cast<LONG>(std::ceil(button.rect.GetRight() + 2)),
                      static_cast<LONG>(std::ceil(button.rect.GetBottom() + 2))};
            InvalidateRect(app.main, &rect, FALSE);
            break;
        }
}
void setHoverButton(int id) {
    if (app.hoverButton == id)
        return;
    int old = app.hoverButton;
    app.hoverButton = id;
    invalidateButton(old);
    invalidateButton(id);
}
void historyIcon(Graphics& g, RectF r, bool redo, Color color) {
    GraphicsState saved = iconTransform(g, r);
    if (redo) {
        g.TranslateTransform(24, 0);
        g.ScaleTransform(-1, 1);
    }
    Pen pen(color, 2);
    iconPen(pen);
    GraphicsPath shaft;
    shaft.AddLine(4.f, 8.f, 12.f, 8.f);
    shaft.AddBezier(12.f, 8.f, 17.f, 8.f, 21.f, 11.f, 21.f, 16.f);
    shaft.AddBezier(21.f, 16.f, 21.f, 17.5f, 20.5f, 19.f, 19.5f, 20.f);
    g.DrawPath(&pen, &shaft);
    // A horizontal tangent at the tip bisects the two 45-degree head sides.
    PointF head[] = {{8, 4}, {4, 8}, {8, 12}};
    g.DrawLines(&pen, head, 3);
    g.Restore(saved);
}
void actionIcon(Graphics& g, RectF r, int id, Color color) {
    GraphicsState saved = iconTransform(g, r);
    Pen pen(color, 2);
    iconPen(pen);
    if (id == 240) {
        g.DrawLine(&pen, 4.f, 6.f, 20.f, 6.f);
        g.DrawLine(&pen, 9.f, 3.f, 15.f, 3.f);
        g.DrawLine(&pen, 9.f, 3.f, 9.f, 6.f);
        g.DrawLine(&pen, 15.f, 3.f, 15.f, 6.f);
        GraphicsPath body;
        body.AddLine(6.f, 9.f, 7.f, 19.f);
        body.AddBezier(7.f, 19.f, 7.f, 20.5f, 7.5f, 21.f, 9.f, 21.f);
        body.AddLine(9.f, 21.f, 15.f, 21.f);
        body.AddBezier(15.f, 21.f, 16.5f, 21.f, 17.f, 20.5f, 17.f, 19.f);
        body.AddLine(17.f, 19.f, 18.f, 9.f);
        g.DrawPath(&pen, &body);
        g.DrawLine(&pen, 10.f, 11.f, 10.5f, 17.f);
        g.DrawLine(&pen, 14.f, 11.f, 13.5f, 17.f);
    } else if (id == 4) {
        GraphicsPath body;
        body.AddLine(8.f, 5.f, 7.f, 5.f);
        body.AddBezier(7.f, 5.f, 5.5f, 5.f, 5.f, 5.5f, 5.f, 7.f);
        body.AddLine(5.f, 7.f, 5.f, 19.f);
        body.AddBezier(5.f, 19.f, 5.f, 20.5f, 5.5f, 21.f, 7.f, 21.f);
        body.AddLine(7.f, 21.f, 17.f, 21.f);
        body.AddBezier(17.f, 21.f, 18.5f, 21.f, 19.f, 20.5f, 19.f, 19.f);
        body.AddLine(19.f, 19.f, 19.f, 7.f);
        body.AddBezier(19.f, 7.f, 19.f, 5.5f, 18.5f, 5.f, 17.f, 5.f);
        body.AddLine(17.f, 5.f, 16.f, 5.f);
        g.DrawPath(&pen, &body);
        GraphicsPath clip;
        roundedRect(clip, {8, 3, 8, 4}, 1);
        g.DrawPath(&pen, &clip);
        g.DrawLine(&pen, 9.f, 11.f, 15.f, 11.f);
        g.DrawLine(&pen, 9.f, 15.f, 15.f, 15.f);
    } else if (id == 5) {
        PointF body[] = {{4, 4}, {17, 4}, {20, 7}, {20, 20}, {4, 20}};
        g.DrawPolygon(&pen, body, 5);
        PointF slot[] = {{8, 4}, {8, 10}, {16, 10}, {16, 4}};
        g.DrawLines(&pen, slot, 4);
        g.DrawLine(&pen, 13.f, 6.f, 13.f, 8.f);
        PointF label[] = {{7, 20}, {7, 14}, {17, 14}, {17, 20}};
        g.DrawLines(&pen, label, 4);
    }
    g.Restore(saved);
}
void checker(Graphics& g, RectF r) {
    SolidBrush white(Color(255, 255, 255, 255)), gray(Color(255, 201, 208, 217));
    g.FillRectangle(&white, r);
    for (REAL y = r.Y; y < r.GetBottom(); y += 5)
        for (REAL x = r.X; x < r.GetRight(); x += 5)
            if ((static_cast<int>((x - r.X) / 5) + static_cast<int>((y - r.Y) / 5)) % 2 == 0)
                g.FillRectangle(&gray, x, y, std::min(5.f, r.GetRight() - x),
                                std::min(5.f, r.GetBottom() - y));
}
InterpolationMode captureInterpolation(const Layout& l) {
    return l.zoom >= 1
               ? InterpolationModeNearestNeighbor
               : (app.zooming ? InterpolationModeBilinear : InterpolationModeHighQualityBicubic);
}

bool drawZoomPreview(Graphics& g, const Layout& l) {
    // The immutable capture is converted to a native raster once, before any
    // interaction. Native stretching avoids GDI+'s full-source conversion on
    // every wheel event. The high-quality view is restored after wheel idle.
    Region clip;
    g.GetClip(&clip);
    HRGN region = clip.GetHRGN(&g);
    HDC dc = g.GetHDC();
    if (!dc) {
        if (region)
            DeleteObject(region);
        return false;
    }
    bool drawn = false;
    if (region && sourceRaster.ensure(app.image.get())) {
        int state = SaveDC(dc);
        ExtSelectClipRgn(dc, region, RGN_AND);
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, nullptr);
        int lines = StretchDIBits(
            dc, static_cast<int>(std::floor(l.imageX)), static_cast<int>(std::floor(l.imageY)),
            std::max(1, static_cast<int>(std::round(sourceRaster.width * l.zoom))),
            std::max(1, static_cast<int>(std::round(sourceRaster.height * l.zoom))), 0, 0,
            sourceRaster.width, sourceRaster.height, sourceRaster.pixels.get(), &sourceRaster.info,
            DIB_RGB_COLORS, SRCCOPY);
        drawn = lines != 0 && lines != GDI_ERROR;
        RestoreDC(dc, state);
    }
    if (region)
        DeleteObject(region);
    g.ReleaseHDC(dc);
    return drawn;
}

bool prepareCaptureView(Graphics& target, const Layout& l, int originX, int originY) {
    const PointF phase{l.imageX - originX, l.imageY - originY};
    const InterpolationMode interpolation = captureInterpolation(l);
    // Cache coordinates are relative to the image's integer screen origin, so
    // a pan translates the raster instead of invalidating its expensive scaling.
    RECT full{-2, -2, static_cast<LONG>(std::ceil(app.image->GetWidth() * l.zoom)) + 3,
              static_cast<LONG>(std::ceil(app.image->GetHeight() * l.zoom)) + 3};
    RECT viewport{static_cast<LONG>(std::floor(l.canvas.X)) - originX,
                  static_cast<LONG>(std::floor(l.canvas.Y)) - originY,
                  static_cast<LONG>(std::ceil(l.canvas.GetRight())) - originX,
                  static_cast<LONG>(std::ceil(l.canvas.GetBottom())) - originY},
        needed{};
    if (!IntersectRect(&needed, &viewport, &full))
        return false;
    const RECT& previous = captureView.extent;
    if (captureView.native && captureView.source == app.image.get() &&
        captureView.layout.zoom == l.zoom && captureView.phase.X == phase.X &&
        captureView.phase.Y == phase.Y && captureView.interpolation == interpolation &&
        previous.left <= needed.left && previous.top <= needed.top &&
        previous.right >= needed.right && previous.bottom >= needed.bottom)
        return true;

    RECT extent = needed;
    if (!app.zooming) {
        // Small scaled images are retained in full. At large zoom, keep a
        // bounded viewport tile with overscan, never a desktop-sized 8x bitmap.
        long long fullPixels =
            static_cast<long long>(full.right - full.left) * (full.bottom - full.top);
        if (fullPixels <= 4 * 1024 * 1024)
            extent = full;
        else {
            InflateRect(&extent, 384, 384);
            IntersectRect(&extent, &extent, &full);
        }
    }
    int width = extent.right - extent.left, height = extent.bottom - extent.top;
    if (static_cast<long long>(width) * height > 16 * 1024 * 1024)
        return false;
    captureView.reset();
    captureView.bitmap = std::make_unique<Bitmap>(width, height, PixelFormat32bppPARGB);
    if (captureView.bitmap->GetLastStatus() != Ok) {
        captureView.reset();
        return false;
    }
    {
        Graphics view(captureView.bitmap.get());
        view.Clear(Color(0, 0, 0, 0));
        view.TranslateTransform(phase.X - extent.left, phase.Y - extent.top);
        view.SetInterpolationMode(interpolation);
        view.DrawImage(app.image.get(),
                       RectF(0, 0, app.image->GetWidth() * l.zoom, app.image->GetHeight() * l.zoom),
                       0, 0, static_cast<REAL>(app.image->GetWidth()),
                       static_cast<REAL>(app.image->GetHeight()), UnitPixel);
    }
    captureView.native = std::make_unique<CachedBitmap>(captureView.bitmap.get(), &target);
    if (captureView.native->GetLastStatus() != Ok) {
        captureView.reset();
        return false;
    }
    captureView.source = app.image.get();
    captureView.layout = l;
    captureView.extent = extent;
    captureView.phase = phase;
    captureView.interpolation = interpolation;
    ++captureView.builds;
    return true;
}

void drawCapture(Graphics& g, const Layout& l, bool cached) {
    GraphicsState saved = g.Save();
    g.SetClip(l.canvas, CombineModeIntersect);
    RECT bounds = canvasBounds();
    RectF canvas(l.imageX + bounds.left * l.zoom, l.imageY + bounds.top * l.zoom,
                 (bounds.right - bounds.left) * l.zoom, (bounds.bottom - bounds.top) * l.zoom);
    SolidBrush shadow(Color(35, 0, 0, 0));
    g.FillRectangle(&shadow, canvas.X + 3 * l.scale, canvas.Y + 4 * l.scale, canvas.Width,
                    canvas.Height);
    SolidBrush background(Color(app.canvas.color));
    g.FillRectangle(&background, canvas);
    g.SetClip(canvas, CombineModeIntersect);
    if (app.zooming && drawZoomPreview(g, l)) {
        g.Restore(saved);
        return;
    }
    if (cached) {
        int originX = static_cast<int>(std::floor(l.imageX)),
            originY = static_cast<int>(std::floor(l.imageY));
        if (prepareCaptureView(g, l, originX, originY) &&
            g.DrawCachedBitmap(captureView.native.get(), originX + captureView.extent.left,
                               originY + captureView.extent.top) == Ok) {
            g.Restore(saved);
            return;
        }
    }
    g.SetInterpolationMode(captureInterpolation(l));
    g.DrawImage(
        app.image.get(),
        RectF(l.imageX, l.imageY, app.image->GetWidth() * l.zoom, app.image->GetHeight() * l.zoom),
        0, 0, static_cast<REAL>(app.image->GetWidth()), static_cast<REAL>(app.image->GetHeight()),
        UnitPixel);
    g.Restore(saved);
}
void drawAnnotations(Graphics& g, const Layout& l, int first, int last) {
    GraphicsState state = g.Save();
    g.SetClip(l.canvas, CombineModeIntersect);
    g.TranslateTransform(l.imageX, l.imageY);
    g.ScaleTransform(l.zoom, l.zoom);
    RECT bounds = canvasBounds();
    g.SetClip(RectF(static_cast<REAL>(bounds.left), static_cast<REAL>(bounds.top),
                    static_cast<REAL>(bounds.right - bounds.left),
                    static_cast<REAL>(bounds.bottom - bounds.top)),
              CombineModeIntersect);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    for (int i = first; i < last; i++)
        drawItem(g, app.items[i], true);
    g.Restore(state);
}
void drawSelection(Graphics& g, const Layout& l) {
    if (!validIndex(app.selected) || app.editIndex >= 0)
        return;
    GraphicsState saved = g.Save();
    g.SetClip(l.canvas, CombineModeIntersect);
    RECT frame = canvasBounds();
    g.SetClip(RectF(l.imageX + frame.left * l.zoom, l.imageY + frame.top * l.zoom,
                    (frame.right - frame.left) * l.zoom, (frame.bottom - frame.top) * l.zoom),
              CombineModeIntersect);
    const Item& item = app.items[app.selected];
    REAL s = l.scale;
    Pen dotted(Blue, s);
    dotted.SetDashStyle(DashStyleDash);
    if (item.tool == Tool::Text || item.tool == Tool::Rectangle || item.tool == Tool::Brush ||
        item.tool == Tool::Redact) {
        RectF r = item.tool == Tool::Text
                      ? textRect(item)
                      : (item.tool == Tool::Brush ? item.strokeBounds : normalized(item.a, item.b));
        PointF a = toScreen({r.X, r.Y});
        g.DrawRectangle(&dotted, a.X - 3 * s, a.Y - 3 * s, r.Width * l.zoom + 6 * s,
                        r.Height * l.zoom + 6 * s);
    }
    SolidBrush hb(Color(255, 255, 255, 255));
    Pen hp(Blue, 1.8f * s);
    auto points = handles(item);
    for (size_t i = 0; i < points.size(); i++) {
        PointF q = toScreen(points[i]);
        if ((item.tool == Tool::Arrow || item.tool == Tool::Line) && i == 2) {
            PointF diamond[] = {
                {q.X, q.Y - 7 * s}, {q.X + 7 * s, q.Y}, {q.X, q.Y + 7 * s}, {q.X - 7 * s, q.Y}};
            g.FillPolygon(&hb, diamond, 4);
            g.DrawPolygon(&hp, diamond, 4);
        } else {
            g.FillEllipse(&hb, q.X - 5 * s, q.Y - 5 * s, 10 * s, 10 * s);
            g.DrawEllipse(&hp, q.X - 5 * s, q.Y - 5 * s, 10 * s, 10 * s);
        }
    }
    g.Restore(saved);
}
void drawCanvasHandles(Graphics& g, const Layout& l) {
    if (!app.image)
        return;
    GraphicsState saved = g.Save();
    g.SetClip(l.canvas, CombineModeIntersect);
    REAL s = l.scale;
    auto corners = canvasCorners(canvasBounds());
    if (app.resizingCanvas || app.hoverCanvas >= 0) {
        PointF first = toScreen(corners[0]), last = toScreen(corners[2]);
        Pen border(Color(150, 0, 110, 235), s);
        g.DrawRectangle(&border, first.X, first.Y, last.X - first.X, last.Y - first.Y);
    }
    for (int i = 0; i < 4; i++) {
        PointF p = toScreen(corners[i]);
        bool active = app.resizingCanvas && app.canvasHandle == i;
        SolidBrush fill(active ? Blue
                               : (app.hoverCanvas == i ? Color(255, 220, 235, 255)
                                                       : Color(255, 255, 255, 255)));
        Pen outline(Blue, 1.5f * s);
        g.FillRectangle(&fill, p.X - 5 * s, p.Y - 5 * s, 10 * s, 10 * s);
        g.DrawRectangle(&outline, p.X - 5 * s, p.Y - 5 * s, 10 * s, 10 * s);
    }
    g.Restore(saved);
}
void drawOcrOverlay(Graphics& g, const Layout& l) {
    if (app.tool != Tool::TextSelect || !ocrReady())
        return;
    GraphicsState saved = g.Save();
    g.SetClip(l.canvas);
    g.TranslateTransform(l.imageX, l.imageY);
    g.ScaleTransform(l.zoom, l.zoom);
    RECT frame = canvasBounds();
    g.SetClip(RectF(float(frame.left), float(frame.top), float(frame.right - frame.left),
                    float(frame.bottom - frame.top)),
              CombineModeIntersect);
    SolidBrush found(Color(45, 0, 110, 235)), picked(Color(105, 0, 110, 235));
    Pen edge(Color(160, 0, 110, 235), 1 / l.zoom);
    for (const auto& line : ocrReport()->lines)
        for (const auto& word : line.words) {
            if (hiddenOcrWord(word))
                continue;
            bool selected = app.hasTextSelection && captura::wordInside(word, app.textArea);
            g.FillRectangle(selected ? &picked : &found, word.bounds);
            g.DrawRectangle(&edge, word.bounds);
        }
    if (app.hasTextSelection) {
        SolidBrush area(Color(20, 0, 110, 235));
        Pen border(Blue, 1.5f / l.zoom);
        border.SetDashStyle(DashStyleDash);
        g.FillRectangle(&area, app.textArea);
        g.DrawRectangle(&border, app.textArea);
    }
    g.Restore(saved);
}
void drawOcrPanel(Graphics& g, REAL x, REAL y, REAL width, REAL scale) {
    auto paragraph = [&](const std::wstring& text, RectF rect, REAL size, Color color) {
        Font font(L"Segoe UI", size, FontStyleRegular, UnitPixel);
        SolidBrush ink(color);
        StringFormat format;
        format.SetTrimming(StringTrimmingEllipsisWord);
        g.DrawString(text.c_str(), -1, &font, rect, &format, &ink);
    };
    drawText(g, L"Select text · preview", {x, y, width, 26 * scale}, 14 * scale, Ink, true);
    y += 38 * scale;
    const auto* report = ocrReport();
    const wchar_t* status = !app.image ? L"Capture an image to begin."
                            : !report  ? L"Recognizing text…"
                            : report->state == captura::OcrState::Ready ? L"Text is ready"
                            : report->state == captura::OcrState::Empty ? L"No text found"
                            : report->error.empty()                     ? L"Recognition cancelled"
                                                                        : report->error.c_str();
    paragraph(status, {x, y, width, 36 * scale}, 12 * scale, Blue);
    y += 40 * scale;
    paragraph(L"Drag over highlighted words to copy them. The editor stays open.",
              {x, y, width, 56 * scale}, 12 * scale, Muted);
    y += 66 * scale;
    if (!app.textFeedback.empty()) {
        paragraph(app.textFeedback, {x, y, width, 36 * scale}, 12 * scale, Blue);
        y += 46 * scale;
    }
    if (report) {
        paragraph(captura::metricsText(*report), {x, y, width, 150 * scale}, 11 * scale, Ink);
        paragraph(L"CPU and RAM measure the whole process. CPU % uses all logical processors.",
                  {x, y + 160 * scale, width, 64 * scale}, 10 * scale, Muted);
    }
}
void drawQrOverlay(Graphics& g, const Layout& l) {
    if (app.tool != Tool::QrScan || !app.image)
        return;
    GraphicsState saved = g.Save();
    g.SetClip(l.canvas);
    g.TranslateTransform(l.imageX, l.imageY);
    g.ScaleTransform(l.zoom, l.zoom);
    RECT frame = canvasBounds();
    g.SetClip(RectF(float(frame.left), float(frame.top), float(frame.right - frame.left),
                    float(frame.bottom - frame.top)),
              CombineModeIntersect);
    SolidBrush found(Color(45, 0, 110, 235)), link(Color(45, 22, 167, 101));
    Pen edge(Blue, 2 / l.zoom), linkEdge(Color(255, 22, 167, 101), 2 / l.zoom);
    for (const auto& code : app.qrCodes) {
        bool isLink = !code.url.empty();
        g.FillPolygon(isLink ? &link : &found, code.corners.data(), 4);
        g.DrawPolygon(isLink ? &linkEdge : &edge, code.corners.data(), 4);
    }
    if (app.hasQrArea) {
        SolidBrush area(Color(20, 0, 110, 235));
        Pen border(Blue, 1.5f / l.zoom);
        border.SetDashStyle(DashStyleDash);
        g.FillRectangle(&area, app.qrArea);
        g.DrawRectangle(&border, app.qrArea);
    }
    g.Restore(saved);
}
void drawQrPanel(Graphics& g, REAL x, REAL y, REAL width, REAL scale) {
    auto paragraph = [&](const std::wstring& text, RectF rect, REAL size, Color color) {
        Font font(L"Segoe UI", size, FontStyleRegular, UnitPixel);
        SolidBrush ink(color);
        StringFormat format;
        format.SetTrimming(StringTrimmingEllipsisCharacter);
        g.DrawString(text.c_str(), -1, &font, rect, &format, &ink);
    };
    drawText(g, L"Scan QR", {x, y, width, 26 * scale}, 14 * scale, Ink, true);
    y += 38 * scale;
    paragraph(app.image ? L"Drag around a QR code, or click the image to scan the visible frame."
                        : L"Capture an image to begin.",
              {x, y, width, 56 * scale}, 12 * scale, Muted);
    y += 62 * scale;
    paragraph(L"Click a found code to copy its content. Ctrl + click opens web links.",
              {x, y, width, 56 * scale}, 12 * scale, Muted);
    y += 66 * scale;
    if (!app.qrFeedback.empty()) {
        paragraph(app.qrFeedback, {x, y, width, 36 * scale}, 12 * scale, Blue);
        y += 46 * scale;
    }
    for (const auto& code : app.qrCodes) {
        paragraph(code.url.empty() ? L"Text" : L"Link", {x, y, width, 18 * scale}, 11 * scale,
                  Muted);
        y += 18 * scale;
        paragraph(code.text, {x, y, width, 54 * scale}, 12 * scale, Ink);
        y += 62 * scale;
    }
}
void renderEditor(Graphics& g, HWND hwnd, int before = -1, bool selection = true,
                  bool cachedCapture = false, bool chromeOnly = false) {
    app.layout = calculateLayout(hwnd);
    auto l = app.layout;
    REAL s = l.scale;
    app.buttons.clear();
    g.Clear(Color(255, 245, 247, 250));
    SolidBrush white(Color(255, 255, 255, 255));
    Pen separator(Color(255, 222, 227, 234), 1);
    g.FillRectangle(&white, 0.f, 0.f, l.width, 56 * s);
    g.FillRectangle(&white, l.side, 56 * s, l.width - l.side, l.height - 86 * s);
    REAL x = 12 * s;
    for (int i = 0; i < ToolCount; i++) {
        RectF r(x, 10 * s, 40 * s, 36 * s);
        button(g, r, 100 + i, L"", app.tool == static_cast<Tool>(i));
        toolIcon(g, static_cast<Tool>(i), {x + 8 * s, 16 * s, 24 * s, 24 * s},
                 app.tool == static_cast<Tool>(i) ? Blue : Ink);
        x += 45 * s;
    }
    x += 15 * s;
    g.DrawLine(&separator, x - 8 * s, 15 * s, x - 8 * s, 41 * s);
    RectF undoRect(x, 10 * s, 36 * s, 36 * s);
    button(g, undoRect, 2, L"", false, false, !app.undo.empty());
    historyIcon(g, {x + 6 * s, 16 * s, 24 * s, 24 * s}, false, app.undo.empty() ? Muted : Ink);
    x += 42 * s;
    button(g, {x, 10 * s, 36 * s, 36 * s}, 3, L"", false, false, !app.redo.empty());
    historyIcon(g, {x + 6 * s, 16 * s, 24 * s, 24 * s}, true, app.redo.empty() ? Muted : Ink);
    x += 42 * s;
    button(g, {x, 10 * s, 36 * s, 36 * s}, 240, L"", false, false, validIndex(app.selected));
    actionIcon(g, {x + 6 * s, 16 * s, 24 * s, 24 * s}, 240, validIndex(app.selected) ? Ink : Muted);
    button(g, {l.width - 106 * s, 10 * s, 40 * s, 36 * s}, 4, L"", false, false, !!app.image);
    actionIcon(g, {l.width - 98 * s, 16 * s, 24 * s, 24 * s}, 4, app.image ? Ink : Muted);
    button(g, {l.width - 56 * s, 10 * s, 40 * s, 36 * s}, 5, L"", false, true, !!app.image);
    actionIcon(g, {l.width - 48 * s, 16 * s, 24 * s, 24 * s}, 5,
               app.image ? Color(255, 255, 255, 255) : Muted);
    g.DrawLine(&separator, 0.f, 56 * s, l.width, 56 * s);
    g.DrawLine(&separator, l.side, 56 * s, l.side, l.height - 30 * s);
    if (app.image) {
        if (!chromeOnly) {
            drawCapture(g, l, cachedCapture);
            drawAnnotations(g, l, 0, before < 0 ? static_cast<int>(app.items.size()) : before);
            if (selection) {
                drawSelection(g, l);
                drawCanvasHandles(g, l);
                drawOcrOverlay(g, l);
                drawQrOverlay(g, l);
            }
        }
    } else {
        drawText(g, ui::kEmptyTitle, {16 * s, 140 * s, l.side - 32 * s, 45 * s}, 24 * s, Ink, true,
                 true);
        drawText(g, captureShortcutName(app.captureShortcut),
                 {16 * s, 197 * s, l.side - 32 * s, 35 * s}, 19 * s, Blue, false, true);
        drawText(g, ui::kEmptyHint, {16 * s, 248 * s, l.side - 32 * s, 30 * s}, 13 * s, Muted,
                 false, true);
    }
    REAL px = l.side + 16 * s, pw = l.width - px - 16 * s, y = 72 * s;
    Tool t = activeTool();
    Style st = activeStyle();
    if (app.tool == Tool::TextSelect) {
        drawOcrPanel(g, px, y, pw, s);
    } else if (app.tool == Tool::QrScan) {
        drawQrPanel(g, px, y, pw, s);
    } else {
        if (t == Tool::Redact) {
            const auto* recognition = ocrReport();
            bool available = loremReady();
            for (int mode = 0; mode < 4; ++mode) {
                button(g, {px, y, pw, 30 * s}, 260 + mode, ui::kRedactionModes[mode],
                       st.redaction == mode, false, mode < 2 || available);
                y += 38 * s;
            }
            const wchar_t* note =
                available     ? L"Lorem replaces detected text before applying the effect."
                : recognition ? L"Lorem image is unavailable."
                              : L"Recognizing text… Lorem modes will be available when ready.";
            Font font(L"Segoe UI", 11 * s, FontStyleRegular, UnitPixel);
            SolidBrush ink(Muted);
            StringFormat wrap;
            RectF area(px, y, pw, 65 * s);
            g.DrawString(note, -1, &font, area, &wrap, &ink);
            y += 75 * s;
        } else {
            if (t == Tool::Arrow) {
                for (int i = 0; i < 2; i++) {
                    RectF r(px + i * (pw / 2), y, pw / 2 - 6 * s, 32 * s);
                    button(g, r, 200 + i, L"", st.arrow == i);
                    Style tiny = st;
                    tiny.size = 2 * s;
                    tiny.color = Ink.GetValue();
                    tiny.arrow = i;
                    drawArrow(g, {r.X + 12 * s, r.Y + 21 * s},
                              {r.GetRight() - 12 * s, r.Y + 11 * s}, tiny);
                }
                y += 46 * s;
            }
            drawText(g, ui::kColor, {px, y, pw - 56 * s, 28 * s}, 12 * s);
            RectF fg(px + pw - 48 * s, y, 48 * s, 28 * s);
            button(g, fg, 210, L"", !app.fillTarget);
            SolidBrush color(Color(st.color));
            g.FillRectangle(&color, fg.X + 6 * s, fg.Y + 5 * s, fg.Width - 12 * s,
                            fg.Height - 10 * s);
            y += 40 * s;
            bool fillable = t == Tool::Text || t == Tool::Rectangle || t == Tool::Counter;
            if (fillable) {
                drawText(g, ui::kBackground, {px, y, pw - 56 * s, 28 * s}, 12 * s);
                RectF bg(px + pw - 48 * s, y, 48 * s, 28 * s);
                button(g, bg, 211, L"", app.fillTarget);
                RectF chip(bg.X + 6 * s, bg.Y + 5 * s, bg.Width - 12 * s, bg.Height - 10 * s);
                if (st.filled) {
                    SolidBrush cb(Color(st.fill));
                    g.FillRectangle(&cb, chip);
                } else
                    checker(g, chip);
                y += 40 * s;
            }
            ARGB paletteColor =
                (app.fillTarget || (t == Tool::Text && st.filled)) ? st.fill : st.color;
            REAL tile = (pw - 5 * 6 * s) / 6;
            for (int i = 0; i < 12; i++) {
                RectF r(px + (i % 6) * (tile + 6 * s), y + (i / 6) * 34 * s, tile, 28 * s);
                int id = i < 11 ? 300 + i : 213;
                button(g, r, id, L"", i < 11 && paletteColor == Swatches[i]);
                RectF inside(r.X + 3 * s, r.Y + 3 * s, r.Width - 6 * s, r.Height - 6 * s);
                if (i < 11) {
                    SolidBrush c{Color(Swatches[i])};
                    g.FillRectangle(&c, inside);
                } else {
                    LinearGradientBrush rainbow(inside, Color(255, 255, 0, 0),
                                                Color(255, 130, 0, 255),
                                                LinearGradientModeHorizontal);
                    Color colors[] = {Color(255, 255, 0, 0),   Color(255, 255, 160, 0),
                                      Color(255, 255, 240, 0), Color(255, 0, 190, 80),
                                      Color(255, 0, 180, 240), Color(255, 40, 70, 240),
                                      Color(255, 180, 0, 220)};
                    REAL positions[] = {0, 1 / 6.f, 2 / 6.f, 3 / 6.f, 4 / 6.f, 5 / 6.f, 1};
                    rainbow.SetInterpolationColors(colors, positions, 7);
                    g.FillRectangle(&rainbow, inside);
                }
                if (app.hoverButton == id) {
                    SolidBrush feedback(Color(app.pressedButton == id ? 75 : 35, 255, 255, 255));
                    g.FillRectangle(&feedback, inside);
                }
            }
            y += 78 * s;
        }
        RectF slider(px + 6 * s, y, pw - 12 * s, 28 * s);
        app.buttons.push_back({slider, 222});
        REAL minSize = t == Tool::Text ? 8.f : (t == Tool::Counter ? 12.f : 1.f),
             maxSize =
                 t == Tool::Text ? 96.f : (t == Tool::Counter || t == Tool::Redact ? 64.f : 24.f);
        Pen track(Color(255, 219, 227, 239), 4 * s);
        g.DrawLine(&track, slider.X, slider.Y + 14 * s, slider.GetRight(), slider.Y + 14 * s);
        REAL k = std::clamp((st.size - minSize) / (maxSize - minSize), 0.f, 1.f);
        Pen activeTrack(Blue, 4 * s);
        g.DrawLine(&activeTrack, slider.X, slider.Y + 14 * s, slider.X + k * slider.Width,
                   slider.Y + 14 * s);
        SolidBrush knob(app.sizing ? Color(255, 0, 77, 176)
                                   : (app.hoverButton == 222 ? Color(255, 0, 94, 211) : Blue));
        REAL radius = app.sizing || app.hoverButton == 222 ? 7 * s : 6 * s;
        g.FillEllipse(&knob, slider.X + k * slider.Width - radius, slider.Y + 14 * s - radius,
                      2 * radius, 2 * radius);
        y += 43 * s;
        if (t == Tool::Redact && validIndex(app.selected)) {
            auto cache = app.items[app.selected].redactionCache;
            if (cache && !cache->result.bitmap && !app.creating)
                drawText(g, L"Effect unavailable; area covered.", {px, y, pw, 30 * s}, 10 * s,
                         Muted);
            else if (cache && st.redaction >= 2)
                drawText(g, std::to_wstring(cache->result.replacedWords) + L" words replaced",
                         {px, y, pw, 30 * s}, 11 * s, Muted);
        }
        if (t == Tool::Counter && validIndex(app.selected)) {
            drawText(g, ui::kNumber, {px, y, pw - 72 * s, 28 * s}, 12 * s);
            button(g, {px + pw - 68 * s, y, 30 * s, 28 * s}, 230, L"−");
            button(g, {px + pw - 30 * s, y, 30 * s, 28 * s}, 231, L"+");
            y += 37 * s;
            button(g, {px, y, pw, 29 * s}, 232, ui::kRemovePointer);
            y += 40 * s;
        }
    }
    g.FillRectangle(&white, 0.f, l.height - 30 * s, l.width, 30 * s);
    g.DrawLine(&separator, 0.f, l.height - 30 * s, l.width, l.height - 30 * s);
    if (app.image) {
        RECT bounds = canvasBounds();
        drawText(g,
                 std::to_wstring(bounds.right - bounds.left) + L" × " +
                     std::to_wstring(bounds.bottom - bounds.top) + L" px",
                 {14 * s, l.height - 30 * s, l.width - 70 * s, 30 * s}, 11 * s, Muted);
    }
    button(g, {l.width - 36 * s, l.height - 27 * s, 25 * s, 24 * s}, 254, L"?");
    syncTooltips();
}
void commitText(bool cancel = false);
void beginCapture();
void perform(int id, PointF point = {0, 0});
LRESULT CALLBACK EditorProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK OverlayProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK TextProc(HWND, UINT, WPARAM, LPARAM);
WNDPROC originalTextProc = nullptr;

CLSID pngEncoder() {
    return {0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
}
std::unique_ptr<Bitmap> flattened() {
    if (!app.image)
        return {};
    RECT bounds = canvasBounds();
    auto bitmap = std::make_unique<Bitmap>(bounds.right - bounds.left, bounds.bottom - bounds.top,
                                           PixelFormat32bppARGB);
    if (bitmap->GetLastStatus() != Ok)
        return {};
    Graphics g(bitmap.get());
    g.Clear(Color(app.canvas.color));
    g.TranslateTransform(static_cast<REAL>(-bounds.left), static_cast<REAL>(-bounds.top));
    g.DrawImage(app.image.get(), 0, 0, app.image->GetWidth(), app.image->GetHeight());
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    for (const auto& item : app.items)
        drawItem(g, item);
    return bitmap;
}
bool encodePng(Bitmap& bitmap, const wchar_t* path) {
    CLSID encoder = pngEncoder();
    return bitmap.Save(path, &encoder, nullptr) == Ok;
}
#ifdef CAPTURA_TEST
std::wstring testPicturesDirectory, testSaveDialogPath, testSavedPath;
int testSaveDialogs = 0;
int testExportCompletions = 0;
bool testClipboardSuccess = true;
std::wstring testCopiedText;
std::wstring testOpenedUrl;
#endif
void exportComplete() {
#ifdef CAPTURA_TEST
    if (app.test)
        ++testExportCompletions;
#endif
    if (app.tooltip)
        SendMessageW(app.tooltip, TTM_POP, 0, 0);
    app.hoverButton = -1;
    app.pressedButton = -1;
    dragBackground.reset();
    dragRevision = 0;
    if (GetCapture() == app.main)
        ReleaseCapture();
    ShowWindow(app.main, SW_HIDE);
}
std::wstring captureFilename() {
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t name[96];
    swprintf_s(name, L"Captura-%04d%02d%02d-%02d%02d%02d-%03d.png", now.wYear, now.wMonth, now.wDay,
               now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    return name;
}
bool chooseSavePath(std::wstring& path) {
#ifdef CAPTURA_TEST
    if (app.test) {
        ++testSaveDialogs;
        path = testSaveDialogPath;
        return !path.empty();
    }
#endif
    wchar_t filename[32768];
    wcscpy_s(filename, captureFilename().c_str());
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app.main;
    ofn.lpstrFilter = ui::kPngFilter;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = 32768;
    ofn.lpstrTitle = ui::kSaveDialogTitle;
    ofn.lpstrDefExt = L"png";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn))
        return false;
    path = filename;
    return true;
}
bool screenshotsDirectory(std::wstring& directory) {
#ifdef CAPTURA_TEST
    if (app.test)
        directory = testPicturesDirectory;
    else
#endif
    {
        PWSTR pictures = nullptr;
        HRESULT result =
            SHGetKnownFolderPath(FOLDERID_Pictures, KF_FLAG_CREATE, nullptr, &pictures);
        if (FAILED(result))
            return false;
        directory = pictures;
        CoTaskMemFree(pictures);
    }
    if (directory.empty())
        return false;
    directory += L"\\Screenshots";
    if (CreateDirectoryW(directory.c_str(), nullptr))
        return true;
    if (GetLastError() != ERROR_ALREADY_EXISTS)
        return false;
    DWORD attributes = GetFileAttributesW(directory.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}
bool writePngFile(Bitmap& bitmap, HANDLE file) {
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)))
        return false;
    CLSID encoder = pngEncoder();
    bool ok = bitmap.Save(stream, &encoder, nullptr) == Ok;
    STATSTG stat{};
    HGLOBAL memory = nullptr;
    if (ok)
        ok = SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME)) &&
             SUCCEEDED(GetHGlobalFromStream(stream, &memory));
    if (ok) {
        BYTE* data = static_cast<BYTE*>(GlobalLock(memory));
        ok = data != nullptr;
        ULONGLONG offset = 0;
        while (ok && offset < stat.cbSize.QuadPart) {
            DWORD amount = static_cast<DWORD>(
                      std::min<ULONGLONG>(stat.cbSize.QuadPart - offset, MAXDWORD)),
                  written = 0;
            ok = WriteFile(file, data + static_cast<SIZE_T>(offset), amount, &written, nullptr) !=
                     FALSE &&
                 written > 0;
            offset += written;
        }
        if (data)
            GlobalUnlock(memory);
    }
    stream->Release();
    return ok;
}
bool saveAutomatically(Bitmap& bitmap, std::wstring& path, const std::wstring& name) {
    std::wstring directory;
    if (!screenshotsDirectory(directory))
        return false;
    for (unsigned suffix = 0; suffix < 10000; suffix++) {
        path = directory + L"\\" +
               (suffix ? name.substr(0, name.size() - 4) + L"-" + std::to_wstring(suffix) + L".png"
                       : name);
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS)
                continue;
            return false;
        }
        bool ok = writePngFile(bitmap, file);
        if (ok)
            ok = FlushFileBuffers(file) != FALSE;
        CloseHandle(file);
        if (!ok)
            DeleteFileW(path.c_str());
        return ok;
    }
    return false;
}
void saveImage(bool custom = false) {
    commitText();
    auto bitmap = flattened();
    if (!bitmap)
        return;
    std::wstring path;
    bool ok = false;
    if (custom) {
        if (!chooseSavePath(path))
            return;
        ok = encodePng(*bitmap, path.c_str());
    } else
        ok = saveAutomatically(*bitmap, path, captureFilename());
#ifdef CAPTURA_TEST
    if (app.test) {
        testSavedPath = ok ? path : L"";
        if (!ok)
            return;
    }
#endif
    if (ok)
        exportComplete();
    else
        error(app.main, ui::kErrorSave);
}
HGLOBAL makeDib(Bitmap& bitmap) {
    const UINT w = bitmap.GetWidth(), h = bitmap.GetHeight();
    SIZE_T bytes = static_cast<SIZE_T>(w) * h * 4;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPV5HEADER) + bytes);
    if (!memory)
        return nullptr;
    auto* header = static_cast<BITMAPV5HEADER*>(GlobalLock(memory));
    if (!header) {
        GlobalFree(memory);
        return nullptr;
    }
    *header = {};
    header->bV5Size = sizeof(*header);
    header->bV5Width = w;
    header->bV5Height = -static_cast<LONG>(h);
    header->bV5Planes = 1;
    header->bV5BitCount = 32;
    header->bV5Compression = BI_BITFIELDS;
    header->bV5RedMask = 0x00ff0000;
    header->bV5GreenMask = 0x0000ff00;
    header->bV5BlueMask = 0x000000ff;
    header->bV5AlphaMask = 0xff000000;
    header->bV5CSType = LCS_sRGB;
    header->bV5SizeImage = static_cast<DWORD>(bytes);
    BitmapData data{};
    Rect r(0, 0, w, h);
    if (bitmap.LockBits(&r, ImageLockModeRead, PixelFormat32bppARGB, &data) != Ok) {
        GlobalUnlock(memory);
        GlobalFree(memory);
        return nullptr;
    }
    BYTE* dest = reinterpret_cast<BYTE*>(header + 1);
    for (UINT y = 0; y < h; y++)
        memcpy(dest + static_cast<SIZE_T>(y) * w * 4,
               static_cast<BYTE*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride,
               static_cast<SIZE_T>(w) * 4);
    bitmap.UnlockBits(&data);
    GlobalUnlock(memory);
    return memory;
}
bool copyText(const std::wstring& text) {
    bool copied = false;
#ifdef CAPTURA_TEST
    if (app.test) {
        copied = testClipboardSuccess;
        if (copied)
            testCopiedText = text;
    } else
#endif
    {
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
        if (memory) {
            void* bytes = GlobalLock(memory);
            if (bytes) {
                memcpy(bytes, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
                GlobalUnlock(memory);
                if (OpenClipboard(app.main)) {
                    copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
                    CloseClipboard();
                }
            }
            if (!copied)
                GlobalFree(memory);
        }
    }
    return copied;
}
bool openUrl(const std::wstring& url) {
#ifdef CAPTURA_TEST
    if (app.test) {
        testOpenedUrl = url;
        return true;
    }
#endif
    return reinterpret_cast<INT_PTR>(
               ShellExecuteW(app.main, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}
void copySelectedText() {
    if (!ocrReady()) {
        app.textFeedback = L"Text is not ready yet.";
        invalidate();
        return;
    }
    RECT bounds = canvasBounds();
    RectF frame(float(bounds.left), float(bounds.top), float(bounds.right - bounds.left),
                float(bounds.bottom - bounds.top)),
        visible;
    RectF::Intersect(visible, app.textArea, frame);
    std::wstring text;
    if (app.hasTextSelection)
        for (const auto& line : ocrReport()->lines) {
            std::wstring row;
            for (const auto& word : line.words)
                if (!hiddenOcrWord(word) && captura::wordInside(word, visible)) {
                    if (!row.empty())
                        row += L' ';
                    row += word.text;
                }
            if (!row.empty()) {
                if (!text.empty())
                    text += L"\r\n";
                text += row;
            }
        }
    if (text.empty()) {
        app.textFeedback = L"No text in the selected area.";
        invalidate();
        return;
    }
    app.textFeedback =
        copyText(text) ? L"Text copied to clipboard." : L"Could not copy text. Try Ctrl+C.";
    invalidate();
}
int qrAt(PointF image) {
    for (int index = 0; index < static_cast<int>(app.qrCodes.size()); ++index)
        if (captura::qrContains(app.qrCodes[index], image))
            return index;
    return -1;
}
void scanQrArea() {
    RECT frame = canvasBounds();
    RectF area = app.qrArea;
    if (area.Width < 2 && area.Height < 2) {
        // A plain click scans the whole visible frame.
        area = {float(frame.left), float(frame.top), float(frame.right - frame.left),
                float(frame.bottom - frame.top)};
        app.hasQrArea = false;
    }
    Rect region(static_cast<INT>(std::floor(area.X)), static_cast<INT>(std::floor(area.Y)),
                static_cast<INT>(std::ceil(area.GetRight())) - static_cast<INT>(std::floor(area.X)),
                static_cast<INT>(std::ceil(area.GetBottom())) -
                    static_cast<INT>(std::floor(area.Y)));
    app.qrCodes = captura::scanQrCodes(*app.image, region);
    size_t links = 0;
    for (const auto& code : app.qrCodes)
        links += !code.url.empty();
    if (app.qrCodes.empty())
        app.qrFeedback = L"No QR code found. Try a larger area around the code.";
    else
        app.qrFeedback = std::to_wstring(app.qrCodes.size()) +
                         (app.qrCodes.size() == 1 ? L" QR code found" : L" QR codes found") +
                         (links ? L". Click to copy; Ctrl + click opens the link."
                                : L". Click to copy its content.");
    invalidate();
}
void activateQr(int index, bool ctrl) {
    const captura::QrCode& code = app.qrCodes[index];
    if (ctrl && !code.url.empty()) {
        app.qrFeedback = openUrl(code.url) ? L"Link opened in the browser."
                                           : L"Could not open the link. Click copies it instead.";
    } else {
        app.qrFeedback = copyText(code.text) ? (code.url.empty() ? L"Text copied to clipboard."
                                                                 : L"Link copied to clipboard.")
                                             : L"Could not copy. Try again.";
    }
    invalidate();
}
void copyImage() {
    if (app.tool == Tool::TextSelect) {
        copySelectedText();
        return;
    }
    commitText();
    auto bitmap = flattened();
    if (!bitmap)
        return;
    HGLOBAL dib = makeDib(*bitmap);
    if (!dib) {
        error(app.main, ui::kErrorCopyMemory);
        return;
    }
    IStream* stream = nullptr;
    HGLOBAL png = nullptr;
    if (SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) {
        CLSID encoder = pngEncoder();
        if (bitmap->Save(stream, &encoder, nullptr) == Ok) {
            STATSTG stat{};
            HGLOBAL source = nullptr;
            if (SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME)) &&
                SUCCEEDED(GetHGlobalFromStream(stream, &source)))
                png = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(stat.cbSize.QuadPart));
            if (png) {
                void* dst = GlobalLock(png);
                void* src = GlobalLock(source);
                bool copied = dst && src;
                if (copied)
                    memcpy(dst, src, static_cast<SIZE_T>(stat.cbSize.QuadPart));
                if (dst)
                    GlobalUnlock(png);
                if (src)
                    GlobalUnlock(source);
                if (!copied) {
                    GlobalFree(png);
                    png = nullptr;
                }
            }
        }
        stream->Release();
    }
    bool ok = false;
#ifdef CAPTURA_TEST
    if (app.test) {
        GlobalFree(dib);
        if (png)
            GlobalFree(png);
        ok = testClipboardSuccess;
    } else
#endif
    {
        if (!OpenClipboard(app.main)) {
            GlobalFree(dib);
            if (png)
                GlobalFree(png);
            error(app.main, ui::kErrorClipboardBusy);
            return;
        }
        if (EmptyClipboard()) {
            ok = SetClipboardData(CF_DIBV5, dib) != nullptr;
            if (!ok)
                GlobalFree(dib);
            if (png) {
                bool pngOk = SetClipboardData(RegisterClipboardFormatW(L"PNG"), png) != nullptr;
                if (!pngOk)
                    GlobalFree(png);
                ok = ok || pngOk;
            }
        } else {
            GlobalFree(dib);
            if (png)
                GlobalFree(png);
        }
        CloseClipboard();
    }
    if (ok)
        exportComplete();
    else {
#ifdef CAPTURA_TEST
        if (app.test)
            return;
#endif
        error(app.main, ui::kErrorCopy);
    }
}
void applyColor(ARGB chosen) {
    Style& style = activeStyle();
    Tool t = activeTool();
    if (t == Tool::Text) {
        if (style.filled || app.fillTarget) {
            style.fill = chosen;
            style.filled = true;
            style.color = contrastingText(chosen);
        } else
            style.color = chosen;
        shareColor(chosen);
    } else if (app.fillTarget) {
        style.fill = chosen;
        style.filled = true;
    } else {
        style.color = chosen;
        shareColor(chosen);
    }
    app.defaults[static_cast<int>(t)] = style;
    invalidate();
}
void chooseColor() {
    Style current = activeStyle();
    ARGB argb = (app.fillTarget || (activeTool() == Tool::Text && current.filled)) ? current.fill
                                                                                   : current.color;
    CHOOSECOLORW cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = app.main;
    cc.rgbResult = RGB((argb >> 16) & 255, (argb >> 8) & 255, argb & 255);
    cc.lpCustColors = app.customColors;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&cc)) {
        if (validIndex(app.selected))
            checkpoint();
        ARGB chosen =
            Color(255, GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult))
                .GetValue();
        applyColor(chosen);
    }
}
void setTool(Tool tool) {
    commitText();
    app.selectingText = false;
    app.scanningQr = app.hasQrArea = false;
    app.pressedQr = -1;
    if (GetCapture() == app.main && !app.dragging && !app.panning && !app.resizingCanvas)
        ReleaseCapture();
    app.tool = tool;
    app.selected = -1;
    app.fillTarget = tool == Tool::Text && app.defaults[static_cast<int>(tool)].filled;
    report(tool == Tool::Select ? ui::kSelectHint
                                : (tool == Tool::Counter ? ui::kCounterHint : ui::kDrawHint));
}
void changeSize(REAL value) {
    Tool t = activeTool();
    REAL max = t == Tool::Text ? 96.f : (t == Tool::Counter || t == Tool::Redact ? 64.f : 24.f);
    REAL minimum = t == Tool::Text ? 8.f : (t == Tool::Counter ? 12.f : 1.f);
    activeStyle().size = std::clamp(value, minimum, max);
    app.defaults[static_cast<int>(t)] = activeStyle();
    invalidate();
}
void sizeAt(PointF point) {
    Tool t = activeTool();
    REAL max = t == Tool::Text ? 96.f : (t == Tool::Counter || t == Tool::Redact ? 64.f : 24.f);
    REAL minimum = t == Tool::Text ? 8.f : (t == Tool::Counter ? 12.f : 1.f);
    changeSize(
        std::round(minimum + (point.X - (app.layout.side + 22 * app.layout.scale)) /
                                 (app.layout.width - app.layout.side - 44 * app.layout.scale) *
                                 (max - minimum)));
}
void perform(int id, PointF point) {
    if (id != 222)
        commitText();
    if (id == 1)
        beginCapture();
    else if (id == 2)
        undo(false);
    else if (id == 3)
        undo(true);
    else if (id == 4)
        copyImage();
    else if (id == 5)
        saveImage(keyPressed(VK_SHIFT));
    else if (id >= 100 && id < 100 + ToolCount)
        setTool(static_cast<Tool>(id - 100));
    else if (id >= 260 && id < 264) {
        if (id >= 262 && !loremReady())
            return;
        if (validIndex(app.selected))
            checkpoint();
        activeStyle().redaction = id - 260;
        app.defaults[static_cast<int>(Tool::Redact)].redaction = id - 260;
        invalidate();
    } else if (id >= 200 && id <= 201) {
        if (validIndex(app.selected))
            checkpoint();
        activeStyle().arrow = id - 200;
        invalidate();
    } else if (id == 210 || id == 211) {
        app.fillTarget = id == 211 || (activeTool() == Tool::Text && activeStyle().filled);
        invalidate();
    } else if (id == 212) {
        if (validIndex(app.selected))
            checkpoint();
        activeStyle().filled = !activeStyle().filled;
        if (activeTool() == Tool::Text) {
            activeStyle().color =
                activeStyle().filled ? contrastingText(activeStyle().fill) : app.sharedColor;
            app.fillTarget = activeStyle().filled;
        }
        invalidate();
    } else if (id == 213)
        chooseColor();
    else if (id >= 300 && id <= 310) {
        if (validIndex(app.selected))
            checkpoint();
        applyColor(Swatches[id - 300]);
    } else if (id == 220 || id == 221) {
        if (validIndex(app.selected))
            checkpoint();
        changeSize(activeStyle().size + (id == 220 ? -1.f : 1.f));
    } else if (id == 222) {
        if (validIndex(app.selected))
            checkpoint();
        sizeAt(point);
    } else if ((id == 230 || id == 231) && validIndex(app.selected)) {
        checkpoint();
        app.items[app.selected].number =
            std::clamp(app.items[app.selected].number + (id == 230 ? -1 : 1), 1, 999);
        invalidate();
    } else if (id == 232 && validIndex(app.selected)) {
        checkpoint();
        app.items[app.selected].pointer = false;
        invalidate();
    } else if (id == 240 && validIndex(app.selected)) {
        checkpoint();
        app.items.erase(app.items.begin() + app.selected);
        app.selected = -1;
        invalidate();
    } else if (id >= 250 && id <= 253) {
        app.pan = {0, 0};
        if (id == 250)
            app.userZoom = 0;
        else if (id == 251)
            app.userZoom = 1;
        else
            app.userZoom = std::clamp(app.layout.zoom * (id == 252 ? 0.8f : 1.25f), 0.02f, 8.f);
        invalidate();
    } else if (id == 254)
        MessageBoxW(app.main, helpText().c_str(), ui::kHelpTitle, MB_OK | MB_ICONINFORMATION);
    if ((id >= 200 && id <= 201) || id == 212)
        app.defaults[static_cast<int>(activeTool())] = activeStyle();
}
void positionTextEditor() {
    if (!app.edit || !validIndex(app.editIndex))
        return;
    // Native input/clipboard/Unicode editing stays available, but its HWND is
    // outside the client area. Only the annotation itself is painted on-screen.
    SetWindowPos(app.edit, nullptr, -32000, -32000, 1000, 100, SWP_NOZORDER | SWP_NOACTIVATE);
}
void syncInlineText() {
    if (!app.edit || !validIndex(app.editIndex))
        return;
    int len = GetWindowTextLengthW(app.edit);
    std::wstring text(len + 1, L'\0');
    GetWindowTextW(app.edit, text.data(), len + 1);
    text.resize(len);
    DWORD first = 0, last = 0;
    SendMessageW(app.edit, EM_GETSEL, reinterpret_cast<WPARAM>(&first),
                 reinterpret_cast<LPARAM>(&last));
    bool changed = app.items[app.editIndex].text != text || first != app.editSelectionStart ||
                   last != app.editSelectionEnd;
    if (!changed)
        return;
    app.items[app.editIndex].text = std::move(text);
    app.editSelectionStart = first;
    app.editSelectionEnd = last;
    if (!app.items[app.editIndex].text.empty())
        growCanvas(app.items[app.editIndex]);
    invalidate();
    if (!app.test && IsWindowVisible(app.main))
        UpdateWindow(app.main);
}
void startTextEdit(int index, bool record = true) {
    commitText();
    if (!validIndex(index))
        return;
    if (record)
        checkpoint();
    app.gestureCanvas = canvasBounds();
    app.editIndex = index;
    app.selected = index;
    app.editOriginal = app.items[index].text;
    app.editCanvas = app.canvas;
    app.fillTarget = app.items[index].style.filled;
    app.editSelectionStart = app.editSelectionEnd = MAXDWORD;
    app.edit = CreateWindowExW(
        0, L"EDIT", app.items[index].text.c_str(),
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL, -32000, -32000,
        1000, 100, app.main, reinterpret_cast<HMENU>(700), app.instance, nullptr);
    if (!app.edit) {
        app.editIndex = -1;
        error(app.main, ui::kErrorTextEditor);
        return;
    }
    SendMessageW(app.edit, EM_SETLIMITTEXT, 4096, 0);
    originalTextProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(app.edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(TextProc)));
    positionTextEditor();
    if (!app.test)
        SetFocus(app.edit);
    HideCaret(app.edit);
    SendMessageW(app.edit, EM_SETSEL, 0, -1);
    invalidate();
}
void commitText(bool cancel) {
    if (!app.edit)
        return;
    int index = app.editIndex;
    HWND edit = app.edit;
    app.edit = nullptr;
    app.editIndex = -1;
    if (validIndex(index)) {
        if (cancel) {
            app.items[index].text = app.editOriginal;
            app.canvas = app.editCanvas;
        } else {
            int len = GetWindowTextLengthW(edit);
            std::wstring text(len + 1, L'\0');
            GetWindowTextW(edit, text.data(), len + 1);
            text.resize(len);
            app.items[index].text = text;
        }
        if (app.items[index].text.empty()) {
            app.items.erase(app.items.begin() + index);
            app.selected = -1;
        }
    }
    if (!cancel && validIndex(index))
        app.items[index].cropped = false;
    DestroyWindow(edit);
    app.editOriginal.clear();
    if (app.editFont) {
        DeleteObject(app.editFont);
        app.editFont = nullptr;
    }
    if (!app.dragging) {
        if (!cancel && trimCanvasToItems())
            app.pinnedCanvas = true;
        centerExpandedCanvas();
    }
    if (!app.test)
        SetFocus(app.main);
    invalidate();
}
LRESULT CALLBACK TextProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_KEYDOWN && (w == VK_ESCAPE || (w == VK_RETURN && !keyPressed(VK_SHIFT)))) {
        commitText(w == VK_ESCAPE);
        return 0;
    }
    if (msg == WM_KEYDOWN && keyPressed(VK_CONTROL) && w == 'A') {
        SendMessageW(hwnd, EM_SETSEL, 0, -1);
        syncInlineText();
        return 0;
    }
    if (msg == WM_KEYDOWN && keyPressed(VK_CONTROL) && w == 'S') {
        saveImage(keyPressed(VK_SHIFT));
        return 0;
    }
    LRESULT result = CallWindowProcW(originalTextProc, hwnd, msg, w, l);
    if (msg == WM_CHAR || msg == WM_KEYDOWN || msg == WM_SETTEXT || msg == WM_PASTE ||
        msg == WM_CUT || msg == WM_CLEAR || msg == WM_UNDO || msg == EM_REPLACESEL ||
        msg == EM_SETSEL || msg == WM_IME_COMPOSITION)
        syncInlineText();
    return result;
}

void mouseDown(PointF screen) {
    bool wasEditing = app.edit != nullptr;
    commitText();
    app.layout = calculateLayout(app.main);
    if (app.tooltip)
        SendMessageW(app.tooltip, TTM_POP, 0, 0);
    int control = buttonAt(screen);
    if (control >= 0) {
        if (buttonAt(screen, true) < 0)
            return;
        setHoverButton(control);
        app.pressedButton = control;
        SetCapture(app.main);
        if (control == 222) {
            perform(control, screen);
            app.sizing = true;
        }
        invalidateButton(control);
        if (!app.test && IsWindowVisible(app.main))
            UpdateWindow(app.main);
        return;
    }
    setHoverButton(-1);
    int canvasCorner = canvasHandleAt(screen);
    if (canvasCorner >= 0 && !keyPressed(VK_SPACE)) {
        app.hasTextSelection = false;
        checkpoint();
        app.resizeStart = canvasBounds();
        app.gestureCanvas = app.resizeStart;
        app.canvasHandle = canvasCorner;
        app.resizingCanvas = true;
        app.canvasGrab = sub(toImage(screen, false), canvasCorners(app.resizeStart)[canvasCorner]);
        app.selected = -1;
        app.hoverCanvas = canvasCorner;
        app.pinnedCanvas = true;
        app.pinnedOrigin = {app.layout.imageX, app.layout.imageY};
        app.pinnedZoom = app.layout.zoom;
        SetCapture(app.main);
        invalidate();
        if (!app.test && IsWindowVisible(app.main))
            UpdateWindow(app.main);
        return;
    }
    if (wasEditing) {
        app.selected = -1;
        invalidate();
        return;
    }
    if (!onImage(screen)) {
        app.selected = -1;
        invalidate();
        return;
    }
    if (keyPressed(VK_SPACE)) {
        app.panning = true;
        app.dragStart = screen;
        app.initialPan = app.pan;
        SetCapture(app.main);
        return;
    }
    PointF p = toImage(screen);
    if (app.tool == Tool::TextSelect) {
        app.selected = -1;
        if (ocrReady()) {
            app.textStart = p;
            app.textArea = {p.X, p.Y, 0, 0};
            app.selectingText = app.hasTextSelection = true;
            app.textFeedback.clear();
            SetCapture(app.main);
        }
        invalidate();
        return;
    }
    if (app.tool == Tool::QrScan) {
        app.selected = -1;
        int index = qrAt(p);
        if (index >= 0) {
            app.pressedQr = index;
        } else {
            app.qrStart = p;
            app.qrArea = {p.X, p.Y, 0, 0};
            app.scanningQr = app.hasQrArea = true;
            app.qrFeedback.clear();
        }
        SetCapture(app.main);
        invalidate();
        return;
    }
    if (app.tool == Tool::Redact) {
        p = {std::round(p.X), std::round(p.Y)};
        if (app.defaults[static_cast<int>(Tool::Redact)].redaction >= 2 && !loremReady()) {
            report(L"Wait for Windows OCR before replacing text.");
            return;
        }
    }
    app.dragHandle = -1;
    if (validIndex(app.selected)) {
        auto points = handles(app.items[app.selected]);
        for (int h = static_cast<int>(points.size()) - 1; h >= 0; h--)
            if (distance(p, points[h]) <= 9 * app.layout.scale / app.layout.zoom) {
                app.dragHandle = h;
                break;
            }
    }
    if (app.dragHandle >= 0 || app.tool == Tool::Select) {
        if (app.dragHandle < 0)
            app.selected = hitTest(p);
        if (validIndex(app.selected)) {
            if (app.items[app.selected].tool == Tool::Text)
                app.fillTarget = app.items[app.selected].style.filled;
            if (app.items[app.selected].tool == Tool::Redact)
                p = {std::round(p.X), std::round(p.Y)};
            checkpoint();
            app.gestureCanvas = canvasBounds();
            app.dragging = true;
            app.creating = false;
            app.dragStart = p;
            app.initialItem = app.items[app.selected];
            SetCapture(app.main);
        }
        invalidate();
        return;
    }
    if (app.tool == Tool::Text) {
        int text = hitTest(p);
        if (validIndex(text) && app.items[text].tool == Tool::Text) {
            startTextEdit(text);
            return;
        }
    }
    checkpoint();
    app.gestureCanvas = canvasBounds();
    Item item;
    item.tool = app.tool;
    item.a = item.b = p;
    item.style = app.defaults[static_cast<int>(app.tool)];
    if (item.tool == Tool::Brush) {
        item.stroke.push_back(p);
        item.strokeBounds = {p.X, p.Y, 0, 0};
    }
    if (item.tool == Tool::Counter) {
        item.number = 1;
        for (const auto& i : app.items)
            if (i.tool == Tool::Counter)
                item.number = std::max(item.number, i.number + 1);
        item.number = std::min(item.number, 999);
    }
    app.items.push_back(item);
    app.selected = static_cast<int>(app.items.size()) - 1;
    if (item.tool == Tool::Text) {
        startTextEdit(app.selected, false);
        return;
    }
    // A new counter starts with its tip handle grabbed, anchored at the
    // original click. A plain click keeps the circle; dragging pulls the tip.
    // It uses the existing handle path, so creation is a single undo action.
    app.dragging = true;
    app.creating = item.tool != Tool::Counter;
    app.dragHandle = item.tool == Tool::Counter ? 1 : -1;
    app.dragStart = p;
    app.initialItem = item;
    SetCapture(app.main);
    invalidate();
}
RECT annotationBounds(const Item& item, const Layout& l) {
    RectF r = normalized(item.a, item.b);
    if (item.tool == Tool::Text)
        r = textRect(item);
    else if (item.tool == Tool::Brush)
        r = item.strokeBounds;
    else if (item.tool == Tool::Counter) {
        REAL radius = counterRadius(item);
        PointF end = item.pointer ? counterTip(item) : item.a;
        REAL left = std::min(item.a.X - radius, end.X), top = std::min(item.a.Y - radius, end.Y);
        r = {left, top, std::max(item.a.X + radius, end.X) - left,
             std::max(item.a.Y + radius, end.Y) - top};
    } else if ((item.tool == Tool::Arrow || item.tool == Tool::Line) && item.bent) {
        PointF m = curveControl(item);
        REAL left = std::min(r.X, m.X), top = std::min(r.Y, m.Y);
        r = {left, top, std::max(r.GetRight(), m.X) - left, std::max(r.GetBottom(), m.Y) - top};
    }
    REAL strokeMargin =
        item.tool == Tool::Arrow
            ? std::max(12.f, item.style.size * 5)
            : (item.tool == Tool::Rectangle || item.tool == Tool::Line || item.tool == Tool::Brush
                   ? item.style.size / 2 + 2
                   : 2.f);
    REAL padding = std::max(12 * l.scale, strokeMargin * l.zoom);
    RECT bounds{static_cast<LONG>(std::floor(l.imageX + r.X * l.zoom - padding)),
                static_cast<LONG>(std::floor(l.imageY + r.Y * l.zoom - padding)),
                static_cast<LONG>(std::ceil(l.imageX + r.GetRight() * l.zoom + padding)),
                static_cast<LONG>(std::ceil(l.imageY + r.GetBottom() * l.zoom + padding))};
    RECT canvas{static_cast<LONG>(std::floor(l.canvas.X)),
                static_cast<LONG>(std::floor(l.canvas.Y)),
                static_cast<LONG>(std::ceil(l.canvas.GetRight())),
                static_cast<LONG>(std::ceil(l.canvas.GetBottom()))},
        clipped{};
    IntersectRect(&clipped, &bounds, &canvas);
    return clipped;
}
void invalidateMovement(RECT oldArea, REAL previousSize, const Item& after) {
    RECT newArea = annotationBounds(after, app.layout), dirty{};
    UnionRect(&dirty, &oldArea, &newArea);
    if (previousSize != after.style.size)
        invalidate();
    else if (!IsRectEmpty(&dirty))
        InvalidateRect(app.main, &dirty, FALSE);
    // Present this cursor position immediately. Windows coalesces subsequent
    // mouse moves; never enqueue a timer or a frame for each intermediate point.
    if (!app.test && IsWindowVisible(app.main))
        UpdateWindow(app.main);
}
void mouseMove(PointF screen) {
    if (app.selectingText) {
        app.textArea = normalized(app.textStart, toImage(screen));
        invalidate();
        if (!app.test && IsWindowVisible(app.main))
            UpdateWindow(app.main);
        return;
    }
    if (app.scanningQr) {
        app.qrArea = normalized(app.qrStart, toImage(screen));
        invalidate();
        if (!app.test && IsWindowVisible(app.main))
            UpdateWindow(app.main);
        return;
    }
    if (!app.trackingMouse) {
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, app.main, 0};
        TrackMouseEvent(&track);
        app.trackingMouse = true;
    }
    if (!app.dragging && !app.panning && !app.resizingCanvas) {
        setHoverButton(buttonAt(screen, true));
        hoverCanvasHandle(canvasHandleAt(screen));
    }
    if (app.resizingCanvas) {
        resizeCanvas(screen);
        return;
    }
    if (app.sizing) {
        sizeAt(screen);
        return;
    }
    if (app.pressedButton >= 0)
        return;
    if (app.panning) {
        PointF requested = add(app.initialPan, sub(screen, app.dragStart));
        app.pan = requested;
        constrainPan();
        if (app.pan.X != requested.X || app.pan.Y != requested.Y) {
            // Rebase at the boundary so reversing the mouse responds immediately,
            // rather than making the user retrace an invisible overscroll distance.
            app.initialPan = app.pan;
            app.dragStart = screen;
        }
        presentNavigation();
        return;
    }
    if (!app.dragging || !validIndex(app.selected))
        return;
    PointF p = toImage(screen, false);
    Item& item = app.items[app.selected];
    if (item.tool == Tool::Redact) {
        if (app.creating)
            p = toImage(screen);
        p = {std::round(p.X), std::round(p.Y)};
    }
    RECT oldArea = annotationBounds(item, app.layout);
    REAL previousSize = item.style.size;
    if (app.creating && item.tool == Tool::Brush) {
        if (item.stroke.empty() || distance(p, item.stroke.back()) >= 0.5f / app.layout.zoom) {
            item.stroke.push_back(p);
            item.b = p;
            REAL left = std::min(item.strokeBounds.X, p.X),
                 top = std::min(item.strokeBounds.Y, p.Y);
            item.strokeBounds = {left, top, std::max(item.strokeBounds.GetRight(), p.X) - left,
                                 std::max(item.strokeBounds.GetBottom(), p.Y) - top};
        }
    } else if (app.creating) {
        item.b = p;
        if (keyPressed(VK_SHIFT)) {
            if (item.tool == Tool::Rectangle || item.tool == Tool::Redact) {
                REAL dx = p.X - item.a.X, dy = p.Y - item.a.Y,
                     v = std::min(std::abs(dx), std::abs(dy));
                item.b = {item.a.X + (dx < 0 ? -v : v), item.a.Y + (dy < 0 ? -v : v)};
            } else {
                REAL angle = std::round(std::atan2(p.Y - item.a.Y, p.X - item.a.X) / (Pi / 4)) *
                             (Pi / 4),
                     len = distance(item.a, p);
                item.b = {item.a.X + len * std::cos(angle), item.a.Y + len * std::sin(angle)};
            }
        }
    } else if (app.dragHandle < 0 || (item.tool == Tool::Text && app.dragHandle == 0) ||
               (item.tool == Tool::Counter && app.dragHandle == 0) ||
               (item.tool == Tool::Brush && item.stroke.size() < 2)) {
        PointF delta = sub(p, app.dragStart);
        if (item.tool == Tool::Brush) {
            item.stroke = app.initialItem.stroke;
            item.strokeBounds = app.initialItem.strokeBounds;
        }
        item.a = app.initialItem.a;
        item.b = app.initialItem.b;
        item.bend = app.initialItem.bend;
        moveItem(item, delta);
    } else if ((item.tool == Tool::Arrow || item.tool == Tool::Line) && app.dragHandle == 2) {
        item.bend = p;
        item.bent = distance(p, {(item.a.X + item.b.X) / 2, (item.a.Y + item.b.Y) / 2}) > 0.5f;
    } else if (item.tool == Tool::Arrow || item.tool == Tool::Line) {
        if (app.dragHandle == 0)
            item.a = p;
        else
            item.b = p;
    } else if (item.tool == Tool::Rectangle || item.tool == Tool::Redact) {
        RectF r = normalized(app.initialItem.a, app.initialItem.b);
        std::array<PointF, 4> corners = {PointF(r.X, r.Y), PointF(r.GetRight(), r.Y),
                                         PointF(r.GetRight(), r.GetBottom()),
                                         PointF(r.X, r.GetBottom())};
        item.a = corners[(app.dragHandle + 2) % 4];
        item.b = p;
    } else if (item.tool == Tool::Brush) {
        auto corners = handles(app.initialItem);
        PointF fixed = corners[(app.dragHandle + 2) % 4], moving = corners[app.dragHandle];
        REAL sx =
            std::abs(moving.X - fixed.X) > 0.001f ? (p.X - fixed.X) / (moving.X - fixed.X) : 1;
        REAL sy =
            std::abs(moving.Y - fixed.Y) > 0.001f ? (p.Y - fixed.Y) / (moving.Y - fixed.Y) : 1;
        item.stroke = app.initialItem.stroke;
        for (auto& point : item.stroke)
            point = {fixed.X + (point.X - fixed.X) * sx, fixed.Y + (point.Y - fixed.Y) * sy};
        item.strokeBounds = pointBounds(item.stroke);
        if (!item.stroke.empty()) {
            item.a = item.stroke.front();
            item.b = item.stroke.back();
        }
    } else if (item.tool == Tool::Text) {
        REAL oldWidth = textRect(app.initialItem).Width;
        item.style.size = std::clamp(app.initialItem.style.size * std::max(12.f, p.X - item.a.X) /
                                         std::max(1.f, oldWidth),
                                     8.f, 96.f);
    } else if (item.tool == Tool::Counter) {
        item.b = p;
        item.pointer = distance(item.a, p) > counterRadius(item) * 0.9f;
        if (item.pointer)
            item.b = counterTip(item);
    }
    if (growCanvas(item)) {
        if (!app.test && IsWindowVisible(app.main))
            UpdateWindow(app.main);
    } else
        invalidateMovement(oldArea, previousSize, item);
}
void mouseUp() {
    app.selectingText = false;
    app.scanningQr = false;
    app.pressedQr = -1;
    if (app.resizingCanvas) {
        RECT frame = canvasBounds();
        for (Item& item : app.items) {
            RectF ink = itemVisualBounds(item);
            item.cropped = ink.X < frame.left || ink.Y < frame.top ||
                           ink.GetRight() > frame.right || ink.GetBottom() > frame.bottom;
        }
    }
    bool discard = false;
    if (app.creating && validIndex(app.selected)) {
        const Item& item = app.items[app.selected];
        RectF area = normalized(item.a, item.b);
        discard = item.tool == Tool::Redact
                      ? area.Width < 1 || area.Height < 1
                      : item.tool != Tool::Brush && distance(item.a, item.b) < 2;
    }
    if (discard) {
        app.items.erase(app.items.begin() + app.selected);
        app.selected = -1;
    }
    if (app.dragging && validIndex(app.selected))
        app.items[app.selected].cropped = false;
    if (app.dragging && trimCanvasToItems())
        app.pinnedCanvas = true;
    app.dragging = false;
    app.creating = false;
    app.panning = false;
    app.sizing = false;
    app.dragHandle = -1;
    app.pressedButton = -1;
    app.resizingCanvas = false;
    app.canvasHandle = -1;
    app.hoverCanvas = -1;
    centerExpandedCanvas();
    dragBackground.reset();
    dragRevision = 0;
    cachedSelection = -1;
    if (GetCapture() == app.main)
        ReleaseCapture();
    invalidate();
}
void releasePointer(PointF screen) {
    if (app.selectingText) {
        mouseMove(screen);
        if (app.textArea.Width < 2 && app.textArea.Height < 2 && ocrReady()) {
            for (const auto& line : ocrReport()->lines)
                for (const auto& word : line.words)
                    if (word.bounds.Contains(app.textStart))
                        app.textArea = word.bounds;
        }
        mouseUp();
        copySelectedText();
        return;
    }
    if (app.scanningQr) {
        mouseMove(screen);
        mouseUp();
        scanQrArea();
        return;
    }
    if (app.pressedQr >= 0) {
        int index = app.pressedQr;
        mouseUp();
        if (index < static_cast<int>(app.qrCodes.size()) && qrAt(toImage(screen)) == index)
            activateQr(index, keyPressed(VK_CONTROL));
        return;
    }
    if (app.pressedButton >= 0) {
        int id = app.pressedButton;
        bool activate = id != 222 && buttonAt(screen, true) == id;
        if (app.sizing)
            sizeAt(screen);
        app.pressedButton = -1;
        mouseUp();
        setHoverButton(buttonAt(screen, true));
        invalidateButton(id);
        if (!app.test && IsWindowVisible(app.main))
            UpdateWindow(app.main);
        if (activate)
            perform(id, screen);
        return;
    }
    if (app.dragging || app.panning || app.sizing || app.resizingCanvas)
        mouseMove(screen);
    mouseUp();
}
void mouseRightDown(PointF screen) {
    if (buttonAt(screen) == 211) {
        commitText();
        perform(212);
    }
}

BOOL CALLBACK enumerateWindow(HWND hwnd, LPARAM) {
    if (hwnd == app.main || hwnd == app.overlay || !IsWindowVisible(hwnd) || IsIconic(hwnd))
        return TRUE;
    if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
        return TRUE;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return TRUE;
    RECT r{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))))
        GetWindowRect(hwnd, &r);
    RECT clipped{};
    if (IntersectRect(&clipped, &r, &app.desktop) && clipped.right - clipped.left > 5 &&
        clipped.bottom - clipped.top > 5) {
        OffsetRect(&clipped, -app.desktop.left, -app.desktop.top);
        app.windows.push_back({clipped});
    }
    return TRUE;
}
std::unique_ptr<Bitmap> captureDesktop(RECT bounds) {
    int w = bounds.right - bounds.left, h = bounds.bottom - bounds.top;
    HDC screen = GetDC(nullptr);
    if (!screen)
        return {};
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, w, h);
    if (!dc || !bitmap) {
        if (dc)
            DeleteDC(dc);
        if (bitmap)
            DeleteObject(bitmap);
        ReleaseDC(nullptr, screen);
        return {};
    }
    HGDIOBJ old = SelectObject(dc, bitmap);
    BOOL ok = BitBlt(dc, 0, 0, w, h, screen, bounds.left, bounds.top, SRCCOPY | CAPTUREBLT);
    SelectObject(dc, old);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    std::unique_ptr<Bitmap> result;
    if (ok)
        result.reset(Bitmap::FromHBITMAP(bitmap, nullptr));
    DeleteObject(bitmap);
    if (result && result->GetLastStatus() != Ok)
        result.reset();
    return result;
}
void hoverWindow(POINT p) {
    app.hover = -1;
    for (int i = 0; i < static_cast<int>(app.windows.size()); i++)
        if (PtInRect(&app.windows[i].rect, p)) {
            app.hover = i;
            break;
        }
}
RECT captureRect(POINT a, POINT b, RECT limit) {
    RECT r{std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)},
        result{};
    IntersectRect(&result, &r, &limit);
    return result;
}
void cancelCapture() {
    app.capturePending = false;
    app.selecting = false;
    if (app.overlay) {
        HWND hwnd = app.overlay;
        app.overlay = nullptr;
        DestroyWindow(hwnd);
    }
    if (app.main)
        KillTimer(app.main, 1);
    overlayCache.reset();
    app.frozen.reset();
    app.windows.clear();
    if (app.captureRestore && !app.test) {
        ShowWindow(app.main, SW_SHOW);
        SetForegroundWindow(app.main);
    }
}
void finishCapture(RECT r) {
    if (!app.frozen)
        return;
    RECT source{0, 0, static_cast<LONG>(app.frozen->GetWidth()),
                static_cast<LONG>(app.frozen->GetHeight())},
        frame{};
    if (!IntersectRect(&frame, &r, &source) || frame.right - frame.left < 2 ||
        frame.bottom - frame.top < 2)
        return;
    // Selection establishes a reversible frame in full-desktop coordinates.
    // Transfer the frozen bitmap without cropping or taking another screenshot.
    auto image = std::move(app.frozen);
    cancelCapture();
    captureView.reset();
    sourceRaster.reset();
    dragBackground.reset();
    app.image = std::move(image);
    app.canvas = {frame, frame, 0xffffffff};
    app.zooming = false;
    KillTimer(app.main, ZoomRefineTimer);
    app.pinnedCanvas = false;
    app.resizingCanvas = false;
    app.canvasHandle = -1;
    app.hoverCanvas = -1;
    edgeColors.reset();
    app.items.clear();
    app.undo.clear();
    app.redo.clear();
    app.selected = -1;
    app.userZoom = 0;
    app.pan = {0, 0};
    app.tool = Tool::Arrow;
    app.status = ui::kCaptureReady;
    startOcr();
    resetQr();
    if (!app.test) {
        ShowWindow(app.main, SW_RESTORE);
        SetForegroundWindow(app.main);
    }
    invalidate();
}
void takeFrozenCapture() {
    app.capturePending = false;
    KillTimer(app.main, 1);
    DwmFlush();
    app.desktop = {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 0, 0};
    app.desktop.right = app.desktop.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    app.desktop.bottom = app.desktop.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    app.windows.clear();
    EnumWindows(enumerateWindow, 0);
    app.frozen = captureDesktop(app.desktop);
    if (!app.frozen) {
        cancelCapture();
        error(app.main, ui::kErrorCapture);
        return;
    }
    app.windowMode = false;
    app.selecting = false;
    app.hover = -1;
    app.selection = {};
    app.overlay = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, OverlayClass, ui::kSelectWindowTitle, WS_POPUP,
        app.desktop.left, app.desktop.top, app.desktop.right - app.desktop.left,
        app.desktop.bottom - app.desktop.top, nullptr, nullptr, app.instance, nullptr);
    if (!app.overlay) {
        cancelCapture();
        error(app.main, ui::kErrorSelector);
        return;
    }
    ShowWindow(app.overlay, SW_SHOW);
    SetForegroundWindow(app.overlay);
    SetFocus(app.overlay);
}
void beginCapture() {
    if (app.overlay || app.capturePending || app.shortcutDialog)
        return;
    commitText();
    if (app.tooltip)
        SendMessageW(app.tooltip, TTM_POP, 0, 0);
    app.captureRestore = IsWindowVisible(app.main) != FALSE;
    ShowWindow(app.main, SW_HIDE);
    app.capturePending = true;
    // Let the compositor remove our own editor before freezing the desktop.
    if (!SetTimer(app.main, 1, 160, nullptr))
        takeFrozenCapture();
}
RECT overlaySelection() {
    return app.windowMode ? (app.hover >= 0 ? app.windows[app.hover].rect : RECT{}) : app.selection;
}
RectF overlayBadge(RECT selected) {
    REAL width = static_cast<REAL>(app.frozen->GetWidth());
    return {std::clamp(static_cast<REAL>(selected.left), 0.f, std::max(0.f, width - 180)),
            std::max(0.f, static_cast<REAL>(selected.top - 32)), 180, 29};
}
RectF overlayHint(HWND hwnd) {
    POINT cursor;
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(monitor, &mi);
#ifdef CAPTURA_TEST
    if (app.test)
        mi.rcMonitor = {app.desktop.left, app.desktop.top, app.desktop.right, app.desktop.bottom};
#endif
    REAL scale = GetDpiForWindow(hwnd) / 96.f, pillWidth = 650 * scale;
    REAL x = static_cast<REAL>(mi.rcMonitor.left - app.desktop.left) +
             (mi.rcMonitor.right - mi.rcMonitor.left - pillWidth) / 2;
    REAL y = static_cast<REAL>(mi.rcMonitor.bottom - app.desktop.top) - 82 * scale;
    return {x, y, pillWidth, 43 * scale};
}
RECT pixelRect(RectF r, int margin = 0) {
    return {static_cast<LONG>(std::floor(r.X)) - margin,
            static_cast<LONG>(std::floor(r.Y)) - margin,
            static_cast<LONG>(std::ceil(r.GetRight())) + margin,
            static_cast<LONG>(std::ceil(r.GetBottom())) + margin};
}
void drawOverlayForeground(Graphics& g, HWND hwnd) {
    RECT selected = overlaySelection();
    if (selected.right > selected.left && selected.bottom > selected.top) {
        if (app.windowMode) {
            SolidBrush tint(Color(65, 0, 117, 255));
            g.FillRectangle(&tint, Rect(int(selected.left), int(selected.top),
                                        int(selected.right - selected.left),
                                        int(selected.bottom - selected.top)));
        }
        Pen border(Blue, 2);
        g.DrawRectangle(&border, Rect(int(selected.left), int(selected.top),
                                      int(selected.right - selected.left - 1),
                                      int(selected.bottom - selected.top - 1)));
        std::wstring size = std::to_wstring(selected.right - selected.left) + L" × " +
                            std::to_wstring(selected.bottom - selected.top) + L" px";
        RectF badgeRect = overlayBadge(selected);
        SolidBrush badge(Color(240, 25, 36, 51));
        g.FillRectangle(&badge, badgeRect);
        drawText(g, size, {badgeRect.X + 8, badgeRect.Y, 164, 29}, 12, Color(255, 255, 255, 255));
    }
    REAL scale = GetDpiForWindow(hwnd) / 96.f;
    RectF hint = overlayHint(hwnd);
    SolidBrush pill(Color(245, 28, 39, 54));
    g.FillRectangle(&pill, hint);
    drawText(g, app.windowMode ? ui::kWindowHint : ui::kAreaHint,
             {hint.X + 14 * scale, hint.Y, hint.Width - 28 * scale, hint.Height}, 13 * scale,
             Color(255, 255, 255, 255), false, true);
}
void renderOverlay(Graphics& g, HWND hwnd) {
    if (!app.frozen)
        return;
    REAL width = static_cast<REAL>(app.frozen->GetWidth()),
         height = static_cast<REAL>(app.frozen->GetHeight());
    g.SetInterpolationMode(InterpolationModeNearestNeighbor);
    g.DrawImage(app.frozen.get(), 0.f, 0.f, width, height);
    SolidBrush dark(Color(115, 10, 20, 34));
    g.FillRectangle(&dark, 0.f, 0.f, width, height);
    RECT selected = overlaySelection();
    if (!IsRectEmpty(&selected))
        g.DrawImage(app.frozen.get(),
                    Rect(selected.left, selected.top, selected.right - selected.left,
                         selected.bottom - selected.top),
                    selected.left, selected.top, selected.right - selected.left,
                    selected.bottom - selected.top, UnitPixel);
    drawOverlayForeground(g, hwnd);
}
bool prepareOverlay(HDC dc, int width, int height) {
    if (overlayCache.source == app.frozen.get() && overlayCache.original.width == width &&
        overlayCache.original.height == height)
        return true;
    overlayCache.reset();
    if (!overlayCache.original.ensure(dc, width, height) ||
        !overlayCache.dark.ensure(dc, width, height))
        return false;
    {
        Graphics g(overlayCache.original.dc);
        g.SetInterpolationMode(InterpolationModeNearestNeighbor);
        g.DrawImage(app.frozen.get(), 0, 0, width, height);
    }
    BitBlt(overlayCache.dark.dc, 0, 0, width, height, overlayCache.original.dc, 0, 0, SRCCOPY);
    {
        Graphics g(overlayCache.dark.dc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        SolidBrush dark(Color(115, 10, 20, 34));
        g.FillRectangle(&dark, 0, 0, width, height);
    }
    overlayCache.source = app.frozen.get();
    ++overlayCache.builds;
    return true;
}
HRGN overlayDamage(RECT previous, RECT next, RECT oldHint, RECT newHint) {
    HRGN oldRegion = CreateRectRgnIndirect(&previous), newRegion = CreateRectRgnIndirect(&next),
         damage = CreateRectRgn(0, 0, 0, 0);
    CombineRgn(damage, oldRegion, newRegion, RGN_XOR);
    DeleteObject(oldRegion);
    DeleteObject(newRegion);
    auto include = [&](RECT r) {
        HRGN part = CreateRectRgnIndirect(&r);
        CombineRgn(damage, damage, part, RGN_OR);
        DeleteObject(part);
    };
    for (RECT r : {previous, next})
        if (!IsRectEmpty(&r)) {
            RECT outer = r;
            InflateRect(&outer, 3, 3);
            RECT inner = r;
            InflateRect(&inner, -3, -3);
            HRGN frame = CreateRectRgnIndirect(&outer), hole = IsRectEmpty(&inner)
                                                                   ? CreateRectRgn(0, 0, 0, 0)
                                                                   : CreateRectRgnIndirect(&inner);
            CombineRgn(frame, frame, hole, RGN_DIFF);
            CombineRgn(damage, damage, frame, RGN_OR);
            DeleteObject(frame);
            DeleteObject(hole);
            include(pixelRect(overlayBadge(r), 2));
        }
    if (!EqualRect(&oldHint, &newHint)) {
        include(oldHint);
        include(newHint);
    }
    return damage;
}
void updateOverlay(HWND hwnd, POINT point) {
    RECT previous = overlaySelection(), oldHint = overlayCache.hint;
    if (app.windowMode)
        hoverWindow(point);
    else if (app.selecting) {
        RECT limit{0, 0, static_cast<LONG>(app.frozen->GetWidth()),
                   static_cast<LONG>(app.frozen->GetHeight())};
        app.selection = captureRect(
            {static_cast<LONG>(app.dragStart.X), static_cast<LONG>(app.dragStart.Y)}, point, limit);
    }
    RECT next = overlaySelection(), newHint = pixelRect(overlayHint(hwnd), 2);
    overlayCache.hint = newHint;
    if (EqualRect(&previous, &next) && EqualRect(&oldHint, &newHint))
        return;
    HRGN damage = overlayDamage(previous, next, oldHint, newHint);
    InvalidateRgn(hwnd, damage, FALSE);
    DeleteObject(damage);
    if (!app.test && IsWindowVisible(hwnd))
        UpdateWindow(hwnd);
}
void paintOverlay(HWND hwnd, HDC supplied) {
    if (!app.frozen)
        return;
    HRGN damage = CreateRectRgn(0, 0, 0, 0);
    if (!supplied)
        GetUpdateRgn(hwnd, damage, FALSE);
    PAINTSTRUCT ps{};
    HDC dc = supplied ? supplied : BeginPaint(hwnd, &ps);
    if (!dc) {
        DeleteObject(damage);
        return;
    }
    RECT dirty = supplied ? RECT{} : ps.rcPaint;
    if (supplied) {
        GetClipBox(dc, &dirty);
        if (GetClipRgn(dc, damage) != 1)
            SetRectRgn(damage, dirty.left, dirty.top, dirty.right, dirty.bottom);
    }
    RECT client;
    GetClientRect(hwnd, &client);
    int width = client.right, height = client.bottom;
    if (overlayBuffer.ensure(dc, width, height) && !IsRectEmpty(&dirty)) {
        if (prepareOverlay(dc, width, height)) {
            int saved = SaveDC(overlayBuffer.dc);
            SelectClipRgn(overlayBuffer.dc, damage);
            BitBlt(overlayBuffer.dc, dirty.left, dirty.top, dirty.right - dirty.left,
                   dirty.bottom - dirty.top, overlayCache.dark.dc, dirty.left, dirty.top, SRCCOPY);
            RECT selected = overlaySelection(), clear{};
            if (IntersectRect(&clear, &selected, &dirty))
                BitBlt(overlayBuffer.dc, clear.left, clear.top, clear.right - clear.left,
                       clear.bottom - clear.top, overlayCache.original.dc, clear.left, clear.top,
                       SRCCOPY);
            {
                Graphics g(overlayBuffer.dc);
                g.SetSmoothingMode(SmoothingModeAntiAlias);
                Region region(damage);
                g.SetClip(&region);
                drawOverlayForeground(g, hwnd);
            }
            RestoreDC(overlayBuffer.dc, saved);
        } else {
            Graphics g(overlayBuffer.dc);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            renderOverlay(g, hwnd);
        }
        overlayCache.hint = pixelRect(overlayHint(hwnd), 2);
        BitBlt(dc, dirty.left, dirty.top, dirty.right - dirty.left, dirty.bottom - dirty.top,
               overlayBuffer.dc, dirty.left, dirty.top, SRCCOPY);
    }
    DeleteObject(damage);
    if (!supplied)
        EndPaint(hwnd, &ps);
}
void paint(HWND hwnd, bool overlay, HDC supplied = nullptr) {
    if (overlay) {
        paintOverlay(hwnd, supplied);
        return;
    }
    PAINTSTRUCT ps{};
    HDC dc = supplied ? supplied : BeginPaint(hwnd, &ps);
    if (!dc)
        return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = std::max(1L, rc.right), h = std::max(1L, rc.bottom);
    PaintBuffer& buffer = editorBuffer;
    RECT dirty = supplied ? RECT{0, 0, w, h} : ps.rcPaint;
    if (supplied)
        GetClipBox(dc, &dirty);
    if (buffer.ensure(dc, w, h) && !IsRectEmpty(&dirty)) {
        sourceRaster.ensure(app.image.get());
        bool moving = app.dragging && validIndex(app.selected) && !app.edit;
        bool navigating = app.image && (app.panning || app.zooming);
        Layout layout = moving || navigating ? calculateLayout(hwnd) : Layout{};
        bool fast = moving && dragBackground.ensure(dc, w, h);
        if (fast) {
            if (dragRevision != sceneRevision || cachedSelection != app.selected ||
                !sameView(dragLayout, layout)) {
                Graphics g(dragBackground.dc);
                g.SetSmoothingMode(SmoothingModeAntiAlias);
                renderEditor(g, hwnd, app.selected, false, true);
                dragRevision = sceneRevision;
                cachedSelection = app.selected;
                dragLayout = app.layout;
            }
            // The backdrop contains only earlier annotations. Later objects
            // are drawn after the moving object to retain the original z-order.
            BitBlt(buffer.dc, dirty.left, dirty.top, dirty.right - dirty.left,
                   dirty.bottom - dirty.top, dragBackground.dc, dirty.left, dirty.top, SRCCOPY);
            Graphics g(buffer.dc);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetClip(
                Rect(dirty.left, dirty.top, dirty.right - dirty.left, dirty.bottom - dirty.top));
            app.layout = layout;
            drawAnnotations(g, layout, app.selected, static_cast<int>(app.items.size()));
            drawSelection(g, layout);
            drawCanvasHandles(g, layout);
        } else if (navigating && navigationBackground.ensure(dc, w, h)) {
            // Toolbars do not change with the camera. Keep their pixels and
            // redraw only the image, annotations and handles during navigation.
            if (navigationRevision != sceneRevision || navigationLayout.width != layout.width ||
                navigationLayout.height != layout.height ||
                navigationLayout.scale != layout.scale || navigationHover != app.hoverButton ||
                navigationPressed != app.pressedButton) {
                Graphics chrome(navigationBackground.dc);
                chrome.SetSmoothingMode(SmoothingModeAntiAlias);
                renderEditor(chrome, hwnd, -1, false, true, true);
                navigationRevision = sceneRevision;
                navigationLayout = layout;
                navigationHover = app.hoverButton;
                navigationPressed = app.pressedButton;
            }
            BitBlt(buffer.dc, dirty.left, dirty.top, dirty.right - dirty.left,
                   dirty.bottom - dirty.top, navigationBackground.dc, dirty.left, dirty.top,
                   SRCCOPY);
            Graphics g(buffer.dc);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetClip(
                Rect(dirty.left, dirty.top, dirty.right - dirty.left, dirty.bottom - dirty.top));
            app.layout = layout;
            drawCapture(g, layout, true);
            drawAnnotations(g, layout, 0, static_cast<int>(app.items.size()));
            drawSelection(g, layout);
            drawCanvasHandles(g, layout);
            drawOcrOverlay(g, layout);
            drawQrOverlay(g, layout);
        } else {
            Graphics g(buffer.dc);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            renderEditor(g, hwnd, -1, true, true);
        }
        BitBlt(dc, dirty.left, dirty.top, dirty.right - dirty.left, dirty.bottom - dirty.top,
               buffer.dc, dirty.left, dirty.top, SRCCOPY);
    }
    if (!supplied)
        EndPaint(hwnd, &ps);
}
LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paint(hwnd, true);
        return 0;
    case WM_PRINTCLIENT:
        paint(hwnd, true, reinterpret_cast<HDC>(w));
        return 0;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) {
            cancelCapture();
            return 0;
        }
        if (w == VK_SPACE && !(l & (1LL << 30))) {
            app.windowMode = !app.windowMode;
            app.selecting = false;
            app.selection = {};
            if (GetCapture() == hwnd)
                ReleaseCapture();
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(hwnd, &p);
            hoverWindow(p);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_LBUTTONDOWN:
        if (app.windowMode) {
            hoverWindow(point);
            if (app.hover >= 0)
                finishCapture(app.windows[app.hover].rect);
            return 0;
        }
        app.selecting = true;
        app.dragStart = {static_cast<REAL>(point.x), static_cast<REAL>(point.y)};
        SetCapture(hwnd);
        updateOverlay(hwnd, point);
        return 0;
    case WM_MOUSEMOVE:
        updateOverlay(hwnd, point);
        return 0;
    case WM_LBUTTONUP:
        if (app.selecting) {
            updateOverlay(hwnd, point);
            app.selecting = false;
            ReleaseCapture();
            finishCapture(app.selection);
        }
        return 0;
    case WM_RBUTTONDOWN:
    case WM_CLOSE:
        cancelCapture();
        return 0;
    case WM_DISPLAYCHANGE:
        cancelCapture();
        return 0;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, app.windowMode ? IDC_HAND : IDC_CROSS));
        return TRUE;
    case WM_KILLFOCUS:
        if (app.overlay == hwnd)
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
    case WM_DESTROY:
        overlayBuffer.reset();
        overlayCache.reset();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}
void addTray() {
#ifdef CAPTURA_TEST
    if (!app.lifecycleDirectory.empty()) {
        app.tray = !app.lifecycleNoTray;
        return;
    }
#endif
    NOTIFYICONDATAW n{};
    n.cbSize = sizeof(n);
    n.hWnd = app.main;
    n.uID = 1;
    n.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    n.uCallbackMessage = TrayMessage;
    n.hIcon = app.icon;
    std::wstring tip =
        std::wstring(ui::kAppName) + L" | " + captureShortcutName(app.captureShortcut);
    wcsncpy_s(n.szTip, tip.c_str(), _TRUNCATE);
    app.tray = Shell_NotifyIconW(NIM_ADD, &n) != FALSE;
}
void removeTray() {
#ifdef CAPTURA_TEST
    if (!app.lifecycleDirectory.empty()) {
        app.tray = false;
        return;
    }
#endif
    NOTIFYICONDATAW n{};
    n.cbSize = sizeof(n);
    n.hWnd = app.main;
    n.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &n);
    app.tray = false;
}
void updateCaptureShortcutLabels() {
    NOTIFYICONDATAW n{};
    n.cbSize = sizeof(n);
    n.hWnd = app.main;
    n.uID = 1;
    n.uFlags = NIF_TIP;
    std::wstring tip =
        std::wstring(ui::kAppName) + L" | " + captureShortcutName(app.captureShortcut);
    wcsncpy_s(n.szTip, tip.c_str(), _TRUNCATE);
    if (app.tray)
        Shell_NotifyIconW(NIM_MODIFY, &n);
    report(captureHint().c_str());
}

struct ShortcutDialogState {
    CaptureShortcut candidate;
    bool valid = true;
    HHOOK recorder = nullptr;
    std::array<bool, 256> swallowed{};
    bool leftWin = false, rightWin = false;
} shortcutDialogState;
constexpr UINT RecordShortcutMessage = WM_APP + 3;

void setShortcutCandidate(HWND dialog, CaptureShortcut shortcut) {
    shortcutDialogState.candidate = shortcut;
    shortcutDialogState.valid = validCaptureShortcut(shortcut);
    SetDlgItemTextW(dialog, IDC_SHORTCUT_INPUT, captureShortcutName(shortcut).c_str());
    SetDlgItemTextW(dialog, IDC_SHORTCUT_HINT,
                    shortcutDialogState.valid ? ui::kShortcutInstructions : ui::kShortcutInvalid);
    EnableWindow(GetDlgItem(dialog, IDOK), shortcutDialogState.valid);
}

UINT shortcutModifiers() {
    return (keyPressed(VK_CONTROL) ? MOD_CONTROL : 0) | (keyPressed(VK_MENU) ? MOD_ALT : 0) |
           (keyPressed(VK_SHIFT) ? MOD_SHIFT : 0) |
           ((keyPressed(VK_LWIN) || keyPressed(VK_RWIN)) ? MOD_WIN : 0);
}

bool shortcutNavigationKey(UINT key, UINT modifiers) {
    return key == VK_ESCAPE || (!modifiers && (key == VK_TAB || key == VK_RETURN)) ||
           (key == VK_TAB && modifiers == MOD_SHIFT);
}

void recordShortcutKey(HWND dialog, UINT key, UINT modifiers) {
    if (key == VK_CONTROL || key == VK_SHIFT || key == VK_MENU || key == VK_LWIN || key == VK_RWIN)
        return;
    setShortcutCandidate(dialog, {modifiers, key});
}

// A recorder exists only while this dialog is open. Swallowing the recorded key keeps
// another application's shortcut from firing and lets us display even an occupied combination.
LRESULT CALLBACK ShortcutRecorder(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION) {
        auto* event = reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
        UINT key = event->vkCode;
        bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        if (key < shortcutDialogState.swallowed.size()) {
            if (!down && shortcutDialogState.swallowed[key]) {
                shortcutDialogState.swallowed[key] = false;
                if (key == VK_LWIN)
                    shortcutDialogState.leftWin = false;
                if (key == VK_RWIN)
                    shortcutDialogState.rightWin = false;
                return 1;
            }
            HWND dialog = app.shortcutDialog;
            if (dialog && GetForegroundWindow() == dialog &&
                GetFocus() == GetDlgItem(dialog, IDC_SHORTCUT_INPUT)) {
                if (key == VK_LWIN || key == VK_RWIN) {
                    (key == VK_LWIN ? shortcutDialogState.leftWin : shortcutDialogState.rightWin) =
                        down;
                    shortcutDialogState.swallowed[key] = down;
                    return 1;
                }
                UINT modifiers =
                    (GetAsyncKeyState(VK_CONTROL) & 0x8000 ? MOD_CONTROL : 0) |
                    (GetAsyncKeyState(VK_MENU) & 0x8000 ? MOD_ALT : 0) |
                    (GetAsyncKeyState(VK_SHIFT) & 0x8000 ? MOD_SHIFT : 0) |
                    (shortcutDialogState.leftWin || shortcutDialogState.rightWin ? MOD_WIN : 0);
                bool modifier = key == VK_LCONTROL || key == VK_RCONTROL || key == VK_CONTROL ||
                                key == VK_LSHIFT || key == VK_RSHIFT || key == VK_SHIFT ||
                                key == VK_LMENU || key == VK_RMENU || key == VK_MENU;
                if (down && !modifier && !shortcutNavigationKey(key, modifiers)) {
                    shortcutDialogState.swallowed[key] = true;
                    PostMessageW(dialog, RecordShortcutMessage, key, modifiers);
                    return 1;
                }
            }
        }
    }
    return CallNextHookEx(nullptr, code, message, data);
}

LRESULT CALLBACK ShortcutFieldProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    HWND dialog = GetParent(hwnd);
    if (msg == WM_GETDLGCODE) {
        UINT modifiers = shortcutModifiers();
        if (!shortcutNavigationKey(static_cast<UINT>(w), modifiers))
            return DLGC_WANTALLKEYS;
    }
    if (msg == WM_SETFOCUS) {
        SetWindowTextW(hwnd, ui::kShortcutPrompt);
        shortcutDialogState.leftWin = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0;
        shortcutDialogState.rightWin = (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
    } else if (msg == WM_KILLFOCUS) {
        SetWindowTextW(hwnd, captureShortcutName(shortcutDialogState.candidate).c_str());
    } else if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        UINT modifiers = shortcutModifiers();
        if (!shortcutNavigationKey(static_cast<UINT>(w), modifiers)) {
            recordShortcutKey(dialog, static_cast<UINT>(w), modifiers);
            return 0;
        }
    } else if (msg == WM_CHAR || msg == WM_SYSCHAR || msg == WM_SYSKEYUP) {
        return 0;
    } else if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, ShortcutFieldProc, 1);
    }
    return DefSubclassProc(hwnd, msg, w, l);
}

void centerShortcutDialog(HWND dialog) {
    RECT frame{};
    GetWindowRect(dialog, &frame);
    POINT cursor{};
    GetCursorPos(&cursor);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);
    RECT anchor = monitor.rcWork;
    if (IsWindowVisible(app.main))
        GetWindowRect(app.main, &anchor);
    int width = frame.right - frame.left, height = frame.bottom - frame.top;
    int x = std::clamp(static_cast<int>((anchor.left + anchor.right - width) / 2),
                       static_cast<int>(monitor.rcWork.left),
                       std::max(static_cast<int>(monitor.rcWork.left),
                                static_cast<int>(monitor.rcWork.right) - width));
    int y = std::clamp(static_cast<int>((anchor.top + anchor.bottom - height) / 2),
                       static_cast<int>(monitor.rcWork.top),
                       std::max(static_cast<int>(monitor.rcWork.top),
                                static_cast<int>(monitor.rcWork.bottom) - height));
    SetWindowPos(dialog, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

INT_PTR CALLBACK ShortcutDialogProc(HWND dialog, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_INITDIALOG:
        app.shortcutDialog = dialog;
        shortcutDialogState = {};
        SetWindowTextW(dialog, ui::kCaptureShortcutTitle);
        SetDlgItemTextW(dialog, IDC_SHORTCUT_LABEL, ui::kCaptureShortcutLabel);
        SetDlgItemTextW(dialog, IDC_SHORTCUT_RESET, ui::kResetShortcut);
        SetDlgItemTextW(dialog, IDCANCEL, ui::kCancel);
        SetDlgItemTextW(dialog, IDOK, ui::kSavePreference);
        SetWindowSubclass(GetDlgItem(dialog, IDC_SHORTCUT_INPUT), ShortcutFieldProc, 1, 0);
        setShortcutCandidate(dialog, app.captureShortcut);
        centerShortcutDialog(dialog);
        if (!app.test)
            shortcutDialogState.recorder =
                SetWindowsHookExW(WH_KEYBOARD_LL, ShortcutRecorder, app.instance, 0);
        // Start with the saved combination visible. Tab or clicking the field begins recording.
        if (!app.test)
            SetFocus(GetDlgItem(dialog, IDOK));
#ifdef CAPTURA_TEST
        if (app.shortcutPreview)
            SetTimer(dialog, 99, 50, nullptr);
#endif
        return FALSE;
#ifdef CAPTURA_TEST
    case WM_TIMER:
        if (w == 99) {
            KillTimer(dialog, 99);
            ShowWindow(dialog, SW_SHOW);
            SetForegroundWindow(dialog);
        }
        return TRUE;
#endif
    case RecordShortcutMessage:
        recordShortcutKey(dialog, static_cast<UINT>(w), static_cast<UINT>(l));
        return TRUE;
    case WM_NOTIFY: {
        auto* notification = reinterpret_cast<NMHDR*>(l);
        if (notification->idFrom == IDC_SHORTCUT_RESET &&
            (notification->code == NM_CLICK || notification->code == NM_RETURN)) {
            setShortcutCandidate(dialog, {});
            return TRUE;
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(w) == IDCANCEL) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        if (LOWORD(w) == IDOK) {
            if (!shortcutDialogState.valid)
                return TRUE;
            ShortcutSaveResult result = replaceCaptureShortcut(
                app.captureShortcut, app.hotkey, app.hotkeyId, shortcutDialogState.candidate,
                [](int id, CaptureShortcut shortcut) {
#ifdef CAPTURA_TEST
                    if (app.test)
                        return true;
#endif
                    return RegisterHotKey(app.main, id, shortcut.modifiers | MOD_NOREPEAT,
                                          shortcut.key) != FALSE;
                },
                [](int id) {
#ifdef CAPTURA_TEST
                    if (app.test)
                        return;
#endif
                    UnregisterHotKey(app.main, id);
                },
                [](CaptureShortcut shortcut) {
                    return persistCaptureShortcut(app.settingsPath, shortcut);
                });
            if (result != ShortcutSaveResult::Saved) {
                SetDlgItemTextW(dialog, IDC_SHORTCUT_HINT,
                                result == ShortcutSaveResult::Unavailable
                                    ? ui::kShortcutUnavailable
                                    : ui::kShortcutPersistenceError);
                return TRUE;
            }
            updateCaptureShortcutLabels();
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    case WM_DESTROY:
        if (shortcutDialogState.recorder)
            UnhookWindowsHookEx(shortcutDialogState.recorder);
        shortcutDialogState.recorder = nullptr;
        app.shortcutDialog = nullptr;
        return TRUE;
    }
    return FALSE;
}

void showCaptureShortcutDialog() {
    if (app.shortcutDialog) {
        SetForegroundWindow(app.shortcutDialog);
        return;
    }
    if (app.capturePending || app.overlay)
        return;
    commitText();
    if (DialogBoxParamW(app.instance, MAKEINTRESOURCEW(IDD_CAPTURE_SHORTCUT), app.main,
                        ShortcutDialogProc, 0) == -1)
        error(app.main, ui::kShortcutDialogError);
}

void trayMenu() {
    HMENU menu = CreatePopupMenu();
    if (app.managed) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Managed by WinKit");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    std::wstring captureLabel =
        std::wstring(ui::kNewCapture) + L"\t" + captureShortcutName(app.captureShortcut);
    AppendMenuW(menu, MF_STRING, 1, captureLabel.c_str());
    AppendMenuW(menu, MF_STRING, 2, ui::kOpenEditor);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 4, ui::kCaptureShortcutMenu);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 3, ui::kExit);
    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(app.main);
    int id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, p.x, p.y, 0,
                            app.main, nullptr);
    DestroyMenu(menu);
    if (id == 1)
        beginCapture();
    if (id == 2) {
        ShowWindow(app.main, SW_RESTORE);
        SetForegroundWindow(app.main);
    }
    if (id == 3)
        DestroyWindow(app.main);
    if (id == 4)
        showCaptureShortcutDialog();
    PostMessageW(app.main, WM_NULL, 0, 0);
}
void keyDown(WPARAM key, bool ctrl = keyPressed(VK_CONTROL), bool shift = keyPressed(VK_SHIFT)) {
    if (ctrl) {
        if (key == 'C')
            copyImage();
        else if (key == 'S')
            saveImage(shift);
        else if (key == 'Z')
            undo(shift);
        else if (key == 'Y')
            undo(true);
        else if (key == '0')
            perform(250);
        else if (key == '1')
            perform(251);
        else if (key == 'D' && validIndex(app.selected)) {
            checkpoint();
            Item item = app.items[app.selected];
            moveItem(item, {14, 14});
            app.items.push_back(item);
            app.selected = static_cast<int>(app.items.size()) - 1;
            invalidate();
        }
        return;
    }
    if (key == VK_ESCAPE) {
        if (app.selectingText || (app.tool == Tool::TextSelect && app.hasTextSelection)) {
            app.hasTextSelection = false;
            mouseUp();
            app.textFeedback.clear();
            return;
        }
        if (app.scanningQr ||
            (app.tool == Tool::QrScan && (app.hasQrArea || !app.qrCodes.empty()))) {
            mouseUp();
            resetQr();
            invalidate();
            return;
        }
        if (app.resizingCanvas) {
            mouseUp();
            undo(false);
            return;
        }
        if (app.pressedButton >= 0) {
            app.pressedButton = -1;
            mouseUp();
        }
        if (app.dragging) {
            mouseUp();
            undo(false);
        }
        app.selected = -1;
        invalidate();
        return;
    }
    if (key == VK_DELETE || key == VK_BACK) {
        perform(240);
        return;
    }
    if (key == VK_TAB && !app.items.empty()) {
        app.selected = (app.selected + (shift ? -1 : 1) + static_cast<int>(app.items.size())) %
                       static_cast<int>(app.items.size());
        app.tool = Tool::Select;
        app.fillTarget =
            app.items[app.selected].tool == Tool::Text && app.items[app.selected].style.filled;
        invalidate();
        return;
    }
    if (key == VK_F1) {
        perform(254);
        return;
    }
    if (key == VK_ADD || key == VK_OEM_PLUS) {
        perform(221);
        return;
    }
    if (key == VK_SUBTRACT || key == VK_OEM_MINUS) {
        perform(220);
        return;
    }
    if (validIndex(app.selected) && key >= VK_LEFT && key <= VK_DOWN) {
        checkpoint();
        REAL d = shift ? 10.f : 1.f;
        PointF delta{key == VK_LEFT ? -d : (key == VK_RIGHT ? d : 0),
                     key == VK_UP ? -d : (key == VK_DOWN ? d : 0)};
        moveItem(app.items[app.selected], delta);
        invalidate();
        return;
    }
    switch (key) {
    case 'V':
        setTool(Tool::Select);
        break;
    case 'A':
        setTool(Tool::Arrow);
        break;
    case 'T':
        setTool(Tool::Text);
        break;
    case 'R':
        setTool(Tool::Rectangle);
        break;
    case 'L':
        setTool(Tool::Line);
        break;
    case 'C':
        setTool(Tool::Counter);
        break;
    case 'P':
        setTool(Tool::Brush);
        break;
    case 'O':
        setTool(Tool::TextSelect);
        break;
    case 'Q':
        setTool(Tool::QrScan);
        break;
    case 'B':
        setTool(Tool::Redact);
        break;
    }
}
void captureHotkey(bool editorActive) {
    if (app.shortcutDialog)
        return;
    if (editorActive && app.image && app.captureShortcut == CaptureShortcut{})
        saveImage(true);
    else
        beginCapture();
}
LRESULT CALLBACK EditorProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    static UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (msg == taskbarCreated && !app.test) {
        addTray();
        return 0;
    }
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paint(hwnd, false);
        return 0;
    case WM_PRINTCLIENT:
        paint(hwnd, false, reinterpret_cast<HDC>(w));
        return 0;
    case WM_GETMINMAXINFO: {
        auto* m = reinterpret_cast<MINMAXINFO*>(l);
        REAL s = GetDpiForWindow(hwnd) / 96.f;
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        m->ptMinTrackSize = {
            std::min(static_cast<LONG>(760 * s), mi.rcWork.right - mi.rcWork.left),
            std::min(static_cast<LONG>(540 * s), mi.rcWork.bottom - mi.rcWork.top)};
        return 0;
    }
    case WM_SIZE:
        constrainPan();
        positionTextEditor();
        invalidate();
        return 0;
    case WM_DPICHANGED: {
        auto* r = reinterpret_cast<RECT*>(l);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        positionTextEditor();
        invalidate();
        return 0;
    }
    case WM_LBUTTONDOWN:
        mouseDown({static_cast<REAL>(GET_X_LPARAM(l)), static_cast<REAL>(GET_Y_LPARAM(l))});
        return 0;
    case WM_MOUSEMOVE:
        mouseMove({static_cast<REAL>(GET_X_LPARAM(l)), static_cast<REAL>(GET_Y_LPARAM(l))});
        return 0;
    case WM_LBUTTONUP:
        releasePointer({static_cast<REAL>(GET_X_LPARAM(l)), static_cast<REAL>(GET_Y_LPARAM(l))});
        return 0;
    case WM_RBUTTONDOWN:
        mouseRightDown({static_cast<REAL>(GET_X_LPARAM(l)), static_cast<REAL>(GET_Y_LPARAM(l))});
        return 0;
    case WM_MOUSELEAVE:
        app.trackingMouse = false;
        setHoverButton(-1);
        hoverCanvasHandle(-1);
        return 0;
    case WM_LBUTTONDBLCLK: {
        PointF screen{static_cast<REAL>(GET_X_LPARAM(l)), static_cast<REAL>(GET_Y_LPARAM(l))};
        if (onImage(screen)) {
            int index = hitTest(toImage(screen));
            if (validIndex(index) && app.items[index].tool == Tool::Text)
                startTextEdit(index);
        }
        return 0;
    }
    case WM_MBUTTONDOWN:
        if (app.selectingText || app.scanningQr)
            return 0;
        if (contains(app.layout.canvas,
                     {static_cast<REAL>(GET_X_LPARAM(l)), static_cast<REAL>(GET_Y_LPARAM(l))})) {
            app.panning = true;
            app.dragStart = {static_cast<REAL>(GET_X_LPARAM(l)),
                             static_cast<REAL>(GET_Y_LPARAM(l))};
            app.initialPan = app.pan;
            SetCapture(hwnd);
        }
        return 0;
    case WM_MBUTTONUP:
        mouseUp();
        return 0;
    case WM_CAPTURECHANGED:
        if (app.selectingText || app.scanningQr || app.pressedQr >= 0) {
            app.selectingText = app.hasTextSelection = false;
            app.scanningQr = app.hasQrArea = false;
            app.pressedQr = -1;
            invalidate();
        }
        if (app.dragging || app.panning || app.sizing || app.pressedButton >= 0 ||
            app.resizingCanvas) {
            if (app.resizingCanvas) {
                mouseUp();
                return 0;
            }
            if (app.dragging && trimCanvasToItems())
                app.pinnedCanvas = true;
            app.dragging = false;
            app.creating = false;
            app.panning = false;
            app.sizing = false;
            app.pressedButton = -1;
            centerExpandedCanvas();
            dragBackground.reset();
            dragRevision = 0;
            invalidate();
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) {
            POINT cursor;
            GetCursorPos(&cursor);
            ScreenToClient(hwnd, &cursor);
            PointF point{static_cast<REAL>(cursor.x), static_cast<REAL>(cursor.y)};
            int handle = app.resizingCanvas ? app.canvasHandle : canvasHandleAt(point);
            LPCWSTR shape = IDC_ARROW;
            if (handle >= 0)
                shape = handle == 0 || handle == 2 ? IDC_SIZENWSE : IDC_SIZENESW;
            else if (app.tool == Tool::QrScan && onImage(point))
                shape = qrAt(toImage(point)) >= 0 ? IDC_HAND : IDC_CROSS;
            else if (app.tool == Tool::TextSelect && onImage(point))
                shape = IDC_CROSS;
            SetCursor(LoadCursorW(nullptr, shape));
            return TRUE;
        }
        break;
    case WM_MOUSEWHEEL: {
        if (app.selectingText || app.scanningQr) {
            app.hasTextSelection = app.hasQrArea = false;
            mouseUp();
        }
        if (!app.image || app.edit)
            return 0;
        if (GET_KEYSTATE_WPARAM(w) & MK_CONTROL) {
            int delta = GET_WHEEL_DELTA_WPARAM(w);
            if (!delta)
                return 0;
            REAL current = app.userZoom > 0 ? app.userZoom : calculateLayout(hwnd).zoom;
            app.userZoom = std::clamp(current * std::pow(1.15f, delta / 120.f), 0.02f, 8.f);
            app.zooming = SetTimer(hwnd, ZoomRefineTimer, 140, nullptr) != 0;
        } else if (GET_KEYSTATE_WPARAM(w) & MK_SHIFT)
            app.pan.X += GET_WHEEL_DELTA_WPARAM(w) * 0.5f;
        else
            app.pan.Y += GET_WHEEL_DELTA_WPARAM(w) * 0.5f;
        presentNavigation();
        return 0;
    }
    case WM_KEYDOWN:
        keyDown(w);
        return 0;
    case WM_COMMAND:
        if (reinterpret_cast<HWND>(l) == app.edit && HIWORD(w) == EN_CHANGE)
            syncInlineText();
        return 0;
    case WM_HOTKEY:
        if (w == static_cast<WPARAM>(app.hotkeyId)) {
            if (app.shortcutDialog) {
                if (GetFocus() == GetDlgItem(app.shortcutDialog, IDC_SHORTCUT_INPUT))
                    recordShortcutKey(app.shortcutDialog, HIWORD(l), LOWORD(l) & ~MOD_NOREPEAT);
                return 0;
            }
            HWND foreground = GetForegroundWindow();
            captureHotkey(IsWindowVisible(hwnd) &&
                          (foreground == hwnd || IsChild(hwnd, foreground)));
        }
        return 0;
    case WM_TIMER:
        if (w == 1 && app.capturePending)
            takeFrozenCapture();
        if (w == ZoomRefineTimer && app.zooming) {
            KillTimer(hwnd, ZoomRefineTimer);
            app.zooming = false;
            captureView.reset();
            dragRevision = 0;
            presentNavigation();
        }
        return 0;
    case OcrMessage:
        if (app.ocr && app.ocr->id == static_cast<uint64_t>(w) && ocrReport())
            invalidate();
        return 0;
    case ActivateMessage:
#ifdef CAPTURA_TEST
        if (!app.lifecycleDirectory.empty()) {
            ++app.lifecycleActivations;
            recordLifecycle();
            return 0;
        }
#endif
        ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
        return 0;
    case TrayMessage:
        if (l == WM_LBUTTONUP) {
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        }
        if (l == WM_LBUTTONDBLCLK)
            beginCapture();
        if (l == WM_RBUTTONUP || l == WM_CONTEXTMENU)
            trayMenu();
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (w)
            DestroyWindow(hwnd);
        return 0;
    case WM_CLOSE:
        commitText();
        if (app.tray) {
            ShowWindow(hwnd, SW_HIDE);
            report(captureHint().c_str());
        } else
            DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (app.overlay) {
            HWND overlay = app.overlay;
            app.overlay = nullptr;
            DestroyWindow(overlay);
        }
        if (app.hotkey)
            UnregisterHotKey(hwnd, app.hotkeyId);
        if (app.tray)
            removeTray();
        KillTimer(hwnd, 1);
        KillTimer(hwnd, ZoomRefineTimer);
        if (IsWindow(app.tooltip))
            DestroyWindow(app.tooltip);
        app.tooltip = nullptr;
        editorBuffer.reset();
        navigationBackground.reset();
        navigationRevision = 0;
        dragBackground.reset();
        captureView.reset();
        sourceRaster.reset();
        ocrService.shutdown();
        app.ocr.reset();
        // Effect bitmaps must be destroyed before GdiplusShutdown, including
        // caches retained by history and the last pointer gesture snapshot.
        app.items.clear();
        app.undo.clear();
        app.redo.clear();
        app.initialItem = {};
        app.image.reset();
        app.frozen.reset();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}
HICON makeIcon() {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 32;
    bi.bmiHeader.biHeight = -32;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &pixels, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color)
        return LoadIconW(nullptr, IDI_APPLICATION);
    auto* p = static_cast<DWORD*>(pixels);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            bool outer = x >= 3 && x <= 28 && y >= 6 && y <= 25;
            bool edge = outer && (x < 7 || x > 24 || y < 10 || y > 21);
            bool lens = (x - 16) * (x - 16) + (y - 16) * (y - 16) <= 30;
            bool top = x >= 9 && x <= 16 && y >= 3 && y < 7;
            p[y * 32 + x] = edge || top ? 0xff087cf0 : (lens ? 0xff087cf0 : 0);
        }
    BYTE maskData[128]{};
    HBITMAP mask = CreateBitmap(32, 32, 1, 1, maskData);
    ICONINFO ii{TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}
void initializeDefaults() {
    app.defaults[static_cast<int>(Tool::Text)].size = 24;
    app.defaults[static_cast<int>(Tool::Text)].filled = true;
    app.defaults[static_cast<int>(Tool::Counter)].size = 26;
    shareColor(app.sharedColor);
}
void registerClasses() {
    WNDCLASSW wc{};
    wc.hInstance = app.instance;
    wc.lpfnWndProc = EditorProc;
    wc.lpszClassName = mainClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = app.icon;
    wc.style = CS_DBLCLKS;
    RegisterClassW(&wc);
    wc.lpfnWndProc = OverlayProc;
    wc.lpszClassName = OverlayClass;
    wc.style = CS_DBLCLKS;
    RegisterClassW(&wc);
}
void createMain() {
    UINT dpi = GetDpiForSystem();
    int w = MulDiv(1040, dpi, 96), h = MulDiv(740, dpi, 96);
    POINT cursor{};
    GetCursorPos(&cursor);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &mi);
    w = std::min(w, static_cast<int>(mi.rcWork.right - mi.rcWork.left));
    h = std::min(h, static_cast<int>(mi.rcWork.bottom - mi.rcWork.top));
    int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2,
        y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - h) / 2;
    app.main = CreateWindowExW(0, mainClassName, ui::kAppName, WS_OVERLAPPEDWINDOW, x, y, w, h,
                               nullptr, nullptr, app.instance, nullptr);
    if (app.main)
        initTooltips();
}

// The optional native integration suite is compiled into this translation unit. They do not
// register the global shortcut, touch the clipboard or capture user content.
#ifdef CAPTURA_TEST
int selfTest(const std::wstring& directory);
#endif
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    app.instance = instance;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const auto options = startupOptions(argc, argv);
    app.background = options.background;
    app.managed = options.managed;
    bool isolatedTest = false;
    std::wstring mutexName = L"Local\\Captura.Native.Portable.Singleton.1";
#ifdef CAPTURA_TEST
    std::wstring lifecycleClass;
    isolatedTest = argc == 1 || (argc >= 3 && (wcscmp(argv[1], L"--shortcut-preview") == 0 ||
                                               wcscmp(argv[1], L"--self-test") == 0));
    if (argc >= 3 && wcscmp(argv[1], L"--lifecycle-test") == 0) {
        app.test = true;
        app.lifecycleDirectory = argv[2];
        std::wstring id =
            app.lifecycleDirectory.substr(app.lifecycleDirectory.find_last_of(L"\\/") + 1);
        lifecycleClass = std::wstring(AppClass) + L".Test." + id;
        mainClassName = lifecycleClass.c_str();
        mutexName += L".Test." + id;
        for (int n = 3; n < argc; ++n)
            app.lifecycleNoTray |= wcscmp(argv[n], L"--no-tray") == 0;
    }
#endif
    // Acquire the session-local singleton before creating windows, graphics,
    // tray icons, or registering shortcuts. Duplicate background launches do
    // nothing visible, even when graphics initialization would otherwise fail.
    if (!isolatedTest) {
        app.mutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
        DWORD result = GetLastError();
        if (!app.mutex) {
            LocalFree(argv);
            if (!app.background)
                error(nullptr, ui::kErrorStartup);
            return 1;
        }
        if (result == ERROR_ALREADY_EXISTS) {
            if (!app.background) {
                HWND existing = FindWindowW(mainClassName, nullptr);
                if (existing) {
                    DWORD pid = 0;
                    GetWindowThreadProcessId(existing, &pid);
                    AllowSetForegroundWindow(pid);
                    PostMessageW(existing, ActivateMessage, 0, 0);
                }
            }
            LocalFree(argv);
            CloseHandle(app.mutex);
            return 0;
        }
    }
    ULONG languageCount = 0;
    SetThreadPreferredUILanguages(MUI_LANGUAGE_NAME, L"en-US\0", &languageCount);
    GdiplusStartupInput gdiplusInput;
    if (GdiplusStartup(&app.gdiplus, &gdiplusInput, nullptr) != Ok) {
        LocalFree(argv);
        if (app.mutex)
            CloseHandle(app.mutex);
        if (!app.background)
            error(nullptr, ui::kErrorGraphics);
        return 1;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    initializeDefaults();
    app.icon = makeIcon();
    registerClasses();
#ifdef CAPTURA_TEST
    if (argc == 1 || (argc >= 3 && wcscmp(argv[1], L"--shortcut-preview") == 0)) {
        app.test = true;
        app.shortcutPreview = true;
        wchar_t executable[MAX_PATH]{};
        GetModuleFileNameW(nullptr, executable, MAX_PATH);
        std::wstring executablePath = executable;
        std::wstring directory =
            argc >= 3 ? argv[2]
                      : executablePath.substr(0, executablePath.find_last_of(L"\\")) + L"\\preview";
        CreateDirectoryW(directory.c_str(), nullptr);
        app.settingsPath = directory + L"\\preview-shortcut.ini";
        app.captureShortcut = loadCaptureShortcut(app.settingsPath);
        LocalFree(argv);
        createMain();
        ShowWindow(app.main, SW_HIDE);
        showCaptureShortcutDialog();
        DestroyWindow(app.main);
        DestroyIcon(app.icon);
        GdiplusShutdown(app.gdiplus);
        return 0;
    }
    if (argc >= 3 && wcscmp(argv[1], L"--self-test") == 0) {
        app.test = true;
        std::wstring dir = argv[2];
        LocalFree(argv);
        int result = selfTest(dir);
        app.image.reset();
        app.frozen.reset();
        if (app.icon)
            DestroyIcon(app.icon);
        GdiplusShutdown(app.gdiplus);
        return result;
    }
#endif
    LocalFree(argv);
    createMain();
    if (!app.main) {
        if (!app.background)
            error(nullptr, ui::kErrorEditor);
        CloseHandle(app.mutex);
        DestroyIcon(app.icon);
        GdiplusShutdown(app.gdiplus);
        return 1;
    }
    app.settingsPath = captureSettingsPath();
#ifdef CAPTURA_TEST
    if (!app.lifecycleDirectory.empty())
        app.settingsPath = app.lifecycleDirectory + L"\\settings.ini";
#endif
    app.captureShortcut = loadCaptureShortcut(app.settingsPath);
    app.status = captureHint();
    addTray();
    if (!app.test && (!app.background || app.tray))
        app.hotkey =
            RegisterHotKey(app.main, app.hotkeyId, app.captureShortcut.modifiers | MOD_NOREPEAT,
                           app.captureShortcut.key) != FALSE;
    if (!app.hotkey && !app.test) {
        app.status = ui::kShortcutBusy;
        if (!app.background)
            MessageBoxW(
                app.main,
                (captureShortcutName(app.captureShortcut) + ui::kShortcutBusyDetails).c_str(),
                ui::kAppName, MB_OK | MB_ICONINFORMATION);
    }
    int exitCode = app.background && !app.tray ? 1 : 0;
#ifdef CAPTURA_TEST
    recordLifecycle();
#endif
    if (exitCode)
        DestroyWindow(app.main);
    else if (!app.background)
        ShowWindow(app.main, show == SW_HIDE ? SW_SHOW : show);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    app.image.reset();
    app.frozen.reset();
    if (app.editFont)
        DeleteObject(app.editFont);
    DestroyIcon(app.icon);
    ReleaseMutex(app.mutex);
    CloseHandle(app.mutex);
    GdiplusShutdown(app.gdiplus);
    return exitCode;
}

#ifdef CAPTURA_TEST
#include "../tests/integration.inc"
#endif
