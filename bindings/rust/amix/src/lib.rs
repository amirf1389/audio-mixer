//! Safe wrapper over `amix-sys`. `Mixer` processes blocks offline (tests, servers, renders); `Runtime` opens a sound card.
use amix_sys as ffi;
use std::ffi::{CStr, CString};

fn take_string(p: *mut std::os::raw::c_char) -> String {
    if p.is_null() { return String::new(); }
    let s = unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned();
    unsafe { ffi::amix_free(p as *mut _) };
    s
}
/// The engine version string ("0.1.0").
pub fn version() -> String { unsafe { CStr::from_ptr(ffi::amix_version()) }.to_string_lossy().into_owned() }
pub fn api_version() -> i32 { unsafe { ffi::amix_api_version() } }
/// JSON array of the sound cards and drivers this machine has.
pub fn devices_json() -> String { take_string(unsafe { ffi::amix_devices_json() }) }
pub fn backends_json() -> String { take_string(unsafe { ffi::amix_backends_json() }) }
pub fn license_json() -> String { take_string(unsafe { ffi::amix_license_json() }) }
pub fn machine_id() -> String { take_string(unsafe { ffi::amix_machine_id() }) }

pub use ffi::{amix_levels as Levels, amix_meter as Meter};

/// EQ filter kinds for [`Runtime::set_eq`].
#[repr(i32)] #[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Filter { Off = 0, Peaking = 1, LowShelf = 2, HighShelf = 3, LowPass = 4, HighPass = 5, Notch = 6, AllPass1 = 7, AllPass2 = 8 }

pub struct Mixer { p: *mut ffi::amix_mixer }
unsafe impl Send for Mixer {}
impl Mixer {
    pub fn new(strips: usize, sample_rate: f64) -> Option<Mixer> { let p = unsafe { ffi::amix_mixer_create(strips as i32, sample_rate) }; if p.is_null() { None } else { Some(Mixer { p }) } }
    pub fn strips(&self) -> usize { unsafe { ffi::amix_mixer_strips(self.p) as usize } }
    pub fn set_gain(&mut self, strip: usize, db: f64) { unsafe { ffi::amix_mixer_set_gain(self.p, strip as i32, db) } }
    pub fn set_pan(&mut self, strip: usize, pan: f64) { unsafe { ffi::amix_mixer_set_pan(self.p, strip as i32, pan) } }
    pub fn set_mute(&mut self, strip: usize, on: bool) { unsafe { ffi::amix_mixer_set_mute(self.p, strip as i32, on as i32) } }
    pub fn set_solo(&mut self, strip: usize, on: bool) { unsafe { ffi::amix_mixer_set_solo(self.p, strip as i32, on as i32) } }
    pub fn set_master(&mut self, db: f64) { unsafe { ffi::amix_mixer_set_master(self.p, db) } }
    pub fn set_limiter(&mut self, on: bool, ceiling_db: f64, release_ms: f64) { unsafe { ffi::amix_mixer_set_limiter(self.p, on as i32, ceiling_db, release_ms) } }
    pub fn set_send(&mut self, strip: usize, bus: usize, db: f64, pre: bool) { unsafe { ffi::amix_mixer_set_send(self.p, strip as i32, bus as i32, db, pre as i32) } }
    pub fn set_bus_level(&mut self, bus: usize, db: f64) { unsafe { ffi::amix_mixer_set_bus_level(self.p, bus as i32, db) } }
    pub fn set_bus_return(&mut self, bus: usize, db: f64) { unsafe { ffi::amix_mixer_set_bus_return(self.p, bus as i32, db) } }
    pub fn set_width(&mut self, width: f64) { unsafe { ffi::amix_mixer_set_width(self.p, width) } }
    /// One block: `inputs[s]` is strip s (a shorter or missing slice reads as silence). Returns (left, right) of `frames` samples.
    pub fn process(&mut self, inputs: &[&[f32]], frames: usize) -> (Vec<f32>, Vec<f32>) {
        let n = self.strips(); let silence = vec![0f32; frames];
        let bufs: Vec<Vec<f32>> = (0..n).map(|s| { let mut b = silence.clone(); if let Some(i) = inputs.get(s) { let k = i.len().min(frames); b[..k].copy_from_slice(&i[..k]); } b }).collect();
        let ptrs: Vec<*const f32> = bufs.iter().map(|b| b.as_ptr()).collect();
        let (mut l, mut r) = (vec![0f32; frames], vec![0f32; frames]);
        unsafe { ffi::amix_mixer_process(self.p, ptrs.as_ptr(), frames as i32, l.as_mut_ptr(), r.as_mut_ptr()) };
        (l, r)
    }
    pub fn levels(&self) -> Levels { let mut lv = Levels::default(); unsafe { ffi::amix_mixer_levels(self.p, &mut lv) }; lv }
}
impl Drop for Mixer { fn drop(&mut self) { unsafe { ffi::amix_mixer_destroy(self.p) } } }

#[derive(Clone, Debug, Default)]
pub struct Config { pub backend: String, pub in_device: String, pub out_device: String, pub sample_rate: f64, pub frames: i32, pub in_channels: i32, pub out_channels: i32, pub exclusive: bool }

pub struct Runtime { p: *mut ffi::amix_rt }
unsafe impl Send for Runtime {}
impl Runtime {
    pub fn new(strips: usize) -> Option<Runtime> { let p = unsafe { ffi::amix_rt_create(strips as i32) }; if p.is_null() { None } else { Some(Runtime { p }) } }
    /// Opens and starts the device; Err carries the driver's message.
    pub fn open(&mut self, c: &Config) -> Result<(), String> {
        let (b, i, o) = (CString::new(c.backend.clone()).unwrap(), CString::new(c.in_device.clone()).unwrap(), CString::new(c.out_device.clone()).unwrap());
        let cfg = ffi::amix_config { backend: b.as_ptr(), in_device: i.as_ptr(), out_device: o.as_ptr(), sample_rate: c.sample_rate, frames: c.frames, in_channels: c.in_channels, out_channels: c.out_channels, exclusive: c.exclusive as i32 };
        let mut err = vec![0u8; 512];
        let rc = unsafe { ffi::amix_rt_open(self.p, &cfg, err.as_mut_ptr() as *mut _, err.len()) };
        if rc == 0 { Ok(()) } else { Err(CStr::from_bytes_until_nul(&err).map(|s| s.to_string_lossy().into_owned()).unwrap_or_default()) }
    }
    pub fn stop(&mut self) { unsafe { ffi::amix_rt_stop(self.p) } }
    pub fn running(&self) -> bool { unsafe { ffi::amix_rt_running(self.p) != 0 } }
    pub fn backend(&self) -> String { unsafe { CStr::from_ptr(ffi::amix_rt_backend(self.p)) }.to_string_lossy().into_owned() }
    pub fn sample_rate(&self) -> f64 { unsafe { ffi::amix_rt_sample_rate(self.p) } }
    pub fn set_gain(&mut self, strip: usize, db: f64) { unsafe { ffi::amix_rt_set_gain(self.p, strip as i32, db) } }
    pub fn set_pan(&mut self, strip: usize, pan: f64) { unsafe { ffi::amix_rt_set_pan(self.p, strip as i32, pan) } }
    pub fn set_mute(&mut self, strip: usize, on: bool) { unsafe { ffi::amix_rt_set_mute(self.p, strip as i32, on as i32) } }
    pub fn set_solo(&mut self, strip: usize, on: bool) { unsafe { ffi::amix_rt_set_solo(self.p, strip as i32, on as i32) } }
    pub fn set_trim(&mut self, strip: usize, db: f64) { unsafe { ffi::amix_rt_set_trim(self.p, strip as i32, db) } }
    pub fn set_master(&mut self, db: f64) { unsafe { ffi::amix_rt_set_master(self.p, db) } }
    pub fn set_limiter(&mut self, on: bool, ceiling_db: f64, release_ms: f64) { unsafe { ffi::amix_rt_set_limiter(self.p, on as i32, ceiling_db, release_ms) } }
    pub fn set_eq(&mut self, strip: usize, band: usize, kind: Filter, freq: f64, q: f64, gain_db: f64) { unsafe { ffi::amix_rt_set_eq(self.p, strip as i32, band as i32, kind as i32, freq, q, gain_db) } }
    pub fn set_compressor(&mut self, strip: usize, on: bool, threshold_db: f64, ratio: f64, knee_db: f64, attack_ms: f64, release_ms: f64, makeup_db: f64) { unsafe { ffi::amix_rt_set_compressor(self.p, strip as i32, on as i32, threshold_db, ratio, knee_db, attack_ms, release_ms, makeup_db) } }
    pub fn set_gate(&mut self, strip: usize, on: bool, threshold_db: f64, range_db: f64, attack_ms: f64, hold_ms: f64, release_ms: f64) { unsafe { ffi::amix_rt_set_gate(self.p, strip as i32, on as i32, threshold_db, range_db, attack_ms, hold_ms, release_ms) } }
    pub fn set_send(&mut self, strip: usize, bus: usize, db: f64, pre: bool) { unsafe { ffi::amix_rt_set_send(self.p, strip as i32, bus as i32, db, pre as i32) } }
    pub fn set_bus_level(&mut self, bus: usize, db: f64) { unsafe { ffi::amix_rt_set_bus_level(self.p, bus as i32, db) } }
    pub fn set_bus_mute(&mut self, bus: usize, on: bool) { unsafe { ffi::amix_rt_set_bus_mute(self.p, bus as i32, on as i32) } }
    pub fn set_bus_return(&mut self, bus: usize, db: f64) { unsafe { ffi::amix_rt_set_bus_return(self.p, bus as i32, db) } }
    pub fn set_width(&mut self, width: f64) { unsafe { ffi::amix_rt_set_width(self.p, width) } }
    pub fn set_monitor(&mut self, source: i32, db: f64, dim: bool, mono: bool, mute: bool) { unsafe { ffi::amix_rt_set_monitor(self.p, source, db, dim as i32, mono as i32, mute as i32) } }
    pub fn set_input_route(&mut self, strip: usize, device_input: i32) { unsafe { ffi::amix_rt_set_input_route(self.p, strip as i32, device_input) } }
    pub fn set_output_route(&mut self, device_output: usize, source: i32) { unsafe { ffi::amix_rt_set_output_route(self.p, device_output as i32, source) } }
    pub fn clear_clips(&mut self) { unsafe { ffi::amix_rt_clear_clips(self.p) } }
    pub fn levels(&self) -> Levels { let mut lv = Levels::default(); unsafe { ffi::amix_rt_levels(self.p, &mut lv) }; lv }
}
impl Drop for Runtime { fn drop(&mut self) { unsafe { ffi::amix_rt_destroy(self.p) } } }

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn offline_mixer_fader_pan_and_meters() {
        assert_eq!(api_version(), 2); assert!(version().len() >= 5); assert!(devices_json().contains("\"null\""));
        let mut m = Mixer::new(4, 48000.0).unwrap(); assert_eq!(m.strips(), 4);
        let tone: Vec<f32> = (0..256).map(|i| 0.5 * (2.0 * std::f64::consts::PI * 1000.0 * i as f64 / 48000.0).sin() as f32).collect();
        m.set_limiter(false, -0.5, 100.0); m.set_gain(0, -6.0); m.set_pan(0, 0.0);
        let mut pk = 0f32; let mut out = (vec![], vec![]);
        for _ in 0..20 { out = m.process(&[&tone], 256); for v in &out.0 { pk = pk.max(v.abs()); } }
        assert!((pk - 0.177).abs() < 0.01, "peak {pk}");
        let lv = m.levels(); assert!(lv.master[0].peak > 0.15 && lv.master[0].peak < 0.2); assert!(lv.strips[0].peak > 0.2);
        m.set_mute(0, true); for _ in 0..5 { out = m.process(&[&tone], 256); } assert!(out.0[100].abs() < 1e-6);
    }
    #[test]
    fn null_runtime_opens_and_stops() {
        let mut rt = Runtime::new(8).unwrap();
        rt.open(&Config { backend: "null".into(), sample_rate: 48000.0, frames: 128, in_channels: 2, out_channels: 2, ..Default::default() }).unwrap();
        assert!(rt.running()); assert_eq!(rt.backend(), "null"); rt.set_gain(0, -3.0); rt.set_eq(0, 0, Filter::Peaking, 1000.0, 1.0, 3.0); let _ = rt.levels(); rt.stop(); assert!(!rt.running());
        assert!(license_json().contains("\"state\"")); assert!(machine_id().len() > 4);
    }
}
