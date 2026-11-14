// PLUG-IN BUILDER: design your own VST3 plug-in from AMFB effect blocks and export it for any DAW (engine/cpp/app/plugin_shell).
// A design: name, vendor, category, up to 6 blocks in series, each knob either FIXED at a value or EXPOSED to the DAW (up to 16, with your
// title), and a faceplate layout (where each exposed knob sits on the AMFB panel). Designs are kept as JSON in <data>/plugin-designs/.
// EXPORT writes <Name>.vst3 (the AMFB Plugin Shell with the design inside) to the per-user VST3 folder, where DAWs and this console find it.
#pragma once
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QTimer>
#include <memory>
namespace am::shell { class Preview; }


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
  // PREVIEW: hear the design live on the sound card (the program's own engine): a source through the same chain the plug-in will run
  Q_PROPERTY(bool previewOn READ previewOn NOTIFY previewChanged)
  Q_PROPERTY(int previewSource READ previewSource NOTIFY previewChanged)     // 0 sample, 1 pink noise, 2 sweep, 3 microphone, 4 file
  Q_PROPERTY(int previewSample READ previewSample NOTIFY previewChanged)
  Q_PROPERTY(bool previewBypass READ previewBypass NOTIFY previewChanged)
  Q_PROPERTY(double previewGainDb READ previewGainDb NOTIFY previewChanged)
  Q_PROPERTY(QString previewFile READ previewFile NOTIFY previewChanged)
  Q_PROPERTY(QString previewNote READ previewNote NOTIFY previewChanged)
  Q_PROPERTY(QVariantList samples READ samples CONSTANT)                      // [{id, name, category}] the 100 built-in samples
  Q_PROPERTY(double previewInDb READ previewInDb NOTIFY metersChanged)
  Q_PROPERTY(double previewOutDb READ previewOutDb NOTIFY metersChanged)
 public:
  explicit Builder(QObject* engine, QObject* parent = nullptr);
  ~Builder() override;   // the console's Engine (called by name, so the standalone builder needs none), or nullptr
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
  bool previewOn() const; int previewSource() const { return pvSource_; } int previewSample() const { return pvSample_; } bool previewBypass() const { return pvBypass_; }
  double previewGainDb() const { return pvGain_; } QString previewFile() const { return pvFile_; } QString previewNote() const { return pvNote_; }
  QVariantList samples() const; double previewInDb() const { return inDb_; } double previewOutDb() const { return outDb_; }
  Q_INVOKABLE bool previewPlay(bool on);
  Q_INVOKABLE void setPreviewSource(int s);
  Q_INVOKABLE void setPreviewSample(int id);
  Q_INVOKABLE bool setPreviewFile(const QString& pathOrUrl);
  Q_INVOKABLE void setPreviewBypass(bool b);
  Q_INVOKABLE void setPreviewGainDb(double db);
  void enablePreview(const QString& backend = QStringLiteral("auto"));   // the builder program turns it on (the console has its own mixer)
 signals:
  void changed(); void designsChanged(); void previewChanged(); void metersChanged();
 private:
  struct Knob { QString id, title, label; double value = 0.5; bool exposed = false; double x = -1, y = -1; };
  struct Blk { QString kind, name; QList<Knob> knobs; };
  QString folder() const; QString shellPath() const;
  QObject* engine_; QString name_, vendor_, category_ = QStringLiteral("Fx"), color_ = QStringLiteral("#EA580C"), uid_, note_, lastExport_, file_;
  QList<Blk> blocks_;
  void pushDesign();
  std::unique_ptr<am::shell::Preview> pv_; QString pvBackend_ = QStringLiteral("auto"), pvFile_, pvNote_; int pvSource_ = 0, pvSample_ = 1; bool pvBypass_ = false; double pvGain_ = 0, inDb_ = -120, outDb_ = -120; QTimer meters_;
};
