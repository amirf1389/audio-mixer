// What amixos-daemon sends and accepts (engine/cpp/app/daemon/daemon.hpp). Keep in step with the daemon; the test in test/client.test.mjs checks the shapes.
export interface Status { name: string; version: string; daemon: string; backend: string; running: boolean; blocks: number; frames: number; xruns: number; strips: number; buses: number; uptimeS: number; lastError?: string; backends: string[] }
export interface Device { backend: string; id: string; name: string; in: number; out: number; defaultRate: number; defaultIn: boolean; defaultOut: boolean; sampleRates: number[] }
export interface StripState { id: number; name: string; db: number; pan: number; mute: boolean; solo: boolean }
export interface BusState { id: number; db: number; returnDb: number; mute: boolean }
export interface MonitorState { source: number; db: number; dim: boolean; mono: boolean; mute: boolean }
export interface ConsoleState { strips: StripState[]; buses: BusState[]; master: { db: number; limiter: boolean; ceilingDb: number }; monitor: MonitorState; backend: string; running: boolean }
export interface StripMeter { pk: number; rms: number; hold: number; clip: number; gr: number; gate: number; in: number }
export interface Meters { running: boolean; strips?: StripMeter[]; master?: { l: number; r: number; rms: number; clip: number }; monitor?: { l: number; r: number }; buses?: number[]; loudness?: { m: number; s: number; i: number; lra: number; tp: number }; correlation?: number; blocks?: number; xruns?: number }

export type FilterType = 'off' | 'peaking' | 'lowshelf' | 'highshelf' | 'lowpass' | 'highpass' | 'notch' | 'allpass1' | 'allpass2';
export type Control =
  | { op: 'open'; backend?: string; in?: string; out?: string; rate?: number; frames?: number; inChannels?: number; outChannels?: number; exclusive?: boolean }
  | { op: 'stop' }
  | { op: 'gain'; strip: number; db: number }
  | { op: 'pan'; strip: number; pan: number }
  | { op: 'mute'; strip: number; on: boolean }
  | { op: 'solo'; strip: number; on: boolean }
  | { op: 'name'; strip: number; name: string }
  | { op: 'master'; db: number }
  | { op: 'limiter'; on: boolean; ceilingDb?: number }
  | { op: 'send'; strip: number; bus: number; db: number; pre?: boolean }
  | { op: 'busLevel'; bus: number; db: number }
  | { op: 'busMute'; bus: number; on: boolean }
  | { op: 'busReturn'; bus: number; db: number }
  | { op: 'eq'; strip: number; band: number; type: FilterType; freq: number; q: number; gainDb: number }
  | { op: 'delay'; strip: number; ms: number }
  | { op: 'polarity'; strip: number; on: boolean }
  | { op: 'compressor'; strip: number; on: boolean; thresholdDb?: number; ratio?: number; kneeDb?: number; attackMs?: number; releaseMs?: number; makeupDb?: number }
  | { op: 'gate'; strip: number; on: boolean; thresholdDb?: number; rangeDb?: number; attackMs?: number; holdMs?: number; releaseMs?: number }
  | { op: 'monitor'; source?: number; db?: number; dim?: boolean; mono?: boolean; mute?: boolean }
  | { op: 'inRoute'; strip: number; input: number }
  | { op: 'outRoute'; out: number; source: number }
  | { op: 'clearClips' };
export interface ControlOk { ok: true; [k: string]: unknown }
