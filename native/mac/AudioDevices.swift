// AudioDevices - lists the Core Audio devices of this Mac as JSON on stdout (read-only: it opens no stream and changes nothing).
//   { "ok":true, "devices":[ {"id":"<uid>","name":"MacBook Pro Microphone","kind":"input","channels":1,"sampleRate":48000,"default":true,"state":"active","transport":"builtin"} ] }
// Same shape as native/win/AudioDevices.exe, so bridge/sysaudio.js reads both. A device with inputs and outputs is listed once per direction.
// Build (Xcode command line tools):  swiftc -O -o native/mac/AudioDevices native/mac/AudioDevices.swift
// install.command does this for you when swiftc is installed; without it the bridge falls back to `system_profiler SPAudioDataType -json`.
import CoreAudio
import Foundation

func address(_ selector: AudioObjectPropertySelector, _ scope: AudioObjectPropertyScope = kAudioObjectPropertyScopeGlobal) -> AudioObjectPropertyAddress {
    return AudioObjectPropertyAddress(mSelector: selector, mScope: scope, mElement: kAudioObjectPropertyElementMain)
}

func devices() -> [AudioObjectID] {
    var addr = address(kAudioHardwarePropertyDevices)
    var size: UInt32 = 0
    guard AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size) == noErr, size > 0 else { return [] }
    var ids = [AudioObjectID](repeating: 0, count: Int(size) / MemoryLayout<AudioObjectID>.size)
    guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size, &ids) == noErr else { return [] }
    return ids
}

func string(_ id: AudioObjectID, _ selector: AudioObjectPropertySelector) -> String {
    var addr = address(selector)
    var value: Unmanaged<CFString>?
    var size = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
    guard AudioObjectGetPropertyData(id, &addr, 0, nil, &size, &value) == noErr, let s = value else { return "" }
    return s.takeRetainedValue() as String
}

func channels(_ id: AudioObjectID, _ scope: AudioObjectPropertyScope) -> Int {
    var addr = address(kAudioDevicePropertyStreamConfiguration, scope)
    var size: UInt32 = 0
    guard AudioObjectGetPropertyDataSize(id, &addr, 0, nil, &size) == noErr, size > 0 else { return 0 }
    let raw = UnsafeMutableRawPointer.allocate(byteCount: Int(size), alignment: MemoryLayout<AudioBufferList>.alignment)
    defer { raw.deallocate() }
    guard AudioObjectGetPropertyData(id, &addr, 0, nil, &size, raw) == noErr else { return 0 }
    let list = UnsafeMutableAudioBufferListPointer(raw.assumingMemoryBound(to: AudioBufferList.self))
    return list.reduce(0) { $0 + Int($1.mNumberChannels) }
}

func sampleRate(_ id: AudioObjectID) -> Double {
    var addr = address(kAudioDevicePropertyNominalSampleRate)
    var rate: Float64 = 0
    var size = UInt32(MemoryLayout<Float64>.size)
    return AudioObjectGetPropertyData(id, &addr, 0, nil, &size, &rate) == noErr ? rate : 0
}

func defaultDevice(_ selector: AudioObjectPropertySelector) -> AudioObjectID {
    var addr = address(selector)
    var id = AudioObjectID(kAudioObjectUnknown)
    var size = UInt32(MemoryLayout<AudioObjectID>.size)
    _ = AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size, &id)
    return id
}

func transport(_ id: AudioObjectID) -> String {
    var addr = address(kAudioDevicePropertyTransportType)
    var t: UInt32 = 0
    var size = UInt32(MemoryLayout<UInt32>.size)
    guard AudioObjectGetPropertyData(id, &addr, 0, nil, &size, &t) == noErr else { return "unknown" }
    switch t {
    case kAudioDeviceTransportTypeBuiltIn: return "builtin"
    case kAudioDeviceTransportTypeUSB: return "usb"
    case kAudioDeviceTransportTypeBluetooth, kAudioDeviceTransportTypeBluetoothLE: return "bluetooth"
    case kAudioDeviceTransportTypeFireWire: return "firewire"
    case kAudioDeviceTransportTypeThunderbolt: return "thunderbolt"
    case kAudioDeviceTransportTypeAirPlay: return "airplay"
    case kAudioDeviceTransportTypeVirtual, kAudioDeviceTransportTypeAggregate: return "virtual"
    case kAudioDeviceTransportTypeHDMI, kAudioDeviceTransportTypeDisplayPort: return "display"
    default: return "other"
    }
}

let defIn = defaultDevice(kAudioHardwarePropertyDefaultInputDevice)
let defOut = defaultDevice(kAudioHardwarePropertyDefaultOutputDevice)
var out: [[String: Any]] = []
for id in devices() {
    let name = string(id, kAudioObjectPropertyName)
    if name.isEmpty { continue }
    for (kind, scope, def) in [("input", kAudioObjectPropertyScopeInput, defIn), ("output", kAudioObjectPropertyScopeOutput, defOut)] {
        let n = channels(id, scope)
        if n == 0 { continue }
        out.append(["id": string(id, kAudioDevicePropertyDeviceUID), "name": name, "kind": kind, "channels": n, "sampleRate": Int(sampleRate(id)),
                    "default": id == def, "state": "active", "transport": transport(id)])
    }
}
let json = try? JSONSerialization.data(withJSONObject: ["ok": true, "devices": out], options: [])
print(String(data: json ?? Data("{\"ok\":false}".utf8), encoding: .utf8) ?? "{\"ok\":false}")
