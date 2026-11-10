// cargo run --example meter: a 1 kHz tone through two strips of the offline mixer, meters printed per block.
fn main() {
    let mut m = amix::Mixer::new(2, 48000.0).expect("mixer");
    m.set_gain(0, -6.0); m.set_gain(1, -12.0); m.set_pan(0, -1.0); m.set_pan(1, 1.0);
    let tone: Vec<f32> = (0..512).map(|i| 0.5 * (2.0 * std::f64::consts::PI * 1000.0 * i as f64 / 48000.0).sin() as f32).collect();
    for k in 0..10 { let (l, r) = m.process(&[&tone, &tone], 512); let lv = m.levels(); println!("block {k}: L {:.3} R {:.3} master peak {:.3} / {:.3}", l[7], r[7], lv.master[0].peak, lv.master[1].peak); }
    println!("engine {} on {}", amix::version(), amix::devices_json());
}
