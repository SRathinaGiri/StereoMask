#include <QApplication>
#include <QLocale>
#include <QImageReader>
#include <QDebug>
#include <QLibraryInfo>
#include "mainwindow.h"
#include "appversion.h"

int main(int argc, char *argv[])
{
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    QApplication a(argc, argv);
    a.setApplicationVersion(STEREOMASK_VERSION);

    qDebug() << "--- StereoMask Diagnostics ---";
    qDebug() << "Library Path:" << QCoreApplication::libraryPaths();
    qDebug() << "Supported Formats:" << QImageReader::supportedImageFormats();
    qDebug() << "------------------------------";

    MainWindow w;
    w.setWindowTitle("StereoMask v2.2.0");
    w.showMaximized();
    return a.exec();
}
