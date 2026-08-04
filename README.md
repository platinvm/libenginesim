# libenginesim

[AngeTheGreat's engine-sim](https://github.com/ange-yaghi/engine-sim) as a
headless C library: a piston engine you can step, read and pull audio from,
with no windows, no threads, no files and no audio device anywhere in it.

The physics and the sound synthesis are upstream's, unmodified. What this
repository adds is a stable C ABI in front of them, a WebAssembly build, a
typed JavaScript binding and a demo page.

**The C ABI is the product.** [`include/enginesim.h`](include/enginesim.h) is
the whole of it — 15 functions and a handful of plain structs. The JavaScript
binding is its first consumer, not its shape.

## Try the demo

The WebAssembly build is committed, so this needs no toolchain at all. It does
need a server: ES modules and AudioWorklets will not load over `file://`.

```sh
git clone <this repository> && cd libenginesim
python3 -m http.server 8000
```

Open <http://localhost:8000/demo/>, press **Start audio**, then hold
**crank** until it catches. Space is wide-open throttle.

Every sound on that page is combustion, gas flow and convolution computed a
sample at a time in an AudioWorklet. There are no recordings and no
dependencies beyond this library.

## Using the C ABI

```c
#include "enginesim.h"

es_engine_def def;
es_preset_engine(ES_PRESET_V8, &def);   /* or build one field by field */

es_sim *sim;
if (es_sim_create(&def, NULL, &sim) != ES_OK) return 1;

es_sim_set_starter(sim, 1);

float audio[512];
for (;;) {
    /* Advances 512/44100 s of physics and writes 512 mono samples in [-1,1].
     * Pass NULL for `audio` to run the physics alone. */
    es_sim_step(sim, audio, 512);
    play(audio, 512);

    es_telemetry t;
    es_sim_telemetry(sim, &t);
    if (t.rpm > 400) es_sim_set_starter(sim, 0);
}

es_sim_destroy(sim);
```

Four things the header commits to, which are worth knowing before you build a
binding on it:

- **Audio frames are the unit of time.** `es_sim_step` is the only clock.
  Physics and synthesis advance together and cannot drift apart, and the host
  decides when — an audio callback, a game loop, an offline render.
- **The host owns everything it passes in.** Definition structs are copied
  whole during `es_sim_create`; nothing is retained past that call, so you can
  build a definition on the stack and let it go.
- **Engines are typed structs.** Upstream's Piranha scripting layer is not
  compiled and not exposed. If you want a file format, write it above this
  library, not inside it.
- **A handle is not thread-safe, and nothing is shared between handles.** Two
  simulations can be driven from two threads. One cannot.

Engine definitions are given in SI units, except where upstream's model is
itself defined in others — RPM, and the CFM ratings that
`es_flow_from_carb_cfm` (carburettors, throttle bodies, tubing) and
`es_flow_from_cfm_28` (flow-bench figures, for ports and blowby) convert into
the dimensionless flow constants the gas model actually wants. Passing raw CFM
where a flow constant belongs is off by five orders of magnitude and will
detonate the engine on the first cycle; it is the single easiest mistake to
make against this API.

## Building

Upstream is vendored as a submodule and never modified. Clone only the two
things the physics and audio need — `--recursive` would drag in the graphics,
video and scripting dependencies, which are hundreds of megabytes this library
does not compile:

```sh
git submodule update --init vendor/engine-sim
git -C vendor/engine-sim submodule update --init \
    dependencies/submodules/simple-2d-constraint-solver
```

### Native

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Produces `build/libenginesim.a`. The test starts both presets, cranks them,
opens the throttle and checks they make sound and hold a dyno load.

### WebAssembly

With the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)
activated (`source /path/to/emsdk/emsdk_env.sh`):

```sh
emcmake cmake -B build-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm -j
```

This regenerates `bindings/js/enginesim-worklet.js` in the source tree, which
is the file the demo loads. It is committed so the demo works from a bare
clone.

Two constraints shape that build, both from the AudioWorklet global scope:

- **There is no `fetch` in a worklet**, so nothing can be loaded at runtime.
  The wasm is inlined into the JavaScript as base64 (`-sSINGLE_FILE=1`) and
  the Emscripten glue is concatenated with the processor into one file that
  `addModule()` can take whole.
- **There is no `performance` either**, which Emscripten's clock shim assumes.
  The bundle prepends a two-line shim over `currentTime` before the glue runs.

The simulation is hosted *inside* the worklet rather than on the main thread,
with telemetry posted out at 20 Hz. That is what keeps `SharedArrayBuffer` out
of the design, and why the demo runs on GitHub Pages with no cross-origin
isolation headers.

## Using it from JavaScript

```js
import { EngineSim, Preset } from './bindings/js/enginesim.js';

const engine = await EngineSim.create({ preset: Preset.V8 });
await engine.resume();               // call from a user gesture

engine.setStarter(true);
engine.onTelemetry((t) => console.log(t.rpm));
engine.setThrottle(0.5);
```

`engine.output` is the `AudioWorkletNode`, already connected to the
destination — connect it to your own nodes for effects or metering.
`engine.context` is the `AudioContext`, and you can supply your own to mix
with other audio. Types are in
[`bindings/js/enginesim.d.ts`](bindings/js/enginesim.d.ts).

## Layout

```
include/enginesim.h      the ABI - the product
src/                     ABI implementation, engine builder, presets
src/js/processor.js      the AudioWorkletProcessor that hosts the simulation
bindings/js/             the JavaScript binding and its built worklet bundle
patches/                 portability fixes applied to a copy of upstream
demo/                    the demo page: vanilla HTML, CSS and modules
tests/smoke.c            drives both presets natively
vendor/engine-sim        upstream, as a submodule, byte for byte
```

Adding a second language means adding one directory under `bindings/`. There
is nothing to generalise first, and deliberately no abstraction waiting for
one.

## Upstream, and why it is patched

Upstream only ever built under MSVC. Seven files need small portability fixes
to compile with GCC, Clang or Emscripten — `extern constexpr` that violates
ODR, a nested class used before it is complete, backslash include paths, a
member name that collides.

Rather than fork, the build copies upstream into the build tree and patches
the copy, so the submodule stays byte-identical to the commit it points at and
can be moved forward normally. `patches/0001-portability.patch` is pinned to
that commit; if it stops applying cleanly — or applies but changes nothing,
which `git apply` will happily report as success — CMake fails loudly instead
of building something silently wrong.

Only the 41 sources the physics and audio need are compiled. The renderer,
the Piranha scripting engine and the video tooling are never cloned, never
built and never linked.

## Known gaps

These are real and unfixed, not roadmap items dressed up as features.

- **The synthesizer calls the global `rand()`.** Upstream's `synthesizer.cpp`
  and `combustion_chamber.cpp` use it directly, so output is not reproducible
  between runs and two simulations in one process draw from the same sequence.
  This is the one place the "no globals" contract leaks. Fixing it means
  patching upstream further than portability requires.
- **Eight cylinders maximum.** Upstream indexes a fixed 8-slot array by
  cylinder number. `es_sim_create` rejects anything larger rather than
  corrupting memory, so V10s and V12s are not expressible.
- **Clipping at full volume.** Upstream's leveling filter can only attenuate,
  and its gain lags the signal, so loud transients saturate the int16 stage
  before the ABI's float conversion sees them. Measured on the V8 at wide-open
  throttle: 2.6% of samples at full scale with volume 1.0, 0.007% at 0.7. The
  demo ships at 0.7 for that reason. Leave headroom.
- **Two presets**, the V8 and the inline-4, transcribed from upstream's
  definitions. Anything else you build by hand.
- **The dyno is the only load.** A vehicle and transmission exist inside the
  simulation, but no gear or clutch control is exposed, so there is no way to
  drive the engine through a drivetrain.
- **Tested on Linux (GCC 15) and Emscripten 6 only.** The `__declspec` export
  plumbing in the header is written but has never been compiled by MSVC, and
  macOS is unexercised.

## Licence

MIT, and it has to be — upstream engine-sim and simple-2d-constraint-solver
are both MIT (© 2022 Ange Yaghi), and nothing in the dependency graph is
copyleft. See [LICENSE](LICENSE).

The physics model, the gas dynamics and the audio synthesis are entirely
AngeTheGreat's work. This repository is a wrapper around them.
