/*
 * Drives both presets through the C ABI exactly as a host would: crank, idle,
 * blip the throttle, then measure torque on the dyno. Fails loudly rather than
 * reporting a pass it did not earn.
 */
#include "enginesim.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR 44100
#define QUANTUM 128

static int failures = 0;

static void check(int condition, const char *what) {
    printf("  %-46s %s\n", what, condition ? "ok" : "FAILED");
    if (!condition) ++failures;
}

/* Runs `seconds` of simulation in AudioWorklet-sized quanta, reporting the
 * peak sample seen so silence can be told apart from sound. */
static double run(es_sim *sim, double seconds, float *peak_out) {
    float buf[QUANTUM];
    const long quanta = (long)(seconds * SR / QUANTUM);
    float peak = 0.0f;
    for (long i = 0; i < quanta; ++i) {
        const uint32_t got = es_sim_step(sim, buf, QUANTUM);
        if (got != QUANTUM) return -1.0;
        for (uint32_t j = 0; j < QUANTUM; ++j) {
            const float a = fabsf(buf[j]);
            if (a > peak) peak = a;
        }
    }
    if (peak_out) *peak_out = peak;
    return (double)quanta * QUANTUM / SR;
}

static void exercise(es_preset preset) {
    es_engine_def def;
    es_telemetry t;
    es_sim *sim = NULL;
    float peak = 0.0f;

    printf("\n== %s ==\n", es_preset_name(preset));

    check(es_preset_engine(preset, &def) == ES_OK, "preset loads");

    const es_result r = es_sim_create(&def, NULL, &sim);
    printf("  %-46s %s\n", "simulation creates", es_result_str(r));
    if (r != ES_OK || sim == NULL) {
        ++failures;
        return;
    }

    es_sim_telemetry(sim, &t);
    printf("  displacement %.2f L across %u cylinders\n",
           t.displacement * 1000.0, t.cylinder_count);
    check(t.cylinder_count > 0 && t.cylinder_count <= ES_MAX_CYLINDERS,
          "cylinder count in range");
    check(t.displacement > 0.0005 && t.displacement < 0.01,
          "displacement is plausible");

    /* Crank it. */
    es_sim_set_starter(sim, 1);
    es_sim_set_throttle(sim, 0.0);
    check(run(sim, 1.5, &peak) > 0.0, "cranks without underrun");
    es_sim_telemetry(sim, &t);
    printf("  cranking at %.0f rpm\n", t.rpm);
    check(t.rpm > 50.0, "starter turns the engine over");

    es_sim_set_starter(sim, 0);
    check(run(sim, 1.5, &peak) > 0.0, "idles without underrun");
    es_sim_telemetry(sim, &t);
    printf("  idle %.0f rpm, peak sample %.4f\n", t.rpm, peak);
    check(t.rpm > 300.0, "keeps running once the starter is released");
    check(peak > 0.001f, "produces audible output");
    check(peak <= 1.0f, "output stays inside [-1, 1]");

    /* Blip it. */
    es_sim_set_throttle(sim, 1.0);
    run(sim, 1.5, &peak);
    es_sim_telemetry(sim, &t);
    const double revving = t.rpm;
    printf("  wide open %.0f rpm, manifold %.0f kPa\n",
           revving, t.manifold_pressure / 1000.0);
    check(revving > 1000.0, "revs up under throttle");
    check(t.rpm < def.rev_limit * 1.15, "stays near the rev limit");

    /* Measure torque the way a dyno does. */
    es_sim_set_dyno(sim, 1, 3000.0);
    run(sim, 2.0, NULL);
    es_sim_telemetry(sim, &t);
    printf("  dyno at 3000 rpm: %.0f Nm, %.0f kW\n", t.torque, t.power / 1000.0);
    check(t.torque > 10.0, "makes positive torque on the dyno");

    /* Physics-only stepping, for faster-than-real-time work. */
    check(es_sim_step(sim, NULL, 4096) == 4096, "runs without an audio buffer");

    es_sim_destroy(sim);
}

int main(void) {
    printf("libenginesim ABI v%u\n", es_abi_version());

    /* A definition that breaks the documented rules must be refused, not
     * crash: nine cylinders exceeds what upstream can index. */
    es_engine_def bad;
    es_sim *nope = NULL;
    if (es_preset_engine(ES_PRESET_V8, &bad) == ES_OK) {
        es_bank_def banks[2];
        memcpy(banks, bad.banks, sizeof(banks));
        banks[0].cylinder_count = 5;   /* 5 + 4 = 9 */
        bad.banks = banks;
        printf("\n== validation ==\n");
        check(es_sim_create(&bad, NULL, &nope) == ES_ERR_INVALID_ARGUMENT,
              "rejects more cylinders than upstream can index");
        check(nope == NULL, "leaves the out-parameter null on failure");
    }

    exercise(ES_PRESET_INLINE_4);
    exercise(ES_PRESET_V8);

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
