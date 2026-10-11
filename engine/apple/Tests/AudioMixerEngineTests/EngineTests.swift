import XCTest
import AudioMixerEngine
import AudioMixerObjC

// A few of the numbers of engine/tests/vectors.txt (a 1 kHz sine of amplitude 0.5 is -6.01 LUFS and -6.02 dBTP; a mono sine pans with constant power).
final class EngineTests: XCTestCase {
    private func sine(_ f: Double, _ a: Double, _ n: Int, fs: Double = 48000) -> [Float] { (0..<n).map { Float(a * sin(2 * Double.pi * f * Double($0) / fs)) } }

    func testLoudnessOfASine() {
        let m = LoudnessMeter(sampleRate: 48000), x = sine(1000, 0.5, 48000 * 10)
        stride(from: 0, to: x.count, by: 2048).forEach { i in let j = min(i + 2048, x.count); m.feed(left: Array(x[i..<j]), right: Array(x[i..<j])) }
        XCTAssertEqual(m.integrated, -6.01389, accuracy: 0.02)
        XCTAssertEqual(m.truePeak, -6.0206, accuracy: 0.01)
    }
    func testMixerPanIsConstantPower() {
        let mixer = Mixer(strips: 2, sampleRate: 48000)
        mixer.setPan(strip: 0, pan: -1)
        let out = mixer.process(inputs: [sine(440, 0.5, 4096), [Float](repeating: 0, count: 4096)])
        XCTAssertEqual(out.left.map { abs($0) }.max() ?? 0, 0.5, accuracy: 0.001)
        XCTAssertEqual(out.right.map { abs($0) }.max() ?? 0, 0, accuracy: 0.001)
    }
    func testScopeFindsTheFrequency() {
        let s = scopeSweep(samples: sine(440, 0.5, 32768), sampleRate: 48000)
        XCTAssertTrue(s.triggered); XCTAssertEqual(s.frequency, 440, accuracy: 0.5); XCTAssertEqual(s.peakToPeak, 1.0, accuracy: 0.01)
    }
    func testObjectiveCMixer() {
        let m = AMEMixer(strips: 1, sampleRate: 48000)
        var left = [Float](repeating: 0, count: 512), right = [Float](repeating: 0, count: 512)
        let tone = sine(440, 0.5, 512)
        tone.withUnsafeBufferPointer { p in var ins: [UnsafePointer<Float>?] = [p.baseAddress]; m.processInputs(&ins, frames: 512, outLeft: &left, outRight: &right) }
        XCTAssertEqual(m.peak(ofStrip: 0), 0.5, accuracy: 0.01)
    }
}
