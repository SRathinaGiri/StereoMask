# StereoMask (Qt 6)

A modern, 64-bit recreation of the StereoMasken application.

## Features
- Load side-by-side stereo images.
- Interactive 3D mask adjustment (Width, Height, Position, Depth/Disparity).
- Multiple preview modes:
  - Side-by-Side (SBS)
  - Anaglyph (Red/Cyan)
- Black-out masking: Areas outside the frame are shaded or blacked out to preserve the 3D window effect.
- Export results as SBS or Anaglyph images.

## Building
This project uses CMake and Qt 6.

1. Open the project in Qt Creator or use the command line:
   ```powershell
   mkdir build
   cd build
   cmake -DCMAKE_PREFIX_PATH=D:/QT/6.11.0/mingw_64 ..
   mingw32-make
   ```

2. Run the executable:
   ```powershell
   ./StereoMask.exe
   ```

## Requirements
- Qt 6.x
- C++17 compatible compiler (MinGW/GCC recommended)
