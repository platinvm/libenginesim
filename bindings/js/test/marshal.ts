/*
 * Round-trips every built-in preset through JavaScript.
 *
 * Reads a preset out of the wasm module into a plain object, writes that
 * object back into a fresh struct, and runs a simulation from the result. If
 * the marshaller drops or misplaces a field, the rebuilt engine will not match
 * the original's behaviour and this catches it.
 *
 * Then builds an engine that was never a preset, which is the part that proves
 * a binding can create arbitrary engines rather than pick from a list.
 *
 * No browser and no audio device: node test/marshal.ts
 */
import { readFileSync } from 'node:fs';
import { Arena, parseSchema, readStruct, writeStruct, type Schema } from '../src/marshal.ts';
import type { EngineModule, Ptr } from '../wasm/enginesim.mjs';
import type { EngineDef, Telemetry } from '../src/types.ts';
import { units as u } from '../src/units.ts';

const wasmDir = new URL('../wasm/', import.meta.url);
const { default: createEngineSim } = await import(new URL('enginesim.mjs', wasmDir).href) as {
  default: (o: { wasmBinary: Uint8Array }) => Promise<EngineModule>;
};
const M = await createEngineSim({ wasmBinary: readFileSync(new URL('enginesim.wasm', wasmDir)) });
const schema: Schema = parseSchema(M.UTF8ToString(M._es_js_schema()));

const failures: string[] = [];
function check(ok: boolean, what: string): void {
  console.log(`${ok ? '  ok  ' : ' FAIL '} ${what}`);
  if (!ok) failures.push(what);
}

function sizeOf(name: string): number {
  const s = schema[name];
  if (!s) throw new Error(`schema has no ${name}`);
  return s.size;
}

interface Result { rpm: number; cylinders: number; displacement: number; peak: number }

/** Cranks a definition, lets it settle, and reports what it did. */
function drive(defPtr: Ptr): Result | { error: string } {
  const handle = M._es_js_alloc(4);
  const rc = M._es_sim_create(defPtr, 0, handle);
  if (rc !== 0) {
    M._es_js_free(handle);
    return { error: `${M.UTF8ToString(M._es_result_str(rc))} (${rc})` };
  }
  const sim = M.HEAPU32[handle >> 2]!;
  M._es_js_free(handle);

  const frames = 512;
  const audio = M._es_js_alloc(frames * 4);
  const tele = M._es_js_alloc(sizeOf('telemetry'));
  let peak = 0;

  const run = (seconds: number): void => {
    for (let i = 0; i < (seconds * 44100) / frames; ++i) {
      M._es_sim_step(sim, audio, frames);
      const block = M.HEAPF32.subarray(audio >> 2, (audio >> 2) + frames);
      for (const v of block) { const a = Math.abs(v); if (a > peak) peak = a; }
    }
  };

  M._es_sim_set_starter(sim, 1); run(2);
  M._es_sim_set_starter(sim, 0); run(3);
  peak = 0; run(2);
  M._es_sim_telemetry(sim, tele);
  const t = readStruct(M, schema, 'telemetry', tele) as unknown as Telemetry;

  M._es_js_free(audio);
  M._es_js_free(tele);
  M._es_sim_destroy(sim);
  return { rpm: t.rpm, cylinders: t.cylinder_count, displacement: t.displacement, peak };
}

const presetCount = M._es_js_preset_count();
check(presetCount >= 2, `module reports ${presetCount} presets`);

for (let i = 0; i < presetCount; ++i) {
  console.log(`\n--- ${M.UTF8ToString(M._es_preset_name(i))} ---`);

  const nativePtr = M._es_js_alloc(sizeOf('engine_def'));
  M._es_preset_engine(i, nativePtr);
  const native = drive(nativePtr);

  const def = readStruct(M, schema, 'engine_def', nativePtr) as unknown as EngineDef;
  M._es_js_free(nativePtr);

  const cylinders = (def.banks ?? []).reduce((n, b) => n + (b.cylinders?.length ?? 0), 0);
  check(typeof def.name === 'string' && def.name.length > 0, `name reads back ("${def.name}")`);
  check((def.banks?.length ?? 0) > 0, `${def.banks?.length} bank(s) read back`);
  check(cylinders > 0, `${cylinders} cylinders read back`);
  check((def.banks?.[0]?.intake_flow?.x?.length ?? 0) > 0,
        `flow curve reads back (${def.banks?.[0]?.intake_flow?.x?.length} points)`);
  check(def.firing_order?.length === cylinders,
        `firing order reads back (${def.firing_order?.join(',')})`);
  check((def.exhaust_systems?.length ?? 0) > 0,
        `${def.exhaust_systems?.length} exhaust systems read back`);
  check((def.timing_curve?.x?.length ?? 0) > 0,
        `timing curve reads back (${def.timing_curve?.x?.length} points)`);

  const arena = new Arena(M);
  const rebuiltPtr = arena.alloc(sizeOf('engine_def'));
  writeStruct(M, schema, arena, 'engine_def', rebuiltPtr, def as Record<string, unknown>);
  const rebuilt = drive(rebuiltPtr);
  arena.free();

  if ('error' in native) { check(false, `native preset runs (${native.error})`); continue; }
  if ('error' in rebuilt) { check(false, `rebuilt definition runs (${rebuilt.error})`); continue; }

  console.log(`       native  ${native.rpm.toFixed(0)} rpm  ${native.cylinders} cyl  ` +
              `${(native.displacement * 1000).toFixed(2)} L  peak ${native.peak.toFixed(3)}`);
  console.log(`       rebuilt ${rebuilt.rpm.toFixed(0)} rpm  ${rebuilt.cylinders} cyl  ` +
              `${(rebuilt.displacement * 1000).toFixed(2)} L  peak ${rebuilt.peak.toFixed(3)}`);

  check(rebuilt.cylinders === native.cylinders, 'cylinder count survives the round trip');
  check(Math.abs(rebuilt.displacement - native.displacement) < 1e-9,
        'displacement survives the round trip');
  /* The synthesiser draws on a shared rand(), so runs differ slightly. */
  check(Math.abs(rebuilt.rpm - native.rpm) / native.rpm < 0.15,
        `idle speed matches within 15% (${native.rpm.toFixed(0)} vs ${rebuilt.rpm.toFixed(0)})`);
  check(rebuilt.peak > 0.01, `rebuilt engine makes sound (peak ${rebuilt.peak.toFixed(3)})`);
}

/* An engine written from scratch, never a preset. */
console.log('\n--- hand-written single cylinder ---');
{
  const x = [0, u.thou(100), u.thou(200), u.thou(300), u.thou(400)];
  const engine: EngineDef = {
    name: 'Test Single',
    simulation_frequency: 10000,
    bore: u.inch(3.5), stroke: u.inch(3.5),
    crank_mass: u.lb(22), crank_moment_of_inertia: 0.05,
    crank_friction_torque: u.lb_ft(1.5),
    flywheel_mass: u.lb(22),
    rod_mass: u.g(300), rod_length: u.inch(6), rod_moment_of_inertia: 0.0015,
    piston_mass: u.g(300), piston_compression_height: u.inch(1),
    rod_journal_angles: [0],
    banks: [{
      angle: 0,
      cylinders: [{
        rod_journal: 0, exhaust_system: 0, sound_attenuation: 1,
        primary_length: u.inch(10), blowby: 0,
      }],
      chamber_volume: u.cc(50),
      intake_runner_volume: u.cc(100), intake_runner_cross_section_area: u.cm2(12),
      exhaust_runner_volume: u.cc(50), exhaust_runner_cross_section_area: u.cm2(9),
      intake_flow: { x, y: [0, 0.05, 0.09, 0.12, 0.13], filter_radius: u.thou(100) },
      exhaust_flow: { x, y: [0, 0.04, 0.07, 0.09, 0.1], filter_radius: u.thou(100) },
      intake_lobe: { duration_at_50_thou: u.deg(220), gamma: 1.1, lift: u.thou(400), steps: 256 },
      exhaust_lobe: { duration_at_50_thou: u.deg(220), gamma: 1.1, lift: u.thou(400), steps: 256 },
      intake_lobe_center: u.deg(110), exhaust_lobe_center: u.deg(110),
      cam_base_radius: u.inch(0.75),
    }],
    exhaust_systems: [{
      outlet_flow_rate: 0.3, primary_tube_length: u.inch(30), primary_flow_rate: 0.2,
      velocity_decay: 1, volume: u.L(10), audio_volume: 4, length: u.inch(100),
    }],
    intake_plenum_volume: u.L(1.5), intake_plenum_cross_section_area: u.cm2(20),
    intake_runner_length: u.inch(8), intake_runner_flow_rate: 0.05,
    intake_flow_rate: 0.3, intake_idle_flow_rate: 0,
    intake_idle_throttle_plate_position: 0.995, intake_velocity_decay: 0.5,
    throttle_gamma: 2,
    firing_order: [0],
    timing_curve: {
      x: [0, 1000, 3000, 6000],
      y: [u.deg(12), u.deg(20), u.deg(30), u.deg(35)],
      filter_radius: 1000,
    },
    rev_limit: 6000, rev_limit_duration: 0.1, redline: 5500,
    starter_torque: u.lb_ft(110), starter_speed: 200,
    hf_gain: 0.01, jitter: 0.6, noise: 1,
  };

  const arena = new Arena(M);
  const ptr = arena.alloc(sizeOf('engine_def'));
  writeStruct(M, schema, arena, 'engine_def', ptr, engine as Record<string, unknown>);
  const result = drive(ptr);
  arena.free();

  if ('error' in result) {
    check(false, `hand-written engine runs (${result.error})`);
  } else {
    console.log(`       ${result.rpm.toFixed(0)} rpm  ${result.cylinders} cyl  ` +
                `${(result.displacement * 1000).toFixed(2)} L  peak ${result.peak.toFixed(3)}`);
    check(result.cylinders === 1, 'hand-written engine has one cylinder');
    check(result.rpm > 200, `hand-written engine idles (${result.rpm.toFixed(0)} rpm)`);
    check(result.peak > 0.01, `hand-written engine makes sound (peak ${result.peak.toFixed(3)})`);
  }
}

/* Something the ABI must refuse rather than crash on. */
{
  const arena = new Arena(M);
  const ptr = arena.alloc(sizeOf('engine_def'));
  writeStruct(M, schema, arena, 'engine_def', ptr, { name: 'Empty', banks: [] });
  const result = drive(ptr);
  arena.free();
  check('error' in result, 'an empty definition is rejected rather than crashed on');
}

console.log(failures.length
  ? `\nFAIL: ${failures.length} check(s)\n  ${failures.join('\n  ')}`
  : '\nPASS: presets round-trip through JavaScript and hand-written engines run');
process.exit(failures.length ? 1 : 0);
