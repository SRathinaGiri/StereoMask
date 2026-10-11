# StereoMask v2.1.1

## Whole-Polygon Movement

- Select a visible polygon in the Selections panel and enable **Move polygon**, the four-arrow toolbar button.
- Drag the polygon's interior or edge in its corresponding eye. Its anchors and saved outline translate together without reshaping or retracing smart edges.
- Hold **Shift** for horizontal-only movement, useful for disparity corrections. **Escape** cancels an active drag.
- The whole shape stays within its image. Each completed drag is one Undo/Redo operation; no-motion clicks do not add undo steps.
- Paired polygons remain independent: moving one does not change its former partner.
- Zoom and swapped display order map movement to source-image coordinates. Moved coordinates and paths persist in project JSON.

Apply or cancel a pending lasso before enabling movement. The tool moves applied polygons, not raster brush/imported groups. Turn it off to resume tracing. View zoom changes are deferred during an active drag.

## Download and Verification

Download **StereoMask_v2.1.1_Release.zip**, extract the complete folder and run StereoMask.exe. Save work and close the old app before replacing its files. Bundled runtimes, offline guide, credits and dependency notices are included.

The three CTest suites passed, including paired-eye independence, shape-preserving smart movement, horizontal constraint, image boundaries, zoom/Swap mapping, Escape cancellation, Undo/Redo, JSON restoration and toolbar wiring.

The [Windows/Mac porting checklist](windows-mac-comparison.md) now includes 27 differences and the move-mode implementation guidance. All v2.1.0 editing and preview/export features remain available; its release is retained unchanged. Use project copies with unmodified Mac R10 because its older schema cannot losslessly preserve the ordered Windows object model.

Original inspiration: Bernd Paksa's [Stereomasken](https://www.stereomasken.de/index_en.htm). Windows maintenance: S. Rathinagiri. Mac port and foreground-editing foundation: Theodore Whitten. Original MIT license retained.
