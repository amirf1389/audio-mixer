'use strict';
// Hardening helpers for the local server: response headers, request limits, rate limiting, safe redirects, static-file allow-list.
const path = require('node:path');

// Applied to every response. The page needs inline scripts and its CDN assets, so the CSP only pins what is safe to pin.
const HEADERS = {
  'X-Content-Type-Options': 'nosniff',
  'Referrer-Policy': 'no-referrer',
  'X-Frame-Options': 'SAMEORIGIN',
  'Content-Security-Policy': "frame-ancestors 'self'; base-uri 'self'; form-action 'self'; object-src 'none'",
  'Permissions-Policy': 'camera=(), geolocation=(), payment=(), usb=(self), microphone=(self), display-capture=(self)',
  'Cross-Origin-Opener-Policy': 'same-origin',
  'Cross-Origin-Resource-Policy': 'same-site',
};

// Sliding window per key: allow(key, limit, windowMs) -> { ok, retryAfter }
function createLimiter({ now = Date.now } = {}) {
  const hits = new Map();
  return {
    allow(key, limit, windowMs = 60000) {
      const t = now(), arr = (hits.get(key) || []).filter(x => t - x < windowMs);
      if (arr.length >= limit) { hits.set(key, arr); return { ok: false, retryAfter: Math.max(1, Math.ceil((windowMs - (t - arr[0])) / 1000)) }; }
      arr.push(t); hits.set(key, arr);
      if (hits.size > 2000) for (const [k, v] of hits) if (!v.length || t - v[v.length - 1] > windowMs) hits.delete(k);
      return { ok: true, retryAfter: 0 };
    },
    reset() { hits.clear(); },
  };
}

// Only these folders/files of the install are ever served; sources, scripts, the installer, native helpers and dependencies are not.
const SERVED_TYPES = new Set(['.html', '.js', '.css', '.json', '.png', '.svg']);
const BLOCKED_TOP = new Set(['bridge', 'client', 'scripts', 'installer', 'deploy', 'native', 'node_modules', 'dist', 'releases']);
function staticAllowed(root, file) {
  const rel = path.relative(root, file);
  if (!rel || rel.startsWith('..') || path.isAbsolute(rel)) return false;
  const parts = rel.split(path.sep);
  if (parts.some(p => p.startsWith('.'))) return false;
  if (BLOCKED_TOP.has(parts[0].toLowerCase())) return false;
  return SERVED_TYPES.has(path.extname(file).toLowerCase());
}

// fetch that checks every redirect hop BEFORE requesting it (a plain `redirect: follow` would already have contacted the untrusted host).
async function fetchChecked(fetchImpl, url, options, hostOk, maxHops = 5) {
  let u = url;
  for (let hop = 0; hop <= maxHops; hop++) {
    if (!hostOk(u)) throw Object.assign(new Error('download redirected to an untrusted host'), { status: 502 });
    const res = await fetchImpl(u, { ...options, redirect: 'manual' });
    const loc = res && res.headers && typeof res.headers.get === 'function' ? res.headers.get('location') : null;
    if (res && res.status >= 300 && res.status < 400 && loc) { u = new URL(loc, u).toString(); continue; }
    return res;
  }
  throw Object.assign(new Error('too many redirects'), { status: 502 });
}

module.exports = { HEADERS, createLimiter, staticAllowed, fetchChecked, SERVED_TYPES, BLOCKED_TOP };
