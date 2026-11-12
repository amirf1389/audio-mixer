#include "MixerBridge.h"
#include <algorithm>
#include <cmath>

namespace {
double toDb(float v) { return v <= 3.2e-5f ? -90.0 : 20.0 * std::log10(double(v)); }
}

MixerBridge::MixerBridge(QObject* parent) : QObject(parent) {
  timer_.setInterval(33);
  connect(&timer_, &QTimer::timeout, this, &MixerBridge::poll);
  timer_.start();
  poll();
}

void MixerBridge::poll() {
  if (!b_) {                                             // look for the engine once a second
    if (retry_-- > 0) return;
    retry_ = 30;
    b_ = mem_.open(false);
    if (!b_) return;
    lastBeat_ = b_->heartbeat.load(); still_ = 0;
  }
  const quint64 beat = b_->heartbeat.load();
  still_ = beat == lastBeat_ ? still_ + 1 : 0;
  lastBeat_ = beat;
  const bool alive = still_ < 45;                        // 1.5 s without a block = the engine is gone
  if (alive != alive_) { alive_ = alive; emit connectedChanged(); emit paramsChanged(); }
  if (!alive) { mem_.close(); b_ = nullptr; levels_.clear(); emit levelsChanged(); return; }

  QVariantList out;
  out.reserve(amfb::kStrips + 1);
  for (int i = 0; i <= amfb::kStrips; ++i) {
    const auto& m = b_->m[i];
    QVariantMap v;
    v.insert(QStringLiteral("peakL"), toDb(m.peakL.load())); v.insert(QStringLiteral("peakR"), toDb(m.peakR.load()));
    v.insert(QStringLiteral("rmsL"), toDb(m.rmsL.load())); v.insert(QStringLiteral("rmsR"), toDb(m.rmsR.load()));
    v.insert(QStringLiteral("truePeak"), toDb(m.truePeak.load())); v.insert(QStringLiteral("hold"), toDb(m.hold.load()));
    v.insert(QStringLiteral("phase"), double(m.phase.load())); v.insert(QStringLiteral("active"), m.active.load() != 0);
    out.append(v);
  }
  levels_ = out;
  emit levelsChanged();
}

QVariantMap MixerBridge::strip(int index) const {
  QVariantMap v;
  if (!ok(index)) return v;
  const auto& p = b_->p[index];
  char label[sizeof b_->m[index].source + 1] = {};
  std::copy(std::begin(b_->m[index].source), std::end(b_->m[index].source), label);
  v.insert(QStringLiteral("source"), QString::fromUtf8(label));
  v.insert(QStringLiteral("gainDb"), double(p.gainDb.load())); v.insert(QStringLiteral("pan"), double(p.pan.load()));
  v.insert(QStringLiteral("width"), double(p.width.load()));
  v.insert(QStringLiteral("mute"), p.mute.load() != 0); v.insert(QStringLiteral("solo"), p.solo.load() != 0);
  v.insert(QStringLiteral("eqOn"), p.eqOn.load() != 0);
  QVariantList eq;
  for (int k = 0; k < amfb::kBands; ++k) {
    QVariantMap b;
    b.insert(QStringLiteral("freq"), double(p.eqFreq[k].load())); b.insert(QStringLiteral("gainDb"), double(p.eqGain[k].load()));
    b.insert(QStringLiteral("q"), double(p.eqQ[k].load()));
    eq.append(b);
  }
  v.insert(QStringLiteral("eq"), eq);
  return v;
}

void MixerBridge::setMasterGainDb(double db) { if (alive_ && std::isfinite(db)) { b_->masterGainDb = float(std::clamp(db, -90.0, 12.0)); bump(); } }
void MixerBridge::setGainDb(int i, double db) { if (ok(i) && std::isfinite(db)) { b_->p[i].gainDb = float(std::clamp(db, -90.0, 12.0)); bump(); } }
void MixerBridge::setPan(int i, double pan) { if (ok(i) && std::isfinite(pan)) { b_->p[i].pan = float(std::clamp(pan, -1.0, 1.0)); bump(); } }
void MixerBridge::setWidth(int i, double w) { if (ok(i) && std::isfinite(w)) { b_->p[i].width = float(std::clamp(w, 0.0, 2.0)); bump(); } }
void MixerBridge::setMute(int i, bool on) { if (ok(i)) { b_->p[i].mute = on; bump(); } }
void MixerBridge::setSolo(int i, bool on) { if (ok(i)) { b_->p[i].solo = on; bump(); } }
void MixerBridge::setEqOn(int i, bool on) { if (ok(i)) { b_->p[i].eqOn = on; bump(); } }
void MixerBridge::setEqBand(int i, int band, double freq, double gainDb, double q) {
  if (!ok(i) || band < 0 || band >= amfb::kBands || !std::isfinite(freq) || !std::isfinite(gainDb) || !std::isfinite(q)) return;
  auto& p = b_->p[i];
  p.eqFreq[band] = float(std::clamp(freq, 20.0, 20000.0)); p.eqGain[band] = float(std::clamp(gainDb, -18.0, 18.0));
  p.eqQ[band] = float(std::clamp(q, 0.1, 18.0));
  bump();
}
