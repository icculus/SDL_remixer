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
#if defined(OGG_HEADER)
#include OGG_HEADER
#elif defined(OGG_USE_TREMOR)
#include <tremor/ivorbisfile.h>
#else
#include <vorbis/vorbisfile.h>
#endif


typedef struct OGG_AudioUserData
{
    const Uint8 *data;
    size_t datalen;
    bool loop;
    ogg_int64_t loop_start;
    ogg_int64_t loop_end;
    ogg_int64_t loop_len;
} OGG_AudioUserData;

typedef struct OGG_UserData
{
    const OGG_AudioUserData *payload;
    SDL_IOStream *io;  // a const-mem IOStream for accessing the payload's data.
    OggVorbis_File vf;
    int current_channels;
    int current_freq;
    int current_bitstream;
} OGG_UserData;


static bool SDLCALL OGG_init(void)
{
    return LoadModule_vorbis();
}

static void SDLCALL OGG_quit(void)
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
    return SDL_SetError("%s: unknown error %d\n", function, error);
}

static size_t OGG_IoRead(void *ptr, size_t size, size_t nmemb, void *datasource)
{
    if (size > 0 && nmemb > 0) {
        return SDL_ReadIO((SDL_IOStream*)datasource, ptr, size * nmemb) / size;
    }
    return 0;
}

static int OGG_IoSeek(void *datasource, ogg_int64_t offset, int whence)
{
    return (SDL_SeekIO((SDL_IOStream*)datasource, offset, whence) < 0)? -1 : 0;
}

static long OGG_IoTell(void *datasource)
{
    return (long) SDL_TellIO((SDL_IOStream*)datasource);
}

static int OGG_IoClose(void *datasource)
{
    (void)datasource;
    return 0;
}

static const ov_callbacks OGG_IoCallbacks = { OGG_IoRead, OGG_IoSeek, OGG_IoClose, OGG_IoTell };



static bool IsLoopTag(const char *tag)
{
    char buf[5];
    SDL_strlcpy(buf, tag, 5);
    return SDL_strcasecmp(buf, "LOOP") == 0;
}

// Parse time string of the form HH:MM:SS.mmm and return equivalent sample position
static Sint64 ParseTime(char *time, long samplerate_hz)
{
    char *num_start, *p;
    Sint64 result;
    char c;
    int val;

    /* Time is directly expressed as a sample position */
    if (SDL_strchr(time, ':') == NULL) {
        return SDL_strtoll(time, NULL, 10);
    }

    result = 0;
    num_start = time;

    for (p = time; *p != '\0'; ++p) {
        if (*p == '.' || *p == ':') {
            c = *p; *p = '\0';
            if ((val = SDL_atoi(num_start)) < 0)
                return -1;
            result = result * 60 + val;
            num_start = p + 1;
            *p = c;
        }

        if (*p == '.') {
            double val_f = SDL_atof(p);
            if (val_f < 0) return -1;
            return result * samplerate_hz + (Sint64) (val_f * samplerate_hz);
        }
    }

    if ((val = SDL_atoi(num_start)) < 0) return -1;
    return (result * 60 + val) * samplerate_hz;
}


static bool SDLCALL OGG_init_audio(SDL_IOStream *io, SDL_AudioSpec *spec, SDL_PropertiesID props, Sint64 *duration_frames, void **audio_userdata)
{
    // just load the bare minimum from the IOStream to verify it's an Ogg Vorbis file.
    OggVorbis_File vf;
    if (vorbis.ov_test_callbacks(io, &vf, NULL, 0, OGG_IoCallbacks) < 0) {
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

    OGG_AudioUserData *payload = (OGG_AudioUserData *) SDL_calloc(1, sizeof *payload);
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
    const int rc = vorbis.ov_open_callbacks(io, &vf, NULL, 0, OGG_IoCallbacks);
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

#ifdef OGG_USE_TREMOR
    spec->format = SDL_AUDIO_S16;
#else
    spec->format = SDL_AUDIO_F32;
#endif

    spec->channels = vi->channels;
    spec->freq = vi->rate;

    bool is_loop_length = false;
    vorbis_comment *vc = vorbis.ov_comment(&vf, -1);
    if (vc != NULL) {
        if (vc->vendor && *vc->vendor) {
            SDL_SetStringProperty(props, "SDL_mixer.metadata.ogg.vendor", vc->vendor);
        }

        for (int i = 0; i < vc->comments; i++) {
            char *param = SDL_strdup(vc->user_comments[i]);
            if (!param) {
                continue;  // maybe better luck on other comments. But you're probably in big trouble now.
            }

            char *argument = param;
            char *value = SDL_strchr(param, '=');
            if (value == NULL) {
                value = param + SDL_strlen(param);
            } else {
                *(value++) = '\0';
            }

            /* Want to match LOOP-START, LOOP_START, etc. Remove - or _ from
             * string if it is present at position 4. */
            if (IsLoopTag(argument) && ((argument[4] == '_') || (argument[4] == '-'))) {
                SDL_memmove(argument + 4, argument + 5, SDL_strlen(argument) - 4);
            }

            char *generic_key = NULL;
            if (SDL_asprintf(&generic_key, "SDL_mixer.metadata.ogg.%s", argument) > 0) {
                SDL_SetStringProperty(props, generic_key, value);
                SDL_free(generic_key);
            }

            if (SDL_strcasecmp(argument, "LOOPSTART") == 0) {
                payload->loop_start = ParseTime(value, spec->freq);
            } else if (SDL_strcasecmp(argument, "LOOPLENGTH") == 0) {
                payload->loop_len = SDL_strtoll(value, NULL, 10);
                is_loop_length = true;
            } else if (SDL_strcasecmp(argument, "LOOPEND") == 0) {
                payload->loop_end = ParseTime(value, spec->freq);
                is_loop_length = false;
            } else if (SDL_strcasecmp(argument, "TITLE") == 0) {
                SDL_SetStringProperty(props, MIX_PROP_METADATA_TITLE_STRING, value);
            } else if (SDL_strcasecmp(argument, "ARTIST") == 0) {
                SDL_SetStringProperty(props, MIX_PROP_METADATA_ARTIST_STRING, value);
            } else if (SDL_strcasecmp(argument, "ALBUM") == 0) {
                SDL_SetStringProperty(props, MIX_PROP_METADATA_ALBUM_STRING, value);
            } else if (SDL_strcasecmp(argument, "COPYRIGHT") == 0) {
                SDL_SetStringProperty(props, MIX_PROP_METADATA_COPYRIGHT_STRING, value);
            }
            SDL_free(param);
        }

        if (is_loop_length) {
            payload->loop_end = payload->loop_start + payload->loop_len;
        } else {
            payload->loop_len = payload->loop_end - payload->loop_start;
        }

        /* Ignore invalid loop tag */
        if (payload->loop_start < 0 || payload->loop_len < 0 || payload->loop_end < 0) {
            payload->loop_start = 0;
            payload->loop_len = 0;
            payload->loop_end = 0;
        }
    }

    vorbis.ov_raw_seek(&vf, 0);  // !!! FIXME: it's not clear if this seek is necessary, but https://stackoverflow.com/a/72482773 suggests it might be, at least on older libvorbisfile releases...
    const Sint64 full_length = (Sint64) vorbis.ov_pcm_total(&vf, -1);
    payload->loop = ((payload->loop_end > 0) && (payload->loop_end <= full_length) && (payload->loop_start < payload->loop_end));
    vorbis.ov_clear(&vf);  // done with this instance. Tracks will maintain their own OggVorbis_File object.
    SDL_CloseIO(io);  // close our memory i/o.

    *duration_frames = payload->loop ? -1 : full_length;  // if looping, stream is infinite.
    *audio_userdata = payload;

    return true;
}

bool SDLCALL OGG_init_track(void *audio_userdata, const SDL_AudioSpec *spec, SDL_PropertiesID props, void **userdata)
{
    OGG_UserData *d = (OGG_UserData *) SDL_calloc(1, sizeof (*d));
    if (!d) {
        return false;
    }

    const OGG_AudioUserData *payload = (const OGG_AudioUserData *) audio_userdata;

    d->io = SDL_IOFromConstMem(payload->data, payload->datalen);
    if (!d->io) {  // uhoh.
        SDL_free(d);
        return false;
    }

    // now open the memory buffer for serious processing.
    int rc = vorbis.ov_open_callbacks(d->io, &d->vf, NULL, 0, OGG_IoCallbacks);
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

int SDLCALL OGG_decode(void *userdata, void *buffer, size_t buflen)
{
    OGG_UserData *d = (OGG_UserData *) userdata;
    //const OGG_AudioUserData *payload = d->payload;
    int bitstream = d->current_bitstream;

    // !!! FIXME: handle looping.

#ifdef OGG_USE_TREMOR
    const int amount = (int)vorbis.ov_read(&d->vf, buffer, buflen, &bitstream);
    if (amount < 0) {
        return set_ov_error("ov_read", amount);
    }
    const int retval = amount;
#else
    const int channels = d->current_channels;
    float **pcm_channels = NULL;
    const int amount = (int)vorbis.ov_read_float(&d->vf, &pcm_channels, (buflen / sizeof (float)) / channels, &bitstream);
    if (amount < 0) {
        set_ov_error("ov_read_float", amount);
        return -1;
    }

    SDL_assert((amount * sizeof (float) * channels) <= buflen);

    if (channels == 1) {  // one channel, just copy it right through.
        SDL_memcpy(buffer, pcm_channels[0], amount * sizeof (float));
    } else {  // multiple channels, we need to interleave them.
        float *fptr = (float *) buffer;
        for (int frame = 0; frame < amount; frame++) {
            for (int channel = 0; channel < channels; channel++) {
                *(fptr++) = pcm_channels[channel][frame];
            }
        }
    }

    const int retval = amount * sizeof (float) * channels;
#endif

    if (bitstream != d->current_bitstream) {
        const vorbis_info *vi = vorbis.ov_info(&d->vf, -1);
        if (vi) {  // this _shouldn't_ be NULL, but if it is, we're just going on without it and hoping the stream format didn't change.
            if ((d->current_channels != vi->channels) || (d->current_freq != vi->rate)) {
                // !!! FIXME: alert higher level that the format changed, so it can adjust the audio stream input format.
                d->current_channels = vi->channels;
                d->current_freq = vi->rate;
            }
        }
        d->current_bitstream = bitstream;
    }

    return retval;
}

bool SDLCALL OGG_seek(void *userdata, Uint64 frame)
{
    OGG_UserData *d = (OGG_UserData *) userdata;
    // !!! FIXME: I assume ov_raw_seek is faster if we're seeking to start, but I could be wrong.
    const int rc = (frame == 0) ? vorbis.ov_raw_seek(&d->vf, 0) : vorbis.ov_pcm_seek(&d->vf, (ogg_int64_t) frame);
    return (rc == 0) ? true : set_ov_error("ov_pcm_seek", rc);
}

void SDLCALL OGG_quit_track(void *userdata)
{
    OGG_UserData *d = (OGG_UserData *) userdata;
    vorbis.ov_clear(&d->vf);
    SDL_CloseIO(d->io);
    SDL_free(d);
}

void SDLCALL OGG_quit_audio(void *audio_userdata)
{
    OGG_AudioUserData *d = (OGG_AudioUserData *) audio_userdata;
    SDL_free((void *) d->data);
    SDL_free(d);
}

Mix_Decoder Mix_Decoder_OGG = {
    "OGG",
    OGG_init,
    OGG_init_audio,
    OGG_init_track,
    OGG_decode,
    OGG_seek,
    OGG_quit_track,
    OGG_quit_audio,
    OGG_quit
};

