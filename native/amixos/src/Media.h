// STREAM & MEDIA: a WAV playlist player into the master (a third Runtime::ExtraSource next to the ARRANGE player and the test generator).
// Files are decoded once (engine/cpp/app/arrange readWav), resampled to the engine rate when it differs, and swapped into the audio
// thread as an immutable snapshot; position, gain and loop are atomics. The playlist is kept in QSettings (paths only).
#pragma once
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVariant>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "audiomixer/runtime.hpp"

class Engine;

class MediaSource : public am::Runtime::ExtraSource {
 public:
  struct Clip { std::vector<float> L, R; double rate = 48000; };
  void set(std::shared_ptr<const Clip> c);                   // UI thread; the audio thread picks it up at the next block
  void render(float* L, float* R, std::size_t frames, double fs) override;
  std::atomic<bool> playing{false}, loop{false}, ended{false};
  std::atomic<double> gainDb{0}, pos{0};                      // pos in seconds (read by the UI, written by both: the UI seeks, the audio thread advances)
  std::atomic<float> peak{0};
 private:
  std::shared_ptr<const Clip> clip_; std::mutex m_; double frac_ = 0;
};

class Media : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList playlist READ playlist NOTIFY playlistChanged)   // [{path, name, seconds, ok, error}]
  Q_PROPERTY(int current READ current NOTIFY playlistChanged)
  Q_PROPERTY(bool playing READ playing NOTIFY transportChanged)
  Q_PROPERTY(double position READ position NOTIFY transportChanged)
  Q_PROPERTY(double duration READ duration NOTIFY transportChanged)
  Q_PROPERTY(double gainDb READ gainDb WRITE setGainDb NOTIFY transportChanged)
  Q_PROPERTY(bool loop READ loop WRITE setLoop NOTIFY transportChanged)
  Q_PROPERTY(bool autoNext READ autoNext WRITE setAutoNext NOTIFY transportChanged)
  Q_PROPERTY(double peakDb READ peakDb NOTIFY transportChanged)
  Q_PROPERTY(QString message READ message NOTIFY transportChanged)
 public:
  explicit Media(Engine* engine, QObject* parent = nullptr);
  ~Media() override;
  QVariantList playlist() const { return list_; }
  int current() const { return current_; }
  bool playing() const { return src_.playing.load(); }
  double position() const { return src_.pos.load(); }
  double duration() const { return duration_; }
  double gainDb() const { return src_.gainDb.load(); } void setGainDb(double db);
  bool loop() const { return src_.loop.load(); } void setLoop(bool on) { src_.loop.store(on); s_.setValue("media/loop", on); emit transportChanged(); }
  bool autoNext() const { return s_.value("media/autoNext", true).toBool(); } void setAutoNext(bool on) { s_.setValue("media/autoNext", on); emit transportChanged(); }
  double peakDb() const;
  QString message() const { return msg_; }
  Q_INVOKABLE int add(const QString& path);                   // a WAV (or every WAV of a folder); returns how many were added
  Q_INVOKABLE void remove(int index);
  Q_INVOKABLE void clear();
  Q_INVOKABLE bool select(int index);                         // loads it (decode + resample to the engine rate)
  Q_INVOKABLE bool play(int index = -1);                      // -1: the current one
  Q_INVOKABLE void pause();
  Q_INVOKABLE void stop();
  Q_INVOKABLE void next();
  Q_INVOKABLE void previous();
  Q_INVOKABLE void seek(double seconds);
  void reloadForRate();                                       // the engine opened at another rate: resample the current clip again
 signals:
  void playlistChanged();
  void transportChanged();
 private:
  bool load(int index, QString& err);
  void save();
  Engine* engine_; QSettings s_; MediaSource src_; QVariantList list_; int current_ = -1; double duration_ = 0, loadedRate_ = 0; QString msg_; QTimer tick_;
};
