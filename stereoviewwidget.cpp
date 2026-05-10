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
        if (!loadProject(fileName)) {
            m_undoStack->clear();
            m_points.clear();
            m_selectedIndices.clear();
            m_zoom = 1.0f;
        }
        update();
        return true;
    }
    m_lastError = m_processor.lastError();
    return false;
}

bool StereoViewWidget::loadProject(const QString &imageName)
{
    QFileInfo info(imageName);
    QString mskPath = info.absolutePath() + "/" + info.completeBaseName() + ".msk";
    QFile file(mskPath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isNull()) return false;
    QJsonObject obj = doc.object();
    QJsonArray pts = obj["points"].toArray();
    m_undoStack->clear();
    m_points.clear();
    m_selectedIndices.clear();
    for (int i = 0; i < pts.size(); ++i) {
        QJsonObject pObj = pts[i].toObject();
        m_points.append({QPointF(pObj["x"].toDouble(), pObj["y"].toDouble()), (float)pObj["d"].toDouble()});
    }
    m_zoom = 1.0f;
    m_panOffset = QPointF(0, 0);
    return true;
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
        pts.append(pObj);
    }
    QJsonObject obj; obj["points"] = pts;
    QFile file(mskPath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(obj).toJson());
    return true;
}

void StereoViewWidget::saveImage(const QString &fileName, const QColor &maskColor, float opacity, int padx, int pady, const QColor &bgColor)
{
    if (!m_processor.isValid()) return;
    int iw = m_processor.leftImage().width();
    int ih = m_processor.leftImage().height();
    
    // Total canvas: 2 * image_width + 2 * padx, 1 * image_height + 2 * pady
    int totalW = (iw * 2) + (padx * 2);
    int totalH = ih + (pady * 2);
    
    QImage result(totalW, totalH, QImage::Format_RGB32);
    result.fill(bgColor);
    
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);

    auto drawEye = [&](const QImage &img, int offsetX, bool isRight) {
        QRect target(offsetX + padx, pady, iw, ih);
        painter.drawImage(target, img);
        
        if (!m_points.isEmpty()) {
            QPolygonF poly;
            for (const auto &pt : m_points) {
                float disp = isRight ? pt.disparity : 0;
                poly << QPointF(target.left() + (pt.pos.x() - disp), target.top() + pt.pos.y());
            }
            
            QPainterPath path;
            path.addRect(target);
            path.addPolygon(poly);
            path.closeSubpath();
            
            QColor fill = maskColor;
            fill.setAlphaF(opacity);
            painter.setPen(Qt::NoPen);
            painter.fillPath(path, fill);
        }
    };

    drawEye(m_processor.leftImage(), 0, false);
    drawEye(m_processor.rightImage(), iw, true);
    
    painter.end();
    result.save(fileName);
}

void StereoViewWidget::setAnaglyphMode(bool enabled) { m_anaglyphMode = enabled; update(); }
void StereoViewWidget::setSwapSides(bool enabled) { m_swapSides = enabled; update(); }
void StereoViewWidget::setZoom(float zoom) { m_zoom = qBound(0.1f, zoom, 20.0f); update(); }

void StereoViewWidget::addPoint(const QPointF &pos, float disparity, int index)
{
    m_undoStack->push(new AddPointCommand(this, {pos, disparity}, index));
    saveProject();
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
    saveProject();
}

void StereoViewWidget::updatePointInternal(int index, const MaskPoint &p) { if (index >= 0 && index < m_points.count()) { m_points[index] = p; update(); } }
void StereoViewWidget::updatePointsInternal(const QList<int> &indices, const QVector<MaskPoint> &ps) { for (int i = 0; i < indices.count(); ++i) { if (indices[i] >= 0 && indices[i] < m_points.count()) m_points[indices[i]] = ps[i]; } update(); }
void StereoViewWidget::addPointInternal(const MaskPoint &p, int index) { if (index >= 0 && index <= m_points.count()) m_points.insert(index, p); else m_points.append(p); update(); }
void StereoViewWidget::removePointInternal(int index) { if (!m_points.isEmpty()) { if (index >= 0 && index < m_points.count()) m_points.removeAt(index); else m_points.removeLast(); } update(); }
void StereoViewWidget::insertPointsInternal(const QList<int> &indices, const QVector<MaskPoint> &ps) { for (int i = 0; i < indices.count(); ++i) m_points.insert(indices[i], ps[i]); update(); }
void StereoViewWidget::removePointsInternal(const QList<int> &indices) { for (int idx : indices) { if (idx >= 0 && idx < m_points.count()) m_points.removeAt(idx); } update(); }

void StereoViewWidget::clearPoints() { m_points.clear(); m_selectedIndices.clear(); m_undoStack->clear(); update(); saveProject(); }

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
        if (side == AlignLeft || side == AlignRight) p.pos.setX(targetVal);
        else if (side == AlignTop || side == AlignBottom) p.pos.setY(targetVal);
        else if (side == AlignDepth) p.disparity = targetDisp;
        newPs << p;
    }
    m_undoStack->push(new BatchMoveCommand(this, m_selectedIndices, oldPs, newPs));
    saveProject();
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

void StereoViewWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (!m_processor.isValid()) {
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, tr("Load SBS Image\nL-Drag: Selection Rect\nP-Drag / Mid-Drag: Pan\nShift+L-Drag: Depth\nWheel: Zoom\nCtrl+Z: Undo\nDel: Delete Selected"));
        return;
    }

    QRect vL, vR; float scale; calculateLayout(vL, vR, scale);

    auto drawInViewport = [&](const QRect &v, const QImage &img, bool isRight) {
        painter.save();
        painter.setClipRect(v);
        float sw = img.width() * scale;
        float sh = img.height() * scale;
        QRectF target(v.center().x() - sw/2.0 + m_panOffset.x()*scale, v.center().y() - sh/2.0 + m_panOffset.y()*scale, sw, sh);
        painter.drawImage(target, img);
        
        if (!m_points.isEmpty()) {
            QPolygonF poly;
            for (const auto &p : m_points) poly << imageToWidget(p.pos, v, scale, isRight ? p.disparity : 0);
            QPainterPath path; path.addRect(target); path.addPolygon(poly); path.closeSubpath();
            painter.setPen(QPen(Qt::white, 1));
            painter.fillPath(path, QColor(0,0,0,150));
            painter.drawPolygon(poly);
            for (int i=0; i<m_points.count(); ++i) {
                painter.setBrush(m_selectedIndices.contains(i) ? Qt::yellow : Qt::red);
                painter.setPen(i == m_selectedPointIndex ? QPen(Qt::cyan, 2) : QPen(Qt::white, 1));
                painter.drawEllipse(poly[i], 5, 5);
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
    bool inRight = vR.contains(event->pos()) && !m_anaglyphMode;
    QRect targetV = inRight ? vR : vL;
    
    if (targetV.contains(event->pos())) {
        bool isRightEye = inRight ? !m_swapSides : m_swapSides;
        for (int i=0; i<m_points.count(); ++i) {
            QPointF pW = imageToWidget(m_points[i].pos, targetV, scale, isRightEye ? m_points[i].disparity : 0);
            if (QLineF(event->pos(), pW).length() < 20) { hitIdx = i; break; }
        }
    }

    if (hitIdx != -1) {
        if (event->modifiers() & Qt::ControlModifier) {
            if (m_selectedIndices.contains(hitIdx)) m_selectedIndices.removeOne(hitIdx); else m_selectedIndices.append(hitIdx);
        } else if (!m_selectedIndices.contains(hitIdx)) {
            m_selectedIndices.clear(); m_selectedIndices.append(hitIdx);
        }
        m_selectedPointIndex = hitIdx;
        m_preMovePoints.clear(); for (int idx : m_selectedIndices) m_preMovePoints << m_points[idx];
        setCursor(Qt::ClosedHandCursor); emit selectionChanged(m_points[hitIdx].disparity);
    } else {
        if (event->button() == Qt::LeftButton) {
            if (vL.contains(event->pos()) || vR.contains(event->pos())) {
                m_selectedIndices.clear(); m_selectedPointIndex = -1;
                m_isSelecting = true; m_selectionRect = QRect(event->pos(), QSize(1, 1));
            } else { m_isPanning = true; setCursor(Qt::SizeAllCursor); }
        } else if (event->button() == Qt::RightButton && targetV.contains(event->pos())) {
            QPointF imgPos = widgetToImage(event->pos(), targetV, scale);
            bool isRightEye = inRight ? !m_swapSides : m_swapSides;
            if (isRightEye) imgPos.setX(imgPos.x() + (m_points.isEmpty() ? 0 : m_points[0].disparity));
            addPoint(imgPos, 0);
            m_selectedPointIndex = m_points.count()-1; m_selectedIndices.clear(); m_selectedIndices << m_selectedPointIndex;
            m_preMovePoints.clear(); m_preMovePoints << m_points.last();
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

        if (event->modifiers() & Qt::ShiftModifier) {
            float dx = (float)(event->pos().x() - m_lastMousePos.x());
            for (int idx : m_selectedIndices) {
                m_points[idx].disparity += dx / scale;
            }
            emit selectionChanged(m_points[m_selectedPointIndex].disparity);
        } else {
            if (m_lockHorizontal) totalDelta.setX(0); if (m_lockVertical) totalDelta.setY(0);
            if (m_selectedIndices.count() == 1) {
                bool inRight = !m_anaglyphMode && vR.contains(m_mouseDragStart);
                float dragDisp = inRight ? (!m_swapSides ? m_preMovePoints[0].disparity : 0) : (m_swapSides ? m_preMovePoints[0].disparity : 0);
                QPointF visualPos = (m_preMovePoints[0].pos - QPointF(dragDisp, 0)) + totalDelta;
                float snap = 10.0f / scale;
                for (int i=0; i<m_points.count(); ++i) {
                    if (i == m_selectedPointIndex) continue;
                    float tx = inRight ? (m_points[i].pos.x() - (!m_swapSides ? m_points[i].disparity : 0)) : (m_points[i].pos.x() - (m_swapSides ? m_points[i].disparity : 0));
                    if (qAbs(visualPos.x() - tx) < snap) visualPos.setX(tx);
                    if (qAbs(visualPos.y() - m_points[i].pos.y()) < snap) visualPos.setY(m_points[i].pos.y());
                }
                m_points[m_selectedPointIndex].pos = visualPos + QPointF(dragDisp, 0);
            } else {
                for (int i=0; i<m_selectedIndices.count(); ++i) m_points[m_selectedIndices[i]].pos = m_preMovePoints[i].pos + totalDelta;
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
    } else if (m_selectedPointIndex != -1) {
        bool moved = false; QVector<MaskPoint> newPs;
        for (int i = 0; i < m_selectedIndices.count(); ++i) {
            newPs << m_points[m_selectedIndices[i]];
            if (m_points[m_selectedIndices[i]].pos != m_preMovePoints[i].pos || m_points[m_selectedIndices[i]].disparity != m_preMovePoints[i].disparity) moved = true;
        }
        if (moved) {
            if (m_selectedIndices.count() == 1) m_undoStack->push(new MovePointCommand(this, m_selectedPointIndex, m_preMovePoints[0], m_points[m_selectedPointIndex]));
            else m_undoStack->push(new BatchMoveCommand(this, m_selectedIndices, m_preMovePoints, newPs));
        }
        saveProject();
    }
    m_selectedPointIndex = -1; setCursor(m_panMode ? Qt::OpenHandCursor : Qt::ArrowCursor); update();
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
    m_undoStack->push(new BatchMoveCommand(this, m_selectedIndices, oldPs, newPs)); saveProject();
}

void StereoViewWidget::transformSelectedPoints(float scaleX, float scaleY, float dx, float dy)
{
    QList<int> indices = m_selectedIndices; if (indices.isEmpty()) for (int i = 0; i < m_points.count(); ++i) indices << i;
    if (indices.isEmpty()) return;
    QPointF center(0, 0); for (int idx : indices) center += m_points[idx].pos; center /= (float)indices.count();
    QVector<MaskPoint> oldPs, newPs;
    for (int idx : indices) { oldPs << m_points[idx]; MaskPoint p = m_points[idx]; p.pos = center + QPointF((p.pos.x() - center.x()) * scaleX, (p.pos.y() - center.y()) * scaleY) + QPointF(dx, dy); newPs << p; }
    m_undoStack->push(new BatchMoveCommand(this, indices, oldPs, newPs)); saveProject();
}
