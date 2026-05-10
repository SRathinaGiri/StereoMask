#ifndef STEREOIMAGEPROCESSOR_H
#define STEREOIMAGEPROCESSOR_H

#include <QImage>

class StereoImageProcessor
{
public:
    StereoImageProcessor();

    bool loadSideBySide(const QString &fileName);
    
    QImage leftImage() const { return m_left; }
    QImage rightImage() const { return m_right; }
    QImage anaglyphImage() const { return m_anaglyph; }
    QString lastError() const { return m_lastError; }

    bool isValid() const { return !m_left.isNull() && !m_right.isNull(); }

private:
    void createAnaglyph();

    QImage m_left;
    QImage m_right;
    QImage m_anaglyph;
    QString m_lastError;
};

#endif // STEREOIMAGEPROCESSOR_H
