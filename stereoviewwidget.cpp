#include <QBuffer>
#include <QBitArray>
#include "smartselection.h"
#include "operationprogress.h"
#include <QScopedValueRollback>
#include <QScopeGuard>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QKeyEvent>
#include <QSaveFile>
#include <QUndoCommand>
#include "stereoviewwidget.h"
#include <QDataStream>
#include "undocommand.h"
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QDebug>
#include <QWheelEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUuid>
#include <QtMath>
#include <algorithm>
#include <limits>
#include <numeric>
#include <stack>
#include <vector>

#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

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
    setFocusPolicy(Qt::StrongFocus);
    m_undoStack = new QUndoStack(this);
    m_autoMaskDebugTimer = new QTimer(this);
    m_autoMaskDebugTimer->setInterval(1000);
    connect(m_autoMaskDebugTimer, &QTimer::timeout, this, [this]() {
        if (m_autoMaskDebugFrameIndex + 1 < m_autoMaskDebugFrames.size()) {
            ++m_autoMaskDebugFrameIndex;
        } else {
            m_autoMaskDebugTimer->stop();
            m_autoMaskDebugFrames.clear();
            m_autoMaskDebugFrameIndex = -1;
            if (!m_pendingAutoMaskPoints.isEmpty() && m_points.isEmpty()) {
                QList<int> indices;
                for (int i = 0; i < m_pendingAutoMaskPoints.size(); ++i) indices.append(i);
                m_undoStack->push(new InsertPointsCommand(this, indices, m_pendingAutoMaskPoints, m_pendingAutoMaskText));
                if (m_autoSave) saveProject();
                emit maskEmptyChanged(false);
            }
            m_pendingAutoMaskPoints.clear();
            m_pendingAutoMaskText.clear();
        }
        update();
    });
}

namespace {
struct CvAutoMaskPoint {
    QPointF point;
    float disparity = 0.0f;
};

struct CvAutoMaskResult {
    bool ok = false;
    float disparity = 0.0f;
    QVector<CvAutoMaskPoint> points;
    QVector<QVector<QPointF>> debugTiles;
    QString method;
};

enum class RectDirection { Vertical, Horizontal };

static cv::Rect largestRectInMask(const cv::Mat &mask)
{
    const int height = mask.rows;
    const int width = mask.cols;
    std::vector<int> heights(width, 0);
    cv::Rect best;
    int bestArea = 0;

    for (int y = 0; y < height; ++y) {
        const uchar *row = mask.ptr<uchar>(y);
        for (int x = 0; x < width; ++x) {
            heights[x] = row[x] ? heights[x] + 1 : 0;
        }

        std::stack<int> stack;
        for (int x = 0; x <= width; ++x) {
            int currentHeight = (x == width) ? 0 : heights[x];
            while (!stack.empty() && heights[stack.top()] > currentHeight) {
                int rectHeight = heights[stack.top()];
                stack.pop();
                int rectWidth = stack.empty() ? x : x - stack.top() - 1;
                int area = rectWidth * rectHeight;
                if (area > bestArea) {
                    int left = stack.empty() ? 0 : stack.top() + 1;
                    best = cv::Rect(left, y - rectHeight + 1, rectWidth, rectHeight);
                    bestArea = area;
                }
            }
            stack.push(x);
        }
    }

    return best;
}

static cv::Rect largestRectConstrained(const cv::Mat &coverage, RectDirection direction)
{
    if (direction == RectDirection::Vertical) {
        std::vector<cv::Point> points;
        cv::findNonZero(coverage, points);
        if (points.empty()) return {};
        cv::Rect bounds = cv::boundingRect(points);
        cv::Rect local = largestRectInMask(coverage(bounds));
        if (local.area() <= 0) return {};
        return cv::Rect(bounds.x + local.x, bounds.y + local.y, local.width, local.height);
    }

    cv::Mat transposed;
    cv::transpose(coverage, transposed);
    cv::Rect rect = largestRectInMask(transposed);
    return cv::Rect(rect.y, rect.x, rect.height, rect.width);
}

static QVector<cv::Rect> decomposeMask(const cv::Mat &mask, int minArea, int maxIterations)
{
    cv::Mat coverage = mask.clone();
    QVector<cv::Rect> tiles;
    RectDirection direction = RectDirection::Vertical;

    cv::Rect seed = largestRectInMask(coverage);
    if (seed.area() < minArea) return tiles;
    tiles.append(seed);
    coverage(seed).setTo(0);

    for (int i = 0; i < maxIterations; ++i) {
        if (cv::countNonZero(coverage) == 0) break;

        cv::Rect candidate = largestRectConstrained(coverage, direction);
        if (candidate.area() < minArea) break;

        bool collides = false;
        for (const cv::Rect &tile : tiles) {
            if ((candidate & tile).area() > 0) {
                collides = true;
                break;
            }
        }
        if (collides) break;
        if (candidate == cv::Rect(0, 0, mask.cols, mask.rows)) break;

        tiles.append(candidate);
        coverage(candidate).setTo(0);
        direction = (direction == RectDirection::Vertical) ? RectDirection::Horizontal : RectDirection::Vertical;
    }

    return tiles;
}

static QVector<QVector<cv::Rect>> decomposeConnectedMasks(const cv::Mat &mask, int minArea, int maxIterations)
{
    cv::Mat labels;
    int labelCount = cv::connectedComponents(mask, labels, 8, CV_32S);
    QVector<QVector<cv::Rect>> allTiles;

    for (int label = 1; label < labelCount; ++label) {
        cv::Mat component = labels == label;
        int area = cv::countNonZero(component);
        if (area < minArea) continue;

        QVector<cv::Rect> tiles = decomposeMask(component, minArea, maxIterations);
        if (!tiles.isEmpty()) allTiles.append(tiles);
    }

    return allTiles;
}

static QVector<QPointF> assemblePolygonFromTiles(const QVector<QVector<cv::Rect>> &components, cv::Size imageSize, double scale, int maxPoints)
{
    cv::Mat canvas = cv::Mat::zeros(imageSize, CV_8U);
    for (const QVector<cv::Rect> &tiles : components) {
        for (const cv::Rect &tile : tiles) {
            canvas(tile).setTo(255);
        }
    }
    cv::dilate(canvas, canvas, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(canvas, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) return {};

    auto largest = std::max_element(contours.begin(), contours.end(), [](const auto &a, const auto &b) {
        return cv::contourArea(a) < cv::contourArea(b);
    });

    std::vector<cv::Point> approx;
    double epsilon = 2.0;
    cv::approxPolyDP(*largest, approx, epsilon, true);
    while ((int)approx.size() > maxPoints && epsilon < 80.0) {
        epsilon *= 1.35;
        cv::approxPolyDP(*largest, approx, epsilon, true);
    }

    QVector<QPointF> polygon;
    polygon.reserve(static_cast<int>(approx.size()));
    for (const cv::Point &point : approx) {
        polygon.append(QPointF(point.x * scale, point.y * scale));
    }
    return polygon;
}

static QVector<QVector<QPointF>> scaleTilesForDebug(const QVector<QVector<cv::Rect>> &components, double scale)
{
    QVector<QVector<QPointF>> polygons;
    for (const QVector<cv::Rect> &tiles : components) {
        for (const cv::Rect &tile : tiles) {
            QRectF rect(tile.x * scale, tile.y * scale, tile.width * scale, tile.height * scale);
            polygons.append({rect.topLeft(), rect.bottomLeft(), rect.bottomRight(), rect.topRight()});
        }
    }
    return polygons;
}

static cv::Mat qImageToBgrMat(const QImage &image)
{
    QImage rgb32 = image.convertToFormat(QImage::Format_RGB32);
    cv::Mat bgra(rgb32.height(), rgb32.width(), CV_8UC4,
                 const_cast<uchar*>(rgb32.constBits()),
                 static_cast<size_t>(rgb32.bytesPerLine()));
    cv::Mat bgr;
    cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
    return bgr;
}

static QVector<QPointF> polygonFromMaskContour(const cv::Mat &mask, double scale, int maxPoints)
{
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) return {};

    auto largest = std::max_element(contours.begin(), contours.end(), [](const auto &a, const auto &b) {
        return cv::contourArea(a) < cv::contourArea(b);
    });
    if (largest == contours.end() || cv::contourArea(*largest) < 100.0) return {};

    std::vector<cv::Point> approx;
    double epsilon = std::max(2.0, 0.004 * cv::arcLength(*largest, true));
    cv::approxPolyDP(*largest, approx, epsilon, true);

    while ((int)approx.size() > maxPoints && epsilon < 80.0) {
        epsilon *= 1.35;
        cv::approxPolyDP(*largest, approx, epsilon, true);
    }

    QVector<QPointF> polygon;
    polygon.reserve(static_cast<int>(approx.size()));
    for (const cv::Point &p : approx) {
        polygon.append(QPointF(p.x * scale, p.y * scale));
    }
    return polygon;
}

static float medianDisparityNear(const cv::Mat &disp, const QPointF &fullPoint, double scale, float fallback)
{
    const int cx = qBound(0, qRound(fullPoint.x() / scale), disp.cols - 1);
    const int cy = qBound(0, qRound(fullPoint.y() / scale), disp.rows - 1);
    std::vector<float> values;
    values.reserve(81);
    for (int dy = -4; dy <= 4; ++dy) {
        int y = cy + dy;
        if (y < 0 || y >= disp.rows) continue;
        const float *row = disp.ptr<float>(y);
        for (int dx = -4; dx <= 4; ++dx) {
            int x = cx + dx;
            if (x < 0 || x >= disp.cols) continue;
            if (row[x] > 0.0f) values.push_back(row[x]);
        }
    }
    if (values.empty()) return fallback;
    std::nth_element(values.begin(), values.begin() + values.size() / 2, values.end());
    return values[values.size() / 2] * scale;
}

static cv::Mat buildConsistencyMask(const cv::Mat &dispL, const cv::Mat &dispR, float threshold)
{
    cv::Mat mask = cv::Mat::zeros(dispL.rows, dispL.cols, CV_8U);
    for (int y = 0; y < dispL.rows; ++y) {
        const float *leftRow = dispL.ptr<float>(y);
        uchar *maskRow = mask.ptr<uchar>(y);
        for (int x = 0; x < dispL.cols; ++x) {
            float disparity = leftRow[x];
            int xr = x - qRound(disparity);
            if (disparity > 0.0f && xr >= 0 && xr < dispL.cols &&
                std::abs(disparity + dispR.at<float>(y, xr)) <= threshold) {
                maskRow[x] = 1;
            }
        }
    }
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));
    return mask;
}

static CvAutoMaskResult computeCvAutoMask(const QImage &leftImage, const QImage &rightImage)
{
    const cv::Mat left = qImageToBgrMat(leftImage);
    const cv::Mat right = qImageToBgrMat(rightImage);

    const int w = left.cols;
    const int h = left.rows;
    const int analysisW = std::min(720, w);
    const int analysisH = std::max(1, qRound(h * (double)analysisW / w));
    const double scale = (double)w / analysisW;

    cv::Mat leftS;
    cv::Mat rightS;
    cv::resize(left, leftS, cv::Size(analysisW, analysisH), 0, 0, cv::INTER_AREA);
    cv::resize(right, rightS, cv::Size(analysisW, analysisH), 0, 0, cv::INTER_AREA);

    cv::Mat grayL;
    cv::Mat grayR;
    cv::cvtColor(leftS, grayL, cv::COLOR_BGR2GRAY);
    cv::cvtColor(rightS, grayR, cv::COLOR_BGR2GRAY);

    int numDisp = std::max(16, ((analysisW / 5) / 16) * 16);
    auto matcherL = cv::StereoSGBM::create(0, numDisp, 5, 8 * 5 * 5, 32 * 5 * 5, 1, 0, 8, 80, 24, cv::StereoSGBM::MODE_SGBM_3WAY);
    auto matcherR = cv::StereoSGBM::create(-numDisp, numDisp, 5, 8 * 5 * 5, 32 * 5 * 5, 1, 0, 8, 80, 24, cv::StereoSGBM::MODE_SGBM_3WAY);

    cv::Mat dispL16;
    cv::Mat dispR16;
    matcherL->compute(grayL, grayR, dispL16);
    matcherR->compute(grayR, grayL, dispR16);
    cv::Mat dispL;
    cv::Mat dispR;
    dispL16.convertTo(dispL, CV_32F, 1.0 / 16.0);
    dispR16.convertTo(dispR, CV_32F, 1.0 / 16.0);

    cv::Mat validMask = buildConsistencyMask(dispL, dispR, 1.5f);
    cv::morphologyEx(validMask, validMask, cv::MORPH_CLOSE, cv::Mat::ones(9, 9, CV_8U));
    cv::morphologyEx(validMask, validMask, cv::MORPH_OPEN, cv::Mat::ones(5, 5, CV_8U));

    std::vector<float> valid;
    for (int y = 0; y < dispL.rows; ++y) {
        const float *row = dispL.ptr<float>(y);
        for (int x = 0; x < dispL.cols; ++x) {
            if (row[x] > 0) valid.push_back(row[x]);
        }
    }
    float disparity = 0.0f;
    if (!valid.empty()) {
        std::nth_element(valid.begin(), valid.begin() + valid.size() / 2, valid.end());
        disparity = valid[valid.size() / 2] * scale;
    }

    const int minArea = qMax(180, analysisW * analysisH / 700);
    QVector<QVector<cv::Rect>> tileComponents = decomposeConnectedMasks(validMask, minArea, 50);
    QVector<QPointF> polygon = assemblePolygonFromTiles(tileComponents, validMask.size(), scale, 32);
    if (polygon.size() < 3) {
        polygon = polygonFromMaskContour(validMask, scale, 28);
    }

    CvAutoMaskResult result;
    result.ok = polygon.size() >= 3;
    result.disparity = disparity;
    result.debugTiles = scaleTilesForDebug(tileComponents, scale);
    result.method = QStringLiteral("sgbm-rect-decompose");
    for (const QPointF &point : polygon) {
        result.points.append({point, medianDisparityNear(dispL, point, scale, disparity)});
    }
    return result;
}

static CvAutoMaskResult computeCvContourAutoMask(const QImage &leftImage, const QImage &rightImage)
{
    const cv::Mat left = qImageToBgrMat(leftImage);
    const cv::Mat right = qImageToBgrMat(rightImage);

    const int w = left.cols;
    const int h = left.rows;
    const int analysisW = std::min(720, w);
    const int analysisH = std::max(1, qRound(h * (double)analysisW / w));
    const double scale = (double)w / analysisW;

    cv::Mat leftS;
    cv::Mat rightS;
    cv::resize(left, leftS, cv::Size(analysisW, analysisH), 0, 0, cv::INTER_AREA);
    cv::resize(right, rightS, cv::Size(analysisW, analysisH), 0, 0, cv::INTER_AREA);

    cv::Mat grayL;
    cv::Mat grayR;
    cv::cvtColor(leftS, grayL, cv::COLOR_BGR2GRAY);
    cv::cvtColor(rightS, grayR, cv::COLOR_BGR2GRAY);

    int numDisp = std::max(16, ((analysisW / 5) / 16) * 16);
    auto matcherL = cv::StereoSGBM::create(0, numDisp, 5, 8 * 5 * 5, 32 * 5 * 5, 1, 0, 8, 80, 24, cv::StereoSGBM::MODE_SGBM_3WAY);
    auto matcherR = cv::StereoSGBM::create(-numDisp, numDisp, 5, 8 * 5 * 5, 32 * 5 * 5, 1, 0, 8, 80, 24, cv::StereoSGBM::MODE_SGBM_3WAY);

    cv::Mat dispL16;
    cv::Mat dispR16;
    matcherL->compute(grayL, grayR, dispL16);
    matcherR->compute(grayR, grayL, dispR16);
    cv::Mat dispL;
    cv::Mat dispR;
    dispL16.convertTo(dispL, CV_32F, 1.0 / 16.0);
    dispR16.convertTo(dispR, CV_32F, 1.0 / 16.0);

    cv::Mat validMask = buildConsistencyMask(dispL, dispR, 1.75f);
    cv::morphologyEx(validMask, validMask, cv::MORPH_CLOSE, cv::Mat::ones(9, 9, CV_8U));
    cv::morphologyEx(validMask, validMask, cv::MORPH_OPEN, cv::Mat::ones(5, 5, CV_8U));

    std::vector<float> valid;
    for (int y = 0; y < dispL.rows; ++y) {
        const float *row = dispL.ptr<float>(y);
        for (int x = 0; x < dispL.cols; ++x) {
            if (row[x] > 0) valid.push_back(row[x]);
        }
    }
    float disparity = 0.0f;
    if (!valid.empty()) {
        std::nth_element(valid.begin(), valid.begin() + valid.size() / 2, valid.end());
        disparity = valid[valid.size() / 2] * scale;
    }

    QVector<QPointF> polygon = polygonFromMaskContour(validMask, scale, 28);
    CvAutoMaskResult result;
    result.ok = polygon.size() >= 3;
    result.disparity = disparity;
    result.method = QStringLiteral("sgbm-contour");
    for (const QPointF &point : polygon) {
        result.points.append({point, medianDisparityNear(dispL, point, scale, disparity)});
    }
    return result;
}
}


namespace {
class SmartHintsCommand : public QUndoCommand {
    StereoViewWidget *view; bool right; QImage before, after; QRect beforeRect, afterRect;
public:
    SmartHintsCommand(StereoViewWidget *v, bool r, const QImage &b, const QImage &a, const QRect &br, const QRect &ar)
        : QUndoCommand("Smart selection hints/box"), view(v), right(r), before(b), after(a), beforeRect(br), afterRect(ar) {}
    void undo() override { view->applySmartHints(right, before, beforeRect); }
    void redo() override { view->applySmartHints(right, after, afterRect); }
};
class RevealStrokeCommand : public QUndoCommand {
    StereoViewWidget *view; SelectionState before, after;
public:
    RevealStrokeCommand(StereoViewWidget *v, bool r, const QImage &b, const QImage &a)
        : QUndoCommand("Paint outside frame"), view(v), before(v->selectionState()), after(v->stateWithRasterChange(r,b,a)) {}
    void undo() override { view->restoreSelectionState(before); }
    void redo() override { view->restoreSelectionState(after); }
};
class SelectionObjectsCommand : public QUndoCommand {
    StereoViewWidget *view; SelectionState before, after;
public:
    SelectionObjectsCommand(StereoViewWidget *v, const SelectionState &next, const QString &text)
        : QUndoCommand(text), view(v), before(v->selectionState()), after(next) {}
    SelectionObjectsCommand(StereoViewWidget *v, const SelectionState &previous, const SelectionState &next, const QString &text)
        : QUndoCommand(text), view(v), before(previous), after(next) {}
    void undo() override { view->restoreSelectionState(before); }
    void redo() override { view->restoreSelectionState(after); }
};
QJsonObject encodeLassoPath(const QPainterPath &path) {
    QJsonArray polygons;
    for (const auto &polygon:path.toSubpathPolygons()) {
        QJsonArray points;
        for (const auto &p:polygon) points.append(QJsonArray{p.x(),p.y()});
        polygons.append(points);
    }
    return QJsonObject{{"rule",int(path.fillRule())},{"polygons",polygons}};
}
QPainterPath decodeLassoPath(const QJsonObject &obj) {
    QPainterPath path;
    path.setFillRule(obj["rule"].toInt()==int(Qt::WindingFill) ? Qt::WindingFill : Qt::OddEvenFill);
    for (const auto &value:obj["polygons"].toArray()) {
        const auto points=value.toArray(); if (points.size()<3 || points.size()>20000) continue;
        QPolygonF poly;
        for (const auto &item:points) { const auto p=item.toArray(); if(p.size()==2) poly.append(QPointF(p[0].toDouble(),p[1].toDouble())); }
        if(poly.size()>=3) {path.addPolygon(poly);path.closeSubpath();}
    }
    return path;
}
QString encodeReveal(const QImage &img) {
    if (img.isNull()) return {};
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG"); return QString::fromLatin1(bytes.toBase64());
}
}
void StereoViewWidget::applyLassoSelection(bool right,const QImage &mask,const QPainterPath &outline) {
    (right ? m_completedRight : m_completedLeft)=outline;
    applyReveal(right,mask);
}
void StereoViewWidget::applyReveal(bool right, const QImage &mask) {
    (right ? m_baseRevealRight : m_baseRevealLeft) = mask;
    (right ? m_baseOutlineRight : m_baseOutlineLeft) = completedLasso(right);
    for (int i=m_selectionObjects.size()-1; i>=0; --i)
        if (m_selectionObjects[i].right==right) m_selectionObjects.removeAt(i);
    rebuildSelectionMasks();
    ++m_smartRevision;
    updateAnaglyphIfActive();
    update();
    if (m_autoSave) saveProject();
}

SelectionState StereoViewWidget::selectionState() const {
    return {m_selectionObjects,m_baseRevealLeft,m_baseRevealRight,m_baseOutlineLeft,m_baseOutlineRight};
}

QImage StereoViewWidget::exportEye(bool right, bool includeText) const {
    const QImage image=right ? m_processor.rightImage() : m_processor.leftImage();
    QByteArray key;QDataStream stream(&key,QIODevice::WriteOnly);
    stream<<image.cacheKey()<<m_smartRevision<<m_revealLeft.cacheKey()<<m_revealRight.cacheKey()
          <<m_pictureFrame.enabled<<m_pictureFrame.width<<m_pictureFrame.style<<m_pictureFrame.color
          <<m_pictureFrame.shadowEnabled<<m_pictureFrame.shadowOpacity<<m_pictureFrame.shadowDistance
          <<m_pictureFrame.shadowAngle<<m_pictureFrame.shadowBlur<<m_pictureFrame.shadowSpread<<m_pictureFrame.shadowColor
          <<includeText
          <<m_cutout<<m_cutoutBackground<<m_cutoutColor<<m_maskColor<<m_maskOpacity<<m_featherAmount<<m_previewFeatherEnabled;
    if(includeText) for(const auto &text:m_textObjects)stream<<QJsonDocument(textToJson(text)).toJson(QJsonDocument::Compact);
    for (const auto &point:m_points)
        stream<<point.pos<<point.disparity<<point.isCurve<<point.cp1<<point.cp2<<point.cp1Disparity<<point.cp2Disparity;
    QByteArray &cachedKey=right ? m_exportKeyRight : m_exportKeyLeft;
    QImage &cached=right ? m_exportCacheRight : m_exportCacheLeft;
    if (cachedKey==key && !cached.isNull()) return cached;
    const bool appearance=!m_cutout || m_cutoutBackground==4;
    const QImage opening=openingMask(right,appearance && m_previewFeatherEnabled ? m_featherAmount : 0);
    cached=appearance ? maskedPreviewEye(image,opening,m_maskColor,m_maskOpacity)
                      : cutoutEye(image,m_points,right,opening,m_cutoutBackground,m_cutoutColor,true);
    if (m_pictureFrame.shadowEnabled && m_points.size()>=3) {
        QPainter painter(&cached);painter.drawImage(0,0,shadowOverlay(image.size(),m_points,right,openingMask(right),m_pictureFrame));
    }
    if (m_pictureFrame.enabled && m_points.size()>=3) {
        QPainter framePainter(&cached);
        framePainter.drawImage(0,0,pictureFrameOverlay(image.size(),m_points,right,openingMask(right),m_pictureFrame));
    }
    if(includeText && !m_textObjects.isEmpty()) {
        QPainter painter(&cached);painter.drawImage(0,0,cachedTextOverlay(right));
    }
    cachedKey=key;return cached;
}
QImage StereoViewWidget::openingMask(bool right, int feather) const {
    const QImage source=right ? m_processor.rightImage() : m_processor.leftImage();
    if (source.isNull()) return {};
    QImage mask(source.size(),QImage::Format_ARGB32_Premultiplied);
    mask.fill(m_points.isEmpty() ? Qt::white : Qt::transparent);
    QPainter painter(&mask); painter.setRenderHint(QPainter::Antialiasing);
    if (!m_points.isEmpty())
        drawFeatheredPath(painter,createImageMaskPath(m_points,QRectF(source.rect()),1.0f,right,false),Qt::white,feather);
    const QImage base=right ? m_baseRevealRight : m_baseRevealLeft;
    if (!base.isNull()) painter.drawImage(0,0,base);
    for (const auto &object:m_selectionObjects) {
        if (!object.visible || object.right!=right) continue;
        painter.setCompositionMode(object.subtract ? QPainter::CompositionMode_DestinationOut : QPainter::CompositionMode_SourceOver);
        if (!object.raster.isNull()) painter.drawImage(0,0,object.raster);
        else painter.fillPath(object.path,Qt::white);
    }
    painter.end(); return mask;
}
void StereoViewWidget::rebuildSelectionMasks() {
    auto state=selectionState();
    const QSize size=m_processor.leftImage().size();
    auto compose=[state,size]() mutable {
    for (const auto &object:state.objects) {
        if (!object.visible) continue;
        QImage &mask=object.right ? state.baseRight : state.baseLeft;
        QPainterPath &outline=object.right ? state.outlineRight : state.outlineLeft;
        if (mask.isNull()) {
            mask=QImage(size,QImage::Format_ARGB32_Premultiplied);
            mask.fill(Qt::transparent);
        }
        QPainter painter(&mask); painter.setRenderHint(QPainter::Antialiasing);
        painter.setCompositionMode(object.subtract ? QPainter::CompositionMode_DestinationOut : QPainter::CompositionMode_SourceOver);
        if (!object.raster.isNull()) painter.drawImage(0,0,object.raster);
        else painter.fillPath(object.path,Qt::white);
        painter.end();
        if (!object.path.isEmpty()) outline=object.subtract ? outline.subtracted(object.path) : outline.united(object.path);
    }
    return state;
    };
    const auto composed=m_operationProgress ? m_operationProgress->run(compose) : compose();
    m_revealLeft=composed.baseLeft;m_revealRight=composed.baseRight;
    m_completedLeft=composed.outlineLeft;m_completedRight=composed.outlineRight;
    emit selectionObjectsChanged();
}

void StereoViewWidget::restoreSelectionState(const SelectionState &state) {
    m_draggingPolygon=false;
    cancelLasso();
    m_selectionObjects=state.objects;
    m_baseRevealLeft=state.baseLeft; m_baseRevealRight=state.baseRight;
    m_baseOutlineLeft=state.outlineLeft; m_baseOutlineRight=state.outlineRight;
    rebuildSelectionMasks(); ++m_smartRevision;
    updateAnaglyphIfActive(); update();
    if (m_autoSave) saveProject();
}

SelectionState StereoViewWidget::stateWithRasterChange(bool right,const QImage &before,const QImage &after) const {
    SelectionState state=selectionState();
    QImage add(m_processor.leftImage().size(),QImage::Format_ARGB32_Premultiplied);
    QImage remove(add.size(),add.format()); add.fill(Qt::transparent); remove.fill(Qt::transparent);
    bool hasAdd=false,hasRemove=false;
    for (int y=0; y<add.height(); ++y) for (int x=0; x<add.width(); ++x) {
        const int b=before.isNull() ? 0 : qAlpha(before.pixel(x,y));
        const int a=after.isNull() ? 0 : qAlpha(after.pixel(x,y));
        if (a>b) {
            const int alpha=qRound(255.0*(a-b)/(255-b));
            add.setPixel(x,y,qRgba(alpha,alpha,alpha,alpha)); hasAdd=true;
        } else if (a<b) {
            const int alpha=qRound(255.0*(b-a)/b);
            remove.setPixel(x,y,qRgba(alpha,alpha,alpha,alpha)); hasRemove=true;
        }
    }
    auto append=[&](const QImage &mask,bool subtract) {
        SelectionObject object; object.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        object.name=tr("Brush %1").arg(state.objects.size()+1); object.right=right;
        object.subtract=subtract; object.raster=mask; state.objects.append(object);
    };
    if (hasRemove) append(remove,true);
    if (hasAdd) append(add,false);
    return state;
}

QImage StereoViewWidget::colorSelectionMask(const QImage &image, const QPoint &seed, int tolerance, bool contiguous)
{
    if (image.isNull() || !image.rect().contains(seed)) return {};
    tolerance=qBound(0,tolerance,255);
    const QImage source=image.convertToFormat(QImage::Format_RGB32);
    QImage mask(source.size(),QImage::Format_ARGB32_Premultiplied);mask.fill(Qt::transparent);
    const QRgb target=source.pixel(seed);
    const int limit=3*tolerance*tolerance;
    auto matches=[&](QRgb pixel) {
        const int dr=qRed(pixel)-qRed(target),dg=qGreen(pixel)-qGreen(target),db=qBlue(pixel)-qBlue(target);
        return dr*dr+dg*dg+db*db<=limit;
    };
    if (!contiguous) {
        for(int y=0;y<source.height();++y) {
            const QRgb *row=reinterpret_cast<const QRgb *>(source.constScanLine(y));
            QRgb *out=reinterpret_cast<QRgb *>(mask.scanLine(y));
            for(int x=0;x<source.width();++x)if(matches(row[x]))out[x]=qRgba(255,255,255,255);
        }
        return mask;
    }
    const int width=source.width(),height=source.height();
    QBitArray visited(width*height);
    QVector<int> queue;queue.reserve(qMin(width*height,262144));
    const int first=seed.y()*width+seed.x();visited.setBit(first);queue.append(first);
    for(qsizetype head=0;head<queue.size();++head) {
        const int index=queue[head],x=index%width,y=index/width;
        if(!matches(source.pixel(x,y)))continue;
        mask.setPixel(x,y,qRgba(255,255,255,255));
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            if((dx==0&&dy==0)||x+dx<0||x+dx>=width||y+dy<0||y+dy>=height)continue;
            const int next=(y+dy)*width+x+dx;if(!visited.testBit(next)){visited.setBit(next);queue.append(next);}
        }
    }
    return mask;
}

void StereoViewWidget::applyColorSelection(bool right,const QPoint &seed,bool contiguous)
{
    if(m_refining || !m_processor.isValid())return;
    QScopedValueRollback<bool> busy(m_refining,true);
    emit smartBusy(true);
    const auto finishBusy=qScopeGuard([this] { emit smartBusy(false); });
    OperationProgress progress(this,tr("StereoMask — Color selection"),tr("Finding pixels matching the selected color…"));
    QScopedValueRollback<OperationProgress *> activeProgress(m_operationProgress,&progress);
    const QImage source=right ? m_processor.rightImage() : m_processor.leftImage();
    const int tolerance=m_colorSelectionTolerance;
    const QImage mask=progress.run([source,seed,tolerance,contiguous] {return colorSelectionMask(source,seed,tolerance,contiguous);});
    if(mask.isNull())return;
    const qint64 pixels=progress.run([mask] {
        qint64 count=0;for(int y=0;y<mask.height();++y){const QRgb *row=reinterpret_cast<const QRgb *>(mask.constScanLine(y));for(int x=0;x<mask.width();++x)count+=qAlpha(row[x])!=0;}
        return count;
    });
    if(!pixels){emit smartStatus(tr("No pixels matched the selected color and tolerance."));return;}
    progress.setStage(tr("Updating the ordered mask and saving the project…"));
    m_cutout=true;
    auto state=selectionState();SelectionObject object;
    object.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    object.name=tr("%1 %2").arg(contiguous ? tr("Fuzzy") : tr("Color"),QString::number(state.objects.size()+1));
    object.right=right;object.subtract=m_lassoSubtract;object.raster=mask;state.objects.append(object);
    m_undoStack->push(new SelectionObjectsCommand(this,state,contiguous ? tr("Fuzzy select region") : tr("Select by color threshold")));
    emit smartStatus(tr("%1 selected %2 pixels in the %3 eye at tolerance %4. Click again to add another History object.")
        .arg(contiguous ? tr("Fuzzy Select") : tr("Color Threshold")).arg(pixels)
        .arg(right ? tr("right") : tr("left")).arg(m_colorSelectionTolerance));
}

void StereoViewWidget::setSelectionVisible(const QString &id,bool visible) {
    if (m_refining) return;
    if (hasLasso()) { emit smartStatus(tr("Apply or cancel the current outline before changing object visibility.")); return; }
    auto state=selectionState();
    for (auto &object:state.objects) if (object.id==id && object.visible!=visible) {
        object.visible=visible;
        m_undoStack->push(new SelectionObjectsCommand(this,state,tr("Selection visibility"))); return;
    }
}
void StereoViewWidget::renameSelection(const QString &id,const QString &name) {
    if (m_refining || name.trimmed().isEmpty()) return;
    if (hasLasso()) { emit selectionObjectsChanged(); emit smartStatus(tr("Apply or cancel the current outline before renaming a selection.")); return; }
    auto state=selectionState();
    for (auto &object:state.objects) if (object.id==id && object.name!=name.trimmed()) {
        object.name=name.trimmed().left(120);
        m_undoStack->push(new SelectionObjectsCommand(this,state,tr("Rename selection"))); return;
    }
}
void StereoViewWidget::deleteSelection(const QString &id) {
    if (m_refining) return;
    auto state=selectionState();
    for (int i=0;i<state.objects.size();++i) if (state.objects[i].id==id) {
        state.objects.removeAt(i);
        m_undoStack->push(new SelectionObjectsCommand(this,state,tr("Delete selection"))); return;
    }
}
void StereoViewWidget::setPolygonMoveEnabled(bool enabled) {
    if (enabled && (hasLasso() || m_refining)) {
        emit smartStatus(tr("Apply or cancel the current outline before moving an applied polygon."));
        emit polygonMoveModeChanged(m_polygonMoveEnabled);return;
    }
    if (enabled==m_polygonMoveEnabled) return;
    if (m_draggingPolygon) restoreSelectionState(m_beforePolygonDrag);
    m_polygonMoveEnabled=enabled;
    if (enabled) {m_panMode=false;m_freehandMode=false;m_pickingColor=false;}
    setCursor(enabled ? Qt::SizeAllCursor : m_brushMode>=7 ? Qt::CrossCursor : Qt::ArrowCursor);
    emit polygonMoveModeChanged(enabled);setFocus();update();
}
void StereoViewWidget::toggleSelectionOperation(const QString &id) {
    if (m_refining) return;
    if (hasLasso()) {
        emit smartStatus(tr("Apply or cancel the current outline before switching an object's Add/Subtract mode.")); return;
    }
    auto state=selectionState();
    for (auto &object:state.objects) if (object.id==id) {
        object.subtract=!object.subtract;
        m_undoStack->push(new SelectionObjectsCommand(this,state,tr("Switch selection Add/Subtract")));
        return;
    }
}
void StereoViewWidget::moveSelection(const QString &id, int offset) {
    if (m_refining || offset == 0) return;
    if (hasLasso()) { emit smartStatus(tr("Apply or cancel the current outline before reordering selections.")); return; }
    auto state=selectionState();
    for (int i=0; i<state.objects.size(); ++i) if (state.objects[i].id==id) {
        const int destination=qBound(0,i+offset,state.objects.size()-1);
        if (destination!=i) {
            state.objects.move(i,destination);
            m_undoStack->push(new SelectionObjectsCommand(this,state,tr("Reorder selections")));
        }
        return;
    }
}
bool StereoViewWidget::editSelection(const QString &id) {
    if (m_refining || hasLasso()) {
        emit smartStatus(tr("Apply or cancel the current outline before editing another selection.")); return false;
    }
    for (const auto &object:m_selectionObjects) if (object.id==id) {
        if (object.anchors.size()<3) {
            emit smartStatus(tr("This raster selection can be hidden or deleted; its original control points are unavailable.")); return false;
        }
        setBrushMode(object.magnetic.contains(true) ? 8 : 7);
        m_editingSelectionId=id; m_lassoRight=object.right; m_lassoSubtract=object.subtract;
        m_lassoAnchors=object.anchors; m_lassoMagnetic=object.magnetic;
        m_lassoSegments.resize(m_lassoAnchors.size()-1);
        for (int i=0;i<m_lassoSegments.size();++i) rebuildLassoSegment(i);
        m_lassoClosed=true; rebuildLassoClosing();
        emit brushSettingsLoaded(7,m_cutoutBackground);
        emit lassoOperationChanged(m_lassoSubtract);
        emit smartStatus(tr("Drag this selection's points, then Apply selection to save the changes.")); update(); return true;
    }
    return false;
}

QJsonObject StereoViewWidget::selectionObjectsJson() const {
    QJsonArray objects;
    for (const auto &object:m_selectionObjects) {
        QJsonArray anchors,magnetic;
        for (const auto &point:object.anchors) anchors.append(QJsonArray{point.x(),point.y()});
        for (bool value:object.magnetic) magnetic.append(value);
        objects.append(QJsonObject{{"id",object.id},{"name",object.name},{"right",object.right},
            {"subtract",object.subtract},{"visible",object.visible},{"anchors",anchors},{"magnetic",magnetic},
            {"path",encodeLassoPath(object.path)},{"rasterPNG",encodeReveal(object.raster)}});
    }
    return {{"version",1},{"objects",objects},{"baseLeftPNG",encodeReveal(m_baseRevealLeft)},
        {"baseRightPNG",encodeReveal(m_baseRevealRight)},{"outlineLeft",encodeLassoPath(m_baseOutlineLeft)},
        {"outlineRight",encodeLassoPath(m_baseOutlineRight)}};
}

void StereoViewWidget::loadSelectionObjects(const QJsonObject &project) {
    m_selectionObjects.clear(); m_baseRevealLeft=QImage(); m_baseRevealRight=QImage();
    m_baseOutlineLeft=QPainterPath(); m_baseOutlineRight=QPainterPath();
    const auto data=project["selectionObjects"].toObject();
    auto raster=[&](const QString &encoded) {
        QImage image=QImage::fromData(QByteArray::fromBase64(encoded.toLatin1()),"PNG");
        return image.size()==m_processor.leftImage().size() ? image.convertToFormat(QImage::Format_ARGB32_Premultiplied) : QImage();
    };
    if (data["version"].toInt()==1) {
        m_baseRevealLeft=raster(data["baseLeftPNG"].toString());
        m_baseRevealRight=raster(data["baseRightPNG"].toString());
        m_baseOutlineLeft=decodeLassoPath(data["outlineLeft"].toObject());
        m_baseOutlineRight=decodeLassoPath(data["outlineRight"].toObject());
        for (const auto &value:data["objects"].toArray()) {
            const auto json=value.toObject(); SelectionObject object;
            object.id=json["id"].toString(); if (object.id.isEmpty()) object.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
            object.name=json["name"].toString(); object.right=json["right"].toBool();
            object.subtract=json["subtract"].toBool(); object.visible=json["visible"].toBool(true);
            object.path=decodeLassoPath(json["path"].toObject()); object.raster=raster(json["rasterPNG"].toString());
            for (const auto &point:json["anchors"].toArray()) {
                const auto xy=point.toArray(); if (xy.size()==2) object.anchors.append(QPointF(xy[0].toDouble(),xy[1].toDouble()));
            }
            for (const auto &flag:json["magnetic"].toArray()) object.magnetic.append(flag.toBool());
            object.magnetic.resize(qMax(0,object.anchors.size()-1));
            m_selectionObjects.append(object);
        }
    } else {
        for (bool right:{false,true}) {
            const QImage mask=revealMask(right);
            if (mask.isNull()) continue;
            SelectionObject object; object.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
            object.name=tr("Imported selection"); object.right=right; object.raster=mask;
            object.path=completedLasso(right); m_selectionObjects.append(object);
        }
    }
    rebuildSelectionMasks();
}
void StereoViewWidget::applySmartHints(bool right, const QImage &hints, const QRect &region) {
    (right ? m_smartHintsRight : m_smartHintsLeft) = hints;
    (right ? m_smartRegionRight : m_smartRegionLeft) = region;
    m_smartRight = right; ++m_smartRevision;
    update(); if (m_autoSave) saveProject();
}
void StereoViewWidget::clearSmartHints() {
    if (m_refining || !m_processor.isValid()) return;
    const QImage before = m_smartRight ? m_smartHintsRight : m_smartHintsLeft;
    const QRect region = m_smartRight ? m_smartRegionRight : m_smartRegionLeft;
    if (before.isNull() || region.isEmpty()) return;
    QImage after = before;
    QPainter p(&after); p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.fillRect(region, Qt::transparent); p.end();
    m_undoStack->push(new SmartHintsCommand(this, m_smartRight, before, after, region, region));
    emit smartStatus(tr("Hints cleared in the active box. Your revealed image is unchanged."));
}
void StereoViewWidget::findEdges() {
    if (m_refining || !m_processor.isValid()) return;
    const bool right = m_smartRight;
    const QRect region = right ? m_smartRegionRight : m_smartRegionLeft;
    if (region.isEmpty()) {
        emit smartStatus(tr("Select Smart box, then drag a loose box around one leg in one eye.")); return;
    }
    if (m_points.size() < 3) { emit smartStatus(tr("Set your frame first.")); return; }
    const QImage source = right ? m_processor.rightImage() : m_processor.leftImage();
    const QImage hints = right ? m_smartHintsRight : m_smartHintsLeft;
    const QImage before = revealMask(right);
    const int revision = m_smartRevision;
    auto progress=std::make_shared<OperationProgress>(this,tr("StereoMask — Find edges"),tr("Refining the selected edges with OpenCV…"));
    m_refining = true; emit smartBusy(true);
    emit smartStatus(tr("Finding edges in the active box…"));
    auto watcher = new QFutureWatcher<SmartSelectionResult>(this);
    connect(watcher, &QFutureWatcher<SmartSelectionResult>::finished, this, [this, watcher, right, region, before, revision, source, progress] {
        const SmartSelectionResult result = watcher->result(); watcher->deleteLater();
        m_refining = false; emit smartBusy(false);
        if (revision != m_smartRevision) { emit smartStatus(tr("Selection changed during processing; result discarded. Click Find edges again.")); return; }
        if (!result.error.isEmpty() || result.mask.isNull()) { emit smartStatus(result.error.isEmpty() ? tr("No edge selection was produced.") : result.error); return; }
        progress->setStage(tr("Updating the refined mask and saving the project…"));
        QScopedValueRollback<OperationProgress *> activeProgress(m_operationProgress,progress.get());
        QImage after = before;
        if (after.isNull()) { after = QImage(source.size(), QImage::Format_ARGB32_Premultiplied); after.fill(Qt::transparent); }
        QPainter p(&after); p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(region.topLeft(), result.mask); p.end();
        m_cutout = true;
        m_undoStack->push(new RevealStrokeCommand(this, right, before, after));
        emit smartStatus(tr("Edges refined. Green is preserved; Keep/Remove strokes can correct it. Undo restores the previous paint."));
    });
    watcher->setFuture(QtConcurrent::run([source, region, hints, before] { return findSmartEdges(source, region, hints, before); }));
}
QImage StereoViewWidget::cutoutEye(const QImage &image, const QVector<MaskPoint> &points, bool right, const QImage &reveal, int background, const QColor &color, bool completeOpening) {
    QImage alpha(image.size(), QImage::Format_ARGB32_Premultiplied);
    alpha.fill(points.isEmpty() ? Qt::white : Qt::transparent);
    QPainter ap(&alpha); ap.setRenderHint(QPainter::Antialiasing);
    if (!points.isEmpty()) ap.fillPath(createImageMaskPath(points, QRectF(image.rect()), 1.0f, right, false), Qt::white);
    if (!reveal.isNull()) ap.drawImage(0, 0, reveal);
    ap.end();
    if (completeOpening) alpha=reveal;
    QImage eye = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter ep(&eye); ep.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    ep.drawImage(0, 0, alpha); ep.end();
    if (background != 0) {
        QImage solid(image.size(), QImage::Format_ARGB32_Premultiplied);
        solid.fill(background == 1 ? QColor(Qt::black) : background == 2 ? QColor(Qt::white) : color);
        QPainter sp(&solid); sp.drawImage(0, 0, eye); sp.end(); return solid;
    }
    return eye;
}
QImage StereoViewWidget::maskedPreviewEye(const QImage &image, const QImage &opening, const QColor &color, float opacity) {
    if (image.isNull() || opening.size()!=image.size()) return {};
    QImage overlay(image.size(),QImage::Format_ARGB32_Premultiplied);
    if (overlay.isNull()) return {};
    QColor fill=color; fill.setAlphaF(qBound(0.0f,opacity,1.0f)); overlay.fill(fill);
    QPainter mask(&overlay); mask.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    mask.drawImage(0,0,opening); mask.end();
    QImage result=image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&result); painter.drawImage(0,0,overlay); painter.end();
    return result;
}
bool StereoViewWidget::saveMaskedPreview(const QString &file, const QImage &left, const QImage &right,
 const QImage &openingLeft, const QImage &openingRight, const QColor &color, float opacity, int px, int py, int gap, const QColor &paddingColor) {
    if (left.isNull() || left.size()!=right.size() || px<0 || py<0 || gap<0) return false;
    const QImage l=maskedPreviewEye(left,openingLeft,color,opacity),r=maskedPreviewEye(right,openingRight,color,opacity);
    if (l.isNull() || r.isNull()) return false;
    QImage result(2*left.width()+2*px+gap,left.height()+2*py,QImage::Format_ARGB32_Premultiplied);
    if (result.isNull()) return false;
    result.fill(paddingColor); QPainter painter(&result);
    painter.drawImage(px,py,l);painter.drawImage(px+left.width()+gap,py,r);painter.end();
    return result.save(file);
}
bool StereoViewWidget::saveCutout(const QString &file, const QImage &left, const QImage &right,
 const QVector<MaskPoint> &points, const QImage &rl, const QImage &rr, int background, int px, int py, int gap, const QColor &color, bool completeOpening) {
    if (left.isNull() || right.isNull() || left.size() != right.size()) return false;
    QImage result(2*left.width()+2*px+gap, left.height()+2*py, QImage::Format_ARGB32_Premultiplied);
    if (result.isNull()) return false;
    result.fill(background == 0 ? QColor(Qt::transparent) : background == 1 ? QColor(Qt::black) : background == 2 ? QColor(Qt::white) : color);
    QPainter p(&result);
    p.drawImage(px, py, cutoutEye(left, points, false, rl, background, color, completeOpening));
    p.drawImage(px+left.width()+gap, py, cutoutEye(right, points, true, rr, background, color, completeOpening));
    p.end(); return result.save(file);
}
void StereoViewWidget::brushAt(const QPoint &pos) {
    QRect l,r; float scale; calculateLayout(l,r,scale);
    bool right = m_brushRight;
    QRect viewport = (right != m_swapSides) ? r : l;
    if (!viewport.contains(pos)) { m_hasBrushPoint = false; return; }
    QPointF ip = widgetToImage(pos, viewport, scale);
    QImage &mask = m_brushMode >= 4 ? (right ? m_smartHintsRight : m_smartHintsLeft) : (right ? m_revealRight : m_revealLeft);
    if (mask.isNull()) { mask=QImage(m_processor.leftImage().size(), QImage::Format_ARGB32_Premultiplied); mask.fill(Qt::transparent); }
    QPainter p(&mask); p.setRenderHint(QPainter::Antialiasing, m_brushMode < 4);
    if (m_brushMode>=4) p.setClipRect(right ? m_smartRegionRight : m_smartRegionLeft);
    const QColor strokeColor = m_brushMode == 5 ? QColor(255,0,0) : m_brushMode >= 4 ? QColor(0,255,0) : QColor(Qt::white);
    p.setCompositionMode((m_brushMode==2 || m_brushMode==6) ? QPainter::CompositionMode_Clear : QPainter::CompositionMode_SourceOver);
    p.setPen(QPen(strokeColor, m_brushSize, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(m_hasBrushPoint ? m_lastBrushPoint : ip, ip);
    p.setPen(Qt::NoPen); p.setBrush(strokeColor);
    p.drawEllipse(ip, m_brushSize/2.0, m_brushSize/2.0);
    p.end(); m_lastBrushPoint=ip; m_hasBrushPoint=true; update();
}

void StereoViewWidget::loadBrushSettings(const QJsonObject &obj) {
    m_processor.setStereoWindowOffset(obj["stereoWindowOffset"].toInt(0));
    const auto frame=obj["pictureFrame"].toObject();
    m_textObjects.clear();m_pictureFrame=PictureFrameSettings{};
    m_pictureFrame.enabled=frame["enabled"].toBool(false);
    m_pictureFrame.width=qBound(1,frame["width"].toInt(24),1000);
    m_pictureFrame.style=qBound(0,frame["style"].toInt(1),2);
    const QColor frameColor(frame["color"].toString("#9b6a35"));
    if (frameColor.isValid()) m_pictureFrame.color=frameColor;
    m_pictureFrame.shadowEnabled=frame["shadowEnabled"].toBool(false);
    m_pictureFrame.shadowOpacity=qBound(0,frame["shadowOpacity"].toInt(60),100);
    m_pictureFrame.shadowDistance=qBound(0,frame["shadowDistance"].toInt(24),1000);
    m_pictureFrame.shadowAngle=qBound(0,frame["shadowAngle"].toInt(45),359);
    m_pictureFrame.shadowBlur=qBound(0,frame["shadowBlur"].toInt(12),200);
    m_pictureFrame.shadowSpread=qBound(0,frame["shadowSpread"].toInt(0),200);
    const QColor shadowColor(frame["shadowColor"].toString("#000000"));if(shadowColor.isValid())m_pictureFrame.shadowColor=shadowColor;
    const auto texts=obj["stereoText"].toArray();for(int i=0;i<qMin(100,texts.size());++i)m_textObjects.append(textFromJson(texts[i].toObject(),m_processor.leftImage().size()));
    cancelLasso();
    m_completedLeft=decodeLassoPath(obj["lassoOutlineLeft"].toObject());
    m_completedRight=decodeLassoPath(obj["lassoOutlineRight"].toObject());
    m_lassoSubtract=false;
    auto decodeRect = [&](const char *key) { const QJsonArray a=obj[key].toArray(); return a.size()==4 ? QRect(a[0].toInt(),a[1].toInt(),a[2].toInt(),a[3].toInt()).intersected(m_processor.leftImage().rect()) : QRect(); };
    m_smartRegionLeft=decodeRect("smartRegionLeft"); m_smartRegionRight=decodeRect("smartRegionRight");
    m_smartHintsLeft=QImage::fromData(QByteArray::fromBase64(obj["smartHintsLeftPNG"].toString().toLatin1()), "PNG");
    m_smartHintsRight=QImage::fromData(QByteArray::fromBase64(obj["smartHintsRightPNG"].toString().toLatin1()), "PNG");
    if (m_smartHintsLeft.size()!=m_processor.leftImage().size()) m_smartHintsLeft=QImage();
    if (m_smartHintsRight.size()!=m_processor.rightImage().size()) m_smartHintsRight=QImage();
    m_smartRight=obj["smartActiveRight"].toBool(); ++m_smartRevision;
    m_cutout = obj["cutout"].toBool();
    m_cutoutBackground = qBound(0, obj["cutoutBackground"].toInt(4), 4);
    m_revealLeft = QImage::fromData(QByteArray::fromBase64(obj["revealLeftPNG"].toString().toLatin1()), "PNG");
    m_revealRight = QImage::fromData(QByteArray::fromBase64(obj["revealRightPNG"].toString().toLatin1()), "PNG");
    if (m_revealLeft.size() != m_processor.leftImage().size()) m_revealLeft = QImage();
    if (m_revealRight.size() != m_processor.rightImage().size()) m_revealRight = QImage();
    loadSelectionObjects(obj);
    QColor color(obj["cutoutColor"].toString());
    m_cutoutColor = color.isValid() ? color : QColor(Qt::black);
    m_smartRegionLeft=QRect(); m_smartRegionRight=QRect();
    m_smartHintsLeft=QImage(); m_smartHintsRight=QImage();
    m_brushMode = 0; m_pickingColor = false; setCursor(Qt::ArrowCursor);
    emit brushSettingsLoaded(m_brushMode, m_cutoutBackground);
}

bool StereoViewWidget::closeProject()
{
    if (m_refining || m_brushing || m_draggingPolygon) return false;
    if (m_autoMaskDebugTimer) m_autoMaskDebugTimer->stop();
    cancelLasso();
    setPolygonMoveEnabled(false);
    setFreehandMode(false);
    m_imagePath.clear();
    m_processor = StereoImageProcessor();
    m_points.clear();
    m_selectedIndices.clear();
    m_selectedPointIndex = -1;
    m_preMovePoints.clear();
    m_hitType = HitNone;
    m_selectionObjects.clear();
    m_activeSelectionId.clear();
    m_baseRevealLeft = QImage();
    m_baseRevealRight = QImage();
    m_revealLeft = QImage();
    m_revealRight = QImage();
    m_beforeStroke = QImage();
    m_completedLeft = QPainterPath();
    m_completedRight = QPainterPath();
    m_baseOutlineLeft = QPainterPath();
    m_baseOutlineRight = QPainterPath();
    m_smartHintsLeft = QImage();
    m_smartHintsRight = QImage();
    m_smartRegionLeft = QRect();
    m_smartRegionRight = QRect();
    ++m_smartRevision;
    m_textObjects.clear();
    m_pictureFrame = PictureFrameSettings{};
    m_exportKeyLeft.clear();
    m_exportKeyRight.clear();
    m_exportCacheLeft = QImage();
    m_exportCacheRight = QImage();
    m_zoom = 1.0f;
    m_panOffset = QPointF();
    m_isSelecting = false;
    m_isPanning = false;
    m_spacePan = false;
    m_isFreehandDrawing = false;
    m_freehandPoints.clear();
    m_autoMaskDebugFrames.clear();
    m_pendingAutoMaskPoints.clear();
    m_pendingAutoMaskText.clear();
    m_brushMode = 0;
    m_pickingColor = false;
    m_cursorVisible = false;
    m_hasBrushPoint = false;
    m_cutout = false;
    m_cutoutBackground = 4;
    m_undoStack->clear();
    setCursor(Qt::ArrowCursor);
    emit selectionObjectsChanged();
    emit brushSettingsLoaded(0, 4);
    notifySelectionState();
    notifyMaskState();
    update();
    return true;
}

bool StereoViewWidget::loadImage(const QString &fileName)
{
    m_lastError = QString();
    if (m_refining) { m_lastError=tr("Wait for Find edges to finish before opening another image."); return false; }
    std::unique_ptr<OperationProgress> opening;
    if (!m_operationProgress) opening=std::make_unique<OperationProgress>(this,tr("StereoMask - Open file"),tr("Opening image: %1").arg(QFileInfo(fileName).fileName()));
    QScopedValueRollback<OperationProgress *> activeProgress(m_operationProgress,opening ? opening.get() : m_operationProgress);
    m_operationProgress->setStage(tr("Opening image: %1").arg(QFileInfo(fileName).fileName()));
    cancelLasso();
    const auto loaded=[&] {
        QScopedValueRollback<bool> loading(m_refining,true);
        return m_operationProgress->run([fileName] {
        StereoImageProcessor processor;
        const bool success=processor.loadSideBySide(fileName);
        return std::make_pair(success,processor);
        });
    }();
    if (loaded.first) {
        m_processor=loaded.second;
        m_operationProgress->setStage(tr("Restoring image settings and selections…"));
        m_textObjects.clear();m_pictureFrame=PictureFrameSettings{};
        m_selectionObjects.clear(); m_baseRevealLeft=QImage(); m_baseRevealRight=QImage();
        m_baseOutlineLeft=QPainterPath(); m_baseOutlineRight=QPainterPath();
        emit selectionObjectsChanged();
        m_completedLeft=QPainterPath(); m_completedRight=QPainterPath(); m_lassoSubtract=false;
        m_smartHintsLeft=QImage(); m_smartHintsRight=QImage(); m_smartRegionLeft=QRect(); m_smartRegionRight=QRect(); m_smartRight=false; ++m_smartRevision;
        m_revealLeft = QImage(); m_revealRight = QImage(); m_cutout = false; m_brushMode = 0; m_pickingColor = false; m_cutoutBackground = 4; m_cutoutColor = Qt::black; setCursor(Qt::ArrowCursor);
        emit brushSettingsLoaded(0, 0);
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
                    loadBrushSettings(obj);
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
        notifySelectionState();
        notifyMaskState();
        update();
        return true;
    }
    m_lastError = loaded.second.lastError();
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
    obj["selectionObjects"]=selectionObjectsJson();
    obj["brushVersion"] = 5;
    obj["stereoWindowOffset"] = stereoWindowOffset();
    obj["pictureFrame"] = QJsonObject{{"enabled",m_pictureFrame.enabled},{"width",m_pictureFrame.width},
        {"style",m_pictureFrame.style},{"color",m_pictureFrame.color.name(QColor::HexArgb)},
        {"shadowEnabled",m_pictureFrame.shadowEnabled},{"shadowOpacity",m_pictureFrame.shadowOpacity},
        {"shadowDistance",m_pictureFrame.shadowDistance},{"shadowAngle",m_pictureFrame.shadowAngle},
        {"shadowBlur",m_pictureFrame.shadowBlur},{"shadowSpread",m_pictureFrame.shadowSpread},
        {"shadowColor",m_pictureFrame.shadowColor.name(QColor::HexArgb)}};
    QJsonArray texts;for(const auto &text:m_textObjects)texts.append(textToJson(text));obj["stereoText"]=texts;obj["stereoTextVersion"]=1;
    obj["lassoOutlineLeft"]=encodeLassoPath(m_completedLeft);
    obj["lassoOutlineRight"]=encodeLassoPath(m_completedRight);
    obj["smartHintsLeftPNG"] = encodeReveal(m_smartHintsLeft);
    obj["smartHintsRightPNG"] = encodeReveal(m_smartHintsRight);
    auto rectJson=[](const QRect &r) { return QJsonArray{r.x(),r.y(),r.width(),r.height()}; };
    obj["smartRegionLeft"]=rectJson(m_smartRegionLeft); obj["smartRegionRight"]=rectJson(m_smartRegionRight);
    obj["smartActiveRight"]=m_smartRight;
    obj["cutoutColor"] = m_cutoutColor.name(QColor::HexArgb);
    obj["revealLeftPNG"] = encodeReveal(m_revealLeft);
    obj["revealRightPNG"] = encodeReveal(m_revealRight);
    obj["cutout"] = m_cutout;
    obj["cutoutBackground"] = m_cutoutBackground;
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

    if (QFile::exists(mskPath) && !QFile::exists(mskPath + ".pre-brush-backup"))
        if (!QFile::copy(mskPath, mskPath + ".pre-brush-backup")) return false;
    QSaveFile file(mskPath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    if (file.write(QJsonDocument(obj).toJson()) < 0) return false;
    return file.commit();
}

bool StereoViewWidget::loadProject(const QString &path)
{
    if (m_refining) { m_lastError=tr("Wait for Find edges to finish before opening another project."); return false; }
    QFileInfo info(path);
    if (info.suffix().toLower() != "msk") {
        return loadImage(path);
    }

    std::unique_ptr<OperationProgress> opening;
    if (!m_operationProgress) opening=std::make_unique<OperationProgress>(this,tr("StereoMask - Open file"),tr("Opening project: %1").arg(info.fileName()));
    QScopedValueRollback<OperationProgress *> activeProgress(m_operationProgress,opening ? opening.get() : m_operationProgress);

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

    loadBrushSettings(obj);
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
    notifySelectionState();
    notifyMaskState();
    emit maskAppearanceChanged(m_maskColor,m_maskOpacity);
    updateAnaglyphIfActive();
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
        const bool preview=m_maskPreviewEnabled || m_cleanPreviewEnabled;
        m_processor.createAnaglyphFromEyes((preview || !m_textObjects.isEmpty()) ? exportEye(false) : m_processor.leftImage(),
                                          (preview || !m_textObjects.isEmpty()) ? exportEye(true) : m_processor.rightImage(),m_swapSides);
    }
}

void StereoViewWidget::setZoom(float zoom) { if (m_draggingPolygon) return; m_zoom = qBound(0.1f, zoom, 20.0f); update(); }

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

void StereoViewWidget::notifyMaskState()
{
    emit maskEmptyChanged(m_points.isEmpty());
}

void StereoViewWidget::setFreehandMode(bool enabled)
{
    if (m_refining) return;
    if (enabled) setBrushMode(0);
    m_freehandMode = enabled && m_processor.isValid() && m_points.isEmpty();
    if (!m_freehandMode) {
        m_isFreehandDrawing = false;
        m_freehandPoints.clear();
    }
    setCursor(m_freehandMode ? Qt::CrossCursor : (m_panMode ? Qt::OpenHandCursor : Qt::ArrowCursor));
    emit freehandModeChanged(m_freehandMode);
    update();
}

double StereoViewWidget::pointLineDistance(const QPointF &point, const QPointF &lineStart, const QPointF &lineEnd) const
{
    QPointF segment = lineEnd - lineStart;
    double lengthSquared = QPointF::dotProduct(segment, segment);
    if (lengthSquared <= 0.0001) return QLineF(point, lineStart).length();
    double t = QPointF::dotProduct(point - lineStart, segment) / lengthSquared;
    t = qBound(0.0, t, 1.0);
    return QLineF(point, lineStart + segment * t).length();
}

QVector<QPointF> StereoViewWidget::simplifyFreehandPath(const QVector<QPointF> &points, double tolerance) const
{
    if (points.size() < 3) return points;
    double maxDistance = 0.0;
    int splitIndex = 0;
    for (int i = 1; i < points.size() - 1; ++i) {
        double distance = pointLineDistance(points[i], points.first(), points.last());
        if (distance > maxDistance) {
            maxDistance = distance;
            splitIndex = i;
        }
    }
    if (maxDistance <= tolerance) return {points.first(), points.last()};
    QVector<QPointF> left = simplifyFreehandPath(points.mid(0, splitIndex + 1), tolerance);
    QVector<QPointF> right = simplifyFreehandPath(points.mid(splitIndex), tolerance);
    if (!left.isEmpty()) left.removeLast();
    left += right;
    return left;
}

int StereoViewWidget::edgeStrengthAt(const QImage &image, int x, int y) const
{
    if (x <= 0 || y <= 0 || x >= image.width() - 1 || y >= image.height() - 1) return 0;
    auto gray = [&](int px, int py) { return qGray(image.pixel(px, py)); };
    int gx = -gray(x - 1, y - 1) + gray(x + 1, y - 1)
             -2 * gray(x - 1, y) + 2 * gray(x + 1, y)
             -gray(x - 1, y + 1) + gray(x + 1, y + 1);
    int gy = -gray(x - 1, y - 1) - 2 * gray(x, y - 1) - gray(x + 1, y - 1)
             +gray(x - 1, y + 1) + 2 * gray(x, y + 1) + gray(x + 1, y + 1);
    return qAbs(gx) + qAbs(gy);
}

QVector<QPointF> StereoViewWidget::snapFreehandPathToEdges(const QVector<QPointF> &points) const
{
    return points;
}

QVector<QPointF> StereoViewWidget::smoothFreehandPath(const QVector<QPointF> &points) const
{
    if (points.size() < 5) return points;
    QVector<QPointF> smoothed;
    smoothed.reserve(points.size());
    for (int i = 0; i < points.size(); ++i) {
        const QPointF prev = points[(i - 1 + points.size()) % points.size()];
        const QPointF current = points[i];
        const QPointF next = points[(i + 1) % points.size()];
        smoothed.append(prev * 0.2 + current * 0.6 + next * 0.2);
    }
    return smoothed;
}

QVector<QPointF> StereoViewWidget::limitFreehandPointCount(const QVector<QPointF> &points, int maxPoints) const
{
    if (points.size() <= maxPoints || maxPoints < 3) return points;
    QVector<QPointF> limited;
    limited.reserve(maxPoints);
    for (int i = 0; i < maxPoints; ++i) {
        int index = qRound((double)i * (points.size() - 1) / (maxPoints - 1));
        limited.append(points[index]);
    }
    return limited;
}

int StereoViewWidget::estimateGlobalDisparity() const
{
    if (!m_processor.isValid()) return 0;
    const QImage left = m_processor.leftImage().scaledToWidth(320, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    const QImage right = m_processor.rightImage().scaled(left.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    const int maxShift = qMax(8, left.width() / 6);
    double bestScore = std::numeric_limits<double>::max();
    int bestShift = 0;
    for (int shift = -maxShift; shift <= maxShift; ++shift) {
        int xStart = qMax(0, shift);
        int xEnd = qMin(left.width(), left.width() + shift);
        if (xEnd - xStart < left.width() / 2) continue;
        qint64 sad = 0;
        int samples = 0;
        for (int y = 0; y < left.height(); y += 3) {
            const QRgb *leftLine = reinterpret_cast<const QRgb*>(left.constScanLine(y));
            const QRgb *rightLine = reinterpret_cast<const QRgb*>(right.constScanLine(y));
            for (int x = xStart; x < xEnd; x += 3) {
                sad += qAbs(qGray(leftLine[x]) - qGray(rightLine[x - shift]));
                ++samples;
            }
        }
        if (samples > 0 && (double)sad / samples < bestScore) {
            bestScore = (double)sad / samples;
            bestShift = shift;
        }
    }
    return qRound(bestShift * (double)m_processor.leftImage().width() / left.width());
}

bool StereoViewWidget::createAutoMaskPoints()
{
    return createAutoMaskPoints(AutoMaskRectDecompose);
}

bool StereoViewWidget::createAutoMaskPoints(AutoMaskMode mode)
{
    if (!canAutoMask()) return false;
    QScopedValueRollback<bool> busy(m_refining,true);
    OperationProgress progress(this,tr("StereoMask — AutoMask"),tr("Analyzing stereo overlap and disparity…"));
    if (m_autoMaskDebugTimer) m_autoMaskDebugTimer->stop();
    m_autoMaskDebugFrames.clear();
    m_pendingAutoMaskPoints.clear();
    m_pendingAutoMaskText.clear();
    m_autoMaskDebugFrameIndex = -1;

    const int iw = m_processor.leftImage().width();
    const int ih = m_processor.leftImage().height();
    if (iw <= 1 || ih <= 1) return false;

    if (mode == AutoMaskRectDecompose || mode == AutoMaskContour) {
        CvAutoMaskResult cvMask;
        try {
            const QImage left=m_processor.leftImage(),right=m_processor.rightImage();
            cvMask = progress.run([left,right,mode] {
                return mode == AutoMaskRectDecompose ? computeCvAutoMask(left,right) : computeCvContourAutoMask(left,right);
            });
        } catch (const cv::Exception &e) {
            qWarning() << "OpenCV auto mask failed:" << e.what();
        }
        if (!cvMask.ok || cvMask.points.size() < 3) return false;

        const double border = qMax(4.0, qMin(iw, ih) * 0.015);
        auto clampBoth = [&](QPointF p, float pointDisparity) {
            double minX = qMax(border, border + pointDisparity);
            double maxX = qMin(iw - border, iw - border + pointDisparity);
            if (minX > maxX) minX = maxX = qBound(border, iw / 2.0 + pointDisparity * 0.5, iw - border);
            p.setX(qBound(minX, p.x(), maxX));
            p.setY(qBound(border, p.y(), ih - border));
            return p;
        };

        QVector<MaskPoint> autoPoints;
        QList<int> indices;
        QVector<QPointF> debugPolygon;
        for (const CvAutoMaskPoint &point : cvMask.points) {
            QPointF clamped = clampBoth(point.point, point.disparity);
            debugPolygon.append(clamped);
            autoPoints.append({clamped, point.disparity});
            indices.append(autoPoints.size() - 1);
        }

        if (autoPoints.size() >= 3) {
            progress.setStage(tr("Creating editable frame points and saving the project…"));
            m_selectedIndices.clear();
            m_selectedPointIndex = -1;
            m_undoStack->push(new InsertPointsCommand(this, indices, autoPoints,
                                                      mode == AutoMaskRectDecompose ? tr("Rect Decompose Auto Mask") : tr("Contour Auto Mask")));
            if (m_autoSave) saveProject();
            notifySelectionState();
            notifyMaskState();
            update();
            return true;
        }
        return false;
    }

    const int disparity = estimateGlobalDisparity();
    const int analysisW = qMin(480, iw);
    const int analysisH = qMax(1, qRound(ih * (double)analysisW / iw));
    const double scaleToFull = (double)iw / analysisW;
    const QImage left = m_processor.leftImage().scaled(analysisW, analysisH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    const QImage right = m_processor.rightImage().scaled(analysisW, analysisH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    const int shift = qRound(disparity / scaleToFull);

    QVector<uchar> coverage(analysisW * analysisH, 0);
    QVector<double> scores;
    scores.reserve(analysisW * analysisH / 4);
    for (int y = 0; y < analysisH; y += 2) {
        const QRgb *ll = reinterpret_cast<const QRgb*>(left.constScanLine(y));
        const QRgb *rr = reinterpret_cast<const QRgb*>(right.constScanLine(y));
        for (int x = 0; x < analysisW; x += 2) {
            int rx = x - shift;
            if (rx < 0 || rx >= analysisW) continue;
            scores.append(qAbs(qGray(ll[x]) - qGray(rr[rx])));
        }
    }
    if (scores.isEmpty()) return false;
    std::sort(scores.begin(), scores.end());
    const double threshold = qBound(18.0, scores[scores.size() / 2] * 1.6 + 10.0, 64.0);
    for (int y = 0; y < analysisH; ++y) {
        const QRgb *ll = reinterpret_cast<const QRgb*>(left.constScanLine(y));
        const QRgb *rr = reinterpret_cast<const QRgb*>(right.constScanLine(y));
        for (int x = 0; x < analysisW; ++x) {
            int rx = x - shift;
            if (rx < 0 || rx >= analysisW) continue;
            if (qAbs(qGray(ll[x]) - qGray(rr[rx])) <= threshold) coverage[y * analysisW + x] = 1;
        }
    }

    struct RectI { int x = 0; int y = 0; int w = 0; int h = 0; int area() const { return w * h; } };
    auto largestRectInMask = [&](const QVector<uchar> &mask, int w, int h) {
        QVector<int> heights(w, 0);
        RectI best;
        int bestArea = 0;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) heights[x] = mask[y * w + x] ? heights[x] + 1 : 0;
            std::stack<int> stack;
            for (int x = 0; x <= w; ++x) {
                int currentHeight = (x == w) ? 0 : heights[x];
                while (!stack.empty() && heights[stack.top()] > currentHeight) {
                    int height = heights[stack.top()];
                    stack.pop();
                    int width = stack.empty() ? x : x - stack.top() - 1;
                    int area = height * width;
                    if (area > bestArea) {
                        int leftX = stack.empty() ? 0 : stack.top() + 1;
                        best = {leftX, y - height + 1, width, height};
                        bestArea = area;
                    }
                }
                stack.push(x);
            }
        }
        return best;
    };
    auto transposeMask = [](const QVector<uchar> &mask, int w, int h) {
        QVector<uchar> out(w * h, 0);
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) out[x * h + y] = mask[y * w + x];
        return out;
    };
    auto clearRect = [](QVector<uchar> &mask, int w, const RectI &r) {
        for (int y = r.y; y < r.y + r.h; ++y) for (int x = r.x; x < r.x + r.w; ++x) mask[y * w + x] = 0;
    };
    auto toPoly = [&](const RectI &r) {
        QRectF rf(r.x * scaleToFull, r.y * scaleToFull, r.w * scaleToFull, r.h * scaleToFull);
        return QVector<QPointF>{rf.topLeft(), rf.bottomLeft(), rf.bottomRight(), rf.topRight()};
    };

    QVector<RectI> tiles;
    QVector<uchar> work = coverage;
    RectI seed = largestRectInMask(work, analysisW, analysisH);
    const int minArea = qMax(300, analysisW * analysisH / 350);
    if (seed.area() < minArea) return false;
    tiles.append(seed);
    clearRect(work, analysisW, seed);
    m_autoMaskDebugFrames.append({{}, {}, {toPoly(seed)}, {(float)disparity}, (float)disparity, tr("Seed rectangle")});

    bool horizontal = true;
    for (int iter = 0; iter < 50; ++iter) {
        RectI candidate;
        if (horizontal) {
            QVector<uchar> t = transposeMask(work, analysisW, analysisH);
            RectI tr = largestRectInMask(t, analysisH, analysisW);
            candidate = {tr.y, tr.x, tr.h, tr.w};
        } else {
            candidate = largestRectInMask(work, analysisW, analysisH);
        }
        if (candidate.area() < minArea) break;
        tiles.append(candidate);
        clearRect(work, analysisW, candidate);
        QVector<QVector<QPointF>> framePolys;
        QVector<float> frameDisps;
        for (const RectI &tile : tiles) {
            framePolys.append(toPoly(tile));
            frameDisps.append((float)disparity);
        }
        m_autoMaskDebugFrames.append({{}, {}, framePolys, frameDisps, (float)disparity,
                                      tr("%1 rectangle %2").arg(horizontal ? tr("Horizontal") : tr("Vertical")).arg(iter + 2)});
        horizontal = !horizontal;
    }

    QVector<double> ySamples;
    for (const RectI &tile : tiles) {
        ySamples << tile.y * scaleToFull << (tile.y + tile.h * 0.5) * scaleToFull << (tile.y + tile.h) * scaleToFull;
    }
    std::sort(ySamples.begin(), ySamples.end());
    QVector<QPointF> leftBoundary;
    QVector<QPointF> rightBoundary;
    for (double y : ySamples) {
        double leftX = iw;
        double rightX = 0;
        bool any = false;
        double ay = y / scaleToFull;
        for (const RectI &tile : tiles) {
            if (ay < tile.y || ay > tile.y + tile.h) continue;
            leftX = qMin(leftX, tile.x * scaleToFull);
            rightX = qMax(rightX, (tile.x + tile.w) * scaleToFull);
            any = true;
        }
        if (any && rightX > leftX) {
            leftBoundary.append(QPointF(leftX, y));
            rightBoundary.append(QPointF(rightX, y));
        }
    }
    QVector<QPointF> polygon = leftBoundary;
    for (int i = rightBoundary.size() - 1; i >= 0; --i) polygon.append(rightBoundary[i]);
    polygon = limitFreehandPointCount(polygon, 18);
    if (polygon.size() < 4) return false;

    const double border = qMax(4.0, qMin(iw, ih) * 0.015);
    auto clampBoth = [&](QPointF p) {
        double minX = qMax(border, border + disparity);
        double maxX = qMin(iw - border, iw - border + disparity);
        if (minX > maxX) minX = maxX = qBound(border, iw / 2.0 + disparity * 0.5, iw - border);
        p.setX(qBound(minX, p.x(), maxX));
        p.setY(qBound(border, p.y(), ih - border));
        return p;
    };
    QVector<MaskPoint> autoPoints;
    QList<int> indices;
    for (int i = 0; i < polygon.size(); ++i) {
        autoPoints.append({clampBoth(polygon[i]), (float)disparity});
        indices.append(i);
    }
    m_pendingAutoMaskPoints = autoPoints;
    m_pendingAutoMaskText = tr("Smart Auto Mask");
    m_autoMaskDebugFrames.append({{}, polygon, {}, {}, (float)disparity, tr("Final editable mask")});
    startAutoMaskDebugPlayback();
    return true;
}

void StereoViewWidget::appendFreehandPoint(const QPoint &widgetPos)
{
    QPointF imagePos = widgetToImage(widgetPos, m_freehandViewport, m_freehandScale);
    if (imagePos.x() < 0 || imagePos.y() < 0 || imagePos.x() > m_processor.leftImage().width() || imagePos.y() > m_processor.leftImage().height()) return;
    if (m_freehandPoints.isEmpty() || QLineF(m_freehandPoints.last(), imagePos).length() >= 5.0) {
        m_freehandPoints.append(imagePos);
        update();
    }
}

void StereoViewWidget::finishFreehandDrawing()
{
    m_isFreehandDrawing = false;
    QVector<QPointF> simplified = limitFreehandPointCount(simplifyFreehandPath(smoothFreehandPath(m_freehandPoints), 10.0), 40);
    m_freehandPoints.clear();
    if (simplified.size() < 3) { update(); return; }
    QVector<MaskPoint> newPoints;
    QList<int> indices;
    for (int i = 0; i < simplified.size(); ++i) {
        newPoints.append({simplified[i], 0});
        indices.append(i);
    }
    m_undoStack->push(new InsertPointsCommand(this, indices, newPoints, tr("Freehand Mask")));
    setFreehandMode(false);
    if (m_autoSave) saveProject();
}

void StereoViewWidget::startAutoMaskDebugPlayback()
{
    if (m_autoMaskDebugFrames.isEmpty() || !m_autoMaskDebugTimer) return;
    m_autoMaskDebugFrameIndex = 0;
    m_autoMaskDebugTimer->start();
    update();
}

void StereoViewWidget::drawAutoMaskDebugOverlay(QPainter &painter, const QRect &viewport, float scale, bool isRight)
{
    if (m_autoMaskDebugFrameIndex < 0 || m_autoMaskDebugFrameIndex >= m_autoMaskDebugFrames.size()) return;
    const AutoMaskDebugFrame &frame = m_autoMaskDebugFrames[m_autoMaskDebugFrameIndex];
    QVector<QVector<QPointF>> polygons = frame.imagePolygons;
    if (polygons.isEmpty() && !frame.imagePolygon.isEmpty()) polygons.append(frame.imagePolygon);
    if (polygons.isEmpty() && !frame.imageRect.isEmpty()) {
        const QRectF &r = frame.imageRect;
        polygons.append({r.topLeft(), r.bottomLeft(), r.bottomRight(), r.topRight()});
    }
    painter.save();
    painter.setClipRect(viewport);
    painter.setRenderHint(QPainter::Antialiasing);
    QPointF labelPoint;
    bool hasLabel = false;
    for (int pi = 0; pi < polygons.size(); ++pi) {
        const QVector<QPointF> &poly = polygons[pi];
        if (poly.size() < 3) continue;
        float d = pi < frame.polygonDisparities.size() ? frame.polygonDisparities[pi] : frame.disparity;
        QPainterPath path;
        QPointF first = imageToWidget(poly.first(), viewport, scale, isRight ? d : 0);
        path.moveTo(first);
        if (!hasLabel) { labelPoint = first; hasLabel = true; }
        for (int i = 1; i < poly.size(); ++i) path.lineTo(imageToWidget(poly[i], viewport, scale, isRight ? d : 0));
        path.closeSubpath();
        painter.fillPath(path, QColor(34, 197, 94, 42));
        painter.setPen(QPen(QColor("#22c55e"), 2));
        painter.drawPath(path);
    }
    if (hasLabel) {
        QRectF labelRect(labelPoint + QPointF(8, 8), QSizeF(250, 24));
        labelRect = labelRect.intersected(QRectF(viewport).adjusted(4, 4, -4, -4));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 180));
        painter.drawRoundedRect(labelRect, 3, 3);
        painter.setPen(Qt::white);
        painter.drawText(labelRect.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
                         tr("Auto Mask: %1/%2  %3").arg(m_autoMaskDebugFrameIndex + 1).arg(m_autoMaskDebugFrames.size()).arg(frame.label));
    }
    painter.restore();
}

void StereoViewWidget::addPoint(const QPointF &pos, float disparity, int index)
{
    if(index>=0 && index<=m_points.size() && m_points.size()>=3) {
        const int previous=(index+m_points.size()-1)%m_points.size();
        const auto a=m_points[previous],b=m_points[index % m_points.size()];
        if(a.isCurve) {
            auto mix=[](QPointF a,QPointF b,double t){return a*(1-t)+b*t;};
            double best=0.5,distance=std::numeric_limits<double>::max();
            for(int i=1;i<200;++i){double t=i/200.0,u=1-t;QPointF p=a.pos*u*u*u+a.cp1*3*u*u*t+a.cp2*3*u*t*t+b.pos*t*t*t;double d=QLineF(pos,p).length();if(d<distance){distance=d;best=t;}}
            const double t=best;const auto ab=mix(a.pos,a.cp1,t),bc=mix(a.cp1,a.cp2,t),cd=mix(a.cp2,b.pos,t);const auto abc=mix(ab,bc,t),bcd=mix(bc,cd,t),center=mix(abc,bcd,t);
            auto scalar=[t](double a,double b){return a*(1-t)+b*t;};const double da=scalar(a.disparity,a.cp1Disparity),db=scalar(a.cp1Disparity,a.cp2Disparity),dc=scalar(a.cp2Disparity,b.disparity);const double dab=scalar(da,db),dbc=scalar(db,dc),depth=scalar(dab,dbc);
            auto points=m_points;points[previous].cp1=ab;points[previous].cp2=abc;points[previous].cp1Disparity=da;points[previous].cp2Disparity=dab;
            MaskPoint inserted{center,float(depth)};inserted.isCurve=true;inserted.cp1=bcd;inserted.cp2=cd;inserted.cp1Disparity=dbc;inserted.cp2Disparity=dc;points.insert(index,inserted);
            replaceFramePoints(points);return;
        }
    }
    m_undoStack->push(new AddPointCommand(this, {pos, disparity}, index));
    updateAnaglyphIfActive();
    notifySelectionState();
    notifyMaskState();
    if (m_autoSave) saveProject();
}

void StereoViewWidget::deleteSelectedPoints()
{
    if (m_brushMode == 7 || m_brushMode == 8) { removeLassoSegment(); return; }
    if (m_selectedIndices.isEmpty()) return;
    QList<int> sortedIndices = m_selectedIndices;
    std::sort(sortedIndices.begin(), sortedIndices.end(), std::greater<int>());
    QVector<MaskPoint> removedPoints;
    for (int idx : sortedIndices) removedPoints.append(m_points[idx]);
    m_undoStack->push(new DeletePointsCommand(this, sortedIndices, removedPoints));
    m_selectedIndices.clear(); m_selectedPointIndex = -1;
    updateAnaglyphIfActive();
    notifySelectionState();
    notifyMaskState();
    if (m_autoSave) saveProject();
}

void StereoViewWidget::updatePointInternal(int index, const MaskPoint &p) { if (index >= 0 && index < m_points.count()) { m_points[index] = p; updateAnaglyphIfActive(); notifySelectionState(); notifyMaskState(); update(); } }
void StereoViewWidget::updatePointsInternal(const QList<int> &indices, const QVector<MaskPoint> &ps) { for (int i = 0; i < indices.count(); ++i) { if (indices[i] >= 0 && indices[i] < m_points.count()) m_points[indices[i]] = ps[i]; } updateAnaglyphIfActive(); notifySelectionState(); notifyMaskState(); update(); }
void StereoViewWidget::addPointInternal(const MaskPoint &p, int index) { if (index >= 0 && index <= m_points.count()) m_points.insert(index, p); else m_points.append(p); updateAnaglyphIfActive(); notifySelectionState(); notifyMaskState(); update(); }
void StereoViewWidget::removePointInternal(int index) { if (!m_points.isEmpty()) { if (index >= 0 && index < m_points.count()) m_points.removeAt(index); else m_points.removeLast(); } updateAnaglyphIfActive(); notifySelectionState(); notifyMaskState(); update(); }
void StereoViewWidget::insertPointsInternal(const QList<int> &indices, const QVector<MaskPoint> &ps) { for (int i = 0; i < indices.count(); ++i) m_points.insert(indices[i], ps[i]); updateAnaglyphIfActive(); notifySelectionState(); notifyMaskState(); update(); }
void StereoViewWidget::removePointsInternal(const QList<int> &indices) { for (int idx : indices) { if (idx >= 0 && idx < m_points.count()) m_points.removeAt(idx); } updateAnaglyphIfActive(); notifySelectionState(); notifyMaskState(); update(); }

void StereoViewWidget::clearPoints() { m_points.clear(); m_selectedIndices.clear(); m_undoStack->clear(); updateAnaglyphIfActive(); notifySelectionState(); notifyMaskState(); update(); if (m_autoSave) saveProject(); }

void StereoViewWidget::alignSelectedPoints(AlignSide side)
{
    if (m_brushMode == 7 || m_brushMode == 8) { removeLassoSegment(); return; }
    if (m_selectedIndices.isEmpty()) return;
    const MaskPoint &first=m_points[m_selectedIndices.first()];
    float targetVal=(side==AlignLeft || side==AlignRight) ? first.pos.x() : first.pos.y();
    float targetDisp=first.disparity;
    if(side==AlignLeft || side==AlignRight || side==AlignTop || side==AlignBottom) {
        for(int idx:m_selectedIndices) {
            const float value=(side==AlignLeft || side==AlignRight) ? m_points[idx].pos.x() : m_points[idx].pos.y();
            if(side==AlignLeft || side==AlignTop)targetVal=qMin(targetVal,value);
            else targetVal=qMax(targetVal,value);
        }
    } else if(side==AlignDepth) {
        QVector<float> disparities;disparities.reserve(m_selectedIndices.size());
        for(int idx:m_selectedIndices)disparities.append(m_points[idx].disparity);
        std::sort(disparities.begin(),disparities.end());
        const int middle=disparities.size()/2;
        targetDisp=disparities.size()%2 ? disparities[middle] : (disparities[middle-1]+disparities[middle])/2.0f;
    }
    
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
    if (m_brushMode == 7 || m_brushMode == 8) { removeLassoSegment(); return; }
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
        if (!m_anaglyphMode && (m_maskPreviewEnabled || m_cleanPreviewEnabled)) {
            const QImage rendered=exportEye(isRight);
            if (m_cutout && m_cutoutBackground==0) {
                QPixmap checker(16,16);checker.fill(QColor(220,220,220));
                QPainter tiles(&checker);tiles.fillRect(0,0,8,8,Qt::white);tiles.fillRect(8,8,8,8,Qt::white);tiles.end();
                painter.fillRect(target,QBrush(checker));
            }
            painter.drawImage(target,rendered);
        } else {
            painter.drawImage(target,img);
            if(!m_anaglyphMode && !m_textObjects.isEmpty())
                painter.drawImage(target,cachedTextOverlay(isRight));
        }
        if (m_cleanPreviewEnabled) { painter.restore(); return; }
        const QImage reveal = revealMask(isRight);
        if (!m_anaglyphMode && !m_maskPreviewEnabled && !reveal.isNull()) {
            QImage &tint = isRight ? m_revealTintRight : m_revealTintLeft;
            qint64 &key = isRight ? m_tintKeyRight : m_tintKeyLeft;
            if (key != reveal.cacheKey()) {
                tint = QImage(reveal.size(), QImage::Format_ARGB32_Premultiplied);
                tint.fill(QColor(0, 255, 100, 75));
                QPainter tp(&tint);
                tp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                tp.drawImage(0, 0, reveal); tp.end();
                key = reveal.cacheKey();
            }
            painter.drawImage(target, tint);
        }
        if (!m_anaglyphMode) {
            const QImage hints = isRight ? m_smartHintsRight : m_smartHintsLeft;
            if (!hints.isNull() && m_brushMode >= 3 && m_brushMode <= 6) { painter.setOpacity(0.6); painter.drawImage(target, hints); painter.setOpacity(1.0); }
            const QRect region = isRight ? m_smartRegionRight : m_smartRegionLeft;
            if (!region.isEmpty() && m_brushMode >= 3 && m_brushMode <= 6) {
                painter.setPen(QPen(isRight==m_smartRight ? Qt::cyan : Qt::gray, 2, Qt::DashLine));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(QRectF(imageToWidget(region.topLeft(),v,scale), imageToWidget(QPointF(region.x()+region.width(),region.y()+region.height()),v,scale)));
            }
        }
        if (!m_anaglyphMode && (m_brushMode==7 || m_brushMode==8)) {
            const QPainterPath completed=completedLasso(isRight);
            const QPointF origin=imageToWidget(QPointF(),v,scale);
            const QPainterPath screen=QTransform(scale,0,0,scale,origin.x(),origin.y()).map(completed);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(Qt::black,3,Qt::DashLine));painter.drawPath(screen);
            painter.setPen(QPen(Qt::yellow,1,Qt::DashLine));painter.drawPath(screen);
        }
        if (!m_anaglyphMode) for (const auto &object:m_selectionObjects) {
            if (object.id!=m_activeSelectionId || object.right!=isRight || object.path.isEmpty()) continue;
            const QPointF origin=imageToWidget(QPointF(),v,scale);
            painter.setBrush(Qt::NoBrush); painter.setPen(QPen(Qt::cyan,2,Qt::DashLine));
            painter.drawPath(QTransform(scale,0,0,scale,origin.x(),origin.y()).map(object.path));
        }
        if (!m_anaglyphMode && isRight == m_lassoRight && !m_lassoAnchors.isEmpty()) {
            painter.setRenderHint(QPainter::Antialiasing);
            QPainterPath outline;
            outline.moveTo(imageToWidget(m_lassoAnchors.first(), v, scale));
            for (const auto &segment : m_lassoSegments)
                for (const QPointF &p : segment) outline.lineTo(imageToWidget(p, v, scale));
            if (m_lassoClosed) {
                for (const QPointF &p : m_lassoClosing) outline.lineTo(imageToWidget(p, v, scale));
                outline.closeSubpath();
            }
            painter.setBrush(m_lassoClosed ? QBrush(QColor(255,210,0,35)) : QBrush(Qt::NoBrush));
            painter.setPen(QPen(Qt::black, 4)); painter.drawPath(outline);
            painter.setPen(QPen(Qt::yellow, 2)); painter.drawPath(outline);
            if (!m_lassoClosed && m_cursorVisible && v.contains(m_brushCursor)) {
                painter.setPen(QPen(Qt::yellow, 1, Qt::DashLine));
                painter.drawLine(imageToWidget(m_lassoAnchors.last(), v, scale), QPointF(m_brushCursor));
            }
            for (const auto &p : m_lassoAnchors) {
                painter.setBrush(Qt::yellow); painter.setPen(QPen(Qt::black, 1));
                painter.drawEllipse(imageToWidget(p,v,scale), 4, 4);
            }
        }
        if (!m_points.isEmpty()) {
            painter.setPen(QPen(Qt::white, 1, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(createMaskPath(m_points, v, scale, isRight, false));
        }

        if (!m_points.isEmpty()) {
            if (m_anaglyphMode) {
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

    if (m_cleanPreviewEnabled) return;
    drawAutoMaskDebugOverlay(painter, vL, scale, m_swapSides);
    if (!m_anaglyphMode) drawAutoMaskDebugOverlay(painter, vR, scale, !m_swapSides);
    
    if (m_isSelecting && !m_selectionRect.isNull()) { 
        painter.setPen(QPen(Qt::white, 1, Qt::DashLine)); painter.setBrush(QColor(255, 255, 255, 50)); 
        painter.drawRect(m_selectionRect); 
    }

    if (m_isFreehandDrawing && m_freehandPoints.size() > 1) {
        QPainterPath path;
        QPointF first = imageToWidget(m_freehandPoints.first(), m_freehandViewport, m_freehandScale);
        path.moveTo(first);
        for (int i = 1; i < m_freehandPoints.size(); ++i) {
            path.lineTo(imageToWidget(m_freehandPoints[i], m_freehandViewport, m_freehandScale));
        }
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(Qt::cyan, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
    }
    if (m_brushMode && m_brushMode != 3 && m_brushMode < 7 && !m_anaglyphMode && !m_pickingColor && m_cursorVisible &&
        (vL.contains(m_brushCursor) || vR.contains(m_brushCursor))) {
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(Qt::NoBrush);
        const qreal radius = m_brushSize * scale / 2.0;
        painter.setPen(QPen(Qt::black, 3));
        painter.drawEllipse(QPointF(m_brushCursor), radius, radius);
        painter.setPen(QPen(m_brushMode==4 ? QColor(Qt::green) : m_brushMode==5 ? QColor(Qt::red) : QColor(Qt::white), 1));
        painter.drawEllipse(QPointF(m_brushCursor), radius, radius);
    }

}

void StereoViewWidget::mousePressEvent(QMouseEvent *event)
{
    if (!m_processor.isValid() || m_refining) return;
    setFocus();
    if (m_spacePan || m_panMode || event->button()==Qt::MiddleButton || (event->button()==Qt::LeftButton && (event->modifiers() & Qt::AltModifier))) {
        m_isPanning=true; m_lastMousePos=event->pos(); setCursor(Qt::ClosedHandCursor); return;
    }
    m_brushCursor = event->pos(); m_cursorVisible = true;
    if (m_cleanPreviewEnabled) return;
    if (m_pickingColor && event->button() == Qt::LeftButton) {
        QRect l, r; float scale; calculateLayout(l, r, scale);
        if (m_anaglyphMode || (!l.contains(event->pos()) && !r.contains(event->pos()))) return;
        const bool inRight = r.contains(event->pos());
        const bool rightEye = inRight ? !m_swapSides : m_swapSides;
        const QImage image = rightEye ? m_processor.rightImage() : m_processor.leftImage();
        const QPoint ip = widgetToImage(event->pos(), inRight ? r : l, scale).toPoint();
        if (!image.rect().contains(ip)) return;
        const QColor color = image.pixelColor(ip);
        setMaskColor(color);
        m_pickingColor = false;
        m_colorPickClick = true;
        setCursor((m_brushMode==3 || m_brushMode>=7) ? Qt::CrossCursor : m_brushMode ? Qt::BlankCursor : Qt::ArrowCursor);
        emit maskColorPicked(color); update(); return;
    }
    if (m_pickingColor) return;
    if (m_polygonMoveEnabled && event->button()==Qt::LeftButton && !m_anaglyphMode) {
        if (hasLasso()) {emit smartStatus(tr("Apply or cancel the current outline first."));return;}
        QRect left,right;float scale;calculateLayout(left,right,scale);
        if (!left.contains(event->pos()) && !right.contains(event->pos())) return;
        const bool eye=right.contains(event->pos()) ? !m_swapSides : m_swapSides;
        const QRect viewport=eye!=m_swapSides ? right : left;
        const QPointF point=widgetToImage(event->pos(),viewport,scale);
        for (const auto &object:m_selectionObjects) if (object.id==m_activeSelectionId) {
            if (object.right!=eye || !object.visible || object.anchors.size()<3 || !object.raster.isNull()) return;
            QPainterPathStroker stroke;stroke.setWidth(14.0/scale);
            if (!object.path.contains(point) && !stroke.createStroke(object.path).contains(point)) return;
            m_beforePolygonDrag=selectionState();m_draggedPolygonId=object.id;
            m_polygonDragStart=point;m_polygonDragDelta=QPointF();
            m_polygonDragViewport=viewport;m_polygonDragScale=scale;m_draggingPolygon=true;
            setCursor(Qt::ClosedHandCursor);return;
        }
        emit smartStatus(tr("Select a visible polygon in History, then drag its interior or edge."));return;
    }
    if ((m_brushMode==9 || m_brushMode==10) && !m_anaglyphMode) {
        if(event->button()!=Qt::LeftButton)return;
        QRect l,r;float scale;calculateLayout(l,r,scale);
        if(!l.contains(event->pos())&&!r.contains(event->pos()))return;
        const bool inRight=r.contains(event->pos());
        const bool rightEye=inRight ? !m_swapSides : m_swapSides;
        const QPoint seed=widgetToImage(event->pos(),inRight ? r : l,scale).toPoint();
        applyColorSelection(rightEye,seed,m_brushMode==9);setFocus();return;
    }
    if ((m_brushMode==7 || m_brushMode==8) && !m_anaglyphMode) {
        if (event->button() != Qt::LeftButton) return;
        QRect l,r; float scale; calculateLayout(l,r,scale);
        if (!l.contains(event->pos()) && !r.contains(event->pos())) return;
        const bool right = r.contains(event->pos()) ? !m_swapSides : m_swapSides;
        const QRect viewport = right != m_swapSides ? r : l;
        const QPointF ip = widgetToImage(event->pos(), viewport, scale);
        const QImage source = right ? m_processor.rightImage() : m_processor.leftImage();
        if (!source.rect().contains(ip.toPoint())) return;
        if (event->modifiers() & Qt::ControlModifier) {
            int nearest=-1; double distance=8.0;
            if (right==m_lassoRight) for (int i=0; i<m_lassoAnchors.size(); ++i) {
                const double d=QLineF(imageToWidget(m_lassoAnchors[i],viewport,scale),QPointF(event->pos())).length();
                if (d<distance) { nearest=i; distance=d; }
            }
            if (nearest>=0) removeLassoPoint(nearest);
            else emit smartStatus(tr("Ctrl-click directly on an outline point to remove it."));
            return;
        }
        if ((event->modifiers() & Qt::ShiftModifier) && right==m_lassoRight && m_lassoAnchors.size()>=2) {
            int edge=-1; double nearest=9.0; QPointF inserted;
            const int edgeCount=m_lassoClosed ? m_lassoAnchors.size() : m_lassoAnchors.size()-1;
            for (int i=0; i<edgeCount; ++i) {
                const QVector<QPointF> trace=i<m_lassoSegments.size() ? m_lassoSegments[i] : m_lassoClosing;
                for (int j=1; j<trace.size(); ++j) {
                    const QPointF a=trace[j-1], delta=trace[j]-a;
                    const double length2=QPointF::dotProduct(delta,delta);
                    const double t=length2>0 ? qBound(0.0,QPointF::dotProduct(ip-a,delta)/length2,1.0) : 0;
                    const QPointF point=a+t*delta;
                    const double distance=QLineF(ip,point).length()*scale;
                    if (distance<nearest) { nearest=distance; edge=i; inserted=point; }
                }
            }
            if (edge>=0) {
                const bool magnetic=edge<m_lassoMagnetic.size() ? m_lassoMagnetic[edge] : m_brushMode==8;
                m_lassoAnchors.insert(edge+1,inserted);
                m_lassoMagnetic.insert(edge,magnetic);
                m_lassoSegments.resize(m_lassoAnchors.size()-1);
                for (int i=0; i<m_lassoSegments.size(); ++i) rebuildLassoSegment(i);
                if (m_lassoClosed) rebuildLassoClosing();
                m_lassoDrag=edge+1; update();
                emit smartStatus(tr("Point inserted. Drag to adjust, then Apply selection."));
            } else emit smartStatus(tr("Shift-click near an outline edge to insert a point."));
            return;
        }
        bool nearAnchor=false;
        if (right==m_lassoRight) for (const auto &p:m_lassoAnchors)
            if (QLineF(imageToWidget(p,viewport,scale),QPointF(event->pos())).length()<=7) nearAnchor=true;
        if (m_lassoClosed && !nearAnchor) finishLasso();
        if (!m_lassoAnchors.isEmpty() && right!=m_lassoRight) {
            emit smartStatus(tr("Close this outline before moving to the other eye."));return;
        }
        for (int i=0; i<m_lassoAnchors.size(); ++i) {
            if (QLineF(imageToWidget(m_lassoAnchors[i],viewport,scale),QPointF(event->pos())).length() <= 7) {
                if (i==0 && !m_lassoClosed && m_lassoAnchors.size()>=3) {
                    closeLasso(); return;
                }
                m_lassoDrag=i; return;
            }
        }
        if (m_lassoClosed) { emit smartStatus(tr("Drag yellow points to adjust. Apply selection to keep the enclosed area.")); return; }
        m_lassoRight=right;
        m_lassoAnchors.append(ip);
        if (m_lassoAnchors.size()>1) {
            m_lassoMagnetic.append(m_brushMode==8);
            m_lassoSegments.append(QVector<QPointF>{}); rebuildLassoSegment(m_lassoSegments.size()-1);
        }
        emit smartStatus(tr("Click along the outline. Shift-click an edge to insert; Ctrl-click a point to remove. Enter or C closes; Apply selection commits."));
        update(); return;
    }
    if (m_brushMode && !m_anaglyphMode && event->button() == Qt::LeftButton) {
        if (m_points.size() < 3) return;
        QRect l,r; float scale; calculateLayout(l,r,scale);
        if (!l.contains(event->pos()) && !r.contains(event->pos())) return;
        m_brushRight = r.contains(event->pos()) ? !m_swapSides : m_swapSides;
        m_smartRight = m_brushRight;
        if (m_brushMode == 3) {
            const QRect viewport = m_brushRight != m_swapSides ? r : l;
            m_regionStart = widgetToImage(event->pos(), viewport, scale);
            m_beforeRegion = m_brushRight ? m_smartRegionRight : m_smartRegionLeft;
            m_selectingRegion = true; update(); return;
        }
        if (m_brushMode >= 4) {
            const QRect region=m_brushRight ? m_smartRegionRight : m_smartRegionLeft;
            if (region.isEmpty()) { emit smartStatus(tr("Draw a Smart box around the leg in this eye first.")); return; }
            m_beforeStroke = m_brushRight ? m_smartHintsRight : m_smartHintsLeft;
        } else m_beforeStroke = revealMask(m_brushRight);
        m_cutout = true; m_brushing = true; m_hasBrushPoint = false;
        brushAt(event->pos()); return;
    }
    m_lastMousePos = event->pos(); m_mouseDragStart = event->pos();
    QRect vL, vR; float scale; calculateLayout(vL, vR, scale);

    if (m_panMode || event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && (event->modifiers() & Qt::AltModifier))) {
        m_isPanning = true; setCursor(Qt::SizeAllCursor); return;
    }

    if (m_freehandMode && event->button() == Qt::LeftButton && m_points.isEmpty()) {
        QRect targetV = vL.contains(event->pos()) ? vL : (vR.contains(event->pos()) && !m_anaglyphMode ? vR : QRect());
        if (!targetV.isNull()) {
            m_freehandViewport = targetV;
            m_freehandScale = scale;
            m_freehandPoints.clear();
            m_isFreehandDrawing = true;
            appendFreehandPoint(event->pos());
            return;
        }
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
            // Extend the outline unless the click is close to an existing edge.
            float minEdgeDist = 10.0f;
            for (int i=0; m_points.count() >= 3 && i<m_points.count(); ++i) {
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
                        const float distance = QLineF(event->pos(), b).length();
                        if (distance < minEdgeDist) { minEdgeDist = distance; insertIdx = nextIdx; }
                    }
                } else {
                    // Linear distance
                    QLineF line(p1W, p2W);
                    QPointF p3 = event->pos();
                    float l2 = line.length() * line.length();
                    if (l2 != 0) {
                        float t = qBound(0.0f, (float)QPointF::dotProduct(p3 - p1W, p2W - p1W) / l2, 1.0f);
                        QPointF projection = p1W + t * (p2W - p1W);
                        const float distance = QLineF(p3, projection).length();
                        if (distance < minEdgeDist) { minEdgeDist = distance; insertIdx = nextIdx; }
                    }
                }
            }

            // Inserting on the closing edge must not rotate the first vertex.
            if (insertIdx == 0) insertIdx = m_points.count();
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
    m_brushCursor = event->pos(); m_cursorVisible = true;
    if (m_refining) return;
    if (m_draggingPolygon && ((event->buttons() & Qt::LeftButton) || event->type()==QEvent::MouseButtonRelease)) {
        QPointF delta=widgetToImage(event->pos(),m_polygonDragViewport,m_polygonDragScale)-m_polygonDragStart;
        if (event->modifiers() & Qt::ShiftModifier) delta.setY(0);
        for (const auto &original:m_beforePolygonDrag.objects) if (original.id==m_draggedPolygonId) {
            const QRectF bounds=original.path.boundingRect();
            const QImage image=original.right ? m_processor.rightImage() : m_processor.leftImage();
            const double minX=-bounds.left(),maxX=image.width()-1-bounds.right();
            const double minY=-bounds.top(),maxY=image.height()-1-bounds.bottom();
            delta.setX(minX<=maxX ? qBound(minX,delta.x(),maxX) : 0);
            delta.setY(minY<=maxY ? qBound(minY,delta.y(),maxY) : 0);
            for (auto &object:m_selectionObjects) if (object.id==m_draggedPolygonId) {
                object=original;object.path=QTransform::fromTranslate(delta.x(),delta.y()).map(original.path);
                for (auto &point:object.anchors) point+=delta;
                break;
            }
            m_polygonDragDelta=delta;++m_smartRevision;rebuildSelectionMasks();update();return;
        }
        return;
    }
    if (m_polygonMoveEnabled && !m_isPanning) {setCursor(Qt::SizeAllCursor);update();return;}
    if (m_isPanning) {
        QRect l,r; float scale; calculateLayout(l,r,scale);
        const QPoint delta=event->pos()-m_lastMousePos;
        m_panOffset+=QPointF(delta.x()/scale,delta.y()/scale);
        m_lastMousePos=event->pos(); update(); return;
    }
    if ((m_brushMode==7 || m_brushMode==8) && !m_pickingColor) {
        setCursor(Qt::CrossCursor);
        if (m_lassoDrag >= 0 && (event->buttons() & Qt::LeftButton)) {
            QRect l,r; float scale; calculateLayout(l,r,scale);
            const QRect viewport=m_lassoRight != m_swapSides ? r : l;
            QPointF p=widgetToImage(event->pos(),viewport,scale);
            const QImage source=m_lassoRight ? m_processor.rightImage() : m_processor.leftImage();
            p.setX(qBound(0.0,p.x(),double(source.width()-1)));
            p.setY(qBound(0.0,p.y(),double(source.height()-1)));
            m_lassoAnchors[m_lassoDrag]=p;
            rebuildLassoSegment(m_lassoDrag-1); rebuildLassoSegment(m_lassoDrag);
            if (m_lassoClosed) rebuildLassoClosing();
        }
        update(); return;
    }
    if (m_selectingRegion) {
        QRect l,r; float scale; calculateLayout(l,r,scale);
        const QRect viewport=m_brushRight != m_swapSides ? r : l;
        const QPointF current=widgetToImage(event->pos(),viewport,scale);
        (m_brushRight ? m_smartRegionRight : m_smartRegionLeft)=QRectF(m_regionStart,current).normalized().toAlignedRect().intersected(m_processor.leftImage().rect());
        update(); return;
    }
    if (m_brushMode && !m_pickingColor) {
        QRect l, r; float scale; calculateLayout(l, r, scale);
        const bool visiblePointer=m_brushMode==3 || m_brushMode>=7;
        setCursor(!m_anaglyphMode && (l.contains(event->pos()) || r.contains(event->pos())) ? (visiblePointer ? Qt::CrossCursor : Qt::BlankCursor) : Qt::ArrowCursor);
        update();
    }
    if (m_pickingColor) return;
    if (m_brushing) { brushAt(event->pos()); return; }
    if (m_isFreehandDrawing) {
        if (event->buttons() & Qt::LeftButton) {
            appendFreehandPoint(event->pos());
        }
        return;
    }

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
    if (m_draggingPolygon && event->button()==Qt::LeftButton) {
        mouseMoveEvent(event);
        const auto before=m_beforePolygonDrag,after=selectionState();
        m_draggingPolygon=false;setCursor(Qt::SizeAllCursor);
        if (!m_polygonDragDelta.isNull())
            m_undoStack->push(new SelectionObjectsCommand(this,before,after,tr("Move polygon")));
        m_beforePolygonDrag=SelectionState{};
        update();return;
    }
    if (m_isPanning) {
        m_isPanning=false;
        setCursor(m_spacePan || m_panMode ? Qt::OpenHandCursor : m_brushMode >= 7 ? Qt::CrossCursor : m_brushMode ? Qt::BlankCursor : Qt::ArrowCursor);
        update(); return;
    }
    if ((m_brushMode==7 || m_brushMode==8) && !m_pickingColor) { m_lassoDrag=-1; return; }
    if (m_selectingRegion && event->button()==Qt::LeftButton) {
        mouseMoveEvent(event); m_selectingRegion=false;
        const QRect region=m_brushRight ? m_smartRegionRight : m_smartRegionLeft;
        const QImage hints=m_brushRight ? m_smartHintsRight : m_smartHintsLeft;
        m_undoStack->push(new SmartHintsCommand(this,m_brushRight,hints,hints,m_beforeRegion,region));
        emit smartStatus(tr("Box selected. Paint Keep inside the leg and Remove on nearby background, then Find edges."));
        update(); return;
    }
    if (m_colorPickClick && event->button() == Qt::LeftButton) {
        m_colorPickClick = false; return;
    }
    if (m_pickingColor) return;
    if (m_brushing && event->button() == Qt::LeftButton) {
        brushAt(event->pos()); m_brushing=false;
        if (m_brushMode>=4) {
            const QImage hints=m_brushRight ? m_smartHintsRight : m_smartHintsLeft;
            const QRect region=m_brushRight ? m_smartRegionRight : m_smartRegionLeft;
            m_undoStack->push(new SmartHintsCommand(this,m_brushRight,m_beforeStroke,hints,region,region));
        } else m_undoStack->push(new RevealStrokeCommand(this, m_brushRight, m_beforeStroke, revealMask(m_brushRight)));
        m_beforeStroke = QImage(); return;
    }
    if (m_isFreehandDrawing) {
        if (event->button() == Qt::LeftButton) {
            appendFreehandPoint(event->pos());
            finishFreehandDrawing();
        }
        return;
    }

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
    if (m_draggingPolygon) {event->accept();return;}
    if (!m_processor.isValid()) {event->ignore();return;}
    QRect vL, vR; float oldScale; calculateLayout(vL, vR, oldScale);
    const bool editingPoints = m_brushMode == 0 || m_brushMode == 7 || m_brushMode == 8;
    if (editingPoints && !(event->modifiers() & Qt::ControlModifier)) {
        const bool horizontal = event->modifiers() & Qt::ShiftModifier;
        const QPoint delta = !event->pixelDelta().isNull() ? event->pixelDelta() : event->angleDelta();
        const float pixels = (horizontal && delta.y() == 0 ? delta.x() : delta.y())
            * (!event->pixelDelta().isNull() ? 1.0f : 48.0f / 120.0f);
        if (horizontal) m_panOffset.rx() += pixels / oldScale;
        else m_panOffset.ry() += pixels / oldScale;
        update();
        event->accept();
        return;
    }
    const int delta = !event->angleDelta().isNull() ? event->angleDelta().y() : event->pixelDelta().y();
    if (!delta) {event->accept();return;}
    QPoint pos = event->position().toPoint();
    QRect targetV = vL.contains(pos) ? vL : (vR.contains(pos) ? vR : QRect());
    if (targetV.isNull()) {
        setZoom(m_zoom * (delta > 0 ? 1.1f : 0.9f));
        event->accept();
        return;
    }

    QPointF imgPos = widgetToImage(pos, targetV, oldScale);
    float factor = (delta > 0 ? 1.1f : 0.9f);
    m_zoom = qBound(0.1f, m_zoom * factor, 20.0f);
    
    float newScale; calculateLayout(vL, vR, newScale);
    float iw = m_processor.leftImage().width();
    float ih = m_processor.leftImage().height();
    m_panOffset.setX((pos.x() - targetV.center().x()) / newScale + iw/2.0 - imgPos.x());
    m_panOffset.setY((pos.y() - targetV.center().y()) / newScale + ih/2.0 - imgPos.y());
    update();
    event->accept();
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

void StereoViewWidget::leaveEvent(QEvent *event) {
    m_cursorVisible = false; update(); QWidget::leaveEvent(event);
}
void StereoViewWidget::keyPressEvent(QKeyEvent *event) {
    if (m_draggingPolygon && event->key()==Qt::Key_Escape) {
        restoreSelectionState(m_beforePolygonDrag);m_beforePolygonDrag=SelectionState{};setCursor(Qt::SizeAllCursor);event->accept();return;
    }
    if (m_processor.isValid() && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        QPointF direction;
        switch (event->key()) {
        case Qt::Key_Left: direction=QPointF(1,0); break;
        case Qt::Key_Right: direction=QPointF(-1,0); break;
        case Qt::Key_Up: direction=QPointF(0,1); break;
        case Qt::Key_Down: direction=QPointF(0,-1); break;
        default: break;
        }
        if (!direction.isNull()) {
            if (!m_brushing && !m_isFreehandDrawing && m_lassoDrag<0 && !m_isPanning && !m_draggingPolygon) {
                QRect left,right; float scale; calculateLayout(left,right,scale);
                const double step=event->modifiers() & Qt::ShiftModifier ? 144.0 : 48.0;
                if (scale>0) m_panOffset+=direction*(step/scale);
                update();
            }
            event->accept(); return;
        }
    }
    if (event->key()==Qt::Key_Space && !event->isAutoRepeat()) { m_spacePan=true; setCursor(Qt::OpenHandCursor); event->accept(); return; }
    if ((m_brushMode==7 || m_brushMode==8) && !m_pickingColor) {
        if ((event->key()==Qt::Key_Return || event->key()==Qt::Key_Enter) &&
            (event->modifiers() & Qt::ControlModifier) &&
            !(event->modifiers() & (Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier))) {
            finishLasso(); event->accept(); return;
        }
        if (event->key()==Qt::Key_Delete || event->key()==Qt::Key_Backspace) { removeLassoSegment(); event->accept(); return; }
        if (event->key()==Qt::Key_Escape) { cancelUnfinishedLasso(); event->accept(); return; }
        if (!(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier)) &&
            (event->key()==Qt::Key_Return || event->key()==Qt::Key_Enter || event->key()==Qt::Key_C)) {
            closeLasso(); event->accept(); return;
        }
    }
    if (m_pickingColor && event->key() == Qt::Key_Escape) {
        m_pickingColor = false;
        setCursor((m_brushMode==3 || m_brushMode>=7) ? Qt::CrossCursor : m_brushMode ? Qt::BlankCursor : Qt::ArrowCursor);
        update(); event->accept(); return;
    }
    QWidget::keyPressEvent(event);
}

void StereoViewWidget::rebuildLassoSegment(int index) {
    if (index < 0 || index >= m_lassoSegments.size()) return;
    const auto a=m_lassoAnchors[index], b=m_lassoAnchors[index+1];
    m_lassoSegments[index] = m_lassoMagnetic[index]
        ? magneticLassoPath(m_lassoRight ? m_processor.rightImage() : m_processor.leftImage(),a,b)
        : QVector<QPointF>{a,b};
}
void StereoViewWidget::rebuildLassoClosing() {
    if (m_lassoAnchors.size()<3) return;
    m_lassoClosing=m_brushMode==8 ? magneticLassoPath(m_lassoRight ? m_processor.rightImage() : m_processor.leftImage(),m_lassoAnchors.last(),m_lassoAnchors.first())
        : QVector<QPointF>{m_lassoAnchors.last(),m_lassoAnchors.first()};
}
void StereoViewWidget::cancelLasso() {
    m_editingSelectionId.clear();
    m_lassoAnchors.clear(); m_lassoSegments.clear(); m_lassoMagnetic.clear(); m_lassoClosing.clear();
    m_lassoClosed=false; m_lassoDrag=-1; update();
}
void StereoViewWidget::removeLassoPoint(int index) {
    if (m_refining || index<0 || index>=m_lassoAnchors.size()) return;
    if (m_lassoClosed && m_lassoAnchors.size()<=3) {
        emit smartStatus(tr("A closed polygon needs at least three points.")); return;
    }
    m_lassoDrag=-1;
    const int last=m_lassoAnchors.size()-1;
    if (index==0 && !m_lassoMagnetic.isEmpty()) m_lassoMagnetic.removeFirst();
    else if (index==last && !m_lassoMagnetic.isEmpty()) m_lassoMagnetic.removeLast();
    else if (index>0 && index<last) {
        m_lassoMagnetic[index-1]=m_lassoMagnetic[index-1] || m_lassoMagnetic[index];
        m_lassoMagnetic.removeAt(index);
    }
    m_lassoAnchors.removeAt(index);
    m_lassoSegments.resize(qMax(0,m_lassoAnchors.size()-1));
    for (int i=0; i<m_lassoSegments.size(); ++i) rebuildLassoSegment(i);
    if (m_lassoClosed) rebuildLassoClosing();
    else m_lassoClosing.clear();
    update(); emit smartStatus(tr("Point removed; neighboring edges reconnected. Apply selection to save the change."));
}
void StereoViewWidget::removeLassoSegment() {
    if (m_lassoAnchors.isEmpty()) return;
    m_lassoClosed=false; m_lassoClosing.clear(); m_lassoDrag=-1;
    m_lassoAnchors.removeLast();
    if (!m_lassoSegments.isEmpty()) { m_lassoSegments.removeLast(); m_lassoMagnetic.removeLast(); }
    update(); emit smartStatus(tr("Latest segment removed; the rest of the outline remains."));
}
void StereoViewWidget::closeLasso() {
    if (m_lassoAnchors.size()<3) { emit smartStatus(tr("Place at least three points before closing the outline.")); return; }
    m_lassoClosed=true; rebuildLassoClosing(); update(); setFocus();
    emit smartStatus(tr("Outline closed. Drag points to adjust, or click elsewhere to keep it and start another. Apply selection or Escape keeps the closed outline."));
}
void StereoViewWidget::applyLasso(bool erase) {
    if (m_refining || !m_processor.isValid() || m_lassoAnchors.size()<3) return;
    QScopedValueRollback<bool> busy(m_refining,true);
    emit smartBusy(true);
    const auto finishBusy=qScopeGuard([this] { emit smartBusy(false); });
    OperationProgress progress(this,tr("StereoMask — Apply selection"),tr("Closing the selection outline…"));
    QScopedValueRollback<OperationProgress *> activeProgress(m_operationProgress,&progress);
    if (!m_lassoClosed) closeLasso();
    QPainterPath path; path.moveTo(m_lassoAnchors.first());
    for (const auto &segment:m_lassoSegments) for (const auto &p:segment) path.lineTo(p);
    for (const auto &p:m_lassoClosing) path.lineTo(p);
    path.closeSubpath();
    m_cutout=true;
    auto state=selectionState();
    SelectionObject object;
    object.id=m_editingSelectionId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : m_editingSelectionId;
    object.name=tr("Polygon %1").arg(state.objects.size()+1);
    object.right=m_lassoRight; object.subtract=erase; object.path=path;
    object.anchors=m_lassoAnchors; object.magnetic=m_lassoMagnetic;
    bool replaced=false;
    for (auto &existing:state.objects) if (existing.id==object.id) {
        object.name=existing.name; object.visible=existing.visible; existing=object; replaced=true; break;
    }
    int matchedAnchors=0;
    const bool paired=!replaced && m_pairedLassoEnabled;
    if (!replaced) state.objects.append(object);
    if (paired) {
        QVector<float> disparities;
        for (const auto &point:m_points) disparities.append(point.disparity);
        std::sort(disparities.begin(),disparities.end());
        const double disparity=disparities.isEmpty() ? 0 : disparities[disparities.size()/2];
        const QImage source=object.right ? m_processor.rightImage() : m_processor.leftImage();
        const QImage opposite=object.right ? m_processor.leftImage() : m_processor.rightImage();
        progress.setStage(tr("Matching the polygon in the opposite eye and tracing its edges…"));
        const int brushMode=m_brushMode;
        const auto pairedResult=progress.run([source,opposite,object,disparity,brushMode] {
        const auto alignment=alignStereoPolygon(source,opposite,object.anchors,object.right ? disparity : -disparity);
        SelectionObject copy=object;
        copy.id=QUuid::createUuid().toString(QUuid::WithoutBraces); copy.right=!object.right;
        copy.anchors=alignment.anchors;
        copy.path=QPainterPath(); copy.path.moveTo(copy.anchors.first());
        for (int i=0; i<copy.anchors.size(); ++i) {
            const QPointF a=copy.anchors[i],b=copy.anchors[(i+1)%copy.anchors.size()];
            const bool magnetic=i<copy.magnetic.size() ? copy.magnetic[i] : brushMode==8;
            if (magnetic) for (const auto &point:magneticLassoPath(opposite,a,b)) copy.path.lineTo(point);
            else copy.path.lineTo(b);
        }
        copy.path.closeSubpath();
        return std::make_pair(copy,alignment.matchedAnchors);
        });
        SelectionObject copy=pairedResult.first;
        copy.name=tr("Polygon %1").arg(state.objects.size()+1);
        matchedAnchors=pairedResult.second;
        state.objects.append(copy);
    }
    QString applyStage=paired ? tr("Applying paired selections…")
        : object.right ? tr("Applying selection to the right eye…") : tr("Applying selection to the left eye…");
    if (m_autoSave) applyStage+=tr(" Saving project…");
    progress.setStage(applyStage);
    m_undoStack->push(new SelectionObjectsCommand(this,state,replaced ? tr("Edit polygon selection") : paired ? tr("Add paired polygon selections") : tr("Add polygon selection")));
    cancelLasso(); setFocus();
    emit smartStatus(paired ? tr("Two independent polygons created (%1/%2 anchors matched). Check the opposite eye and adjust with Edit points.").arg(matchedAnchors).arg(object.anchors.size())
                            : erase ? tr("Lasso subtraction applied to the ordered mask. Undo restores it.") : tr("Lasso area preserved. Undo restores it."));
}

void StereoViewWidget::keyReleaseEvent(QKeyEvent *event) {
    if (event->key()==Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePan=false;
        setCursor(m_panMode ? Qt::OpenHandCursor : m_brushMode >= 7 ? Qt::CrossCursor : m_brushMode ? Qt::BlankCursor : Qt::ArrowCursor);
        event->accept(); return;
    }
    QWidget::keyReleaseEvent(event);
}
void StereoViewWidget::clearPaintedReveal() {
    if (m_refining) return;
    cancelLasso();
    m_undoStack->push(new SelectionObjectsCommand(this,SelectionState{},tr("Clear painted reveal")));
    emit smartStatus(tr("Painted reveal cleared in both eyes; your frame stays open. Undo restores the paint."));
}

namespace {
class StereoWindowCommand : public QUndoCommand {
    StereoViewWidget *m_view;
    int m_before;
    int m_after;
public:
    StereoWindowCommand(StereoViewWidget *view, int after)
        : m_view(view), m_before(view->stereoWindowOffset()), m_after(after)
    {
        setText(QObject::tr("Adjust stereo window"));
    }
    void undo() override { m_view->applyStereoWindowOffset(m_before); }
    void redo() override { m_view->applyStereoWindowOffset(m_after); }
};
}

void StereoViewWidget::setStereoWindowOffset(int pixels)
{
    if (!isImageLoaded() || m_refining || hasLasso() || m_draggingPolygon) return;
    pixels = qBound(-m_processor.leftImage().width() / 2,
                    pixels,
                    m_processor.leftImage().width() / 2);
    if (pixels != stereoWindowOffset())
        m_undoStack->push(new StereoWindowCommand(this, pixels));
}

void StereoViewWidget::applyStereoWindowOffset(int pixels)
{
    m_processor.setStereoWindowOffset(pixels);
    ++m_smartRevision;
    m_exportKeyLeft.clear();
    m_exportKeyRight.clear();
    m_exportCacheLeft = QImage();
    m_exportCacheRight = QImage();
    updateAnaglyphIfActive();
    update();
    if (m_autoSave) saveProject();
}

QImage StereoViewWidget::pictureFrameOverlay(QSize size, const QVector<MaskPoint> &points, bool right,
                                             const QImage &opening, const PictureFrameSettings &settings)
{
    QImage layer(size,QImage::Format_ARGB32_Premultiplied);layer.fill(Qt::transparent);
    if (!settings.enabled || points.size()<3 || opening.size()!=size) return layer;
    const auto path=createImageMaskPath(points,QRectF(QPointF(0,0),QSizeF(size)),1.0f,right,false);
    auto outside=[&](double width){QPainterPathStroker stroke;stroke.setWidth(2*width);stroke.setJoinStyle(Qt::MiterJoin);return stroke.createStroke(path).subtracted(path);};
    const int width=qBound(1,settings.width,1000);
    QPainter painter(&layer);painter.setRenderHint(QPainter::Antialiasing);
    const auto band=outside(width);painter.fillPath(band,settings.color);
    if (settings.style!=0) {
        QLinearGradient bevel(0,0,size.width(),size.height());
        bevel.setColorAt(0,settings.color.lighter(170));bevel.setColorAt(0.5,settings.color);bevel.setColorAt(1,settings.color.darker(180));
        painter.fillPath(band,bevel);
        const double trim=qMax(1.0,width*0.12);
        painter.fillPath(band.subtracted(outside(qMax(0.1,width-trim))),settings.color.lighter(155));
        painter.fillPath(outside(trim),settings.color.darker(180));
        if (settings.style==2) {
            for (double ratio:{0.35,0.68}) {
                const auto groove=outside(width*ratio).subtracted(outside(qMax(0.1,width*ratio-trim*0.6)));
                painter.fillPath(groove,settings.color.darker(170));
            }
        }
    }
    painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    painter.drawImage(0,0,opening);
    return layer;
}

bool StereoViewWidget::saveComposedPair(const QString &file,const QImage &left,const QImage &right,
                                        int px,int py,int gap,const QColor &background)
{
    if (left.isNull() || left.size()!=right.size() || px<0 || py<0 || gap<0) return false;
    QImage result(2*left.width()+2*px+gap,left.height()+2*py,QImage::Format_ARGB32_Premultiplied);
    if(result.isNull())return false;
    result.fill(background);QPainter painter(&result);painter.drawImage(px,py,left);painter.drawImage(px+left.width()+gap,py,right);
    return result.save(file);
}

namespace {
class PictureFrameCommand : public QUndoCommand {
    StereoViewWidget *m_view;PictureFrameSettings m_before,m_after;
public:
    PictureFrameCommand(StereoViewWidget *view,const PictureFrameSettings &next)
        :m_view(view),m_before(view->pictureFrameSettings()),m_after(next){setText(QObject::tr("Change picture frame"));}
    void undo() override {m_view->applyPictureFrameSettings(m_before);}
    void redo() override {m_view->applyPictureFrameSettings(m_after);}
};
}

void StereoViewWidget::setPictureFrameSettings(const PictureFrameSettings &settings)
{
    if (!isImageLoaded()) return;
    auto next=settings;next.shadowOpacity=qBound(0,next.shadowOpacity,100);next.shadowDistance=qBound(0,next.shadowDistance,1000);
    next.shadowBlur=qBound(0,next.shadowBlur,200);next.shadowSpread=qBound(0,next.shadowSpread,200);next.shadowAngle=qBound(0,next.shadowAngle,359);
    next.width=qBound(1,next.width,1000);next.style=qBound(0,next.style,2);
    if(!next.color.isValid())return;
    if (!(next==m_pictureFrame)) m_undoStack->push(new PictureFrameCommand(this,next));
}

void StereoViewWidget::applyPictureFrameSettings(const PictureFrameSettings &settings)
{
    m_pictureFrame=settings;updateAnaglyphIfActive();update();if(m_autoSave)saveProject();
}

namespace {
class FrameShapeCommand : public QUndoCommand {
    StereoViewWidget *m_view;QVector<MaskPoint> m_before,m_after;
public:
    FrameShapeCommand(StereoViewWidget *view,const QVector<MaskPoint> &next)
        :m_view(view),m_before(view->points()),m_after(next){setText(QObject::tr("Create or reshape frame"));}
    void undo()override{m_view->applyFramePoints(m_before);}void redo()override{m_view->applyFramePoints(m_after);}
};
}

void StereoViewWidget::replaceFramePoints(const QVector<MaskPoint> &points)
{
    if(isImageLoaded() && !m_refining && !m_draggingPolygon && !hasLasso())m_undoStack->push(new FrameShapeCommand(this,points));
}

void StereoViewWidget::applyFramePoints(const QVector<MaskPoint> &points)
{
    m_points=points;m_selectedIndices.clear();for(int i=0;i<points.size();++i)m_selectedIndices.append(i);m_selectedPointIndex=-1;
    updateAnaglyphIfActive();notifySelectionState();notifyMaskState();update();if(m_autoSave)saveProject();
}

QImage StereoViewWidget::shadowOverlay(QSize size,const QVector<MaskPoint> &points,bool right,const QImage &opening,const PictureFrameSettings &settings)
{
    QImage result(size,QImage::Format_ARGB32_Premultiplied);result.fill(Qt::transparent);
    if(!settings.shadowEnabled || points.size()<3 || settings.shadowOpacity==0)return result;
    auto path=createImageMaskPath(points,QRectF(QPointF(0,0),QSizeF(size)),1.0f,right,false);
    const double spread=settings.shadowSpread+(settings.enabled?settings.width:0);
    if(spread>0){QPainterPathStroker stroker;stroker.setWidth(spread*2);path=path.united(stroker.createStroke(path));}
    const double angle=qDegreesToRadians(double(settings.shadowAngle));QImage alpha(size,QImage::Format_Grayscale8);alpha.fill(0);
    {QPainter painter(&alpha);painter.setRenderHint(QPainter::Antialiasing);painter.translate(qCos(angle)*settings.shadowDistance,qSin(angle)*settings.shadowDistance);painter.fillPath(path,Qt::white);}
    if(settings.shadowBlur>0){cv::Mat mask(alpha.height(),alpha.width(),CV_8UC1,alpha.bits(),alpha.bytesPerLine());cv::GaussianBlur(mask,mask,cv::Size(),qMax(0.5,settings.shadowBlur/2.0),qMax(0.5,settings.shadowBlur/2.0),cv::BORDER_CONSTANT);}
    for(int y=0;y<size.height();++y){auto row=reinterpret_cast<QRgb *>(result.scanLine(y));const auto values=alpha.constScanLine(y);for(int x=0;x<size.width();++x){QColor color=settings.shadowColor;color.setAlpha(values[x]*settings.shadowOpacity/100);row[x]=qPremultiply(color.rgba());}}
    if(opening.size()==size){QPainter painter(&result);painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);painter.drawImage(0,0,opening);}return result;
}

namespace {
class TextObjectsCommand : public QUndoCommand {
    StereoViewWidget *m_view;QVector<StereoText> m_before,m_after;
public:
    TextObjectsCommand(StereoViewWidget *view,const QVector<StereoText> &next):m_view(view),m_before(view->textObjects()),m_after(next){setText(QObject::tr("Edit stereo text and bubbles"));}
    void undo()override{m_view->applyTextObjects(m_before);}void redo()override{m_view->applyTextObjects(m_after);}
};
}
void StereoViewWidget::setTextObjects(const QVector<StereoText> &texts){if(isImageLoaded())m_undoStack->push(new TextObjectsCommand(this,texts.mid(0,100)));}
QByteArray StereoViewWidget::textOverlayKey(bool right) const {
    QByteArray key;QDataStream stream(&key,QIODevice::WriteOnly);
    stream<<(right ? m_processor.rightImage().cacheKey() : m_processor.leftImage().cacheKey())<<revealMask(right).cacheKey();
    for(const auto &text:m_textObjects)stream<<QJsonDocument(textToJson(text)).toJson(QJsonDocument::Compact);
    return key;
}

QImage StereoViewWidget::cachedTextOverlay(bool right) const {
    if(m_textObjects.isEmpty())return {};
    const QByteArray key=textOverlayKey(right);
    auto &cachedKey=right ? m_textKeyRight : m_textKeyLeft;
    auto &cached=right ? m_textCacheRight : m_textCacheLeft;
    if(cachedKey!=key){cached=textOverlay(m_processor.leftImage().size(),m_textObjects,right,revealMask(right));cachedKey=key;}
    return cached;
}

void StereoViewWidget::applyTextObjects(const QVector<StereoText> &texts) {
    QScopedValueRollback<bool> busy(m_refining,true);
    OperationProgress progress(this,tr("StereoMask - Apply text and bubbles"),tr("Rendering text and bubbles for the left eye…"));
    const QSize size=m_processor.leftImage().size();
    const QImage leftForeground=revealMask(false),rightForeground=revealMask(true);
    const QImage left=progress.run([size,texts,leftForeground] {return texts.isEmpty() ? QImage() : textOverlay(size,texts,false,leftForeground);});
    progress.setStage(tr("Rendering text and bubbles for the right eye…"));
    const QImage right=progress.run([size,texts,rightForeground] {return texts.isEmpty() ? QImage() : textOverlay(size,texts,true,rightForeground);});
    m_textObjects=texts;
    m_textCacheLeft=left;m_textCacheRight=right;
    m_textKeyLeft=textOverlayKey(false);m_textKeyRight=textOverlayKey(true);
    m_exportKeyLeft.clear();m_exportKeyRight.clear();
    progress.setStage(tr("Updating the text and bubbles preview…"));
    exportEye(false);exportEye(true);updateAnaglyphIfActive();
    if(m_autoSave){progress.setStage(tr("Saving text and bubbles in the project…"));saveProject();}
    update();
    emit smartStatus(tr("Text and bubbles updated. Undo restores the previous text objects."));
}
