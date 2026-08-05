/**
 * libenginesim - browser binding.
 *
 * The simulation runs inside an AudioWorklet. Nothing is shared between
 * threads except messages, so this needs no SharedArrayBuffer and no
 * cross-origin isolation.
 *
 * A worklet scope has no fetch, so the wasm is fetched here on the main thread
 * and the bytes are handed over when the node is constructed. Nothing is
 * embedded in a script and nothing is loaded behind your back.
 *
 * @module enginesim
 */
import workletUrl from './worklet/processor.ts?worker&url';
import wasmUrl from '../wasm/enginesim.wasm?url';
import type { EngineDef, Preset, Telemetry } from './types.ts';

export type { EngineDef, Preset, Telemetry } from './types.ts';
export type {
  BankDef, CamLobe, Curve, CylinderDef, ExhaustDef, SimConfig,
} from './types.ts';
export { units } from './units.ts';

export interface EngineSimOptions {
  /** Built-in engine to start with. Defaults to the first. */
  preset?: number;
  /** Start from a definition of your own instead of a preset. */
  engine?: EngineDef;
  /** Supply one to mix with other audio. */
  context?: AudioContext;
}

interface ReadyMessage { type: 'ready'; presets: Preset[]; sampleRate: number }
interface TelemetryMessage extends Telemetry { type: 'telemetry' }
interface LoadedMessage { type: 'loaded'; id: number; name: string | null }
interface InvalidMessage { type: 'invalid'; id: number; message: string }
interface ErrorMessage { type: 'error'; message: string }
type FromWorklet = ReadyMessage | TelemetryMessage | LoadedMessage | InvalidMessage | ErrorMessage;

let pending = 0;

/** A running engine. Create with {@link EngineSim.create}. */
export class EngineSim {
  readonly #node: AudioWorkletNode;
  readonly #context: AudioContext;
  readonly #ownsContext: boolean;
  readonly #presets: readonly Preset[];
  readonly #listeners = new Set<(t: Telemetry) => void>();
  readonly #waiters = new Map<number, { resolve: () => void; reject: (e: Error) => void }>();
  #telemetry: Telemetry | null = null;
  #destroyed = false;

  private constructor(
    context: AudioContext,
    node: AudioWorkletNode,
    ownsContext: boolean,
    presets: readonly Preset[],
  ) {
    this.#context = context;
    this.#node = node;
    this.#ownsContext = ownsContext;
    this.#presets = presets;

    node.port.onmessage = ({ data }: MessageEvent<FromWorklet>) => {
      if (data.type === 'telemetry') {
        this.#telemetry = data;
        for (const fn of this.#listeners) fn(data);
        return;
      }
      if (data.type !== 'loaded' && data.type !== 'invalid') return;
      const waiter = this.#waiters.get(data.id);
      if (!waiter) return;
      this.#waiters.delete(data.id);
      if (data.type === 'invalid') waiter.reject(new Error(data.message));
      else waiter.resolve();
    };
  }

  /**
   * Loads the worklet and starts an engine, silent until the starter is
   * engaged. Browsers refuse to start audio without a user gesture, so call
   * this from a click handler or call {@link EngineSim#resume} from one.
   */
  static async create(options: EngineSimOptions = {}): Promise<EngineSim> {
    const ownsContext = !options.context;
    const context = options.context ?? new AudioContext();

    const [, wasmBinary] = await Promise.all([
      context.audioWorklet.addModule(workletUrl),
      fetch(wasmUrl).then((r) => {
        if (!r.ok) throw new Error(`could not fetch ${wasmUrl}: ${r.status}`);
        return r.arrayBuffer();
      }),
    ]);

    const node = new AudioWorkletNode(context, 'enginesim', {
      numberOfInputs: 0,
      numberOfOutputs: 1,
      outputChannelCount: [2],
      processorOptions: {
        wasmBinary,
        preset: options.preset ?? 0,
        engine: options.engine,
      },
    });
    node.connect(context.destination);

    const ready = await new Promise<ReadyMessage>((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('worklet did not start')), 20000);
      node.port.onmessage = ({ data }: MessageEvent<FromWorklet>) => {
        if (data.type === 'ready') { clearTimeout(timer); resolve(data); }
        else if (data.type === 'error') { clearTimeout(timer); reject(new Error(data.message)); }
      };
    });

    return new EngineSim(context, node, ownsContext, ready.presets);
  }

  /** The AudioContext driving the engine. */
  get context(): AudioContext { return this.#context; }

  /**
   * The engine's output node, already connected to the destination. Connect it
   * to your own nodes for effects or metering.
   */
  get output(): AudioWorkletNode { return this.#node; }

  /** Most recent telemetry, or null before the first frame. */
  get telemetry(): Telemetry | null { return this.#telemetry; }

  /**
   * The built-in engines, as plain objects you can edit and pass back to
   * {@link EngineSim#setEngine}. Read out of the library itself, so these are
   * the real definitions rather than a copy kept in JavaScript.
   */
  get presets(): readonly Preset[] { return this.#presets; }

  /** Resumes a context suspended by the browser's autoplay policy. */
  async resume(): Promise<void> { await this.#context.resume(); }

  /** Suspends audio without destroying the engine. */
  async suspend(): Promise<void> { await this.#context.suspend(); }

  /**
   * Subscribes to telemetry, delivered about 20 times a second.
   * @returns Call to unsubscribe.
   */
  onTelemetry(fn: (t: Telemetry) => void): () => void {
    this.#listeners.add(fn);
    return () => this.#listeners.delete(fn);
  }

  /**
   * Replaces the running engine, keeping the audio graph intact.
   *
   * Rejects if the definition is invalid and leaves the previous engine
   * running when it does, so an unfinished edit never drops you into silence.
   */
  setEngine(def: EngineDef): Promise<void> { return this.#request({ type: 'engine', def }); }

  /** Loads one of the built-in engines by index. */
  setPreset(index: number): Promise<void> { return this.#request({ type: 'preset', value: index }); }

  /** @param value 0 closed, 1 wide open. */
  setThrottle(value: number): void { this.#post({ type: 'throttle', value }); }

  /** @param engaged Crank the engine over. */
  setStarter(engaged: boolean): void { this.#post({ type: 'starter', value: !!engaged }); }

  /** @param enabled Cut spark when false. */
  setIgnition(enabled: boolean): void { this.#post({ type: 'ignition', value: !!enabled }); }

  /** @param value Output gain; 1 is unity. */
  setVolume(value: number): void { this.#post({ type: 'volume', value }); }

  /** Holds the crank at a fixed speed so torque can be measured. */
  setDyno(enabled: boolean, rpm = 0): void {
    this.#post({ type: 'dyno', enabled: !!enabled, speed: rpm });
  }

  /** Stops the engine and releases the audio graph. */
  async destroy(): Promise<void> {
    if (this.#destroyed) return;
    this.#destroyed = true;
    this.#listeners.clear();
    this.#node.port.onmessage = null;
    this.#node.disconnect();
    if (this.#ownsContext) await this.#context.close();
  }

  #request(message: Record<string, unknown>): Promise<void> {
    if (this.#destroyed) return Promise.reject(new Error('engine destroyed'));
    const id = ++pending;
    return new Promise<void>((resolve, reject) => {
      this.#waiters.set(id, { resolve, reject });
      this.#node.port.postMessage({ ...message, id });
    });
  }

  #post(message: Record<string, unknown>): void {
    if (!this.#destroyed) this.#node.port.postMessage(message);
  }
}
