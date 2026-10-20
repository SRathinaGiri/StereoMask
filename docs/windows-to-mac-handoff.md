# Windows v2.5.0 to Mac R20 Handoff

## Verified Baselines

Compared on **2026-10-20** against fetched Mac `origin/main`: **R20 / 2.3.1**, commit [425f403755e8085973a58ef95052188ceef60017](https://github.com/TedInVR/StereoMask-Mac/commit/425f403755e8085973a58ef95052188ceef60017). R19 is [4003062b4e164d800f00aba4ba9d74f40f7d3f07](https://github.com/TedInVR/StereoMask-Mac/commit/4003062b4e164d800f00aba4ba9d74f40f7d3f07). The Mac checkout was not modified; only remote references were fetched. No Mac build, signing or Finder-launch validation was performed.

Windows source: [v2.5.0](https://github.com/SRathinaGiri/StereoMask/tree/v2.5.0). Its release includes a Windows package and a Mac Handoff ZIP with this guide, release notes, changed source files, and the Windows v2.3.0-to-v2.5.0 patch. This is a review aid, not a patch to apply blindly over Mac-specific files. A fresh fetch on 2026-10-20 confirmed that Mac R20 remains the tip.

The [previous v2.3.0 handoff](https://github.com/SRathinaGiri/StereoMask/blob/v2.3.0/docs/windows-to-mac-handoff.md) remains historical. Mac R19 has now integrated its principal Windows color-selection, alignment, frame-ordering, text-relief/tilt, processing-feedback and version-constant additions. Do not describe those as still missing from Mac R20.

## Mac Changes Reviewed for Windows

| Mac change | Windows v2.4.0 decision |
| --- | --- |
| Processing-panel lifetime: heap-owned widgets and QPointer guards; no broad event processing during construction | Adopted, with host-before-progress destruction, cursor restoration and queued-command regression coverage. |
| Ignore non-object stereoText entries | Adopted; malformed entries no longer create default text objects. |
| Clear text-cache keys/images on load, image replacement and Close | Adopted. Existing eye-specific cache invalidation remains. |
| Fuzzy selection uses four shared-edge neighbors | Not adopted. Windows retains eight-neighbor connectivity, including diagonals, with an explicit regression. New clicks can therefore produce different regions; saved raster objects are unchanged. |
| Optional Ted in VR support panel with Ko-fi, PayPal and Patreon | Not imported. These are Ted's Mac/creative-work accounts, not shared Windows payment destinations. |
| Mac wheel-panning opt-in, Command/Option keys, anaglyph Stereo Window default | Preserved as Mac choices; Windows keeps its existing wheel-pan defaults, Ctrl/Alt keys and optional-anaglyph SBS dialog. |

The main shared editing and project features remain the same: ordered Add/Subtract objects and frame holes; paired and smart lassos; per-eye brush/color selections; editable shapes/borders/shadows; relief and cutout text; stereo tilt and offset; preview/export composition; and worker-backed processing feedback.

## New Windows Features for Optional Mac Adoption

### 1. Docked toolbox drawers

A permanent narrow icon rail opens actual QDockWidget drawers instead of stacked/collapsible toolbar rows. File, View, Create Frame, Edit Frame, Out-of-Frame and Help use 150 logical pixels; History uses 240. One docked drawer is open at a time, while independently floating drawers stay visible. Drag title bars to float or change sides; close/click the icon to reclaim the canvas.

Each command drawer contains vertically grouped original toolbars in a scroll area. Labels wrap, buttons remain left aligned and borderless, and checked/hover/pressed/focus states remain visible. Dropdowns, labels and Open Recent track the actual viewport width when scrollbars appear. Numeric fields stay compact. Stereo Window and Text & Bubbles open directly; Help is last after Guided Process.

Preferences use an explicitly located layout.ini beside settings.json. saveState/restoreState remembers dock placement, floating state and the open drawer. Close all drawers and Reset toolbox layout retain the original menus and shortcuts. A reset affects layout, not favorites or project content. Mac should choose widths after native font/DPI testing rather than copying pixel values uncritically.

### 2. Quick Access favorites

quickaccess.h/.cpp implements a compact drawer opened by the star icon. It registers original QActions with stable command IDs and categories. Favorites share handlers and enabled/checked states instead of cloning commands or copying document state. Menu favorites such as Open Recent retain their menus.

Right-click a supported command button to add/remove it. In Quick Access, the same context menu also moves a favorite up/down. The manager supplies searchable available commands and an ordered chosen list, with Add, Remove, native Qt drag reordering and up/down buttons. Save commits the draft list; Cancel discards manager changes. Empty favorites do not add document objects or duplicate shortcuts.

Selection-tool and AutoMask menu choices are registered too. AutoMask's widget button has a QAction adapter using the same handler and availability predicate. Align/Scale/Move categories disambiguate identically named actions. IDs are independent of translated labels and dynamic Undo text; keep them stable in future releases. Unknown/duplicate IDs are ignored during restoration.

quickAccess/commands in layout.ini stores the ordered IDs. Favorites are per-user application preferences, not .msk fields. Native Mac menus, shortcuts, updater URLs, signing and deployment should remain Mac-specific. Confirm right-click/Control-click behavior separately from polygon anchor gestures on the canvas.

### 3. Shared application/help version (v2.4.1)

appversion.h supplies the runtime version and CMake project metadata. The User Guide source now has a version placeholder rather than a hardcoded number. CMake generates build/generated/docs/user-guide.html and configures resources.qrc.in into the build directory, retaining the existing resource aliases. Embed and package the generated guide. Changes to either the header or source guide trigger regeneration. The app opens help under help/<version>/user-guide.html instead of the shared help/r10 URL.

MainWindow sets its versioned title from the constant. Tests inspect the actual splash label, title, About, embedded HTML and cache URL. Mac should preserve its own revision label and platform-specific cache location while using its appversion.h for numeric version text. Publication metadata such as historical release notes and updater URLs remains separate from runtime version displays.

### 4. Alignment grid (v2.5.0)

GridSettings and StereoViewWidget::paintAlignmentGrid provide source-image-aligned grids. Independent horizontal/vertical line counts are bounded to 0-100; zero omits that axis. Lines are interior divisions at i/(count+1). Cosmetic screen-space strokes remain readable while the image-coordinate origin and spacing follow shared pan/zoom. Optional second-color dashed segments provide contrast over the first solid color. Opacity is 0-100.

Render grids after the eye image but before editing handles, clipped to the image/viewport; do not put them in exportEye or the composed anaglyph pixels. Anaglyph receives the same editor overlay once. Clean Preview skips the grid. Grid changes are not document Undo commands and do not change .msk data. workspace/grid/* keys in layout.ini store enabled, twoColors, horizontal, vertical, opacity and the first/second colors. Mac may adapt the shortcut and preferences location; Windows uses Ctrl+G.

### 5. Three-band outline frames (v2.5.0)

PictureFrameSettings now includes optional outline fields. The outline is independent of the existing border/shadow enable flags but uses the same undoable frame-settings command. Offset is an exterior gap from the base frame contour; inner/middle/outer widths are successive exterior bands. Each offset/width is bounded to 0-1000 source pixels; zero-width bands are skipped. The renderer uses each eye's curved/disparity-aware path and removes the ordered opening mask from the outline, allowing preserved foreground to cover it. It does not expand the source canvas or export padding.

Persist under pictureFrame/outline: enabled (default false), displayInEditor (true), offset (20), and inner/middle/outer objects containing width (5/10/5), color (ARGB), and useMaskColor (false). Use mask color follows the mask QColor, not the custom cutout color or mask-opacity setting. Invalid JSON values are bounded/defaulted. Keep backups when round-tripping through older readers, which may discard these fields.

exportEye gains an optional editor flag, default false for output. Normal editing respects displayInEditor; Clean Preview and export always include applied outlines. Apply/Cancel and Restore Defaults are exposed by workspacecontrols.cpp. Per-eye outline caches key image identity, geometry/disparities, foreground revision/masks, band settings and mask color. Clear caches on document changes; zoom/pan should reuse them. Foreground, color, geometry and Undo changes must invalidate correctly.

## Source Map

| File | Responsibility |
| --- | --- |
| mainwindow.cpp/.h | Icon rail, drawers, accordion/floating behavior, layout persistence, viewport-width control, command catalog/context menus and existing handler integration |
| quickaccess.cpp/.h | Favorites, command registration, context menus, searchable draft manager, ordering and QSettings persistence |
| CMakeLists.txt | Compile Quick Access and its focused test target |
| workspacecontrols.cpp, mainwindow.h/.cpp | Grid/outline dialogs, workspace persistence, View/Edit Frame actions and Quick Access registration |
| stereoviewwidget.h/.cpp | Grid paint routing, outline settings/JSON/rendering/cache, editor-vs-output composition and existing frame Undo integration |
| operationprogress.h | Reviewed lifetime/reentry fix; Mac R19 already has the corresponding implementation |
| stereoviewwidget.cpp | Reviewed malformed-text and cache-cleanup fixes; preserve Windows diagonal connectivity |
| appversion.h, updates.json | Windows runtime version and publication metadata; retain Mac's own destinations |
| resources.qrc.in, main.cpp, CMakeLists.txt | Generated help/resource version and centralized window-title setup |
| tests/help_test.cpp | Real drawer/command wiring, layout round-trip, widths, live favorite states, processing lifetime |
| tests/quickaccess_test.cpp | Duplicate/unknown IDs, original-action execution/state, ordering, context removal, manager Save/Cancel and persistence |
| tests/creative_test.cpp | Malformed project entries, replacement-image text state and diagonal Fuzzy convention |
| README.md, docs/user-guide.html | Current Windows workflow; adapt platform wording |

## Validation and Compatibility

1. Verify dock accordion behavior, independent floating panels, left/right docking, collapse/reset and layout restoration. Closing the app must not overwrite the saved open-drawer state with shutdown visibility signals.
2. Check normal and high DPI, small windows, full-width Recent Files/dropdowns, scrollbars, native menus and keyboard access. Windows test screenshots cover 1280x900 and 900x650 logical windows.
3. Exercise favorites through original buttons, source menus, the manager and right-click menus. Test shared checked/disabled states, one execution per activation, dynamic Undo labels and popup menus.
4. Test Add/Remove/drag/up/down, duplicate prevention, invalid saved IDs, Save/Cancel and reopen. Favorites/layout must not alter project Undo or require a new project schema.
5. Confirm no host/progress double deletion, stale widget use or leaked wait cursor. Initial painting must not dispatch arbitrary queued application commands.
6. Preserve Mac's already adopted color/text/progress features and its platform-specific navigation defaults. Mac physical keys, fonts, Apple Silicon packaging, signing, relocation and Finder launch need native verification.

Five Windows CTest suites cover these areas. Synthetic tests do not certify segmentation quality or native Mac deployment. Bernd Paksa/Stereomasken, Subbiah Rathinagiri and Theodore Whitten retain their existing creative/development credits; MIT licensing and dependency notices remain unchanged.

For the v2.5.0 additions, also verify grid clipping and aligned pan/zoom in both eyes, one/two-color contrast, no exported grid pixels, Clean Preview, preference restoration and dialog cancellation. Verify outline gaps/bands, zero widths, curves, disparity, foreground cutouts, mask-color changes, cache invalidation, Undo/Redo and project reload. Older projects must keep outlines disabled unless explicitly enabled. Run native Mac UI/font/key/deployment checks separately.
