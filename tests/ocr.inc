// Synthetic pixels and clipboard hooks only. No desktop capture or global shortcuts.
template <typename Check> void testOcr(const std::wstring& directory, Check check) {
    using namespace captura;
    app.image = std::make_unique<Bitmap>(1000, 600, PixelFormat32bppARGB);
    {
        Graphics g(app.image.get());
        g.Clear(Color(0xffffffff));
    }
    app.canvas = {{100, 50, 700, 450}, {100, 50, 700, 450}, 0xffffffff};
    app.items.clear();
    app.undo.clear();
    app.redo.clear();
    app.userZoom = 1;
    app.pan = {};
    app.pinnedCanvas = false;
    app.ocr = std::make_shared<OcrJob>();
    auto& report = app.ocr->report;
    report.state = OcrState::Ready;
    report.lines = {{{{L"Hello", {160, 100, 60, 24}}, {L"Windows", {235, 100, 100, 24}}}},
                    {{{L"Café", {160, 150, 50, 24}}, {L"世界", {230, 150, 40, 24}}}},
                    {{{L"Hidden", {750, 200, 80, 24}}}}};
    app.ocr->complete.store(true);
    keyDown('O', false, false);
    check(app.tool == Tool::TextSelect && !validIndex(app.selected),
          L"O activates Select text without selecting annotations");
    check(selectedText(report, {150, 90, 190, 90}) == L"Hello Windows\r\nCafé 世界",
          L"Text selection preserves word order, line breaks and Unicode");
    check(selectedText(report, {215, 90, 20, 40}).empty(),
          L"Barely touching neighboring word edges does not select them");
    app.layout = calculateLayout(app.main);
    int exports = testExportCompletions;
    mouseDown(toScreen({150, 90}));
    releasePointer(toScreen({340, 180}));
    check(testCopiedText == L"Hello Windows\r\nCafé 世界" && !app.selectingText &&
              app.hasTextSelection,
          L"Text rectangle automatically copies on mouse release");
    check(app.items.empty() && app.undo.empty() && exports == testExportCompletions,
          L"OCR selection leaves annotation history and editor lifetime intact");
    testCopiedText = L"sentinel";
    mouseDown(toScreen({340, 140}));
    releasePointer(toScreen({150, 90}));
    check(testCopiedText == L"Hello Windows", L"Reverse text drag normalizes image coordinates");
    testCopiedText.clear();
    mouseDown(toScreen({175, 160}));
    releasePointer(toScreen({175, 160}));
    check(testCopiedText == L"Café", L"Clicking a recognized word copies that word");
    testCopiedText = L"previous clipboard";
    mouseDown(toScreen({400, 250}));
    releasePointer(toScreen({450, 300}));
    check(testCopiedText == L"previous clipboard" &&
              app.textFeedback.find(L"No text") != std::wstring::npos,
          L"Empty selection does not erase existing clipboard contents");
    mouseDown(toScreen({150, 90}));
    mouseMove(toScreen({340, 180}));
    keyDown(VK_ESCAPE, false, false);
    check(!app.selectingText && !app.hasTextSelection && testCopiedText == L"previous clipboard",
          L"Escape cancels a text rectangle without copying");
    mouseDown(toScreen({150, 90}));
    SendMessageW(app.main, WM_CAPTURECHANGED, 0, 0);
    check(!app.selectingText && !app.hasTextSelection && testCopiedText == L"previous clipboard",
          L"Capture loss cancels text selection without copying");
    ReleaseCapture();
    app.userZoom = 0.6f;
    app.pan = {20, 10};
    app.layout = calculateLayout(app.main);
    mouseDown(toScreen({150, 90}));
    releasePointer(toScreen({340, 140}));
    check(testCopiedText == L"Hello Windows",
          L"Zoomed and panned OCR selection stays in full desktop coordinates");
    testCopiedText.clear();
    keyDown('C', true, false);
    check(testCopiedText == L"Hello Windows" && testExportCompletions == exports,
          L"Ctrl+C repeats text selection without exporting or closing editor");
    testClipboardSuccess = false;
    testCopiedText = L"intact";
    perform(4);
    check(testCopiedText == L"intact" && app.textFeedback.find(L"Could not") != std::wstring::npos,
          L"Failed text copy shows feedback and retains selection");
    testClipboardSuccess = true;
    auto job = app.ocr;
    app.canvas.bounds.right = app.canvas.manualFrame.right = 900;
    app.layout = calculateLayout(app.main);
    mouseDown(toScreen({740, 190}));
    releasePointer(toScreen({840, 240}));
    check(testCopiedText == L"Hidden" && app.ocr == job,
          L"Expanding initial crop reveals already recognized surrounding text");
    auto exported = flattened();
    Color pixel;
    exported->GetPixel(80, 60, &pixel);
    check(pixel.GetValue() == Color::White && app.items.empty(),
          L"OCR overlays never become exported annotation pixels");
    app.canvas.bounds.right = app.canvas.manualFrame.right = 700;
    testCopiedText = L"keep clipboard";
    copySelectedText();
    check(testCopiedText == L"keep clipboard",
          L"Text copy clips an older selection to the current visible frame");
    app.ocr->complete = false;
    app.hasTextSelection = false;
    mouseDown(toScreen({180, 120}));
    releasePointer(toScreen({320, 180}));
    copySelectedText();
    check(!app.selectingText && app.items.empty() && testCopiedText == L"keep clipboard" &&
              app.textFeedback.find(L"not ready") != std::wstring::npos,
          L"Pending OCR never creates annotations or copies an incomplete result");
    app.ocr->complete = true;
    report.state = OcrState::Unavailable;
    copySelectedText();
    check(testCopiedText == L"keep clipboard",
          L"Unavailable OCR leaves clipboard content untouched");
    report.state = OcrState::Ready;
    auto revision = sceneRevision;
    SendMessageW(app.main, OcrMessage, static_cast<WPARAM>(app.ocr->id + 1), 0);
    check(sceneRevision == revision, L"Stale OCR completion message cannot update current capture");

    auto wait = [](const std::shared_ptr<OcrJob>& queued) {
        ULONGLONG started = GetTickCount64();
        while (!queued->complete.load(std::memory_order_acquire) &&
               GetTickCount64() - started < 30000)
            Sleep(5);
        return queued->complete.load(std::memory_order_acquire);
    };
    auto synthetic = [](int width, int height, bool dense) {
        auto bitmap = std::make_unique<Bitmap>(width, height, PixelFormat32bppARGB);
        Graphics g(bitmap.get());
        g.Clear(Color(0xffffffff));
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        if (dense) {
            for (int y = 40; y < height - 70; y += 90)
                for (int x = 40; x < width - 650; x += 760)
                    drawText(g, L"Captura OCR test 12345", {float(x), float(y), 710, 50}, 28);
        } else {
            drawText(g, L"Captura OCR test 12345", {30, 20, 940, 90}, 64);
            drawText(g, L"Hello Windows", {30, 120, 940, 90}, 64);
        }
        return bitmap;
    };
    std::wstring measurements =
        L"Synthetic Windows OCR benchmark\r\nCPU and RAM cover the whole Captura process. CPU % is "
        L"normalized across logical processors.\r\n\r\n";
    bool available = true;
    app.testAutoOcr = true;
    for (int sample = 0; sample < 4; ++sample) {
        bool dense = sample == 3;
        app.frozen = synthetic(dense ? 3840 : 1000, dense ? 2160 : 220, dense);
        finishCapture({0, 0, 1000, 220});
        auto current = app.ocr;
        check(current && wait(current),
              L"Automatic OCR completes after opening a synthetic capture");
        if (!current || !current->complete)
            break;
        const auto& found = current->report;
        if (found.state == OcrState::Unavailable) {
            measurements += L"SKIP: Windows has no available OCR language.\r\n";
            available = false;
            break;
        }
        check(found.state == OcrState::Ready,
              L"System OCR recognizes synthetic screenshot without package identity");
        std::wstring text = selectedText(found, {0, 0, float(found.width), float(found.height)});
        check(text.find(L"Captura") != std::wstring::npos &&
                  text.find(L"12345") != std::wstring::npos,
              L"System OCR retains expected recognized text");
        if (dense)
            check(found.width == 3840 && found.height == 2160 && found.tiles > 1 &&
                      found.words == 460,
                  L"Large full desktop OCR uses native resolution tiles beyond initial crop");
        else
            check(text == L"Captura OCR test 12345\r\nHello Windows",
                  L"Native OCR preserves exact two-line text and reading order");
        check(found.elapsedMs > 0 && found.cpuMs >= 0 && found.cpuPercent >= 0 &&
                  found.peakMemoryMiB > 0,
              L"OCR records precise wall time, process CPU time and sampled RAM");
        measurements += L"Sample " + std::to_wstring(sample + 1) +
                        (dense ? L" — 4K text grid\r\n" : L" — two lines\r\n") +
                        metricsText(found) + L"\r\n\r\n";
    }
    if (available) {
        app.frozen = fixture();
        finishCapture({0, 0, 1000, 600});
        check(wait(app.ocr) && ocrReady(),
              L"OCR processes a realistic synthetic application screenshot");
        setTool(Tool::TextSelect);
        app.layout = calculateLayout(app.main);
        mouseDown(toScreen({235, 298}));
        releasePointer(toScreen({510, 410}));
        RECT client{};
        GetClientRect(app.main, &client);
        Bitmap preview(client.right, client.bottom, PixelFormat32bppARGB);
        Graphics g(&preview);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        renderEditor(g, app.main);
        encodePng(preview, (directory + L"\\ocr-editor.png").c_str());
        measurements += L"Application screenshot\r\n" + metricsText(*ocrReport()) + L"\r\n";
    }
    // Start twice without waiting: stale results cannot become the current capture.
    auto firstSource = synthetic(3840, 2160, true);
    auto older = ocrService.start(*firstSource, nullptr, 0);
    auto secondSource = synthetic(1000, 220, false);
    auto newer = ocrService.start(*secondSource, nullptr, 0);
    check(older->cancelled && newer->id > older->id && wait(newer),
          L"New OCR job cancels previous capture and has a distinct result identity");
    app.testAutoOcr = false;
    FILE* file = nullptr;
    _wfopen_s(&file, (directory + L"\\ocr-benchmark.txt").c_str(), L"wb");
    if (file) {
        int length = WideCharToMultiByte(CP_UTF8, 0, measurements.c_str(), int(measurements.size()),
                                         nullptr, 0, nullptr, nullptr);
        std::string bytes(length, '\0');
        WideCharToMultiByte(CP_UTF8, 0, measurements.c_str(), int(measurements.size()),
                            bytes.data(), length, nullptr, nullptr);
        fwrite(bytes.data(), 1, bytes.size(), file);
        fclose(file);
    }
}
