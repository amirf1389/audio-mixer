// AMF X1 DIAGNOSTICS: the system check behind the separate "AmF X1 Diagnostics" program. It needs nothing from the console: it reads every
// audio endpoint (speakers, headphones, microphones, line, digital; active, unplugged or disabled), the ASIO drivers with who made them and
// whether they load, plays a test tone on any output and meters any input through its own small engine, runs the full check and writes a
// plain-text report. It never changes a Windows setting; it only opens Windows' own Sound settings when asked.
#pragma once
#include <QObject>
#include <QTimer>
#include <QVariant>
#include <memory>

namespace am { class Runtime; }

class Diag : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList outputs READ outputs NOTIFY changed)      // [{id, name, kind, state, channels, rate, isDefault}]
  Q_PROPERTY(QVariantList inputs READ inputs NOTIFY changed)
  Q_PROPERTY(QVariantList asio READ asio NOTIFY changed)            // [{id, name, maker, type, status, in, out, note}]
  Q_PROPERTY(QVariantList checks READ checks NOTIFY changed)        // [{name, level: ok | warn | fail, detail, fix}]
  Q_PROPERTY(QVariantMap system READ system CONSTANT)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(QString testing READ testing NOTIFY changed)           // the endpoint id under test ("" = none)
  Q_PROPERTY(double level READ level NOTIFY levelChanged)           // the input under LISTEN: its peak in dBFS
  Q_PROPERTY(QString message READ message NOTIFY changed)
 public:
  explicit Diag(QObject* parent = nullptr);
  ~Diag() override;
  QVariantList outputs() const { return outs_; } QVariantList inputs() const { return ins_; } QVariantList asio() const { return asio_; }
  QVariantList checks() const { return checks_; } QVariantMap system() const; bool busy() const { return busy_; } QString testing() const { return testing_; }
  double level() const { return level_; } QString message() const { return msg_; }
  Q_INVOKABLE void refresh();                         // reads the endpoints and drivers again and runs every check
  Q_INVOKABLE bool testOutput(const QString& id);     // a 1 kHz tone, -20 dBFS, 2 s, on that output (speakers or headphones)
  Q_INVOKABLE bool listen(const QString& id);         // meters that input for 8 s (level); "" stops
  Q_INVOKABLE void stop();
  Q_INVOKABLE QString report() const;                 // the whole check as plain text
  Q_INVOKABLE QString saveReport(const QString& path = QString());   // "" = Documents/AmF X1 Diagnostics <date>.txt; returns the file or ""
  Q_INVOKABLE void copyReport() const;
  Q_INVOKABLE void openSoundSettings() const;         // Windows: the classic Sound control panel (playback / recording devices)
  Q_INVOKABLE bool openConsole() const;               // starts amixos next to this program
  void setBackendOverride(const QString& b) { backend_ = b; }   // tests: "null"
  void loadDemo();                                     // screenshots: a typical laptop (speakers, unplugged headphones, mics, a busy ASIO interface)
 signals:
  void changed(); void levelChanged();
 private:
  bool openOn(const QString& in, const QString& out, int inCh, int outCh);
  void runChecks();
  QVariantList outs_, ins_, asio_, checks_; bool busy_ = false; QString testing_, msg_, backend_; double level_ = -120;
  std::unique_ptr<am::Runtime> rt_; QTimer stopTimer_, meterTimer_;
};
