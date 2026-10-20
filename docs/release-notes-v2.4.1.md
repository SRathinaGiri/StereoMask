# StereoMask v2.4.1

## Consistent Version Displays

- appversion.h is the single source for the application version. This release changes only that version constant to 2.4.1 for all runtime displays and build metadata.
- The window title, startup banner, About and updater use the same constant. MainWindow sets the versioned title directly, including when created outside main.cpp.
- CMake generates the embedded and packaged User Guide from version-neutral source HTML using the header's version. No manually maintained help-banner version remains.
- In-app help opens a version-specific cache directory instead of sharing help/r10 across releases, preventing reuse of the same help URL by older/newer copies.
- Added checks for the actual startup banner, window title, embedded help version, versioned cache URL and existing About display. All five CTest suites pass after the version bump.

All v2.4.0 drawers, Quick Access favorites and Mac-reviewed safety fixes are retained. No project format or fuzzy-selection behavior changes. Previous releases remain available; older executable copies still show their own installed version.

## Packaging and Mac Handoff

Extract the complete Windows x64 ZIP and run its StereoMask.exe. Do not copy only the executable out of the package or continue launching an older folder.

The packaged standalone guide is build/generated/docs/user-guide.html, not the version-neutral docs/user-guide.html source. CMake configures resources.qrc.in into the build directory while preserving the existing resource aliases. Rebuild after changing appversion.h; both resource generation and project metadata track it.

The Mac Handoff ZIP includes the updated guide, release notes, source references, changed Windows files and the Windows v2.3.0-to-v2.4.1 patch. See [Windows-to-Mac Handoff](windows-to-mac-handoff.md) for the verified Mac R20 baseline and optional adoption details. This is source/documentation for the Mac maintainer, not a Mac binary.

Five automated suites and a packaged startup smoke test cover this release. Dependency notices, original Stereomasken inspiration and both collaborators' credits are retained.
