#include <QElapsedTimer>
#include "Remote.h"
#include <cmath>
#include <QSettings>
#include <QCryptographicHash>
#include <QDateTime>
#include <QVariantMap>

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include "Engine.h"
#include "paths/paths.hpp"
#include "qr/qrcodegen.hpp"

Remote::Remote(Engine* engine, QObject* parent) : QObject(parent), engine_(engine) {
  linkTimer_.setInterval(2000); connect(&linkTimer_, &QTimer::timeout, this, &Remote::refreshLinks);   // CONNECTED PHONES: every 2 s while the remote runs
  sync_.setInterval(60);   // LIVE SYNC: a remote's fader reaches this console within a frame or two
  connect(&sync_, &QTimer::timeout, this, &Remote::syncTick);
  proc_.setProcessChannelMode(QProcess::MergedChannels);
  connect(&proc_, &QProcess::readyRead, this, [this] { const QString out = QString::fromLocal8Bit(proc_.readAll()).trimmed(); if (!out.isEmpty()) { status_ = out.section('\n', -1).left(200); emit changed(); } });
  connect(&proc_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) { status_ = code == 0 ? QStringLiteral("off") : QStringLiteral("stopped (%1): %2").arg(code).arg(status_); emit changed(); emit stopped(); });
  connect(&ext_, &Extension::changed, this, [this] { const QString out = ext_.takeOutput().trimmed(); if (!out.isEmpty()) status_ = out.section('\n', -1).left(200); emit changed(); });
  connect(&ext_, &Extension::stopped, this, [this] { if (status_.isEmpty() || status_ == "off") status_ = QStringLiteral("off"); emit changed(); emit stopped(); });
  connect(&proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) { if (e == QProcess::FailedToStart) { status_ = QStringLiteral("the remote service could not be started: ") + program(); emit changed(); } });
}
// ---- LIVE SYNC (see Remote.h)
QVector<Remote::SyncStrip> Remote::consoleMix() const {
  QVector<SyncStrip> v; const QVariantList l = engine_->stripState(); const int n = qBound(1, engine_->licensedChannels(), 32);
  for (int i = 0; i < n && i < l.size(); ++i) { const QVariantMap m = l[i].toMap(); SyncStrip s; s.db = m.value("db").toDouble(); s.pan = m.value("pan").toDouble();
    s.mute = m.value("mute").toBool(); s.solo = m.value("solo").toBool(); s.name = syncName(i, m.value("name").toString());
    s.trim = engine_->trim(i); const QVariantMap t = engine_->channelEq(i); s.low = t.value("low").toDouble(); s.mid = t.value("mid").toDouble(); s.midHz = t.value("midFreq", 1000).toDouble(); s.high = t.value("high").toDouble();
    s.toneOn = t.value("on", true).toBool();   // the EQ key's own state (Engine keeps it)
    for (int b = 0; b < 8; ++b) { const QVariantMap e = engine_->eqBand(i, b); s.eq[b] = {e.value("type").toInt(), e.value("freq", 1000).toDouble(), e.value("q", 0.707).toDouble(), e.value("gainDb").toDouble()}; }
    v << s; }
  return v;
}
QVariantList Remote::eqList(const SyncStrip& s) { QVariantList l; for (const auto& e : s.eq) l << QVariant(QVariantList{e.type, e.freq, e.q, e.gain}); return l; }
QVariantMap Remote::mastNow() const {   // MASTERING as LIVE SYNC carries it (the keys the service knows)
  const QVariantMap m = engine_->mastering(); QVariantMap o; for (const char* k : {"on", "low", "air", "thr", "ratio", "attack", "release", "makeup", "drive", "gain"}) if (m.contains(k)) o[k] = m.value(k); return o; }
QByteArray Remote::rackJson(int strip) const { return QJsonDocument::fromVariant(engine_->remoteRack(strip)).toJson(QJsonDocument::Compact); }
void Remote::writeSeed() {
  base_ = consoleMix(); baseMaster_ = engine_->masterDb(); v_ = 0; posting_ = 0; stale_ = getting_ = false; token_.clear(); syncedChanges_ = 0; remotes_ = 0; touched_.clear();
  QVariantList l; for (const SyncStrip& s : base_) l << QVariantMap{{"name", s.name}, {"db", s.db}, {"pan", s.pan}, {"mute", s.mute}, {"solo", s.solo}, {"trim", s.trim},
                                                    {"tone", QVariantMap{{"low", s.low}, {"mid", s.mid}, {"midHz", s.midHz}, {"high", s.high}, {"on", s.toneOn}}}, {"eq", eqList(s)}};
  QFile f(QString::fromStdString(am::paths::file("remote-seed.json")));
  baseSet_ = consoleSettings();   // the service starts with the console's settings too
  QVariantList racks; const QVariantList ss = engine_->stripState();   // and with the console's plug-ins (with their state): they keep sounding
  for (int i = 0; i < ss.size(); ++i) { const QVariantList r = engine_->remoteRack(i); if (r.isEmpty()) continue; racks << QVariantMap{{"strip", i}, {"name", ss[i].toMap().value("name")}, {"plugins", r}}; }
  racksV_ = 0; masterV_ = 0; baseMast_ = mastNow(); baseIn_ = engine_->inputRoutes(); baseOut_ = engine_->outputRoutes(); baseRacks_.clear(); for (int i = 0; i < ss.size(); ++i) baseRacks_[i] = rackJson(i);
  if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument::fromVariant(QVariantMap{{"strips", l}, {"master", QVariantMap{{"db", baseMaster_}}}, {"settings", baseSet_}, {"racks", racks}, {"masterChain", engine_->masterChainSeed()}, {"routes", QVariantMap{{"in", baseIn_}, {"out", baseOut_}}}}).toJson(QJsonDocument::Compact));
}
QVariantMap Remote::consoleSettings() const {
  return {{"width", engine_->width()}, {"outputBits", engine_->outputBits()}, {"dcBlock", engine_->dcBlock()}, {"analog", engine_->dspMode() == QLatin1String("analog")},
          {"autoLevel", engine_->autoLevelOn()}, {"autoTarget", engine_->autoLevelTarget()}, {"autoMax", engine_->autoLevelMax()}, {"winGain", engine_->windowsGainMode()}, {"winMuteSync", engine_->windowsMuteSync()}};
}
void Remote::applySetting(const QString& k, const QVariant& v) {
  if (k == "width") engine_->setWidth(v.toDouble()); else if (k == "outputBits") engine_->setOutputBits(v.toInt()); else if (k == "dcBlock") engine_->setDcBlock(v.toBool());
  else if (k == "analog") engine_->setDspMode(v.toBool() ? QStringLiteral("analog") : QStringLiteral("digital"));
  else if (k == "autoLevel") engine_->setAutoLevelOn(v.toBool()); else if (k == "autoTarget") engine_->setAutoLevelTarget(v.toDouble()); else if (k == "autoMax") engine_->setAutoLevelMax(v.toDouble());
  else if (k == "winGain") engine_->setWindowsGainMode(v.toBool()); else if (k == "winMuteSync") engine_->setWindowsMuteSync(v.toBool());
}
void Remote::post(const QVariantMap& cmd) {
  QNetworkRequest q(QUrl(QStringLiteral("http://127.0.0.1:%1/api/control").arg(port()))); q.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"); q.setRawHeader("X-Amix-Token", token_);
  ++posting_; stale_ = true; ++syncedChanges_;
  QNetworkReply* r = net_.post(q, QJsonDocument::fromVariant(cmd).toJson(QJsonDocument::Compact));
  connect(r, &QNetworkReply::finished, this, [this, r] { r->deleteLater(); --posting_; emit synced(); });
}
bool Remote::touched(int strip, int) const { const auto it = touched_.constFind(strip); return running() && it != touched_.cend() && QDateTime::currentMSecsSinceEpoch() - it.value() < 800; }
void Remote::takeRacks() {
  ensureToken(); if (token_.isEmpty()) return;
  QNetworkReply* r = net_.get(api(QStringLiteral("/api/racks?states=1")));
  connect(r, &QNetworkReply::finished, this, [this, r] { r->deleteLater(); if (r->error() != QNetworkReply::NoError) return;
    for (const QVariant& rv : QJsonDocument::fromJson(r->readAll()).toVariant().toMap().value("racks").toList()) { const QVariantMap rk = rv.toMap();
      for (const QVariant& sv : rk.value("slots").toList()) { const QVariantMap s = sv.toMap(); if (!s.value("loaded").toBool()) continue;
        engine_->takeRemotePlugin(rk.value("strip").toInt(), s.value("slot").toInt(), s.value("bypass").toBool(), s.value("state").toString()); }
      baseRacks_[rk.value("strip").toInt()] = rackJson(rk.value("strip").toInt()); } });   // what came from a remote is not sent back
}
void Remote::syncTick() {
  ++dTicks_;
  if (!running()) { sync_.stop(); if (lastTouch_) { lastTouch_ = 0; ++pulse_; emit pulseChanged(); } return; }
  if (lastTouch_ && QDateTime::currentMSecsSinceEpoch() - lastTouch_ < 1000) { ++pulse_; emit pulseChanged(); }   // keeps the glow of a remote fader up to date, then lets it go
  if (token_.isEmpty()) { QFile f(tokenFile()); if (f.open(QIODevice::ReadOnly)) token_ = f.readLine().trimmed(); if (token_.isEmpty()) return; }
  // 1. what moved on the console since the last agreement goes out
  const QVector<SyncStrip> c = consoleMix(); const auto moved = [](double a, double b) { return std::abs(a - b) > 0.05; };
  for (int i = 0; i < c.size() && i < base_.size(); ++i) { SyncStrip& b = base_[i]; const SyncStrip& x = c[i];
    if (moved(x.db, b.db)) { post({{"op", "gain"}, {"strip", i}, {"db", x.db}}); b.db = x.db; }
    if (moved(x.pan, b.pan)) { post({{"op", "pan"}, {"strip", i}, {"pan", x.pan}}); b.pan = x.pan; }
    if (x.mute != b.mute) { post({{"op", "mute"}, {"strip", i}, {"on", x.mute}}); b.mute = x.mute; }
    if (x.solo != b.solo) { post({{"op", "solo"}, {"strip", i}, {"on", x.solo}}); b.solo = x.solo; }
    if (x.name != b.name) { post({{"op", "name"}, {"strip", i}, {"name", x.name}}); b.name = x.name; }
    if (moved(x.trim, b.trim)) { post({{"op", "trim"}, {"strip", i}, {"db", x.trim}}); b.trim = x.trim; }
    if (moved(x.low, b.low) || moved(x.mid, b.mid) || std::abs(x.midHz - b.midHz) > 1 || moved(x.high, b.high) || x.toneOn != b.toneOn) {
      post({{"op", "tone"}, {"strip", i}, {"low", x.low}, {"mid", x.mid}, {"midHz", x.midHz}, {"high", x.high}, {"on", x.toneOn}}); b.low = x.low; b.mid = x.mid; b.midHz = x.midHz; b.high = x.high; b.toneOn = x.toneOn; }
    for (int k = 0; k < 8; ++k) if (x.eq[k] != b.eq[k]) { static const char* names[] = {"off", "peaking", "lowshelf", "highshelf", "lowpass", "highpass", "notch", "allpass1", "allpass2"};   // the console's EQ out to the remotes
      post({{"op", "eq"}, {"strip", i}, {"band", k}, {"type", QString::fromLatin1(names[std::clamp(x.eq[k].type, 0, 8)])}, {"freq", x.eq[k].freq}, {"q", x.eq[k].q}, {"gainDb", x.eq[k].gain}}); b.eq[k] = x.eq[k]; } }
  if (++rackTick_ >= 8) {   // about twice a second: a plug-in added, removed, bypassed or turned in its own window goes to the service
    rackTick_ = 0; for (int i = 0; i < c.size(); ++i) { const QByteArray j = rackJson(i); if (j == baseRacks_.value(i, QByteArrayLiteral("[]"))) continue;
      baseRacks_[i] = j; post({{"op", "rack"}, {"strip", i}, {"plugins", QJsonDocument::fromJson(j).toVariant()}}); } }
  { const QVariantMap mn = mastNow(); QVariantMap diff;   // MASTERING moved on the console: out to the service and the remotes
    for (auto it = mn.cbegin(); it != mn.cend(); ++it) { const QVariant b = baseMast_.value(it.key()); if (it.value().typeId() == QMetaType::Bool ? it.value().toBool() != b.toBool() : std::abs(it.value().toDouble() - b.toDouble()) > 0.005) diff[it.key()] = it.value(); }
    if (!diff.isEmpty()) { baseMast_ = mn; diff["op"] = "mastering"; post(diff); } }
  {   // ROUTING moved on the console: out to the service (and so to the phones)
    const QVariantList in = engine_->inputRoutes(), out = engine_->outputRoutes();
    for (int i = 0; i < in.size(); ++i) if (i >= baseIn_.size() || in[i].toInt() != baseIn_[i].toInt()) post({{"op", "inRoute"}, {"strip", i}, {"input", in[i].toInt()}});
    for (int k = 0; k < out.size() && k < 32; ++k) if (k >= baseOut_.size() || out[k].toInt() != baseOut_[k].toInt()) post({{"op", "outRoute"}, {"out", k}, {"source", out[k].toInt()}});
    baseIn_ = in; baseOut_ = out; }
  if (moved(engine_->masterDb(), baseMaster_)) { baseMaster_ = engine_->masterDb(); post({{"op", "master"}, {"db", baseMaster_}}); }
  { const QVariantMap cs = consoleSettings();   // a setting changed on the console goes out (numbers compared with a small tolerance)
    for (auto it = cs.constBegin(); it != cs.constEnd(); ++it) { const QVariant b = baseSet_.value(it.key());
      const bool diff = it.value().typeId() == QMetaType::Double ? std::abs(it.value().toDouble() - b.toDouble()) > 0.005 : it.value() != b;
      if (diff) { baseSet_[it.key()] = it.value(); post({{"op", "setting"}, {"key", it.key()}, {"value", it.value()}}); } } }
  // 2. what moved on the remotes comes in (only when nothing of ours is on the way, so an older answer never undoes a newer move here)
  if (getting_ || posting_ > 0) return;
  getting_ = true; stale_ = false; ++dGets_;
  QNetworkReply* r = net_.get(QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1/api/state?since=%2").arg(port()).arg(v_))));
  connect(r, &QNetworkReply::finished, this, [this, r] { r->deleteLater(); getting_ = false;
    if (r->error() != QNetworkReply::NoError || stale_ || !running()) return;
    const QVariantMap o = QJsonDocument::fromJson(r->readAll()).toVariant().toMap();
    if (o.contains("remotes") && o.value("remotes").toInt() != remotes_) { remotes_ = o.value("remotes").toInt(); emit synced(); }
    if (o.value("same").toBool() || !o.contains("strips")) return;
    { const quint64 rv = o.value("racksV").toULongLong(); if (rv && rv != racksV_) { racksV_ = rv; if (o.value("racksBy").toString() != QLatin1String("console")) takeRacks(); } }   // our own rack change is not taken back
    { const quint64 mv = o.value("masterV").toULongLong(); const QVariantMap pe = o.value("programEq").toMap();   // a remote's RTA EQ onto the console
      if (mv && mv != masterV_) { const bool first = masterV_ == 0; masterV_ = mv; if (!first && pe.contains("gains")) engine_->setGraphicEq(-1, pe.value("gains").toList(), pe.value("on").toBool());
        const QVariantMap ms = o.value("mastering").toMap();   // a remote's MASTERING onto the console (what it already has is not sent back)
        if (!first && !ms.isEmpty()) { QVariantMap in; for (auto it = ms.cbegin(); it != ms.cend(); ++it) if (it.key() != QLatin1String("reductionDb")) in[it.key()] = it.value(); engine_->setMastering(in); baseMast_ = mastNow(); } } }   // a phone changed a plug-in: the console's instance follows
    { const QVariantMap rs = o.value("settings").toMap();   // a setting changed on a remote lands on the console
      for (auto it = baseSet_.begin(); it != baseSet_.end(); ++it) { if (!rs.contains(it.key())) continue; const QVariant r = rs.value(it.key());
        const bool diff = it.value().typeId() == QMetaType::Double ? std::abs(r.toDouble() - it.value().toDouble()) > 0.005 : (it.value().typeId() == QMetaType::Bool ? r.toBool() != it.value().toBool() : r.toInt() != it.value().toInt());
        if (diff) { it.value() = it.value().typeId() == QMetaType::Double ? QVariant(r.toDouble()) : it.value().typeId() == QMetaType::Bool ? QVariant(r.toBool()) : QVariant(r.toInt()); applySetting(it.key(), it.value()); } } }
    { const QVariantMap rt = o.value("routing").toMap(); const QVariantList in = rt.value("in").toList(), out = rt.value("out").toList();   // a phone's ROUTING onto the console
      bool moved = false;
      for (int i = 0; i < in.size() && i < baseIn_.size(); ++i) if (in[i].toInt() != baseIn_[i].toInt()) { engine_->setInputRoute(i, in[i].toInt()); moved = true; }
      for (int k = 0; k < out.size() && k < baseOut_.size(); ++k) if (out[k].toInt() != baseOut_[k].toInt()) { engine_->setOutputRoute(k, out[k].toInt()); moved = true; }
      if (moved) { baseIn_ = engine_->inputRoutes(); baseOut_ = engine_->outputRoutes(); } }
    v_ = o.value("v").toULongLong(); const QVariantList l = o.value("strips").toList(); int n = 0;
    for (int i = 0; i < l.size() && i < base_.size(); ++i) { const QVariantMap m = l[i].toMap(); SyncStrip& b = base_[i];
      const double db = m.value("db").toDouble(), pan = m.value("pan").toDouble(); const bool mu = m.value("mute").toBool(), so = m.value("solo").toBool(); const QString nm = m.value("name").toString();
      if (std::abs(db - b.db) > 0.05) { b.db = db; engine_->setGain(i, db); touched_[i] = lastTouch_ = QDateTime::currentMSecsSinceEpoch(); ++n; }
      if (std::abs(pan - b.pan) > 0.005) { b.pan = pan; engine_->setPan(i, pan); ++n; }
      if (mu != b.mute) { b.mute = mu; engine_->setMute(i, mu); ++n; }
      if (so != b.solo) { b.solo = so; engine_->setSolo(i, so); ++n; }
      if (!nm.isEmpty() && nm != b.name) { b.name = nm; engine_->setName(i, nm == syncName(i, QString()) ? QString() : nm); ++n; }
      if (m.contains("trim") && std::abs(m.value("trim").toDouble() - b.trim) > 0.05) { b.trim = m.value("trim").toDouble(); engine_->setTrim(i, b.trim); ++n; }
      if (m.contains("tone")) { const QVariantMap t = m.value("tone").toMap(); const double lo = t.value("low").toDouble(), mi = t.value("mid").toDouble(), hz = t.value("midHz", 1000).toDouble(), hi = t.value("high").toDouble(); const bool on = t.value("on", true).toBool();
        if (std::abs(lo - b.low) > 0.05 || std::abs(mi - b.mid) > 0.05 || std::abs(hz - b.midHz) > 1 || std::abs(hi - b.high) > 0.05 || on != b.toneOn) {
          b.low = lo; b.mid = mi; b.midHz = hz; b.high = hi; b.toneOn = on; engine_->setChannelEq(i, lo, mi, hz, hi, on); ++n; } }
      { const QVariantList eq = m.value("eq").toList();   // a remote's EQ band onto the console, as it moves
        for (int k = 0; k < 8 && k < eq.size(); ++k) { const QVariantList e = eq[k].toList(); if (e.size() < 4) continue; const SyncStrip::Band r{e[0].toInt(), e[1].toDouble(), e[2].toDouble(), e[3].toDouble()};
          if (r != b.eq[k]) { b.eq[k] = r; engine_->setEq(i, k, r.type, r.freq, r.q, r.gain); ++n; } } } }
    const double md = o.value("master").toMap().value("db", baseMaster_).toDouble();
    if (std::abs(md - baseMaster_) > 0.05) { baseMaster_ = md; engine_->setMaster(md); touched_[-1] = lastTouch_ = QDateTime::currentMSecsSinceEpoch(); ++n; }
    if (n) { syncedChanges_ += n; dApplied_ += n; emit synced(); } });   // the console's own follow-ups (a stereo link moving the partner) go out on the next tick
}

Remote::~Remote() { if (running()) { proc_.terminate(); if (!proc_.waitForFinished(1500)) proc_.kill(); } }
// The service program: "AmF X1 Network" next to the console (its name in the release); the build's own name is the fallback.
QString Remote::program() const {
  const QString d = QCoreApplication::applicationDirPath();
#ifdef _WIN32
  const QStringList names{"/AmF X1 Network.exe", "/amixos-daemon.exe"};
#else
  const QStringList names{"/AmF X1 Network", "/amixos-daemon", "/engine/amixos-daemon"};
#endif
  for (const QString& n : names) if (QFileInfo::exists(d + n)) return d + n;
  return d + names.first();
}
// THE NAMED TOKENS (one per phone or operator): the service program keeps them (hashes only); the console asks it to list, make and revoke.
QString Remote::runTool(const QStringList& args, bool* ok) const {
  // the window keeps painting while the service program works (it normally answers in a fraction of a second; a stuck one used to
  // hold the whole window for up to 11 seconds: "Not Responding"). Clicks and keys wait until it is done, so nothing runs twice.
  QProcess p; p.start(program(), args); QElapsedTimer t; t.start();
  while (p.state() != QProcess::NotRunning && t.elapsed() < 8000) { if (p.waitForFinished(15)) break; QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 10); }
  const bool fin = p.state() == QProcess::NotRunning && p.error() != QProcess::FailedToStart; if (!fin) { p.kill(); p.waitForFinished(200); }
  if (ok) *ok = fin && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
  return QString::fromUtf8(fin ? (p.exitCode() == 0 ? p.readAllStandardOutput() : p.readAllStandardError()) : QByteArray("the service program did not answer"));
}
QString Remote::pairPin() const {
  const QString t = token(); if (t.isEmpty()) return {};
  const QByteArray h = QCryptographicHash::hash((t + "|pair|" + QString::number(QDateTime::currentSecsSinceEpoch() / 60)).toUtf8(), QCryptographicHash::Sha256).toHex();
  return QString::number(h.left(8).toULong(nullptr, 16) % 1000000UL).rightJustified(6, '0');
}
QString Remote::pairUrl(const QString& address) const { const QString pin = pairPin(); return pin.isEmpty() || address.isEmpty() ? QString() : address + QStringLiteral("/remote#pin=") + pin; }
QVariantList Remote::qr(const QString& text) const {
  QVariantList out; if (text.isEmpty() || text.size() > 200) return out;
  try { const qrcodegen::QrCode q = qrcodegen::QrCode::encodeText(text.toUtf8().constData(), qrcodegen::QrCode::Ecc::MEDIUM);
    const int n = q.getSize(); out.reserve(n * n + 1); out << n;
    for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) out << (q.getModule(x, y) ? 1 : 0);
  } catch (...) { out.clear(); }
  return out;
}
int Remote::pairSecondsLeft() const { return 60 - static_cast<int>(QDateTime::currentSecsSinceEpoch() % 60); }
void Remote::refreshTokens() {
  if (running()) { ensureToken(); if (!token_.isEmpty()) {   // the running service answers itself (also when only its extension is installed)
    QNetworkReply* r = net_.get(api(QStringLiteral("/api/tokens")));
    connect(r, &QNetworkReply::finished, this, [this, r] { r->deleteLater(); if (r->error() != QNetworkReply::NoError) return;
      const qint64 now = QDateTime::currentSecsSinceEpoch(); QVariantList l;
      for (const QVariant& v : QJsonDocument::fromJson(r->readAll()).toVariant().toMap().value("tokens").toList()) { const QVariantMap e = v.toMap(); QVariantMap m;
        const qint64 ex = e.value("expires").toLongLong(); m["id"] = e.value("id"); m["role"] = e.value("role"); m["name"] = e.value("name");
        m["state"] = e.value("revoked").toBool() ? QStringLiteral("revoked") : (ex && ex < now ? QStringLiteral("ended") : QStringLiteral("active")); l << m; }
      tokens_ = l; emit changed(); });
    return; } }
  tokens_.clear(); if (!available()) { emit changed(); return; }
  bool ok = false; const QString out = runTool({"--list-tokens"}, &ok);
  if (ok) for (const QString& line : out.split('\n', Qt::SkipEmptyParts)) {   // id  role  state  name
    const QStringList f = line.simplified().split(' '); if (f.size() < 4 || f[0].size() != 8) continue;
    QVariantMap m; m["id"] = f[0]; m["role"] = f[1]; m["state"] = f[2]; m["name"] = QStringList(f.mid(3)).join(' '); tokens_ << m; }
  emit changed();
}
void Remote::restartIfRunning() { if (!running()) return; const bool w = wifi_; stop(); start(w); }   // the service reads its tokens when it starts
QString Remote::newToken(const QString& name, const QString& role, int days) {
  if (!available()) { tokenNote_ = QStringLiteral("the service program is not installed next to the console"); emit changed(); return {}; }
  QString n; for (const QChar c : name.trimmed()) if (c.isPrint() && c != '"' && n.size() < 40) n += c;
  if (n.isEmpty()) { tokenNote_ = QStringLiteral("Give the token a name first (whose phone it is)."); emit changed(); return {}; }
  bool ok = false; const QString out = runTool({"--new-token", n, "--role", role == "view" ? "view" : "control", "--days", QString::number(qBound(0, days, 3650))}, &ok);
  QString tok; if (ok) for (const QString& line : out.split('\n', Qt::SkipEmptyParts)) if (line.trimmed().startsWith("amx_")) tok = line.trimmed();
  tokenNote_ = tok.isEmpty() ? out.trimmed().left(160) : QStringLiteral("Made. It is shown once, for 10 minutes: type it into the phone, then press nothing else here. The first phone that uses it owns it.");
  refreshTokens(); if (!tok.isEmpty()) restartIfRunning(); return tok;   // the token itself is returned to the page only: never stored or logged here
}
bool Remote::revokeToken(const QString& id) {
  if (running()) { forgetLink(id); return true; }   // the running service revokes it and closes its links: the other phones stay connected
  bool ok = false; const QString out = runTool({"--revoke-token", id}, &ok); tokenNote_ = ok ? QStringLiteral("Revoked: that phone is out.") : out.trimmed().left(160);
  refreshTokens(); return ok;
}
// ---- CONNECTED PHONES: the service's live links, asked with the master token (it never leaves this computer: 127.0.0.1)
void Remote::ensureToken() { if (token_.isEmpty()) { QFile f(tokenFile()); if (f.open(QIODevice::ReadOnly)) token_ = f.readLine().trimmed(); } }
QNetworkRequest Remote::api(const QString& path) const {
  QNetworkRequest q(QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(port()).arg(path))); q.setRawHeader("X-Amix-Token", token_); q.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  q.setTransferTimeout(3000); return q;
}
void Remote::refreshLinks() {
  if (!running()) { linkTimer_.stop(); if (!links_.isEmpty()) { links_.clear(); emit linksChanged(); } return; }
  if (!linkTimer_.isActive()) linkTimer_.start();
  ensureToken(); if (token_.isEmpty() || linksGetting_) return; linksGetting_ = true;
  QNetworkReply* r = net_.get(api(QStringLiteral("/api/links")));
  connect(r, &QNetworkReply::finished, this, [this, r] { r->deleteLater(); linksGetting_ = false; if (r->error() != QNetworkReply::NoError) return;
    QVariantList l = QJsonDocument::fromJson(r->readAll()).toVariant().toMap().value("links").toList();
    for (QVariant& v : l) { QVariantMap m = v.toMap(); m["age"] = linkAge(m.value("seconds").toInt()); m["phone"] = m.value("tokenId").toString() != QLatin1String("master"); v = m; }
    if (l != links_) { links_ = l; emit linksChanged(); } });
}
void Remote::disconnectLink(int id) {
  ensureToken(); if (!running() || token_.isEmpty()) return;
  QNetworkReply* r = net_.post(api(QStringLiteral("/api/links/kick")), QJsonDocument(QJsonObject{{"id", id}}).toJson(QJsonDocument::Compact));
  connect(r, &QNetworkReply::finished, this, [this, r] { r->deleteLater();
    linkNote_ = r->error() == QNetworkReply::NoError ? QStringLiteral("Disconnected. That phone can connect again in 2 minutes; FORGET it to need the PIN again.") : QStringLiteral("It had already gone.");
    emit linksChanged(); refreshLinks(); });
}
void Remote::forgetLink(const QString& tokenId) {
  ensureToken(); if (tokenId.isEmpty() || tokenId == QLatin1String("master")) return;
  if (!running() || token_.isEmpty()) { bool ok = false; runTool({"--revoke-token", tokenId}, &ok); linkNote_ = ok ? QStringLiteral("Forgotten: that phone needs the PIN again.") : QStringLiteral("Could not forget it."); refreshTokens(); emit linksChanged(); return; }
  QNetworkReply* r = net_.post(api(QStringLiteral("/api/tokens/revoke")), QJsonDocument(QJsonObject{{"id", tokenId}}).toJson(QJsonDocument::Compact));
  connect(r, &QNetworkReply::finished, this, [this, r] { r->deleteLater(); const bool ok = r->error() == QNetworkReply::NoError;
    linkNote_ = ok ? QStringLiteral("Forgotten: that phone is out and needs the PIN again.") : QStringLiteral("Could not forget it: ") + r->errorString();
    tokenNote_ = linkNote_; refreshTokens(); emit linksChanged(); refreshLinks(); });
}
QString Remote::linkAge(int s) {
  if (s < 60) return QStringLiteral("%1 s").arg(qMax(0, s)); if (s < 3600) return QStringLiteral("%1 min").arg(s / 60);
  return QStringLiteral("%1 h %2 min").arg(s / 3600).arg((s % 3600) / 60, 2, 10, QLatin1Char('0'));
}
bool Remote::available() const { return ext_.available() || QFileInfo::exists(program()); }
QString Remote::tokenFile() const { return QString::fromStdString(am::paths::file("daemon.token")); }
QString Remote::token() const { QFile f(tokenFile()); if (!f.open(QIODevice::ReadOnly) || f.size() > 256) return {}; return QString::fromLatin1(f.readAll()).trimmed(); }
// The private IPv4 addresses phones can reach: interfaces that are up and running, real ones (Wi-Fi, Ethernet) first. Virtual adapters
// (VirtualBox, VMware, Hyper-V / WSL vEthernet, Docker, VPN tunnels) come last: a phone on the Wi-Fi cannot reach them, and putting
// one first sent phones (and the QR code) to an address that never answers.
// The list of network cards, read at most every 3 seconds. On Windows the system call behind it can take a good part of a second
// (many adapters, a VPN), and the pages read it from several bindings at once: uncached, opening NETWORK or a burst of status lines
// from the remote service held the window ("Not Responding").
static QList<QNetworkInterface> netCards() {
  static QList<QNetworkInterface> list; static QElapsedTimer age;
  if (!age.isValid() || age.elapsed() > 3000) { list = QNetworkInterface::allInterfaces(); age.restart(); }
  return list;
}
QStringList Remote::addresses() const {
  QStringList real, virt;
  static const QStringList virtualNames{"virtualbox", "vmware", "vethernet", "hyper-v", "wsl", "docker", "vbox", "tap", "tun", "vpn", "zerotier", "tailscale", "hamachi", "loopback", "bluetooth"};
  for (const QNetworkInterface& ni : netCards()) {
    const auto f = ni.flags(); if (!(f & QNetworkInterface::IsUp) || !(f & QNetworkInterface::IsRunning) || (f & QNetworkInterface::IsLoopBack)) continue;
    const QString nm = (ni.humanReadableName() + " " + ni.name()).toLower(); bool isVirt = ni.type() == QNetworkInterface::Virtual;
    for (const QString& v : virtualNames) if (nm.contains(v)) isVirt = true;
    for (const QNetworkAddressEntry& e : ni.addressEntries()) { const QHostAddress a = e.ip(); if (a.protocol() != QAbstractSocket::IPv4Protocol) continue;
      const quint32 ip = a.toIPv4Address(); const unsigned x = ip >> 24, y = (ip >> 16) & 255;
      if (!(x == 10 || (x == 172 && y >= 16 && y <= 31) || (x == 192 && y == 168))) continue;
      if (x == 192 && y == 168 && ((ip >> 8) & 255) == 56) isVirt = true;   // VirtualBox host-only network
      const QString u = QStringLiteral("http://%1:%2").arg(a.toString()).arg(port());
      const bool wifi = ni.type() == QNetworkInterface::Wifi || ni.type() == QNetworkInterface::Ieee80211, eth = ni.type() == QNetworkInterface::Ethernet;
      if (isVirt) virt << u; else if (wifi || eth) real.prepend(u); else real << u; } }
  real.removeDuplicates(); for (const QString& v : virt) if (!real.contains(v)) real << v;
  return real;
}
bool Remote::start(bool wifi) {
  if (running()) return true;
  if (!available()) { status_ = QStringLiteral("the remote service program is not installed next to the console"); emit changed(); return false; }
  writeConsoleInfo();   // what the phone shows under PLUG-INS: the catalogue, the racks and the extensions, as they are now
  writeSeed();          // LIVE SYNC: the phones start where the console is
  engine_->stop();   // the service opens the sound card itself
  wifi_ = wifi;
  QStringList args{"--port", QString::number(port()), "--open", "auto", "--version", QCoreApplication::applicationVersion(),
    "--strips", QString::number(qBound(1, engine_->licensedChannels(), 32)), "--plan", engine_->license().value("planName").toString()};   // the phone gets the channels of the licence plan, no more
  args << "--seed" << QString::fromStdString(am::paths::file("remote-seed.json"));
  // AUTO INPUT (MICROPHONE ON AT START) off: the service opens no input either
  if (!engine_->micAtStart()) args << "--in" << QStringLiteral("-");   // AUTO INPUT OFF: the service captures nothing (it used to take COMPUTER SOUND)
  if (wifi) args << "--allow-lan";   // phones on this network may connect (local policy stays on: private addresses only). This sat inside the comment above and was never passed: every phone was refused
  if (ext_.available() && !QSettings().value("store/off").toStringList().contains(QStringLiteral("ext:AmF X1 Network"))) {   // the extension runs inside the console (unless switched off in the LOCAL STORE): same process, same rights on the sound card
    const int rc = ext_.start(args);
    if (rc == 0 && ext_.running()) { sync_.start(); linkTimer_.start(); status_ = wifi ? QStringLiteral("starting for the phones on this network (extension)") : QStringLiteral("starting for this computer only (extension)"); emit changed(); return true; }
    status_ = QStringLiteral("the network extension did not start (%1): %2").arg(rc).arg(ext_.takeOutput().trimmed().section('\n', -1).left(160)); emit changed();   // then the program next to it, if there is one
  }
  proc_.start(program(), args); if (!proc_.waitForStarted(3000)) { if (status_.isEmpty() || status_ == "off") status_ = QStringLiteral("the remote service did not start"); emit changed(); return false; }
  sync_.start(); linkTimer_.start(); status_ = wifi ? QStringLiteral("starting for the phones on this network") : QStringLiteral("starting for this computer only"); emit changed(); return true;
}
void Remote::stop() { sync_.stop(); linkTimer_.stop(); if (!links_.isEmpty()) { links_.clear(); emit linksChanged(); } if (!running()) return; if (ext_.running()) { ext_.stop(); return; } proc_.terminate(); if (!proc_.waitForFinished(2000)) proc_.kill(); }

// remote-console.json: read-only for the phone (served by the service at /api/console-info). Names and states only: no plug-in state data.
void Remote::writeConsoleInfo() const {
  QVariantMap o; o["available"] = true; o["written"] = QDateTime::currentMSecsSinceEpoch(); o["version"] = QCoreApplication::applicationVersion();
  QVariantList vst; for (const QVariant& v : engine_->vstCatalog()) { const QVariantMap m = v.toMap(); QVariantMap x; for (const char* k : {"name", "vendor", "format", "kind"}) x[k] = m.value(k); vst << x; }
  o["vst"] = vst; o["store"] = engine_->storeItems();
  QVariantList racks; const QVariantList ss = engine_->stripState();
  for (int i = 0; i < ss.size(); ++i) { QVariantList used; for (const QVariant& p : engine_->plugins(i)) { QVariantMap m = p.toMap(); if (m.value("kind").toString().isEmpty()) continue; m.remove("state"); used << m; }
    if (used.isEmpty()) continue; QVariantMap r; r["strip"] = i; r["name"] = ss[i].toMap().value("name"); r["plugins"] = used; racks << r; }
  o["racks"] = racks;
  QFile f(QString::fromStdString(am::paths::file("remote-console.json"))); if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument::fromVariant(o).toJson(QJsonDocument::Compact));
}
