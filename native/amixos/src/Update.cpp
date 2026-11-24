#include <algorithm>
#include "Update.h"
#include <memory>
#include <QRegularExpression>
#include "paths/paths.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QNetworkReply>
#include <QProcess>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QStandardPaths>
#include <QStorageInfo>

#include "license/crypto.hpp"
#include "license/license.hpp"
#include "update_logic.hpp"

const char* Update::kDefaultUrl = "https://raw.githubusercontent.com/amirf1389/audio-mixer/main/releases/update.json";

Update::Update(const QString& currentVersion, QObject* parent) : QObject(parent), current_(currentVersion) {
  for (const QString& h : s_.value("update/hosts").toStringList()) am::upd::extraHosts().push_back(h.toStdString());   // the hosts the owner added
  // back from INSTALL AND RESTART: the version we were installing is now the running one (or it is not: say so once)
  const QString pending = s_.value("update/installing").toString();
  if (!pending.isEmpty()) { s_.remove("update/installing"); if (compareVersions(current_, pending) >= 0) justUpdated_ = current_; else failed_ = pending; }
  connect(&laterTimer_, &QTimer::timeout, this, [this] { emit changed(); });
  // a console that stays open for days still hears about a new version: the manifest is read again every 6 hours (when "check at start" is on)
  recheck_.setInterval(6 * 3600 * 1000); connect(&recheck_, &QTimer::timeout, this, [this] { if (checkAtStart() && state_ != "checking" && state_ != "downloading") check(); }); recheck_.start();
}
bool Update::canInstall() const {
#if defined(_WIN32)
  return state_ == "downloaded" && !downloaded_.isEmpty() && downloaded_.endsWith(".exe", Qt::CaseInsensitive) && QFileInfo::exists(downloaded_);
#else
  return false;
#endif
}
bool Update::installAndRestart() {
  if (!canInstall()) { set(state_, "nothing to install: download the update first (this works on Windows; on other systems open the download folder)"); return false; }
  QFile f(downloaded_); if (!f.open(QIODevice::ReadOnly)) { set("error", "cannot open the downloaded setup"); return false; }
  QCryptographicHash h(QCryptographicHash::Sha256); h.addData(&f); f.close();
  if (QString::fromLatin1(h.result().toHex()) != file_["sha256"].toString().toLower()) { QFile::remove(downloaded_); downloaded_.clear(); set("error", "the setup on disk no longer matches the signed manifest: deleted, download it again"); return false; }
  s_.setValue("update/installing", latest_); s_.sync();
  if (!QProcess::startDetached(downloaded_, {"/S", "/RESTART"})) { s_.remove("update/installing"); set("error", "the setup could not be started"); return false; }
  set("installing", QStringLiteral("installing %1: the console closes and starts again by itself").arg(latest_)); emit restarting(); return true;
}
void Update::setUrl(const QString& u) { const QString t = u.trimmed(); if (!am::upd::hostAllowed(t.toStdString())) { set("error", "the manifest URL must be https on one of the project's own hosts"); return; } s_.setValue("update/url", t); emit changed(); }
// THE UPDATE LINK IS OPEN: any https address may hold the update list. Its host joins the allowed ones; what the list offers is still
// only installed when the list is signed by the vendor and the file matches its checksum.
bool Update::setServerLink(const QString& link) {
  const QUrl u(link.trimmed()); const QString h = u.host().toLower();
  if (u.scheme() != "https" || h.isEmpty() || !u.userInfo().isEmpty() || link.contains('@')) { set("error", "the update link must be an https address"); return false; }
  QStringList hosts = s_.value("update/hosts").toStringList();
  if (!am::upd::hostAllowed(u.toString().toStdString())) { if (hosts.size() >= 8) hosts.removeFirst(); hosts << h; s_.setValue("update/hosts", hosts); am::upd::extraHosts().push_back(h.toStdString()); }
  s_.setValue("update/url", u.toString()); s_.sync(); emit changed(); return true;
}
void Update::setLicenseServer(const QString& link) {
  QString t = link.trimmed(); while (t.endsWith('/')) t.chop(1); const QUrl u(t);
  const bool local = u.scheme() == "http" && (u.host() == "127.0.0.1" || u.host() == "localhost");   // a server on this computer (the vendor's own desk)
  if (!t.isEmpty() && !(u.scheme() == "https" || local)) { redeem_ = QStringLiteral("The licence server must be an https address."); emit changed(); return; }
  s_.setValue("licence/server", t); s_.sync(); if (redeem_ != "asking") redeem_.clear(); emit changed();
}
QString Update::manageLicenceUrl(const QString& machine, const QString& edition) const {
  QString base = licenseServer().trimmed(); while (base.endsWith('/')) base.chop(1);
  const QUrl b(base); const bool local = b.scheme() == "http" && (b.host() == "127.0.0.1" || b.host() == "localhost");
  if (base.isEmpty() || !b.isValid() || b.host().isEmpty() || !(b.scheme() == "https" || local) || !b.query().isEmpty() || !b.fragment().isEmpty()) return QString();
  QString m; for (const QChar c : machine.trimmed().toUpper()) if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-') m += c;   // what a machine code holds, nothing else
  m.truncate(64); if (m.size() < 8) m.clear();
  static const QStringList plans{"basic", "pro", "studio", "master", "ultimate"}; const QString e = edition.trimmed().toLower();
  QUrlQuery q; if (!m.isEmpty()) q.addQueryItem("machine", m); if (plans.contains(e)) q.addQueryItem("edition", e);
  QUrl u(base + "/licence/"); u.setQuery(q); return u.toString(QUrl::FullyEncoded);
}
void Update::redeemProductKey(const QString& code) {
  const QString server = licenseServer();
  if (server.isEmpty()) { redeem_ = QStringLiteral("Type the address of the licence server first (the vendor gives it with the product key)."); emit changed(); return; }
  if (redeem_ == "asking") return;
  QString c; for (const QChar ch : code.toUpper()) if (ch.isLetterOrNumber()) c += ch;
  if (c.size() != 25) { redeem_ = QStringLiteral("A product key has 25 characters."); emit changed(); return; }
  QNetworkRequest req{QUrl(server + "/redeem")}; req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"); req.setTransferTimeout(15000);
  redeem_ = "asking"; emit changed();
  QNetworkReply* r = net_.post(req, QJsonDocument(QJsonObject{{"code", c}}).toJson(QJsonDocument::Compact));
  connect(r, &QNetworkReply::finished, this, [this, r] {
    r->deleteLater(); const QJsonObject o = QJsonDocument::fromJson(r->readAll().left(16384)).object(); const QString key = o.value("key").toString();
    if (o.value("ok").toBool() && key.startsWith("AMIX1")) { redeem_ = "done"; emit changed(); emit redeemed(key); return; }   // the console still checks this key itself before it stores it
    redeem_ = !o.value("error").toString().isEmpty() ? QStringLiteral("The licence server says: ") + o.value("error").toString().left(120) : QStringLiteral("The licence server did not answer (") + r->errorString().left(80) + ")";
    emit changed(); });
}
void Update::activateOnline(const QString& text, const QString& machine) {
  const QString server = licenseServer();
  if (server.isEmpty()) { redeem_ = QStringLiteral("Type the address of the licence server first (the vendor gives it with the product key)."); emit changed(); return; }
  if (redeem_ == "asking") return;
  const QString t = text.trimmed(); QJsonObject body{{"machine", machine}};
  if (t.startsWith("AMIX1.")) body["key"] = t; else if (t.startsWith("AMFXE1.")) body["token"] = t;
  else { QString c; for (const QChar ch : t.toUpper()) if (ch.isLetterOrNumber()) c += ch; if (c.size() != 25) { redeem_ = QStringLiteral("Type a product key (25 characters), a licence key or an edition token."); emit changed(); return; } body["code"] = c; }
  QNetworkRequest req{QUrl(server + "/activate-online")}; req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"); req.setTransferTimeout(15000);
  redeem_ = "asking"; emit changed();
  QNetworkReply* r = net_.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
  connect(r, &QNetworkReply::finished, this, [this, r] {
    r->deleteLater(); const QJsonObject o = QJsonDocument::fromJson(r->readAll().left(16384)).object(); const QString a = o.value("activation").toString();
    if (o.value("ok").toBool() && a.startsWith("AMXA1.")) { redeem_ = "done"; emit changed(); emit activated(a); return; }   // the console verifies it for this computer before it stores it
    redeem_ = !o.value("error").toString().isEmpty() ? QStringLiteral("The licence server says: ") + o.value("error").toString().left(160) : QStringLiteral("The licence server did not answer (") + r->errorString().left(80) + ")";
    emit changed(); });
}
void Update::startTrial(const QString& machine) {
  const QString server = licenseServer();
  if (server.isEmpty()) { redeem_ = QStringLiteral("Type the address of the licence server first."); emit changed(); return; }
  if (redeem_ == "asking") return;
  QNetworkRequest req{QUrl(server + "/trial")}; req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"); req.setTransferTimeout(15000);
  redeem_ = "asking"; emit changed();
  QNetworkReply* r = net_.post(req, QJsonDocument(QJsonObject{{"machine", machine}}).toJson(QJsonDocument::Compact));
  connect(r, &QNetworkReply::finished, this, [this, r] {
    r->deleteLater(); const QJsonObject o = QJsonDocument::fromJson(r->readAll().left(16384)).object(); const QString a = o.value("activation").toString();
    if (o.value("ok").toBool() && a.startsWith("AMXA1.")) { redeem_ = "done"; emit changed(); emit activated(a); return; }
    redeem_ = !o.value("error").toString().isEmpty() ? QStringLiteral("The licence server says: ") + o.value("error").toString().left(160) : QStringLiteral("The licence server did not answer (") + r->errorString().left(80) + ")";
    emit changed(); });
}
void Update::fetchRevocations() {
  const QString server = licenseServer(); if (server.isEmpty()) return;
  QNetworkRequest req{QUrl(server + "/revocations")}; req.setTransferTimeout(15000);
  QNetworkReply* r = net_.get(req);
  connect(r, &QNetworkReply::finished, this, [this, r] { r->deleteLater(); const QString l = QJsonDocument::fromJson(r->readAll().left(1 << 20)).object().value("list").toString(); if (l.startsWith("AMXR1.")) emit revocations(l); });   // checked by the console before it is kept
}
// ---- THE UPDATE NOTICE
static qint64 nowMs(const std::function<qint64()>& clock) { return clock ? clock() : QDateTime::currentMSecsSinceEpoch(); }
bool Update::found() const { return !latest_.isEmpty() && (state_ == "available" || state_ == "downloading" || state_ == "downloaded") && compareVersions(latest_, current_) > 0; }
static am::upd::Notice noticeOf(bool found, const QSettings& s, const QString& latest, const std::function<qint64()>& clock) {
  return am::upd::noticeFor(found, latest.toStdString(), s.value("update/skipped").toString().toStdString(), s.value("update/seen").toString().toStdString(), s.value("update/toasted").toString().toStdString(), s.value("update/laterUntil", 0).toLongLong(), nowMs(clock));
}
bool Update::noticeBanner() const { return noticeOf(found(), s_, latest_, clock).banner; }
bool Update::noticeBadge() const { return noticeOf(found(), s_, latest_, clock).badge; }
void Update::remindLater(int hours) {
  hours = std::clamp(hours, 1, 24 * 14); s_.setValue("update/laterUntil", nowMs(clock) + qint64(hours) * 3600000);
  laterTimer_.setSingleShot(true); laterTimer_.start(static_cast<int>(std::min<qint64>(qint64(hours) * 3600000 + 1000, 0x7fffffff)));   // the banner comes back by itself
  emit changed();
}
void Update::skipThisVersion() { if (!latest_.isEmpty()) s_.setValue("update/skipped", latest_); emit changed(); }
void Update::unskip() { s_.remove("update/skipped"); s_.remove("update/laterUntil"); emit changed(); }
void Update::markUpdateSeen() { if (found() && s_.value("update/seen").toString() != latest_) { s_.setValue("update/seen", latest_); emit changed(); } }
void Update::announce() {
  if (!noticeOf(found(), s_, latest_, clock).toast) return;
  s_.setValue("update/toasted", latest_);   // once per version, whether or not the notification is switched on (switching it on later does not replay old news)
  if (!systemNotice()) return;
  const QString first = notes_.isEmpty() ? QString() : notes_.first().left(160);
  emit notifySystem(QStringLiteral("AmF X1 %1 is ready").arg(latest_), first.isEmpty() ? QStringLiteral("Open SOFTWARE UPDATE in the console to see what is new and install it.") : first + QStringLiteral("\nOpen SOFTWARE UPDATE in the console to install it."));
}
int Update::compareVersions(const QString& a, const QString& b) { return am::upd::cmpVersion(a.toStdString(), b.toStdString()); }
QString Update::platformKey() {
#if defined(_WIN32)
  return "win-x64-exe";
#elif defined(__APPLE__)
  return "macos-dmg";
#elif defined(__ANDROID__)
  return "android-apk";
#else
  return "linux-tar";
#endif
}
bool Update::applyManifest(const QString& text) {
  const std::string env = text.toStdString(); std::string payload, sigText;
  if (!am::upd::getString(env, "payload", payload) || !am::upd::getString(env, "signature", sigText)) { set("error", "the update file is not a signed manifest (payload + signature)"); return false; }
  std::vector<std::uint8_t> sig, x, y;
  if (!am::crypto::base64urlDecode(sigText, sig) || sig.size() != 64 || !am::crypto::base64urlDecode(am::lic::kVendorJwk.x, x) || !am::crypto::base64urlDecode(am::lic::kVendorJwk.y, y)) { set("error", "the signature is malformed"); return false; }
  if (!am::crypto::ecdsaP256Verify(x, y, reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size(), sig)) { set("error", "the signature does not match the vendor key " + QString::fromStdString(am::lic::fingerprint(am::lic::kVendorJwk)) + ": the file is not from AMFB Audio or was altered"); return false; }
  const QJsonObject m = QJsonDocument::fromJson(QByteArray::fromStdString(payload)).object();
  if (m.isEmpty() || !m.contains("version")) { set("error", "the signed payload is not a manifest"); return false; }
  latest_ = m.value("version").toString(); channel_ = m.value("channel").toString("stable"); released_ = m.value("released").toString(); notes_.clear(); for (const QJsonValue& n : m.value("notes").toArray()) notes_ << n.toString();
  file_.clear(); const QJsonObject files = m.value("files").toObject(); const QJsonObject f = files.value(platformKey()).toObject();
  if (!f.isEmpty() && am::upd::hostAllowed(f.value("url").toString().toStdString()) && am::upd::isHex64(f.value("sha256").toString().toStdString())) { file_["key"] = platformKey(); file_["name"] = f.value("name").toString(); file_["url"] = f.value("url").toString(); file_["size"] = f.value("size").toDouble(); file_["sha256"] = f.value("sha256").toString(); }
  setPluginList(m.value("plugins").toArray());
  features_.clear();
  for (const QJsonValue& v : m.value("features").toArray()) { const QJsonObject o = v.toObject(); QVariantMap e; e["version"] = o.value("version").toString(); e["date"] = o.value("date").toString(); e["title"] = o.value("title").toString(); QStringList items; for (const QJsonValue& i : o.value("items").toArray()) items << i.toString(); e["items"] = items; features_ << e; }
  s_.setValue("update/lastCheck", QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"));
  const int cmp = compareVersions(latest_, current_);
  if (cmp > 0 && autoDownload() && !file_.isEmpty() && state_ != "downloading" && state_ != "downloaded") { set("available", QStringLiteral("%1 is available: downloading").arg(latest_)); QMetaObject::invokeMethod(this, &Update::download, Qt::QueuedConnection); return true; }
  if (cmp > 0) set("available", QStringLiteral("%1 %2 (%3) is available; this is %4").arg(latest_, channel_, released_, current_)); else set("upToDate", QStringLiteral("this %1 is current (manifest %2)").arg(current_, latest_));
  return true;
}
void Update::check() {
  const QString u = url(); if (!am::upd::hostAllowed(u.toStdString())) { set("error", "the manifest URL is not on one of the project's hosts"); return; }
  set("checking", "reading " + u);
  QNetworkRequest req{QUrl(u)}; req.setTransferTimeout(15000); req.setHeader(QNetworkRequest::UserAgentHeader, "AmF-X1-console");
  QNetworkReply* r = net_.get(req);
  connect(r, &QNetworkReply::finished, this, [this, r] { r->deleteLater(); if (r->error() != QNetworkReply::NoError) { set("error", "cannot read the manifest: " + r->errorString()); return; } applyManifest(QString::fromUtf8(r->readAll())); });
}
void Update::download() {
  if (file_.isEmpty()) { set("error", "no file for this platform in the manifest"); return; }
  const QString u = file_["url"].toString(); if (!am::upd::hostAllowed(u.toStdString())) { set("error", "the download URL is not on one of the project's hosts"); return; }
  const QString dir = QString::fromStdString(am::paths::downloadsDir()); const QString dest = dir + "/" + QFileInfo(file_["name"].toString()).fileName();
  progress_ = 0; set("downloading", "downloading " + file_["name"].toString());
  QNetworkRequest req{QUrl(u)}; req.setTransferTimeout(600000); req.setHeader(QNetworkRequest::UserAgentHeader, "AmF-X1-console"); req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
  QNetworkReply* r = net_.get(req); auto* out = new QFile(dest, r); if (!out->open(QIODevice::WriteOnly | QIODevice::Truncate)) { set("error", "cannot write " + dest); r->abort(); return; }
  auto* hash = new QCryptographicHash(QCryptographicHash::Sha256);
  connect(r, &QNetworkReply::readyRead, this, [r, out, hash] { const QByteArray b = r->readAll(); out->write(b); hash->addData(b); });
  connect(r, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) { progress_ = total > 0 ? static_cast<int>(100 * got / total) : 0; emit changed(); });
  connect(r, &QNetworkReply::finished, this, [this, r, out, hash, dest] {
    r->deleteLater(); out->close(); const QString got = QString::fromLatin1(hash->result().toHex()); delete hash;
    if (r->error() != QNetworkReply::NoError) { QFile::remove(dest); set("error", "download failed: " + r->errorString()); return; }
    if (got != file_["sha256"].toString().toLower()) { QFile::remove(dest); set("error", "the download's sha256 does not match the signed manifest: deleted"); return; }
    downloaded_ = dest; progress_ = 100; set("downloaded", "verified and saved: " + dest + ". Open it to install; the console does not run installers itself.");
  });
}
int Update::setPluginList(const QJsonArray& list) {
  pluginList_.clear();
  for (const QJsonValue& v : list) { if (pluginList_.size() >= 200) break; const QJsonObject o = v.toObject(), f = o.value("files").toObject().value(platformKey()).toObject();
    const QString id = o.value("id").toString(), file = f.value("name").toString(), url = f.value("url").toString(), sha = f.value("sha256").toString().toLower(); const double size = f.value("size").toDouble();
    static const QRegularExpression idRe(QStringLiteral("^[a-z0-9][a-z0-9._-]{0,47}$"));
    if (!idRe.match(id).hasMatch() || !am::upd::pluginFileOk(file.toStdString()) || !am::upd::hostAllowed(url.toStdString()) || !am::upd::isHex64(sha.toStdString()) || size < 1 || size > static_cast<double>(am::upd::kMaxPluginBytes)) continue;   // anything odd is left out
    bool dup = false; for (const QVariant& e : pluginList_) if (e.toMap()["id"].toString() == id || e.toMap()["file"].toString().compare(file, Qt::CaseInsensitive) == 0) dup = true;
    if (dup) continue;
    QVariantMap e; e["id"] = id; e["name"] = o.value("name").toString().left(60); e["version"] = o.value("version").toString().left(20); e["note"] = o.value("note").toString().left(200); e["file"] = file; e["url"] = url; e["sha256"] = sha; e["size"] = size; pluginList_ << e; }
  emit pluginsChanged(); return static_cast<int>(pluginList_.size());
}
QVariantList Update::plugins() const {
  QVariantList out; for (const QVariant& v : pluginList_) { QVariantMap e = v.toMap(); e.remove("url"); e["installed"] = !pluginDir.isEmpty() && QFile::exists(pluginDir + "/" + e["file"].toString());
    e["busy"] = pluginBusy_ == e["id"].toString(); e["progress"] = e["busy"].toBool() ? pluginProgress_ : 0; out << e; }
  return out;
}
bool Update::removePlugin(const QString& id) {
  for (const QVariant& v : pluginList_) { const QVariantMap e = v.toMap(); if (e["id"].toString() != id) continue;
    const QString path = pluginDir + "/" + e["file"].toString(); const bool ok = !pluginDir.isEmpty() && QFile::exists(path) && QFile::remove(path);
    pluginMsg_ = ok ? e["name"].toString() + ": removed." : e["name"].toString() + ": nothing to remove."; emit pluginsChanged(); if (ok) emit pluginInstalled(); return ok; }
  return false;
}
void Update::downloadPlugin(const QString& id) {
  if (!pluginBusy_.isEmpty()) { pluginMsg_ = "One download at a time: wait for the one that is running."; emit pluginsChanged(); return; }
  QVariantMap e; for (const QVariant& v : pluginList_) if (v.toMap()["id"].toString() == id) e = v.toMap();
  if (e.isEmpty() || pluginDir.isEmpty()) { pluginMsg_ = "That plug-in is not in the signed list."; emit pluginsChanged(); return; }
  const QString u = e["url"].toString(), file = e["file"].toString(), name = e["name"].toString(), sha = e["sha256"].toString(); const qint64 size = static_cast<qint64>(e["size"].toDouble());
  if (!am::upd::hostAllowed(u.toStdString()) || !am::upd::pluginFileOk(file.toStdString())) { pluginMsg_ = name + ": refused (not one of the project's hosts)."; emit pluginsChanged(); return; }   // checked again right before the request
  QDir().mkpath(pluginDir); const QString dest = pluginDir + "/" + file, part = dest + ".part";
  QNetworkRequest req{QUrl(u)}; req.setTransferTimeout(600000); req.setHeader(QNetworkRequest::UserAgentHeader, "AmF-X1-console"); req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
  QNetworkReply* r = net_.get(req); auto* out = new QFile(part, r); if (!out->open(QIODevice::WriteOnly | QIODevice::Truncate)) { pluginMsg_ = name + ": cannot write into the plug-in folder."; r->abort(); r->deleteLater(); emit pluginsChanged(); return; }
  pluginBusy_ = id; pluginProgress_ = 0; pluginMsg_ = "Downloading " + name + " ..."; emit pluginsChanged();
  auto hash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256); auto got = std::make_shared<qint64>(0);
  connect(r, &QNetworkReply::readyRead, this, [this, r, out, hash, got, size] { const QByteArray b = r->readAll(); *got += b.size(); if (*got > size) { r->abort(); return; }   // more than the signed size: stop at once
    out->write(b); hash->addData(b); pluginProgress_ = size > 0 ? static_cast<int>(100 * *got / size) : 0; emit pluginsChanged(); });
  connect(r, &QNetworkReply::finished, this, [this, r, out, hash, got, size, sha, dest, part, name] {
    r->deleteLater(); out->close(); pluginBusy_.clear(); pluginProgress_ = 0; const QString have = QString::fromLatin1(hash->result().toHex());
    if (r->error() != QNetworkReply::NoError || *got != size || have != sha) { QFile::remove(part);
      pluginMsg_ = name + (*got > size ? ": larger than the signed list says: deleted." : r->error() != QNetworkReply::NoError ? ": the download failed (" + r->errorString() + ")." : ": the file does not match the signed list (sha256): deleted."); emit pluginsChanged(); return; }
    QFile::remove(dest); if (!QFile::rename(part, dest)) { QFile::remove(part); pluginMsg_ = name + ": could not be put into the plug-in folder."; emit pluginsChanged(); return; }
    pluginMsg_ = name + ": downloaded and verified. It is in the plug-in list now."; emit pluginsChanged(); emit pluginInstalled(); });
}
void Update::openDownloadFolder() { QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(am::paths::downloadsDir()))); }
int Update::newFeatures() const {
  const QString seen = s_.value("update/featuresSeen", current_).toString(); int n = 0;
  for (const QVariant& v : features_) if (compareVersions(v.toMap().value("version").toString(), seen) > 0) ++n;
  return n;
}
void Update::markFeaturesSeen() { QString top = current_; for (const QVariant& v : features_) { const QString ver = v.toMap().value("version").toString(); if (compareVersions(ver, top) > 0) top = ver; } s_.setValue("update/featuresSeen", top); emit changed(); }

// ---- LOCAL UPDATE --------------------------------------------------------------------------------------------------------------------
namespace {
// the signed envelope {payload, signature}: the payload's object when one of the trusted keys signed it, else empty with the reason
QJsonObject verifiedPayload(const QString& text, const std::vector<am::lic::Jwk>& trusted, QString& why) {
  const std::string env = text.toStdString(); std::string payload, sigText;
  if (!am::upd::getString(env, "payload", payload) || !am::upd::getString(env, "signature", sigText)) { why = QStringLiteral("not a signed update (payload + signature)"); return {}; }
  std::vector<std::uint8_t> sig; if (!am::crypto::base64urlDecode(sigText, sig) || sig.size() != 64) { why = QStringLiteral("the signature is malformed"); return {}; }
  bool ok = false;
  for (const am::lic::Jwk& k : trusted) { std::vector<std::uint8_t> x, y; if (!am::crypto::base64urlDecode(k.x, x) || !am::crypto::base64urlDecode(k.y, y)) continue;
    if (am::crypto::ecdsaP256Verify(x, y, reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size(), sig)) { ok = true; break; } }
  if (!ok) { why = QStringLiteral("the signature is not the vendor's: this update was changed or did not come from AMFB Audio"); return {}; }
  const QJsonObject m = QJsonDocument::fromJson(QByteArray::fromStdString(payload)).object();
  if (m.isEmpty() || !m.contains("version")) { why = QStringLiteral("the signed payload is not an update"); return {}; }
  return m;
}
// a path the person gave: the folder, its update.json, or the setup beside it -> the update.json
QString manifestFor(const QString& path) {
  const QFileInfo fi(QDir::fromNativeSeparators(path.trimmed()).remove(QLatin1Char('"')));
  if (fi.isDir()) { for (const QString& c : {QStringLiteral("update.json"), QStringLiteral("AmF-X1-update/update.json")}) if (QFileInfo::exists(fi.absoluteFilePath() + "/" + c)) return fi.absoluteFilePath() + "/" + c; return {}; }
  if (fi.suffix().compare("json", Qt::CaseInsensitive) == 0) return fi.exists() ? fi.absoluteFilePath() : QString();
  const QString side = fi.absolutePath() + "/update.json"; return QFileInfo::exists(side) ? side : QString();
}
}  // namespace

bool Update::loadLocal(const QString& path, bool allowOlder) { return loadLocalWith(path, allowOlder, {am::lic::kVendorJwk}); }
bool Update::loadLocalWith(const QString& path, bool allowOlder, const std::vector<am::lic::Jwk>& trusted) {
  const QString mf = manifestFor(path);
  if (mf.isEmpty()) { set("error", QStringLiteral("no update.json there: a local update is a folder with the signed update.json and the setup")); return false; }
  QFile f(mf); if (!f.open(QIODevice::ReadOnly) || f.size() > 4 * 1024 * 1024) { set("error", "cannot read " + mf); return false; }
  QString why; const QJsonObject m = verifiedPayload(QString::fromUtf8(f.readAll()), trusted, why); f.close();
  if (m.isEmpty()) { set("error", "local update refused: " + why); return false; }
  const QJsonObject e = m.value("files").toObject().value(platformKey()).toObject();
  const QString name = QFileInfo(e.value("name").toString()).fileName(), sha = e.value("sha256").toString().toLower();
  if (name.isEmpty() || !am::upd::isHex64(sha.toStdString())) { set("error", QStringLiteral("this update has no file for this computer (%1)").arg(platformKey())); return false; }
  const QString file = QFileInfo(mf).absolutePath() + "/" + name;
  if (!QFileInfo::exists(file)) { set("error", QStringLiteral("the signed update names %1, which is not next to update.json").arg(name)); return false; }
  // CHECKED WHERE IT RUNS: the setup is first copied into the console's own downloads folder and the COPY is hashed and installed. Checking
  // it on the stick and running it from there later let the file be swapped in between (a removable or shared drive).
  const QString dir = QString::fromStdString(am::paths::downloadsDir()); QDir().mkpath(dir);
  const QString copy = dir + "/" + name; QFile::remove(copy);
  if (!QFile::copy(file, copy)) { set("error", QStringLiteral("cannot copy %1 into %2").arg(name, dir)); return false; }
  QFile bin(copy); if (!bin.open(QIODevice::ReadOnly)) { set("error", "cannot read " + copy); return false; }
  QCryptographicHash h(QCryptographicHash::Sha256); h.addData(&bin); bin.close();
  if (QString::fromLatin1(h.result().toHex()) != sha) { QFile::remove(copy); set("error", QStringLiteral("%1 does not match the signed checksum: it was changed or damaged, do not install it").arg(name)); return false; }
  const QString ver = m.value("version").toString(); const int cmp = compareVersions(ver, current_);
  if (cmp < 0 && !allowOlder) { set("error", QStringLiteral("%1 is older than this %2: allow an older version to go back to it").arg(ver, current_)); return false; }
  latest_ = ver; channel_ = m.value("channel").toString("stable"); released_ = m.value("released").toString(); notes_.clear(); for (const QJsonValue& n : m.value("notes").toArray()) notes_ << n.toString();
  file_.clear(); file_["key"] = platformKey(); file_["name"] = name; file_["url"] = QStringLiteral("file:") + file; file_["size"] = static_cast<qlonglong>(QFileInfo(copy).size()); file_["sha256"] = sha;
  downloaded_ = copy; progress_ = 100;
  set("downloaded", QStringLiteral("local update %1 verified (signed by the vendor, checksum matches)%2: %3").arg(ver, cmp == 0 ? QStringLiteral(", the same version as this one") : cmp < 0 ? QStringLiteral(", OLDER than this one") : QString(), copy));
  return true;
}
QVariantList Update::scanLocal() {
  QStringList roots{QString::fromStdString(am::paths::downloadsDir()), QStandardPaths::writableLocation(QStandardPaths::DownloadLocation), QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)};
  for (const QStorageInfo& v : QStorageInfo::mountedVolumes()) {   // every local drive's top level (a USB stick, a card, a DVD); network shares are left out (they can hang)
    const QByteArray fs = v.fileSystemType().toLower(); const QString dev = QString::fromUtf8(v.device());
    if (!v.isValid() || !v.isReady() || dev.startsWith("\\\\") || fs.contains("cifs") || fs.contains("smb") || fs.contains("nfs") || fs.contains("fuse.sshfs")) continue;
    roots << v.rootPath(); }
  QVariantList out; QStringList seen;
  for (const QString& r : roots) { if (r.isEmpty()) continue;
    for (const QString& dir : {r, r + "/AmF-X1-update"}) { const QString mf = manifestFor(dir); if (mf.isEmpty() || seen.contains(mf)) continue; seen << mf;
      QFile f(mf); QVariantMap c; c["path"] = QFileInfo(mf).absolutePath(); if (!f.open(QIODevice::ReadOnly) || f.size() > 4 * 1024 * 1024) continue;
      QString why; const QJsonObject m = verifiedPayload(QString::fromUtf8(f.readAll()), {am::lic::kVendorJwk}, why);
      c["ok"] = !m.isEmpty(); c["why"] = why; c["version"] = m.value("version").toString(); c["older"] = !m.isEmpty() && compareVersions(m.value("version").toString(), current_) < 0; out << c; } }
  return out;
}
