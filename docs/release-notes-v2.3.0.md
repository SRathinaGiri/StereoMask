# StereoMask v2.3.0

## Text Effects and Stereo Tilt

- Flat, Embossed and Engraved effects on Colored Letters or a Cutout Plate with real transparent glyph openings.
- Editable relief depth, foreground occlusion, background color/opacity, and persistent project settings with Undo/Redo.
- X is top/bottom pitch, Y is left/right yaw, and Z is roll. Both eyes view one consistently rotated plane; X/Y produce a horizontal depth gradient while corresponding vertical coordinates remain aligned.
- Fixed relief text disappearing at steep angles on large images. Regression fixtures include 32, 33, 34, 45, 60 and 80 degrees.
- Applying Text & Bubbles now shows left-eye rendering, right-eye rendering, preview and saving stages. Applied text layers render in workers and are cached for repaint/export.

## Editing and Feedback

- Right-click frame construction preserves corner click order. Near-edge insertion is retained, and closing-edge insertion preserves the first point and curved-edge geometry.
- In frame-point and lasso editing, wheel scrolls vertically, Shift+wheel horizontally, and Ctrl+wheel zooms. Middle-button drag remains aligned free pan.
- Opaque, frameless processing messages and status-bar stages for file opening, selections, AutoMask, edge refinement, text/bubbles, saving and export. Single-eye messages do not claim paired creation; no guessed percentages.
- Image decoding, paired lasso matching/tracing, color selection and selection-mask composition use worker snapshots. Final composition and some saving remain synchronous with visible stage feedback.
- About, splash, window title, update checks and CMake project/bundle metadata now share the application version in `appversion.h`.

## Compatibility and Handoff

Existing projects retain flat/unrotated text by default. New text fields are optional extensions under `stereoTextVersion: 1`; older Windows/Mac builds may ignore or discard them when saving. Exchange copies rather than overwriting a project in an older app.

See the [Windows v2.3.0 to Mac R18 Handoff](https://github.com/SRathinaGiri/StereoMask/blob/v2.3.0/docs/windows-to-mac-handoff.md) for verified Mac baseline, source map, projection/JSON details, suggested port order and validation checklist. The release includes a separate Mac Handoff ZIP with this documentation and the Windows source delta. No Mac binary was built or certified.

Four automated CTest suites cover selection/editing, help/UI/progress, version checking and creative rendering, including persistence, Undo/Redo, alpha holes, tilt, preview/export pixels and visible text-apply feedback.

Original creative inspiration remains credited to [Bernd Paksa's Stereomasken](https://www.stereomasken.de/index_en.htm), with the Windows and Mac collaborators credited in About and CREDITS.md.
