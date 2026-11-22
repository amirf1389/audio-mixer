'use strict';
// 3. C / C++: unsafe library calls in our own sources, cppcheck / clang-tidy when installed, and compiler / linker hardening in CMake.
const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { matchAll, stripC } = require('../lib/core');

const CHECK = 'cpp';
const TEST = /(^|\/)(test|tests)\/|_test\.(c|cc|cpp)$|(^|\/)test_[^/]+$/;
const C_EXT = /\.(c|cc|cpp|cxx|c\+\+|h|hh|hpp|hxx|inl|ipp|m|mm)$/i;

// the call must be a free function (not a member like x.system( or foo::strcpy( of another class); std:: and :: are allowed
const call = name => new RegExp('(?:^|[^\\w.>:])((?:std)?::)?(' + name + ')\\s*\\(', 'g');
const RULES = [
  { id: 'gets', re: call('gets'), sev: 'HIGH', title: 'gets() reads without any bound' },
  { id: 'strcpy', re: call('strcpy|wcscpy|_tcscpy|lstrcpy[AW]?|StrCpy[AW]?'), sev: 'MEDIUM', title: 'strcpy-family copy without a bound' },
  { id: 'strcat', re: call('strcat|wcscat|_tcscat|lstrcat[AW]?|StrCat[AW]?'), sev: 'MEDIUM', title: 'strcat-family append without a bound' },
  { id: 'sprintf', re: call('sprintf|vsprintf|swprintf|wsprintf[AW]?|_stprintf'), sev: 'MEDIUM', title: 'sprintf without a buffer size (use snprintf / std::format)' },
  { id: 'scanf-s', re: /(?:^|[^\w.>:])(?:std::)?(f|s)?scanf\s*\([^;]*%s/g, sev: 'MEDIUM', title: 'scanf %s without a width' },
  { id: 'system', re: call('system|popen|_popen|_wsystem|WinExec'), sev: 'MEDIUM', title: 'shell command execution (command injection risk if any part is not constant)' },
  { id: 'alloca', re: call('alloca|_alloca'), sev: 'LOW', title: 'alloca (stack exhaustion with a large size)' },
  { id: 'strncpy', re: call('strncpy'), sev: 'LOW', title: 'strncpy does not always NUL-terminate' },
  { id: 'atoi', re: call('atoi|atol|atof'), sev: 'INFO', title: 'ato* has no error reporting (prefer strtol / from_chars) ' },
];

function isOwnCode(rel, vendored) {
  return !vendored.some(v => rel === v || rel.startsWith(v + '/')) && !/(^|\/)(third_party|3rdparty|vendor|external)\//.test(rel);
}

// third argument of memcpy / memmove: report when it is not a constant / sizeof-bounded expression and no bound check is near
function memcpyFindings(ctx, rel, clean) {
  const lines = clean.split('\n');
  for (const h of matchAll(clean, /(?:^|[^\w.>:])(?:std::)?(memcpy|memmove|CopyMemory|RtlCopyMemory)\s*\(/)) {
    // collect the argument list (balanced parentheses)
    let i = h.index + h.m[0].length, depth = 1, args = [''], j = i;
    for (; j < clean.length && depth; j++) {
      const c = clean[j];
      if (c === '(') depth++; else if (c === ')') { depth--; if (!depth) break; }
      if (c === ',' && depth === 1) args.push(''); else args[args.length - 1] += c;
    }
    if (args.length < 3) continue;
    const n = args[2].trim();
    if (/^\d+$|sizeof|std::min|\bmin\(|\bstd::clamp|\.size\(\)\s*\*\s*sizeof|^\w+\s*\*\s*sizeof/.test(n)) continue;
    const near = lines.slice(Math.max(0, h.line - 6), h.line).join(' ');
    const ids = (n.match(/[A-Za-z_]\w*/g) || []).filter(x => !/^(size_t|static_cast|std|const|unsigned|int)$/.test(x));
    const bounded = ids.some(id => new RegExp('(if|while|assert|resize|reserve|throw|return)[^;]*\\b' + id + '\\b[^;]*(<|>|<=|>=)|\\b' + id + '\\b\\s*=\\s*(std::)?min').test(near));
    if (!bounded) ctx.add({ severity: 'LOW', check: CHECK, id: 'memcpy-size', title: h.m[1] + ' with a computed size and no bound check in the lines before it', file: rel, line: h.line, detail: 'size: ' + n.replace(/\s+/g, ' ').slice(0, 80) });
  }
}

function hardening(ctx, cfg) {
  const vendored = (cfg.vendored || []).map(v => v.path);
  const cmakeFiles = ctx.files.filter(f => /(^|\/)CMakeLists\.txt$|\.cmake$/.test(f) && isOwnCode(f, vendored));
  const projects = cmakeFiles.filter(f => /CMakeLists\.txt$/.test(f) && /^\s*project\s*\(/m.test(ctx.read(f) || ''));
  const sharedHardening = ctx.read('scripts/security/hardening.cmake') || '';
  for (const proj of projects) {
    const dir = path.posix.dirname(proj);
    // everything this project's CMake can see: its own files, the .cmake files it includes
    let text = cmakeFiles.filter(f => f === proj || f.startsWith(dir + '/')).map(f => ctx.read(f) || '').join('\n');
    const inc = [...text.matchAll(/include\s*\(\s*"?([^")\s]+\.cmake)"?/g)].map(m => m[1]);
    for (const i of inc) {
      const name = path.posix.basename(i.replace(/\$\{[^}]+\}\/?/g, ''));
      const hit = cmakeFiles.find(f => path.posix.basename(f) === name);
      if (hit) text += '\n' + (ctx.read(hit) || '');
      if (name === 'hardening.cmake') text += '\n' + sharedHardening;
    }
    const android = /android/i.test(proj) || /ANDROID_NDK|ANDROID_ABI/.test(text);
    if (android) { ctx.add({ severity: 'INFO', check: CHECK, id: 'hardening-ndk', title: 'Android NDK build: stack protector, FORTIFY, PIE and RELRO are NDK defaults', file: proj }); continue; }
    const want = [
      { id: 'stack-protector', re: /-fstack-protector(-strong|-all)|\/GS\b/, sev: 'MEDIUM', title: 'stack protector (-fstack-protector-strong)' },
      { id: 'fortify', re: /_FORTIFY_SOURCE/, sev: 'MEDIUM', title: '_FORTIFY_SOURCE=2/3 in optimised builds' },
      { id: 'pie', re: /-pie\b|check_pie_supported|-fPIE/, sev: 'LOW', title: 'position-independent executables (ASLR): -fPIE / -pie or check_pie_supported()' },
      { id: 'relro', re: /-z,?relro|-z,?now|relro/, sev: 'LOW', title: 'full RELRO (-Wl,-z,relro,-z,now)' },
      { id: 'win-aslr-dep', re: /\/DYNAMICBASE|--dynamicbase|\/NXCOMPAT|--nxcompat/i, sev: 'LOW', title: 'Windows ASLR / DEP stated explicitly (/DYNAMICBASE /NXCOMPAT, MinGW --dynamicbase --nxcompat)' },
      { id: 'win-cfg', re: /\/guard:cf|-mguard=cf/i, sev: 'LOW', title: 'Windows Control Flow Guard (/guard:cf, clang -mguard=cf)' },
    ];
    const windows = /WIN32|MSVC|MINGW|mingw/.test(text) || ctx.files.some(f => /mingw-cross\.cmake$/.test(f));
    for (const w of want) {
      if (w.id.startsWith('win') && !windows) continue;
      if (w.re.test(text)) ctx.add({ severity: 'INFO', check: CHECK, id: 'hardening-' + w.id, title: 'hardening present: ' + w.title, file: proj });
      else ctx.add({ severity: w.sev, check: CHECK, id: 'hardening-' + w.id, title: 'hardening flag missing: ' + w.title, file: proj, line: 1, detail: 'include(scripts/security/hardening.cmake) from this project sets all of these' });
    }
  }
  // Visual Studio projects
  for (const rel of ctx.files.filter(f => /\.vcxproj$/.test(f))) {
    const t = ctx.read(rel) || '';
    if (/<RandomizedBaseAddress>\s*false|<DataExecutionPrevention>\s*false/i.test(t)) ctx.add({ severity: 'HIGH', check: CHECK, id: 'vcxproj-aslr-dep-off', title: 'ASLR or DEP switched off in a Visual Studio project', file: rel });
    if (!/<ControlFlowGuard>\s*Guard/i.test(t)) ctx.add({ severity: 'LOW', check: CHECK, id: 'vcxproj-cfg', title: 'Control Flow Guard not enabled in Visual Studio project', file: rel, detail: '<ControlFlowGuard>Guard</ControlFlowGuard>' });
    if (/<BufferSecurityCheck>\s*false/i.test(t)) ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'vcxproj-gs-off', title: '/GS buffer security check switched off', file: rel });
  }
}

function findCompileDb(ctx) {
  const cands = [ctx.opts.compileDb, path.join(ctx.root, 'build'), path.join(ctx.root, 'engine/cpp/build'), '/tmp/amixbuild'].filter(Boolean);
  for (const c of cands) { const f = c.endsWith('.json') ? c : path.join(c, 'compile_commands.json'); if (fs.existsSync(f)) return f; }
  return null;
}

function have(tool) { const r = spawnSync(tool, ['--version'], { encoding: 'utf8', timeout: 10000 }); return r.status === 0; }

function externalTools(ctx, cfg, ownFiles) {
  if (ctx.opts.noTools) { ctx.note(CHECK, 'external analysers skipped (--no-tools)'); return; }
  const focus = ctx.opts.deep ? ownFiles.filter(f => /\.(c|cc|cpp|cxx)$/.test(f)) : ownFiles.filter(f => /\.(c|cc|cpp|cxx)$/.test(f) && (cfg.clangTidyFocus || []).some(d => f.startsWith(d + '/')));
  // cppcheck: works without a compile database
  if (have('cppcheck')) {
    const r = spawnSync('cppcheck', ['--enable=warning,portability', '--inconclusive', '--quiet', '--template={file}:{line}:{severity}:{id}:{message}', ...focus.map(f => path.join(ctx.root, f))], { encoding: 'utf8', timeout: 600000, maxBuffer: 64 * 1024 * 1024 });
    let n = 0;
    for (const l of String(r.stderr || '').split('\n')) {
      const m = /^(.+?):(\d+):(\w+):([\w-]+):(.*)$/.exec(l); if (!m) continue; n++;
      ctx.add({ severity: m[3] === 'error' ? 'MEDIUM' : 'LOW', check: CHECK, id: 'cppcheck-' + m[4], title: 'cppcheck: ' + m[5].trim().slice(0, 140), file: path.relative(ctx.root, m[1]).split(path.sep).join('/'), line: Number(m[2]) });
    }
    ctx.note(CHECK, `cppcheck ran on ${focus.length} files: ${n} reports`);
  } else ctx.note(CHECK, 'cppcheck is not installed: skipped (apt install cppcheck to enable)');
  // clang-tidy: needs a compile database for include paths
  if (!have('clang-tidy')) { ctx.note(CHECK, 'clang-tidy is not installed: skipped'); return; }
  const db = findCompileDb(ctx);
  if (!db) { ctx.note(CHECK, 'clang-tidy is installed but no compile_commands.json was found (configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON and pass --compile-db <build dir>): skipped'); return; }
  const listed = new Set((() => { try { return JSON.parse(fs.readFileSync(db, 'utf8')).map(e => path.resolve(e.directory || '', e.file)); } catch (_) { return []; } })());
  const files = focus.map(f => path.join(ctx.root, f)).filter(f => listed.has(f));
  const checks = '-*,clang-analyzer-security.*,clang-analyzer-unix.cstring.*,bugprone-unsafe-functions,cert-err34-c,cert-env33-c,cert-msc50-cpp,bugprone-not-null-terminated-result';
  let n = 0;
  for (const f of files) {
    const r = spawnSync('clang-tidy', ['-p', path.dirname(db), '--quiet', '--checks=' + checks, f], { encoding: 'utf8', timeout: 180000, maxBuffer: 32 * 1024 * 1024 });
    for (const l of String(r.stdout || '').split('\n')) {
      const m = /^(.+?):(\d+):\d+: (warning|error): (.*) \[([\w.,-]+)\]$/.exec(l); if (!m) continue;
      const rel = path.relative(ctx.root, m[1]).split(path.sep).join('/'); if (rel.startsWith('..')) continue; n++;
      ctx.add({ severity: /security|unsafe/.test(m[5]) ? 'MEDIUM' : 'LOW', check: CHECK, id: 'clang-tidy-' + m[5], title: 'clang-tidy: ' + m[4].slice(0, 140), file: rel, line: Number(m[2]) });
    }
  }
  ctx.note(CHECK, `clang-tidy (${checks}) ran on ${files.length} files with ${path.relative(ctx.root, db) || db}: ${n} reports`);
}

function run(ctx, cfg) {
  const vendored = (cfg.vendored || []).map(v => v.path);
  const all = ctx.files.filter(f => C_EXT.test(f));
  const own = all.filter(f => isOwnCode(f, vendored));
  const counts = {};
  for (const rel of own) {
    const raw = ctx.read(rel); if (raw === null) continue;
    const clean = stripC(raw);
    for (const r of RULES) for (const h of matchAll(clean, r.re)) {
      // `system(` used as a variable / Qt property name is not a call to the C library: require a preceding statement boundary-ish token
      const lineText = clean.split('\n')[h.line - 1];
      if (r.id === 'system' && /\b(bool|void|QVariantMap|QString|auto|int)\s+(\w+::)?system\s*\(/.test(lineText)) continue;
      if (r.id === 'system' && /^\s*\)/.test(clean.slice(h.index + h.m[0].length))) continue;   // system() with no argument is a member, not the C call
      if (r.id === 'system' && /(Q_INVOKABLE|const\s*\{|\)\s*const)/.test(lineText) && !/std::system|::system\s*\(/.test(lineText)) continue;
      counts[r.id] = (counts[r.id] || 0) + 1;
      if (r.sev === 'INFO') continue;   // counted in the notes only
      const sev = TEST.test(rel) && r.sev !== 'HIGH' ? 'LOW' : r.sev;   // test programs never ship
      ctx.add({ severity: sev, check: CHECK, id: 'unsafe-' + r.id, title: r.title, file: rel, line: h.line, detail: raw.split('\n')[h.line - 1].trim().slice(0, 120) });
    }
    memcpyFindings(ctx, rel, clean);
  }
  ctx.note(CHECK, `C/C++: ${own.length} own source files scanned (${all.length - own.length} vendored files excluded); unsafe calls: ${JSON.stringify(counts)}`);
  hardening(ctx, cfg);
  externalTools(ctx, cfg, own);
}

module.exports = { run, CHECK, RULES };
