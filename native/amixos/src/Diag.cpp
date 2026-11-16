// see Diag.h
#include "Diag.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>
#include <QUrl>
#include <cmath>
#include <thread>

#include "audiomixer/driver_intel.hpp"
#include "audiomixer/runtime.hpp"

#ifndef AMIX_VERSION
#define AMIX_VERSION "dev"
#endif

Diag::Diag(QObject* parent) : QObject(parent) {
  stopTimer_.setSingleShot(true); connect(&stopTimer_, &QTimer::timeout, this, [this] { stop(); });
  meterTimer_.setInterval(50);
  connect(&meterTimer_, &QTimer::timeout, this, [this] { if (!rt_) return; const am::Snapshot s = rt_->snapshot(); double pk = 0;
    for (int i = 0; i < 8; ++i) pk = std::max(pk, s.strips[i].inPk); level_ = pk > 1e-6 ? 20.0 * std::log10(pk) : -120.0; emit levelChanged(); });
}
Diag::~Diag() { stop(); }

QVariantMap Diag::system() const {
  return {{"app", QStringLiteral(AMIX_VERSION)}, {"os", QSysInfo::prettyProductName()}, {"kernel", QSysInfo::kernelVersion()}, {"cpu", QSysInfo::currentCpuArchitecture()},
          {"cores", QThread::idealThreadCount()}, {"qt", QString::fromLatin1(qVersion())}, {"backends", [] { QStringList l; for (const std::string& b : am::availableBackends()) l << QString::fromStdString(b); return l.join(", "); }()}};
}

// the endpoints first (fast, never blocks), then the drivers: an ASIO driver is loaded to be read, which a broken one can stall, so it runs
// on its own thread and the window stays live; a result comes back to the UI thread
void Diag::refresh() {
  if (busy_) return; busy_ = true; msg_ = QStringLiteral("Reading the audio devices..."); emit changed();
  QPointer<Diag> self(this);
  std::thread([self] {
    auto eps = std::make_shared<std::vector<am::EndpointInfo>>(am::listEndpoints());
    auto devs = std::make_shared<std::vector<am::DeviceInfo>>(am::listDevices(true));
    QMetaObject::invokeMethod(QCoreApplication::instance(), [self, eps, devs] {
      if (!self) return; Diag* d = self.data(); d->outs_.clear(); d->ins_.clear(); d->asio_.clear();
      for (const am::EndpointInfo& e : *eps) {
        QVariantMap m{{"id", QString::fromStdString(e.id)}, {"name", QString::fromStdString(e.name)}, {"kind", QString::fromStdString(e.kind)}, {"state", QString::fromStdString(e.state)},
                      {"channels", e.channels}, {"rate", e.rate}, {"isDefault", e.isDefault}};
        (e.flow == "out" ? d->outs_ : d->ins_) << m; }
      for (const am::DeviceInfo& v : *devs) { if (v.backend != "asio") continue; const am::drv::AsioIdentity a = am::drv::identifyAsio(v.name);
        const QString err = QString::fromStdString(v.probeError);
        const QString st = a.own || a.type == "virtual" || a.type == "network" ? "virtual" : err.contains("bit") ? "other bitness" : !err.isEmpty() ? "busy" : v.inChannels + v.outChannels > 0 ? "ready" : "no channels";
        d->asio_ << QVariantMap{{"id", QString::fromStdString(v.id)}, {"name", QString::fromStdString(v.name)}, {"maker", QString::fromStdString(a.maker)}, {"type", QString::fromStdString(a.type)},
                               {"status", st}, {"in", v.inChannels}, {"out", v.outChannels}, {"note", err.isEmpty() ? QString::fromStdString(a.note) : err}}; }
      d->runChecks(); d->busy_ = false; d->msg_ = QStringLiteral("%1 outputs, %2 inputs, %3 ASIO drivers read.").arg(d->outs_.size()).arg(d->ins_.size()).arg(d->asio_.size()); emit d->changed(); }, Qt::QueuedConnection);
  }).detach();
}

void Diag::loadDemo() {
  auto ep = [](const char* id, const char* n, const char* k, const char* st, int ch, bool def) { return QVariant(QVariantMap{{"id", id}, {"name", n}, {"kind", k}, {"state", st}, {"channels", ch}, {"rate", ch ? 48000.0 : 0.0}, {"isDefault", def}}); };
  outs_ = {ep("{o1}", "Speakers (Realtek(R) Audio)", "speakers", "active", 2, true), ep("{o2}", "Headphones (Realtek(R) Audio)", "headphones", "unplugged", 0, false),
           ep("{o3}", "Headphones (USB Audio CODEC)", "headphones", "active", 2, false), ep("{o4}", "Digital Output (SPDIF)", "digital", "disabled", 0, false)};
  ins_ = {ep("{i1}", "Microphone Array (Realtek(R) Audio)", "microphone", "active", 2, true), ep("{i2}", "Microphone (USB Audio CODEC)", "microphone", "active", 1, false), ep("{i3}", "Line In (Realtek(R) Audio)", "line", "unplugged", 0, false)};
  asio_ = {QVariantMap{{"id", "Focusrite USB ASIO"}, {"name", "Focusrite USB ASIO"}, {"maker", "Focusrite"}, {"type", "interface"}, {"status", "busy"}, {"in", 0}, {"out", 0}, {"note", "did not start: the device is in use by another program"}},
           QVariantMap{{"id", "Audio Mixer"}, {"name", "Audio Mixer"}, {"maker", "AMFB Audio"}, {"type", "virtual"}, {"status", "virtual"}, {"in", 2}, {"out", 2}, {"note", "AmF X1's own virtual driver"}}};
  runChecks(); msg_ = QStringLiteral("Demo devices (screenshot)."); emit changed();
}
void Diag::runChecks() {
  checks_.clear();
  auto add = [this](const QString& n, const QString& lvl, const QString& det, const QString& fix = QString()) { checks_ << QVariantMap{{"name", n}, {"level", lvl}, {"detail", det}, {"fix", fix}}; };
  auto find = [](const QVariantList& l, auto pred) { QVariantList r; for (const QVariant& v : l) if (pred(v.toMap())) r << v; return r; };
  const QVariantMap sys = system();
  add("Audio engine", "ok", QStringLiteral("AmF X1 %1 · backends: %2").arg(sys["app"].toString(), sys["backends"].toString()));
  const QVariantList defOut = find(outs_, [](const QVariantMap& m) { return m["isDefault"].toBool() && m["state"] == "active"; });
  const QVariantList defIn = find(ins_, [](const QVariantMap& m) { return m["isDefault"].toBool() && m["state"] == "active"; });
  const QVariantList activeOut = find(outs_, [](const QVariantMap& m) { return m["state"] == "active"; });
  const QVariantList activeIn = find(ins_, [](const QVariantMap& m) { return m["state"] == "active"; });
  if (!defOut.isEmpty()) add("Playback (default)", "ok", defOut[0].toMap()["name"].toString() + QStringLiteral(" · %1 ch · %2 Hz").arg(defOut[0].toMap()["channels"].toInt()).arg(defOut[0].toMap()["rate"].toDouble()));
  else if (!activeOut.isEmpty()) add("Playback (default)", "warn", "no default output is set", "Sound settings: pick an output as default");
  else add("Playback (default)", "fail", "no output is active: nothing can be heard", "Plug in speakers or headphones, or enable an output in Sound settings");
  if (!defIn.isEmpty()) add("Recording (default)", "ok", defIn[0].toMap()["name"].toString() + QStringLiteral(" · %1 ch").arg(defIn[0].toMap()["channels"].toInt()));
  else add("Recording (default)", activeIn.isEmpty() ? "warn" : "warn", activeIn.isEmpty() ? "no input is active (no microphone)" : "no default input is set", "Plug in a microphone or enable one in Sound settings");
  for (const QString& kind : {QStringLiteral("headphones"), QStringLiteral("speakers")}) {
    const QString title = kind == "headphones" ? "Headphones" : "Speakers";
    // speakers include the ones on a cable from the sound card: S/PDIF (optical / coaxial), HDMI to a soundbar or receiver, and line out to powered speakers
    const QVariantList all = find(outs_, [&](const QVariantMap& m) { return m["kind"] == kind || (kind == "headphones" && m["kind"] == "headset") || (kind == "speakers" && (m["kind"] == "digital" || m["kind"] == "line")); });
    const QVariantList on = find(all, [](const QVariantMap& m) { return m["state"] == "active"; }), off = find(all, [](const QVariantMap& m) { return m["state"] == "unplugged"; });
    const auto via = [](const QVariantMap& m) { return m["kind"] == "digital" ? QStringLiteral(" · on the digital cable (S/PDIF / HDMI)") : m["kind"] == "line" ? QStringLiteral(" · on the line output") : QString(); };
    if (!on.isEmpty()) add(title, "ok", on[0].toMap()["name"].toString() + via(on[0].toMap()) + (on.size() > 1 ? QStringLiteral(" (+%1 more)").arg(on.size() - 1) : QString()) + " · ready: press TEST to hear it");
    else if (!off.isEmpty()) { const QVariantMap o = off[0].toMap(); const bool dig = o["kind"] == "digital";
      add(title, "warn", o["name"].toString() + (dig ? ": the TV, amplifier or soundbar on this cable is off or not connected" : ": nothing plugged into the jack"),
          kind == "headphones" ? "Plug the headphones in; the console sees them at once" : dig ? "Switch the TV / amplifier / soundbar on and select this input on it; check the optical, coaxial or HDMI cable" : "Plug the speakers in"); }
    else add(title, "ok", kind == "headphones" ? "none found (fine when you listen on speakers; a headphone jack shared with the speakers shows as Speakers)" : "none found by that name (the default output may still be speakers)");
  }
  const QVariantList dis = find(outs_ + ins_, [](const QVariantMap& m) { return m["state"] == "disabled"; });
  if (!dis.isEmpty()) { QStringList n; for (const QVariant& v : dis) n << v.toMap()["name"].toString(); add("Switched-off devices", "warn", n.join(", "), "Sound settings → right-click → Enable, if you want to use them"); }
  else add("Switched-off devices", "ok", "none");
  if (!defOut.isEmpty()) { const double r = defOut[0].toMap()["rate"].toDouble(); add("Sample rate", r == 44100 || r == 48000 || r == 96000 ? "ok" : "warn", QStringLiteral("%1 Hz on the default output").arg(r), r == 44100 || r == 48000 || r == 96000 ? QString() : "48000 Hz is the safest choice (Sound settings → device → Advanced)"); }
  int busyN = 0, bitN = 0, readyN = 0; bool own = false;
  for (const QVariant& v : asio_) { const QVariantMap m = v.toMap(); const QString s = m["status"].toString(); busyN += s == "busy"; bitN += s == "other bitness"; readyN += s == "ready"; own = own || (m["type"] == "virtual" && m["name"].toString().compare("Audio Mixer", Qt::CaseInsensitive) == 0); }
  if (asio_.isEmpty()) add("ASIO drivers", "ok", "none installed: WASAPI is used (fine for speakers, headphones and USB microphones)");
  else add("ASIO drivers", busyN + bitN ? "warn" : "ok", QStringLiteral("%1 found · %2 ready · %3 busy · %4 other bitness").arg(asio_.size()).arg(readyN).arg(busyN).arg(bitN), busyN ? "Close the other audio program that holds the driver" : bitN ? "Use the console build that matches the driver (32 / 64 bit)" : QString());
  add("AmF X1 ASIO driver", "ok", own ? "installed: other programs can reach the console" : "not installed (optional: only for sending the console into another program)");
  const QString dir = QCoreApplication::applicationDirPath();
  const bool console = QFileInfo::exists(dir + "/amixos.exe") || QFileInfo::exists(dir + "/amixos");
  add("Console program", console ? "ok" : "warn", console ? "found next to this program" : "not found next to this program", console ? QString() : "Reinstall AmF X1, or run Diagnostics from its install folder");
  const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation); QDir().mkpath(data); QFile t(data + "/.diag-write-test");
  const bool w = t.open(QIODevice::WriteOnly) && t.write("ok", 2) == 2; t.close(); t.remove();
  add("Data folder", w ? "ok" : "fail", data, w ? QString() : "The folder is read-only: check its permissions or free disk space");
}

bool Diag::openOn(const QString& in, const QString& out, int inCh, int outCh) {
  stop(); rt_ = std::make_unique<am::Runtime>(8);
  am::DeviceConfig cfg; cfg.backend = backend_.isEmpty() ? QStringLiteral("auto").toStdString() : backend_.toStdString();
#ifdef _WIN32
  if (backend_.isEmpty()) cfg.backend = "wasapi";   // every endpoint here is a WASAPI one: speakers, headphones and microphones alike
#endif
  cfg.inDevice = in.toStdString(); cfg.outDevice = out.toStdString(); cfg.inChannels = inCh; cfg.outChannels = outCh; cfg.sampleRate = 48000; cfg.frames = 480;
  std::string err; if (!rt_->open(cfg, err) || !rt_->start(err)) { msg_ = QStringLiteral("Could not open it: ") + QString::fromStdString(err); rt_.reset(); emit changed(); return false; }
  return true;
}
bool Diag::testOutput(const QString& id) {
  QVariantMap e; for (const QVariant& v : outs_) if (v.toMap()["id"] == id) e = v.toMap();
  if (!e.isEmpty() && e["state"] != "active") { msg_ = e["name"].toString() + (e["state"] == "unplugged" ? ": nothing is plugged in." : ": switched off in Sound settings."); emit changed(); return false; }
  if (!openOn(QStringLiteral("-"), id, 0, std::max(2, std::min(8, e.value("channels", 2).toInt())))) return false;
  rt_->testTone(2.0); testing_ = id; msg_ = QStringLiteral("Playing a 1 kHz tone on ") + e.value("name", id).toString() + QStringLiteral(" (left and right, -20 dBFS, 2 s)."); stopTimer_.start(2400); emit changed(); return true;
}
bool Diag::listen(const QString& id) {
  if (id.isEmpty()) { stop(); return true; }
  QVariantMap e; for (const QVariant& v : ins_) if (v.toMap()["id"] == id) e = v.toMap();
  if (!e.isEmpty() && e["state"] != "active") { msg_ = e["name"].toString() + (e["state"] == "unplugged" ? ": nothing is plugged in." : ": switched off in Sound settings."); emit changed(); return false; }
  if (!openOn(id, QStringLiteral("-"), std::max(1, std::min(8, e.value("channels", 2).toInt())), 0)) return false;
  testing_ = id; level_ = -120; meterTimer_.start(); stopTimer_.start(8000); msg_ = QStringLiteral("Listening to ") + e.value("name", id).toString() + QStringLiteral(": speak or play for a few seconds."); emit changed(); return true;
}
void Diag::stop() { stopTimer_.stop(); meterTimer_.stop(); if (rt_) { rt_->stop(); rt_.reset(); } if (!testing_.isEmpty()) { testing_.clear(); emit changed(); } }

QString Diag::report() const {
  QString r; const QVariantMap s = system();
  r += QStringLiteral("AmF X1 Diagnostics report · %1\n").arg(QDateTime::currentDateTime().toString(Qt::ISODate));
  r += QStringLiteral("AmF X1 %1 · %2 (%3) · %4 cores · Qt %5 · backends: %6\n\n").arg(s["app"].toString(), s["os"].toString(), s["cpu"].toString()).arg(s["cores"].toInt()).arg(s["qt"].toString(), s["backends"].toString());
  r += "CHECKS\n"; for (const QVariant& v : checks_) { const QVariantMap m = v.toMap(); r += QStringLiteral("  [%1] %2: %3%4\n").arg(m["level"].toString().toUpper(), m["name"].toString(), m["detail"].toString(), m["fix"].toString().isEmpty() ? QString() : " → " + m["fix"].toString()); }
  for (const auto& [title, list] : {std::pair<const char*, const QVariantList*>{"\nOUTPUTS (speakers, headphones ...)\n", &outs_}, {"\nINPUTS (microphones, line ...)\n", &ins_}}) {
    r += title; for (const QVariant& v : *list) { const QVariantMap m = v.toMap(); r += QStringLiteral("  %1%2 · %3 · %4%5\n").arg(m["isDefault"].toBool() ? "* " : "  ", m["name"].toString(), m["kind"].toString(), m["state"].toString(),
        m["channels"].toInt() ? QStringLiteral(" · %1 ch · %2 Hz").arg(m["channels"].toInt()).arg(m["rate"].toDouble()) : QString()); } }
  r += "\nASIO DRIVERS\n"; for (const QVariant& v : asio_) { const QVariantMap m = v.toMap(); r += QStringLiteral("  %1 · %2 · %3 · %4 in / %5 out · %6\n").arg(m["name"].toString(), m["type"].toString(), m["status"].toString()).arg(m["in"].toInt()).arg(m["out"].toInt()).arg(m["note"].toString()); }
  if (asio_.isEmpty()) r += "  none\n";
  return r;
}
QString Diag::saveReport(const QString& path) {
  QString p = path; if (p.isEmpty()) { const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation); QDir().mkpath(docs); p = docs + QStringLiteral("/AmF X1 Diagnostics %1.txt").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH-mm-ss")); }
  QFile f(p); if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { msg_ = "Could not write " + p; emit changed(); return {}; }
  f.write(report().toUtf8()); msg_ = "Report saved: " + p; emit changed(); return p;
}
void Diag::copyReport() const { if (QGuiApplication::clipboard()) QGuiApplication::clipboard()->setText(report()); }
void Diag::openSoundSettings() const {
#ifdef _WIN32
  QProcess::startDetached(QStringLiteral("control.exe"), {QStringLiteral("mmsys.cpl")});
#else
  QDesktopServices::openUrl(QUrl(QStringLiteral("settings://sound")));
#endif
}
bool Diag::openConsole() const {
  const QString dir = QCoreApplication::applicationDirPath();
  for (const QString& n : {QStringLiteral("amixos.exe"), QStringLiteral("amixos")}) if (QFileInfo::exists(dir + "/" + n)) return QProcess::startDetached(dir + "/" + n, {});
  return false;
}
