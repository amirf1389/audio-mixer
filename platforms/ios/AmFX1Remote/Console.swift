import Foundation
import Security

// The AmF X1 remote protocol (engine/cpp/app/daemon): POST /api/pair {pin,name} -> {token}; every other call carries X-Amix-Token;
// GET /api/state, POST /api/control {op,...}; GET /api/inserts, /api/racks?strip=N; live push on ws://HOST/ws/meters?token=&client=.
struct Strip: Identifiable, Decodable { let id: Int; var name: String?; var db: Double; var mute: Bool; var solo: Bool? }
struct Master: Decodable { var db: Double; var mute: Bool }
struct MixState: Decodable { var strips: [Strip]; var master: Master }
struct Plugin: Identifiable, Decodable { let id: String; let name: String }
struct Slot: Identifiable, Decodable { var id: Int { slot }; let slot: Int; let name: String?; let bypass: Bool? }

@MainActor
final class Console: ObservableObject {
    @Published var base: URL? = UserDefaults.standard.url(forKey: "base")
    @Published var token: String? = Keychain.read()
    @Published var state: MixState?
    @Published var note = ""
    var paired: Bool { base != nil && token != nil }
    private var ws: URLSessionWebSocketTask?
    private var poll: Timer?

    // PAIRING from the QR code (http://HOST:PORT/remote#pin=123456) or typed by hand
    func pair(link: String, pinOverride: String? = nil) async {
        guard var comps = URLComponents(string: link.trimmingCharacters(in: .whitespaces)), comps.host != nil else { note = "not an AmF X1 address"; return }
        let pin = pinOverride ?? comps.fragment.flatMap { f in f.hasPrefix("pin=") ? String(f.dropFirst(4)) : nil } ?? ""
        comps.path = ""; comps.fragment = nil; comps.query = nil
        guard let b = comps.url, pin.count >= 4 else { note = "the PIN is missing"; return }
        do {
            let j = try await call(b, "/api/pair", ["pin": pin, "name": "iPhone / iPad (app)"], tok: nil)
            guard let t = j["token"] as? String else { note = (j["error"] as? String) ?? "pairing refused"; return }
            Keychain.write(t); token = t; base = b; UserDefaults.standard.set(b, forKey: "base"); note = ""
            start()
        } catch { note = error.localizedDescription }
    }
    func forget() { Keychain.write(nil); token = nil; ws?.cancel(); poll?.invalidate(); state = nil }

    func start() {
        guard let b = base, let t = token else { return }
        Task { await refresh() }
        var c = URLComponents(url: b, resolvingAgainstBaseURL: false)!
        c.scheme = b.scheme == "https" ? "wss" : "ws"; c.path = "/ws/meters"
        c.queryItems = [URLQueryItem(name: "token", value: t), URLQueryItem(name: "client", value: "iOS app")]
        ws = URLSession.shared.webSocketTask(with: c.url!); ws?.resume(); receive()
        poll?.invalidate()
        poll = Timer.scheduledTimer(withTimeInterval: 3, repeats: true) { [weak self] _ in Task { await self?.refresh() } }   // covers a stream that is down
    }
    private func receive() {
        ws?.receive { [weak self] r in
            Task { @MainActor in
                guard let self else { return }
                if case .success(.string(let s)) = r, s.contains("\"strips\""), let d = s.data(using: .utf8), let st = try? JSONDecoder().decode(MixState.self, from: d) { self.state = st }
                if case .failure = r { DispatchQueue.main.asyncAfter(deadline: .now() + 2) { self.start() }; return }
                self.receive()
            }
        }
    }
    func refresh() async {
        guard let b = base else { return }
        do { let d = try await raw(b, "/api/state", nil, tok: token); state = try JSONDecoder().decode(MixState.self, from: d) } catch { note = error.localizedDescription }
    }
    func control(_ cmd: [String: Any]) { guard let b = base else { return }
        Task { do { let j = try await call(b, "/api/control", cmd, tok: token); if let e = j["error"] as? String { note = e } } catch { note = error.localizedDescription } } }
    func inserts() async -> [Plugin] {
        guard let b = base, let d = try? await raw(b, "/api/inserts", nil, tok: token) else { return [] }
        struct R: Decodable { let plugins: [Plugin]? }; return (try? JSONDecoder().decode(R.self, from: d).plugins) ?? []
    }
    func slots(strip: Int) async -> [Slot] {
        guard let b = base, let d = try? await raw(b, "/api/racks?strip=\(strip)", nil, tok: token) else { return [] }
        struct Rack: Decodable { let slots: [Slot] }; struct R: Decodable { let racks: [Rack]? }
        return (try? JSONDecoder().decode(R.self, from: d).racks?.first?.slots) ?? []
    }

    private func raw(_ b: URL, _ path: String, _ body: [String: Any]?, tok: String?) async throws -> Data {
        var r = URLRequest(url: URL(string: path, relativeTo: b)!); r.timeoutInterval = 5
        r.setValue("application/json", forHTTPHeaderField: "Content-Type")
        if let tok { r.setValue(tok, forHTTPHeaderField: "X-Amix-Token") }
        if let body { r.httpMethod = "POST"; r.httpBody = try JSONSerialization.data(withJSONObject: body) }
        let (d, resp) = try await URLSession.shared.data(for: r)
        if (resp as? HTTPURLResponse)?.statusCode == 401, tok != nil { forget(); note = "this device was signed out on the computer" }
        return d
    }
    private func call(_ b: URL, _ path: String, _ body: [String: Any]?, tok: String?) async throws -> [String: Any] {
        (try JSONSerialization.jsonObject(with: try await raw(b, path, body, tok: tok)) as? [String: Any]) ?? [:]
    }
}

// the token lives in the Keychain (this device only), never in UserDefaults
enum Keychain {
    static let q: [String: Any] = [kSecClass as String: kSecClassGenericPassword, kSecAttrService as String: "amfx1.remote", kSecAttrAccount as String: "token"]
    static func read() -> String? {
        var o: AnyObject?; var a = q; a[kSecReturnData as String] = true
        guard SecItemCopyMatching(a as CFDictionary, &o) == errSecSuccess, let d = o as? Data else { return nil }
        return String(data: d, encoding: .utf8)
    }
    static func write(_ s: String?) {
        SecItemDelete(q as CFDictionary)
        guard let s else { return }
        var a = q; a[kSecValueData as String] = Data(s.utf8); a[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
        SecItemAdd(a as CFDictionary, nil)
    }
}
