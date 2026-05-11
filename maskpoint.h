#ifndef MASKPOINT_H
#define MASKPOINT_H

#include <QPointF>

struct MaskPoint {
    QPointF pos;      // Normalized coordinates (0.0 to 1.0) or pixel coordinates? Let's use pixels for now.
    float disparity;  // Horizontal shift for this specific point in pixels

    // Bezier control points for the segment STARTING at this point
    bool isCurve = false;
    QPointF cp1;
    float cp1Disparity = 0;
    QPointF cp2;
    float cp2Disparity = 0;
};

#endif // MASKPOINT_H
