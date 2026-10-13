#pragma once

// The interface is English regardless of the system language.
namespace ui {
inline constexpr wchar_t kAppName[] = L"Captura";
inline constexpr const wchar_t* kTooltips[] = {L"Select (V)",    L"Arrow (A)", L"Text (T)",
                                               L"Rectangle (R)", L"Line (L)",  L"Counter (C)",
                                               L"Brush (P)"};
inline constexpr wchar_t kUndo[] = L"Undo (Ctrl+Z)";
inline constexpr wchar_t kRedo[] = L"Redo (Ctrl+Y)";
inline constexpr wchar_t kCopy[] = L"Copy (Ctrl+C)";
inline constexpr wchar_t kSave[] = L"Save (Ctrl+S)";
inline constexpr wchar_t kOneHead[] = L"One arrowhead";
inline constexpr wchar_t kTwoHeads[] = L"Two arrowheads";
inline constexpr wchar_t kColor[] = L"Color";
inline constexpr wchar_t kBackground[] = L"Background";
inline constexpr wchar_t kBackgroundTip[] = L"Background (right-click to toggle)";
inline constexpr wchar_t kMoreColors[] = L"More colors";
inline constexpr wchar_t kDecreaseNumber[] = L"Decrease number";
inline constexpr wchar_t kIncreaseNumber[] = L"Increase number";
inline constexpr wchar_t kRemovePointer[] = L"Remove pointer";
inline constexpr wchar_t kDelete[] = L"Delete (Del)";
inline constexpr wchar_t kHelpTip[] = L"Help (F1)";
inline constexpr wchar_t kNumber[] = L"Number";
inline constexpr wchar_t kEmptyTitle[] = L"Capture an area or a window";
inline constexpr wchar_t kEmptyHint[] = L"Space switches area / window. Esc cancels.";
inline constexpr wchar_t kSelectHint[] = L"Select and drag an annotation";
inline constexpr wchar_t kCounterHint[] = L"Click: counter. Click and drag: pointer.";
inline constexpr wchar_t kDrawHint[] = L"Draw on the screenshot. V to select.";
inline constexpr wchar_t kCaptureReady[] = L"Screenshot ready to annotate";
inline constexpr wchar_t kWindowHint[] =
    L"WINDOW   |   Space: area   |   Click: capture   |   Esc: cancel";
inline constexpr wchar_t kAreaHint[] =
    L"AREA   |   Drag to crop   |   Space: window   |   Esc: cancel";
inline constexpr wchar_t kNewCapture[] = L"New screenshot";
inline constexpr wchar_t kCaptureShortcutMenu[] = L"Capture shortcut…";
inline constexpr wchar_t kCaptureShortcutTitle[] = L"Captura — Capture shortcut";
inline constexpr wchar_t kCaptureShortcutLabel[] = L"Capture shortcut";
inline constexpr wchar_t kShortcutPrompt[] = L"Press a shortcut…";
inline constexpr wchar_t kShortcutInstructions[] =
    L"Click the field and press a combination with Ctrl, Alt, or Win.";
inline constexpr wchar_t kShortcutInvalid[] =
    L"Use a combination with Ctrl, Alt, or Win. F12 is reserved.";
inline constexpr wchar_t kShortcutUnavailable[] =
    L"This shortcut is unavailable. Try another combination.";
inline constexpr wchar_t kShortcutPersistenceError[] =
    L"Could not save this preference. Your previous shortcut is unchanged.";
inline constexpr wchar_t kShortcutDialogError[] = L"Could not open capture shortcut settings.";
inline constexpr wchar_t kResetShortcut[] = L"<a>Reset to default</a>";
inline constexpr wchar_t kCancel[] = L"Cancel";
inline constexpr wchar_t kSavePreference[] = L"Save";
inline constexpr wchar_t kOpenEditor[] = L"Open editor";
inline constexpr wchar_t kExit[] = L"Exit";
inline constexpr wchar_t kSelectWindowTitle[] = L"Captura: select";
inline constexpr wchar_t kPngFilter[] = L"PNG image (*.png)\0*.png\0\0";
inline constexpr wchar_t kSaveDialogTitle[] = L"Save screenshot";
inline constexpr wchar_t kErrorSave[] =
    L"Could not save the image. Check the destination and available disk space.";
inline constexpr wchar_t kErrorCopyMemory[] = L"Not enough memory to copy the image.";
inline constexpr wchar_t kErrorClipboardBusy[] = L"The clipboard is busy. Please try again.";
inline constexpr wchar_t kErrorCopy[] = L"Could not copy the image.";
inline constexpr wchar_t kErrorTextEditor[] = L"Could not open the text editor.";
inline constexpr wchar_t kErrorCapture[] =
    L"Could not capture the desktop. There may not be enough memory.";
inline constexpr wchar_t kErrorSelector[] = L"Could not display the capture selector.";
inline constexpr wchar_t kErrorGraphics[] = L"Could not initialize Windows graphics.";
inline constexpr wchar_t kErrorStartup[] = L"Could not start Captura.";
inline constexpr wchar_t kErrorEditor[] = L"Could not create the editor.";
inline constexpr wchar_t kShortcutBusy[] = L"Shortcut unavailable: use the tray menu";
inline constexpr wchar_t kShortcutBusyDetails[] =
    L" is unavailable. Capture from the tray menu or choose Capture shortcut… to change it.";
inline constexpr wchar_t kHelpTitle[] = L"Captura Help";
inline constexpr wchar_t kHelp[] =
    L"Captura\n\n"
    L"CAPTURE\n"
    L"{capture-shortcut}: new screenshot (Ctrl + Shift + S saves as inside the editor)\n"
    L"Change this shortcut from Capture shortcut… in the tray menu.\n"
    L"Space: switch area / window while capturing\n"
    L"Esc: cancel capture or selection\n\n"
    L"TOOLS\n"
    L"V Select   A Arrow   T Text\n"
    L"R Rectangle   L Line   C Counter   P Brush\n\n"
    L"SHARE\n"
    L"Ctrl + C: copy image\n"
    L"Ctrl + S: save PNG to Pictures\\Screenshots\n"
    L"Shift + click Save or Ctrl + Shift + S in the editor: choose a destination\n"
    L"Successful copying or saving closes the editor. Captura stays in the tray.\n\n"
    L"EDIT\n"
    L"Ctrl + Z: undo   Ctrl + Y / Ctrl + Shift + Z: redo\n"
    L"Del: delete   Ctrl + D: duplicate\n"
    L"Tab / Shift + Tab: next / previous annotation\n"
    L"Arrow keys: move; Shift: 10-pixel steps\n"
    L"+ / -: tool size\n"
    L"Shift while drawing: square rectangles or 45-degree angles\n\n"
    L"Drag the center diamond to bend arrows and lines; circles adjust their ends.\n"
    L"Text: type directly. Enter confirms; Shift + Enter adds a line; Esc cancels.\n"
    L"Double-click existing text to edit it.\n"
    L"Text starts with a rounded background and automatic black / white contrast.\n"
    L"Counter: click for a circle; click and drag for a pointer. Drag it to the center to remove "
    L"it.\n"
    L"Right-click the Background swatch to toggle fill.\n"
    L"The selected color is shared across tools. Strokes start at 12 px.\n\n"
    L"CANVAS\n"
    L"Ctrl + wheel: zoom   Ctrl + 0: fit   Ctrl + 1: actual size\n"
    L"Middle mouse or Space + drag: pan\n"
    L"Wheel: vertical scroll; Shift + wheel: horizontal scroll\n"
    L"Scrolling and panning stop at the image edges so the frame stays within reach.\n"
    L"Drag corner squares to crop or expand. Esc cancels.\n"
    L"Cropping is reversible during the session: stretch back to reveal hidden content.\n"
    L"Expand your area or window selection to reveal the surrounding frozen desktop.\n"
    L"Draw past an edge to reveal context; beyond the captured desktop, matching fill is added.\n"
    L"On release, unused automatic margins are trimmed.\n"
    L"Copy and Save export only the visible frame, without handles.\n\n"
    L"Close keeps Captura in the tray. Use its menu to exit.\n"
    L"No installation or internet connection required.";
} // namespace ui
