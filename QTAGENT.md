# AGENT.md — Stereo Masking Polygon Generator
> This is an algorithm design specification, not a statement that every stage is implemented. For the current shipped/local feature behavior and limitations, see README.md and docs/user-guide.html. Windows currently offers three AutoMask starting-frame algorithms; complete calibrated independent multi-region masking remains unimplemented.
## Build target: Qt 6 · MinGW (Windows) · C++17

---

## Purpose

This agent computes a **masking polygon** for a stereo image pair (left eye / right eye side-by-side). The polygon covers the region in each image where features have no correspondence in the other image — caused by depth disparity and occlusion near object boundaries. The result is a pair of `QPolygon` objects (one per eye) that can be painted directly onto a `QWidget` or used as clip regions in a `QPainter`.

---

## Toolchain Constraints (MinGW-specific — read first)

MinGW is a GCC-based toolchain for Windows. Several constraints differ from MSVC builds and must be respected throughout the codebase.

### Compiler
```
Qt 6.x ships MinGW 11.2 (64-bit) in the Qt Online Installer.
Use exactly that bundled MinGW — do NOT mix with a separately installed MSYS2/MinGW.
Mismatched runtime DLLs (libgcc_s_seh-1.dll, libstdc++-6.dll, libwinpthread-1.dll)
will produce silent crashes or "procedure entry point not found" errors at startup.
```

Compiler flags for `CMakeLists.txt`:
```cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

target_compile_options(StereoMasking PRIVATE
    -O2 -march=native -funroll-loops
    -Wall -Wextra
)
```

### OpenCV + MinGW
OpenCV's official pre-built Windows binaries are compiled with MSVC. They **will not link** with MinGW. You have two options:

| Option | Effort | Recommended |
|---|---|---|
| Build OpenCV from source with the Qt-bundled MinGW | ~30 min, one-time | ✅ Yes |
| Use the vcpkg MinGW triplet (`x64-mingw-dynamic`) | ~20 min | ✅ Yes |
| Download MSVC OpenCV and try to link with MinGW | Undefined behaviour | ❌ No |

**Building OpenCV with vcpkg (recommended path):**
```bat
git clone https://github.com/microsoft/vcpkg
cd vcpkg
bootstrap-vcpkg.bat
vcpkg install opencv4[core,imgproc,calib3d]:x64-mingw-dynamic
```

In `CMakeLists.txt`:
```cmake
find_package(OpenCV REQUIRED COMPONENTS core imgproc calib3d)
target_link_libraries(StereoMasking PRIVATE ${OpenCV_LIBS})
```

Set `CMAKE_TOOLCHAIN_FILE` to `<vcpkg_root>/scripts/buildsystems/vcpkg.cmake` and `VCPKG_TARGET_TRIPLET` to `x64-mingw-dynamic` when configuring.

### Deep Learning Disparity (RAFT-Stereo, CREStereo)
LibTorch and ONNX Runtime ship MSVC-only Windows binaries — they will not link with MinGW. For this build target, disparity estimation must use **OpenCV StereoSGBM** or **StereoBM**. If neural disparity is required, run it as a separate Python sidecar process and communicate via named pipe or shared memory segment — do not attempt to link PyTorch directly.

### Threading
Use Qt's primitives (`QThread`, `QtConcurrent::run`) rather than raw `std::thread` where possible. MinGW's `libwinpthread` has known edge cases with thread-local storage that Qt's layer avoids.

---

## Background and Related Work

The algorithm is a **greedy iterative maximal-rectangle decomposition** applied to a stereo disparity mask. Related techniques:

| Technique | Relevance |
|---|---|
| Maximal rectangle in histogram (O(n) stack algorithm) | Phase 1 seed + Phase 2 growth inner loop |
| Rectilinear polygon decomposition (Klee 1979, Ohtsuki 1982) | Overall tiling strategy |
| Horowitz–Sahni / Toussaint rectilinear convex hull | Phase 4 polygon assembly |
| Stereo disparity estimation — OpenCV StereoSGBM / StereoBM | Phase 0 upstream |
| Epipolar rectification — `cv::stereoRectify` | Prerequisite |

---

## Definitions

| Term | C++ type | Meaning |
|---|---|---|
| `leftImg`, `rightImg` | `cv::Mat` (CV_8UC3) | Rectified stereo pair, same resolution W×H |
| `disparityMap` | `cv::Mat` (CV_32F) | Per-pixel horizontal offset, left→right |
| `occlusionMask` | `cv::Mat` (CV_8UC1) | 1 = pixel has no correspondence in the other image |
| `coverageMask` | `cv::Mat` (CV_8UC1) | Working mask: 1 = still needs coverage |
| `Rect` | `cv::Rect` | Axis-aligned rectangle (x, y, width, height) |
| `polygonL`, `polygonR` | `QPolygon` | Ordered vertex list for Qt rendering |
| `Direction` | `enum class Direction` | `Vertical` / `Horizontal`, alternates each iteration |

---

## Inputs

```cpp
struct StereoMaskParams {
    cv::Mat   leftImg;            // CV_8UC3, rectified
    cv::Mat   rightImg;           // CV_8UC3, rectified
    cv::Mat   disparityMap;       // CV_32F, optional — computed internally if empty
    int       minRectArea  = 500;
    int       maxIterations = 50;
    float     lrCheckThresh = 1.5f;  // left-right consistency threshold in pixels
};
```

---

## Outputs

```cpp
struct StereoMaskResult {
    QPolygon              polygonL;    // left-eye masking polygon
    QPolygon              polygonR;    // right-eye masking polygon
    std::vector<cv::Rect> tilesL;      // intermediate rectangle decomposition
    std::vector<cv::Rect> tilesR;
    QImage                debugVis;   // side-by-side visualisation, ready to display
};
```

---

## Algorithm Overview

```
Phase 0  →  Rectify images, estimate disparity, build occlusion mask
Phase 1  →  Seed: find the single largest spanning rectangle (vertical pass)
Phase 2  →  Grow: iteratively find next largest rectangle, alternating axis
Phase 3  →  Terminate: empty mask / edge reached / collision / iteration cap
Phase 4  →  Assemble: union tiles into a rectilinear QPolygon
Phase 5  →  Pair: run phases 1–4 independently for left and right masks
```

---

## Phase 0 — Preprocessing

### 0a. Epipolar Rectification

```cpp
// With calibration data:
cv::Mat R1, R2, P1, P2, Q;
cv::stereoRectify(cameraMatrixL, distCoeffsL,
                  cameraMatrixR, distCoeffsR,
                  imageSize, R, T,
                  R1, R2, P1, P2, Q,
                  cv::CALIB_ZERO_DISPARITY, 0, imageSize);

cv::Mat mapL1, mapL2, mapR1, mapR2;
cv::initUndistortRectifyMap(cameraMatrixL, distCoeffsL, R1, P1,
                            imageSize, CV_32F, mapL1, mapL2);
cv::initUndistortRectifyMap(cameraMatrixR, distCoeffsR, R2, P2,
                            imageSize, CV_32F, mapR1, mapR2);

cv::remap(leftImg,  leftRect,  mapL1, mapL2, cv::INTER_LINEAR);
cv::remap(rightImg, rightRect, mapR1, mapR2, cv::INTER_LINEAR);
```

If calibration is unavailable, use homography-based rectification (Fusiello et al. 2000).

### 0b. Disparity Estimation

```cpp
auto matcher = cv::StereoSGBM::create(
    /*minDisparity=*/0,
    /*numDisparities=*/128,   // must be divisible by 16
    /*blockSize=*/5,
    /*P1=*/8  * 3 * 5 * 5,
    /*P2=*/32 * 3 * 5 * 5,
    /*disp12MaxDiff=*/1,
    /*preFilterCap=*/0,
    /*uniquenessRatio=*/10,
    /*speckleWindowSize=*/100,
    /*speckleRange=*/32,
    /*mode=*/cv::StereoSGBM::MODE_SGBM_3WAY
);

cv::Mat disp16;
matcher->compute(leftRect, rightRect, disp16);
cv::Mat disparityL;
disp16.convertTo(disparityL, CV_32F, 1.0 / 16.0);
// Repeat with images swapped for disparityR
```

### 0c. Occlusion Mask Construction

Left-right consistency check: pixel `(x, y)` is occluded if the reverse-projected counterpart disagrees by more than `lrCheckThresh`.

```cpp
cv::Mat buildOcclusionMask(const cv::Mat& dispL, const cv::Mat& dispR,
                           float threshold) {
    const int H = dispL.rows, W = dispL.cols;
    cv::Mat mask = cv::Mat::zeros(H, W, CV_8UC1);

    for (int y = 0; y < H; ++y) {
        const float* dL = dispL.ptr<float>(y);
        uint8_t*     m  = mask.ptr<uint8_t>(y);
        for (int x = 0; x < W; ++x) {
            int xr = x - static_cast<int>(dL[x]);
            if (xr < 0 || xr >= W) { m[x] = 1; continue; }
            if (std::abs(dL[x] + dispR.ptr<float>(y)[xr]) > threshold)
                m[x] = 1;
        }
    }
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, {3, 3});
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);
    return mask;
}
```

---

## Phase 1 — Seed Rectangle (Vertical Pass)

Find the largest axis-aligned rectangle fully within the occlusion mask using the **maximal rectangle in histogram** algorithm (O(W × H)):

```cpp
cv::Rect largestRectInMask(const cv::Mat& mask) {
    const int H = mask.rows, W = mask.cols;
    std::vector<int> heights(W, 0);
    cv::Rect best;
    int bestArea = 0;

    for (int y = 0; y < H; ++y) {
        const uint8_t* row = mask.ptr<uint8_t>(y);
        for (int x = 0; x < W; ++x)
            heights[x] = row[x] ? heights[x] + 1 : 0;

        // Largest rectangle in histogram (monotonic stack)
        std::stack<int> stk;
        for (int x = 0; x <= W; ++x) {
            int h = (x == W) ? 0 : heights[x];
            while (!stk.empty() && heights[stk.top()] > h) {
                int height = heights[stk.top()]; stk.pop();
                int width  = stk.empty() ? x : x - stk.top() - 1;
                int area   = height * width;
                if (area > bestArea) {
                    bestArea = area;
                    int left = stk.empty() ? 0 : stk.top() + 1;
                    best = cv::Rect(left, y - height + 1, width, height);
                }
            }
            stk.push(x);
        }
    }
    return best;
}
```

After finding the seed: `coverageMask(seedRect).setTo(0)`.

---

## Phase 2 — Iterative Rectangle Growth

```cpp
enum class Direction { Vertical, Horizontal };

cv::Rect largestRectConstrained(const cv::Mat& coverage, Direction dir) {
    if (dir == Direction::Vertical) {
        cv::Mat points;
        cv::findNonZero(coverage, points);
        if (points.empty()) return {};
        cv::Rect bb = cv::boundingRect(points);
        cv::Mat crop = coverage(bb);
        cv::Rect local = largestRectInMask(crop);
        return cv::Rect(bb.x + local.x, bb.y + local.y,
                        local.width, local.height);
    } else {
        cv::Mat transposed;
        cv::transpose(coverage, transposed);
        cv::Rect tRect = largestRectInMask(transposed);
        return cv::Rect(tRect.y, tRect.x, tRect.height, tRect.width);
    }
}
```

Direction alternates `Vertical → Horizontal → Vertical → ...` each iteration.

---

## Phase 3 — Termination Conditions

Stop when **any** of the following is true:

1. `cv::countNonZero(coverageMask) == 0` — mask fully covered.
2. `candidate.area() < minRectArea` — diminishing returns.
3. `(candidate & existingTile).area() > 0` for any existing tile — collision.
4. `candidate == cv::Rect(0, 0, mask.cols, mask.rows)` — reached all image edges.
5. `iteration >= maxIterations`.

---

## Phase 4 — Polygon Assembly

```cpp
QPolygon assemblePolygon(const std::vector<cv::Rect>& tiles,
                         cv::Size imageSize) {
    cv::Mat canvas = cv::Mat::zeros(imageSize, CV_8UC1);
    for (const auto& r : tiles)
        canvas(r).setTo(255);

    // Dilate to absorb sub-pixel gaps between adjacent tiles
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, {3, 3});
    cv::dilate(canvas, canvas, kernel, {-1,-1}, 1);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(canvas, contours, cv::RETR_EXTERNAL,
                     cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) return {};

    std::vector<cv::Point> approx;
    cv::approxPolyDP(contours[0], approx, 2.0, /*closed=*/true);

    QPolygon poly;
    for (const auto& pt : approx)
        poly << QPoint(std::lround(pt.x), std::lround(pt.y));
    return poly;
}
```

---

## Phase 5 — Top-Level Orchestrator

```cpp
StereoMaskResult computeStereoMasks(const StereoMaskParams& p) {
    cv::Mat dispL = p.disparityMap.empty()
                        ? estimateDisparity(p.leftImg, p.rightImg)
                        : p.disparityMap;
    cv::Mat dispR = estimateDisparity(p.rightImg, p.leftImg);

    cv::Mat maskL = buildOcclusionMask(dispL, dispR, p.lrCheckThresh);
    cv::Mat maskR = buildOcclusionMask(dispR, dispL, p.lrCheckThresh);

    StereoMaskResult result;
    result.tilesL   = decomposeMask(maskL, p.minRectArea, p.maxIterations);
    result.tilesR   = decomposeMask(maskR, p.minRectArea, p.maxIterations);
    result.polygonL = assemblePolygon(result.tilesL, maskL.size());
    result.polygonR = assemblePolygon(result.tilesR, maskR.size());
    result.debugVis = renderDebug(p.leftImg, p.rightImg,
                                  result.polygonL, result.polygonR,
                                  result.tilesL,   result.tilesR);
    return result;
}

std::vector<cv::Rect> decomposeMask(const cv::Mat& mask,
                                    int minArea, int maxIter) {
    cv::Mat coverage = mask.clone();
    std::vector<cv::Rect> tiles;
    Direction dir = Direction::Vertical;

    cv::Rect seed = largestRectInMask(coverage);
    if (seed.area() == 0) return tiles;
    tiles.push_back(seed);
    coverage(seed).setTo(0);

    for (int i = 0; i < maxIter; ++i) {
        if (cv::countNonZero(coverage) == 0) break;

        cv::Rect candidate = largestRectConstrained(coverage, dir);
        if (candidate.area() < minArea) break;

        bool collides = false;
        for (const auto& t : tiles)
            if ((candidate & t).area() > 0) { collides = true; break; }
        if (collides) break;

        if (candidate == cv::Rect(0, 0, mask.cols, mask.rows)) break;

        tiles.push_back(candidate);
        coverage(candidate).setTo(0);
        dir = (dir == Direction::Vertical)
                  ? Direction::Horizontal : Direction::Vertical;
    }
    return tiles;
}
```

---

## Qt Integration

### Rendering the Polygons on a QWidget

```cpp
class StereoMaskWidget : public QWidget {
    Q_OBJECT
public:
    void setImages(const QImage& left, const QImage& right) {
        m_left = left; m_right = right; update();
    }
    void setPolygons(const QPolygon& polyL, const QPolygon& polyR) {
        m_polyL = polyL; m_polyR = polyR; update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        int half = width() / 2;

        p.drawImage(QRect(0,    0, half, height()), m_left);
        p.drawImage(QRect(half, 0, half, height()), m_right);

        // Left mask — red semi-transparent
        p.setBrush(QColor(220, 50, 50, 100));
        p.setPen(QPen(QColor(220, 50, 50), 1.5));
        p.drawPolygon(m_polyL);

        // Right mask — blue semi-transparent, offset by half-width
        p.setBrush(QColor(50, 100, 220, 100));
        p.setPen(QPen(QColor(50, 100, 220), 1.5));
        p.drawPolygon(m_polyR.translated(half, 0));
    }
private:
    QImage   m_left, m_right;
    QPolygon m_polyL, m_polyR;
};
```

### Running the Pipeline Off the UI Thread

```cpp
// Use QtConcurrent — safer than std::thread with MinGW's libwinpthread
void MainWindow::onComputeClicked() {
    auto* watcher = new QFutureWatcher<StereoMaskResult>(this);
    connect(watcher, &QFutureWatcher<StereoMaskResult>::finished,
            this, [=] {
        m_maskWidget->setPolygons(watcher->result().polygonL,
                                  watcher->result().polygonR);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(computeStereoMasks, buildParams()));
}
```

### Converting `cv::Mat` to `QImage`

```cpp
QImage matToQImage(const cv::Mat& mat) {
    // OpenCV is BGR; Qt expects RGB
    cv::Mat rgb;
    cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
    // .copy() makes the QImage own its buffer — safe to return
    return QImage(rgb.data, rgb.cols, rgb.rows,
                  static_cast<int>(rgb.step),
                  QImage::Format_RGB888).copy();
}
```

---

## CMakeLists.txt (Complete)

```cmake
cmake_minimum_required(VERSION 3.22)
project(StereoMasking LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

# vcpkg toolchain — set via -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
# and -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent)
find_package(OpenCV REQUIRED COMPONENTS core imgproc calib3d)

add_executable(StereoMasking
    src/main.cpp
    src/main_window.cpp
    src/stereo_mask_widget.cpp
    src/pipeline.cpp
    src/disparity.cpp
    src/occlusion.cpp
    src/rect_decompose.cpp
    src/polygon_assemble.cpp
    src/visualise.cpp
)

target_link_libraries(StereoMasking PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent
    ${OpenCV_LIBS}
)

target_compile_options(StereoMasking PRIVATE
    -O2 -march=native -funroll-loops -Wall -Wextra
)
```

---

## Deployment (Windows)

When distributing, include these DLLs alongside the `.exe`. Run `windeployqt6.exe <your_exe>` first — it handles Qt DLLs automatically. Add the MinGW runtime and OpenCV DLL manually:

```
libgcc_s_seh-1.dll       ← from Qt's MinGW bin/
libstdc++-6.dll
libwinpthread-1.dll
opencv_world4xx.dll      ← version number from your vcpkg build
```

---

## Implementation Notes

### Performance
- The histogram inner loop is O(W) per row — fast enough for single-frame use on the UI thread.
- For real-time (≥30 fps), downsample the occlusion mask to ¼ resolution before decomposition, then scale polygon vertices up by 4×.
- `cv::StereoSGBM` at 1080p takes 80–200 ms — always run it on a `QtConcurrent` worker thread.

### Edge Cases
- **Uniform scene (flat wall):** Check `cv::countNonZero(mask) < minRectArea` before decomposition and return an empty `QPolygon`.
- **Large foreground disparity:** Seed rectangle may cover almost the full image strip — this is correct.
- **Multi-region occlusion:** Use `cv::connectedComponents` to label components, run decomposition on each separately.
- **Left/right polygon size mismatch:** Expected and correct — do not force them to match.

---

## File Structure

```
StereoMasking/
├── AGENT.md
├── CMakeLists.txt
├── src/
│   ├── main.cpp                  ← QApplication entry point
│   ├── main_window.h/cpp         ← QMainWindow, layout, controls
│   ├── stereo_mask_widget.h/cpp  ← side-by-side display + polygon overlay
│   ├── pipeline.h/cpp            ← computeStereoMasks(), decomposeMask()
│   ├── disparity.h/cpp           ← Phase 0: rectification + StereoSGBM
│   ├── occlusion.h/cpp           ← Phase 0: buildOcclusionMask()
│   ├── rect_decompose.h/cpp      ← Phases 1–3: largestRectInMask(), growth loop
│   ├── polygon_assemble.h/cpp    ← Phase 4: assemblePolygon()
│   └── visualise.h/cpp           ← renderDebug() → QImage
├── tests/
│   ├── test_rect_decompose.cpp
│   └── test_pipeline.cpp
└── assets/
    └── test_stereo_pair.png
```

---

## Testing Checklist

- [ ] Project configures and builds cleanly in Qt Creator with the Qt6 MinGW kit (no MSVC CMake preset active)
- [ ] No mixed MinGW/MSVC runtime DLLs in the build output (`dumpbin /dependents` or `objdump -p`)
- [ ] `windeployqt6.exe` + manual MinGW/OpenCV DLLs produce a self-contained folder that runs on a clean Windows machine
- [ ] Seed rectangle covers the largest connected occlusion region
- [ ] Second rectangle is perpendicular to the seed axis
- [ ] No two tiles overlap (`(a & b).area() == 0` for all tile pairs)
- [ ] Final `QPolygon` vertices are axis-aligned (no diagonal edges)
- [ ] Left and right polygons differ when disparity is non-zero
- [ ] `StereoMaskWidget::paintEvent` renders without flicker on repeated calls
- [ ] `QtConcurrent::run` pipeline does not crash or deadlock on repeated calls
- [ ] Algorithm terminates within `maxIterations` on all test images
- [ ] Output polygon area ≥ 95% of the occlusion mask area on clean synthetic test cases
- [ ] Full pipeline (excluding disparity) runs in < 50 ms at 1920×1080
