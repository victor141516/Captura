// Drive real wheel and middle-button handlers without capturing personal content.
template <typename Check> void testNavigationBounds(Check check) {
    app.dragging = app.panning = app.resizingCanvas = false;
    app.pinnedCanvas = false;
    app.items.clear();
    app.selected = -1;
    const Bitmap* retained = app.image.get();
    auto reset = [&](RECT frame, REAL zoom) {
        app.canvas.bounds = app.canvas.manualFrame = frame;
        app.userZoom = zoom;
        app.pan = {};
        app.layout = calculateLayout(app.main);
    };
    auto screenFrame = [&]() {
        Layout view = calculateLayout(app.main);
        RECT frame = canvasBounds();
        return RectF(view.imageX + frame.left * view.zoom, view.imageY + frame.top * view.zoom,
                     (frame.right - frame.left) * view.zoom,
                     (frame.bottom - frame.top) * view.zoom);
    };
    auto equal = [](REAL a, REAL b) { return std::abs(a - b) < 0.01f; };
    auto scroll = [&](int delta, bool horizontal = false) {
        SendMessageW(app.main, WM_MOUSEWHEEL,
                     MAKEWPARAM(horizontal ? MK_SHIFT : 0, static_cast<WORD>(delta)), 0);
    };
    auto reach = [&](int direction, bool horizontal) {
        for (int i = 0; i < 40; i++)
            scroll(direction * WHEEL_DELTA, horizontal);
    };
    reset({140, 100, 300, 220}, 1);
    Layout view = calculateLayout(app.main);
    reach(1, false);
    check(equal(screenFrame().GetBottom(), view.canvas.GetBottom()),
          L"Small frame vertical scroll stops exactly at bottom of viewport");
    PointF stopped = app.pan;
    reach(1, false);
    check(equal(app.pan.Y, stopped.Y),
          L"Repeated scroll at boundary cannot move image farther out");
    scroll(-WHEEL_DELTA);
    check(equal(app.pan.Y, stopped.Y - 60),
          L"Reversing wheel at boundary moves immediately without overscroll debt");
    reach(-1, false);
    check(equal(screenFrame().Y, view.canvas.Y),
          L"Small frame vertical scroll stops exactly at top of viewport");
    reach(1, true);
    check(equal(screenFrame().GetRight(), view.canvas.GetRight()),
          L"Shift wheel stops small frame at right of viewport");
    reach(-1, true);
    check(equal(screenFrame().X, view.canvas.X),
          L"Shift wheel stops small frame at left of viewport");
    RectF shown = screenFrame();
    check(shown.X >= view.canvas.X - 0.01f && shown.Y >= view.canvas.Y - 0.01f &&
              shown.GetRight() <= view.canvas.GetRight() + 0.01f &&
              shown.GetBottom() <= view.canvas.GetBottom() + 0.01f,
          L"Limits use selected frame rather than hidden retained desktop dimensions");

    reset({1000, 550, 2200, 1450}, 2);
    view = calculateLayout(app.main);
    reach(1, false);
    check(equal(screenFrame().Y, view.canvas.Y),
          L"Zoomed image scroll stops when its top reaches viewport top");
    reach(-1, false);
    check(equal(screenFrame().GetBottom(), view.canvas.GetBottom()),
          L"Zoomed image scroll reaches all bottom content without scrolling beyond it");
    reach(1, true);
    check(equal(screenFrame().X, view.canvas.X),
          L"Zoomed image scroll reaches left edge without blank gutter");
    reach(-1, true);
    check(equal(screenFrame().GetRight(), view.canvas.GetRight()),
          L"Zoomed image scroll reaches right edge without losing image");

    reset({-80, -40, 480, 320}, 1);
    view = calculateLayout(app.main);
    reach(1, true);
    reach(1, false);
    check(equal(screenFrame().GetRight(), view.canvas.GetRight()) &&
              equal(screenFrame().GetBottom(), view.canvas.GetBottom()),
          L"Expanded canvas with negative source coordinates respects same viewport bounds");

    reset({140, 100, 300, 220}, 1);
    view = calculateLayout(app.main);
    int x = static_cast<int>(view.canvas.Width / 2),
        y = static_cast<int>(view.canvas.Y + view.canvas.Height / 2);
    SendMessageW(app.main, WM_MBUTTONDOWN, MK_MBUTTON, MAKELPARAM(x, y));
    SendMessageW(app.main, WM_MOUSEMOVE, MK_MBUTTON, MAKELPARAM(x + 3000, y + 3000));
    check(equal(screenFrame().GetRight(), view.canvas.GetRight()) &&
              equal(screenFrame().GetBottom(), view.canvas.GetBottom()),
          L"Middle drag cannot push small frame out of viewport");
    stopped = app.pan;
    SendMessageW(app.main, WM_MOUSEMOVE, MK_MBUTTON, MAKELPARAM(x + 2995, y + 2993));
    check(equal(app.pan.X, stopped.X - 5) && equal(app.pan.Y, stopped.Y - 7),
          L"Middle drag reverses immediately after hitting boundary");
    SendMessageW(app.main, WM_MBUTTONUP, 0, MAKELPARAM(x + 2995, y + 2993));

    reset({1000, 550, 2200, 1450}, 8);
    reach(1, true);
    reach(-1, false);
    SendMessageW(app.main, WM_MOUSEWHEEL,
                 MAKEWPARAM(MK_CONTROL, static_cast<WORD>(-20 * WHEEL_DELTA)), 0);
    view = calculateLayout(app.main);
    shown = screenFrame();
    check(shown.X >= view.canvas.X - 0.01f && shown.Y >= view.canvas.Y - 0.01f &&
              shown.GetRight() <= view.canvas.GetRight() + 0.01f &&
              shown.GetBottom() <= view.canvas.GetBottom() + 0.01f,
          L"Zooming out after panning re-limits camera so smaller image remains visible");
    SendMessageW(app.main, WM_TIMER, ZoomRefineTimer, 0);

    reset({140, 100, 300, 220}, 1);
    reach(1, true);
    reach(1, false);
    RECT oldWindow{};
    GetWindowRect(app.main, &oldWindow);
    SetWindowPos(app.main, nullptr, 0, 0, 820, 580, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    view = calculateLayout(app.main);
    shown = screenFrame();
    check(shown.GetRight() <= view.canvas.GetRight() + 0.01f &&
              shown.GetBottom() <= view.canvas.GetBottom() + 0.01f,
          L"Resizing window re-limits old pan to its new visible area");
    SetWindowPos(app.main, nullptr, 0, 0, oldWindow.right - oldWindow.left,
                 oldWindow.bottom - oldWindow.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    check(app.image.get() == retained && app.image->GetWidth() == 3840 &&
              app.image->GetHeight() == 2160,
          L"Navigation bounds preserve full retained bitmap");
    app.pan = {};
}
