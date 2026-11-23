#!/usr/bin/env node
'use strict';
// AmF X1 security audit. Static, offline, no packages: scans the repository and writes security-report.md + security-report.json.
//   node scripts/security/audit.js [options]
//     --root <dir>        repository to scan (default: the repository this script is in)
//     --out <file.md>     report path (default <root>/security-report.md; the JSON goes next to it)
//     --only a,b          run only these checks: secrets,signing,cpp,network,portal,android,web,deps
//     --fail-on <level>   exit 1 when a finding of this level or worse exists (default HIGH)
//     --offline           never try the network (npm audit is skipped with a note)
//     --no-tools          do not run cppcheck / clang-tidy even when installed
//     --deep              run clang-tidy / cppcheck on every own C/C++ file, not only the network-facing ones
//     --compile-db <dir>  build folder with compile_commands.json for clang-tidy
//     --quiet             print only the summary line
// Exit codes: 0 no finding at or above --fail-on, 1 there is one, 2 the audit itself failed.
const fs = require('node:fs');
const path = require('node:path');
const { createContext, sortFindings, RANK, SEVERITIES } = require('./lib/core');

const CHECKS = {
  secrets: require('./checks/secrets'), signing: require('./checks/signing'), cpp: require('./checks/cpp'), network: require('./checks/network'),
  portal: require('./checks/portal'), android: require('./checks/android'), web: require('./checks/web'), deps: require('./checks/deps'),
};
const TITLES = { secrets: '1. Secrets', signing: '2. Signing safety', cpp: '3. C / C++', network: '4. Network surfaces', portal: '5. Portal and deployment', android: '6. Android', web: '7. Web pages', deps: '8. Dependencies' };

function parseArgs(argv) {
  const o = { failOn: 'HIGH' };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i], v = () => argv[++i];
    if (a === '--root') o.root = v(); else if (a === '--out') o.out = v(); else if (a === '--only') o.only = v().split(',').map(s => s.trim()).filter(Boolean);
    else if (a === '--fail-on') o.failOn = String(v()).toUpperCase(); else if (a === '--offline') o.offline = true; else if (a === '--no-tools') o.noTools = true;
    else if (a === '--deep') o.deep = true; else if (a === '--compile-db') o.compileDb = v(); else if (a === '--quiet') o.quiet = true; else if (a === '--config') o.config = v();
    else if (a === '-h' || a === '--help') o.help = true; else throw new Error('unknown option ' + a);
  }
  if (!SEVERITIES.includes(o.failOn)) throw new Error('--fail-on must be one of ' + SEVERITIES.join(', '));
  return o;
}

function audit(opts = {}) {
  const root = path.resolve(opts.root || path.join(__dirname, '..', '..'));
  const cfgFile = opts.config || path.join(__dirname, 'config.json');
  const cfg = JSON.parse(fs.readFileSync(cfgFile, 'utf8'));
  const ctx = createContext(root, opts);
  const names = opts.only || Object.keys(CHECKS);
  const timings = {};
  for (const n of names) {
    if (!CHECKS[n]) throw new Error('unknown check ' + n);
    const t0 = Date.now();
    try { CHECKS[n].run(ctx, cfg); } catch (e) { ctx.add({ severity: 'MEDIUM', check: n, id: 'check-crashed', title: 'the ' + n + ' check failed to run: ' + e.message }); }
    timings[n] = Date.now() - t0;
  }
  const findings = sortFindings(ctx.findings);
  const counts = Object.fromEntries(SEVERITIES.map(s => [s, findings.filter(f => f.severity === s).length]));
  return { root, generated: new Date().toISOString(), checks: names, counts, findings, notes: ctx.notes, sections: ctx.sections, timings, filesScanned: ctx.files.length };
}

const esc = s => String(s == null ? '' : s).replace(/\|/g, '\\|').replace(/\n/g, ' ');
const loc = f => f.file ? '`' + f.file + (f.line ? ':' + f.line : '') + '`' : '';

function markdown(r) {
  const L = [];
  L.push('# Security audit report', '', `Generated ${r.generated} by \`node scripts/security/audit.js\` over ${r.filesScanned} files.`, '');
  L.push('| Severity | Count |', '|---|---|', ...SEVERITIES.map(s => `| ${s} | ${r.counts[s]} |`), '');
  L.push(r.counts.HIGH ? `**${r.counts.HIGH} HIGH finding(s): the audit exits non-zero.**` : 'No HIGH findings.', '');
  L.push('## Findings needing action (HIGH / MEDIUM)', '');
  const act = r.findings.filter(f => RANK[f.severity] <= 1);
  if (!act.length) L.push('None.', '');
  else { L.push('| Severity | Check | Where | Finding | Detail |', '|---|---|---|---|---|'); for (const f of act) L.push(`| ${f.severity} | ${f.check} | ${loc(f)} | ${esc(f.title)} | ${esc(f.detail)} |`); L.push(''); }
  for (const c of r.checks) {
    L.push('## ' + (TITLES[c] || c), '');
    const fs_ = r.findings.filter(f => f.check === c);
    const low = fs_.filter(f => f.severity === 'LOW'), info = fs_.filter(f => f.severity === 'INFO');
    const tally = SEVERITIES.map(s => s + ' ' + fs_.filter(f => f.severity === s).length).join(', ');
    L.push(tally, '');
    if (c === 'network' && r.sections.network) {
      L.push('### Listening components', '', '| Component | File | Kind | Bind (default) | Auth | Rate limit | Body limit | CORS | Routes |', '|---|---|---|---|---|---|---|---|---|');
      for (const n of r.sections.network) L.push(`| ${esc(n.component)} | \`${n.file}\` | ${esc(n.kind)} | ${esc(n.bind)} | ${n.auth === true ? 'yes' : n.auth === false ? '**no**' : n.auth} | ${n.rate === true ? 'yes' : n.rate === false ? 'no' : n.rate} | ${n.bodyLimit === true ? 'yes' : n.bodyLimit === false ? '**no**' : n.bodyLimit} | ${esc(n.cors)} | ${n.routes.length ? esc(n.routes.slice(0, 40).join(' ')) + (n.routes.length > 40 ? ` (+${n.routes.length - 40})` : '') : '-'} |`);
      L.push('');
    }
    if (c === 'deps' && r.sections.vendored) {
      L.push('### Vendored third-party code', '', '| Path | Component | Files | Licence (declared) | Licence file(s) | Detected |', '|---|---|---|---|---|---|');
      for (const v of r.sections.vendored) L.push(`| \`${v.path}\` | ${esc(v.name)} | ${v.files} | ${esc(v.licence)} | ${esc(v.licenceFiles)} | ${esc(v.detected)} |`);
      L.push('', '### Package-manager dependencies', '');
      for (const p of r.sections.packages || []) L.push(`- \`${p.manifest}\`: ${p.deps.length ? p.deps.map(esc).join(', ') : 'none'}${p.dev.length ? ' (dev: ' + p.dev.join(', ') + ')' : ''}`);
      L.push('');
    }
    if (low.length) {
      L.push('### LOW', '');
      const byTitle = new Map(); for (const f of low) { const k = f.id + '|' + f.title; if (!byTitle.has(k)) byTitle.set(k, []); byTitle.get(k).push(f); }
      for (const [, list] of byTitle) L.push(`- ${esc(list[0].title)}: ${list.slice(0, 12).map(loc).join(', ')}${list.length > 12 ? ` and ${list.length - 12} more` : ''}${list[0].detail && list.length === 1 ? ' - ' + esc(list[0].detail) : ''}`);
      L.push('');
    }
    if (info.length) {
      L.push('<details><summary>INFO (' + info.length + ')</summary>', '');
      for (const f of info.slice(0, 200)) L.push(`- ${esc(f.title)} ${loc(f)}${f.detail ? ' - ' + esc(f.detail) : ''}`);
      if (info.length > 200) L.push(`- ... ${info.length - 200} more in security-report.json`);
      L.push('', '</details>', '');
    }
    const notes = r.notes.filter(n => n.check === c);
    if (notes.length) { L.push('Notes:', ''); for (const n of notes) L.push('- ' + esc(n.text)); L.push(''); }
  }
  return L.join('\n');
}

function main(argv = process.argv.slice(2)) {
  let o; try { o = parseArgs(argv); } catch (e) { console.error(e.message); return 2; }
  if (o.help) { console.log(fs.readFileSync(__filename, 'utf8').split('\n').slice(2, 15).map(l => l.replace(/^\/\/ ?/, '')).join('\n')); return 0; }
  let r; try { r = audit(o); } catch (e) { console.error('security audit failed: ' + (e.stack || e.message)); return 2; }
  const out = path.resolve(o.out || path.join(r.root, 'security-report.md'));
  const json = out.replace(/\.md$/i, '') + '.json';
  fs.writeFileSync(out, markdown(r));
  fs.writeFileSync(json, JSON.stringify(r, null, 1));
  const bad = r.findings.filter(f => RANK[f.severity] <= RANK[o.failOn]);
  if (!o.quiet) {
    for (const f of r.findings.filter(x => x.severity === 'HIGH').slice(0, 40)) console.log(`HIGH   [${f.check}] ${f.file}${f.line ? ':' + f.line : ''}  ${f.title}`);
    for (const f of r.findings.filter(x => x.severity === 'MEDIUM').slice(0, 25)) console.log(`MEDIUM [${f.check}] ${f.file}${f.line ? ':' + f.line : ''}  ${f.title}`);
  }
  console.log(`security audit: HIGH ${r.counts.HIGH}, MEDIUM ${r.counts.MEDIUM}, LOW ${r.counts.LOW}, INFO ${r.counts.INFO}  ->  ${path.relative(process.cwd(), out) || out} (+ .json)`);
  return bad.length ? 1 : 0;
}

module.exports = { audit, markdown, parseArgs, CHECKS };
if (require.main === module) process.exitCode = main();
