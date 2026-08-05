#include "engine_builder.h"

#include "engine.h"
#include "function.h"
#include "impulse_response.h"
#include "direct_throttle_linkage.h"
#include "standard_valvetrain.h"
#include "units.h"
#include "constants.h"

#include <cmath>
#include <cstring>
#include <new>

namespace es {

EngineParts::~EngineParts() {
    for (Function *f : functions) {
        if (f != nullptr) {
            f->destroy();
            delete f;
        }
    }
    for (ImpulseResponse *ir : impulseResponses) delete ir;
    for (Camshaft *c : camshafts) {
        c->destroy();
        delete c;
    }
    for (Valvetrain *v : valvetrains) delete v;
}

Function *make_curve(const es_curve &curve, EngineParts *parts) {
    Function *f = new (std::nothrow) Function;
    if (f == nullptr) return nullptr;

    f->initialize(static_cast<int>(curve.count), curve.filter_radius);
    for (uint32_t i = 0; i < curve.count; ++i) {
        f->addSample(curve.x[i], curve.y[i]);
    }
    parts->functions.push_back(f);
    return f;
}

/*
 * Upstream generates cam lobes harmonically from the numbers on a cam card.
 * Reproduced from scripting/include/actions.h so definitions stay comparable
 * with upstream's own engines.
 */
Function *make_cam_lobe(const es_cam_lobe &lobe, EngineParts *parts) {
    const int steps = (lobe.steps == 0) ? 256 : static_cast<int>(lobe.steps);
    const double gamma = (lobe.gamma == 0.0) ? 1.0 : lobe.gamma;
    const double lift = lobe.lift;
    if (lift <= 0.0 || steps < 6) return nullptr;

    const double angle = lobe.duration_at_50_thou / 4;
    const double s =
        std::pow(2 * units::distance(50, units::thou) / lift, 1 / gamma) - 1;
    const double k = std::acos(s) / angle;
    const double extents = constants::pi / k;
    const double step = extents / (steps - 5.0);

    Function *f = new (std::nothrow) Function;
    if (f == nullptr) return nullptr;
    f->initialize(steps * 2, step);

    for (int i = 0; i < steps; ++i) {
        if (i == 0) {
            f->addSample(0.0, lift);
        } else {
            const double x = i * step;
            const double y = (x >= extents)
                ? 0.0
                : lift * std::pow(0.5 + 0.5 * std::cos(k * x), gamma);
            f->addSample(x, y);
            f->addSample(-x, y);
        }
    }

    parts->functions.push_back(f);
    return f;
}

static bool curve_ok(const es_curve &c) {
    return c.x != nullptr && c.y != nullptr && c.count >= 2;
}

es_result validate(const es_engine_def *def) {
    if (def == nullptr) return ES_ERR_INVALID_ARGUMENT;
    if (def->bank_count == 0 || def->banks == nullptr) return ES_ERR_INVALID_ARGUMENT;
    if (def->exhaust_system_count == 0 || def->exhaust_systems == nullptr) {
        return ES_ERR_INVALID_ARGUMENT;
    }
    if (def->rod_journal_count == 0 || def->rod_journal_angles == nullptr) {
        return ES_ERR_INVALID_ARGUMENT;
    }
    if (def->bore <= 0.0 || def->stroke <= 0.0 || def->rod_length <= 0.0) {
        return ES_ERR_INVALID_ARGUMENT;
    }
    if (!curve_ok(def->timing_curve)) return ES_ERR_INVALID_ARGUMENT;

    uint32_t cylinders = 0;
    for (uint32_t b = 0; b < def->bank_count; ++b) {
        const es_bank_def &bank = def->banks[b];
        if (bank.cylinder_count == 0 || bank.cylinders == nullptr) {
            return ES_ERR_INVALID_ARGUMENT;
        }
        if (!curve_ok(bank.intake_flow) || !curve_ok(bank.exhaust_flow)) {
            return ES_ERR_INVALID_ARGUMENT;
        }
        if (bank.intake_lobe.lift <= 0.0 || bank.exhaust_lobe.lift <= 0.0) {
            return ES_ERR_INVALID_ARGUMENT;
        }
        for (uint32_t c = 0; c < bank.cylinder_count; ++c) {
            if (bank.cylinders[c].rod_journal >= def->rod_journal_count) {
                return ES_ERR_INVALID_ARGUMENT;
            }
            if (bank.cylinders[c].exhaust_system >= def->exhaust_system_count) {
                return ES_ERR_INVALID_ARGUMENT;
            }
        }
        cylinders += bank.cylinder_count;
    }

    /* Upstream writes through a fixed 8-slot array indexed by cylinder. */
    if (cylinders > ES_MAX_CYLINDERS) return ES_ERR_INVALID_ARGUMENT;

    if (def->firing_order == nullptr) return ES_ERR_INVALID_ARGUMENT;
    /* The firing order must be a permutation of every cylinder. */
    bool seen[ES_MAX_CYLINDERS] = { false };
    for (uint32_t i = 0; i < cylinders; ++i) {
        const uint32_t c = def->firing_order[i];
        if (c >= cylinders || seen[c]) return ES_ERR_INVALID_ARGUMENT;
        seen[c] = true;
    }

    return ES_OK;
}

es_result build_engine(const es_engine_def *def, Engine *engine, EngineParts *parts) {
    uint32_t cylinderCount = 0;
    for (uint32_t b = 0; b < def->bank_count; ++b) {
        cylinderCount += def->banks[b].cylinder_count;
    }

    /* Where each cylinder sits in the firing order. This single mapping drives
     * both spark timing and cam lobe placement, exactly as upstream's engine
     * definitions do by hand. */
    uint32_t firingPosition[ES_MAX_CYLINDERS] = { 0 };
    for (uint32_t i = 0; i < cylinderCount; ++i) {
        firingPosition[def->firing_order[i]] = i;
    }

    DirectThrottleLinkage *linkage = new (std::nothrow) DirectThrottleLinkage;
    if (linkage == nullptr) return ES_ERR_OUT_OF_MEMORY;
    DirectThrottleLinkage::Parameters lp;
    lp.gamma = (def->throttle_gamma > 0.0) ? def->throttle_gamma : 1.0;
    linkage->initialize(lp);

    Engine::Parameters ep;
    ep.name = (def->name != nullptr) ? def->name : "Engine";
    ep.cylinderBanks = static_cast<int>(def->bank_count);
    ep.cylinderCount = static_cast<int>(cylinderCount);
    ep.crankshaftCount = 1;
    ep.exhaustSystemCount = static_cast<int>(def->exhaust_system_count);
    ep.intakeCount = 1;
    ep.starterTorque = def->starter_torque;
    ep.starterSpeed = units::rpm(def->starter_speed);
    ep.redline = units::rpm(def->redline);
    ep.dynoMinSpeed = units::rpm(1000.0);
    ep.dynoMaxSpeed = units::rpm(def->redline);
    ep.dynoHoldStep = units::rpm(100.0);
    ep.throttle = linkage;   /* Engine::destroy() takes ownership */
    ep.initialSimulationFrequency =
        (def->simulation_frequency == 0) ? 10000.0 : def->simulation_frequency;
    ep.initialHighFrequencyGain = def->hf_gain;
    ep.initialNoise = def->noise;
    ep.initialJitter = def->jitter;
    engine->initialize(ep);

    /* -- exhaust systems -- */
    for (uint32_t i = 0; i < def->exhaust_system_count; ++i) {
        const es_exhaust_def &x = def->exhaust_systems[i];

        ImpulseResponse *ir = new (std::nothrow) ImpulseResponse;
        if (ir == nullptr) return ES_ERR_OUT_OF_MEMORY;
        /* The audio path never reads this back; the sample data is handed to
         * the synthesizer directly. It exists so upstream's plumbing is
         * satisfied without touching the filesystem. */
        ir->initialize("", 1.0);
        parts->impulseResponses.push_back(ir);

        ExhaustSystem::Parameters xp;
        xp.length = (x.length > 0.0) ? x.length : units::distance(100.0, units::inch);
        xp.collectorCrossSectionArea = (x.collector_cross_section_area > 0.0)
            ? x.collector_cross_section_area
            : constants::pi * units::distance(2.0, units::inch) *
              units::distance(2.0, units::inch);
        xp.outletFlowRate = x.outlet_flow_rate;
        xp.primaryTubeLength = x.primary_tube_length;
        xp.primaryFlowRate = x.primary_flow_rate;
        xp.velocityDecay = x.velocity_decay;
        xp.audioVolume = x.audio_volume;
        xp.impulseResponse = ir;
        engine->getExhaustSystem(static_cast<int>(i))->initialize(xp);

        if (x.volume > 0.0) {
            engine->getExhaustSystem(static_cast<int>(i))->getSystem()->setVolume(x.volume);
        }
    }

    /* -- intake, shared by every cylinder -- */
    Intake::Parameters ip;
    ip.volume = def->intake_plenum_volume;
    ip.CrossSectionArea = def->intake_plenum_cross_section_area;
    ip.InputFlowK = def->intake_flow_rate;
    ip.IdleFlowK = def->intake_idle_flow_rate;
    ip.RunnerFlowRate = def->intake_runner_flow_rate;
    ip.IdleThrottlePlatePosition = def->intake_idle_throttle_plate_position;
    ip.RunnerLength = def->intake_runner_length;
    ip.VelocityDecay = def->intake_velocity_decay;
    engine->getIntake(0)->initialize(ip);

    /* -- crankshaft --
     * TDC is measured from vertical, so a bank tilted by half the vee angle
     * puts top dead centre a quarter turn minus that tilt away. */
    double maxBankAngle = 0.0;
    for (uint32_t b = 0; b < def->bank_count; ++b) {
        const double a = std::fabs(def->banks[b].angle);
        if (a > maxBankAngle) maxBankAngle = a;
    }

    Crankshaft::Parameters cp;
    cp.mass = def->crank_mass;
    cp.flywheelMass = def->flywheel_mass;
    cp.momentOfInertia = def->crank_moment_of_inertia;
    cp.crankThrow = def->stroke / 2;
    cp.pos_x = 0.0;
    cp.pos_y = 0.0;
    cp.tdc = (def->crank_tdc != 0.0) ? def->crank_tdc : constants::pi / 2 - maxBankAngle;
    cp.frictionTorque = def->crank_friction_torque;
    cp.rodJournals = static_cast<int>(def->rod_journal_count);
    Crankshaft *crank = engine->getCrankshaft(0);
    crank->initialize(cp);
    for (uint32_t i = 0; i < def->rod_journal_count; ++i) {
        crank->setRodJournalAngle(static_cast<int>(i), def->rod_journal_angles[i]);
    }

    const double deckHeight =
        def->stroke / 2 + def->rod_length + def->piston_compression_height;

    /* -- banks, heads, pistons and rods -- */
    uint32_t base = 0;
    for (uint32_t b = 0; b < def->bank_count; ++b) {
        const es_bank_def &bank = def->banks[b];

        CylinderBank::Parameters bp;
        bp.crankshaft = crank;
        bp.positionX = 0.0;
        bp.positionY = 0.0;
        bp.angle = bank.angle;
        bp.bore = def->bore;
        bp.deckHeight = deckHeight;
        bp.displayDepth = 1.0;
        bp.cylinderCount = static_cast<int>(bank.cylinder_count);
        bp.index = static_cast<int>(b);
        CylinderBank *cb = engine->getCylinderBank(static_cast<int>(b));
        cb->initialize(bp);

        for (uint32_t c = 0; c < bank.cylinder_count; ++c) {
            const es_cylinder_def &cyl = bank.cylinders[c];
            const int flat = static_cast<int>(base + c);

            Piston *piston = engine->getPiston(flat);
            ConnectingRod *rod = engine->getConnectingRod(flat);

            Piston::Parameters pp;
            pp.Rod = rod;
            pp.Bank = cb;
            pp.CylinderIndex = static_cast<int>(c);
            pp.BlowbyFlowCoefficient = cyl.blowby;
            pp.CompressionHeight = def->piston_compression_height;
            pp.WristPinPosition = 0.0;
            pp.Displacement = 0.0;
            pp.mass = def->piston_mass;
            piston->initialize(pp);

            ConnectingRod::Parameters rp;
            rp.mass = def->rod_mass;
            rp.momentOfInertia = def->rod_moment_of_inertia;
            rp.centerOfMass = def->rod_center_of_mass;
            rp.length = def->rod_length;
            rp.rodJournals = 0;
            rp.slaveThrow = 0.0;
            rp.piston = piston;
            rp.crankshaft = crank;
            rp.master = nullptr;
            rp.journal = static_cast<int>(cyl.rod_journal);
            rod->initialize(rp);
        }

        /* -- cam --
         * A lobe's centreline is its nominal centre offset by where that
         * cylinder falls in the firing order. */
        Function *intakeLobe = make_cam_lobe(bank.intake_lobe, parts);
        Function *exhaustLobe = make_cam_lobe(bank.exhaust_lobe, parts);
        if (intakeLobe == nullptr || exhaustLobe == nullptr) return ES_ERR_OUT_OF_MEMORY;

        Camshaft *intakeCam = new (std::nothrow) Camshaft;
        Camshaft *exhaustCam = new (std::nothrow) Camshaft;
        if (intakeCam == nullptr || exhaustCam == nullptr) {
            delete intakeCam;
            delete exhaustCam;
            return ES_ERR_OUT_OF_MEMORY;
        }
        parts->camshafts.push_back(intakeCam);
        parts->camshafts.push_back(exhaustCam);

        Camshaft::Parameters camp;
        camp.lobes = static_cast<int>(bank.cylinder_count);
        camp.advance = bank.cam_advance;
        camp.crankshaft = crank;
        camp.baseRadius = (bank.cam_base_radius > 0.0)
            ? bank.cam_base_radius
            : units::distance(600, units::thou);

        camp.lobeProfile = intakeLobe;
        intakeCam->initialize(camp);
        camp.lobeProfile = exhaustLobe;
        exhaustCam->initialize(camp);

        const double cycle = 4 * constants::pi;
        const double spacing = cycle / cylinderCount;
        for (uint32_t c = 0; c < bank.cylinder_count; ++c) {
            const double offset = firingPosition[base + c] * spacing;
            intakeCam->setLobeCenterline(
                static_cast<int>(c), 2 * constants::pi + bank.intake_lobe_center + offset);
            exhaustCam->setLobeCenterline(
                static_cast<int>(c), 2 * constants::pi - bank.exhaust_lobe_center + offset);
        }

        StandardValvetrain *valvetrain = new (std::nothrow) StandardValvetrain;
        if (valvetrain == nullptr) return ES_ERR_OUT_OF_MEMORY;
        parts->valvetrains.push_back(valvetrain);

        StandardValvetrain::Parameters vp;
        vp.intakeCamshaft = intakeCam;
        vp.exhaustCamshaft = exhaustCam;
        valvetrain->initialize(vp);

        /* -- head -- */
        Function *intakeFlow = make_curve(bank.intake_flow, parts);
        Function *exhaustFlow = make_curve(bank.exhaust_flow, parts);
        if (intakeFlow == nullptr || exhaustFlow == nullptr) return ES_ERR_OUT_OF_MEMORY;

        CylinderHead *head = engine->getHead(static_cast<int>(b));
        CylinderHead::Parameters hp;
        hp.Bank = cb;
        hp.IntakePortFlow = intakeFlow;
        hp.ExhaustPortFlow = exhaustFlow;
        hp.Valvetrain = valvetrain;
        hp.CombustionChamberVolume = bank.chamber_volume;
        hp.IntakeRunnerVolume = (bank.intake_runner_volume > 0.0)
            ? bank.intake_runner_volume
            : units::volume(149.6, units::cc);
        hp.IntakeRunnerCrossSectionArea = (bank.intake_runner_cross_section_area > 0.0)
            ? bank.intake_runner_cross_section_area
            : units::distance(2.2, units::inch) * units::distance(2.2, units::inch);
        hp.ExhaustRunnerVolume = (bank.exhaust_runner_volume > 0.0)
            ? bank.exhaust_runner_volume
            : units::volume(50.0, units::cc);
        hp.ExhaustRunnerCrossSectionArea = (bank.exhaust_runner_cross_section_area > 0.0)
            ? bank.exhaust_runner_cross_section_area
            : units::distance(1.75, units::inch) * units::distance(1.75, units::inch);
        hp.FlipDisplay = (b > 0);
        head->initialize(hp);

        for (uint32_t c = 0; c < bank.cylinder_count; ++c) {
            const es_cylinder_def &cyl = bank.cylinders[c];
            const int i = static_cast<int>(c);
            head->setIntake(i, engine->getIntake(0));
            head->setExhaustSystem(
                i, engine->getExhaustSystem(static_cast<int>(cyl.exhaust_system)));
            head->setSoundAttenuation(
                i, (cyl.sound_attenuation > 0.0) ? cyl.sound_attenuation : 1.0);
            head->setHeaderPrimaryLength(i, cyl.primary_length);
        }

        base += bank.cylinder_count;
    }

    /* -- ignition -- */
    Function *timing = make_curve(def->timing_curve, parts);
    if (timing == nullptr) return ES_ERR_OUT_OF_MEMORY;

    IgnitionModule::Parameters igp;
    igp.cylinderCount = static_cast<int>(cylinderCount);
    igp.crankshaft = crank;
    igp.timingCurve = timing;
    igp.revLimit = units::rpm(def->rev_limit);
    igp.limiterDuration =
        (def->rev_limit_duration > 0.0) ? def->rev_limit_duration : 0.5;
    IgnitionModule *ignition = engine->getIgnitionModule();
    ignition->initialize(igp);

    const double cycle = 4 * constants::pi;
    for (uint32_t i = 0; i < cylinderCount; ++i) {
        ignition->setFiringOrder(
            static_cast<int>(def->firing_order[i]), i * (cycle / cylinderCount));
    }

    /* -- fuel and combustion chambers -- */
    Function *turbulence = new (std::nothrow) Function;
    if (turbulence == nullptr) return ES_ERR_OUT_OF_MEMORY;
    turbulence->initialize(30, 1.0);
    for (int i = 0; i < 30; ++i) {
        turbulence->addSample((double)i, i * 0.5);
    }
    parts->functions.push_back(turbulence);

    /* Fuel::flameSpeed() dereferences this unconditionally, and Fuel's default
     * parameters leave it null. Upstream's engine definitions all supply the
     * same curve; it is not worth an ABI knob, so it lives here. */
    Function *flameSpeed = new (std::nothrow) Function;
    if (flameSpeed == nullptr) return ES_ERR_OUT_OF_MEMORY;
    flameSpeed->initialize(10, 5.0);
    flameSpeed->addSample(0.0, 3.0);
    flameSpeed->addSample(5.0, 1.5 * 5.0);
    flameSpeed->addSample(10.0, 1.75 * 10.0);
    for (int i = 15; i <= 45; i += 5) {
        flameSpeed->addSample((double)i, 2.0 * i);
    }
    parts->functions.push_back(flameSpeed);

    Fuel::Parameters fp;
    fp.turbulenceToFlameSpeedRatio = flameSpeed;
    fp.maxBurningEfficiency = 1.0;
    engine->getFuel()->initialize(fp);

    CombustionChamber::Parameters ccp;
    ccp.CrankcasePressure = units::pressure(1.0, units::atm);
    ccp.Fuel = engine->getFuel();
    ccp.StartingPressure = units::pressure(1.0, units::atm);
    ccp.StartingTemperature = units::celcius(25.0);
    ccp.MeanPistonSpeedToTurbulence = turbulence;
    for (int i = 0; i < static_cast<int>(cylinderCount); ++i) {
        ccp.Piston = engine->getPiston(i);
        ccp.Head = engine->getHead(ccp.Piston->getCylinderBank()->getIndex());
        engine->getChamber(i)->initialize(ccp);
    }

    engine->calculateDisplacement();
    return ES_OK;
}

} /* namespace es */
