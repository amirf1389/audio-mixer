// SAMPLES: the 100 built-in one-shots, the 1000 SOUND EFFECTS (engine/cpp/app/samples/sfx) and the 1000 CUSTOM PADS (padbank: what each pad
// plays, its name, gain and colour), played polyphonically (16 voices) into the master through the shared pad player (pad_player.hpp) as a
// fourth Runtime::ExtraSource. The pads live in <data>/pads.json, the file the remote service reads too, so the phones show and play the
// same pads; a change made on a phone reaches this page within a second (the file is watched), a change made here reaches the phones the
// same way. While phones are linked the console's engine is stopped and every press goes to the service (ops sample, pad, sfx).
// EXPORT writes the 100 samples as WAV into <cache>/samples.
#pragma once
#include <functional>
#include <memory>
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVariant>

#include "samples/pad_player.hpp"
#include "samples/padbank.hpp"

class Engine;

class Samples : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList list READ list CONSTANT)                       // the 100 built-in samples [{id, name, category, seconds}]
  Q_PROPERTY(QStringList categories READ categories CONSTANT)
  Q_PROPERTY(QStringList sfxFamilies READ sfxFamilies CONSTANT)          // the 20 families of the 1000 sound effects
  Q_PROPERTY(double gainDb READ gainDb WRITE setGainDb NOTIFY changed)
  Q_PROPERTY(int lastPlayed READ lastPlayed NOTIFY changed)              // the built-in sample last played (1..100)
  Q_PROPERTY(int lastPad READ lastPad NOTIFY changed)                    // the custom pad last played (1..1000)
  Q_PROPERTY(int voices READ voices NOTIFY changed)                     // playing now
  Q_PROPERTY(double peakDb READ peakDb NOTIFY changed)
  Q_PROPERTY(QString folder READ folder CONSTANT)                       // where EXPORT writes the WAVs
  Q_PROPERTY(QString message READ message NOTIFY changed)
  Q_PROPERTY(bool ready READ ready NOTIFY changed)                      // the built-in bank is rendered at the engine rate
  Q_PROPERTY(int padsVersion READ padsVersion NOTIFY padsChanged)        // goes up when any pad changes (here or on a phone)
  Q_PROPERTY(int padCount READ padCount CONSTANT)                       // 1000
  Q_PROPERTY(double keptMb READ keptMb NOTIFY changed)                  // sounds held in memory
 public:
  explicit Samples(Engine* engine, QObject* parent = nullptr);
  ~Samples() override;
  QVariantList list() const; QStringList categories() const; QStringList sfxFamilies() const;
  double gainDb() const { return player_.gainDb.load(); } void setGainDb(double db);
  int lastPlayed() const { return last_; } int lastPad() const { return lastPad_; } int voices() const { return player_.active.load(); } double peakDb() const; QString folder() const;
  QString message() const { return msg_; } bool ready() const { return player_.ready(); } int padsVersion() const { return bank_->version(); } int padCount() const { return am::pads::kPads; }
  double keptMb() const { return player_.bytesKept() / 1048576.0; }
  // while phones are linked the console's engine is stopped and the remote service plays the sound: presses are sent there
  std::function<bool(int id, double gainDb)> remotePlay; std::function<void()> remoteStop; std::function<bool(const QVariantMap& cmd)> remoteSend;
  Q_INVOKABLE bool play(int id);                                        // a built-in sample, 1..100
  Q_INVOKABLE bool playPad(int n);                                      // a custom pad, 1..1000
  Q_INVOKABLE bool playSfx(int id);                                     // a sound effect from the list, 1..1000 (to try it)
  Q_INVOKABLE void stopAll();
  Q_INVOKABLE QVariantList pads(int from, int to) const;               // [{n, source, name, gainDb, color, custom, kind}] (kind: sample / sfx / file / empty)
  Q_INVOKABLE QVariantMap pad(int n) const;
  Q_INVOKABLE QVariantList sfx(const QString& family) const;           // [{id, name, family, seconds}] of one family ("" = all 1000)
  Q_INVOKABLE bool setPad(int n, const QString& source, const QString& name, double gainDb, int color);   // "sample:N", "sfx:N", "" (empty)
  Q_INVOKABLE bool setPadFile(int n, const QString& pathOrUrl);         // an audio file on this computer (file:// URL from a dialog or a path)
  Q_INVOKABLE bool resetPad(int n);
  Q_INVOKABLE QStringList audioFilters() const;                         // for the file dialog
  Q_INVOKABLE int exportAll();                                          // writes the 100 samples as WAV into folder(); returns how many files exist afterwards
  Q_INVOKABLE void prepare(double sampleRate);                          // (re)render at this rate (the Engine calls it when a device opens)
 signals:
  void changed();
  void padsChanged();
 private:
  bool saveBank(); bool playSource(const std::string& source, int padN, double padGainDb);
  Engine* engine_; am::pads::Player player_; std::unique_ptr<am::pads::Bank> bank_; QSettings s_; int last_ = 0, lastPad_ = 0; QString msg_; QTimer poll_; int watch_ = 0;
};
