# This is a temporary CMake project. We'll presumably update the real SDL_mixer
# CMake and throw most of this away, when we get to that point.

cmake_minimum_required(VERSION 3.12.0)
project(SDL_remixer LANGUAGES C)

find_package(SDL3 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FLUIDSYNTH REQUIRED fluidsynth)
pkg_check_modules(OPUS REQUIRED opus)
pkg_check_modules(VORBISFILE REQUIRED vorbisfile)
pkg_check_modules(FLAC REQUIRED flac)
pkg_check_modules(WAVPACK REQUIRED wavpack)
pkg_check_modules(GME REQUIRED libgme)
pkg_check_modules(XMP REQUIRED libxmp)
pkg_check_modules(MPG123 REQUIRED libmpg123)

# Yes I know it says STATIC.
add_library(SDL_remixer STATIC
    src/timidity/common.c
    src/timidity/instrum.c
    src/timidity/mix.c
    src/timidity/output.c
    src/timidity/playmidi.c
    src/timidity/readmidi.c
    src/timidity/resample.c
    src/timidity/tables.c
    src/timidity/timidity.c
    src/decoder_aiff.c
    src/decoder_drflac.c
    src/decoder_drmp3.c
    src/decoder_flac.c
    src/decoder_fluidsynth.c
    src/decoder_gme.c
    src/decoder_mpg123.c
    src/decoder_opus.c
    src/decoder_raw.c
    src/decoder_sinewave.c
    src/decoder_voc.c
    src/decoder_vorbis.c
    src/decoder_stb_vorbis.c
    src/decoder_wav.c
    src/decoder_wavpack.c
    src/decoder_xmp.c
    src/decoder_timidity.c
    src/SDL_mixer.c
    src/SDL_mixer_metadata_tags.c
)
if ("c_std_99" IN_LIST CMAKE_C_COMPILE_FEATURES)
    target_compile_features(SDL_remixer PRIVATE c_std_99)
else()
    message(WARNING "target_compile_features does not know c_std_99 for C compiler")
endif()
target_link_libraries(SDL_remixer PRIVATE SDL3::Headers)
target_include_directories(SDL_remixer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(SDL_remixer PRIVATE ${FLUIDSYNTH_INCLUDE_DIRS})
target_include_directories(SDL_remixer PRIVATE ${OPUS_INCLUDE_DIRS})
target_include_directories(SDL_remixer PRIVATE ${VORBISFILE_INCLUDE_DIRS})
target_include_directories(SDL_remixer PRIVATE ${WAVPACK_INCLUDE_DIRS})
target_include_directories(SDL_remixer PRIVATE ${FLAC_INCLUDE_DIRS})
target_include_directories(SDL_remixer PRIVATE ${GME_INCLUDE_DIRS})
target_include_directories(SDL_remixer PRIVATE ${XMP_INCLUDE_DIRS})
target_include_directories(SDL_remixer PRIVATE ${MPG123_INCLUDE_DIRS})
target_compile_definitions(SDL_remixer PRIVATE VORBIS_DYNAMIC="libvorbisfile.so.3")
target_compile_definitions(SDL_remixer PRIVATE OPUS_DYNAMIC="libopusfile.so.0")
target_compile_definitions(SDL_remixer PRIVATE WAVPACK_DYNAMIC="libwavpack.so.1")
target_compile_definitions(SDL_remixer PRIVATE FLUIDSYNTH_DYNAMIC="libfluidsynth.so.3")
target_compile_definitions(SDL_remixer PRIVATE FLAC_DYNAMIC="libFLAC.so.12")
target_compile_definitions(SDL_remixer PRIVATE GME_DYNAMIC="libgme.so.0")
target_compile_definitions(SDL_remixer PRIVATE XMP_DYNAMIC="libxmp.so.4")
target_compile_definitions(SDL_remixer PRIVATE MPG123_DYNAMIC="libmpg123.so.0")

add_executable(testmixer src/testmixer.c)
if ("c_std_99" IN_LIST CMAKE_C_COMPILE_FEATURES)
    target_compile_features(testmixer PRIVATE c_std_99)
endif()
target_link_libraries(testmixer PRIVATE SDL_remixer)
target_link_libraries(testmixer PRIVATE SDL3::SDL3)
target_include_directories(testmixer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
