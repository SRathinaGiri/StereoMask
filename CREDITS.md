# Credits

**Bernd Paksa** created the original **Stereomasken** program and its creative 3D framing and masking ideas. StereoMask is inspired by that work, and acknowledges his contribution to the stereo photography community. Original website: [Stereomasken](https://www.stereomasken.de/index_en.htm).

StereoMask is a collaborative effort between Subbiah Rathinagiri and Theodore Whitten.

Subbiah Rathinagiri (credited in StereoMask's MIT license as S. Rathinagiri) created the modern StereoMask Windows implementation and maintains the upstream PC/Windows project: https://github.com/SRathinaGiri/StereoMask.

Theodore Whitten contributed the Mac port, feature direction and foreground-editing improvements, with Codex assistance: brush preservation/erasing, custom and transparent backgrounds, eyedropper sampling, polygonal lasso, multiple additive/subtractive areas, view shortcuts and this user guide.

The original application describes development with GEMINI CLI and Qt 6. That upstream credit is retained. Qt and OpenCV are third-party dependencies, not authored by the collaborators.

Windows v2.1.0 incorporates these foreground-editing improvements from the Mac source, with Windows shortcuts, documentation and update checking. This source distribution preserves the original MIT license. Binary distributions must also include applicable third-party license notices.

The subsequent Windows work adds independent selection objects and ordered frame holes, applied polygon editing, paired stereo alignment, point gestures and shortcuts, synchronized mask controls, and consistent export-mode preview. The magnetic-lasso and GrabCut helper foundation was imported from Theodore's Mac work; it is not claimed as a newly authored Windows engine. See [the source-backed Windows/Mac comparison](docs/windows-mac-comparison.md) for current implementation differences and a reciprocal porting checklist.

Windows v2.4.0 also adopts the processing-panel lifetime guards, malformed-text-entry handling and explicit text-cache cleanup reviewed in Theodore's Mac R19 integration. Windows retains its own fuzzy-selection connectivity and platform defaults. See the [current Windows-to-Mac handoff](docs/windows-to-mac-handoff.md) for the reviewed Mac R20 baseline and the new Windows toolbox/Quick Access delta.
