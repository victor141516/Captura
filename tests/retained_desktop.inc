// Exercise selection, framing, annotation growth and export against a synthetic frozen desktop.
// Coordinates stay relative to the whole desktop bitmap, including initially hidden pixels.
template <typename Check> void testRetainedDesktop(const std::wstring& directory, Check check) {
    const RECT initial{140, 100, 300, 220};
    auto desktop = []() {
        auto bitmap = std::make_unique<Bitmap>(480, 320, PixelFormat32bppARGB);
        Graphics g(bitmap.get());
        g.Clear(Color(Swatches[5]));
        SolidBrush left{Color(Swatches[3])}, right{Color(Swatches[4])}, top{Color(Swatches[0])},
            bottom{Color(Swatches[2])}, window{Color(0xffd5e7fc)};
        g.FillRectangle(&left, 0, 0, 10, 320);
        g.FillRectangle(&right, 470, 0, 10, 320);
        g.FillRectangle(&top, 0, 0, 480, 10);
        g.FillRectangle(&bottom, 0, 310, 480, 10);
        g.FillRectangle(&window, 140, 100, 160, 120);
        return bitmap;
    };
    auto start = [&](RECT frame = RECT{140, 100, 300, 220}) {
        app.frozen = desktop();
        app.desktop = {-1920, -200, -1440, 120};
        app.captureRestore = false;
        finishCapture(frame);
        app.userZoom = 1;
        app.layout = calculateLayout(app.main);
    };
    auto pixel = [](Bitmap& bitmap, int x, int y) {
        Color value;
        return bitmap.GetPixel(x, y, &value) == Ok ? value.GetValue() : ARGB{0};
    };
    auto exportPixel = [&](Bitmap& bitmap, POINT original) {
        RECT bounds = canvasBounds();
        return pixel(bitmap, original.x - bounds.left, original.y - bounds.top);
    };
    auto dragCorner = [&](int handle, PointF target) {
        app.layout = calculateLayout(app.main);
        mouseDown(toScreen(canvasCorners(canvasBounds())[handle]));
        PointF release = toScreen(target);
        mouseMove(release);
        mouseUp();
        app.layout = calculateLayout(app.main);
    };
    start();
    const Bitmap* retained = app.image.get();
    check(app.image->GetWidth() == 480 && app.image->GetHeight() == 320 && !app.frozen &&
              EqualRect(&app.canvas.bounds, &initial) &&
              EqualRect(&app.canvas.manualFrame, &initial),
          L"Small selection retains full frozen desktop and stores only its initial visible frame");
    auto first = flattened();
    check(first && first->GetWidth() == 160 && first->GetHeight() == 120 &&
              pixel(*first, 0, 0) == pixel(*app.image, 140, 100),
          L"Initial export contains precisely selected area with correct desktop pixel origin");
    dragCorner(0, {70, 50});
    auto revealed = flattened();
    check(
        canvasBounds().left == 70 && canvasBounds().top == 50 &&
            exportPixel(*revealed, {90, 70}) == Swatches[5] &&
            exportPixel(*revealed, {140, 100}) == 0xffd5e7fc && app.image.get() == retained,
        L"Corner expansion reveals retained desktop above and left without moving original pixels");
    undo(false);
    RECT restored = canvasBounds();
    check(EqualRect(&restored, &initial) && app.image.get() == retained,
          L"Undo initial frame expansion restores selected area while retaining all context");
    undo(true);
    check(canvasBounds().left == 70 && canvasBounds().top == 50,
          L"Redo restores revealed desktop frame");
    dragCorner(2, {400, 280});
    revealed = flattened();
    check(exportPixel(*revealed, {350, 260}) == Swatches[5],
          L"Opposite corner reveals retained desktop below and right");
    dragCorner(0, {140, 100});
    dragCorner(2, {300, 220});
    restored = canvasBounds();
    check(EqualRect(&restored, &initial) && app.image.get() == retained,
          L"Reframing back to original selection never discards hidden desktop pixels");
    dragCorner(0, {0, 0});
    dragCorner(2, {480, 320});
    auto whole = flattened();
    check(whole->GetWidth() == 480 && whole->GetHeight() == 320 &&
              pixel(*whole, 5, 80) == Swatches[3] && pixel(*whole, 475, 80) == Swatches[4] &&
              pixel(*whole, 200, 5) == Swatches[0] && pixel(*whole, 200, 315) == Swatches[2],
          L"Stretching to full desktop reveals exact edge pixels on all four sides");
    dragCorner(2, {540, 370});
    auto outside = flattened();
    check(app.canvas.color == dominantEdgeColor(2 | 8) &&
              pixel(*outside, 539, 369) == app.canvas.color &&
              pixel(*outside, 475, 80) == Swatches[4],
          L"Synthetic fill begins beyond full desktop limits and samples its real outer edges");

    start();
    setTool(Tool::Arrow);
    app.defaults[static_cast<int>(Tool::Arrow)].size = 12;
    Layout stationary = app.layout;
    mouseDown(toScreen({200, 160}));
    mouseMove(toScreen({520, 160}));
    RECT maximum = canvasBounds();
    mouseMove(toScreen({360, 160}));
    RECT held = canvasBounds();
    check(maximum.right > 480 && EqualRect(&maximum, &held) &&
              std::abs(app.layout.imageX - stationary.imageX) < 0.01f &&
              std::abs(app.layout.imageY - stationary.imageY) < 0.01f,
          L"Annotation growth holds maximum excursion and stationary desktop origin during drag");
    PointF release = toScreen({360, 160});
    SendMessageW(app.main, WM_LBUTTONUP, 0,
                 MAKELPARAM(static_cast<int>(std::round(release.X)),
                            static_cast<int>(std::round(release.Y))));
    RECT grown = canvasBounds();
    auto annotated = flattened();
    check(
        grown.left == initial.left && grown.top == initial.top && grown.right > 360 &&
            grown.right < 480 && grown.bottom == initial.bottom &&
            exportPixel(*annotated, {340, 110}) == Swatches[5] &&
            std::abs(app.items.front().a.X - 200) < 0.1f &&
            std::abs(app.items.front().b.X - 360) < 0.1f,
        L"Final arrow extension reveals real desktop and trims unused margin to final annotation");
    undo(false);
    restored = canvasBounds();
    check(EqualRect(&restored, &initial) && app.items.empty(),
          L"Undo growing annotation restores original selection rather than entire desktop");
    undo(true);
    RECT redone = canvasBounds();
    check(EqualRect(&redone, &grown) && app.items.size() == 1,
          L"Redo restores annotation and only its final revealed frame");
    app.layout = calculateLayout(app.main);
    app.tool = Tool::Select;
    mouseDown(toScreen(app.items.front().b));
    release = toScreen({270, 160});
    mouseMove(release);
    SendMessageW(app.main, WM_LBUTTONUP, 0,
                 MAKELPARAM(static_cast<int>(std::round(release.X)),
                            static_cast<int>(std::round(release.Y))));
    restored = canvasBounds();
    check(EqualRect(&restored, &initial), L"Retracting annotation inside selected area restores "
                                          L"initial crop without revealing full desktop");

    start();
    app.frozen = desktop();
    const Bitmap* frozen = app.frozen.get();
    finishCapture({-20, -40, 70, 80});
    check(app.image.get() == frozen && canvasBounds().left == 0 && canvasBounds().top == 0 &&
              canvasBounds().right == 70 && canvasBounds().bottom == 80,
          L"Selection clips to desktop boundaries and transfers frozen bitmap without cloning");
    start();
    dragCorner(0, {90, 60});
    testSaveDialogPath = directory + L"\\retained-desktop-export.png";
    int exportsBefore = testExportCompletions;
    saveImage(true);
    {
        Bitmap saved(testSavedPath.c_str());
        check(saved.GetLastStatus() == Ok && saved.GetWidth() == 210 && saved.GetHeight() == 160 &&
                  pixel(saved, 0, 0) == Swatches[5] && testExportCompletions == exportsBefore + 1,
              L"Save exports visible revealed frame instead of hidden full desktop");
    }
    auto visible = flattened();
    HGLOBAL dib = makeDib(*visible);
    bool correctDib = false;
    if (dib) {
        auto* info = static_cast<BITMAPINFOHEADER*>(GlobalLock(dib));
        if (info) {
            correctDib = info->biWidth == 210 && info->biHeight == -160;
            GlobalUnlock(dib);
        }
        GlobalFree(dib);
    }
    check(correctDib, L"Clipboard bitmap contains only visible frame after revealing context");
    dragCorner(2, {480, 320});
    check(app.image->GetWidth() == 480 && app.image->GetHeight() == 320 &&
              pixel(*app.image, 475, 80) == Swatches[4] && canvasBounds().right == 480,
          L"Saving preserves full frozen source for further editing in the same session");
    app.frozen = desktop();
    const Bitmap* previous = app.image.get();
    RECT previousFrame = canvasBounds();
    finishCapture({700, 400, 800, 500});
    restored = canvasBounds();
    check(app.image.get() == previous && app.frozen && EqualRect(&restored, &previousFrame),
          L"Out-of-desktop selection does not discard previous editable screenshot");
    cancelCapture();

    // Preview uses the real editor renderer with only synthetic source pixels.
    start();
    dragCorner(0, {70, 50});
    dragCorner(2, {410, 275});
    RECT client{};
    GetClientRect(app.main, &client);
    Bitmap preview(client.right, client.bottom, PixelFormat32bppARGB);
    {
        Graphics g(&preview);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        renderEditor(g, app.main);
    }
    check(encodePng(preview, (directory + L"\\retained-desktop-editor.png").c_str()),
          L"Retained desktop framing preview uses production editor renderer");
}
