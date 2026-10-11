#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H
#include <QObject>
#include <QNetworkAccessManager>
#include <QUrl>

struct UpdateInfo {
    QString version, notes, packageType;
    QUrl page;
};

class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject *parent = nullptr);
    void check(const QUrl &endpoint = defaultEndpoint());
    static QUrl defaultEndpoint();
    static bool parse(const QByteArray &data, UpdateInfo &info, QString &error);
    static int compareVersions(const QString &available, const QString &installed, bool *valid = nullptr);
signals:
    void finished(bool newer, const UpdateInfo &info, const QString &error);
private:
    QNetworkAccessManager m_network;
    bool m_busy = false;
};
#endif
