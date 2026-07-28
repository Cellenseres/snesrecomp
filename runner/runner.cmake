# runner.cmake — shared source list for snesrecomp game projects.
#
# Usage from a game project's CMakeLists.txt:
#   set(SNESRECOMP_ROOT ${CMAKE_SOURCE_DIR}/snesrecomp)
#   include(${SNESRECOMP_ROOT}/runner/runner.cmake)
#   add_executable(MyGame ${SNESRECOMP_RUNNER_SOURCES} <game sources> <generated sources>)
#   target_include_directories(MyGame PRIVATE ${SNESRECOMP_RUNNER_INCLUDE_DIRS} ...)
#
# Mirrors the file list in the MSVC .vcxproj so the same sources build on
# Windows (MSVC) and on macOS/Linux (clang/gcc + CMake). The snes9x emulator
# oracle (snes9x_bridge.cpp / ENABLE_ORACLE_BACKEND) is intentionally NOT part
# of this list — it is a developer-only verify backend, off for normal builds.

set(SNESRECOMP_RUNNER_ROOT ${CMAKE_CURRENT_LIST_DIR})

set(SNESRECOMP_RUNNER_SOURCES
    ${SNESRECOMP_RUNNER_ROOT}/src/common_cpu_infra.c
    ${SNESRECOMP_RUNNER_ROOT}/src/common_rtl.c
    ${SNESRECOMP_RUNNER_ROOT}/src/widescreen.c
    ${SNESRECOMP_RUNNER_ROOT}/src/recomp_hw.c
    ${SNESRECOMP_RUNNER_ROOT}/src/framedump.c
    ${SNESRECOMP_RUNNER_ROOT}/src/host_paths.c
    ${SNESRECOMP_RUNNER_ROOT}/src/launcher.c
    ${SNESRECOMP_RUNNER_ROOT}/src/crc32.c
    ${SNESRECOMP_RUNNER_ROOT}/src/sha256.c
    ${SNESRECOMP_RUNNER_ROOT}/src/keybinds.c
    ${SNESRECOMP_RUNNER_ROOT}/src/cpu_state.c
    ${SNESRECOMP_RUNNER_ROOT}/src/cpu_trace.c
    ${SNESRECOMP_RUNNER_ROOT}/src/audio_trace.c
    ${SNESRECOMP_RUNNER_ROOT}/src/ppu_dma_trace.c
    ${SNESRECOMP_RUNNER_ROOT}/src/host_report.c
    ${SNESRECOMP_RUNNER_ROOT}/src/execution_mode.c
    ${SNESRECOMP_RUNNER_ROOT}/src/util.c
    # SNES hardware model
    ${SNESRECOMP_RUNNER_ROOT}/src/snes/apu.c
    ${SNESRECOMP_RUNNER_ROOT}/src/snes/cart.c
    ${SNESRECOMP_RUNNER_ROOT}/src/snes/cpu.c
    ${SNESRECOMP_RUNNER_ROOT}/src/snes/dma.c
    ${SNESRECOMP_RUNNER_ROOT}/src/snes/dsp.c
    ${SNESRECOMP_RUNNER_ROOT}/src/snes/audio_shadow.c
    ${SNESRECOMP_RUNNER_ROOT}/src/snes/dsp_shadow.c
    ${SNESRECOMP_RUNNER_ROOT}/src/snes/msu1.c
    ${SNESRECOMP_RUNNER_ROOT}/src/snes/color_lut.c
    ${SNESRECOMP_RUNNER_ROOT}/src/snes/ppu.c
    ${SNESRECOMP_RUNNER_ROOT}/src/snes/ppu_legacy.c
    ${SNESRECOMP_RUNNER_ROOT}/src/snes/ws_shadow.c
    ${SNESRECOMP_RUNNER_ROOT}/src/snes/snes.c
    ${SNESRECOMP_RUNNER_ROOT}/src/snes/snes_other.c
    ${SNESRECOMP_RUNNER_ROOT}/src/snes/spc.c
    ${SNESRECOMP_RUNNER_ROOT}/src/snes/superfx.c
    # Interpreter-fallback tier (docs/MULTI_TIER.md): LakeSnes core + bridge.
    ${SNESRECOMP_RUNNER_ROOT}/src/snes/interp816.c
    ${SNESRECOMP_RUNNER_ROOT}/src/snes/interp_bridge.c
)

# ── Capcom Cx4 coprocessor (Mega Man X2 / X3 only) ────────────────────────
# cx4.c is an instruction-level Hitachi HG51B S169 core ported from ares
# (ISC licence — permissive, notice-only). It is the faithful LLE floor; any
# future host-side Cx4 shortcut must be a gated optimization layered ON TOP of
# it, authored from this core's observed behavior.
#
list(APPEND SNESRECOMP_RUNNER_SOURCES
    ${SNESRECOMP_RUNNER_ROOT}/src/snes/cx4.c)
message(STATUS "Cx4: instruction-level HG51B S169 core (ares, ISC)")

# The TCP debug server + emulator-oracle command handlers are a developer-only
# feature. debug_server.h provides static-inline no-op stubs when SNESRECOMP_TRACE
# is 0 (the default), so debug_server.c must only be compiled when tracing is on —
# otherwise the real definitions collide with the header stubs. Off by default for
# a normal playable build; opt in with -DSNESRECOMP_ENABLE_TRACE=ON.
option(SNESRECOMP_ENABLE_TRACE "Build the TCP debug server / observability rings" OFF)
if(SNESRECOMP_ENABLE_TRACE)
    list(APPEND SNESRECOMP_RUNNER_SOURCES
        ${SNESRECOMP_RUNNER_ROOT}/src/debug_server.c
    )
    if(EXISTS ${SNESRECOMP_RUNNER_ROOT}/src/emu_oracle_cmds.c)
        list(APPEND SNESRECOMP_RUNNER_SOURCES
            ${SNESRECOMP_RUNNER_ROOT}/src/emu_oracle_cmds.c
        )
    endif()
endif()

# Schema-driven mod packages and trusted static plugins. This is deliberately
# opt-in: ordinary games do not compile the loader, expose a Mods navigation
# item, create a mods directory, or change any runtime behavior. A game that
# opts in owns its recomp-ui pin, package catalog, and statically linked plugin
# implementations.
option(SNESRECOMP_ENABLE_MODS
    "Build the SNES mod package loader and trusted-plugin runtime"
    OFF)
if(SNESRECOMP_ENABLE_MODS)
    list(APPEND SNESRECOMP_RUNNER_SOURCES
        ${SNESRECOMP_RUNNER_ROOT}/src/mod_runtime.cpp
    )
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    add_compile_definitions(SNESRECOMP_ENABLE_MODS=1)
    # recomp-ui still requires a non-null provider at runtime. This enables
    # only the UI half of that double gate for the explicitly opting-in game.
    set(RECOMP_UI_ENABLE_MODS ON CACHE BOOL
        "Enable recomp-ui Mods view for this SNES mod-enabled game" FORCE)
    message(STATUS
        "SNES mods: package loader + trusted static plugins enabled")
else()
    add_compile_definitions(SNESRECOMP_ENABLE_MODS=0)
endif()

set(SNESRECOMP_RUNNER_LIBRARIES)
if(NOT WIN32)
    # cx4.c synthesizes its internal data ROM with libm.
    list(APPEND SNESRECOMP_RUNNER_LIBRARIES m)
endif()
if(SNESRECOMP_ENABLE_TRACE AND WIN32)
    list(APPEND SNESRECOMP_RUNNER_LIBRARIES ws2_32)
endif()

# Differential co-simulation (SNES_COSIM.md): full-state first-divergence oracle.
# DEV/DIAGNOSTICS ONLY — must NEVER be enabled in a shipping Production config.
# Adds the frame-keyed park/step engine (cosim.c) + canonical state hash
# (cosim_state.c) + a loopback TCP server; needs ws2_32 on Windows. Defines
# SNES_COSIM for every target configured after this include (the game exe).
option(SNES_COSIM "Build the differential co-simulation engine (DEV ONLY)" OFF)
if(SNES_COSIM)
    list(APPEND SNESRECOMP_RUNNER_SOURCES
        ${SNESRECOMP_RUNNER_ROOT}/src/cosim.c
        ${SNESRECOMP_RUNNER_ROOT}/src/cosim_state.c
    )
    add_compile_definitions(SNES_COSIM)
    if(WIN32)
        list(APPEND SNESRECOMP_RUNNER_LIBRARIES ws2_32)
    endif()
    message(STATUS "SNES_COSIM enabled — DEV co-simulation build (not for release)")
endif()

set(SNESRECOMP_RUNNER_INCLUDE_DIRS
    ${SNESRECOMP_RUNNER_ROOT}/src
    ${SNESRECOMP_RUNNER_ROOT}/src/snes
)

# Optional desktop GLSL preset renderer. It deliberately stays out of
# SNESRECOMP_RUNNER_SOURCES because headless tools and non-OpenGL frontends do
# not carry the game-owned gl_core/stb/config dependencies it consumes.
function(snesrecomp_target_glsl_shader target)
    target_sources(${target} PRIVATE
        ${SNESRECOMP_RUNNER_ROOT}/src/desktop/glsl_shader.c)
    target_include_directories(${target} PRIVATE
        ${SNESRECOMP_RUNNER_ROOT}/src/desktop)
    if(NOT MSVC)
        target_link_libraries(${target} PRIVATE m)
    endif()
endfunction()

# Shared configuration/keybinding implementation used by the Mega Man X
# trilogy hosts. Kept opt-in because its Config structure and INI grammar are
# intentionally game-facing rather than part of the core runner ABI.
function(snesrecomp_target_mmx_config target)
    target_sources(${target} PRIVATE
        ${SNESRECOMP_RUNNER_ROOT}/src/desktop/mmx_config.c)
    target_include_directories(${target} PRIVATE
        ${SNESRECOMP_RUNNER_ROOT}/src/desktop)
endfunction()

# Shared crash/exit report serializer. Games with interpreter fallback coverage
# can opt into the additional standalone Tier-2 manifest.
function(snesrecomp_target_post_mortem target)
    set(options TIER2)
    cmake_parse_arguments(PM "${options}" "" "" ${ARGN})
    target_sources(${target} PRIVATE
        ${SNESRECOMP_RUNNER_ROOT}/src/desktop/post_mortem.c)
    target_include_directories(${target} PRIVATE
        ${SNESRECOMP_RUNNER_ROOT}/src/desktop)
    if(PM_TIER2)
        target_compile_definitions(${target} PRIVATE
            SNESRECOMP_POST_MORTEM_TIER2=1)
    endif()
    if(WIN32)
        target_link_libraries(${target} PRIVATE dbghelp)
    endif()
endfunction()

# Win32 Fiber API compatibility for non-Windows cooperative schedulers.
function(snesrecomp_target_fiber_compat target)
    target_sources(${target} PRIVATE
        ${SNESRECOMP_RUNNER_ROOT}/src/desktop/fiber_compat.c)
    target_include_directories(${target} PRIVATE
        ${SNESRECOMP_RUNNER_ROOT}/src/desktop)
endfunction()

# Optional delay-sync netcode (lib/recomp-net submodule). See docs/RECOMP_NET.md.
# Does nothing unless SNESRECOMP_ENABLE_NET=ON or the game calls
# snesrecomp_enable_recomp_net(<target>).
include(${SNESRECOMP_RUNNER_ROOT}/recomp_net.cmake)

# Dear ImGui pre-boot launcher is NOT vendored here. Games that need it add
# mstan/recomp-ui as a repo-root submodule and call recomp_target_launcher_ui()
# themselves (see docs/LAUNCHER_DESIGN.md).
