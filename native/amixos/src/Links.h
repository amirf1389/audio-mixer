// STREAMING LINKS: the list of pasted links to YouTube, Spotify, TIDAL, Deezer and TikTok (engine/cpp/app/links). A link is recognised
// from its address alone, kept with a name, and OPEN hands its clean https address (or the service's own app address) to the system: the
// service's app or the browser plays it. The console never fetches, downloads, records or decodes what a link points to.
#pragma once
#include <QObject>
#include <QSettings>
#include <QVariant>

class Links : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList list READ list NOTIFY changed)        // [{service, serviceName, kind, id, web, app, name}]
  Q_PROPERTY(QString message READ message NOTIFY changed)
 public:
  explicit Links(QObject* parent = nullptr);
  QVariantList list() const { return list_; } QString message() const { return msg_; }
  Q_INVOKABLE QVariantMap check(const QString& url) const;       // what a link is, without adding it: {ok, service, serviceName, kind, id, web, app, error}
  Q_INVOKABLE bool add(const QString& url, const QString& name); // false (with a message) when it is not one of the services, or already in the list
  Q_INVOKABLE void remove(int index);
  Q_INVOKABLE bool open(int index, bool inApp);                  // the service's own app address where it has one, else the https address
  Q_INVOKABLE QString lastOpened() const { return lastOpened_; } // for the self-test
  bool dryRun = false;                                           // the self-test: record what would be opened, open nothing
  static constexpr int kMax = 200;
 signals:
  void changed();
 private:
  void save(); QVariantList list_; QString msg_, lastOpened_; QSettings s_;
};
