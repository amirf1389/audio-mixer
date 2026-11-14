// I/O FRAME: the console as a DSP frame with four card slots, a 256-slot frame bus, a front panel and GPIO.
//
//   CARDS   Each slot holds one card type. A card's channels are the sound card's real channels: an INPUT card routes device inputs to strips
//           (setInputRoute), an OUTPUT card routes console sources to device outputs (setOutputRoute). ANALOG IN and AEC IN have a 48 V
//           switch per channel; AEC IN and TELEPHONE HYBRID also put their processing object (fx:aec, fx:hybrid) into the strip's rack.
//   BUS     256 slots: every card channel takes the next free slot (card 1 = 1..64, card 2 = 65..128 ...), so a channel has one fixed number
//           for OSC, logs and the panel display. It is a patch table: the audio itself runs in the console's 32 strips and its outputs.
//   PANEL   LEDs per card channel (signal, clip, 48 V), a status display, LOCATE (flashes the panel; LOCATE from OSC, MIDI or the GPIO box
//           flashes it too, and pressing it here sends LOCATE out: both directions).
//   GPIO    12 control inputs and 6 logic outputs (am::frame::logic(), shared with the LOGIC / GPIO rack object), reachable from the screen,
//           OSC (/gpio/in/N 0|1 in, /gpio/out/N 0|1 out), MIDI (notes 36..47 = inputs 1..12) and a USB serial GPIO box (text lines:
//           "I3=1" in, "O2=1" out, "LOCATE", 115200 8N1).
//   48 V    Phantom power cannot be switched by software on class-compliant interfaces: the switch is kept with the card and shown on the
//           panel, and the page says to switch it on the interface itself.
#pragma once
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVariantList>
#include <memory>

class Engine;
class Osc;
class SerialLine;

class Frame : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList cards READ cards NOTIFY changed)              // [{slot, type, name, channels: [{n, device, strip, out, source, phantom, bus, label}]}]
  Q_PROPERTY(QVariantList cardTypes READ cardTypes CONSTANT)           // [{type, name, dir: in|out|both, channels, phantom, note}]
  Q_PROPERTY(QVariantList logicIn READ logicIn NOTIFY logicChanged)     // 12 bools
  Q_PROPERTY(QVariantList logicOut READ logicOut NOTIFY logicChanged)   // 6 bools
  Q_PROPERTY(bool locating READ locating NOTIFY locateChanged)
  Q_PROPERTY(int busUsed READ busUsed NOTIFY changed)
  Q_PROPERTY(QStringList serialPorts READ serialPorts NOTIFY serialChanged)
  Q_PROPERTY(QString serialPort READ serialPort NOTIFY serialChanged)  // the open one, "" = none
  Q_PROPERTY(QString serialNote READ serialNote NOTIFY serialChanged)
  Q_PROPERTY(QStringList log READ log NOTIFY logicChanged)
 public:
  explicit Frame(Engine* engine, QObject* parent = nullptr);
  ~Frame() override;
  static constexpr int kSlots = 4, kBus = 256;
  QVariantList cards() const; QVariantList cardTypes() const;
  QVariantList logicIn() const; QVariantList logicOut() const;
  bool locating() const { return locateLeft_ > 0; }
  int busUsed() const;
  QStringList serialPorts() const; QString serialPort() const { return serialName_; } QString serialNote() const { return serialNote_; }
  QStringList log() const { return log_; }

  Q_INVOKABLE void setCardType(int slot, const QString& type);          // "" = empty slot
  Q_INVOKABLE void setChannelDevice(int slot, int ch, int device);       // input card: device input (0-based); output card: device output
  Q_INVOKABLE void setChannelStrip(int slot, int ch, int strip);         // input card: the strip it feeds
  Q_INVOKABLE void setChannelSource(int slot, int ch, int source);       // output card: the console source (Engine::outputSources index)
  Q_INVOKABLE void setPhantom(int slot, int ch, bool on);
  Q_INVOKABLE void apply();                                              // pushes every card's routing (and processing objects) to the console
  Q_INVOKABLE void setLogicIn(int i, bool on, const QString& from = QStringLiteral("SCREEN"));
  Q_INVOKABLE void locate(bool on = true, bool send = true);            // flashes the panel for 15 s (false stops); send = tell OSC / GPIO box too
  Q_INVOKABLE bool openSerial(const QString& port);                      // "" closes
  Q_INVOKABLE void rescanSerial();
  // from the outside (main.cpp wires these to OSC and MIDI)
  bool handleOsc(const QString& addr, double value);                    // /gpio/in/N, /locate; true when it was one of ours
  bool handleMidi(int status, int d1, int d2);                           // notes 36..47 -> inputs 1..12; note 48 -> LOCATE
  void setOsc(Osc* o) { osc_ = o; }
 signals:
  void changed(); void logicChanged(); void locateChanged(); void serialChanged();
 private:
  struct Ch { int device = -1, strip = -1, source = 0; bool phantom = false; };
  struct Card { QString type; Ch ch[8]; };
  const QVariantMap* typeOf(const QString& t) const;
  void save(); void load(); void note(const QString& m);
  void pollLogic(); void serialLine(const QByteArray& line);
  Engine* engine_; Osc* osc_ = nullptr; QSettings s_; Card cards_[kSlots];
  QTimer poll_; unsigned lastSeq_ = ~0u; bool lastOut_[6] = {false, false, false, false, false, false};
  int locateLeft_ = 0; QStringList log_;
  std::unique_ptr<SerialLine> serial_; QString serialName_, serialNote_; QStringList ports_;
};
