// Minimal unpackaged C++/WinRT probe. Uses only synthetic pixels and Windows OCR.
#include <windows.h>
#include <appmodel.h>
#include <cstdio>
#include <string>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>

int wmain() {
    using namespace winrt;
    using namespace Windows::Media::Ocr;
    using namespace Windows::Graphics::Imaging;
    using namespace Windows::Storage::Streams;
    const wchar_t* stage = L"initialize WinRT";
    try {
        init_apartment(apartment_type::multi_threaded);
        UINT32 packageLength = 0;
        LONG packageStatus = GetCurrentPackageFullName(&packageLength, nullptr);
        wprintf(L"Package identity: %ls (status %ld)\n",
                packageStatus == APPMODEL_ERROR_NO_PACKAGE ? L"none" : L"present or unknown",
                packageStatus);
        stage = L"query available OCR languages";
        auto languages = OcrEngine::AvailableRecognizerLanguages();
        wprintf(L"Available OCR languages (%u):", languages.Size());
        for (const auto& language : languages)
            wprintf(L" %ls", language.LanguageTag().c_str());
        wprintf(L"\n");
        stage = L"create system OCR engine";
        auto engine = OcrEngine::TryCreateFromUserProfileLanguages();
        if (!engine && languages.Size())
            engine = OcrEngine::TryCreateFromLanguage(languages.GetAt(0));
        if (!engine) {
            wprintf(L"FAIL: no OCR language available for the engine. Nothing was installed.\n");
            return 2;
        }
        wprintf(L"Selected language: %ls\n", engine.RecognizerLanguage().LanguageTag().c_str());

        // Draw two known lines into a top-down BGRA bitmap, without photographing the desktop.
        constexpr int width = 1000, height = 220, byteCount = width * height * 4;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* raw = nullptr;
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP image = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &raw, nullptr, 0);
        if (!dc || !image || !raw) {
            if (image) DeleteObject(image);
            if (dc) DeleteDC(dc);
            wprintf(L"FAIL: synthetic image allocation.\n");
            return 3;
        }
        HGDIOBJ original = SelectObject(dc, image);
        RECT area{0, 0, width, height};
        FillRect(dc, &area, GetSysColorBrush(COLOR_WINDOW));
        HFONT font = CreateFontW(-64, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                  DEFAULT_PITCH, L"Segoe UI");
        HGDIOBJ originalFont = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(0, 0, 0));
        const wchar_t* first = L"Captura OCR test 12345";
        const wchar_t* second = L"Hello Windows";
        TextOutW(dc, 30, 20, first, static_cast<int>(wcslen(first)));
        TextOutW(dc, 30, 110, second, static_cast<int>(wcslen(second)));
        GdiFlush();

        // Save the exact input so the result is reviewable.
        BITMAPFILEHEADER header{};
        header.bfType = 0x4d42;
        header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
        header.bfSize = header.bfOffBits + byteCount;
        FILE* file = nullptr;
        _wfopen_s(&file, L"input.bmp", L"wb");
        if (file) {
            fwrite(&header, sizeof(header), 1, file);
            fwrite(&info.bmiHeader, sizeof(BITMAPINFOHEADER), 1, file);
            fwrite(raw, byteCount, 1, file);
            fclose(file);
        }
        stage = L"prepare SoftwareBitmap";
        DataWriter writer;
        auto* bytes = static_cast<const uint8_t*>(raw);
        writer.WriteBytes(array_view<const uint8_t>(bytes, bytes + byteCount));
        SoftwareBitmap bitmap(BitmapPixelFormat::Bgra8, width, height, BitmapAlphaMode::Ignore);
        bitmap.CopyFromBuffer(writer.DetachBuffer());
        SelectObject(dc, originalFont);
        DeleteObject(font);
        SelectObject(dc, original);
        DeleteObject(image);
        DeleteDC(dc);

        stage = L"recognize synthetic image";
        ULONGLONG start = GetTickCount64();
        auto result = engine.RecognizeAsync(bitmap).get();
        wprintf(L"Recognition time: %llu ms\n", GetTickCount64() - start);
        wprintf(L"Recognized text: %ls\nLines: %u\n", result.Text().c_str(), result.Lines().Size());
        for (const auto& line : result.Lines()) {
            wprintf(L"  %ls\n", line.Text().c_str());
            for (const auto& word : line.Words()) {
                auto bounds = word.BoundingRect();
                wprintf(L"    %ls [%.0f, %.0f, %.0f, %.0f]\n", word.Text().c_str(),
                        bounds.X, bounds.Y, bounds.Width, bounds.Height);
            }
        }
        std::wstring text = result.Text().c_str();
        bool recognized = text.find(L"Captura") != std::wstring::npos &&
                          text.find(L"12345") != std::wstring::npos &&
                          text.find(L"Hello Windows") != std::wstring::npos;
        wprintf(L"%ls: Windows OCR recognized the expected synthetic text.\n",
                recognized ? L"PASS" : L"FAIL");
        return recognized && packageStatus == APPMODEL_ERROR_NO_PACKAGE ? 0 : 4;
    } catch (const hresult_error& failure) {
        wprintf(L"FAIL at %ls: HRESULT 0x%08X: %ls\n", stage,
                static_cast<unsigned>(failure.code().value), failure.message().c_str());
        return 1;
    }
}
