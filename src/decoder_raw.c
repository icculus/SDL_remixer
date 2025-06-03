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

// (this decoder is always enabled, as it is used internally.)

#include "SDL_mixer_internal.h"

// !!! FIXME: change track interface to provide the stream when seeking, then we could use SDL_AudioStreamPutDataNoCopy to push the whole buffer upfront for free, and clear/push a subset when seeking.

typedef struct RAW_AudioData
{
    const Uint8 *data;
    size_t datalen;
    size_t framesize;
    size_t total_frames;
    bool free_when_done;
} RAW_AudioData;

typedef struct RAW_TrackData
{
    const RAW_AudioData *adata;
    size_t position;
} RAW_TrackData;


// for use outside of this decoder.
void *MIX_RAW_InitFromMemoryBuffer(const void *data, const size_t datalen, const SDL_AudioSpec *spec, Sint64 *duration_frames, bool free_when_done)
{
    // we don't have to inspect the data, we treat anything as valid.
    RAW_AudioData *adata = (RAW_AudioData *) SDL_malloc(sizeof (*adata));
    if (!adata) {
        return NULL;
    }

    // Clamp data to complete sample frames, just in case.
    adata->framesize = SDL_AUDIO_FRAMESIZE(*spec);
    adata->total_frames = datalen / adata->framesize;
    adata->datalen = adata->total_frames * adata->framesize;
    adata->data = data;
    adata->free_when_done = free_when_done;

    *duration_frames = (Sint64) adata->total_frames;

    return adata;
}

static bool SDLCALL RAW_init_audio(SDL_IOStream *io, SDL_AudioSpec *spec, SDL_PropertiesID props, Sint64 *duration_frames, void **audio_userdata)
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

    *audio_userdata = MIX_RAW_InitFromMemoryBuffer(data, datalen, spec, duration_frames, true);
    if (!*audio_userdata) {
        SDL_free(data);
        return false;
    }

    return true;
}

bool SDLCALL MIX_RAW_init_track(void *audio_userdata, const SDL_AudioSpec *spec, SDL_PropertiesID props, void **track_userdata)
{
    RAW_TrackData *tdata = (RAW_TrackData *) SDL_calloc(1, sizeof (*tdata));
    if (!tdata) {
        return false;
    }

    tdata->adata = (const RAW_AudioData *) audio_userdata;
    *track_userdata = tdata;

    return true;
}

bool SDLCALL MIX_RAW_decode(void *track_userdata, SDL_AudioStream *stream)
{
    RAW_TrackData *tdata = (RAW_TrackData *) track_userdata;
    const RAW_AudioData *adata = tdata->adata;
    const size_t remaining = adata->datalen - tdata->position;
    const size_t cpy = SDL_min(4096, remaining);
    if (!cpy) {
        return false;  // we're at EOF.
    }

    SDL_PutAudioStreamData(stream, adata->data + tdata->position, cpy);
    tdata->position += cpy;
    return true;
}

bool SDLCALL MIX_RAW_seek(void *track_userdata, Uint64 frame)
{
    RAW_TrackData *tdata = (RAW_TrackData *) track_userdata;
    const RAW_AudioData *adata = tdata->adata;
    if (frame > adata->total_frames) {
        return SDL_SetError("Seek past end of data");
    }
    tdata->position = (size_t) (frame * adata->framesize);
    return true;
}

void SDLCALL MIX_RAW_quit_track(void *track_userdata)
{
    SDL_free(track_userdata);
}

void SDLCALL MIX_RAW_quit_audio(void *audio_userdata)
{
    RAW_AudioData *tdata = (RAW_AudioData *) audio_userdata;
    if (tdata->free_when_done) {
        SDL_free((void *) tdata->data);
    }
    SDL_free(tdata);
}

MIX_Decoder MIX_Decoder_RAW = {
    "RAW",
    NULL,  // init
    RAW_init_audio,
    MIX_RAW_init_track,
    MIX_RAW_decode,
    MIX_RAW_seek,
    MIX_RAW_quit_track,
    MIX_RAW_quit_audio,
    NULL  // quit
};

