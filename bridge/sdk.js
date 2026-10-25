'use strict';
// Third-party audio-processing SDK integration points: AudioShake (stem separation), GPU Audio SDK (GPU-accelerated
// DSP), Camb.ai (voice/translation), Clarity Voice Studio (voice clarity / noise suppression). We ship none of
// these binaries or client libraries and hold no API keys for them, so this module is an honest plugin slot, not a
// fake integration: it never makes a network call or returns invented output. A provider is "configured" only when
// its API key env var is set; it is "wired" only once a real client has been registered with registerClient() (the
// npm package or native binding for that SDK, added as a dependency when the project actually integrates it).
// process() throws a specific, actionable error at whichever stage is missing, the same honesty bridge/audify.js
// uses for a host API RtAudio was not compiled with ("not compiled" instead of silently substituting another API).

const PROVIDERS = {
  audioshake: { name: 'AudioShake', purpose: 'AI stem separation (vocals / instruments) for the mixer channels.', envKey: 'AUDIOSHAKE_API_KEY', pkg: '@audioshake/sdk' },
  'gpu-audio': { name: 'GPU Audio SDK', purpose: 'GPU-accelerated real-time DSP (EQ, convolution, the FFT-heavy effects) offloaded from the CPU.', envKey: 'GPU_AUDIO_API_KEY', pkg: '@gpu-audio/sdk' },
  cambai: { name: 'Camb.ai', purpose: 'Real-time voice translation / dubbing for the mixer\'s channels.', envKey: 'CAMBAI_API_KEY', pkg: '@cambai/sdk' },
  'clarity-voice': { name: 'Clarity Voice Studio', purpose: 'Voice clarity / de-noise enhancement for mic channels.', envKey: 'CLARITY_VOICE_API_KEY', pkg: 'clarity-voice-studio-sdk' },
};

const clients = new Map();   // provider id -> real client implementation, registered once the project actually depends on that SDK

function ids() { return Object.keys(PROVIDERS); }

function provider(id) {
  const p = PROVIDERS[id];
  if (!p) throw new Error('unknown SDK provider: ' + id + ' (known: ' + ids().join(', ') + ')');
  return p;
}

// An API key is present for this provider. This is NOT the same as being usable (see wired()): a key with no
// client registered still cannot process anything, and process() says so rather than pretending the key is enough.
function configured(id) { return !!(process.env[provider(id).envKey] || '').trim(); }

// A real client (the SDK's own package, wrapped to this module's process() signature) has been registered.
function wired(id) { provider(id); return clients.has(id); }

function registerClient(id, client) {
  provider(id);
  if (typeof client !== 'object' || client === null || typeof client.process !== 'function') throw new Error('registerClient(' + id + ', client): client.process must be a function');
  clients.set(id, client);
}
function unregisterClient(id) { provider(id); clients.delete(id); }

// One line per provider: who it is, what it would do, and whether it can actually run right now.
function status() {
  return ids().map(id => {
    const p = PROVIDERS[id], cfg = configured(id), w = wired(id);
    return { id, name: p.name, purpose: p.purpose, envKey: p.envKey, configured: cfg, wired: w, ready: cfg && w, package: p.pkg };
  });
}

// Hands a job to the named SDK. Throws instead of faking output at the first missing piece: no API key, no
// registered client, or (once wired) whatever the real client itself throws.
// Named runJob, not "process": a top-level function called process() would shadow Node's global process object for
// the rest of this module (process.env above would silently stop working) - exported as sdk.process instead.
async function runJob(id, job) {
  const p = provider(id);
  if (!configured(id)) throw new Error(p.name + ' is not configured: set ' + p.envKey + ' to enable it.');
  const client = clients.get(id);
  if (!client) throw new Error(p.name + ' has no SDK client installed: add the "' + p.pkg + '" package and call sdk.registerClient(' + JSON.stringify(id) + ', client) to wire it in.');
  return client.process(job);
}

module.exports = { PROVIDERS, ids, provider, configured, wired, registerClient, unregisterClient, status, process: runJob };
