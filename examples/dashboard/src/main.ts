/*
 * libenginesim demo.
 *
 * Edit an engine definition on the left; it is marshalled into native structs
 * and a new simulation is built while the old one keeps playing. Nothing is
 * swapped until the replacement exists, so a half-finished edit never drops
 * the page into silence.
 */
import { EditorView, basicSetup } from 'codemirror';
import { javascript } from '@codemirror/lang-javascript';
import { oneDark } from '@codemirror/theme-one-dark';
import { EngineSim, units } from '../../../bindings/js/src/enginesim.ts';
import type { EngineDef, Preset, Telemetry } from '../../../bindings/js/src/types.ts';
import { toSource, fromSource } from './serialize.ts';
import './style.css';

const $ = (id: string): HTMLElement => document.getElementById(id)!;
const ui = {
  presets: $('presets'), editor: $('editor'), apply: $('apply'), msg: $('msg'),
  live: $('live') as HTMLInputElement,
  power: $('power') as HTMLButtonElement, starter: $('starter') as HTMLButtonElement,
  ignition: $('ignition') as HTMLButtonElement,
  throttle: $('throttle') as HTMLInputElement, throttleOut: $('throttle-out'),
  volume: $('volume') as HTMLInputElement, volumeOut: $('volume-out'),
  dyno: $('dyno') as HTMLInputElement, dynoOut: $('dyno-out'), status: $('status'),
  tach: document.querySelector('.tach')!, tachValue: $('tach-value'), tachRedline: $('tach-redline'),
  ticks: $('tach-ticks'), rpmText: $('rpm-text'), limiter: $('limiter'),
  rEngine: $('r-engine'), rDisp: $('r-disp'), rTorque: $('r-torque'),
  rPower: $('r-power'), rMap: $('r-map'), rThrottle: $('r-throttle'),
};

const ARC = 2 * Math.PI * 82 * 0.75;   // 75% of a circle of radius 82
let engine: EngineSim | null = null;
let scale = 8000;
let redline = 6500;
let displayedRpm = 0;
let dynoOn = false;
let activePreset = 0;

/* ---------------------------------------------------------------- editor */

const editor = new EditorView({
  parent: ui.editor,
  extensions: [
    basicSetup,
    javascript(),
    oneDark,
    EditorView.updateListener.of((v) => { if (v.docChanged) scheduleRebuild(); }),
  ],
});

let rebuildTimer: ReturnType<typeof setTimeout> | undefined;
function scheduleRebuild() {
  if (!engine || !ui.live.checked) return;
  clearTimeout(rebuildTimer);
  /* Long enough not to rebuild on every keystroke mid-word. */
  rebuildTimer = setTimeout(rebuild, 700);
}

function setMessage(text: string, kind = ''): void {
  ui.msg.textContent = text || ' ';
  ui.msg.className = `msg ${kind}`;
}

async function rebuild() {
  if (!engine) return;
  clearTimeout(rebuildTimer);
  let def: EngineDef;
  try {
    def = fromSource(editor.state.doc.toString(), units);
  } catch (err) {
    setMessage(err instanceof Error ? err.message : String(err), 'bad');
    return;
  }
  try {
    setMessage('Rebuilding…');
    await engine.setEngine(def);
    setMessage(`Running ${def.name ?? 'engine'}.`, 'good');
  } catch (err) {
    setMessage(err instanceof Error ? err.message : String(err), 'bad');
  }
}

/* ------------------------------------------------------------------ dial */

function layoutDial(): void {
  scale = Math.max(1000, Math.ceil((redline * 1.08) / 1000) * 1000);
  const redFrac = Math.min(1, redline / scale);
  ui.tachRedline.style.strokeDasharray = `${ARC * (1 - redFrac)} 515.2`;
  ui.tachRedline.style.strokeDashoffset = `${-ARC * redFrac}`;

  const step = scale > 9000 ? 2000 : 1000;
  let markup = '';
  for (let rpm = 0; rpm <= scale; rpm += step) {
    const a = ((135 + (rpm / scale) * 270) * Math.PI) / 180;
    const cos = Math.cos(a), sin = Math.sin(a);
    markup +=
      `<line class="tach-tick" x1="${100 + cos * 73}" y1="${100 + sin * 73}"` +
      ` x2="${100 + cos * 67}" y2="${100 + sin * 67}"/>` +
      `<text class="tach-tick-label" x="${100 + cos * 58}" y="${100 + sin * 58 + 3}">` +
      `${rpm / 1000}</text>`;
  }
  ui.ticks.innerHTML = markup;
}

function drawRpm(rpm: number): void {
  const frac = Math.max(0, Math.min(1, rpm / scale));
  ui.tachValue.style.strokeDasharray = `${ARC * frac} 515.2`;
  ui.tach.classList.toggle('over', rpm > redline);
  ui.rpmText.textContent = Math.round(rpm).toLocaleString();
}

function animate(): void {
  requestAnimationFrame(animate);
  const target = engine?.telemetry?.rpm ?? 0;
  displayedRpm += (target - displayedRpm) * 0.25;
  if (Math.abs(target - displayedRpm) < 1) displayedRpm = target;
  drawRpm(displayedRpm);
}

/* -------------------------------------------------------------- controls */

function setEnabled(on: boolean): void {
  for (const el of [ui.starter, ui.ignition, ui.throttle, ui.volume, ui.dyno]) el.disabled = !on;
}

function applyThrottle(percent: number): void {
  ui.throttle.value = String(percent);
  ui.throttleOut.textContent = `${Math.round(percent)}%`;
  engine?.setThrottle(percent / 100);
}

function crank(on: boolean): void {
  engine?.setStarter(on);
  ui.starter.classList.toggle('cranking', on);
}

function showPresets(presets: readonly Preset[]): void {
  ui.presets.replaceChildren(...presets.map((p) => {
    const b = document.createElement('button');
    b.type = 'button';
    b.className = `seg${p.index === activePreset ? ' active' : ''}`;
    b.textContent = p.name;
    b.addEventListener('click', () => loadPreset(presets, p.index));
    return b;
  }));
}

function loadPreset(presets: readonly Preset[], index: number): void {
  const preset = presets[index];
  if (!preset) return;
  activePreset = index;
  for (const [i, b] of [...ui.presets.children].entries()) b.classList.toggle('active', i === index);
  editor.dispatch({
    changes: { from: 0, to: editor.state.doc.length, insert: toSource(preset.def) },
  });
  ui.dyno.value = '0';
  dynoOn = false;
  ui.dynoOut.textContent = 'off';
  applyThrottle(0);
  rebuild();
}

async function start() {
  ui.power.disabled = true;
  setStatus('Loading engine…');
  try {
    engine = await EngineSim.create({ preset: activePreset });
    await engine.resume();

    engine.onTelemetry((t: Telemetry) => {
      if (t.redline && Math.abs(t.redline - redline) > 1) { redline = t.redline; layoutDial(); }
      ui.rEngine.textContent = `${t.cylinder_count} cyl`;
      ui.rDisp.textContent = `${(t.displacement * 1000).toFixed(2)} L`;
      /* Torque and power are dyno measurements; they mean nothing with the
       * dyno disengaged. */
      ui.rTorque.textContent = dynoOn ? `${Math.round(t.torque)} N·m` : '—';
      ui.rPower.textContent = dynoOn ? `${Math.round(t.power / 1000)} kW` : '—';
      ui.rMap.textContent = `${Math.round(t.manifold_pressure / 1000)} kPa`;
      ui.rThrottle.textContent = `${Math.round(t.throttle_plate_position * 100)}%`;
      ui.limiter.hidden = !t.rev_limiter_active;
    });

    /* Handy from the browser console, and what the browser test drives. */
    Object.assign(globalThis as Record<string, unknown>, { engineSim: engine, editor });   // handy from the console
    showPresets(engine.presets);
    const first = engine.presets[activePreset];
    if (!first) throw new Error('the library reported no built-in engines');
    editor.dispatch({
      changes: { from: 0, to: editor.state.doc.length, insert: toSource(first.def) },
    });

    setEnabled(true);
    engine.setVolume(Number(ui.volume.value) / 100);
    ui.power.textContent = 'Running';
    ui.power.classList.remove('btn-primary');
    setMessage(`Running ${first.name}.`, 'good');
    setStatus('Hold <strong>crank</strong> until it catches, then use the throttle.');
  } catch (err) {
    ui.power.disabled = false;
    setStatus(`Could not start: ${err instanceof Error ? err.message : String(err)}`, true);
    console.error(err);
  }
}

function setStatus(html: string, isError = false): void {
  ui.status.innerHTML = html;
  ui.status.classList.toggle('error', isError);
}

/* ----------------------------------------------------------------- wires */

ui.power.addEventListener('click', start);
ui.apply.addEventListener('click', rebuild);

ui.starter.addEventListener('pointerdown', (e) => {
  if (ui.starter.disabled) return;
  e.preventDefault();
  crank(true);
});
ui.starter.addEventListener('pointerleave', () => crank(false));
window.addEventListener('pointerup', () => crank(false));
window.addEventListener('pointercancel', () => crank(false));
ui.starter.addEventListener('keydown', (e) => {
  if ((e.key === ' ' || e.key === 'Enter') && !e.repeat) { e.preventDefault(); crank(true); }
});
ui.starter.addEventListener('keyup', (e) => {
  if (e.key === ' ' || e.key === 'Enter') crank(false);
});

ui.ignition.addEventListener('click', () => {
  const on = ui.ignition.getAttribute('aria-pressed') !== 'true';
  ui.ignition.setAttribute('aria-pressed', String(on));
  ui.ignition.textContent = on ? 'Ignition on' : 'Ignition off';
  engine?.setIgnition(on);
});

ui.throttle.addEventListener('input', () => applyThrottle(Number(ui.throttle.value)));

ui.volume.addEventListener('input', () => {
  ui.volumeOut.textContent = `${ui.volume.value}%`;
  engine?.setVolume(Number(ui.volume.value) / 100);
});

ui.dyno.addEventListener('input', () => {
  const rpm = Number(ui.dyno.value);
  dynoOn = rpm > 0;
  ui.dynoOut.textContent = dynoOn ? `${rpm.toLocaleString()} rpm` : 'off';
  engine?.setDyno(dynoOn, rpm);
});

/* Space is wide-open throttle, unless you are typing in the editor. */
let heldThrottle: number | null = null;
window.addEventListener('keydown', (e) => {
  if (e.code !== 'Space') return;
  const target = e.target as HTMLElement | null;
  /* Typing wins, and the crank button handles Space itself. */
  if (target?.closest('.editor, #starter') || target?.matches('input[type="text"], textarea')) {
    return;
  }

  /* Before the repeat guard, and on every repeat. Holding a key fires keydown
   * over and over, and an unprevented Space scrolls the page each time, which
   * is what made it creep downwards while revving. */
  e.preventDefault();
  if (e.repeat || !engine) return;

  heldThrottle = Number(ui.throttle.value);
  applyThrottle(100);
});
window.addEventListener('keyup', (e) => {
  if (e.code !== 'Space' || heldThrottle === null) return;
  applyThrottle(heldThrottle);
  heldThrottle = null;
});

editor.dispatch({
  changes: { from: 0, to: editor.state.doc.length, insert: '// Press "Start audio" to load an engine.\n' },
});
layoutDial();
drawRpm(0);
animate();
