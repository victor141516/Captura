// A symmetric input must spread equally along both axes at every strength.
template <typename Check> void testGaussian(const std::wstring& directory, Check check) {
    using namespace captura;
    Bitmap source(257, 257, PixelFormat32bppARGB);
    {
        Graphics g(&source);
        g.Clear(Color(0xff000000));
        SolidBrush white(Color(0xffffffff));
        g.FillRectangle(&white, 112, 112, 33, 33);
    }
    const auto original = redactionPixels(source);
    OcrReport report;
    report.state = OcrState::Empty;
    report.loremImage.reset(source.Clone(0, 0, 257, 257, PixelFormat32bppARGB));
    FILE* measurements = nullptr;
    _wfopen_s(&measurements, (directory + L"\\gaussian-spread.txt").c_str(), L"wb");
    double previous = 0;
    for (int step = 1; step <= 64; ++step) {
        float strength = float(step);
        auto result = redact(source, {0, 0, 257, 257}, RedactionMode::Gaussian, strength, nullptr,
                             0xff000000);
        check(result.bitmap != nullptr, L"Gaussian produces a patch at every slider strength");
        if (!result.bitmap)
            continue;
        auto pixels = redactionPixels(*result.bitmap);
        double mass = 0, xx = 0, yy = 0;
        int asymmetry = 0;
        for (int y = 0; y < 257; ++y)
            for (int x = 0; x < 257; ++x) {
                int value = pixels[size_t(y) * 257 + x] & 255;
                mass += value;
                xx += value * double((x - 128) * (x - 128));
                yy += value * double((y - 128) * (y - 128));
                asymmetry =
                    std::max(asymmetry, std::abs(value - int(pixels[size_t(x) * 257 + y] & 255)));
            }
        xx /= mass;
        yy /= mass;
        if (measurements)
            fprintf(measurements,
                    "strength %.1f: X variance %.3f, Y variance %.3f, transpose error %d\r\n",
                    strength, xx, yy, asymmetry);
        check(asymmetry <= 1 && std::abs(xx - yy) < 0.02 * std::max(xx, yy),
              L"Gaussian has equal horizontal and vertical spread, including low strengths");
        check(xx >= previous, L"Gaussian spread increases monotonically with slider strength");
        check(!previous || (xx - previous) / previous < 0.12,
              L"Gaussian has no abrupt strength transition around the first slider quarter");
        previous = xx;
        // Independent scalar reference: the square's response is the product
        // of two normalized one-dimensional Gaussian integrals at pixel centers.
        double sigma = strength * 0.45;
        int radius = int(std::ceil(3 * sigma));
        std::vector<double> kernel(size_t(2 * radius + 1)), profile(257);
        double total = 0;
        for (int k = -radius; k <= radius; ++k)
            total += kernel[size_t(k + radius)] = std::exp(-k * k / (2 * sigma * sigma));
        for (int x = 0; x < 257; ++x)
            for (int k = -radius; k <= radius; ++k)
                if (x + k >= 112 && x + k <= 144)
                    profile[x] += kernel[size_t(k + radius)] / total;
        int referenceError = 0;
        for (int y = 0; y < 257; ++y)
            for (int x = 0; x < 257; ++x)
                referenceError = std::max(referenceError,
                                          std::abs(int(pixels[size_t(y) * 257 + x] & 255) -
                                                   int(std::round(255 * profile[x] * profile[y]))));
        check(referenceError <= 1, L"Gaussian pixels match a normalized two-dimensional Gaussian");
        auto lorem = redact(source, {0, 0, 257, 257}, RedactionMode::LoremGaussian, strength,
                            &report, 0xff000000);
        check(lorem.bitmap && redactionPixels(*lorem.bitmap) == pixels,
              L"Lorem Gaussian uses the same isotropic filter as ordinary Gaussian");
    }
    if (measurements)
        fclose(measurements);
    check(redactionPixels(source) == original, L"Gaussian leaves the source bitmap immutable");
    for (unsigned edge : {65u, 128u}) {
        auto preview = redact(source, {0, 0, 257, 257}, RedactionMode::Gaussian, 12, nullptr,
                              0xff000000, {}, edge);
        bool symmetric = bool(preview.bitmap);
        if (symmetric) {
            auto pixels = redactionPixels(*preview.bitmap);
            for (unsigned y = 0; y < edge; ++y)
                for (unsigned x = 0; x < edge; ++x)
                    symmetric &= std::abs(int(pixels[size_t(y) * edge + x] & 255) -
                                          int(pixels[size_t(x) * edge + y] & 255)) <= 1;
        }
        check(symmetric, L"Downsampled Gaussian gesture previews spread equally on both axes");
    }
    for (POINT dimensions : {POINT{1, 1}, POINT{1, 31}, POINT{31, 1}, POINT{9, 7}}) {
        Bitmap uniform(dimensions.x, dimensions.y, PixelFormat32bppARGB);
        {
            Graphics g(&uniform);
            g.Clear(Color(0xff29b3e7));
        }
        auto result = redact(uniform, {0, 0, dimensions.x, dimensions.y}, RedactionMode::Gaussian,
                             64, nullptr, 0xff000000);
        bool unchanged = bool(result.bitmap);
        if (unchanged)
            for (ARGB pixel : redactionPixels(*result.bitmap))
                unchanged &= pixel == 0xff29b3e7;
        check(unchanged, L"Gaussian preserves opaque uniform RGB even for one-pixel dimensions");
    }
    Bitmap sample(180, 160, PixelFormat32bppARGB), comparison(1260, 200, PixelFormat32bppARGB);
    {
        Graphics g(&sample);
        g.Clear(Color(0xfff0f4f8));
        drawText(g, L"Sample", {8, 8, 170, 55}, 26, Ink, true);
        SolidBrush blue(Color(0xff087cf0));
        g.FillEllipse(&blue, 60, 80, 60, 60);
    }
    {
        Graphics g(&comparison);
        g.Clear(Color(0xffffffff));
        int column = 0;
        for (float strength : {1.f, 4.f, 8.f, 12.f, 16.f, 20.f, 32.f}) {
            drawText(g, L"Strength " + std::to_wstring(int(strength)),
                     {float(column * 180), 0, 180, 36}, 14, Ink, true, true);
            auto result = redact(sample, {0, 0, 180, 160}, RedactionMode::Gaussian, strength,
                                 nullptr, 0xffffffff);
            if (result.bitmap)
                g.DrawImage(result.bitmap.get(), column * 180, 38);
            ++column;
        }
    }
    encodePng(comparison, (directory + L"\\gaussian-strengths.png").c_str());
}
