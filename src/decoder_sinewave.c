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

typedef struct SINEWAVE_AudioUserData
{
    int hz;
    float amplitude;
    int sample_rate;
} SINEWAVE_AudioUserData;

typedef struct SINEWAVE_UserData
{
    const SINEWAVE_AudioUserData *payload;
    int current_sine_sample;
} SINEWAVE_UserData;

static bool SDLCALL SINEWAVE_init_audio(SDL_IOStream *io, SDL_AudioSpec *spec, SDL_PropertiesID props, Sint64 *duration_frames, void **audio_userdata)
{
    const char *decoder_name = SDL_GetStringProperty(props, MIX_PROP_AUDIO_DECODER_STRING, NULL);
    if (!decoder_name || (SDL_strcasecmp(decoder_name, "sinewave") != 0)) {
        return false;
    }

    const Sint64 si64hz = SDL_GetNumberProperty(props, MIX_PROP_DECODER_SINEWAVE_HZ_NUMBER, -1);
    const float famp = SDL_GetFloatProperty(props, MIX_PROP_DECODER_SINEWAVE_AMPLITUDE_FLOAT, -1.0f);

    if ((si64hz <= 0) || (famp <= 0.0f)) {
        return false;
    }

    spec->format = SDL_AUDIO_F32;
    spec->channels = 1;
    // we use the existing spec->freq to match the device sample rate, avoiding unnecessary resampling.

    SINEWAVE_AudioUserData *payload = (SINEWAVE_AudioUserData *) SDL_malloc(sizeof (*payload));
    if (!payload) {
        return false;
    }

    payload->hz = (int) si64hz;
    payload->amplitude = famp;
    payload->sample_rate = spec->freq;

    *duration_frames = MIX_DURATION_INFINITE;
    *audio_userdata = payload;
    return true;
}

static bool SDLCALL SINEWAVE_init_track(void *audio_userdata, const SDL_AudioSpec *spec, SDL_PropertiesID props, void **userdata)
{
    SINEWAVE_UserData *d = (SINEWAVE_UserData *) SDL_calloc(1, sizeof (*d));
    if (!d) {
        return false;
    }

    d->payload = (const SINEWAVE_AudioUserData *) audio_userdata;
    *userdata = d;

    return true;
}

static bool SDLCALL SINEWAVE_decode(void *userdata, SDL_AudioStream *stream)
{
    SINEWAVE_UserData *d = (SINEWAVE_UserData *) userdata;
    const SINEWAVE_AudioUserData *payload = d->payload;
    const int sample_rate = payload->sample_rate;
    const float fsample_rate = (float) sample_rate;
    const int hz = payload->hz;
    const float amplitude = payload->amplitude;
    int current_sine_sample = d->current_sine_sample;
    float samples[256];

    for (int i = 0; i < SDL_arraysize(samples); i++) {
        const float phase = current_sine_sample * hz / fsample_rate;
        samples[i] = SDL_sinf(phase * 2.0f * SDL_PI_F) * amplitude;
        current_sine_sample++;
    }

    // wrapping around to avoid floating-point errors
    d->current_sine_sample = current_sine_sample % sample_rate;

    SDL_PutAudioStreamData(stream, samples, sizeof (samples));

    return true;   // infinite data
}

static bool SDLCALL SINEWAVE_seek(void *userdata, Uint64 frame)
{
    SINEWAVE_UserData *d = (SINEWAVE_UserData *) userdata;
    const SINEWAVE_AudioUserData *payload = d->payload;
    d->current_sine_sample = frame % payload->sample_rate;
    return true;
}

static void SDLCALL SINEWAVE_quit_track(void *userdata)
{
    SDL_free(userdata);
}

static void SDLCALL SINEWAVE_quit_audio(void *audio_userdata)
{
    SDL_free(audio_userdata);
}

Mix_Decoder Mix_Decoder_SINEWAVE = {
    "SINEWAVE",
    NULL,  // init
    SINEWAVE_init_audio,
    SINEWAVE_init_track,
    SINEWAVE_decode,
    SINEWAVE_seek,
    SINEWAVE_quit_track,
    SINEWAVE_quit_audio,
    NULL  // quit
};

