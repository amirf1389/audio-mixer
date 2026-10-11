#include "MixerServer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QUrl>

#include "AudioDevices.h"

#ifdef Q_OS_ANDROID
static const QString kRoot = QStringLiteral("assets:/www");
#else
static const QString kRoot = QStringLiteral("assets/www");    // desktop test runs: android/qt/assets/www next to the binary
#endif

MixerServer::MixerServer(AudioDevices *devices, QObject *parent) : QTcpServer(parent), m_devices(devices)
{
    connect(this, &QTcpServer::newConnection, this, &MixerServer::onConnection);
}

quint16 MixerServer::start(quint16 firstPort, int tries)
{
    for (int i = 0; i < tries; ++i)
        if (listen(QHostAddress::LocalHost, firstPort + i)) return serverPort();
    return 0;
}

void MixerServer::onConnection()
{
    while (QTcpSocket *s = nextPendingConnection()) {
        connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        connect(s, &QTcpSocket::readyRead, s, [this, s] {
            const QByteArray head = s->peek(8192);
            if (!head.contains("\r\n\r\n")) return;                 // wait for the whole request head
            answer(s, s->readAll());
        });
    }
}

void MixerServer::answer(QTcpSocket *s, const QByteArray &request)
{
    const QList<QByteArray> line = request.left(request.indexOf("\r\n")).split(' ');
    if (line.size() < 2 || line[0] != "GET") return send(s, 405, "text/plain", "method not allowed");
    QString path = QUrl::fromPercentEncoding(line[1]).section('?', 0, 0);
    if (path.contains("..") || path.contains('\\')) return send(s, 400, "text/plain", "bad path");   // never leave the page folder
    if (path == "/api/status") {
        QJsonObject o{{"ok", true}, {"name", "audio-mixer-bridge"}, {"version", QCoreApplication::applicationVersion()}, {"engine", "qt"}, {"streams", QJsonObject()}};
        o["native"] = QJsonDocument::fromJson(m_devices->nativeJson()).object();
        return send(s, 200, "application/json", QJsonDocument(o).toJson(QJsonDocument::Compact));
    }
    if (path == "/api/interfaces") return send(s, 200, "application/json", m_devices->interfacesJson());
    if (path == "/") path = "/index.html";
    QFile f(kRoot + path);
    if (!f.open(QIODevice::ReadOnly)) return send(s, 404, "text/plain", "not found");
    send(s, 200, mimeFor(path), f.readAll());
}

void MixerServer::send(QTcpSocket *s, int status, const QByteArray &type, const QByteArray &body)
{
    const char *text = status == 200 ? "OK" : status == 400 ? "Bad Request" : status == 404 ? "Not Found" : "Method Not Allowed";
    s->write("HTTP/1.1 " + QByteArray::number(status) + " " + text + "\r\nContent-Type: " + type + "\r\nContent-Length: " + QByteArray::number(body.size())
             + "\r\nCache-Control: no-cache\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n");
    s->write(body);
    s->disconnectFromHost();
}

QByteArray MixerServer::mimeFor(const QString &n)
{
    if (n.endsWith(".html")) return "text/html; charset=utf-8";
    if (n.endsWith(".js")) return "text/javascript; charset=utf-8";
    if (n.endsWith(".css")) return "text/css; charset=utf-8";
    if (n.endsWith(".json")) return "application/json";
    if (n.endsWith(".woff2")) return "font/woff2";
    if (n.endsWith(".svg")) return "image/svg+xml";
    if (n.endsWith(".png")) return "image/png";
    return "application/octet-stream";
}
