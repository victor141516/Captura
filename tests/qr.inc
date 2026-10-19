// Synthetic QR pixels and clipboard/browser hooks only. No desktop capture or shell launches.
template <typename Check> void testQr(const std::wstring& directory, Check check) {
    using namespace captura;
    // Fixtures generated with segno (error level M): a 29-module link and a 21-module text.
    static const wchar_t* linkRows[] = {
        L"11111110001011000001001111111", L"10000010101001001100001000001",
        L"10111010101010101011001011101", L"10111010000011101100001011101",
        L"10111010011010010001001011101", L"10000010010010011111001000001",
        L"11111110101010101010101111111", L"00000000000010110110100000000",
        L"01110110000100010110000000110", L"01111000000110110101101111101",
        L"11110111011001100110101001010", L"11000100111111010010000000001",
        L"10111110100100110100110100111", L"11100100111100111011001101101",
        L"00100110010100010001101111011", L"01001101110010011010110011001",
        L"01111010001001110000010111000", L"01100001010101101010110100100",
        L"10000011100110010010101101000", L"00001001110011100100011100111",
        L"01010111110000001111111111110", L"00000000111000100110100011011",
        L"11111110000011101111101010110", L"10000010110011100010100010000",
        L"10111010001000010110111111101", L"10111010100101000011000011010",
        L"10111010111001100010100100101", L"10000010100100010011100011010",
        L"11111110000010100011100100010"};
    static const wchar_t* textRows[] = {
        L"111111101000101111111", L"100000100111001000001", L"101110100100001011101",
        L"101110101010001011101", L"101110101110101011101", L"100000101110101000001",
        L"111111101010101111111", L"000000001111000000000", L"100010111000111111001",
        L"001100001010100011010", L"111010100101001010110", L"010101000011110010000",
        L"010100111110110100010", L"000000001111101111011", L"111111101100000011010",
        L"100000100100010010010", L"101110101010110110000", L"101110100001100010111",
        L"101110100010011010000", L"100000100001000111000", L"111111101110110000001"};
    const std::wstring linkText = L"https://example.com/captura?x=1", plainText = L"Hola mundo QR";
    auto drawCode = [](Graphics& g, const wchar_t* const* rows, int size, REAL x, REAL y,
                       REAL module) {
        SolidBrush ink(Color(0xff000000));
        for (int row = 0; row < size; ++row)
            for (int column = 0; column < size; ++column)
                if (rows[row][column] == L'1')
                    g.FillRectangle(&ink, x + column * module, y + row * module, module, module);
    };
    app.image = std::make_unique<Bitmap>(1000, 600, PixelFormat32bppARGB);
    {
        Graphics g(app.image.get());
        g.Clear(Color(0xffffffff));
        drawCode(g, linkRows, 29, 160, 100, 6); // 174 px, inside the visible frame.
        drawCode(g, textRows, 21, 450, 120, 4); // 84 px, needs upscaling.
        drawCode(g, textRows, 21, 820, 300, 5); // Outside the visible frame.
    }
    app.canvas = {{100, 50, 700, 450}, {100, 50, 700, 450}, 0xffffffff};
    app.items.clear();
    app.undo.clear();
    app.redo.clear();
    app.userZoom = 1;
    app.pan = {};
    app.pinnedCanvas = false;
    app.ocr.reset();
    resetQr();
    testOpenedUrl.clear();
    testClipboardSuccess = true;

    check(qrUrl(L"https://a.example/x") == L"https://a.example/x" &&
              qrUrl(L"  HTTP://A.example ") == L"HTTP://A.example" &&
              qrUrl(L"www.example.com/q") == L"https://www.example.com/q" &&
              qrUrl(L"mailto:a@example.com").empty() && qrUrl(L"file:///C:/x").empty() &&
              qrUrl(L"https://a b").empty() && qrUrl(L"plain text").empty(),
          L"Only http(s) and www payloads become links; other schemes are plain content");

    keyDown('Q', false, false);
    check(app.tool == Tool::QrScan && !validIndex(app.selected) && app.qrCodes.empty(),
          L"Q activates Scan QR without selecting annotations or scanning");
    app.layout = calculateLayout(app.main);
    int exports = testExportCompletions;
    testCopiedText = L"previous clipboard";
    mouseDown(toScreen({150, 90}));
    mouseMove(toScreen({250, 200}));
    check(app.scanningQr && app.hasQrArea && app.qrArea.Equals(RectF(150, 90, 100, 110)),
          L"Dragging shows a QR scan rectangle in image coordinates");
    releasePointer(toScreen({345, 285}));
    bool one = app.qrCodes.size() == 1;
    check(one && app.qrCodes[0].text == linkText && app.qrCodes[0].url == linkText &&
              !app.scanningQr && app.hasQrArea,
          L"Dragging around a QR code decodes a link without copying");
    check(one && qrBounds(app.qrCodes[0]).X > 150 && qrBounds(app.qrCodes[0]).Y > 90 &&
              qrBounds(app.qrCodes[0]).GetRight() < 345 &&
              qrBounds(app.qrCodes[0]).GetBottom() < 285 && qrBounds(app.qrCodes[0]).Width > 150 &&
              qrBounds(app.qrCodes[0]).Height > 150,
          L"Decoded corners map back to full-image coordinates inside the scanned area");
    check(testCopiedText == L"previous clipboard" && app.items.empty() && app.undo.empty() &&
              exports == testExportCompletions,
          L"Scanning leaves clipboard, annotation history and editor lifetime intact");
    check(one && qrContains(app.qrCodes[0], {247, 187}) && !qrContains(app.qrCodes[0], {120, 80}) &&
              qrAt({247, 187}) == 0 && qrAt({600, 400}) < 0,
          L"Hit testing uses the decoded quadrilateral");

    mouseDown(toScreen({247, 187}));
    releasePointer(toScreen({250, 190}));
    check(testCopiedText == linkText && testOpenedUrl.empty() && app.qrCodes.size() == 1 &&
              app.qrFeedback.find(L"copied") != std::wstring::npos,
          L"Clicking a found link copies it and keeps the result");
    testCopiedText = L"previous clipboard";
    testCtrlPressed = true;
    mouseDown(toScreen({247, 187}));
    releasePointer(toScreen({247, 187}));
    testCtrlPressed = false;
    check(testOpenedUrl == linkText && testCopiedText == L"previous clipboard",
          L"Ctrl + click opens a link without touching the clipboard");
    testOpenedUrl.clear();
    mouseDown(toScreen({247, 187}));
    releasePointer(toScreen({600, 400}));
    check(testCopiedText == L"previous clipboard" && testOpenedUrl.empty() &&
              app.qrCodes.size() == 1 && !app.scanningQr,
          L"Releasing outside the pressed code neither copies nor rescans");

    mouseDown(toScreen({600, 400}));
    releasePointer(toScreen({600, 400}));
    check(app.qrCodes.size() == 2 && !app.hasQrArea,
          L"Clicking empty image scans the whole visible frame and ignores hidden content");
    int textIndex = -1;
    for (int i = 0; i < static_cast<int>(app.qrCodes.size()); ++i)
        if (app.qrCodes[i].text == plainText && app.qrCodes[i].url.empty())
            textIndex = i;
    check(textIndex >= 0, L"Small QR codes decode after upscaling and are not links");
    testCtrlPressed = true;
    mouseDown(toScreen({492, 162}));
    releasePointer(toScreen({492, 162}));
    testCtrlPressed = false;
    check(testCopiedText == plainText && testOpenedUrl.empty(),
          L"Ctrl + click on plain content copies it instead of opening anything");
    testClipboardSuccess = false;
    testCopiedText = L"intact";
    mouseDown(toScreen({492, 162}));
    releasePointer(toScreen({492, 162}));
    check(testCopiedText == L"intact" && app.qrFeedback.find(L"Could not") != std::wstring::npos,
          L"Failed QR copy shows feedback");
    testClipboardSuccess = true;

    mouseDown(toScreen({600, 380}));
    releasePointer(toScreen({680, 460}));
    check(app.qrCodes.empty() && app.hasQrArea &&
              app.qrFeedback.find(L"No QR") != std::wstring::npos,
          L"Empty area reports that no QR code was found");
    mouseDown(toScreen({600, 400}));
    releasePointer(toScreen({600, 400}));
    {
        RECT client;
        GetClientRect(app.main, &client);
        Bitmap preview(client.right, client.bottom, PixelFormat32bppARGB);
        Graphics g(&preview);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        renderEditor(g, app.main);
        encodePng(preview, (directory + L"\\qr-editor.png").c_str());
    }
    auto exported = flattened();
    Color pixel;
    exported->GetPixel(105, 53, &pixel); // White module inside the link code.
    check(pixel.GetValue() == Color::White && app.items.empty(),
          L"QR overlays never become exported pixels");

    keyDown(VK_ESCAPE, false, false);
    check(app.qrCodes.empty() && !app.hasQrArea && app.qrFeedback.empty(),
          L"Escape clears QR results");
    mouseDown(toScreen({150, 90}));
    mouseMove(toScreen({300, 250}));
    keyDown(VK_ESCAPE, false, false);
    check(!app.scanningQr && !app.hasQrArea && app.qrCodes.empty(),
          L"Escape cancels a QR rectangle without scanning");
    mouseDown(toScreen({150, 90}));
    SendMessageW(app.main, WM_CAPTURECHANGED, 0, 0);
    check(!app.scanningQr && !app.hasQrArea && app.qrCodes.empty(),
          L"Capture loss cancels a QR rectangle without scanning");
    ReleaseCapture();

    app.userZoom = 0.6f;
    app.pan = {20, 10};
    app.layout = calculateLayout(app.main);
    mouseDown(toScreen({150, 90}));
    releasePointer(toScreen({345, 285}));
    check(app.qrCodes.size() == 1 && app.qrCodes[0].url == linkText,
          L"Zoomed and panned QR scan stays in full image coordinates");
    app.userZoom = 0;
    app.pan = {};
    setTool(Tool::Arrow);
    check(!app.scanningQr && app.qrCodes.size() == 1,
          L"Switching tools keeps decoded codes for the next visit");
    resetQr();
}
