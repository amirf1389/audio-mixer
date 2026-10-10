import AVFoundation
import SwiftUI
import UIKit
import WebKit

/// Audio Mixer for iOS: the mixer page (index.html, WING TITAN STAGE theme) in a full-screen WKWebView, bundled offline in the app.
/// Web Audio, the microphone and the license / plans work as in the browser; PC-mode features (ASIO, WASAPI, plugins) need the PC.
@main
struct AudioMixerApp: App {
    @StateObject private var model = AppModel()

    var body: some Scene {
        WindowGroup {
            ZStack {
                if let url = model.url { WebView(url: url) } else { Color.black }
                if !model.booted { BootView().transition(.opacity) }
            }
            .ignoresSafeArea()
            .statusBarHidden(true)
            .preferredColorScheme(.dark)
            .onAppear { model.start() }
        }
    }
}

final class AppModel: ObservableObject {
    @Published var url: URL?
    @Published var booted = false                                   // the start-up screen is shown until the page is served and the animation has had its time
    private var server: LocalServer?
    private let started = Date()
    static let port: UInt16 = 47831

    func start() {
        guard server == nil else { return }
        UIApplication.shared.isIdleTimerDisabled = true            // a mixer on stage must not go to sleep
        configureAudioSession()
        guard let www = Bundle.main.url(forResource: "www", withExtension: nil) else { return }
        DispatchQueue.main.asyncAfter(deadline: .now() + 8) { [weak self] in self?.booted = true }   // never stay on the start-up screen
        let s = LocalServer(root: www)
        server = s
        s.start(preferredPort: AppModel.port) { [weak self] port in
            self?.url = URL(string: "http://127.0.0.1:\(port)/index.html")
            let wait = max(0.4, 2.2 - Date().timeIntervalSince(self?.started ?? Date()))   // the page loads under the screen; its own power-on sequence follows
            DispatchQueue.main.asyncAfter(deadline: .now() + wait) { withAnimation(.easeOut(duration: 0.5)) { self?.booted = true } }
        }
    }

    /// Play and record together, through the speaker or a Bluetooth / USB interface, also with the silent switch on.
    private func configureAudioSession() {
        let session = AVAudioSession.sharedInstance()
        try? session.setCategory(.playAndRecord, mode: .default, options: [.defaultToSpeaker, .allowBluetooth, .allowBluetoothA2DP])
        try? session.setActive(true)
    }
}

/// Start-up screen: power LED (red, amber, green), the logo spelling in, the OS boot lines ticking to OK and a progress bar, like the console's power-on.
struct BootView: View {
    private let start = Date()
    private let lines = ["TITAN OS", "DSP CORE", "AUDIO ENGINE", "I/O", "FADERS", "CONSOLE"]

    private func led(_ e: Double) -> Color {
        e < 0.5 ? Color(red: 0.94, green: 0.27, blue: 0.27) : e < 1.0 ? Color(red: 0.96, green: 0.62, blue: 0.04) : Color(red: 0.13, green: 0.77, blue: 0.37)
    }

    var body: some View {
        TimelineView(.animation) { context in
            let e = context.date.timeIntervalSince(start)
            let letters = Array("AUDIO MIXER")
            let shown = max(0, min(letters.count, Int((e - 0.5) / 0.06)))
            ZStack {
                Color.black
                VStack(spacing: 18) {
                    Circle().fill(led(e)).frame(width: 14, height: 14).shadow(color: led(e), radius: 9)
                    Text(String(letters.prefix(shown)))
                        .font(.system(size: 26, weight: .black, design: .monospaced)).kerning(7).foregroundColor(.white)
                        .frame(height: 34)
                    Text("TITAN STAGE").font(.system(size: 9, weight: .bold, design: .monospaced)).kerning(5).foregroundColor(.gray)
                    VStack(spacing: 3) {
                        ForEach(0..<lines.count, id: \.self) { i in
                            HStack {
                                Text(lines[i]).foregroundColor(Color(white: 0.6))
                                Spacer()
                                Text("OK").foregroundColor(.green).bold()
                            }
                            .opacity(e > 0.9 + Double(i) * 0.22 ? 1 : 0)
                        }
                    }
                    .font(.system(size: 11, design: .monospaced)).frame(width: 240)
                    ZStack(alignment: .leading) {
                        Capsule().fill(Color(white: 0.1)).frame(width: 240, height: 3)
                        Capsule().fill(Color.green).frame(width: 240 * CGFloat(min(1, e / 2.2)), height: 3)
                    }
                }
            }
        }
    }
}

struct WebView: UIViewRepresentable {
    let url: URL

    func makeCoordinator() -> Coordinator { Coordinator() }

    func makeUIView(context: Context) -> WKWebView {
        let config = WKWebViewConfiguration()
        config.allowsInlineMediaPlayback = true
        config.mediaTypesRequiringUserActionForPlayback = []
        config.applicationNameForUserAgent = "AudioMixeriOS/" + (Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "0")
        let view = WKWebView(frame: .zero, configuration: config)
        view.uiDelegate = context.coordinator
        view.navigationDelegate = context.coordinator
        view.isOpaque = false
        view.backgroundColor = .black
        view.scrollView.backgroundColor = .black
        view.scrollView.bounces = false
        view.scrollView.contentInsetAdjustmentBehavior = .never
        view.load(URLRequest(url: url))
        return view
    }

    func updateUIView(_ view: WKWebView, context: Context) {}

    final class Coordinator: NSObject, WKUIDelegate, WKNavigationDelegate {
        /// Only the bundled page may use the microphone; no camera.
        @available(iOS 15.0, *)
        func webView(_ webView: WKWebView, requestMediaCapturePermissionFor origin: WKSecurityOrigin, initiatedByFrame frame: WKFrameInfo,
                     type: WKMediaCaptureType, decisionHandler: @escaping (WKPermissionDecision) -> Void) {
            decisionHandler(origin.host == "127.0.0.1" && type == .microphone ? .grant : .deny)
        }

        /// The mixer never navigates away from its own page: links open in the browser.
        func webView(_ webView: WKWebView, decidePolicyFor navigationAction: WKNavigationAction, decisionHandler: @escaping (WKNavigationActionPolicy) -> Void) {
            guard let url = navigationAction.request.url else { decisionHandler(.cancel); return }
            if url.host == "127.0.0.1" || url.scheme == "about" || url.scheme == "blob" || url.scheme == "data" { decisionHandler(.allow); return }
            if navigationAction.navigationType == .linkActivated { UIApplication.shared.open(url) }
            decisionHandler(.cancel)
        }
    }
}
