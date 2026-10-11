'use strict';
// Plugin insert slots for the PHASE and FX pages: which scanned plugin (.vst3 / .dll / .vst) sits in which slot, and whether it is bypassed.
// Read and written through the local server and kept in ~/.audio-mixer/inserts.json (BRIDGE_INSERTS_FILE overrides), so the choice survives
// restarts and every page sees the same racks. This is the routing record: the bridge lists and validates plugins but does not run them.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const plugins = require('./plugins');

const MAX_SLOTS = 64;
const SLOT = /^(phase|fx):[a-z0-9_-]{1,16}$/;

function storeFile(env = process.env) { return env.BRIDGE_INSERTS_FILE || path.join(os.homedir(), '.audio-mixer', 'inserts.json'); }

function read(file = storeFile()) {
  try {
    const j = JSON.parse(fs.readFileSync(file, 'utf8'));
    const slots = {};
    for (const [k, v] of Object.entries(j.slots || {})) if (SLOT.test(k) && v && typeof v.plugin === 'string') slots[k] = { plugin: v.plugin.slice(0, 120), format: v.format === 'VST3' ? 'VST3' : 'VST2', bypass: !!v.bypass };
    return { slots };
  } catch (_) { return { slots: {} }; }
}

function write(data, file = storeFile()) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  const tmp = file + '.' + process.pid + '.tmp';
  fs.writeFileSync(tmp, JSON.stringify(data, null, 2), { mode: 0o600 });
  fs.renameSync(tmp, file);
}

// set({ slot, plugin, bypass }) -> new state. plugin: a scanned plugin's name (loadable only), or null to empty the slot; bypass alone toggles an occupied slot.
function set(body, { file = storeFile(), scan = () => plugins.scan() } = {}) {
  const fail = (status, message) => Object.assign(new Error(message), { status });
  if (!body || typeof body.slot !== 'string' || !SLOT.test(body.slot)) throw fail(400, 'slot must look like phase:ch1 or fx:1');
  const state = read(file);
  if (body.plugin === null) { delete state.slots[body.slot]; write(state, file); return state; }
  if (body.plugin !== undefined) {
    if (typeof body.plugin !== 'string' || !body.plugin || body.plugin.length > 120) throw fail(400, 'plugin must be the name of a scanned plugin');
    const found = scan().plugins.find(p => p.name === body.plugin && (!body.format || p.format === body.format));
    if (!found) throw fail(404, 'plugin not found: scan the plugin folders first');
    if (!found.valid || !found.compatible) throw fail(409, found.reason || 'this plugin cannot be loaded on this system');
    if (!state.slots[body.slot] && Object.keys(state.slots).length >= MAX_SLOTS) throw fail(400, 'too many insert slots');
    state.slots[body.slot] = { plugin: found.name, format: found.format, bypass: body.bypass === true };
  } else {
    if (!state.slots[body.slot]) throw fail(404, 'that slot is empty');
    state.slots[body.slot].bypass = body.bypass === true;
  }
  write(state, file);
  return state;
}

module.exports = { read, set, write, storeFile, SLOT, MAX_SLOTS };
