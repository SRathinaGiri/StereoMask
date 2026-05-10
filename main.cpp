#include <QApplication>
#include <QLocale>
#include <QImageReader>
#include <QDebug>
#include <QLibraryInfo>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    QApplication a(argc, argv);

    qDebug() << "--- StereoMask Diagnostics ---";
    qDebug() << "Library Path:" << QCoreApplication::libraryPaths();
    qDebug() << "Supported Formats:" << QImageReader::supportedImageFormats();
    qDebug() << "------------------------------";

    MainWindow w;
    w.show();
    return a.exec();
}
