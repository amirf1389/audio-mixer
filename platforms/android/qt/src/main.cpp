// Audio Mixer, Qt edition for Android: a Qt Quick shell around the mixer page.
//   MixerServer   serves the staged page (assets:/www) and /api/status, /api/interfaces on 127.0.0.1 (the page runs from http://localhost, like in the Java app)
//   AudioDevices  the audio inputs / outputs Qt Multimedia sees, as the JSON the page's LIVE SOURCES panel reads
//   Main.qml      a WebView on the page; asks for the microphone permission first
#include <QGuiApplication>
#include <QMicrophonePermission>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtWebView>

#include "AudioDevices.h"
#include "MixerServer.h"

int main(int argc, char *argv[])
{
    QtWebView::initialize();                      // must come before QGuiApplication
    QGuiApplication app(argc, argv);
    app.setApplicationName("Audio Mixer");
    app.setApplicationVersion(QStringLiteral(AM_VERSION));

    AudioDevices devices;
    MixerServer server(&devices);
    const quint16 port = server.start(8765, 10);  // the same port range as the Java app

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("mixerPort", port);
    engine.rootContext()->setContextProperty("appVersion", QStringLiteral(AM_VERSION));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("AudioMixer", "Main");

    QMicrophonePermission mic;                    // Android asks once; the page's own audio engine and the native inputs need it
    if (app.checkPermission(mic) == Qt::PermissionStatus::Undetermined)
        app.requestPermission(mic, [&devices] { devices.refresh(); });
    return app.exec();
}
