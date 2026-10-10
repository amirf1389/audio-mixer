import QtQuick
import QtQuick.Window
import QtWebView

// The mixer page in the system WebView, served by MixerServer on 127.0.0.1 (context properties: mixerPort, appVersion).
Window {
    id: root
    visible: true
    width: 1280
    height: 800
    title: "Audio Mixer " + appVersion
    color: "#05070a"

    WebView {
        id: view
        anchors.fill: parent
        url: mixerPort > 0 ? "http://localhost:" + mixerPort + "/index.html" : ""
        onLoadingChanged: function (request) {
            if (request.status === WebView.LoadFailedStatus)
                console.warn("page failed:", request.errorString)
        }
    }

    Text {
        anchors.centerIn: parent
        visible: mixerPort === 0
        color: "#e11d48"
        font.pixelSize: 18
        text: "The local mixer server could not start (ports 8765-8774 are busy)."
    }
}
