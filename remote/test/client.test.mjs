// Runs the TypeScript client (compiled by `tsc -p tsconfig.client.json`) against a real amixos-daemon on the null backend.
// AMIXOS_DAEMON=path/to/amixos-daemon (default: ../build/amixos-daemon or /tmp/ambuild/amixos-daemon).
import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { DaemonClient, DaemonError, dbToPos, posToDb, fmtDb } from './build/client.js';

const bin = process.env.AMIXOS_DAEMON || ['../build/amixos-daemon', '/tmp/ambuild/amixos-daemon'].find(existsSync);
if (!bin) { console.error('client.test: no amixos-daemon binary (set AMIXOS_DAEMON)'); process.exit(2); }
const token = 'remote-test-token-0123456789abcdef0123456789';
const daemon = spawn(bin, ['--port', '0', '--token', token, '--strips', '8']);
let port = 0;
await new Promise((res, rej) => { daemon.stdout.on('data', d => { const m = /listening on [\d.]+:(\d+)/.exec(String(d)); if (m) { port = +m[1]; res(); } }); daemon.on('exit', c => rej(new Error('daemon exited ' + c))); setTimeout(() => rej(new Error('daemon did not start')), 5000); });

let failed = 0;
const check = (c, what) => { if (!c) { failed++; console.error('FAIL', what); } };
try {
  check(Math.abs(posToDb(dbToPos(-6.5)) + 6.5) < 1e-9 && posToDb(0.75) === 0 && posToDb(1) === 10 && fmtDb(-120) === '-∞' && fmtDb(3) === '+3.0', 'fader law');
  const bad = new DaemonClient({ host: '127.0.0.1', port, token: 'wrong' });
  const st = await bad.status(); check(st.name === 'A mix OS' && st.strips === 8 && !st.running, 'status without token');
  let err = null; try { await bad.control({ op: 'gain', strip: 0, db: -3 }); } catch (e) { err = e; } check(err instanceof DaemonError && err.status === 401, 'control with a wrong token is 401');
  const c = new DaemonClient({ host: '127.0.0.1', port, token });
  const devs = await c.devices(); check(Array.isArray(devs) && devs.some(d => d.backend === 'null'), 'devices lists the null backend');
  const opened = await c.control({ op: 'open', backend: 'null', rate: 48000, frames: 128 }); check(opened.ok && opened.backend === 'null', 'open null');
  await c.control({ op: 'gain', strip: 1, db: -6.5 }); await c.control({ op: 'name', strip: 1, name: 'Guitar' }); await c.control({ op: 'mute', strip: 2, on: true }); await c.control({ op: 'master', db: -2 });
  await c.control({ op: 'compressor', strip: 1, on: true, thresholdDb: -20, ratio: 4 }); await c.control({ op: 'eq', strip: 1, band: 0, type: 'peaking', freq: 1000, q: 1, gainDb: 3 });
  const s = await c.state(); check(s.strips[1].db === -6.5 && s.strips[1].name === 'Guitar' && s.strips[2].mute && s.master.db === -2 && s.running, 'state reflects control');
  err = null; try { await c.control({ op: 'gain', strip: 99, db: 0 }); } catch (e) { err = e; } check(err instanceof DaemonError && err.status === 400 && /range/.test(err.message), 'strip out of range is 400 with the daemon message');
  const frames = []; let connected = false;
  const stop = c.meterStream(m => frames.push(m), ok => { if (ok) connected = true; });
  await new Promise(r => setTimeout(r, 700)); stop();
  check(connected && frames.length >= 8 && frames.length <= 16 && frames[0].strips.length === 8 && typeof frames[0].master.l === 'number' && frames[frames.length - 1].blocks > 0, `meter stream: ${frames.length} frames in 0.7 s`);
  const badWs = new DaemonClient({ host: '127.0.0.1', port, token: 'wrong' }); let wsErr = '';
  const stop2 = badWs.meterStream(() => {}, (ok, e) => { if (e) wsErr = e; }); await new Promise(r => setTimeout(r, 300)); stop2();
  check(/unauthorised/.test(wsErr), 'meter stream with a wrong token reports unauthorised');
  await c.control({ op: 'stop' }); check(!(await c.state()).running, 'stop');
} catch (e) { failed++; console.error('FAIL', e); }
daemon.kill();
if (failed) { console.error(`client.test: ${failed} failed`); process.exit(1); }
console.log(`client.test: TS client against amixos-daemon on port ${port}: status, 401, devices, open, control -> state, 400 message, meter stream, unauthorised stream, stop OK`);
