/*
 * Built-in engine definitions, so any binding can make noise with no data
 * files. All are transcribed from engine-sim's own definitions:
 *
 *   ES_PRESET_V8       assets/engines/atg-video-2/07_gm_ls.mr
 *   ES_PRESET_INLINE_4 assets/engines/atg-video-1/04_hayabusa.mr
 *   ES_PRESET_SINGLE   assets/engines/atg-video-1/01_honda_trx520.mr
 *   ES_PRESET_V_TWIN   assets/engines/atg-video-1/03_harley_davidson_shovelhead.mr
 *   ES_PRESET_FLAT_4   assets/engines/atg-video-1/06_subaru_ej25.mr
 *   ES_PRESET_INLINE_5 assets/engines/atg-video-1/07_audi_i5.mr
 *   ES_PRESET_INLINE_6 assets/engines/atg-video-2/03_2jz.mr
 *   ES_PRESET_V6       assets/engines/atg-video-2/06_even_fire_v6.mr
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
constexpr double mm(double v) { return units::distance(v, units::mm); }
constexpr double cc(double v) { return units::volume(v, units::cc); }
constexpr double litres(double v) { return units::volume(v, units::L); }
constexpr double grams(double v) { return units::mass(v, units::g); }
constexpr double pounds(double v) { return units::mass(v, units::lb); }
constexpr double kg(double v) { return units::mass(v, units::kg); }
constexpr double cm2(double v) { return units::area(v, units::cm2); }

constexpr double disk_moment(double mass, double radius) {
    return 0.5 * mass * radius * radius;
}
constexpr double rod_moment(double mass, double length) {
    return (1.0 / 12.0) * mass * length * length;
}

/*
 * Upstream's exhaust_system_parameters defaults collector_cross_section_area
 * to a 2" circle and derives length as volume / that area whenever a script
 * doesn't override length outright. Several engines below rely on that
 * default rather than stating a length explicitly, so it is reproduced here
 * rather than falling back to this library's own unrelated 100" default.
 */
constexpr double default_exhaust_length(double volume) {
    return volume / (constants::pi * inch(2.0) * inch(2.0));
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

/* ------------------------------------------------------------ single --- */
/* Honda TRX520: 0.52 L ATV single. Shares its head's base flow curve with the
 * V-twin below (upstream's generic_small_engine_head), scaled 2x. */

const double single_intake_flow_x[] = {
    thou(0), thou(50), thou(100), thou(150), thou(200), thou(250), thou(300),
    thou(350), thou(400), thou(450), thou(500), thou(550), thou(600), thou(650), thou(700)
};
const double single_intake_flow_y[] = {
    0, 50, 150, 200, 260, 360, 380, 440, 480, 500, 520, 520, 520, 510, 500
};
const double single_exhaust_flow_x[] = {
    thou(0), thou(50), thou(100), thou(150), thou(200), thou(250), thou(300),
    thou(350), thou(400), thou(450), thou(500), thou(550), thou(600), thou(650), thou(700)
};
const double single_exhaust_flow_y[] = {
    0, 50, 100, 150, 200, 250, 320, 350, 360, 380, 400, 410, 420, 420, 420
};

const double single_timing_x[] = { 0, 1000, 2000, 3000, 4000 };
const double single_timing_y[] = { deg(12), deg(12), deg(20), deg(35), deg(35) };

const double single_rod_journals[] = { 0.0 };

const es_exhaust_def single_exhausts[] = {
    { 0, inch(20.0), 0, 0.5, litres(5.0), 0, 1.0, default_exhaust_length(litres(5.0)) }
};

/* Blowby is filled in at first use: k_28inH2O is not a constant-expression. */
const double single_blowby_cfm[] = { 0.1 };
const es_cylinder_def single_cylinders[] = {
    { 0, 0, 0.0, 0.0, 0.0 }
};

const uint32_t single_firing_order[] = { 0 };

/* ----------------------------------------------------------- v-twin ---- */
/* Harley Davidson Shovelhead: 1.34 L 45 degree V-twin, one shared crank pin.
 *
 * Upstream fires its second cylinder 315 degrees after the first (405 the
 * other way around), not the even 360/360 a two-cylinder engine gets from
 * this library's firing model (every cylinder's spark and cam offset is
 * spaced evenly by firing position - see build_engine() in
 * engine_builder.cpp). That uneven interval is what gives a real Shovelhead
 * its "potato-potato" idle; reproducing it exactly would need a further ABI
 * addition (an explicit per-cylinder firing angle) beyond the crank_tdc fix
 * this batch of engines already required. This preset runs as an even-fire
 * V-twin instead - stable and correctly timed to compression, just not
 * rhythmically authentic. See "Known gaps" in the README.
 */

const double vtwin_intake_flow_x[] = {
    thou(0), thou(50), thou(100), thou(150), thou(200), thou(250), thou(300),
    thou(350), thou(400), thou(450), thou(500), thou(550), thou(600), thou(650), thou(700)
};
const double vtwin_exhaust_flow_x[] = {
    thou(0), thou(50), thou(100), thou(150), thou(200), thou(250), thou(300),
    thou(350), thou(400), thou(450), thou(500), thou(550), thou(600), thou(650), thou(700)
};
/* flow_attenuation is 2.0 on the front cylinder's head and 1.0 on the rear's,
 * matching upstream's asymmetric ports exactly. */
const double vtwin_intake_flow_y0[] = {
    0, 50, 150, 200, 260, 360, 380, 440, 480, 500, 520, 520, 520, 510, 500
};
const double vtwin_intake_flow_y1[] = {
    0, 25, 75, 100, 130, 180, 190, 220, 240, 250, 260, 260, 260, 255, 250
};
const double vtwin_exhaust_flow_y0[] = {
    0, 50, 100, 150, 200, 250, 320, 350, 360, 380, 400, 410, 420, 420, 420
};
const double vtwin_exhaust_flow_y1[] = {
    0, 25, 50, 75, 100, 125, 160, 175, 180, 190, 200, 205, 210, 210, 210
};

const double vtwin_timing_x[] = { 0, 1000, 2000, 3000, 4000 };
const double vtwin_timing_y[] = { deg(18), deg(18), deg(30), deg(40), deg(40) };

/* Both cylinders share the one crank pin. */
const double vtwin_rod_journals[] = { 0.0 };

const es_exhaust_def vtwin_exhausts[] = {
    { 0, inch(70.0), 0, 0.75, litres(10.0), 0, 1.0 * 0.1, default_exhaust_length(litres(10.0)) },
    { 0, inch(70.0), 0, 0.75, litres(10.0), 0, 2.0 * 0.1, default_exhaust_length(litres(10.0)) }
};

const double vtwin_blowby_cfm[] = { 0.2, 0.1 };
const es_cylinder_def vtwin_bank0[] = { { 0, 0, 0.0, 0.0, 0.0 } };
const es_cylinder_def vtwin_bank1[] = { { 0, 1, 0.0, 0.0, 0.0 } };

const uint32_t vtwin_firing_order[] = { 0, 1 };

/* ----------------------------------------------------------- flat-4 ---- */
/* Subaru EJ25: 2.5 L flat-4 boxer, opposed 90 degree banks. */

const double flat4_intake_flow_x[] = {
    thou(0), thou(50), thou(100), thou(150), thou(200),
    thou(250), thou(300), thou(350), thou(400), thou(450)
};
const double flat4_intake_flow_y[] = { 0, 58, 103, 156, 214, 249, 268, 280, 280, 281 };

const double flat4_exhaust_flow_x[] = {
    thou(0), thou(50), thou(100), thou(150), thou(200),
    thou(250), thou(300), thou(350), thou(400), thou(450)
};
const double flat4_exhaust_flow_y[] = { 0, 37, 72, 113, 160, 196, 222, 235, 245, 246 };

const double flat4_timing_x[] = { 0, 1000, 2000, 3000, 4000 };
const double flat4_timing_y[] = { deg(25), deg(25), deg(30), deg(40), deg(40) };

/* rj0..rj3, in the order upstream declares them. */
const double flat4_rod_journals[] = { deg(0), deg(180), deg(0), deg(180) };

/* audio_volume is upstream's 0.5/1.0 ratio times 16, not its own 8: measured
 * against the shipped impulse response, 8x idled at 0.16 RMS, under half the
 * V8's 0.34. See "Known gaps" in the README. */
const es_exhaust_def flat4_exhausts[] = {
    { 0, inch(10.0), 0, 1.0, litres(100.0), 0, 0.5 * 16.0, default_exhaust_length(litres(100.0)) },
    { 0, inch(10.0), 0, 1.0, litres(100.0), 0, 1.0 * 16.0, default_exhaust_length(litres(100.0)) }
};

/* Bank 0 is rj0 (wire1) then rj3 (wire3); bank 1 is rj1 (wire2) then rj2
 * (wire4) - upstream's flattened firing order comes out simply 0,1,2,3. */
const double flat4_blowby_cfm[] = { 0.001, 0.002, 0.001, 0.002 };
const es_cylinder_def flat4_bank0[] = {
    { 0, 0, 0.0, 0.0, 0.0 },
    { 3, 0, 0.0, 0.0, 0.0 }
};
const es_cylinder_def flat4_bank1[] = {
    { 1, 1, 0.0, 0.0, 0.0 },
    { 2, 1, 0.0, 0.0, 0.0 }
};

const uint32_t flat4_firing_order[] = { 0, 1, 2, 3 };

/* ---------------------------------------------------------- inline-5 --- */
/* Audi 2.3 I5: 5-cylinder inline, one crank pin per 72 degrees. */

const double inline5_intake_flow_x[] = {
    thou(0), thou(50), thou(100), thou(150), thou(200),
    thou(250), thou(300), thou(350), thou(400), thou(450)
};
const double inline5_intake_flow_y[] = {
    0, 52.2, 92.7, 140.4, 192.6, 224.1, 241.2, 252, 252, 252.9
};

const double inline5_exhaust_flow_x[] = {
    thou(0), thou(50), thou(100), thou(150), thou(200),
    thou(250), thou(300), thou(350), thou(400), thou(450)
};
const double inline5_exhaust_flow_y[] = {
    0, 33.3, 64.8, 101.7, 144, 176.4, 199.8, 211.5, 220.5, 221.4
};

const double inline5_timing_x[] = {
    0, 1000, 2000, 3000, 4000, 5000, 6000, 7000
};
const double inline5_timing_y[] = {
    deg(12), deg(12), deg(20), deg(26), deg(30), deg(34), deg(38), deg(38)
};

const double inline5_rod_journals[] = {
    deg(0), deg(144), deg(216), deg(288), deg(72)
};

/* audio_volume is upstream's 1.0/0.8 ratio times 7, not its own 1: measured
 * against the shipped impulse response, upstream's literal values idled at
 * 0.04 RMS, about 8x quieter than the V8's 0.34. See "Known gaps" in the
 * README. */
const es_exhaust_def inline5_exhausts[] = {
    { 0, inch(10.0), 0, 1.0, litres(100.0), 0, 1.0 * 7.0, default_exhaust_length(litres(100.0)) },
    { 0, inch(10.0), 0, 1.0, litres(100.0), 0, 0.8 * 7.0, default_exhaust_length(litres(100.0)) }
};

/* Cylinder index == rod journal index; wires 1..5 fire 0,1,3,4,2. */
const double inline5_blowby_cfm[] = { 0.2, 0.6, 0.6, 0.4, 0.4 };
const es_cylinder_def inline5_cylinders[] = {
    { 0, 0, 0.9, 0.0, 0.0 },
    { 1, 1, 0.8, 0.0, 0.0 },
    { 2, 0, 0.9, 0.0, 0.0 },
    { 3, 1, 1.0, 0.0, 0.0 },
    { 4, 0, 1.1, 0.0, 0.0 }
};

const uint32_t inline5_firing_order[] = { 0, 1, 3, 4, 2 };

/* ---------------------------------------------------------- inline-6 --- */
/* Toyota 2JZ: 3.0 L inline-6, one crank pin per 120 (nominal) degrees. */

const double inline6_intake_flow_x[] = {
    thou(0), thou(50), thou(100), thou(150), thou(200),
    thou(250), thou(300), thou(350), thou(400), thou(450)
};
const double inline6_intake_flow_y[] = {
    0, 52.2, 92.7, 140.4, 192.6, 224.1, 241.2, 252, 252, 252.9
};

const double inline6_exhaust_flow_x[] = {
    thou(0), thou(50), thou(100), thou(150), thou(200),
    thou(250), thou(300), thou(350), thou(400), thou(450)
};
const double inline6_exhaust_flow_y[] = {
    0, 33.3, 64.8, 101.7, 144, 176.4, 199.8, 211.5, 220.5, 221.4
};

const double inline6_timing_x[] = {
    0, 1000, 2000, 3000, 4000, 5000, 6000, 7000
};
const double inline6_timing_y[] = {
    deg(12), deg(12), deg(20), deg(26), deg(30), deg(34), deg(38), deg(38)
};

/* Upstream states these past a full turn (480, 600 degrees); left as given
 * since sin/cos are periodic and it is the literal upstream value. */
const double inline6_rod_journals[] = {
    deg(0), deg(480), deg(240), deg(600), deg(120), deg(360)
};

/* audio_volume is upstream's 0.2 times 28: measured against the shipped
 * impulse response, upstream's literal value idled at 0.01 RMS, about 32x
 * quieter than the V8's 0.34. See "Known gaps" in the README. */
const es_exhaust_def inline6_exhausts[] = {
    { 0, inch(40.0), 0, 1.0, 0, 0, 0.2 * 28.0, inch(100.0) },
    { 0, inch(40.0), 0, 1.0, 0, 0, 0.2 * 28.0, inch(100.0) }
};

const double inline6_spacing = inch(0.5);
const double inline6_blowby_cfm[] = { 0.1, 0.05, 0.1, 0.05, 0.1, 0.05 };
const es_cylinder_def inline6_cylinders[] = {
    { 0, 0, 0.9,  5 * inline6_spacing, 0.0 },
    { 1, 0, 0.95, 4 * inline6_spacing, 0.0 },
    { 2, 0, 0.9,  3 * inline6_spacing, 0.0 },
    { 3, 1, 0.97, 3 * inline6_spacing, 0.0 },
    { 4, 1, 0.98, 4 * inline6_spacing, 0.0 },
    { 5, 1, 0.93, 5 * inline6_spacing, 0.0 }
};

const uint32_t inline6_firing_order[] = { 0, 4, 2, 5, 1, 3 };

/* --------------------------------------------------------------- V6 ---- */
/* Generic even-fire 90 degree V6: split rod journals (30 degrees apart per
 * pair) trade a shared-pin V6's uneven fire for a perfectly even one. */

const double v6_intake_flow_x[] = {
    thou(0), thou(50), thou(100), thou(150), thou(200),
    thou(250), thou(300), thou(350), thou(400), thou(450)
};
const double v6_intake_flow_y[] = { 0, 58, 103, 156, 214, 249, 268, 280, 280, 281 };

const double v6_exhaust_flow_x[] = {
    thou(0), thou(50), thou(100), thou(150), thou(200),
    thou(250), thou(300), thou(350), thou(400), thou(450)
};
const double v6_exhaust_flow_y[] = { 0, 37, 72, 113, 160, 196, 222, 235, 245, 246 };

const double v6_timing_x[] = { 0, 1000, 2000, 3000, 4000 };
const double v6_timing_y[] = { deg(12), deg(20), deg(25), deg(30), deg(30) };

/* rj0..rj5, in the order upstream declares them: 0, -30, 120, 90, 240, 210. */
const double v6_rod_journals[] = {
    deg(0), deg(-30), deg(120), deg(90), deg(240), deg(210)
};

const double v6_spacing = inch(5.0);

/* audio_volume is upstream's 1.0 times 5: measured against the shipped
 * impulse response, upstream's literal value idled at 0.06 RMS, about 6x
 * quieter than the V8's 0.34. See "Known gaps" in the README. */
const es_exhaust_def v6_exhausts[] = {
    { 0, inch(20.0), 0, 1.0, 0, 0, 1.0 * 5.0, inch(100.0) },
    { 0, inch(20.0), 0, 1.0, 0, 0, 1.0 * 5.0, inch(172.0) }
};

/* Bank 0 is rj0/rj2/rj4 (wires 1,3,5); bank 1 is rj1/rj3/rj5 (wires 2,4,6). */
const double v6_blowby_cfm[] = { 0.1, 0.2, 0.2, 0.1, 0.2, 0.2 };
const es_cylinder_def v6_bank0[] = {
    { 0, 0, 0.8, 2 * v6_spacing, 0.0 },
    { 2, 0, 0.9, 1 * v6_spacing, 0.0 },
    { 4, 0, 0.0, 0 * v6_spacing, 0.0 }
};
const es_cylinder_def v6_bank1[] = {
    { 1, 1, 0.6, 2 * v6_spacing, 0.0 },
    { 3, 1, 0.3, 1 * v6_spacing, 0.0 },
    { 5, 1, 1.1, 0 * v6_spacing, 0.0 }
};

/* Wires fire 1,6,5,4,3,2 -> flattened cylinders 0,5,2,4,1,3. */
const uint32_t v6_firing_order[] = { 0, 5, 2, 4, 1, 3 };

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

es_bank_def make_single_bank() {
    es_bank_def b = {};
    b.angle = 0.0;
    b.cylinders = single_cylinders;
    b.cylinder_count = 1;
    b.chamber_volume = cc(60.0);
    b.intake_runner_volume = cc(100.0);
    b.intake_runner_cross_section_area = cm2(9.0);
    b.exhaust_runner_volume = cc(100.0);
    b.exhaust_runner_cross_section_area = cm2(9.0);
    b.intake_flow = { single_intake_flow_x, single_intake_flow_y, 15, thou(50) };
    b.exhaust_flow = { single_exhaust_flow_x, single_exhaust_flow_y, 15, thou(50) };
    b.intake_lobe = { deg(180), 1.0, thou(200), 100 };
    b.exhaust_lobe = { deg(180), 1.0, thou(200), 100 };
    b.intake_lobe_center = deg(100);
    b.exhaust_lobe_center = deg(100);
    b.cam_advance = 0.0;
    b.cam_base_radius = thou(500);
    return b;
}

const es_bank_def single_banks[] = { make_single_bank() };

es_bank_def make_vtwin_bank(const es_cylinder_def *cylinders, double angle,
                             const double *intake_y, const double *exhaust_y) {
    es_bank_def b = {};
    b.angle = angle;
    b.cylinders = cylinders;
    b.cylinder_count = 1;
    b.chamber_volume = cc(100.0);
    b.intake_runner_volume = cc(100.0);
    b.intake_runner_cross_section_area = cm2(20.0);
    b.exhaust_runner_volume = cc(100.0);
    b.exhaust_runner_cross_section_area = cm2(20.0);
    b.intake_flow = { vtwin_intake_flow_x, intake_y, 15, thou(50) };
    b.exhaust_flow = { vtwin_exhaust_flow_x, exhaust_y, 15, thou(50) };
    b.intake_lobe = { deg(210), 0.9, thou(400), 100 };
    b.exhaust_lobe = { deg(210), 0.9, thou(400), 100 };
    b.intake_lobe_center = deg(110);
    b.exhaust_lobe_center = deg(110);
    b.cam_advance = 0.0;
    b.cam_base_radius = thou(500);
    return b;
}

const es_bank_def vtwin_banks[] = {
    make_vtwin_bank(vtwin_bank0, -deg(0.5 * 45), vtwin_intake_flow_y0, vtwin_exhaust_flow_y0),
    make_vtwin_bank(vtwin_bank1, deg(0.5 * 45), vtwin_intake_flow_y1, vtwin_exhaust_flow_y1)
};

es_bank_def make_flat4_bank(const es_cylinder_def *cylinders, double angle) {
    es_bank_def b = {};
    b.angle = angle;
    b.cylinders = cylinders;
    b.cylinder_count = 2;
    b.chamber_volume = cc(67.0);
    b.intake_runner_volume = cc(149.6);
    b.intake_runner_cross_section_area = inch(1.35) * inch(1.35);
    b.exhaust_runner_volume = cc(50.0);
    b.exhaust_runner_cross_section_area = inch(1.25) * inch(1.25);
    b.intake_flow = { flat4_intake_flow_x, flat4_intake_flow_y, 10, thou(50) };
    b.exhaust_flow = { flat4_exhaust_flow_x, flat4_exhaust_flow_y, 10, thou(50) };
    b.intake_lobe = { deg(232), 2.0, mm(9.78), 100 };
    b.exhaust_lobe = { deg(236), 2.0, mm(9.60), 100 };
    b.intake_lobe_center = deg(117);
    b.exhaust_lobe_center = deg(112);
    b.cam_advance = 0.0;
    b.cam_base_radius = mm(17.0);
    return b;
}

const es_bank_def flat4_banks[] = {
    make_flat4_bank(flat4_bank0, deg(90)),
    make_flat4_bank(flat4_bank1, -deg(90))
};

es_bank_def make_inline5_bank() {
    es_bank_def b = {};
    b.angle = 0.0;
    b.cylinders = inline5_cylinders;
    b.cylinder_count = 5;
    b.chamber_volume = cc(50.0);
    b.intake_runner_volume = cc(149.6);
    b.intake_runner_cross_section_area = inch(1.9) * inch(1.9);
    b.exhaust_runner_volume = cc(50.0);
    b.exhaust_runner_cross_section_area = inch(1.25) * inch(1.25);
    b.intake_flow = { inline5_intake_flow_x, inline5_intake_flow_y, 10, thou(50) };
    b.exhaust_flow = { inline5_exhaust_flow_x, inline5_exhaust_flow_y, 10, thou(50) };
    b.intake_lobe = { deg(210), 2.0, mm(9.78), 100 };
    b.exhaust_lobe = { deg(215), 2.0, mm(9.60), 100 };
    b.intake_lobe_center = deg(116);
    b.exhaust_lobe_center = deg(116);
    b.cam_advance = 0.0;
    b.cam_base_radius = mm(17.0);
    return b;
}

const es_bank_def inline5_banks[] = { make_inline5_bank() };

es_bank_def make_inline6_bank() {
    es_bank_def b = {};
    b.angle = 0.0;
    b.cylinders = inline6_cylinders;
    b.cylinder_count = 6;
    b.chamber_volume = cc(50.0);
    b.intake_runner_volume = cc(149.6);
    b.intake_runner_cross_section_area = inch(1.9) * inch(1.9);
    b.exhaust_runner_volume = cc(50.0);
    b.exhaust_runner_cross_section_area = inch(1.25) * inch(1.25);
    b.intake_flow = { inline6_intake_flow_x, inline6_intake_flow_y, 10, thou(50) };
    b.exhaust_flow = { inline6_exhaust_flow_x, inline6_exhaust_flow_y, 10, thou(50) };
    b.intake_lobe = { deg(220), 1.1, mm(9.78), 100 };
    b.exhaust_lobe = { deg(220), 1.1, mm(9.60), 100 };
    b.intake_lobe_center = deg(116);
    b.exhaust_lobe_center = deg(116);
    b.cam_advance = 0.0;
    b.cam_base_radius = mm(17.0);
    return b;
}

const es_bank_def inline6_banks[] = { make_inline6_bank() };

es_bank_def make_v6_bank(const es_cylinder_def *cylinders, double angle) {
    es_bank_def b = {};
    b.angle = angle;
    b.cylinders = cylinders;
    b.cylinder_count = 3;
    b.chamber_volume = cc(67.0);
    b.intake_runner_volume = cc(149.6);
    b.intake_runner_cross_section_area = inch(1.35) * inch(1.35);
    b.exhaust_runner_volume = cc(50.0);
    b.exhaust_runner_cross_section_area = inch(2.0) * inch(2.0);
    b.intake_flow = { v6_intake_flow_x, v6_intake_flow_y, 10, thou(50) };
    b.exhaust_flow = { v6_exhaust_flow_x, v6_exhaust_flow_y, 10, thou(50) };
    b.intake_lobe = { deg(222), 1.0, thou(400), 100 };
    b.exhaust_lobe = { deg(226), 1.0, thou(300), 100 };
    b.intake_lobe_center = deg(117);
    b.exhaust_lobe_center = deg(112);
    b.cam_advance = 0.0;
    b.cam_base_radius = inch(0.75);
    return b;
}

const es_bank_def v6_banks[] = {
    make_v6_bank(v6_bank0, -deg(45)),
    make_v6_bank(v6_bank1, deg(45))
};

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

es_engine_def make_single() {
    es_engine_def d = {};
    d.name = "Honda TRX520 Single";
    /* Upstream's own 40 kHz; reconsider once this preset is measured against
     * an audio-callback budget the way the V8/I4 already were. */
    d.simulation_frequency = 40000;
    d.bore = mm(96.0);
    d.stroke = mm(71.5);
    d.crank_mass = pounds(5.0);
    /* Upstream states this outright, not derived from disks - and it is the
     * same literal the Hayabusa inline-4 uses. */
    d.crank_moment_of_inertia = 0.22986844776863666 * 0.2;
    d.crank_friction_torque = units::torque(5.0, units::ft_lb);
    d.flywheel_mass = pounds(5.0);
    d.rod_mass = grams(100.0);
    d.rod_length = inch(6.0);
    d.rod_moment_of_inertia = 0.0015884918028487504;
    d.rod_center_of_mass = 0.0;
    d.piston_mass = grams(100.0);
    d.piston_compression_height = inch(1.0);
    d.rod_journal_angles = single_rod_journals;
    d.rod_journal_count = 1;
    d.banks = single_banks;
    d.bank_count = 1;
    d.exhaust_systems = single_exhausts;
    d.exhaust_system_count = 1;
    d.intake_plenum_volume = litres(1.5);
    d.intake_plenum_cross_section_area = cm2(10.0);
    d.intake_runner_length = inch(4.0);
    d.intake_idle_throttle_plate_position = 0.993;
    d.intake_velocity_decay = 0.5;
    d.throttle_gamma = 1.0;
    d.firing_order = single_firing_order;
    d.timing_curve = { single_timing_x, single_timing_y, 5, 1000.0 };
    d.rev_limit = 6000.0;
    d.rev_limit_duration = 0.0;
    d.redline = 5000.0;
    d.starter_torque = units::torque(50.0, units::ft_lb);
    d.starter_speed = 500.0;
    d.hf_gain = 0.00121;
    d.jitter = 0.42;
    d.noise = 0.229;
    return d;
}

es_engine_def make_vtwin() {
    const double stroke = inch(4.25);
    const double crankMass = kg(9.39);
    const double flywheelMass = kg(15.0);
    const double flywheelRadius = inch(6.0);

    es_engine_def d = {};
    d.name = "Harley Davidson Shovelhead V-Twin";
    /* Upstream's own 35 kHz; reconsider once measured. */
    d.simulation_frequency = 35000;
    d.bore = inch(3.5);
    d.stroke = stroke;
    d.crank_mass = crankMass;
    d.crank_moment_of_inertia =
        disk_moment(crankMass, stroke / 2) +
        disk_moment(flywheelMass, flywheelRadius) +
        disk_moment(kg(10.0), units::distance(3.0, units::cm));
    d.crank_friction_torque = units::torque(5.0, units::ft_lb);
    d.flywheel_mass = flywheelMass;
    d.rod_mass = grams(500.0);
    d.rod_length = inch(8.0);
    d.rod_moment_of_inertia = 0.0015884918028487504;
    d.rod_center_of_mass = 0.0;
    d.piston_mass = grams(500.0);
    d.piston_compression_height = inch(1.0);
    d.rod_journal_angles = vtwin_rod_journals;
    d.rod_journal_count = 1;
    d.banks = vtwin_banks;
    d.bank_count = 2;
    d.exhaust_systems = vtwin_exhausts;
    d.exhaust_system_count = 2;
    d.intake_plenum_volume = litres(1.5);
    d.intake_plenum_cross_section_area = cm2(10.0);
    d.intake_runner_length = inch(4.0);
    d.intake_idle_throttle_plate_position = 0.991;
    d.intake_velocity_decay = 1.0;
    d.throttle_gamma = 1.0;
    d.firing_order = vtwin_firing_order;
    d.timing_curve = { vtwin_timing_x, vtwin_timing_y, 5, 1000.0 };
    d.rev_limit = 5500.0;
    d.rev_limit_duration = 0.0;
    d.redline = 5000.0;
    d.starter_torque = units::torque(70.0, units::ft_lb);
    d.starter_speed = 500.0;
    d.hf_gain = 0.01;
    d.jitter = 0.136;
    d.noise = 0.115;
    /* Bank angles are +-22.5 degrees, which the automatic crank_tdc formula
     * already covers correctly - no override needed here. */
    return d;
}

es_engine_def make_flat4() {
    const double stroke = mm(79.0);
    const double rodLength = inch(5.142);
    const double rodMass = grams(535.0);
    const double crankMass = kg(9.39);
    const double flywheelMass = kg(6.8);
    const double flywheelRadius = inch(6.0);

    es_engine_def d = {};
    d.name = "Subaru EJ25 Flat-4";
    /* Upstream's own 20 kHz; reconsider once measured. */
    d.simulation_frequency = 20000;
    d.bore = mm(99.5);
    d.stroke = stroke;
    d.crank_mass = crankMass;
    d.crank_moment_of_inertia =
        disk_moment(crankMass, stroke / 2) +
        disk_moment(flywheelMass, flywheelRadius) +
        disk_moment(kg(10.0), units::distance(6.0, units::cm));
    d.crank_friction_torque = units::torque(1.0, units::ft_lb);
    d.flywheel_mass = flywheelMass;
    d.rod_mass = rodMass;
    d.rod_length = rodLength;
    d.rod_moment_of_inertia = rod_moment(rodMass, rodLength);
    d.rod_center_of_mass = 0.0;
    d.piston_mass = grams(414.0 + 152.0);
    d.piston_compression_height = inch(1.0);
    d.rod_journal_angles = flat4_rod_journals;
    d.rod_journal_count = 4;
    d.banks = flat4_banks;
    d.bank_count = 2;
    d.exhaust_systems = flat4_exhausts;
    d.exhaust_system_count = 2;
    d.intake_plenum_volume = litres(1.325);
    d.intake_plenum_cross_section_area = cm2(20.0);
    d.intake_runner_length = inch(12.0);
    d.intake_idle_throttle_plate_position = 0.9985;
    d.intake_velocity_decay = 0.5;
    d.throttle_gamma = 2.0;
    d.firing_order = flat4_firing_order;
    d.timing_curve = { flat4_timing_x, flat4_timing_y, 5, 1000.0 };
    d.rev_limit = 6500.0;
    d.rev_limit_duration = 0.08;
    d.redline = 6500.0;
    d.starter_torque = units::torque(70.0, units::ft_lb);
    d.starter_speed = 500.0;
    d.hf_gain = 0.01;
    d.jitter = 0.5;
    d.noise = 1.0;
    /* A true 90 degree boxer: opposed cylinders share one crank throw, and
     * the automatic pi/2 - max(|bank angle|) reference (engine_builder.cpp)
     * cannot reach the 180 degrees this needs - it tops out at exactly 0 at
     * a 90 degree bank angle. Stated outright, as upstream states it. */
    d.crank_tdc = deg(180.0);
    return d;
}

es_engine_def make_inline5() {
    const double stroke = mm(79.5);
    const double rodLength = inch(5.142);
    const double rodMass = grams(535.0);
    const double crankMass = kg(9.39);
    const double flywheelMass = kg(6.8);
    const double flywheelRadius = inch(6.0);

    es_engine_def d = {};
    d.name = "Audi I5 Inline-5";
    /* Upstream's own 17 kHz; reconsider once measured. */
    d.simulation_frequency = 17000;
    d.bore = mm(86.4);
    d.stroke = stroke;
    d.crank_mass = crankMass;
    d.crank_moment_of_inertia =
        disk_moment(crankMass, stroke / 2) +
        disk_moment(flywheelMass, flywheelRadius) +
        disk_moment(kg(20.0), units::distance(8.0, units::cm));
    d.crank_friction_torque = units::torque(5.0, units::ft_lb);
    d.flywheel_mass = flywheelMass;
    d.rod_mass = rodMass;
    d.rod_length = rodLength;
    d.rod_moment_of_inertia = rod_moment(rodMass, rodLength);
    d.rod_center_of_mass = 0.0;
    d.piston_mass = grams(414.0 + 152.0);
    d.piston_compression_height = mm(32.8);
    d.rod_journal_angles = inline5_rod_journals;
    d.rod_journal_count = 5;
    d.banks = inline5_banks;
    d.bank_count = 1;
    d.exhaust_systems = inline5_exhausts;
    d.exhaust_system_count = 2;
    d.intake_plenum_volume = litres(1.0);
    d.intake_plenum_cross_section_area = cm2(10.0);
    d.intake_runner_length = inch(5.0);
    d.intake_idle_throttle_plate_position = 0.993;
    d.intake_velocity_decay = 0.25;
    d.throttle_gamma = 1.0;
    d.firing_order = inline5_firing_order;
    d.timing_curve = { inline5_timing_x, inline5_timing_y, 8, 1000.0 };
    d.rev_limit = 6500.0;
    d.rev_limit_duration = 0.1;
    d.redline = 6000.0;
    d.starter_torque = units::torque(200.0, units::ft_lb);
    d.starter_speed = 200.0;
    d.hf_gain = 0.01;
    d.jitter = 0.299;
    d.noise = 1.0;
    return d;
}

es_engine_def make_inline6() {
    const double stroke = mm(86.0);
    const double rodLength = mm(142.0);
    const double rodMass = grams(500.0);
    const double crankMass = kg(15.0);
    const double flywheelMass = kg(10.0);
    const double flywheelRadius = inch(7.0);

    es_engine_def d = {};
    d.name = "2JZ Inline-6";
    /* Upstream's own 10 kHz - the same rate the V8 originally shipped at and
     * needed lowering from. Reconsider once measured. */
    d.simulation_frequency = 10000;
    d.bore = mm(86.0);
    d.stroke = stroke;
    d.crank_mass = crankMass;
    d.crank_moment_of_inertia =
        disk_moment(crankMass, stroke / 2) +
        disk_moment(flywheelMass, flywheelRadius) +
        disk_moment(kg(20.0), units::distance(8.0, units::cm));
    d.crank_friction_torque = units::torque(5.0, units::ft_lb);
    d.flywheel_mass = flywheelMass;
    d.rod_mass = rodMass;
    d.rod_length = rodLength;
    d.rod_moment_of_inertia = rod_moment(rodMass, rodLength);
    d.rod_center_of_mass = 0.0;
    d.piston_mass = grams(200.0 + 50.0);
    d.piston_compression_height = mm(32.8);
    d.rod_journal_angles = inline6_rod_journals;
    d.rod_journal_count = 6;
    d.banks = inline6_banks;
    d.bank_count = 1;
    d.exhaust_systems = inline6_exhausts;
    d.exhaust_system_count = 2;
    d.intake_plenum_volume = litres(1.0);
    d.intake_plenum_cross_section_area = cm2(10.0);
    d.intake_runner_length = inch(40.0);
    d.intake_idle_throttle_plate_position = 0.9965;
    d.intake_velocity_decay = 0.25;
    d.throttle_gamma = 1.0;
    d.firing_order = inline6_firing_order;
    d.timing_curve = { inline6_timing_x, inline6_timing_y, 8, 1000.0 };
    d.rev_limit = 6500.0;
    d.rev_limit_duration = 0.1;
    d.redline = 6000.0;
    d.starter_torque = units::torque(200.0, units::ft_lb);
    /* Upstream doesn't state this (200 is this library's own default, shared
     * with the other engines that don't override it either). At 200 this
     * engine's 40" intake runner never gets enough flow established before
     * the starter lets go, and it dies rather than idling - raised until
     * measurement showed it catching reliably. */
    d.starter_speed = 400.0;
    d.hf_gain = 0.01;
    d.jitter = 0.23;
    d.noise = 1.0;
    return d;
}

es_engine_def make_v6() {
    const double stroke = inch(3.48);
    const double rodLength = inch(5.142);
    const double rodMass = grams(535.0);
    const double crankMass = pounds(50.0);
    const double flywheelMass = pounds(30.0);
    const double flywheelRadius = inch(6.0);

    es_engine_def d = {};
    d.name = "Even-Fire V6";
    /* Not stated upstream; the engine node's own default. Reconsider once
     * measured. */
    d.simulation_frequency = 10000;
    d.bore = inch(3.5);
    d.stroke = stroke;
    d.crank_mass = crankMass;
    /* Upstream derives this from the full stroke here, not stroke/2 as every
     * other engine in this file does - transcribed literally regardless. */
    d.crank_moment_of_inertia =
        disk_moment(crankMass, stroke) +
        disk_moment(flywheelMass, flywheelRadius) +
        disk_moment(kg(10.0), units::distance(1.0, units::cm));
    d.crank_friction_torque = units::torque(5.0, units::ft_lb);
    d.flywheel_mass = flywheelMass;
    d.rod_mass = rodMass;
    d.rod_length = rodLength;
    d.rod_moment_of_inertia = rod_moment(rodMass, rodLength);
    d.rod_center_of_mass = 0.0;
    d.piston_mass = grams(414.0 + 152.0);
    d.piston_compression_height = inch(1.0);
    d.rod_journal_angles = v6_rod_journals;
    d.rod_journal_count = 6;
    d.banks = v6_banks;
    d.bank_count = 2;
    d.exhaust_systems = v6_exhausts;
    d.exhaust_system_count = 2;
    d.intake_plenum_volume = litres(1.325);
    d.intake_plenum_cross_section_area = cm2(20.0);
    d.intake_runner_length = inch(4.0);
    d.intake_idle_throttle_plate_position = 0.995;
    d.intake_velocity_decay = 0.5;
    d.throttle_gamma = 2.0;
    d.firing_order = v6_firing_order;
    d.timing_curve = { v6_timing_x, v6_timing_y, 5, 1000.0 };
    d.rev_limit = 5600.0;
    d.rev_limit_duration = 0.2;
    d.redline = 5500.0;
    d.starter_torque = units::torque(70.0, units::ft_lb);
    d.starter_speed = 500.0;
    d.hf_gain = 0.01;
    d.jitter = 0.5;
    d.noise = 1.0;
    /* Bank angles are +-45 degrees, which the automatic crank_tdc formula
     * already covers correctly - no override needed here. */
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
    static es_engine_def single = {};
    static es_engine_def vtwin = {};
    static es_engine_def flat4 = {};
    static es_engine_def inline5 = {};
    static es_engine_def inline6 = {};
    static es_engine_def v6 = {};
    static es_bank_def v8b[2];
    static es_bank_def i4b[1];
    static es_bank_def singleb[1];
    static es_bank_def vtwinb[2];
    static es_bank_def flat4b[2];
    static es_bank_def inline5b[1];
    static es_bank_def inline6b[1];
    static es_bank_def v6b[2];
    static es_cylinder_def i4c[4];
    static es_cylinder_def singlec[1];
    static es_cylinder_def vtwinc0[1];
    static es_cylinder_def vtwinc1[1];
    static es_cylinder_def flat4c[4];
    static es_cylinder_def inline5c[5];
    static es_cylinder_def inline6c[6];
    static es_cylinder_def v6c[6];
    static es_exhaust_def singlex[1];
    static es_exhaust_def vtwinx[2];
    static es_exhaust_def flat4x[2];
    static es_exhaust_def inline5x[2];
    static es_exhaust_def inline6x[2];
    static es_exhaust_def v6x[2];
    static double v8_in[10], v8_ex[10], i4_in[7], i4_ex[7];
    static double single_in[15], single_ex[15];
    static double vtwin_in0[15], vtwin_ex0[15], vtwin_in1[15], vtwin_ex1[15];
    static double flat4_in[10], flat4_ex[10];
    static double inline5_in[10], inline5_ex[10];
    static double inline6_in[10], inline6_ex[10];
    static double v6_in[10], v6_ex[10];
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

        single = make_single();
        single.exhaust_systems = singlex;
        singlex[0] = single_exhausts[0];
        singlex[0].outlet_flow_rate = GasSystem::k_carb(500.0);
        singlex[0].primary_flow_rate = GasSystem::k_carb(200.0);
        singleb[0] = single_banks[0];
        singleb[0].intake_flow.y = converted_flow(single_intake_flow_y, single_in);
        singleb[0].exhaust_flow.y = converted_flow(single_exhaust_flow_y, single_ex);
        singlec[0] = single_cylinders[0];
        singlec[0].blowby = GasSystem::k_28inH2O(single_blowby_cfm[0]);
        singleb[0].cylinders = singlec;
        single.banks = singleb;
        single.intake_flow_rate = GasSystem::k_carb(100.0);
        single.intake_runner_flow_rate = GasSystem::k_carb(200.0);
        single.intake_idle_flow_rate = GasSystem::k_carb(0.0);

        vtwin = make_vtwin();
        for (int i = 0; i < 2; ++i) {
            vtwinx[i] = vtwin_exhausts[i];
            vtwinx[i].outlet_flow_rate = GasSystem::k_carb(100.0);
            vtwinx[i].primary_flow_rate = GasSystem::k_carb(100.0);
        }
        vtwin.exhaust_systems = vtwinx;
        vtwinb[0] = vtwin_banks[0];
        vtwinb[1] = vtwin_banks[1];
        vtwinb[0].intake_flow.y = converted_flow(vtwin_intake_flow_y0, vtwin_in0);
        vtwinb[0].exhaust_flow.y = converted_flow(vtwin_exhaust_flow_y0, vtwin_ex0);
        vtwinb[1].intake_flow.y = converted_flow(vtwin_intake_flow_y1, vtwin_in1);
        vtwinb[1].exhaust_flow.y = converted_flow(vtwin_exhaust_flow_y1, vtwin_ex1);
        vtwinc0[0] = vtwin_bank0[0];
        vtwinc0[0].blowby = GasSystem::k_28inH2O(vtwin_blowby_cfm[0]);
        vtwinc1[0] = vtwin_bank1[0];
        vtwinc1[0].blowby = GasSystem::k_28inH2O(vtwin_blowby_cfm[1]);
        vtwinb[0].cylinders = vtwinc0;
        vtwinb[1].cylinders = vtwinc1;
        vtwin.banks = vtwinb;
        vtwin.intake_flow_rate = GasSystem::k_carb(100.0);
        vtwin.intake_runner_flow_rate = GasSystem::k_carb(200.0);
        vtwin.intake_idle_flow_rate = GasSystem::k_carb(0.0);

        flat4 = make_flat4();
        for (int i = 0; i < 2; ++i) {
            flat4x[i] = flat4_exhausts[i];
            flat4x[i].outlet_flow_rate = GasSystem::k_carb(1000.0);
            flat4x[i].primary_flow_rate = GasSystem::k_carb(200.0);
        }
        flat4.exhaust_systems = flat4x;
        flat4b[0] = flat4_banks[0];
        flat4b[1] = flat4_banks[1];
        flat4b[0].intake_flow.y = converted_flow(flat4_intake_flow_y, flat4_in);
        flat4b[0].exhaust_flow.y = converted_flow(flat4_exhaust_flow_y, flat4_ex);
        flat4b[1].intake_flow.y = flat4_in;
        flat4b[1].exhaust_flow.y = flat4_ex;
        flat4c[0] = flat4_bank0[0];
        flat4c[0].blowby = GasSystem::k_28inH2O(flat4_blowby_cfm[0]);
        flat4c[1] = flat4_bank0[1];
        flat4c[1].blowby = GasSystem::k_28inH2O(flat4_blowby_cfm[1]);
        flat4c[2] = flat4_bank1[0];
        flat4c[2].blowby = GasSystem::k_28inH2O(flat4_blowby_cfm[2]);
        flat4c[3] = flat4_bank1[1];
        flat4c[3].blowby = GasSystem::k_28inH2O(flat4_blowby_cfm[3]);
        flat4b[0].cylinders = &flat4c[0];
        flat4b[1].cylinders = &flat4c[2];
        flat4.banks = flat4b;
        flat4.intake_flow_rate = GasSystem::k_carb(800.0);
        flat4.intake_runner_flow_rate = GasSystem::k_carb(250.0);
        flat4.intake_idle_flow_rate = GasSystem::k_carb(0.0);

        inline5 = make_inline5();
        for (int i = 0; i < 2; ++i) {
            inline5x[i] = inline5_exhausts[i];
            inline5x[i].outlet_flow_rate = GasSystem::k_carb(500.0);
            inline5x[i].primary_flow_rate = GasSystem::k_carb(100.0);
        }
        inline5.exhaust_systems = inline5x;
        inline5b[0] = inline5_banks[0];
        inline5b[0].intake_flow.y = converted_flow(inline5_intake_flow_y, inline5_in);
        inline5b[0].exhaust_flow.y = converted_flow(inline5_exhaust_flow_y, inline5_ex);
        for (int i = 0; i < 5; ++i) {
            inline5c[i] = inline5_cylinders[i];
            inline5c[i].blowby = GasSystem::k_28inH2O(inline5_blowby_cfm[i]);
        }
        inline5b[0].cylinders = inline5c;
        inline5.banks = inline5b;
        inline5.intake_flow_rate = GasSystem::k_carb(350.0);
        inline5.intake_runner_flow_rate = GasSystem::k_carb(175.0);
        inline5.intake_idle_flow_rate = GasSystem::k_carb(0.0);

        inline6 = make_inline6();
        for (int i = 0; i < 2; ++i) {
            inline6x[i] = inline6_exhausts[i];
            inline6x[i].outlet_flow_rate = GasSystem::k_carb(1000.0);
            inline6x[i].primary_flow_rate = GasSystem::k_carb(400.0);
        }
        inline6.exhaust_systems = inline6x;
        inline6b[0] = inline6_banks[0];
        inline6b[0].intake_flow.y = converted_flow(inline6_intake_flow_y, inline6_in);
        inline6b[0].exhaust_flow.y = converted_flow(inline6_exhaust_flow_y, inline6_ex);
        for (int i = 0; i < 6; ++i) {
            inline6c[i] = inline6_cylinders[i];
            inline6c[i].blowby = GasSystem::k_28inH2O(inline6_blowby_cfm[i]);
        }
        inline6b[0].cylinders = inline6c;
        inline6.banks = inline6b;
        inline6.intake_flow_rate = GasSystem::k_carb(500.0);
        inline6.intake_runner_flow_rate = GasSystem::k_carb(200.0);
        inline6.intake_idle_flow_rate = GasSystem::k_carb(0.0);

        v6 = make_v6();
        for (int i = 0; i < 2; ++i) {
            v6x[i] = v6_exhausts[i];
            v6x[i].outlet_flow_rate = GasSystem::k_carb(1000.0);
            v6x[i].primary_flow_rate = GasSystem::k_carb(300.0);
        }
        v6.exhaust_systems = v6x;
        v6b[0] = v6_banks[0];
        v6b[1] = v6_banks[1];
        v6b[0].intake_flow.y = converted_flow(v6_intake_flow_y, v6_in);
        v6b[0].exhaust_flow.y = converted_flow(v6_exhaust_flow_y, v6_ex);
        v6b[1].intake_flow.y = v6_in;
        v6b[1].exhaust_flow.y = v6_ex;
        for (int i = 0; i < 3; ++i) {
            v6c[i] = v6_bank0[i];
            v6c[i].blowby = GasSystem::k_28inH2O(v6_blowby_cfm[i]);
        }
        for (int i = 0; i < 3; ++i) {
            v6c[3 + i] = v6_bank1[i];
            v6c[3 + i].blowby = GasSystem::k_28inH2O(v6_blowby_cfm[3 + i]);
        }
        v6b[0].cylinders = &v6c[0];
        v6b[1].cylinders = &v6c[3];
        v6.banks = v6b;
        v6.intake_flow_rate = GasSystem::k_carb(400.0);
        v6.intake_runner_flow_rate = GasSystem::k_carb(250.0);
        v6.intake_idle_flow_rate = GasSystem::k_carb(0.0);

        ready = true;
    }

    switch (preset) {
        case ES_PRESET_INLINE_4: *out = i4; return ES_OK;
        case ES_PRESET_V8:       *out = v8; return ES_OK;
        case ES_PRESET_SINGLE:   *out = single; return ES_OK;
        case ES_PRESET_V_TWIN:   *out = vtwin; return ES_OK;
        case ES_PRESET_FLAT_4:   *out = flat4; return ES_OK;
        case ES_PRESET_INLINE_5: *out = inline5; return ES_OK;
        case ES_PRESET_INLINE_6: *out = inline6; return ES_OK;
        case ES_PRESET_V6:       *out = v6; return ES_OK;
        default:                 return ES_ERR_INVALID_ARGUMENT;
    }
}

extern "C" const char *es_preset_name(es_preset preset) {
    switch (preset) {
        case ES_PRESET_INLINE_4: return "Inline-4";
        case ES_PRESET_V8: return "V8";
        case ES_PRESET_SINGLE: return "Single";
        case ES_PRESET_V_TWIN: return "V-Twin";
        case ES_PRESET_FLAT_4: return "Flat-4";
        case ES_PRESET_INLINE_5: return "Inline-5";
        case ES_PRESET_INLINE_6: return "Inline-6";
        case ES_PRESET_V6: return "V6";
        default: return "unknown";
    }
}
