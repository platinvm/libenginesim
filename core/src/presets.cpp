/*
 * Built-in engine definitions, so any binding can make noise with no data
 * files. Both are transcribed from engine-sim's own definitions:
 *
 *   ES_PRESET_V8       assets/engines/atg-video-2/07_gm_ls.mr
 *   ES_PRESET_INLINE_4 assets/engines/atg-video-1/04_hayabusa.mr
 *
 * Everything below is static const, so the pointers handed out stay valid for
 * the life of the program and no preset can be corrupted by a caller.
 */
#include "enginesim.h"

#include "gas_system.h"
#include "units.h"
#include "constants.h"

namespace {

constexpr double deg(double d) { return d * constants::pi / 180.0; }
constexpr double inch(double v) { return units::distance(v, units::inch); }
constexpr double thou(double v) { return units::distance(v, units::thou); }
constexpr double cc(double v) { return units::volume(v, units::cc); }
constexpr double litres(double v) { return units::volume(v, units::L); }
constexpr double grams(double v) { return units::mass(v, units::g); }
constexpr double pounds(double v) { return units::mass(v, units::lb); }

constexpr double disk_moment(double mass, double radius) {
    return 0.5 * mass * radius * radius;
}
constexpr double rod_moment(double mass, double length) {
    return (1.0 / 12.0) * mass * length * length;
}

/* --------------------------------------------------------------- V8 ----- */
/* GM LS: 4.8 L cross-plane V8, firing order 1-8-7-2-6-5-4-3. */

const double v8_intake_flow_x[] = {
    thou(0), thou(50), thou(100), thou(150), thou(200),
    thou(250), thou(300), thou(350), thou(400), thou(450)
};
const double v8_intake_flow_y[] = { 0, 1, 103, 156, 214, 249, 268, 280, 280, 281 };

const double v8_exhaust_flow_x[] = {
    thou(0), thou(50), thou(100), thou(150), thou(200),
    thou(250), thou(300), thou(350), thou(400), thou(450)
};
const double v8_exhaust_flow_y[] = { 0, 1, 72, 113, 160, 196, 222, 235, 245, 246 };

const double v8_timing_x[] = { 0, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000 };
const double v8_timing_y[] = {
    deg(12), deg(12), deg(20), deg(30), deg(40), deg(40), deg(40), deg(40), deg(40)
};

/* Crank pins, in the order the vee's cylinder pairs sit along the shaft. */
const double v8_rod_journals[] = { deg(0), deg(270), deg(90), deg(180) };

/*
 * The two banks vent through separate paths of markedly different length.
 * That asymmetry is most of why a cross-plane V8 burbles rather than howls.
 */
const es_exhaust_def v8_exhausts[] = {
    { 0, inch(29.0), 0, 1.0, 0, 0, 4.0, inch(100.0) },
    { 0, inch(29.0), 0, 1.0, 0, 0, 4.0, inch(172.0) }
};

const double v8_spacing = inch(2.0);
const double cm = units::distance(1.0, units::cm);

/* { rod_journal, exhaust_system, sound_attenuation, primary_length, blowby } */
const es_cylinder_def v8_bank0[] = {
    { 0, 0, 1.0, 3 * v8_spacing + 2 * cm, 0.0 },
    { 1, 0, 1.0, 2 * v8_spacing + 1 * cm, 0.0 },
    { 2, 0, 1.0, 1 * v8_spacing + 3 * cm, 0.0 },
    { 3, 0, 1.0, 0 * v8_spacing + 5 * cm, 0.0 }
};
const es_cylinder_def v8_bank1[] = {
    { 0, 1, 1.0, 3 * v8_spacing + 1 * cm, 0.0 },
    { 1, 1, 1.0, 2 * v8_spacing + 5 * cm, 0.0 },
    { 2, 1, 1.0, 1 * v8_spacing + 7 * cm, 0.0 },
    { 3, 1, 1.0, 0 * v8_spacing + 0 * cm, 0.0 }
};

es_bank_def make_v8_bank(const es_cylinder_def *cylinders, double angle) {
    es_bank_def b = {};
    b.angle = angle;
    b.cylinders = cylinders;
    b.cylinder_count = 4;
    b.chamber_volume = cc(90.0);
    b.intake_runner_volume = cc(149.6);
    b.intake_runner_cross_section_area = inch(2.2) * inch(2.2);
    b.exhaust_runner_volume = cc(50.0);
    b.exhaust_runner_cross_section_area = inch(1.75) * inch(1.75);
    b.intake_flow = { v8_intake_flow_x, v8_intake_flow_y, 10, thou(50) };
    b.exhaust_flow = { v8_exhaust_flow_x, v8_exhaust_flow_y, 10, thou(50) };
    b.intake_lobe = { deg(234), 1.1, thou(551), 256 };
    b.exhaust_lobe = { deg(235), 1.1, thou(551), 256 };
    b.intake_lobe_center = deg(116);
    b.exhaust_lobe_center = deg(116);
    b.cam_advance = 0.0;
    b.cam_base_radius = inch(1.0);
    return b;
}

/* Flattened cylinder indices: 0-3 are bank 0, 4-7 are bank 1.
 * Upstream's 1-8-7-2-6-5-4-3 in one-based cylinder numbers, where odd numbers
 * are bank 0 and even are bank 1, maps to this. */
const uint32_t v8_firing_order[] = { 0, 7, 3, 4, 6, 2, 5, 1 };

/* ---------------------------------------------------------- inline-4 ---- */
/* Suzuki Hayabusa: 1.3 L 16-valve inline four, firing order 1-2-4-3. */

const double i4_intake_flow_x[] = {
    thou(0), thou(100), thou(200), thou(300), thou(400), thou(500), thou(600)
};
const double i4_intake_flow_y[] = { 0, 69, 90, 106, 116, 118, 118 };

const double i4_exhaust_flow_x[] = {
    thou(0), thou(100), thou(200), thou(300), thou(400), thou(500), thou(600)
};
const double i4_exhaust_flow_y[] = { 0, 46, 65, 79, 87, 91, 92 };

const double i4_timing_x[] = { 0, 1000, 2000, 3000, 4000, 5000, 6000, 8000, 10000 };
const double i4_timing_y[] = {
    deg(12), deg(12), deg(20), deg(30), deg(35), deg(38), deg(40), deg(40), deg(40)
};

const double i4_rod_journals[] = { deg(0), deg(180), deg(180), deg(0) };

/*
 * audio_volume is 64x upstream's 0.25 and 0.5, keeping its 1:2 ratio.
 *
 * Upstream pairs this engine with a different impulse response from the V8's,
 * and each response carries its own energy, so upstream's mix levels are only
 * meaningful next to the response they were set against. This library ships
 * one built-in response for every engine, which leaves the levels to be
 * renormalised against it. Upstream's literal values here put idle around
 * 1/70th of the V8's, far below the leveling filter's target - and that filter
 * only ever attenuates, so nothing downstream brings it back up. Measured
 * against the shipped response, 64x puts idle at 0.31 RMS against the V8's
 * 0.34, still short of clipping. See "Known gaps" in the README.
 */
const es_exhaust_def i4_exhausts[] = {
    { 0, inch(40.0), 0, 1.0, litres(10.0), 0, 16.0, inch(90.0) },
    { 0, inch(40.0), 0, 1.0, litres(10.0), 0, 32.0, inch(120.0) }
};

/* Cylinders 1 and 3 share one collector, 2 and 4 the other. Blowby is filled
 * in at first use: k_28inH2O is not a constant-expression. */
const double i4_blowby_cfm[] = { 0.001, 0.002, 0.001, 0.002 };
const es_cylinder_def i4_cylinders[] = {
    { 0, 0, 0.9, inch(10.0), 0.0 },
    { 1, 1, 0.8, inch(10.0), 0.0 },
    { 2, 0, 1.1, inch(10.0), 0.0 },
    { 3, 1, 0.9, inch(10.0), 0.0 }
};

const uint32_t i4_firing_order[] = { 0, 1, 3, 2 };

/* ------------------------------------------------------------ storage --- */

const es_bank_def v8_banks[] = {
    make_v8_bank(v8_bank0, -deg(45)),
    make_v8_bank(v8_bank1, deg(45))
};

es_bank_def make_i4_bank() {
    es_bank_def b = {};
    b.angle = 0.0;
    b.cylinders = i4_cylinders;
    b.cylinder_count = 4;
    b.chamber_volume = cc(32.0);
    b.intake_runner_volume = cc(100.0);
    b.intake_runner_cross_section_area = inch(1.4) * inch(1.4);
    b.exhaust_runner_volume = cc(30.0);
    b.exhaust_runner_cross_section_area = inch(1.1) * inch(1.1);
    b.intake_flow = { i4_intake_flow_x, i4_intake_flow_y, 7, thou(100) };
    b.exhaust_flow = { i4_exhaust_flow_x, i4_exhaust_flow_y, 7, thou(100) };
    b.intake_lobe = { deg(220), 1.1, thou(390), 256 };
    b.exhaust_lobe = { deg(215), 1.1, thou(360), 256 };
    b.intake_lobe_center = deg(105);
    b.exhaust_lobe_center = deg(110);
    b.cam_advance = 0.0;
    b.cam_base_radius = inch(0.75);
    return b;
}

const es_bank_def i4_banks[] = { make_i4_bank() };

es_engine_def make_v8() {
    const double stroke = inch(3.622);
    const double rodLength = units::distance(160.0, units::mm);
    const double rodMass = grams(50.0);
    const double crankMass = pounds(60.0);
    const double flywheelMass = pounds(30.0);
    const double flywheelRadius = inch(8.0);

    es_engine_def d = {};
    d.name = "GM LS V8";
    /* Upstream's 10000 runs at 0.91x real time in an audio callback. */
    d.simulation_frequency = 4000;
    d.bore = inch(3.78);
    d.stroke = stroke;
    d.crank_mass = crankMass;
    d.crank_moment_of_inertia =
        1.5 * disk_moment(crankMass, stroke) +
        disk_moment(flywheelMass, flywheelRadius) +
        disk_moment(1.0, units::distance(1.0, units::cm));
    d.crank_friction_torque = units::torque(20.0, units::ft_lb);
    d.flywheel_mass = flywheelMass;
    d.flywheel_radius = flywheelRadius;
    d.rod_mass = rodMass;
    d.rod_length = rodLength;
    d.rod_moment_of_inertia = rod_moment(rodMass, rodLength);
    d.rod_center_of_mass = 0.0;
    d.piston_mass = grams(100.0);
    d.piston_compression_height = inch(1.0);
    d.rod_journal_angles = v8_rod_journals;
    d.rod_journal_count = 4;
    d.banks = v8_banks;
    d.bank_count = 2;
    d.exhaust_systems = v8_exhausts;
    d.exhaust_system_count = 2;
    d.intake_plenum_volume = litres(1.325);
    d.intake_plenum_cross_section_area = units::area(20.0, units::cm2);
    d.intake_runner_length = inch(12.0);
    d.intake_idle_throttle_plate_position = 0.996;
    d.intake_velocity_decay = 0.5;
    d.throttle_gamma = 2.0;
    d.firing_order = v8_firing_order;
    d.timing_curve = { v8_timing_x, v8_timing_y, 9, 1000.0 };
    d.rev_limit = 6800.0;
    d.rev_limit_duration = 0.2;
    d.redline = 6500.0;
    d.starter_torque = units::torque(200.0, units::ft_lb);
    d.starter_speed = 200.0;
    d.hf_gain = 0.01;
    d.jitter = 0.6;
    d.noise = 1.0;
    return d;
}

es_engine_def make_i4() {
    const double stroke = units::distance(65.0, units::mm);
    const double rodLength = inch(4.705);
    const double rodMass = grams(395.837);
    const double crankMass = pounds(24.8);
    const double flywheelMass = pounds(10.0);
    const double flywheelRadius = inch(4.0);

    es_engine_def d = {};
    d.name = "Hayabusa Inline-4";
    /* Upstream runs this one at 20 kHz - it revs to 11000 with only four
     * cylinders, so the pulses are short and closely spaced. That costs more
     * than an audio callback can afford: measured at 0.72x real time, which
     * glitches continuously. 8 kHz keeps it comfortably ahead. See "Known
     * gaps" in the README. */
    d.simulation_frequency = 6000;
    d.bore = units::distance(81.0, units::mm);
    d.stroke = stroke;
    d.crank_mass = crankMass;
    /* Upstream states this outright rather than deriving it from disks, and
     * the value is far lower than the disk approximation gives. */
    d.crank_moment_of_inertia = 0.22986844776863666 * 0.2;
    d.crank_friction_torque = units::torque(1.0, units::ft_lb);
    d.flywheel_mass = flywheelMass;
    d.flywheel_radius = flywheelRadius;
    d.rod_mass = rodMass;
    d.rod_length = rodLength;
    d.rod_moment_of_inertia = 0.0015884918028487504;
    d.rod_center_of_mass = 0.0;
    d.piston_mass = grams(303.5);
    d.piston_compression_height = inch(1.0);
    d.rod_journal_angles = i4_rod_journals;
    d.rod_journal_count = 4;
    d.banks = i4_banks;
    d.bank_count = 1;
    d.exhaust_systems = i4_exhausts;
    d.exhaust_system_count = 2;
    d.intake_plenum_volume = litres(4.5);
    d.intake_plenum_cross_section_area = units::area(10.0, units::cm2);
    d.intake_runner_length = inch(10.0);
    d.intake_idle_throttle_plate_position = 0.999;
    d.intake_velocity_decay = 0.5;
    d.throttle_gamma = 2.0;
    d.firing_order = i4_firing_order;
    d.timing_curve = { i4_timing_x, i4_timing_y, 9, 1000.0 };
    d.rev_limit = 11300.0;
    d.rev_limit_duration = 0.1;
    d.redline = 11000.0;
    d.starter_torque = units::torque(70.0, units::ft_lb);
    d.starter_speed = 500.0;
    /* This engine's own synthesis character. The V8's numbers were sitting
     * here, and they are nothing like it. */
    d.hf_gain = 0.00407;
    d.jitter = 0.062;
    d.noise = 0.292;
    return d;
}

} /* namespace */

/* Upstream's add_flow_sample is add_sample(lift * thou, k_28inH2O(flow)), so
 * the CFM figures above have to go through the same conversion. */
template <int N>
static const double *converted_flow(const double (&cfm)[N], double (&out)[N]) {
    for (int i = 0; i < N; ++i) out[i] = GasSystem::k_28inH2O(cfm[i]);
    return out;
}

extern "C" es_result es_preset_engine(es_preset preset, es_engine_def *out) {
    if (out == nullptr) return ES_ERR_INVALID_ARGUMENT;

    /* Flow constants are not constant-expressions, so they are filled in on
     * first use rather than at static-initialisation time. */
    static es_engine_def v8 = {};
    static es_engine_def i4 = {};
    static es_bank_def v8b[2];
    static es_bank_def i4b[1];
    static es_cylinder_def i4c[4];
    static double v8_in[10], v8_ex[10], i4_in[7], i4_ex[7];
    static bool ready = false;
    if (!ready) {
        static es_exhaust_def v8x[2];
        static es_exhaust_def i4x[2];
        for (int i = 0; i < 2; ++i) {
            v8x[i] = v8_exhausts[i];
            v8x[i].outlet_flow_rate = GasSystem::k_carb(1000.0);
            v8x[i].primary_flow_rate = GasSystem::k_carb(500.0);
            i4x[i] = i4_exhausts[i];
            i4x[i].outlet_flow_rate = GasSystem::k_carb(1000.0);
            i4x[i].primary_flow_rate = GasSystem::k_carb(500.0);
        }

        v8 = make_v8();
        v8.exhaust_systems = v8x;
        for (int i = 0; i < 2; ++i) {
            v8b[i] = v8_banks[i];
            v8b[i].intake_flow.y = converted_flow(v8_intake_flow_y, v8_in);
            v8b[i].exhaust_flow.y = converted_flow(v8_exhaust_flow_y, v8_ex);
        }
        v8.banks = v8b;
        v8.intake_flow_rate = GasSystem::k_carb(700.0);
        v8.intake_runner_flow_rate = GasSystem::k_carb(100.0);
        v8.intake_idle_flow_rate = GasSystem::k_carb(0.0);

        i4 = make_i4();
        i4.exhaust_systems = i4x;
        i4b[0] = i4_banks[0];
        i4b[0].intake_flow.y = converted_flow(i4_intake_flow_y, i4_in);
        i4b[0].exhaust_flow.y = converted_flow(i4_exhaust_flow_y, i4_ex);
        for (int i = 0; i < 4; ++i) {
            i4c[i] = i4_cylinders[i];
            i4c[i].blowby = GasSystem::k_28inH2O(i4_blowby_cfm[i]);
        }
        i4b[0].cylinders = i4c;
        i4.banks = i4b;
        i4.intake_flow_rate = GasSystem::k_carb(800.0);
        i4.intake_runner_flow_rate = GasSystem::k_carb(300.0);
        i4.intake_idle_flow_rate = GasSystem::k_carb(0.0);

        ready = true;
    }

    switch (preset) {
        case ES_PRESET_INLINE_4: *out = i4; return ES_OK;
        case ES_PRESET_V8:       *out = v8; return ES_OK;
        default:                 return ES_ERR_INVALID_ARGUMENT;
    }
}

extern "C" const char *es_preset_name(es_preset preset) {
    switch (preset) {
        case ES_PRESET_INLINE_4: return "Inline-4";
        case ES_PRESET_V8: return "V8";
        default: return "unknown";
    }
}
