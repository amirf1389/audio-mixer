// GRAPHICS (Graphics.h): the drawing back end per computer, the first-frame guard, PERFORMANCE and the frame statistics.
#include "Graphics.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QProcess>
#include <QQuickWindow>
#include <QScreen>
#include <QSettings>
#include <QThread>
#include <algorithm>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#endif

namespace {
QSettings store() { return QSettings(QStringLiteral("AMFB Audio"), QStringLiteral("A mix OS")); }
const char* kChoice = "gfx/choice"; const char* kPending = "gfx/pending"; const char* kBad = "gfx/bad"; const char* kGood = "gfx/good"; const char* kPerf = "perf/mode";

bool hasD3D12() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0) && defined(_WIN32)
  return true;
#else
  return false;
#endif
}
bool hasVulkan() {
#if QT_CONFIG(vulkan) && defined(_WIN32)
  HMODULE m = LoadLibraryW(L"vulkan-1.dll"); if (m) FreeLibrary(m); return m != nullptr;   // the loader comes with every current graphics driver
#elif QT_CONFIG(vulkan)
  return false;   // Linux: the console is not offered Vulkan (it is tested on Windows only)
#else
  return false;
#endif
}

struct Gpu { QString name, driver; unsigned vendor = 0, device = 0; bool found = false; };
Gpu findGpu() {
  Gpu g;
#ifdef _WIN32
  using CreateFactory = HRESULT(WINAPI*)(REFIID, void**);
  HMODULE dx = LoadLibraryW(L"dxgi.dll"); if (!dx) return g;
  auto cf = reinterpret_cast<CreateFactory>(reinterpret_cast<void*>(GetProcAddress(dx, "CreateDXGIFactory1")));
  IDXGIFactory1* f = nullptr;
  if (cf && SUCCEEDED(cf(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&f))) && f) {
    IDXGIAdapter1* a = nullptr;
    if (SUCCEEDED(f->EnumAdapters1(0, &a)) && a) {   // adapter 0: the one Direct3D and Qt use by default (the monitor's card)
      DXGI_ADAPTER_DESC1 d{}; if (SUCCEEDED(a->GetDesc1(&d))) { g.name = QString::fromWCharArray(d.Description).trimmed(); g.vendor = d.VendorId; g.device = d.DeviceId; g.found = true; }
      LARGE_INTEGER v{}; if (SUCCEEDED(a->CheckInterfaceSupport(__uuidof(IDXGIDevice), &v)))
        g.driver = QStringLiteral("%1.%2.%3.%4").arg(HIWORD(v.HighPart)).arg(LOWORD(v.HighPart)).arg(HIWORD(v.LowPart)).arg(LOWORD(v.LowPart));
      a->Release();
    }
    f->Release();
  }
  FreeLibrary(dx);
#endif
  return g;
}
QString vendorName(unsigned v) {
  switch (v) { case 0x10DE: return QStringLiteral("NVIDIA"); case 0x1002: case 0x1022: return QStringLiteral("AMD"); case 0x8086: return QStringLiteral("Intel");
    case 0x1414: return QStringLiteral("Microsoft"); case 0x5143: return QStringLiteral("Qualcomm"); default: return QString(); }
}
// the short known-bad list: no real driver (Windows' own Basic Render Driver draws on the processor; the software renderer is lighter)
bool knownBad(const Gpu& g, QString* why) {
  if (g.vendor == 0x1414 && (g.device == 0x8c || g.name.contains(QLatin1String("Basic Render"), Qt::CaseInsensitive))) { *why = QStringLiteral("no graphics driver installed (Microsoft Basic Render Driver)"); return true; }
  return false;
}
bool probeD3D11(int* level) {
#ifdef _WIN32
  using Create = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
  HMODULE m = LoadLibraryW(L"d3d11.dll"); if (!m) return false;
  auto create = reinterpret_cast<Create>(reinterpret_cast<void*>(GetProcAddress(m, "D3D11CreateDevice")));
  const D3D_FEATURE_LEVEL lv[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
  ID3D11Device* dev = nullptr; D3D_FEATURE_LEVEL got{}; HRESULT hr = E_FAIL;
  if (create) { hr = create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, lv, 4, D3D11_SDK_VERSION, &dev, &got, nullptr);
    if (hr == E_INVALIDARG) hr = create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, lv + 1, 3, D3D11_SDK_VERSION, &dev, &got, nullptr); }   // Windows 7 without 11.1
  if (dev) dev->Release();
  FreeLibrary(m);
  if (level) *level = SUCCEEDED(hr) ? static_cast<int>(got) : 0;
  return SUCCEEDED(hr) && got >= D3D_FEATURE_LEVEL_10_0;
#else
  (void)level; return false;
#endif
}
bool probeOpenGL(QString* info) {
  QOpenGLContext ctx; if (!ctx.create()) return false;
  QOffscreenSurface s; s.setFormat(ctx.format()); s.create(); if (!s.isValid() || !ctx.makeCurrent(&s)) return false;
  const auto* r = reinterpret_cast<const char*>(ctx.functions()->glGetString(GL_RENDERER)); const auto* v = reinterpret_cast<const char*>(ctx.functions()->glGetString(GL_VERSION));
  if (info) *info = QStringLiteral("%1, OpenGL %2").arg(QString::fromLatin1(r ? r : "?"), QString::fromLatin1(v ? v : "?"));
  const bool ok = ctx.format().majorVersion() >= 2; ctx.doneCurrent(); return ok;
}
void apply(const QString& api) {
  if (api == QLatin1String("software")) QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
  else if (api == QLatin1String("opengl")) QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  else if (api == QLatin1String("d3d11")) QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
  else if (api == QLatin1String("d3d12")) QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D12);
#endif
  else if (api == QLatin1String("vulkan")) QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
  // the threaded render loop (drawing beside the interface thread, paced by the monitor's vertical sync) for the modern back ends
  if ((api == QLatin1String("d3d11") || api == QLatin1String("d3d12") || api == QLatin1String("vulkan")) && qEnvironmentVariableIsEmpty("QSG_RENDER_LOOP")) qputenv("QSG_RENDER_LOOP", "threaded");
}
bool available(const QString& api) {
  if (api == QLatin1String("software") || api == QLatin1String("opengl")) return true;
#ifdef _WIN32
  if (api == QLatin1String("d3d11")) return true;
#endif
  if (api == QLatin1String("d3d12")) return hasD3D12();
  if (api == QLatin1String("vulkan")) return hasVulkan();
  return false;
}
double pct(std::vector<double> v, double p) { if (v.empty()) return 0; std::sort(v.begin(), v.end()); return v[std::min(v.size() - 1, static_cast<size_t>(p * (v.size() - 1) + 0.5))]; }
double mean(const std::vector<double>& v) { double s = 0; for (double x : v) s += x; return v.empty() ? 0 : s / v.size(); }
}  // namespace

QString Graphics::apiName(const QString& v) {
  if (v == QLatin1String("d3d11")) return QStringLiteral("DIRECT3D 11");
  if (v == QLatin1String("d3d12")) return QStringLiteral("DIRECT3D 12");
  if (v == QLatin1String("vulkan")) return QStringLiteral("VULKAN");
  if (v == QLatin1String("opengl")) return QStringLiteral("OPENGL");
  if (v == QLatin1String("software")) return QStringLiteral("SOFTWARE");
  if (v == QLatin1String("auto")) return QStringLiteral("AUTO");
  return v.toUpper();
}

void Graphics::choose(bool testRun, const QStringList& args) {
  s_test = testRun; QSettings st = store();
  s_choice = st.value(kChoice, QStringLiteral("auto")).toString();
  const Gpu g = findGpu();
  if (g.found) { s_gpu = vendorName(g.vendor).isEmpty() || g.name.startsWith(vendorName(g.vendor), Qt::CaseInsensitive) ? g.name : vendorName(g.vendor) + QLatin1Char(' ') + g.name; s_driver = g.driver; }
  if (args.contains(QStringLiteral("--software"))) { apply(QStringLiteral("software")); s_started = QStringLiteral("software"); s_safe = true; s_note = QStringLiteral("SOFTWARE SAFE MODE (asked for on the command line)"); }
  else if (args.contains(QStringLiteral("--opengl"))) { apply(QStringLiteral("opengl")); s_started = QStringLiteral("opengl"); s_note = QStringLiteral("OPENGL (asked for on the command line)"); }
  else if (testRun || !qEnvironmentVariableIsEmpty("QSG_RHI_BACKEND")) { s_started = QStringLiteral("default"); s_note = testRun ? QStringLiteral("test run: Qt's own choice") : QStringLiteral("set by QSG_RHI_BACKEND"); }
  if (!s_started.isEmpty()) { if (!testRun) { st.setValue(kPending, s_started); st.sync(); } return; }
  QStringList bad = st.value(kBad).toStringList();
  const QString pending = st.value(kPending).toString();
  if (!pending.isEmpty()) {   // the last start never showed its first picture
    if (pending != QLatin1String("software") && pending != QLatin1String("default") && !bad.contains(pending)) { bad << pending; st.setValue(kBad, bad); }
    s_started = QStringLiteral("software"); s_safe = true;
    s_note = QStringLiteral("SOFTWARE SAFE MODE: the last start did not reach its first picture with %1. AUTO leaves %1 out from now on; choose it again in SETTINGS to try it once more.").arg(apiName(pending));
  } else if (s_choice == QLatin1String("software")) { s_started = s_choice; s_safe = true; s_note = QStringLiteral("SOFTWARE SAFE MODE, chosen in SETTINGS"); }
  else if (s_choice != QLatin1String("auto") && available(s_choice) && !bad.contains(s_choice)) { s_started = s_choice; s_note = QStringLiteral("%1, chosen in SETTINGS").arg(apiName(s_choice)); }
  else {
    const QString lead = s_choice != QLatin1String("auto") ? QStringLiteral("%1 is not usable here (%2): ").arg(apiName(s_choice), bad.contains(s_choice) ? QStringLiteral("it failed at an earlier start") : QStringLiteral("not in this build or not installed")) : QString();
#ifdef _WIN32
    QStringList order{QStringLiteral("d3d11"), QStringLiteral("opengl")}; const QString good = st.value(kGood).toString();
    if (order.contains(good)) { order.removeAll(good); order.prepend(good); }
    QString why; const bool badCard = knownBad(g, &why);
    for (const QString& c : order) {
      if (badCard) break;
      if (bad.contains(c)) continue;
      int lv = 0; QString gl;
      if (c == QLatin1String("d3d11") && probeD3D11(&lv)) { s_started = c; s_note = lead + QStringLiteral("AUTO: DIRECT3D 11 (feature level %1.%2)").arg((lv >> 12) & 0xf).arg((lv >> 8) & 0xf); break; }
      if (c == QLatin1String("opengl") && probeOpenGL(&gl)) { s_started = c; s_note = lead + QStringLiteral("AUTO: OPENGL (%1), Direct3D 11 could not start on this card").arg(gl); if (s_driver.isEmpty()) s_driver = gl; break; }
    }
    if (s_started.isEmpty()) { s_started = QStringLiteral("software"); s_safe = true; s_note = lead + QStringLiteral("AUTO: SOFTWARE SAFE MODE, %1").arg(badCard ? why : QStringLiteral("neither Direct3D 11 nor OpenGL could start")); }
#else
    s_started = QStringLiteral("default"); s_note = lead + QStringLiteral("AUTO: Qt's own choice for this system");
#endif
  }
  if (s_started != QLatin1String("default")) apply(s_started);
  st.setValue(kPending, s_started); st.sync();   // cleared at the first frame shown
}

Graphics::Graphics(QObject* parent) : QObject(parent) {
  clock_.start();
  stats_.setInterval(1000); connect(&stats_, &QTimer::timeout, this, &Graphics::sample);
  power_.setInterval(30000); connect(&power_, &QTimer::timeout, this, &Graphics::recompute); power_.start();
  recompute();
}

QString Graphics::choice() const { return store().value(kChoice, QStringLiteral("auto")).toString(); }
void Graphics::setChoice(const QString& c) {
  if (c != QLatin1String("auto") && !available(c)) return;
  QSettings st = store(); st.setValue(kChoice, c);
  QStringList bad = st.value(kBad).toStringList(); if (bad.removeAll(c) > 0) st.setValue(kBad, bad);   // chosen again by hand: tried once more
  if (c == QLatin1String("auto")) st.remove(kBad);
  emit changed();
}
QVariantList Graphics::choices() const {
  QVariantList l; auto add = [&](const QString& v, const QString& t) { l << QVariantMap{{QStringLiteral("v"), v}, {QStringLiteral("t"), t}}; };
  add(QStringLiteral("auto"), QStringLiteral("AUTO"));
#ifdef _WIN32
  add(QStringLiteral("d3d11"), QStringLiteral("DIRECT3D 11"));
#endif
  if (hasD3D12()) add(QStringLiteral("d3d12"), QStringLiteral("DIRECT3D 12"));
  if (hasVulkan()) add(QStringLiteral("vulkan"), QStringLiteral("VULKAN"));
  add(QStringLiteral("opengl"), QStringLiteral("OPENGL")); add(QStringLiteral("software"), QStringLiteral("SOFTWARE SAFE MODE"));
  return l;
}
QString Graphics::inUse() const {
  if (!win_ || !win_->rendererInterface()) return s_started;
  switch (win_->rendererInterface()->graphicsApi()) {
    case QSGRendererInterface::Software: return QStringLiteral("software");
    case QSGRendererInterface::OpenGL: return QStringLiteral("opengl");
    case QSGRendererInterface::Direct3D11: return QStringLiteral("d3d11");
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    case QSGRendererInterface::Direct3D12: return QStringLiteral("d3d12");
#endif
    case QSGRendererInterface::Vulkan: return QStringLiteral("vulkan");
    case QSGRendererInterface::Metal: return QStringLiteral("metal");
    case QSGRendererInterface::Null: return QStringLiteral("null");
    default: return s_started;
  }
}
QString Graphics::renderLoop() const { const QString e = qEnvironmentVariable("QSG_RENDER_LOOP"); return e.isEmpty() ? QStringLiteral("Qt default") : e; }
QString Graphics::perfMode() const { return store().value(kPerf, QStringLiteral("auto")).toString(); }
void Graphics::setPerfMode(const QString& m) {
  if (m != QLatin1String("auto") && m != QLatin1String("smooth") && m != QLatin1String("battery")) return;
  QSettings st = store(); st.setValue(kPerf, m); recompute(); emit changed();
}
void Graphics::safeModeNextStart() { setChoice(QStringLiteral("software")); }
void Graphics::restartNow() {
  if (s_test) return;
  QSettings st = store(); st.remove(kPending); st.sync();   // a deliberate restart is not a failed start
  relaunch();
}
void Graphics::relaunch() {
  QStringList args = QCoreApplication::arguments().mid(1); args.removeAll(QStringLiteral("--software")); args.removeAll(QStringLiteral("--opengl"));
  if (QProcess::startDetached(QCoreApplication::applicationFilePath(), args)) QCoreApplication::quit();
}

void Graphics::attach(QQuickWindow* w) {
  if (!w || win_) return; win_ = w;
  connect(w, &QQuickWindow::beforeSynchronizing, this, [this] { QMutexLocker l(&mu_); syncAt_ = clock_.nsecsElapsed(); }, Qt::DirectConnection);
  connect(w, &QQuickWindow::frameSwapped, this, [this] {   // the render thread (threaded loop) or the interface thread
    bool first = false;
    { QMutexLocker l(&mu_); const qint64 now = clock_.nsecsElapsed();
      if (last_ > 0) { const double gap = (now - last_) / 1e6; gaps_.push_back(gap); if (allGaps_.size() < 200000) allGaps_.push_back(gap); }
      if (syncAt_ > 0) { const double wk = (now - syncAt_) / 1e6; work_.push_back(wk); if (allWork_.size() < 200000) allWork_.push_back(wk); }
      last_ = now; if (!first_) { first_ = true; first = true; } }
    if (first) QMetaObject::invokeMethod(this, [this] {
        if (!s_test) { QSettings st = store(); st.remove(kPending); if (inUse() != QLatin1String("software")) st.setValue(kGood, inUse()); st.sync(); }
        emit changed(); }, Qt::QueuedConnection);
  }, Qt::DirectConnection);
  connect(w, &QQuickWindow::sceneGraphInitialized, this, [this] {   // the render thread, with the context current
    if (auto* c = QOpenGLContext::currentContext()) { const auto* r = reinterpret_cast<const char*>(c->functions()->glGetString(GL_RENDERER)); const auto* v = reinterpret_cast<const char*>(c->functions()->glGetString(GL_VERSION));
      const QString d = QStringLiteral("%1, OpenGL %2").arg(QString::fromLatin1(r ? r : "?"), QString::fromLatin1(v ? v : "?"));
      QMetaObject::invokeMethod(this, [this, d] { driver_ = s_driver.isEmpty() ? d : s_driver + QStringLiteral(" · ") + d; emit changed(); }, Qt::QueuedConnection); }
  }, Qt::DirectConnection);
  connect(w, &QQuickWindow::sceneGraphError, this, [this](QQuickWindow::SceneGraphError, const QString& msg) {
    qWarning("graphics: %s could not start: %s", qPrintable(inUse()), qPrintable(msg));
    if (s_test) return;
    relaunch();   // "gfx/pending" is still set: the new start takes SOFTWARE SAFE MODE and says why
  }, Qt::QueuedConnection);
  connect(w, &QWindow::screenChanged, this, [this](QScreen*) { recompute(); emit changed(); });
  stats_.start(); recompute();
}

void Graphics::recompute() {
  QScreen* sc = win_ ? win_->screen() : QGuiApplication::primaryScreen();
  refresh_ = sc && sc->refreshRate() > 1 ? sc->refreshRate() : 60; dpr_ = win_ ? win_->devicePixelRatio() : sc ? sc->devicePixelRatio() : 1;
  const QString mode = perfMode(); const int threads = QThread::idealThreadCount(); bool onBattery = false;
#ifdef _WIN32
  SYSTEM_POWER_STATUS ps{}; if (GetSystemPowerStatus(&ps)) onBattery = ps.ACLineStatus == 0;
#endif
  const bool weak = threads > 0 && threads <= 4;
  battery_ = mode == QLatin1String("battery") || (mode == QLatin1String("auto") && (weak || onBattery));
  const int hz = battery_ ? 30 : std::clamp(static_cast<int>(std::lround(refresh_)), 24, 60);
  perfNote_ = mode == QLatin1String("battery") ? QStringLiteral("BATTERY: meters at 30 fps, moving backgrounds off")
            : mode == QLatin1String("smooth") ? QStringLiteral("SMOOTH: meters at %1 fps (the monitor runs at %2 Hz)").arg(hz).arg(refresh_, 0, 'f', 0)
            : battery_ ? QStringLiteral("AUTO: %1, so meters at 30 fps and moving backgrounds off").arg(onBattery ? QStringLiteral("running on battery") : QStringLiteral("a processor with %1 threads").arg(threads))
                       : QStringLiteral("AUTO: meters at %1 fps (the monitor runs at %2 Hz, %3 processor threads, mains power)").arg(hz).arg(refresh_, 0, 'f', 0).arg(threads);
  if (hz != meterHz_) { meterHz_ = hz; if (onMeterHz && !s_test) onMeterHz(hz); emit changed(); }
}

void Graphics::sample() {
  std::vector<double> g, wk; { QMutexLocker l(&mu_); g.swap(gaps_); wk.swap(work_); }
  fps_ = static_cast<double>(g.size()); frameMs_ = mean(wk); p95_ = pct(wk, 0.95); max_ = wk.empty() ? 0 : *std::max_element(wk.begin(), wk.end());
  emit statsChanged();
}
void Graphics::resetFrames() { QMutexLocker l(&mu_); allGaps_.clear(); allWork_.clear(); last_ = 0; syncAt_ = 0; }
QVariantMap Graphics::frameStats() const {
  std::vector<double> g, wk; { QMutexLocker l(&mu_); g = allGaps_; wk = allWork_; }
  return {{QStringLiteral("frames"), static_cast<int>(wk.size())}, {QStringLiteral("gapMeanMs"), mean(g)}, {QStringLiteral("gapP95Ms"), pct(g, 0.95)}, {QStringLiteral("gapMaxMs"), g.empty() ? 0.0 : *std::max_element(g.begin(), g.end())},
          {QStringLiteral("workMeanMs"), mean(wk)}, {QStringLiteral("workP95Ms"), pct(wk, 0.95)}, {QStringLiteral("workMaxMs"), wk.empty() ? 0.0 : *std::max_element(wk.begin(), wk.end())}};
}
QVariantMap Graphics::report() const {
  return {{QStringLiteral("choice"), choice()}, {QStringLiteral("inUse"), inUse()}, {QStringLiteral("safeMode"), s_safe}, {QStringLiteral("note"), s_note}, {QStringLiteral("gpu"), s_gpu}, {QStringLiteral("driver"), driver()},
          {QStringLiteral("refreshHz"), refresh_}, {QStringLiteral("dpr"), dpr_}, {QStringLiteral("renderLoop"), renderLoop()}, {QStringLiteral("perfMode"), perfMode()}, {QStringLiteral("perfInUse"), perfInUse()},
          {QStringLiteral("meterHz"), meterHz_}, {QStringLiteral("heavyEffects"), heavyEffects()}, {QStringLiteral("fps"), fps_}, {QStringLiteral("frameMs"), frameMs_}};
}
