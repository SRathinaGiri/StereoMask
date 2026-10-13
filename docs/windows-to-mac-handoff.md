# Windows v2.2.0 to Mac R18 handoff

## Scope and verified reference

This is the reciprocal feature delta after importing the Mac workflow and creative tools into Windows. It compares the Windows v2.2.0 source with **StereoMask Mac R18 / 2.1.8**, commit `705095b74b5e54bbb99e020dffb6d8c3ee0ca8a7`, inspected on 2026-10-13.

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

## Validation checklist for Mac

1. Verify Fuzzy connectivity and full-image color threshold behavior at tolerance 0, the default, and 255.
2. Test clicks in both eyes with Swap on and off, and verify Add/Subtract ordering, Undo/Redo, save/reload, preview, anaglyph, and transparent export.
3. Confirm the crosshair remains visible through mouse move, panning, color picking, Escape, and tool changes.
4. Test alignment with deliberately unordered selections so the clicked/first point is not the required extreme. Cover even and odd disparity counts.
5. If adopting SBS Stereo Window, test large images, resize/maximize, zoom retention, keyboard controls, Apply/Cancel/Reset, and project reload.
6. Run Mac packaging and physical Command/Control input checks independently; Windows tests cannot certify Finder launch, signing, or native event delivery.
