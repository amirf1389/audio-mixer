// PHONE REMOTE over Wi-Fi: starts and stops the remote service (amixos-daemon, the program next to the console) as a child process.
// Local policy: the service answers this computer and private-network addresses only, and every control call needs the token. Nothing is
// encrypted on the wire: for use on your own network; for anything else put the nginx TLS front (deploy/nginx) before it.
// The service runs its own engine, so the console hands the sound card over while it runs and takes it back when it stops.
#pragma once
#include <QObject>
#include <QProcess>
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
 private:
  Engine* engine_; QProcess proc_; Extension ext_{QStringLiteral("AmF X1 Network")}; QString status_ = "off";
  QString runTool(const QStringList& args, bool* ok) const; void restartIfRunning();
  QVariantList tokens_; QString tokenNote_; bool wifi_ = false;
};
