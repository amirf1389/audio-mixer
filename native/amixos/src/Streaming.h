// STREAMING (console): the services' sign-in (OAuth 2.0 + PKCE, the user's own client id, no client secret), now playing and the
// transport buttons, through official APIs only (engine/cpp/app/streaming). Without a sign-in: the system's MEDIA SESSION
// (Windows GlobalSystemMediaTransportControls via PowerShell, Linux MPRIS via playerctl). Tokens: sealed by DPAPI on Windows; other
// systems keep them in memory only (until the console closes). Nothing is downloaded, recorded or captured.
#pragma once
#include <QNetworkAccessManager>
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVariant>
#include <map>
#include "streaming/streaming.hpp"

class QTcpServer;
class Remote;
class Streaming : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList providers READ providers NOTIFY changed)
  Q_PROPERTY(QVariantMap now READ now NOTIFY changed)
  Q_PROPERTY(QString message READ message NOTIFY changed)
  Q_PROPERTY(bool active READ active WRITE setActive NOTIFY changed)
  Q_PROPERTY(bool sealed READ sealed CONSTANT)
 public:
  explicit Streaming(Remote* remote, QObject* parent = nullptr);
  QVariantList providers() const; QVariantMap now() const { return now_; } QString message() const { return msg_; }
  bool active() const { return active_; } void setActive(bool on);
  static bool sealed();   // the OS seals the tokens here (DPAPI)
  Q_INVOKABLE void setClientId(const QString& provider, const QString& id);
  Q_INVOKABLE void signIn(const QString& provider);
  Q_INVOKABLE void signOut(const QString& provider);
  Q_INVOKABLE void command(const QString& cmd);   // play pause toggle next previous
  Q_INVOKABLE void refresh();
  bool dryRun = false;   // --shots / --smoke: no network, no processes
 signals:
  void changed();
 private:
  void updatePoll();
  void mediaSession(const QString& action); void spotify(const QString& cmd); void finishSignIn(const QString& code, const QString& state);
  void store(const std::string& id); void log(const QString& line) const;
  QVariantMap now_; QString msg_; bool active_ = false; QSettings s_; QNetworkAccessManager net_; QTimer poll_; Remote* remote_;
  std::map<std::string, am::streaming::Tokens> tokens_; QTcpServer* cb_ = nullptr; std::string pendProv_, pendState_, pendVerifier_;
};
