/*
  SDL_mixer:  An audio mixer library based on the SDL library
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

// This file supports Ogg Vorbis audio streams using the public-domain, header-only library, stb_vorbis.

#include "SDL_mixer_internal.h"

#define STB_VORBIS_NO_STDIO 1
#define STB_VORBIS_NO_CRT 1
#define STB_VORBIS_NO_PUSHDATA_API 1
#define STB_VORBIS_MAX_CHANNELS 8   /* For 7.1 surround sound */
#define STB_FORCEINLINE SDL_FORCE_INLINE
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
#define STB_VORBIS_BIG_ENDIAN 1
#endif
#define STBV_CDECL SDLCALL /* for SDL_qsort() */

#ifdef assert
#undef assert
#endif
#ifdef memset
#undef memset
#endif
#ifdef memcpy
#undef memcpy
#endif
#define assert SDL_assert
#define memset SDL_memset
#define memcmp SDL_memcmp
#define memcpy SDL_memcpy
#define qsort SDL_qsort
#define malloc SDL_malloc
#define realloc SDL_realloc
#define free SDL_free

#define pow SDL_pow
#define floor SDL_floor
#define ldexp(v, e) SDL_scalbn((v), (e))
#define abs(x) SDL_abs(x)
#define cos(x) SDL_cos(x)
#define sin(x) SDL_sin(x)
#define log(x) SDL_log(x)
#define exp(x) SDL_exp(x)

#define STB_VORBIS_STDINT_DEFINED 1
typedef Uint8 uint8;
typedef Sint8 int8;
typedef Uint16 uint16;
typedef Sint16 int16;
typedef Uint32 uint32;
typedef Sint32 int32;

#include "stb_vorbis/stb_vorbis.h"

static bool SetStbVorbisError(const char *function, int error)
{
    switch (error) {
        #define HANDLE_ERROR_CASE(X) case X: return SDL_SetError("%s: %s", function, #X)
        HANDLE_ERROR_CASE(VORBIS_need_more_data);
        HANDLE_ERROR_CASE(VORBIS_invalid_api_mixing);
        HANDLE_ERROR_CASE(VORBIS_outofmem);
        HANDLE_ERROR_CASE(VORBIS_feature_not_supported);
        HANDLE_ERROR_CASE(VORBIS_too_many_channels);
        HANDLE_ERROR_CASE(VORBIS_file_open_failure);
        HANDLE_ERROR_CASE(VORBIS_seek_without_length);
        HANDLE_ERROR_CASE(VORBIS_unexpected_eof);
        HANDLE_ERROR_CASE(VORBIS_seek_invalid);
        HANDLE_ERROR_CASE(VORBIS_invalid_setup);
        HANDLE_ERROR_CASE(VORBIS_invalid_stream);
        HANDLE_ERROR_CASE(VORBIS_missing_capture_pattern);
        HANDLE_ERROR_CASE(VORBIS_invalid_stream_structure_version);
        HANDLE_ERROR_CASE(VORBIS_continued_packet_flag_invalid);
        HANDLE_ERROR_CASE(VORBIS_incorrect_stream_serial_number);
        HANDLE_ERROR_CASE(VORBIS_invalid_first_page);
        HANDLE_ERROR_CASE(VORBIS_bad_packet_type);
        HANDLE_ERROR_CASE(VORBIS_cant_find_last_page);
        HANDLE_ERROR_CASE(VORBIS_seek_failed);
        HANDLE_ERROR_CASE(VORBIS_ogg_skeleton_not_supported);
        #undef HANDLE_ERROR_CASE
        default: break;
    }
    return SDL_SetError("%s: unknown error %d\n", function, error);
}

typedef struct STBVORBIS_AudioUserData
{
    const Uint8 *data;
    size_t datalen;
    bool loop;
    Sint64 loop_start;
    Sint64 loop_end;
    Sint64 loop_len;
} STBVORBIS_AudioUserData;

typedef struct STBVORBIS_UserData
{
    const STBVORBIS_AudioUserData *payload;
    stb_vorbis *vorbis;
    Uint32 skip_samples;
} STBVORBIS_UserData;


static bool SDLCALL STBVORBIS_init(void)
{
    return true;
}

static void SDLCALL STBVORBIS_quit(void)
{
}

static bool SDLCALL STBVORBIS_init_audio(SDL_IOStream *io, SDL_AudioSpec *spec, SDL_PropertiesID props, Sint64 *duration_frames, void **audio_userdata)
{
    // just load the bare minimum from the IOStream to verify it's an Ogg Vorbis file.
    Uint8 buffer[35];  // this is just enough to see "OggS" at the start and "vorbis" at the end.

    // the initial Ogg Page should catch this in 35 bytes; no matter how large the page might be,
    // the initial portion will still start with OggS and have a Vorbis header at the same place,
    // knock on wood.
    if (SDL_ReadIO(io, buffer, sizeof (buffer)) != sizeof (buffer)) {
        return false;
    } else if (SDL_memcmp(buffer, "OggS", 4) != 0) {
        return SDL_SetError("Not an Ogg Vorbis audio stream");
    } else if (SDL_memcmp(&buffer[29], "vorbis", 6) != 0) {
        return SDL_SetError("Not an Ogg Vorbis audio stream");
    }

    // now rewind, load the whole thing to memory, and use that buffer for future processing.
    if (SDL_SeekIO(io, 0, SDL_IO_SEEK_SET) < 0) {
        return false;
    }
    size_t datalen = 0;
    Uint8 *data = (Uint8 *) SDL_LoadFile_IO(io, &datalen, false);
    if (!data) {
        return false;
    }

    STBVORBIS_AudioUserData *payload = (STBVORBIS_AudioUserData *) SDL_calloc(1, sizeof (*payload));
    if (!payload) {
        SDL_free(data);
        return false;
    }

    payload->data = data;
    payload->datalen = datalen;

    // now open the memory buffer for serious processing.
    int error = 0;
    stb_vorbis *vorbis = stb_vorbis_open_memory((const unsigned char *) data, (int) datalen, &error, NULL);
    if (!vorbis) {
        SDL_free(data);
        SDL_free(payload);
        return SetStbVorbisError("stb_vorbis_open_memory", error);
    }

    const stb_vorbis_info vi = stb_vorbis_get_info(vorbis);
    spec->format = SDL_AUDIO_F32;
    spec->channels = vi.channels;
    spec->freq = vi.sample_rate;

    const stb_vorbis_comment vc = stb_vorbis_get_comment(vorbis);
    MIX_ParseOggComments(props, spec->freq, vc.vendor, (const char * const *) vc.comment_list, vc.comment_list_length, &payload->loop_start, &payload->loop_end, &payload->loop_len);

    const Sint64 full_length = (Sint64) stb_vorbis_stream_length_in_samples(vorbis);
    payload->loop = ((payload->loop_end > 0) && (payload->loop_end <= full_length) && (payload->loop_start < payload->loop_end));
    stb_vorbis_close(vorbis);  // done with this instance. Tracks will maintain their own stb_vorbis object.

    *duration_frames = payload->loop ? MIX_DURATION_INFINITE : full_length;  // if looping, stream is infinite.
    *audio_userdata = payload;

    return true;
}

bool SDLCALL STBVORBIS_init_track(void *audio_userdata, const SDL_AudioSpec *spec, SDL_PropertiesID props, void **userdata)
{
    STBVORBIS_UserData *d = (STBVORBIS_UserData *) SDL_calloc(1, sizeof (*d));
    if (!d) {
        return false;
    }

    const STBVORBIS_AudioUserData *payload = (const STBVORBIS_AudioUserData *) audio_userdata;

    int error = 0;
    d->vorbis = stb_vorbis_open_memory((const unsigned char *) payload->data, (int) payload->datalen, &error, NULL);
    if (!d->vorbis) {
        SDL_free(d);
        return SetStbVorbisError("stb_vorbis_open_memory", error);
    }

    d->payload = payload;

    *userdata = d;

    return true;
}

bool SDLCALL STBVORBIS_decode(void *userdata, SDL_AudioStream *stream)
{
    STBVORBIS_UserData *d = (STBVORBIS_UserData *) userdata;

    // !!! FIXME: handle looping.

    // Note that stb_vorbis does not currently handle the bitstream id
    // changing--a "chained" ogg file, or perhaps a "frankenstein" file, as
    // mpg123 calls it--where two unrelated .ogg files, possibly with
    // different audio specs, are cat'd together. libvorbisfile can handle
    // this, but at the moment stb_vorbis will call it EOF at the end of the
    // current bitstream. So we don't have all the decoder_vorbis.c code to
    // change audio specs mid-file here.

    float **pcm_channels = NULL;
    int num_channels = 0;
    const int amount = stb_vorbis_get_frame_float(d->vorbis, &num_channels, &pcm_channels);
    if (amount <= 0) {
        return false;  // EOF
    }

    // did we just seek and need to throw away some samples at the start of the frame to reach the exact seek point?
    float *outputs[8];
    if (d->skip_samples) {
        const Uint32 skip = d->skip_samples;
        SDL_assert(num_channels <= SDL_arraysize(outputs));
        for (int i = 0; i < num_channels; i++) {
            outputs[i] = pcm_channels[i] + skip;
        }
        pcm_channels = outputs;
        d->skip_samples = 0;
    }

    SDL_PutAudioStreamPlanarData(stream, (const void * const *) pcm_channels, num_channels, amount);

    return true;  // had more data to decode.
}

bool SDLCALL STBVORBIS_seek(void *userdata, Uint64 frame)
{
    STBVORBIS_UserData *d = (STBVORBIS_UserData *) userdata;
    const int rc = stb_vorbis_seek_frame(d->vorbis, (unsigned int) frame);
    if (!rc) {
        return SetStbVorbisError("stb_vorbis_seek", stb_vorbis_get_error(d->vorbis));
    }

    d->skip_samples = (Uint32) (frame - ((Uint64) d->vorbis->current_loc));
    return true;
}

void SDLCALL STBVORBIS_quit_track(void *userdata)
{
    STBVORBIS_UserData *d = (STBVORBIS_UserData *) userdata;
    stb_vorbis_close(d->vorbis);
    SDL_free(d);
}

void SDLCALL STBVORBIS_quit_audio(void *audio_userdata)
{
    STBVORBIS_AudioUserData *d = (STBVORBIS_AudioUserData *) audio_userdata;
    SDL_free((void *) d->data);
    SDL_free(d);
}

MIX_Decoder MIX_Decoder_STBVORBIS = {
    "STBVORBIS",
    STBVORBIS_init,
    STBVORBIS_init_audio,
    STBVORBIS_init_track,
    STBVORBIS_decode,
    STBVORBIS_seek,
    STBVORBIS_quit_track,
    STBVORBIS_quit_audio,
    STBVORBIS_quit
};

