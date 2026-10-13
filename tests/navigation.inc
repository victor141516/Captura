// Benchmark real middle-button/wheel handlers and production painting with retained 4K source.
// Hidden native windows and synthetic pixels only; no desktop capture or global input hooks.
template <typename Check> void testNavigation(const std::wstring& directory, Check check) {
    app.image = std::make_unique<Bitmap>(3840, 2160, PixelFormat32bppARGB);
    auto source = fixture();
    {
        Graphics g(app.image.get());
        g.DrawImage(source.get(), 0, 0, 3840, 2160);
    }
    app.canvas = {{1000, 550, 2200, 1450}, {1000, 550, 2200, 1450}, 0xffffffff};
    app.items.clear();
    Item box;
    box.tool = Tool::Rectangle;
    box.a = {1100, 650};
    box.b = {1750, 900};
    app.items.push_back(box);
    Item text;
    text.tool = Tool::Text;
    text.a = {1120, 960};
    text.text = L"Pan and zoom";
    text.style.size = 26;
    text.style.filled = true;
    app.items.push_back(text);
    app.selected = 0;
    app.tool = Tool::Select;
    app.userZoom = 0.5f;
    app.pan = {};
    app.dragging = app.panning = app.creating = app.resizingCanvas = false;
    app.pinnedCanvas = false;
    captureView.reset();
    sourceRaster.reset();
    dragBackground.reset();
    invalidate();
    const Bitmap* retained = app.image.get();
    RECT originalFrame = canvasBounds();
    RECT client{};
    GetClientRect(app.main, &client);
    HDC screen = GetDC(app.main);
    PaintBuffer target;
    if (!target.ensure(screen, client.right, client.bottom)) {
        ReleaseDC(app.main, screen);
        check(false, L"Navigation benchmark buffer allocation");
        return;
    }
    auto paintFrame = [&]() {
        SendMessageW(app.main, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(target.dc), PRF_CLIENT);
        GdiFlush();
    };
    paintFrame();
    int x = static_cast<int>(app.layout.canvas.Width / 2),
        y = static_cast<int>(app.layout.canvas.Y + app.layout.canvas.Height / 2);
    SendMessageW(app.main, WM_MBUTTONDOWN, MK_MBUTTON, MAKELPARAM(x, y));
    check(app.panning && !app.dragging,
          L"Middle button in viewport enters navigation instead of annotation editing");
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    auto measure = [&](auto action) {
        LARGE_INTEGER start{}, end{};
        QueryPerformanceCounter(&start);
        action();
        paintFrame();
        QueryPerformanceCounter(&end);
        return (end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart;
    };
    std::vector<double> panTimes, zoomTimes;
    unsigned panBuildStart = captureView.builds;
    for (int i = 0; i < 65; i++) {
        double ms = measure([&]() {
            SendMessageW(app.main, WM_MOUSEMOVE, MK_MBUTTON,
                         MAKELPARAM(x + (i % 30) * 3, y + (i % 20) * 2));
        });
        if (i >= 5)
            panTimes.push_back(ms);
    }
    unsigned panBuilds = captureView.builds - panBuildStart;
    check(panBuilds == 0,
          L"Middle-button navigation reuses warmed scaled capture for integer pans");
    check(std::abs(app.pan.X - 12) < 0.1f && std::abs(app.pan.Y - 8) < 0.1f,
          L"Middle-button drag follows latest pointer position without accumulating deltas");
    SendMessageW(app.main, WM_MBUTTONUP, 0, MAKELPARAM(x + 12, y + 8));
    check(!app.panning && GetCapture() != app.main,
          L"Middle-button release ends navigation and releases mouse capture");
    unsigned zoomBuildStart = captureView.builds;
    unsigned nativeBuildStart = sourceRaster.builds;
    for (int i = 0; i < 45; i++) {
        int delta = i % 6 < 3 ? WHEEL_DELTA : -WHEEL_DELTA;
        double ms = measure([&]() {
            SendMessageW(app.main, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, static_cast<WORD>(delta)),
                         0);
        });
        if (i >= 5)
            zoomTimes.push_back(ms);
    }
    unsigned zoomBuilds = captureView.builds - zoomBuildStart;
    check(zoomBuilds == 0 && sourceRaster.builds == nativeBuildStart,
          L"Wheel gesture reuses immutable native source instead of rasterizing full capture per "
          L"tick");
    RECT afterFrame = canvasBounds();
    check(app.image.get() == retained && EqualRect(&afterFrame, &originalFrame) &&
              app.items.size() == 2 && app.items[0].a.X == 1100 && app.items[0].b.X == 1750,
          L"Pan and zoom leave full retained source, visible framing and annotations unchanged");
    std::sort(panTimes.begin(), panTimes.end());
    std::sort(zoomTimes.begin(), zoomTimes.end());
    FILE* report = nullptr;
    _wfopen_s(&report, (directory + L"\\navigation-performance.txt").c_str(), L"wb");
    if (report) {
        fprintf(report,
                "4K retained source, 1200 x 900 selected frame, %ld x %ld client\n"
                "60 middle-drag + paint frames (5 warmup): median %.3f ms, P95 %.3f ms; "
                "%u capture rasterizations\n"
                "40 Ctrl-wheel + paint frames (5 warmup): median %.3f ms, P95 %.3f ms; "
                "%u capture rasterizations\n",
                client.right, client.bottom, panTimes[panTimes.size() / 2],
                panTimes[static_cast<size_t>(panTimes.size() * 0.95)], panBuilds,
                zoomTimes[zoomTimes.size() / 2],
                zoomTimes[static_cast<size_t>(zoomTimes.size() * 0.95)], zoomBuilds);
        fclose(report);
    }
    check(app.zooming, L"Wheel gesture enters interactive preview mode");
    SendMessageW(app.main, WM_TIMER, ZoomRefineTimer, 0);
    paintFrame();
    check(!app.zooming && captureView.interpolation == InterpolationModeHighQualityBicubic,
          L"Wheel idle restores high-quality downsampling at final zoom");

    auto pixels = [](PaintBuffer& buffer) {
        std::vector<DWORD> result(static_cast<size_t>(buffer.width) * buffer.height);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = buffer.width;
        info.bmiHeader.biHeight = -buffer.height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        GdiFlush();
        HGDIOBJ previous = SelectObject(buffer.dc, buffer.original);
        int rows = GetDIBits(buffer.dc, buffer.bitmap, 0, buffer.height, result.data(), &info,
                             DIB_RGB_COLORS);
        SelectObject(buffer.dc, previous);
        if (rows != buffer.height)
            result.clear();
        return result;
    };
    PaintBuffer reference;
    check(reference.ensure(screen, client.right, client.bottom),
          L"Navigation reference frame allocation");
    int comparison = 0;
    auto matchesReference = [&]() {
        paintFrame();
        {
            Graphics g(reference.dc);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            renderEditor(g, app.main, -1, true, false);
        }
        auto actual = pixels(target), expected = pixels(reference);
        if (actual.empty() || actual.size() != expected.size())
            return false;
        int differences = 0, maximumDifference = 0, outsideDifference = 0;
        Layout view = calculateLayout(app.main);
        RECT frame = canvasBounds();
        RECT sourceArea{static_cast<LONG>(std::floor(view.imageX + frame.left * view.zoom)),
                        static_cast<LONG>(std::floor(view.imageY + frame.top * view.zoom)),
                        static_cast<LONG>(std::ceil(view.imageX + frame.right * view.zoom)),
                        static_cast<LONG>(std::ceil(view.imageY + frame.bottom * view.zoom))};
        RECT region{client.right, client.bottom, 0, 0};
        for (size_t k = 0; k < actual.size(); k++)
            if ((actual[k] & 0xffffff) != (expected[k] & 0xffffff)) {
                differences++;
                int x = static_cast<int>(k % client.right), y = static_cast<int>(k / client.right);
                if (x < sourceArea.left || x >= sourceArea.right || y < sourceArea.top ||
                    y >= sourceArea.bottom)
                    outsideDifference++;
                region.left = std::min(region.left, static_cast<LONG>(x));
                region.top = std::min(region.top, static_cast<LONG>(y));
                region.right = std::max(region.right, static_cast<LONG>(x));
                region.bottom = std::max(region.bottom, static_cast<LONG>(y));
                for (int shift : {0, 8, 16})
                    maximumDifference =
                        std::max(maximumDifference,
                                 std::abs(static_cast<int>((actual[k] >> shift) & 255) -
                                          static_cast<int>((expected[k] >> shift) & 255)));
            }
        FILE* diagnostics = nullptr;
        _wfopen_s(&diagnostics, (directory + L"\\navigation-pixels.txt").c_str(), L"ab");
        if (diagnostics) {
            fprintf(diagnostics,
                    "Comparison %d: %d differing pixels, maximum channel difference %d, "
                    "bounds %ld,%ld..%ld,%ld\n",
                    ++comparison, differences, maximumDifference, region.left, region.top,
                    region.right, region.bottom);
            fclose(diagnostics);
        }
        // GDI+ rounds filtered PARGB tiles and direct HDC downsampling slightly
        // differently. Accept at most two RGB levels within image pixels only;
        // framing, annotations outside that area and all chrome remain exact.
        return maximumDifference <= 2 && outsideDifference == 0;
    };
    check(matchesReference(),
          L"Final zoom matches full-quality reference within source filter rounding");
    app.panning = true;
    app.userZoom = 0.5f;
    app.pan = {18, -14};
    check(matchesReference(),
          L"Cached navigation chrome and translated capture match complete rendering");
    app.hoverButton = 100;
    check(matchesReference(),
          L"Navigation chrome refreshes hover appearance without stale toolbar pixels");
    app.hoverButton = -1;
    app.pan = {18.25f, -14.5f};
    check(matchesReference(),
          L"Fractional camera translation retains subpixel image and annotation alignment");
    app.userZoom = 8;
    app.pan = {0, 0};
    paintFrame();
    check(
        captureView.bitmap &&
            static_cast<unsigned long long>(captureView.bitmap->GetWidth()) *
                    captureView.bitmap->GetHeight() <=
                16 * 1024 * 1024 &&
            captureView.bitmap->GetWidth() < app.image->GetWidth() * 8,
        L"Maximum zoom retains bounded viewport tile instead of allocating enlarged full desktop");
    unsigned tileBuildStart = captureView.builds;
    app.pan = {150, 100};
    paintFrame();
    check(captureView.builds == tileBuildStart,
          L"Overscan reuses high-zoom tile when panning within cached coverage");
    app.pan = {1200, 900};
    check(matchesReference() && captureView.builds > tileBuildStart,
          L"Panning beyond tile coverage rebuilds correct visible source pixels");

    app.panning = false;
    app.userZoom = 0.5f;
    for (int i = 0; i < 2; i++)
        SendMessageW(app.main, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA), 0);
    check(std::abs(app.userZoom - 0.5f * 1.15f * 1.15f) < 0.0001f,
          L"Queued wheel events accumulate zoom even before a paint updates the layout");
    app.userZoom = 0.5f;
    SendMessageW(app.main, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, 3 * WHEEL_DELTA), 0);
    check(std::abs(app.userZoom - 0.5f * std::pow(1.15f, 3.f)) < 0.0001f,
          L"Wheel delta containing multiple notches preserves every zoom step");
    REAL previousZoom = app.userZoom;
    SendMessageW(app.main, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, 0), 0);
    check(app.userZoom == previousZoom, L"Zero wheel delta does not change zoom");
    SendMessageW(app.main, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA / 2), 0);
    SendMessageW(app.main, WM_MOUSEWHEEL,
                 MAKEWPARAM(MK_CONTROL, static_cast<WORD>(-WHEEL_DELTA / 2)), 0);
    check(std::abs(app.userZoom - previousZoom) < 0.0001f,
          L"High-resolution wheel fractions zoom smoothly and reverse symmetrically");
    SendMessageW(app.main, WM_TIMER, ZoomRefineTimer, 0);
    app.userZoom = 0.5f;
    app.pan = {};
    paintFrame();
    SendMessageW(app.main, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA), 0);
    check(matchesReference(),
          L"Interactive zoom preview preserves viewport clipping and chrome pixels");
    auto exported = flattened();
    check(exported && exported->GetWidth() == 1200 && exported->GetHeight() == 900 &&
              app.image.get() == retained,
          L"Interactive preview has no effect on full-resolution cropped image export");
    SendMessageW(app.main, WM_TIMER, ZoomRefineTimer, 0);
    paintFrame();
    app.userZoom = 0.5f;
    app.pan = {};
    paintFrame();
    SendMessageW(app.main, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA), 0);
    paintFrame();
    mouseDown(toScreen(app.items[0].a));
    paintFrame();
    SendMessageW(app.main, WM_TIMER, ZoomRefineTimer, 0);
    check(matchesReference(),
          L"Idle refinement refreshes preview captured in an active annotation drag backdrop");
    mouseUp();
    paintFrame();
    DWORD handles = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < 10; i++) {
        app.panning = true;
        app.pan = {static_cast<REAL>(i * 5), static_cast<REAL>(i * 3)};
        paintFrame();
    }
    check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == handles,
          L"Repeated navigation reuses native resources without GDI handle growth");
    app.panning = false;
    ReleaseDC(app.main, screen);
}
