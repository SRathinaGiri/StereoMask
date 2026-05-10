#include "stereoimageprocessor.h"
#include <QPainter>
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

void StereoImageProcessor::createAnaglyph()
{
    if (m_left.isNull() || m_right.isNull())
        return;

    m_anaglyph = QImage(m_left.size(), QImage::Format_RGB32);

    for (int y = 0; y < m_left.height(); ++y) {
        const QRgb *leftLine = reinterpret_cast<const QRgb*>(m_left.scanLine(y));
        const QRgb *rightLine = reinterpret_cast<const QRgb*>(m_right.scanLine(y));
        QRgb *dstLine = reinterpret_cast<QRgb*>(m_anaglyph.scanLine(y));

        for (int x = 0; x < m_left.width(); ++x) {
            int r = qRed(leftLine[x]);
            int g = qGreen(rightLine[x]);
            int b = qBlue(rightLine[x]);
            dstLine[x] = qRgb(r, g, b);
        }
    }
}
