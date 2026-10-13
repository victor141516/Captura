#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace captura {
struct OcrWord {
    std::wstring text;
    Gdiplus::RectF bounds;
};
struct OcrLine {
    std::vector<OcrWord> words;
};
enum class OcrState { Ready, Empty, Unavailable, Failed, Cancelled };
struct OcrReport {
    OcrState state = OcrState::Failed;
    std::wstring language, error;
    std::vector<OcrLine> lines;
    double elapsedMs = 0, recognitionMs = 0, loremMs = 0, cpuMs = 0, cpuPercent = 0;
    double peakMemoryMiB = 0, memoryGrowthMiB = 0;
    unsigned width = 0, height = 0, tiles = 0, words = 0;
    std::shared_ptr<Gdiplus::Bitmap> loremImage;
};
struct OcrJob {
    uint64_t id = 0;
    std::atomic<bool> cancelled{false}, complete{false};
    std::atomic<unsigned> processedTiles{0};
    OcrReport report; // Published only when complete becomes true (release/acquire).
    std::unique_ptr<Gdiplus::Bitmap> source;
    HWND notifyWindow = nullptr;
    UINT notifyMessage = 0;
    double started = 0, startedCpu = 0;
    SIZE_T startedMemory = 0;
};

// A single worker replaces pending captures and cancels obsolete recognition.
// The worker exclusively owns its bitmap clone; GDI+ never reads the UI bitmap
// from a second thread. Neither screenshots nor recognized text leave the process.
class OcrService {
  public:
    ~OcrService();
    std::shared_ptr<OcrJob> start(Gdiplus::Bitmap& source, HWND window, UINT message);
    void shutdown();

  private:
    void run();
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    std::shared_ptr<OcrJob> pending, active;
    bool stopping = false;
    uint64_t sequence = 0;
};

bool wordInside(const OcrWord& word, const Gdiplus::RectF& area);
std::wstring selectedText(const OcrReport& report, const Gdiplus::RectF& area);
std::wstring metricsText(const OcrReport& report);
} // namespace captura
