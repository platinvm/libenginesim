/** libenginesim - browser binding. */

export declare const Preset: {
  readonly Inline4: 0;
  readonly V8: 1;
};
export type Preset = (typeof Preset)[keyof typeof Preset];

export declare const PresetName: readonly string[];

export interface Telemetry {
  rpm: number;
  /** Newton-metres at the crank. */
  torque: number;
  /** Watts. */
  power: number;
  /** Actual throttle plate position, 0..1. */
  throttlePlate: number;
  /** Pascals, absolute. */
  manifoldPressure: number;
  redline: number;
  /** Cubic metres. */
  displacement: number;
  cylinderCount: number;
  revLimiterActive: boolean;
}

export interface EngineSimOptions {
  /** Engine to load. Defaults to the V8. */
  preset?: Preset;
  /** Location of enginesim-worklet.js. */
  workletUrl?: string;
  /** Supply one to mix with other audio. */
  context?: AudioContext;
}

export declare class EngineSim {
  private constructor();

  /**
   * Loads the worklet and starts an engine, silent until the starter is
   * engaged. Call from a user gesture, or call resume() from one afterwards.
   */
  static create(options?: EngineSimOptions): Promise<EngineSim>;

  readonly context: AudioContext;
  /** Output node, already connected to the destination. Connect it to your
   *  own nodes to run the sound through effects or metering. */
  readonly output: AudioWorkletNode;
  readonly telemetry: Telemetry | null;

  resume(): Promise<void>;
  suspend(): Promise<void>;

  /** Subscribes to telemetry (~20 Hz). Returns an unsubscribe function. */
  onTelemetry(fn: (t: Telemetry) => void): () => void;

  /** 0 closed, 1 wide open. */
  setThrottle(value: number): void;
  setStarter(engaged: boolean): void;
  /** Cuts spark when false. */
  setIgnition(enabled: boolean): void;
  /** Output gain; 1 is unity. */
  setVolume(value: number): void;
  /** Holds the crank at a fixed speed so torque can be measured. */
  setDyno(enabled: boolean, rpm?: number): void;
  /** Swaps the engine, keeping the audio graph intact. */
  setPreset(preset: Preset): void;

  destroy(): Promise<void>;
}
