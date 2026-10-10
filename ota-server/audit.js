'use strict';
// Audit log of the OTA server: who did what to the update server, and when.
// An append-only JSON-lines file (<data>/audit.jsonl), one entry per line:
//   { seq, t, actor: { fp, ip, ua }, action, target, detail, status, result, prev, hash }
//   actor.fp   first 8 hex characters of the SHA-256 of the admin token that was used (identifies the token, reveals nothing; never set for a refused attempt)
//   result     ok | rejected (the server refused the request: bad checksum, bad signature, not newer ...) | denied (no / wrong token, rate limit) | error
//   hash       SHA-256 over prev + the entry; prev = the hash of the line before. Editing, deleting or inserting a line breaks the chain: verify() finds where.
// Rotating the file (move it away) starts a new chain; the first entry of a file is taken as it is. Values are cut to a sane length and stripped of control characters.
const fs = require('node:fs');
const crypto = require('node:crypto');

const ZERO = '0'.repeat(64);
const clean = (v, n = 300) => String(v == null ? '' : v).replace(/[\u0000-\u001f\u007f]/g, ' ').slice(0, n);
const sha = s => crypto.createHash('sha256').update(s).digest('hex');
function cleanDetail(d, depth = 0) {
  if (d == null) return null;
  if (typeof d === 'string') return clean(d);
  if (typeof d === 'number' || typeof d === 'boolean') return d;
  if (Array.isArray(d)) return depth > 2 ? [] : d.slice(0, 40).map(x => cleanDetail(x, depth + 1));
  if (typeof d === 'object') { if (depth > 2) return {}; const o = {}; for (const k of Object.keys(d).slice(0, 30)) o[clean(k, 40)] = cleanDetail(d[k], depth + 1); return o; }
  return null;
}
const body = e => JSON.stringify({ seq: e.seq, t: e.t, actor: e.actor, action: e.action, target: e.target, detail: e.detail, status: e.status, result: e.result });
const hashOf = (prev, e) => sha(prev + body(e));

function createAudit({ file, now = () => new Date() } = {}) {
  if (!file) throw new Error('file is required');
  let seq = 0, prev = ZERO;
  const lines = () => { try { return fs.readFileSync(file, 'utf8').split('\n').filter(Boolean); } catch (_) { return []; } };
  const parse = l => { try { return JSON.parse(l); } catch (_) { return null; } };
  (function init() {                                       // continue the chain of an existing file
    const ls = lines(); if (!ls.length) return;
    const last = ls[ls.length - 1], e = parse(last);
    if (e && typeof e.seq === 'number' && /^[0-9a-f]{64}$/.test(e.hash || '')) { seq = e.seq; prev = e.hash; } else { seq = ls.length; prev = sha(last); }   // a damaged last line: the next entry no longer chains, verify() reports it
  })();

  function record({ actor = {}, action, target = '', detail = null, status = 0, result = 'ok' }) {
    const e = { seq: ++seq, t: now().toISOString(), actor: { fp: clean(actor.fp || '', 16), ip: clean(actor.ip || '', 64), ua: clean(actor.ua || '', 120) }, action: clean(action, 60), target: clean(target, 200), detail: cleanDetail(detail), status: Number(status) || 0, result: clean(result, 12) };
    e.prev = prev; e.hash = hashOf(prev, e); prev = e.hash;
    fs.appendFileSync(file, JSON.stringify(e) + '\n', { mode: 0o640 });
    return e;
  }
  // newest first; filters: action (exact or "prefix."), result, before (seq), limit
  function list({ limit = 100, before = 0, action = '', result = '' } = {}) {
    limit = Math.max(1, Math.min(500, Number(limit) || 100)); before = Number(before) || 0;
    const match = e => (!action || e.action === action || (action.endsWith('.') && e.action.startsWith(action))) && (!result || e.result === result);
    const matched = lines().map(parse).filter(e => e && match(e)).reverse(), older = before ? matched.filter(e => e.seq < before) : matched;
    return { entries: older.slice(0, limit), total: matched.length, hasMore: older.length > limit };
  }
  function verify() {
    const ls = lines(); let p = null, n = 0;
    for (let i = 0; i < ls.length; i++) {
      const e = parse(ls[i]);
      if (!e || !/^[0-9a-f]{64}$/.test(e.hash || '') || !/^[0-9a-f]{64}$/.test(e.prev || '')) return { chainOk: false, entries: n, brokenAt: i + 1, reason: 'line ' + (i + 1) + ' is not a valid entry' };
      if (p !== null && e.prev !== p) return { chainOk: false, entries: n, brokenAt: e.seq, reason: 'entry ' + e.seq + ' does not follow the one before it (a line was removed, changed or inserted)' };
      if (hashOf(e.prev, e) !== e.hash) return { chainOk: false, entries: n, brokenAt: e.seq, reason: 'entry ' + e.seq + ' was changed' };
      if (i > 0 && e.seq !== JSON.parse(ls[i - 1]).seq + 1) return { chainOk: false, entries: n, brokenAt: e.seq, reason: 'entry numbers jump at ' + e.seq + ' (entries are missing)' };
      p = e.hash; n++;
    }
    return { chainOk: true, entries: n, brokenAt: null, reason: '' };
  }
  return { record, list, verify, exportText: () => (fs.existsSync(file) ? fs.readFileSync(file, 'utf8') : ''), count: () => lines().length, file };
}

module.exports = { createAudit, ZERO };
