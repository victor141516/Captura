# Changelog

## 1.16.0

- Stop vertical scrolling, Shift + wheel scrolling, and mouse panning at the visible image boundaries.
- Keep smaller images wholly inside the editor viewport; navigate larger images to either edge without losing the frame.
- Adjust navigation limits after zooming or resizing the window, using the chosen frame rather than the hidden desktop.
- Reverse a drag or scroll immediately after reaching a boundary, with no accumulated overscroll distance.


## 1.15.0

- Make middle-button panning respond immediately and reuse the scaled capture while moving.
- Keep toolbar pixels during navigation and use bounded image tiles at large zoom levels.
- Use a fast native preview during Ctrl + wheel zoom, then restore final-quality rendering after the wheel stops.
- Accumulate queued wheel input correctly, including multiple notches and high-resolution wheel fractions.
- Preserve the retained desktop, current framing, annotations, and full-resolution image exports.


## 1.14.0

- Keep the complete frozen desktop when selecting an area or a window; the selection becomes the initial visible frame.
- Reveal the original surrounding desktop by expanding the corner handles or drawing beyond the selected area.
- Add matching background fill only beyond the complete captured desktop.
- Preserve reversible cropping, undo/redo, final annotation trimming, and visible-frame-only PNG and clipboard exports.


## 1.13.0

- Choose your capture shortcut from the tray menu's **Capture shortcut…** dialog.
- Record a combination directly, restore the default, or cancel without changing it.
- Keep the previous shortcut if the chosen combination is unavailable or the preference cannot be saved.
- Remember the shortcut between sessions and show it in Help, the tray menu, and capture hints.
- Preserve Ctrl + Shift + S for Save As inside the editor.
- The portable Windows x64 executable is 299 KiB.

## 1.12.0

First public release of Captura, with an English interface.

- Capture an area or a window with a global keyboard shortcut.
- Annotate with arrows, text, rectangles, lines, numbered counters, and a freehand brush.
- Adjust arrows and lines with editable endpoints and a center handle for curves.
- Crop with corner squares and recover hidden content by stretching the canvas back out during the session.
- Add room for annotations by drawing past an edge; unused automatic space is trimmed on release.
- Copy an image or save a PNG in one step. The editor closes after successful export and stays available in the tray.
- Portable Windows x64 executable, approximately 284 KiB, with no installation required.

The project includes a product README, a development guide, consistent source formatting, and a native integration suite.
