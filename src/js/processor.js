/*
 * The engine runs here, inside the audio thread.
 *
 * Audio is the only reliable clock in a browser, and es_sim_step derives its
 * physics from the frame count it is asked for, so hosting the simulation in
 * the worklet makes underruns structurally impossible: the engine advances
 * exactly as far as the samples that were consumed. Telemetry is posted back
 * to the main thread, which needs no shared memory - and therefore no
 * SharedArrayBuffer and no cross-origin isolation.
 */

/* Field indices matching es_js_telemetry_get. */
const T = {
  rpm: 0, torque: 1, power: 2, throttle: 3, throttlePlate: 4,
  manifoldPressure: 5, intakeAfr: 6, exhaustO2: 7, fuelConsumed: 8,
  redline: 9, displacement: 10, cylinderCount: 11, revLimiterActive: 12,
};

class EngineProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    this.ready = false;
    this.failed = null;
    this.sim = 0;
    this.pendingPreset = options?.processorOptions?.preset ?? 0;
    this.telemetryCountdown = 0;

    this.port.onmessage = (e) => this.onMessage(e.data);

    createEngineSim().then((M) => {
      this.M = M;
      this.audioPtr = M._es_js_alloc(128 * 4);
      this.telemetryPtr = M._es_js_alloc(M._es_js_sizeof_telemetry());
      this.load(this.pendingPreset);
      this.ready = true;
      this.port.postMessage({ type: 'ready' });
    }).catch((err) => {
      this.failed = String(err);
      this.port.postMessage({ type: 'error', message: this.failed });
    });
  }

  load(preset) {
    const M = this.M;
    if (this.sim !== 0) {
      M._es_sim_destroy(this.sim);
      this.sim = 0;
    }

    const defPtr = M._es_js_alloc(M._es_js_sizeof_engine_def());
    const handlePtr = M._es_js_alloc(4);
    try {
      const r = M._es_js_preset(preset, defPtr);
      if (r !== 0) throw new Error(`preset ${preset} failed (${r})`);

      /* sampleRate is a worklet global; the engine matches the device so no
       * resampling ever happens. */
      const cfgPtr = M._es_js_alloc(M._es_js_sizeof_sim_config());
      M.HEAPU32[cfgPtr >> 2] = sampleRate;
      const created = M._es_sim_create(defPtr, cfgPtr, handlePtr);
      M._es_js_free(cfgPtr);
      if (created !== 0) throw new Error(`es_sim_create failed (${created})`);

      this.sim = M.HEAPU32[handlePtr >> 2];
      this.preset = preset;
      M._es_sim_set_volume(this.sim, this.volume ?? 0.7);
      M._es_sim_set_throttle(this.sim, 0);
      this.port.postMessage({ type: 'loaded', preset, redline: M._es_js_def_redline(defPtr) });
    } finally {
      M._es_js_free(defPtr);
      M._es_js_free(handlePtr);
    }
  }

  onMessage(msg) {
    const M = this.M;
    if (msg.type === 'preset') {
      this.pendingPreset = msg.value;
      if (this.ready) this.load(msg.value);
      return;
    }
    if (!this.ready || this.sim === 0) return;
    switch (msg.type) {
      case 'throttle': M._es_sim_set_throttle(this.sim, msg.value); break;
      case 'starter':  M._es_sim_set_starter(this.sim, msg.value ? 1 : 0); break;
      case 'ignition': M._es_sim_set_ignition(this.sim, msg.value ? 1 : 0); break;
      case 'volume':   this.volume = msg.value; M._es_sim_set_volume(this.sim, msg.value); break;
      case 'dyno':     M._es_sim_set_dyno(this.sim, msg.enabled ? 1 : 0, msg.speed ?? 0); break;
    }
  }

  process(_inputs, outputs) {
    const out = outputs[0];
    if (!this.ready || this.sim === 0) return true;

    const frames = out[0].length;
    const M = this.M;

    /* The scratch buffer is sized for the standard 128-frame quantum; grow it
     * if a browser ever asks for more. */
    if (frames * 4 > this.audioBytes) {
      if (this.audioPtr) M._es_js_free(this.audioPtr);
      this.audioPtr = M._es_js_alloc(frames * 4);
      this.audioBytes = frames * 4;
    }

    M._es_sim_step(this.sim, this.audioPtr, frames);
    const mono = M.HEAPF32.subarray(this.audioPtr >> 2, (this.audioPtr >> 2) + frames);
    for (const channel of out) channel.set(mono);

    /* ~20 Hz is plenty for a gauge and keeps the message queue quiet. */
    this.telemetryCountdown -= frames;
    if (this.telemetryCountdown <= 0) {
      this.telemetryCountdown = sampleRate / 20;
      M._es_sim_telemetry(this.sim, this.telemetryPtr);
      const g = (f) => M._es_js_telemetry_get(this.telemetryPtr, f);
      this.port.postMessage({
        type: 'telemetry',
        rpm: g(T.rpm),
        torque: g(T.torque),
        power: g(T.power),
        throttlePlate: g(T.throttlePlate),
        manifoldPressure: g(T.manifoldPressure),
        redline: g(T.redline),
        displacement: g(T.displacement),
        cylinderCount: g(T.cylinderCount),
        revLimiterActive: g(T.revLimiterActive) !== 0,
      });
    }
    return true;
  }
}

registerProcessor('enginesim', EngineProcessor);
