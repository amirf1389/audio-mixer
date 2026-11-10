// The ARRANGE page's model for QML: the project (tracks, clips, markers, automation), the decoded audio files, the transport and the
// realtime player (an ExtraSource of the Engine's runtime). Saved in ~/.amixos/arrangement.json; clips reference WAV files by path and are
// re-loaded at start (a file that cannot be read makes its clips MISSING and silent). Export renders offline to a 24-bit WAV. No recording.
#pragma once
#include <QObject>
#include <QTimer>
#include <QVariant>

#include "arrange/arrange.hpp"

class Engine;

class Arrange : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList tracks READ tracks NOTIFY projectChanged)
  Q_PROPERTY(QVariantList clips READ clips NOTIFY projectChanged)
  Q_PROPERTY(QVariantList markers READ markers NOTIFY projectChanged)
  Q_PROPERTY(double bpm READ bpm WRITE setBpm NOTIFY projectChanged)
  Q_PROPERTY(int snap READ snap WRITE setSnap NOTIFY projectChanged)
  Q_PROPERTY(bool loop READ loop WRITE setLoop NOTIFY projectChanged)
  Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY projectChanged)       // pixels per second
  Q_PROPERTY(double end READ end NOTIFY projectChanged)
  Q_PROPERTY(bool playing READ playing NOTIFY transportChanged)
  Q_PROPERTY(double position READ position NOTIFY transportChanged)
  Q_PROPERTY(int selectedClip READ selectedClip WRITE setSelectedClip NOTIFY selectionChanged)
  Q_PROPERTY(QString message READ message NOTIFY messageChanged)

 public:
  explicit Arrange(Engine* engine, QObject* parent = nullptr);
  ~Arrange() override;

  QVariantList tracks() const; QVariantList clips() const; QVariantList markers() const;
  double bpm() const { return p_.bpm; } void setBpm(double v);
  int snap() const { return p_.snap; } void setSnap(int v);
  bool loop() const { return p_.loop; } void setLoop(bool v);
  double zoom() const { return p_.zoom; } void setZoom(double v);
  double end() const { return am::arr::projectEnd(p_); }
  bool playing() const { return player_.playing(); }
  double position() const { return player_.position(); }
  int selectedClip() const { return sel_; } void setSelectedClip(int id) { if (sel_ != id) { sel_ = id; emit selectionChanged(); } }
  QString message() const { return msg_; }

  Q_INVOKABLE void play(double from = -1);         // -1: from the current position
  Q_INVOKABLE void stop();
  Q_INVOKABLE void toStart();
  Q_INVOKABLE void seek(double seconds);
  Q_INVOKABLE int addTrack(const QString& name = QString());
  Q_INVOKABLE void removeTrack(int id);
  Q_INVOKABLE void setTrack(int id, const QString& key, const QVariant& value);   // name | db | pan | mute | solo | color
  Q_INVOKABLE int importFile(const QString& path, int trackId = 0);             // a WAV onto a track (0 = the last track, or a new one); returns the clip id or 0
  Q_INVOKABLE QStringList wavFilesIn(const QString& folder) const;               // *.wav of a folder, for the picker
  Q_INVOKABLE void moveClip(int id, double start);
  Q_INVOKABLE void moveClipToTrack(int id, int trackId);
  Q_INVOKABLE void splitAtHead();
  Q_INVOKABLE void deleteSelected();
  Q_INVOKABLE void setClipGain(int id, double db);
  Q_INVOKABLE void addMarker(const QString& name = QString());
  Q_INVOKABLE void removeMarker(int index);
  Q_INVOKABLE void addAutomation(int trackId, double t, double db);
  Q_INVOKABLE void clearAutomation(int trackId);
  Q_INVOKABLE double automationAt(int trackId, double t) const;
  Q_INVOKABLE double snapTime(double t) const { return am::arr::snapTime(t, p_.bpm, p_.snap); }
  Q_INVOKABLE QString exportWav(const QString& path);                           // "" = ~/amixos-arrangement.wav; returns the path written or ""
  Q_INVOKABLE void newProject();
  Q_INVOKABLE void save();
  Q_INVOKABLE QString projectFile() const;

 signals:
  void projectChanged();
  void transportChanged();
  void selectionChanged();
  void messageChanged();

 private:
  void load(); void push(); void note(const QString& m); bool loadFile(const std::string& path);
  Engine* engine_; am::arr::Project p_; am::arr::Buffers bufs_; am::arr::Player player_; QTimer tick_; int sel_ = 0; QString msg_; bool wasPlaying_ = false;
};
