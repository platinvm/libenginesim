/*
 * Drives the built dashboard in real Chromium.
 *
 * This is the only test that exercises the stack as a user meets it: a real
 * AudioWorklet on a real audio thread, wasm fetched over HTTP and handed
 * across as an ArrayBuffer, CodeMirror, and a live rebuild from edited source.
 *
 *   npm run build
 *   npx playwright install chromium
 *   npm run test:browser
 */
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { extname, join, normalize } from 'node:path';
import { chromium } from 'playwright';

const ROOT = fileURLToPath(new URL('../dist/', import.meta.url));
const TYPES: Record<string, string> = {
  '.html': 'text/html', '.css': 'text/css', '.js': 'text/javascript',
  '.wasm': 'application/wasm', '.json': 'application/json',
};

const server = createServer(async (req, res) => {
  try {
    let path = decodeURIComponent(new URL(req.url ?? '/', 'http://x').pathname);
    if (path.endsWith('/')) path += 'index.html';
    const file = join(ROOT, normalize(path).replace(/^(\.\.[/\\])+/, ''));
    const body = await readFile(file);
    res.writeHead(200, { 'content-type': TYPES[extname(file)] ?? 'application/octet-stream' });
    res.end(body);
  } catch {
    res.writeHead(404).end('not found');
  }
});
await new Promise<void>((r) => server.listen(0, '127.0.0.1', () => r()));
const port = (server.address() as { port: number }).port;
const base = `http://127.0.0.1:${port}/`;

const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required'] });
const page = await browser.newPage();

const errors: string[] = [];
page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
page.on('pageerror', (e) => errors.push(`pageerror: ${e.message}`));

/* The wasm must arrive as its own request, not baked into a script. */
const wasmRequests: string[] = [];
page.on('response', (r) => { if (r.url().endsWith('.wasm')) wasmRequests.push(r.url()); });

const failures: string[] = [];
const check = (ok: boolean, what: string): void => {
  console.log(`${ok ? '  ok  ' : ' FAIL '} ${what}`);
  if (!ok) failures.push(what);
};

await page.goto(base, { waitUntil: 'networkidle' });
/* No click: the page starts its own audio and cranks itself. */
await page.waitForFunction(() => !(document.getElementById('starter') as HTMLButtonElement).disabled,
                           null, { timeout: 60000 });
await page.waitForFunction(() => ((globalThis as any).engineSim?.telemetry?.rpm ?? 0) > 300,
                           null, { timeout: 30000 });
check(true, 'the page starts and cranks itself with no interaction');

check(wasmRequests.length === 1, `wasm fetched as its own file (${wasmRequests.length} request)`);

await page.evaluate(() => {
  const sim = (globalThis as any).engineSim;
  const analyser = sim.context.createAnalyser();
  analyser.fftSize = 2048;
  sim.output.connect(analyser);
  const buf = new Float32Array(analyser.fftSize);
  (window as any).__peak = 0;
  setInterval(() => {
    analyser.getFloatTimeDomainData(buf);
    for (const v of buf) {
      const a = Math.abs(v);
      if (a > (window as any).__peak) (window as any).__peak = a;
    }
  }, 30);
});

const crank = async (): Promise<void> => {
  await page.evaluate(() => (globalThis as any).engineSim.setStarter(true));
  await page.waitForTimeout(2500);
  await page.evaluate(() => (globalThis as any).engineSim.setStarter(false));
  await page.waitForTimeout(1800);
};
const sample = () => page.evaluate(() => ({
  rpm: (globalThis as any).engineSim.telemetry?.rpm ?? 0,
  peak: (window as any).__peak as number,
  cylinders: document.getElementById('r-engine')!.textContent,
  displacement: document.getElementById('r-disp')!.textContent,
}));

await crank();
const first = await sample();
console.log(`       preset 0: ${first.rpm.toFixed(0)} rpm  ${first.cylinders}  ` +
            `${first.displacement}  peak ${first.peak.toFixed(3)}`);
check(first.rpm > 300, `first preset idles (${first.rpm.toFixed(0)} rpm)`);
check(first.peak > 0.01, `first preset makes sound (peak ${first.peak.toFixed(3)})`);

/* Switch engines from the buttons at the top. */
const buttons = await page.$$('#presets .seg');
check(buttons.length >= 2, `${buttons.length} presets offered`);
await buttons[1]!.click();
await page.waitForTimeout(1000);
await page.evaluate(() => { (window as any).__peak = 0; });
await crank();
const second = await sample();
console.log(`       preset 1: ${second.rpm.toFixed(0)} rpm  ${second.cylinders}  ` +
            `${second.displacement}  peak ${second.peak.toFixed(3)}`);
check(second.rpm > 300, `second preset idles (${second.rpm.toFixed(0)} rpm)`);
check(second.cylinders !== first.cylinders, 'switching presets changes the engine');

/* Now the point of the whole page: edit the source and hear the difference. */
const beforeEdit = await sample();
await page.evaluate(() => {
  const view = (globalThis as any).editor;
  const text: string = view.state.doc.toString();
  /* Change the redline, which the tachometer and the limiter both key off. */
  const edited = text.replace(/redline: [\d.]+/, 'redline: 4000');
  view.dispatch({ changes: { from: 0, to: view.state.doc.length, insert: edited } });
});
/* The rebuild motors the engine for a second before it catches. */
await page.waitForTimeout(5000);
const afterEdit = await page.evaluate(() => ({
  rpm: (globalThis as any).engineSim.telemetry?.rpm ?? 0,
  redline: (globalThis as any).engineSim.telemetry?.redline ?? 0,
  msg: document.getElementById('msg')!.textContent,
}));
console.log(`       after edit: redline ${afterEdit.redline}, ${afterEdit.rpm.toFixed(0)} rpm, ` +
            `"${afterEdit.msg?.trim()}"`);
check(afterEdit.redline === 4000, `editing the source rebuilt the engine (redline ${afterEdit.redline})`);
check(afterEdit.rpm > 300, `engine still running after the rebuild (${afterEdit.rpm.toFixed(0)} rpm)`);

/* A broken edit must report the problem and keep the old engine alive. */
await page.evaluate(() => {
  const view = (globalThis as any).editor;
  view.dispatch({ changes: { from: 0, to: view.state.doc.length, insert: '({ banks: [] })' } });
});
await page.waitForTimeout(2000);
const broken = await page.evaluate(() => ({
  rpm: (globalThis as any).engineSim.telemetry?.rpm ?? 0,
  msg: document.getElementById('msg')!.textContent,
  bad: document.getElementById('msg')!.classList.contains('bad'),
}));
console.log(`       broken edit: "${broken.msg?.trim()}", still ${broken.rpm.toFixed(0)} rpm`);
check(broken.bad, 'a broken definition is reported as an error');
check(broken.rpm > 300, `a broken definition leaves the engine running (${broken.rpm.toFixed(0)} rpm)`);

await browser.close();
server.close();

if (errors.length) failures.push(`console errors: ${errors.join(' | ')}`);
console.log(failures.length
  ? `\nFAIL: ${failures.length}\n  ${failures.join('\n  ')}`
  : '\nPASS: the dashboard runs, swaps engines and rebuilds from edited source');
process.exit(failures.length ? 1 : 0);
