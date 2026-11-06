// tools/web/run_trace.js -- run one port --trace scenario in the Emscripten build under node
// (#59 step 2 coverage).  The browser only ever sees interactive play, so this drives the
// engines headlessly but with --yield-test: every present callback yields through ASYNCIFY,
// exactly as the interactive build does, so a yield path missing from the instrumented set
// shows up as a crash or a trace that differs from the native one.
//
// usage: node run_trace.js BUILD_DIR GAME_DIR LOAD_NAME OUT_TRACE_DIR SCRIPT [extra port args...]
//   GAME_DIR  flat directory of game files (symlinks followed); LOAD_NAME the start save
'use strict';
const fs = require('fs'), path = require('path');
const [build, gdir, load, out, script, ...extra] = process.argv.slice(2);
const log = [];
let done = false;
const t0 = Date.now();
global.Module = {
  print: t => log.push(t), printErr: t => log.push(t),
  onRuntimeInitialized() {
    const FS = Module.FS;
    FS.mkdir('/game'); FS.mkdir('/trace'); FS.mkdir('/state');
    for (const f of fs.readdirSync(gdir)) {
      const p = path.join(gdir, f);
      if (fs.statSync(p).isFile()) FS.writeFile('/game/' + f, fs.readFileSync(p));
    }
    Module.callMain(['--dir', '/game', '--state-dir', '/state', '--load', load, '--trace', '/trace',
                     '--script', script, '--yield-test', ...extra]);
    poll();
  },
};
let last = '', stable = 0;
function lastLine() {
  try { const t = Module.FS.readFile('/trace/frames.jsonl', { encoding: 'utf8' }); return t.length + ':' + t.slice(-200); } catch (e) { return ''; }
}
function finish(why) {
  const FS = Module.FS; fs.mkdirSync(out, { recursive: true });
  for (const f of FS.readdir('/trace')) {
    if (f[0] === '.') continue;
    fs.writeFileSync(path.join(out, f), FS.readFile('/trace/' + f));
  }
  fs.writeFileSync(path.join(out, '..', path.basename(out) + '.log'), log.join('\n'));
  console.log(why, 'in', ((Date.now() - t0) / 1000).toFixed(1) + 's');
  process.exit(0);
}
function poll() {
  const l = lastLine();
  if (l && l === last) stable++; else stable = 0;
  last = l;
  if (log.some(x => /RuntimeError|abort|unreachable/.test(x))) return finish('CRASH');
  if (stable >= 20) return finish('done');                 // 20 x 250 ms with no new trace output
  if (Date.now() - t0 > 600000) return finish('TIMEOUT');
  setTimeout(poll, 250);
}
process.on('uncaughtException', e => { log.push('uncaught ' + e.message); try { finish('CRASH ' + e.message); } catch (x) { console.log('CRASH', e.message); process.exit(1); } });
process.on('unhandledRejection', e => { log.push('rejected ' + e); });
// zeliard.js is a classic script (`var Module = Module || {}`): run it in the global scope,
// not as a CommonJS module, or it cannot see the Module object above.
global.require = require; global.__dirname = path.resolve(build); global.__filename = path.resolve(build, 'zeliard.js');
require('vm').runInThisContext(fs.readFileSync(global.__filename, 'utf8'), { filename: global.__filename });
