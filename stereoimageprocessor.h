#ifndef STEREOIMAGEPROCESSOR_H
#define STEREOIMAGEPROCESSOR_H

#include <QImage>
#include <QVector>
#include <QColor>
#include "maskpoint.h"

class StereoImageProcessor
{
public:
    StereoImageProcessor();

    int stereoWindowOffset() const { return m_windowOffset; }
    void setStereoWindowOffset(int pixels);
    QImage stereoWindowPreview(int pixels, bool anaglyph = true, bool swapped = false) const;
    static QImage shiftedEye(const QImage &source, int shift);
    bool loadSideBySide(const QString &fileName);
    
    QImage leftImage() const { return m_left; }
    QImage rightImage() const { return m_right; }
    QImage anaglyphImage() const { return m_anaglyph; }
    QString lastError() const { return m_lastError; }

    bool isValid() const { return !m_left.isNull() && !m_right.isNull(); }

    void createAnaglyph(bool swapped = false, const QVector<MaskPoint>* points = nullptr, const QColor& maskColor = Qt::black, float opacity = 0.6f,
                        const QImage *revealLeft = nullptr, const QImage *revealRight = nullptr,
                        const QImage *openingLeft = nullptr, const QImage *openingRight = nullptr);
    void createAnaglyphFromEyes(const QImage &left, const QImage &right, bool swapped = false);

private:

    QImage m_originalLeft;
    QImage m_originalRight;
    int m_windowOffset = 0;
    QImage m_left;
    QImage m_right;
    QImage m_anaglyph;
    QString m_lastError;
};

#endif // STEREOIMAGEPROCESSOR_H
