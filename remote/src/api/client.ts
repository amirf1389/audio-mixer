// The daemon client: plain fetch + WebSocket, no React Native imports, so the same file runs under Node for the test.
// The token travels in the X-Amix-Token header (control) and the ?token= query (meters WebSocket). Nothing here is encrypted:
// the daemon is for the loopback or a trusted LAN; for anything else the daemon sits behind deploy/nginx (TLS) and `secure: true` here.
import type { ConsoleState, Control, ControlOk, Device, Meters, Status } from './types';

export interface Endpoint { host: string; port: number; token: string; secure?: boolean }

export class DaemonError extends Error { constructor(message: string, public status: number) { super(message); } }

export class DaemonClient {
  constructor(public ep: Endpoint, private fetchImpl: typeof fetch = fetch) {}
  get http(): string { return `${this.ep.secure ? 'https' : 'http'}://${this.ep.host}:${this.ep.port}`; }
  get ws(): string { return `${this.ep.secure ? 'wss' : 'ws'}://${this.ep.host}:${this.ep.port}`; }

  private async get<T>(path: string): Promise<T> {
    const r = await this.fetchImpl(this.http + path, { headers: { 'X-Amix-Token': this.ep.token } });
    const text = await r.text();
    if (!r.ok) throw new DaemonError(errorOf(text) ?? `HTTP ${r.status}`, r.status);
    return JSON.parse(text) as T;
  }
  status(): Promise<Status> { return this.get<Status>('/api/status'); }
  devices(): Promise<Device[]> { return this.get<Device[]>('/api/devices'); }
  state(): Promise<ConsoleState> { return this.get<ConsoleState>('/api/state'); }
  meters(): Promise<Meters> { return this.get<Meters>('/api/meters'); }

  async control(cmd: Control): Promise<ControlOk> {
    const r = await this.fetchImpl(this.http + '/api/control', { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Amix-Token': this.ep.token }, body: JSON.stringify(cmd) });
    const text = await r.text();
    if (!r.ok) throw new DaemonError(errorOf(text) ?? `HTTP ${r.status}`, r.status);
    return JSON.parse(text) as ControlOk;
  }

  // the meter stream: onFrame 20 times a second; returns a stop function. Reconnects after a drop until stopped.
  meterStream(onFrame: (m: Meters) => void, onState?: (connected: boolean, error?: string) => void, WS: typeof WebSocket = WebSocket): () => void {
    let stopped = false; let sock: WebSocket | null = null; let timer: ReturnType<typeof setTimeout> | null = null;
    const connect = () => {
      if (stopped) return;
      sock = new WS(`${this.ws}/ws/meters?token=${encodeURIComponent(this.ep.token)}`);
      sock.onopen = () => onState?.(true);
      sock.onmessage = (e: MessageEvent) => {
        try { const m = JSON.parse(String(e.data)) as Meters & { error?: string }; if (m.error) { onState?.(false, m.error); stopped = true; sock?.close(); return; } onFrame(m); } catch { /* a partial frame: skip */ }
      };
      sock.onerror = () => onState?.(false, 'socket error');
      sock.onclose = () => { onState?.(false); if (!stopped) timer = setTimeout(connect, 1000); };
    };
    connect();
    return () => { stopped = true; if (timer) clearTimeout(timer); sock?.close(); };
  }
}

function errorOf(text: string): string | undefined { try { const j = JSON.parse(text); return typeof j?.error === 'string' ? j.error : undefined; } catch { return undefined; } }

// a slider position 0..1 <-> fader dB, the same law as the desktop console (-inf at 0, 0 dB at 0.75, +10 at 1)
export function posToDb(p: number): number { if (p <= 0) return -120; if (p >= 1) return 10; return p >= 0.75 ? (p - 0.75) * 40 : p >= 0.5 ? -20 + (p - 0.5) * 80 : p >= 0.25 ? -60 + (p - 0.25) * 160 : -120 + p * 240; }
export function dbToPos(db: number): number { if (db <= -120) return 0; if (db >= 10) return 1; return db >= 0 ? 0.75 + db / 40 : db >= -20 ? 0.5 + (db + 20) / 80 : db >= -60 ? 0.25 + (db + 60) / 160 : (db + 120) / 240; }
export function fmtDb(db: number): string { return db <= -119 ? '-∞' : (db > 0 ? '+' : '') + db.toFixed(1); }
