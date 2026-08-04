/*
 * Drives the demo in real Chromium and verifies it makes sound.
 *
 * This is the only test that exercises the whole stack as a user meets it:
 * a real AudioWorklet on a real audio thread, wasm instantiated from the
 * committed bundle, and the page's own controls. tests/worklet.mjs covers the
 * bundle far more cheaply, so this is not wired into CI - it is here for when
 * the demo or the binding changes.
 *
 *   npm install          # playwright, the only dev dependency
 *   npx playwright install chromium
 *   npm run test:browser
 */
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { extname, join, normalize } from 'node:path';

const ROOT = fileURLToPath(new URL('..', import.meta.url));
const TYPES = {
  '.html': 'text/html', '.css': 'text/css', '.js': 'text/javascript',
  '.mjs': 'text/javascript', '.json': 'application/json',
};

/* Static server: ES modules and AudioWorklets will not load over file://. */
const server = createServer(async (req, res) => {
  try {
    let path = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    if (path.endsWith('/')) path += 'index.html';
    const file = join(ROOT, normalize(path).replace(/^(\.\.[/\\])+/, ''));
    const body = await readFile(file);
    res.writeHead(200, { 'content-type': TYPES[extname(file)] ?? 'application/octet-stream' });
    res.end(body);
  } catch {
    res.writeHead(404).end('not found');
  }
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const base = `http://127.0.0.1:${server.address().port}`;

const { chromium } = await import('playwright');
const browser = await chromium.launch({
  args: ['--autoplay-policy=no-user-gesture-required'],
});
const page = await browser.newPage();

const errors = [];
page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
page.on('pageerror', (e) => errors.push(`pageerror: ${e.message}`));

await page.goto(`${base}/demo/`, { waitUntil: 'networkidle' });

await page.click('#power');
await page.waitForFunction(() => !document.getElementById('starter').disabled,
                           null, { timeout: 60000 });
console.log('engine loaded, controls enabled');

/* Tap the graph so we measure what is actually reaching the destination. */
await page.evaluate(() => {
  const analyser = engineSim.context.createAnalyser();
  analyser.fftSize = 2048;
  engineSim.output.connect(analyser);
  const buf = new Float32Array(analyser.fftSize);
  window.__peak = 0;
  setInterval(() => {
    analyser.getFloatTimeDomainData(buf);
    for (const v of buf) { const a = Math.abs(v); if (a > window.__peak) window.__peak = a; }
  }, 30);
});

const sample = () => page.evaluate(() => ({
  rpm: engineSim.telemetry?.rpm ?? 0,
  peak: window.__peak,
  cylinders: document.getElementById('r-engine').textContent,
  displacement: document.getElementById('r-disp').textContent,
  dial: document.getElementById('rpm-text').textContent,
}));

await page.evaluate(() => engineSim.setStarter(true));
await page.waitForTimeout(2500);
await page.evaluate(() => engineSim.setStarter(false));
await page.waitForTimeout(1500);
const idle = await sample();
console.log(`idle: rpm=${idle.rpm.toFixed(0)} dial="${idle.dial}" ` +
            `${idle.displacement} peak=${idle.peak.toFixed(4)}`);

await page.evaluate(() => { window.__peak = 0; engineSim.setThrottle(1); });
await page.waitForTimeout(2500);
const wot = await sample();
console.log(`wide open: rpm=${wot.rpm.toFixed(0)} peak=${wot.peak.toFixed(4)}`);

await page.evaluate(() => engineSim.setThrottle(0));
await page.click('[data-preset="0"]');
await page.waitForTimeout(1200);
await page.evaluate(() => engineSim.setStarter(true));
await page.waitForTimeout(2500);
await page.evaluate(() => engineSim.setStarter(false));
await page.waitForTimeout(1200);
const i4 = await sample();
console.log(`inline-4: rpm=${i4.rpm.toFixed(0)} ${i4.cylinders} ${i4.displacement}`);

await browser.close();
server.close();

const problems = [];
if (idle.rpm < 300) problems.push('engine did not idle');
if (idle.peak < 0.01) problems.push('no audio at idle');
if (wot.rpm <= idle.rpm) problems.push('throttle did not raise rpm');
if (i4.rpm < 300) problems.push('inline-4 did not idle after preset swap');
if (!i4.cylinders.includes('4')) problems.push(`preset swap did not take (${i4.cylinders})`);
if (errors.length) problems.push(`console errors: ${errors.join(' | ')}`);

console.log(problems.length ? `\nFAIL:\n  ${problems.join('\n  ')}`
                            : '\nPASS: demo runs and makes sound in Chromium');
process.exit(problems.length ? 1 : 0);
