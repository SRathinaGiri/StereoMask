#ifndef SMARTSELECTION_H
#define SMARTSELECTION_H
#include <QImage>
#include <QRect>
#include <QString>

struct SmartSelectionResult {
    QImage mask; // Alpha mask for the selected rectangle only.
    QString error;
};

SmartSelectionResult findSmartEdges(const QImage &image, const QRect &region,
                                  const QImage &hints, const QImage &roughReveal);
#include <QVector>
#include <QPointF>
QVector<QPointF> magneticLassoPath(const QImage &image, const QPointF &start, const QPointF &end);

struct StereoPolygonMatch {
    QVector<QPointF> anchors;
    int matchedAnchors = 0;
};
StereoPolygonMatch alignStereoPolygon(const QImage &source, const QImage &opposite,
                                     const QVector<QPointF> &anchors, double fallbackShift);

#endif
