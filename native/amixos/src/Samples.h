// SAMPLES: the 100 built-in one-shots (engine/cpp/app/samples) on 100 pads. Rendered once at the engine rate into memory, exported to
// <cache>/samples/NNN-name.wav (am::paths: the OS cache folder, Android clears it when space is short) on request, and played polyphonically (8 voices) into the master as a fourth Runtime::ExtraSource. The
// trigger path is lock-free: the UI pushes ids into a small ring, the audio thread starts voices from it.
#pragma once
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVariant>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "audiomixer/runtime.hpp"

class Engine;

class SamplePlayer : public am::Runtime::ExtraSource {
 public:
  struct Clip { std::vector<float> L, R; };
  static constexpr int kVoices = 8, kCount = 100;
  void setBank(std::shared_ptr<const std::array<Clip, kCount>> bank) { bank_.store(bank.get()); old_ = std::move(keep_); keep_ = std::move(bank); }   // UI thread; the previous bank stays alive one more round for a block still reading it
  bool trigger(int id) { const unsigned w = w_.load(); if (w - r_.load() >= kRing) return false; ring_[w % kRing] = id; w_.store(w + 1); return true; }
  void stopAll() { stop_.store(true); }
  void render(float* L, float* R, std::size_t frames, double fs) override;
  std::atomic<double> gainDb{-6}; std::atomic<float> peak{0}; std::atomic<int> active{0};
 private:
  struct Voice { int id = 0; std::size_t pos = 0; };
  static constexpr unsigned kRing = 32; std::array<int, kRing> ring_{}; std::atomic<unsigned> w_{0}, r_{0}; std::atomic<bool> stop_{false};
  std::atomic<const std::array<Clip, kCount>*> bank_{nullptr}; std::shared_ptr<const std::array<Clip, kCount>> keep_, old_; std::array<Voice, kVoices> voices_{}; unsigned next_ = 0;
};

class Samples : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList list READ list CONSTANT)                       // [{id, name, category, seconds}]
  Q_PROPERTY(QStringList categories READ categories CONSTANT)
  Q_PROPERTY(double gainDb READ gainDb WRITE setGainDb NOTIFY changed)
  Q_PROPERTY(int lastPlayed READ lastPlayed NOTIFY changed)
  Q_PROPERTY(int voices READ voices NOTIFY changed)                     // playing now
  Q_PROPERTY(double peakDb READ peakDb NOTIFY changed)
  Q_PROPERTY(QString folder READ folder CONSTANT)                       // where EXPORT writes the WAVs
  Q_PROPERTY(QString message READ message NOTIFY changed)
  Q_PROPERTY(bool ready READ ready NOTIFY changed)                      // the bank is rendered at the engine rate
 public:
  explicit Samples(Engine* engine, QObject* parent = nullptr);
  ~Samples() override;
  QVariantList list() const; QStringList categories() const;
  double gainDb() const { return player_.gainDb.load(); } void setGainDb(double db);
  int lastPlayed() const { return last_; } int voices() const { return player_.active.load(); } double peakDb() const; QString folder() const; QString message() const { return msg_; } bool ready() const { return ready_; }
  Q_INVOKABLE bool play(int id);                                        // 1..100
  Q_INVOKABLE void stopAll();
  Q_INVOKABLE int exportAll();                                          // writes every sample as WAV into folder(); returns how many files exist afterwards
  Q_INVOKABLE void prepare(double sampleRate);                          // (re)render the bank at this rate (the Engine calls it when a device opens)
 signals:
  void changed();
 private:
  Engine* engine_; SamplePlayer player_; QSettings s_; int last_ = 0; QString msg_; bool ready_ = false; double rate_ = 0; QTimer poll_;
};
