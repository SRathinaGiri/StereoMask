#include "stereoimageprocessor.h"
#include <QPainter>
#include <QPainterPath>
#include <QDebug>
#include <QImageReader>
#include <QFile>
#include <QBuffer>

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

    QByteArray data = file.readAll();
    QBuffer buffer(&data);
    QImageReader reader(&buffer);
    
    // Disable the default allocation limit (usually 128MB) to handle very large images
    reader.setAllocationLimit(0);

    if (!reader.canRead()) {
        m_lastError = QString("Qt Image IO: %1 (Format: %2)")
                        .arg(reader.errorString())
                        .arg(QString(reader.format()));
        return false;
    }

    QSize imgSize = reader.size();
    qDebug() << "Attempting to load image:" << imgSize.width() << "x" << imgSize.height();

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

    qDebug() << "Image loaded successfully. Size:" << width << "x" << height;

    createAnaglyph();
    return true;
}

void StereoImageProcessor::createAnaglyph(bool swapped, const QVector<MaskPoint>* points, const QColor& maskColor, float opacity)
{
    if (m_left.isNull() || m_right.isNull())
        return;

    m_anaglyph = QImage(m_left.size(), QImage::Format_RGB32);

    const QImage &srcRed = swapped ? m_right : m_left;
    const QImage &srcGB = swapped ? m_left : m_right;

    QImage redImg = srcRed;
    QImage gbImg = srcGB;

    if (points && !points->isEmpty()) {
        auto applyMask = [&](QImage& target, bool isRightEye) {
            QPainter p(&target);
            p.setRenderHint(QPainter::Antialiasing);
            
            QPainterPath path;
            path.addRect(target.rect());
            
            if (!points->isEmpty()) {
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
            p.end();
        };
        applyMask(redImg, swapped);   // If swapped, Red uses Right eye data (which has disparity)
        applyMask(gbImg, !swapped);   // If swapped, GB uses Left eye data (no disparity)
    }

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
