# SPDX-License-Identifier: GPL-3.0-or-later
# DOA3 NX: Nintendo Switch build of the doa3-re runner (libnx .nro).
#
# Configure with devkitPro's toolchain file and the program the desktop
# build already generated and authenticated (build_game.sh):
#
#   cmake -S switch -B build/switch \
#     -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake \
#     -DRECOMP_PROGRAM_DIR=private/program-XXXXXXXX/generated \
#     -DRECOMP_PROGRAM_MANIFEST_SHA256=<manifest> -DRECOMP_PROGRAM_EBP_EXPECTED=0
#
# switch/build_switch.sh fills these in. The runtime sources are shared with
# recomp-runtime/; only the host layer (video, audio, input, entry point)
# lives in switch/src.
cmake_minimum_required(VERSION 3.20)

project(doa3_nx C CXX ASM)

if(NOT NINTENDO_SWITCH)
    message(FATAL_ERROR "Configure with -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake")
endif()

set(DOA3_NX_VERSION "0.1.0" CACHE STRING "Version shown in the homebrew menu")
set(DOA3_NX_ICON "" CACHE FILEPATH "Optional 256x256 JPEG icon for the .nro")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# newlib hides some POSIX declarations under strict ISO modes.
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(RUNTIME "${CMAKE_CURRENT_SOURCE_DIR}/../recomp-runtime")
set(XBE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../xbe")
set(NX_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src")

# Same code generation rules as the desktop builds (see recomp-runtime):
# IEEE operation order, no strict aliasing, 32-bit wraparound.
add_compile_options(
    $<$<COMPILE_LANGUAGE:C,CXX>:-ffp-contract=off>
    $<$<COMPILE_LANGUAGE:C,CXX>:-fno-strict-aliasing>
    $<$<COMPILE_LANGUAGE:C,CXX>:-fwrapv>
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-unused-parameter>)

set(RECOMP_PROGRAM_DIR "" CACHE PATH "Authenticated complete generated-source snapshot")
# The disc version the program was lifted from (switch/program_info.py).
set(RECOMP_PROGRAM_FORWARDS "program_forwards.c" CACHE STRING "program_forwards.c (USA 3.0) or program_forwards_jp.c (Japan 3.1)")
option(RECOMP_DOA3_JP "The lifted XBE is Japan 3.1 (D3D8 4134)" OFF)
set(RECOMP_PROGRAM_MANIFEST_SHA256 "" CACHE STRING "SHA-256 of the generated-source manifest")
set(RECOMP_PROGRAM_EBP_EXPECTED "" CACHE STRING "Expected EBP prologue override count")
if(NOT RECOMP_PROGRAM_DIR OR NOT RECOMP_PROGRAM_MANIFEST_SHA256)
    message(FATAL_ERROR "RECOMP_PROGRAM_DIR and RECOMP_PROGRAM_MANIFEST_SHA256 are required")
endif()

# ---------------------------------------------------------------------------
# Generated program: authenticate, derive and stub exactly as
# recomp-runtime/CMakeLists.txt does for the desktop runner.
# ---------------------------------------------------------------------------
get_filename_component(_recomp_program_dir "${RECOMP_PROGRAM_DIR}" ABSOLUTE
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
file(GLOB _recomp_program_names LIST_DIRECTORIES false
    RELATIVE "${_recomp_program_dir}"
    "${_recomp_program_dir}/recomp_[0-9][0-9][0-9][0-9].c")
list(SORT _recomp_program_names)
if(NOT _recomp_program_names)
    message(FATAL_ERROR "Authenticated program has no source chunks in ${_recomp_program_dir}")
endif()
set(_chunk_index 0)
foreach(_name IN LISTS _recomp_program_names)
    string(REGEX REPLACE "^recomp_0*([0-9]+)\\.c$" "\\1" _number "${_name}")
    if(NOT _number EQUAL _chunk_index)
        message(FATAL_ERROR "Authenticated program chunk sequence has a gap: ${_name}")
    endif()
    math(EXPR _chunk_index "${_chunk_index} + 1")
endforeach()
list(APPEND _recomp_program_names recomp_dispatch.c recomp_funcs.h)
set(_recomp_program_sources)
set(_recomp_program_manifest "")
foreach(_name IN LISTS _recomp_program_names)
    set(_path "${_recomp_program_dir}/${_name}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Authenticated program member is missing: ${_name}")
    endif()
    file(SIZE "${_path}" _size)
    file(SHA256 "${_path}" _sha256)
    string(APPEND _recomp_program_manifest "${_name}\t${_size}\t${_sha256}\n")
    if(_name MATCHES "\\.c$")
        list(APPEND _recomp_program_sources "${_path}")
    endif()
endforeach()
string(SHA256 _recomp_program_manifest_sha256 "${_recomp_program_manifest}")
string(TOLOWER "${RECOMP_PROGRAM_MANIFEST_SHA256}" _expected_program_manifest_sha256)
if(NOT _recomp_program_manifest_sha256 STREQUAL _expected_program_manifest_sha256)
    message(FATAL_ERROR "RECOMP_PROGRAM_DIR does not match its authenticated manifest")
endif()

set(_ebp_before "\n    uint32_t ebp;\n")
set(_ebp_after "\n    uint32_t ebp = g_seh_ebp;\n")
set(_ebp_growth 12)
if(NOT RECOMP_PROGRAM_EBP_EXPECTED MATCHES "^[0-9]+$")
    message(FATAL_ERROR "RECOMP_PROGRAM_EBP_EXPECTED must be a non-negative integer")
endif()
set(_ebp_applied 0)
set(_recomp_derived_sources)
foreach(_source IN LISTS _recomp_program_sources)
    file(READ "${_source}" _source_text)
    string(LENGTH "${_source_text}" _length_before)
    string(REPLACE "${_ebp_before}" "${_ebp_after}" _derived_text "${_source_text}")
    string(LENGTH "${_derived_text}" _length_after)
    math(EXPR _applied "(${_length_after} - ${_length_before}) / ${_ebp_growth}")
    math(EXPR _ebp_applied "${_ebp_applied} + ${_applied}")
    get_filename_component(_derived_name "${_source}" NAME_WE)
    set(_derived_source "${CMAKE_CURRENT_BINARY_DIR}/${_derived_name}_ebp.c")
    # Rewrite only on change, so reconfiguring does not rebuild every chunk.
    set(_old_text "")
    if(EXISTS "${_derived_source}")
        file(READ "${_derived_source}" _old_text)
    endif()
    if(NOT _old_text STREQUAL _derived_text)
        file(WRITE "${_derived_source}" "${_derived_text}")
    endif()
    list(APPEND _recomp_derived_sources "${_derived_source}")
endforeach()
if(NOT _ebp_applied EQUAL RECOMP_PROGRAM_EBP_EXPECTED)
    message(FATAL_ERROR "Authenticated EBP prologue count is ${_ebp_applied}, "
        "expected ${RECOMP_PROGRAM_EBP_EXPECTED}")
endif()
set(_recomp_program_sources ${_recomp_derived_sources})
configure_file("${_recomp_program_dir}/recomp_funcs.h"
    "${CMAKE_CURRENT_BINARY_DIR}/recomp_funcs.h" COPYONLY)

set(_recomp_program_definitions)
foreach(_source IN LISTS _recomp_program_sources)
    file(READ "${_source}" _source_text)
    string(REGEX MATCHALL
        "void[ \t\r\n]+sub_[0-9A-Fa-f]+[ \t\r\n]*\\([ \t\r\n]*void[ \t\r\n]*\\)"
        _matches "${_source_text}")
    foreach(_match IN LISTS _matches)
        string(REGEX MATCH "sub_[0-9A-Fa-f]+" _name "${_match}")
        list(APPEND _recomp_program_definitions "${_name}")
    endforeach()
endforeach()
foreach(_adapter_source program_adapters.c ${RECOMP_PROGRAM_FORWARDS})
    file(READ "${RUNTIME}/${_adapter_source}" _adapter_text)
    string(REGEX MATCHALL
        "void[ \t\r\n]+sub_[0-9A-Fa-f]+[ \t\r\n]*\\([ \t\r\n]*void[ \t\r\n]*\\)"
        _adapter_matches "${_adapter_text}")
    foreach(_match IN LISTS _adapter_matches)
        string(REGEX MATCH "sub_[0-9A-Fa-f]+" _name "${_match}")
        list(APPEND _recomp_program_definitions "${_name}")
    endforeach()
endforeach()
list(REMOVE_DUPLICATES _recomp_program_definitions)

set(_recomp_program_references)
foreach(_source IN LISTS _recomp_program_sources)
    file(READ "${_source}" _source_text)
    string(REGEX MATCHALL "sub_[0-9A-Fa-f]+[ \t\r\n]*[(),]" _references "${_source_text}")
    foreach(_reference IN LISTS _references)
        string(REGEX MATCH "sub_[0-9A-Fa-f]+" _name "${_reference}")
        list(APPEND _recomp_program_references "${_name}")
    endforeach()
endforeach()
file(GLOB _recomp_hand_sources "${RUNTIME}/*.c" "${RUNTIME}/*.cpp")
list(FILTER _recomp_hand_sources EXCLUDE REGEX "_test\\.(c|cpp)$|runtime_public_fixture\\.c$")
foreach(_source IN LISTS _recomp_hand_sources)
    file(READ "${_source}" _source_text)
    string(REGEX MATCHALL "sub_[0-9A-Fa-f]+[ \t\r\n]*\\(" _references "${_source_text}")
    foreach(_reference IN LISTS _references)
        string(REGEX MATCH "sub_[0-9A-Fa-f]+" _name "${_reference}")
        list(APPEND _recomp_program_references "${_name}")
    endforeach()
endforeach()
list(REMOVE_DUPLICATES _recomp_program_references)
set(_missing_source "${CMAKE_CURRENT_BINARY_DIR}/recomp_program_missing.c")
set(_missing_text "#include <stdint.h>\nvoid recomp_program_missing(uint32_t);\n")
set(_missing_count 0)
foreach(_name IN LISTS _recomp_program_references)
    list(FIND _recomp_program_definitions "${_name}" _defined_index)
    if(_defined_index EQUAL -1)
        string(SUBSTRING "${_name}" 4 8 _address)
        string(APPEND _missing_text
            "void ${_name}(void) { recomp_program_missing(0x${_address}u); }\n")
        math(EXPR _missing_count "${_missing_count} + 1")
    endif()
endforeach()
file(WRITE "${_missing_source}" "${_missing_text}")
message(STATUS "Applied ${_ebp_applied} EBP overrides; ${_missing_count} fail-loud stubs")

# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------
set(_runtime_sources
    doa3_collision_adapter.c runtime.c stop_report.c runner.cpp
    controller_settings.cpp program_adapters.c guest_heap.c ${RECOMP_PROGRAM_FORWARDS}
    xapi_clock.cpp xapi_time_adapter.cpp program_manual.c save_adapter.c
    d3d_creation_model.c d3d_creation_adapter.c d3d_draw_model.c d3d_draw_adapter.c
    d3d_frame_model.c d3d_frame_adapter.c d3d_miniport_adapter.c d3d_vblank.cpp
    d3d_presenter_capture.cpp d3d_vertex_program.cpp
    d3d_render_state_model.c d3d_render_state_adapter.c
    d3d_texture_model.c d3d_texture_adapter.c d3d_tile_model.c d3d_tile_adapter.c
    d3d_vertex_shader_model.c d3d_vertex_shader_adapter.c
    xbox_adpcm.c dsound_service_model.c dsound_service_adapter.c dsound_api_adapter.c
    dsound_hrtf.c cri_adxm_adapter.c custom_music.cpp soundtrack_adapter.c
    movie_color_model.c cri_service_model.c cri_service_adapter.c
    crt_format_model.c crt_format_adapter.c crt_string_adapter.c
    fiber_model.c fiber_adapter.c directory_model.c
    input_model.c input_adapter.c input_pulse_source.c
    symbolic_link_model.c kernel_ordinals.c kernel_dispatch.c kernel_config.c
    kernel_debug.c device_model.c ohci_model.c apu_model.c kernel_device.c
    kernel_interrupt.c kernel_file.c save_transaction.cpp kernel_memory.c
    kernel_rtl.c kernel_crypto.c recomp_math.c kernel_thread.c native_fiber.c
    kernel_video.c host_diagnostics.cpp input_log.c
    costume_inject.c suit_recipe.c suit_joints.c suit_hands.c suit_legs.c suit_skirt.c
    suit_chain.c body_joints.c free_camera.c free_camera_adapter.c scene_freeze.c
    scene_freeze_adapter.c screenshot.c overlay_text.c
    win32_compat.c fiber_switch_arm64.S)
list(TRANSFORM _runtime_sources PREPEND "${RUNTIME}/")

# gl: OpenGL renderer on its own thread (default). probe: no rendering, the
# CPU-only measuring presenter from stage 1.
set(DOA3_NX_PRESENTER "gl" CACHE STRING "Presenter backend: gl or probe")
if(DOA3_NX_PRESENTER STREQUAL "probe")
    set(DOA3_NX_PRESENTER_SOURCES "${NX_SRC}/d3d_presenter_switch_probe.cpp")
    set(DOA3_NX_PRESENTER_LIBS)
else()
    set(DOA3_NX_PRESENTER_SOURCES
        "${NX_SRC}/d3d_presenter_gl.cpp"
        "${NX_SRC}/d3d_presenter_gl_thread.cpp")
    # devkitPro switch-mesa and switch-glad (switch-portlibs).
    set(DOA3_NX_PRESENTER_LIBS glad EGL glapi drm_nouveau)
endif()

add_executable(doa3-nx
    ${_runtime_sources}
    "${NX_SRC}/nx_main.cpp"
    "${NX_SRC}/switch_shims.c"
    "${NX_SRC}/boot_trace.cpp"
    "${NX_SRC}/crash_report.cpp"
    "${NX_SRC}/profile.cpp"
    "${NX_SRC}/input_host_switch.c"
    "${NX_SRC}/audio_output_switch.cpp"
    "${NX_SRC}/doa3_nx_math.c"
    ${DOA3_NX_PRESENTER_SOURCES}
    "${_missing_source}"
    ${_recomp_program_sources}
    "${XBE_DIR}/sha.cpp"
    "${XBE_DIR}/xbe_parser.cpp")

target_compile_definitions(doa3-nx PRIVATE
    RECOMP_FULL_PROGRAM
    RECOMP_D3D_FRAME_ENABLED
    $<$<BOOL:${RECOMP_DOA3_JP}>:RECOMP_DOA3_JP_BINDINGS>
    DOA3_NX_VERSION="${DOA3_NX_VERSION}")
# switch/include comes first: it supplies the POSIX headers newlib lacks.
target_include_directories(doa3-nx BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${RUNTIME}"
    "${XBE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}")
set_source_files_properties("${RUNTIME}/runner.cpp" PROPERTIES
    COMPILE_DEFINITIONS "main=recomp_runner_main")
# The generated chunks are ~140k lines each; -O2 keeps the build time sane
# while still giving the register allocation the lifted code needs.
set(DOA3_NX_GENERATED_OPT "-O2" CACHE STRING "Optimization level of the lifted program chunks")
option(DOA3_NX_FUNC_PROFILE "Time every lifted function (diagnostic build, slower)" OFF)
if(DOA3_NX_FUNC_PROFILE)
    # Runtime headers' inline helpers (guest memory access...) stay untimed.
    set(_func_profile_flags ";-finstrument-functions;-finstrument-functions-exclude-file-list=recomp-runtime,devkitpro,include")
    target_compile_definitions(doa3-nx PRIVATE DOA3_NX_FUNC_PROFILE)
else()
    set(_func_profile_flags "")
endif()
# doa3_nx_native.h routes direct calls to the hottest math helpers to the
# native versions in src/doa3_nx_math.c (DOA3_NX_NATIVE=OFF keeps the lifted ones).
option(DOA3_NX_NATIVE "Native versions of DOA3's hottest math helpers" ON)
if(DOA3_NX_NATIVE)
    set(_native_flags ";-include;${CMAKE_CURRENT_SOURCE_DIR}/include/doa3_nx_native.h")
else()
    set(_native_flags "")
    target_compile_definitions(doa3-nx PRIVATE DOA3_NX_NATIVE_OFF)
endif()
# Profile-guided optimization of the lifted code, in two builds:
#   GEN  instruments it; playing writes the profile to sdmc:/switch/doa3-nx/pgo
#        (an absolute path: GCC prefixes a relative -fprofile-dir, and "sdmc:"
#        counts as relative, with the build directory; newlib maps / to sdmc:)
#   USE  rebuilds it with the profile copied back to DOA3_NX_PGO_DIR
set(DOA3_NX_PGO "OFF" CACHE STRING "Profile-guided optimization: OFF, GEN or USE")
set(DOA3_NX_PGO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../private/pgo" CACHE PATH "Profile for DOA3_NX_PGO=USE")
set(_pgo_flags "")
if(DOA3_NX_PGO STREQUAL "GEN")
    set(_pgo_flags ";-fprofile-generate;-fprofile-update=single;-fprofile-dir=/switch/doa3-nx/pgo")
    target_link_options(doa3-nx PRIVATE "-fprofile-generate")
    target_compile_definitions(doa3-nx PRIVATE DOA3_NX_PGO_GEN)
elseif(DOA3_NX_PGO STREQUAL "USE")
    set(_pgo_flags ";-fprofile-use;-fprofile-partial-training;-Wno-missing-profile;-fprofile-dir=${DOA3_NX_PGO_DIR}")
endif()
set_source_files_properties(${_recomp_program_sources} PROPERTIES
    COMPILE_OPTIONS "${DOA3_NX_GENERATED_OPT};-w${_func_profile_flags}${_native_flags}${_pgo_flags}")
# Logged at boot, so a log tells which build produced it.
set_property(SOURCE "${NX_SRC}/nx_main.cpp" APPEND PROPERTY COMPILE_DEFINITIONS
    "DOA3_NX_BUILD_INFO=\"pgo=${DOA3_NX_PGO} opt=${DOA3_NX_GENERATED_OPT} native=${DOA3_NX_NATIVE} profile=${DOA3_NX_FUNC_PROFILE}\"")
# x87 rounding: no fused multiply-add in the native math.
set_source_files_properties("${NX_SRC}/doa3_nx_math.c" PROPERTIES COMPILE_OPTIONS "-O2;-ffp-contract=off")
# Direct (not GOT) access to globals from C, and libm calls such as sqrt
# inlined as instructions: errno is never read after them.
target_compile_options(doa3-nx PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/include/doa3_nx_visibility.h>"
    "$<$<COMPILE_LANGUAGE:C>:-fno-math-errno>")
target_link_options(doa3-nx PRIVATE "-Wl,--wrap=exit" "-Wl,--wrap=abort" "-Wl,--wrap=setvbuf" "-Wl,--wrap=pthread_create"
    "-Wl,--wrap=recomp_install_host_diagnostics" "-Wl,--wrap=recomp_save_initialize"
    "-Wl,--wrap=recomp_runtime_init" "-Wl,--wrap=recomp_music_initialize"
    "-Wl,--wrap=recomp_dispatch"
    "-Wl,--wrap=recomp_lookup_manual" "-Wl,--wrap=recomp_lookup_kernel"
    "-Wl,--wrap=recomp_program_lookup" "-Wl,--wrap=recomp_native_fiber_switch"
    "-Wl,--wrap=nanosleep" "-Wl,--wrap=recomp_stop_note_kernel_call" "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/doa3-nx.map")
target_include_directories(doa3-nx SYSTEM PRIVATE "${DEVKITPRO}/portlibs/switch/include")
target_link_libraries(doa3-nx PRIVATE ${DOA3_NX_PRESENTER_LIBS} nx m)

nx_generate_nacp(OUTPUT doa3-nx.nacp
    NAME "DOA3 NX"
    AUTHOR "Toniisound"
    VERSION "${DOA3_NX_VERSION}")
if(DOA3_NX_ICON)
    nx_create_nro(doa3-nx NACP doa3-nx.nacp ICON "${DOA3_NX_ICON}")
else()
    nx_create_nro(doa3-nx NACP doa3-nx.nacp)
endif()
