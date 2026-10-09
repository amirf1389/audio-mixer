'use strict';
// Registry of the native audio streams that are open right now (ASIO / WASAPI / ... through PortAudio or Audify), for live status.
const streams = new Map();
let next = 1;

function add(info) {
  const id = next++;
  streams.set(id, { id, since: Date.now(), ...info });
  return () => streams.delete(id);
}
function list() { return [...streams.values()].map(s => ({ ...s, uptimeSec: Math.round((Date.now() - s.since) / 1000) })); }

module.exports = { add, list };
