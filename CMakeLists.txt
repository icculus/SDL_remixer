# This is a temporary CMake project. We'll presumably update the real SDL_mixer
# CMake and throw most of this away, when we get to that point.

cmake_minimum_required(VERSION 3.12.0)
project(SDL_remixer)

find_package(SDL3 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPUS REQUIRED opus)

# Yes I know it says STATIC.
add_library(SDL_remixer STATIC
    src/decoder_aiff.c
    src/decoder_drflac.c
    src/decoder_drmp3.c
    src/decoder_fluidsynth.c
    src/decoder_opus.c
    src/decoder_raw.c
    src/decoder_sinewave.c
    src/decoder_voc.c
    src/decoder_vorbis.c
    src/decoder_wav.c
    src/decoder_wavpack.c
    src/SDL_mixer.c
    src/SDL_mixer_metadata_tags.c
)
target_include_directories(SDL_remixer PRIVATE ${SDL3_INCLUDE_DIRS})
target_include_directories(SDL_remixer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(SDL_remixer PRIVATE ${OPUS_INCLUDE_DIRS})
target_compile_definitions(SDL_remixer PRIVATE VORBIS_DYNAMIC="libvorbisfile.so.3")
target_compile_definitions(SDL_remixer PRIVATE OPUS_DYNAMIC="libopusfile.so.0")
target_compile_definitions(SDL_remixer PRIVATE WAVPACK_DYNAMIC="libwavpack.so.1")
target_compile_definitions(SDL_remixer PRIVATE FLUIDSYNTH_DYNAMIC="libfluidsynth.so.3")



add_executable(testmixer src/testmixer.c)
target_link_libraries(testmixer PRIVATE SDL_remixer)
target_link_libraries(testmixer PRIVATE ${SDL3_LIBRARIES})
target_include_directories(testmixer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(testmixer PRIVATE ${SDL3_INCLUDE_DIRS})
