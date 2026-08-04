/*
 * Emscripten entry points.
 *
 * The C ABI is the product; this file only keeps its symbols alive through
 * the linker and adds the two things a wasm host cannot do for itself:
 * allocate a definition struct without knowing its layout, and read struct
 * fields without hand-written offsets.
 *
 * Nothing here is JS-specific beyond that. A binding still calls es_sim_step,
 * es_sim_set_throttle and the rest directly.
 */
#include "enginesim.h"

#include <emscripten/emscripten.h>

#include <cstdlib>
#include <cstring>

extern "C" {

/*
 * Struct sizes, so a binding can allocate correctly without duplicating the
 * layout. Offsets are not exposed: the accessors below cover every field a
 * host needs, which keeps the layout free to change.
 */
EMSCRIPTEN_KEEPALIVE uint32_t es_js_sizeof_engine_def(void) {
    return (uint32_t)sizeof(es_engine_def);
}
EMSCRIPTEN_KEEPALIVE uint32_t es_js_sizeof_sim_config(void) {
    return (uint32_t)sizeof(es_sim_config);
}
EMSCRIPTEN_KEEPALIVE uint32_t es_js_sizeof_telemetry(void) {
    return (uint32_t)sizeof(es_telemetry);
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

/*
 * Telemetry field accessors, indexed rather than offset-mapped. The order
 * matches es_telemetry as declared; a binding reads what it needs by name
 * through its own enum.
 */
EMSCRIPTEN_KEEPALIVE double es_js_telemetry_get(const es_telemetry *t, uint32_t field) {
    if (t == nullptr) return 0.0;
    switch (field) {
        case 0:  return t->rpm;
        case 1:  return t->torque;
        case 2:  return t->power;
        case 3:  return t->throttle;
        case 4:  return t->throttle_plate_position;
        case 5:  return t->manifold_pressure;
        case 6:  return t->intake_afr;
        case 7:  return t->exhaust_o2;
        case 8:  return t->fuel_consumed;
        case 9:  return t->redline;
        case 10: return t->displacement;
        case 11: return (double)t->cylinder_count;
        case 12: return (double)t->rev_limiter_active;
        default: return 0.0;
    }
}

/* Builds a preset into caller-allocated storage. */
EMSCRIPTEN_KEEPALIVE es_result es_js_preset(uint32_t preset, es_engine_def *out) {
    return es_preset_engine((es_preset)preset, out);
}

/* Redline before a simulation exists, so a UI can scale its gauges. */
EMSCRIPTEN_KEEPALIVE double es_js_def_redline(const es_engine_def *def) {
    return (def != nullptr) ? def->redline : 0.0;
}

} /* extern "C" */
