#include "Waterfall.h"

#include <QPainter>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGImageNode>
#include <QSGRendererInterface>
#include <QSGVertexColorMaterial>
#include <QScreen>
#include <QtMath>
#include <algorithm>
#include <cmath>

#include "Engine.h"

namespace wf = am::measure::wf;

namespace {
constexpr double kMargin = 28;   // room for the axis labels around the block
// the mesh node: one geometry (surface vertices, then two per ridge-line point), indices kept while the row count and order stay
struct MeshNode : QSGGeometryNode {
  QSGGeometry geo{QSGGeometry::defaultAttributes_ColoredPoint2D(), 0, 0, QSGGeometry::UnsignedIntType};
  QSGVertexColorMaterial mat; int rows = -1, bins = -1; bool olderFar = true;
  MeshNode() { geo.setDrawingMode(QSGGeometry::DrawTriangles); geo.setVertexDataPattern(QSGGeometry::StreamPattern); geo.setIndexDataPattern(QSGGeometry::StaticPattern);
    setGeometry(&geo); setMaterial(&mat); setFlag(QSGNode::OwnedByParent, true); }
};
}  // namespace

Waterfall3D::Waterfall3D(QQuickItem* parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  work_.assign(kFft, {}); power_.assign(kFft / 2 + 1, 0.f); ring_.configure(history_, kBins);
  timer_.setTimerType(Qt::PreciseTimer); connect(&timer_, &QTimer::timeout, this, &Waterfall3D::tick);
  statClock_.start();
}

QObject* Waterfall3D::engineObj() const { return engine_.data(); }
void Waterfall3D::setEngineObj(QObject* e) { engine_ = qobject_cast<Engine*>(e); emit configChanged(); reconsider(); }
void Waterfall3D::setSource(int s) { if (s == source_) return; source_ = s; ring_.clear(); rowsIn_ = 0; emit configChanged(); update(); }
void Waterfall3D::setFrozen(bool f) { if (f == frozen_) return; frozen_ = f; emit configChanged(); reconsider(); }
void Waterfall3D::setHistory(int h) { h = std::clamp(h, 16, 256); if (h == history_) return; history_ = h; ring_.configure(history_, kBins); rowsIn_ = 0; emit configChanged(); update(); }
void Waterfall3D::setSpeed(double s) { s = std::clamp(s, 2.0, 60.0); if (s == speed_) return; speed_ = s; emit configChanged(); reconsider(); }
void Waterfall3D::setDbMin(double v) { v = std::clamp(v, -140.0, dbMax_ - 12); if (v == dbMin_) return; dbMin_ = v; touchView(); }
void Waterfall3D::setDbMax(double v) { v = std::clamp(v, dbMin_ + 12, 12.0); if (v == dbMax_) return; dbMax_ = v; touchView(); }
void Waterfall3D::setYaw(double v) { v = std::remainder(v, 360.0); if (v == view_.yawDeg) return; view_.yawDeg = v; touchView(); }
void Waterfall3D::setPitch(double v) { v = std::clamp(v, 0.0, 90.0); if (v == view_.pitchDeg) return; view_.pitchDeg = v; touchView(); }
void Waterfall3D::clear() { ring_.clear(); rowsIn_ = 0; emit statsChanged(); update(); }

QPointF Waterfall3D::project(double freq, double db, double age) const {
  wf::Projector p; p.set(view_, width(), height(), kMargin); const wf::LogAxis ax;
  const wf::P2 q = p.map(ax.u(freq), wf::norm(db, dbMin_, dbMax_), std::clamp(age, 0.0, 1.0)); return {q.x, q.y};
}

void Waterfall3D::geometryChange(const QRectF& n, const QRectF& o) { QQuickItem::geometryChange(n, o); if (n.size() != o.size()) { touchView(); reconsider(); } }
void Waterfall3D::itemChange(ItemChange c, const ItemChangeData& d) {
  QQuickItem::itemChange(c, d);
  if (c == ItemSceneChange && d.window) connect(d.window, &QWindow::visibilityChanged, this, &Waterfall3D::reconsider, Qt::UniqueConnection);
  if (c == ItemVisibleHasChanged || c == ItemSceneChange) reconsider();
}

void Waterfall3D::reconsider() {
  QQuickWindow* w = window();
  const bool run = engine_ && isVisible() && !frozen_ && w && w->isVisible() && w->visibility() != QWindow::Minimized && width() > 8 && height() > 8;
  const double hz = std::min(speed_, w && w->screen() ? std::max(30.0, w->screen()->refreshRate()) : 60.0);   // never faster than the display
  const int ms = std::max(1, static_cast<int>(std::lround(1000.0 / hz)));
  if (run && (!timer_.isActive() || timer_.interval() != ms)) timer_.start(ms); else if (!run && timer_.isActive()) timer_.stop();
  emit statsChanged();
}

void Waterfall3D::tick() {
  if (!engine_) return;
  QElapsedTimer t; t.start();
  ScopeTap& sc = engine_->scope(); sc.setAnalysisSource(source_);
  if (sc.sampleRate() != bandsFs_) { bandsFs_ = sc.sampleRate(); bands_.prepare(kBins, kFft, bandsFs_); }
  sc.analysisPower(work_.data(), kFft, power_.data());
  bands_.fold(power_.data(), ring_.push()); ++rowsIn_;
  update();
  statNs_ += t.nsecsElapsed();
  if (statClock_.elapsed() >= 1000) { cpuMs_ = statNs_ / 1e6 * 1000.0 / statClock_.elapsed(); statNs_ = 0; statClock_.restart(); emit statsChanged(); }
}

// the software back end: rows painted far to near. From the front or at an angle, each row is a filled ridge (it hides what is behind it)
// with its line in the level colours; from above, each row is a band of colour (a spectrogram).
void Waterfall3D::paintSoftware(QImage& img) {
  const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0; const QSize px(std::max(1, qCeil(width() * dpr)), std::max(1, qCeil(height() * dpr)));
  if (img.size() != px) { img = QImage(px, QImage::Format_ARGB32_Premultiplied); img.setDevicePixelRatio(dpr); }
  img.fill(Qt::transparent); QPainter g(&img); g.setRenderHint(QPainter::Antialiasing, false);   // plain pixels: a third of the cost
  wf::Projector pr; pr.set(view_, width(), height(), kMargin); const int H = ring_.rows(), B = ring_.bins(), have = std::min(ring_.count(), H);
  const bool fromAbove = view_.pitchDeg >= 60; const double rowStep = std::fabs(pr.map(0.5, 0, 0).y - pr.map(0.5, 0, 1.0 / std::max(1, H - 1)).y);
  QPointF pts[kBins + 2]; const QColor panel(12, 16, 26, 235);
  for (int s = 0; s < have; ++s) {
    const int r = pr.olderIsFarther() ? have - 1 - s : s; const float* row = ring_.row(r); const double age = H > 1 ? static_cast<double>(r) / (H - 1) : 0, fade = view_.pitchDeg < 12 ? std::pow(1.0 - age, 1.6) : 1.0 - 0.55 * age;
    for (int b = 0; b < B; ++b) { const wf::P2 q = pr.map((b + 0.5) / B, wf::norm(row[b], dbMin_, dbMax_), age); pts[b] = QPointF(q.x, q.y); }
    if (!fromAbove && view_.pitchDeg >= 12) { const wf::P2 a = pr.map((B - 0.5) / B, 0, age), z = pr.map(0.5 / B, 0, age); pts[B] = QPointF(a.x, a.y); pts[B + 1] = QPointF(z.x, z.y);
      g.setPen(Qt::NoPen); g.setBrush(panel); g.drawPolygon(pts, B + 2); }
    const double w = fromAbove ? std::max(1.0, rowStep + 0.6) : 1.3;
    int from = 0; int key = -1;   // runs of one colour step drawn as one polyline
    for (int b = 0; b <= B; ++b) {
      const int k = b < B ? static_cast<int>(wf::norm(row[b], dbMin_, dbMax_) * 31) : -2;
      if (k != key || b == B) { if (key >= 0 && b - from >= 1) { const wf::Rgba8 c = cmap_.at((key + 0.5) / 31.0); QPen pen(QColor(int(c.r * fade), int(c.g * fade), int(c.b * fade)), w); pen.setCapStyle(Qt::FlatCap); g.setPen(pen); g.drawPolyline(pts + std::max(0, from - 1), b - std::max(0, from - 1)); } key = k; from = b; } }
  }
}

QSGNode* Waterfall3D::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
  QElapsedTimer t; t.start();
  QQuickWindow* w = window(); if (!w || width() <= 0 || height() <= 0) { delete old; return nullptr; }
  const bool software = w->rendererInterface()->graphicsApi() == QSGRendererInterface::Software;
  if (software) {
    auto* n = dynamic_cast<QSGImageNode*>(old); if (!n) { delete old; n = w->createImageNode(); n->setOwnsTexture(true); }
    softAt_ ^= 1; paintSoftware(soft_[softAt_]); n->setTexture(w->createTextureFromImage(soft_[softAt_])); n->setRect(boundingRect()); n->setFiltering(QSGTexture::Linear);
    statNs_ += t.nsecsElapsed(); return n;
  }
  auto* n = dynamic_cast<MeshNode*>(old); if (!n) { delete old; n = new MeshNode; }
  const int H = ring_.rows(), B = ring_.bins(), have = std::min(ring_.count(), H); wf::Projector pr; pr.set(view_, width(), height(), kMargin);
  const int surf = H * B, verts = 3 * H * B; const bool olderFar = pr.olderIsFarther();
  if (n->rows != H || n->bins != B || n->olderFar != olderFar) {   // the triangles: far to near, each row's ridge line, then the surface towards the nearer row
    n->geo.allocate(verts, (H - 1) * (B - 1) * 6 + H * (B - 1) * 6); quint32* ix = n->geo.indexDataAsUInt(); int k = 0;
    for (int s = 0; s < H; ++s) { const int r = olderFar ? H - 1 - s : s;
      for (int b = 0; b + 1 < B; ++b) { const quint32 a = surf + 2 * (r * B + b), c = surf + 2 * (r * B + b + 1); ix[k++] = a; ix[k++] = a + 1; ix[k++] = c; ix[k++] = c; ix[k++] = a + 1; ix[k++] = c + 1; }
      const int r2 = olderFar ? r - 1 : r + 1; if (r2 < 0 || r2 >= H) continue;
      for (int b = 0; b + 1 < B; ++b) { const quint32 a = r * B + b, c = r2 * B + b; ix[k++] = a; ix[k++] = a + 1; ix[k++] = c; ix[k++] = c; ix[k++] = a + 1; ix[k++] = c + 1; } }
    n->geo.markIndexDataDirty(); n->rows = H; n->bins = B; n->olderFar = olderFar; }
  QSGGeometry::ColoredPoint2D* v = n->geo.vertexDataAsColoredPoint2D();
  const bool fromAbove = view_.pitchDeg >= 70, flat = view_.pitchDeg < 12; const float lw = 1.3f;
  for (int r = 0; r < H; ++r) {
    const float* row = ring_.row(r); const double age = H > 1 ? static_cast<double>(r) / (H - 1) : 0; const bool filled = r < have;
    const double fade = 1.0 - 0.55 * age, alphaLine = !filled || fromAbove ? 0.0 : flat ? std::pow(1.0 - age, 1.6) : 1.0, alphaSurf = !filled ? 0.0 : flat ? 0.0 : 0.96;
    for (int b = 0; b < B; ++b) {
      const double lv = wf::norm(row[b], dbMin_, dbMax_); const wf::P2 q = pr.map((b + 0.5) / B, lv, age); const wf::Rgba8 c = cmap_.at(lv);
      const double sb = fromAbove ? fade * 1.0 : fade * (0.30 + 0.45 * lv);   // the surface darker than its ridge line, unless seen from above
      const uchar sa = static_cast<uchar>(255 * alphaSurf), la = static_cast<uchar>(255 * alphaLine);
      v[r * B + b].set(float(q.x), float(q.y), uchar(c.r * sb * alphaSurf), uchar(c.g * sb * alphaSurf), uchar(c.b * sb * alphaSurf), sa);   // premultiplied
      const double lb = fade * alphaLine; QSGGeometry::ColoredPoint2D* l = v + surf + 2 * (r * B + b);
      l[0].set(float(q.x), float(q.y) - lw, uchar(c.r * lb), uchar(c.g * lb), uchar(c.b * lb), la); l[1].set(float(q.x), float(q.y) + lw * 0.4f, uchar(c.r * lb), uchar(c.g * lb), uchar(c.b * lb), la); }
  }
  n->geo.markVertexDataDirty(); n->markDirty(QSGNode::DirtyGeometry);
  statNs_ += t.nsecsElapsed(); return n;
}
