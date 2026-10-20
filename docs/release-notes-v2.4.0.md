# StereoMask v2.4.0

## Compact Toolbox Drawers

- A narrow left-side icon rail replaces the stacked top toolbars, returning space to the stereo canvas.
- File, View, Create Frame, Edit Frame, Out-of-Frame and Help use 150-logical-pixel drawers. History uses 240 pixels for its object list and seven editing buttons.
- Only one docked drawer opens at a time. Click its icon again to close it. Drag a title bar to float a drawer or dock it on the other side; floating drawers stay independent.
- Layout and open drawers are remembered. View > Layout offers Close all drawers and Reset toolbox layout.
- Borderless, left-aligned command buttons retain hover, pressed, checked and keyboard-focus states. Long labels wrap; compact numeric fields remain editable by keyboard or wheel.
- Dropdowns and Open Recent fill the usable drawer width, including when a scrollbar is visible.
- Stereo Window and Text & Bubbles open their dialogs directly. Help is last, after Guided Process.

## Quick Access Favorites

- The star icon opens a 150-pixel Quick Access drawer.
- Right-click a supported command button to add or remove it. Favorite context menus also offer Move up/Move down.
- The manager provides command search, add/remove, drag reordering and up/down controls. Save commits the changes; Cancel leaves the current list unchanged.
- Favorites use the original QAction handlers, enabled/checked states and dialogs, including Recent Files. Selection-tool and AutoMask choices are available in the manager.
- Favorites and order are saved in local layout.ini, independently of projects. Existing menus and shortcuts remain available.

## Safety Fixes Reviewed From Mac

Reviewed Mac R19/R20 at commit 425f403755e8085973a58ef95052188ceef60017 on 2026-10-20. Credit to Theodore Whitten's reciprocal integration and regression work.

- Processing panels now use normal heap-allocated Qt ownership and QPointer guards. Closing the host before the progress owner no longer causes invalid deletion or access through stale widget pointers.
- Initial processing feedback delivers paint/layout work without running arbitrary queued commands. Worker waits retain the existing synchronous command/Undo contract.
- Non-object entries in a project's stereoText array are ignored instead of producing default text objects.
- Old text-cache keys and images are cleared when loading, replacing or closing a document.

Windows intentionally retains eight-neighbor Fuzzy Select connectivity, including diagonals. Mac's optional support/payment links and its platform-specific navigation defaults are not imported.

## Compatibility and Verification

No project schema change. Existing selections, text effects, frames and stereo offsets remain compatible with v2.3.0. Quick Access and drawer layout are local preferences, not project objects. Older releases may still discard unfamiliar project fields; exchange copies when necessary.

Five automated suites cover selections, creative rendering, help/UI, update checking and Quick Access. Added regressions cover parent-before-progress destruction, cursor restoration, queued-command isolation, malformed text entries, image replacement, unchanged diagonal connectivity, full-width dropdowns, favorites state/ordering/persistence, and manager Save/Cancel.

See [Windows-to-Mac Handoff](windows-to-mac-handoff.md) for the current comparison and porting checklist. No Mac binary was built or certified. Original creative inspiration remains credited to [Bernd Paksa's Stereomasken](https://www.stereomasken.de/index_en.htm); existing collaborator credits and MIT license are retained.
