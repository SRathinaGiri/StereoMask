# StereoMask v2.2.0

## Mac R18 workflow and creative tools on Windows

- Compact, independently collapsible workflow sections and a complete Main Menu.
- History naming and ordered object controls, Guided Process, Close Project, Open Recent, remembered folders, maximized launch, and editable Windows keyboard shortcuts.
- Stereo Window depth adjustment with the same side-by-side view as the main canvas, plus optional anaglyph, precise pixels, Push Back/Pull Forward, zoom/pan, project persistence, and Undo/Redo.
- Nearest-edge frame-point insertion, visible frame editing controls, editable rectangle/square/circle/oval creation, flat/beveled/decorative picture frames, stereo-aware drop shadows, and persistent 3D text with speech/thought bubbles.

## New Windows selection and alignment work

- Fuzzy Select creates a connected selection from the clicked color.
- Select by Color Threshold selects matching pixels throughout the clicked eye, including disconnected regions.
- A 0–255 Color Tolerance control drives both tools. Each click creates an Add/Subtract raster object in History and supports Undo/Redo, visibility, reordering, project persistence, preview, and export.
- Both color tools retain a visible crosshair over either eye; the normal arrow is restored outside the image and after temporary picker/navigation states.
- Left, Right, Top, and Bottom alignment now use the corresponding extreme selected point. Align Depth uses the median selected disparity, averaging the two middle values for an even selection.

## Compatibility and verification

The creative settings use the project extensions shared with Mac R18. Color selections use the existing `selectionObjects.version: 1` raster-object representation, so no additional project schema is required. Older releases may discard unfamiliar creative fields when saving; exchange project copies.

Four automated CTest suites cover selection/editing, UI/help wiring, update checking, creative rendering, color selection, alignment, cursor visibility, project restoration, Undo/Redo, and export behavior. The packaged Windows application also receives a startup smoke test.

For reciprocal Mac work, see [Windows v2.2.0 to Mac R18 Handoff](windows-to-mac-handoff.md).
