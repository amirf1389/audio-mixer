'use strict';
// ASIO drivers are single-client: only one ASIO device may be open at a time. Opening the same device twice (read + write) is fine.
const owners = new Map();   // device id -> { name, count }

function claim(dev) {
  const noop = { ok: true, release() {} };
  if (!dev || !/asio/i.test(dev.hostAPIName || '')) return noop;
  for (const [id, o] of owners) {
    if (id !== dev.id && o.count > 0) return { ok: false, message: `ASIO allows one driver at a time: "${o.name}" is already open. Close it first, or use WASAPI for this device.` };
  }
  const o = owners.get(dev.id) || { name: dev.name, count: 0 };
  o.count++; owners.set(dev.id, o);
  let done = false;
  return { ok: true, release() { if (done) return; done = true; o.count--; if (o.count <= 0) owners.delete(dev.id); } };
}

module.exports = { claim, _owners: owners };
