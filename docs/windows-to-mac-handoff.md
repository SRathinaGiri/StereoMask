# Windows v2.3.0 to Mac R18 Handoff

## Scope and verified reference

This is the reciprocal feature delta after importing the Mac workflow and creative tools into Windows. It compares Windows **v2.3.0** with **StereoMask Mac R18 / 2.1.8**, commit `705095b74b5e54bbb99e020dffb6d8c3ee0ca8a7`. Fetching Mac `origin/main` on 2026-10-16 confirmed this remains its tip. The local Mac checkout was not changed, and no Mac build/signing validation was performed.

Source of truth: [Windows v2.3.0 source](https://github.com/SRathinaGiri/StereoMask/tree/v2.3.0). The release's Mac Handoff ZIP contains this guide, release notes, and the Windows delta from v2.2.0. That delta is a review aid, not a patch to apply blindly over Mac-specific application code.

Mac R18 already contains the compact workflow, History, Guided Process, editable shortcuts, Stereo Window adjustment, nearest-edge frame insertion, picture frames, frame shapes, drop shadows, and stereo text/bubbles. Those features do not need to be ported back. Preserve Mac's Command/Option wording, updater, application bundle, signing, and Homebrew deployment.

## Features Mac should consider adding

### 1. Fuzzy Select

Click a seed pixel in either physical eye and flood through neighboring pixels whose RGB distance from the seed is within the selected tolerance. Only the connected region is selected. The result is committed immediately as an ordinary raster History object using the current Add/Subtract operation.

Windows implementation: `StereoViewWidget::colorSelectionMask(..., contiguous=true)` and `applyColorSelection` in `stereoviewwidget.cpp`; mode 9 in the Selection Tool combo. The flood fill uses an explicit visited map and four-neighbor connectivity, remains inside the source eye, and writes alpha into an ARGB mask.

### 2. Select by Color Threshold

Click a seed color to select every matching pixel in that physical eye, including disconnected regions. It uses the same tolerance and History integration as Fuzzy Select, but scans the complete source image rather than flood filling. Windows exposes this as mode 10.

Both tools should retain physical-eye identity under Swap, support Add/Subtract, create one Undo step per click, and use the existing raster `SelectionObject`/`rasterPNG` format. No project schema change is necessary. A zero-result click should not create an empty object.

### 3. Color Tolerance and workflow integration

Add a 0–255 Color Tolerance control beside the selection tools; Windows defaults to 32. The Guided Process and User Guide explain that Fuzzy selects one connected region while Color Threshold selects all matching pixels. Each created item can be renamed, hidden, reordered, switched between Add/Subtract, deleted, saved, previewed, and exported through the existing ordered compositor.

Suggested source map: `mainwindow.cpp` for combo entries, tolerance control and tooltips; `guidedprocess.cpp` for tutorial routing; `stereoviewwidget.h/.cpp` for the algorithms and click handling; `tests/creative_test.cpp` and `tests/help_test.cpp` for behavior/UI coverage.

### 4. Visible cursor for color tools

Fuzzy and Color Threshold are click tools, so hiding the system cursor as for a paint brush makes the seed location impossible to see. Use a visible crosshair while the pointer is over either eye and the normal arrow outside the image. Restore the crosshair after color picking, Escape, panning, and other temporary cursor states. Windows treats selection modes 7 and above as visible-pointer tools while Select/Erase brushes retain their drawn brush-circle behavior.

Regression coverage should synthesize mouse movement over the image in both modes and assert `Qt::CrossCursor`.

### 5. Directional and depth alignment references

Mac R18 currently uses the active/first selected frame point as the pivot for every alignment direction. Windows v2.2.0 uses geometry-specific references:

- Left: minimum selected X.
- Right: maximum selected X.
- Top: minimum selected Y.
- Bottom: maximum selected Y.
- Depth: median selected disparity; for an even count, average the two middle disparities.

Apply the chosen X/Y to the point and its curve-control coordinates exactly as the existing alignment command does. Keep the operation as one Undo step. Median depth is preferred because one accidentally distant disparity point does not pull the whole selection to an extreme.

### 6. Optional side-by-side Stereo Window preview

Windows adapts the R18 Stereo Window dialog to open in the same side-by-side layout as the main canvas, which makes physical-eye comparison and cropping easier. Red/cyan anaglyph remains available as an optional checkbox. Both views share the same offset, zoom/pan state, pixel controls, Apply/Cancel behavior, and original-eye recomputation.

This item is a presentation choice rather than a project-format change. Mac may retain anaglyph as its default or adopt the Windows SBS default after user testing.

### 7. Frame creation and closing-edge insertion

Right-clicks away from existing edges append corners in click order. With at least three vertices, a click within 10 screen pixels of an edge inserts there. Inserting on the closing edge uses index `count`, preserving the original first vertex instead of rotating the list. `addPoint` accepts that closing-edge index and splits a curved closing edge without flattening its Bezier geometry. Preserve Undo/Redo and source-coordinate ordering. See `stereoviewwidget.cpp`, `tests/smartselection_test.cpp`. This deliberately corrects R18's unlimited nearest-edge insertion behavior, which can reorder newly drawn corners.

### 8. Mouse-only navigation during precise editing

In frame editing (mode 0), polygonal lasso (7), and smart lasso (8), ordinary wheel scrolls both eyes vertically, Shift+wheel horizontally, and Ctrl+wheel zooms toward the pointer. Pixel-delta trackpads and angle-delta wheels are supported. Middle-button dragging remains free two-axis pan. Other tools retain ordinary wheel zoom. Pending source-coordinate anchors and frame geometry must not change. Keep Mac's physical Command/Control and trackpad expectations platform-specific rather than copying Windows modifiers without testing.

### 9. Processing feedback and worker boundaries

`operationprogress.h` provides an opaque, frameless, single-line modal message with a matching status-bar stage. It paints the background explicitly; styling alone gave blank/transparent native windows before synchronous work. It uses a wait cursor, blocks editing input, has no cancellation while committing, and restores UI state when destroyed. No guessed percentages are shown. Its worker-wait helper processes painting/timers while retaining the synchronous command/Undo contract; do not add unrestricted event processing around mutable document state.

Covered operations: image/project opening, lasso application, color selection, AutoMask, edge refinement, project saving, export, and applying text/bubbles (also Undo/Redo). Pure image decoding, paired matching/edge tracing, mask composition, color matching and text-layer rendering use copied snapshots in workers, not QWidget access. Some final composition/serialization remains synchronous with a visible stage message; this is not a claim that every operation is fully asynchronous.

Text application reports left-eye rendering, right-eye rendering, preview update, and saving when enabled. `applyTextObjects` populates both eye-specific text caches before returning, then warms composed preview caches. `cachedTextOverlay` keys include the source-image cache key, foreground-mask cache key, and every serialized text field. Repainting, anaglyph and export reuse those layers; changes to foreground or text invalidate them. Keep cache identity eye-specific and test invalidation, not only rendering speed.

### 10. One application-version constant

Change `STEREOMASK_VERSION` in `appversion.h`. Runtime title, About, splash and update checker use it. CMake parses the same header for project and macOS bundle versions and reconfigures when the header changes. Preserve Mac's separate updater URLs and packaging conventions. Release manifests, notes, filenames and screenshots are publication metadata, not runtime version constants.

### 11. Text relief and stereo orientation

New in v2.3.0: `StereoText` carries optional `relief` (0 flat, 1 embossed, 2 engraved), `surface` (0 colored letters, 1 cutout plate), `reliefSize` (1-32 pixels), and `tiltX`, `tiltY`, `tiltZ` integer degrees. X/Y are limited to -80..80 and Z to -180..180. These extend each existing `stereoText` object without changing `stereoTextVersion: 1`; absent fields retain the old flat appearance. Older readers may discard these fields when saving. Keep project copies for exchange; no selection-object or raster schema change is required.

`decorations.cpp` implements alpha-mask bevels, actual transparent glyph holes, per-eye relief offsets, and the shared `stereoTextTransform`. Both eyes use the same 3D orientation around the text-box center, followed by rectified off-axis projection. X is top/bottom pitch (positive bottom forward), Y is left/right yaw (positive left forward), and Z is clockwise roll. Camera distance is max(1024, twice the largest box dimension), with an artistic baseline of 6% of that distance. X/Y therefore create a horizontal disparity gradient while vertical coordinates match; the existing right-eye `-depth` offset still sets the overall depth. This projection is not calibrated to the source photo's cameras. Relief glyphs are rasterized in local coordinates with a 16-million-pixel working cap; do not crop them using the inverse-projected whole photo, which can cross the projective horizon and incorrectly hide text at steep angles. The Qt renderer is shared by preview, anaglyph, editing handles and export. Tests cover large-photo visibility from 32 through 80 degrees, pitch/yaw depth direction, vertical alignment, roll, alpha holes, persistence, Undo/Redo and export. Verify Mac fonts, modifier keys and native rendering separately.

## Suggested Port Order

1. Centralized version reporting and frame-point ordering regression fixes.
2. Navigation and platform modifier mapping.
3. Relief/tilt model, JSON defaults/bounds and shared renderer; then editing outlines/hit testing.
4. Processing panel, copied worker snapshots, text caches and applying-text stages.
5. Color-selection/alignment deltas above if not adopted independently.
6. Native fonts, source/pixel tests and finally Mac packaging/signing.

## Source Map

| Files | Porting responsibility |
| --- | --- |
| `decorations.h/.cpp` | Text fields, JSON, relief alpha masks, shared 3D plane projection |
| `creativetools.cpp` | Effects/axis controls, eye-specific outlines, move/resize hit testing |
| `operationprogress.h` | Opaque processing UI and worker wait/lifetime helper |
| `stereoviewwidget.h/.cpp` | Opening, selection work, text apply/cache, frame points, wheel routing |
| `mainwindow.h/.cpp` | Save/export progress lifetime, tool wiring, About/splash |
| `main.cpp`, `appversion.h`, `CMakeLists.txt` | Version source, application title, build/bundle metadata |
| `keyboardshortcuts.cpp` | Updated wheel gestures; adapt platform wording |
| `tests/{smartselection,creative,help,update}_test.cpp` | Geometry/pixels, UI/progress, persistence and update-check contracts |

## Validation Checklist for Mac

1. Verify Fuzzy connectivity and full-image color threshold behavior at tolerance 0, the default, and 255.
2. Test clicks in both eyes with Swap on and off, and verify Add/Subtract ordering, Undo/Redo, save/reload, preview, anaglyph, and transparent export.
3. Confirm the crosshair remains visible through mouse move, panning, color picking, Escape, and tool changes.
4. Test alignment with deliberately unordered selections so the clicked/first point is not the required extreme. Cover even and odd disparity counts.
5. If adopting SBS Stereo Window, test large images, resize/maximize, zoom retention, keyboard controls, Apply/Cancel/Reset, and project reload.
6. Run Mac packaging and physical Command/Control input checks independently; Windows tests cannot certify Finder launch, signing, or native event delivery.
7. Draw four frame corners in order, insert on the closing edge, and test straight/curved edge insertion with Undo/Redo. The first vertex must remain first.
8. Test ordinary/Shift/Ctrl wheel and pixel-delta trackpads in modes 0/7/8, while zoomed and with pending anchors. Verify both physical eyes pan together and point coordinates do not change.
9. Apply colored/cutout Flat/Embossed/Engraved text, opacity, bubble shapes and foreground occlusion. Verify transparent glyph holes, independent per-eye relief, Undo/Redo, old-project defaults, JSON bounds and roundtrip.
10. Check X angles 32/33/34/45/60/80 and negative angles on a large photo: text must not disappear. X/Y must give a disparity gradient with equal corresponding Y coordinates; Z alone must not introduce depth variation. Both eyes view one orientation, never opposite half-angles. Cross-eye viewing requires eye-order swapping, not reversing the object rotation.
11. Observe an opaque message before work starts and correct stages during file opening, single/paired lasso application and applying text/bubbles. Test success, invalid files, empty selections, errors, and cursor/modal cleanup. Avoid saying paired creation for single-eye operations.
12. Change text, image and foreground occlusion in turn; verify cached text layers invalidate correctly. Saved PNG eye pixels must match composed preview pixels. Check native fonts, anaglyph, Swap and transparent export.

## Release Verification on Windows

Build with Qt 6.11.0, MinGW 13.1 and OpenCV 4.12.0. Run all four CTest suites; creative/UI tests use the Windows font backend rather than offscreen placeholder fonts. Native screenshots include `windows-text-effects-ui.png`, `windows-operation-progress-ui.png`, `windows-apply-text-progress-ui.png` and `text-effects-gallery.png` in the build directory. Synthetic fixtures are regression evidence, not certification of every stereo image or Mac rendering environment.
