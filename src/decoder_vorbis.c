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

// This file supports Ogg Vorbis audio streams using libvorbisfile (or the integer-only implementation, "tremor").

#include "SDL_mixer_internal.h"

#define OV_EXCLUDE_STATIC_CALLBACKS
#if defined(VORBIS_HEADER)
#include VORBIS_HEADER
#elif defined(VORBIS_USE_TREMOR)
#include <tremor/ivorbisfile.h>
#else
#include <vorbis/vorbisfile.h>
#endif

#ifdef VORBIS_DYNAMIC
#define MIX_LOADER_DYNAMIC VORBIS_DYNAMIC
#endif

#define MIX_LOADER_FUNCTIONS_vorbisbase \
    MIX_LOADER_FUNCTION(true,int,ov_clear,(OggVorbis_File *vf)) \
    MIX_LOADER_FUNCTION(true,vorbis_info *,ov_info,(OggVorbis_File *vf,int link)) \
    MIX_LOADER_FUNCTION(true,vorbis_comment *,ov_comment,(OggVorbis_File *vf,int link)) \
    MIX_LOADER_FUNCTION(true,int,ov_test_callbacks,(void *datasource, OggVorbis_File *vf, const char *initial, long ibytes, ov_callbacks callbacks)) \
    MIX_LOADER_FUNCTION(true,int,ov_open_callbacks,(void *datasource, OggVorbis_File *vf, const char *initial, long ibytes, ov_callbacks callbacks)) \
    MIX_LOADER_FUNCTION(true,ogg_int64_t,ov_pcm_total,(OggVorbis_File *vf,int i)) \
    MIX_LOADER_FUNCTION(true,int,ov_pcm_seek,(OggVorbis_File *vf, ogg_int64_t pos)) \
    MIX_LOADER_FUNCTION(true,int,ov_raw_seek,(OggVorbis_File *vf, ogg_int64_t pos)) \
    MIX_LOADER_FUNCTION(true,ogg_int64_t,ov_pcm_tell,(OggVorbis_File *vf)) \

#ifdef VORBIS_USE_TREMOR
    #define MIX_LOADER_FUNCTIONS \
        MIX_LOADER_FUNCTIONS_vorbisbase \
        MIX_LOADER_FUNCTION(true,long,ov_read,(OggVorbis_File *vf,char *buffer,int length, int *bitstream))
#else
    #define MIX_LOADER_FUNCTIONS \
        MIX_LOADER_FUNCTIONS_vorbisbase \
        MIX_LOADER_FUNCTION(true,long,ov_read_float,(OggVorbis_File *vf, float ***pcm_channels, int samples, int *bitstream))
#endif

#define MIX_LOADER_MODULE vorbis
#include "SDL_mixer_loader.h"


#ifdef VORBIS_USE_TREMOR
#define VORBIS_AUDIO_FORMAT SDL_AUDIO_S16
#else
#define VORBIS_AUDIO_FORMAT SDL_AUDIO_F32
#endif

typedef struct VORBIS_AudioUserData
{
    const Uint8 *data;
    size_t datalen;
    bool loop;
    Sint64 loop_start;
    Sint64 loop_end;
    Sint64 loop_len;
} VORBIS_AudioUserData;

typedef struct VORBIS_UserData
{
    const VORBIS_AudioUserData *payload;
    SDL_IOStream *io;  // a const-mem IOStream for accessing the payload's data.
    OggVorbis_File vf;
    int current_channels;
    int current_freq;
    int current_bitstream;
} VORBIS_UserData;


static bool SDLCALL VORBIS_init(void)
{
    return LoadModule_vorbis();
}

static void SDLCALL VORBIS_quit(void)
{
    UnloadModule_vorbis();
}

static bool set_ov_error(const char *function, int error)
{
    #define HANDLE_ERROR_CASE(X) case X: return SDL_SetError("%s: %s", function, #X)
    switch (error) {
        HANDLE_ERROR_CASE(OV_FALSE);
        HANDLE_ERROR_CASE(OV_EOF);
        HANDLE_ERROR_CASE(OV_HOLE);
        HANDLE_ERROR_CASE(OV_EREAD);
        HANDLE_ERROR_CASE(OV_EFAULT);
        HANDLE_ERROR_CASE(OV_EIMPL);
        HANDLE_ERROR_CASE(OV_EINVAL);
        HANDLE_ERROR_CASE(OV_ENOTVORBIS);
        HANDLE_ERROR_CASE(OV_EBADHEADER);
        HANDLE_ERROR_CASE(OV_EVERSION);
        HANDLE_ERROR_CASE(OV_ENOTAUDIO);
        HANDLE_ERROR_CASE(OV_EBADPACKET);
        HANDLE_ERROR_CASE(OV_EBADLINK);
        HANDLE_ERROR_CASE(OV_ENOSEEK);
    #undef HANDLE_ERROR_CASE
    default: break;
    }
    return SDL_SetError("%s: unknown error %d", function, error);
}

static size_t VORBIS_IoRead(void *ptr, size_t size, size_t nmemb, void *datasource)
{
    if (size > 0 && nmemb > 0) {
        return SDL_ReadIO((SDL_IOStream*)datasource, ptr, size * nmemb) / size;
    }
    return 0;
}

static int VORBIS_IoSeek(void *datasource, ogg_int64_t offset, int whence)
{
    return (SDL_SeekIO((SDL_IOStream*)datasource, offset, whence) < 0) ? -1 : 0;
}

static long VORBIS_IoTell(void *datasource)
{
    return (long) SDL_TellIO((SDL_IOStream*)datasource);
}

static int VORBIS_IoClose(void *datasource)
{
    (void)datasource;
    return 0;
}

static const ov_callbacks VORBIS_IoCallbacks = { VORBIS_IoRead, VORBIS_IoSeek, VORBIS_IoClose, VORBIS_IoTell };


static bool SDLCALL VORBIS_init_audio(SDL_IOStream *io, SDL_AudioSpec *spec, SDL_PropertiesID props, Sint64 *duration_frames, void **audio_userdata)
{
    // just load the bare minimum from the IOStream to verify it's an Ogg Vorbis file.
    OggVorbis_File vf;
    if (vorbis.ov_test_callbacks(io, &vf, NULL, 0, VORBIS_IoCallbacks) < 0) {
        return SDL_SetError("Not an Ogg Vorbis audio stream");
    }
    vorbis.ov_clear(&vf);

    // now rewind, load the whole thing to memory, and use that buffer for future processing.
    if (SDL_SeekIO(io, 0, SDL_IO_SEEK_SET) < 0) {
        return false;
    }
    size_t datalen = 0;
    Uint8 *data = (Uint8 *) SDL_LoadFile_IO(io, &datalen, false);
    if (!data) {
        return false;
    }

    VORBIS_AudioUserData *payload = (VORBIS_AudioUserData *) SDL_calloc(1, sizeof (*payload));
    if (!payload) {
        SDL_free(data);
        return false;
    }

    payload->data = data;
    payload->datalen = datalen;

    io = SDL_IOFromConstMem(data, datalen);  // switch over to a memory IOStream.
    if (!io) {  // uhoh.
        SDL_free(data);
        SDL_free(payload);
        return false;
    }

    // now open the memory buffer for serious processing.
    const int rc = vorbis.ov_open_callbacks(io, &vf, NULL, 0, VORBIS_IoCallbacks);
    if (rc < 0) {
        SDL_CloseIO(io);
        SDL_free(data);
        SDL_free(payload);
        return set_ov_error("ov_open_callbacks", rc);
    }

    const vorbis_info *vi = vorbis.ov_info(&vf, -1);
    if (!vi) {
        vorbis.ov_clear(&vf);
        SDL_CloseIO(io);
        SDL_free(data);
        SDL_free(payload);
        return SDL_SetError("Couldn't get Ogg Vorbis info; corrupt data?");
    }

    spec->format = VORBIS_AUDIO_FORMAT;
    spec->channels = vi->channels;
    spec->freq = vi->rate;

    vorbis_comment *vc = vorbis.ov_comment(&vf, -1);
    if (vc != NULL) {
        Mix_ParseOggComments(props, spec->freq, vc->vendor, (const char * const *) vc->user_comments, vc->comments, &payload->loop_start, &payload->loop_end, &payload->loop_len);
    }

    vorbis.ov_raw_seek(&vf, 0);  // !!! FIXME: it's not clear if this seek is necessary, but https://stackoverflow.com/a/72482773 suggests it might be, at least on older libvorbisfile releases...
    const Sint64 full_length = (Sint64) vorbis.ov_pcm_total(&vf, -1);
    payload->loop = ((payload->loop_end > 0) && (payload->loop_end <= full_length) && (payload->loop_start < payload->loop_end));
    vorbis.ov_clear(&vf);  // done with this instance. Tracks will maintain their own OggVorbis_File object.
    SDL_CloseIO(io);  // close our memory i/o.

    *duration_frames = payload->loop ? MIX_DURATION_INFINITE : full_length;  // if looping, stream is infinite.
    *audio_userdata = payload;

    return true;
}

bool SDLCALL VORBIS_init_track(void *audio_userdata, const SDL_AudioSpec *spec, SDL_PropertiesID props, void **userdata)
{
    VORBIS_UserData *d = (VORBIS_UserData *) SDL_calloc(1, sizeof (*d));
    if (!d) {
        return false;
    }

    const VORBIS_AudioUserData *payload = (const VORBIS_AudioUserData *) audio_userdata;

    d->io = SDL_IOFromConstMem(payload->data, payload->datalen);
    if (!d->io) {  // uhoh.
        SDL_free(d);
        return false;
    }

    // now open the memory buffer for serious processing.
    int rc = vorbis.ov_open_callbacks(d->io, &d->vf, NULL, 0, VORBIS_IoCallbacks);
    if (rc < 0) {
        SDL_CloseIO(d->io);
        SDL_free(d);
        return set_ov_error("ov_open_callbacks", rc);
    }

    d->current_channels = spec->channels;
    d->current_freq = spec->freq;
    d->current_bitstream = -1;
    d->payload = payload;

    *userdata = d;

    return true;
}

bool SDLCALL VORBIS_decode(void *userdata, SDL_AudioStream *stream)
{
    VORBIS_UserData *d = (VORBIS_UserData *) userdata;
    //const VORBIS_AudioUserData *payload = d->payload;
    int bitstream = d->current_bitstream;

    // !!! FIXME: handle looping.

#ifdef VORBIS_USE_TREMOR
    Uint8 samples[256];
    const int amount = (int)vorbis.ov_read(&d->vf, samples, sizeof (samples), &bitstream);
    if (amount < 0) {
        return set_ov_error("ov_read", amount);
    }
#else
    float samples[256];
    float **pcm_channels = NULL;
    const int amount = (int)vorbis.ov_read_float(&d->vf, &pcm_channels, SDL_arraysize(samples), &bitstream);
    if (amount < 0) {
        return set_ov_error("ov_read_float", amount);
    }
#endif

    if (bitstream != d->current_bitstream) {
        const vorbis_info *vi = vorbis.ov_info(&d->vf, -1);
        if (vi) {  // this _shouldn't_ be NULL, but if it is, we're just going on without it and hoping the stream format didn't change.
            if ((d->current_channels != vi->channels) || (d->current_freq != vi->rate)) {
                const SDL_AudioSpec spec = { VORBIS_AUDIO_FORMAT, vi->channels, vi->rate };
                SDL_SetAudioStreamFormat(stream, &spec, NULL);
                d->current_channels = vi->channels;
                d->current_freq = vi->rate;
            }
        }
        d->current_bitstream = bitstream;
    }

    if (amount == 0) {
        return false;  // EOF
    }

#ifdef VORBIS_USE_TREMOR
    SDL_PutAudioStreamData(stream, samples, amount);  // ov_read gave us bytes, not sample frames.
#else
    SDL_assert(amount <= SDL_arraysize(samples));
    const int channels = d->current_channels;
    if (channels == 1) {  // one channel, just copy it right through.
        SDL_PutAudioStreamData(stream, samples, amount * sizeof (float));
    } else {  // multiple channels, we need to interleave them.
        // We have `amount` sample frames, which is <= SDL_arraysize(samples), but at most,
        //  `samples` is `channels` multiples too small to hold the interleaved data, so
        //  interleave it and put it to the audio stream in chunks.
        const int max_frames = SDL_arraysize(samples) / channels;  // most sample frames per chunk.
        const int num_chunks = amount / max_frames;
        const int chunksize = max_frames * channels * sizeof (float);
        int frame = 0;
        for (int chunk = 0; chunk < num_chunks; chunk++) {
            float *fptr = samples;
            for (int i = 0; i < max_frames; i++) {
                for (int channel = 0; channel < channels; channel++) {
                    *(fptr++) = pcm_channels[channel][frame];
                }
                frame++;
            }
            SDL_PutAudioStreamData(stream, samples, chunksize);
        }

        // put the last bit.
        SDL_assert(frame <= amount);
        SDL_assert(((amount - frame) * channels) <= SDL_arraysize(samples));

        const int remaining = amount - frame;
        if (remaining > 0) {
            float *fptr = samples;
            for (int i = 0; i < remaining; i++) {
                for (int channel = 0; channel < channels; channel++) {
                    *(fptr++) = pcm_channels[channel][frame];
                }
                frame++;
            }
            SDL_PutAudioStreamData(stream, samples, remaining * channels * sizeof (float));
        }

        SDL_assert(frame == amount);
    }
#endif

    return true;  // had more data to decode.
}

bool SDLCALL VORBIS_seek(void *userdata, Uint64 frame)
{
    VORBIS_UserData *d = (VORBIS_UserData *) userdata;
    // !!! FIXME: I assume ov_raw_seek is faster if we're seeking to start, but I could be wrong.
    const int rc = (frame == 0) ? vorbis.ov_raw_seek(&d->vf, 0) : vorbis.ov_pcm_seek(&d->vf, (ogg_int64_t) frame);
    return (rc == 0) ? true : set_ov_error("ov_pcm_seek", rc);
}

void SDLCALL VORBIS_quit_track(void *userdata)
{
    VORBIS_UserData *d = (VORBIS_UserData *) userdata;
    vorbis.ov_clear(&d->vf);
    SDL_CloseIO(d->io);
    SDL_free(d);
}

void SDLCALL VORBIS_quit_audio(void *audio_userdata)
{
    VORBIS_AudioUserData *d = (VORBIS_AudioUserData *) audio_userdata;
    SDL_free((void *) d->data);
    SDL_free(d);
}

Mix_Decoder Mix_Decoder_VORBIS = {
    "VORBIS",
    VORBIS_init,
    VORBIS_init_audio,
    VORBIS_init_track,
    VORBIS_decode,
    VORBIS_seek,
    VORBIS_quit_track,
    VORBIS_quit_audio,
    VORBIS_quit
};

