// LEVELS: every strip and the master, big meters with peak / RMS / hold / clip
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Layouts

Item {
    id: page
    // HIDDEN IN THE PAGE CACHE: the meter tick is held while the page is not shown (30 to 60 meter updates a second re-ran every binding
    // here behind the shown page); shown again, it takes the engine's tick at once
    property int mTick: 0
    Binding on mTick { when: page.visible; value: engine.meterTick; restoreMode: Binding.RestoreNone }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Panel {
            Layout.fillWidth: true; Layout.fillHeight: true
            title: i18n.t("LEVELS", i18n.lang)
            subtitle: i18n.t("Peak (white line), RMS (bar), hold (amber), clip lamp. The master meters follow the limiter.", i18n.lang)
            RowLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 3
                Repeater {
                    model: engine.strips
                    delegate: ColumnLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 3
                        property var mt: (page.mTick, engine.meterAt(index))
                        MeterBar { Layout.fillHeight: true; Layout.fillWidth: true; Layout.topMargin: 10; peakDb: mt ? mt.peakDb : -120; rmsDb: mt ? mt.rmsDb : -120; holdDb: mt ? mt.holdDb : -120; clip: mt ? mt.clip : false }
                        Text { Layout.alignment: Qt.AlignHCenter; text: (index + 1); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
                        Text { Layout.alignment: Qt.AlignHCenter; text: mt && mt.peakDb > -60 ? mt.peakDb.toFixed(0) : ""; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7 } }
                }
                Rectangle { Layout.fillHeight: true; width: 1; color: Theme.line; Layout.leftMargin: 6; Layout.rightMargin: 6 }
                ColumnLayout { Layout.fillHeight: true; spacing: 3
                    RowLayout { Layout.fillHeight: true; spacing: 4
                        MeterBar { Layout.fillHeight: true; implicitWidth: 18; Layout.topMargin: 10; peakDb: engine.telv("masterLDb", page.mTick); rmsDb: engine.telv("masterRmsDb", page.mTick); clip: engine.telv("masterClip", page.mTick) }
                        MeterBar { Layout.fillHeight: true; implicitWidth: 18; Layout.topMargin: 10; peakDb: engine.telv("masterRDb", page.mTick); rmsDb: engine.telv("masterRmsDb", page.mTick); clip: engine.telv("masterClip", page.mTick) } }
                    Text { Layout.alignment: Qt.AlignHCenter; text: i18n.t("MASTER", i18n.lang); color: Theme.orange; font.family: Theme.mono; font.pixelSize: 7; font.bold: true }
                    Text { Layout.alignment: Qt.AlignHCenter; text: Theme.dbText(engine.telv("masterPeakDb", page.mTick)); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8 } }
            }
        }
    }
}
