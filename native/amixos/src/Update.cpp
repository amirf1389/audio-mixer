#include "Update.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include "license/crypto.hpp"
#include "license/license.hpp"
#include "update_logic.hpp"

const char* Update::kDefaultUrl = "https://raw.githubusercontent.com/amirf1389/audio-mixer/main/releases/update.json";

Update::Update(const QString& currentVersion, QObject* parent) : QObject(parent), current_(currentVersion) {}
void Update::setUrl(const QString& u) { const QString t = u.trimmed(); if (!am::upd::hostAllowed(t.toStdString())) { set("error", "the manifest URL must be https on one of the project's own hosts"); return; } s_.setValue("update/url", t); emit changed(); }
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
  s_.setValue("update/lastCheck", QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"));
  const int cmp = compareVersions(latest_, current_);
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
  const QString dir = QDir::homePath() + "/.amixos/downloads"; QDir().mkpath(dir); const QString dest = dir + "/" + QFileInfo(file_["name"].toString()).fileName();
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
void Update::openDownloadFolder() { QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::homePath() + "/.amixos/downloads")); }
