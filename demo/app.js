/*
 * libenginesim demo.
 *
 * Vanilla ES modules; the library is the only dependency.
 */
import { EngineSim, Preset, PresetName } from '../bindings/js/enginesim.js';

const $ = (id) => document.getElementById(id);

const ui = {
  power: $('power'),
  starter: $('starter'),
  ignition: $('ignition'),
  throttle: $('throttle'),
  throttleOut: $('throttle-out'),
  volume: $('volume'),
  volumeOut: $('volume-out'),
  dyno: $('dyno'),
  dynoOut: $('dyno-out'),
  presets: $('presets'),
  status: $('status'),
  tach: document.querySelector('.tach'),
  tachValue: $('tach-value'),
  tachRedline: $('tach-redline'),
  ticks: $('tach-ticks'),
  rpmText: $('rpm-text'),
  limiter: $('limiter'),
  rEngine: $('r-engine'),
  rDisp: $('r-disp'),
  rTorque: $('r-torque'),
  rPower: $('r-power'),
  rMap: $('r-map'),
  rThrottle: $('r-throttle'),
};

/** Live arc length of the dial: 75% of a circle of radius 82. */
const ARC = 2 * Math.PI * 82 * 0.75;

let engine = null;
let scale = 8000;          // full-scale RPM, derived from the engine's redline
let redline = 6500;
let displayedRpm = 0;      // eased, so the needle does not jitter at idle
let dynoOn = false;

function setStatus(text, isError = false) {
  ui.status.innerHTML = text;
  ui.status.classList.toggle('error', isError);
}

/* ------------------------------------------------------------------ dial */

function layoutDial() {
  /* Round the scale up to a whole thousand above the redline. */
  scale = Math.ceil((redline * 1.08) / 1000) * 1000;

  const redFrac = Math.min(1, redline / scale);
  ui.tachRedline.style.strokeDasharray = `${ARC * (1 - redFrac)} ${515.2}`;
  ui.tachRedline.style.strokeDashoffset = `${-ARC * redFrac}`;

  /* Ticks every 1000 rpm around the 270 degree sweep. */
  const step = scale > 9000 ? 2000 : 1000;
  let markup = '';
  for (let rpm = 0; rpm <= scale; rpm += step) {
    const a = (135 + (rpm / scale) * 270) * (Math.PI / 180);
    const cos = Math.cos(a), sin = Math.sin(a);
    markup +=
      `<line class="tach-tick" x1="${100 + cos * 73}" y1="${100 + sin * 73}"` +
      ` x2="${100 + cos * 67}" y2="${100 + sin * 67}"/>` +
      `<text class="tach-tick-label" x="${100 + cos * 58}" y="${100 + sin * 58 + 3}">` +
      `${rpm / 1000}</text>`;
  }
  ui.ticks.innerHTML = markup;
}

function drawRpm(rpm) {
  const frac = Math.max(0, Math.min(1, rpm / scale));
  ui.tachValue.style.strokeDasharray = `${ARC * frac} 515.2`;
  ui.tach.classList.toggle('over', rpm > redline);
  ui.rpmText.textContent = Math.round(rpm).toLocaleString();
}

/* Ease the needle at ~60 Hz between the 20 Hz telemetry messages. */
function animate() {
  requestAnimationFrame(animate);
  const target = engine?.telemetry?.rpm ?? 0;
  displayedRpm += (target - displayedRpm) * 0.25;
  if (Math.abs(target - displayedRpm) < 1) displayedRpm = target;
  drawRpm(displayedRpm);
}

/* -------------------------------------------------------------- controls */

function setEnabled(on) {
  for (const el of [ui.starter, ui.ignition, ui.throttle, ui.volume, ui.dyno]) {
    el.disabled = !on;
  }
  for (const b of ui.presets.querySelectorAll('.seg')) b.disabled = !on;
}

function applyThrottle(percent) {
  ui.throttle.value = String(percent);
  ui.throttleOut.textContent = `${Math.round(percent)}%`;
  engine?.setThrottle(percent / 100);
}

function crank(on) {
  engine?.setStarter(on);
  ui.starter.classList.toggle('cranking', on);
}

async function start() {
  ui.power.disabled = true;
  setStatus('Loading engine…');
  try {
    engine = await EngineSim.create({ preset: Preset.V8 });
    await engine.resume();

    engine.onTelemetry((t) => {
      redline = t.redline || redline;
      if (Math.abs(scale - Math.ceil((redline * 1.08) / 1000) * 1000) > 1) layoutDial();

      ui.rEngine.textContent = `${t.cylinderCount} cyl`;
      ui.rDisp.textContent = `${(t.displacement * 1000).toFixed(1)} L`;
      /* Torque and power are measured by the dyno, so they only mean
       * something while it is holding the crank. */
      ui.rTorque.textContent = dynoOn ? `${Math.round(t.torque)} N·m` : '—';
      ui.rPower.textContent = dynoOn ? `${Math.round(t.power / 1000)} kW` : '—';
      ui.rMap.textContent = `${Math.round(t.manifoldPressure / 1000)} kPa`;
      ui.rThrottle.textContent = `${Math.round(t.throttlePlate * 100)}%`;
      ui.limiter.hidden = !t.revLimiterActive;
    });

    /* Handy for poking at the engine from the browser console. */
    globalThis.engineSim = engine;

    setEnabled(true);
    engine.setVolume(Number(ui.volume.value) / 100);
    ui.power.textContent = 'Running';
    ui.power.classList.remove('btn-primary');
    setStatus('Hold <strong>crank</strong> until it catches, then use the throttle.');
  } catch (err) {
    ui.power.disabled = false;
    setStatus(`Could not start: ${err.message}`, true);
    console.error(err);
  }
}

/* ----------------------------------------------------------------- wires */

ui.power.addEventListener('click', start);

/* Cranking is momentary: held with the pointer or the keyboard. */
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

ui.presets.addEventListener('click', (e) => {
  const button = e.target.closest('.seg');
  if (!button || button.disabled) return;
  for (const b of ui.presets.querySelectorAll('.seg')) b.classList.remove('active');
  button.classList.add('active');
  const preset = Number(button.dataset.preset);
  engine?.setPreset(preset);
  applyThrottle(0);
  ui.dyno.value = '0';
  dynoOn = false;
  ui.dynoOut.textContent = 'off';
  setStatus(`Loaded the ${PresetName[preset]}. Hold <strong>crank</strong> to start it.`);
});

/* Space is wide-open throttle, released back to whatever the slider says. */
let heldThrottle = null;
window.addEventListener('keydown', (e) => {
  if (e.code !== 'Space' || e.repeat || !engine) return;
  if (e.target.matches('input, button')) return;
  e.preventDefault();
  heldThrottle = Number(ui.throttle.value);
  applyThrottle(100);
});
window.addEventListener('keyup', (e) => {
  if (e.code !== 'Space' || heldThrottle === null) return;
  applyThrottle(heldThrottle);
  heldThrottle = null;
});

layoutDial();
drawRpm(0);
animate();
