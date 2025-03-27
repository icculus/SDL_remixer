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

typedef struct WAV_AudioUserData
{
    Uint8 *data;
    Uint32 datalen;
} WAV_AudioUserData;

typedef struct WAV_UserData
{
    const Uint8 *data;
    size_t datalen;
    size_t position;
    size_t framesize;
    size_t total_frames;
} WAV_UserData;


static bool WAV_init(void)
{
    return true;  // already available without external dependencies.
}

static bool WAV_init_audio(const void *data, size_t datalen, SDL_AudioSpec *spec, SDL_PropertiesID props, void **audio_userdata)
{
    WAV_AudioUserData *d = (WAV_AudioUserData *) SDL_calloc(1, sizeof (*d));
    if (!d) {
        return false;
    }

    // this is obviously wrong.
    if (!SDL_LoadWAV_IO(SDL_IOFromConstMem(data, datalen), true, spec, &d->data, &d->datalen)) {
        SDL_free(d);
        return false;
    }

    *audio_userdata = d;

    return true;
}

static bool WAV_init_track(void *audio_userdata, const void *data, size_t datalen, const SDL_AudioSpec *spec, SDL_PropertiesID props, void **userdata)
{
    WAV_AudioUserData *ad = (WAV_AudioUserData *) audio_userdata;
    WAV_UserData *d = (WAV_UserData *) SDL_calloc(1, sizeof (*d));
    if (!d) {
        return false;
    }

    // Clamp data to complete sample frames, just in case.
    d->data = ad->data;
    d->framesize = SDL_AUDIO_FRAMESIZE(*spec);
    d->total_frames = ad->datalen / d->framesize;
    d->datalen = (size_t) (d->total_frames * d->framesize);
    *userdata = d;

    return true;
}

static int WAV_decode(void *userdata, void *buffer, size_t buflen)
{
    WAV_UserData *d = (WAV_UserData *) userdata;
    const size_t remaining = d->datalen - d->position;
    const size_t cpy = SDL_min(buflen, remaining);
    if (cpy) {
        SDL_memcpy(buffer, d->data + d->position, cpy);
        d->position += cpy;
    }
    return cpy;
}

static bool WAV_seek(void *userdata, Uint64 frame)
{
    WAV_UserData *d = (WAV_UserData *) userdata;
    if (frame > d->total_frames) {
        return SDL_SetError("Seek past end of data");
    }
    d->position = (size_t) (frame * d->framesize);
    return true;
}

static void WAV_quit_track(void *userdata)
{
    SDL_free(userdata);
}

static void WAV_quit_audio(void *audio_userdata)
{
    SDL_free(audio_userdata);
}

static void WAV_quit(void)
{
    // no-op.
}

Mix_Decoder Mix_Decoder_WAV = {
    "WAV",
    WAV_init,
    WAV_init_audio,
    WAV_init_track,
    WAV_decode,
    WAV_seek,
    WAV_quit_track,
    WAV_quit_audio,
    WAV_quit
};

