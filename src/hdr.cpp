#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

#include "hdr.h"

namespace captura {
namespace {
using Microsoft::WRL::ComPtr;
constexpr float ScRgbNits = 80; // scRGB 1.0 is 80 nits.
constexpr int SrgbSteps = 65535;

const std::array<float, 65536>& halfTable() {
    static const auto table = [] {
        std::array<float, 65536> values{};
        for (uint32_t bits = 0; bits < 65536; ++bits) {
            uint32_t sign = (bits & 0x8000u) << 16, exponent = (bits >> 10) & 0x1f,
                     mantissa = bits & 0x3ff, result;
            if (exponent == 0) {
                if (!mantissa) {
                    result = sign;
                } else { // Subnormal: normalize into a float exponent.
                    int shift = 0;
                    while (!(mantissa & 0x400)) {
                        mantissa <<= 1;
                        ++shift;
                    }
                    result = sign | static_cast<uint32_t>(127 - 15 + 1 - shift) << 23 |
                             (mantissa & 0x3ff) << 13;
                }
            } else if (exponent == 31) {
                result = sign | 0x7f800000u | mantissa << 13;
            } else {
                result = sign | (exponent + 127 - 15) << 23 | mantissa << 13;
            }
            memcpy(&values[bits], &result, sizeof(float));
        }
        return values;
    }();
    return table;
}

const std::array<uint8_t, SrgbSteps + 1>& srgbTable() {
    static const auto table = [] {
        std::array<uint8_t, SrgbSteps + 1> values{};
        for (int i = 0; i <= SrgbSteps; ++i) {
            double linear = static_cast<double>(i) / SrgbSteps;
            double encoded =
                linear <= 0.0031308 ? 12.92 * linear : 1.055 * std::pow(linear, 1 / 2.4) - 0.055;
            values[i] = static_cast<uint8_t>(std::clamp(encoded * 255 + 0.5, 0.0, 255.0));
        }
        return values;
    }();
    return table;
}

double pq(double nits) {
    constexpr double m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625,
                     c3 = 18.6875;
    double y = std::pow(std::clamp(nits / 10000, 0.0, 1.0), m1);
    return std::pow((c1 + c2 * y) / (1 + c3 * y), m2);
}
double pqNits(double signal) {
    constexpr double m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625,
                     c3 = 18.6875;
    double e = std::pow(std::clamp(signal, 0.0, 1.0), 1 / m2);
    return 10000 * std::pow(std::max(e - c1, 0.0) / (c2 - c3 * e), 1 / m1);
}

// Positive halves sort like unsigned integers, so the largest bit pattern is max RGB.
uint16_t maxKey(const uint16_t* pixel) {
    auto positive = [](uint16_t value) -> uint16_t {
        return value & 0x8000 ? 0 : std::min<uint16_t>(value, 0x7bff);
    };
    return std::max({positive(pixel[0]), positive(pixel[1]), positive(pixel[2])});
}

float sdrWhiteNits(const wchar_t* gdiName) {
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
        return ScRgbNits;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount,
                           modes.data(), nullptr) != ERROR_SUCCESS)
        return ScRgbNits;
    for (UINT32 i = 0; i < pathCount; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = paths[i].sourceInfo.adapterId;
        source.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
            wcscmp(source.viewGdiDeviceName, gdiName) != 0)
            continue;
        DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
        white.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
        white.header.size = sizeof(white);
        white.header.adapterId = paths[i].targetInfo.adapterId;
        white.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS && white.SDRWhiteLevel)
            return white.SDRWhiteLevel / 1000.f * ScRgbNits;
    }
    return ScRgbNits;
}

// Top-level windows front to back. Click-through overlays (game bars, recorders) are skipped
// so they do not merge the content beneath them into one region.
std::vector<RECT> visibleWindows() {
    std::vector<RECT> windows;
    EnumWindows(
        [](HWND hwnd, LPARAM parameter) -> BOOL {
            if (!IsWindowVisible(hwnd) || IsIconic(hwnd) ||
                (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT))
                return TRUE;
            DWORD cloaked = 0;
            if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
                cloaked)
                return TRUE;
            RECT r{};
            if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))))
                GetWindowRect(hwnd, &r);
            if (r.right > r.left && r.bottom > r.top)
                reinterpret_cast<std::vector<RECT>*>(parameter)->push_back(r);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&windows));
    return windows;
}

bool duplicate(IDXGIOutput6* output, RECT monitor, ID3D11Device* device,
               ID3D11DeviceContext* context, std::vector<uint16_t>& pixels, UINT& width,
               UINT& height, std::wstring& error) {
    ComPtr<IDXGIOutput5> output5;
    if (FAILED(output->QueryInterface(IID_PPV_ARGS(&output5)))) {
        error = L"HDR duplication is unavailable.";
        return false;
    }
    const DXGI_FORMAT formats[] = {DXGI_FORMAT_R16G16B16A16_FLOAT};
    ComPtr<IDXGIOutputDuplication> duplication;
    HRESULT hr = output5->DuplicateOutput1(device, 0, 1, formats, &duplication);
    if (FAILED(hr)) {
        error = L"HDR duplication failed (0x" + std::to_wstring(static_cast<unsigned>(hr)) + L").";
        return false;
    }
    // A new duplication can deliver an empty frame first, and DWM only produces a desktop image
    // when something changes. If nothing presents soon, an almost transparent 1-pixel window at
    // the monitor corner forces one composition (changing that pixel by at most one level).
    DXGI_OUTDUPL_FRAME_INFO info{};
    ComPtr<IDXGIResource> resource;
    ULONGLONG deadline = GetTickCount64() + 600;
    HWND nudge = nullptr;
    auto done = [&] {
        if (nudge)
            DestroyWindow(nudge);
    };
    for (;;) {
        ULONGLONG now = GetTickCount64();
        UINT wait =
            now < deadline ? static_cast<UINT>(std::min<ULONGLONG>(deadline - now, 100)) : 0;
        hr = duplication->AcquireNextFrame(wait, &info, &resource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT && GetTickCount64() < deadline) {
            if (!nudge) {
                nudge = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW |
                                            WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                                        L"STATIC", nullptr, WS_POPUP, monitor.left, monitor.top, 1,
                                        1, nullptr, nullptr, nullptr, nullptr);
                if (nudge) {
                    SetLayeredWindowAttributes(nudge, 0, 1, LWA_ALPHA);
                    ShowWindow(nudge, SW_SHOWNOACTIVATE);
                }
            }
            continue;
        }
        if (FAILED(hr)) {
            done();
            error = L"No HDR frame was available.";
            return false;
        }
        if (info.LastPresentTime.QuadPart)
            break;
        resource.Reset();
        duplication->ReleaseFrame();
    }
    done();
    ComPtr<ID3D11Texture2D> texture, staging;
    D3D11_TEXTURE2D_DESC desc{};
    bool copied = SUCCEEDED(resource.As(&texture));
    if (copied) {
        texture->GetDesc(&desc);
        copied = desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
    }
    if (copied) {
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        desc.MipLevels = desc.ArraySize = 1;
        desc.SampleDesc = {1, 0};
        copied = SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging));
        if (copied)
            context->CopyResource(staging.Get(), texture.Get());
    }
    duplication->ReleaseFrame();
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (!copied || FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        error = L"The HDR frame could not be read.";
        return false;
    }
    width = desc.Width;
    height = desc.Height;
    pixels.resize(static_cast<size_t>(width) * height * 4);
    for (UINT y = 0; y < height; ++y)
        memcpy(pixels.data() + static_cast<size_t>(y) * width * 4,
               static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch,
               static_cast<size_t>(width) * 8);
    context->Unmap(staging.Get(), 0);
    return true;
}
} // namespace

float halfToFloat(uint16_t value) {
    return halfTable()[value];
}

uint16_t floatToHalf(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    uint16_t sign = static_cast<uint16_t>((bits >> 16) & 0x8000);
    float magnitude = std::fabs(value);
    if (std::isnan(value))
        return sign | 0x7e00;
    if (magnitude >= 65520.f)
        return sign | 0x7c00;
    if (magnitude < 6.103515625e-05f) // Subnormal half.
        return sign | static_cast<uint16_t>(std::lround(magnitude / 5.9604644775390625e-08f));
    int exponent;
    float fraction = std::frexp(magnitude, &exponent); // magnitude = fraction * 2^exponent
    long mantissa = std::lround((fraction * 2 - 1) * 1024);
    if (mantissa == 1024) {
        mantissa = 0;
        ++exponent;
    }
    return sign | static_cast<uint16_t>((exponent - 1 + 15) << 10 | mantissa);
}

float hdrToneMapNits(float nits, float sourcePeakNits, float targetPeakNits) {
    if (nits <= 0)
        return 0;
    if (sourcePeakNits <= targetPeakNits)
        return std::min(nits, targetPeakNits);
    double sourcePq = pq(sourcePeakNits);
    double e1 = std::min(pq(nits) / sourcePq, 1.0);
    double maximum = pq(targetPeakNits) / sourcePq;
    double knee = std::max(1.5 * maximum - 0.5, 0.0), e2 = e1;
    if (e1 > knee) {
        double t = (e1 - knee) / (1 - knee), t2 = t * t, t3 = t2 * t;
        e2 = (2 * t3 - 3 * t2 + 1) * knee + (t3 - 2 * t2 + t) * (1 - knee) +
             (-2 * t3 + 3 * t2) * maximum;
    }
    return static_cast<float>(std::min(pqNits(e2 * sourcePq), double(targetPeakNits)));
}

namespace {
struct Row {
    LONG y, left, right;
};
unsigned workers(size_t rows) {
    return rows < 256 ? 1 : std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
}
// Splits rows into contiguous chunks; `work(worker, row)` runs on `threads` threads.
template <typename Work> void forRows(const std::vector<Row>& rows, unsigned threads, Work work) {
    size_t chunk = (rows.size() + threads - 1) / threads;
    auto run = [&](unsigned worker) {
        size_t end = std::min(rows.size(), (worker + 1) * chunk);
        for (size_t i = worker * chunk; i < end; ++i)
            work(worker, rows[i]);
    };
    std::vector<std::thread> pool;
    for (unsigned worker = 1; worker < threads; ++worker)
        pool.emplace_back(run, worker);
    run(0);
    for (std::thread& thread : pool)
        thread.join();
}
} // namespace

bool toneMapRegion(const uint16_t* rgba, int pitch, const RECT* rects, size_t count,
                   float sdrWhiteNits, float displayPeakNits, uint8_t* bgra, int stride) {
    const auto& toFloat = halfTable();
    std::vector<Row> rows;
    for (size_t i = 0; i < count; ++i)
        for (LONG y = rects[i].top; y < rects[i].bottom; ++y)
            if (rects[i].right > rects[i].left)
                rows.push_back({y, rects[i].left, rects[i].right});
    if (rows.empty())
        return false;
    unsigned threads = workers(rows.size());
    std::vector<std::vector<uint32_t>> partial(threads, std::vector<uint32_t>(0x7c00));
    forRows(rows, threads, [&](unsigned worker, const Row& row) {
        auto& counts = partial[worker];
        const uint16_t* pixel = rgba + (static_cast<ptrdiff_t>(row.y) * pitch + row.left) * 4;
        for (LONG x = row.left; x < row.right; ++x, pixel += 4)
            ++counts[maxKey(pixel)];
    });
    std::vector<uint32_t>& histogram = partial[0];
    for (unsigned worker = 1; worker < threads; ++worker)
        for (size_t key = 0; key < histogram.size(); ++key)
            histogram[key] += partial[worker][key];
    uint64_t total = 0;
    for (const Row& row : rows)
        total += row.right - row.left;
    // The 99.9th percentile ignores tiny specular glints that would darken the whole region.
    uint64_t wanted = total - total / 1000, seen = 0;
    uint16_t peakKey = 0;
    for (uint32_t key = 0; key < histogram.size(); ++key) {
        seen += histogram[key];
        if (seen >= wanted) {
            peakKey = static_cast<uint16_t>(key);
            break;
        }
    }
    float peakNits = toFloat[peakKey] * ScRgbNits;
    if (displayPeakNits > sdrWhiteNits)
        peakNits = std::min(peakNits, displayPeakNits);
    bool mapped = peakNits > sdrWhiteNits * 1.02f;

    // Gain per max-RGB value; scaling all channels by one factor preserves hue. Without
    // brighter content the curve is linear up to SDR white, which reproduces SDR windows.
    float sourcePeak = mapped ? peakNits : sdrWhiteNits;
    std::vector<float> gain(histogram.size());
    for (uint32_t key = 1; key < gain.size(); ++key) {
        float value = toFloat[key];
        gain[key] =
            hdrToneMapNits(value * ScRgbNits, sourcePeak, sdrWhiteNits) / sdrWhiteNits / value;
    }
    const auto& encode = srgbTable();
    forRows(rows, threads, [&](unsigned, const Row& row) {
        const uint16_t* pixel = rgba + (static_cast<ptrdiff_t>(row.y) * pitch + row.left) * 4;
        uint8_t* out = bgra + static_cast<ptrdiff_t>(row.y) * stride + row.left * 4;
        for (LONG x = row.left; x < row.right; ++x, pixel += 4, out += 4) {
            float g = gain[maxKey(pixel)];
            float c[3] = {toFloat[pixel[0]] * g, toFloat[pixel[1]] * g, toFloat[pixel[2]] * g};
            // Wide-gamut colors have negative sRGB components; desaturate toward their
            // luminance instead of clipping channels independently.
            float minimum = std::min({c[0], c[1], c[2]});
            if (minimum < 0) {
                float luma = std::max(0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2], 0.f);
                float t = luma > 0 ? luma / (luma - minimum) : 0;
                for (float& channel : c)
                    channel = luma + t * (channel - luma);
            }
            for (int channel = 0; channel < 3; ++channel)
                out[2 - channel] =
                    encode[static_cast<int>(std::clamp(c[channel], 0.f, 1.f) * SrgbSteps + 0.5f)];
            out[3] = 255;
        }
    });
    return mapped;
}

HdrReport convertHdrDesktop(uint8_t* bgra, int stride, RECT desktop) {
    HdrReport report;
    LARGE_INTEGER frequency, started, finished;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&started);
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        report.error = L"DXGI is unavailable.";
        return report;
    }
    // Creating a device loads the graphics driver, which dominates the first capture, so
    // Captura keeps it for the session.
    static struct {
        LUID adapter{};
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
    } cache;
    std::vector<RECT> windows;
    bool enumerated = false;
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<IDXGIOutput> output;
        for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
            ComPtr<IDXGIOutput6> output6;
            DXGI_OUTPUT_DESC1 desc{};
            if (FAILED(output.As(&output6)) || FAILED(output6->GetDesc1(&desc)) ||
                !desc.AttachedToDesktop ||
                desc.ColorSpace != DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
                continue;
            RECT monitor = desc.DesktopCoordinates, area;
            if (!IntersectRect(&area, &monitor, &desktop))
                continue;
            ++report.hdrOutputs;
            if (desc.Rotation != DXGI_MODE_ROTATION_IDENTITY &&
                desc.Rotation != DXGI_MODE_ROTATION_UNSPECIFIED) {
                report.error = L"Rotated HDR displays keep standard capture colors.";
                continue;
            }
            if (!device) {
                DXGI_ADAPTER_DESC1 adapterDesc{};
                adapter->GetDesc1(&adapterDesc);
                bool reusable = cache.device &&
                                cache.adapter.LowPart == adapterDesc.AdapterLuid.LowPart &&
                                cache.adapter.HighPart == adapterDesc.AdapterLuid.HighPart &&
                                cache.device->GetDeviceRemovedReason() == S_OK;
                if (!reusable) {
                    cache = {};
                    if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
                                                 nullptr, 0, D3D11_SDK_VERSION, &cache.device,
                                                 nullptr, &cache.context))) {
                        cache = {};
                        report.error = L"Direct3D is unavailable for HDR capture.";
                        continue;
                    }
                    cache.adapter = adapterDesc.AdapterLuid;
                }
                device = cache.device;
                context = cache.context;
            }
            std::vector<uint16_t> pixels;
            UINT width = 0, height = 0;
            if (!duplicate(output6.Get(), monitor, device.Get(), context.Get(), pixels, width,
                           height, report.error)) {
                cache = {}; // A stale or removed device is recreated on the next capture.
                continue;
            }
            if (static_cast<LONG>(width) != monitor.right - monitor.left ||
                static_cast<LONG>(height) != monitor.bottom - monitor.top) {
                report.error = L"The HDR frame does not match the display size.";
                continue;
            }
            if (!enumerated) {
                windows = visibleWindows();
                enumerated = true;
            }
            float white = sdrWhiteNits(desc.DeviceName);
            // Destination pixel for monitor-local (x, y) is (x + dx, y + dy) in `bgra`.
            uint8_t* target = bgra + static_cast<ptrdiff_t>(monitor.top - desktop.top) * stride +
                              static_cast<ptrdiff_t>(monitor.left - desktop.left) * 4;
            auto convert = [&](HRGN region) {
                HRGN clip = CreateRectRgnIndirect(&area);
                int kind = CombineRgn(region, region, clip, RGN_AND);
                DeleteObject(clip);
                if (kind == NULLREGION || kind == ERROR)
                    return;
                OffsetRgn(region, -monitor.left, -monitor.top);
                DWORD size = GetRegionData(region, 0, nullptr);
                std::vector<uint8_t> data(size);
                auto* region_data = reinterpret_cast<RGNDATA*>(data.data());
                if (!size || !GetRegionData(region, size, region_data))
                    return;
                ++report.regions;
                if (toneMapRegion(pixels.data(), static_cast<int>(width),
                                  reinterpret_cast<const RECT*>(region_data->Buffer),
                                  region_data->rdh.nCount, white, desc.MaxLuminance, target,
                                  stride))
                    ++report.mappedRegions;
            };
            HRGN covered = CreateRectRgn(0, 0, 0, 0);
            for (const RECT& window : windows) {
                RECT inside;
                if (!IntersectRect(&inside, &window, &area))
                    continue;
                HRGN visible = CreateRectRgnIndirect(&inside);
                CombineRgn(visible, visible, covered, RGN_DIFF);
                HRGN shape = CreateRectRgnIndirect(&inside);
                CombineRgn(covered, covered, shape, RGN_OR);
                DeleteObject(shape);
                convert(visible);
                DeleteObject(visible);
            }
            HRGN rest = CreateRectRgnIndirect(&area);
            CombineRgn(rest, rest, covered, RGN_DIFF);
            convert(rest);
            DeleteObject(rest);
            DeleteObject(covered);
            ++report.convertedOutputs;
        }
    }
    QueryPerformanceCounter(&finished);
    report.milliseconds =
        1000.0 * static_cast<double>(finished.QuadPart - started.QuadPart) / frequency.QuadPart;
    return report;
}
} // namespace captura
