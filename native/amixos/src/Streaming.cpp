#include "Streaming.h"
#include "Remote.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QDesktopServices>
#include <QNetworkReply>
#include <QProcess>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <cstdio>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace st = am::streaming;
static long long nowS() { return QDateTime::currentSecsSinceEpoch(); }
static QString q(const std::string& s) { return QString::fromStdString(s); }

#ifdef Q_OS_WIN
static bool dpapi(const std::string& in, std::string& out, bool seal) {
  DATA_BLOB a{static_cast<DWORD>(in.size()), reinterpret_cast<BYTE*>(const_cast<char*>(in.data()))}, b{};
  const BOOL ok = seal ? CryptProtectData(&a, L"AmF X1 streaming", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &b) : CryptUnprotectData(&a, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &b);
  if (!ok) return false; out.assign(reinterpret_cast<char*>(b.pbData), b.cbData); LocalFree(b.pbData); return true;
}
#endif
bool Streaming::sealed() {
#ifdef Q_OS_WIN
  return true;
#else
  return false;
#endif
}

Streaming::Streaming(Remote* remote, QObject* parent) : QObject(parent), remote_(remote) {
#ifdef Q_OS_WIN
  for (const st::Provider& p : st::providers()) { const QByteArray b = s_.value(q("streaming/" + p.id + "/sealed")).toByteArray(); if (b.isEmpty()) continue;
    std::string plain; st::Tokens t; if (dpapi(b.toStdString(), plain, false) && st::deserialize(plain, t)) tokens_[p.id] = t; }
#endif
  // polls only while the STREAMING page is shown or the web remote's service runs: nothing ticks on the GUI thread otherwise (page budget)
  poll_.setInterval(3000); connect(&poll_, &QTimer::timeout, this, &Streaming::refresh);
  if (remote_) connect(remote_, &Remote::changed, this, &Streaming::updatePoll);
  updatePoll();
}
void Streaming::updatePoll() { const bool want = !dryRun && (active_ || (remote_ && remote_->running())); if (want == poll_.isActive()) return; if (want) poll_.start(); else poll_.stop(); }
void Streaming::log(const QString& line) const { std::fprintf(stderr, "streaming: %s\n", st::redact(line.toStdString()).c_str()); }   // every log line is redacted
void Streaming::store(const std::string& id) {
#ifdef Q_OS_WIN
  std::string b; const auto it = tokens_.find(id);
  if (it == tokens_.end() || !it->second.valid()) { s_.remove(q("streaming/" + id + "/sealed")); return; }
  if (dpapi(st::serialize(it->second), b, true)) s_.setValue(q("streaming/" + id + "/sealed"), QByteArray::fromStdString(b));
#else
  (void)id;   // no OS key store used here: the sign-in lasts until the console closes (nothing is written)
#endif
}
QVariantList Streaming::providers() const {
  QVariantList l;
  for (const st::Provider& p : st::providers()) { QStringList steps; for (const auto& x : p.setup) steps << q(x);
    const auto it = tokens_.find(p.id);
    l << QVariantMap{{"id", q(p.id)}, {"name", q(p.name)}, {"login", p.login}, {"control", p.control}, {"note", q(p.note)}, {"setup", steps}, {"console", q(p.consoleUrl)},
                     {"clientId", s_.value(q("streaming/" + p.id + "/clientId")).toString()}, {"signedIn", it != tokens_.end() && it->second.valid()}}; }
  return l;
}
void Streaming::setActive(bool on) { if (active_ == on) return; active_ = on; updatePoll(); if (on) refresh(); emit changed(); }
void Streaming::setClientId(const QString& provider, const QString& id) {
  const QString v = id.trimmed(); if (!st::find(provider.toStdString())) return;
  if (!v.isEmpty() && !st::validClientId(v.toStdString())) { msg_ = QStringLiteral("That is not a client id (8 to 128 letters, digits, . _ -)."); emit changed(); return; }
  s_.setValue("streaming/" + provider + "/clientId", v); msg_.clear(); emit changed();
}
void Streaming::signIn(const QString& provider) {
  const st::Provider* p = st::find(provider.toStdString()); if (!p || !p->login) return;
  const QString cid = s_.value("streaming/" + provider + "/clientId").toString(); if (!st::validClientId(cid.toStdString())) { msg_ = QStringLiteral("Type the app's client id first (see the steps)."); emit changed(); return; }
  std::vector<std::uint8_t> rnd(32), sr(16); for (auto& b : rnd) b = static_cast<std::uint8_t>(QRandomGenerator::system()->bounded(256)); for (auto& b : sr) b = static_cast<std::uint8_t>(QRandomGenerator::system()->bounded(256));
  st::Pkce pk; if (!st::makePkce(rnd, pk)) return; pendProv_ = p->id; pendVerifier_ = pk.verifier; pendState_ = am::crypto::base64url(sr.data(), sr.size());
  if (dryRun) { msg_ = QStringLiteral("Sign-in would open %1 in the browser.").arg(q(p->name)); emit changed(); return; }
  if (!cb_) { cb_ = new QTcpServer(this);
    connect(cb_, &QTcpServer::newConnection, this, [this] { while (QTcpSocket* c = cb_->nextPendingConnection()) { connect(c, &QTcpSocket::readyRead, this, [this, c] {
      const QByteArray req = c->readAll(); const QList<QByteArray> parts = req.split(' '); const QUrl u(QString::fromLatin1(parts.value(1)));
      const QUrlQuery uq(u.query()); c->write("HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nConnection: close\r\n\r\nSigned in: you can close this tab and go back to the console.");
      c->disconnectFromHost(); c->deleteLater(); if (u.path() == "/callback") finishSignIn(uq.queryItemValue("code"), uq.queryItemValue("state")); }); } }); }
  if (!cb_->isListening() && !cb_->listen(QHostAddress::LocalHost, 43117)) { msg_ = QStringLiteral("Port 43117 is in use: close what uses it and try again."); emit changed(); return; }
  QDesktopServices::openUrl(QUrl(q(st::authorizeUrl(*p, cid.toStdString(), pendState_, pk.challenge))));
  msg_ = QStringLiteral("Sign in to %1 in the browser that opened.").arg(q(p->name)); emit changed();
}
void Streaming::finishSignIn(const QString& code, const QString& state) {
  if (cb_) cb_->close();
  const st::Provider* p = st::find(pendProv_); if (!p || code.isEmpty() || state.toStdString() != pendState_) { msg_ = QStringLiteral("Sign-in cancelled or refused."); pendState_.clear(); emit changed(); return; }
  const QString cid = s_.value(q("streaming/" + p->id + "/clientId")).toString(); const std::string id = p->id;
  QNetworkRequest rq(QUrl(q(p->tokenUrl))); rq.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
  QNetworkReply* r = net_.post(rq, QByteArray::fromStdString(st::tokenBody(cid.toStdString(), code.toStdString(), pendVerifier_))); pendVerifier_.clear(); pendState_.clear();
  connect(r, &QNetworkReply::finished, this, [this, r, id] { r->deleteLater(); st::Tokens t; std::string err;
    if (st::parseTokens(r->readAll().toStdString(), nowS(), t, err)) { tokens_[id] = t; store(id); msg_ = QStringLiteral("Signed in."); } else { msg_ = q(err); log(q("sign-in failed: " + err)); }
    emit changed(); refresh(); });
}
void Streaming::signOut(const QString& provider) { tokens_.erase(provider.toStdString()); store(provider.toStdString()); msg_ = QStringLiteral("Signed out."); emit changed(); }

void Streaming::refresh() {
  if (dryRun) return;
  const bool remoteOn = remote_ && remote_->running(); if (!active_ && !remoteOn) return;
  auto it = tokens_.find("spotify"); if (it != tokens_.end() && it->second.valid()) spotify(QString()); else mediaSession(QString());
  if (remoteOn) remote_->streamingSync(now_, [this](const QStringList& cmds) { for (const QString& c : cmds) command(c); });
}
void Streaming::command(const QString& cmd) {
  if (!st::validCommand(cmd.toStdString()) || dryRun) return;
  auto it = tokens_.find("spotify"); if (it != tokens_.end() && it->second.valid()) spotify(cmd); else mediaSession(cmd);
}
void Streaming::spotify(const QString& cmd) {
  const st::Provider* p = st::find("spotify"); st::Tokens& t = tokens_["spotify"]; const QString cid = s_.value("streaming/spotify/clientId").toString();
  if (t.expiresAt - nowS() <= 60) {   // refresh first (asynchronously), then try again
    if (t.refresh.empty()) { t = st::Tokens{}; store("spotify"); emit changed(); return; }
    QNetworkRequest rq(QUrl(q(p->tokenUrl))); rq.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QNetworkReply* r = net_.post(rq, QByteArray::fromStdString(st::refreshBody(cid.toStdString(), t.refresh)));
    connect(r, &QNetworkReply::finished, this, [this, r, cmd] { r->deleteLater(); const int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); st::Tokens& tt = tokens_["spotify"]; st::Tokens n = tt; std::string err;
      if (code == 200 && st::parseTokens(r->readAll().toStdString(), nowS(), n, err)) { tt = n; store("spotify"); spotify(cmd); }
      else if (code == 400 || code == 401) { tt = st::Tokens{}; store("spotify"); msg_ = QStringLiteral("The Spotify sign-in ended: sign in again."); emit changed(); } });
    return; }
  auto req = [&](const QString& path) { QNetworkRequest rq(QUrl(q(p->apiBase) + path)); rq.setRawHeader("Authorization", "Bearer " + QByteArray::fromStdString(t.access)); rq.setTransferTimeout(5000); return rq; };
  if (cmd.isEmpty()) { QNetworkReply* r = net_.get(req("/me/player/currently-playing"));
    connect(r, &QNetworkReply::finished, this, [this, r] { r->deleteLater(); const st::NowPlaying n = st::parseSpotifyPlayer(r->readAll().toStdString());
      now_ = {{"service", "Spotify"}, {"title", q(n.title)}, {"artist", q(n.artist)}, {"album", q(n.album)}, {"playing", n.playing}, {"any", n.any}, {"source", "Spotify Web API"}}; emit changed(); });
    return; }
  const auto c = st::spotifyCall(cmd.toStdString(), now_.value("playing").toBool()); QNetworkRequest rq = req(q(c.second));
  QNetworkReply* r = net_.sendCustomRequest(rq, QByteArray::fromStdString(c.first), QByteArray());
  connect(r, &QNetworkReply::finished, this, [this, r] { r->deleteLater(); const int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (code == 403) { msg_ = QStringLiteral("Spotify refused: play / pause / next need Spotify Premium."); emit changed(); } QTimer::singleShot(600, this, [this] { spotify(QString()); }); });
}

// MEDIA SESSION: a fixed script, the action from the allow-list only (never text from elsewhere)
void Streaming::mediaSession(const QString& action) {
  if (!action.isEmpty() && !st::validCommand(action.toStdString())) return;
  auto* pr = new QProcess(this); QString prog; QStringList args;
#ifdef Q_OS_WIN
  static const QString ps = QStringLiteral(
      "$ErrorActionPreference='SilentlyContinue';Add-Type -AssemblyName System.Runtime.WindowsRuntime;"
      "$at=([System.WindowsRuntimeSystemExtensions].GetMethods()|?{$_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'})[0];"
      "function W($o,$t){$k=$at.MakeGenericMethod($t).Invoke($null,@($o));$k.Wait(-1)|Out-Null;$k.Result};"
      "[Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager,Windows.Media.Control,ContentType=WindowsRuntime]|Out-Null;"
      "$m=W ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager]::RequestAsync()) ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager]);"
      "$s=$m.GetCurrentSession();if(-not $s){'{}';exit};$a='%1';"
      "if($a -eq 'toggle'){W ($s.TryTogglePlayPauseAsync()) ([bool])|Out-Null}elseif($a -eq 'play'){W ($s.TryPlayAsync()) ([bool])|Out-Null}elseif($a -eq 'pause'){W ($s.TryPauseAsync()) ([bool])|Out-Null}"
      "elseif($a -eq 'next'){W ($s.TrySkipNextAsync()) ([bool])|Out-Null}elseif($a -eq 'previous'){W ($s.TrySkipPreviousAsync()) ([bool])|Out-Null};"
      "$p=W ($s.TryGetMediaPropertiesAsync()) ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionMediaProperties]);"
      "@{app=$s.SourceAppUserModelId;title=$p.Title;artist=$p.Artist;album=$p.AlbumTitle;playing=($s.GetPlaybackInfo().PlaybackStatus -eq 'Playing')}|ConvertTo-Json -Compress");
  prog = QStringLiteral("powershell"); args = {"-NoProfile", "-NonInteractive", "-Command", ps.arg(action)};
#else
  prog = QStringLiteral("playerctl");
  if (!action.isEmpty()) args = {action == "toggle" ? QStringLiteral("play-pause") : action};
  else args = {"metadata", "--format", "{{status}}\t{{playerName}}\t{{title}}\t{{artist}}\t{{album}}"};
#endif
  connect(pr, &QProcess::finished, this, [this, pr, action](int, QProcess::ExitStatus) { pr->deleteLater(); const QString out = QString::fromUtf8(pr->readAllStandardOutput()).trimmed();
    if (!action.isEmpty()) { QTimer::singleShot(500, this, [this] { mediaSession(QString()); }); return; }
    QVariantMap n{{"source", "media session"}};
#ifdef Q_OS_WIN
    const QVariantMap m = QJsonDocument::fromJson(out.toUtf8()).toVariant().toMap(); n["any"] = !m.value("title").toString().isEmpty(); n["title"] = m.value("title"); n["artist"] = m.value("artist"); n["album"] = m.value("album"); n["playing"] = m.value("playing"); n["service"] = m.value("app").toString().section('!', 0, 0).section('.', -1);
#else
    const QStringList f = out.split('\t'); n["any"] = f.size() >= 5 && !f[2].isEmpty(); if (f.size() >= 5) { n["playing"] = f[0] == "Playing"; n["service"] = f[1]; n["title"] = f[2]; n["artist"] = f[3]; n["album"] = f[4]; }
#endif
    for (auto it = n.begin(); it != n.end(); ++it) if (it.value().typeId() == QMetaType::QString) it.value() = it.value().toString().left(200);
    if (n != now_) { now_ = n; emit changed(); } });
  connect(pr, &QProcess::errorOccurred, this, [this, pr](QProcess::ProcessError) { pr->deleteLater(); if (now_.value("source").toString() != "none") { now_ = {{"source", "none"}, {"any", false}}; emit changed(); } });
  pr->start(prog, args);
}
