// Multiline text with shared baselines and different per-word ink heights.
// Compare fragmented OCR against complete-line OCR, then exercise Windows OCR.
template <typename Check> void testLoremLines(const std::wstring& directory, Check check) {
    using namespace captura;
    Bitmap source(1100, 390, PixelFormat32bppARGB);
    OcrReport fragmented, complete;
    fragmented.state = complete.state = OcrState::Ready;
    {
        Graphics g(&source);
        g.Clear(Color(0xfff0f4f8));
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        FontFamily family(L"Segoe UI");
        Font font(&family, 28, FontStyleRegular, UnitPixel);
        StringFormat format(StringFormat::GenericTypographic());
        format.SetFormatFlags(format.GetFormatFlags() | StringFormatFlagsMeasureTrailingSpaces);
        SolidBrush foreground(Color(0xff202833));
        auto sentence = [&](const std::wstring& text, float left, float top) {
            std::wstring prefix;
            OcrWord whole{text, {}};
            size_t begin = 0;
            bool first = true;
            while (begin < text.size()) {
                size_t end = text.find(L' ', begin);
                if (end == std::wstring::npos)
                    end = text.size();
                std::wstring word = text.substr(begin, end - begin);
                RectF advance;
                if (!prefix.empty())
                    g.MeasureString(prefix.c_str(), int(prefix.size()), &font, PointF(0, 0),
                                    &format, &advance);
                GraphicsPath glyphs;
                glyphs.AddString(word.c_str(), int(word.size()), &family, FontStyleRegular, 28,
                                 PointF(left + advance.Width, top), &format);
                RectF ink;
                glyphs.GetBounds(&ink);
                g.FillPath(&foreground, &glyphs);
                fragmented.lines.push_back({{{word, ink}}});
                ++fragmented.words;
                if (first) {
                    whole.bounds = ink;
                    first = false;
                } else {
                    float right = std::max(whole.bounds.GetRight(), ink.GetRight());
                    float bottom = std::max(whole.bounds.GetBottom(), ink.GetBottom());
                    whole.bounds.Y = std::min(whole.bounds.Y, ink.Y);
                    whole.bounds.Width = right - whole.bounds.X;
                    whole.bounds.Height = bottom - whole.bounds.Y;
                }
                prefix += word + L' ';
                begin = end + 1;
            }
            complete.lines.push_back({{whole}});
        };
        sentence(L"A small app makes sharing screenshots quick and easy.", 30, 40);
        sentence(L"Some words grow tall while others dip below the baseline.", 30, 90);
        sentence(L"Keep every phrase aligned with consistent letter shapes.", 30, 140);
        sentence(L"Move or resize a region without changing the hidden text.", 30, 190);
        sentence(L"Separate column", 30, 300);
        sentence(L"Another column", 780, 300);
    }
    const auto original = redactionPixels(source);
    auto prepare = [&](const OcrReport& report, uint32_t seed) {
        return prepareLoremImage(
            std::unique_ptr<Bitmap>(source.Clone(0, 0, 1100, 390, PixelFormat32bppRGB)), report,
            seed);
    };
    std::shared_ptr<Bitmap> prepared;
    for (uint32_t seed : {17u, 71u, 917u}) {
        prepared = prepare(fragmented, seed);
        auto expected = prepare(complete, seed);
        check(prepared && expected && redactionPixels(*prepared) == redactionPixels(*expected),
              L"Fragmented words with varied ink heights render as one coherent Lorem line");
        OcrReport reordered = fragmented;
        std::reverse(reordered.lines.begin(), reordered.lines.end());
        auto reversed = prepare(reordered, seed);
        check(prepared && reversed && redactionPixels(*prepared) == redactionPixels(*reversed),
              L"OCR fragment order does not change line grouping or stable Lorem content");
    }
    check(redactionPixels(source) == original,
          L"Line-level Lorem preparation keeps original pixels");
    bool outsideUnchanged = bool(prepared);
    if (prepared) {
        auto pixels = redactionPixels(*prepared);
        for (int y = 0; y < 390; ++y)
            for (int x = 0; x < 1100; ++x) {
                bool inside = false;
                for (const auto& line : complete.lines) {
                    RectF box = line.words[0].bounds;
                    inside |=
                        x >= int(std::floor(box.X)) - 2 && x < int(std::ceil(box.GetRight())) + 2 &&
                        y >= int(std::floor(box.Y)) - 2 && y < int(std::ceil(box.GetBottom())) + 2;
                }
                if (!inside)
                    outsideUnchanged &=
                        pixels[size_t(y) * 1100 + x] == original[size_t(y) * 1100 + x];
            }
    }
    check(outsideUnchanged, L"Separate columns and gaps remain intact during line substitution");
    auto job = ocrService.start(source, nullptr, 0);
    ULONGLONG started = GetTickCount64();
    while (!job->complete.load(std::memory_order_acquire) && GetTickCount64() - started < 30000)
        Sleep(5);
    check(job->complete, L"Real Windows OCR completes on a multiline paragraph fixture");
    if (job->complete && job->report.state != OcrState::Unavailable) {
        check(job->report.state == OcrState::Ready && job->report.words >= 35 &&
                  job->report.loremImage,
              L"Real OCR publishes a prepared Lorem image for several multiword lines");
        if (job->report.loremImage) {
            fragmented.loremImage = job->report.loremImage;
            fragmented.lines = job->report.lines;
            prepared = fragmented.loremImage;
        }
    }
    if (!prepared)
        return;
    fragmented.loremImage = prepared;
    auto gaussian =
        redact(source, {0, 0, 1100, 390}, RedactionMode::LoremGaussian, 4, &fragmented, 0xffffffff);
    auto pixelated =
        redact(source, {0, 0, 1100, 390}, RedactionMode::LoremPixelate, 8, &fragmented, 0xffffffff);
    check(gaussian.bitmap && pixelated.bitmap,
          L"Both Lorem redaction modes reuse the prepared multiline layout");
    Bitmap comparison(2200, 850, PixelFormat32bppARGB);
    {
        Graphics g(&comparison);
        g.Clear(Color(0xffffffff));
        drawText(g, L"Original", {0, 0, 1100, 35}, 18, Ink, true, true);
        drawText(g, L"Prepared Lorem lines", {1100, 0, 1100, 35}, 18, Ink, true, true);
        drawText(g, L"Lorem + Gaussian", {0, 425, 1100, 35}, 18, Ink, true, true);
        drawText(g, L"Lorem + Pixelate", {1100, 425, 1100, 35}, 18, Ink, true, true);
        g.DrawImage(&source, 0, 35);
        g.DrawImage(prepared.get(), 1100, 35);
        if (gaussian.bitmap)
            g.DrawImage(gaussian.bitmap.get(), 0, 460);
        if (pixelated.bitmap)
            g.DrawImage(pixelated.bitmap.get(), 1100, 460);
    }
    encodePng(comparison, (directory + L"\\lorem-lines-comparison.png").c_str());
}
