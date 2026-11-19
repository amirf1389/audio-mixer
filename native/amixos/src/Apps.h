// APP CAPTURE in the console: several programs at once, each on its own strip (am::app::Capture does the work). A program is picked
// on a strip's INPUT (mono on that strip, or stereo on that strip and the next); the strip then listens to the program's slot and
// shows its name. A slot is given back when no strip listens to it any more. Which program sat on which slot is remembered by name:
// when the console starts, or the program is closed and opened again, the slot waits and takes the program back when it plays.
#pragma once
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <memory>
#include <vector>

#include "audiomixer/app_capture.hpp"

class Engine;

class Apps : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList captures READ captures NOTIFY changed)   // [{slot, name, pid, state: opening | live | silent | waiting | failed, message, test, strips: [strip ...], peakDb, gaps}]
  Q_PROPERTY(int count READ count NOTIFY changed)                  // the slots in use (8 at most)
  Q_PROPERTY(bool available READ available CONSTANT)               // this system can capture a program (Windows 10 2004 and later)
  Q_PROPERTY(QString note READ note NOTIFY changed)                // what the last pick did, or why it could not
 public:
  explicit Apps(Engine* engine, QObject* parent = nullptr);
  ~Apps() override;
  QVariantList captures() const; int count() const; bool available() const { return am::app::Capture::available() || testMode; }
  QString note() const { return note_; }
  // the programs that play sound now: [{id "loopback:app:<pid>", name, pid, slot (-1 = not captured)}]
  Q_INVOKABLE QVariantList programs();
  // put that program on `strip`: mono on the strip, or stereo on the strip (left) and the next one (right, panned apart). {ok, slot, message}
  Q_INVOKABLE QVariantMap capture(const QString& id, const QString& name, int strip, bool stereo);
  Q_INVOKABLE void release(int slot);                              // the slot is given back; its strips go to no input
  Q_INVOKABLE QString inputName(int input) const;                  // "Spotify" / "Spotify L" / "Spotify R" for an app input, "" otherwise
  Q_INVOKABLE int slotOfInput(int input) const;
  // tests and the screenshots: a steady tone in place of a program (the same path from the slot to the strip)
  Q_INVOKABLE QVariantMap captureTest(const QString& name, double hz, int strip, bool stereo);
  bool testMode = false;                                           // tests: programs come from testPrograms, nothing is asked of Windows
  QVariantList testPrograms;
  std::function<std::vector<std::pair<quint32, QString>>()> sessionsNow;   // tests: what "plays now" (pid, name); empty = ask Windows
  void poll();                                                     // what the timer does (tests call it)
 signals:
  void changed();
 private:
  void attach(); void prune(); void save(); void load(); void place(int slot, int strip, bool stereo); am::app::Capture& cap();
  QVariantMap finish(int slot, const QString& name, int strip, bool stereo, const std::string& err);
  Engine* engine_; QSettings s_; QTimer timer_; std::unique_ptr<am::app::Capture> cap_; am::Runtime* rt_ = nullptr; double rate_ = 0;
  QString waiting_[am::app::Capture::kSlots];                      // the program a free slot waits for ("" = none)
  QString note_; bool looking_ = false, placing_ = false; QString lastSig_;
};
