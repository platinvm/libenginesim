/*
 * Emscripten entry points.
 *
 * The C ABI is the product; this file keeps its symbols alive through the
 * linker and adds the one thing a wasm host genuinely cannot do for itself:
 * find out where the fields of a struct are.
 *
 * It answers that with a schema rather than a wall of accessors. The compiler
 * owns the layout, offsetof reports it, and the binding marshals against what
 * it is told at run time. There is no second copy of the layout to keep in
 * step: adding a field to enginesim.h costs one line here and nothing at all
 * on the JavaScript side.
 */
#include "enginesim.h"

#include <emscripten/emscripten.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

/*
 * One line per struct, fields separated by '|':
 *
 *   name:size|field:offset:kind[:arg]|...
 *
 * Kinds:
 *   f64 u32 i32        scalars
 *   str                const char *, NUL-terminated
 *   sub:S              struct S stored inline
 *   arr:S:countField   pointer to an array of struct S
 *   arrf:countField    pointer to an array of double
 *   arru:countField    pointer to an array of uint32_t
 *   arri16:countField  pointer to an array of int16_t
 *
 * A count of "@cylinders" means the array is as long as the engine has
 * cylinders. firing_order is the only field that carries no count of its own.
 */
std::string &schema() {
    static std::string cached;
    if (!cached.empty()) return cached;

    struct Builder {
        std::string out;
        void begin(const char *name, size_t size) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%s:%zu", name, size);
            if (!out.empty()) out += '\n';
            out += buf;
        }
        void field(const char *name, size_t off, const char *kind) {
            char buf[160];
            std::snprintf(buf, sizeof buf, "|%s:%zu:%s", name, off, kind);
            out += buf;
        }
    } b;

#define BEGIN(st) b.begin(#st, sizeof(es_##st))
#define FIELD(st, name, kind) b.field(#name, offsetof(es_##st, name), kind)

    BEGIN(curve);
    FIELD(curve, x, "arrf:count");
    FIELD(curve, y, "arrf:count");
    FIELD(curve, count, "u32");
    FIELD(curve, filter_radius, "f64");

    BEGIN(cam_lobe);
    FIELD(cam_lobe, duration_at_50_thou, "f64");
    FIELD(cam_lobe, gamma, "f64");
    FIELD(cam_lobe, lift, "f64");
    FIELD(cam_lobe, steps, "u32");

    BEGIN(cylinder_def);
    FIELD(cylinder_def, rod_journal, "u32");
    FIELD(cylinder_def, exhaust_system, "u32");
    FIELD(cylinder_def, sound_attenuation, "f64");
    FIELD(cylinder_def, primary_length, "f64");
    FIELD(cylinder_def, blowby, "f64");

    BEGIN(exhaust_def);
    FIELD(exhaust_def, outlet_flow_rate, "f64");
    FIELD(exhaust_def, primary_tube_length, "f64");
    FIELD(exhaust_def, primary_flow_rate, "f64");
    FIELD(exhaust_def, velocity_decay, "f64");
    FIELD(exhaust_def, volume, "f64");
    FIELD(exhaust_def, collector_cross_section_area, "f64");
    FIELD(exhaust_def, audio_volume, "f64");
    FIELD(exhaust_def, length, "f64");

    BEGIN(bank_def);
    FIELD(bank_def, angle, "f64");
    FIELD(bank_def, cylinders, "arr:cylinder_def:cylinder_count");
    FIELD(bank_def, cylinder_count, "u32");
    FIELD(bank_def, chamber_volume, "f64");
    FIELD(bank_def, intake_runner_volume, "f64");
    FIELD(bank_def, intake_runner_cross_section_area, "f64");
    FIELD(bank_def, exhaust_runner_volume, "f64");
    FIELD(bank_def, exhaust_runner_cross_section_area, "f64");
    FIELD(bank_def, intake_flow, "sub:curve");
    FIELD(bank_def, exhaust_flow, "sub:curve");
    FIELD(bank_def, intake_lobe, "sub:cam_lobe");
    FIELD(bank_def, exhaust_lobe, "sub:cam_lobe");
    FIELD(bank_def, intake_lobe_center, "f64");
    FIELD(bank_def, exhaust_lobe_center, "f64");
    FIELD(bank_def, cam_advance, "f64");
    FIELD(bank_def, cam_base_radius, "f64");

    BEGIN(engine_def);
    FIELD(engine_def, name, "str");
    FIELD(engine_def, simulation_frequency, "u32");
    FIELD(engine_def, bore, "f64");
    FIELD(engine_def, stroke, "f64");
    FIELD(engine_def, crank_mass, "f64");
    FIELD(engine_def, crank_moment_of_inertia, "f64");
    FIELD(engine_def, crank_friction_torque, "f64");
    FIELD(engine_def, flywheel_mass, "f64");
    FIELD(engine_def, flywheel_radius, "f64");
    FIELD(engine_def, rod_mass, "f64");
    FIELD(engine_def, rod_length, "f64");
    FIELD(engine_def, rod_moment_of_inertia, "f64");
    FIELD(engine_def, rod_center_of_mass, "f64");
    FIELD(engine_def, piston_mass, "f64");
    FIELD(engine_def, piston_compression_height, "f64");
    FIELD(engine_def, rod_journal_angles, "arrf:rod_journal_count");
    FIELD(engine_def, rod_journal_count, "u32");
    FIELD(engine_def, banks, "arr:bank_def:bank_count");
    FIELD(engine_def, bank_count, "u32");
    FIELD(engine_def, exhaust_systems, "arr:exhaust_def:exhaust_system_count");
    FIELD(engine_def, exhaust_system_count, "u32");
    FIELD(engine_def, intake_plenum_volume, "f64");
    FIELD(engine_def, intake_plenum_cross_section_area, "f64");
    FIELD(engine_def, intake_runner_length, "f64");
    FIELD(engine_def, intake_runner_flow_rate, "f64");
    FIELD(engine_def, intake_flow_rate, "f64");
    FIELD(engine_def, intake_idle_flow_rate, "f64");
    FIELD(engine_def, intake_idle_throttle_plate_position, "f64");
    FIELD(engine_def, intake_velocity_decay, "f64");
    FIELD(engine_def, throttle_gamma, "f64");
    FIELD(engine_def, firing_order, "arru:@cylinders");
    FIELD(engine_def, timing_curve, "sub:curve");
    FIELD(engine_def, rev_limit, "f64");
    FIELD(engine_def, rev_limit_duration, "f64");
    FIELD(engine_def, redline, "f64");
    FIELD(engine_def, starter_torque, "f64");
    FIELD(engine_def, starter_speed, "f64");
    FIELD(engine_def, hf_gain, "f64");
    FIELD(engine_def, jitter, "f64");
    FIELD(engine_def, noise, "f64");

    BEGIN(sim_config);
    FIELD(sim_config, sample_rate, "u32");
    FIELD(sim_config, impulse_response, "arri16:impulse_response_frames");
    FIELD(sim_config, impulse_response_frames, "u32");
    FIELD(sim_config, impulse_response_volume, "f64");

    BEGIN(telemetry);
    FIELD(telemetry, rpm, "f64");
    FIELD(telemetry, torque, "f64");
    FIELD(telemetry, power, "f64");
    FIELD(telemetry, throttle, "f64");
    FIELD(telemetry, throttle_plate_position, "f64");
    FIELD(telemetry, manifold_pressure, "f64");
    FIELD(telemetry, intake_afr, "f64");
    FIELD(telemetry, exhaust_o2, "f64");
    FIELD(telemetry, fuel_consumed, "f64");
    FIELD(telemetry, redline, "f64");
    FIELD(telemetry, displacement, "f64");
    FIELD(telemetry, cylinder_count, "u32");
    FIELD(telemetry, rev_limiter_active, "i32");

#undef BEGIN
#undef FIELD

    cached = b.out;
    return cached;
}

} /* namespace */

extern "C" {

/* The struct layout, as the compiler laid it out. Valid for program life. */
EMSCRIPTEN_KEEPALIVE const char *es_js_schema(void) {
    return schema().c_str();
}

/* How many es_preset values exist, so a host can enumerate them. */
EMSCRIPTEN_KEEPALIVE uint32_t es_js_preset_count(void) {
    uint32_t n = 0;
    es_engine_def probe;
    while (es_preset_engine((es_preset)n, &probe) == ES_OK) ++n;
    return n;
}

/* Zeroed scratch space; the caller frees it with es_js_free. */
EMSCRIPTEN_KEEPALIVE void *es_js_alloc(uint32_t bytes) {
    void *p = std::malloc(bytes);
    if (p != nullptr) std::memset(p, 0, bytes);
    return p;
}

EMSCRIPTEN_KEEPALIVE void es_js_free(void *p) {
    std::free(p);
}

} /* extern "C" */
