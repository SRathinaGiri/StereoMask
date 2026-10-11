# Windows v2.1.1 vs Mac R10: Comparison and Porting Checklist

## Verified Baseline

Compared on **2026-10-11**, using the current Windows working source and fetched `origin/main` of [TedInVR/StereoMask-Mac](https://github.com/TedInVR/StereoMask-Mac). The Mac tip is [`94c8ff8269d5e0e744ddb8aefcd61e487bcc6e73`](https://github.com/TedInVR/StereoMask-Mac/commit/94c8ff8269d5e0e744ddb8aefcd61e487bcc6e73), dated 2026-10-10, R10. Fetch confirmed it had not advanced beyond the inspected checkout. Mac source was read, not modified or built on macOS.

The comparison covers [v2.1.0](https://github.com/SRathinaGiri/StereoMask/releases/tag/v2.1.0) and the whole-polygon movement added in [v2.1.1](https://github.com/SRathinaGiri/StereoMask/releases/tag/v2.1.1). The original preceding Windows baseline is v2.0.0 (`9da98e0`). Recheck when either repository changes; the Mac baseline above remains the inspected R10 revision.

This is the complete user-facing feature/behavior and integration checklist for these baselines, not a line-by-line formatting diff.

## Already Shared

Editable frame/disparity points and curves; native OpenCV AutoMask; simplified Freehand framing; Select/Erase brushes; per-eye alpha masks; multiple Add/Subtract lassos; draggable pending anchors; last-segment Delete/Backspace; closed-outline Escape retention; transparent/black/white/custom cutouts; pointer zoom/shared drag pan; fit/zoom shortcuts; Undo/Redo; project persistence; async export; offline HTML help; and manual async GitHub update checking already exist in Mac R10.

Mac also supplies the existing `magneticLassoPath` and GrabCut helpers. Neither inspected toolbar exposes the GrabCut box/hint workflow. Do not describe these shared helpers as newly written Windows segmentation or promise a visible tool that is absent.

## Windows Additions and Changes

| # | Area | Windows v2.1.1 | Mac R10 / porting action |
| --- | --- | --- | --- |
| 1 | Independent applied objects | UUID, name, physical eye, operation, visibility, anchors, magnetic flags and path for each polygon; brush deltas are raster objects. | Flattened per-eye masks and merged outlines. Port `SelectionObject` and `SelectionState`. |
| 2 | Selections panel | Dock lists eye/mode; row selection highlights the object's path. | No object dock or selected-object ID. |
| 3 | Object management | Rename, eye-icon visibility, delete, Undo/Redo. | No individual applied-object management. |
| 4 | Reopen/edit applied polygons | Edit points restores anchors; Apply replaces the same ID without creating a duplicate. | Only pending outline anchors remain editable. Merged paths do not retain individual original vertices. |
| 5 | Ordered frame composition | Start with frame opening; apply visible objects top to bottom. Subtract cuts inside the frame; later Add restores pixels. | Frame remains open regardless of reveal-mask subtraction. Port `openingMask`, not just a list. |
| 6 | Move Up/Down | Changes composition order; order persists and is undoable. | No reorderable object list. |
| 7 | Switch Add/Subtract | Selected-object toolbar button preserves geometry, ID, visibility and order. | Operation control changes the next pending lasso only. |
| 8 | Insert anchor | Shift-click near an edge, including closing/magnetic edges; split/retrace paths. | No arbitrary lasso insertion gesture. |
| 9 | Remove anchor | Ctrl-click point; reconnect neighbors; preserve closure and at least three closed anchors. | Delete/Backspace removes only the last point and reopens. Validate Mac modifier delivery. |
| 10 | Close/apply keys | Enter/keypad Enter/C closes; Ctrl+Enter closes and applies. Existing Escape behavior retained. | Enter closes; no C or dedicated Apply shortcut. Adapt Command equivalents. |
| 11 | Smart lasso dropdown | Exposes mode 8 as Smart polygon lasso; reopened smart polygons retain magnetic editing. | Magnetic code exists, but mode 8 is not in the tool dropdown. |
| 12 | Paired lasso | Connected-circles toggle creates an estimated opposite-eye object on new Apply. Either eye, Swap, Add/Subtract and initial paired Undo work. Objects are independent afterward. | No opposite-eye copy. Port `alignStereoPolygon`; do not pair again on editing an existing object. |
| 13 | Arrow scrolling | Shared pan moves with arrows; Shift triples the screen-space step. Coordinates and unfinished outlines are retained. | Shared drag pan exists; arrow scrolling absent. |
| 14 | Preview Mask toggle | Default enabled; toolbar/View action; off restores source-image editing/tint. | No explicit preview toggle; frame opacity and cutout editing have different paint paths. |
| 15 | Clean Preview | F8/toolbar/View hide points, outlines, hints and cursor; retain work and navigation. Leave clean mode to draw. | No clean-view toggle. |
| 16 | Match preview export | New background mode 4, default for new images; uses mask color/opacity and optional frame feather. Existing saved choices retained. | Modes 0-3 only; cutout export ignores overlay color/opacity. |
| 17 | Preview/export parity | Same `exportEye`, `maskedPreviewEye`, `cutoutEye` composition. White previews white, Black previews black. Transparent SBS view uses non-exported checkerboard. | Preview appearance can differ from exported background. Port compositor and export together. |
| 18 | Per-eye anaglyph | Uses composed export eyes, including objects, holes, selected mode and Swap channel order. | Anaglyph receives frame points, not per-eye foreground reveal objects. |
| 19 | Composed-image cache | Key covers images, frame geometry, selection state and appearance; zoom/pan reuse composed eyes. | No equivalent export-eye cache. Test invalidation carefully. |
| 20 | Unified color picker target | Mask color and Pick mask color both edit Settings' mask color. Picker samples source photo without changing export mode. | Eyedropper changes custom cutout color and switches export mode. Intentional behavior change. |
| 21 | Appearance synchronization | `maskAppearanceChanged` refreshes toolbar swatch/opacity from Settings/project load. Background Color renamed Padding/gap color; custom cutout color explicitly separate. | No shared appearance signal/swatch synchronization; ambiguous color targets/labels. |
| 22 | Settings cancellation | Dialog edits a settings copy; apply only on acceptance. | Dialog edits the original settings object directly. |
| 23 | AutoMask algorithm menu | QTAGENT Rect Decompose / SGBM Contour / Legacy Smart Mask choices. | Native AutoMask already exists but without this three-way selector. Add alternatives, do not discard Mac's native functionality. |
| 24 | Freehand routing regression | Enabling Freehand releases lasso mouse routing so initial frame drawing works after switching tools. | Freehand exists, but its setter does not release lasso brush mode the same way. Port the fix, not a supposedly new tool. |
| 25 | Original inspiration credits | About/welcome/README/credits name Bernd Paksa and link to original Stereomasken website; About link opens externally. | R10 names Subbiah/Theodore but not Bernd in About. Add original creative attribution without replacing modern authorship. |
| 26 | Documentation and regression tests | Current ordered-object/shortcut/export guide; expanded geometry, pairing, cache, pixel and UI tests. | R10 documentation/tests cover earlier flattened-mask behavior. Port assertions with features. |
| 27 | Whole-polygon drag (new in v2.1.1) | Selected visible polygon translates as a whole via four-arrow toolbar toggle. Shift-drag locks horizontal motion; Escape cancels, boundaries clamp the whole shape, one drag is one undo step. Paired partner stays independent. | No applied-object translation. Port the move-mode mouse routing and explicit before/after undo snapshot; do not retrace magnetic edges during a rigid translation. |

## Source Map

| Files | What to port |
| --- | --- |
| `stereoviewwidget.h/.cpp` | Object/state types, `SelectionObjectsCommand`, brush raster deltas, management APIs, object JSON/migration, `openingMask`, `exportEye`/cache, preview toggles, keyboard pan, point gestures, pairing in `applyLasso`, Freehand routing. |
| `smartselection.h/.cpp` | New `StereoPolygonMatch` and `alignStereoPolygon`; retain the existing Mac magnetic/GrabCut functions. |
| `mainwindow.cpp` | Dock/actions, smart dropdown, paired icon, shortcuts, preview modes, mode 4 export, shared color signals/swatch, Settings labels/copy, credits link. |
| `stereoimageprocessor.h/.cpp` | `createAnaglyphFromEyes` for composited physical-eye images. Legacy `createAnaglyph` remains. |
| `tests/smartselection_test.cpp` | Objects, anchors, JSON/migration, frame holes/order, pairing, gestures/navigation, clean mode, export pixel parity and cache invalidation. |
| `tests/help_test.cpp` | Actual UI wiring, list retention, Settings/toolbar synchronization, picker target, bundled guide and clickable credit. |
| `README.md`, `docs/user-guide.html`, `CREDITS.md` | Adapt workflow and keys; preserve Mac-specific build/launch instructions. |

## JSON and Exchange Compatibility

Windows saves `brushVersion: 5`, with `selectionObjects.version: 1`; Mac R10 saves brush version 4. The ordered `objects` array has `id`, `name`, `right`, `subtract`, `visible`, `anchors` (`[x,y]` source-pixel coordinates), `magnetic`, `path` (encoded subpaths), and optional base64 `rasterPNG`. Magnetic flags cover non-closing anchor segments; inspect the code for closing-edge handling rather than inventing another schema.

The container stores `baseLeftPNG`, `baseRightPNG`, `outlineLeft`, `outlineRight`. Flattened `revealLeftPNG`/`revealRightPNG` and lasso outlines remain, but are **not** a lossless replacement for the object stack. Visibility/order and frame holes require the new compositor.

Export background IDs remain 0 transparent, 1 black, 2 white, 3 custom solid; 4 is Match preview. Existing color/opacity/padding fields remain. `bgColor` means padding/gap color, not mask color. Keep explicit saved old modes; only new images default to mode 4.

Windows imports old flattened masks as raster objects named Imported selection; original vertices cannot be recovered. These objects can be hidden/deleted, but do not have reconstructed editable anchors.

**Unmodified Mac R10 cannot losslessly round-trip these Windows projects.** Its loader clamps mode 4 to mode 3 and ignores independent objects. It cannot preserve frame holes/object metadata, and saving back can overwrite the richer project. Exchange copies until support is ported, or add a newer-format warning before saving. Windows migration is tested; Mac round-trip needs new tests.

## Paired Alignment Algorithm

`alignStereoPolygon` uses source-resolution grayscale patches with 12-pixel radius (25x25 pixels). Horizontal search radius is `min(1024, max(48, width/4))`, centered on frame-disparity prediction. OpenCV normalized template correlation rejects low-variation patches (standard deviation below 5), requires best score at least 0.65 and a 0.04 margin over alternatives outside a small neighborhood, and checks reverse matches within 2 pixels.

Reliable shifts apply per anchor. Unmatched anchors use median reliable shift, otherwise median frame disparity with the correct directional sign. Y stays unchanged and coordinates are bounded to the opposite image. Status reports matched/total anchors. Smart copied segments retrace against the opposite image.

This is a starting contour, not calibrated rectification, occlusion recovery or an enduring stereo constraint. Copies have different IDs and no link after Apply; editing either must never recreate or modify the other.

## Recommended Port Order

1. Object/state model, undo, JSON, old-mask migration and round-trip tests.
2. Ordered frame compositor and shared preview/export paths; pixel tests for holes, opacity, alpha and all backgrounds.
3. Selections dock and edit/visibility/rename/delete/reorder/operation actions; pending-edit guards and selection retention.
4. Smart tool exposure, point insertion/removal, close/apply keys, Freehand routing fix.
5. Arrow navigation, Preview Mask and Clean Preview; preserve work under Swap/anaglyph.
6. Paired matching, opposite-eye smart paths, fallback and independent editing tests.
7. Color synchronization, Settings, cache invalidation, help/credits and UI tests.
8. Actual Apple Silicon build, bundle repair/signing, relocation to Applications and Finder launch before a Mac binary release.

## Platform Pieces to Preserve

Keep Mac's `BUILD_BRUSH_VERSION.command`, `scripts/repair_macos_bundle.py`, Homebrew/Mach-O dependency checks, `.app` packaging and signing process. Windows uses Qt/MinGW/vcpkg DLL/plugin and `.exe`/ZIP deployment. Do not replace Mac packaging with Windows files.

Retain Mac's update endpoint/URL allowlist (`TedInVR/StereoMask-Mac`, `main`) and revision labels. Windows uses `SRathinaGiri/StereoMask`, `master`. Both already implement async numeric-version checks, timeout/errors and user-confirmed browser opening.

Qt maps Control to Command for many macOS shortcuts. Test actual Command-click versus macOS Control-click secondary-click behavior for point deletion, and Return/keypad Enter. F8 may need Fn. Windows synthetic tests do not verify Mac input or launch behavior.

## Not Implemented on Windows Either

Canvas click-to-select applied objects, groups, per-object feathering and complete calibrated multi-region QTAGENT masking are not implemented. GrabCut helpers are not exposed in the current toolbar. Paired alignment is not guaranteed correct. No Mac build/launch certification was performed for these Windows changes.
