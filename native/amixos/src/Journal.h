// AUTO-SAVE JOURNAL: the mixer, the routing, the scenes and the settings written to one file a short time after every change, so a crash,
// a power cut or Windows ending the console loses at most the last two seconds.
//   DEBOUNCED: touch() on every change; the file is written `debounceMs` after the last one (2 s), and at most `maxWaitMs` after the first
//              unsaved change (10 s) while changes keep coming (a fader held moving is still saved).
//   ATOMIC:    the new state goes to <file>.tmp and replaces <file> in one rename (QSaveFile), so the file is always either the old state
//              or the new one, never half of each. The previous good file is kept as <file>.bak (one backup).
//   CRASH:     begin() leaves <file>.running, end() removes it on a clean quit; begin() finding it there = the last run died. Then
//              loadGood() gives the newest file that still parses (the journal, else its backup) for the console to be restored from.
// Qt Core only (unit tested without the console: test/journal_test.cpp).
#pragma once
#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>

class Journal : public QObject {
  Q_OBJECT
 public:
  using Provider = std::function<QByteArray()>;
  Journal(const QString& path, Provider provider, int debounceMs = 2000, int maxWaitMs = 10000, QObject* parent = nullptr);
  ~Journal() override;

  void touch();                 // something changed: save after the debounce
  bool flush();                 // a pending change saved now (on quit); true when nothing was pending or the save worked
  bool saveNow();               // write the provider's state now (atomic, with the backup)
  bool pending() const { return pending_; }
  int saves() const { return saves_; }
  QString path() const { return path_; }
  QString backupPath() const { return path_ + QStringLiteral(".bak"); }
  void setEnabled(bool on) { enabled_ = on; if (!on) { timer_.stop(); pending_ = false; } }

  bool begin();                 // the run starts: true when the previous one did not end() (it crashed)
  void end();                   // a clean quit

  /** the newest state that parses as a JSON object: the journal, else its backup; empty when neither. `from` gets the file used. */
  QByteArray loadGood(QString* from = nullptr) const;
  /** writes `data` to `path` atomically (temp + rename); a previous file that parses is kept first as `path`.bak */
  static bool writeAtomic(const QString& path, const QByteArray& data, QString* error = nullptr);
  static bool parses(const QByteArray& data);

 signals:
  void saved();

 private:
  QString path_; Provider provider_; int debounce_, maxWait_; QTimer timer_; QElapsedTimer first_; bool pending_ = false, enabled_ = true; int saves_ = 0;
};
