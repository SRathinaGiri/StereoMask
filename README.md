# StereoMask v1.2

A precision masking tool for side-by-side (SBS) stereo images, developed with **GEMINI CLI** and Qt6.

## New in v1.2

- **Refined Mask Feathering:** Soften mask edges with pixel-perfect precision (0-200px) without affecting image borders.
- **Snapping Toggle:** Quickly toggle point snapping on/off via the toolbar or shortcut (**`S`**).
- **Export Progress Tracking:** Real-time feedback in the status bar during image export.
- **Smart Folder Persistence:** Automatically remembers the last used directory for all file operations.
- **Optimized Rendering:** Improved masking engine with better memory safety and performance capping.

## Core Features

- **Modern Emoji Toolbar:** Intuitive controls for file operations, editing, and view modes.
- **Precision Masking:** Add, move, and multi-select points to define custom masks for 3D images.
- **Masked Anaglyph Preview:** Real-time 3D preview of your mask using Red/Cyan channels, supporting both parallel and cross-eye sources.
- **Project Persistence (.msk):** Save your work as mask projects that store point data and project-specific settings.
- **Interleaving Space:** Configure gaps between eye images in exported masks, filled with customizable background colors.
- **AutoSave:** Optional automatic saving of project changes.
- **Recent Files:** Quick access to the last 5 worked-on projects.
- **High-DPI Support:** Fully optimized for 4K and multi-monitor setups.

## Author

**S. Rathinagiri**

## License

This project is licensed under the [MIT License](LICENSE).

## Requirements

- Qt 6.x
- CMake 3.16+
- C++17 Compatible Compiler

## Building

```powershell
$env:PATH = "D:\Qt\6.11.0\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;" + $env:PATH
cmake -B build -S . -G "MinGW Makefiles"
cmake --build build
```
