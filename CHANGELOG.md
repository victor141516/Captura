# Changelog

## 1.18.6 — Redaction preview

- Keep normal Lorem letter proportions instead of stretching width and height independently to fit the OCR rectangle.
- Choose from similarly long replacement phrases that naturally fit the line, with a uniform font scale and vertical centering. Exact bounding-box coverage no longer overrides letter shape.
- Preserve whole-line alignment, original color spans, stable one-time preparation, and full-resolution effects.

## 1.18.5 — Redaction preview

- Preserve original text colors within complete Lorem lines, including blue links surrounded by dark text, in both Lorem effects.
- Estimate foreground color from solid glyph cores rather than blended edges, preserving white lettering and avoiding black/white fallbacks for faint colored text.
- Reuse one line path and transform for every color span, favoring nearby gaps for color transitions. Color sampling still runs only once after OCR.

## 1.18.4 — Redaction preview

- Replace adjacent words as complete Lorem lines with one baseline, height, color, and horizontal scale, instead of fitting every replacement word to a separate OCR box.
- Join aligned OCR fragments, keep paragraph rows separate, and split large gaps between columns or independent labels.
- Keep line content stable in the one-time prepared image shared by both Lorem modes; moving and resizing effects still preserves the original outside the selected area.

## 1.18.3 — Redaction preview

- Fix low-strength Gaussian blur appearing horizontal and abruptly changing near the first quarter of the slider, in both Gaussian modes.
- Use the same normalized Gaussian kernel on both axes, with continuous strength scaling, fractional intermediate colors, and opaque clamped edges.
- Keep bounded interactive previews and full-resolution exports, with no extra runtime dependencies.

## 1.18.2 — Redaction preview

- Prepare one full-desktop Lorem image immediately after OCR, in the background worker, reusing its private screenshot snapshot.
- Reuse that immutable image for every Lorem blur and pixelation rectangle. Moving, resizing, adding, and overlapping areas no longer regenerate replacement text or resample text colors.
- Keep the original capture unchanged outside each effect rectangle and retain full-resolution exports.
- Use bounded direct previews for Lorem modes too, and show the one-time Lorem preparation duration in OCR metrics.
- Reject cancelled, unavailable, and obsolete prepared images; prevent earlier ordinary redactions from overwriting the Lorem base with original text.

## 1.18.1 — Redaction preview

- Match Lorem substitution to the original word's character count, capitalization, and visible width. Prefer equally long Lorem words and use phrases for longer originals.
- Preserve estimated glyph height, colors, and exact selection clipping.

## 1.18.0 — Redaction preview (local experiment)

- Add **Redact (B)** with Gaussian blur, pixelation, Lorem + Gaussian, and Lorem + Pixelate.
- Draw, move, resize, delete, undo, and redo effect rectangles. Moving the slider right increases blur radius or pixel block size.
- Estimate the background and text color around each intersecting OCR word, erase the original within the selection, and draw Lorem Ipsum with matching glyph height before applying the effect.
- Keep every pixel outside the selected rectangle unchanged, even when a rectangle cuts through a letter. Retain the full editable original until export.
- Exclude original words intersecting redactions from OCR highlights and text copying.
- Reuse processed regions during navigation, use bounded interactive previews, and always process exports at full resolution.
- Include earlier annotations in the effect and release all cached bitmaps before graphics shutdown.

## 1.17.0 — OCR preview (local experiment)

- Recognize the complete frozen desktop automatically in the background when a capture opens, including context outside the initial frame.
- Add **Select text (O)**: highlight recognized words and drag a rectangle to copy its text, preserving line breaks. Clicking a word copies that word.
- Keep the editor open after copying selected text; Ctrl+C and the Copy icon repeat the selection. Image copy and save still close the editor.
- Show elapsed and recognition time, process CPU time and average CPU, and sampled process RAM in the preview panel.
- Cancel obsolete OCR when a new capture opens. Bound native-resolution OCR tiles to limit temporary pixel memory.
- Use the Windows OCR engine and installed recognition languages, with no bundled model, network service, installer, or external runtime.

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
