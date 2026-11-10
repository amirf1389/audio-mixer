//! Raw bindings to `sdk/c/include/amix.h`. Hand-written: the header is small and stable (AMIX_API_VERSION 1).
#![allow(non_camel_case_types)]
use std::os::raw::{c_char, c_int, c_void};

pub const AMIX_API_VERSION: c_int = 1;
pub const AMIX_MAX_STRIPS: usize = 32;
pub const AMIX_MAX_BUSES: usize = 8;

#[repr(C)] #[derive(Clone, Copy, Debug, Default)]
pub struct amix_meter { pub peak: f64, pub rms: f64, pub hold: f64, pub clips: u32, pub comp_gr_db: f64, pub gate_db: f64, pub in_peak: f64 }
#[repr(C)] #[derive(Clone, Copy, Debug)]
pub struct amix_levels {
    pub strips: [amix_meter; AMIX_MAX_STRIPS], pub master: [amix_meter; 2], pub buses: [amix_meter; AMIX_MAX_BUSES], pub monitor_peak: [f64; 2],
    pub momentary_lufs: f64, pub short_term_lufs: f64, pub integrated_lufs: f64, pub lra_lu: f64, pub true_peak_dbtp: f64, pub correlation: f64,
    pub blocks: u64, pub frames: u64, pub xruns: u64,
}
impl Default for amix_levels { fn default() -> Self { unsafe { std::mem::zeroed() } } }
#[repr(C)]
pub struct amix_config { pub backend: *const c_char, pub in_device: *const c_char, pub out_device: *const c_char, pub sample_rate: f64, pub frames: c_int, pub in_channels: c_int, pub out_channels: c_int, pub exclusive: c_int }
#[repr(C)] pub struct amix_mixer { _p: [u8; 0] }
#[repr(C)] pub struct amix_rt { _p: [u8; 0] }

extern "C" {
    pub fn amix_version() -> *const c_char;
    pub fn amix_api_version() -> c_int;
    pub fn amix_free(p: *mut c_void);
    pub fn amix_backends_json() -> *mut c_char;
    pub fn amix_devices_json() -> *mut c_char;
    pub fn amix_mixer_create(strips: c_int, sample_rate: f64) -> *mut amix_mixer;
    pub fn amix_mixer_destroy(m: *mut amix_mixer);
    pub fn amix_mixer_strips(m: *const amix_mixer) -> c_int;
    pub fn amix_mixer_set_gain(m: *mut amix_mixer, strip: c_int, db: f64);
    pub fn amix_mixer_set_pan(m: *mut amix_mixer, strip: c_int, pan: f64);
    pub fn amix_mixer_set_mute(m: *mut amix_mixer, strip: c_int, on: c_int);
    pub fn amix_mixer_set_solo(m: *mut amix_mixer, strip: c_int, on: c_int);
    pub fn amix_mixer_set_master(m: *mut amix_mixer, db: f64);
    pub fn amix_mixer_set_limiter(m: *mut amix_mixer, on: c_int, ceiling_db: f64, release_ms: f64);
    pub fn amix_mixer_set_send(m: *mut amix_mixer, strip: c_int, bus: c_int, db: f64, pre: c_int);
    pub fn amix_mixer_set_bus_level(m: *mut amix_mixer, bus: c_int, db: f64);
    pub fn amix_mixer_set_bus_return(m: *mut amix_mixer, bus: c_int, db: f64);
    pub fn amix_mixer_set_width(m: *mut amix_mixer, width: f64);
    pub fn amix_mixer_process(m: *mut amix_mixer, inputs: *const *const f32, frames: c_int, out_l: *mut f32, out_r: *mut f32) -> c_int;
    pub fn amix_mixer_levels(m: *const amix_mixer, out: *mut amix_levels);
    pub fn amix_rt_create(strips: c_int) -> *mut amix_rt;
    pub fn amix_rt_destroy(r: *mut amix_rt);
    pub fn amix_rt_open(r: *mut amix_rt, cfg: *const amix_config, err: *mut c_char, err_len: usize) -> c_int;
    pub fn amix_rt_stop(r: *mut amix_rt);
    pub fn amix_rt_running(r: *const amix_rt) -> c_int;
    pub fn amix_rt_backend(r: *const amix_rt) -> *const c_char;
    pub fn amix_rt_sample_rate(r: *const amix_rt) -> f64;
    pub fn amix_rt_set_gain(r: *mut amix_rt, strip: c_int, db: f64);
    pub fn amix_rt_set_pan(r: *mut amix_rt, strip: c_int, pan: f64);
    pub fn amix_rt_set_mute(r: *mut amix_rt, strip: c_int, on: c_int);
    pub fn amix_rt_set_solo(r: *mut amix_rt, strip: c_int, on: c_int);
    pub fn amix_rt_set_trim(r: *mut amix_rt, strip: c_int, db: f64);
    pub fn amix_rt_set_master(r: *mut amix_rt, db: f64);
    pub fn amix_rt_set_limiter(r: *mut amix_rt, on: c_int, ceiling_db: f64, release_ms: f64);
    pub fn amix_rt_set_eq(r: *mut amix_rt, strip: c_int, band: c_int, kind: c_int, freq: f64, q: f64, gain_db: f64);
    pub fn amix_rt_set_compressor(r: *mut amix_rt, strip: c_int, on: c_int, threshold_db: f64, ratio: f64, knee_db: f64, attack_ms: f64, release_ms: f64, makeup_db: f64);
    pub fn amix_rt_set_gate(r: *mut amix_rt, strip: c_int, on: c_int, threshold_db: f64, range_db: f64, attack_ms: f64, hold_ms: f64, release_ms: f64);
    pub fn amix_rt_set_send(r: *mut amix_rt, strip: c_int, bus: c_int, db: f64, pre: c_int);
    pub fn amix_rt_set_bus_level(r: *mut amix_rt, bus: c_int, db: f64);
    pub fn amix_rt_set_bus_mute(r: *mut amix_rt, bus: c_int, on: c_int);
    pub fn amix_rt_set_bus_return(r: *mut amix_rt, bus: c_int, db: f64);
    pub fn amix_rt_set_width(r: *mut amix_rt, width: f64);
    pub fn amix_rt_set_monitor(r: *mut amix_rt, source: c_int, db: f64, dim: c_int, mono: c_int, mute: c_int);
    pub fn amix_rt_set_input_route(r: *mut amix_rt, strip: c_int, device_input: c_int);
    pub fn amix_rt_set_output_route(r: *mut amix_rt, device_output: c_int, source: c_int);
    pub fn amix_rt_clear_clips(r: *mut amix_rt);
    pub fn amix_rt_levels(r: *const amix_rt, out: *mut amix_levels);
    pub fn amix_license_json() -> *mut c_char;
    pub fn amix_machine_id() -> *mut c_char;
}
