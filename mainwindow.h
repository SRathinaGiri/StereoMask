#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QColor>
#include <QJsonObject>

class StereoViewWidget;
class QSpinBox;
class QSlider;

#include <QStringList>
#include <QTimer>

struct AppSettings {
    QColor maskColor = Qt::black;
    float opacity = 0.6f;
    int padx = 0;
    int pady = 0;
    QColor bgColor = Qt::white;
    int interleavingSpace = 0;
    bool autoSave = true;
    QStringList recentFiles;

    void load();
    void save() const;
    QJsonObject toJson() const;
    void fromJson(const QJsonObject &obj);
    void addRecentFile(const QString &path);
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void newProject();
    void openProject();
    void saveProject();
    void exportImage();
    void updatePointDisparity(int value);
    void clearPoints();
    void setAnaglyphMode(bool enabled);
    void toggleSwapSides(bool enabled);
    void showSettings();
    void showHelp();
    void showAbout();
    void openRecentFile();

private:
    void createMenus();
    void createToolbar();
    void setupConnections();
    void updateRecentFileActions();

    StereoViewWidget *m_viewWidget;
    AppSettings m_settings;
    QMenu *m_recentFilesMenu;
    QAction *m_recentFileActions[5];
    QSpinBox *m_opacitySpin;
};

#endif // MAINWINDOW_H
