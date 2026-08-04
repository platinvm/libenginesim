/*
 * Runs the shipped worklet bundle in an AudioWorkletGlobalScope lookalike.
 *
 * The point is what this scope does NOT have. A worklet has no window, self,
 * document, fetch, performance, importScripts or module loader, and code that
 * quietly depends on any of them works everywhere except where it has to.
 * Node and the browser both hide that; a bare vm context does not.
 *
 * Zero dependencies, about two seconds. Run with: node tests/worklet.mjs
 */
import { readFileSync } from 'node:fs';
import { createContext, runInContext } from 'node:vm';

const BUNDLE = new URL('../bindings/js/enginesim-worklet.js', import.meta.url);
const SAMPLE_RATE = 44100;
const QUANTUM = 128;

const failures = [];
const check = (ok, what) => {
  console.log(`${ok ? '  ok  ' : ' FAIL '} ${what}`);
  if (!ok) failures.push(what);
};

/* A MessagePort with both ends exposed, so the test can play host. */
class Port {
  constructor() { this.onmessage = null; this.sent = []; }
  postMessage(message) { this.sent.push(message); }
  deliver(message) { this.onmessage?.({ data: message }); }
  take(type) {
    const found = this.sent.filter((m) => m.type === type);
    this.sent = this.sent.filter((m) => m.type !== type);
    return found;
  }
}

const registered = new Map();
const scope = {
  sampleRate: SAMPLE_RATE,
  currentTime: 0,
  registerProcessor: (name, ctor) => registered.set(name, ctor),
  AudioWorkletProcessor: class { constructor() { this.port = new Port(); } },
  console,
  /* Emscripten's glue reaches for these; a real worklet has them. */
  TextDecoder,
  TextEncoder,
  atob,
  queueMicrotask,
  setTimeout,
  clearTimeout,
};
createContext(scope);

/* Anything the bundle needs beyond the list above should fail loudly here. */
runInContext(readFileSync(BUNDLE, 'utf8'), scope, { filename: 'enginesim-worklet.js' });

check(registered.has('enginesim'), 'registers an "enginesim" processor');
for (const absent of ['window', 'self', 'document', 'fetch', 'importScripts']) {
  check(!(absent in scope), `runs without ${absent}`);
}

const Processor = registered.get('enginesim');
const processor = new Processor({ processorOptions: { preset: 1 } });  /* V8 */

/* The wasm instantiates asynchronously; the vm context shares our event loop. */
const settle = async () => { for (let i = 0; i < 200; ++i) await new Promise((r) => setTimeout(r, 5)); };
const ready = await (async () => {
  for (let i = 0; i < 600; ++i) {
    if (processor.port.sent.some((m) => m.type === 'ready')) return true;
    const failed = processor.port.sent.find((m) => m.type === 'error');
    if (failed) { console.log(`  worklet reported: ${failed.message}`); return false; }
    await new Promise((r) => setTimeout(r, 10));
  }
  return false;
})();
check(ready, 'instantiates the wasm module inside the worklet scope');
if (!ready) { console.log('\nFAIL'); process.exit(1); }

check(processor.port.take('loaded').length === 1, 'reports the loaded engine');

/* Drive it the way an audio thread would. */
const output = [new Float32Array(QUANTUM), new Float32Array(QUANTUM)];
let peak = 0;
function run(seconds) {
  const blocks = Math.round((seconds * SAMPLE_RATE) / QUANTUM);
  for (let i = 0; i < blocks; ++i) {
    processor.process([], [output], {});
    scope.currentTime += QUANTUM / SAMPLE_RATE;
    for (const v of output[0]) { const a = Math.abs(v); if (a > peak) peak = a; }
  }
}

run(0.1);
check(processor.process([], [output], {}) === true, 'process() keeps the node alive');

processor.port.deliver({ type: 'starter', value: true });
run(2);
processor.port.deliver({ type: 'starter', value: false });
run(2);

const telemetry = processor.port.take('telemetry');
const idle = telemetry.at(-1);
check(telemetry.length > 10, `posts telemetry while running (${telemetry.length} messages)`);
check(idle?.cylinderCount === 8, `loads the V8 (${idle?.cylinderCount} cylinders)`);
check(idle?.rpm > 300, `idles after cranking (${idle?.rpm.toFixed(0)} rpm)`);
check(peak > 0.01, `produces audio (peak ${peak.toFixed(3)})`);
check(output[0].every(Number.isFinite), 'output samples are all finite');
check(output[0].every((v) => Math.abs(v) <= 1), 'output samples stay inside [-1, 1]');
check(output[1].every((v, i) => v === output[0][i]), 'both channels carry the signal');

/* The spec fixes the render quantum at 128, but the processor grows its
 * scratch buffer for anything larger and that path must not corrupt the heap. */
const wide = [new Float32Array(1024), new Float32Array(1024)];
processor.process([], [wide], {});
check(wide[0].every(Number.isFinite), 'survives a render quantum larger than 128');

peak = 0;
processor.port.deliver({ type: 'throttle', value: 1 });
run(2);
const revving = processor.port.take('telemetry').at(-1);
check(revving?.rpm > idle?.rpm, `throttle raises rpm (${revving?.rpm.toFixed(0)})`);

processor.port.deliver({ type: 'preset', value: 0 });
run(0.2);
const swapped = processor.port.take('loaded').at(-1);
check(swapped?.preset === 0, 'swaps preset on request');

await settle();
console.log(failures.length ? `\nFAIL: ${failures.length} check(s)\n  ${failures.join('\n  ')}`
                            : '\nPASS: the worklet bundle runs in a bare AudioWorklet scope');
process.exit(failures.length ? 1 : 0);
