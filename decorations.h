#pragma once
#include <QColor>
#include <QRectF>
#include <QFont>
#include <QImage>
#include <QJsonObject>
#include <QVector>
#include <QTransform>
#include "maskpoint.h"

struct StereoText {
    QString id, text=QStringLiteral("Your text");
    QRectF rect=QRectF(40,40,240,120);
    QPointF tail=QPointF(100,200);
    int depth=0, fontSize=32, style=0, alignment=0, opacity=100, outline=2;
    QFont font;
    QColor color=Qt::black, fill=Qt::white, stroke=Qt::black;
    bool visible=true, foregroundCovers=false;
    // Zero defaults preserve the appearance of existing projects.
    int relief=0, surface=0, reliefSize=3;
    int tiltX=0, tiltY=0, tiltZ=0;
};
QJsonObject textToJson(const StereoText &text);
StereoText textFromJson(const QJsonObject &value, QSize canvas);
QImage textOverlay(QSize canvas,const QVector<StereoText> &texts,bool right,const QImage &foreground={});
QTransform stereoTextTransform(const StereoText &text,bool right);
QVector<MaskPoint> shapePoints(int shape,const QRectF &rect,int depth);
