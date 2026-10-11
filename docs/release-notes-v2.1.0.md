# StereoMask v2.1.0

Windows 64-bit release, combining Theodore Whitten's Mac foreground-editing foundation with the newer Windows object, lasso and preview workflows.

## Highlights

- Independent editable selection objects with names, visibility, delete, reorder, operation switching and saved JSON coordinates.
- Ordered Add/Subtract composition over the frame opening, including holes and later additions that restore them.
- Smart polygon lasso using OpenCV contrast edges; Shift-click inserts anchors and Ctrl-click removes them.
- Optional paired lasso creates an estimated opposite-eye polygon; both objects remain independent after Apply.
- Ctrl+Enter applies, Enter/C closes, arrow keys scroll both eyes, Shift+Arrow scrolls farther, and F8 toggles Clean Preview.
- Preview and export share per-eye compositing for Match preview, transparent PNG and solid cutout modes. Match preview retains mask color/opacity; transparent SBS preview uses a non-exported checkerboard.
- Synchronized mask-color picker, Settings, swatch and opacity; clearly separate padding/gap and custom cutout colors.
- Three selectable native OpenCV AutoMask starting algorithms, Freehand framing and its tool-switching regression fix.
- Updated offline help, original Stereomasken credits and a 26-item Windows/Mac comparison with porting/schema guidance.

## Windows Download

Download **StereoMask_v2.1.0_Release.zip**, extract the entire folder, and run StereoMask.exe. Keep the bundled Qt/MinGW/OpenCV DLLs and plugin folders alongside the executable. Save your project and close an older app before replacing its files. The package includes the guide, comparison, credits, license and third-party notices. No Python installation is required for the native OpenCV features.

## Compatibility and Limits

New images default to Match preview; existing projects retain their saved export background. Solid cutout modes intentionally do not use mask opacity. Windows imports older flattened selection masks as raster groups without inventing original polygon vertices.

**Use copies when exchanging projects with unmodified Mac R10.** It cannot losslessly preserve the new ordered object schema, holes inside the frame or Match preview mode. See [Windows/Mac comparison](windows-mac-comparison.md) for the implementation checklist.

Smart edges and paired alignment need inspection in both eyes; occlusions, texture and vertical misalignment can defeat matching. AutoMask is an editable starting frame, not a complete calibrated multi-region QTAGENT implementation. Canvas click-to-select, grouping and per-object feathering remain unimplemented.

## Verification and Credits

The three CTest suites cover selection/editing/JSON, matching, navigation, preview/export pixels and cache, actual help/UI wiring, and update checking. Synthetic checks do not certify every stereo photograph or a Mac build.

Original creative inspiration: **Bernd Paksa**, creator of [Stereomasken](https://www.stereomasken.de/index_en.htm). Modern Windows implementation/maintenance: **S. Rathinagiri (Subbiah Rathinagiri)**. Mac port and imported foreground-editing foundation: **Theodore Whitten**. Original MIT license retained; see CREDITS.md and packaged dependency notices.
