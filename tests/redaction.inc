// Synthetic secrets, never real screen pixels or the user's clipboard.
std::vector<ARGB> redactionPixels(Bitmap& bitmap) {
    Rect region(0, 0, bitmap.GetWidth(), bitmap.GetHeight());
    BitmapData data{};
    if (bitmap.LockBits(&region, ImageLockModeRead, PixelFormat32bppARGB, &data) != Ok)
        return {};
    std::vector<ARGB> pixels(size_t(region.Width) * region.Height);
    for (int y = 0; y < region.Height; ++y)
        memcpy(pixels.data() + size_t(y) * region.Width,
               static_cast<const BYTE*>(data.Scan0) + ptrdiff_t(y) * data.Stride,
               size_t(region.Width) * 4);
    bitmap.UnlockBits(&data);
    return pixels;
}
#include "gaussian.inc"
#include "lorem_lines.inc"
template <typename Check> void testRedaction(const std::wstring& directory, Check check) {
    using namespace captura;
    testGaussian(directory, check);
    testLoremLines(directory, check);
    app.image = std::make_unique<Bitmap>(800, 400, PixelFormat32bppARGB);
    auto recognition = std::make_shared<OcrJob>();
    recognition->report.state = OcrState::Ready;
    recognition->report.language = L"en-US";
    recognition->complete = true;
    app.ocr = recognition;
    auto addWord = [&](Graphics& g, const wchar_t* text, PointF origin, REAL size, ARGB color) {
        GraphicsPath letters;
        glyphPath(letters, text, size);
        RectF ink;
        letters.GetBounds(&ink);
        Matrix move;
        move.Translate(origin.X - ink.X, origin.Y - ink.Y);
        letters.Transform(&move);
        SolidBrush paint{Color(color)};
        g.FillPath(&paint, &letters);
        recognition->report.lines.push_back(
            {{{text, {origin.X, origin.Y, ink.Width, ink.Height}}}});
    };
    {
        Graphics g(app.image.get());
        g.Clear(Color(0xfff0f4f8));
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        addWord(g, L"PRIVATE123", {50, 50}, 44, 0xff202833);
        addWord(g, L"BLUESECRET", {50, 150}, 40, 0xff087cf0);
        SolidBrush blue{Color(0xff087cf0)};
        g.FillRectangle(&blue, 400, 220, 370, 130);
        addWord(g, L"WHITESECRET", {420, 250}, 36, 0xffffffff);
        addWord(g, L"SOFTTEXT", {420, 355}, 36, 0xffe6e8ea);
        addWord(g, L"A", {60, 250}, 56, 0xff202833);
    }
    const auto original = redactionPixels(*app.image);
    auto prepare = [&](uint32_t seed) {
        std::unique_ptr<Bitmap> copy(app.image->Clone(0, 0, app.image->GetWidth(),
                                                      app.image->GetHeight(), PixelFormat32bppRGB));
        return prepareLoremImage(std::move(copy), recognition->report, seed);
    };
    recognition->report.loremImage = prepare(917);
    check(recognition->report.loremImage != nullptr && redactionPixels(*app.image) == original,
          L"Preparing a whole Lorem screenshot preserves immutable original pixels");
    auto prepared = recognition->report.loremImage;
    auto preparedPixels = redactionPixels(*prepared);
    auto cropped = redact(*app.image, {35, 135, 355, 195}, RedactionMode::LoremPixelate, 1,
                          &recognition->report, 0xffffffff);
    auto expanded = redact(*app.image, {35, 35, 360, 195}, RedactionMode::LoremPixelate, 1,
                           &recognition->report, 0xffffffff);
    auto movedPatch = redact(*app.image, {45, 145, 365, 205}, RedactionMode::LoremPixelate, 1,
                             &recognition->report, 0xffffffff);
    auto sameOverlap = [&](const RedactionResult& a, RECT ra, const RedactionResult& b, RECT rb) {
        if (!a.bitmap || !b.bitmap)
            return false;
        RECT overlap;
        if (!IntersectRect(&overlap, &ra, &rb))
            return false;
        auto first = redactionPixels(*a.bitmap), second = redactionPixels(*b.bitmap);
        for (int y = overlap.top; y < overlap.bottom; ++y)
            for (int x = overlap.left; x < overlap.right; ++x)
                if (first[size_t(y - ra.top) * (ra.right - ra.left) + x - ra.left] !=
                    second[size_t(y - rb.top) * (rb.right - rb.left) + x - rb.left])
                    return false;
        return true;
    };
    check(sameOverlap(cropped, {35, 135, 355, 195}, expanded, {35, 35, 360, 195}),
          L"Resizing Lorem area to include earlier OCR words preserves existing text pixels");
    check(sameOverlap(cropped, {35, 135, 355, 195}, movedPatch, {45, 145, 365, 205}),
          L"Moving Lorem area samples identical text in overlapping screenshot coordinates");
    check(recognition->report.loremImage == prepared &&
              redactionPixels(*prepared) == preparedPixels,
          L"Repeated redaction reads one immutable prepared Lorem screenshot");
    std::atomic<bool> cancelled{true};
    check(!prepareLoremImage(
              std::unique_ptr<Bitmap>(app.image->Clone(0, 0, 800, 400, PixelFormat32bppRGB)),
              recognition->report, 917, &cancelled),
          L"Cancelled Lorem preparation never publishes a partially modified screenshot");
    OcrReport empty;
    empty.state = OcrState::Empty;
    auto emptyImage = prepareLoremImage(
        std::unique_ptr<Bitmap>(app.image->Clone(0, 0, 800, 400, PixelFormat32bppRGB)), empty, 917);
    check(emptyImage && redactionPixels(*emptyImage) == original,
          L"No-text OCR prepares an identical fallback screenshot once");
    app.items.clear();
    app.undo.clear();
    app.redo.clear();
    app.selected = -1;
    app.canvas = {{0, 0, 800, 400}, {0, 0, 800, 400}, 0xffffffff};
    app.userZoom = 1;
    app.pan = {};
    app.pinnedCanvas = false;
    const RECT area{35, 35, 360, 195};
    auto onlyAreaChanged = [&](const std::vector<ARGB>& after, RECT r) {
        bool changed = false;
        if (after.size() != original.size())
            return false;
        for (int y = 0; y < 400; ++y)
            for (int x = 0; x < 800; ++x) {
                size_t at = size_t(y) * 800 + x;
                bool inside = x >= r.left && x < r.right && y >= r.top && y < r.bottom;
                if (!inside && after[at] != original[at])
                    return false;
                if (inside && after[at] != original[at])
                    changed = true;
            }
        return changed;
    };
    for (int mode = 0; mode < 4; ++mode) {
        Item effect;
        effect.tool = Tool::Redact;
        effect.a = {float(area.left), float(area.top)};
        effect.b = {float(area.right), float(area.bottom)};
        effect.style.redaction = mode;
        effect.style.size = 12;
        app.items = {effect};
        app.selected = 0;
        auto output = flattened();
        auto cache = app.items[0].redactionCache;
        check(output && cache && cache->result.bitmap,
              L"All four native redaction modes produce a processed image");
        check(output && onlyAreaChanged(redactionPixels(*output), area),
              L"Each redaction changes only its exact selected rectangle");
        check(cache && cache->result.replacedWords == (mode >= 2 ? 2u : 0u),
              L"Lorem modes replace only detected intersecting words");
        if (output)
            encodePng(*output,
                      (directory + L"\\redaction-mode-" + std::to_wstring(mode) + L".png").c_str());
        if (cache && cache->result.bitmap) {
            auto rgba = redactionPixels(*cache->result.bitmap);
            check(std::all_of(rgba.begin(), rgba.end(), [](ARGB c) { return (c >> 24) == 255; }),
                  L"Processed patch is fully opaque including Gaussian edges");
        }
    }
    check(redactionPixels(*app.image) == original,
          L"Redaction leaves retained original screenshot unchanged");
    RectF letter = recognition->report.lines.back().words.front().bounds;
    RECT partial{LONG(std::round(letter.X + letter.Width * 0.45f)), 242, 110, 308};
    for (int mode = 2; mode < 4; ++mode) {
        app.items[0].a = {float(partial.left), float(partial.top)};
        app.items[0].b = {float(partial.right), float(partial.bottom)};
        app.items[0].style.redaction = mode;
        auto output = flattened();
        check(output && onlyAreaChanged(redactionPixels(*output), partial) &&
                  app.items[0].redactionCache->result.replacedWords == 1,
              L"Cutting through a letter replaces only its selected portion and preserves outside "
              L"pixels exactly");
    }
    auto tiny = redact(*app.image, {50, 50, 51, 51}, RedactionMode::LoremGaussian, 64,
                       &recognition->report, 0xffffffff);
    check(tiny.bitmap && tiny.replacedWords == 1 && tiny.bitmap->GetWidth() == 1,
          L"One-pixel intersection replaces word even when its center lies outside selection");
    auto white = redact(*app.image, {405, 235, 750, 305}, RedactionMode::LoremPixelate, 1,
                        &recognition->report, 0xffffffff);
    if (white.bitmap) {
        auto pixels = redactionPixels(*white.bitmap);
        check(std::count(pixels.begin(), pixels.end(), ARGB(0xff087cf0)) > 1000 &&
                  std::count(pixels.begin(), pixels.end(), ARGB(0xffffffff)) > 100,
              L"Lorem replacement preserves blue background and white foreground estimates");
    } else
        check(false, L"Lorem replacement preserves blue background and white foreground estimates");
    auto blue = redact(*app.image, {35, 135, 355, 195}, RedactionMode::LoremPixelate, 1,
                       &recognition->report, 0xffffffff);
    if (blue.bitmap) {
        auto pixels = redactionPixels(*blue.bitmap);
        check(std::count(pixels.begin(), pixels.end(), ARGB(0xff087cf0)) > 100 &&
                  std::count(pixels.begin(), pixels.end(), ARGB(0xfff0f4f8)) > 1000,
              L"Lorem replacement preserves colored foreground on a light background");
    } else
        check(false, L"Lorem replacement preserves colored foreground on a light background");
    Bitmap grid(12, 8, PixelFormat32bppARGB);
    for (uint32_t seed : {17u, 19u, 71u, 917u}) {
        recognition->report.loremImage = prepare(seed);
        auto matching = redact(*app.image, {35, 135, 355, 195}, RedactionMode::LoremPixelate, 1,
                               &recognition->report, 0xffffffff);
        int left = 320, right = -1;
        if (matching.bitmap) {
            auto pixels = redactionPixels(*matching.bitmap);
            for (int y = 0; y < 60; ++y)
                for (int x = 0; x < 320; ++x)
                    if (pixels[y * 320 + x] != ARGB(0xfff0f4f8)) {
                        left = std::min(left, x);
                        right = std::max(right, x);
                    }
        }
        const auto& word = recognition->report.lines[1].words[0];
        check(right >= left && std::abs((right - left + 1) - word.bounds.Width) <= 3,
              L"Lorem replacement covers the original word width across different random seeds");
    }
    auto lowContrast = redact(*app.image, {410, 345, 740, 395}, RedactionMode::LoremPixelate, 1,
                              &recognition->report, 0xffffffff);
    bool preservedGray = false;
    if (lowContrast.bitmap)
        for (ARGB color : redactionPixels(*lowContrast.bitmap)) {
            int r = int((color >> 16) & 255), g = int((color >> 8) & 255), b = int(color & 255);
            preservedGray |=
                std::abs(r - 230) < 3 && std::abs(g - 232) < 3 && std::abs(b - 234) < 3;
        }
    check(preservedGray, L"Lorem color estimation preserves low-contrast gray text instead of "
                         L"replacing it with black");
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 12; ++x)
            grid.SetPixel(x, y, Color(255, BYTE(x * 20), BYTE(y * 30), BYTE((x + y) * 10)));
    auto blocks = redact(grid, {0, 0, 12, 8}, RedactionMode::Pixelate, 4, nullptr, 0xffffffff);
    auto fine = redact(grid, {0, 0, 12, 8}, RedactionMode::Pixelate, 1, nullptr, 0xffffffff);
    auto coarse = redact(grid, {0, 0, 12, 8}, RedactionMode::Pixelate, 8, nullptr, 0xffffffff);
    check(fine.bitmap && redactionPixels(*fine.bitmap) == redactionPixels(grid),
          L"Pixelate at minimum strength preserves exact source pixels");
    bool uniform = blocks.bitmap != nullptr;
    if (blocks.bitmap) {
        auto pixels = redactionPixels(*blocks.bitmap);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 12; ++x)
                uniform &= pixels[y * 12 + x] == pixels[(y / 4 * 4) * 12 + x / 4 * 4];
        uniform &=
            pixels[0] == ARGB(0xff1e2d1e); // Exact area average, without filtered upsampling.
    }
    check(uniform && coarse.bitmap &&
              redactionPixels(*coarse.bitmap) != redactionPixels(*blocks.bitmap),
          L"Pixelation uses uniform averaged blocks and grows them with strength");
    check(
        !redact(grid, {0, 0, 12, 8}, RedactionMode::LoremGaussian, 12, nullptr, 0xffffffff).bitmap,
        L"Lorem processing never falls back to the original when OCR is unavailable");
    app.items.clear();
    app.undo.clear();
    app.redo.clear();
    app.layout = calculateLayout(app.main);
    keyDown('B', false, false);
    app.defaults[int(Tool::Redact)].redaction = 1;
    mouseDown(toScreen({35, 35}));
    releasePointer(toScreen({360, 195}));
    check(app.items.size() == 1 && app.items[0].tool == Tool::Redact &&
              handles(app.items[0]).size() == 4,
          L"B creates an editable redaction rectangle with four corner handles");
    size_t count = app.items.size();
    mouseDown(toScreen({600, 150}));
    releasePointer(toScreen({600, 185}));
    check(app.items.size() == count, L"Zero-width redaction gesture leaves no invisible item");
    mouseDown(toScreen({600, 150}));
    releasePointer(toScreen({601, 151}));
    check(app.items.size() == count + 1 && redactionRegion(app.items.back()).right == 601,
          L"Single-pixel redaction is retained as a valid region");
    perform(240);
    app.selected = 0;
    auto processed = flattened();
    check(app.items[0].redactionCache->source == app.image.get(),
          L"Ordinary pixelation uses the original source screenshot");
    auto firstCache = app.items[0].redactionCache;
    processed = flattened();
    check(firstCache == app.items[0].redactionCache,
          L"Unchanged redaction reuses processed pixels across rendering and export");
    perform(263);
    processed = flattened();
    auto loremCache = app.items[0].redactionCache;
    check(loremCache->source == recognition->report.loremImage.get(),
          L"Lorem rectangle uses the OCR job's shared prepared screenshot");
    check(app.items[0].style.redaction == 3 && loremCache != firstCache &&
              loremCache->result.replacedWords == 2,
          L"Mode selector updates selected rectangle and rebuilds its effect");
    perform(221);
    processed = flattened();
    check(app.items[0].style.size == 13 && app.items[0].redactionCache != loremCache,
          L"Strength changes update selected effect and invalidate cached pixels");
    undo(false);
    check(app.items[0].style.size == 12, L"Undo restores previous redaction strength");
    setTool(Tool::Select);
    app.selected = 0;
    auto stableLorem = recognition->report.loremImage;
    auto stablePixels = redactionPixels(*stableLorem);
    mouseDown(toScreen({200, 100}));
    releasePointer(toScreen({230, 125}));
    RECT moved = redactionRegion(app.items[0]);
    processed = flattened();
    check(moved.left == 65 && moved.top == 60 &&
              onlyAreaChanged(redactionPixels(*processed), moved),
          L"Moving redaction processes its new underlying location and restores its previous "
          L"location");
    undo(false);
    setTool(Tool::Select);
    app.selected = 0;
    mouseDown(toScreen({360, 195}));
    releasePointer(toScreen({390, 210}));
    check(redactionRegion(app.items[0]).right == 390 && redactionRegion(app.items[0]).bottom == 210,
          L"Redaction corner handles resize the actual processing region");
    check(recognition->report.loremImage == stableLorem &&
              redactionPixels(*stableLorem) == stablePixels,
          L"Real pointer move, corner resize and undo do not regenerate Lorem content");
    undo(false);
    setTool(Tool::TextSelect);
    testCopiedText = L"clipboard retained";
    mouseDown(toScreen({35, 35}));
    releasePointer(toScreen({360, 195}));
    check(testCopiedText == L"clipboard retained",
          L"OCR text copy cannot expose original words intersecting a redaction");
    setTool(Tool::Select);
    app.selected = 0;
    perform(240);
    check(app.items.empty() && redactionPixels(*flattened()) == original,
          L"Deleting redaction restores original visible pixels");
    setTool(Tool::TextSelect);
    mouseDown(toScreen({35, 35}));
    releasePointer(toScreen({360, 195}));
    check(testCopiedText == L"PRIVATE123\r\nBLUESECRET",
          L"Removing redaction restores original OCR selection");
    Item cover;
    cover.tool = Tool::Redact;
    cover.a = {35, 35};
    cover.b = {360, 195};
    cover.style.redaction = 3;
    cover.style.size = 1;
    app.items = {cover};
    auto solo = redactionPixels(*flattened());
    Item prior = cover;
    prior.style.redaction = 0;
    app.items = {prior, cover};
    check(redactionPixels(*flattened()) == solo,
          L"Earlier ordinary blur cannot reintroduce original text into a later Lorem rectangle");
    prior.style.redaction = 1;
    app.items = {prior, cover};
    check(redactionPixels(*flattened()) == solo,
          L"Earlier ordinary pixelation cannot overwrite the prepared Lorem base");
    // Earlier annotations participate in effects; cache tracks their content, not hover state.
    Item annotation;
    annotation.tool = Tool::Text;
    annotation.a = {400, 35};
    annotation.text = L"Annotation";
    annotation.style.size = 32;
    Item effect;
    effect.tool = Tool::Redact;
    effect.a = {385, 20};
    effect.b = {700, 100};
    effect.style.redaction = 1;
    app.items = {annotation, effect};
    processed = flattened();
    auto beforeCache = app.items[1].redactionCache;
    auto beforePixels = redactionPixels(*processed);
    app.items[0].style.color = 0xff087cf0;
    processed = flattened();
    check(app.items[1].redactionCache != beforeCache && redactionPixels(*processed) != beforePixels,
          L"Redaction includes earlier annotations and refreshes when their content changes");
    recognition->complete = false;
    app.items = {effect};
    app.items[0].style.redaction = 3;
    processed = flattened();
    Color covered;
    processed->GetPixel(450, 60, &covered);
    check(!app.items[0].redactionCache->result.bitmap && covered.GetValue() == 0xff758294,
          L"Pending OCR exports an opaque cover instead of original pixels in a Lorem rectangle");
    recognition->complete = true;
    // Real system OCR sees the substituted pixels, not the synthetic private string.
    auto substituted = redact(*app.image, {35, 35, 360, 110}, RedactionMode::LoremGaussian, 1,
                              &recognition->report, 0xffffffff);
    if (substituted.bitmap) {
        auto job = ocrService.start(*substituted.bitmap, nullptr, 0);
        auto started = GetTickCount64();
        while (!job->complete.load(std::memory_order_acquire) && GetTickCount64() - started < 30000)
            Sleep(5);
        check(job->complete.load(std::memory_order_acquire),
              L"Native OCR on processed output completes");
        if (job->complete && job->report.state != OcrState::Unavailable) {
            auto read = selectedText(job->report, {0, 0, 325, 75});
            check(job->report.state == OcrState::Ready && !read.empty() &&
                      read.find(L"PRIVATE") == std::wstring::npos &&
                      read.find(L"123") == std::wstring::npos,
                  L"Windows OCR on mildly blurred Lorem output finds replacement text instead of "
                  L"original secret");
        }
    }
    // A four-mode visual comparison and actual production editor preview.
    Bitmap comparison(1680, 320, PixelFormat32bppARGB);
    {
        Graphics g(&comparison);
        g.Clear(Color(0xffffffff));
        for (int mode = 0; mode < 4; ++mode) {
            drawText(g, ui::kRedactionModes[mode], {float(mode * 420), 8, 420, 34}, 16, Ink, true,
                     true);
            auto result =
                redact(*app.image, area, RedactionMode(mode), 6, &recognition->report, 0xffffffff);
            if (result.bitmap)
                g.DrawImage(result.bitmap.get(), 420 * mode + 45, 90);
        }
        encodePng(comparison, (directory + L"\\redaction-comparison.png").c_str());
    }
    app.items = {effect};
    app.items[0].a = {35, 35};
    app.items[0].b = {360, 195};
    app.items[0].style.redaction = 3;
    app.items[0].style.size = 6;
    setTool(Tool::Redact);
    app.selected = 0;
    app.layout = calculateLayout(app.main);
    RECT client;
    GetClientRect(app.main, &client);
    Bitmap editor(client.right, client.bottom, PixelFormat32bppARGB);
    {
        Graphics g(&editor);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        renderEditor(g, app.main);
    }
    encodePng(editor, (directory + L"\\redaction-editor.png").c_str());
    // Bound an interactive raster, then ensure export ignores the preview resolution.
    app.image = std::make_unique<Bitmap>(1600, 900, PixelFormat32bppARGB);
    {
        Graphics g(app.image.get());
        g.Clear(Color(0xfff0f4f8));
        drawText(g, L"Preview test", {30, 50, 1000, 120}, 80);
    }
    app.canvas = {{0, 0, 1600, 900}, {0, 0, 1600, 900}, 0xffffffff};
    check(!redact(*app.image, {20, 20, 1500, 850}, RedactionMode::LoremPixelate, 1,
                  &recognition->report, 0xffffffff)
               .bitmap,
          L"A prepared image with obsolete capture dimensions is rejected");
    recognition->report.loremImage = prepare(917);
    app.items = {effect};
    app.items[0].a = {20, 20};
    app.items[0].b = {1500, 850};
    app.items[0].style.redaction = 0;
    app.selected = 0;
    app.dragging = true;
    {
        Bitmap preview(1600, 900, PixelFormat32bppARGB);
        Graphics g(&preview);
        drawItem(g, app.items[0], true);
    }
    auto previewCache = app.items[0].redactionCache;
    check(previewCache->result.bitmap && previewCache->result.bitmap->GetWidth() <= 384,
          L"Interactive large-area redaction bounds effect raster size");
    processed = flattened();
    check(app.items[0].redactionCache != previewCache &&
              app.items[0].redactionCache->result.bitmap->GetWidth() == 1480,
          L"Export during an active gesture rebuilds full-resolution effect pixels");
    app.items.insert(app.items.begin(), annotation);
    app.selected = 0;
    {
        Bitmap preview(1600, 900, PixelFormat32bppARGB);
        Graphics g(&preview);
        drawItem(g, app.items[1], true);
    }
    check(app.items[1].redactionCache->preview &&
              app.items[1].redactionCache->result.bitmap->GetWidth() <= 384,
          L"Editing an earlier annotation uses a bounded preview for dependent redaction effects");
    app.dragging = false;
    app.selected = -1;
    // Record performance without enforcing machine-dependent timing thresholds.
    FILE* measurements = nullptr;
    _wfopen_s(&measurements, (directory + L"\\redaction-performance.txt").c_str(), L"wb");
    if (measurements) {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        for (int mode = 0; mode < 4; ++mode)
            for (unsigned edge : {0u, 384u}) {
                LARGE_INTEGER start, end;
                QueryPerformanceCounter(&start);
                auto result = redact(*app.image, {20, 20, 1500, 850}, RedactionMode(mode), 32,
                                     &recognition->report, 0xffffffff, {}, edge);
                QueryPerformanceCounter(&end);
                double ms = (end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart;
                fprintf(measurements, "%s %s: %.2f ms (%u x %u pixels)\r\n",
                        mode == 0   ? "Gaussian"
                        : mode == 1 ? "Pixelate"
                        : mode == 2 ? "Lorem + Gaussian"
                                    : "Lorem + Pixelate",
                        edge ? "interactive preview" : "full resolution", ms,
                        result.bitmap ? result.bitmap->GetWidth() : 0,
                        result.bitmap ? result.bitmap->GetHeight() : 0);
            }
        fclose(measurements);
    }
}
