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

// This file supports Ogg Opus audio streams using libopusfile.

#ifdef DECODER_OPUS

#include "SDL_mixer_internal.h"

#ifdef OPUSFILE_HEADER
#include OPUSFILE_HEADER
#else
#include <opus/opusfile.h>
#endif

#ifdef OPUS_DYNAMIC
#define MIX_LOADER_DYNAMIC OPUS_DYNAMIC
#endif

#define MIX_LOADER_FUNCTIONS \
    MIX_LOADER_FUNCTION(true,const OpusTags *,op_tags,(const OggOpusFile *,int)) \
    MIX_LOADER_FUNCTION(true,OggOpusFile *,op_open_callbacks,(void *,const OpusFileCallbacks *,const unsigned char *,size_t,int *)) \
    MIX_LOADER_FUNCTION(true,OggOpusFile *,op_test_callbacks,(void *,const OpusFileCallbacks *,const unsigned char *,size_t,int *)) \
    MIX_LOADER_FUNCTION(true,void,op_free,(OggOpusFile *)) \
    MIX_LOADER_FUNCTION(true,const OpusHead *,op_head,(const OggOpusFile *,int)) \
    MIX_LOADER_FUNCTION(true,int,op_seekable,(const OggOpusFile *)) \
    MIX_LOADER_FUNCTION(true,int,op_read_float,(OggOpusFile *, float *,int,int *)) \
    MIX_LOADER_FUNCTION(true,int,op_raw_seek,(OggOpusFile *,ogg_int64_t)) \
    MIX_LOADER_FUNCTION(true,int,op_pcm_seek,(OggOpusFile *,ogg_int64_t)) \
    MIX_LOADER_FUNCTION(true,ogg_int64_t,op_pcm_tell,(const OggOpusFile *)) \
    MIX_LOADER_FUNCTION(true,ogg_int64_t,op_pcm_total,(const OggOpusFile *, int)) \

#define MIX_LOADER_MODULE opus
#include "SDL_mixer_loader.h"


typedef struct OPUS_AudioUserData
{
    const Uint8 *data;
    size_t datalen;
    bool loop;
    Sint64 loop_start;
    Sint64 loop_end;
    Sint64 loop_len;
} OPUS_AudioUserData;

typedef struct OPUS_UserData
{
    const OPUS_AudioUserData *payload;
    SDL_IOStream *io;  // a const-mem IOStream for accessing the payload's data.
    OggOpusFile *of;
    int current_channels;
    int current_bitstream;
} OPUS_UserData;


static bool SDLCALL OPUS_init(void)
{
    return LoadModule_opus();
}

static void SDLCALL OPUS_quit(void)
{
    UnloadModule_opus();
}

static bool set_op_error(const char *function, int error)
{
    #define HANDLE_ERROR_CASE(X) case X: return SDL_SetError("%s: %s", function, #X)
    switch (error) {
        HANDLE_ERROR_CASE(OP_FALSE);
        HANDLE_ERROR_CASE(OP_EOF);
        HANDLE_ERROR_CASE(OP_HOLE);
        HANDLE_ERROR_CASE(OP_EREAD);
        HANDLE_ERROR_CASE(OP_EFAULT);
        HANDLE_ERROR_CASE(OP_EIMPL);
        HANDLE_ERROR_CASE(OP_EINVAL);
        HANDLE_ERROR_CASE(OP_ENOTFORMAT);
        HANDLE_ERROR_CASE(OP_EBADHEADER);
        HANDLE_ERROR_CASE(OP_EVERSION);
        HANDLE_ERROR_CASE(OP_ENOTAUDIO);
        HANDLE_ERROR_CASE(OP_EBADPACKET);
        HANDLE_ERROR_CASE(OP_EBADLINK);
        HANDLE_ERROR_CASE(OP_ENOSEEK);
        HANDLE_ERROR_CASE(OP_EBADTIMESTAMP);
    #undef HANDLE_ERROR_CASE
    default: break;
    }
    return SDL_SetError("%s: unknown error %d", function, error);
}

static int OPUS_IoRead(void *datasource, unsigned char *ptr, int size)
{
    return SDL_ReadIO((SDL_IOStream*)datasource, ptr, (size_t) size);
}

static int OPUS_IoSeek(void *datasource, opus_int64 offset, int whence)
{
    return (SDL_SeekIO((SDL_IOStream*)datasource, offset, whence) < 0) ? -1 : 0;
}

static opus_int64 OPUS_IoTell(void *datasource)
{
    return (opus_int64) SDL_TellIO((SDL_IOStream*)datasource);
}

static int OPUS_IoClose(void *datasource)
{
    (void)datasource;
    return 0;
}

static const OpusFileCallbacks OPUS_IoCallbacks = { OPUS_IoRead, OPUS_IoSeek, OPUS_IoTell, OPUS_IoClose };


static bool SDLCALL OPUS_init_audio(SDL_IOStream *io, SDL_AudioSpec *spec, SDL_PropertiesID props, Sint64 *duration_frames, void **audio_userdata)
{
    // just load the bare minimum from the IOStream to verify it's an Ogg Vorbis file.
    int rc = 0;
    OggOpusFile *of = opus.op_test_callbacks(io, &OPUS_IoCallbacks, NULL, 0, &rc);
    if (!of) {
        return SDL_SetError("Not an Opus audio stream");
    }
    opus.op_free(of);

    // now rewind, load the whole thing to memory, and use that buffer for future processing.
    if (SDL_SeekIO(io, 0, SDL_IO_SEEK_SET) < 0) {
        return false;
    }
    size_t datalen = 0;
    Uint8 *data = (Uint8 *) SDL_LoadFile_IO(io, &datalen, false);
    if (!data) {
        return false;
    }

    OPUS_AudioUserData *payload = (OPUS_AudioUserData *) SDL_calloc(1, sizeof (*payload));
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
    of = opus.op_open_callbacks(io, &OPUS_IoCallbacks, NULL, 0, &rc);
    if (!of) {
        SDL_CloseIO(io);
        SDL_free(data);
        SDL_free(payload);
        return set_op_error("ov_open_callbacks", rc);
    }

    const OpusHead *info = opus.op_head(of, -1);
    if (!info) {
        opus.op_free(of);
        SDL_CloseIO(io);
        SDL_free(data);
        SDL_free(payload);
        return SDL_SetError("Couldn't get Opus info; corrupt data?");
    }

    spec->format = SDL_AUDIO_F32;
    spec->channels = info->channel_count;
    spec->freq = 48000;  // libopus only outputs at 48kHz.

    const OpusTags *tags = opus.op_tags(of, -1);
    if (tags != NULL) {
        MIX_ParseOggComments(props, spec->freq, tags->vendor, (const char * const *) tags->user_comments, tags->comments, &payload->loop_start, &payload->loop_end, &payload->loop_len);
    }

    opus.op_raw_seek(of, 0);  // !!! FIXME: it's not clear if this seek is necessary, but https://stackoverflow.com/a/72482773 suggests it might be, at least on older libvorbisfile releases...
    const Sint64 full_length = (Sint64) opus.op_pcm_total(of, -1);
    payload->loop = ((payload->loop_end > 0) && (payload->loop_end <= full_length) && (payload->loop_start < payload->loop_end));
    opus.op_free(of);  // done with this instance. Tracks will maintain their own OggVorbis_File object.
    SDL_CloseIO(io);  // close our memory i/o.

    *duration_frames = payload->loop ? MIX_DURATION_INFINITE : full_length;  // if looping, stream is infinite.
    *audio_userdata = payload;

    return true;
}

bool SDLCALL OPUS_init_track(void *audio_userdata, const SDL_AudioSpec *spec, SDL_PropertiesID props, void **userdata)
{
    OPUS_UserData *d = (OPUS_UserData *) SDL_calloc(1, sizeof (*d));
    if (!d) {
        return false;
    }

    const OPUS_AudioUserData *payload = (const OPUS_AudioUserData *) audio_userdata;

    d->io = SDL_IOFromConstMem(payload->data, payload->datalen);
    if (!d->io) {  // uhoh.
        SDL_free(d);
        return false;
    }

    // now open the memory buffer for serious processing.
    int rc = 0;
    d->of = opus.op_open_callbacks(d->io, &OPUS_IoCallbacks, NULL, 0, &rc);
    if (!d->of) {
        SDL_CloseIO(d->io);
        SDL_free(d);
        return set_op_error("op_open_callbacks", rc);
    }

    d->current_channels = spec->channels;
    d->current_bitstream = -1;
    d->payload = payload;

    *userdata = d;

    return true;
}

bool SDLCALL OPUS_decode(void *userdata, SDL_AudioStream *stream)
{
    OPUS_UserData *d = (OPUS_UserData *) userdata;
    int bitstream = d->current_bitstream;
    float samples[256];

    // !!! FIXME: handle looping.

    const int channels = d->current_channels;
    const int amount = (int)opus.op_read_float(d->of, samples, SDL_arraysize(samples), &bitstream);
    if (amount < 0) {
        return set_op_error("op_read_float", amount);
    }

    SDL_assert((amount * channels) <= SDL_arraysize(samples));

    if (bitstream != d->current_bitstream) {
        const OpusHead *info = opus.op_head(d->of, -1);
        if (info) {  // this _shouldn't_ be NULL, but if it is, we're just going on without it and hoping the stream format didn't change.
            if (d->current_channels != info->channel_count) {
                const SDL_AudioSpec spec = { SDL_AUDIO_F32, info->channel_count, 48000 };
                SDL_SetAudioStreamFormat(stream, &spec, NULL);
                d->current_channels = info->channel_count;
            }
        }
        d->current_bitstream = bitstream;
    }

    SDL_PutAudioStreamData(stream, samples, amount * d->current_channels * sizeof (float));

    return (amount > 0);
}

bool SDLCALL OPUS_seek(void *userdata, Uint64 frame)
{
    OPUS_UserData *d = (OPUS_UserData *) userdata;
    // !!! FIXME: I assume op_raw_seek is faster if we're seeking to start, but I could be wrong.
    const int rc = (frame == 0) ? opus.op_raw_seek(d->of, 0) : opus.op_pcm_seek(d->of, (ogg_int64_t) frame);
    return (rc == 0) ? true : set_op_error("op_pcm_seek", rc);
}

void SDLCALL OPUS_quit_track(void *userdata)
{
    OPUS_UserData *d = (OPUS_UserData *) userdata;
    opus.op_free(d->of);
    SDL_CloseIO(d->io);
    SDL_free(d);
}

void SDLCALL OPUS_quit_audio(void *audio_userdata)
{
    OPUS_AudioUserData *d = (OPUS_AudioUserData *) audio_userdata;
    SDL_free((void *) d->data);
    SDL_free(d);
}

MIX_Decoder MIX_Decoder_OPUS = {
    "OPUS",
    OPUS_init,
    OPUS_init_audio,
    OPUS_init_track,
    OPUS_decode,
    OPUS_seek,
    OPUS_quit_track,
    OPUS_quit_audio,
    OPUS_quit
};

#endif
