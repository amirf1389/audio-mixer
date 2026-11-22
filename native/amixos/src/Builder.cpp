#include "Builder.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <QMetaObject>
#include "fx/fx.hpp"
#include "paths/paths.hpp"
#include "plugin_shell/design.hpp"
#include "plugin_shell/preview.hpp"
#include "samples/samples.hpp"
#include <QUrl>
#include <QFileInfo>
#include <cmath>

Builder::Builder(QObject* engine, QObject* parent) : QObject(parent), engine_(engine) {
  newDesign(); connect(this, &Builder::changed, this, &Builder::pushDesign);   // every edit reaches a running preview at once
  meters_.setInterval(50); connect(&meters_, &QTimer::timeout, this, [this] { if (!pv_) return; auto db = [](float v) { return v > 1e-6f ? 20.0 * std::log10(v) : -120.0; };
    const double i = db(pv_->inPeak()), o = db(pv_->outPeak()); inDb_ = std::max(i, inDb_ - 2.0); outDb_ = std::max(o, outDb_ - 2.0); emit metersChanged(); });
}
Builder::~Builder() = default;
void Builder::enablePreview(const QString& backend) { pv_ = std::make_unique<am::shell::Preview>(); pvBackend_ = backend; pushDesign(); }
bool Builder::previewOn() const { return pv_ && pv_->running(); }
QVariantList Builder::samples() const { QVariantList l; for (const auto& s : am::samples::list()) { QVariantMap m; m["id"] = s.id; m["name"] = QString::fromStdString(s.name); m["category"] = QString::fromStdString(s.category); l << m; } return l; }
void Builder::pushDesign() {
  if (!pv_) return; am::shell::Design d; std::string err;
  if (blocks_.isEmpty()) { d.name = "x"; d.uid = uid_.toStdString(); pv_->setDesign(d); return; }   // no blocks: the source as it is
  if (am::shell::parse(designJson().toStdString(), d, err)) pv_->setDesign(d);
}
bool Builder::previewPlay(bool on) {
  if (!pv_) { pvNote_ = tr("Preview runs in the Plug-in Builder program."); emit previewChanged(); return false; }
  if (!on) { pv_->stop(); meters_.stop(); inDb_ = outDb_ = -120; pvNote_.clear(); emit previewChanged(); emit metersChanged(); return true; }
  std::string err; pushDesign(); const bool ok = pv_->start(err, pvBackend_.toStdString(), 48000, 512);
  pvNote_ = ok ? tr("Playing through the design: move the knobs, A/B with BYPASS") : tr("The sound card did not open: %1").arg(QString::fromStdString(err));
  if (ok) meters_.start(); emit previewChanged(); return ok;
}
void Builder::setPreviewSource(int s) {
  if (s < 0 || s > 4) return; const bool micChange = (s == 3) != (pvSource_ == 3); pvSource_ = s;
  if (pv_) { pv_->setSource(static_cast<am::shell::Preview::Source>(s)); if (s == 0) pv_->setSample(pvSample_);
    if (pv_->running() && micChange) { previewPlay(false); previewPlay(true); } }   // the microphone needs the sound card's input opened (or closed again)
  emit previewChanged();
}
void Builder::setPreviewSample(int id) { pvSample_ = id; pvSource_ = 0; if (pv_) pv_->setSample(id); emit previewChanged(); }
bool Builder::setPreviewFile(const QString& p) {
  const QString path = p.startsWith("file:") ? QUrl(p).toLocalFile() : p; pvFile_ = path;
  if (!pv_) return false; std::string err; const bool ok = pv_->setFile(path.toStdString(), err); if (ok) pvSource_ = 4;
  pvNote_ = ok ? tr("Playing %1 on a loop").arg(QFileInfo(path).fileName()) : tr("Could not read the file: %1").arg(QString::fromStdString(err)); emit previewChanged(); return ok;
}
void Builder::setPreviewBypass(bool b) { pvBypass_ = b; if (pv_) pv_->setBypass(b); emit previewChanged(); }
void Builder::setPreviewGainDb(double db) { pvGain_ = qBound(-30.0, db, 6.0); if (pv_) pv_->setGainDb(pvGain_); emit previewChanged(); }

QVariantList Builder::catalog() const {
  QVariantList l; for (const auto& [k, n] : am::fx::catalog()) { if (k == "sampler" || k == "iamfrender" || k == "logic" || k == "dyneq") continue;   // need the console around them (the dynamic EQ: its own editor)
    QVariantMap m; m["kind"] = QString::fromStdString(k); m["name"] = QString::fromStdString(n); l << m; }
  return l;
}
void Builder::newDesign() {
  blocks_.clear(); name_ = QStringLiteral("My Plugin"); vendor_ = QStringLiteral("AMFB Audio"); category_ = QStringLiteral("Fx"); color_ = QStringLiteral("#EA580C");
  uid_ = QString::fromStdString(am::shell::newUid("new")); file_.clear(); note_.clear(); emit changed();
}
void Builder::addBlock(const QString& kind) {
  if (blocks_.size() >= am::shell::kMaxBlocks) { note_ = tr("A plug-in holds 6 blocks at most."); emit changed(); return; }
  auto e = am::fx::make(kind.toStdString(), 48000); if (!e) return;
  Blk b; b.kind = kind; b.name = QString::fromLatin1(e->name());
  for (const auto& p : e->params()) { Knob k; k.id = QString::fromStdString(p.id); k.title = QString::fromStdString(p.title); k.label = k.title; k.value = p.value; b.knobs << k; }
  blocks_ << b; note_.clear(); emit changed();
}
void Builder::removeBlock(int i) { if (i < 0 || i >= blocks_.size()) return; blocks_.removeAt(i); emit changed(); }
void Builder::moveBlock(int i, int by) { const int j = i + by; if (i < 0 || j < 0 || i >= blocks_.size() || j >= blocks_.size()) return; blocks_.swapItemsAt(i, j); emit changed(); }
void Builder::setKnob(int b, const QString& id, double v) { if (b < 0 || b >= blocks_.size()) return; for (auto& k : blocks_[b].knobs) if (k.id == id) k.value = qBound(0.0, v, 1.0); emit changed(); }
void Builder::exposeKnob(int b, const QString& id, bool on) {
  if (b < 0 || b >= blocks_.size()) return;
  int n = 0; for (const auto& bl : blocks_) for (const auto& k : bl.knobs) n += k.exposed;
  if (on && n >= am::shell::kMaxParams) { note_ = tr("16 knobs go to the DAW at most: fix one of the others first."); emit changed(); return; }
  for (auto& k : blocks_[b].knobs) if (k.id == id) k.exposed = on; note_.clear(); emit changed();
}
void Builder::setKnobLabel(int b, const QString& id, const QString& l) { if (b < 0 || b >= blocks_.size()) return; for (auto& k : blocks_[b].knobs) if (k.id == id) { QString s; for (QChar c : l) if (c.unicode() >= 32 && c.unicode() < 127) s += c; k.label = s.left(24); } emit changed(); }
void Builder::placeKnob(int b, const QString& id, double x, double y) { if (b < 0 || b >= blocks_.size()) return; for (auto& k : blocks_[b].knobs) if (k.id == id) { k.x = qBound(0.0, x, 1.0); k.y = qBound(0.0, y, 1.0); } emit changed(); }

QVariantList Builder::blocks() const {
  QVariantList l;
  for (int b = 0; b < blocks_.size(); ++b) { const Blk& bl = blocks_[b]; QVariantMap m; m["kind"] = bl.kind; m["name"] = bl.name; QVariantList ks;
    auto e = am::fx::make(bl.kind.toStdString(), 48000);
    for (const auto& k : bl.knobs) { QVariantMap x; x["id"] = k.id; x["title"] = k.title; x["label"] = k.label; x["value"] = k.value; x["exposed"] = k.exposed; x["x"] = k.x; x["y"] = k.y;
      x["text"] = e ? QString::fromStdString(e->text(k.id.toStdString(), k.value)) : QString::number(k.value, 'f', 2); ks << x; }
    m["knobs"] = ks; l << m; }
  return l;
}
QVariantList Builder::exposed() const {
  QVariantList l; int n = 0;
  for (int b = 0; b < blocks_.size(); ++b) { auto e = am::fx::make(blocks_[b].kind.toStdString(), 48000);
    for (const auto& k : blocks_[b].knobs) { if (!k.exposed) continue; QVariantMap x; x["block"] = b; x["id"] = k.id; x["label"] = k.label; x["value"] = k.value;
      x["text"] = e ? QString::fromStdString(e->text(k.id.toStdString(), k.value)) : QString(); x["x"] = k.x >= 0 ? k.x : 0.08 + 0.21 * (n % 5); x["y"] = k.y >= 0 ? k.y : 0.22 + 0.42 * (n / 5 % 2); l << x; ++n; } }
  return l;
}

QString Builder::designJson() const {
  am::shell::Design d; d.name = am::shell::safeName(name_.toStdString()); d.vendor = vendor_.isEmpty() ? "AMFB Audio" : vendor_.toStdString(); d.uid = uid_.toStdString(); d.category = category_.toStdString();
  QJsonArray layout; int n = 0;
  for (int b = 0; b < blocks_.size(); ++b) { am::shell::Block bl; bl.kind = blocks_[b].kind.toStdString();
    for (const auto& k : blocks_[b].knobs) {
      if (k.exposed) { am::shell::Param p; p.title = k.label.isEmpty() ? k.title.toStdString() : k.label.toStdString(); p.block = b; p.id = k.id.toStdString(); p.def = k.value; d.params.push_back(p);
        const QVariantMap pos = exposed().value(n).toMap(); layout << QJsonObject{{"param", n}, {"x", pos.value("x").toDouble()}, {"y", pos.value("y").toDouble()}, {"kind", "knob"}}; ++n; }
      else bl.set.emplace_back(k.id.toStdString(), k.value); }
    d.blocks.push_back(bl); }
  d.panelJson = QJsonDocument(QJsonObject{{"color", color_}, {"layout", layout}}).toJson(QJsonDocument::Compact).toStdString();
  return QString::fromStdString(am::shell::toJson(d));
}

QString Builder::folder() const { const QString f = QString::fromStdString(am::paths::dataDir()) + "/plugin-designs"; QDir().mkpath(f); return f; }
QVariantList Builder::designs() const {
  QVariantList l;
  for (const QFileInfo& fi : QDir(folder()).entryInfoList({"*.json"}, QDir::Files, QDir::Time)) {
    QFile f(fi.absoluteFilePath()); if (!f.open(QIODevice::ReadOnly)) continue; const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    QVariantMap m; m["name"] = o.value("name").toString(); m["file"] = fi.absoluteFilePath(); m["blocks"] = o.value("blocks").toArray().size(); m["knobs"] = o.value("params").toArray().size(); m["when"] = fi.lastModified().toString("yyyy-MM-dd HH:mm"); l << m; }
  return l;
}
bool Builder::save() {
  if (blocks_.isEmpty()) { note_ = tr("Add at least one block first."); emit changed(); return false; }
  const QString file = folder() + "/" + QString::fromStdString(am::shell::safeName(name_.toStdString())) + ".json";
  QFile f(file); if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { note_ = tr("Could not save the design."); emit changed(); return false; }
  f.write(designJson().toUtf8()); file_ = file; note_ = tr("Saved: %1").arg(file); emit changed(); emit designsChanged(); return true;
}
bool Builder::load(const QString& file) {
  QFile f(file); if (!f.open(QIODevice::ReadOnly)) return false; const QByteArray text = f.readAll();
  am::shell::Design d; std::string err; if (!am::shell::parse(text.toStdString(), d, err)) { note_ = QString::fromStdString(err); emit changed(); return false; }
  blocks_.clear(); for (const auto& b : d.blocks) { addBlock(QString::fromStdString(b.kind)); for (const auto& [k, v] : b.set) setKnob(blocks_.size() - 1, QString::fromStdString(k), v); }
  const QJsonArray layout = QJsonDocument::fromJson(QByteArray::fromStdString(d.panelJson)).object().value("layout").toArray(); int n = 0;
  for (const auto& p : d.params) { for (auto& k : blocks_[p.block].knobs) if (k.id == QString::fromStdString(p.id)) { k.exposed = true; k.value = p.def; k.label = QString::fromStdString(p.title);
      const QJsonObject pos = layout.at(n).toObject(); if (!pos.isEmpty()) { k.x = pos.value("x").toDouble(); k.y = pos.value("y").toDouble(); } } ++n; }
  name_ = QString::fromStdString(d.name); vendor_ = QString::fromStdString(d.vendor); category_ = QString::fromStdString(d.category); uid_ = QString::fromStdString(d.uid);
  color_ = QJsonDocument::fromJson(QByteArray::fromStdString(d.panelJson)).object().value("color").toString("#EA580C"); file_ = file; note_ = tr("Opened %1").arg(name_); emit changed(); return true;
}
bool Builder::remove(const QString& file) { if (!file.startsWith(folder())) return false; const bool ok = QFile::remove(file); emit designsChanged(); return ok; }

QString Builder::shellPath() const {
  const QString env = qEnvironmentVariable("AMIX_PLUGIN_SHELL"); if (!env.isEmpty()) return env;
  return QCoreApplication::applicationDirPath() + "/AMFB Plugin Shell.vst3";
}
bool Builder::shellFound() const { return QDir(shellPath() + "/Contents").exists(); }
QString Builder::exportDir() const {   // the per-user VST3 folder of the VST3 specification: every DAW looks there, no administrator rights needed
#ifdef _WIN32
  return qEnvironmentVariable("LOCALAPPDATA") + "/Programs/Common/VST3";
#elif defined(__APPLE__)
  return QDir::homePath() + "/Library/Audio/Plug-Ins/VST3";
#else
  return qEnvironmentVariableIsSet("AMIX_VST3_OUT") ? qEnvironmentVariable("AMIX_VST3_OUT") : QDir::homePath() + "/.vst3";
#endif
}
QString Builder::exportPlugin() {
  am::shell::Design d; std::string err;
  if (!am::shell::parse(designJson().toStdString(), d, err)) { note_ = QString::fromStdString(err); emit changed(); return {}; }
  const std::string out = am::shell::exportBundle(shellPath().toStdString(), d, exportDir().toStdString(), err);
  if (out.empty()) { note_ = QString::fromStdString(err); emit changed(); return {}; }
  lastExport_ = QString::fromStdString(out); note_ = tr("Exported: %1 (open your DAW's plug-in list, or rescan)").arg(lastExport_); save(); emit changed(); return lastExport_;
}
bool Builder::tryOnStrip(int strip) {
  if (!engine_) { note_ = tr("TRY IN THE MIXER works inside the console: here, EXPORT and load the plug-in in your DAW."); emit changed(); return false; }
  const QString bundle = exportPlugin(); if (bundle.isEmpty()) return false;
  int found = 0; QMetaObject::invokeMethod(engine_, "rescanVst", Q_RETURN_ARG(int, found), Q_ARG(QStringList, QStringList{exportDir()}));
  QVariantList cat; QMetaObject::invokeMethod(engine_, "vstCatalog", Q_RETURN_ARG(QVariantList, cat));
  QVariantList rack; QMetaObject::invokeMethod(engine_, "plugins", Q_RETURN_ARG(QVariantList, rack), Q_ARG(int, strip));
  for (const QVariant& v : cat) { const QVariantMap m = v.toMap(); if (!QDir::cleanPath(m.value("module").toString()).startsWith(QDir::cleanPath(bundle))) continue;
    for (const QVariant& p : rack) { const QVariantMap s = p.toMap(); if (s.value("kind").toString().isEmpty()) { QMetaObject::invokeMethod(engine_, "setPlugin", Q_ARG(int, strip), Q_ARG(int, s.value("slot").toInt()), Q_ARG(QString, m.value("kind").toString()));
        note_ = tr("%1 is on CH %2: play something through it").arg(name_).arg(strip + 1); emit changed(); return true; } }
    note_ = tr("The rack of CH %1 is full.").arg(strip + 1); emit changed(); return false; }
  note_ = tr("Exported, but the console did not find it on the rescan (%1)").arg(exportDir()); emit changed(); return false;
}
