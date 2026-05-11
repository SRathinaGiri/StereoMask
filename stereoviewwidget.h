#ifndef STEREOVIEWWIDGET_H
#define STEREOVIEWWIDGET_H

#include <QWidget>
#include <QRect>
#include "stereoimageprocessor.h"

#include <QVector>
#include "maskpoint.h"

#include <QUndoStack>

class StereoViewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit StereoViewWidget(QWidget *parent = nullptr);

    bool loadImage(const QString &fileName);
    bool loadProject(const QString &fileName);
    bool saveProject();
    QString lastError() const { return m_lastError; }
    bool saveImage(const QString &fileName, const QColor &maskColor, float opacity, int padx, int pady, const QColor &bgColor, int interleavingSpace, std::function<void(int)> progressCallback = nullptr);
    static bool saveImageData(const QString &fileName, const QImage &leftImage, const QImage &rightImage, const QVector<MaskPoint> &points, const QColor &maskColor, float opacity, int padx, int pady, const QColor &bgColor, int interleavingSpace, int featherAmount, std::function<void(int)> progressCallback = nullptr);
    
    bool isImageLoaded() const { return !m_imagePath.isEmpty() && m_processor.isValid(); }
    void setAnaglyphMode(bool enabled);
    void setSwapSides(bool enabled);
    void setZoom(float zoom);
    void setLockHorizontal(bool locked) { m_lockHorizontal = locked; }
    void setLockVertical(bool locked) { m_lockVertical = locked; }
    void setSnapEnabled(bool enabled) { m_snapEnabled = enabled; }
    void setPanMode(bool enabled) { m_panMode = enabled; }
    float zoom() const { return m_zoom; }

    void addPoint(const QPointF &pos, float disparity = 0, int index = -1);
    void deleteSelectedPoints();
    void clearPoints();
    void setSelectedPointDisparity(int disparity);
    void transformSelectedPoints(float scaleX, float scaleY, float dx, float dy);
    void toggleCurve();

    void setMaskSettings(const QColor &color, float opacity, int feather = 0) { 
        m_maskColor = color; m_maskOpacity = opacity; m_featherAmount = feather; 
        updateAnaglyphIfActive(); update(); 
    }
    void setMaskColor(const QColor &color) { m_maskColor = color; updateAnaglyphIfActive(); update(); }
    void setMaskOpacity(float opacity) { m_maskOpacity = opacity; updateAnaglyphIfActive(); update(); }
    void setMaskFeather(int amount) { m_featherAmount = amount; updateAnaglyphIfActive(); update(); }
    void setPaddingSettings(int px, int py, const QColor &bg, int interleaving) { m_padx = px; m_pady = py; m_bgColor = bg; m_interleavingSpace = interleaving; }

    void updateAnaglyphIfActive();

    QColor maskColor() const { return m_maskColor; }
    float maskOpacity() const { return m_maskOpacity; }
    int padx() const { return m_padx; }
    int pady() const { return m_pady; }
    int featherAmount() const { return m_featherAmount; }
    bool snapEnabled() const { return m_snapEnabled; }
    QColor bgColor() const { return m_bgColor; }
    int interleavingSpace() const { return m_interleavingSpace; }
    bool hasCurveSelection() const;
    QImage leftImage() const { return m_processor.leftImage(); }
    QImage rightImage() const { return m_processor.rightImage(); }
    QVector<MaskPoint> points() const { return m_points; }

    void setAutoSave(bool enabled) { m_autoSave = enabled; }

    enum AlignSide { AlignLeft, AlignRight, AlignTop, AlignBottom, AlignDepth };
    void alignSelectedPoints(AlignSide side);

    enum RotationAxis { AxisNone, AxisX, AxisY, AxisZ };
    void setRotationMode(RotationAxis axis) { m_rotationMode = axis; update(); }
    RotationAxis rotationMode() const { return m_rotationMode; }
    void rotateSelectedPoints(float angleDegrees, RotationAxis axis);

    QUndoStack* undoStack() const { return m_undoStack; }
    QString imagePath() const { return m_imagePath; }

signals:
    void selectionChanged(int currentDisparity);
    void curveSelectionChanged(bool hasCurveSelection);

public:
    // Internal methods for Undo Commands
    void updatePointInternal(int index, const MaskPoint &p);
    void updatePointsInternal(const QList<int> &indices, const QVector<MaskPoint> &ps);
    void addPointInternal(const MaskPoint &p, int index = -1);
    void removePointInternal(int index = -1);
    void insertPointsInternal(const QList<int> &indices, const QVector<MaskPoint> &ps);
    void removePointsInternal(const QList<int> &indices);

    QImage image() const { return m_processor.leftImage(); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    QPainterPath createMaskPath(const QVector<MaskPoint> &pts, const QRectF &v, float scale, bool isRight, bool includeRect = true, bool ignorePan = false);
    void drawFeatheredPath(QPainter &painter, const QPainterPath &path, const QColor &color, int feather);
    QPointF widgetToImage(const QPoint &pos, const QRect &targetRect, float scale);
    QPointF imageToWidget(const QPointF &pos, const QRect &targetRect, float scale, float disparity = 0);
    void calculateLayout(QRect &rL, QRect &rR, float &scale);
    void notifySelectionState();

    enum ControlPointType { HitNone, HitPoint, HitCP1, HitCP2 };
    ControlPointType m_hitType = HitNone;
    RotationAxis m_rotationMode = AxisNone;

    StereoImageProcessor m_processor;
    QVector<MaskPoint> m_points;
    QVector<MaskPoint> m_preMovePoints; // For batch undo
    QList<int> m_selectedIndices;
    QRect m_selectionRect;
    MaskPoint m_preMovePoint;
    int m_selectedPointIndex = -1;
    QPoint m_lastMousePos;
    QPoint m_mouseDragStart;
    float m_zoom = 1.0f;
    QPointF m_panOffset; // Panning offset in image coordinates
    QUndoStack *m_undoStack;
    bool m_anaglyphMode = false;
    bool m_swapSides = false;
    bool m_lockHorizontal = false;
    bool m_lockVertical = false;
    bool m_snapEnabled = true;
    bool m_isSelecting = false;
    bool m_isPanning = false;
    bool m_panMode = false;
    QString m_imagePath;
    QString m_lastError;

    QColor m_maskColor = Qt::black;
    float m_maskOpacity = 0.6f;
    int m_featherAmount = 0;
    int m_padx = 0;
    int m_pady = 0;
    QColor m_bgColor = Qt::white;
    int m_interleavingSpace = 0;
    bool m_autoSave = true;
};

#endif // STEREOVIEWWIDGET_H
