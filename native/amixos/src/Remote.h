// PHONE REMOTE over Wi-Fi: starts and stops the remote service (amixos-daemon, the program next to the console) as a child process.
// Local policy: the service answers this computer and private-network addresses only, and every control call needs the token. Nothing is
// encrypted on the wire: for use on your own network; for anything else put the nginx TLS front (deploy/nginx) before it.
// The service runs its own engine, so the console hands the sound card over while it runs and takes it back when it stops.
#pragma once
#include <QObject>
#include <QHash>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QTimer>
#include <QVector>
#include <QStringList>
#include <QVariantList>
#include "Extension.h"

class Engine;

class Remote : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool running READ running NOTIFY changed)
  Q_PROPERTY(bool available READ available CONSTANT)          // the service program is next to the console
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(int port READ port CONSTANT)
  Q_PROPERTY(QStringList addresses READ addresses NOTIFY changed)   // http://<private address>:<port> of this computer
  Q_PROPERTY(QString tokenFile READ tokenFile CONSTANT)
  Q_PROPERTY(QVariantList tokens READ tokens NOTIFY changed)     // the named tokens [{id, name, role, state}]: never the tokens themselves
  Q_PROPERTY(QString tokenNote READ tokenNote NOTIFY changed)
  Q_PROPERTY(int syncedChanges READ syncedChanges NOTIFY synced)   // LIVE SYNC: changes carried between the console and the remotes since start
  Q_PROPERTY(int remotes READ remotes NOTIFY synced)               // phones and browsers connected right now (their live streams)
  Q_PROPERTY(int pulse READ pulse NOTIFY pulseChanged)             // moves on while a remote's fader glow is lit: bind touched(strip, remote.pulse)
 public:
  explicit Remote(Engine* engine, QObject* parent = nullptr);
  ~Remote() override;
  bool running() const { return ext_.running() || proc_.state() != QProcess::NotRunning; }
  bool inProcess() const { return ext_.running(); }   // the service runs as an extension inside the console
  bool available() const; QString status() const { return status_; } int port() const { return 8765; }
  QStringList addresses() const; QString tokenFile() const;
  Q_INVOKABLE bool start(bool wifi);        // wifi false = this computer only (127.0.0.1)
  Q_INVOKABLE void stop();
  void writeConsoleInfo() const;   // remote-console.json for the phone's PLUG-INS page
  Q_INVOKABLE QString pairPin() const;      // the pairing PIN of this minute (six digits; made from the master token and the clock, as the service makes it)
  Q_INVOKABLE int pairSecondsLeft() const;  // until the PIN changes
  // THE PAIRING QR CODE: http://<address>:<port>/remote#pin=<this minute's PIN>. A phone camera opens it in the browser remote (which pairs
  // by itself), the Android app's SCAN QR reads host, port and PIN from it. The PIN rides in the #fragment: browsers never send it in a request.
  Q_INVOKABLE QString pairUrl(const QString& address) const;
  Q_INVOKABLE QVariantList qr(const QString& text) const;   // [size, row-major 0/1 modules...] for a Canvas; empty when it does not fit
  Q_INVOKABLE void refreshTokens();
  Q_INVOKABLE QString newToken(const QString& name, const QString& role, int days);   // returns the token once ("" on failure); days 0 = no end
  Q_INVOKABLE bool revokeToken(const QString& id);
  QVariantList tokens() const { return tokens_; } QString tokenNote() const { return tokenNote_; }
  Q_INVOKABLE QString token() const;        // the token to type into the phone (read from the token file when asked; never logged)
  QString program() const;
 signals:
  void changed();
  void stopped();
 public:
  int syncedChanges() const { return syncedChanges_; }
  int remotes() const { return running() ? remotes_ : 0; }
  int pulse() const { return pulse_; }
  QString syncDebug() const { return QStringLiteral("ticks %1, gets %2, applied %3, posts %4, v %5, token %6, timer %7").arg(dTicks_).arg(dGets_).arg(dApplied_).arg(syncedChanges_).arg(v_).arg(token_.isEmpty() ? "none" : "set").arg(sync_.isActive()); }
  // the fader a phone or browser just moved (strip, or -1 for the MASTER): true for 0.8 s after its last remote move, so the mixer can
  // light it while the remote holds it (tick: any value that changes, to re-read it from a binding)
  Q_INVOKABLE bool touched(int strip, int tick = 0) const;
  // LIVE SYNC, the console side: the service starts from the console's mixer (remote-seed.json, --seed), and while it runs the console
  // follows it: a fader, mute, solo or name moved on a phone or in a browser moves here (about 150 ms), and one moved here goes out to
  // every phone and browser. Each side is compared with the last value both agreed on, so neither echoes the other back.
  struct SyncStrip { double db = 0, pan = 0; bool mute = false, solo = false; QString name; };
  static QString syncName(int i, const QString& n) { return n.isEmpty() ? QStringLiteral("CH %1").arg(i + 1) : n; }
  QVector<SyncStrip> consoleMix() const;   // the console's strips as the service names them (licensed channels only)
 signals:
  void synced();
  void pulseChanged();
 private:
  void writeSeed();
  void syncTick();
  void post(const QVariantMap& cmd);
  QTimer sync_; QNetworkAccessManager net_; QVector<SyncStrip> base_; double baseMaster_ = 0; quint64 v_ = 0; int posting_ = 0; bool stale_ = false, getting_ = false; QByteArray token_; int syncedChanges_ = 0, remotes_ = 0, pulse_ = 0; qint64 lastTouch_ = 0; int dTicks_ = 0, dGets_ = 0, dApplied_ = 0; QHash<int, qint64> touched_;
  Engine* engine_; QProcess proc_; Extension ext_{QStringLiteral("AmF X1 Network")}; QString status_ = "off";
  QString runTool(const QStringList& args, bool* ok) const; void restartIfRunning();
  QVariantList tokens_; QString tokenNote_; bool wifi_ = false;
};
