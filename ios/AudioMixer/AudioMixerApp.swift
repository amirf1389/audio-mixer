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
            Group {
                if let url = model.url { WebView(url: url) } else { Color.black }
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
    private var server: LocalServer?
    static let port: UInt16 = 47831

    func start() {
        guard server == nil else { return }
        UIApplication.shared.isIdleTimerDisabled = true            // a mixer on stage must not go to sleep
        configureAudioSession()
        guard let www = Bundle.main.url(forResource: "www", withExtension: nil) else { return }
        let s = LocalServer(root: www)
        server = s
        s.start(preferredPort: AppModel.port) { [weak self] port in
            self?.url = URL(string: "http://127.0.0.1:\(port)/index.html")
        }
    }

    /// Play and record together, through the speaker or a Bluetooth / USB interface, also with the silent switch on.
    private func configureAudioSession() {
        let session = AVAudioSession.sharedInstance()
        try? session.setCategory(.playAndRecord, mode: .default, options: [.defaultToSpeaker, .allowBluetooth, .allowBluetoothA2DP])
        try? session.setActive(true)
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
