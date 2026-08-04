/*
 * libenginesim - a headless, embeddable piston engine simulator.
 *
 * Built on engine-sim by AngeTheGreat (MIT). This header is the whole product;
 * every language binding sits on top of it.
 *
 * Design contract
 * ---------------
 *   - Pure state machine. No threads, no globals, no file or device I/O.
 *     Every call is host-driven and returns without blocking.
 *   - The host owns all memory it passes in. The library never takes ownership
 *     of a pointer in a definition struct and never retains one past the call
 *     it was given to, except where explicitly stated.
 *   - A simulation handle is not thread-safe. Distinct handles share nothing
 *     and may be driven concurrently from different threads.
 *   - Units are SI unless the field name says otherwise: metres, kilograms,
 *     radians, seconds, pascals, newton-metres, watts. RPM is spelled out.
 */

#ifndef ENGINESIM_H
#define ENGINESIM_H

#include <stdint.h>

#if defined(_WIN32) && defined(ES_SHARED)
#  ifdef ES_BUILDING
#    define ES_API __declspec(dllexport)
#  else
#    define ES_API __declspec(dllimport)
#  endif
#else
#  define ES_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ ABI -- */

#define ES_ABI_VERSION 1

/* Returns ES_ABI_VERSION as compiled into the library. A binding should
 * refuse to run against a library whose value differs from its own. */
ES_API uint32_t es_abi_version(void);

typedef enum es_result {
    ES_OK = 0,
    ES_ERR_INVALID_ARGUMENT = 1,
    ES_ERR_OUT_OF_MEMORY = 2,
    ES_ERR_UNSUPPORTED = 3
} es_result;

/* Static, never-null description of a result code. Valid for program life. */
ES_API const char *es_result_str(es_result result);

/* Upstream indexes a fixed 8-slot array by cylinder number, so engines beyond
 * eight cylinders are rejected at creation rather than corrupting memory. */
#define ES_MAX_CYLINDERS 8

/* -------------------------------------------------------------- curves --- */

/*
 * A sampled lookup curve: `count` (x, y) pairs in ascending x. Used for cam
 * lobe profiles, port flow and ignition timing.
 *
 * `x` and `y` are host-owned and are copied during es_sim_create, so they may
 * be freed as soon as that call returns.
 *
 * `filter_radius` is the half-width, in x units, of the smoothing upstream
 * applies when sampling. Pass 0 to use the curve as given.
 */
typedef struct es_curve {
    const double *x;
    const double *y;
    uint32_t count;
    double filter_radius;
} es_curve;

/* --------------------------------------------------------- engine defs --- */

/*
 * One bank of cylinders and the head that sits on it. An inline engine has a
 * single bank; a V8 has two.
 */
typedef struct es_bank_def {
    /* Bank angle from vertical. Positive tilts right. */
    double angle;
    uint32_t cylinder_count;

    /* Combustion chamber volume at top dead centre. Sets compression ratio
     * together with the bore and stroke on es_engine_def. */
    double chamber_volume;

    /* Port flow as a function of valve lift (m). y is in SCFM, matching
     * upstream's flow model. */
    es_curve intake_flow;
    es_curve exhaust_flow;

    /* Cam. `lobe_profile` maps camshaft angle (rad, 0 at the lobe peak) to
     * normalised lift in [0, 1]; it is scaled by the lift figures below. */
    es_curve lobe_profile;
    double intake_lobe_center;   /* rad after TDC */
    double exhaust_lobe_center;  /* rad before TDC */
    double intake_lift;
    double exhaust_lift;
} es_bank_def;

/*
 * A complete engine. Build one in code, or start from a preset and adjust.
 *
 * Every pointer in this struct (and in any nested es_curve) is host-owned and
 * is fully copied by es_sim_create.
 */
typedef struct es_engine_def {
    /* Informational; copied. May be NULL. */
    const char *name;

    /* Physics tick rate. Upstream engines use 10000. Higher is more stable
     * and more expensive; this does not affect the audio sample rate. */
    uint32_t simulation_frequency;

    /* -- rotating assembly -- */
    double crank_throw;             /* half the stroke */
    double crank_mass;
    double crank_moment_of_inertia;
    double crank_friction_torque;
    double flywheel_mass;
    double flywheel_radius;

    double rod_mass;
    double rod_length;
    double rod_moment_of_inertia;
    double rod_center_of_mass;      /* from the big end */

    double piston_mass;
    double piston_compression_height;
    double bore;
    double stroke;

    /* Crank pin angles, one per rod journal, in firing-geometry order. A
     * cross-plane V8 has four; an inline-4 has four. */
    const double *rod_journal_angles;
    uint32_t rod_journal_count;

    /* -- banks -- */
    const es_bank_def *banks;
    uint32_t bank_count;

    /* -- intake -- */
    double intake_plenum_volume;
    double intake_plenum_cross_section;
    double intake_runner_length;
    double intake_runner_flow_rate;   /* SCFM */
    double intake_flow_rate;          /* SCFM, throttle body */
    double intake_idle_flow_rate;     /* SCFM, bypass at closed throttle */
    double intake_idle_throttle_plate_position; /* 0..1 */
    double intake_velocity_decay;

    /* -- exhaust -- */
    double exhaust_volume;
    double exhaust_collector_cross_section;
    double exhaust_outlet_flow_rate;  /* SCFM */
    double exhaust_primary_tube_length;
    double exhaust_primary_flow_rate; /* SCFM */
    double exhaust_length;
    double exhaust_audio_volume;      /* 0..1, this bank's share of the mix */

    /* -- ignition -- */
    /* Cylinder firing order as indices into the flattened cylinder list
     * (bank 0's cylinders first, then bank 1's, ...). Exactly as many entries
     * as there are cylinders. */
    const uint32_t *firing_order;
    /* Ignition advance (rad before TDC) as a function of engine speed (RPM). */
    es_curve timing_curve;
    double rev_limit;               /* RPM */
    double redline;                 /* RPM, reported in telemetry */

    /* -- starter -- */
    double starter_torque;
    double starter_speed;           /* RPM */

    /* -- audio character --
     * Upstream's synthesis knobs, preserved verbatim. Sensible values are
     * roughly hf_gain 0.01, jitter 0.5, noise 1.0. */
    double hf_gain;
    double jitter;
    double noise;
} es_engine_def;

/* ------------------------------------------------------------- presets --- */

typedef enum es_preset {
    ES_PRESET_INLINE_4 = 0,
    ES_PRESET_V8 = 1
} es_preset;

/*
 * Fills `out` with a ready-to-run engine definition so a binding can make
 * noise without shipping data files.
 *
 * The pointers written into `out` reference static library data that is valid
 * for the life of the program; the caller may point them elsewhere but must
 * not free them. Adjust any field and pass the result to es_sim_create.
 */
ES_API es_result es_preset_engine(es_preset preset, es_engine_def *out);

/* ---------------------------------------------------------- simulation --- */

typedef struct es_sim es_sim;

typedef struct es_sim_config {
    /* Output sample rate in Hz. 0 selects 44100. */
    uint32_t sample_rate;

    /* Convolution impulse response giving the exhaust its character: mono
     * 16-bit PCM at `sample_rate`. Leave NULL to use the built-in response.
     * Copied during es_sim_create. */
    const int16_t *impulse_response;
    uint32_t impulse_response_frames;
    double impulse_response_volume;   /* 0 selects 1.0 */
} es_sim_config;

/*
 * Creates a simulation. `config` may be NULL for defaults.
 *
 * Returns ES_ERR_INVALID_ARGUMENT if the definition is inconsistent - most
 * commonly a cylinder count above ES_MAX_CYLINDERS, a firing order whose
 * length disagrees with the cylinder count, or an empty curve.
 *
 * On success `*out` receives a handle that must be released with
 * es_sim_destroy. On failure `*out` is set to NULL.
 */
ES_API es_result es_sim_create(const es_engine_def *def,
                               const es_sim_config *config,
                               es_sim **out);

/* Releases a simulation. Passing NULL is a no-op. */
ES_API void es_sim_destroy(es_sim *sim);

/* --------------------------------------------------------------- drive --- */

/*
 * Advances the simulation by `frames` audio frames - that is, by
 * frames / sample_rate seconds - and writes exactly that many mono samples,
 * normalised to [-1, 1], into `audio`.
 *
 * Physics and synthesis advance together by construction, so they cannot drift
 * apart. Pass audio = NULL to run the physics alone, which is how you sweep a
 * dyno faster than real time.
 *
 * Returns the number of frames written, which equals `frames` unless the
 * arguments were invalid, in which case it is 0.
 *
 * This is the only call that consumes time, and it never blocks. Call it from
 * an audio callback, a game loop, or a batch job.
 */
ES_API uint32_t es_sim_step(es_sim *sim, float *audio, uint32_t frames);

/* All controls are clamped to their valid range and take effect on the next
 * es_sim_step. */

/* 0 = closed, 1 = wide open. */
ES_API void es_sim_set_throttle(es_sim *sim, double throttle);

/* Engages the starter motor. Nonzero to crank. */
ES_API void es_sim_set_starter(es_sim *sim, int engaged);

/* Cuts spark when zero. Engines start with ignition on. */
ES_API void es_sim_set_ignition(es_sim *sim, int enabled);

/*
 * Holds the crankshaft at `speed` RPM so torque can be measured, the way a
 * real dyno does. Pass enabled = 0 to let the engine spin freely.
 */
ES_API void es_sim_set_dyno(es_sim *sim, int enabled, double speed);

/* Output gain applied after synthesis. 1.0 is unity. */
ES_API void es_sim_set_volume(es_sim *sim, double volume);

/* ----------------------------------------------------------- telemetry --- */

typedef struct es_telemetry {
    double rpm;
    double torque;                  /* N·m at the crank */
    double power;                   /* W */
    double throttle;                /* commanded, 0..1 */
    double throttle_plate_position; /* actual, 0..1 */
    double manifold_pressure;       /* Pa, absolute */
    double intake_afr;
    double exhaust_o2;
    double fuel_consumed;           /* kg since creation */
    double redline;                 /* RPM, from the definition */
    double displacement;            /* m^3, computed from geometry */
    uint32_t cylinder_count;
    /* Nonzero while the rev limiter is cutting spark. */
    int32_t rev_limiter_active;
} es_telemetry;

/* Samples the current state. `out` must be non-NULL. Cheap; safe to call
 * every frame. */
ES_API void es_sim_telemetry(const es_sim *sim, es_telemetry *out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ENGINESIM_H */
