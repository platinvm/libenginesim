/*
 * Types for the Emscripten output next to this file.
 *
 * enginesim.mjs and enginesim.wasm are build products (CMake writes them here
 * and .gitignore skips them), but their shape is stable and worth describing
 * so the rest of the app can be type-checked against it.
 */

/** A pointer into the wasm heap. */
export type Ptr = number;

export interface EngineModule {
  HEAPU8: Uint8Array;
  HEAPU32: Uint32Array;
  HEAPF32: Float32Array;
  HEAPF64: Float64Array;
  UTF8ToString(ptr: Ptr): string;
  stringToUTF8(text: string, ptr: Ptr, maxBytes: number): number;

  /* Layout and scratch memory, from core/src/wasm_exports.cpp. */
  _es_js_schema(): Ptr;
  _es_js_preset_count(): number;
  _es_js_alloc(bytes: number): Ptr;
  _es_js_free(ptr: Ptr): void;

  /* The C ABI itself, from core/include/enginesim.h. */
  _es_abi_version(): number;
  _es_result_str(result: number): Ptr;
  _es_flow_from_carb_cfm(scfm: number): number;
  _es_flow_from_cfm_28(scfm: number): number;
  _es_preset_engine(preset: number, out: Ptr): number;
  _es_preset_name(preset: number): Ptr;
  _es_sim_create(def: Ptr, config: Ptr, out: Ptr): number;
  _es_sim_destroy(sim: Ptr): void;
  _es_sim_step(sim: Ptr, audio: Ptr, frames: number): number;
  _es_sim_set_throttle(sim: Ptr, throttle: number): void;
  _es_sim_set_starter(sim: Ptr, engaged: number): void;
  _es_sim_set_ignition(sim: Ptr, enabled: number): void;
  _es_sim_set_dyno(sim: Ptr, enabled: number, speed: number): void;
  _es_sim_set_crank_speed(sim: Ptr, rpm: number): void;
  _es_sim_set_volume(sim: Ptr, volume: number): void;
  _es_sim_telemetry(sim: Ptr, out: Ptr): void;
}

export interface EngineModuleOptions {
  /** The wasm bytes, when the caller already has them. */
  wasmBinary?: ArrayBuffer | Uint8Array;
  /**
   * Full control over instantiation. Required in an AudioWorklet: that scope
   * has no URL constructor, and Emscripten's default path builds a URL for the
   * .wasm before it checks whether the binary was supplied.
   */
  instantiateWasm?: (
    imports: WebAssembly.Imports,
    receive: (instance: WebAssembly.Instance) => void,
  ) => void;
}

declare const createEngineSim: (options?: EngineModuleOptions) => Promise<EngineModule>;

export default createEngineSim;
