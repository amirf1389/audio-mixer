'use strict';
// Shared helpers of the security audit: the file list, cached reads, findings, and small text utilities.
// Offline only: nothing here talks to the network. Plain Node.js, no packages.
const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');

const SEVERITIES = ['HIGH', 'MEDIUM', 'LOW', 'INFO'];
const RANK = { HIGH: 0, MEDIUM: 1, LOW: 2, INFO: 3 };

// Paths never scanned: generated output, dependency caches and the checker's own planted fixtures.
const ALWAYS_SKIP = [
  /(^|\/)node_modules\//, /(^|\/)\.git\//, /(^|\/)\.gradle\//, /(^|\/)build\//, /^build-[^/]+\//, /^b-[^/]+\//,
  /^scripts\/security\/test-fixtures\//, /^security-report\.(md|json)$/,
];
const BINARY_EXT = /\.(png|jpe?g|gif|webp|ico|icns|bmp|tiff?|psd|pdf|docx?|xlsx?|pptx?|zip|7z|gz|tgz|xz|bz2|zst|tar|rar|apk|aab|aar|jar|class|dex|so|dll|exe|dylib|a|lib|o|obj|wasm|bin|dat|wav|flac|mp3|ogg|opus|m4a|aac|mp4|mkv|mov|webm|avi|ttf|otf|woff2?|eot|sqlite|db|pyc)$/i;
const MAX_TEXT = 2 * 1024 * 1024;

function listFiles(root, { extraSkip = [] } = {}) {
  const skip = rel => ALWAYS_SKIP.some(r => r.test(rel)) || extraSkip.some(r => r.test(rel));
  let rels = null;
  try {
    const top = execFileSync('git', ['-C', root, 'rev-parse', '--show-toplevel'], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] }).trim();
    if (path.resolve(top) === path.resolve(root)) {
      // tracked files plus untracked files that are not ignored (a forgotten .env must be seen before it is committed)
      const out = execFileSync('git', ['-C', root, 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], { encoding: 'utf8', maxBuffer: 256 * 1024 * 1024, stdio: ['ignore', 'pipe', 'ignore'] });
      rels = [...new Set(out.split('\0').filter(Boolean))].filter(r => { try { return fs.statSync(path.join(root, r)).isFile(); } catch (_) { return false; } });
    }
  } catch (_) { rels = null; }
  if (!rels) {
    rels = [];
    const walk = dir => {
      for (const e of fs.readdirSync(path.join(root, dir), { withFileTypes: true })) {
        const rel = dir ? dir + '/' + e.name : e.name;
        if (e.isDirectory()) { if (!skip(rel + '/')) walk(rel); } else if (e.isFile()) rels.push(rel);
      }
    };
    walk('');
  }
  return rels.map(r => r.split(path.sep).join('/')).filter(r => !skip(r)).sort();
}

function createContext(root, opts = {}) {
  const files = listFiles(root, opts);
  const cache = new Map();
  const read = rel => {
    if (cache.has(rel)) return cache.get(rel);
    let t = null;
    try {
      const abs = path.join(root, rel), st = fs.statSync(abs);
      if (st.size <= MAX_TEXT && !BINARY_EXT.test(rel)) {
        const b = fs.readFileSync(abs);
        if (!b.subarray(0, 8000).includes(0)) t = b.toString('utf8');
      }
    } catch (_) { t = null; }
    cache.set(rel, t); return t;
  };
  const findings = [];
  const notes = [];
  const sections = {};
  const ctx = {
    root, files, read, findings, notes, sections, opts,
    exists: rel => fs.existsSync(path.join(root, rel)),
    textFiles: re => files.filter(f => (!re || re.test(f)) && read(f) !== null),
    add(f) {
      if (!SEVERITIES.includes(f.severity)) throw new Error('bad severity ' + f.severity);
      findings.push({ severity: f.severity, check: f.check, id: f.id, title: f.title, file: f.file || '', line: f.line || 0, detail: f.detail || '' });
    },
    note: (check, text) => notes.push({ check, text }),
  };
  return ctx;
}

// 1-based line number of a character offset
function lineOf(text, index) { let n = 1; for (let i = 0; i < index && i < text.length; i++) if (text.charCodeAt(i) === 10) n++; return n; }

// every match of a global regex with its line
function matchAll(text, re) {
  const out = []; const g = new RegExp(re.source, re.flags.includes('g') ? re.flags : re.flags + 'g');
  let m; while ((m = g.exec(text))) { out.push({ m, index: m.index, line: lineOf(text, m.index) }); if (m[0] === '') g.lastIndex++; }
  return out;
}

function shannon(s) {
  const f = new Map(); for (const c of s) f.set(c, (f.get(c) || 0) + 1);
  let h = 0; for (const n of f.values()) { const p = n / s.length; h -= p * Math.log2(p); } return h;
}

// C / C++ source with comments and the contents of string / char literals blanked (newlines kept, so line numbers stay right)
function stripC(src) {
  let out = '', i = 0; const n = src.length;
  const blank = s => s.replace(/[^\n]/g, ' ');
  while (i < n) {
    const c = src[i], d = src[i + 1];
    if (c === '/' && d === '/') { let j = src.indexOf('\n', i); if (j < 0) j = n; out += blank(src.slice(i, j)); i = j; continue; }
    if (c === '/' && d === '*') { let j = src.indexOf('*/', i + 2); j = j < 0 ? n : j + 2; out += blank(src.slice(i, j)); i = j; continue; }
    if (c === 'R' && d === '"' && !/[\w]/.test(src[i - 1] || '')) {
      const open = src.indexOf('(', i + 2); const delim = open > 0 ? src.slice(i + 2, open) : '';
      if (open > 0 && delim.length <= 16 && !/[\s\\()]/.test(delim)) { let j = src.indexOf(')' + delim + '"', open); j = j < 0 ? n : j + delim.length + 2; out += 'R"' + blank(src.slice(i + 2, j - 1)) + '"'; i = j; continue; }
    }
    if (c === '"' || (c === "'" && !/[0-9a-fA-F]/.test(src[i - 1] || ''))) {   // 1'000 digit separators are not char literals
      let j = i + 1; while (j < n && src[j] !== c && src[j] !== '\n') { if (src[j] === '\\') j++; j++; }
      out += c + blank(src.slice(i + 1, j)) + (j < n && src[j] === c ? c : ''); i = j < n && src[j] === c ? j + 1 : j; continue;
    }
    out += c; i++;
  }
  return out;
}

// JS / Kotlin / Python: comments blanked (strings kept, the checks need their content). Good enough for line-oriented heuristics.
function stripLineComments(src, marker = '//') {
  return src.split('\n').map(l => {
    const t = l.trimStart();
    if (t.startsWith(marker) || t.startsWith('*') || t.startsWith('/*')) return '';
    return l;
  }).join('\n');
}

function sortFindings(list) {
  return list.slice().sort((a, b) => RANK[a.severity] - RANK[b.severity] || a.check.localeCompare(b.check) || a.file.localeCompare(b.file) || a.line - b.line);
}

function globToRe(g) {
  return new RegExp('^' + g.split('**').map(p => p.split('*').map(s => s.replace(/[.+?^${}()|[\]\\]/g, '\\$&')).join('[^/]*')).join('.*') + '$');
}

module.exports = { SEVERITIES, RANK, listFiles, createContext, lineOf, matchAll, shannon, stripC, stripLineComments, sortFindings, globToRe, BINARY_EXT };
