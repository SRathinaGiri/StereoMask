#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QImage>

class StereoViewWidget;
class QSlider;
class QSpinBox;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void openImage();
    void saveImage();
    void updatePointDisparity(int value);
    void setAnaglyphMode(bool enabled);
    void toggleSwapSides(bool enabled);
    void clearPoints();

private:
    void createMenus();
    void createDockWidgets();

    StereoViewWidget *m_viewWidget;
    
    // Mask controls
    QSlider *m_disparitySlider;
    QSpinBox *m_disparitySpinBox;
};

#endif // MAINWINDOW_H
