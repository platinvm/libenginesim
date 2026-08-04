/* Internal: the impulse response used when the host supplies none. */
#ifndef ES_BUILTIN_IR_H
#define ES_BUILTIN_IR_H

#include <stdint.h>

namespace es {

extern const int16_t builtin_impulse_response[];
extern const uint32_t builtin_impulse_response_frames;
extern const double builtin_impulse_response_volume;

} /* namespace es */

#endif /* ES_BUILTIN_IR_H */
