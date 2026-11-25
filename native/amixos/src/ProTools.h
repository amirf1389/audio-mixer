// PRO TOOLS (PRO, STUDIO and MASTER editions): the MULTITRACK RECORDER's timestamped session folder, the ADVANCED ANALYZER's
// numbers (phase correlation, goniometer points, loudness history) and the SESSION NOTES / MARKER list. Plain Qt Core: the engine's
// existing taps (lissajous points, telemetry loudness, the recorder) feed it from QML, so nothing here touches the audio thread.
#pragma once
#include <QDateTime>
#include <QObject>
#include <QVariantList>
#include <QVector>

class ProTools : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList markers READ markers NOTIFY markersChanged)
  Q_PROPERTY(QString notes READ notes WRITE setNotes NOTIFY markersChanged)
  Q_PROPERTY(QVariantList loudHistory READ loudHistory NOTIFY historyChanged)
  Q_PROPERTY(QString session READ session NOTIFY markersChanged)
  Q_PROPERTY(bool preview MEMBER preview_ NOTIFY markersChanged)   // --shots only: the cards drawn unlocked for the pictures
 public:
  explicit ProTools(const QString& stateFile = QString(), QObject* parent = nullptr);
  static bool tierAllows(const QString& edition);                         // PRO and above
  Q_INVOKABLE bool allowed(const QString& edition) const { return tierAllows(edition); }
  static QString sessionName(const QDateTime& at);                       // "Session 2026-11-25 14-03-07"
  Q_INVOKABLE QString makeSessionFolder(const QString& base);           // creates base/Session ..., returns its path ("" on failure)
  QString session() const { return session_; }
  // ANALYZER: correlation (-1 .. +1) of interleaved [l0, r0, l1, r1, ...]; the goniometer points (M/S, rotated 45 degrees, scaled to 0..1)
  static double correlation(const QVector<double>& lr);
  Q_INVOKABLE double correlationOf(const QVariantList& lr) const;
  Q_INVOKABLE QVariantList goniometer(const QVariantList& lr) const;
  Q_INVOKABLE void pushLoudness(double momentary, double shortTerm);    // one point per call (the page calls it a few times a second); 600 kept
  Q_INVOKABLE void clearHistory() { hist_.clear(); emit historyChanged(); }
  QVariantList loudHistory() const;
  // MARKERS + NOTES
  QVariantList markers() const;
  Q_INVOKABLE int addMarker(double seconds, const QString& label);
  Q_INVOKABLE bool renameMarker(int id, const QString& label);
  Q_INVOKABLE bool removeMarker(int id);
  Q_INVOKABLE void clearMarkers();
  Q_INVOKABLE QString exportText() const;                                // CSV of the markers, then the notes
  Q_INVOKABLE bool saveTo(const QString& folder) const;                  // folder/markers.csv (into the session folder)
  QString notes() const { return notes_; } void setNotes(const QString& n);
 signals:
  void markersChanged();
  void historyChanged();
 private:
  struct Marker { int id; double t; QString label; };
  void persist() const; void load();
  QVector<Marker> marks_; QString notes_; int nextId_ = 1; QString session_; QString state_; bool preview_ = false;
  QVector<QPair<double, double>> hist_;
};
