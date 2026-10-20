# StereoMask v2.5.0

## Alignment Grids

- View > Show Grid (Ctrl+G) overlays matching source-image grids in both eyes. Grid Settings is available from View and its drawer; both commands can be added to Quick Access.
- Independent horizontal/vertical line counts from 0 to 100, single- or two-color contrast and adjustable opacity. Two-color lines alternate along their length.
- Grids follow pan/zoom, are clipped to the eye images and work over anaglyph. They are hidden in Clean Preview and never become export pixels or document Undo commands.
- Grid preferences are remembered locally in layout.ini, not inside .msk projects. Apply/Cancel and Restore Defaults are available.

## Three-Band Outline Frames

- Edit Frame > Outline Frame restores a framing idea from Bernd Paksa's original Stereomasken, alongside the existing picture-border and shadow tools.
- After creating at least three frame points, enable Apply outline frame. A source-pixel offset leaves a gap outside the main frame contour.
- Inner, Middle and Outer bands have independent widths and colors. Zero width omits a band; Use mask color keeps a band synchronized with the current mask color.
- The outline follows curves and each eye's disparity, stays behind preserved foreground objects, and is clipped to the existing canvas. It does not add margins or expand the image.
- Display in editor is independent of output. Clean Preview and export include applied outlines even when normal editor display is off.
- Changes are undoable and saved in the project. Apply/Cancel and Restore Defaults are available; existing borders and shadows remain independent.
- Per-eye outline caches avoid recomputing the bands during ordinary pan/zoom and invalidate on geometry, foreground, color or document changes.

## Compatibility and Verification

Outline settings are optional fields under pictureFrame/outline. Older projects default to outlines disabled. Older readers may ignore or discard these fields when saving; exchange copies instead of overwriting a project with an older app. No existing text/selection schema has changed. Grid state is a workspace preference only.

All five CTest suites pass. New coverage includes grid pixels/clipping/two-color contrast, editor-only rendering, Clean Preview, local preference restoration, dialog Apply/Cancel, Quick Access registration, outline band/gap geometry, per-eye disparity, curves, foreground occlusion, zero-width bands, mask-color/cache changes, Undo/Redo and project reload. Existing export pixel-parity and version/help checks remain.

The Windows package includes the generated 2.5.0 User Guide, runtime DLLs/plugins and dependency notices. A separate Mac Handoff ZIP contains the updated comparison, source references, changed Windows sources and the cumulative Windows v2.3.0-to-v2.5.0 patch. It is not a Mac binary. See [Windows-to-Mac Handoff](windows-to-mac-handoff.md).

Original creative inspiration and authorship remain credited to [Bernd Paksa's Stereomasken](https://www.stereomasken.de/index_en.htm), with Subbiah Rathinagiri and Theodore Whitten's existing Windows/Mac collaboration credits and MIT licensing retained.
