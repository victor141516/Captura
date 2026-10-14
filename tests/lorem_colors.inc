template <typename Check> void testLoremColors(const std::wstring& directory, Check check) {
    using namespace captura;
    Bitmap source(900, 330, PixelFormat32bppARGB);
    OcrReport report;
    report.state = OcrState::Ready;
    std::vector<RectF> blueAreas;
    RectF faintArea, whiteArea;
    {
        Graphics g(&source);
        g.Clear(Color(0xffffffff));
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        FontFamily family(L"Segoe UI");
        auto word = [&](const wchar_t* text, float x, float y, ARGB color) {
            GraphicsPath glyphs;
            glyphs.AddString(text, int(wcslen(text)), &family, FontStyleRegular, 30, PointF(x, y),
                             StringFormat::GenericTypographic());
            RectF bounds;
            glyphs.GetBounds(&bounds);
            SolidBrush ink{Color(color)};
            g.FillPath(&ink, &glyphs);
            report.lines.push_back({{{text, bounds}}});
            return bounds;
        };
        blueAreas.push_back(word(L"Blue sentence keeps its original color", 25, 25, 0xff1b76d1));
        auto start = word(L"Ordinary", 25, 95, 0xff202833);
        blueAreas.push_back(word(L"blue hyperlink", start.GetRight() + 10, 95, 0xff1b76d1));
        word(L"ordinary words continue", blueAreas.back().GetRight() + 10, 95, 0xff202833);
        faintArea = word(L"Very light blue text", 25, 165, 0xfffafcff);
        SolidBrush blue(Color(0xff1b76d1));
        g.FillRectangle(&blue, 20, 235, 850, 70);
        whiteArea = word(L"White text on a blue background", 25, 240, 0xffffffff);
    }
    const auto original = redactionPixels(source);
    report.loremImage = prepareLoremImage(
        std::unique_ptr<Bitmap>(source.Clone(0, 0, 900, 330, PixelFormat32bppRGB)), report, 917);
    check(bool(report.loremImage), L"Colored Lorem preparation produces a reusable bitmap");
    if (!report.loremImage)
        return;
    const auto prepared = redactionPixels(*report.loremImage);
    auto count = [&](RectF area, const auto& pixels, auto predicate) {
        int found = 0;
        for (int y = int(area.Y); y < int(std::ceil(area.GetBottom())); ++y)
            for (int x = int(area.X); x < int(std::ceil(area.GetRight())); ++x)
                found += predicate(pixels[size_t(y) * 900 + x]);
        return found;
    };
    auto closeColor = [](ARGB a, ARGB b, int tolerance) {
        return std::abs(int((a >> 16) & 255) - int((b >> 16) & 255)) <= tolerance &&
               std::abs(int((a >> 8) & 255) - int((b >> 8) & 255)) <= tolerance &&
               std::abs(int(a & 255) - int(b & 255)) <= tolerance;
    };
    for (const auto& area : blueAreas)
        check(count(area, prepared, [&](ARGB pixel) { return closeColor(pixel, 0xff1b76d1, 3); }) >
                  50,
              L"Lorem preserves blue foreground in uniform lines and inline hyperlinks");
    check(count(faintArea, prepared,
                [&](ARGB pixel) {
                    return closeColor(pixel, 0xfffafcff, 3) && pixel != 0xffffffff;
                }) > 50 &&
              count(faintArea, prepared, [](ARGB pixel) { return (pixel & 255) < 240; }) == 0,
          L"Faint colored lettering never falls back to black or white contrast text");
    check(count(whiteArea, prepared, [](ARGB pixel) { return pixel == 0xffffffff; }) > 50,
          L"Lorem preserves white foreground on a colored background");
    for (auto mode : {RedactionMode::LoremGaussian, RedactionMode::LoremPixelate}) {
        auto result = redact(source, {0, 0, 900, 330}, mode, 4, &report, 0xffffffff);
        bool colored = bool(result.bitmap);
        if (colored) {
            auto pixels = redactionPixels(*result.bitmap);
            for (const auto& area : blueAreas)
                colored &= count(area, pixels, [](ARGB p) {
                               return int(p & 255) - int((p >> 16) & 255) > 40;
                           }) > 50;
        }
        check(colored, L"Both Lorem filters retain blue lettering before blending with background");
    }
    check(redactionPixels(source) == original && redactionPixels(*report.loremImage) == prepared,
          L"Colored redaction leaves original and prepared screenshots immutable");
    auto job = ocrService.start(source, nullptr, 0);
    ULONGLONG started = GetTickCount64();
    while (!job->complete.load(std::memory_order_acquire) && GetTickCount64() - started < 30000)
        Sleep(5);
    check(job->complete, L"Real Windows OCR completes on the colored lettering fixture");
    if (job->complete && job->report.state != OcrState::Unavailable) {
        bool colored = job->report.state == OcrState::Ready && bool(job->report.loremImage);
        if (colored) {
            auto pixels = redactionPixels(*job->report.loremImage);
            for (const auto& area : blueAreas)
                colored &=
                    count(area, pixels, [&](ARGB p) { return closeColor(p, 0xff1b76d1, 3); }) > 50;
        }
        check(colored, L"Real OCR retains original blue in both lines and inline color spans");
    }
    Bitmap comparison(1800, 370, PixelFormat32bppARGB);
    {
        Graphics g(&comparison);
        g.Clear(Color(0xffffffff));
        drawText(g, L"Original colors", {0, 0, 900, 38}, 18, Ink, true, true);
        drawText(g, L"Prepared Lorem colors", {900, 0, 900, 38}, 18, Ink, true, true);
        g.DrawImage(&source, 0, 40);
        g.DrawImage(report.loremImage.get(), 900, 40);
    }
    encodePng(comparison, (directory + L"\\lorem-colors-comparison.png").c_str());
}
