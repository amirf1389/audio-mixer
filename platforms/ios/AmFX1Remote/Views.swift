import SwiftUI
import AVFoundation

struct PairView: View {
    @EnvironmentObject var console: Console
    @State private var address = ""; @State private var pin = ""; @State private var scanning = false
    var body: some View {
        NavigationStack {
            Form {
                Section("Scan the QR code shown in AmF X1 (REMOTE page)") { Button("Scan QR code") { scanning = true } }
                Section("Or type it") {
                    TextField("http://192.168.1.20:8765", text: $address).keyboardType(.URL).textInputAutocapitalization(.never).autocorrectionDisabled()
                    TextField("PIN", text: $pin).keyboardType(.numberPad)
                    Button("Pair") { Task { await console.pair(link: address, pinOverride: pin) } }.disabled(address.isEmpty || pin.isEmpty)
                }
                if !console.note.isEmpty { Text(console.note).foregroundStyle(.red) }
            }
            .navigationTitle("AmF X1 Remote")
            .sheet(isPresented: $scanning) {
                QRScanner { code in scanning = false; Task { await console.pair(link: code) } }.ignoresSafeArea()
            }
        }
    }
}

struct MixerView: View {
    @EnvironmentObject var console: Console
    var body: some View {
        NavigationStack {
            List {
                if let m = console.state?.master {
                    Section("Master") {
                        FaderRow(title: "MASTER", db: m.db, mute: m.mute, onDb: { console.control(["op": "master", "db": $0]) }, onMute: { console.control(["op": "master", "mute": $0]) })
                    }
                }
                Section("Channels") {
                    ForEach(console.state?.strips ?? []) { s in
                        NavigationLink { InsertsView(strip: s.id, name: s.name ?? "CH \(s.id + 1)") } label: {
                            FaderRow(title: s.name ?? "CH \(s.id + 1)", db: s.db, mute: s.mute,
                                     onDb: { console.control(["op": "gain", "strip": s.id, "db": $0]) }, onMute: { console.control(["op": "mute", "strip": s.id, "on": $0]) })
                        }
                    }
                }
                if !console.note.isEmpty { Text(console.note).font(.footnote).foregroundStyle(.secondary) }
            }
            .navigationTitle("AmF X1")
            .toolbar { Button("Forget") { console.forget() } }
            .onAppear { console.start() }
        }
    }
}

struct FaderRow: View {
    let title: String; let db: Double; let mute: Bool; let onDb: (Double) -> Void; let onMute: (Bool) -> Void
    @State private var v: Double = 0; @State private var last = Date.distantPast
    var body: some View {
        VStack(alignment: .leading) {
            HStack { Text(title).bold(); Spacer(); Text(String(format: "%.1f dB", v)).monospacedDigit()
                Button(mute ? "MUTED" : "MUTE") { onMute(!mute) }.buttonStyle(.borderedProminent).tint(mute ? .red : .gray) }
            Slider(value: $v, in: -60...10, step: 0.5) { _ in }.onChange(of: v) { nv in
                if Date().timeIntervalSince(last) > 0.025 { last = Date(); onDb(nv) } }   // at most 40 sends a second, like the web remote
        }
        .onAppear { v = db }.onChange(of: db) { v = $0 }
    }
}

struct InsertsView: View {
    @EnvironmentObject var console: Console
    let strip: Int; let name: String
    @State private var slots: [Slot] = []; @State private var catalog: [Plugin] = []
    var body: some View {
        List {
            Section("Inserts") {
                ForEach(slots) { s in
                    HStack { Text("\(s.slot + 1). \(s.name ?? "?")"); Spacer()
                        Button(s.bypass == true ? "OFF" : "ON") { console.control(["op": "plugin", "strip": strip, "slot": s.slot, "bypass": !(s.bypass ?? false)]); reload() }
                        Button(role: .destructive) { console.control(["op": "insert", "strip": strip, "action": "remove", "slot": s.slot]); reload() } label: { Image(systemName: "trash") }
                    }
                }
            }
            Section("Add") { ForEach(catalog) { p in Button(p.name) { console.control(["op": "insert", "strip": strip, "action": "add", "id": p.id]); reload() } } }
        }
        .navigationTitle(name)
        .task { catalog = await console.inserts(); slots = await console.slots(strip: strip) }
    }
    private func reload() { Task { try? await Task.sleep(nanoseconds: 300_000_000); slots = await console.slots(strip: strip) } }
}

// QR PAIRING through the camera (AVCaptureMetadataOutput)
struct QRScanner: UIViewControllerRepresentable {
    let found: (String) -> Void
    func makeUIViewController(context: Context) -> ScannerVC { let v = ScannerVC(); v.found = found; return v }
    func updateUIViewController(_ vc: ScannerVC, context: Context) {}
}
final class ScannerVC: UIViewController, AVCaptureMetadataOutputObjectsDelegate {
    var found: ((String) -> Void)?
    private let session = AVCaptureSession()
    override func viewDidLoad() {
        super.viewDidLoad()
        guard let cam = AVCaptureDevice.default(for: .video), let input = try? AVCaptureDeviceInput(device: cam), session.canAddInput(input) else { return }
        session.addInput(input)
        let out = AVCaptureMetadataOutput(); guard session.canAddOutput(out) else { return }
        session.addOutput(out); out.setMetadataObjectsDelegate(self, queue: .main); out.metadataObjectTypes = [.qr]
        let layer = AVCaptureVideoPreviewLayer(session: session); layer.frame = view.bounds; layer.videoGravity = .resizeAspectFill; view.layer.addSublayer(layer)
        DispatchQueue.global(qos: .userInitiated).async { self.session.startRunning() }
    }
    func metadataOutput(_ o: AVCaptureMetadataOutput, didOutput m: [AVMetadataObject], from c: AVCaptureConnection) {
        guard let s = (m.first as? AVMetadataMachineReadableCodeObject)?.stringValue, s.contains("#pin=") else { return }
        session.stopRunning(); found?(s); found = nil
    }
}
