#pragma once
#include <QTcpServer>

class AudioDevices;

// A very small HTTP/1.1 server on 127.0.0.1: static files from assets:/www and two JSON routes. GET only; no WebSocket PCM streaming (see README).
class MixerServer : public QTcpServer
{
    Q_OBJECT
public:
    explicit MixerServer(AudioDevices *devices, QObject *parent = nullptr);
    quint16 start(quint16 firstPort, int tries);   // the first free port from firstPort on; 0 when none is free

private:
    void onConnection();
    void answer(QTcpSocket *s, const QByteArray &request);
    static void send(QTcpSocket *s, int status, const QByteArray &type, const QByteArray &body);
    static QByteArray mimeFor(const QString &name);
    AudioDevices *m_devices;
};
