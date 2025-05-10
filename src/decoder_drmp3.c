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

#ifdef DECODER_MP3_DRMP3

#include "SDL_mixer_internal.h"

#define DR_MP3_IMPLEMENTATION
#if defined(__GNUC__) && (__GNUC__ >= 4) && \
  !(defined(_WIN32) || defined(__EMX__))
#define DRMP3_API __attribute__((visibility("hidden")))
#elif defined(__APPLE__)
#define DRMP3_API __private_extern__
#else
#define DRMP3_API /* just in case.. */
#endif
#define DR_MP3_FLOAT_OUTPUT
#define DR_MP3_NO_STDIO
#define DRMP3_ASSERT(expression) SDL_assert((expression))
#define DRMP3_COPY_MEMORY(dst, src, sz) SDL_memcpy((dst), (src), (sz))
#define DRMP3_MOVE_MEMORY(dst, src, sz) SDL_memmove((dst), (src), (sz))
#define DRMP3_ZERO_MEMORY(p, sz) SDL_memset((p), 0, (sz))
#define DRMP3_MALLOC(sz) SDL_malloc((sz))
#define DRMP3_REALLOC(p, sz) SDL_realloc((p), (sz))
#define DRMP3_FREE(p) SDL_free((p))

// !!! FIXME: we need a DRMP3_NO_PARSE_METADATA_TAGS option to remove the ID3/APE checks, since we filtered them elsewhere.

#include "dr_libs/dr_mp3.h"

typedef struct DRMP3_AudioUserData
{
    void *buffer;
    size_t buflen;
    size_t framesize;
    drmp3_seek_point *seek_points;
    drmp3_uint32 num_seek_points;
} DRMP3_AudioUserData;

typedef struct DRMP3_UserData
{
    const DRMP3_AudioUserData *payload;
    drmp3 decoder;
} DRMP3_UserData;


// the i/o callbacks are only used for initial open, so it can read as little
//  as possible to verify it's really an MP3 file. After we're sure it is, we
//  pull the whole thing into RAM.

static size_t DRMP3_IoRead(void *context, void *buf, size_t size)
{
    return SDL_ReadIO((SDL_IOStream *) context, buf, size);
}

static drmp3_bool32 DRMP3_IoSeek(void *context, int offset, drmp3_seek_origin origin)
{
    // SDL_IOWhence and drmp3_seek_origin happen to match up.
    return (SDL_SeekIO((SDL_IOStream *) context, offset, (SDL_IOWhence) origin) < 0) ? DRMP3_FALSE : DRMP3_TRUE;
}

static drmp3_bool32 DRMP3_IoTell(void *context, drmp3_int64 *pos)
{
    *pos = (drmp3_int64) SDL_TellIO((SDL_IOStream *) context);
    return (*pos < 0) ? DRMP3_FALSE : DRMP3_TRUE;
}

static bool SDLCALL DRMP3_init_audio(SDL_IOStream *io, SDL_AudioSpec *spec, SDL_PropertiesID props, Sint64 *duration_frames, void **audio_userdata)
{
    drmp3 decoder;
    // do an initial load from the IOStream directly, so it can determine if this is really an MP3 file without
    //  reading a lot of the stream into memory first.
    if (!drmp3_init(&decoder, DRMP3_IoRead, DRMP3_IoSeek, DRMP3_IoTell, NULL, io, NULL)) {
        return false;  // probably not an MP3 file.
    }
    drmp3_uninit(&decoder);

    // suck the whole thing into memory and work out of there from now on.
    if (SDL_SeekIO(io, SDL_IO_SEEK_SET, 0) == -1) {
        return false;
    }

    DRMP3_AudioUserData *payload = (DRMP3_AudioUserData *) SDL_calloc(1, sizeof(*payload));
    if (!payload) {
        return false;
    }

    payload->buffer = SDL_LoadFile_IO(io, &payload->buflen, false);
    if (!payload->buffer) {
        SDL_free(payload);
        return false;
    }

    if (!drmp3_init_memory(&decoder, payload->buffer, payload->buflen, NULL)) {
        SDL_free(payload->buffer);
        SDL_free(payload);
        return false;
    }

    // I don't know if this is a great idea, as this is allegedly inefficient, but let's precalculate a seek table at load time, so each track can reuse it.
    // (If any of this fails, we go on without it.)
    drmp3_uint64 num_mp3_frames = 0;
    drmp3_uint64 num_pcm_frames = 0;
    if (drmp3_get_mp3_and_pcm_frame_count(&decoder, &num_mp3_frames, &num_pcm_frames)) {
        payload->num_seek_points = (drmp3_uint32) num_mp3_frames;
        payload->seek_points = (drmp3_seek_point *) SDL_calloc(num_mp3_frames, sizeof (*payload->seek_points));
        if (payload->seek_points) {
            if (drmp3_calculate_seek_points(&decoder, &payload->num_seek_points, payload->seek_points)) {
                // shrink the array if possible.
                if (payload->num_seek_points < ((drmp3_uint32) num_mp3_frames)) {
                    void *ptr = SDL_realloc(payload->seek_points, payload->num_seek_points * sizeof (*payload->seek_points));
                    if (ptr) {
                        payload->seek_points = (drmp3_seek_point *) ptr;
                    }
                }
            } else {  // failed, oh well. Live without.
                SDL_free(payload->seek_points);
                payload->seek_points = NULL;
                payload->num_seek_points = 0;
            }
        }
    }

    spec->format = SDL_AUDIO_F32;
    spec->channels = (int) decoder.channels;
    spec->freq = (int) decoder.sampleRate;

    drmp3_uninit(&decoder);

    payload->framesize = SDL_AUDIO_FRAMESIZE(*spec);

    *duration_frames = num_pcm_frames;
    *audio_userdata = payload;

    return true;
}

static bool SDLCALL DRMP3_init_track(void *audio_userdata, const SDL_AudioSpec *spec, SDL_PropertiesID props, void **userdata)
{
    const DRMP3_AudioUserData *payload = (const DRMP3_AudioUserData *) audio_userdata;
    DRMP3_UserData *d = (DRMP3_UserData *) SDL_calloc(1, sizeof (*d));
    if (!d) {
        return false;
    }

    if (!drmp3_init_memory(&d->decoder, payload->buffer, payload->buflen, NULL)) {
        SDL_free(d);
        return false;
    }

    if (payload->seek_points) {
        drmp3_bind_seek_table(&d->decoder, payload->num_seek_points, payload->seek_points);
    }

    d->payload = payload;
    *userdata = d;

    return true;
}

static bool SDLCALL DRMP3_decode(void *userdata, SDL_AudioStream *stream)
{
    DRMP3_UserData *d = (DRMP3_UserData *) userdata;
    const int framesize = d->payload->framesize;
    float samples[256];
    const drmp3_uint64 rc = drmp3_read_pcm_frames_f32(&d->decoder, sizeof (samples) / framesize, samples);
    if (!rc) {
        return false;  // done decoding.
    }
    SDL_PutAudioStreamData(stream, samples, rc * framesize);
    return true;
}

static bool SDLCALL DRMP3_seek(void *userdata, Uint64 frame)
{
    DRMP3_UserData *d = (DRMP3_UserData *) userdata;
    return !!drmp3_seek_to_pcm_frame(&d->decoder, (drmp3_uint64) frame);
}

static void SDLCALL DRMP3_quit_track(void *userdata)
{
    DRMP3_UserData *d = (DRMP3_UserData *) userdata;
    drmp3_uninit(&d->decoder);
    SDL_free(d);
}

static void SDLCALL DRMP3_quit_audio(void *audio_userdata)
{
    DRMP3_AudioUserData *payload = (DRMP3_AudioUserData *) audio_userdata;
    SDL_free(payload->seek_points);
    SDL_free(payload->buffer);
    SDL_free(payload);
}

MIX_Decoder MIX_Decoder_DRMP3 = {
    "DRMP3",
    NULL,  // init
    DRMP3_init_audio,
    DRMP3_init_track,
    DRMP3_decode,
    DRMP3_seek,
    DRMP3_quit_track,
    DRMP3_quit_audio,
    NULL  // quit
};

#endif
