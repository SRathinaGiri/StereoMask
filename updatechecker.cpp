#include "updatechecker.h"
#include "appversion.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QVersionNumber>
#include <QTimer>
#include <memory>

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent) {}

QUrl UpdateChecker::defaultEndpoint() {
    return QUrl("https://api.github.com/repos/SRathinaGiri/StereoMask/contents/updates.json?ref=master");
}

int UpdateChecker::compareVersions(const QString &available, const QString &installed, bool *valid) {
    const QRegularExpression pattern("^v?([0-9]{1,6})\\.([0-9]{1,6})\\.([0-9]{1,6})$");
    const auto a = pattern.match(available), b = pattern.match(installed);
    const bool ok = a.hasMatch() && b.hasMatch();
    if (valid) *valid = ok;
    if (!ok) return 0;
    return QVersionNumber::compare(QVersionNumber(a.captured(1).toInt(),a.captured(2).toInt(),a.captured(3).toInt()),
                                  QVersionNumber(b.captured(1).toInt(),b.captured(2).toInt(),b.captured(3).toInt()));
}

bool UpdateChecker::parse(const QByteArray &data, UpdateInfo &info, QString &error) {
    error.clear();
    info = UpdateInfo{};
    const auto doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) { error = tr("GitHub returned an unreadable update notice."); return false; }
    const auto obj = doc.object();
    info.version = obj.value("version").toString();
    bool valid;
    compareVersions(info.version, STEREOMASK_VERSION, &valid);
    info.page = QUrl(obj.value("page_url").toString());
    info.notes = obj.value("notes").toString().left(4000);
    info.packageType = obj.value("package_type").toString();
    if (!valid || !info.page.isValid() || info.page.scheme() != "https" || info.page.host() != "github.com"
        || !info.page.userInfo().isEmpty() || (info.page.port() != -1 && info.page.port() != 443)
        || !(info.page.path() == "/SRathinaGiri/StereoMask" || info.page.path().startsWith("/SRathinaGiri/StereoMask/"))
        || (info.packageType != "source" && info.packageType != "app")) {
        error = tr("GitHub returned an invalid update notice."); return false;
    }
    return true;
}

void UpdateChecker::check(const QUrl &endpoint) {
    if (m_busy) return;
    m_busy = true;
    QNetworkRequest request(endpoint);
    request.setRawHeader("Accept", "application/vnd.github.raw+json");
    request.setRawHeader("User-Agent", QByteArray("StereoMask-Windows/") + STEREOMASK_VERSION);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setTransferTimeout(15000);
    auto reply = m_network.get(request);
    auto data = std::make_shared<QByteArray>();
    auto tooLarge = std::make_shared<bool>(false);
    auto timedOut = std::make_shared<bool>(false);
    auto timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, [reply, timedOut] { *timedOut = true; reply->abort(); });
    timer->start(20000); // Total deadline as well as the transfer's inactivity timeout.
    connect(reply, &QNetworkReply::readyRead, this, [reply,data,tooLarge] {
        data->append(reply->read(65537 - data->size()));
        if (data->size() > 65536) { *tooLarge = true; reply->abort(); }
    });
    connect(reply, &QNetworkReply::finished, this, [this,reply,data,tooLarge,timedOut,timer] {
        timer->stop();
        m_busy = false;
        QString error;
        UpdateInfo info;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (*timedOut) error = tr("The update check timed out. Please try again later.");
        else if (*tooLarge) error = tr("GitHub returned an oversized update notice.");
        else if (reply->error() != QNetworkReply::NoError || status != 200) {
            if (status == 404) error = tr("The update notice is not available on GitHub yet.");
            else if (status == 403 || status == 429) error = tr("GitHub is limiting update checks. Please try again later.");
            else error = tr("Could not check GitHub for updates. Check your internet connection and try again.");
        } else {
            data->append(reply->read(65537 - data->size()));
            if (data->size() > 65536) error = tr("GitHub returned an oversized update notice.");
            else parse(*data, info, error);
        }
        reply->deleteLater();
        emit finished(error.isEmpty() && compareVersions(info.version, STEREOMASK_VERSION) > 0, info, error);
    });
}
