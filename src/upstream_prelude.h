/*
 * Force-included ahead of every upstream translation unit.
 *
 * Upstream relies on MSVC's implicit includes and on MSVC deferring the
 * parsing of uninstantiated templates. Supplying these here keeps the fixes
 * out of the patch series: nothing below changes upstream's code, it only
 * makes declarations visible that MSVC happened to provide anyway.
 */
#ifndef ES_UPSTREAM_PRELUDE_H
#define ES_UPSTREAM_PRELUDE_H

/* <math.h> rather than <cmath>: synthesizer.cpp calls fpclassify unqualified,
 * which only resolves against the global-namespace macro. */
#include <math.h>
#include <cmath>
#include <cstdlib>  /* audio_buffer.cpp: std::abs */
#include <cstring>

/* sparse_matrix.h calls into a forward-declared Matrix from inline template
 * bodies, and sparse_matrix.cpp includes it before matrix.h. */
#include <simple-2d-constraint-solver/include/matrix.h>

#endif /* ES_UPSTREAM_PRELUDE_H */
