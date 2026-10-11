import Foundation
import Network

/// Serves the bundled mixer page (the `www` folder of the app) on http://127.0.0.1:<port>/ and nothing else.
/// WebKit treats 127.0.0.1 as a secure context, so Web Audio worklets, the microphone and WebCrypto (license keys) work; a file:// page would not get them.
/// Only GET / HEAD, only files inside `root`, only reachable from this device (loopback interface).
final class LocalServer {
    private var listener: NWListener?
    private let root: URL
    private let queue = DispatchQueue(label: "com.audiomixer.server")
    private static let types: [String: String] = [
        "html": "text/html; charset=utf-8", "js": "text/javascript; charset=utf-8", "css": "text/css; charset=utf-8", "json": "application/json",
        "png": "image/png", "svg": "image/svg+xml", "ico": "image/x-icon", "woff2": "font/woff2", "woff": "font/woff", "ttf": "font/ttf",
    ]

    init(root: URL) { self.root = root.standardizedFileURL }

    /// The same port every launch (so localStorage, which belongs to the origin, keeps the scenes and the license key); another one if it is taken.
    func start(preferredPort: UInt16, ready: @escaping (UInt16) -> Void) {
        begin(port: NWEndpoint.Port(rawValue: preferredPort), ready: ready, fallback: true)
    }

    func stop() { listener?.cancel(); listener = nil }

    private func begin(port: NWEndpoint.Port?, ready: @escaping (UInt16) -> Void, fallback: Bool) {
        let params = NWParameters.tcp
        params.requiredInterfaceType = .loopback
        let created: NWListener?
        if let port = port { created = try? NWListener(using: params, on: port) } else { created = try? NWListener(using: params) }
        guard let l = created else {
            if fallback { begin(port: nil, ready: ready, fallback: false) }
            return
        }
        l.stateUpdateHandler = { [weak self, weak l] state in
            switch state {
            case .ready:
                if let p = l?.port?.rawValue { DispatchQueue.main.async { ready(p) } }
            case .failed:
                l?.cancel()
                if fallback { self?.begin(port: nil, ready: ready, fallback: false) }
            default:
                break
            }
        }
        l.newConnectionHandler = { [weak self] connection in self?.accept(connection) }
        l.start(queue: queue)
        listener = l
    }

    private func accept(_ c: NWConnection) {
        c.start(queue: queue)
        receive(c, Data())
    }

    private func receive(_ c: NWConnection, _ acc: Data) {
        c.receive(minimumIncompleteLength: 1, maximumLength: 16384) { [weak self] data, _, isComplete, error in
            guard let self = self else { c.cancel(); return }
            var buf = acc
            if let d = data { buf.append(d) }
            if let end = buf.range(of: Data("\r\n\r\n".utf8)) {
                self.respond(c, head: String(decoding: buf[buf.startIndex..<end.lowerBound], as: UTF8.self))
            } else if error != nil || isComplete || buf.count > 32768 {
                c.cancel()
            } else {
                self.receive(c, buf)
            }
        }
    }

    private func respond(_ c: NWConnection, head: String) {
        let parts = (head.components(separatedBy: "\r\n").first ?? "").split(separator: " ")
        guard parts.count >= 2 else { return send(c, 400, "Bad Request", Data(), "text/plain", head: false) }
        let method = String(parts[0])
        guard method == "GET" || method == "HEAD" else { return send(c, 405, "Method Not Allowed", Data(), "text/plain", head: false) }
        var path = String(parts[1])
        if let q = path.firstIndex(where: { $0 == "?" || $0 == "#" }) { path = String(path[path.startIndex..<q]) }
        path = path.removingPercentEncoding ?? path
        if path == "/" { path = "/index.html" }
        if path.contains("..") || path.contains("\0") || !path.hasPrefix("/") { return send(c, 403, "Forbidden", Data(), "text/plain", head: false) }
        let file = root.appendingPathComponent(String(path.dropFirst())).standardizedFileURL
        guard file.path.hasPrefix(root.path + "/"), let body = try? Data(contentsOf: file) else {
            return send(c, 404, "Not Found", Data("not found".utf8), "text/plain", head: method == "HEAD")
        }
        let type = LocalServer.types[file.pathExtension.lowercased()] ?? "application/octet-stream"
        send(c, 200, "OK", body, type, head: method == "HEAD")
    }

    private func send(_ c: NWConnection, _ code: Int, _ text: String, _ body: Data, _ type: String, head: Bool) {
        var header = "HTTP/1.1 \(code) \(text)\r\nContent-Type: \(type)\r\nContent-Length: \(body.count)\r\nCache-Control: no-store\r\n"
        header += "X-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n"
        var out = Data(header.utf8)
        if !head { out.append(body) }
        c.send(content: out, completion: .contentProcessed { _ in c.cancel() })
    }
}
