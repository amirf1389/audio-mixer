#pragma once
#include <QByteArray>
#include <QObject>

// What Qt Multimedia knows about the audio hardware, as JSON in the shape of /api/interfaces of the PC bridge and the Java app.
class AudioDevices : public QObject
{
    Q_OBJECT
public:
    explicit AudioDevices(QObject *parent = nullptr);
    QByteArray interfacesJson() const;   // {"ok":true,"interfaces":[...],"native":{...}}
    QByteArray nativeJson() const;       // sample rate / channel limits of the preferred devices
    void refresh();                      // after the permission was granted: device names appear only then
};
