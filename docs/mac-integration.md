# Mac Feature Integration Into Windows v2.1.0

## Source Comparison

The Windows checkout matched origin/master at 9da98e0 (v2.0.0) when checked on 2026-10-11. The local unpublished three-algorithm AutoMask selector and QTAGENT decomposition changes were retained.

Imported source: https://github.com/TedInVR/StereoMask-Mac at commit 94c8ff8. Its source is based on Windows v2.0.0 and contains R10 foreground-editing and documentation changes.

| Mac addition | Windows integration |
| --- | --- |
| Select/Erase brush and per-eye alpha masks | Imported, including stroke undo/redo |
| Polygonal lasso and multiple Add/Subtract areas | Imported, including outline persistence |
| Transparent/custom export and eyedropper | Imported with PNG alpha protection; the current Windows picker now targets mask color, not cutout color |
| Space pan, pointer zoom and fit | Imported with Ctrl shortcuts |
| Offline guide | Embedded and adapted for Windows |
| Manual update checker | Adapted to SRathinaGiri/StereoMask, master branch |
| Smart edge and magnetic-lasso helpers | Imported; Smart polygon lasso is now exposed in Windows, while the GrabCut box/hint workflow remains unexposed |
| Homebrew bundle repair and signing | Mac-only; Windows retains MinGW/vcpkg deployment |

## Verification

The port includes the Mac integration checks for native segmentation, per-eye selection, Add/Subtract, Undo/Redo, project restoration, transparent export, navigation, help resources and update checking. Windows-specific regression checks cover freehand after switching tools and retention of the AutoMask algorithm menu.

Synthetic integration checks do not establish AutoMask accuracy on all photographs. The existing editable-point representation still represents one frame boundary; this port does not claim complete independent multi-region QTAGENT occlusion polygons or calibrated rectification.

## Subsequent Windows Changes and Mac Handoff

The current feature inventory and controls are in [README](../README.md) and the [User Guide](user-guide.html). The complete source-backed difference list against fetched Mac R10 is in [Windows/Mac Comparison and Porting Checklist](windows-mac-comparison.md), including object state, ordering/frame holes, paired alignment, gestures, preview/export parity, schema compatibility and Mac-specific cautions. This original-import record is not a claim that those later Windows changes already exist in Mac.

## Update Metadata

Windows update metadata is in updates.json. Keep appversion.h, CMake's project version and the metadata version consistent when publishing. The checker only opens the trusted Windows GitHub page after a user action; it never installs a download.
