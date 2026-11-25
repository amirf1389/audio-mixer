#ifdef _WIN32   // the default gateway of each network card (GetAdaptersAddresses): a phone reaches the card that has one
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#endif
#include <QElapsedTimer>
#include "Remote.h"
#include <QBuffer>
#include <QImage>
#include <algorithm>
#include <cstring>
#include <QSet>
#include <cmath>
#include <QSettings>
#include <QCryptographicHash>
#include <QDateTime>
#include <QVariantMap>

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
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
  connect(&proc_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) { status_ = code == 0 ? QStringLiteral("off") : QStringLiteral("stopped (%1): %2").arg(code).arg(status_); giveBack(); emit changed(); emit stopped(); });
  connect(&ext_, &Extension::changed, this, [this] { const QString out = ext_.takeOutput().trimmed(); if (!out.isEmpty()) status_ = out.section('\n', -1).left(200); emit changed(); });
  connect(&ext_, &Extension::stopped, this, [this] { if (status_.isEmpty() || status_ == "off") status_ = QStringLiteral("off"); giveBack(); emit changed(); emit stopped(); });
  connect(&proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) { if (e == QProcess::FailedToStart) { status_ = QStringLiteral("the remote service could not be started: ") + program(); emit changed(); } });
}
// ---- LIVE SYNC (see Remote.h)
QVector<Remote::SyncStrip> Remote::consoleMix() const {
  QVector<SyncStrip> v; const QVariantList l = engine_->stripState(); const int n = qBound(1, engine_->licensedChannels(), 32); const QVariantList dc = engine_->dcaState();
  for (int i = 0; i < n && i < l.size(); ++i) { const QVariantMap m = l[i].toMap(); SyncStrip s; s.db = m.value("db").toDouble(); s.pan = m.value("pan").toDouble();
    if (!std::isfinite(s.db) || s.db < -120) s.db = -120;   // a fader at -inf: down (JSON has no -inf; it used to arrive as 0 dB)
    s.mute = m.value("mute").toBool(); s.solo = m.value("solo").toBool(); s.name = syncName(i, m.value("name").toString());
    s.gMute = m.value("effectiveMute").toBool() && !s.mute;   // silent through its DCA or a MUTE GROUP: silent on the service too
    { const int g = m.value("dca").toInt(); s.gDb = g > 0 && g <= dc.size() ? std::clamp(dc[g - 1].toMap().value("db").toDouble(), -60.0, 10.0) : 0.0; }
    s.trim = engine_->trim(i); const QVariantMap t = engine_->channelEq(i); s.low = t.value("low").toDouble(); s.mid = t.value("mid").toDouble(); s.midHz = t.value("midFreq", 1000).toDouble(); s.high = t.value("high").toDouble();
    s.toneOn = t.value("on", true).toBool();   // the EQ key's own state (Engine keeps it)
    for (int b = 0; b < 8; ++b) { const QVariantMap e = engine_->eqBand(i, b); s.eq[b] = {e.value("type").toInt(), e.value("freq", 1000).toDouble(), e.value("q", 0.707).toDouble(), e.value("gainDb").toDouble()}; }
    v << s; }
  return v;
}
// the part of a DCA group the remotes share: level, mute, name, colour, members (numbers compared as the service sends them back)
QVariantList Remote::dcaSync(const QVariantList& st) {
  QVariantList out; for (const QVariant& v : st) { const QVariantMap m = v.toMap(); QVariantList mem; for (const QVariant& x : m.value("members").toList()) mem << x.toInt();
    out << QVariantMap{{"db", std::round(m.value("db").toDouble() * 100) / 100}, {"mute", m.value("mute").toBool()}, {"name", m.value("name").toString()}, {"color", m.value("color").toInt()}, {"members", mem}, {"winVol", m.value("winVol").toBool()}}; }   // winVol: the WINDOWS VOLUME switch, shown on the phones
  return out;
}
QVariantList Remote::eqList(const SyncStrip& s) { QVariantList l; for (const auto& e : s.eq) l << QVariant(QVariantList{e.type, e.freq, e.q, e.gain}); return l; }
QVariantMap Remote::mastNow() const {   // MASTERING as LIVE SYNC carries it (the keys the service knows)
  const QVariantMap m = engine_->mastering(); QVariantMap o; for (const char* k : {"on", "low", "air", "thr", "ratio", "attack", "release", "makeup", "drive", "gain"}) if (m.contains(k)) o[k] = m.value(k); o["preset"] = m.value("preset").toString(); return o; }
QByteArray Remote::rackJson(int strip) const { return QJsonDocument::fromVariant(engine_->remoteRack(strip)).toJson(QJsonDocument::Compact); }
void Remote::writeSeed() {
  base_ = consoleMix(); baseMaster_ = engine_->masterDb(); baseMute_ = engine_->masterMuted(); v_ = 0; posting_ = 0; stale_ = getting_ = false; token_.clear(); syncedChanges_ = 0; remotes_ = 0; touched_.clear();
  if (!std::isfinite(baseMaster_) || baseMaster_ < -120) baseMaster_ = -120;
  QVariantList l; for (const SyncStrip& s : base_) l << QVariantMap{{"name", s.name}, {"db", s.db}, {"pan", s.pan}, {"mute", s.mute}, {"solo", s.solo}, {"groupMute", s.gMute}, {"groupDb", s.gDb}, {"trim", s.trim},
                                                    {"tone", QVariantMap{{"low", s.low}, {"mid", s.mid}, {"midHz", s.midHz}, {"high", s.high}, {"on", s.toneOn}}}, {"eq", eqList(s)}};
  QFile f(QString::fromStdString(am::paths::file("remote-seed.json")));
  baseSet_ = consoleSettings();   // the service starts with the console's settings too
  QVariantList racks; const QVariantList ss = engine_->stripState();   // and with the console's plug-ins (with their state): they keep sounding
  for (int i = 0; i < ss.size(); ++i) { const QVariantList r = engine_->remoteRack(i); if (r.isEmpty()) continue; racks << QVariantMap{{"strip", i}, {"name", ss[i].toMap().value("name")}, {"plugins", r}}; }
  baseDca_ = dcaSync(engine_->dcaState()); feedbackSeen_ = 0; feedbackNote_.clear();   // the DCA groups go in the seed (they hold strips silent from the first block)
  racksV_ = 0; masterV_ = 0; baseMast_ = mastNow(); baseIn_ = engine_->inputRoutes(); baseOut_ = engine_->outputRoutes(); baseRacks_.clear(); for (int i = 0; i < ss.size(); ++i) baseRacks_[i] = rackJson(i);
  if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument::fromVariant(QVariantMap{{"strips", l}, {"master", QVariantMap{{"db", baseMaster_}, {"mute", baseMute_}}}, {"settings", baseSet_}, {"racks", racks}, {"masterChain", engine_->masterChainSeed()}, {"routes", QVariantMap{{"in", baseIn_}, {"out", baseOut_}}}, {"dcas", baseDca_}}).toJson(QJsonDocument::Compact));
}
QVariantMap Remote::consoleSettings() const {
  return {{"width", engine_->width()}, {"outputBits", engine_->outputBits()}, {"dcBlock", engine_->dcBlock()}, {"analog", engine_->dspMode() == QLatin1String("analog")}, {"engineer", engine_->dspMode() == QLatin1String("engineer")}, {"fast", engine_->dspMode() == QLatin1String("fast")},
          {"autoLevel", engine_->autoLevelOn()}, {"autoTarget", engine_->autoLevelTarget()}, {"autoMax", engine_->autoLevelMax()}, {"winGain", engine_->windowsGainMode()}, {"winMuteSync", engine_->windowsMuteSync()},
          {"loudMaster", engine_->loudState().value("masterOn")}, {"loudPreset", engine_->loudState().value("preset")}, {"loudTarget", engine_->loudState().value("masterTarget")},
          {"loudRide", engine_->loudState().value("rides").toInt() > 0}, {"loudGate", engine_->loudState().value("gates").toInt() > 0}};   // LOUDNESS MANAGER: state + on / off for the remotes
}
void Remote::applySetting(const QString& k, const QVariant& v) {
  if (k == "width") engine_->setWidth(v.toDouble()); else if (k == "outputBits") engine_->setOutputBits(v.toInt()); else if (k == "dcBlock") engine_->setDcBlock(v.toBool());
  else if (k == "analog") engine_->setDspMode(v.toBool() ? QStringLiteral("analog") : QStringLiteral("digital"));
  else if (k == "engineer") engine_->setDspMode(v.toBool() ? QStringLiteral("engineer") : QStringLiteral("digital"));
  else if (k == "fast") engine_->setDspMode(v.toBool() ? QStringLiteral("fast") : QStringLiteral("digital"));
  else if (k == "autoLevel") engine_->setAutoLevelOn(v.toBool()); else if (k == "autoTarget") engine_->setAutoLevelTarget(v.toDouble()); else if (k == "autoMax") engine_->setAutoLevelMax(v.toDouble());
  else if (k == "winGain") engine_->setWindowsGainMode(v.toBool()); else if (k == "winMuteSync") engine_->setWindowsMuteSync(v.toBool());
  else if (k == "loudMaster") engine_->setLoud({{"masterOn", v.toBool()}}); else if (k == "loudPreset") engine_->setLoud({{"preset", v.toString()}}); else if (k == "loudTarget") engine_->setLoud({{"masterTarget", v.toDouble()}});
  else if (k == "loudRide") engine_->setLoudAll(QStringLiteral("ride"), v.toBool()); else if (k == "loudGate") engine_->setLoudAll(QStringLiteral("gate"), v.toBool());
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
    QSet<QString> allowed; for (const QVariant& c : engine_->pluginCatalog()) { const QString k = c.toMap().value("kind").toString(); if (k.startsWith("fx:") || k.startsWith("vst3:") || k.startsWith("vst2:")) allowed << k; }
    for (const QVariant& rv : QJsonDocument::fromJson(r->readAll()).toVariant().toMap().value("racks").toList()) { const QVariantMap rk = rv.toMap();
      {   // a remote added, removed or moved an insert: the console's rack takes the same structure (only plug-ins it knows itself; built-in stages stay)
        const int st = rk.value("strip").toInt(); QHash<int, QString> want; for (const QVariant& sv : rk.value("slots").toList()) { const QVariantMap s = sv.toMap(); want[s.value("slot").toInt()] = s.value("kind").toString(); }
        QHash<int, QString> have; for (const QVariant& pv : engine_->remoteRack(st)) { const QVariantMap p = pv.toMap(); have[p.value("slot").toInt()] = p.value("kind").toString(); }
        for (auto it = have.cbegin(); it != have.cend(); ++it) if (!want.contains(it.key())) engine_->setPlugin(st, it.key(), QString());
        for (auto it = want.cbegin(); it != want.cend(); ++it) if (have.value(it.key()) != it.value() && allowed.contains(it.value()) && it.key() >= 0 && it.key() < Engine::kPluginSlots) { const bool free = engine_->plugins(st).value(it.key()).toMap().value("kind").toString().isEmpty(); if (free || have.contains(it.key())) engine_->setPlugin(st, it.key(), it.value()); } }
      for (const QVariant& sv : rk.value("slots").toList()) { const QVariantMap s = sv.toMap(); if (!s.value("loaded").toBool() && s.value("state").toString().isEmpty()) continue;
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
    if (x.gMute != b.gMute || moved(x.gDb, b.gDb)) { post({{"op", "group"}, {"strip", i}, {"mute", x.gMute}, {"db", x.gDb}}); b.gMute = x.gMute; b.gDb = x.gDb; }   // a DCA or MUTE GROUP moved here
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
    for (auto it = mn.cbegin(); it != mn.cend(); ++it) { const QVariant b = baseMast_.value(it.key()); if (it.value().typeId() == QMetaType::QString ? it.value().toString() != b.toString() : it.value().typeId() == QMetaType::Bool ? it.value().toBool() != b.toBool() : std::abs(it.value().toDouble() - b.toDouble()) > 0.005) diff[it.key()] = it.value(); }
    if (!diff.isEmpty()) { baseMast_ = mn; diff["op"] = "mastering"; post(diff); } }
  {   // ROUTING moved on the console: out to the service (and so to the phones)
    const QVariantList in = engine_->inputRoutes(), out = engine_->outputRoutes();
    for (int i = 0; i < in.size(); ++i) if (i >= baseIn_.size() || in[i].toInt() != baseIn_[i].toInt()) post({{"op", "inRoute"}, {"strip", i}, {"input", in[i].toInt()}});
    for (int k = 0; k < out.size() && k < 32; ++k) if (k >= baseOut_.size() || out[k].toInt() != baseOut_[k].toInt()) post({{"op", "outRoute"}, {"out", k}, {"source", out[k].toInt()}});
    baseIn_ = in; baseOut_ = out; }
  {   // DCA GROUP CHANNELS moved on the console: out to the service (and so to the phones and browsers)
    const QVariantList now = dcaSync(engine_->dcaState());
    for (int g = 0; g < now.size(); ++g) if (g >= baseDca_.size() || now[g] != baseDca_[g]) { QVariantMap c = now[g].toMap(); c["op"] = "dca"; c["group"] = g + 1; post(c); }
    baseDca_ = now; }
  if (moved(engine_->masterDb(), baseMaster_)) { baseMaster_ = engine_->masterDb(); post({{"op", "master"}, {"db", baseMaster_}}); }
  if (engine_->masterMuted() != baseMute_) { baseMute_ = engine_->masterMuted(); post({{"op", "master"}, {"mute", baseMute_}}); }   // MASTER MUTE out (the fader's own value is not re-sent with it)
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
    { const QVariantMap fb = o.value("feedback").toMap();   // the service's FEEDBACK GUARD muted a strip that howled: said here and on the status line
      if (fb.value("count").toInt() > feedbackSeen_) { feedbackSeen_ = fb.value("count").toInt(); feedbackNote_ = fb.value("text").toString().left(240); status_ = feedbackNote_; emit changed(); } }
    if (o.value("same").toBool() || !o.contains("strips")) return;
    { const quint64 rv = o.value("racksV").toULongLong(); if (rv && rv != racksV_) { racksV_ = rv; if (o.value("racksBy").toString() != QLatin1String("console")) takeRacks(); } }   // our own rack change is not taken back
    { const quint64 mv = o.value("masterV").toULongLong(); const QVariantMap pe = o.value("programEq").toMap();   // a remote's RTA EQ onto the console
      if (mv && mv != masterV_) { const bool first = masterV_ == 0; masterV_ = mv; if (!first && pe.contains("gains")) engine_->setGraphicEq(-1, pe.value("gains").toList(), pe.value("on").toBool());
        const QVariantMap ms = o.value("mastering").toMap();   // a remote's MASTERING onto the console (what it already has is not sent back)
        const QString pn = ms.value("preset").toString();   // a remote picked a MASTERING PRESET by name: the console applies its own tuned values, and the next tick sends them back out
        if (!first && !pn.isEmpty() && pn != engine_->mastering().value("preset").toString() && engine_->masteringPresets().contains(pn)) engine_->applyMasteringPreset(pn);
        else if (!first && !ms.isEmpty()) { QVariantMap in; for (auto it = ms.cbegin(); it != ms.cend(); ++it) if (it.key() != QLatin1String("reductionDb")) in[it.key()] = it.value(); engine_->setMastering(in); baseMast_ = mastNow(); } } }   // a phone changed a plug-in: the console's instance follows
    { const QVariantMap rs = o.value("settings").toMap();   // a setting changed on a remote lands on the console
      for (auto it = baseSet_.begin(); it != baseSet_.end(); ++it) { if (!rs.contains(it.key())) continue; const QVariant r = rs.value(it.key());
        const bool diff = it.value().typeId() == QMetaType::Double ? std::abs(r.toDouble() - it.value().toDouble()) > 0.005 : (it.value().typeId() == QMetaType::Bool ? r.toBool() != it.value().toBool() : r.toInt() != it.value().toInt());
        if (diff) { it.value() = it.value().typeId() == QMetaType::Double ? QVariant(r.toDouble()) : it.value().typeId() == QMetaType::Bool ? QVariant(r.toBool()) : QVariant(r.toInt()); applySetting(it.key(), it.value()); } } }
    { const QVariantMap rt = o.value("routing").toMap(); const QVariantList in = rt.value("in").toList(), out = rt.value("out").toList();   // a phone's ROUTING onto the console
      bool moved = false;
      for (int i = 0; i < in.size() && i < baseIn_.size(); ++i) if (in[i].toInt() != baseIn_[i].toInt()) { engine_->setInputRoute(i, in[i].toInt()); moved = true; }
      for (int k = 0; k < out.size() && k < baseOut_.size(); ++k) if (out[k].toInt() != baseOut_[k].toInt()) { engine_->setOutputRoute(k, out[k].toInt()); moved = true; }
      if (moved) { baseIn_ = engine_->inputRoutes(); baseOut_ = engine_->outputRoutes(); } }
    if (!baseDca_.isEmpty() && o.contains("dcas")) {   // a remote moved a DCA group strip: onto the console (members too)
      const QVariantList rd = dcaSync(o.value("dcas").toList());
      for (int g = 0; g < rd.size() && g < baseDca_.size(); ++g) { const QVariantMap r = rd[g].toMap(), b = baseDca_[g].toMap(); if (r == b) continue;
        if (std::abs(r.value("db").toDouble() - b.value("db").toDouble()) > 0.05) engine_->setDcaLevel(g + 1, r.value("db").toDouble());
        if (r.value("mute").toBool() != b.value("mute").toBool()) engine_->setDcaMute(g + 1, r.value("mute").toBool());
        if (r.value("name").toString() != b.value("name").toString() && !r.value("name").toString().isEmpty()) engine_->setDcaName(g + 1, r.value("name").toString());
        if (r.value("color").toInt() != b.value("color").toInt()) engine_->setDcaColor(g + 1, r.value("color").toInt());
        if (r.value("members") != b.value("members")) { const QVariantList was = b.value("members").toList(), want = r.value("members").toList();
          for (const QVariant& x : was) if (!want.contains(x)) engine_->setDca(x.toInt(), 0);
          for (const QVariant& x : want) engine_->setDca(x.toInt(), g + 1); } }
      baseDca_ = dcaSync(engine_->dcaState()); }
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
    { const QVariantMap mm = o.value("master").toMap();   // a remote's MASTER MUTE onto the console (the fader the console then sets goes out on the next tick)
      if (mm.contains("mute") && mm.value("mute").toBool() != baseMute_) { baseMute_ = mm.value("mute").toBool(); engine_->setMasterMuted(baseMute_); baseMaster_ = engine_->masterDb(); ++n; } }
    if (n) { syncedChanges_ += n; dApplied_ += n; emit synced(); } });   // the console's own follow-ups (a stereo link moving the partner) go out on the next tick
}

Remote::~Remote() { restore_ = false; if (running()) { proc_.terminate(); if (!proc_.waitForFinished(1500)) proc_.kill(); } }
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
void Remote::restartIfRunning() { if (!running()) return; const bool w = wifi_, back = restore_; restore_ = false; stop(); reuseTook_ = true; start(w); restore_ = back && running(); }   // the devices handed over stay the same   // the service reads its tokens when it starts
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
// The addresses phones can reach, best first: a private IPv4 address (10.x, 172.16-31.x, 192.168.x: the service's local policy answers
// these only) of a card that is up and running and has a DEFAULT GATEWAY (the router the phone is on too), Wi-Fi and Ethernet before the
// rest; then private addresses without a gateway; virtual adapters (VirtualBox, VMware, Hyper-V / WSL vEthernet, Docker, VPN tunnels)
// last: a phone on the Wi-Fi cannot reach them, and putting one first sent phones (and the QR code) to an address that never answers.
// Everything else (169.254.x: the card got no address from the router; carrier / public IPv4; IPv6) is listed apart (otherAddresses).
// The list of network cards, read at most every 3 seconds. On Windows the system call behind it can take a good part of a second
// (many adapters, a VPN), and the pages read it from several bindings at once: uncached, opening NETWORK or a burst of status lines
// from the remote service held the window ("Not Responding").
static QList<QNetworkInterface> netCards() {
  static QList<QNetworkInterface> list; static QElapsedTimer age;
  if (!age.isValid() || age.elapsed() > 3000) { list = QNetworkInterface::allInterfaces(); age.restart(); }
  return list;
}
// the cards with a default gateway (Windows: interface index; elsewhere: name); *known false where the system does not say
static QSet<QString> gatewayCards(bool* known) {
  static QSet<QString> cards; static bool have = false; static QElapsedTimer age;
  if (age.isValid() && age.elapsed() < 3000) { *known = have; return cards; }
  age.restart(); cards.clear(); have = false;
#ifdef _WIN32
  using Gaa = ULONG(WINAPI*)(ULONG, ULONG, PVOID, PIP_ADAPTER_ADDRESSES, PULONG);
  HMODULE h = GetModuleHandleW(L"iphlpapi.dll"); if (!h) h = LoadLibraryExW(L"iphlpapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (const auto gaa = h ? reinterpret_cast<Gaa>(reinterpret_cast<void*>(GetProcAddress(h, "GetAdaptersAddresses"))) : nullptr) {
    ULONG len = 32 * 1024; QByteArray buf; ULONG r = ERROR_BUFFER_OVERFLOW;
    for (int tries = 0; tries < 3 && r == ERROR_BUFFER_OVERFLOW; ++tries) { buf.resize(static_cast<int>(len)); r = gaa(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()), &len); }
    if (r == NO_ERROR) { have = true;
      for (auto* a = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()); a; a = a->Next) if (a->FirstGatewayAddress) cards << QString::number(a->IfIndex); } }
#elif defined(__linux__)
  QFile f(QStringLiteral("/proc/net/route"));   // Iface Destination Gateway ...: a destination of 00000000 is the default route
  if (f.open(QIODevice::ReadOnly)) { have = true; f.readLine();
    while (!f.atEnd()) { const QList<QByteArray> c = f.readLine().simplified().split(' '); if (c.size() > 2 && c[1] == "00000000") cards << QString::fromLatin1(c[0]); } }
#endif
  *known = have; return cards;
}
namespace {
struct Found { QString url, card; int tier; };
QList<Found> findAddresses(int port, QList<Found>* others) {
  QList<Found> best;
  static const QStringList virtualNames{"virtualbox", "vmware", "vethernet", "hyper-v", "wsl", "docker", "vbox", "tap-", "tap0", "tun", "vpn", "zerotier", "tailscale", "hamachi", "loopback", "bluetooth", "wireguard", "openvpn", "npcap", "teredo", "isatap"};
  bool gwKnown = false; const QSet<QString> gw = gatewayCards(&gwKnown);
  for (const QNetworkInterface& ni : netCards()) {
    const auto f = ni.flags(); if (!(f & QNetworkInterface::IsUp) || !(f & QNetworkInterface::IsRunning) || (f & QNetworkInterface::IsLoopBack)) continue;
    const QString nm = (ni.humanReadableName() + " " + ni.name()).toLower(); bool isVirt = ni.type() == QNetworkInterface::Virtual;
    for (const QString& v : virtualNames) if (nm.contains(v)) isVirt = true;
#ifdef _WIN32
    const bool hasGw = !gwKnown || gw.contains(QString::number(ni.index()));
#else
    const bool hasGw = !gwKnown || gw.contains(ni.name());
#endif
    const bool real = ni.type() == QNetworkInterface::Wifi || ni.type() == QNetworkInterface::Ieee80211 || ni.type() == QNetworkInterface::Ethernet;
    for (const QNetworkAddressEntry& e : ni.addressEntries()) { const QHostAddress a = e.ip();
      if (a.protocol() == QAbstractSocket::IPv6Protocol) { if (others && !a.isLinkLocal() && !a.isLoopback()) *others << Found{QStringLiteral("http://[%1]:%2").arg(a.toString()).arg(port), ni.humanReadableName(), 9}; continue; }
      if (a.protocol() != QAbstractSocket::IPv4Protocol) continue;
      const quint32 ip = a.toIPv4Address(); const unsigned x = ip >> 24, y = (ip >> 16) & 255; const QString u = QStringLiteral("http://%1:%2").arg(a.toString()).arg(port);
      if (!(x == 10 || (x == 172 && y >= 16 && y <= 31) || (x == 192 && y == 168))) { if (others && x != 127) *others << Found{u, ni.humanReadableName(), 8}; continue; }
      const bool virt = isVirt || (x == 192 && y == 168 && ((ip >> 8) & 255) == 56);   // 192.168.56.x: the VirtualBox host-only network
      best << Found{u, ni.humanReadableName(), virt ? 3 : (hasGw && real) ? 0 : hasGw ? 1 : 2}; } }
  std::stable_sort(best.begin(), best.end(), [](const Found& a, const Found& b) { return a.tier < b.tier; });
  return best;
}
}  // namespace
QStringList Remote::addresses() const {
  QStringList l; for (const Found& f : findAddresses(port(), nullptr)) if (!l.contains(f.url)) { l << f.url; adapters_[f.url] = f.card; }
  return l;
}
QStringList Remote::otherAddresses() const {
  QList<Found> o; findAddresses(port(), &o); QStringList l; for (const Found& f : o) if (!l.contains(f.url)) { l << f.url; adapters_[f.url] = f.card; }
  return l;
}
QString Remote::adapterName(const QString& address) const { if (!adapters_.contains(address)) { (void)addresses(); (void)otherAddresses(); } return adapters_.value(address); }
QString Remote::qrImage(const QString& text) const {
  const QVariantList m = qr(text); if (m.size() < 2) return {};
  const int n = m[0].toInt(), px = 8, side = (n + 8) * px; QImage img(side, side, QImage::Format_Grayscale8); img.fill(255);
  for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) if (m[1 + y * n + x].toInt()) for (int dy = 0; dy < px; ++dy) { uchar* row = img.scanLine((y + 4) * px + dy); std::memset(row + (x + 4) * px, 0, px); }
  QByteArray png; QBuffer b(&png); b.open(QIODevice::WriteOnly); if (!img.save(&b, "PNG")) return {};
  return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
}
// what the console plays now: its backend, the output by id, the input it reads (or "-": none), rate, buffer and channels. The output id
// is the remembered device's when that is the device playing (the engine names it), else the device of that name on that backend.
Remote::Takeover Remote::takeover() const {
  Takeover t; const QVariantMap last = QSettings().value(QStringLiteral("audio/lastDevice")).toMap();
  t.running = engine_->running();
  if (!t.running) {   // the engine is off: the remembered output, and no input (nothing was open, so nothing is captured)
    t.backend = last.value("backend").toString(); t.out = last.value("out").toString(); t.rate = last.value("rate").toDouble(); t.frames = last.value("frames").toInt();
    t.outChannels = last.value("outChannels").toInt(); t.in = QStringLiteral("-"); t.inChannels = 0; return t; }
  t.backend = engine_->backend(); t.rate = engine_->sampleRate(); t.frames = engine_->blockFrames(); t.outChannels = engine_->deviceOutputs();
  const bool none = engine_->inputSource() == QLatin1String("none") || engine_->deviceInputs() <= 0;
  t.in = none ? QStringLiteral("-") : QString::fromStdString(engine_->currentInputId()); t.inChannels = none ? 0 : engine_->deviceInputs();
  const QString outName = engine_->outputDeviceName(); QString byName, lastOut = last.value("backend").toString() == t.backend ? last.value("out").toString() : QString(); bool lastIsIt = false;
  for (const QVariant& v : engine_->devices()) { const QVariantMap d = v.toMap(); if (d.value("backend").toString() != t.backend || d.value("outChannels").toInt() <= 0) continue;
    if (!lastOut.isEmpty() && d.value("id").toString() == lastOut && (outName.isEmpty() || d.value("name").toString() == outName)) lastIsIt = true;
    if (byName.isEmpty() && !outName.isEmpty() && d.value("name").toString() == outName) byName = d.value("id").toString(); }
  t.out = lastIsIt ? lastOut : !byName.isEmpty() ? byName : lastOut;
  return t;
}
// the service has let the sound card go: the console reopens exactly what it had handed over (main's startLastDevice is the fallback)
void Remote::giveBack() {
  if (!restore_ || running()) return; restore_ = false;
  const Takeover& t = took_; if (!t.running || engine_->running()) return;
  const bool in = t.inChannels > 0 && t.in != QLatin1String("-");
  engine_->open(t.backend, in ? t.in : QStringLiteral("-"), t.out, t.rate, t.frames, in ? t.inChannels : 0, t.outChannels);
}
bool Remote::start(bool wifi) {
  if (running()) return true;
  if (!available()) { status_ = QStringLiteral("the remote service program is not installed next to the console"); emit changed(); return false; }
  writeConsoleInfo();   // what the phone shows under PLUG-INS: the catalogue, the racks and the extensions, as they are now
  writeSeed();          // LIVE SYNC: the phones start where the console is (faders, mutes, DCA groups: a muted microphone stays muted)
  if (!reuseTook_) took_ = takeover(); reuseTook_ = false;   // THE SOUND CARD HANDED OVER: the service opens what the console plays now
  engine_->stop();   // the service opens the sound card itself
  restore_ = took_.running;   // when the service stops (or does not start), the console takes its devices back
  wifi_ = wifi;
  QStringList args{"--port", QString::number(port()), "--version", QCoreApplication::applicationVersion(),
    "--strips", QString::number(qBound(1, engine_->licensedChannels(), 32)), "--plan", engine_->license().value("planName").toString()};   // the phone gets the channels of the licence plan, no more
  args << openArgs(took_);   // the console's backend, output, input ("-" when it had none: never the default microphone), rate, buffer
  args << "--seed" << QString::fromStdString(am::paths::file("remote-seed.json"));
  if (wifi) args << "--allow-lan";   // phones on this network may connect (local policy stays on: private addresses only). This sat inside the comment above and was never passed: every phone was refused
  if (ext_.available() && !QSettings().value("store/off").toStringList().contains(QStringLiteral("ext:AmF X1 Network"))) {   // the extension runs inside the console (unless switched off in the LOCAL STORE): same process, same rights on the sound card
    const int rc = ext_.start(args);
    if (rc == 0 && ext_.running()) { sync_.start(); linkTimer_.start(); status_ = wifi ? QStringLiteral("starting for the phones on this network (extension)") : QStringLiteral("starting for this computer only (extension)"); emit changed(); return true; }
    status_ = QStringLiteral("the network extension did not start (%1): %2").arg(rc).arg(ext_.takeOutput().trimmed().section('\n', -1).left(160)); emit changed();   // then the program next to it, if there is one
  }
  proc_.start(program(), args); if (!proc_.waitForStarted(3000)) { if (status_.isEmpty() || status_ == "off") status_ = QStringLiteral("the remote service did not start"); giveBack(); emit changed(); return false; }
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

void Remote::streamingSync(const QVariantMap& now, std::function<void(const QStringList&)> onCmds) {
  if (!running()) return; ensureToken(); if (token_.isEmpty()) return;
  QNetworkReply* r = net_.post(api(QStringLiteral("/api/streaming/state")), QJsonDocument(QJsonObject::fromVariantMap(now)).toJson(QJsonDocument::Compact));
  connect(r, &QNetworkReply::finished, this, [r, onCmds] { r->deleteLater(); if (r->error() != QNetworkReply::NoError) return;
    QStringList l; for (const QJsonValue& v : QJsonDocument::fromJson(r->readAll()).object().value("cmds").toArray()) l << v.toString(); if (!l.isEmpty() && onCmds) onCmds(l); });
}
