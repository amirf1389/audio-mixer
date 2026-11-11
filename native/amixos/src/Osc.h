// NETWORK: an Open Sound Control (OSC 1.0) server over UDP for TouchOSC / Mixing Station style remotes. Receives fader, mute, solo, pan,
// master and bus levels; sends peak meters to the last sender at 10 Hz. Binds 127.0.0.1 unless LAN is explicitly allowed (plain UDP,
// no authentication: the page says so; use it only on a network you own).
#pragma once
#include <QHostAddress>
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QUdpSocket>
#include <QVariant>

class Engine;

class Osc : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
  Q_PROPERTY(int port READ port WRITE setPort NOTIFY changed)             // receive port (default 8000)
  Q_PROPERTY(int txPort READ txPort WRITE setTxPort NOTIFY changed)       // meter port on the remote (default 9000)
  Q_PROPERTY(bool allowLan READ allowLan WRITE setAllowLan NOTIFY changed)
  Q_PROPERTY(bool meters READ meters WRITE setMeters NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QString lastClient READ lastClient NOTIFY statsChanged)
  Q_PROPERTY(int rx READ rx NOTIFY statsChanged)
  Q_PROPERTY(int tx READ tx NOTIFY statsChanged)
  Q_PROPERTY(QStringList log READ log NOTIFY statsChanged)                // the last 40 messages, newest last
  Q_PROPERTY(QStringList addresses READ addresses CONSTANT)
  Q_PROPERTY(QVariantList interfaces READ interfaces NOTIFY changed)     // the network cards: [{name, ip, mask, up, kind}]
  Q_PROPERTY(QString bindAddress READ bindAddress WRITE setBindAddress NOTIFY changed)   // "" = every card (LAN) / 127.0.0.1; else one card's IP
  Q_PROPERTY(QString allowedIps READ allowedIps WRITE setAllowedIps NOTIFY changed)      // comma list of remote IPs allowed to control (LAN); empty = the local policy: any private-network address, no public one
  Q_PROPERTY(QString remoteAddress READ remoteAddress NOTIFY changed)                    // what to type into the remote: ip:port
 public:
  explicit Osc(Engine* engine, QObject* parent = nullptr);
  ~Osc() override;
  bool enabled() const { return sock_ != nullptr; } void setEnabled(bool on);
  int port() const { return s_.value("osc/port", 8000).toInt(); } void setPort(int p);
  int txPort() const { return s_.value("osc/txPort", 9000).toInt(); } void setTxPort(int p) { s_.setValue("osc/txPort", std::clamp(p, 1, 65535)); emit changed(); }
  bool allowLan() const { return s_.value("osc/lan", false).toBool(); } void setAllowLan(bool v);
  bool meters() const { return s_.value("osc/meters", true).toBool(); } void setMeters(bool v) { s_.setValue("osc/meters", v); emit changed(); }
  QString status() const { return status_; }
  QString lastClient() const { return client_.isNull() ? QString() : client_.toString() + ":" + QString::number(clientPort_); }
  int rx() const { return rx_; } int tx() const { return tx_; }
  QStringList log() const { return log_; }
  QStringList addresses() const;
  QVariantList interfaces() const;
  QString bindAddress() const { return s_.value("osc/bind").toString(); } void setBindAddress(const QString& ip);
  QString allowedIps() const { return s_.value("osc/allow").toString(); } void setAllowedIps(const QString& list);
  QString remoteAddress() const;
  bool ipAllowed(const QHostAddress& a) const;
  Q_INVOKABLE bool handle(const QByteArray& datagram);                   // one OSC packet (also used by the smoke test)
 signals:
  void changed();
  void statsChanged();
 private:
  void onReadyRead();
  void sendMeters();
  void note(const QString& m);
  Engine* engine_; QSettings s_; QUdpSocket* sock_ = nullptr; QTimer meterTimer_; QHostAddress client_; quint16 clientPort_ = 0; int rx_ = 0, tx_ = 0; QStringList log_; QString status_ = "off";
};
