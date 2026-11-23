'use strict';
// 8. DEPENDENCIES: vendored third-party code with its licence, licence files that are missing, copyleft code to keep an eye on, and the
// package-manager dependencies declared by each part (npm, Gradle, pip).
const path = require('node:path');

const CHECK = 'deps';
// licence texts by name (LICENSE, LICENSE.txt, LICENSE_1_0.txt, COPYING.LGPL, ...), not code that happens to be called license-*.js
const LICENCE_FILE = /(^|\/)(LICEN[CS]E|COPYING|NOTICE|UNLICENSE)([._-][^/]*)?$/i;
const NOT_LICENCE_TEXT = /\.(js|mjs|cjs|ts|kt|java|c|cc|cpp|h|hpp|html?|qml|py|json|bat|cmd|command|sh|ps1|pdf|docx?)$/i;

function detectLicence(text) {
  const t = String(text || '').slice(0, 20000);
  const tests = [
    ['AGPL', /GNU AFFERO GENERAL PUBLIC LICENSE/i], ['LGPL', /GNU LESSER GENERAL PUBLIC LICENSE|GNU Library General Public/i], ['GPL', /GNU GENERAL PUBLIC LICENSE/i],
    ['Apache-2.0', /Apache License,?\s+Version 2\.0/i], ['MPL-2.0', /Mozilla Public License/i], ['Boost-1.0', /Boost Software License/i],
    ['MIT', /Permission is hereby granted, free of charge/i], ['BSD', /Redistribution and use in source and binary forms/i], ['zlib', /This software is provided 'as-is'/i],
    ['Public domain / Unlicense', /public domain|unlicense\.org/i], ['Steinberg', /Steinberg/i],
  ];
  return tests.filter(([, re]) => re.test(t)).map(([n]) => n).join(' + ') || 'unknown';
}

function run(ctx, cfg) {
  const rows = [];
  const configured = cfg.vendored || [];
  const seen = new Set();
  for (const v of configured) {
    seen.add(v.path);
    const inDir = ctx.files.filter(f => f.startsWith(v.path + '/'));
    if (!inDir.length) { ctx.add({ severity: 'INFO', check: CHECK, id: 'vendored-absent', title: 'configured vendored path not in the tree: ' + v.path }); continue; }
    const lic = inDir.filter(f => LICENCE_FILE.test(f) && !NOT_LICENCE_TEXT.test(f) && f.split('/').length - v.path.split('/').length <= 2);
    const extra = v.licenceFile && ctx.exists(v.licenceFile) ? [v.licenceFile] : [];
    let inline = false;
    if (!lic.length && !extra.length && v.inlineLicence) inline = inDir.some(f => /licen[cs]e|public domain/i.test((ctx.read(f) || '').slice(-12000)));
    const detected = lic.length ? lic.map(f => detectLicence(ctx.read(f))).join(', ') : extra.length ? 'see ' + extra[0] : inline ? 'inline in sources' : 'NONE';
    rows.push({ path: v.path, name: v.name, files: inDir.length, licence: v.licence, licenceFiles: [...lic, ...extra].join(', ') || (inline ? '(inline)' : '-'), detected });
    if (!lic.length && !extra.length && !inline) ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'licence-missing', title: 'vendored code without a licence file: ' + v.name, file: v.path, detail: 'add the upstream LICENSE / COPYING file next to the code' });
    else if (!lic.length && inline) ctx.add({ severity: 'INFO', check: CHECK, id: 'licence-inline', title: 'licence only inside the source files: ' + v.name, file: v.path });
    else if (!lic.length && extra.length) ctx.add({ severity: 'LOW', check: CHECK, id: 'licence-elsewhere', title: 'licence kept outside the vendored folder (' + extra[0] + '): ' + v.name, file: v.path });
    if (/GPL/.test(detected + v.licence)) ctx.add({ severity: /AGPL/.test(detected) ? 'MEDIUM' : 'INFO', check: CHECK, id: 'copyleft', title: `copyleft licence (${v.licence}): ${v.name} - keep the linking / source-offer obligations in docs/LICENSING-REVIEW.md`, file: v.path });
  }
  // auto-detected: folders below the top level that carry their own licence file and are not configured
  const roots = ['', ...seen];
  for (const f of ctx.files.filter(f => LICENCE_FILE.test(f) && !NOT_LICENCE_TEXT.test(f))) {
    const dir = path.posix.dirname(f); if (dir === '.' || roots.some(r => r && (dir === r || dir.startsWith(r + '/')))) continue;
    if (/^(docs|licenses|scripts\/security)(\/|$)/.test(dir)) continue;
    seen.add(dir);
    const name = dir.split('/').slice(-1)[0];
    rows.push({ path: dir, name: name + ' (auto-detected)', files: ctx.files.filter(x => x.startsWith(dir + '/')).length, licence: '?', licenceFiles: f, detected: detectLicence(ctx.read(f)) });
    ctx.add({ severity: 'LOW', check: CHECK, id: 'vendored-unlisted', title: 'folder with its own licence file is not in the vendored list: ' + dir, file: f, detail: 'add it to scripts/security/config.json "vendored" so it is excluded from own-code checks and listed here' });
  }
  // well-known single-file libraries outside the list
  for (const f of ctx.files) {
    if (seen.has(path.posix.dirname(f)) || [...seen].some(s => f.startsWith(s + '/'))) continue;
    if (/(^|\/)(miniaudio|stb_[a-z_]+|dr_(wav|mp3|flac)|kissfft|pffft|nlohmann|json\.hpp|sqlite3|tinyxml2|cJSON|httplib|picojson|utf8)\.(h|hpp|c|cpp)$/i.test(f)) {
      const t = ctx.read(f) || '';
      if (/Copyright|SPDX|licen[cs]e/i.test(t.slice(0, 4000) + t.slice(-8000)) && !/AmF|amfb|A mix OS/i.test(t.slice(0, 600))) ctx.add({ severity: 'LOW', check: CHECK, id: 'single-file-lib', title: 'third-party single-file library not in the vendored list: ' + f, file: f });
    }
  }
  // the project's own licence and the third-party notices
  if (!ctx.files.some(f => /^(LICEN[CS]E|COPYING)(\.\w+)?$/i.test(f))) ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'no-root-licence', title: 'no LICENSE file at the repository root' });
  // package-manager dependencies (inventory)
  const pkgs = [];
  for (const f of ctx.files.filter(f => /(^|\/)package\.json$/.test(f))) {
    let j; try { j = JSON.parse(ctx.read(f) || '{}'); } catch (_) { continue; }
    const deps = Object.assign({}, j.dependencies, j.optionalDependencies);
    const dev = Object.keys(j.devDependencies || {});
    pkgs.push({ manifest: f, deps: Object.entries(deps).map(([n, v]) => n + '@' + v), dev });
    for (const [n, v] of Object.entries(deps)) if (/^(\*|latest|x|>=?\s*0|github:|git\+|https?:)/.test(String(v))) ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'unpinned-dep', title: `npm dependency not pinned to a version range: ${n}@${v}`, file: f });
    if ((Object.keys(deps).length || dev.length) && !ctx.files.includes(path.posix.join(path.posix.dirname(f), 'package-lock.json')) && path.posix.dirname(f) !== '.') ctx.add({ severity: 'LOW', check: CHECK, id: 'no-lockfile', title: 'npm dependencies without a package-lock.json', file: f });
  }
  for (const f of ctx.files.filter(f => /build\.gradle(\.kts)?$/.test(f))) {
    const t = ctx.read(f) || '';
    const deps = [...t.matchAll(/(implementation|api|kapt|ksp|testImplementation)\s*\(?\s*["']([^"']+)["']/g)].map(m => m[2]);
    pkgs.push({ manifest: f, deps: deps.filter(d => !/^project/.test(d)), dev: [] });
    for (const d of deps) if (/:\+$|latest\.release|SNAPSHOT/.test(d)) ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'unpinned-dep', title: 'Gradle dependency not pinned: ' + d, file: f });
  }
  for (const f of ctx.files.filter(f => /(^|\/)requirements([-_.][\w]+)?\.txt$/.test(f) && !/_dox\.txt$/.test(f) && ![...seen].some(v => f.startsWith(v + '/')))) {
    const deps = (ctx.read(f) || '').split('\n').map(l => l.trim()).filter(l => l && !l.startsWith('#'));
    pkgs.push({ manifest: f, deps, dev: [] });
    for (const d of deps) if (!/[=<>~]=|===/.test(d)) ctx.add({ severity: 'LOW', check: CHECK, id: 'unpinned-dep', title: 'Python requirement without a version: ' + d, file: f });
  }
  ctx.sections.vendored = rows;
  ctx.sections.packages = pkgs;
  ctx.note(CHECK, `${rows.length} vendored components, ${pkgs.length} dependency manifests`);
}

module.exports = { run, CHECK, detectLicence };
