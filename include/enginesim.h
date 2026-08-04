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
 *   - The host owns all memory it passes in. Definition structs are fully
 *     copied during es_sim_create; nothing is retained past that call.
 *   - A simulation handle is not thread-safe. Distinct handles share nothing
 *     and may be driven concurrently from different threads.
 *   - Units are SI unless a field name says otherwise: metres, kilograms,
 *     radians, seconds, pascals, newton-metres, watts. RPM and SCFM are
 *     spelled out where upstream's model is defined in them.
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
 * eight cylinders are rejected by es_sim_create rather than corrupting
 * memory. See "Known gaps" in the README. */
#define ES_MAX_CYLINDERS 8

/* --------------------------------------------------------------- units --- */

/*
 * Upstream models gas flow through a restriction with a dimensionless "flow
 * constant" rather than a volumetric rate. These convert the ratings parts are
 * actually sold under. Pure functions; no state.
 *
 * es_flow_from_carb_cfm  - carburettor CFM, for throttle bodies, runners and
 *                          exhaust tubing.
 * es_flow_from_cfm_28    - CFM measured at 28 inH2O, the flow-bench
 *                          convention, used here for piston ring blowby.
 */
ES_API double es_flow_from_carb_cfm(double scfm);
ES_API double es_flow_from_cfm_28(double scfm);

/* -------------------------------------------------------------- curves --- */

/*
 * A sampled lookup curve: `count` (x, y) pairs in ascending x.
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

/*
 * A cam lobe, generated from the numbers a cam card is specified in rather
 * than as a sampled profile. Upstream's harmonic generator is reproduced
 * exactly.
 */
typedef struct es_cam_lobe {
    double duration_at_50_thou;  /* rad of crank rotation at 0.050" lift */
    double gamma;                /* profile aggressiveness; 1.1 is typical */
    double lift;                 /* peak valve lift, m */
    uint32_t steps;              /* samples to generate; 0 selects 256 */
} es_cam_lobe;

/* --------------------------------------------------------- engine defs --- */

/*
 * One exhaust path from the head to open air. Engines commonly have more than
 * one, and the difference in their lengths is a large part of why a
 * cross-plane V8 burbles and an inline-4 does not.
 */
typedef struct es_exhaust_def {
    double outlet_flow_rate;              /* es_flow_from_carb_cfm */
    double primary_tube_length;
    double primary_flow_rate;             /* es_flow_from_carb_cfm */
    double velocity_decay;
    double volume;                        /* 0 selects upstream's default */
    double collector_cross_section_area;  /* 0 selects upstream's default */
    double audio_volume;                  /* this path's share of the mix */
    double length;                        /* total path length; sets delay */
} es_exhaust_def;

/* One cylinder's individuality within its bank. */
typedef struct es_cylinder_def {
    uint32_t rod_journal;      /* index into es_engine_def.rod_journal_angles */
    uint32_t exhaust_system;   /* index into es_engine_def.exhaust_systems */
    double sound_attenuation;  /* 0 selects 1.0 */
    double primary_length;     /* header runner length; louder as it shortens */
    double blowby;             /* es_flow_from_cfm_28; 0 for none */
} es_cylinder_def;

/*
 * One bank of cylinders and the head that sits on it. An inline engine has a
 * single bank; a V8 has two. All cylinders in a bank share a head and cam.
 */
typedef struct es_bank_def {
    /* Bank angle from vertical. Positive tilts right. */
    double angle;

    const es_cylinder_def *cylinders;
    uint32_t cylinder_count;

    /* -- head -- */
    double chamber_volume;   /* at TDC; sets compression with bore and stroke */
    double intake_runner_volume;
    double intake_runner_cross_section_area;
    double exhaust_runner_volume;
    double exhaust_runner_cross_section_area;

    /* Port flow against valve lift (m). y is a flow constant, so pass
     * flow-bench figures through es_flow_from_cfm_28 first - the same
     * conversion upstream's add_flow_sample applies. */
    es_curve intake_flow;
    es_curve exhaust_flow;

    /* -- cam -- */
    es_cam_lobe intake_lobe;
    es_cam_lobe exhaust_lobe;
    double intake_lobe_center;   /* rad after TDC */
    double exhaust_lobe_center;  /* rad before TDC */
    double cam_advance;
    double cam_base_radius;
} es_bank_def;

/*
 * A complete engine. Build one in code, or start from a preset and adjust.
 *
 * Every pointer here (and in any nested struct) is host-owned and is fully
 * copied by es_sim_create.
 */
typedef struct es_engine_def {
    /* Informational; copied. May be NULL. */
    const char *name;

    /* Physics tick rate in Hz. Upstream engines use 10000. Higher is more
     * stable and more expensive; independent of the audio sample rate. */
    uint32_t simulation_frequency;

    /* -- rotating assembly -- */
    double bore;
    double stroke;
    double crank_mass;
    double crank_moment_of_inertia;
    double crank_friction_torque;
    double flywheel_mass;
    double flywheel_radius;

    double rod_mass;
    double rod_length;
    double rod_moment_of_inertia;
    double rod_center_of_mass;   /* from the big end */

    double piston_mass;
    double piston_compression_height;

    /* Crank pin angles. Cylinders reference these by index. */
    const double *rod_journal_angles;
    uint32_t rod_journal_count;

    /* -- banks and exhaust -- */
    const es_bank_def *banks;
    uint32_t bank_count;
    const es_exhaust_def *exhaust_systems;
    uint32_t exhaust_system_count;

    /* -- intake, shared by every cylinder -- */
    double intake_plenum_volume;
    double intake_plenum_cross_section_area;
    double intake_runner_length;
    double intake_runner_flow_rate;              /* es_flow_from_carb_cfm */
    double intake_flow_rate;                     /* es_flow_from_carb_cfm */
    double intake_idle_flow_rate;                /* es_flow_from_carb_cfm */
    double intake_idle_throttle_plate_position;  /* 0..1 */
    double intake_velocity_decay;
    double throttle_gamma;                       /* pedal response; 0 -> 1.0 */

    /* -- ignition --
     * `firing_order` lists cylinder indices into the flattened cylinder list
     * (bank 0's cylinders first, then bank 1's, ...), in the order they fire,
     * with exactly as many entries as there are cylinders. It determines both
     * spark timing and cam lobe placement, so it is the only place firing
     * geometry is written down. */
    const uint32_t *firing_order;
    /* Ignition advance (rad before TDC) against engine speed (RPM). */
    es_curve timing_curve;
    double rev_limit;           /* RPM */
    double rev_limit_duration;  /* s of spark cut; 0 selects 0.5 */
    double redline;             /* RPM, reported in telemetry */

    /* -- starter -- */
    double starter_torque;
    double starter_speed;       /* RPM */

    /* -- audio character --
     * Upstream's synthesis knobs, preserved verbatim. Typical values are
     * hf_gain 0.01, jitter 0.6, noise 1.0. */
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
 * The pointers written into `out` reference static library data valid for the
 * life of the program; the caller may point them elsewhere but must not free
 * them. Adjust any field and pass the result to es_sim_create.
 */
ES_API es_result es_preset_engine(es_preset preset, es_engine_def *out);

/* Static, never-null display name for a preset, e.g. "Inline-4". */
ES_API const char *es_preset_name(es_preset preset);

/* ---------------------------------------------------------- simulation --- */

typedef struct es_sim es_sim;

typedef struct es_sim_config {
    /* Output sample rate in Hz. 0 selects 44100. */
    uint32_t sample_rate;

    /* Convolution impulse response giving the exhaust its character: mono
     * 16-bit PCM at `sample_rate`. NULL selects the built-in response.
     * Copied during es_sim_create. */
    const int16_t *impulse_response;
    uint32_t impulse_response_frames;
    double impulse_response_volume;   /* 0 selects 1.0 */
} es_sim_config;

/*
 * Creates a simulation. `config` may be NULL for defaults.
 *
 * Returns ES_ERR_INVALID_ARGUMENT if the definition is inconsistent - most
 * commonly a total cylinder count above ES_MAX_CYLINDERS, a firing order whose
 * length disagrees with that count, an index that is out of range, or an empty
 * curve.
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

/* Controls are clamped to their valid range and take effect on the next
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
    double torque;                  /* N*m at the crank */
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
