#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <psapi.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include "ocr.h"
#include "redaction.h"

namespace captura {
namespace {
double clockMs() {
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return now.QuadPart * 1000.0 / frequency.QuadPart;
}
double processCpuMs() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        return 0;
    auto value = [](FILETIME t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
    return (value(kernel) + value(user)) / 10000.0;
}
SIZE_T workingSet() {
    PROCESS_MEMORY_COUNTERS counters{};
    return K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))
               ? counters.WorkingSetSize
               : 0;
}
void recognize(OcrJob& job) {
    using namespace winrt;
    using namespace Windows::Media::Ocr;
    using namespace Windows::Graphics::Imaging;
    using namespace Windows::Storage::Streams;
    auto& report = job.report;
    SIZE_T peak = std::max(job.startedMemory, workingSet());
    try {
        if (!job.source || job.source->GetLastStatus() != Gdiplus::Ok)
            throw std::runtime_error("Could not prepare the screenshot for OCR.");
        report.width = job.source->GetWidth();
        report.height = job.source->GetHeight();
        auto engine = OcrEngine::TryCreateFromUserProfileLanguages();
        auto languages = OcrEngine::AvailableRecognizerLanguages();
        if (!engine && languages.Size())
            engine = OcrEngine::TryCreateFromLanguage(languages.GetAt(0));
        if (!engine) {
            report.state = OcrState::Unavailable;
            report.error = L"No Windows OCR language is available.";
        } else {
            report.language = engine.RecognizerLanguage().LanguageTag().c_str();
            // Keep transient pixel copies bounded even on Windows versions with
            // a much higher API limit. Older versions may require smaller tiles.
            const int maximum = std::min(2600, static_cast<int>(OcrEngine::MaxImageDimension()));
            if (maximum < 512)
                throw std::runtime_error("Unsupported Windows OCR image limit.");
            // Native resolution, with context on both sides of each tile seam.
            // Each word belongs to the core containing its center, avoiding duplicates.
            const int padding = 128, step = maximum - 2 * padding;
            const int width = static_cast<int>(report.width),
                      height = static_cast<int>(report.height);
            for (int y = 0; y < height && !job.cancelled; y += step) {
                for (int x = 0; x < width && !job.cancelled; x += step) {
                    int right = std::min(width, x + step), bottom = std::min(height, y + step);
                    Gdiplus::Rect roi(std::max(0, x - padding), std::max(0, y - padding), 0, 0);
                    roi.Width = std::min(width, right + padding) - roi.X;
                    roi.Height = std::min(height, bottom + padding) - roi.Y;
                    Gdiplus::BitmapData pixels{};
                    if (job.source->LockBits(&roi, Gdiplus::ImageLockModeRead, PixelFormat32bppRGB,
                                             &pixels) != Gdiplus::Ok)
                        throw std::runtime_error("Could not read OCR pixels.");
                    std::vector<uint8_t> bytes;
                    try {
                        bytes.resize(static_cast<size_t>(roi.Width) * roi.Height * 4);
                        for (int row = 0; row < roi.Height; ++row)
                            memcpy(bytes.data() + static_cast<size_t>(row) * roi.Width * 4,
                                   static_cast<const uint8_t*>(pixels.Scan0) +
                                       static_cast<ptrdiff_t>(row) * pixels.Stride,
                                   static_cast<size_t>(roi.Width) * 4);
                    } catch (...) {
                        job.source->UnlockBits(&pixels);
                        throw;
                    }
                    job.source->UnlockBits(&pixels);
                    DataWriter writer;
                    writer.WriteBytes(bytes);
                    SoftwareBitmap bitmap(BitmapPixelFormat::Bgra8, roi.Width, roi.Height,
                                          BitmapAlphaMode::Ignore);
                    bitmap.CopyFromBuffer(writer.DetachBuffer());
                    writer.Close();
                    std::vector<uint8_t>().swap(bytes);
                    double started = clockMs();
                    auto operation = engine.RecognizeAsync(bitmap);
                    while (operation.Status() == Windows::Foundation::AsyncStatus::Started) {
                        peak = std::max(peak, workingSet());
                        if (job.cancelled) {
                            operation.Cancel();
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                    if (job.cancelled)
                        break;
                    auto result = operation.GetResults();
                    report.recognitionMs += clockMs() - started;
                    for (const auto& line : result.Lines()) {
                        OcrLine found;
                        for (const auto& word : line.Words()) {
                            auto r = word.BoundingRect();
                            OcrWord plain{word.Text().c_str(),
                                          {r.X + roi.X, r.Y + roi.Y, r.Width, r.Height}};
                            if (wordInside(plain, {float(x), float(y), float(right - x),
                                                   float(bottom - y)})) {
                                found.words.push_back(std::move(plain));
                                ++report.words;
                            }
                        }
                        if (!found.words.empty())
                            report.lines.push_back(std::move(found));
                    }
                    ++report.tiles;
                    job.processedTiles.store(report.tiles);
                    peak = std::max(peak, workingSet());
                }
            }
            // Join fragments from the same row across horizontal tile boundaries.
            std::stable_sort(report.lines.begin(), report.lines.end(),
                             [](const OcrLine& a, const OcrLine& b) {
                                 return a.words.front().bounds.Y < b.words.front().bounds.Y;
                             });
            std::vector<OcrLine> rows;
            for (auto& line : report.lines) {
                auto first = line.words.front().bounds;
                bool merged = false;
                for (auto& row : rows) {
                    auto other = row.words.front().bounds;
                    if (std::abs(first.Y + first.Height / 2 - other.Y - other.Height / 2) <
                        std::min(first.Height, other.Height) * 0.4f) {
                        row.words.insert(row.words.end(), line.words.begin(), line.words.end());
                        merged = true;
                        break;
                    }
                }
                if (!merged)
                    rows.push_back(std::move(line));
            }
            for (auto& row : rows)
                std::stable_sort(
                    row.words.begin(), row.words.end(),
                    [](const OcrWord& a, const OcrWord& b) { return a.bounds.X < b.bounds.X; });
            report.lines = std::move(rows);
            report.state = report.words ? OcrState::Ready : OcrState::Empty;
        }
    } catch (const winrt::hresult_error& failure) {
        wchar_t code[32];
        swprintf_s(code, L"Windows OCR error 0x%08X", unsigned(failure.code().value));
        report.error = code;
        const HRESULT codeValue = failure.code().value;
        report.state = codeValue == REGDB_E_CLASSNOTREG || codeValue == CLASS_E_CLASSNOTAVAILABLE ||
                               codeValue == E_NOTIMPL
                           ? OcrState::Unavailable
                           : OcrState::Failed;
    } catch (...) {
        report.error = L"Could not recognize this screenshot.";
        report.state = OcrState::Failed;
    }
    if (!job.cancelled && (report.state == OcrState::Ready || report.state == OcrState::Empty)) {
        double started = clockMs();
        try {
            report.loremImage = prepareLoremImage(std::move(job.source), report,
                                                  uint32_t(job.id * 2654435761ull), &job.cancelled);
        } catch (...) {
            report.loremImage.reset();
        }
        report.loremMs = clockMs() - started;
        peak = std::max(peak, workingSet());
    }
    if (job.cancelled) {
        report.state = OcrState::Cancelled;
        report.loremImage.reset();
    }
    report.elapsedMs = clockMs() - job.started;
    report.cpuMs = std::max(0.0, processCpuMs() - job.startedCpu);
    unsigned processors = std::max(1UL, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    report.cpuPercent =
        report.elapsedMs > 0 ? report.cpuMs / report.elapsedMs * 100 / processors : 0;
    report.peakMemoryMiB = peak / (1024.0 * 1024);
    report.memoryGrowthMiB = (peak - job.startedMemory) / (1024.0 * 1024);
    job.source.reset();
}
} // namespace

OcrService::~OcrService() {
    shutdown();
}
std::shared_ptr<OcrJob> OcrService::start(Gdiplus::Bitmap& source, HWND window, UINT message) {
    auto job = std::make_shared<OcrJob>();
    job->started = clockMs();
    job->startedCpu = processCpuMs();
    job->startedMemory = workingSet();
    job->notifyWindow = window;
    job->notifyMessage = message;
    job->source.reset(
        source.Clone(0, 0, source.GetWidth(), source.GetHeight(), PixelFormat32bppRGB));
    std::lock_guard<std::mutex> lock(mutex);
    job->id = ++sequence;
    if (active)
        active->cancelled = true;
    if (pending)
        pending->cancelled = true;
    stopping = false;
    pending = job;
    if (!worker.joinable())
        worker = std::thread(&OcrService::run, this);
    wake.notify_one();
    return job;
}
void OcrService::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
        if (active)
            active->cancelled = true;
        if (pending)
            pending->cancelled = true;
        pending.reset();
    }
    wake.notify_one();
    if (worker.joinable())
        worker.join();
}
void OcrService::run() {
    bool initialized = false;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        initialized = true;
    } catch (...) {
    }
    for (;;) {
        std::shared_ptr<OcrJob> job;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [&] { return stopping || pending; });
            if (stopping)
                break;
            active = job = std::move(pending);
        }
        if (initialized)
            recognize(*job);
        else {
            job->report.error = L"Windows OCR could not initialize.";
            job->source.reset();
        }
        job->complete.store(true, std::memory_order_release);
        if (job->notifyWindow && !job->cancelled)
            PostMessageW(job->notifyWindow, job->notifyMessage, static_cast<WPARAM>(job->id), 0);
        {
            std::lock_guard<std::mutex> lock(mutex);
            active.reset();
        }
    }
    if (initialized)
        winrt::uninit_apartment();
}
bool wordInside(const OcrWord& word, const Gdiplus::RectF& area) {
    return area.Contains(word.bounds.X + word.bounds.Width / 2,
                         word.bounds.Y + word.bounds.Height / 2);
}
std::wstring selectedText(const OcrReport& report, const Gdiplus::RectF& area) {
    std::wstring text;
    for (const auto& line : report.lines) {
        std::wstring row;
        for (const auto& word : line.words)
            if (wordInside(word, area)) {
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
    return text;
}
std::wstring metricsText(const OcrReport& report) {
    wchar_t text[512];
    swprintf_s(text,
               L"Elapsed: %.1f ms\nRecognition: %.1f ms\nLorem preparation: %.1f ms\nCPU "
               L"(process): %.1f ms\nAverage CPU: "
               L"%.1f%%\nSampled peak RAM: %.1f MiB\nRAM growth: %.1f MiB\n%u × %u px · %u "
               L"tiles\n%u words · %ls",
               report.elapsedMs, report.recognitionMs, report.loremMs, report.cpuMs,
               report.cpuPercent, report.peakMemoryMiB, report.memoryGrowthMiB, report.width,
               report.height, report.tiles, report.words, report.language.c_str());
    return text;
}
} // namespace captura
