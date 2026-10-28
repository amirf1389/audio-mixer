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

function claim(dev) {
  const noop = { ok: true, release() {} };
  if (!dev || !/asio/i.test(dev.hostAPIName || '')) return noop;
  const key = keyOf(dev);
  for (const [k, o] of owners) {
    if (k !== key && o.count > 0) return { ok: false, message: `ASIO allows one driver at a time: "${o.name}" is already open. Close it first, or use WASAPI for this device.` };
  }
  const o = owners.get(key) || { name: dev.name, count: 0 };
  o.count++; owners.set(key, o);
  let done = false;
  return { ok: true, release() { if (done) return; done = true; o.count--; if (o.count <= 0) owners.delete(key); } };
}

module.exports = { claim, keyOf, _owners: owners };
