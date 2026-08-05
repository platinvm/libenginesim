# libenginesim

[![CI](https://github.com/platinvm/libenginesim/actions/workflows/ci.yml/badge.svg)](https://github.com/platinvm/libenginesim/actions/workflows/ci.yml)

[AngeTheGreat's engine-sim](https://github.com/ange-yaghi/engine-sim) as a
headless C library: a piston engine you can step, read and pull audio from,
with no windows, no threads, no files and no audio device anywhere in it.

The physics and the sound synthesis are upstream's, unmodified. What this adds
is a stable C ABI in front of them, a WebAssembly build, a typed JavaScript
binding, and a dashboard where you edit an engine and hear the change.

**[Try it →](https://platinvm.github.io/libenginesim/)**

## Layout

```
core/       the C library, its public header, and vendor/ (upstream, as a
            submodule, byte for byte)
bindings/   language bindings; js/ is the only one so far
examples/   apps built on a binding; dashboard/ is the live editor
```

`core/include/enginesim.h` is the product: 16 functions and some plain structs.
Everything else consumes it. Adding a second language means adding one
directory under `bindings/`, and nothing needs generalising first.

## Building

Clone only the two submodules the physics and audio need. `--recursive` would
drag in the renderer, the scripting engine and the video tooling, hundreds of
megabytes this library never compiles:

```sh
git submodule update --init core/vendor/engine-sim
git -C core/vendor/engine-sim submodule update --init \
    dependencies/submodules/simple-2d-constraint-solver
```

### The C library

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Produces `build/libenginesim.a`. The test starts both built-in engines, cranks
them, opens the throttle and checks they make sound and hold a dyno load.

### The dashboard

Needs [Emscripten](https://emscripten.org/docs/getting_started/downloads.html)
and Node 22 or newer:

```sh
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -B build-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm -j          # writes bindings/js/wasm/ and src/types.ts

cd examples/dashboard
npm install          # links bindings/js as `libenginesim`
npm run dev
```

Nothing generated is committed, so the wasm build has to run first. CMake owns
every generated file: the `.wasm`, its loader, and `bindings/js/src/types.ts`.

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

Four things the header commits to:

- **Audio frames are the unit of time.** `es_sim_step` is the only clock.
  Physics and synthesis advance together and cannot drift apart, and the host
  decides when — an audio callback, a game loop, an offline render.
- **The host owns everything it passes in.** Definitions are copied whole
  during `es_sim_create`; nothing is retained past that call.
- **Engines are typed structs.** Upstream's Piranha scripting layer is not
  compiled and not exposed. If you want a file format, write it above this
  library, not inside it.
- **A handle is not thread-safe, and handles share nothing.** Two simulations
  can be driven from two threads. One cannot.

Units are SI except where upstream's model is itself defined in others — RPM,
and the CFM ratings that `es_flow_from_carb_cfm` and `es_flow_from_cfm_28`
convert into the dimensionless flow constants the gas model wants. Passing raw
CFM where a flow constant belongs is off by five orders of magnitude and will
detonate the engine on the first cycle.

## Using it from JavaScript

```ts
import { EngineSim, units as u } from 'libenginesim';

const engine = await EngineSim.create();
await engine.resume();               // may need a user gesture first

engine.setStarter(true);
engine.onTelemetry((t) => console.log(t.rpm));

/* Any engine, not just the built-in ones. */
await engine.setEngine({ ...engine.presets[1].def, redline: 9000 });
```

`engine.presets` are read out of the library at startup and handed over as
plain objects, so they are the real definitions rather than a copy kept in
JavaScript. Edit one and pass it back to `setEngine`, or build one from
nothing. `setEngine` rejects an invalid definition and leaves the previous
engine running, so a half-finished edit never drops you into silence.

`engine.output` is the `AudioWorkletNode`, already connected to the
destination — connect it to your own nodes for effects or metering.

### How arbitrary engines work

`es_engine_def` is a plain C struct, and the compiler owns its layout. Rather
than copy that layout into JavaScript and watch the two drift, the wasm module
reports a description of itself, and the marshaller walks it. The TypeScript
types in `bindings/js/src/types.ts` come from the same description at build
time. So adding a field to `enginesim.h` gives you the marshalling and the type
at once, and removing one turns every stale use into a compile error.

### How the wasm is loaded

An `AudioWorkletGlobalScope` has no `fetch`, no `URL` and no `TextEncoder`.
Rather than embed the wasm in a script to sidestep that, the main thread
fetches `enginesim.wasm` as an ordinary file and hands the bytes to the worklet
in `processorOptions`; the worklet passes them to Emscripten's `instantiateWasm`
hook, which is the one path that never constructs a `URL`. Strings are written
through Emscripten's own UTF-8 writer, which degrades without `TextEncoder`.

The simulation is hosted *inside* the worklet, with telemetry posted out at
20 Hz. That keeps `SharedArrayBuffer` out of the design, and is why the demo
runs on GitHub Pages with no cross-origin isolation headers.

## Tests

```sh
ctest --test-dir build                     # native: both engines run
cd examples/dashboard && npm test          # types, then marshalling round-trip
npm run test:browser                       # real Chromium (needs `npx playwright install chromium`)
```

`bindings/js/test/marshal.ts` is the interesting one. It reads each built-in
engine out of the library into a plain object, writes it back into a fresh
struct, runs both, and compares them — so a dropped or misplaced field shows up
as an engine that behaves differently. Then it builds a single-cylinder engine
that was never a preset, which is what proves a binding can create arbitrary
engines rather than pick from a list.

## Upstream, and why it is patched

Upstream only ever built under MSVC. Seven files need small portability fixes to
compile with GCC, Clang or Emscripten — `extern constexpr` that violates ODR, a
nested class used before it is complete, backslash include paths, a member name
that collides.

Rather than fork, the build copies upstream into the build tree and patches the
copy, so the submodule stays byte-identical to the commit it points at.
`core/patches/0001-portability.patch` is pinned to that commit; if it stops
applying cleanly — or applies but changes nothing, which `git apply` reports as
success — CMake fails loudly instead of building something silently wrong.

Only the 41 sources the physics and audio need are compiled. The renderer, the
Piranha scripting engine and the video tooling are never cloned, never built and
never linked.

## Known gaps

- **The synthesizer calls the global `rand()`.** Upstream's `synthesizer.cpp`
  and `combustion_chamber.cpp` use it directly, so output is not reproducible
  between runs and two simulations in one process draw from the same sequence.
  This is the one place the "no globals" contract leaks.
- **Eight cylinders maximum.** Upstream indexes a fixed 8-slot array by cylinder
  number. `es_sim_create` rejects anything larger rather than corrupting memory,
  so V10s and V12s are not expressible.
- **Clipping at full volume.** Upstream's leveling filter can only attenuate and
  its gain lags the signal, so loud transients saturate the int16 stage before
  the ABI's float conversion sees them. On the V8 at wide-open throttle: 2.6% of
  samples at full scale with volume 1.0, 0.007% at 0.7. Leave headroom.
- **One built-in impulse response, shared by every engine.** Upstream pairs each
  engine with its own, and a response's energy and an exhaust's `audio_volume`
  only mean anything together. So upstream's literal mix levels do not transfer:
  the inline-4's are 64× its upstream values, measured against the shipped
  response. Build an engine with upstream's numbers verbatim and it may come out
  inaudible or clipped.
- **Rebuilding a running engine restarts it.** `setEngine` hands the replacement
  the speed its predecessor had, but a fresh simulation has no gas charge and
  stalls within half a second, so it is motored for one second the way a starter
  would. You hear that.
- **`es_engine_def.flywheel_radius` is not read by anything.** Upstream's
  `Crankshaft::Parameters` has no such field; only `crank_moment_of_inertia`
  reaches the physics. The field is inert and should come out of the ABI.
- **The simulation is barely faster than real time, and glitches.** Measured
  in an audio callback with a 128-frame quantum (2.90 ms of budget per block):
  at upstream's own rates the V8 ran at 0.91x real time and the inline-4 at
  0.72x, so the audio thread could not fill every block and the output popped
  continuously. Their simulation frequencies are now 4000 and 6000 Hz rather
  than upstream's 10000 and 20000, which buys 1.27x and 1.58x. Occasional
  blocks still overrun, and lowering the rate costs high-frequency detail in
  the sound. This wants profiling, not another guess at a number.
- **Two built-in engines**, the V8 and the inline-4. Anything else you write
  yourself — which is now the point.
- **The dyno is the only load.** A vehicle and transmission exist inside the
  simulation, but no gear or clutch control is exposed.
- **Tested on Linux (GCC 15) and Emscripten 6 only.** The `__declspec` export
  plumbing in the header has never been compiled by MSVC, and macOS is
  unexercised.

## Licence

MIT, and it has to be — upstream engine-sim and simple-2d-constraint-solver are
both MIT (© 2022 Ange Yaghi), and nothing in the dependency graph is copyleft.
See [LICENSE](LICENSE).

The physics model, the gas dynamics and the audio synthesis are entirely
AngeTheGreat's work. This repository is a wrapper around them.
