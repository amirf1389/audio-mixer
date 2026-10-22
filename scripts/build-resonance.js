#!/usr/bin/env node
'use strict';
// Builds the engine's optional 360 degree output (engine/cpp, am::Spatial) on the Resonance Audio engine and runs its test:
//   node scripts/build-resonance.js [--dir dist/cache/resonance]
// Fetches google/resonance-audio (Apache-2.0) at a pinned commit plus the two libraries its CMake build wants (Eigen 3.4.0, pffft), builds libResonanceAudioStatic.a,
// then configures engine/cpp with -DAM_RESONANCE_INCLUDE / -DAM_RESONANCE_LIB and runs ctest (spatial_test: left / right ear levels, latency, the Runtime path).
// Nothing from the sources is copied into this repository. Needs git, cmake and a C++ compiler; needs internet once.
const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const ROOT = path.resolve(__dirname, '..');
const dirArg = process.argv.indexOf('--dir');
const WORK = path.resolve(ROOT, dirArg > 0 ? process.argv[dirArg + 1] : path.join('dist', 'cache', 'resonance'));
const PINS = {
  resonance: { url: 'https://github.com/resonance-audio/resonance-audio.git', rev: '4556a46afd4ffae092aa281bfd072eb0279d3a29', dir: 'src' },
  eigen: { url: 'https://gitlab.com/libeigen/eigen.git', rev: '3147391d946bb4b6c68edd901f2add6ac1f31f8c', dir: 'src/third_party/eigen' },     // 3.4.0 (master needs C++14, the project builds as C++11)
  pffft: { url: 'https://bitbucket.org/jpommier/pffft.git', rev: '67a4a6a113179ab4ac398298f46fce8dd60498dc', dir: 'src/third_party/pffft' },
};
function run(cmd, args, opts = {}) {
  const r = spawnSync(cmd, args, { encoding: 'utf8', ...opts });
  if (r.status !== 0) throw new Error(`${cmd} ${args.slice(0, 3).join(' ')} failed:\n${((r.stdout || '') + (r.stderr || '')).slice(-1500)}`);
  return r.stdout;
}
function fetchPinned(p) {
  const d = path.join(WORK, p.dir);
  if (fs.existsSync(path.join(d, '.am-rev')) && fs.readFileSync(path.join(d, '.am-rev'), 'utf8').trim() === p.rev) return d;
  fs.rmSync(d, { recursive: true, force: true }); fs.mkdirSync(d, { recursive: true });
  run('git', ['init', '-q'], { cwd: d }); run('git', ['remote', 'add', 'origin', p.url], { cwd: d });
  run('git', ['fetch', '-q', '--depth', '1', 'origin', p.rev], { cwd: d }); run('git', ['checkout', '-q', 'FETCH_HEAD'], { cwd: d });
  fs.rmSync(path.join(d, '.git'), { recursive: true, force: true });
  fs.writeFileSync(path.join(d, '.am-rev'), p.rev + '\n');
  return d;
}

function main() {
  fs.mkdirSync(WORK, { recursive: true });
  const src = fetchPinned(PINS.resonance); fetchPinned(PINS.eigen); fetchPinned(PINS.pffft);
  const lib = path.join(WORK, 'build', 'resonance_audio', 'libResonanceAudioStatic.a');
  if (!fs.existsSync(lib)) {
    run('cmake', ['-S', src, '-B', path.join(WORK, 'build'), '-DBUILD_RESONANCE_AUDIO_API=ON', '-DCMAKE_BUILD_TYPE=Release']);
    run('cmake', ['--build', path.join(WORK, 'build'), '-j', '4']);
  }
  const eng = path.join(WORK, 'engine-build');
  run('cmake', ['-S', path.join(ROOT, 'engine', 'cpp'), '-B', eng, `-DAM_RESONANCE_INCLUDE=${path.join(src, 'resonance_audio', 'api')}`, `-DAM_RESONANCE_LIB=${lib}`]);
  run('cmake', ['--build', eng, '-j', '4']);
  process.stdout.write(run('ctest', ['--test-dir', eng, '--output-on-failure']));
  console.log(`Resonance Audio library: ${lib}\nTo use it: cmake -S engine/cpp -B <dir> -DAM_RESONANCE_INCLUDE=${path.join(src, 'resonance_audio', 'api')} -DAM_RESONANCE_LIB=${lib}`);
}
try { main(); } catch (e) { console.error('Resonance Audio build failed: ' + e.message); process.exit(1); }
