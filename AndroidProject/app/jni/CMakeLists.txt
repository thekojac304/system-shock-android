cmake_minimum_required(VERSION 3.22.1)
project(rp5np_systemshock_android LANGUAGES C CXX)

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)

file(TO_CMAKE_PATH "$ENV{RP5NP_SDL2_SOURCE_DIR}" SDL2_SOURCE_DIR)
file(TO_CMAKE_PATH "$ENV{RP5NP_SDL2_MIXER_SOURCE_DIR}" SDL2_MIXER_SOURCE_DIR)

if(NOT EXISTS "${SDL2_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "SDL2 source not found: ${SDL2_SOURCE_DIR}")
endif()
if(NOT EXISTS "${SDL2_MIXER_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "SDL2_mixer source not found: ${SDL2_MIXER_SOURCE_DIR}")
endif()

get_filename_component(RP5NP_SHOCKOLATE_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../../SourceSnapshot" ABSOLUTE)

add_subdirectory("${SDL2_SOURCE_DIR}" d)

set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(SDL2MIXER_VENDORED ON CACHE BOOL "" FORCE)
set(SDL2MIXER_DEPS_SHARED OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_CMD OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_FLAC OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_GME OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MOD OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MP3 ON CACHE BOOL "" FORCE)
set(SDL2MIXER_MP3_MINIMP3 ON CACHE BOOL "" FORCE)
set(SDL2MIXER_MP3_MPG123 OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MIDI OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_OPUS OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_VORBIS "" CACHE STRING "" FORCE)
set(SDL2MIXER_WAVE ON CACHE BOOL "" FORCE)
set(SDL2MIXER_WAVPACK OFF CACHE BOOL "" FORCE)
add_subdirectory("${SDL2_MIXER_SOURCE_DIR}" m)

# Do not leak SDL2_mixer's shared-library policy into Shockolate libraries.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(ENABLE_SDL2 ON CACHE STRING "" FORCE)
set(ENABLE_SOUND ON CACHE STRING "" FORCE)
set(ENABLE_FLUIDSYNTH OFF CACHE STRING "" FORCE)
set(ENABLE_OPENGL OFF CACHE BOOL "" FORCE)
set(ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory("${RP5NP_SHOCKOLATE_SOURCE_DIR}" s)

# Android-only touch adapter. Keep gestures and the Shock bridge outside the
# preserved engine source tree; its three consumers only see this narrow API.
target_sources(main PRIVATE touch/touch_controls.c touch/touch_shock_bridge.c touch/touch_surface.c
    touch/touch_settings.c touch/touch_editor.c touch/touch_render.c touch/touch_radial.c)
foreach(touch_consumer main GAME_LIB INPUT_LIB)
    target_include_directories(${touch_consumer} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/touch")
endforeach()

# QA commands are linked only into the separate QA application. Runtime opt-in
# remains OFF even in that application.
option(RP5NP_QA_TOOLS "Build touch-accessible QA commands" OFF)
if(RP5NP_QA_TOOLS)
    target_sources(GAME_LIB PRIVATE touch/qa_tools.c)
    foreach(qa_consumer main GAME_LIB)
        target_compile_definitions(${qa_consumer} PRIVATE RP5NP_QA_TOOLS=1)
    endforeach()
endif()

# TEMPORARY AUDLIFE: opt-in diagnostic build, no playback policy changes.
option(RP5NP_AUDIO_DIAGNOSTICS "Trace Android audio lifecycle and mixer timing" OFF)
if(RP5NP_AUDIO_DIAGNOSTICS)
    foreach(audio_consumer main GAME_LIB)
        target_compile_definitions(${audio_consumer} PRIVATE RP5NP_AUDIO_DIAGNOSTICS=1)
        target_include_directories(${audio_consumer} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/audio_diag")
    endforeach()
    include(audio_diag/instrument.cmake)
endif()

# Optional GLES world backend is isolated to the existing QA application.
if(RP5NP_QA_TOOLS)
    target_sources(main PRIVATE
        "${RP5NP_SHOCKOLATE_SOURCE_DIR}/src/MacSrc/GpuWorld.c"
        "${RP5NP_SHOCKOLATE_SOURCE_DIR}/src/MacSrc/GpuGenerated.c"
        "${RP5NP_SHOCKOLATE_SOURCE_DIR}/src/MacSrc/GpuTextureCache.c")
    target_link_libraries(main GLESv3 EGL)
    foreach(gpu_consumer main GAME_LIB 3D_LIB INPUT_LIB)
        target_compile_definitions(${gpu_consumer} PRIVATE RP5NP_GPU_WORLD=1)
    endforeach()
endif()
