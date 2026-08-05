import type { EngineDef } from 'libenginesim/types';
import type { units } from 'libenginesim/units';

/**
 * Turns an engine definition into editable JavaScript source, and back.
 *
 * The definitions shown in the editor are read out of the library itself, so
 * what you see is the real thing rather than a copy kept in JavaScript. The
 * cost is that the numbers arrive in SI, which is what the ABI speaks: bore
 * comes back as 0.096012, not 3.78 inches. Unit helpers are in scope as `u`
 * for anything you type yourself.
 */

/** Fields worth annotating with a friendlier unit, purely as a comment. */
const HINTS: Record<string, (v: number) => string> = {
  bore: (v) => `${(v / 0.0254).toFixed(3)}"`,
  stroke: (v) => `${(v / 0.0254).toFixed(3)}"`,
  rod_length: (v) => `${(v * 1000).toFixed(1)} mm`,
  piston_compression_height: (v) => `${(v / 0.0254).toFixed(3)}"`,
  crank_mass: (v) => `${(v / 0.45359237).toFixed(1)} lb`,
  flywheel_mass: (v) => `${(v / 0.45359237).toFixed(1)} lb`,
  crank_friction_torque: (v) => `${(v / 1.3558179483314004).toFixed(2)} lb-ft`,
  starter_torque: (v) => `${(v / 1.3558179483314004).toFixed(0)} lb-ft`,
  chamber_volume: (v) => `${(v * 1e6).toFixed(1)} cc`,
  intake_plenum_volume: (v) => `${(v * 1000).toFixed(2)} L`,
  volume: (v) => `${(v * 1000).toFixed(1)} L`,
  primary_tube_length: (v) => `${(v / 0.0254).toFixed(0)}"`,
  length: (v) => `${(v / 0.0254).toFixed(0)}"`,
  primary_length: (v) => `${(v / 0.0254).toFixed(0)}"`,
  intake_runner_length: (v) => `${(v / 0.0254).toFixed(0)}"`,
  angle: (v) => `${((v * 180) / Math.PI).toFixed(1)}°`,
  intake_lobe_center: (v) => `${((v * 180) / Math.PI).toFixed(1)}°`,
  exhaust_lobe_center: (v) => `${((v * 180) / Math.PI).toFixed(1)}°`,
  duration_at_50_thou: (v) => `${((v * 180) / Math.PI).toFixed(0)}°`,
  lift: (v) => `${(v / 0.0254 * 1000).toFixed(0)} thou`,
  cam_base_radius: (v) => `${(v / 0.0254).toFixed(3)}"`,
};

function num(v: number): string {
  if (!Number.isFinite(v)) return '0';
  if (Number.isInteger(v)) return String(v);
  /* Enough precision to round-trip without a screenful of digits. */
  const s = Number(v.toPrecision(10));
  return String(s);
}

function isNumberArray(v: unknown): v is number[] {
  return Array.isArray(v) && v.every((x) => typeof x === 'number');
}

function write(value: unknown, indent: number): string {
  const pad = '  '.repeat(indent);
  const padIn = '  '.repeat(indent + 1);

  if (value === null || value === undefined) return 'null';
  if (typeof value === 'string') return JSON.stringify(value);
  if (typeof value === 'number') return num(value);

  if (isNumberArray(value)) {
    const flat = `[${value.map(num).join(', ')}]`;
    if (flat.length + padIn.length <= 92) return flat;
    /* Wrap long numeric arrays at a readable width. */
    const lines: string[] = [];
    let line = '';
    for (const v of value) {
      const piece = `${num(v)}, `;
      if (line.length + piece.length > 84) { lines.push(line.trimEnd()); line = ''; }
      line += piece;
    }
    if (line) lines.push(line.trimEnd().replace(/,$/, ''));
    return `[\n${lines.map((l) => padIn + l).join('\n')}\n${pad}]`;
  }

  if (Array.isArray(value)) {
    if (value.length === 0) return '[]';
    const items = value.map((v) => padIn + write(v, indent + 1));
    return `[\n${items.join(',\n')}\n${pad}]`;
  }

  const entries = Object.entries(value as Record<string, unknown>).filter(([, v]) => v !== undefined);
  if (entries.length === 0) return '{}';
  const body = entries.map(([k, v]) => {
    const rendered = write(v, indent + 1);
    const hint = typeof v === 'number' && HINTS[k] && v !== 0 ? `  // ${HINTS[k](v)}` : '';
    return `${padIn}${k}: ${rendered},${hint}`;
  });
  return `{\n${body.join('\n')}\n${pad}}`;
}

const HEADER = `// This is plain JavaScript. Edit anything; the engine is rebuilt as you type,
// and the old one keeps running until the new one starts, so a half-typed
// number never cuts the sound.
//
// Values are SI - metres, kilograms, radians, seconds - which is what the C
// ABI speaks. \`u\` converts from units you actually think in:
//
//   lengths   u.inch(3.78)  u.mm(65)  u.thou(400)  u.cm(2)
//   angles    u.deg(45)
//   volumes   u.cc(50)  u.L(4.5)      areas  u.cm2(10)  u.inch2(2)
//   masses    u.lb(30)  u.g(303.5)    torque u.lb_ft(20)
//   inertia   u.diskMoment(mass, radius)   u.rodMoment(mass, length)
//
// Things worth trying:
//   banks[].cylinders          add or remove one, then fix firing_order
//   firing_order               every cylinder index exactly once
//   exhaust_systems[].length   different lengths per bank is what burbles
//   redline / rev_limit        where the limiter cuts in
//   noise / jitter / hf_gain   the synthesiser's character, not the physics
//
// Up to eight cylinders. Flow figures are already converted; if you want to type
// a flow-bench number, the conversion lives in the C ABI, not here.
`;

/** Renders a definition as source for the editor. */
export function toSource(def: EngineDef): string {
  return `${HEADER}({\n${write(def, 0).slice(2, -2)}\n})\n`;
}

/**
 * Evaluates editor source into a definition object.
 *
 * The code is the user's own, running on their own page, so this is a plain
 * Function call rather than anything pretending to be a sandbox.
 *
 * @throws {Error} with a readable message if the source is bad.
 */
export function fromSource(source: string, u: typeof units): EngineDef {
  let fn: (u: typeof units) => unknown;
  try {
    fn = new Function('u', `"use strict"; return (${source}\n);`) as typeof fn;
  } catch {
    /* Not a bare expression - allow statements ending in a return. */
    fn = new Function('u', `"use strict"; ${source}`) as typeof fn;
  }
  const def = fn(u) as EngineDef;
  if (!def || typeof def !== 'object') {
    throw new Error('the code did not produce an engine definition object');
  }
  if (!Array.isArray(def.banks) || def.banks.length === 0) {
    throw new Error('an engine needs at least one bank in `banks`');
  }
  return def;
}
