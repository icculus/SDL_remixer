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

typedef struct RAW_UserData
{
    const Uint8 *data;
    size_t datalen;
    size_t position;
    size_t framesize;
    size_t total_frames;
} RAW_UserData;

static bool RAW_init(void)
{
    return true;  // always succeeds.
}

static bool RAW_init_audio(const void *data, size_t datalen, SDL_AudioSpec *spec, SDL_PropertiesID props, void **audio_userdata)
{
    const char *decoder_name = SDL_GetStringProperty(props, MIX_PROP_AUDIO_DECODER_STRING, NULL);
    if (!decoder_name || (SDL_strcasecmp(decoder_name, "raw") != 0)) {
        return false;
    }

    const Sint64 si64fmt = SDL_GetNumberProperty(props, MIX_PROP_DECODER_FORMAT_NUMBER, -1);
    const Sint64 si64channels = SDL_GetNumberProperty(props, MIX_PROP_DECODER_CHANNELS_NUMBER, -1);
    const Sint64 si64freq = SDL_GetNumberProperty(props, MIX_PROP_DECODER_FREQ_NUMBER, -1);

    if ((si64fmt <= 0) || (si64channels <= 0) || (si64freq <= 0)) {
        return false;
    }

    spec->format = (SDL_AudioFormat) si64fmt;
    spec->channels = (int) si64channels;
    spec->freq = (int) si64freq;

    // we don't have to inspect the data, we treat anything as valid.
    *audio_userdata = NULL;

    return true;
}

static bool RAW_init_track(void *audio_userdata, const void *data, size_t datalen, const SDL_AudioSpec *spec, SDL_PropertiesID props, void **userdata)
{
    RAW_UserData *d = (RAW_UserData *) SDL_calloc(1, sizeof (*d));
    if (!d) {
        return false;
    }

    // Clamp data to complete sample frames, just in case.
    d->data = data;
    d->framesize = SDL_AUDIO_FRAMESIZE(*spec);
    d->total_frames = datalen / d->framesize;
    d->datalen = d->total_frames * d->framesize;
    *userdata = d;

    return true;
}

static int RAW_decode(void *userdata, void *buffer, size_t buflen)
{
    RAW_UserData *d = (RAW_UserData *) userdata;
    const size_t remaining = d->datalen - d->position;
    const size_t cpy = SDL_min(buflen, remaining);
    if (cpy) {
        SDL_memcpy(buffer, d->data + d->position, cpy);
        d->position += cpy;
    }
    return cpy;
}

static bool RAW_seek(void *userdata, Uint64 frame)
{
    RAW_UserData *d = (RAW_UserData *) userdata;
    if (frame > d->total_frames) {
        return SDL_SetError("Seek past end of data");
    }
    d->position = (size_t) (frame * d->framesize);
    return true;
}

static void RAW_quit_track(void *userdata)
{
    SDL_free(userdata);
}

static void RAW_quit_audio(void *audio_userdata)
{
    SDL_assert(!audio_userdata);
}

static void RAW_quit(void)
{
    // no-op.
}

Mix_Decoder Mix_Decoder_RAW = {
    "RAW",
    RAW_init,
    RAW_init_audio,
    RAW_init_track,
    RAW_decode,
    RAW_seek,
    RAW_quit_track,
    RAW_quit_audio,
    RAW_quit
};

