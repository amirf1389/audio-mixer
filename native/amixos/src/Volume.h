// VOLUME CONTROL beside the Windows volume mixer: the tray icon (quick master slider, mute, open the console), the volume keys (the
// keyboard's media keys when taken, and the hotkeys Ctrl+Alt+Up / Down / M) sent to the console's master or to the Windows volume, and
// PER-APP VOLUME: every program Windows plays, with its slider and mute, kept two-way with the Windows volume mixer.
//
// The program list comes from am::vol (engine/cpp/include/audiomixer/app_volume.hpp). Windows reports a change on a thread of its own;
// here it is carried to the interface thread (queued), so QML only ever sees the list change on its own thread. The list is read once,
// then again only when Windows says a program came or went (several reports in a row are folded into one read). A slider drag sends
// one write per frame per program, the newest value only; the console's own writes are not taken back as moves (no echo, no drift).
//
// Other systems: no programs (appsAvailable false), the hotkeys are not registered, the tray where the platform has one.
#pragma once
#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QTimer>
#include <QVariantList>
#include <memory>

#include "audiomixer/app_volume.hpp"

class Engine;
class Apps;
class QSystemTrayIcon;
class QMenu;

class Volume : public QObject, public QAbstractNativeEventFilter {
  Q_OBJECT
  Q_PROPERTY(QVariantList apps READ apps NOTIFY appsChanged)            // [{id, name, pid, volume 0..100, mute, system, icon, strips: [captured strip ...]}]
  Q_PROPERTY(bool appsAvailable READ appsAvailable NOTIFY appsChanged)
  Q_PROPERTY(QStringList appIds READ appIds NOTIFY appIdsChanged)        // the programs' ids only: changes when a program comes or goes (the rows stay while a slider moves)
  Q_PROPERTY(int target READ target WRITE setTarget NOTIFY settingsChanged)        // the volume keys move: 0 the console's master, 1 the Windows volume
  Q_PROPERTY(int stepDb READ stepDb WRITE setStepDb NOTIFY settingsChanged)        // 1, 2 or 3 dB a press (Windows: 2 % per dB)
  Q_PROPERTY(bool mediaKeys READ mediaKeys WRITE setMediaKeys NOTIFY settingsChanged)   // take the keyboard's volume keys (off: Windows and other programs keep them)
  Q_PROPERTY(bool hotkeys READ hotkeys WRITE setHotkeys NOTIFY settingsChanged)    // Ctrl+Alt+Up / Down / M anywhere
  Q_PROPERTY(bool tray READ tray WRITE setTray NOTIFY settingsChanged)             // the icon in the notification area
  Q_PROPERTY(QString keysNote READ keysNote NOTIFY settingsChanged)                // which keys Windows gave us, or why not
  Q_PROPERTY(bool trayAvailable READ trayAvailable CONSTANT)
 public:
  explicit Volume(Engine* engine, Apps* apps, QObject* parent = nullptr);
  ~Volume() override;
  /** the session layer (tests and the screenshots: am::vol::MemoryLayer); the console starts with this computer's own */
  void setLayer(std::unique_ptr<am::vol::SessionLayer> layer);
  am::vol::SessionLayer* layer() const { return layer_.get(); }

  QVariantList apps() const; bool appsAvailable() const; QStringList appIds() const { return ids_; }
  Q_INVOKABLE QVariantMap app(const QString& id) const;   // one row of apps (empty when gone)
  int target() const { return static_cast<int>(cfg_.target); } void setTarget(int t);
  int stepDb() const { return cfg_.stepDb; } void setStepDb(int s);
  bool mediaKeys() const { return cfg_.mediaKeys; } void setMediaKeys(bool on);
  bool hotkeys() const { return cfg_.hotkeys; } void setHotkeys(bool on);
  bool tray() const { return tray_; } void setTray(bool on);
  QString keysNote() const { return keysNote_; }
  bool trayAvailable() const;

  Q_INVOKABLE void setAppVolume(const QString& id, double percent);   // coalesced: one write per frame per program
  Q_INVOKABLE void setAppMute(const QString& id, bool on);
  Q_INVOKABLE void refreshApps();
  Q_INVOKABLE void stepMaster(int dir);          // one press on the chosen target (the master at -inf / muted: up un-mutes first)
  Q_INVOKABLE void toggleMute();
  /** a key press as if it came from the keyboard: key 0 up, 1 down, 2 mute; media = the keyboard's own volume key. false: not ours */
  Q_INVOKABLE bool press(int key, bool media);
  Q_INVOKABLE QString dbText(double db) const;
  Q_INVOKABLE double stepped(double db, int dir) const;
  /** an arrow key: Up / Down the master, Ctrl the selected channel, Shift 0.1 dB (else 1 dB); false with a text field focused or another key */
  Q_INVOKABLE bool arrowKey(int key, int modifiers, bool textFocus);
  static bool textHasFocus();
  bool eventFilter(QObject* o, QEvent* e) override;   // the master after one step (the header knob's wheel)
  bool trayShown() const;                                       // the icon is in the notification area now (a hidden window can come back)
  void showMessage(const QString& title, const QString& text);  // a balloon from the tray icon
  void quiet();   // test runs: no tray icon, no keys taken from the system
  void flush();   // pending slider writes now (tests)
  int writes() const { return writes_; }   // writes handed to the layer (tests: the coalescing)

  bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override;

 signals:
  void appsChanged();
  void appIdsChanged();
  void settingsChanged();
  void popupRequested(int x, int y);   // the tray icon was clicked: the quick volume popup near (x, y)
  void openConsole();                  // the tray's OPEN CONSOLE
  void quitRequested();

 private:
  void notify(); QStringList ids_;
  void wire(); void readList(); void registerKeys(); void unregisterKeys(); void makeTray(); void save() const;
  Engine* engine_; Apps* capture_; std::unique_ptr<am::vol::SessionLayer> layer_; am::vol::SessionList list_;
  am::vol::KeyConfig cfg_; bool tray_ = true; QString keysNote_;
  QHash<QString, float> pendingVol_; QTimer sendT_, listT_; int writes_ = 0; bool listQueued_ = false;
  QSystemTrayIcon* icon_ = nullptr; QMenu* menu_ = nullptr; bool keysOn_ = false, quiet_ = false;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};
