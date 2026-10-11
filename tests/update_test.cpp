#include "updatechecker.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <iostream>
#include <cstdlib>

static void require(bool ok, const char *message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
static QByteArray notice(const QString &version, const QString &url = "https://github.com/SRathinaGiri/StereoMask") {
    return QJsonDocument(QJsonObject{{"version",version},{"page_url",url},{"notes","Example notes"},{"package_type","source"}}).toJson();
}
static void httpCheck(const QByteArray &body, int status, bool expectedNewer, bool expectedError) {
    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost), "Cannot start test HTTP server");
    int requests = 0;
    QObject::connect(&server,&QTcpServer::newConnection,&server,[&] {
        auto socket = server.nextPendingConnection();
        QObject::connect(socket,&QTcpSocket::readyRead,socket,[socket,body,status,&requests] {
            QByteArray request = socket->property("request").toByteArray() + socket->readAll();
            socket->setProperty("request",request);
            if (!request.contains("\r\n\r\n") || socket->property("sent").toBool()) return;
            socket->setProperty("sent",true); ++requests;
            require(request.contains("Accept: application/vnd.github.raw+json"), "Missing GitHub raw Accept header");
            socket->write("HTTP/1.1 " + QByteArray::number(status) + " Test\r\nContent-Type: application/json\r\nContent-Length: "
                          + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
        QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
    });
    UpdateChecker checker;
    QEventLoop loop;
    bool finished = false;
    QObject::connect(&checker,&UpdateChecker::finished,&loop,[&](bool newer,const UpdateInfo &,const QString &error) {
        require(newer == expectedNewer, "Wrong update availability");
        require(!error.isEmpty() == expectedError, "Wrong update error state");
        finished = true; loop.quit();
    });
    QTimer::singleShot(4000,&loop,&QEventLoop::quit);
    const QUrl url(QString("http://127.0.0.1:%1/updates.json").arg(server.serverPort()));
    checker.check(url); checker.check(url); // A repeated click cannot start another request.
    loop.exec();
    require(finished && requests == 1,"Check stalled or duplicate request occurred");
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    bool valid = false;
    require(UpdateChecker::compareVersions("2.0.10","2.0.9",&valid)>0 && valid,"Versions compared lexically");
    require(UpdateChecker::compareVersions("v2.1.0","2.0.10",&valid)>0 && valid,"Minor version comparison failed");
    require(UpdateChecker::compareVersions("2.0.10","2.0.10")==0,"Equal version comparison failed");
    UpdateChecker::compareVersions("2.0.10-preview","2.0.10",&valid); require(!valid,"Invalid version accepted");
    UpdateInfo info; QString error;
    require(!UpdateChecker::parse(notice("2.0.11","https://github.com.evil.test/SRathinaGiri/StereoMask"),info,error),"Untrusted host accepted");
    require(!UpdateChecker::parse(notice("2.0.11","https://github.com/SRathinaGiri/StereoMask-evil"),info,error),"Wrong repository accepted");
    require(!UpdateChecker::parse(notice("2.0.11","http://github.com/SRathinaGiri/StereoMask"),info,error),"Insecure URL accepted");
    require(!UpdateChecker::parse(notice("nonsense"),info,error),"Invalid manifest version accepted");
    httpCheck(notice("2.1.1"),200,true,false);
    httpCheck(notice("2.1.0"),200,false,false);
    httpCheck(notice("2.0.9"),200,false,false);
    httpCheck("not JSON",200,false,true);
    httpCheck("{}",404,false,true);
    httpCheck("{}",403,false,true);
    httpCheck(QByteArray(70000,'x'),200,false,true);
    std::cout << "Numeric versions, trusted URLs, newer/equal/older responses, malformed data and HTTP failures, size limit and duplicate-check guard: PASS\n";
    return 0;
}
