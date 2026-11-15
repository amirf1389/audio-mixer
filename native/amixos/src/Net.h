// NETWORK AUDIO AND MIDI for the console (engine/cpp/app/net): an AES67 sender (the master, and the strips before their faders when more
// than two channels are asked for), an AES67 receiver whose channels become mixer inputs, the list of streams announced on the network,
// network MIDI (RTP-MIDI) and the MIDI inputs of this computer moving faders, pans, mutes and solos.
// Everything is off until switched on. Network audio is not encrypted and has no password: the page says so. The network MIDI session
// answers this computer only unless LAN is allowed.
#pragma once
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVariant>

#include "net/aoip.hpp"
#include "net/midi.hpp"
#include "net/ptp.hpp"

class Engine;

class Net : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool txOn READ txOn NOTIFY changed)
  Q_PROPERTY(QString txName READ txName WRITE setTxName NOTIFY changed)
  Q_PROPERTY(QString txAddress READ txAddress WRITE setTxAddress NOTIFY changed)
  Q_PROPERTY(int txPort READ txPort WRITE setTxPort NOTIFY changed)
  Q_PROPERTY(int txChannels READ txChannels WRITE setTxChannels NOTIFY changed)   // 1 mono sum, 2 master, 3.. master + strips before their faders (8 at most)
  Q_PROPERTY(int txBits READ txBits WRITE setTxBits NOTIFY changed)
  Q_PROPERTY(bool rxOn READ rxOn NOTIFY changed)
  Q_PROPERTY(QString rxAddress READ rxAddress WRITE setRxAddress NOTIFY changed)   // a multicast group, or 0.0.0.0 for a stream sent straight to this computer
  Q_PROPERTY(int rxPort READ rxPort WRITE setRxPort NOTIFY changed)
  Q_PROPERTY(int rxChannels READ rxChannels WRITE setRxChannels NOTIFY changed)
  Q_PROPERTY(int rxBits READ rxBits WRITE setRxBits NOTIFY changed)
  Q_PROPERTY(int rxRate READ rxRate WRITE setRxRate NOTIFY changed)
  Q_PROPERTY(int rxLatency READ rxLatency WRITE setRxLatency NOTIFY changed)       // jitter buffer, ms
  Q_PROPERTY(int rxFirstStrip READ rxFirstStrip WRITE setRxFirstStrip NOTIFY changed)   // network channel 1 lands on this strip (1-based), the next ones follow; 0 = not patched
  Q_PROPERTY(QString card READ card WRITE setCard NOTIFY changed)                  // the local IPv4 address to use; "" = the system's choice
  Q_PROPERTY(QStringList cards READ cards NOTIFY changed)
  Q_PROPERTY(QString message READ message NOTIFY changed)
  Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)
  Q_PROPERTY(bool discoverOn READ discoverOn NOTIFY changed)
  Q_PROPERTY(QVariantList streams READ streams NOTIFY statsChanged)                // [{name, origin, address, port, channels, bits, rate, clock, age}]
  Q_PROPERTY(QVariantList transports READ transports CONSTANT)                    // [{id, name, available, note}]
  Q_PROPERTY(bool ptpOn READ ptpOn NOTIFY changed)                                // the network clock monitor (PTP v2, listen only)
  Q_PROPERTY(QVariantList ptpMasters READ ptpMasters NOTIFY statsChanged)          // [{id, from, domain, priority1, priority2, clockClass, clockName, syncPerSec, age, steps}], best first
  Q_PROPERTY(bool midiNetOn READ midiNetOn NOTIFY changed)
  Q_PROPERTY(int midiNetPort READ midiNetPort WRITE setMidiNetPort NOTIFY changed)
  Q_PROPERTY(bool midiLan READ midiLan WRITE setMidiLan NOTIFY changed)
  Q_PROPERTY(QVariantList midiInputs READ midiInputs NOTIFY changed)               // [{id, name}]
  Q_PROPERTY(QString midiPort READ midiPort NOTIFY changed)                        // the open one's id, "" = none
  Q_PROPERTY(QString midiLast READ midiLast NOTIFY statsChanged)
  Q_PROPERTY(int midiCount READ midiCount NOTIFY statsChanged)
  Q_PROPERTY(bool midiLearning READ midiLearning NOTIFY statsChanged)
  Q_PROPERTY(int midiLearned READ midiLearned NOTIFY statsChanged)
 public:
  explicit Net(Engine* engine, QObject* parent = nullptr);
  ~Net() override;
  bool txOn() const { return tx_.running(); } bool rxOn() const { return rx_.running(); }
  QString txName() const { return s_.value("net/txName", "AmF X1").toString(); } void setTxName(const QString& v) { s_.setValue("net/txName", v.left(60)); emit changed(); }
  QString txAddress() const { return s_.value("net/txAddress", "239.69.0.1").toString(); } void setTxAddress(const QString& v) { s_.setValue("net/txAddress", v.trimmed()); emit changed(); }
  int txPort() const { return s_.value("net/txPort", 5004).toInt(); } void setTxPort(int v) { s_.setValue("net/txPort", std::clamp(v, 1024, 65535)); emit changed(); }
  int txChannels() const { return s_.value("net/txChannels", 2).toInt(); } void setTxChannels(int v) { s_.setValue("net/txChannels", std::clamp(v, 1, 8)); emit changed(); }
  int txBits() const { return s_.value("net/txBits", 24).toInt(); } void setTxBits(int v) { s_.setValue("net/txBits", v == 16 ? 16 : 24); emit changed(); }
  QString rxAddress() const { return s_.value("net/rxAddress", "239.69.0.2").toString(); } void setRxAddress(const QString& v) { s_.setValue("net/rxAddress", v.trimmed()); emit changed(); }
  int rxPort() const { return s_.value("net/rxPort", 5004).toInt(); } void setRxPort(int v) { s_.setValue("net/rxPort", std::clamp(v, 1024, 65535)); emit changed(); }
  int rxChannels() const { return s_.value("net/rxChannels", 2).toInt(); } void setRxChannels(int v) { s_.setValue("net/rxChannels", std::clamp(v, 1, 16)); emit changed(); }
  int rxBits() const { return s_.value("net/rxBits", 24).toInt(); } void setRxBits(int v) { s_.setValue("net/rxBits", v == 16 ? 16 : 24); emit changed(); }
  int rxRate() const { return s_.value("net/rxRate", 48000).toInt(); } void setRxRate(int v) { s_.setValue("net/rxRate", v == 44100 || v == 96000 ? v : 48000); emit changed(); }
  int rxLatency() const { return s_.value("net/rxLatency", 20).toInt(); } void setRxLatency(int v) { s_.setValue("net/rxLatency", std::clamp(v, 5, 500)); emit changed(); }
  int rxFirstStrip() const { return s_.value("net/rxFirstStrip", 0).toInt(); } void setRxFirstStrip(int v);
  QString card() const { return s_.value("net/card").toString(); } void setCard(const QString& v) { s_.setValue("net/card", v.trimmed()); emit changed(); }
  QStringList cards() const;
  QString message() const { return msg_; }
  QVariantMap stats() const;
  bool discoverOn() const { return disc_.running(); }
  QVariantList streams() const;
  QVariantList transports() const;
  bool midiNetOn() const { return rtpMidi_.running(); }
  int midiNetPort() const { return s_.value("net/midiPort", 5004).toInt(); } void setMidiNetPort(int v) { s_.setValue("net/midiPort", std::clamp(v, 1024, 65534)); emit changed(); }
  bool midiLan() const { return s_.value("net/midiLan", false).toBool(); } void setMidiLan(bool v);
  QVariantList midiInputs() const;
  QString midiPort() const { return ports_.isOpen() ? QString::fromStdString(ports_.id()) : QString(); }
  QString midiLast() const { return midiLast_; } int midiCount() const { return midiCount_; }
  bool midiLearning() const { return map_.learnPending(); } int midiLearned() const { return map_.learned(); }

  Q_INVOKABLE bool startSend();
  Q_INVOKABLE void stopSend();
  Q_INVOKABLE bool startReceive();
  Q_INVOKABLE void stopReceive();
  Q_INVOKABLE bool setDiscover(bool on);
  Q_INVOKABLE void useStream(int index);
  Q_INVOKABLE bool setPtp(bool on);
  Q_INVOKABLE void injectPtp(const QByteArray& packet, const QString& from) { ptp_.inject(reinterpret_cast<const std::uint8_t*>(packet.constData()), static_cast<std::size_t>(packet.size()), from.toStdString()); emit statsChanged(); }   // tests
  bool ptpOn() const { return ptp_.running(); }
  QVariantList ptpMasters() const;                 // copy a discovered stream's address / port / format into the receiver
  Q_INVOKABLE bool setMidiNet(bool on);
  Q_INVOKABLE bool openMidi(const QString& id);          // "" closes
  Q_INVOKABLE void rescanMidi() { emit changed(); }
  Q_INVOKABLE void learn(const QString& kind, int strip);   // fader | pan | mute | solo | master; the next control moved is bound to it
  Q_INVOKABLE void cancelLearn() { map_.learn(am::net::MidiAction::None, 0); emit statsChanged(); }
  Q_INVOKABLE void clearLearned();
  Q_INVOKABLE void midiIn(int status, int d1, int d2);   // one message, as if it had arrived (also used by the smoke test)
 signals:
  void changed();
  void statsChanged();
  public:
  void setFrame(class Frame* f) { frame_ = f; }
 private:
  class Frame* frame_ = nullptr;
  void attach();                                         // the engine has a new runtime (or none): hook the sender and receiver into it
  void patch(bool on);
  void fail(const QString& m) { msg_ = m; emit changed(); }
  am::net::MidiSink sink();
  Engine* engine_; QSettings s_; am::net::Sender tx_; am::net::Receiver rx_; am::net::Discovery disc_; am::net::PtpMonitor ptp_; am::net::RtpMidi rtpMidi_; am::net::MidiPorts ports_; am::net::MidiMap map_;
  am::Runtime* rt_ = nullptr; double rate_ = 0; bool txWanted_ = false; QTimer timer_; QString msg_, midiLast_; int midiCount_ = 0; QList<int> patched_;
};
