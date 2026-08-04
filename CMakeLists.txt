cmake_minimum_required(VERSION 3.16)
project(enginesim C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(UPSTREAM ${CMAKE_CURRENT_SOURCE_DIR}/vendor/engine-sim)
set(SOLVER_REL dependencies/submodules/simple-2d-constraint-solver)

if(NOT EXISTS ${UPSTREAM}/CMakeLists.txt)
    message(FATAL_ERROR
        "vendor/engine-sim is empty. Run:\n"
        "  git submodule update --init vendor/engine-sim\n"
        "  git -C vendor/engine-sim submodule update --init ${SOLVER_REL}")
endif()
if(NOT EXISTS ${UPSTREAM}/${SOLVER_REL}/CMakeLists.txt)
    message(FATAL_ERROR
        "The constraint solver is missing. Run:\n"
        "  git -C vendor/engine-sim submodule update --init ${SOLVER_REL}")
endif()

# ---------------------------------------------------------------------------
# Upstream needs six one-line portability fixes to build outside MSVC. Copy it
# into the build tree and patch the copy, so the submodule stays pristine.
# ---------------------------------------------------------------------------
set(PATCHED ${CMAKE_BINARY_DIR}/upstream)
set(PATCH_FILE ${CMAKE_CURRENT_SOURCE_DIR}/patches/0001-portability.patch)
set(PATCH_STAMP ${CMAKE_BINARY_DIR}/upstream.stamp)

if(NOT EXISTS ${PATCH_STAMP} OR ${PATCH_FILE} IS_NEWER_THAN ${PATCH_STAMP})
    message(STATUS "Staging patched upstream sources")
    file(REMOVE_RECURSE ${PATCHED})
    file(MAKE_DIRECTORY ${PATCHED})
    file(COPY ${UPSTREAM}/include ${UPSTREAM}/src DESTINATION ${PATCHED})
    file(COPY ${UPSTREAM}/${SOLVER_REL}/include ${UPSTREAM}/${SOLVER_REL}/src
         DESTINATION ${PATCHED}/${SOLVER_REL})
    # GIT_CEILING_DIRECTORIES stops git discovering the enclosing repository.
    # Without it, git apply notices these files are under an ignored directory,
    # skips every hunk and still exits 0.
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env
                GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}
                git apply -p1 ${PATCH_FILE}
        WORKING_DIRECTORY ${PATCHED}
        RESULT_VARIABLE patch_result
        ERROR_VARIABLE patch_error)
    if(NOT patch_result EQUAL 0)
        message(FATAL_ERROR "Failed to apply ${PATCH_FILE}:\n${patch_error}")
    endif()

    # Exit status alone is not evidence the patch landed, so check for it.
    file(READ ${PATCHED}/include/constants.h _constants)
    if(NOT _constants MATCHES "inline constexpr double pi")
        message(FATAL_ERROR
            "${PATCH_FILE} reported success but changed nothing. The upstream "
            "submodule has probably moved; regenerate the patch.")
    endif()

    file(WRITE ${PATCH_STAMP} "ok")
endif()

# Upstream's audio file includes delta-studio headers but references no symbol
# from them. Two empty stubs satisfy the include so the graphics submodule is
# never needed.
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/compat/delta-studio/include)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/compat/delta-studio/engines/basic/include)
file(TOUCH ${CMAKE_BINARY_DIR}/compat/delta-studio/include/yds_core.h)
file(TOUCH ${CMAKE_BINARY_DIR}/compat/delta-studio/engines/basic/include/delta_basic_engine.h)

# The physics and audio subset. Everything else upstream ships is rendering,
# UI, scripting or video capture.
set(ES_CORE
    audio_buffer camshaft crankshaft combustion_chamber connecting_rod
    convolution_filter cylinder_bank cylinder_head delay_filter
    derivative_filter direct_throttle_linkage dynamometer engine exhaust_system
    feedback_comb_filter filter fuel function gas_system gaussian_filter
    governor ignition_module impulse_response intake jitter_filter
    leveling_filter low_pass_filter part piston piston_engine_simulator
    simulator standard_valvetrain starter_motor synthesizer throttle
    transmission utilities valvetrain vehicle vehicle_drag_constraint
    vtec_valvetrain)
list(TRANSFORM ES_CORE PREPEND ${PATCHED}/src/)
list(TRANSFORM ES_CORE APPEND .cpp)

file(GLOB SOLVER_SRC ${PATCHED}/${SOLVER_REL}/src/*.cpp)

add_library(enginesim STATIC
    ${ES_CORE}
    ${SOLVER_SRC}
    src/enginesim.cpp
    src/engine_builder.cpp
    src/presets.cpp
    src/builtin_ir.cpp)

target_include_directories(enginesim
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${PATCHED}/include
            ${PATCHED}
            ${PATCHED}/dependencies/submodules
            ${CMAKE_BINARY_DIR}/compat)

# __forceinline is an MSVC keyword; the prelude supplies headers upstream
# forgot to include and orders one that must precede another.
target_compile_options(enginesim PRIVATE
    -D__forceinline=inline
    -include ${CMAKE_CURRENT_SOURCE_DIR}/src/upstream_prelude.h)

# Upstream is not warning-clean and is not ours to fix.
target_compile_options(enginesim PRIVATE -w)

if(EMSCRIPTEN)
    # One self-contained .js with the wasm inlined as base64: the AudioWorklet
    # global scope has no fetch(), so nothing may be loaded at runtime.
    add_executable(enginesim_wasm src/wasm_exports.cpp)
    target_link_libraries(enginesim_wasm PRIVATE enginesim)
    target_link_options(enginesim_wasm PRIVATE
        -sSINGLE_FILE=1
        -sMODULARIZE=1
        -sEXPORT_ES6=1
        -sEXPORT_NAME=createEngineSim
        -sENVIRONMENT=web,worker
        -sALLOW_MEMORY_GROWTH=1
        -sFILESYSTEM=0
        -sEXPORTED_RUNTIME_METHODS=['HEAPF32','HEAPU8','HEAPU32','HEAPF64']
        -O3)
    set_target_properties(enginesim_wasm PROPERTIES
        OUTPUT_NAME enginesim
        SUFFIX .mjs
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bindings/js)
else()
    add_executable(enginesim_smoke tests/smoke.c)
    target_link_libraries(enginesim_smoke PRIVATE enginesim)
    enable_testing()
    add_test(NAME smoke COMMAND enginesim_smoke)
endif()
