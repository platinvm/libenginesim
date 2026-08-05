/*
 * The engine runs here, inside the audio thread.
 *
 * Audio is the only reliable clock in a browser, and es_sim_step derives its
 * physics from the frame count it is asked for, so hosting the simulation in
 * the worklet makes underruns structurally impossible: the engine advances
 * exactly as far as the samples that were consumed. Telemetry is posted to the
 * main thread, so there is no shared memory, no SharedArrayBuffer and no
 * cross-origin isolation.
 *
 * The wasm bytes arrive in processorOptions. A worklet scope has no fetch, so
 * the main thread does the fetching and hands the ArrayBuffer over.
 */
import createEngineSim from '../../wasm/enginesim.mjs';
import type { EngineModule, Ptr } from '../../wasm/enginesim.mjs';
import { parseSchema, readStruct, writeStruct, Arena, type Schema } from '../marshal.ts';
import type { EngineDef, Preset, Telemetry } from '../types.ts';

/* Emscripten's clock shim calls performance.now(). A worklet has no
 * performance object, but it does have currentTime. */
globalThis.performance ??= { now: () => currentTime * 1000 } as Performance;

interface ProcessorOptions {
  wasmBinary?: ArrayBuffer;
  preset?: number;
  engine?: EngineDef;
}

type Incoming =
  | { type: 'engine'; id: number; def: EngineDef }
  | { type: 'preset'; id: number; value: number }
  | { type: 'throttle'; value: number }
  | { type: 'starter'; value: boolean }
  | { type: 'ignition'; value: boolean }
  | { type: 'volume'; value: number }
  | { type: 'dyno'; enabled: boolean; speed?: number };

class EngineProcessor extends AudioWorkletProcessor {
  #M!: EngineModule;
  #schema!: Schema;
  #presets: Preset[] = [];
  #sim: Ptr = 0;
  #audioPtr: Ptr = 0;
  #telemetryPtr: Ptr = 0;
  #audioBytes = 128 * 4;
  #ready = false;
  #volume = 0.7;
  #throttle = 0;
  #starter = false;
  #telemetryCountdown = 0;
  #restartFrames = 0;

  constructor(options?: AudioWorkletNodeOptions) {
    super();
    this.port.onmessage = (e: MessageEvent<Incoming>) => this.#onMessage(e.data);

    const opts = (options?.processorOptions ?? {}) as ProcessorOptions;

    /* instantiateWasm rather than wasmBinary: this scope has no URL
     * constructor, and Emscripten's default path builds one for the .wasm
     * before it notices the bytes were handed to it. */
    createEngineSim({
      instantiateWasm: (imports, receive) => {
        const bytes = opts.wasmBinary;
        if (!bytes) throw new Error('no wasm bytes were passed to the worklet');
        void WebAssembly.instantiate(bytes, imports).then((r) => receive(r.instance));
      },
    })
      .then((M) => {
        this.#M = M;
        this.#schema = parseSchema(M.UTF8ToString(M._es_js_schema()));
        this.#audioPtr = M._es_js_alloc(this.#audioBytes);
        this.#telemetryPtr = M._es_js_alloc(this.#schema['telemetry']!.size);

        /* Hand the presets over as plain objects, so a host can show and edit
         * their real numbers instead of keeping a second copy. */
        const size = this.#schema['engine_def']!.size;
        const defPtr = M._es_js_alloc(size);
        for (let i = 0; i < M._es_js_preset_count(); ++i) {
          M.HEAPU8.fill(0, defPtr, defPtr + size);
          if (M._es_preset_engine(i, defPtr) !== 0) continue;
          this.#presets.push({
            index: i,
            name: M.UTF8ToString(M._es_preset_name(i)),
            def: readStruct(M, this.#schema, 'engine_def', defPtr) as EngineDef,
          });
        }
        M._es_js_free(defPtr);

        this.#load(opts.engine ?? this.#presets[opts.preset ?? 0]?.def);
        this.#ready = true;
        this.port.postMessage({ type: 'ready', presets: this.#presets, sampleRate });
      })
      .catch((err: unknown) => {
        this.port.postMessage({ type: 'error', message: `wasm failed to start: ${String(err)}` });
      });
  }

  /** Replaces the running engine. Throws if the definition is rejected. */
  #load(def: EngineDef | undefined): void {
    const M = this.#M;
    if (!def) throw new Error('no engine definition');

    const arena = new Arena(M);
    let created: Ptr;
    try {
      const defPtr = arena.alloc(this.#schema['engine_def']!.size);
      writeStruct(M, this.#schema, arena, 'engine_def', defPtr, def as Record<string, unknown>);

      /* Match the device rate so nothing ever resamples. */
      const cfgPtr = arena.alloc(this.#schema['sim_config']!.size);
      writeStruct(M, this.#schema, arena, 'sim_config', cfgPtr, { sample_rate: sampleRate });

      const handle = arena.alloc(4);
      const rc = M._es_sim_create(defPtr, cfgPtr, handle);
      if (rc !== 0) throw new Error(`${M.UTF8ToString(M._es_result_str(rc))} (${rc})`);
      created = M.HEAPU32[handle >> 2]!;
    } finally {
      arena.free();
    }

    /* Carry the running speed across, so editing a definition changes how the
     * engine sounds without stopping it. */
    let speed = 0;
    if (this.#sim !== 0) {
      M._es_sim_telemetry(this.#sim, this.#telemetryPtr);
      speed = readStruct(M, this.#schema, 'telemetry', this.#telemetryPtr)['rpm'] as number;
    }

    /* Only swap once the replacement exists, so a bad edit leaves the old
     * engine running rather than dropping the host into silence. */
    if (this.#sim !== 0) M._es_sim_destroy(this.#sim);
    this.#sim = created;
    M._es_sim_set_volume(this.#sim, this.#volume);
    M._es_sim_set_throttle(this.#sim, this.#throttle);
    M._es_sim_set_starter(this.#sim, this.#starter ? 1 : 0);

    /* Last, so restoring the host's starter state above cannot undo it.
     *
     * Seeding the speed alone is not enough: a fresh simulation has no gas
     * charge anywhere and stalls inside half a second. Motor it for a moment,
     * exactly as a starter would, until combustion takes over. process()
     * releases it and puts the host's own starter state back. */
    if (speed > 0) {
      M._es_sim_set_crank_speed(this.#sim, speed);
      M._es_sim_set_starter(this.#sim, 1);
      this.#restartFrames = sampleRate;
    }
  }

  #onMessage(msg: Incoming): void {
    if (msg.type === 'engine' || msg.type === 'preset') {
      if (!this.#ready) return;
      const def = msg.type === 'preset' ? this.#presets[msg.value]?.def : msg.def;
      try {
        this.#load(def);
        this.port.postMessage({ type: 'loaded', id: msg.id, name: def?.name ?? null });
      } catch (err) {
        this.port.postMessage({
          type: 'invalid', id: msg.id, message: err instanceof Error ? err.message : String(err),
        });
      }
      return;
    }

    const M = this.#M;
    if (!this.#ready || this.#sim === 0) return;
    switch (msg.type) {
      case 'throttle': this.#throttle = msg.value; M._es_sim_set_throttle(this.#sim, msg.value); break;
      case 'starter': this.#starter = msg.value; M._es_sim_set_starter(this.#sim, msg.value ? 1 : 0); break;
      case 'ignition': M._es_sim_set_ignition(this.#sim, msg.value ? 1 : 0); break;
      case 'volume': this.#volume = msg.value; M._es_sim_set_volume(this.#sim, msg.value); break;
      case 'dyno': M._es_sim_set_dyno(this.#sim, msg.enabled ? 1 : 0, msg.speed ?? 0); break;
    }
  }

  process(_inputs: Float32Array[][], outputs: Float32Array[][]): boolean {
    const out = outputs[0];
    if (!this.#ready || this.#sim === 0 || !out?.[0]) return true;

    const frames = out[0].length;
    const M = this.#M;

    /* The spec fixes the quantum at 128, but grow rather than overrun if that
     * ever changes. */
    if (frames * 4 > this.#audioBytes) {
      M._es_js_free(this.#audioPtr);
      this.#audioBytes = frames * 4;
      this.#audioPtr = M._es_js_alloc(this.#audioBytes);
    }

    if (this.#restartFrames > 0) {
      this.#restartFrames -= frames;
      if (this.#restartFrames <= 0) M._es_sim_set_starter(this.#sim, this.#starter ? 1 : 0);
    }

    M._es_sim_step(this.#sim, this.#audioPtr, frames);
    const mono = M.HEAPF32.subarray(this.#audioPtr >> 2, (this.#audioPtr >> 2) + frames);
    for (const channel of out) channel.set(mono);

    /* ~20 Hz is plenty for a gauge and keeps the message queue quiet. */
    this.#telemetryCountdown -= frames;
    if (this.#telemetryCountdown <= 0) {
      this.#telemetryCountdown = sampleRate / 20;
      M._es_sim_telemetry(this.#sim, this.#telemetryPtr);
      const t = readStruct(M, this.#schema, 'telemetry', this.#telemetryPtr) as unknown as Telemetry;
      this.port.postMessage({ type: 'telemetry', ...t });
    }
    return true;
  }
}

registerProcessor('enginesim', EngineProcessor);
