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

static bool SDLCALL DRMP3_init_audio(SDL_IOStream *io, SDL_AudioSpec *spec, SDL_PropertiesID props, void **audio_userdata)
{
    DRMP3_AudioUserData *payload = (DRMP3_AudioUserData *) SDL_calloc(1, sizeof(*payload));
    if (!payload) {
        return NULL;
    }

    payload->buffer = SDL_LoadFile_IO(io, &payload->buflen, false);
    if (!payload->buffer) {
        return false;
    }

    // We keep a drmp3 decoder in its initial state and copy that struct for
    //  each track, so it doesn't have to do the initial MP3 parsing each time.
    // They all share the same const payload buffer, seek table, etc.
    drmp3 decoder;
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
        #if 0  // !!! FIXME: seek points are causing incorrect seeks, so it's either buggy or I'm doing this wrong.
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
        #endif

        // !!! FIXME: set duration to num_pcm_frames.
    }

    spec->format = SDL_AUDIO_F32;
    spec->channels = (int) decoder.channels;
    spec->freq = (int) decoder.sampleRate;

    drmp3_uninit(&decoder);

    payload->framesize = SDL_AUDIO_FRAMESIZE(*spec);

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

static int SDLCALL DRMP3_decode(void *userdata, void *buffer, size_t buflen)
{
    DRMP3_UserData *d = (DRMP3_UserData *) userdata;
    const size_t framesize = d->payload->framesize;
    const drmp3_uint64 rc = drmp3_read_pcm_frames_f32(&d->decoder, (buflen / framesize), (float *) buffer);
    return (int) (size_t) (rc * framesize);
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

Mix_Decoder Mix_Decoder_DRMP3 = {
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

