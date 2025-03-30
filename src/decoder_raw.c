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

typedef struct RAW_AudioUserData
{
    const Uint8 *data;
    size_t datalen;
    size_t framesize;
    size_t total_frames;
} RAW_AudioUserData;

typedef struct RAW_UserData
{
    const RAW_AudioUserData *payload;
    size_t position;
} RAW_UserData;


// for use outside of this decoder.
void *Mix_RAW_InitFromMemoryBuffer(void *data, const size_t datalen, const SDL_AudioSpec *spec)
{
    // we don't have to inspect the data, we treat anything as valid.
    RAW_AudioUserData *payload = (RAW_AudioUserData *) SDL_malloc(sizeof (*payload));
    if (!payload) {
        return false;
    }

    // Clamp data to complete sample frames, just in case.
    payload->framesize = SDL_AUDIO_FRAMESIZE(*spec);
    payload->total_frames = datalen / payload->framesize;
    payload->datalen = payload->total_frames * payload->framesize;
    payload->data = data;

    return payload;
}

static bool SDLCALL RAW_init_audio(SDL_IOStream *io, SDL_AudioSpec *spec, SDL_PropertiesID props, void **audio_userdata)
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

    // slurp in the raw PCM...
    size_t datalen = 0;
    Uint8 *data = (Uint8 *) SDL_LoadFile_IO(io, &datalen, false);
    if (!data) {
        return false;
    }

    *audio_userdata = Mix_RAW_InitFromMemoryBuffer(data, datalen, spec);
    if (!*audio_userdata) {
        SDL_free(data);
        return false;
    }

    return true;
}

static bool SDLCALL RAW_init_track(void *audio_userdata, const SDL_AudioSpec *spec, SDL_PropertiesID props, void **userdata)
{
    RAW_UserData *d = (RAW_UserData *) SDL_calloc(1, sizeof (*d));
    if (!d) {
        return false;
    }

    d->payload = (const RAW_AudioUserData *) audio_userdata;
    *userdata = d;

    return true;
}

static int SDLCALL RAW_decode(void *userdata, void *buffer, size_t buflen)
{
    RAW_UserData *d = (RAW_UserData *) userdata;
    const RAW_AudioUserData *payload = d->payload;
    const size_t remaining = payload->datalen - d->position;
    const size_t cpy = SDL_min(buflen, remaining);
    if (cpy) {
        SDL_memcpy(buffer, payload->data + d->position, cpy);
        d->position += cpy;
    }
    return cpy;
}

static bool SDLCALL RAW_seek(void *userdata, Uint64 frame)
{
    RAW_UserData *d = (RAW_UserData *) userdata;
    const RAW_AudioUserData *payload = d->payload;
    if (frame > payload->total_frames) {
        return SDL_SetError("Seek past end of data");
    }
    d->position = (size_t) (frame * payload->framesize);
    return true;
}

static void SDLCALL RAW_quit_track(void *userdata)
{
    SDL_free(userdata);
}

static void SDLCALL RAW_quit_audio(void *audio_userdata)
{
    RAW_AudioUserData *d = (RAW_AudioUserData *) audio_userdata;
    SDL_free((void *) d->data);
    SDL_free(d);
}

bool Mix_RAW_init_track(void *audio_userdata, const SDL_AudioSpec *spec, SDL_PropertiesID props, void **userdata)
{
    return RAW_init_track(audio_userdata, spec, props, userdata);
}

int Mix_RAW_decode(void *userdata, void *buffer, size_t buflen)
{
    return RAW_decode(userdata, buffer, buflen);
}

bool Mix_RAW_seek(void *userdata, Uint64 frame)
{
    return RAW_seek(userdata, frame);
}

void Mix_RAW_quit_track(void *userdata)
{
    RAW_quit_track(userdata);
}

void Mix_RAW_quit_audio(void *audio_userdata)
{
    RAW_quit_audio(audio_userdata);
}

Mix_Decoder Mix_Decoder_RAW = {
    "RAW",
    NULL,  // init
    RAW_init_audio,
    RAW_init_track,
    RAW_decode,
    RAW_seek,
    RAW_quit_track,
    RAW_quit_audio,
    NULL  // quit
};

