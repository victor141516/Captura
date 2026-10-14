// Adversarial OCR aspect ratios must not distort the replacement typeface.
// Compare the result against glyphs rendered directly at ordinary font sizes.
template <typename Check> void testLoremTypography(Check check) {
    using namespace captura;
    FontFamily family(L"Segoe UI");
    for (PointF size : {PointF(180, 18), PointF(18, 180)}) {
        Bitmap source(240, 240, PixelFormat32bppARGB);
        RectF box(25, 25, size.X, size.Y);
        {
            Graphics g(&source);
            g.Clear(Color(0xffffffff));
            SolidBrush black(Color(0xff000000));
            g.FillRectangle(&black, box);
        }
        OcrReport report;
        report.state = OcrState::Ready;
        report.lines = {{{{L"Q", box}}}};
        const auto original = redactionPixels(source);
        for (uint32_t seed : {17u, 71u, 917u}) {
            auto prepared = prepareLoremImage(
                std::unique_ptr<Bitmap>(source.Clone(0, 0, 240, 240, PixelFormat32bppRGB)), report,
                seed);
            check(bool(prepared), L"Unusual OCR bounds still produce a Lorem replacement");
            if (!prepared)
                continue;
            auto actual = redactionPixels(*prepared);
            bool natural = false;
            for (wchar_t letter : std::wstring(L"AEILMNSTU")) {
                GraphicsPath unit;
                unit.AddString(&letter, 1, &family, FontStyleRegular, 32, PointF(0, 0),
                               StringFormat::GenericTypographic());
                RectF ink;
                unit.GetBounds(&ink);
                float fontSize = 32 * std::min(box.Width / ink.Width, box.Height / ink.Height);
                GraphicsPath glyph;
                glyph.AddString(&letter, 1, &family, FontStyleRegular, fontSize, PointF(0, 0),
                                StringFormat::GenericTypographic());
                glyph.GetBounds(&ink);
                Matrix position;
                position.Translate(box.X - ink.X, box.Y + (box.Height - ink.Height) / 2 - ink.Y);
                glyph.Transform(&position);
                Bitmap expected(240, 240, PixelFormat32bppARGB);
                {
                    Graphics g(&expected);
                    g.Clear(Color(0xffffffff));
                    g.SetSmoothingMode(SmoothingModeAntiAlias);
                    SolidBrush black(Color(0xff000000));
                    g.FillPath(&black, &glyph);
                }
                auto pixels = redactionPixels(expected);
                bool matches = pixels.size() == actual.size();
                for (size_t n = 0; n < pixels.size() && matches; ++n)
                    matches &= std::abs(int(pixels[n] & 255) - int(actual[n] & 255)) <= 3;
                natural |= matches;
            }
            check(
                natural,
                L"Lorem glyphs match a normal typeface without horizontal or vertical distortion");
        }
        check(redactionPixels(source) == original,
              L"Natural typography leaves the source immutable");
    }
}
