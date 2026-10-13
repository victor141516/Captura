// Included only by builds with CAPTURA_TEST; never part of the release binary.
#include "shortcut.inc"
#include "retained_desktop.inc"
#include "navigation.inc"
#include "navigation_bounds.inc"
std::unique_ptr<Bitmap> fixture() {
    auto bitmap = std::make_unique<Bitmap>(1000, 600, PixelFormat32bppARGB);
    Graphics g(bitmap.get());
    g.Clear(Color(255, 255, 255, 255));
    SolidBrush title(Color(255, 242, 246, 252)), sidebar(Color(255, 247, 249, 252)),
        yellow(Color(255, 255, 204, 61));
    Pen edge(Color(255, 222, 228, 236));
    g.FillRectangle(&title, 0, 0, 1000, 44);
    drawText(g, L"Documents", {20, 0, 700, 44}, 15, Ink, true);
    drawText(g, L"−       □       ×", {867, 0, 119, 44}, 15, Muted, false, true);
    drawText(g, L"←   ↑       This PC  >  Documents", {20, 51, 680, 39}, 15);
    drawText(g, L"Search Documents", {741, 51, 242, 39}, 13, Muted);
    g.DrawLine(&edge, 0, 99, 1000, 99);
    g.FillRectangle(&sidebar, 0, 100, 178, 500);
    const wchar_t* navigation[] = {L"Home",      L"Gallery",  L"Desktop", L"Downloads",
                                   L"Documents", L"Pictures", L"Music",   L"Videos"};
    for (int i = 0; i < 8; i++) {
        if (i == 4) {
            SolidBrush selected(Color(255, 222, 234, 252));
            g.FillRectangle(&selected, 9, 120 + i * 40, 157, 33);
        }
        drawText(g, navigation[i], {30.f, 120.f + i * 40, 143, 33}, 14);
    }
    drawText(g, L"Name", {218, 110, 360, 35}, 13, Muted);
    drawText(g, L"Date modified", {579, 110, 190, 35}, 13, Muted);
    drawText(g, L"Type", {798, 110, 175, 35}, 13, Muted);
    const wchar_t* names[] = {
        L"Projects",          L"Reports",           L"Resources", L"Personal",    L"Budget.xlsx",
        L"Project plan.docx", L"Meeting notes.pdf", L"Ideas.txt", L"Diagram.png", L"Tasks.csv"};
    for (int i = 0; i < 10; i++) {
        REAL y = 150.f + i * 38;
        if (i < 4) {
            g.FillRectangle(&yellow, 208.f, y + 9, 21.f, 15.f);
            g.FillRectangle(&yellow, 208.f, y + 6, 10.f, 5.f);
        } else {
            SolidBrush file(Color(255, 217, 227, 242));
            g.FillRectangle(&file, 211.f, y + 6, 16.f, 22.f);
        }
        drawText(g, names[i], {240, y, 331, 34}, 14);
        drawText(g, L"2026-10-13 10:24", {580, y, 193, 34}, 13, Muted);
        drawText(g, i < 4 ? L"File folder" : L"Document", {798, y, 180, 34}, 13, Muted);
    }
    return bitmap;
}
#include "ocr.inc"
void addFixtureItems() {
    app.canvas = {};
    app.pinnedCanvas = false;
    app.items.clear();
    Item i;
    i.tool = Tool::Arrow;
    i.a = {170, 349};
    i.b = {231, 394};
    i.style.size = 5;
    app.items.push_back(i);
    i = {};
    i.tool = Tool::Rectangle;
    i.a = {199, 302};
    i.b = {966, 414};
    i.style.color = 0xff087cf0;
    i.style.size = 3;
    app.items.push_back(i);
    i = {};
    i.tool = Tool::Text;
    i.a = {355, 159};
    i.text = L"Review this folder";
    i.style.color = 0xff202833;
    i.style.size = 22;
    i.style.filled = true;
    app.items.push_back(i);
    i = {};
    i.tool = Tool::Text;
    i.a = {776, 431};
    i.text = L"No background";
    i.style.color = 0xff202833;
    i.style.size = 18;
    app.items.push_back(i);
    i = {};
    i.tool = Tool::Counter;
    i.a = {392, 448};
    i.b = i.a;
    i.style.size = 26;
    app.items.push_back(i);
    i = {};
    i.tool = Tool::Counter;
    i.a = {888, 483};
    i.b = {952, 518};
    i.number = 2;
    i.pointer = true;
    i.style.size = 26;
    i.style.color = 0xff087cf0;
    app.items.push_back(i);
    i = {};
    i.tool = Tool::Line;
    i.a = {241, 536};
    i.b = {866, 536};
    i.style.color = 0xff16a765;
    i.style.size = 3;
    app.items.push_back(i);
    i = {};
    i.tool = Tool::Brush;
    i.stroke = {{650, 565}, {681, 553}, {714, 574}, {746, 549}, {780, 563}};
    i.a = i.stroke.front();
    i.b = i.stroke.back();
    i.strokeBounds = pointBounds(i.stroke);
    i.style.color = 0xff087cf0;
    i.style.size = 12;
    app.items.push_back(i);
    app.selected = 0;
    app.tool = Tool::Arrow;
}
int selfTest(const std::wstring& directory) {
    CreateDirectoryW(directory.c_str(), nullptr);
    std::wstring log;
    int failures = 0, checks = 0;
    auto check = [&](bool condition, const wchar_t* name) {
        checks++;
        log += (condition ? L"PASS " : L"FAIL ") + std::wstring(name) + L"\r\n";
        if (!condition)
            failures++;
    };
    auto approx = [](REAL a, REAL b) { return std::abs(a - b) < 0.1f; };
    createMain();
    check(app.main != nullptr, L"Native hidden editor creation");
    if (!app.main)
        return 2;
    testCaptureShortcuts(directory, check);
    check(app.defaults[1].size == 12 && app.defaults[3].size == 12 && app.defaults[4].size == 12 &&
              app.defaults[6].size == 12,
          L"All stroke tools, including brush, start at 12 pixels");
    check(app.defaults[2].filled && app.defaults[2].fill == app.sharedColor &&
              app.defaults[2].color == contrastingText(app.sharedColor),
          L"Text starts with shared background and automatic contrast");
    check(contrastingText(0xff000000) == 0xffffffff && contrastingText(0xffffffff) == 0xff000000 &&
              contrastingText(0xffffd23f) == 0xff000000 &&
              contrastingText(0xff8752d6) == 0xffffffff,
          L"Automatic text chooses higher-contrast black or white");
    // Deliberately no tray icon or RegisterHotKey in the test path.
    app.image = fixture();
    app.layout = calculateLayout(app.main);
    check(app.image && app.image->GetLastStatus() == Ok, L"Fixture allocation");
    RECT crop = captureRect({400, 300}, {-20, 15}, {0, 0, 1000, 600});
    check(crop.left == 0 && crop.top == 15 && crop.right == 400 && crop.bottom == 300,
          L"Reverse area drag and clipping");
    app.desktop = {-1920, -200, 1000, 1080};
    RECT screenWindow{-1850, -50, -1500, 400};
    OffsetRect(&screenWindow, -app.desktop.left, -app.desktop.top);
    check(screenWindow.left == 70 && screenWindow.top == 150,
          L"Negative virtual desktop coordinates");
    app.windows = {{{50, 50, 300, 300}}, {{0, 0, 500, 500}}};
    hoverWindow({100, 100});
    check(app.hover == 0, L"Window selection respects foreground order");
    hoverWindow({400, 400});
    check(app.hover == 1, L"Window selection falls through outside foreground");
    hoverWindow({600, 600});
    check(app.hover == -1, L"No-window selection is safe");
    app.desktop = {0, 0, 1000, 600};
    app.windows.clear();
    auto screen = [&](PointF p) { return toScreen(p); };
    // Drive the production pointer handlers against the real native window.
    setTool(Tool::Arrow);
    mouseDown(screen({100, 100}));
    mouseMove(screen({250, 220}));
    mouseUp();
    check(app.items.size() == 1 && approx(app.items[0].b.X, 250),
          L"Create arrow with production drag handlers");
    mouseDown(screen(app.items[0].b));
    mouseMove(screen({290, 240}));
    mouseUp();
    check(approx(app.items[0].a.X, 100) && approx(app.items[0].b.X, 290),
          L"Arrow tip handle changes only tip");
    mouseDown(screen(app.items[0].a));
    mouseMove(screen({120, 80}));
    mouseUp();
    check(approx(app.items[0].a.Y, 80) && approx(app.items[0].b.X, 290),
          L"Arrow tail handle changes only tail");
    undo(false);
    check(approx(app.items[0].a.Y, 100), L"Undo endpoint edit");
    undo(true);
    check(approx(app.items[0].a.Y, 80), L"Redo endpoint edit");
    perform(304);
    check(app.items[0].style.color == Swatches[4] && app.defaults[1].color == Swatches[4],
          L"Selected color also becomes next arrow default");
    check(app.defaults[3].color == Swatches[4] && app.defaults[4].color == Swatches[4] &&
              app.defaults[5].color == Swatches[4] && app.defaults[2].fill == Swatches[4],
          L"Arrow color is shared by rectangle, line, counter and text background");
    perform(201);
    check(app.items[0].style.arrow == 1, L"Double-headed arrow style");
    PointF originalTail = app.items[0].a, originalTip = app.items[0].b,
           middle = curveMiddle(app.items[0]), bendTarget = add(middle, {0, -70});
    mouseDown(screen(middle));
    mouseMove(screen(bendTarget));
    mouseUp();
    check(app.items[0].bent && distance(curvePoint(app.items[0], 0.5f), bendTarget) < 0.01f,
          L"Center handle bends arrow through its exact position");
    check(distance(app.items[0].a, originalTail) < 0.01f &&
              distance(app.items[0].b, originalTip) < 0.01f,
          L"Bending preserves both endpoints");
    check(hitItem(app.items[0], bendTarget, 5), L"Curved arrow selection follows the edited curve");
    {
        RECT rc;
        GetClientRect(app.main, &rc);
        Bitmap ui(rc.right, rc.bottom, PixelFormat32bppARGB);
        Graphics g(&ui);
        renderEditor(g, app.main);
    }
    auto slider = std::find_if(app.buttons.begin(), app.buttons.end(),
                               [](const Button& b) { return b.id == 222; });
    if (slider != app.buttons.end()) {
        RectF r = slider->rect;
        mouseDown({r.X + r.Width * 0.2f, r.Y + 10 * app.layout.scale});
        REAL previousSize = activeStyle().size;
        mouseMove({r.X + r.Width * 0.8f, r.Y + 10 * app.layout.scale});
        mouseUp();
        check(activeStyle().size > previousSize && !app.sizing,
              L"Size slider supports continuous drag");
    } else
        check(false, L"Size slider supports continuous drag");
    setTool(Tool::Rectangle);
    mouseDown(screen({310, 100}));
    mouseMove(screen({500, 220}));
    mouseUp();
    check(app.items.size() == 2 && !app.items[1].style.filled, L"Rectangle starts without fill");
    check(!hitItem(app.items[1], {400, 160}, 3) && hitItem(app.items[1], {312, 160}, 5),
          L"Hollow rectangle hit test selects border");
    auto corners = handles(app.items[1]);
    mouseDown(screen(corners[0]));
    mouseMove(screen({340, 120}));
    mouseUp();
    check(approx(normalized(app.items[1].a, app.items[1].b).X, 340), L"Rectangle corner resizing");
    perform(211);
    perform(302);
    check(app.items[1].style.filled && app.items[1].style.fill == Swatches[2],
          L"Independent fill color enables fill");
    check(hitItem(app.items[1], {400, 160}, 3), L"Filled rectangle interior selection");
    perform(212);
    check(!app.items[1].style.filled, L"Toggle back to no fill");
    setTool(Tool::Line);
    mouseDown(screen({100, 280}));
    mouseMove(screen({350, 280}));
    mouseUp();
    check(app.items.back().tool == Tool::Line && hitItem(app.items.back(), {200, 283}, 5),
          L"Line creation and selection");
    PointF lineTail = app.items.back().a, lineTip = app.items.back().b,
           lineMid = curveMiddle(app.items.back()), lineBend = add(lineMid, {0, -65});
    mouseDown(screen(lineMid));
    mouseMove(screen(lineBend));
    mouseUp();
    check(app.items.back().bent && distance(curvePoint(app.items.back(), 0.5f), lineBend) < 0.01f &&
              distance(app.items.back().a, lineTail) < 0.01f &&
              distance(app.items.back().b, lineTip) < 0.01f,
          L"Line midpoint bends through handle while preserving endpoints");
    check(handles(app.items.back()).size() == 3 && hitItem(app.items.back(), lineBend, 5),
          L"Bent line has center handle and follows curved hit testing");
    keyDown('P');
    check(app.tool == Tool::Brush, L"P shortcut selects freehand brush");
    check(activeStyle().color == app.sharedColor, L"Brush inherits shared drawing color");
    PointF inkStart{130, 420};
    mouseDown(screen(inkStart));
    mouseMove(screen({150, 400}));
    mouseMove(screen({185, 425}));
    mouseMove(screen({150, 450}));
    mouseMove(screen(inkStart));
    mouseUp();
    int brushIndex = app.selected;
    check(validIndex(brushIndex) && app.items[brushIndex].stroke.size() == 5 &&
              distance(app.items[brushIndex].a, app.items[brushIndex].b) < 0.01f,
          L"Closed freehand stroke persists when release returns to start");
    check(hitItem(app.items[brushIndex], {140, 410}, 3) &&
              !hitItem(app.items[brushIndex], {157, 425}, 3),
          L"Brush selection follows ink instead of filled bounding box");
    auto inkBefore = app.items[brushIndex].stroke;
    setTool(Tool::Select);
    mouseDown(screen({140, 410}));
    mouseMove(screen({180, 435}));
    mouseUp();
    bool translated = app.selected == brushIndex;
    for (size_t i = 0; i < inkBefore.size(); i++)
        if (distance(app.items[brushIndex].stroke[i], add(inkBefore[i], {40, 25})) > 0.01f)
            translated = false;
    check(translated, L"Selecting and dragging moves every freehand point together");
    undo(false);
    check(distance(app.items[brushIndex].stroke.front(), inkBefore.front()) < 0.01f,
          L"Undo restores original brush geometry");
    undo(true);
    RectF oldInkBounds = app.items[brushIndex].strokeBounds;
    PointF inkCorner = handles(app.items[brushIndex])[2];
    mouseDown(screen(inkCorner));
    mouseMove(screen(add(inkCorner, {oldInkBounds.Width, oldInkBounds.Height})));
    mouseUp();
    check(std::abs(app.items[brushIndex].strokeBounds.Width - oldInkBounds.Width * 2) < 0.01f &&
              std::abs(app.items[brushIndex].strokeBounds.Height - oldInkBounds.Height * 2) < 0.01f,
          L"Brush corner handle resizes complete stroke");
    setTool(Tool::Brush);
    mouseDown(screen({100, 540}));
    mouseUp();
    int dotIndex = app.selected;
    check(validIndex(dotIndex) && app.items[dotIndex].stroke.size() == 1,
          L"Brush click leaves a round dot");
    auto inkExport = flattened();
    Color dotPixel;
    inkExport->GetPixel(100, 540, &dotPixel);
    check(dotPixel.GetValue() == app.items[dotIndex].style.color,
          L"Exported image contains freehand brush ink");
    setTool(Tool::Select);
    mouseDown(screen({100, 540}));
    mouseMove(screen({125, 530}));
    mouseUp();
    check(app.selected == dotIndex && distance(app.items[dotIndex].a, {125, 530}) < 0.01f &&
              handles(app.items[dotIndex]).size() == 1,
          L"Single brush dot can be selected and moved");
    size_t beforeText = app.items.size();
    setTool(Tool::Text);
    mouseDown(screen({400, 300}));
    check(app.edit != nullptr, L"Native editable text control");
    check(app.items.back().style.filled && app.items.back().style.fill == app.sharedColor &&
              app.items.back().style.color == contrastingText(app.sharedColor),
          L"New text uses most recent common color and automatic contrast");
    if (app.edit) {
        SetWindowTextW(app.edit, L"Primera línea\r\nSegunda línea");
        commitText();
    }
    check(app.items.size() == beforeText + 1 &&
              app.items.back().text.find(L"Segunda") != std::wstring::npos,
          L"Multiline text commit");
    undo(false);
    check(app.items.size() == beforeText, L"One undo removes newly committed text");
    undo(true);
    int textIndex = static_cast<int>(app.items.size()) - 1;
    startTextEdit(textIndex);
    if (app.edit) {
        SetWindowTextW(app.edit, L"Cancelado");
        commitText(true);
    }
    check(app.items[textIndex].text.find(L"Primera") != std::wstring::npos,
          L"Cancel preserves existing text");
    app.selected = textIndex;
    perform(221);
    check(app.items[textIndex].style.size == 25, L"Text size controls font size");
    app.selected = textIndex;
    app.fillTarget = true;
    perform(305);
    check(app.items[textIndex].style.fill == Swatches[5] &&
              app.items[textIndex].style.color == 0xffffffff &&
              app.defaults[1].color == Swatches[5] && app.defaults[3].color == Swatches[5],
          L"Text background changes shared drawing color and contrast");
    setTool(Tool::Text);
    mouseDown(screen({450, 400}));
    if (app.edit) {
        SendMessageW(app.edit, WM_CHAR, L'A', 0);
        check(app.items.back().text == L"A", L"Typing updates annotation before confirming text");
        RECT nativeRect;
        GetWindowRect(app.edit, &nativeRect);
        MapWindowPoints(nullptr, app.main, reinterpret_cast<POINT*>(&nativeRect), 2);
        check(nativeRect.right < 0 && nativeRect.bottom < 0 &&
                  !(GetWindowLongPtrW(app.edit, GWL_EXSTYLE) & WS_EX_CLIENTEDGE),
              L"Inline text never displays a native edit box");
        SetWindowTextW(app.edit, L"Árbol\r\nniño");
        check(app.items.back().text == L"Árbol\r\nniño",
              L"Inline editing synchronizes Unicode multiline input");
    }
    commitText(true);
    check(app.items.size() == beforeText + 1, L"Cancel new live text removes it");
    setTool(Tool::Counter);
    mouseDown(screen({680, 200}));
    mouseUp();
    int counter = app.selected;
    check(app.items[counter].number == 1 && !app.items[counter].pointer,
          L"First counter number and initial no-pointer state");
    auto counterHandles = handles(app.items[counter]);
    mouseDown(screen(counterHandles[1]));
    mouseMove(screen({770, 250}));
    mouseUp();
    check(app.items[counter].pointer && distance(app.items[counter].a, app.items[counter].b) <=
                                            3 * counterRadius(app.items[counter]) + 0.01f,
          L"Counter edge handle creates a bounded cone");
    {
        GraphicsPath body;
        counterPath(body, app.items[counter]);
        PointF direction = sub(counterTip(app.items[counter]), app.items[counter].a);
        REAL length = distance(counterTip(app.items[counter]), app.items[counter].a),
             r = counterRadius(app.items[counter]);
        PointF probe =
            add(app.items[counter].a, {direction.X / length * r - direction.Y / length * r * 0.5f,
                                       direction.Y / length * r + direction.X / length * r * 0.5f});
        check(body.IsVisible(probe), L"Counter pointed extension preserves original circle width");
        check(length + r <= 4 * r + 0.01f,
              L"Counter marker total length limited to two circle diameters");
    }
    mouseDown(screen(app.items[counter].b));
    mouseMove(screen(app.items[counter].a));
    mouseUp();
    check(!app.items[counter].pointer, L"Dragging pointer to center removes it");
    mouseDown(screen({680, 330}));
    mouseUp();
    check(app.items.back().number == 2, L"Counter numbering advances automatically");
    setTool(Tool::Select);
    app.selected = counter;
    PointF old = app.items[counter].a;
    mouseDown(screen(old));
    mouseMove(screen(add(old, {25, 20})));
    mouseUp();
    check(approx(app.items[counter].a.X, old.X + 25), L"Counter center moves the annotation");
    perform(240);
    check(app.items.size() == beforeText + 2, L"Delete selected annotation");
    undo(false);
    check(app.items.size() == beforeText + 3, L"Undo deletion restores annotation");
    app.frozen = fixture();
    auto clipped =
        std::unique_ptr<Bitmap>(app.frozen->Clone(210, 150, 170, 90, PixelFormat32bppARGB));
    check(clipped && clipped->GetWidth() == 170 && clipped->GetHeight() == 90,
          L"Frozen-image crop returns exact pixel dimensions");
    Color original, copy;
    app.frozen->GetPixel(220, 160, &original);
    clipped->GetPixel(10, 10, &copy);
    check(original.GetValue() == copy.GetValue(), L"Crop pixel origin matches frozen desktop");
    app.frozen.reset();
    // Route actual Windows messages through the production overlay window.
    auto makeOverlay = [&]() {
        app.frozen = fixture();
        app.desktop = {0, 0, 1000, 600};
        app.windowMode = false;
        app.selecting = false;
        app.selection = {};
        app.captureRestore = false;
        app.overlay = CreateWindowExW(WS_EX_TOOLWINDOW, OverlayClass, L"test capture", WS_POPUP, 0,
                                      0, 1000, 600, nullptr, nullptr, app.instance, nullptr);
        return app.overlay;
    };
    HWND areaOverlay = makeOverlay();
    SendMessageW(areaOverlay, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(300, 250));
    SendMessageW(areaOverlay, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(100, 80));
    SendMessageW(areaOverlay, WM_LBUTTONUP, 0, MAKELPARAM(100, 80));
    RECT areaFrame = canvasBounds();
    check(!app.overlay && !app.frozen && app.image && app.image->GetWidth() == 1000 &&
              app.image->GetHeight() == 600 && areaFrame.left == 100 && areaFrame.top == 80 &&
              areaFrame.right == 300 && areaFrame.bottom == 250,
          L"Area overlay keeps frozen desktop and establishes exact initial frame");
    HWND windowOverlay = makeOverlay();
    app.windows = {{{70, 80, 450, 390}}, {{0, 0, 900, 500}}};
    SendMessageW(windowOverlay, WM_KEYDOWN, VK_SPACE, 0);
    check(app.windowMode, L"Space switches from area to window");
    SendMessageW(windowOverlay, WM_MOUSEMOVE, 0, MAKELPARAM(100, 100));
    check(app.hover == 0, L"Overlay hover selects foremost window");
    SendMessageW(windowOverlay, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(100, 100));
    RECT windowFrame = canvasBounds();
    check(!app.overlay && app.image && app.image->GetWidth() == 1000 &&
              app.image->GetHeight() == 600 && windowFrame.left == 70 && windowFrame.top == 80 &&
              windowFrame.right == 450 && windowFrame.bottom == 390,
          L"Window click keeps frozen desktop and establishes window frame");
    HWND cancelOverlay = makeOverlay();
    SendMessageW(cancelOverlay, WM_KEYDOWN, VK_ESCAPE, 0);
    RECT canceledFrame = canvasBounds();
    check(!app.overlay && !app.frozen && app.image->GetWidth() == 1000 &&
              EqualRect(&canceledFrame, &windowFrame),
          L"Escape cancels selector and preserves previous capture");
    check(!IsWindowVisible(app.main), L"Integration tests leave editor hidden");
    app.image = fixture();
    // Real down/move/up messages exercise the initial held-button gesture.
    app.canvas = {};
    app.items.clear();
    app.undo.clear();
    app.redo.clear();
    setTool(Tool::Counter);
    app.layout = calculateLayout(app.main);
    auto mouseMessage = [&](UINT message, PointF position) {
        PointF pixel = toScreen(position);
        SendMessageW(app.main, message, message == WM_LBUTTONUP ? 0 : MK_LBUTTON,
                     MAKELPARAM(static_cast<int>(std::round(pixel.X)),
                                static_cast<int>(std::round(pixel.Y))));
    };
    PointF firstClick{200, 180};
    mouseMessage(WM_LBUTTONDOWN, firstClick);
    check(app.dragging && !app.creating && app.dragHandle == 1 && app.items.size() == 1,
          L"New counter starts holding its tip handle");
    mouseMessage(WM_LBUTTONUP, firstClick);
    check(app.items.size() == 1 && !app.items[0].pointer && !app.dragging,
          L"Click without drag keeps a circular counter");
    PointF secondClick{430, 280}, secondEnd{530, 340};
    mouseMessage(WM_LBUTTONDOWN, secondClick);
    PointF fixedCenter = app.items.back().a;
    mouseMessage(WM_MOUSEMOVE, secondEnd);
    check(app.items.back().pointer && app.dragging &&
              distance(app.items.back().a, fixedCenter) < 0.01f,
          L"Initial held drag creates tip immediately without moving circle");
    mouseMessage(WM_LBUTTONUP, secondEnd);
    check(app.items.size() == 2 && app.items.back().number == 2 && app.items.back().pointer &&
              distance(app.items.back().a, fixedCenter) < 0.01f &&
              distance(app.items.back().a, app.items.back().b) <=
                  3 * counterRadius(app.items.back()) + 0.01f,
          L"Release keeps new counter with bounded tip and sequential number");
    undo(false);
    check(app.items.size() == 1 && !app.items[0].pointer,
          L"One undo removes counter and tip created in same gesture");
    undo(true);
    check(app.items.size() == 2 && app.items.back().pointer &&
              distance(app.items.back().a, fixedCenter) < 0.01f,
          L"One redo restores complete counter gesture");
    PointF thirdClick{600, 400};
    mouseMessage(WM_LBUTTONDOWN, thirdClick);
    mouseMessage(WM_MOUSEMOVE, add(thirdClick, {70, -45}));
    mouseMessage(WM_MOUSEMOVE, thirdClick);
    mouseMessage(WM_LBUTTONUP, thirdClick);
    check(app.items.size() == 3 && !app.items.back().pointer && app.items.back().number == 3,
          L"Dragging back to original click leaves circular counter");
    PointF fourthClick{800, 450};
    mouseMessage(WM_LBUTTONDOWN, fourthClick);
    mouseMessage(WM_LBUTTONUP, add(fourthClick, {60, -40}));
    check(app.items.size() == 4 && app.items.back().pointer,
          L"Counter creation uses final release location without move message");
    PointF cancelledClick{800, 200};
    mouseMessage(WM_LBUTTONDOWN, cancelledClick);
    mouseMessage(WM_MOUSEMOVE, add(cancelledClick, {0, 70}));
    SendMessageW(app.main, WM_KEYDOWN, VK_ESCAPE, 0);
    check(app.items.size() == 4 && !app.dragging, L"Escape cancels entire newly dragged counter");
    PointF jitterClick{800, 150};
    mouseMessage(WM_LBUTTONDOWN, jitterClick);
    mouseMessage(WM_MOUSEMOVE, add(jitterClick, {2, 1}));
    mouseMessage(WM_LBUTTONUP, add(jitterClick, {2, 1}));
    check(app.items.size() == 5 && !app.items.back().pointer && app.items.back().number == 5,
          L"Small click jitter creates circle without unintended tip");
    // Check actual raster pixels at a 12-pixel arrowhead, not just geometry.
    {
        Bitmap tipImage(320, 180, PixelFormat32bppARGB);
        Item arrow;
        arrow.a = {40, 90};
        arrow.b = {260, 90};
        arrow.style.size = 12;
        arrow.style.color = 0xff087cf0;
        {
            Graphics g(&tipImage);
            g.Clear(Color(255, 255, 255, 255));
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            drawItem(g, arrow);
        }
        bool noOvershoot = true;
        int nearTipWidth = 0;
        for (int y = 0; y < 180; y++) {
            Color p;
            tipImage.GetPixel(254, y, &p);
            if (p.GetValue() != 0xffffffff)
                nearTipWidth++;
            for (int x = 262; x < 280; x++) {
                tipImage.GetPixel(x, y, &p);
                if (p.GetValue() != 0xffffffff)
                    noOvershoot = false;
            }
        }
        check(noOvershoot && nearTipWidth < 10,
              L"12-pixel arrow shaft ends inside triangular head");
        encodePng(tipImage, (directory + L"\\arrow-12px.png").c_str());
    }
    // Optical centering is checked against the visible ink, including pixels.
    {
        Bitmap aImage(180, 180, PixelFormat32bppARGB);
        Item text;
        text.tool = Tool::Text;
        text.a = {30, 30};
        text.text = L"A";
        text.style.size = 64;
        text.style.filled = true;
        text.style.fill = 0xffffd23f;
        text.style.color = 0xff000000;
        {
            Graphics g(&aImage);
            g.Clear(Color(255, 255, 255, 255));
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            drawItem(g, text);
        }
        int left = 180, top = 180, right = 0, bottom = 0;
        for (int y = 0; y < 180; y++)
            for (int x = 0; x < 180; x++) {
                Color p;
                aImage.GetPixel(x, y, &p);
                if (p.GetR() < 70 && p.GetG() < 70 && p.GetB() < 70) {
                    left = std::min(left, x);
                    right = std::max(right, x);
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
            }
        RectF r = textRect(text);
        check(right > left && std::abs((left + right + 1) / 2.f - (r.X + r.Width / 2)) < 1.6f &&
                  std::abs((top + bottom + 1) / 2.f - (r.Y + r.Height / 2)) < 1.6f,
              L"Capital A visible ink is centered in its background");
        Color corner, background;
        aImage.GetPixel(31, 31, &corner);
        aImage.GetPixel(static_cast<int>(r.X + r.Width / 2), 31, &background);
        check(corner.GetValue() == 0xffffffff && background.GetValue() == text.style.fill,
              L"Text background has rounded corners");
        encodePng(aImage, (directory + L"\\text-A-centered.png").c_str());
    }
    // Tangent joins must retain the whole original disk at every orientation
    // and every supported reach, including clamped very short/long drags.
    {
        bool contained = true, rasterInterior = true, tangent = true, onCircle = true;
        Item marker;
        marker.tool = Tool::Counter;
        marker.a = {120, 120};
        marker.style.size = 60;
        marker.pointer = true;
        REAL r = counterRadius(marker);
        const REAL reaches[] = {0, 1.12f, 1.6f, 2, 3, 20};
        for (int direction = 0; direction < 24; direction++)
            for (REAL reach : reaches) {
                REAL angle = direction * Pi / 12;
                marker.b =
                    add(marker.a, {r * reach * std::cos(angle), r * reach * std::sin(angle)});
                CounterTangents contacts = counterTangents(marker);
                PointF tip = counterTip(marker);
                for (PointF contact : {contacts.lower, contacts.upper}) {
                    PointF normal = sub(contact, marker.a), side = sub(tip, contact);
                    REAL product = normal.X * side.X + normal.Y * side.Y;
                    if (std::abs(product) / std::max(1.f, r * distance(tip, contact)) > 0.00001f)
                        tangent = false;
                    if (std::abs(distance(contact, marker.a) - r) > 0.001f)
                        onCircle = false;
                }
                GraphicsPath outline;
                counterPath(outline, marker);
                for (int sample = 0; sample < 120; sample++) {
                    REAL a = sample * Pi / 60;
                    PointF diskBoundary = {r * std::cos(a), r * std::sin(a)};
                    for (PointF contact : {contacts.lower, contacts.upper}) {
                        PointF normal = sub(contact, marker.a);
                        if (normal.X * diskBoundary.X + normal.Y * diskBoundary.Y > r * r + 0.01f)
                            contained = false;
                    }
                    // IsVisible uses pixel-rounded regions: test its interior at a
                    // 1.5-pixel inset, and test the exact boundary analytically.
                    PointF inside =
                        add(marker.a, {(r - 1.5f) * std::cos(a), (r - 1.5f) * std::sin(a)});
                    if (!outline.IsVisible(inside))
                        rasterInterior = false;
                }
            }
        check(tangent, L"Both tip sides are tangent to the circle in 24 directions and 6 reaches");
        check(onCircle, L"Tangent contacts stay exactly on original circumference");
        check(contained,
              L"Counter with tip contains complete original disk in all tested directions");
        check(rasterInterior,
              L"Native path retains circle interior at all orientations and reaches");
        marker.b = add(marker.a, {r, 0});
        CounterTangents contacts = counterTangents(marker);
        check(std::abs((contacts.lower.X - marker.a.X) / r - 0.625f) < 0.00001f &&
                  std::abs((contacts.lower.Y - marker.a.Y) / r - 0.78062475f) < 0.00001f,
              L"Short wide pointer matches approved 1.6-radius tangent geometry");
    }
    // Raster check: adding a tip must not remove any interior circle pixels.
    {
        Bitmap plain(260, 260, PixelFormat32bppARGB), pointed(260, 260, PixelFormat32bppARGB);
        Item marker;
        marker.tool = Tool::Counter;
        marker.a = {100, 130};
        marker.style.size = 60;
        marker.style.color = 0xff064bcc;
        {
            Graphics g(&plain);
            g.Clear(Color(255, 255, 255, 255));
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            drawItem(g, marker);
        }
        marker.pointer = true;
        marker.b = add(marker.a, {1.6f * counterRadius(marker), 0});
        {
            Graphics g(&pointed);
            g.Clear(Color(255, 255, 255, 255));
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            drawItem(g, marker);
        }
        bool preserved = true;
        int tested = 0;
        for (int y = 1; y < 259; y++)
            for (int x = 1; x < 259; x++) {
                Color center, left, right, top, bottom;
                plain.GetPixel(x, y, &center);
                if (center.GetValue() != marker.style.color)
                    continue;
                plain.GetPixel(x - 1, y, &left);
                plain.GetPixel(x + 1, y, &right);
                plain.GetPixel(x, y - 1, &top);
                plain.GetPixel(x, y + 1, &bottom);
                if (left.GetValue() != marker.style.color ||
                    right.GetValue() != marker.style.color ||
                    top.GetValue() != marker.style.color || bottom.GetValue() != marker.style.color)
                    continue;
                Color actual;
                pointed.GetPixel(x, y, &actual);
                tested++;
                if (actual.GetValue() != marker.style.color)
                    preserved = false;
            }
        check(preserved && tested > 1000,
              L"Adding tangent tip removes no original circle interior pixels");
        Bitmap preview(1100, 480, PixelFormat32bppARGB);
        {
            Graphics g(&preview);
            g.Clear(Color(255, 255, 255, 255));
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.ScaleTransform(3, 3);
            marker.pointer = false;
            marker.a = {100, 80};
            drawItem(g, marker);
            marker.pointer = true;
            marker.a = {260, 80};
            marker.b = add(marker.a, {1.6f * counterRadius(marker), 0});
            drawItem(g, marker);
        }
        check(encodePng(preview, (directory + L"\\counter-comparison.png").c_str()),
              L"Native renderer exports side-by-side circle and tangent tip preview");
    }
    addFixtureItems();
    auto bitmap = flattened();
    check(bitmap && bitmap->GetWidth() == 1000 && bitmap->GetHeight() == 600,
          L"Flattening preserves native resolution");
    Color annotation;
    bitmap->GetPixel(300, 536, &annotation);
    check(annotation.GetG() > 120 && annotation.GetR() < 70,
          L"Export contains line annotation pixels");
    std::wstring exportPath = directory + L"\\annotated.png";
    check(encodePng(*bitmap, exportPath.c_str()), L"PNG encoder saves annotated image");
    Bitmap decoded(exportPath.c_str());
    check(decoded.GetLastStatus() == Ok && decoded.GetWidth() == 1000 && decoded.GetHeight() == 600,
          L"PNG round trip dimensions");
    HGLOBAL dib = makeDib(*bitmap);
    check(dib != nullptr, L"Clipboard DIB allocation");
    if (dib) {
        auto* h = static_cast<BITMAPV5HEADER*>(GlobalLock(dib));
        check(h && h->bV5Height == -600 && h->bV5Width == 1000 && h->bV5AlphaMask == 0xff000000,
              L"Clipboard top-down format and alpha mask");
        if (h)
            GlobalUnlock(dib);
        GlobalFree(dib);
    }
    // Render actual production UI to an image for visual QA, using a hidden HWND.
    app.status = L"Screenshot ready to annotate";
    app.layout = calculateLayout(app.main);
    RECT client;
    GetClientRect(app.main, &client);
    Bitmap preview(client.right, client.bottom, PixelFormat32bppARGB);
    {
        Graphics g(&preview);
        renderEditor(g, app.main);
    }
    check(encodePng(preview, (directory + L"\\editor.png").c_str()), L"Native editor render");
    // Visual review sheet renders the production glyphs at Windows DPI scales;
    // kept out of the portable release, like the rest of this test fixture.
    {
        Bitmap icons(1160, 280, PixelFormat32bppARGB);
        Graphics g(&icons);
        g.Clear(Color(255, 255, 255, 255));
        const wchar_t* labels[] = {L"Select",  L"Arrow", L"Text",        L"Rectangle", L"Line",
                                   L"Counter", L"Brush", L"Select text", L"Undo",      L"Redo",
                                   L"Delete",  L"Copy",  L"Save"};
        const wchar_t* scales[] = {L"100 %", L"150 %", L"200 %"};
        for (int i = 0; i < 13; i++)
            drawText(g, labels[i], {80.f + i * 80, 12, 80, 28}, 10, Muted, false, true);
        for (int row = 0; row < 3; row++) {
            REAL size = 24.f + 12 * row, y = 58.f + row * 72;
            drawText(g, scales[row], {0, y, 75, size}, 11, Muted, false, true);
            for (int i = 0; i < 13; i++) {
                RectF r(80.f + i * 80 + (80 - size) / 2, y, size, size);
                if (i < ToolCount)
                    toolIcon(g, static_cast<Tool>(i), r, Ink);
                else if (i < 10)
                    historyIcon(g, r, i == 9, Ink);
                else
                    actionIcon(g, r, i == 10 ? 240 : (i == 11 ? 4 : 5), Ink);
            }
        }
        encodePng(icons, (directory + L"\\icons.png").c_str());
    }
    auto controlRect = [&](int id) {
        for (const Button& b : app.buttons)
            if (b.id == id)
                return b.rect;
        return RectF();
    };
    auto controlCenter = [&](int id) {
        RectF r = controlRect(id);
        return PointF(r.X + r.Width / 2, r.Y + r.Height / 2);
    };
    bool oneRow = true;
    for (int id : {100, 101, 102, 103, 104, 105, 106, 107, 2, 3, 240, 4, 5})
        if (std::abs(controlRect(id).Y - controlRect(100).Y) > 0.01f)
            oneRow = false;
    check(oneRow && controlRect(2).X - controlRect(107).GetRight() > 10 * app.layout.scale,
          L"Eight icon tools and history actions share a single spaced toolbar row");
    bool removed = true;
    for (int id : {1, 212, 220, 221, 250, 251, 252, 253})
        if (controlRect(id).Width > 0)
            removed = false;
    check(removed && controlRect(254).Width > 0,
          L"Removed capture, fill toggle, size buttons and footer zoom controls; help remains");
    bool balanced = true;
    for (int i = 0; i < 6; i++) {
        int topId = 300 + i, bottomId = i < 5 ? 306 + i : 213;
        RectF top = controlRect(topId), bottom = controlRect(bottomId);
        if (top.Width <= 0 || bottom.Width <= 0 || std::abs(top.X - bottom.X) > 0.01f ||
            std::abs(top.Width - bottom.Width) > 0.01f)
            balanced = false;
    }
    check(balanced,
          L"Palette has two equal rows of six tiles including extra cyan and full-color gradient");
    bool tooltipNames = app.tooltip != nullptr;
    for (int id = 100; id < 100 + ToolCount; id++) {
        wchar_t text[128]{};
        TOOLINFOW tool{};
        tool.cbSize = sizeof(tool);
        tool.hwnd = app.main;
        tool.uId = id;
        tool.lpszText = text;
        SendMessageW(app.tooltip, TTM_GETTEXTW, 128, reinterpret_cast<LPARAM>(&tool));
        if (wcscmp(text, tooltipFor(id)) != 0) {
            tooltipNames = false;
            log += L"Tooltip " + std::to_wstring(id) + L" registered=" +
                   std::to_wstring(app.tooltipAdded[id]) + L" text=" + text + L"\r\n";
        }
    }
    check(tooltipNames,
          L"Native popovers contain exact tool names and parenthesized keyboard shortcuts");
    auto buttonPixel = [&](int id) {
        Bitmap frame(client.right, client.bottom, PixelFormat32bppARGB);
        {
            Graphics g(&frame);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            renderEditor(g, app.main);
        }
        RectF r = controlRect(id);
        Color p;
        frame.GetPixel(static_cast<int>(r.X + 6 * app.layout.scale),
                       static_cast<int>(r.Y + 6 * app.layout.scale), &p);
        return p.GetValue();
    };
    for (int id : {4, 5}) {
        setHoverButton(-1);
        app.pressedButton = -1;
        ARGB normal = buttonPixel(id);
        PointF center = controlCenter(id);
        mouseMove(center);
        ARGB hover = buttonPixel(id);
        mouseDown(center);
        ARGB down = buttonPixel(id);
        bool pressed = app.pressedButton == id;
        releasePointer({-10, -10});
        check(normal != hover && hover != down && pressed && app.pressedButton == -1,
              id == 4 ? L"Copy button has distinct hover and pressed pixels without executing on "
                        L"canceled release"
                      : L"Save button has distinct hover and pressed pixels without opening a "
                        L"canceled dialog");
    }
    RectF toolButton = controlRect(106);
    PointF brushButton = controlCenter(106);
    Tool previousTool = app.tool;
    mouseDown(brushButton);
    check(app.tool == previousTool && app.pressedButton == 106,
          L"Tool action waits until button release while showing pressed state");
    releasePointer(brushButton);
    check(app.tool == Tool::Brush && app.pressedButton == -1,
          L"Releasing over brush icon activates brush");
    mouseMove(brushButton);
    SendMessageW(app.main, WM_MOUSELEAVE, 0, 0);
    check(app.hoverButton == -1, L"Hover state clears when pointer leaves editor");
    {
        Bitmap frame(client.right, client.bottom, PixelFormat32bppARGB);
        Graphics g(&frame);
        renderEditor(g, app.main);
    }
    setTool(Tool::Rectangle);
    {
        Bitmap frame(client.right, client.bottom, PixelFormat32bppARGB);
        Graphics g(&frame);
        renderEditor(g, app.main);
    }
    PointF fillChip = controlCenter(211);
    bool oldFill = activeStyle().filled;
    mouseRightDown(fillChip);
    check(activeStyle().filled != oldFill, L"Right-click on background chip toggles fill");
    mouseRightDown(fillChip);
    check(activeStyle().filled == oldFill, L"Second background right-click restores no-fill state");
    perform(210);
    perform(310);
    check(app.sharedColor == Swatches[10] && app.defaults[6].color == Swatches[10],
          L"Extra cyan preset participates in shared color including brush");
    setTool(Tool::Select);
    app.selected = 0;
    app.hoverButton = -1;
    app.pressedButton = -1;
    editorBuffer.reset();
    HDC reference = GetDC(app.main);
    HDC memory = CreateCompatibleDC(reference);
    HBITMAP surface = CreateCompatibleBitmap(reference, client.right, client.bottom);
    HGDIOBJ oldSurface = SelectObject(memory, surface);
    SendMessageW(app.main, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT);
    // Warm GDI+'s per-HDC font cache before measuring our own buffer lifetime.
    editorBuffer.reset();
    DWORD gdiBefore = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    SendMessageW(app.main, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT);
    DWORD cached = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < 8; i++)
        SendMessageW(app.main, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT);
    check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == cached,
          L"Repainting reuses buffers without GDI handle growth");
    editorBuffer.reset();
    DWORD afterCleanup = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    log += L"GDI baseline " + std::to_wstring(gdiBefore) + L", cached " + std::to_wstring(cached) +
           L", after cleanup " + std::to_wstring(afterCleanup) + L"\r\n";
    check(afterCleanup == gdiBefore, L"Paint buffer cleanup releases all GDI handles");
    SelectObject(memory, oldSurface);
    DeleteObject(surface);
    DeleteDC(memory);
    ReleaseDC(app.main, reference);
    // Compare incremental frames pixel-for-pixel with a complete reference
    // render. Opaque overlaps deliberately catch moving-object z-order bugs.
    auto pixels = [](PaintBuffer& b) {
        std::vector<DWORD> out(static_cast<size_t>(b.width) * b.height);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = b.width;
        info.bmiHeader.biHeight = -b.height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        HGDIOBJ saved = SelectObject(b.dc, b.original);
        GetDIBits(b.dc, b.bitmap, 0, b.height, out.data(), &info, DIB_RGB_COLORS);
        SelectObject(b.dc, saved);
        return out;
    };
    HDC motionDC = GetDC(app.main);
    PaintBuffer actual, expected;
    actual.ensure(motionDC, client.right, client.bottom);
    expected.ensure(motionDC, client.right, client.bottom);
    auto motionCheck = [&](int index, int handle, PointF delta, const wchar_t* name) {
        addFixtureItems();
        app.items[1].style.filled = true;
        app.items[1].style.fill = 0xffd7eaff;
        app.selected = index;
        app.tool = Tool::Select;
        app.dragging = true;
        app.creating = false;
        app.dragHandle = handle;
        app.initialItem = app.items[index];
        app.dragStart = handle < 0 ? app.items[index].a : handles(app.items[index])[handle];
        invalidate();
        paint(app.main, false, actual.dc);
        ValidateRect(app.main, nullptr);
        unsigned scaledBefore = captureView.builds;
        Item oldItem = app.items[index];
        mouseMove(toScreen(add(app.dragStart, delta)));
        RECT dirty{};
        // Hidden HWNDs have no visible update region; use exactly the bounds
        // submitted by invalidateMovement to exercise the partial paint path.
        RECT previous = annotationBounds(oldItem, app.layout),
             next = annotationBounds(app.items[index], app.layout);
        UnionRect(&dirty, &previous, &next);
        if (oldItem.style.size != app.items[index].style.size || app.pinnedCanvas)
            dirty = client;
        int saved = SaveDC(actual.dc);
        IntersectClipRect(actual.dc, dirty.left, dirty.top, dirty.right, dirty.bottom);
        paint(app.main, false, actual.dc);
        RestoreDC(actual.dc, saved);
        ValidateRect(app.main, nullptr);
        {
            Graphics g(expected.dc);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            renderEditor(g, app.main, -1, true, true);
        }
        GdiFlush();
        auto a = pixels(actual), b = pixels(expected);
        bool equal = a.size() == b.size();
        size_t mismatches = 0, firstMismatch = 0;
        for (size_t i = 0; i < a.size() && i < b.size(); i++)
            if ((a[i] & 0xffffff) != (b[i] & 0xffffff)) {
                if (!mismatches)
                    firstMismatch = i;
                ++mismatches;
                equal = false;
            }
        if (!equal) {
            log += L"Pixel differences " + std::to_wstring(mismatches) + L"; first x=" +
                   std::to_wstring(firstMismatch % actual.width) + L" y=" +
                   std::to_wstring(firstMismatch / actual.width) + L"; dirty=" +
                   std::to_wstring(dirty.left) + L"," + std::to_wstring(dirty.top) + L"," +
                   std::to_wstring(dirty.right) + L"," + std::to_wstring(dirty.bottom) + L"\r\n";
        }
        check(equal, name);
        check(captureView.builds == scaledBefore, L"Dragging reuses scaled capture");
        mouseUp();
    };
    motionCheck(0, -1, {90, 55},
                L"Partial arrow move matches full render and preserves overlapping objects");
    motionCheck(0, 2, {75, -65},
                L"Dragging arrow midpoint redraws editable curvature without trails");
    motionCheck(1, -1, {60, 45}, L"Partial rectangle move leaves no trails");
    motionCheck(1, 0, {140, 70}, L"Partial rectangle corner resize matches full render");
    motionCheck(2, -1, {40, 35}, L"Partial text move preserves background and glyph pixels");
    motionCheck(2, 1, {80, 30}, L"Text resizing refreshes sidebar and canvas together");
    motionCheck(4, 0, {60, 20}, L"Partial counter move matches full render");
    motionCheck(5, 1, {30, -40}, L"Rotating and stretching tangent counter tip leaves no trails");
    motionCheck(5, 1, {-64, -35}, L"Removing counter pointer erases old arrow completely");
    motionCheck(6, 1, {-180, -90}, L"Partial line endpoint edit matches full render");
    motionCheck(6, 2, {50, -65}, L"Line center-handle curvature redraws without trails");
    motionCheck(7, -1, {60, -35}, L"Partial brush movement matches full render");
    motionCheck(7, 2, {50, 30}, L"Partial brush resizing matches full render");
    check(!dragBackground.dc, L"Drag-specific buffer released on mouse-up");
    // A release can contain a newer pointer location than the last move.
    addFixtureItems();
    app.tool = Tool::Select;
    app.selected = 1;
    app.dragging = true;
    app.dragHandle = -1;
    app.creating = false;
    app.initialItem = app.items[1];
    app.dragStart = app.items[1].a;
    PointF release = toScreen(add(app.dragStart, {50, 40}));
    SendMessageW(app.main, WM_LBUTTONUP, 0,
                 MAKELPARAM(static_cast<int>(release.X), static_cast<int>(release.Y)));
    check(std::abs(app.items[1].a.X - app.initialItem.a.X - 50) < 2,
          L"Mouse-up uses final pointer location");
    actual.reset();
    expected.reset();
    ReleaseDC(app.main, motionDC);
    addFixtureItems();
    // Frozen-mode preview also uses the production compositor and real window proc.
    app.frozen = fixture();
    app.desktop = {0, 0, 1000, 600};
    app.windowMode = true;
    app.hover = 0;
    app.windows = {{{178, 100, 981, 566}}};
    HWND overlay = CreateWindowExW(WS_EX_TOOLWINDOW, OverlayClass, L"test", WS_POPUP, 0, 0, 1000,
                                   600, nullptr, nullptr, app.instance, nullptr);
    Bitmap selector(1000, 600, PixelFormat32bppARGB);
    {
        Graphics g(&selector);
        renderOverlay(g, overlay);
    }
    check(encodePng(selector, (directory + L"\\selector.png").c_str()),
          L"Frozen window overlay render");
    DestroyWindow(overlay);
    app.frozen.reset();
    // Exercise real incremental selector painting, including nonrectangular
    // damage. The fully rendered reference must match every pixel.
    app.frozen = fixture();
    app.desktop = {-1000, -600, 0, 0};
    app.windowMode = false;
    app.selection = {};
    app.selecting = true;
    app.dragStart = {300, 250};
    app.overlay = CreateWindowExW(WS_EX_TOOLWINDOW, OverlayClass, L"test selector", WS_POPUP, 0, 0,
                                  1000, 600, nullptr, nullptr, app.instance, nullptr);
    HDC selectorDC = GetDC(app.overlay);
    PaintBuffer selectorActual, selectorExpected;
    selectorActual.ensure(selectorDC, 1000, 600);
    selectorExpected.ensure(selectorDC, 1000, 600);
    paintOverlay(app.overlay, selectorActual.dc);
    unsigned selectorBuilds = overlayCache.builds;
    bool areaEqual = true, interiorUntouched = true;
    for (POINT point : std::initializer_list<POINT>{{900, 550},
                                                    {910, 560},
                                                    {650, 420},
                                                    {100, 100},
                                                    {0, 599},
                                                    {999, 0},
                                                    {300, 250},
                                                    {320, 250},
                                                    {320, 270}}) {
        RECT previousSelection = overlaySelection(), oldHint = overlayCache.hint;
        updateOverlay(app.overlay, point);
        HRGN damage =
            overlayDamage(previousSelection, overlaySelection(), oldHint, overlayCache.hint);
        if (point.x == 910)
            interiorUntouched = !PtInRegion(damage, 600, 400);
        int saved = SaveDC(selectorActual.dc);
        SelectClipRgn(selectorActual.dc, damage);
        paintOverlay(app.overlay, selectorActual.dc);
        RestoreDC(selectorActual.dc, saved);
        DeleteObject(damage);
        {
            Graphics g(selectorExpected.dc);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            renderOverlay(g, app.overlay);
        }
        {
            auto a = pixels(selectorActual), b = pixels(selectorExpected);
            size_t diff = 0;
            for (size_t p = 0; p < a.size(); p++)
                if (a[p] != b[p]) {
                    if (diff == 0) {
                        wchar_t diagnostic[180];
                        swprintf_s(
                            diagnostic,
                            L"area (%ld,%ld), first diff (%zu,%zu) actual %08x expected %08x\r\n",
                            point.x, point.y, p % 1000, p / 1000, a[p], b[p]);
                        log += diagnostic;
                    }
                    diff++;
                }
            if (diff) {
                log += L"area mismatches " + std::to_wstring(diff) + L"\r\n";
                areaEqual = false;
            }
        }
    }
    check(areaEqual, L"Partial area selector matches full render for expansion, shrinking, crossed "
                     L"origin, zero area and screen edges");
    check(interiorUntouched, L"Growing selection leaves unchanged interior outside dirty region");
    check(overlayCache.builds == selectorBuilds,
          L"Moving area selector reuses frozen and shaded desktop caches");
    app.windowMode = true;
    app.windows = {{{20, 30, 750, 500}}, {{200, 150, 990, 580}}};
    app.hover = -1;
    paintOverlay(app.overlay, selectorActual.dc);
    bool windowEqual = true;
    for (POINT point :
         std::initializer_list<POINT>{{100, 100}, {950, 550}, {999, 599}, {250, 250}}) {
        RECT previousSelection = overlaySelection(), oldHint = overlayCache.hint;
        updateOverlay(app.overlay, point);
        HRGN damage =
            overlayDamage(previousSelection, overlaySelection(), oldHint, overlayCache.hint);
        int saved = SaveDC(selectorActual.dc);
        SelectClipRgn(selectorActual.dc, damage);
        paintOverlay(app.overlay, selectorActual.dc);
        RestoreDC(selectorActual.dc, saved);
        DeleteObject(damage);
        {
            Graphics g(selectorExpected.dc);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            renderOverlay(g, app.overlay);
        }
        {
            auto a = pixels(selectorActual), b = pixels(selectorExpected);
            size_t diff = 0;
            for (size_t p = 0; p < a.size(); p++)
                if (a[p] != b[p]) {
                    if (diff == 0) {
                        wchar_t diagnostic[180];
                        swprintf_s(
                            diagnostic,
                            L"window (%ld,%ld), first diff (%zu,%zu) actual %08x expected %08x\r\n",
                            point.x, point.y, p % 1000, p / 1000, a[p], b[p]);
                        log += diagnostic;
                    }
                    diff++;
                }
            if (diff) {
                log += L"window mismatches " + std::to_wstring(diff) + L"\r\n";
                windowEqual = false;
            }
        }
    }
    check(
        windowEqual,
        L"Partial window selector matches full render for overlapping windows and no-window hover");
    RECT steady = overlaySelection();
    ValidateRect(app.overlay, nullptr);
    updateOverlay(app.overlay, {100, 100});
    check(EqualRect(&steady, &app.windows[0].rect) && !GetUpdateRect(app.overlay, nullptr, FALSE),
          L"Moving cursor within same window avoids repaint");
    selectorActual.reset();
    selectorExpected.reset();
    ReleaseDC(app.overlay, selectorDC);
    app.windowMode = false;
    app.selecting = true;
    app.dragStart = {100, 100};
    app.selection = {100, 100, 200, 200};
    SendMessageW(app.overlay, WM_LBUTTONUP, 0, MAKELPARAM(420, 360));
    RECT releasedFrame = canvasBounds();
    check(releasedFrame.left == 100 && releasedFrame.top == 100 && releasedFrame.right == 420 &&
              releasedFrame.bottom == 360,
          L"Initial frame uses actual release position without a final move message");
    check(!overlayCache.original.dc && !overlayCache.dark.dc && !overlayBuffer.dc,
          L"Completing capture releases selector buffers");

    // Export only synthetic content to the test directory. Native file chooser
    // and clipboard publication are substituted so no user data is touched.
    testPicturesDirectory = directory + L"\\Pictures";
    CreateDirectoryW(testPicturesDirectory.c_str(), nullptr);
    testSaveDialogPath = L"";
    int exportsBefore = testExportCompletions, dialogsBefore = testSaveDialogs;
    perform(5);
    check(testSaveDialogs == dialogsBefore && !testSavedPath.empty() &&
              testSavedPath.find(testPicturesDirectory + L"\\Screenshots\\") == 0,
          L"Save button writes automatically under Pictures Screenshots without a dialog");
    check(testExportCompletions == exportsBefore + 1 && !IsWindowVisible(app.main) &&
              IsWindow(app.main),
          L"Successful automatic save closes editor while preserving application lifetime");
    auto savedPng = std::unique_ptr<Bitmap>(Bitmap::FromFile(testSavedPath.c_str()));
    check(savedPng && savedPng->GetLastStatus() == Ok && savedPng->GetWidth() == 320 &&
              savedPng->GetHeight() == 260,
          L"Automatic PNG decodes at original crop resolution");
    savedPng.reset();
    std::wstring firstPath, secondPath;
    auto exportBitmap = flattened();
    bool firstSaved = saveAutomatically(*exportBitmap, firstPath, L"same-name.png"),
         secondSaved = saveAutomatically(*exportBitmap, secondPath, L"same-name.png");
    check(firstSaved && secondSaved && firstPath != secondPath &&
              GetFileAttributesW(firstPath.c_str()) != INVALID_FILE_ATTRIBUTES,
          L"Repeated filenames preserve existing captures using atomic unique names");
    testSaveDialogPath = directory + L"\\custom-destination.png";
    exportsBefore = testExportCompletions;
    dialogsBefore = testSaveDialogs;
    keyDown('S', true, true);
    check(testSaveDialogs == dialogsBefore + 1 && testSavedPath == testSaveDialogPath &&
              testExportCompletions == exportsBefore + 1,
          L"Ctrl Shift S chooses custom destination then closes editor after saving");
    testSaveDialogPath = directory + L"\\shift-click-custom.png";
    dialogsBefore = testSaveDialogs;
    exportsBefore = testExportCompletions;
    testShiftPressed = true;
    perform(5);
    testShiftPressed = false;
    check(testSaveDialogs == dialogsBefore + 1 && testSavedPath == testSaveDialogPath &&
              testExportCompletions == exportsBefore + 1,
          L"Shift clicking Save chooses destination and closes only after successful write");
    dialogsBefore = testSaveDialogs;
    exportsBefore = testExportCompletions;
    keyDown('S', true, false);
    check(testSaveDialogs == dialogsBefore && testExportCompletions == exportsBefore + 1 &&
              testSavedPath.find(L"\\Screenshots\\") != std::wstring::npos,
          L"Ctrl S saves automatically without a chooser and closes editor");
    testSaveDialogPath = L"";
    exportsBefore = testExportCompletions;
    saveImage(true);
    check(testExportCompletions == exportsBefore && app.image != nullptr,
          L"Canceling custom save keeps editor session and does not complete export");
    testSaveDialogPath = directory + L"\\missing-parent\\failure.png";
    saveImage(true);
    check(testExportCompletions == exportsBefore, L"Failed custom save does not close editor");
    std::wstring picturesBefore = testPicturesDirectory;
    testPicturesDirectory = directory + L"\\missing-parent\\Pictures";
    saveImage();
    check(testExportCompletions == exportsBefore,
          L"Failed automatic directory creation does not close editor");
    testPicturesDirectory = picturesBefore;
    dialogsBefore = testSaveDialogs;
    testSaveDialogPath = directory + L"\\hotkey-custom.png";
    captureHotkey(true);
    check(testSaveDialogs == dialogsBefore + 1 && testSavedPath == testSaveDialogPath,
          L"Global capture hotkey in active editor chooses save destination");
    captureHotkey(false);
    check(app.capturePending, L"Global capture hotkey outside editor still starts capture");
    cancelCapture();
    app.captureShortcut = {MOD_CONTROL | MOD_ALT, 'K'};
    dialogsBefore = testSaveDialogs;
    captureHotkey(true);
    check(app.capturePending && testSaveDialogs == dialogsBefore,
          L"Custom shortcut starts capture in editor without replacing Save As binding");
    cancelCapture();
    keyDown('S', true, true);
    check(testSaveDialogs == dialogsBefore + 1,
          L"Ctrl Shift S still chooses save destination after capture shortcut changes");
    app.captureShortcut = {};
    testClipboardSuccess = true;
    exportsBefore = testExportCompletions;
    perform(4);
    check(testExportCompletions == exportsBefore + 1,
          L"Copy button closes editor after clipboard publication succeeds");
    keyDown('C', true, false);
    check(testExportCompletions == exportsBefore + 2,
          L"Ctrl C closes editor after clipboard publication succeeds");
    testClipboardSuccess = false;
    copyImage();
    check(testExportCompletions == exportsBefore + 2,
          L"Clipboard publication failure keeps editor session");
    testClipboardSuccess = true;
    check(wcscmp(tooltipFor(5), L"Save (Ctrl+S)") == 0,
          L"Save tooltip stays simple and does not disclose alternate save shortcut");

    // Compare old whole-desktop rendering with the new production region path.
    app.frozen = std::make_unique<Bitmap>(3840, 2160, PixelFormat32bppARGB);
    {
        auto base = fixture();
        Graphics g(app.frozen.get());
        g.DrawImage(base.get(), 0, 0, 3840, 2160);
    }
    app.desktop = {0, 0, 3840, 2160};
    app.windowMode = false;
    app.selection = {300, 250, 3000, 1900};
    app.selecting = true;
    app.dragStart = {300, 250};
    app.overlay = CreateWindowExW(WS_EX_TOOLWINDOW, OverlayClass, L"test 4K selector", WS_POPUP, 0,
                                  0, 3840, 2160, nullptr, nullptr, app.instance, nullptr);
    HDC overlayBenchDC = GetDC(app.overlay);
    PaintBuffer overlayBench;
    overlayBench.ensure(overlayBenchDC, 3840, 2160);
    paintOverlay(app.overlay, overlayBench.dc);
    LARGE_INTEGER overlayFrequency;
    QueryPerformanceFrequency(&overlayFrequency);
    std::vector<double> legacyTimes, fastTimes;
    for (int i = 0; i < 12; i++) {
        LARGE_INTEGER start, end;
        QueryPerformanceCounter(&start);
        {
            Graphics g(overlayBench.dc);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            renderOverlay(g, app.overlay);
        }
        GdiFlush();
        QueryPerformanceCounter(&end);
        if (i >= 2)
            legacyTimes.push_back((end.QuadPart - start.QuadPart) * 1000.0 /
                                  overlayFrequency.QuadPart);
    }
    for (int i = 0; i < 125; i++) {
        RECT previousSelection = overlaySelection(), hint = overlayCache.hint;
        LARGE_INTEGER start, end;
        QueryPerformanceCounter(&start);
        updateOverlay(app.overlay, {3000 + (i % 30) * 3, 1900 + (i % 20) * 2});
        HRGN damage = overlayDamage(previousSelection, overlaySelection(), hint, overlayCache.hint);
        int saved = SaveDC(overlayBench.dc);
        SelectClipRgn(overlayBench.dc, damage);
        paintOverlay(app.overlay, overlayBench.dc);
        RestoreDC(overlayBench.dc, saved);
        DeleteObject(damage);
        GdiFlush();
        QueryPerformanceCounter(&end);
        if (i >= 5)
            fastTimes.push_back((end.QuadPart - start.QuadPart) * 1000.0 /
                                overlayFrequency.QuadPart);
    }
    std::sort(legacyTimes.begin(), legacyTimes.end());
    std::sort(fastTimes.begin(), fastTimes.end());
    FILE* selectorPerformance = nullptr;
    _wfopen_s(&selectorPerformance, (directory + L"\\selector-performance.txt").c_str(), L"wb");
    if (selectorPerformance) {
        fprintf(selectorPerformance,
                "3840 x 2160 frozen desktop, large area selection\nLegacy full rendering median "
                "%.3f ms (10 iterations, 2 warmup)\nCached region rendering median %.3f ms, P95 "
                "%.3f ms (120 iterations, 5 warmup)\n",
                legacyTimes[legacyTimes.size() / 2], fastTimes[fastTimes.size() / 2],
                fastTimes[static_cast<size_t>(fastTimes.size() * 0.95)]);
        fclose(selectorPerformance);
    }
    overlayBench.reset();
    ReleaseDC(app.overlay, overlayBenchDC);
    cancelCapture();
    app.dragging = false;
    addFixtureItems();
    // Benchmark the actual production paint path at 4K source resolution.
    // Includes restore, annotation drawing and final HDC blit; no user input.
    auto smallFixture = fixture();
    app.image = std::make_unique<Bitmap>(3840, 2160, PixelFormat32bppARGB);
    {
        Graphics g(app.image.get());
        g.DrawImage(smallFixture.get(), 0, 0, 3840, 2160);
    }
    app.items.clear();
    Item moving;
    moving.tool = Tool::Rectangle;
    moving.a = {600, 500};
    moving.b = {1300, 1000};
    moving.style.size = 5;
    app.items.push_back(moving);
    app.selected = 0;
    app.tool = Tool::Select;
    app.dragging = true;
    app.creating = false;
    app.layout = calculateLayout(app.main);
    app.dragStart = {900, 500};
    app.initialItem = moving;
    HDC benchDC = GetDC(app.main), benchMemory = CreateCompatibleDC(benchDC);
    HBITMAP benchSurface = CreateCompatibleBitmap(benchDC, client.right, client.bottom);
    HGDIOBJ benchOld = SelectObject(benchMemory, benchSurface);
    std::vector<double> durations;
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    for (int i = 0; i < 125; i++) {
        PointF next = toScreen({900.f + (i % 30) * 5, 500.f + (i % 20) * 3});
        LARGE_INTEGER start, end;
        QueryPerformanceCounter(&start);
        mouseMove(next);
        SendMessageW(app.main, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(benchMemory), PRF_CLIENT);
        GdiFlush();
        QueryPerformanceCounter(&end);
        if (i >= 5)
            durations.push_back((end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart);
    }
    std::sort(durations.begin(), durations.end());
    FILE* performance = nullptr;
    _wfopen_s(&performance, (directory + L"\\performance.txt").c_str(), L"wb");
    if (performance) {
        fprintf(performance,
                "4K source, %ld x %ld client, 120 measured drag+paint iterations (5 "
                "warmup)\nMedian %.3f ms\nP95 %.3f ms\n",
                client.right, client.bottom, durations[durations.size() / 2],
                durations[static_cast<size_t>(durations.size() * 0.95)]);
        fclose(performance);
    }
    mouseUp();
    SelectObject(benchMemory, benchOld);
    DeleteObject(benchSurface);
    DeleteDC(benchMemory);
    ReleaseDC(app.main, benchDC);
    // Expansion uses original-image coordinates, so left/top growth never
    // changes stored annotation coordinates or copies the captured bitmap.
    app.image = std::make_unique<Bitmap>(240, 160, PixelFormat32bppARGB);
    app.canvas = {};
    app.pinnedCanvas = false;
    edgeColors.reset();
    {
        Graphics g(app.image.get());
        g.Clear(Color(255, 244, 241, 233));
        SolidBrush left{Color(Swatches[3])}, right{Color(Swatches[4])}, top{Color(Swatches[0])},
            bottom{Color(Swatches[2])}, accent{Color(Swatches[5])};
        g.FillRectangle(&left, 0, 0, 10, 160);
        g.FillRectangle(&right, 230, 0, 10, 160);
        g.FillRectangle(&top, 0, 0, 240, 10);
        g.FillRectangle(&bottom, 0, 150, 240, 10);
        g.FillRectangle(&accent, 20, 20, 200, 120);
    }
    auto originalPixels = [&]() {
        BitmapData data{};
        Rect whole(0, 0, 240, 160);
        std::vector<ARGB> result(240 * 160);
        if (app.image->LockBits(&whole, ImageLockModeRead, PixelFormat32bppARGB, &data) != Ok)
            return std::vector<ARGB>();
        for (int y = 0; y < 160; y++)
            memcpy(result.data() + y * 240,
                   static_cast<const BYTE*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride,
                   240 * 4);
        app.image->UnlockBits(&data);
        return result;
    };
    auto untouchedPixels = originalPixels();
    check(dominantEdgeColor(1) == Swatches[3] && dominantEdgeColor(2) == Swatches[4] &&
              dominantEdgeColor(4) == Swatches[0] && dominantEdgeColor(8) == Swatches[2],
          L"Each side samples only its original 10-pixel strip and chooses the most frequent exact "
          L"RGB");
    check(dominantEdgeColor(1 | 4) == Swatches[0] && dominantEdgeColor(2 | 8) == Swatches[2],
          L"Corner color combines both edge strips with corner pixels counted once");
    unsigned sampledBefore = edgeColors.builds;
    dominantEdgeColor(1 | 4);
    dominantEdgeColor(2);
    check(edgeColors.builds == sampledBefore,
          L"Edge color sampling is cached between moves and repeated extensions");
    auto resetExpansion = [&]() {
        commitText();
        app.canvas = {};
        app.pinnedCanvas = false;
        app.items.clear();
        app.undo.clear();
        app.redo.clear();
        app.selected = -1;
        app.userZoom = 1;
        app.pan = {0, 0};
        captureView.reset();
        dragBackground.reset();
        app.layout = calculateLayout(app.main);
        invalidate();
    };
    auto centered = [&]() {
        Layout view = calculateLayout(app.main);
        RECT bounds = canvasBounds();
        return std::abs(view.imageX + (bounds.left + bounds.right) * 0.5f * view.zoom -
                        (view.canvas.X + view.canvas.Width / 2)) < 0.1f &&
               std::abs(view.imageY + (bounds.top + bounds.bottom) * 0.5f * view.zoom -
                        (view.canvas.Y + view.canvas.Height / 2)) < 0.1f;
    };
    resetExpansion();
    setTool(Tool::Arrow);
    app.defaults[1].size = 12;
    app.defaults[1].color = 0xff202833;
    Layout stationary = app.layout;
    mouseDown(toScreen({120, 80}));
    HDC growingDC = GetDC(app.main);
    PaintBuffer growingFrame;
    growingFrame.ensure(growingDC, client.right, client.bottom);
    paint(app.main, false, growingFrame.dc);
    unsigned sourceBuilds = captureView.builds;
    mouseMove(toScreen({280, 80}));
    paint(app.main, false, growingFrame.dc);
    RECT enlarged = canvasBounds();
    Layout live = calculateLayout(app.main);
    check(enlarged.right > 280 && enlarged.left == 0 && enlarged.top == 0 &&
              enlarged.bottom == 160 && approx(app.items[0].b.X, 280),
          L"Dragging arrow past right edge grows only that side and does not clamp its endpoint");
    check(app.pinnedCanvas && approx(live.imageX, stationary.imageX) &&
              approx(live.imageY, stationary.imageY) && approx(live.zoom, stationary.zoom),
          L"Original capture position and zoom remain fixed while canvas expands");
    mouseMove(toScreen({330, 80}));
    paint(app.main, false, growingFrame.dc);
    check(
        captureView.builds == sourceBuilds && app.canvas.color == Swatches[4],
        L"Growing canvas reuses scaled capture and fills new space with original right-edge color");
    Item beforeMarginMove = app.items[0];
    RECT oldMarginArea = annotationBounds(beforeMarginMove, app.layout);
    mouseMove(toScreen({333, 80}));
    RECT marginArea = annotationBounds(app.items[0], app.layout), marginDamage{};
    UnionRect(&marginDamage, &oldMarginArea, &marginArea);
    int marginSaved = SaveDC(growingFrame.dc);
    IntersectClipRect(growingFrame.dc, marginDamage.left, marginDamage.top, marginDamage.right,
                      marginDamage.bottom);
    paint(app.main, false, growingFrame.dc);
    RestoreDC(growingFrame.dc, marginSaved);
    PaintBuffer growthReference;
    growthReference.ensure(growingDC, client.right, client.bottom);
    {
        Graphics g(growthReference.dc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        renderEditor(g, app.main, -1, true, true);
    }
    auto marginActual = pixels(growingFrame), marginExpected = pixels(growthReference);
    bool marginEqual = marginActual.size() == marginExpected.size();
    for (size_t k = 0; k < marginActual.size() && k < marginExpected.size(); k++)
        if ((marginActual[k] & 0xffffff) != (marginExpected[k] & 0xffffff))
            marginEqual = false;
    check(marginEqual,
          L"Partial dragging inside newly exposed margin matches full render without trails");
    growthReference.reset();
    PointF finalTip = toScreen({365, 80});
    releasePointer(finalTip);
    enlarged = canvasBounds();
    check(!app.pinnedCanvas && centered() && approx(app.items[0].b.X, 365),
          L"Releasing recenters expanded canvas and applies latest cursor position");
    auto expandedImage = flattened();
    Color beyondOriginal;
    expandedImage->GetPixel(300, 80, &beyondOriginal);
    check(expandedImage->GetWidth() == static_cast<UINT>(enlarged.right) &&
              expandedImage->GetHeight() == 160 && beyondOriginal.GetB() < 100,
          L"Export contains arrow pixels outside original screenshot at enlarged resolution");
    Color originalCorner, paddingCorner;
    expandedImage->GetPixel(5, 5, &originalCorner);
    expandedImage->GetPixel(enlarged.right - 1, 1, &paddingCorner);
    check(originalCorner.GetValue() == Swatches[0] && paddingCorner.GetValue() == Swatches[4],
          L"Export preserves original pixels and uses inferred color only for added canvas");
    std::wstring expandedRoundTrip = directory + L"\\expanded-roundtrip.png";
    bool expandedSaved = encodePng(*expandedImage, expandedRoundTrip.c_str());
    {
        Bitmap restored(expandedRoundTrip.c_str());
        check(expandedSaved && restored.GetLastStatus() == Ok &&
                  restored.GetWidth() == expandedImage->GetWidth() && restored.GetHeight() == 160,
              L"Expanded PNG round trip preserves padded canvas dimensions");
    }
    HGLOBAL expandedDib = makeDib(*expandedImage);
    bool expandedDibCorrect = false;
    if (expandedDib) {
        auto* header = static_cast<BITMAPV5HEADER*>(GlobalLock(expandedDib));
        expandedDibCorrect =
            header && header->bV5Width == enlarged.right && header->bV5Height == -160;
        if (header)
            GlobalUnlock(expandedDib);
        GlobalFree(expandedDib);
    }
    check(expandedDibCorrect, L"Clipboard bitmap includes enlarged canvas at full resolution");
    undo(false);
    check(app.items.empty() && canvasBounds().right == 240 && canvasBounds().bottom == 160,
          L"One undo removes arrow and its complete canvas expansion");
    undo(true);
    check(app.items.size() == 1 && EqualRect(&enlarged, &app.canvas.bounds) &&
              app.canvas.color == Swatches[4],
          L"One redo restores drawing, canvas dimensions and sampled background color");
    growingFrame.reset();
    ReleaseDC(app.main, growingDC);
    resetExpansion();
    setTool(Tool::Line);
    Layout leftView = app.layout;
    mouseDown(toScreen({100, 80}));
    mouseMove(toScreen({-45, 80}));
    RECT leftBounds = canvasBounds();
    check(leftBounds.left < -45 && leftBounds.right == 240 && leftBounds.top == 0 &&
              app.canvas.color == Swatches[3] && approx(app.items[0].b.X, -45),
          L"Left expansion retains negative drawing coordinates and samples original left edge");
    check(approx(calculateLayout(app.main).imageX, leftView.imageX),
          L"Left growth leaves original capture screen origin stationary");
    mouseUp();
    leftBounds = canvasBounds();
    auto leftImage = flattened();
    Color leftOriginal;
    leftImage->GetPixel(5 - leftBounds.left, 5, &leftOriginal);
    check(leftImage->GetWidth() == static_cast<UINT>(240 - leftBounds.left) &&
              leftOriginal.GetValue() == Swatches[0],
          L"Left-expanded export translates original screenshot without changing its pixels");
    resetExpansion();
    setTool(Tool::Arrow);
    mouseDown(toScreen({120, 80}));
    mouseMove(toScreen({-40, -40}));
    RECT cornerBounds = canvasBounds();
    check(cornerBounds.left < -40 && cornerBounds.top < -40 && cornerBounds.right == 240 &&
              cornerBounds.bottom == 160 && app.canvas.color == Swatches[0],
          L"Upper-left corner extends both requested directions and combines their edge samples");
    mouseUp();
    auto cornerImage = flattened();
    Color preserved;
    cornerImage->GetPixel(5 - cornerBounds.left, 5 - cornerBounds.top, &preserved);
    check(preserved.GetValue() == Swatches[0] && centered(),
          L"Corner extension keeps original pixels at translated export origin and centers after "
          L"release");
    resetExpansion();
    setTool(Tool::Rectangle);
    mouseDown(toScreen({180, 100}));
    mouseMove(toScreen({270, 190}));
    check(canvasBounds().right > 270 && canvasBounds().bottom > 190 && canvasBounds().left == 0 &&
              canvasBounds().top == 0 && app.canvas.color == Swatches[2],
          L"Rectangle expands lower-right corner using combined right and bottom strips");
    mouseUp();
    resetExpansion();
    setTool(Tool::Brush);
    mouseDown(toScreen({200, 100}));
    mouseMove(toScreen({275, 195}));
    mouseUp();
    check(app.items[0].stroke.back().X > 240 && app.items[0].stroke.back().Y > 160 &&
              canvasBounds().right > 275 && canvasBounds().bottom > 195,
          L"Freehand stroke continues past both original image edges without clipping");
    resetExpansion();
    setTool(Tool::Counter);
    mouseDown(toScreen({230, 80}));
    mouseMove(toScreen({320, 80}));
    mouseUp();
    check(app.items[0].pointer && canvasBounds().right > 240 &&
              itemVisualBounds(app.items[0]).GetRight() <= canvasBounds().right,
          L"Counter extends canvas while retaining its bounded tangent tip geometry");
    resetExpansion();
    Item bentLine;
    bentLine.tool = Tool::Line;
    bentLine.a = {60, 80};
    bentLine.b = {180, 80};
    app.items.push_back(bentLine);
    app.selected = 0;
    app.tool = Tool::Select;
    mouseDown(toScreen(curveMiddle(app.items[0])));
    mouseMove(toScreen({120, -35}));
    mouseUp();
    check(app.items[0].bent && app.items[0].bend.Y < 0 && canvasBounds().top < -35 &&
              canvasBounds().left == 0 && canvasBounds().right == 240,
          L"Dragging curvature handle upward grows top only and keeps both endpoints fixed");
    resetExpansion();
    Item movedText;
    movedText.tool = Tool::Text;
    movedText.a = {20, 40};
    movedText.text = L"Text";
    app.items.push_back(movedText);
    app.selected = 0;
    app.tool = Tool::Select;
    mouseDown(toScreen(movedText.a));
    mouseMove(toScreen({-40, 40}));
    mouseUp();
    check(app.items[0].a.X < 0 && canvasBounds().left < 0 && canvasBounds().right == 240,
          L"Moving existing text past left edge extends canvas instead of clamping movement");
    resetExpansion();
    setTool(Tool::Text);
    mouseDown(toScreen({210, 40}));
    SetWindowTextW(app.edit, L"Un texto que supera el borde de la captura");
    check(app.canvas.bounds.right > 240 && app.pinnedCanvas,
          L"Live text grows canvas while keeping capture position fixed during typing");
    commitText();
    check(centered() && !app.pinnedCanvas, L"Committing live text centers expanded canvas");
    undo(false);
    check(app.items.empty() && canvasBounds().right == 240,
          L"Undo live text creation also removes its canvas expansion");
    resetExpansion();
    setTool(Tool::Text);
    mouseDown(toScreen({210, 40}));
    SetWindowTextW(app.edit, L"Texto cancelado fuera del borde");
    commitText(true);
    check(app.items.empty() && canvasBounds().right == 240 && !app.pinnedCanvas,
          L"Canceling inline text restores original canvas dimensions");
    resetExpansion();
    setTool(Tool::Arrow);
    mouseDown(toScreen({120, 80}));
    mouseMove(toScreen({-50, -40}));
    keyDown(VK_ESCAPE);
    check(app.items.empty() && canvasBounds().left == 0 && canvasBounds().top == 0 &&
              canvasBounds().right == 240,
          L"Escape cancels drawing and restores canvas from before gesture");
    resetExpansion();
    setTool(Tool::Arrow);
    app.defaults[1].arrow = 0;
    mouseDown(toScreen({120, 80}));
    mouseMove(toScreen({420, 80}));
    RECT maximumCanvas = canvasBounds();
    Layout maximumView = calculateLayout(app.main);
    mouseMove(toScreen({205, 80}));
    RECT heldCanvas = canvasBounds();
    Layout heldView = calculateLayout(app.main);
    check(EqualRect(&maximumCanvas, &heldCanvas) && approx(heldView.imageX, maximumView.imageX) &&
              approx(heldView.zoom, maximumView.zoom),
          L"Retracting arrow retains maximum canvas and stationary capture while button remains "
          L"down");
    releasePointer(toScreen({205, 80}));
    check(canvasBounds().right == 240 && canvasBounds().bottom == 160 && app.items.size() == 1 &&
              approx(app.items[0].b.X, 205) && centered(),
          L"Releasing arrow back inside capture removes all temporary margin and recenters");
    auto retractedExport = flattened();
    check(retractedExport->GetWidth() == 240 && retractedExport->GetHeight() == 160,
          L"Export excludes maximum drag excursion when final arrow fits original image");
    resetExpansion();
    setTool(Tool::Arrow);
    mouseDown(toScreen({120, 80}));
    mouseMove(toScreen({420, 80}));
    maximumCanvas = canvasBounds();
    mouseMove(toScreen({280, 80}));
    check(canvasBounds().right == maximumCanvas.right,
          L"Partly retracting beyond edge keeps largest extent until release");
    releasePointer(toScreen({280, 80}));
    RECT fittedCanvas = canvasBounds();
    RectF fittedInk = itemVisualBounds(app.items[0]);
    check(fittedCanvas.right < maximumCanvas.right && fittedCanvas.right >= fittedInk.GetRight() &&
              fittedCanvas.right - fittedInk.GetRight() < 1 && centered(),
          L"Release keeps only pixel-rounded space required by final arrow and stroke");
    undo(false);
    check(app.items.empty() && canvasBounds().right == 240,
          L"Undo final trimmed arrow restores original canvas");
    undo(true);
    check(app.items.size() == 1 && canvasBounds().right == fittedCanvas.right &&
              approx(app.items[0].b.X, 280),
          L"Redo restores final trimmed dimensions rather than transient maximum");
    setTool(Tool::Arrow);
    mouseDown(toScreen({80, 110}));
    mouseMove(toScreen({430, 110}));
    releasePointer(toScreen({195, 110}));
    check(app.items.size() == 2 && canvasBounds().right == fittedCanvas.right &&
              approx(app.items[0].b.X, 280),
          L"Trimming new arrow preserves margins needed by previously placed annotations");
    app.tool = Tool::Select;
    app.selected = 0;
    mouseDown(toScreen(app.items[0].b));
    mouseMove(toScreen({180, 80}));
    mouseUp();
    check(canvasBounds().right == 240 && app.items.size() == 2,
          L"Moving an existing expanded arrow inside shrinks canvas even without new growth during "
          L"this gesture");
    resetExpansion();
    setTool(Tool::Arrow);
    mouseDown(toScreen({120, 80}));
    mouseMove(toScreen({-150, -150}));
    maximumCanvas = canvasBounds();
    releasePointer(toScreen({-20, 30}));
    check(
        canvasBounds().left > maximumCanvas.left && canvasBounds().left < 0 &&
            canvasBounds().top == 0 && app.canvas.color == Swatches[3],
        L"Retreating from corner drops unused top margin and resamples remaining left edge color");
    resetExpansion();
    setTool(Tool::Arrow);
    mouseDown(toScreen({120, 80}));
    mouseMove(toScreen({420, 80}));
    PointF actualRelease = toScreen({260, 80});
    SendMessageW(app.main, WM_LBUTTONUP, 0,
                 MAKELPARAM(static_cast<int>(actualRelease.X), static_cast<int>(actualRelease.Y)));
    check(std::abs(app.items[0].b.X - 260) < 1 && canvasBounds().right < 300 &&
              canvasBounds().right > 260,
          L"Native mouse release trims using latest release position without an intervening "
          L"movement");
    resetExpansion();
    setTool(Tool::Arrow);
    mouseDown(toScreen({120, 80}));
    mouseMove(toScreen({420, 80}));
    releasePointer(toScreen({120, 80}));
    check(app.items.empty() && canvasBounds().right == 240,
          L"Discarding zero-length retracted arrow also removes its temporary canvas");
    resetExpansion();
    setTool(Tool::Brush);
    mouseDown(toScreen({200, 80}));
    mouseMove(toScreen({280, 80}));
    releasePointer(toScreen({200, 80}));
    check(canvasBounds().right > 280 && app.items[0].strokeBounds.GetRight() == 280,
          L"Retracted brush cursor retains margin for real stroke pixels drawn outside capture");
    resetExpansion();
    Item trimCurve;
    trimCurve.tool = Tool::Line;
    trimCurve.a = {60, 80};
    trimCurve.b = {180, 80};
    app.items.push_back(trimCurve);
    app.selected = 0;
    app.tool = Tool::Select;
    mouseDown(toScreen(curveMiddle(app.items[0])));
    mouseMove(toScreen({120, -80}));
    maximumCanvas = canvasBounds();
    releasePointer(toScreen({120, -20}));
    check(canvasBounds().top > maximumCanvas.top && canvasBounds().top < 0 &&
              itemVisualBounds(app.items[0]).Y >= canvasBounds().top &&
              approx(app.items[0].a.Y, 80) && approx(app.items[0].b.Y, 80),
          L"Retracting curve handle trims top margin to final visible curve while preserving "
          L"endpoints");
    resetExpansion();
    setTool(Tool::Text);
    mouseDown(toScreen({150, 40}));
    SetWindowTextW(app.edit, L"Texto largo que amplía temporalmente mucho el lienzo");
    maximumCanvas = canvasBounds();
    SetWindowTextW(app.edit, L"A");
    heldCanvas = canvasBounds();
    check(EqualRect(&maximumCanvas, &heldCanvas),
          L"Shortening inline text retains maximum canvas while editing");
    commitText();
    check(canvasBounds().right == 240 && app.items[0].text == L"A" && centered(),
          L"Confirming shortened text trims canvas to final content");
    // Manual framing keeps source pixels and complete annotation geometry.
    auto dragCorner = [&](int corner, PointF target) {
        app.layout = calculateLayout(app.main);
        mouseDown(toScreen(canvasCorners(canvasBounds())[corner]));
        mouseMove(toScreen(target));
        releasePointer(toScreen(target));
    };
    resetExpansion();
    app.tool = Tool::Select;
    bool cornerHits = true;
    auto startingCorners = canvasCorners(canvasBounds());
    for (int i = 0; i < 4; i++)
        if (canvasHandleAt(toScreen(startingCorners[i])) != i)
            cornerHits = false;
    check(cornerHits, L"All four canvas corner squares are directly actionable");
    Item retainedArrow;
    retainedArrow.tool = Tool::Arrow;
    retainedArrow.a = {80, 70};
    retainedArrow.b = {205, 70};
    retainedArrow.style.color = 0xff202833;
    Item retainedCounter;
    retainedCounter.tool = Tool::Counter;
    retainedCounter.a = {205, 135};
    app.items = {retainedArrow, retainedCounter};
    app.selected = 0;
    Layout beforeCrop = app.layout;
    mouseDown(toScreen(canvasCorners(canvasBounds())[2]));
    check(app.resizingCanvas && !app.dragging && app.canvasHandle == 2 && app.items.size() == 2,
          L"Canvas square starts framing instead of creating or modifying an annotation");
    mouseMove(toScreen({170, 115}));
    RECT cropPreview = canvasBounds();
    Layout duringCrop = calculateLayout(app.main);
    check(cropPreview.right == 170 && cropPreview.bottom == 115 &&
              approx(duringCrop.imageX, beforeCrop.imageX) &&
              approx(duringCrop.imageY, beforeCrop.imageY),
          L"Crop preview updates immediately with opposite corner and original capture fixed");
    releasePointer(toScreen({170, 115}));
    RECT manualCrop = canvasBounds();
    check(EqualRect(&manualCrop, &app.canvas.manualFrame) && !app.resizingCanvas && centered(),
          L"Release keeps explicit crop and recenters viewport without automatic trim");
    check(app.image->GetWidth() == 240 && app.image->GetHeight() == 160 && app.items.size() == 2 &&
              approx(app.items[0].b.X, 205) && approx(app.items[1].a.Y, 135),
          L"Cropping preserves complete original bitmap and all hidden annotation geometry");
    auto croppedImage = flattened();
    Color retainedCorner;
    croppedImage->GetPixel(0, 0, &retainedCorner);
    check(croppedImage->GetWidth() == 170 && croppedImage->GetHeight() == 115 &&
              retainedCorner.GetValue() == Swatches[0],
          L"Crop export uses visible frame dimensions and excludes square UI handles");
    HDC cropDC = GetDC(app.main);
    PaintBuffer cropFrame, cropReference;
    cropFrame.ensure(cropDC, client.right, client.bottom);
    cropReference.ensure(cropDC, client.right, client.bottom);
    paint(app.main, false, cropFrame.dc);
    {
        Graphics g(cropReference.dc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        renderEditor(g, app.main, -1, true, false);
    }
    auto cropPixels = pixels(cropFrame), cropReferencePixels = pixels(cropReference);
    bool cropEqual = cropPixels.size() == cropReferencePixels.size();
    for (size_t k = 0; k < cropPixels.size() && k < cropReferencePixels.size(); k++)
        if ((cropPixels[k] & 0xffffff) != (cropReferencePixels[k] & 0xffffff))
            cropEqual = false;
    check(cropEqual, L"Cached cropped viewport matches uncached source rendering pixel for pixel");
    PointF hiddenPixel = toScreen({210, 40});
    ARGB hiddenRGB = cropPixels[static_cast<size_t>(hiddenPixel.Y) * client.right +
                                static_cast<size_t>(hiddenPixel.X)] &
                     0xffffff;
    check(hiddenRGB == 0xf5f7fa, L"Original pixels outside crop are hidden in editor rather than "
                                 L"drawn in surrounding workspace");
    cropFrame.reset();
    cropReference.reset();
    ReleaseDC(app.main, cropDC);
    int cropExportsBefore = testExportCompletions;
    saveImage();
    {
        Bitmap savedCrop(testSavedPath.c_str());
        check(savedCrop.GetLastStatus() == Ok && savedCrop.GetWidth() == 170 &&
                  savedCrop.GetHeight() == 115 && testExportCompletions == cropExportsBefore + 1,
              L"Saving writes final crop and keeps full editable source in memory after export");
    }
    HGLOBAL cropDib = makeDib(*croppedImage);
    bool cropDibOK = false;
    if (cropDib) {
        auto* header = static_cast<BITMAPV5HEADER*>(GlobalLock(cropDib));
        cropDibOK = header && header->bV5Width == 170 && header->bV5Height == -115;
        if (header)
            GlobalUnlock(cropDib);
        GlobalFree(cropDib);
    }
    check(cropDibOK, L"Clipboard DIB uses final cropped dimensions");
    undo(false);
    check(canvasBounds().right == 240 && canvasBounds().bottom == 160 &&
              IsRectEmpty(&app.canvas.manualFrame) && app.items.size() == 2,
          L"Undo crop restores original frame and preserves annotations");
    undo(true);
    RECT redoneCrop = canvasBounds();
    check(EqualRect(&redoneCrop, &manualCrop), L"Redo restores exact manual crop");
    dragCorner(2, {240, 160});
    auto restoredImage = flattened();
    Color restoredCounter;
    restoredImage->GetPixel(215, 135, &restoredCounter);
    check(restoredImage->GetWidth() == 240 && restoredImage->GetHeight() == 160 &&
              restoredCounter.GetValue() == retainedCounter.style.color &&
              approx(app.items[0].b.X, 205),
          L"Stretching back reveals original pixels and annotation previously outside crop");
    dragCorner(2, {290, 200});
    auto extendedFrame = flattened();
    Color newPadding;
    extendedFrame->GetPixel(270, 180, &newPadding);
    check(extendedFrame->GetWidth() == 290 && extendedFrame->GetHeight() == 200 &&
              newPadding.GetValue() == Swatches[2] && app.image->GetWidth() == 240,
          L"Stretching beyond retained source fills extra canvas with combined edge color");
    RECT deliberatePadding = canvasBounds();
    setTool(Tool::Line);
    mouseDown(toScreen({100, 50}));
    releasePointer(toScreen({140, 50}));
    RECT afterInsideDrawing = canvasBounds();
    check(EqualRect(&deliberatePadding, &afterInsideDrawing),
          L"Drawing inside manually expanded frame does not remove user-selected padding");
    const std::array<PointF, 4> cropTargets = {PointF(40, 30), PointF(200, 30), PointF(200, 130),
                                               PointF(40, 130)};
    const std::array<RECT, 4> expectedFrames = {RECT{40, 30, 240, 160}, RECT{0, 30, 200, 160},
                                                RECT{0, 0, 200, 130}, RECT{40, 0, 240, 130}};
    bool allAnchored = true;
    for (int i = 0; i < 4; i++) {
        resetExpansion();
        dragCorner(i, cropTargets[i]);
        RECT actualFrame = canvasBounds();
        if (!EqualRect(&actualFrame, &expectedFrames[i]))
            allAnchored = false;
    }
    check(allAnchored, L"Each corner crops its two sides while opposite corner stays anchored");
    resetExpansion();
    mouseDown(toScreen(canvasCorners(canvasBounds())[0]));
    PointF offsetTarget = toScreen({40, 30});
    releasePointer(offsetTarget);
    RECT finalCornerFrame = canvasBounds();
    check(EqualRect(&finalCornerFrame, &expectedFrames[0]),
          L"Corner release uses final pointer position even without a move message");
    auto croppedOffset = flattened();
    Color sourceAtOrigin;
    app.image->GetPixel(40, 30, &sourceAtOrigin);
    Color exportAtOrigin;
    croppedOffset->GetPixel(0, 0, &exportAtOrigin);
    check(croppedOffset->GetWidth() == 200 && croppedOffset->GetHeight() == 130 &&
              sourceAtOrigin.GetValue() == exportAtOrigin.GetValue(),
          L"Left-top crop exports retained source at correct translated pixel origin");
    resetExpansion();
    PointF offCenter = add(toScreen(canvasCorners(canvasBounds())[2]), {4, 3});
    mouseDown(offCenter);
    releasePointer(add(toScreen({170, 115}), {4, 3}));
    check(canvasBounds().right == 170 && canvasBounds().bottom == 115,
          L"Grabbing square off center retains pointer offset without frame jump");
    resetExpansion();
    mouseDown(toScreen(canvasCorners(canvasBounds())[0]));
    mouseMove(toScreen({300, 200}));
    mouseUp();
    check(canvasBounds().right - canvasBounds().left == 2 &&
              canvasBounds().bottom - canvasBounds().top == 2,
          L"Dragging beyond opposite corner clamps to valid two-pixel crop instead of inversion");
    dragCorner(0, {0, 0});
    check(canvasBounds().right == 240 && canvasBounds().bottom == 160 && canvasBounds().left == 0,
          L"Tiny crop remains reversible by stretching original corner back out");
    resetExpansion();
    mouseDown(toScreen(canvasCorners(canvasBounds())[2]));
    mouseMove(toScreen({150, 100}));
    keyDown(VK_ESCAPE);
    check(canvasBounds().right == 240 && canvasBounds().bottom == 160 && !app.resizingCanvas &&
              !app.pinnedCanvas,
          L"Escape cancels framing and restores previous viewport");
    resetExpansion();
    mouseDown(toScreen(canvasCorners(canvasBounds())[2]));
    mouseMove(toScreen({150, 100}));
    SendMessageW(app.main, WM_CAPTURECHANGED, 0, 0);
    check(canvasBounds().right == 150 && !app.resizingCanvas && centered(),
          L"Capture loss finishes visible crop and recenters without leaving drag state");
    resetExpansion();
    dragCorner(2, {160, 110});
    dragCorner(0, {40, 30});
    manualCrop = canvasBounds();
    Item hiddenArrow = retainedArrow;
    hiddenArrow.a = {175, 55};
    hiddenArrow.b = {220, 55};
    hiddenArrow.cropped = true;
    app.items.push_back(hiddenArrow);
    setTool(Tool::Arrow);
    mouseDown(toScreen({60, 70}));
    releasePointer(toScreen({120, 70}));
    RECT unchangedCrop = canvasBounds();
    check(EqualRect(&manualCrop, &unchangedCrop) && app.items.size() == 2,
          L"Drawing inside crop does not reveal or discard a previously hidden annotation");
    app.tool = Tool::Select;
    app.selected = 1;
    mouseDown(toScreen(app.items[1].b));
    mouseMove(toScreen({205, 70}));
    RECT maximumCrop = canvasBounds();
    mouseMove(toScreen({130, 70}));
    RECT heldCrop = canvasBounds();
    check(maximumCrop.right > 160 && EqualRect(&maximumCrop, &heldCrop),
          L"Automatic growth still retains maximum extent while dragging on a manually cropped "
          L"frame");
    releasePointer(toScreen({130, 70}));
    RECT returnedCrop = canvasBounds();
    check(EqualRect(&manualCrop, &returnedCrop),
          L"Retracted annotation restores chosen manual crop without exposing hidden annotations");
    undo(false);
    RECT undoneCrop = canvasBounds();
    check(EqualRect(&manualCrop, &undoneCrop) && approx(app.items[1].b.X, 120),
          L"Undo annotation edit after crop preserves manual framing");
    check(originalPixels() == untouchedPixels, L"All expansion, cropping, editing, undo and export "
                                               L"operations leave original bitmap bytes unchanged");
    // Public visual example uses the same renderer and pointer path.
    app.image = fixture();
    edgeColors.reset();
    resetExpansion();
    addFixtureItems();
    app.layout = calculateLayout(app.main);
    setTool(Tool::Arrow);
    mouseDown(toScreen({915, 345}));
    mouseMove(toScreen({1180, 345}));
    mouseUp();
    {
        Bitmap expansionPreview(client.right, client.bottom, PixelFormat32bppARGB);
        Graphics g(&expansionPreview);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        renderEditor(g, app.main);
        encodePng(expansionPreview, (directory + L"\\expanded-editor.png").c_str());
    }
    auto expandedExample = flattened();
    encodePng(*expandedExample, (directory + L"\\expanded-capture.png").c_str());
    RECT fullExample = canvasBounds();
    dragCorner(2, {980, 570});
    dragCorner(0, {185, 290});
    {
        Bitmap cropPreviewImage(client.right, client.bottom, PixelFormat32bppARGB);
        Graphics g(&cropPreviewImage);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        renderEditor(g, app.main);
        encodePng(cropPreviewImage, (directory + L"\\cropped-editor.png").c_str());
    }
    auto finalCropExample = flattened();
    encodePng(*finalCropExample, (directory + L"\\cropped-capture.png").c_str());
    dragCorner(2, {static_cast<REAL>(fullExample.right), static_cast<REAL>(fullExample.bottom)});
    dragCorner(0, {static_cast<REAL>(fullExample.left), static_cast<REAL>(fullExample.top)});
    {
        Bitmap restoredPreview(client.right, client.bottom, PixelFormat32bppARGB);
        Graphics g(&restoredPreview);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        renderEditor(g, app.main);
        encodePng(restoredPreview, (directory + L"\\restored-editor.png").c_str());
    }
    testRetainedDesktop(directory, check);
    testNavigation(directory, check);
    testNavigationBounds(check);
    testOcr(directory, check);
    log += L"\r\n" + std::to_wstring(checks) + L" checks, " + std::to_wstring(failures) +
           L" failures\r\n";
    FILE* file = nullptr;
    _wfopen_s(&file, (directory + L"\\tests.txt").c_str(), L"wb");
    if (file) {
        int bytes = WideCharToMultiByte(CP_UTF8, 0, log.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string utf8(bytes, '\0');
        WideCharToMultiByte(CP_UTF8, 0, log.c_str(), -1, utf8.data(), bytes, nullptr, nullptr);
        fwrite(utf8.data(), 1, bytes - 1, file);
        fclose(file);
    } else
        failures++;
    DestroyWindow(app.main);
    app.main = nullptr;
    return failures ? 1 : 0;
}
