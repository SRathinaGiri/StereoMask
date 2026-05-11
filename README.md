# StereoMask v1.1

A precision masking tool for side-by-side (SBS) stereo images, developed with **GEMINI CLI** and Qt6.

## Features

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
cmake -B build -G Ninja
cmake --build build
```
