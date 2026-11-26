// PHONE REMOTE over Wi-Fi: starts and stops the remote service (amixos-daemon, the program next to the console) as a child process.
// Local policy: the service answers this computer and private-network addresses only, and every control call needs the token. Nothing is
// encrypted on the wire: for use on your own network; for anything else put the nginx TLS front (deploy/nginx) before it.
// The service runs its own engine, so the console hands the sound card over while it runs and takes it back when it stops.
#pragma once
#include <cmath>
#include <functional>
#include <QObject>
#include <QHash>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QSettings>
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
  Q_PROPERTY(QStringList addresses READ addresses NOTIFY changed)   // http://<private address>:<port> of this computer, the one a phone reaches first
  Q_PROPERTY(QStringList otherAddresses READ otherAddresses NOTIFY changed)   // the rest: no-network (169.254), carrier / public, IPv6 (listed, not for the code)
  Q_PROPERTY(QString feedbackNote READ feedbackNote NOTIFY changed)   // the service's FEEDBACK GUARD muted a strip that howled ("" until it does)
  Q_PROPERTY(QString tokenFile READ tokenFile CONSTANT)
  Q_PROPERTY(QVariantList tokens READ tokens NOTIFY changed)     // the named tokens [{id, name, role, state}]: never the tokens themselves
  Q_PROPERTY(QString tokenNote READ tokenNote NOTIFY changed)
  Q_PROPERTY(int syncedChanges READ syncedChanges NOTIFY synced)   // LIVE SYNC: changes carried between the console and the remotes since start
  Q_PROPERTY(int remotes READ remotes NOTIFY synced)               // phones and browsers connected right now (their live streams)
  Q_PROPERTY(int pulse READ pulse NOTIFY pulseChanged)             // moves on while a remote's fader glow is lit: bind touched(strip, remote.pulse)
  Q_PROPERTY(QVariantList links READ links NOTIFY linksChanged)    // CONNECTED PHONES: [{id, tokenId, name, role, ip, seconds}] (every 2 s while running)
  Q_PROPERTY(QString linkNote READ linkNote NOTIFY linksChanged)
  // WEB STACK (OFF by default): nginx (web/nginx) in front of the service on webStackPort, plus the Node bridge (web/node, web/bridge),
  // started together with the service and stopped with it. The service's own browser remote (port, /remote) works with or without it.
  Q_PROPERTY(bool webStack READ webStack WRITE setWebStack NOTIFY webStackChanged)
  Q_PROPERTY(bool webStackAvailable READ webStackAvailable CONSTANT)
  Q_PROPERTY(bool webStackRunning READ webStackRunning NOTIFY webStackChanged)
  Q_PROPERTY(int webStackPort READ webStackPort NOTIFY webStackChanged)
 public:
  explicit Remote(Engine* engine, QObject* parent = nullptr);
  ~Remote() override;
  bool running() const { return ext_.running() || proc_.state() != QProcess::NotRunning; }
  bool inProcess() const { return ext_.running(); }   // the service runs as an extension inside the console
  bool available() const; QString status() const { return status_; } int port() const { if (runPort_ > 0 && running()) return runPort_; const int p = QSettings().value("remote/port", 8765).toInt(); return p >= 1024 && p <= 65535 ? p : 8765; }   // while the service runs: the port it was STARTED on (a port changed in SETTINGS since applies at the next start; the QR code and the links must not point at a port nothing listens on)
  bool webStack() const { return QSettings().value("remote/webStack", false).toBool(); }
  void setWebStack(bool on) { if (on == webStack()) return; QSettings().setValue("remote/webStack", on); if (!on) stopWebStack(); else if (running()) startWebStack(); emit webStackChanged(); }
  bool webStackAvailable() const; bool webStackRunning() const { return nginx_.state() != QProcess::NotRunning; }
  int webStackPort() const { const int p = QSettings().value("remote/webStackPort", 8080).toInt(); return p >= 1024 && p <= 65535 && p != port() ? p : 8080; }
  mutable int runPort_ = 0;   // REMOTE PORT from SETTINGS (validated); read when the service starts
  QStringList addresses() const; QStringList otherAddresses() const; QString tokenFile() const;
  Q_INVOKABLE QString adapterName(const QString& address) const;   // the network card an address belongs to ("Wi-Fi", "Ethernet"), for the address picker
  QString feedbackNote() const { return running() ? feedbackNote_ : QString(); }
  // THE SOUND CARD HANDED OVER: the service opens exactly what the console plays now (backend, output, input or none, rate, buffer), so
  // starting it never swaps the console's devices for the system defaults (the default input is the microphone: it howled through the
  // speakers). A console with no input open hands over no input. When the service stops, the console reopens what it had.
  struct Takeover { bool running = false; QString backend, in, out; double rate = 0; int frames = 0, inChannels = 0, outChannels = 0; };
  Takeover takeover() const;   // what the console plays now (or, with its engine off, the remembered device with no input)
  static QStringList openArgs(const Takeover& t) {   // the service's --open arguments for it
    QStringList a{QStringLiteral("--open"), t.backend.isEmpty() ? QStringLiteral("auto") : t.backend};
    if (!t.out.isEmpty() && t.out != QLatin1String("-")) a << QStringLiteral("--out") << t.out;
    const bool in = t.running && t.inChannels > 0 && t.in != QLatin1String("-");
    a << QStringLiteral("--in") << (in ? (t.in.isEmpty() ? QStringLiteral("default") : t.in) : QStringLiteral("-"));
    if (in) a << QStringLiteral("--in-channels") << QString::number(t.inChannels);
    if (t.rate > 0) a << QStringLiteral("--rate") << QString::number(t.rate, 'g', 10);
    if (t.frames > 0) a << QStringLiteral("--frames") << QString::number(t.frames);
    if (t.outChannels > 0) a << QStringLiteral("--out-channels") << QString::number(t.outChannels);
    return a; }
  const Takeover& handedOver() const { return took_; }
  Q_INVOKABLE bool start(bool wifi);        // wifi false = this computer only (127.0.0.1)
  Q_INVOKABLE void stop();
  void writeConsoleInfo() const;   // remote-console.json for the phone's PLUG-INS page
  Q_INVOKABLE QString pairPin() const;      // the pairing PIN of this minute (six digits; made from the master token and the clock, as the service makes it)
  Q_INVOKABLE int pairSecondsLeft() const;  // until the PIN changes
  // THE PAIRING QR CODE: http://<address>:<port>/remote#pin=<this minute's PIN>. A phone camera opens it in the browser remote (which pairs
  // by itself), the Android app's SCAN QR reads host, port and PIN from it. The PIN rides in the #fragment: browsers never send it in a request.
  Q_INVOKABLE QString pairUrl(const QString& address) const;
  Q_INVOKABLE QVariantList qr(const QString& text) const;   // [size, row-major 0/1 modules...] for a Canvas; empty when it does not fit
  Q_INVOKABLE QString qrImage(const QString& text) const;   // the same code as a PNG (data: URL, 8 px a module, quiet zone): the page's fallback when its Canvas does not draw
  Q_INVOKABLE void refreshTokens();
  Q_INVOKABLE QString newToken(const QString& name, const QString& role, int days);   // returns the token once ("" on failure); days 0 = no end
  Q_INVOKABLE bool revokeToken(const QString& id);
  QVariantList tokens() const { return tokens_; } QString tokenNote() const { return tokenNote_; }
  Q_INVOKABLE QString token() const;        // the token to type into the phone (read from the token file when asked; never logged)
  QString program() const;
 signals:
  void changed();
  void stopped();
  void webStackChanged();
 public:
  int syncedChanges() const { return syncedChanges_; }
  int remotes() const { return running() ? remotes_ : 0; }
  int pulse() const { return pulse_; }
  QString syncDebug() const { return QStringLiteral("ticks %1, gets %2, applied %3, posts %4, v %5, token %6, timer %7").arg(dTicks_).arg(dGets_).arg(dApplied_).arg(syncedChanges_).arg(v_).arg(token_.isEmpty() ? "none" : "set").arg(sync_.isActive()); }
  // the fader a phone or browser just moved (strip, or -1 for the MASTER): true for 0.8 s after its last remote move, so the mixer can
  // light it while the remote holds it (tick: any value that changes, to re-read it from a binding)
  Q_INVOKABLE bool touched(int strip, int tick = 0) const;
  QVariantList links() const { return running() ? links_ : QVariantList(); } QString linkNote() const { return linkNote_; }
  Q_INVOKABLE void refreshLinks();
  // STREAMING: report what plays to the service and take the phones' queued transport commands (master token; metadata only)
  void streamingSync(const QVariantMap& now, std::function<void(const QStringList&)> onCmds);
  Q_INVOKABLE void disconnectLink(int id);            // DISCONNECT: drops that phone now; its pairing is refused for 2 minutes
  Q_INVOKABLE void forgetLink(const QString& tokenId); // FORGET: revokes the phone's pairing (it needs the PIN again); its link closes at once
  static QString linkAge(int seconds);                // "12 s", "4 min", "2 h 05 min"
  // LIVE SYNC, the console side: the service starts from the console's mixer (remote-seed.json, --seed), and while it runs the console
  // follows it: a fader, mute, solo or name moved on a phone or in a browser moves here (about 150 ms), and one moved here goes out to
  // every phone and browser. Each side is compared with the last value both agreed on, so neither echoes the other back.
  struct SyncStrip;
  static QVariantList eqList(const SyncStrip& s);
  // gMute / gDb: what the strip's DCA and MUTE GROUPS do to it on top of its own MUTE and fader
  struct SyncStrip { double db = 0, pan = 0; bool mute = false, solo = false; bool gMute = false; double gDb = 0; QString name; double trim = 0, low = 0, mid = 0, midHz = 1000, high = 0; bool toneOn = true;
    struct Band { int type = 0; double freq = 1000, q = 0.707, gain = 0; bool operator!=(const Band& o) const { return type != o.type || std::abs(freq - o.freq) > 0.5 || std::abs(q - o.q) > 0.005 || std::abs(gain - o.gain) > 0.05; } };
    Band eq[8]; };   // the channel's parametric EQ, bands 0..7   // + GAIN and the 3-band channel EQ
  static QString syncName(int i, const QString& n) { return n.isEmpty() ? QStringLiteral("CH %1").arg(i + 1) : n; }
  QVector<SyncStrip> consoleMix() const;   // the console's strips as the service names them (licensed channels only)
  // SETTINGS SYNC: the console settings a remote may change (the service's "settings"), kept in step both ways like the faders
  QVariantMap consoleSettings() const;                       // {width, outputBits, dcBlock, analog, autoLevel, autoTarget, autoMax, winGain, winMuteSync}
  void applySetting(const QString& key, const QVariant& v);  // one setting from a remote onto the console
 signals:
  void synced();
  void pulseChanged();
  void linksChanged();
 private:
  void writeSeed();
  void syncTick();
  void post(const QVariantMap& cmd);
 public:
  /** a command to the running service (a SAMPLE PAD while it plays the sound); false when it does not run */
  bool send(const QVariantMap& cmd) { if (!running()) return false; ensureToken(); if (token_.isEmpty()) return false; post(cmd); return true; }
 private:
  QTimer sync_; QNetworkAccessManager net_; QVector<SyncStrip> base_; double baseMaster_ = 0; bool baseMute_ = false; quint64 v_ = 0; int posting_ = 0; bool stale_ = false, getting_ = false; QByteArray token_; int syncedChanges_ = 0, remotes_ = 0, pulse_ = 0; qint64 lastTouch_ = 0; int dTicks_ = 0, dGets_ = 0, dApplied_ = 0; QHash<int, qint64> touched_;
  QProcess nginx_, bridge_; QString webDir() const; QString webStackPrefix() const; void startWebStack(); void stopWebStack();
  Engine* engine_; QProcess proc_; Extension ext_{QStringLiteral("AmF X1 Network")}; QString status_ = "off";
  QString runTool(const QStringList& args, bool* ok) const; void restartIfRunning();
  QVariantList tokens_; QString tokenNote_; bool wifi_ = false;
  QVariantMap baseSet_;   // the settings both sides last agreed on
  quint64 racksV_ = 0; QHash<int, QByteArray> baseRacks_; int rackTick_ = 0; QVariantMap baseMast_; QVariantMap mastNow() const; QVariantList baseIn_, baseOut_; QVariantList baseDca_; static QVariantList dcaSync(const QVariantList& dcaState);   // DCA GROUP CHANNELS as the service last got them   // ROUTING as the service last got it   // the racks as the service last got them (strip -> JSON)
  QByteArray rackJson(int strip) const; void takeRacks();
  quint64 masterV_ = 0;   // the RTA EQ changes a remote made, taken back   // the plug-ins a phone changed, taken back into the console's racks
  QVariantList links_; QString linkNote_; QTimer linkTimer_; bool linksGetting_ = false;
  void ensureToken(); QNetworkRequest api(const QString& path) const;
  Takeover took_; bool restore_ = false, reuseTook_ = false; void giveBack();   // the console's sound card, given back when the service stops
  QString feedbackNote_; int feedbackSeen_ = 0; mutable QHash<QString, QString> adapters_;
};
