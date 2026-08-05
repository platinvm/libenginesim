/* Internal: assembles an upstream Engine from a typed es_engine_def. */
#ifndef ES_ENGINE_BUILDER_H
#define ES_ENGINE_BUILDER_H

#include "enginesim.h"

#include <vector>

class Camshaft;
class Engine;
class Function;
class ImpulseResponse;
class Throttle;
class Valvetrain;

namespace es {

/*
 * Owns every heap object upstream's Engine refers to but does not itself free:
 * flow curves, cam profiles, the timing curve, camshafts, valvetrains and the
 * per-exhaust impulse response records.
 *
 * The throttle linkage is deliberately absent - Engine::destroy() deletes it.
 */
struct EngineParts {
    std::vector<Function *> functions;
    std::vector<ImpulseResponse *> impulseResponses;
    std::vector<Camshaft *> camshafts;
    std::vector<Valvetrain *> valvetrains;

    ~EngineParts();
};

/* Validates `def` against the rules documented on es_sim_create. */
es_result validate(const es_engine_def *def);

/*
 * Builds `engine` from `def`. `def` must already have passed validate().
 * Anything allocated along the way is recorded in `parts`, which the caller
 * must keep alive for as long as the engine and destroy afterwards.
 */
es_result build_engine(const es_engine_def *def, Engine *engine, EngineParts *parts);

/* Reproduces upstream's harmonic cam lobe generator. */
Function *make_cam_lobe(const es_cam_lobe &lobe, EngineParts *parts);

/* Builds a Function from a sampled curve. */
Function *make_curve(const es_curve &curve, EngineParts *parts);

} /* namespace es */

#endif /* ES_ENGINE_BUILDER_H */
