/**
 * libenginesim - browser binding.
 *
 * The simulation runs inside an AudioWorklet, because audio is the only clock
 * in a browser that must not stutter and the C ABI derives its physics from
 * the frame count it is asked for. Nothing is shared between threads except
 * messages, so this needs no SharedArrayBuffer and no cross-origin isolation.
 *
 * @module enginesim
 */

/** @enum {number} */
export const Preset = Object.freeze({
  Inline4: 0,
  V8: 1,
});

/** Display names, indexed by {@link Preset}. */
export const PresetName = Object.freeze(['Inline-4', 'V8']);

/**
 * @typedef {object} Telemetry
 * @property {number} rpm
 * @property {number} torque            Newton-metres at the crank.
 * @property {number} power             Watts.
 * @property {number} throttlePlate     Actual plate position, 0..1.
 * @property {number} manifoldPressure  Pascals, absolute.
 * @property {number} redline           RPM.
 * @property {number} displacement      Cubic metres.
 * @property {number} cylinderCount
 * @property {boolean} revLimiterActive
 */

/**
 * @typedef {object} EngineSimOptions
 * @property {Preset} [preset]        Engine to load. Defaults to the V8.
 * @property {string} [workletUrl]    Location of enginesim-worklet.js.
 * @property {AudioContext} [context] Supply one to mix with other audio.
 */

const DEFAULT_WORKLET = new URL('./enginesim-worklet.js', import.meta.url).href;

/**
 * A running engine. Create with {@link EngineSim.create}.
 */
export class EngineSim {
  #node;
  #context;
  #ownsContext;
  #listeners = new Set();
  #telemetry = null;
  #destroyed = false;

  /** @private */
  constructor(context, node, ownsContext) {
    this.#context = context;
    this.#node = node;
    this.#ownsContext = ownsContext;

    node.port.onmessage = (e) => {
      const msg = e.data;
      if (msg.type === 'telemetry') {
        this.#telemetry = msg;
        for (const fn of this.#listeners) fn(msg);
      }
    };
  }

  /**
   * Loads the worklet and starts an engine. The returned instance is running
   * but silent until {@link EngineSim#setStarter} is called.
   *
   * Browsers refuse to start audio without a user gesture, so call this from
   * a click handler, or call {@link EngineSim#resume} from one afterwards.
   *
   * @param {EngineSimOptions} [options]
   * @returns {Promise<EngineSim>}
   */
  static async create(options = {}) {
    const preset = options.preset ?? Preset.V8;
    const ownsContext = !options.context;
    const context = options.context ?? new AudioContext();

    await context.audioWorklet.addModule(options.workletUrl ?? DEFAULT_WORKLET);

    const node = new AudioWorkletNode(context, 'enginesim', {
      numberOfInputs: 0,
      numberOfOutputs: 1,
      outputChannelCount: [2],
      processorOptions: { preset },
    });
    node.connect(context.destination);

    /* The worklet reports 'ready' once the wasm module has instantiated. */
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('worklet did not start')), 15000);
      node.port.onmessage = (e) => {
        if (e.data.type === 'ready') { clearTimeout(timer); resolve(); }
        else if (e.data.type === 'error') { clearTimeout(timer); reject(new Error(e.data.message)); }
      };
    });

    return new EngineSim(context, node, ownsContext);
  }

  /** The AudioContext driving the engine. */
  get context() { return this.#context; }

  /**
   * The engine's output node, already connected to the destination. Connect it
   * to your own nodes to run the sound through effects or metering.
   * @type {AudioWorkletNode}
   */
  get output() { return this.#node; }

  /** Most recent telemetry, or null before the first frame. @type {?Telemetry} */
  get telemetry() { return this.#telemetry; }

  /** Resumes a context suspended by the browser's autoplay policy. */
  async resume() { await this.#context.resume(); }

  /** Suspends audio without destroying the engine. */
  async suspend() { await this.#context.suspend(); }

  /**
   * Subscribes to telemetry, delivered about 20 times a second.
   * @param {(t: Telemetry) => void} fn
   * @returns {() => void} Call to unsubscribe.
   */
  onTelemetry(fn) {
    this.#listeners.add(fn);
    return () => this.#listeners.delete(fn);
  }

  /** @param {number} value 0 closed, 1 wide open. */
  setThrottle(value) { this.#post({ type: 'throttle', value }); }

  /** @param {boolean} engaged Crank the engine over. */
  setStarter(engaged) { this.#post({ type: 'starter', value: !!engaged }); }

  /** @param {boolean} enabled Cut spark when false. */
  setIgnition(enabled) { this.#post({ type: 'ignition', value: !!enabled }); }

  /** @param {number} value Output gain; 1 is unity. */
  setVolume(value) { this.#post({ type: 'volume', value }); }

  /**
   * Holds the crank at a fixed speed so torque can be measured.
   * @param {boolean} enabled
   * @param {number} [rpm]
   */
  setDyno(enabled, rpm = 0) { this.#post({ type: 'dyno', enabled: !!enabled, speed: rpm }); }

  /** Swaps the engine, keeping the audio graph intact. @param {Preset} preset */
  setPreset(preset) { this.#post({ type: 'preset', value: preset }); }

  /** Stops the engine and releases the audio graph. */
  async destroy() {
    if (this.#destroyed) return;
    this.#destroyed = true;
    this.#listeners.clear();
    this.#node.port.onmessage = null;
    this.#node.disconnect();
    if (this.#ownsContext) await this.#context.close();
  }

  #post(message) {
    if (!this.#destroyed) this.#node.port.postMessage(message);
  }
}
