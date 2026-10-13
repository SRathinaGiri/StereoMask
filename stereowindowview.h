#pragma once

#include <cmath>
#include <QGraphicsView>
#include <QGraphicsPixmapItem>
#include <QWheelEvent>
#include <QResizeEvent>

// Full-resolution stereo-window inspection canvas. Changing depth keeps the
// user's current zoom and inspection area unless Fit is active.
class StereoWindowView : public QGraphicsView
{
    QGraphicsScene m_scene;
    QGraphicsPixmapItem *m_image;
    bool m_fitting = true;
public:
    explicit StereoWindowView(QWidget *parent = nullptr)
        : QGraphicsView(parent), m_scene(this), m_image(m_scene.addPixmap({}))
    {
        setScene(&m_scene);
        setBackgroundBrush(QColor("#202020"));
        setDragMode(QGraphicsView::ScrollHandDrag);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setResizeAnchor(QGraphicsView::AnchorViewCenter);
        setRenderHint(QPainter::SmoothPixmapTransform);
        setMinimumSize(300, 200);
    }

    void setImage(const QImage &value)
    {
        m_image->setPixmap(QPixmap::fromImage(value));
        m_scene.setSceneRect(m_image->boundingRect());
        if (m_fitting) fitImage();
    }

    void fitImage()
    {
        m_fitting = true;
        resetTransform();
        if (!m_image->pixmap().isNull()) fitInView(m_image, Qt::KeepAspectRatio);
    }

    void zoomImage(double factor)
    {
        const double next = transform().m11() * factor;
        if (next < 0.01 || next > 64.0) return;
        m_fitting = false;
        scale(factor, factor);
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QGraphicsView::resizeEvent(event);
        if (m_fitting) fitImage();
    }

    void wheelEvent(QWheelEvent *event) override
    {
        if (event->angleDelta().y())
            zoomImage(std::pow(1.2, event->angleDelta().y() / 120.0));
        event->accept();
    }
};
