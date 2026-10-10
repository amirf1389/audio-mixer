#include "AudioDevices.h"

#include <QAudioDevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaDevices>

AudioDevices::AudioDevices(QObject *parent) : QObject(parent) {}

void AudioDevices::refresh() {}   // QMediaDevices reads the live list on every call; nothing is cached here

static QJsonObject describe(const QAudioDevice &d, const char *kind)
{
    QJsonObject o;
    o["id"] = QString::fromUtf8(d.id().toHex());
    o["name"] = d.description();
    o["kind"] = kind;                                           // "input" | "output"
    o["default"] = d.isDefault();
    o["channels"] = d.maximumChannelCount();
    o["minRate"] = d.minimumSampleRate();
    o["maxRate"] = d.maximumSampleRate();
    return o;
}

QByteArray AudioDevices::interfacesJson() const
{
    QJsonArray list;
    for (const QAudioDevice &d : QMediaDevices::audioInputs()) list.append(describe(d, "input"));
    for (const QAudioDevice &d : QMediaDevices::audioOutputs()) list.append(describe(d, "output"));
    QJsonObject root;
    root["ok"] = true;
    root["engine"] = "qt";
    root["interfaces"] = list;
    root["native"] = QJsonDocument::fromJson(nativeJson()).object();
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray AudioDevices::nativeJson() const
{
    const QAudioDevice out = QMediaDevices::defaultAudioOutput(), in = QMediaDevices::defaultAudioInput();
    QJsonObject o;
    o["sampleRate"] = out.isNull() ? 0 : out.preferredFormat().sampleRate();
    o["inputChannels"] = in.isNull() ? 0 : in.maximumChannelCount();
    o["outputChannels"] = out.isNull() ? 0 : out.maximumChannelCount();
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}
