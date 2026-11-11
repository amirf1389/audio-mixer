// PHONE REMOTE over Wi-Fi: starts and stops the remote service (amixos-daemon, the program next to the console) as a child process.
// Local policy: the service answers this computer and private-network addresses only, and every control call needs the token. Nothing is
// encrypted on the wire: for use on your own network; for anything else put the nginx TLS front (deploy/nginx) before it.
// The service runs its own engine, so the console hands the sound card over while it runs and takes it back when it stops.
#pragma once
#include <QObject>
#include <QProcess>
#include <QStringList>

class Engine;

class Remote : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool running READ running NOTIFY changed)
  Q_PROPERTY(bool available READ available CONSTANT)          // the service program is next to the console
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(int port READ port CONSTANT)
  Q_PROPERTY(QStringList addresses READ addresses NOTIFY changed)   // http://<private address>:<port> of this computer
  Q_PROPERTY(QString tokenFile READ tokenFile CONSTANT)
 public:
  explicit Remote(Engine* engine, QObject* parent = nullptr);
  ~Remote() override;
  bool running() const { return proc_.state() != QProcess::NotRunning; }
  bool available() const; QString status() const { return status_; } int port() const { return 8765; }
  QStringList addresses() const; QString tokenFile() const;
  Q_INVOKABLE bool start(bool wifi);        // wifi false = this computer only (127.0.0.1)
  Q_INVOKABLE void stop();
  Q_INVOKABLE QString token() const;        // the token to type into the phone (read from the token file when asked; never logged)
  QString program() const;
 signals:
  void changed();
  void stopped();
 private:
  Engine* engine_; QProcess proc_; QString status_ = "off";
};
