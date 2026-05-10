#ifndef MASKPOINT_H
#define MASKPOINT_H

#include <QPointF>

struct MaskPoint {
    QPointF pos;      // Normalized coordinates (0.0 to 1.0) or pixel coordinates? Let's use pixels for now.
    float disparity;  // Horizontal shift for this specific point in pixels
};

#endif // MASKPOINT_H
