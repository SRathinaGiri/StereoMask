#include "stereoimageprocessor.h"
#include <QPainter>
#include <QPainterPath>
#include <QDebug>
#include <QImageReader>
#include <QFile>
#include <QtMath>

StereoImageProcessor::StereoImageProcessor()
{
}

bool StereoImageProcessor::loadSideBySide(const QString &fileName)
{
    m_lastError = QString();
    
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = QString("File System: %1").arg(file.errorString());
        return false;
    }

    QImageReader reader(&file);
    reader.setAutoTransform(true);
    reader.setAllocationLimit(512);

    if (!reader.canRead()) {
        m_lastError = QString("Qt Image IO: %1 (Format: %2)")
                        .arg(reader.errorString())
                        .arg(QString(reader.format()));
        return false;
    }

    QSize imgSize = reader.size();
    qDebug() << "Attempting to load image:" << imgSize.width() << "x" << imgSize.height();

    if (!imgSize.isValid() || imgSize.width() < 2 || imgSize.height() < 1 || (imgSize.width() % 2) != 0) {
        m_lastError = QString("Invalid SBS image size: %1x%2. Width must be an even number.")
                        .arg(imgSize.width())
                        .arg(imgSize.height());
        return false;
    }

    constexpr qsizetype maxPixels = 20000LL * 20000LL;
    const qsizetype pixelCount = qsizetype(imgSize.width()) * qsizetype(imgSize.height());
    if (pixelCount <= 0 || pixelCount > maxPixels) {
        m_lastError = QString("Image is too large: %1x%2.")
                        .arg(imgSize.width())
                        .arg(imgSize.height());
        return false;
    }

    QImage fullImage = reader.read();
    if (fullImage.isNull()) {
        m_lastError = QString("Decode Error: %1 (Size: %2x%3)")
                        .arg(reader.errorString())
                        .arg(imgSize.width())
                        .arg(imgSize.height());
        return false;
    }

    if (fullImage.format() != QImage::Format_RGB32 && fullImage.format() != QImage::Format_ARGB32) {
        fullImage = fullImage.convertToFormat(QImage::Format_RGB32);
    }

    int width = fullImage.width() / 2;
    int height = fullImage.height();

    m_left = fullImage.copy(0, 0, width, height);
    m_right = fullImage.copy(width, 0, width, height);
    m_originalLeft = m_left;
    m_originalRight = m_right;
    m_windowOffset = 0;

    qDebug() << "Image loaded successfully. Size:" << width << "x" << height;

    createAnaglyph();
    return true;
}

void StereoImageProcessor::createAnaglyph(bool swapped, const QVector<MaskPoint>* points, const QColor& maskColor, float opacity,
                                          const QImage *revealLeft, const QImage *revealRight,
                                          const QImage *openingLeft, const QImage *openingRight)
{
    if (m_left.isNull() || m_right.isNull())
        return;

    m_anaglyph = QImage(m_left.size(), QImage::Format_RGB32);

    const QImage &srcRed = swapped ? m_right : m_left;
    const QImage &srcGB = swapped ? m_left : m_right;

    QImage redImg = srcRed;
    QImage gbImg = srcGB;

    if ((points && !points->isEmpty()) || openingLeft || openingRight) {
        auto applyMask = [&](QImage& target, bool isRightEye) {
            QImage overlay(target.size(), QImage::Format_ARGB32_Premultiplied);
            overlay.fill(Qt::transparent);
            QPainter p(&overlay);
            p.setRenderHint(QPainter::Antialiasing);
            
            QPainterPath path;
            path.addRect(target.rect());
            
            if (points && !points->isEmpty()) {
                for (int i = 0; i < points->count(); ++i) {
                    const auto &pt = (*points)[i];
                    float d = isRightEye ? pt.disparity : 0;
                    QPointF pPos(pt.pos.x() - d, pt.pos.y());
                    
                    if (i == 0) {
                        path.moveTo(pPos);
                    } else {
                        const auto &prev = (*points)[i-1];
                        if (prev.isCurve) {
                            float d1 = isRightEye ? prev.cp1Disparity : 0;
                            float d2 = isRightEye ? prev.cp2Disparity : 0;
                            path.cubicTo(QPointF(prev.cp1.x() - d1, prev.cp1.y()),
                                         QPointF(prev.cp2.x() - d2, prev.cp2.y()),
                                         pPos);
                        } else {
                            path.lineTo(pPos);
                        }
                    }
                }
                
                // Close path
                if (points->count() > 2) {
                    const auto &last = points->last();
                    float dS = isRightEye ? (*points)[0].disparity : 0;
                    QPointF sPos((*points)[0].pos.x() - dS, (*points)[0].pos.y());
                    if (last.isCurve) {
                        float d1 = isRightEye ? last.cp1Disparity : 0;
                        float d2 = isRightEye ? last.cp2Disparity : 0;
                        path.cubicTo(QPointF(last.cp1.x() - d1, last.cp1.y()),
                                     QPointF(last.cp2.x() - d2, last.cp2.y()),
                                     sPos);
                    } else {
                        path.lineTo(sPos);
                    }
                }
            }
            path.closeSubpath();
            
            QColor fill = maskColor;
            fill.setAlphaF(opacity);
            p.setPen(Qt::NoPen);
            p.fillPath(path, fill);
            const QImage *reveal = isRightEye ? revealRight : revealLeft;
            if (reveal && !reveal->isNull()) {
                p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
                p.drawImage(0, 0, *reveal);
            }
            const QImage *opening=isRightEye ? openingRight : openingLeft;
            if (opening && !opening->isNull()) {
                p.setCompositionMode(QPainter::CompositionMode_Source);
                p.fillRect(overlay.rect(),fill);
                p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
                p.drawImage(0,0,*opening);
            }
            p.end();
            QPainter composite(&target);
            composite.drawImage(0, 0, overlay);
        };
        applyMask(redImg, swapped);   // If swapped, Red uses Right eye data (which has disparity)
        applyMask(gbImg, !swapped);   // If swapped, GB uses Left eye data (no disparity)
    }

    createAnaglyphFromEyes(redImg,gbImg);
}

void StereoImageProcessor::createAnaglyphFromEyes(const QImage &left, const QImage &right, bool swapped) {
    if (left.isNull() || left.size()!=right.size()) return;
    const QImage redImg=(swapped ? right : left).convertToFormat(QImage::Format_RGB32);
    const QImage gbImg=(swapped ? left : right).convertToFormat(QImage::Format_RGB32);
    m_anaglyph=QImage(left.size(),QImage::Format_RGB32);
    for (int y = 0; y < redImg.height(); ++y) {
        const QRgb *redLine = reinterpret_cast<const QRgb*>(redImg.scanLine(y));
        const QRgb *gbLine = reinterpret_cast<const QRgb*>(gbImg.scanLine(y));
        QRgb *dstLine = reinterpret_cast<QRgb*>(m_anaglyph.scanLine(y));

        for (int x = 0; x < redImg.width(); ++x) {
            int r = qRed(redLine[x]);
            int g = qGreen(gbLine[x]);
            int b = qBlue(gbLine[x]);
            dstLine[x] = qRgb(r, g, b);
        }
    }
}

QImage StereoImageProcessor::shiftedEye(const QImage &source, int shift)
{
    if (shift == 0) return source;
    QImage result(source.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.drawImage(shift, 0, source);
    return result;
}

void StereoImageProcessor::setStereoWindowOffset(int pixels)
{
    if (!isValid() || m_originalLeft.isNull()) return;
    m_windowOffset = qBound(-m_originalLeft.width() / 2, pixels, m_originalLeft.width() / 2);
    const int leftShift = qCeil(m_windowOffset / 2.0);
    const int rightShift = leftShift - m_windowOffset;
    m_left = shiftedEye(m_originalLeft, leftShift);
    m_right = shiftedEye(m_originalRight, rightShift);
    createAnaglyph();
}

QImage StereoImageProcessor::stereoWindowPreview(int pixels, bool anaglyph, bool swapped) const
{
    if (!isValid() || m_originalLeft.isNull()) return {};
    pixels = qBound(-m_originalLeft.width() / 2, pixels, m_originalLeft.width() / 2);
    const int leftShift = qCeil(pixels / 2.0);
    const QImage left = shiftedEye(m_originalLeft, leftShift);
    const QImage right = shiftedEye(m_originalRight, leftShift - pixels);
    if (anaglyph) {
        StereoImageProcessor preview;
        preview.createAnaglyphFromEyes(left, right, swapped);
        return preview.anaglyphImage();
    }

    QImage pair(left.width() * 2, left.height(), QImage::Format_ARGB32_Premultiplied);
    pair.fill(Qt::transparent);
    QPainter painter(&pair);
    painter.drawImage(0, 0, swapped ? right : left);
    painter.drawImage(left.width(), 0, swapped ? left : right);
    return pair;
}
