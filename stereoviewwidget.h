#ifndef STEREOVIEWWIDGET_H
#define STEREOVIEWWIDGET_H

#include <QWidget>
#include <QRect>
#include "stereoimageprocessor.h"

#include <QVector>
#include "maskpoint.h"

#include <QUndoStack>
#include <QPainterPath>

class QTimer;
class QJsonObject;
class QKeyEvent;

struct SelectionObject {
    QString id;
    QString name;
    bool right = false;
    bool subtract = false;
    bool visible = true;
    QVector<QPointF> anchors;
    QVector<bool> magnetic;
    QPainterPath path;
    QImage raster;
};

struct SelectionState {
    QVector<SelectionObject> objects;
    QImage baseLeft, baseRight;
    QPainterPath outlineLeft, outlineRight;
};

class StereoViewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit StereoViewWidget(QWidget *parent = nullptr);

    QVector<SelectionObject> selectionObjects() const { return m_selectionObjects; }
    SelectionState selectionState() const;
    void restoreSelectionState(const SelectionState &state);
    SelectionState stateWithRasterChange(bool right, const QImage &before, const QImage &after) const;
    void setSelectionVisible(const QString &id, bool visible);
    void renameSelection(const QString &id, const QString &name);
    void deleteSelection(const QString &id);
    void moveSelection(const QString &id, int offset);
    void toggleSelectionOperation(const QString &id);
    bool editSelection(const QString &id);
    void selectSelection(const QString &id) { m_activeSelectionId=id; update(); }

    void setBrushMode(int mode) { if (m_refining) return; if (mode < 7) { if (m_lassoClosed) finishLasso(); else cancelLasso(); } m_brushMode = mode; m_panMode = false; m_freehandMode = false; m_pickingColor = false; setCursor((mode == 3 || mode >= 7) ? Qt::CrossCursor : mode ? Qt::BlankCursor : Qt::ArrowCursor); setFocus(); update(); }
    int brushMode() const { return m_brushMode; }
    void setBrushSize(int size) { m_brushSize = qBound(1, size, 1000); update(); }
    void setCutoutColor(const QColor &color) { m_cutoutColor = color; setCutoutBackground(3); }
    QColor cutoutColor() const { return m_cutoutColor; }
    void beginColorPick() { m_pickingColor = true; setCursor(Qt::CrossCursor); setFocus(); update(); }
    void setCutoutBackground(int mode) { m_cutout = true; m_cutoutBackground = qBound(0,mode,4); updateAnaglyphIfActive(); update(); }
    bool cutoutEnabled() const { return m_cutout; }
    int cutoutBackground() const { return m_cutoutBackground; }
    QImage revealMask(bool right) const { return right ? m_revealRight : m_revealLeft; }
    QImage openingMask(bool right, int feather = 0) const;
    QImage exportEye(bool right) const;
    void applyReveal(bool right, const QImage &mask);
    void applySmartHints(bool right, const QImage &hints, const QRect &region);
    void findEdges();
    void closeLasso();
    void applyLasso(bool erase = false);
    void finishLasso() { applyLasso(m_lassoSubtract); }
    void cancelUnfinishedLasso() { if (m_lassoClosed) finishLasso(); else cancelLasso(); }
    void setLassoSubtract(bool subtract) { if (m_lassoClosed) finishLasso(); m_lassoSubtract=subtract; setFocus(); }
    bool lassoSubtract() const { return m_lassoSubtract; }
    void setPairedLassoEnabled(bool enabled) { m_pairedLassoEnabled=enabled; setFocus(); }
    bool pairedLassoEnabled() const { return m_pairedLassoEnabled; }
    QPointF panOffset() const { return m_panOffset; }
    bool lassoClosed() const { return m_lassoClosed; }
    QPainterPath completedLasso(bool right) const { return right ? m_completedRight : m_completedLeft; }
    void applyLassoSelection(bool right, const QImage &mask, const QPainterPath &outline);
    void clearPaintedReveal();
    void fitView() { m_zoom=1.0f; m_panOffset=QPointF(); update(); setFocus(); }
    void cancelLasso();
    void removeLassoSegment();
    bool hasLasso() const { return !m_lassoAnchors.isEmpty(); }
    void clearSmartHints();
    static QImage cutoutEye(const QImage &image, const QVector<MaskPoint> &points, bool right, const QImage &reveal, int background, const QColor &color = Qt::black, bool completeOpening = false);
    static QImage maskedPreviewEye(const QImage &image, const QImage &opening, const QColor &color, float opacity);
    static bool saveMaskedPreview(const QString &file, const QImage &left, const QImage &right, const QImage &openingLeft, const QImage &openingRight, const QColor &color, float opacity, int padx, int pady, int gap, const QColor &paddingColor);
    static bool saveCutout(const QString &file, const QImage &left, const QImage &right, const QVector<MaskPoint> &points, const QImage &revealLeft, const QImage &revealRight, int background, int padx, int pady, int gap, const QColor &color = Qt::black, bool completeOpening = false);
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
    void setFreehandMode(bool enabled);
    float zoom() const { return m_zoom; }

    void addPoint(const QPointF &pos, float disparity = 0, int index = -1);
    void deleteSelectedPoints();
    void clearPoints();
    void setSelectedPointDisparity(int disparity);
    void transformSelectedPoints(float scaleX, float scaleY, float dx, float dy);
    void toggleCurve();
    bool createAutoMaskPoints();

    void setMaskSettings(const QColor &color, float opacity, int feather = 0) { 
        m_maskColor = color; m_maskOpacity = opacity; m_featherAmount = feather; 
        emit maskAppearanceChanged(m_maskColor,m_maskOpacity);
        updateAnaglyphIfActive(); update(); 
    }
    void setMaskColor(const QColor &color) { if (!color.isValid()) return; m_maskColor = color; emit maskAppearanceChanged(m_maskColor,m_maskOpacity); updateAnaglyphIfActive(); update(); }
    void setMaskOpacity(float opacity) { m_maskOpacity = qBound(0.0f,opacity,1.0f); emit maskAppearanceChanged(m_maskColor,m_maskOpacity); updateAnaglyphIfActive(); update(); }
    void setMaskFeather(int amount) { m_featherAmount = amount; updateAnaglyphIfActive(); update(); }
    void setPreviewFeatherEnabled(bool enabled) { m_previewFeatherEnabled = enabled; updateAnaglyphIfActive(); update(); }
    void setMaskPreviewEnabled(bool enabled) { m_maskPreviewEnabled = enabled; updateAnaglyphIfActive(); update(); }
    void setCleanPreviewEnabled(bool enabled) { m_cleanPreviewEnabled=enabled; updateAnaglyphIfActive(); update(); setFocus(); }
    bool cleanPreviewEnabled() const { return m_cleanPreviewEnabled; }
    void setPaddingSettings(int px, int py, const QColor &bg, int interleaving) { m_padx = px; m_pady = py; m_bgColor = bg; m_interleavingSpace = interleaving; }

    void updateAnaglyphIfActive();

    QColor maskColor() const { return m_maskColor; }
    float maskOpacity() const { return m_maskOpacity; }
    int padx() const { return m_padx; }
    int pady() const { return m_pady; }
    int featherAmount() const { return m_featherAmount; }
    bool previewFeatherEnabled() const { return m_previewFeatherEnabled; }
    bool maskPreviewEnabled() const { return m_maskPreviewEnabled; }
    bool snapEnabled() const { return m_snapEnabled; }
    QColor bgColor() const { return m_bgColor; }
    int interleavingSpace() const { return m_interleavingSpace; }
    bool hasCurveSelection() const;
    bool canFreehandDraw() const { return m_points.isEmpty(); }
    bool canAutoMask() const { return m_processor.isValid() && m_points.isEmpty(); }
    QImage leftImage() const { return m_processor.leftImage(); }
    QImage rightImage() const { return m_processor.rightImage(); }
    QVector<MaskPoint> points() const { return m_points; }

    void setAutoSave(bool enabled) { m_autoSave = enabled; }

    enum AlignSide { AlignLeft, AlignRight, AlignTop, AlignBottom, AlignDepth };
    void alignSelectedPoints(AlignSide side);

    enum AutoMaskMode { AutoMaskRectDecompose, AutoMaskContour, AutoMaskLegacy };
    bool createAutoMaskPoints(AutoMaskMode mode);

    enum RotationAxis { AxisNone, AxisX, AxisY, AxisZ };
    void setRotationMode(RotationAxis axis) { m_rotationMode = axis; update(); }
    RotationAxis rotationMode() const { return m_rotationMode; }
    void rotateSelectedPoints(float angleDegrees, RotationAxis axis);

    QUndoStack* undoStack() const { return m_undoStack; }
    QString imagePath() const { return m_imagePath; }

signals:
    void selectionObjectsChanged();
    void lassoOperationChanged(bool subtract);
    void smartStatus(const QString &message);
    void smartBusy(bool busy);
    void maskColorPicked(const QColor &color);
    void maskAppearanceChanged(const QColor &color, float opacity);
    void brushSettingsLoaded(int mode, int background);
    void selectionChanged(int currentDisparity);
    void curveSelectionChanged(bool hasCurveSelection);
    void maskEmptyChanged(bool isEmpty);
    void freehandModeChanged(bool enabled);

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
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    QVector<SelectionObject> m_selectionObjects;
    QImage m_baseRevealLeft, m_baseRevealRight;
    QPainterPath m_baseOutlineLeft, m_baseOutlineRight;
    QString m_editingSelectionId;
    QString m_activeSelectionId;
    void rebuildSelectionMasks();
    void loadSelectionObjects(const QJsonObject &obj);
    QJsonObject selectionObjectsJson() const;
    QImage m_smartHintsLeft, m_smartHintsRight;
    QRect m_smartRegionLeft, m_smartRegionRight, m_beforeRegion;
    QPointF m_regionStart;
    bool m_selectingRegion = false, m_smartRight = false, m_refining = false;
    int m_smartRevision = 0;
    void loadBrushSettings(const QJsonObject &obj);
    QImage m_revealTintLeft, m_revealTintRight;
    qint64 m_tintKeyLeft = -1, m_tintKeyRight = -1;
    QColor m_cutoutColor = Qt::black;
    QPoint m_brushCursor;
    bool m_cursorVisible = false, m_pickingColor = false, m_colorPickClick = false;
    void brushAt(const QPoint &pos);
    QVector<QPointF> m_lassoAnchors;
    QVector<QVector<QPointF>> m_lassoSegments;
    QVector<bool> m_lassoMagnetic;
    QVector<QPointF> m_lassoClosing;
    bool m_lassoRight = false, m_lassoClosed = false;
    bool m_spacePan = false;
    bool m_lassoSubtract = false;
    bool m_pairedLassoEnabled = false;
    QPainterPath m_completedLeft, m_completedRight;
    int m_lassoDrag = -1;
    void rebuildLassoSegment(int index);
    void rebuildLassoClosing();
    void removeLassoPoint(int index);
    int m_brushMode = 0, m_brushSize = 40, m_cutoutBackground = 4;
    bool m_cutout = false, m_brushing = false, m_brushRight = false;
    QImage m_revealLeft, m_revealRight, m_beforeStroke;
    QPointF m_lastBrushPoint;
    bool m_hasBrushPoint = false;
    struct AutoMaskDebugFrame {
        QRectF imageRect;
        QVector<QPointF> imagePolygon;
        QVector<QVector<QPointF>> imagePolygons;
        QVector<float> polygonDisparities;
        float disparity = 0.0f;
        QString label;
    };

    QPainterPath createMaskPath(const QVector<MaskPoint> &pts, const QRectF &v, float scale, bool isRight, bool includeRect = true, bool ignorePan = false);
    static void drawFeatheredPath(QPainter &painter, const QPainterPath &path, const QColor &color, int feather);
    void drawAutoMaskDebugOverlay(QPainter &painter, const QRect &viewport, float scale, bool isRight);
    void startAutoMaskDebugPlayback();
    QPointF widgetToImage(const QPoint &pos, const QRect &targetRect, float scale);
    QPointF imageToWidget(const QPointF &pos, const QRect &targetRect, float scale, float disparity = 0);
    void calculateLayout(QRect &rL, QRect &rR, float &scale);
    void notifySelectionState();
    void notifyMaskState();
    void appendFreehandPoint(const QPoint &widgetPos);
    void finishFreehandDrawing();
    QVector<QPointF> simplifyFreehandPath(const QVector<QPointF> &points, double tolerance) const;
    QVector<QPointF> snapFreehandPathToEdges(const QVector<QPointF> &points) const;
    QVector<QPointF> smoothFreehandPath(const QVector<QPointF> &points) const;
    QVector<QPointF> limitFreehandPointCount(const QVector<QPointF> &points, int maxPoints) const;
    int estimateGlobalDisparity() const;
    int edgeStrengthAt(const QImage &image, int x, int y) const;
    double pointLineDistance(const QPointF &point, const QPointF &lineStart, const QPointF &lineEnd) const;

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
    QRect m_freehandViewport;
    float m_freehandScale = 1.0f;
    QVector<QPointF> m_freehandPoints;
    QVector<AutoMaskDebugFrame> m_autoMaskDebugFrames;
    QVector<MaskPoint> m_pendingAutoMaskPoints;
    QString m_pendingAutoMaskText;
    QTimer *m_autoMaskDebugTimer = nullptr;
    int m_autoMaskDebugFrameIndex = -1;
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
    bool m_freehandMode = false;
    bool m_isFreehandDrawing = false;
    QString m_imagePath;
    QString m_lastError;

    QColor m_maskColor = Qt::black;
    float m_maskOpacity = 0.6f;
    int m_featherAmount = 0;
    bool m_previewFeatherEnabled = false;
    bool m_maskPreviewEnabled = true;
    bool m_cleanPreviewEnabled = false;
    mutable QByteArray m_exportKeyLeft,m_exportKeyRight;
    mutable QImage m_exportCacheLeft,m_exportCacheRight;
    int m_padx = 0;
    int m_pady = 0;
    QColor m_bgColor = Qt::white;
    int m_interleavingSpace = 0;
    bool m_autoSave = true;
};

#endif // STEREOVIEWWIDGET_H
