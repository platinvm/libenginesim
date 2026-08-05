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
import { EngineSim, units } from 'libenginesim';
import type { EngineDef, Preset, Telemetry } from 'libenginesim/types';
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
/* Lowest speed seen while running: the engine's own idle, whatever it is. */
let idleRpm = 700;

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
  ui.dyno.value = "0";
  dynoOn = false;
  ui.dynoOut.textContent = 'off';
  applyThrottle(0);
  rebuild();
}

/* Cranks until it catches, the way you would, then lets go. */
function autoCrank(): void {
  if (!engine) return;
  crank(true);
  const started = performance.now();
  const poll = setInterval(() => {
    const caught = (engine?.telemetry?.rpm ?? 0) > 500;
    if (caught || performance.now() - started > 4000) {
      clearInterval(poll);
      crank(false);
      setStatus(caught
        ? 'Edit the engine on the left. Hold <strong>Space</strong> for wide open.'
        : 'It did not catch. Hold <strong>crank</strong> to try again.');
    }
  }, 100);
}

/*
 * Browsers refuse to start audio until the page has been interacted with. Try
 * anyway - a context is often allowed to start if the visitor has been here
 * before - and fall back to starting on the first gesture, whatever it is,
 * rather than making a button click the price of admission.
 */
async function armAudio(): Promise<void> {
  if (!engine) return;
  await engine.resume().catch(() => {});
  if (engine.context.state === 'running') {
    ui.power.hidden = true;
    autoCrank();
    return;
  }
  ui.power.hidden = false;
  ui.power.disabled = false;
  ui.power.textContent = 'Start audio';
  setStatus('Your browser is holding the sound until you interact. Click anywhere.');
  const onGesture = async (): Promise<void> => {
    window.removeEventListener('pointerdown', onGesture);
    window.removeEventListener('keydown', onGesture);
    await engine?.resume().catch(() => {});
    ui.power.hidden = true;
    autoCrank();
  };
  window.addEventListener('pointerdown', onGesture, { once: true });
  window.addEventListener('keydown', onGesture, { once: true });
}

async function boot() {
  setStatus('Loading engine…');
  try {
    engine = await EngineSim.create({ preset: activePreset });

    engine.onTelemetry((t: Telemetry) => {
      if (t.redline && Math.abs(t.redline - redline) > 1) { redline = t.redline; layoutDial(); }
      /* The engine's own idle, learned rather than assumed. */
      if (!dynoOn && t.throttle_plate_position < 0.2 && t.rpm > 200) {
        idleRpm = idleRpm === 0 ? t.rpm : Math.min(idleRpm, t.rpm) * 0.999 + t.rpm * 0.001;
      }
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
    setMessage(`Running ${first.name}.`, 'good');
    await armAudio();
  } catch (err) {
    ui.power.hidden = false;
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

ui.power.addEventListener('click', () => void armAudio());
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

/*
 * The slider runs from this engine's idle to its rev limit, spaced
 * logarithmically. Even steps in rpm put almost the whole travel up at the top
 * where nothing interesting happens; even steps in ratio give the same
 * proportional change everywhere, so the bottom of the range is usable.
 */
function dynoRpm(position: number): number {
  const top = Math.max(idleRpm * 1.2, redline);
  return idleRpm * (top / idleRpm) ** position;
}

ui.dyno.addEventListener('input', () => {
  const position = Number(ui.dyno.value) / 1000;
  dynoOn = position > 0;
  const rpm = dynoRpm(position);
  ui.dynoOut.textContent = dynoOn ? `${Math.round(rpm).toLocaleString()} rpm` : 'off';
  /* Ramped rather than stepped: the dyno grabs the crank, and jumping it
   * across a thousand rpm in one block is what makes it crack. */
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
  changes: { from: 0, to: editor.state.doc.length, insert: '// Loading…\n' },
});
/*
 * Nothing to listen to in a background tab, and the simulation is expensive
 * enough that leaving it running is rude. The AudioContext keeps its state, so
 * coming back resumes exactly where it left off.
 */
document.addEventListener('visibilitychange', () => {
  if (!engine) return;
  if (document.hidden) void engine.suspend();
  else if (ui.power.hidden) void engine.resume();
});

layoutDial();
drawRpm(0);
animate();
void boot();
