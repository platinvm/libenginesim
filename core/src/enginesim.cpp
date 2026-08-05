#include "enginesim.h"
#include "engine_builder.h"
#include "builtin_ir.h"

#include "engine.h"
#include "piston_engine_simulator.h"
#include "transmission.h"
#include "vehicle.h"
#include "gas_system.h"
#include "constants.h"
#include "units.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <string>
#include <vector>

/* ---------------------------------------------------------------- state -- */

struct es_sim {
    Engine engine;
    Vehicle vehicle;
    Transmission transmission;
    PistonEngineSimulator sim;
    es::EngineParts parts;

    std::string name;
    uint32_t sampleRate = 44100;
    uint32_t simFrequency = 10000;
    double redline = 0.0;
    uint32_t cylinderCount = 0;

    double throttle = 0.0;
    bool starter = false;
    bool ignition = true;

    /* Upstream keeps its rev-limiter countdown protected, so it is mirrored
     * here with the same rule: refreshed whenever crank speed exceeds the
     * limit, otherwise counting down. */
    double revLimit = 0.0;
    double revLimitDuration = 0.5;
    double revLimitTimer = 0.0;

    /* Sim steps owed to the synthesizer, carried between calls so a request
     * for 128 frames does not quietly round the engine's clock. */
    double stepDebt = 0.0;

    /* Staging for the int16 the synthesizer produces. */
    std::vector<int16_t> scratch;
};

/* ------------------------------------------------------------------ ABI -- */

extern "C" uint32_t es_abi_version(void) {
    return ES_ABI_VERSION;
}

extern "C" const char *es_result_str(es_result result) {
    switch (result) {
        case ES_OK: return "ok";
        case ES_ERR_INVALID_ARGUMENT: return "invalid argument";
        case ES_ERR_OUT_OF_MEMORY: return "out of memory";
        case ES_ERR_UNSUPPORTED: return "unsupported";
        default: return "unknown error";
    }
}

extern "C" double es_flow_from_carb_cfm(double scfm) {
    return GasSystem::k_carb(scfm);
}

extern "C" double es_flow_from_cfm_28(double scfm) {
    return GasSystem::k_28inH2O(scfm);
}

/* --------------------------------------------------------------- create -- */

extern "C" es_result es_sim_create(const es_engine_def *def,
                                   const es_sim_config *config,
                                   es_sim **out) {
    if (out == nullptr) return ES_ERR_INVALID_ARGUMENT;
    *out = nullptr;

    const es_result valid = es::validate(def);
    if (valid != ES_OK) return valid;

    es_sim *s = new (std::nothrow) es_sim;
    if (s == nullptr) return ES_ERR_OUT_OF_MEMORY;

    s->name = (def->name != nullptr) ? def->name : "Engine";
    s->sampleRate = (config != nullptr && config->sample_rate != 0)
        ? config->sample_rate : 44100;
    s->simFrequency = (def->simulation_frequency != 0)
        ? def->simulation_frequency : 10000;
    s->redline = def->redline;
    s->revLimit = units::rpm(def->rev_limit);
    s->revLimitDuration =
        (def->rev_limit_duration > 0.0) ? def->rev_limit_duration : 0.5;

    const es_result built = es::build_engine(def, &s->engine, &s->parts);
    if (built != ES_OK) {
        delete s;
        return built;
    }
    s->cylinderCount = static_cast<uint32_t>(s->engine.getCylinderCount());

    /* A vehicle and transmission are mandatory in upstream's simulator. The
     * ABI exposes no drivetrain, so these stand in as a free-revving engine on
     * a stand: heavy enough to be stable, clutch disengaged. */
    Vehicle::Parameters vp;
    vp.mass = units::mass(1597.0, units::kg);
    vp.diffRatio = 3.42;
    vp.tireRadius = units::distance(10.0, units::inch);
    vp.dragCoefficient = 0.25;
    vp.crossSectionArea = units::distance(72.0, units::inch) *
                          units::distance(54.0, units::inch);
    vp.rollingResistance = 2000.0;
    s->vehicle.initialize(vp);

    static const double gearRatios[] = { 2.97 };
    Transmission::Parameters tp;
    tp.GearCount = 1;
    tp.GearRatios = gearRatios;
    tp.MaxClutchTorque = units::torque(1000.0, units::ft_lb);
    s->transmission.initialize(tp);

    Simulator::Parameters sp;
    sp.systemType = Simulator::SystemType::NsvOptimized;
    s->sim.initialize(sp);
    s->sim.loadSimulation(&s->engine, &s->vehicle, &s->transmission);
    s->sim.setFluidSimulationSteps(8);
    s->sim.setSimulationFrequency(static_cast<int>(s->simFrequency));

    /*
     * Upstream adapts its step count to how far the synthesizer has fallen
     * behind, because its audio runs on another thread. Here the host asks for
     * a frame count and we derive the steps from it, so that feedback loop
     * must not interfere. An unreachable latency target pins startFrame() to
     * its "needs more" branch, which only ever grows the per-frame budget; we
     * then take exactly the steps we intended. The budget is a ceiling, not a
     * quota, so overshooting it costs nothing.
     */
    s->sim.setTargetSynthesizerLatency(1.0e9);

    /* loadSimulation() has already built a synthesizer at a hardcoded 44.1 kHz.
     * Tear it down so the configured sample rate and this engine's audio
     * character take effect. */
    s->sim.synthesizer().destroy();

    Synthesizer::Parameters synth;
    synth.inputChannelCount = s->engine.getExhaustSystemCount();
    synth.inputBufferSize = static_cast<int>(s->sampleRate);
    synth.audioBufferSize = static_cast<int>(s->sampleRate);
    synth.inputSampleRate = static_cast<float>(s->simFrequency);
    synth.audioSampleRate = static_cast<float>(s->sampleRate);
    synth.initialAudioParameters.airNoise = static_cast<float>(def->noise);
    synth.initialAudioParameters.inputSampleNoise = static_cast<float>(def->jitter);
    synth.initialAudioParameters.dF_F_mix = static_cast<float>(def->hf_gain);
    s->sim.synthesizer().initialize(synth);

    /* The convolution filter dereferences null until an impulse response is
     * installed, so this is not optional. */
    const bool hostIr = config != nullptr &&
                        config->impulse_response != nullptr &&
                        config->impulse_response_frames > 0;
    const int16_t *ir = hostIr ? config->impulse_response
                               : es::builtin_impulse_response;
    const uint32_t irFrames = hostIr ? config->impulse_response_frames
                                     : es::builtin_impulse_response_frames;
    /* The built-in response is calibrated at a specific gain; a host-supplied
     * one defaults to unity. Either can be overridden explicitly. */
    double irVolume = hostIr ? 1.0 : es::builtin_impulse_response_volume;
    if (config != nullptr && config->impulse_response_volume > 0.0) {
        irVolume = config->impulse_response_volume;
    }
    for (int i = 0; i < s->engine.getExhaustSystemCount(); ++i) {
        s->sim.synthesizer().initializeImpulseResponse(
            ir, irFrames, static_cast<float>(irVolume), i);
    }

    /* Start closed rather than at the linkage's default of wide open. */
    s->engine.setSpeedControl(0.0);
    s->engine.getIgnitionModule()->m_enabled = true;
    s->sim.m_dyno.m_enabled = false;
    s->sim.m_starterMotor.m_enabled = false;

    *out = s;
    return ES_OK;
}

extern "C" void es_sim_destroy(es_sim *sim) {
    if (sim == nullptr) return;
    sim->sim.releaseSimulation();
    sim->engine.destroy();
    delete sim;
}

/* ---------------------------------------------------------------- drive -- */

/*
 * Upstream's Synthesizer permits exactly one renderAudio() per endInputBlock()
 * and caps its internal buffer at 2000 samples. Its condition variable is only
 * entered when the wait predicate already holds, so checking the predicate
 * here means the wait returns immediately and no thread is ever needed.
 */
static void generate(es_sim *s, uint32_t frames) {
    Synthesizer &synth = s->sim.synthesizer();
    const double stepsPerFrame = (double)s->simFrequency / (double)s->sampleRate;

    int guard = 0;
    while (synth.m_audioBuffer.size() < frames) {
        if (++guard > 4096) break;  /* cannot happen; refuses to hang if it does */

        /* Run enough physics to cover the shortfall, carrying the fractional
         * remainder so the engine's clock stays exact across calls. */
        const size_t shortfall = frames - synth.m_audioBuffer.size();
        s->stepDebt += shortfall * stepsPerFrame;
        int steps = (int)s->stepDebt;
        if (steps < 1) steps = 1;
        s->stepDebt -= steps;

        /* endFrame() ends the synthesizer's input block for us. */
        const double elapsed = (double)steps / (double)s->simFrequency;
        s->sim.startFrame(elapsed);
        for (int i = 0; i < steps; ++i) s->sim.simulateStep();
        s->sim.endFrame();

        s->revLimitTimer -= elapsed;
        if (std::fabs(s->engine.getSpeed()) > s->revLimit) {
            s->revLimitTimer = s->revLimitDuration;
        }
        if (s->revLimitTimer < 0.0) s->revLimitTimer = 0.0;

        if (synth.m_inputChannels[0].data.size() > 0 &&
            synth.m_audioBuffer.size() < 2000 && !synth.m_processed) {
            synth.renderAudio();
        }
    }
}

extern "C" uint32_t es_sim_step(es_sim *sim, float *audio, uint32_t frames) {
    if (sim == nullptr || frames == 0) return 0;

    uint32_t written = 0;
    while (written < frames) {
        /* Bounded by the synthesizer's internal cap. */
        const uint32_t chunk = std::min<uint32_t>(frames - written, 1024);
        generate(sim, chunk);

        if (sim->scratch.size() < chunk) sim->scratch.resize(chunk);
        const int got = sim->sim.readAudioOutput(
            static_cast<int>(chunk), sim->scratch.data());
        (void)got;

        if (audio != nullptr) {
            for (uint32_t i = 0; i < chunk; ++i) {
                audio[written + i] = sim->scratch[i] * (1.0f / 32768.0f);
            }
        }
        written += chunk;
    }
    return written;
}

/* -------------------------------------------------------------- control -- */

extern "C" void es_sim_set_throttle(es_sim *sim, double throttle) {
    if (sim == nullptr) return;
    sim->throttle = std::min(1.0, std::max(0.0, throttle));
    /* Upstream's "speed control" is the pedal: 0 idles, 1 is wide open. The
     * linkage inverts it into a throttle plate position internally. */
    sim->engine.setSpeedControl(sim->throttle);
}

extern "C" void es_sim_set_starter(es_sim *sim, int engaged) {
    if (sim == nullptr) return;
    sim->starter = (engaged != 0);
    sim->sim.m_starterMotor.m_enabled = sim->starter;
}

extern "C" void es_sim_set_ignition(es_sim *sim, int enabled) {
    if (sim == nullptr) return;
    sim->ignition = (enabled != 0);
    sim->engine.getIgnitionModule()->m_enabled = sim->ignition;
}

extern "C" void es_sim_set_crank_speed(es_sim *sim, double rpm) {
    if (sim == nullptr) return;
    /* Upstream spins the crank negative and reports getRpm() as the absolute
     * value, so match its sign or the engine runs backwards. */
    const double radiansPerSecond = -rpm * (2.0 * constants::pi / 60.0);
    for (int i = 0; i < sim->engine.getCrankshaftCount(); ++i) {
        sim->engine.getCrankshaft(i)->m_body.v_theta = radiansPerSecond;
    }
}

extern "C" void es_sim_set_dyno(es_sim *sim, int enabled, double speed) {
    if (sim == nullptr) return;
    sim->sim.m_dyno.m_enabled = (enabled != 0);
    sim->sim.m_dyno.m_rotationSpeed = units::rpm(std::max(0.0, speed));
}

extern "C" void es_sim_set_volume(es_sim *sim, double volume) {
    if (sim == nullptr) return;
    Synthesizer::AudioParameters p = sim->sim.synthesizer().getAudioParameters();
    p.volume = static_cast<float>(std::max(0.0, volume));
    sim->sim.synthesizer().setAudioParameters(p);
}

/* ------------------------------------------------------------ telemetry -- */

extern "C" void es_sim_telemetry(const es_sim *sim, es_telemetry *out) {
    if (out == nullptr) return;
    std::memset(out, 0, sizeof(*out));
    if (sim == nullptr) return;

    es_sim *s = const_cast<es_sim *>(sim);
    Engine &e = s->engine;

    out->rpm = e.getRpm();
    out->torque = s->sim.getFilteredDynoTorque();
    out->power = s->sim.getDynoPower();
    out->throttle = s->throttle;
    /* getThrottlePlateAngle() is an angle in radians from shut to wide open;
     * the ABI promises a 0..1 fraction. */
    out->throttle_plate_position =
        e.getThrottlePlateAngle() / (constants::pi / 2);
    out->manifold_pressure = e.getManifoldPressure();
    out->intake_afr = e.getIntakeAfr();
    out->exhaust_o2 = e.getExhaustO2();
    out->fuel_consumed = e.getTotalFuelMassConsumed();
    out->redline = s->redline;
    out->displacement = e.getDisplacement();
    out->cylinder_count = s->cylinderCount;
    out->rev_limiter_active = (s->revLimitTimer > 0.0) ? 1 : 0;
}
