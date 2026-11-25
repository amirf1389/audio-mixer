import SwiftUI

@main
struct AmFX1RemoteApp: App {
    @StateObject private var console = Console()
    var body: some Scene {
        WindowGroup {
            if console.paired { MixerView().environmentObject(console) } else { PairView().environmentObject(console) }
        }
    }
}
