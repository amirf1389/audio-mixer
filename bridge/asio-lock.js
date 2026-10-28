'use strict';
// ASIO drivers are single-client: only one ASIO device may be open at a time. Opening the same device twice (read + write) is fine.
const owners = new Map();   // driver key (hostAPIName + name) -> { name, count }

// ASIO's single-client limit is per physical driver, whose only stable identity is its driver name. dev.id must NOT be used
// here: it is a per-enumeration sequential integer (see listDevices in bridge/audify.js, `id: next++`) that differs between
// engines and shifts on every re-scan, so keying on it made two separately-enumerated handles to the SAME driver look like
// two different drivers - the read+write duplex (or an output opened from one scan and an input from another) was then
// wrongly refused with "one driver at a time". Keying on the driver name makes the same driver claim the same slot every time.
function keyOf(dev) {
  return `${String(dev.hostAPIName || '').toLowerCase()}\u0000${String(dev.name || '').trim().toLowerCase()}`;
}

// `opts.takeover`: a device the user asked for (WRITE / READ pressed on its row) takes the single-client slot from the device that holds it. Each
// holder registers a `preempt` callback when it claims; a takeover calls those callbacks, which stop the holder's stream and release its claim.
// Without `takeover` a second device is refused, as before.
function claim(dev, opts = {}) {
  const noop = { ok: true, release() {} };
  if (!dev || !/asio/i.test(dev.hostAPIName || '')) return noop;
  const key = keyOf(dev);
  const others = () => [...owners].filter(([k, o]) => k !== key && o.count > 0);
  if (others().length && opts.takeover) {
    for (const [, o] of others()) for (const preempt of [...o.preempts]) { try { preempt(); } catch (_) { /* that session is already gone */ } }
  }
  const blocked = others()[0];
  if (blocked) return { ok: false, message: `ASIO allows one driver at a time: "${blocked[1].name}" is already open. Close it first, or use WASAPI for this device.` };
  const o = owners.get(key) || { name: dev.name, count: 0, preempts: new Set() };
  o.count++; owners.set(key, o);
  const preempt = typeof opts.preempt === 'function' ? opts.preempt : null;
  if (preempt) o.preempts.add(preempt);
  let done = false;
  return { ok: true, release() { if (done) return; done = true; if (preempt) o.preempts.delete(preempt); o.count--; if (o.count <= 0) owners.delete(key); } };
}

module.exports = { claim, keyOf, _owners: owners };
