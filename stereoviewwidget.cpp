#include "stereoviewwidget.h"
#include "undocommand.h"
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QPainterPath>
#include <QDebug>
#include <QWheelEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QtMath>

static QPainterPath createImageMaskPath(const QVector<MaskPoint> &pts, const QRectF &v, float scale, bool isRight, bool includeRect)
{
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    if (pts.isEmpty()) return path;
    if (includeRect) path.addRect(v);

    auto toTarget = [&](const QPointF &pos, float disp) {
        return QPointF(v.left() + (pos.x() - disp) * scale, v.top() + pos.y() * scale);
    };

    for (int i = 0; i < pts.count(); ++i) {
        const auto &p = pts[i];
        QPointF pW = toTarget(p.pos, isRight ? p.disparity : 0);
        if (i == 0) {
            path.moveTo(pW);
        } else {
            const auto &prev = pts[i - 1];
            if (prev.isCurve) {
                path.cubicTo(toTarget(prev.cp1, isRight ? prev.cp1Disparity : 0),
                             toTarget(prev.cp2, isRight ? prev.cp2Disparity : 0),
                             pW);
            } else {
                path.lineTo(pW);
            }
        }
    }

    if (pts.count() > 2) {
        const auto &last = pts.last();
        QPointF startW = toTarget(pts[0].pos, isRight ? pts[0].disparity : 0);
        if (last.isCurve) {
            path.cubicTo(toTarget(last.cp1, isRight ? last.cp1Disparity : 0),
                         toTarget(last.cp2, isRight ? last.cp2Disparity : 0),
                         startW);
        } else {
            path.lineTo(startW);
        }
    }

    path.closeSubpath();
    return path;
}

static void boxBlurAlpha(QImage &image, int radius)
{
    radius = qBound(1, radius, 200);
    const int w = image.width();
    const int h = image.height();
    if (w <= 0 || h <= 0) return;

    QVector<int> horizontal(w * h);
    for (int y = 0; y < h; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        int sum = 0;
        for (int x = -radius; x <= radius; ++x) {
            int sx = qBound(0, x, w - 1);
            sum += qAlpha(line[sx]);
        }
        for (int x = 0; x < w; ++x) {
            horizontal[y * w + x] = sum / (radius * 2 + 1);
            int removeX = qBound(0, x - radius, w - 1);
            int addX = qBound(0, x + radius + 1, w - 1);
            sum += qAlpha(line[addX]) - qAlpha(line[removeX]);
        }
    }

    for (int x = 0; x < w; ++x) {
        int sum = 0;
        for (int y = -radius; y <= radius; ++y) {
            int sy = qBound(0, y, h - 1);
            sum += horizontal[sy * w + x];
        }
        for (int y = 0; y < h; ++y) {
            QRgb *line = reinterpret_cast<QRgb*>(image.scanLine(y));
            int alpha = sum / (radius * 2 + 1);
            QColor c = QColor::fromRgba(line[x]);
            c.setAlpha(alpha);
            line[x] = c.rgba();

            int removeY = qBound(0, y - radius, h - 1);
            int addY = qBound(0, y + radius + 1, h - 1);
            sum += horizontal[addY * w + x] - horizontal[removeY * w + x];
        }
    }
}

static void drawFeatheredImagePath(QPainter &painter, const QPainterPath &path, const QColor &color, int feather)
{
    if (feather <= 0) {
        painter.fillPath(path, color);
        return;
    }

    feather = qMin(feather, 200);
    QRectF clip = painter.hasClipping()
        ? painter.clipBoundingRect()
        : QRectF(0, 0, painter.device()->width(), painter.device()->height());
    QRectF br = path.boundingRect().adjusted(-feather * 2, -feather * 2, feather * 2, feather * 2).intersected(clip);

    if (br.width() < 1 || br.height() < 1 || br.width() > 8000 || br.height() > 8000) return;

    QImage img(br.size().toSize(), QImage::Format_ARGB32);
    if (img.isNull()) return;
    img.fill(Qt::transparent);

    QPainter imgPainter(&img);
    imgPainter.setRenderHint(QPainter::Antialiasing);
    imgPainter.translate(-br.topLeft());
    imgPainter.fillPath(path, color);
    imgPainter.end();

    boxBlurAlpha(img, feather);
    painter.drawImage(br.topLeft(), img);
}

StereoViewWidget::StereoViewWidget(QWidget *parent)
    : QWidget(parent), m_panOffset(0, 0), m_isPanning(false), m_panMode(false)
{
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setMouseTracking(true);
    m_undoStack = new QUndoStack(this);
}

bool StereoViewWidget::loadImage(const QString &fileName)
{
    m_lastError = QString();
    if (m_processor.loadSideBySide(fileName)) {
        m_imagePath = fileName;
        m_panOffset = QPointF(0, 0);
        
        QFileInfo info(fileName);
        QString mskPath = info.absolutePath() + "/" + info.completeBaseName() + ".msk";
        if (QFile::exists(mskPath)) {
            QFile file(mskPath);
            if (file.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                if (!doc.isNull()) {
                    QJsonObject obj = doc.object();
                    QJsonArray pts = obj["points"].toArray();
                    m_undoStack->clear();
                    m_points.clear();
                    m_selectedIndices.clear();
                    for (int i = 0; i < pts.size(); ++i) {
                        QJsonObject pObj = pts[i].toObject();
                        MaskPoint mp;
                        mp.pos = QPointF(pObj["x"].toDouble(), pObj["y"].toDouble());
                        mp.disparity = (float)pObj["d"].toDouble();
                        if (pObj["isCurve"].toBool()) {
                            mp.isCurve = true;
                            mp.cp1 = QPointF(pObj["cp1x"].toDouble(), pObj["cp1y"].toDouble());
                            mp.cp1Disparity = (float)pObj["cp1d"].toDouble();
                            mp.cp2 = QPointF(pObj["cp2x"].toDouble(), pObj["cp2y"].toDouble());
                            mp.cp2Disparity = (float)pObj["cp2d"].toDouble();
                        }
                        m_points.append(mp);
                    }
                }
            }
        } else {
            m_undoStack->clear();
            m_points.clear();
            m_selectedIndices.clear();
        }
        m_zoom = 1.0f;
        updateAnaglyphIfActive();
        update();
        return true;
    }
    m_lastError = m_processor.lastError();
    return false;
}

bool StereoViewWidget::saveProject()
{
    if (m_imagePath.isEmpty()) return false;
    QFileInfo info(m_imagePath);
    QString mskPath = info.absolutePath() + "/" + info.completeBaseName() + ".msk";
    QJsonArray pts;
    for (const auto &p : m_points) {
        QJsonObject pObj;
        pObj["x"] = p.pos.x(); pObj["y"] = p.pos.y(); pObj["d"] = p.disparity;
        if (p.isCurve) {
            pObj["isCurve"] = true;
            pObj["cp1x"] = p.cp1.x(); pObj["cp1y"] = p.cp1.y(); pObj["cp1d"] = p.cp1Disparity;
            pObj["cp2x"] = p.cp2.x(); pObj["cp2y"] = p.cp2.y(); pObj["cp2d"] = p.cp2Disparity;
        }
        pts.append(pObj);
    }
    QJsonObject obj; 
    obj["points"] = pts;
    obj["imagePath"] = m_imagePath;
    
    // Project specific settings
    obj["maskColor"] = m_maskColor.name();
    obj["maskOpacity"] = (double)m_maskOpacity;
    obj["padx"] = m_padx;
    obj["pady"] = m_pady;
    obj["bgColor"] = m_bgColor.name();
    obj["interleavingSpace"] = m_interleavingSpace;
    obj["featherAmount"] = m_featherAmount;

    QFile file(mskPath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(obj).toJson());
    return true;
}

bool StereoViewWidget::loadProject(const QString &path)
{
    QFileInfo info(path);
    if (info.suffix().toLower() != "msk") {
        return loadImage(path);
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = tr("Could not open file: %1").arg(path);
        return false;
    }
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isNull()) {
        m_lastError = tr("Invalid JSON in mask file.");
        return false;
    }
    QJsonObject obj = doc.object();
    
    QString storedImgPath = obj["imagePath"].toString();
    bool imgLoaded = false;

    // 1. Try stored path
    if (!storedImgPath.isEmpty() && QFile::exists(storedImgPath)) {
        imgLoaded = loadImage(storedImgPath);
    }

    // 2. Try same folder with common extensions
    if (!imgLoaded) {
        QString base = info.absolutePath() + "/" + info.completeBaseName();
        QStringList extensions = {".jpg", ".jpeg", ".png", ".bmp", ".JPG", ".JPEG", ".PNG", ".BMP"};
        for (const QString &ext : extensions) {
            if (QFile::exists(base + ext)) {
                if (loadImage(base + ext)) {
                    imgLoaded = true;
                    break;
                }
            }
        }
    }

    if (!imgLoaded) {
        m_lastError = tr("Associated image not found for this mask project.");
        return false;
    }

    // Load project specific settings if they exist
    if (obj.contains("maskColor")) m_maskColor = QColor(obj["maskColor"].toString());
    if (obj.contains("maskOpacity")) m_maskOpacity = (float)obj["maskOpacity"].toDouble();
    if (obj.contains("padx")) m_padx = obj["padx"].toInt();
    if (obj.contains("pady")) m_pady = obj["pady"].toInt();
    if (obj.contains("bgColor")) m_bgColor = QColor(obj["bgColor"].toString());
    if (obj.contains("interleavingSpace")) m_interleavingSpace = obj["interleavingSpace"].toInt();
    if (obj.contains("featherAmount")) m_featherAmount = obj["featherAmount"].toInt();

    // Reload points from THIS msk file
    QJsonArray pts = obj["points"].toArray();
    m_undoStack->clear();
    m_points.clear();
    m_selectedIndices.clear();
    for (int i = 0; i < pts.size(); ++i) {
        QJsonObject pObj = pts[i].toObject();
        MaskPoint mp;
        mp.pos = QPointF(pObj["x"].toDouble(), pObj["y"].toDouble());
        mp.disparity = (float)pObj["d"].toDouble();
        if (pObj["isCurve"].toBool()) {
            mp.isCurve = true;
            mp.cp1 = QPointF(pObj["cp1x"].toDouble(), pObj["cp1y"].toDouble());
            mp.cp1Disparity = (float)pObj["cp1d"].toDouble();
            mp.cp2 = QPointF(pObj["cp2x"].toDouble(), pObj["cp2y"].toDouble());
            mp.cp2Disparity = (float)pObj["cp2d"].toDouble();
        }
        m_points.append(mp);
    }
    m_zoom = 1.0f;
    m_panOffset = QPointF(0, 0);
    update();
    return true;
}

void StereoViewWidget::drawFeatheredPath(QPainter &painter, const QPainterPath &path, const QColor &color, int feather)
{
    drawFeatheredImagePath(painter, path, color, feather);
}

bool StereoViewWidget::saveImage(const QString &fileName, const QColor &maskColor, float opacity, int padx, int pady, const QColor &bgColor, int interleavingSpace, std::function<void(int)> progressCallback)
{
    return saveImageData(fileName, m_processor.leftImage(), m_processor.rightImage(), m_points, maskColor, opacity,
                         padx, pady, bgColor, interleavingSpace, m_featherAmount, progressCallback);
}

bool StereoViewWidget::saveImageData(const QString &fileName, const QImage &leftImage, const QImage &rightImage, const QVector<MaskPoint> &points, const QColor &maskColor, float opacity, int padx, int pady, const QColor &bgColor, int interleavingSpace, int featherAmount, std::function<void(int)> progressCallback)
{
    if (leftImage.isNull() || rightImage.isNull() || leftImage.size() != rightImage.size()) return false;
    int iw = leftImage.width();
    int ih = leftImage.height();
    
    int totalW = (iw * 2) + (padx * 2) + interleavingSpace;
    int totalH = ih + (pady * 2);
    
    QImage result(totalW, totalH, QImage::Format_ARGB32);
    if (result.isNull()) return false;
    result.fill(bgColor);
    
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);

    if (progressCallback) progressCallback(10);

    auto drawEye = [&](const QImage &img, int offsetX, bool isRight) {
        QRect target(offsetX, pady, iw, ih);
        painter.drawImage(target, img);
        
        if (!points.isEmpty()) {
            QImage maskImg(target.size(), QImage::Format_ARGB32);
            maskImg.fill(Qt::transparent);
            
            QPainter maskPainter(&maskImg);
            maskPainter.setRenderHint(QPainter::Antialiasing);
            
            QColor fill = maskColor;
            fill.setAlphaF(opacity);
            maskPainter.fillRect(maskImg.rect(), fill);
            
            QPainterPath polyPath = createImageMaskPath(points, QRectF(0, 0, iw, ih), 1.0f, isRight, false);
            
            maskPainter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
            drawFeatheredImagePath(maskPainter, polyPath, Qt::black, featherAmount);
            maskPainter.end();
            
            painter.drawImage(target.topLeft(), maskImg);
        }
    };

    drawEye(leftImage, padx, false);
    if (progressCallback) progressCallback(50);
    
    drawEye(rightImage, padx + iw + interleavingSpace, true);
    if (progressCallback) progressCallback(90);
    
    painter.end();
    if (!result.save(fileName)) return false;
    if (progressCallback) progressCallback(100);
    return true;
}

void StereoViewWidget::setAnaglyphMode(bool enabled) { 
    m_anaglyphMode = enabled; 
    updateAnaglyphIfActive();
    update(); 
}
void StereoViewWidget::setSwapSides(bool enabled) { 
    m_swapSides = enabled; 
    updateAnaglyphIfActive();
    update(); 
}

void StereoViewWidget::updateAnaglyphIfActive()
{
    if (m_anaglyphMode && m_processor.isValid()) {
        m_processor.createAnaglyph(m_swapSides, &m_points, m_maskColor, m_maskOpacity);
    }
}

void StereoViewWidget::setZoom(float zoom) { m_zoom = qBound(0.1f, zoom, 20.0f); update(); }

bool StereoViewWidget::hasCurveSelection() const
{
    for (int idx : m_selectedIndices) {
        if (idx >= 0 && idx < m_points.count() && m_points[idx].isCurve) {
            return true;
        }
    }
    return false;
}

void StereoViewWidget::notifySelectionState()
{
    emit curveSelectionChanged(hasCurveSelection());
}

void StereoViewWidget::addPoint(const QPointF &pos, float disparity, int index)
{
    m_undoStack->push(new AddPointCommand(this, {pos, disparity}, index));
    updateAnaglyphIfActive();
    notifySelectionState();
    if (m_autoSave) saveProject();
}

void StereoViewWidget::deleteSelectedPoints()
{
    if (m_selectedIndices.isEmpty()) return;
    QList<int> sortedIndices = m_selectedIndices;
    std::sort(sortedIndices.begin(), sortedIndices.end(), std::greater<int>());
    QVector<MaskPoint> removedPoints;
    for (int idx : sortedIndices) removedPoints.append(m_points[idx]);
    m_undoStack->push(new DeletePointsCommand(this, sortedIndices, removedPoints));
    m_selectedIndices.clear(); m_selectedPointIndex = -1;
    updateAnaglyphIfActive();
    notifySelectionState();
    if (m_autoSave) saveProject();
}

void StereoViewWidget::updatePointInternal(int index, const MaskPoint &p) { if (index >= 0 && index < m_points.count()) { m_points[index] = p; updateAnaglyphIfActive(); update(); } }
void StereoViewWidget::updatePointsInternal(const QList<int> &indices, const QVector<MaskPoint> &ps) { for (int i = 0; i < indices.count(); ++i) { if (indices[i] >= 0 && indices[i] < m_points.count()) m_points[indices[i]] = ps[i]; } updateAnaglyphIfActive(); update(); }
void StereoViewWidget::addPointInternal(const MaskPoint &p, int index) { if (index >= 0 && index <= m_points.count()) m_points.insert(index, p); else m_points.append(p); updateAnaglyphIfActive(); update(); }
void StereoViewWidget::removePointInternal(int index) { if (!m_points.isEmpty()) { if (index >= 0 && index < m_points.count()) m_points.removeAt(index); else m_points.removeLast(); } updateAnaglyphIfActive(); update(); }
void StereoViewWidget::insertPointsInternal(const QList<int> &indices, const QVector<MaskPoint> &ps) { for (int i = 0; i < indices.count(); ++i) m_points.insert(indices[i], ps[i]); updateAnaglyphIfActive(); update(); }
void StereoViewWidget::removePointsInternal(const QList<int> &indices) { for (int idx : indices) { if (idx >= 0 && idx < m_points.count()) m_points.removeAt(idx); } updateAnaglyphIfActive(); update(); }

void StereoViewWidget::clearPoints() { m_points.clear(); m_selectedIndices.clear(); m_undoStack->clear(); updateAnaglyphIfActive(); notifySelectionState(); update(); if (m_autoSave) saveProject(); }

void StereoViewWidget::alignSelectedPoints(AlignSide side)
{
    if (m_selectedIndices.isEmpty()) return;
    int pivotIdx = (m_selectedPointIndex != -1) ? m_selectedPointIndex : m_selectedIndices.first();
    float targetVal = (side == AlignLeft || side == AlignRight) ? m_points[pivotIdx].pos.x() : m_points[pivotIdx].pos.y();
    float targetDisp = m_points[pivotIdx].disparity;
    
    QVector<MaskPoint> oldPs, newPs;
    for (int idx : m_selectedIndices) {
        oldPs << m_points[idx];
        MaskPoint p = m_points[idx];
        if (side == AlignLeft || side == AlignRight) {
            p.pos.setX(targetVal);
            if (p.isCurve) { p.cp1.setX(targetVal); p.cp2.setX(targetVal); }
        } else if (side == AlignTop || side == AlignBottom) {
            p.pos.setY(targetVal);
            if (p.isCurve) { p.cp1.setY(targetVal); p.cp2.setY(targetVal); }
        } else if (side == AlignDepth) {
            p.disparity = targetDisp;
            if (p.isCurve) { p.cp1Disparity = targetDisp; p.cp2Disparity = targetDisp; }
        }
        newPs << p;
    }
    m_undoStack->push(new BatchMoveCommand(this, m_selectedIndices, oldPs, newPs));
    updateAnaglyphIfActive();
    notifySelectionState();
    if (m_autoSave) saveProject();
}

void StereoViewWidget::calculateLayout(QRect &rL, QRect &rR, float &scale)
{
    if (!m_processor.isValid()) return;
    int iw = m_processor.leftImage().width();
    int ih = m_processor.leftImage().height();
    
    if (m_anaglyphMode) {
        float s = qMin((float)(width() - 40) / iw, (float)(height() - 40) / ih);
        rL = QRect((width() - iw*s)/2, (height() - ih*s)/2, iw*s, ih*s);
        rR = rL;
        scale = s * m_zoom;
    } else {
        float s = qMin((float)(width()/2.0 - 40) / iw, (float)(height() - 40) / ih);
        rL = QRect((width()/4.0 - (iw*s)/2.0), (height() - (ih*s))/2.0, iw*s, ih*s);
        rR = QRect((3.0*width()/4.0 - (iw*s)/2.0), (height() - (ih*s))/2.0, iw*s, ih*s);
        scale = s * m_zoom;
    }
}

QPointF StereoViewWidget::imageToWidget(const QPointF &pos, const QRect &v, float scale, float disparity)
{
    float sw = m_processor.leftImage().width() * scale;
    float sh = m_processor.leftImage().height() * scale;
    float left = v.center().x() - sw/2.0 + m_panOffset.x()*scale;
    float top = v.center().y() - sh/2.0 + m_panOffset.y()*scale;
    return QPointF(left + (pos.x() - disparity) * scale, top + pos.y() * scale);
}

QPointF StereoViewWidget::widgetToImage(const QPoint &pos, const QRect &v, float scale) 
{
    float sw = m_processor.leftImage().width() * scale;
    float sh = m_processor.leftImage().height() * scale;
    float left = v.center().x() - sw/2.0 + m_panOffset.x()*scale;
    float top = v.center().y() - sh/2.0 + m_panOffset.y()*scale;
    return QPointF((pos.x() - left) / scale, (pos.y() - top) / scale);
}

QPainterPath StereoViewWidget::createMaskPath(const QVector<MaskPoint> &pts, const QRectF &v, float scale, bool isRight, bool includeRect, bool ignorePan)
{
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    if (pts.isEmpty()) return path;
    if (includeRect) path.addRect(v);

    auto toWidget = [&](const QPointF &pos, float disp) {
        if (ignorePan) {
            return QPointF(v.left() + (pos.x() - disp) * scale, v.top() + pos.y() * scale);
        } else {
            return imageToWidget(pos, v.toRect(), scale, disp);
        }
    };

    for (int i = 0; i < pts.count(); ++i) {
        const auto &p = pts[i];
        QPointF pW = toWidget(p.pos, isRight ? p.disparity : 0);
        
        if (i == 0) {
            path.moveTo(pW);
        } else {
            const auto &prev = pts[i-1];
            if (prev.isCurve) {
                QPointF cp1W = toWidget(prev.cp1, isRight ? prev.cp1Disparity : 0);
                QPointF cp2W = toWidget(prev.cp2, isRight ? prev.cp2Disparity : 0);
                path.cubicTo(cp1W, cp2W, pW);
            } else {
                path.lineTo(pW);
            }
        }
    }
    
    // Close path with last segment
    if (pts.count() > 2) {
        const auto &last = pts.last();
        QPointF startW = toWidget(pts[0].pos, isRight ? pts[0].disparity : 0);
        if (last.isCurve) {
            QPointF cp1W = toWidget(last.cp1, isRight ? last.cp1Disparity : 0);
            QPointF cp2W = toWidget(last.cp2, isRight ? last.cp2Disparity : 0);
            path.cubicTo(cp1W, cp2W, startW);
        } else {
            path.lineTo(startW);
        }
    }

    path.closeSubpath();
    return path;
}

void StereoViewWidget::toggleCurve()
{
    if (m_selectedIndices.isEmpty()) return;
    QVector<MaskPoint> oldPs, newPs;
    for (int idx : m_selectedIndices) {
        oldPs << m_points[idx];
        MaskPoint p = m_points[idx];
        p.isCurve = !p.isCurve;
        if (p.isCurve) {
            // Initialize handles between this point and the next
            int nextIdx = (idx + 1) % m_points.count();
            p.cp1 = p.pos + (m_points[nextIdx].pos - p.pos) * 0.33f;
            p.cp1Disparity = p.disparity + (m_points[nextIdx].disparity - p.disparity) * 0.33f;
            p.cp2 = p.pos + (m_points[nextIdx].pos - p.pos) * 0.66f;
            p.cp2Disparity = p.disparity + (m_points[nextIdx].disparity - p.disparity) * 0.66f;
        }
        newPs << p;
    }
    m_undoStack->push(new BatchMoveCommand(this, m_selectedIndices, oldPs, newPs));
    updateAnaglyphIfActive();
    notifySelectionState();
    if (m_autoSave) saveProject();
}

void StereoViewWidget::rotateSelectedPoints(float angleDegrees, RotationAxis axis)
{
    QList<int> indices = m_selectedIndices;
    if (indices.isEmpty()) for (int i = 0; i < m_points.count(); ++i) indices << i;
    if (indices.isEmpty()) return;

    // Calculate 3D center (X, Y, Disparity)
    QPointF centerPos(0, 0);
    float centerDisp = 0;
    for (int idx : indices) {
        centerPos += m_points[idx].pos;
        centerDisp += m_points[idx].disparity;
    }
    centerPos /= (float)indices.count();
    centerDisp /= (float)indices.count();

    float rad = qDegreesToRadians(angleDegrees);
    float cosA = qCos(rad);
    float sinA = qSin(rad);

    QVector<MaskPoint> oldPs, newPs;
    for (int idx : indices) {
        oldPs << m_points[idx];
        MaskPoint p = m_points[idx];

        auto rotate3D = [&](QPointF &pos, float &disp) {
            float dx = pos.x() - centerPos.x();
            float dy = pos.y() - centerPos.y();
            float dz = disp - centerDisp;

            if (axis == AxisX) {
                float newY = dy * cosA - dz * sinA;
                float newZ = dy * sinA + dz * cosA;
                pos.setY(centerPos.y() + newY);
                disp = centerDisp + newZ;
            } else if (axis == AxisY) {
                float newX = dx * cosA + dz * sinA;
                float newZ = -dx * sinA + dz * cosA;
                pos.setX(centerPos.x() + newX);
                disp = centerDisp + newZ;
            } else if (axis == AxisZ) {
                float newX = dx * cosA - dy * sinA;
                float newY = dx * sinA + dy * cosA;
                pos.setX(centerPos.x() + newX);
                pos.setY(centerPos.y() + newY);
            }
        };

        rotate3D(p.pos, p.disparity);
        if (p.isCurve) {
            rotate3D(p.cp1, p.cp1Disparity);
            rotate3D(p.cp2, p.cp2Disparity);
        }
        newPs << p;
    }
    m_undoStack->push(new BatchMoveCommand(this, indices, oldPs, newPs));
    updateAnaglyphIfActive();
    if (m_autoSave) saveProject();
}

void StereoViewWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (!m_processor.isValid()) return;

    QRect vL, vR; float scale; calculateLayout(vL, vR, scale);

    auto drawInViewport = [&](const QRect &v, const QImage &img, bool isRight) {
        painter.save();
        painter.setClipRect(v);
        float sw = img.width() * scale;
        float sh = img.height() * scale;
        QRectF target(v.center().x() - sw/2.0 + m_panOffset.x()*scale, v.center().y() - sh/2.0 + m_panOffset.y()*scale, sw, sh);
        painter.drawImage(target, img);
        
        if (!m_points.isEmpty()) {
            if (!m_anaglyphMode) {
                QRectF visible = target.intersected(QRectF(v));
                if (!visible.isEmpty()) {
                    QRect visibleRect = visible.toAlignedRect();
                    QRectF localVisible = visible.translated(-target.topLeft());

                    QImage maskImg(visibleRect.size(), QImage::Format_ARGB32);
                    if (!maskImg.isNull()) {
                        maskImg.fill(Qt::transparent);

                        QPainter maskPainter(&maskImg);
                        maskPainter.setRenderHint(QPainter::Antialiasing);

                        QColor fill = m_maskColor;
                        fill.setAlphaF(m_maskOpacity);
                        maskPainter.fillRect(maskImg.rect(), fill);

                        QPainterPath polyPath = createImageMaskPath(m_points, QRectF(0, 0, sw, sh), scale, isRight, false);

                        maskPainter.translate(-localVisible.topLeft());
                        maskPainter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
                        drawFeatheredPath(maskPainter, polyPath, Qt::black, m_featherAmount * scale);
                        maskPainter.end();

                        painter.drawImage(visibleRect.topLeft(), maskImg);
                    }
                }
            } else {
                QPainterPath path = createMaskPath(m_points, v, scale, isRight, false);
                painter.setPen(QPen(Qt::white, 1, Qt::DotLine));
                painter.drawPath(path);
            }

            for (int i=0; i<m_points.count(); ++i) {
                const auto &p = m_points[i];
                QPointF pW = imageToWidget(p.pos, v, scale, isRight ? p.disparity : 0);
                
                // Draw Bezier handles if curve
                if (p.isCurve) {
                    int nextIdx = (i + 1) % m_points.count();
                    QPointF nextW = imageToWidget(m_points[nextIdx].pos, v, scale, isRight ? m_points[nextIdx].disparity : 0);
                    QPointF cp1W = imageToWidget(p.cp1, v, scale, isRight ? p.cp1Disparity : 0);
                    QPointF cp2W = imageToWidget(p.cp2, v, scale, isRight ? p.cp2Disparity : 0);
                    
                    painter.setPen(QPen(Qt::gray, 1, Qt::DashLine));
                    painter.drawLine(pW, cp1W);
                    painter.drawLine(nextW, cp2W);
                    
                    painter.setBrush(Qt::green);
                    painter.setPen(m_selectedPointIndex == i && m_hitType == HitCP1 ? QPen(Qt::cyan, 2) : QPen(Qt::white, 1));
                    painter.drawRect(QRectF(cp1W.x()-3, cp1W.y()-3, 6, 6));
                    
                    painter.setPen(m_selectedPointIndex == i && m_hitType == HitCP2 ? QPen(Qt::cyan, 2) : QPen(Qt::white, 1));
                    painter.drawRect(QRectF(cp2W.x()-3, cp2W.y()-3, 6, 6));
                }

                painter.setBrush(m_selectedIndices.contains(i) ? Qt::yellow : Qt::red);
                painter.setPen(i == m_selectedPointIndex && m_hitType == HitPoint ? QPen(Qt::cyan, 2) : QPen(Qt::white, 1));
                painter.drawEllipse(pW, 5, 5);
            }
        }
        painter.restore();
    };

    if (m_anaglyphMode) {
        drawInViewport(vL, m_processor.anaglyphImage(), false);
    } else {
        drawInViewport(vL, m_swapSides ? m_processor.rightImage() : m_processor.leftImage(), m_swapSides);
        drawInViewport(vR, m_swapSides ? m_processor.leftImage() : m_processor.rightImage(), !m_swapSides);
    }
    
    if (m_isSelecting && !m_selectionRect.isNull()) { 
        painter.setPen(QPen(Qt::white, 1, Qt::DashLine)); painter.setBrush(QColor(255, 255, 255, 50)); 
        painter.drawRect(m_selectionRect); 
    }
}

void StereoViewWidget::mousePressEvent(QMouseEvent *event)
{
    if (!m_processor.isValid()) return;
    m_lastMousePos = event->pos(); m_mouseDragStart = event->pos();
    QRect vL, vR; float scale; calculateLayout(vL, vR, scale);

    if (m_panMode || event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && (event->modifiers() & Qt::AltModifier))) {
        m_isPanning = true; setCursor(Qt::SizeAllCursor); return;
    }

    int hitIdx = -1;
    m_hitType = HitNone;
    bool inRight = vR.contains(event->pos()) && !m_anaglyphMode;
    QRect targetV = inRight ? vR : vL;
    
    if (targetV.contains(event->pos())) {
        bool isRightEye = inRight ? !m_swapSides : m_swapSides;
        for (int i=0; i<m_points.count(); ++i) {
            const auto &p = m_points[i];
            QPointF pW = imageToWidget(p.pos, targetV, scale, isRightEye ? p.disparity : 0);
            if (QLineF(event->pos(), pW).length() < 10) { hitIdx = i; m_hitType = HitPoint; break; }
            
            if (p.isCurve) {
                QPointF cp1W = imageToWidget(p.cp1, targetV, scale, isRightEye ? p.cp1Disparity : 0);
                if (QLineF(event->pos(), cp1W).length() < 10) { hitIdx = i; m_hitType = HitCP1; break; }
                QPointF cp2W = imageToWidget(p.cp2, targetV, scale, isRightEye ? p.cp2Disparity : 0);
                if (QLineF(event->pos(), cp2W).length() < 10) { hitIdx = i; m_hitType = HitCP2; break; }
            }
        }
    }

    if (hitIdx != -1) {
        if (m_hitType == HitPoint) {
            if (event->modifiers() & Qt::ControlModifier) {
                if (m_selectedIndices.contains(hitIdx)) m_selectedIndices.removeOne(hitIdx); else m_selectedIndices.append(hitIdx);
            } else if (!m_selectedIndices.contains(hitIdx)) {
                m_selectedIndices.clear(); m_selectedIndices.append(hitIdx);
            }
        } else {
            m_selectedIndices.clear(); m_selectedIndices.append(hitIdx);
        }
        
        m_selectedPointIndex = hitIdx;
        m_preMovePoints.clear(); for (int idx : m_selectedIndices) m_preMovePoints << m_points[idx];
        setCursor(Qt::ClosedHandCursor); 
        float disp = (m_hitType == HitCP1) ? m_points[hitIdx].cp1Disparity : (m_hitType == HitCP2 ? m_points[hitIdx].cp2Disparity : m_points[hitIdx].disparity);
        emit selectionChanged(disp);
        notifySelectionState();
    } else {
        if (event->button() == Qt::LeftButton) {
            if (vL.contains(event->pos()) || vR.contains(event->pos())) {
                m_selectedIndices.clear(); m_selectedPointIndex = -1;
                m_isSelecting = true; m_selectionRect = QRect(event->pos(), QSize(1, 1));
                notifySelectionState();
            } else { m_isPanning = true; setCursor(Qt::SizeAllCursor); }
        } else if (event->button() == Qt::RightButton && targetV.contains(event->pos())) {
            QPointF imgPos = widgetToImage(event->pos(), targetV, scale);
            bool isRightEye = inRight ? !m_swapSides : m_swapSides;
            float d = (m_points.isEmpty() ? 0 : m_points[0].disparity);
            if (isRightEye) imgPos.setX(imgPos.x() + d);
            
            // Check if we clicked on an edge to insert
            int insertIdx = -1;
            float minEdgeDist = 10.0f; // px threshold
            for (int i=0; i<m_points.count(); ++i) {
                int nextIdx = (i + 1) % m_points.count();
                const auto &p1 = m_points[i];
                const auto &p2 = m_points[nextIdx];
                QPointF p1W = imageToWidget(p1.pos, targetV, scale, isRightEye ? p1.disparity : 0);
                QPointF p2W = imageToWidget(p2.pos, targetV, scale, isRightEye ? p2.disparity : 0);
                
                if (p1.isCurve) {
                    // Approximate curve distance by sampling
                    QPointF cp1W = imageToWidget(p1.cp1, targetV, scale, isRightEye ? p1.cp1Disparity : 0);
                    QPointF cp2W = imageToWidget(p1.cp2, targetV, scale, isRightEye ? p1.cp2Disparity : 0);
                    for (float t = 0.1f; t < 1.0f; t += 0.1f) {
                        float it = 1.0f - t;
                        QPointF b = it*it*it*p1W + 3*it*it*t*cp1W + 3*it*t*t*cp2W + t*t*t*p2W;
                        if (QLineF(event->pos(), b).length() < minEdgeDist) { insertIdx = nextIdx; break; }
                    }
                } else {
                    // Linear distance
                    QLineF line(p1W, p2W);
                    QPointF p3 = event->pos();
                    float l2 = line.length() * line.length();
                    if (l2 != 0) {
                        float t = qBound(0.0f, (float)QPointF::dotProduct(p3 - p1W, p2W - p1W) / l2, 1.0f);
                        QPointF projection = p1W + t * (p2W - p1W);
                        if (QLineF(p3, projection).length() < minEdgeDist) insertIdx = nextIdx;
                    }
                }
                if (insertIdx != -1) break;
            }

            addPoint(imgPos, d, insertIdx);
            int newIdx = (insertIdx == -1) ? m_points.count()-1 : insertIdx;
            m_selectedPointIndex = newIdx; m_selectedIndices.clear(); m_selectedIndices << m_selectedPointIndex; m_hitType = HitPoint;
            m_preMovePoints.clear(); m_preMovePoints << m_points[newIdx];
            notifySelectionState();
        }
    }
    update();
}

void StereoViewWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isPanning) {
        QPoint delta = event->pos() - m_lastMousePos;
        float scale; QRect vL, vR; calculateLayout(vL, vR, scale);
        m_panOffset += QPointF(delta.x() / scale, delta.y() / scale);
        m_lastMousePos = event->pos(); update(); return;
    }
    if (m_isSelecting) { m_selectionRect.setBottomRight(event->pos()); update(); return; }
    
    if (m_selectedPointIndex != -1 && (event->buttons() & Qt::LeftButton)) {
        float scale; QRect vL, vR; calculateLayout(vL, vR, scale);
        QPointF totalDelta((event->pos().x() - m_mouseDragStart.x()) / scale, (event->pos().y() - m_mouseDragStart.y()) / scale);

        if (m_rotationMode != AxisNone) {
            float dx = (float)(event->pos().x() - m_lastMousePos.x());
            float dy = (float)(event->pos().y() - m_lastMousePos.y());
            if (m_lockHorizontal) dx = 0; if (m_lockVertical) dy = 0;
            
            float angle = 0;
            if (m_rotationMode == AxisX) angle = -dy; // Vertical drag tilts around X
            else if (m_rotationMode == AxisY) angle = dx; // Horizontal drag tilts around Y
            else if (m_rotationMode == AxisZ) angle = dx + dy; // Combined drag rolls around Z
            
            if (angle != 0) {
                QPointF centerPos(0, 0);
                float centerDisp = 0;
                for (int idx : m_selectedIndices) {
                    centerPos += m_points[idx].pos;
                    centerDisp += m_points[idx].disparity;
                }
                centerPos /= (float)m_selectedIndices.count();
                centerDisp /= (float)m_selectedIndices.count();

                float rad = qDegreesToRadians(angle);
                float cosA = qCos(rad);
                float sinA = qSin(rad);

                auto rotate3D = [&](QPointF &pos, float &disp) {
                    float x = pos.x() - centerPos.x();
                    float y = pos.y() - centerPos.y();
                    float z = disp - centerDisp;

                    if (m_rotationMode == AxisX) {
                        pos.setY(centerPos.y() + y * cosA - z * sinA);
                        disp = centerDisp + y * sinA + z * cosA;
                    } else if (m_rotationMode == AxisY) {
                        pos.setX(centerPos.x() + x * cosA + z * sinA);
                        disp = centerDisp - x * sinA + z * cosA;
                    } else if (m_rotationMode == AxisZ) {
                        pos.setX(centerPos.x() + x * cosA - y * sinA);
                        pos.setY(centerPos.y() + x * sinA + y * cosA);
                    }
                };

                for (int idx : m_selectedIndices) {
                    rotate3D(m_points[idx].pos, m_points[idx].disparity);
                    if (m_points[idx].isCurve) {
                        rotate3D(m_points[idx].cp1, m_points[idx].cp1Disparity);
                        rotate3D(m_points[idx].cp2, m_points[idx].cp2Disparity);
                    }
                }
                updateAnaglyphIfActive();
            }
        } else if (event->modifiers() & Qt::ShiftModifier) {
            float dx = (float)(event->pos().x() - m_lastMousePos.x()) / scale;
            if (m_hitType == HitCP1) m_points[m_selectedPointIndex].cp1Disparity += dx;
            else if (m_hitType == HitCP2) m_points[m_selectedPointIndex].cp2Disparity += dx;
            else {
                for (int idx : m_selectedIndices) m_points[idx].disparity += dx;
            }
            float disp = (m_hitType == HitCP1) ? m_points[m_selectedPointIndex].cp1Disparity : (m_hitType == HitCP2 ? m_points[m_selectedPointIndex].cp2Disparity : m_points[m_selectedPointIndex].disparity);
            emit selectionChanged(disp);
        } else {
            if (m_lockHorizontal) totalDelta.setX(0); if (m_lockVertical) totalDelta.setY(0);
            if (m_hitType == HitCP1) m_points[m_selectedPointIndex].cp1 = m_preMovePoints[0].cp1 + totalDelta;
            else if (m_hitType == HitCP2) m_points[m_selectedPointIndex].cp2 = m_preMovePoints[0].cp2 + totalDelta;
            else {
                if (m_selectedIndices.count() == 1) {
                    bool inRight = !m_anaglyphMode && vR.contains(m_mouseDragStart);
                    float dragDisp = inRight ? (!m_swapSides ? m_preMovePoints[0].disparity : 0) : (m_swapSides ? m_preMovePoints[0].disparity : 0);
                    QPointF visualPos = (m_preMovePoints[0].pos - QPointF(dragDisp, 0)) + totalDelta;
                    float snap = 10.0f / scale;
                    for (int i=0; i<m_points.count(); ++i) {
                        if (i == m_selectedPointIndex) continue;
                        float tx = inRight ? (m_points[i].pos.x() - (!m_swapSides ? m_points[i].disparity : 0)) : (m_points[i].pos.x() - (m_swapSides ? m_points[i].disparity : 0));
                        if (m_snapEnabled) {
                            if (qAbs(visualPos.x() - tx) < snap) visualPos.setX(tx);
                            if (qAbs(visualPos.y() - m_points[i].pos.y()) < snap) visualPos.setY(m_points[i].pos.y());
                        }
                    }
                    m_points[m_selectedPointIndex].pos = visualPos + QPointF(dragDisp, 0);
                } else {
                    for (int i=0; i<m_selectedIndices.count(); ++i) m_points[m_selectedIndices[i]].pos = m_preMovePoints[i].pos + totalDelta;
                }
            }
        }
        m_lastMousePos = event->pos(); update();
    }
}

void StereoViewWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_isPanning) { m_isPanning = false; setCursor(m_panMode ? Qt::OpenHandCursor : Qt::ArrowCursor); return; }
    if (m_isSelecting) {
        float scale; QRect vL, vR; calculateLayout(vL, vR, scale);
        QRect normRect = m_selectionRect.normalized();
        for (int i = 0; i < m_points.count(); ++i) {
            QPointF pL = imageToWidget(m_points[i].pos, vL, scale, m_swapSides ? m_points[i].disparity : 0);
            QPointF pR = imageToWidget(m_points[i].pos, vR, scale, !m_swapSides ? m_points[i].disparity : 0);
            if (normRect.contains(pL.toPoint()) || normRect.contains(pR.toPoint())) {
                if (!m_selectedIndices.contains(i)) m_selectedIndices << i;
            }
        }
        if (!m_selectedIndices.isEmpty()) emit selectionChanged(m_points[m_selectedIndices.last()].disparity);
        m_isSelecting = false; m_selectionRect = QRect();
        notifySelectionState();
    } else if (m_selectedPointIndex != -1) {
        bool moved = false; QVector<MaskPoint> newPs;
        for (int i = 0; i < m_selectedIndices.count(); ++i) {
            newPs << m_points[m_selectedIndices[i]];
            const auto &p = m_points[m_selectedIndices[i]];
            const auto &pre = m_preMovePoints[i];
            if (p.pos != pre.pos || p.disparity != pre.disparity || p.cp1 != pre.cp1 || p.cp1Disparity != pre.cp1Disparity || p.cp2 != pre.cp2 || p.cp2Disparity != pre.cp2Disparity || p.isCurve != pre.isCurve) moved = true;
        }
        if (moved) {
            m_undoStack->push(new BatchMoveCommand(this, m_selectedIndices, m_preMovePoints, newPs));
            if (m_autoSave) saveProject();
        }
    }
    m_selectedPointIndex = -1; m_hitType = HitNone; notifySelectionState(); setCursor(m_panMode ? Qt::OpenHandCursor : Qt::ArrowCursor); update();
}

void StereoViewWidget::wheelEvent(QWheelEvent *event) 
{ 
    QRect vL, vR; float oldScale; calculateLayout(vL, vR, oldScale);
    QPoint pos = event->position().toPoint();
    QRect targetV = vL.contains(pos) ? vL : (vR.contains(pos) ? vR : QRect());
    if (targetV.isNull()) {
        setZoom(m_zoom * (event->angleDelta().y() > 0 ? 1.1f : 0.9f));
        return;
    }

    QPointF imgPos = widgetToImage(pos, targetV, oldScale);
    float factor = (event->angleDelta().y() > 0 ? 1.1f : 0.9f);
    m_zoom = qBound(0.1f, m_zoom * factor, 20.0f);
    
    float newScale; calculateLayout(vL, vR, newScale);
    float iw = m_processor.leftImage().width();
    float ih = m_processor.leftImage().height();
    m_panOffset.setX((pos.x() - targetV.center().x()) / newScale + iw/2.0 - imgPos.x());
    m_panOffset.setY((pos.y() - targetV.center().y()) / newScale + ih/2.0 - imgPos.y());
    update();
}

void StereoViewWidget::setSelectedPointDisparity(int disparity) 
{ 
    if (m_selectedIndices.isEmpty()) return;
    QVector<MaskPoint> oldPs, newPs;
    for (int idx : m_selectedIndices) { oldPs << m_points[idx]; MaskPoint p = m_points[idx]; p.disparity = (float)disparity; newPs << p; }
    m_undoStack->push(new BatchMoveCommand(this, m_selectedIndices, oldPs, newPs));
    if (m_autoSave) saveProject();
}

void StereoViewWidget::transformSelectedPoints(float scaleX, float scaleY, float dx, float dy)
{
    QList<int> indices = m_selectedIndices; 
    if (indices.isEmpty()) for (int i = 0; i < m_points.count(); ++i) indices << i;
    if (indices.isEmpty()) return;

    QPointF center(0, 0); 
    float totalDisp = 0;
    for (int idx : indices) {
        center += m_points[idx].pos;
        totalDisp += m_points[idx].disparity;
    }
    center /= (float)indices.count();
    float avgDisp = totalDisp / (float)indices.count();

    QVector<MaskPoint> oldPs, newPs;
    for (int idx : indices) {
        oldPs << m_points[idx];
        MaskPoint p = m_points[idx];
        
        // Transform main position
        p.pos = center + QPointF((p.pos.x() - center.x()) * scaleX, (p.pos.y() - center.y()) * scaleY) + QPointF(dx, dy);
        
        // Transform handles if segment is a curve
        if (p.isCurve) {
            p.cp1 = center + QPointF((p.cp1.x() - center.x()) * scaleX, (p.cp1.y() - center.y()) * scaleY) + QPointF(dx, dy);
            p.cp2 = center + QPointF((p.cp2.x() - center.x()) * scaleX, (p.cp2.y() - center.y()) * scaleY) + QPointF(dx, dy);
        }
        newPs << p;
    }
    m_undoStack->push(new BatchMoveCommand(this, indices, oldPs, newPs));
    if (m_autoSave) saveProject();
}
