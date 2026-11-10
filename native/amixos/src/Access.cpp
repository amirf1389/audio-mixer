#include "Access.h"
#include "paths/paths.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include "Engine.h"
#include "license/crypto.hpp"

namespace {
struct Role { const char* id; const char* name; const char* blurb; bool def[6]; };   // faders dsp routing loudness archive system
const Role kRoles[] = {
  {"master", "FOH MASTER", "Full console: mixing, DSP, routing, scenes, system and admin pages", {true, true, true, true, true, true}},
  {"monitor", "MONITOR TECH", "Faders, DSP and sends for the stage mix; routing, loudness, archive and system locked", {true, true, false, false, false, false}},
  {"broadcast", "BROADCAST", "Mix, DSP, routing and loudness / limiter; no scene import, no system pages", {true, true, true, true, false, false}},
  {"guest", "GUEST", "Faders and meters only; everything else read-only or hidden", {true, false, false, false, false, false}},
};
const char* kPerms[6] = {"faders", "dsp", "routing", "loudness", "archive", "system"};
int roleIndex(const QString& id) { for (int i = 0; i < 4; ++i) if (id == kRoles[i].id) return i; return 0; }
}  // namespace

Access::Access(Engine* engine, QObject* parent) : QObject(parent), engine_(engine) {}

QVariantList Access::roles() const { QVariantList l; for (const Role& r : kRoles) { QVariantMap m; m["id"] = r.id; m["name"] = r.name; m["blurb"] = r.blurb; l << m; } return l; }
QVariantMap Access::matrix() const {
  QVariantMap all;
  for (const Role& r : kRoles) { QVariantMap row; for (int p = 0; p < 6; ++p) row[kPerms[p]] = s_.value(QStringLiteral("access/matrix/%1/%2").arg(r.id, kPerms[p]), r.def[p]).toBool(); all[r.id] = row; }
  return all;
}
bool Access::can(const QString& perm) const {
  if (perm.isEmpty()) return true;
  if (perm == "faders" && locked_) return false;
  const Role& r = kRoles[roleIndex(role())]; int p = 0; for (; p < 6; ++p) if (perm == kPerms[p]) break; if (p == 6) return true;
  return s_.value(QStringLiteral("access/matrix/%1/%2").arg(r.id, kPerms[p]), r.def[p]).toBool();
}
QString Access::permOfPage(const QString& id) const {
  static const QHash<QString, QString> k = {
    {"dsp", "dsp"}, {"dyn", "dsp"}, {"gate", "dsp"}, {"fx", "dsp"}, {"miceq", "dsp"}, {"bus", "dsp"}, {"dca", "dsp"}, {"arrange", "dsp"}, {"tools", "dsp"},
    {"routing", "routing"}, {"io", "routing"}, {"live", "routing"},
    {"lufs", "loudness"},
    {"scenes", "archive"}, {"presets", "archive"},
    {"sys", "system"}, {"calib", "system"}, {"boot", "system"}, {"acct", "system"}, {"access", "system"}, {"admin", "system"}, {"renew", "system"}};
  return k.value(id);
}
bool Access::canPage(const QString& id) const {
  if (consumerEdition() && (id == "acct" || id == "access" || id == "admin" || id == "boot")) return adminMode();   // the consumer edition hides them unless an admin is in
  if (id == "acct") return true;   // every role can reach ACCOUNT to switch role or unlock
  return can(permOfPage(id));
}
bool Access::adminMode() const { return adminAt_ > 0 && QDateTime::currentSecsSinceEpoch() - adminAt_ < 1800; }
void Access::setConsumerEdition(bool v) { if (!adminMode() && v != consumerEdition()) { note("ADMIN mode (PIN) is needed to switch the edition"); return; } s_.setValue("access/consumer", v); s_.sync(); log(v ? "Edition set to CONSUMER (operator pages hidden)" : "Edition set to PRO (operator pages shown)"); emit changed(); }

QString Access::hashPin(const QString& pin) { const QByteArray b = ("amixos-pin:" + pin).toUtf8(); const auto h = am::crypto::sha256(reinterpret_cast<const std::uint8_t*>(b.constData()), static_cast<std::size_t>(b.size())); return QString::fromStdString(am::crypto::hex(h.data(), h.size(), false)); }
bool Access::checkPin(const QString& pin) {
  const QString want = s_.value("access/pinHash", hashPin("1234")).toString();
  const bool ok = !pin.isEmpty() && hashPin(pin) == want;
  if (ok) failed_ = 0; else { ++failed_; log(QStringLiteral("Wrong PIN (%1 in a row)").arg(failed_)); }
  emit changed(); return ok;
}
bool Access::setRole(const QString& id, const QString& pin) {
  const int to = roleIndex(id), from = roleIndex(role()); bool more = false;
  for (int p = 0; p < 6; ++p) if (kRoles[to].def[p] && !kRoles[from].def[p]) more = true;
  if (more && !adminMode() && !checkPin(pin)) { note(QStringLiteral("%1 has more rights than %2: the PIN is needed").arg(kRoles[to].name, kRoles[from].name)); return false; }
  s_.setValue("access/role", kRoles[to].id); s_.sync(); log(QStringLiteral("Role switched to %1").arg(kRoles[to].name)); note(QStringLiteral("Operator: %1").arg(kRoles[to].name)); emit changed(); return true;
}
void Access::setPerm(const QString& role, const QString& perm, bool on) {
  if (!adminMode()) { note("ADMIN mode (PIN) is needed to change permissions"); return; }
  if (role == "master" && perm == "system") { note("FOH MASTER always keeps the system pages, or nobody could administer the console"); return; }
  s_.setValue(QStringLiteral("access/matrix/%1/%2").arg(role, perm), on); s_.sync(); log(QStringLiteral("Permission %1 for %2: %3").arg(perm.toUpper(), kRoles[roleIndex(role)].name, on ? "ALLOWED" : "DENIED")); emit changed();
}
void Access::resetMatrix() { if (!adminMode()) { note("ADMIN mode (PIN) is needed"); return; } s_.remove("access/matrix"); s_.sync(); log("Permission matrix reset to defaults"); emit changed(); }
void Access::lock() { locked_ = true; adminAt_ = 0; log("Console surface LOCKED"); note("Surface locked: faders, mutes and solos do not react until the PIN"); emit changed(); }
bool Access::unlock(const QString& pin) { if (!checkPin(pin)) { note("Wrong PIN"); return false; } locked_ = false; log("Console surface unlocked"); note("Surface unlocked"); emit changed(); return true; }
bool Access::enterAdmin(const QString& pin) { if (!checkPin(pin)) { note("Wrong PIN"); return false; } adminAt_ = QDateTime::currentSecsSinceEpoch(); log("ADMIN mode entered"); note("ADMIN mode for 30 minutes"); emit changed(); return true; }
void Access::leaveAdmin() { if (adminAt_) log("ADMIN mode left"); adminAt_ = 0; emit changed(); }
bool Access::setPin(const QString& oldPin, const QString& newPin) {
  static const QRegularExpression digits("^[0-9]{4,8}$");
  if (!digits.match(newPin).hasMatch()) { note("The PIN is 4 to 8 digits"); return false; }
  if (!checkPin(oldPin)) { note("The current PIN is wrong"); return false; }
  s_.setValue("access/pinHash", hashPin(newPin)); s_.sync(); log("PIN changed"); note("PIN changed"); emit changed(); return true;
}
QString Access::auditPath() const { return QString::fromStdString(am::paths::file("audit.log")); }
void Access::log(const QString& text) {
  QFile f(auditPath()); if (!f.open(QIODevice::Append | QIODevice::Text)) return;
  QTextStream(&f) << QDateTime::currentDateTime().toString(Qt::ISODate) << '\t' << kRoles[roleIndex(role())].name << '\t' << QString(text).replace('\n', ' ') << '\n';
  f.close(); emit auditChanged();
}
QVariantList Access::audit() const {
  QVariantList l; QFile f(auditPath()); if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return l;
  QStringList lines; while (!f.atEnd()) lines << QString::fromUtf8(f.readLine()).trimmed(); if (lines.size() > 300) lines = lines.mid(lines.size() - 300);
  for (const QString& ln : lines) { const QStringList p = ln.split('\t'); if (p.size() < 3) continue; QVariantMap m; m["at"] = p[0]; m["who"] = p[1]; m["text"] = p.mid(2).join('\t'); l << m; }
  return l;
}
void Access::clearAudit() { if (!adminMode()) { note("ADMIN mode (PIN) is needed to clear the log"); return; } QFile::remove(auditPath()); log("Audit log cleared"); }
QString Access::exportAudit() {
  const QString out = QString::fromStdString(am::paths::dataDir()) + "/audit-export-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".csv";
  QFile in(auditPath()), o(out); if (!in.open(QIODevice::ReadOnly | QIODevice::Text) || !o.open(QIODevice::WriteOnly | QIODevice::Truncate)) { note("Nothing to export yet"); return QString(); }
  QTextStream ts(&o); ts << "time,role,event\n"; while (!in.atEnd()) { const QStringList p = QString::fromUtf8(in.readLine()).trimmed().split('\t'); if (p.size() >= 3) ts << p[0] << ',' << p[1] << ",\"" << QString(p.mid(2).join(' ')).replace('"', "'") << "\"\n"; }
  note(QStringLiteral("Audit log exported to %1").arg(out)); return out;
}
