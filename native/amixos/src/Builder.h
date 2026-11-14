// PLUG-IN BUILDER: design your own VST3 plug-in from AMFB effect blocks and export it for any DAW (engine/cpp/app/plugin_shell).
// A design: name, vendor, category, up to 6 blocks in series, each knob either FIXED at a value or EXPOSED to the DAW (up to 16, with your
// title), and a faceplate layout (where each exposed knob sits on the AMFB panel). Designs are kept as JSON in <data>/plugin-designs/.
// EXPORT writes <Name>.vst3 (the AMFB Plugin Shell with the design inside) to the per-user VST3 folder, where DAWs and this console find it.
#pragma once
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class Engine;

class Builder : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
  Q_PROPERTY(QString vendor READ vendor WRITE setVendor NOTIFY changed)
  Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY changed)
  Q_PROPERTY(QVariantList blocks READ blocks NOTIFY changed)        // [{kind, name, knobs: [{id, title, value, text, exposed, label, x, y}]}]
  Q_PROPERTY(QVariantList exposed READ exposed NOTIFY changed)      // the DAW's knobs in order: [{block, id, label, value, text, x, y}]
  Q_PROPERTY(QVariantList catalog READ catalog CONSTANT)            // [{kind, name}] every AMFB effect a block can be
  Q_PROPERTY(QVariantList designs READ designs NOTIFY designsChanged)   // saved designs [{name, file, blocks, knobs}]
  Q_PROPERTY(QString note READ note NOTIFY changed)
  Q_PROPERTY(QString exportDir READ exportDir CONSTANT)
  Q_PROPERTY(QString lastExport READ lastExport NOTIFY changed)
  Q_PROPERTY(bool shellFound READ shellFound CONSTANT)
  Q_PROPERTY(QString color READ color WRITE setColor NOTIFY changed)   // the faceplate colour
 public:
  explicit Builder(Engine* engine, QObject* parent = nullptr);
  QString name() const { return name_; } void setName(const QString& v) { name_ = v.left(60); emit changed(); }
  QString vendor() const { return vendor_; } void setVendor(const QString& v) { vendor_ = v.left(60); emit changed(); }
  QString category() const { return category_; } void setCategory(const QString& v) { category_ = v; emit changed(); }
  QString color() const { return color_; } void setColor(const QString& v) { color_ = v; emit changed(); }
  QVariantList blocks() const; QVariantList exposed() const; QVariantList catalog() const; QVariantList designs() const;
  QString note() const { return note_; } QString exportDir() const; QString lastExport() const { return lastExport_; } bool shellFound() const;

  Q_INVOKABLE void newDesign();
  Q_INVOKABLE void addBlock(const QString& kind);
  Q_INVOKABLE void removeBlock(int i);
  Q_INVOKABLE void moveBlock(int i, int by);
  Q_INVOKABLE void setKnob(int block, const QString& id, double value);
  Q_INVOKABLE void exposeKnob(int block, const QString& id, bool on);
  Q_INVOKABLE void setKnobLabel(int block, const QString& id, const QString& label);
  Q_INVOKABLE void placeKnob(int block, const QString& id, double x, double y);   // the faceplate position (0..1 of the panel)
  Q_INVOKABLE bool save();
  Q_INVOKABLE bool load(const QString& file);
  Q_INVOKABLE bool remove(const QString& file);
  Q_INVOKABLE QString exportPlugin();                // the new bundle's path, "" on failure (note says why)
  Q_INVOKABLE bool tryOnStrip(int strip);            // export, rescan, insert it on that strip: hear it in the console
  QString designJson() const;                        // for the tests
 signals:
  void changed(); void designsChanged();
 private:
  struct Knob { QString id, title, label; double value = 0.5; bool exposed = false; double x = -1, y = -1; };
  struct Blk { QString kind, name; QList<Knob> knobs; };
  QString folder() const; QString shellPath() const;
  Engine* engine_; QString name_, vendor_, category_ = QStringLiteral("Fx"), color_ = QStringLiteral("#EA580C"), uid_, note_, lastExport_, file_;
  QList<Blk> blocks_;
};
