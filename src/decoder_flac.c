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

#include <FLAC/stream_decoder.h>

#ifdef FLAC_DYNAMIC
#define MIX_LOADER_DYNAMIC FLAC_DYNAMIC
#endif

#define MIX_LOADER_FUNCTIONS \
    MIX_LOADER_FUNCTION(true,FLAC__StreamDecoder*,FLAC__stream_decoder_new,(void)) \
    MIX_LOADER_FUNCTION(true,void,FLAC__stream_decoder_delete,(FLAC__StreamDecoder *decoder)) \
    MIX_LOADER_FUNCTION(true,FLAC__StreamDecoderInitStatus,FLAC__stream_decoder_init_stream,(FLAC__StreamDecoder *,FLAC__StreamDecoderReadCallback,FLAC__StreamDecoderSeekCallback,FLAC__StreamDecoderTellCallback,FLAC__StreamDecoderLengthCallback,FLAC__StreamDecoderEofCallback,FLAC__StreamDecoderWriteCallback,FLAC__StreamDecoderMetadataCallback,FLAC__StreamDecoderErrorCallback,void *)) \
    MIX_LOADER_FUNCTION(true,FLAC__StreamDecoderInitStatus,FLAC__stream_decoder_init_ogg_stream,(FLAC__StreamDecoder *,FLAC__StreamDecoderReadCallback,FLAC__StreamDecoderSeekCallback,FLAC__StreamDecoderTellCallback,FLAC__StreamDecoderLengthCallback,FLAC__StreamDecoderEofCallback,FLAC__StreamDecoderWriteCallback,FLAC__StreamDecoderMetadataCallback,FLAC__StreamDecoderErrorCallback,void *)) \
    MIX_LOADER_FUNCTION(true,FLAC__bool,FLAC__stream_decoder_finish,(FLAC__StreamDecoder *decoder)) \
    MIX_LOADER_FUNCTION(true,FLAC__bool,FLAC__stream_decoder_flush,(FLAC__StreamDecoder *decoder)) \
    MIX_LOADER_FUNCTION(true,FLAC__bool,FLAC__stream_decoder_process_single,(FLAC__StreamDecoder *decoder)) \
    MIX_LOADER_FUNCTION(true,FLAC__bool,FLAC__stream_decoder_process_until_end_of_metadata,(FLAC__StreamDecoder *decoder)) \
    MIX_LOADER_FUNCTION(true,FLAC__bool,FLAC__stream_decoder_process_until_end_of_stream,(FLAC__StreamDecoder *decoder)) \
    MIX_LOADER_FUNCTION(true,FLAC__bool,FLAC__stream_decoder_seek_absolute,(FLAC__StreamDecoder *decoder,FLAC__uint64 sample)) \
    MIX_LOADER_FUNCTION(true,FLAC__StreamDecoderState,FLAC__stream_decoder_get_state,(const FLAC__StreamDecoder *decoder)) \
    MIX_LOADER_FUNCTION(true,FLAC__uint64,FLAC__stream_decoder_get_total_samples,(const FLAC__StreamDecoder *decoder)) \
    MIX_LOADER_FUNCTION(true,FLAC__bool,FLAC__stream_decoder_set_metadata_respond,(FLAC__StreamDecoder *decoder,FLAC__MetadataType type)) \

#define MIX_LOADER_MODULE flac
#include "SDL_mixer_loader.h"

typedef struct FLAC_AudioUserData
{
    const Uint8 *data;
    size_t datalen;
    bool is_ogg_stream;
    bool loop;
    Sint64 loop_start;
    Sint64 loop_end;
    Sint64 loop_len;
    SDL_PropertiesID props;
} FLAC_AudioUserData;

typedef struct FLAC_UserData
{
    const FLAC_AudioUserData *payload;
    SDL_IOStream *io;
    FLAC__StreamDecoder *decoder;
    SDL_AudioStream *stream;
    SDL_AudioSpec spec;
    int bits_per_sample;
} FLAC_UserData;



static FLAC__StreamDecoderReadStatus FLAC_IoRead(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *userdata)
{
    if (*bytes > 0) {
        FLAC_UserData *d = (FLAC_UserData *) userdata;
        *bytes = SDL_ReadIO(d->io, buffer, *bytes);
        const SDL_IOStatus status = SDL_GetIOStatus(d->io);
        if (status == SDL_IO_STATUS_READY || status == SDL_IO_STATUS_NOT_READY) {
            return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
        } else if (status == SDL_IO_STATUS_EOF) {
            return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
        }
    }
    return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
}

static FLAC__StreamDecoderSeekStatus FLAC_IoSeek(const FLAC__StreamDecoder *decoder, FLAC__uint64 absolute_byte_offset, void *userdata)
{
    FLAC_UserData *d = (FLAC_UserData *) userdata;
    if (SDL_SeekIO(d->io, (Sint64)absolute_byte_offset, SDL_IO_SEEK_SET) < 0) {
        return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
    }
    return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
}

static FLAC__StreamDecoderTellStatus FLAC_IoTell(const FLAC__StreamDecoder *decoder, FLAC__uint64 *absolute_byte_offset, void *userdata)
{
    FLAC_UserData *d = (FLAC_UserData *) userdata;
    const Sint64 pos = SDL_TellIO(d->io);
    if (pos < 0) {
        return FLAC__STREAM_DECODER_TELL_STATUS_ERROR;
    }
    *absolute_byte_offset = (FLAC__uint64)pos;
    return FLAC__STREAM_DECODER_TELL_STATUS_OK;
}

static FLAC__StreamDecoderLengthStatus FLAC_IoLength(const FLAC__StreamDecoder *decoder, FLAC__uint64 *stream_length, void *userdata)
{
    FLAC_UserData *d = (FLAC_UserData *) userdata;
    const Sint64 iolen = SDL_GetIOSize(d->io);
    if (iolen < 0) {
        return FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR;
    }
    *stream_length = (FLAC__uint64)iolen;
    return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
}

static FLAC__bool FLAC_IoEOF(const FLAC__StreamDecoder *decoder, void *userdata)
{
    FLAC_UserData *d = (FLAC_UserData *) userdata;
    // these are either ConstMem or ClampIO streams, so this should be fast in either case.
    return SDL_TellIO(d->io) >= SDL_GetIOSize(d->io);
}

static FLAC__StreamDecoderWriteStatus FLAC_IoWriteNoOp(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 *const buffer[], void *userdata)
{
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;  // we don't need this data at this moment.
}

static FLAC__StreamDecoderWriteStatus FLAC_IoWrite(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 *const buffer[], void *userdata)
{
    FLAC_UserData *d = (FLAC_UserData *) userdata;
    SDL_AudioStream *stream = d->stream;
    const int headerchannels = (int) frame->header.channels;
    int sdlchannels = (int) headerchannels;
    if (sdlchannels == 3) {
        sdlchannels = 6;  // !!! FIXME: this is kinda gross, but FLAC 3-channel is FL, FR, FC, whereas SDL is FL, FR, LFE...we don't have a "front center" channel until 5.1 output.  :/  The channel mask thing Sam wants in SDL3 would fix this.
    }

    // change the stream format if we're suddenly getting data in a different format. I assume this can happen if you chain FLAC files together.
    if ((d->spec.freq != (int) frame->header.sample_rate) || (d->spec.channels != sdlchannels) || (d->bits_per_sample != (int) frame->header.bits_per_sample)) {
        d->bits_per_sample = (int) frame->header.bits_per_sample;
        d->spec.freq = (int) frame->header.sample_rate;
        d->spec.channels = sdlchannels;
        // decoded FLAC data is always int, from 4 to 32 bits, apparently.
        if (d->bits_per_sample <= 8) {
            d->spec.format = SDL_AUDIO_S8;
        } else if (d->bits_per_sample <= 16) {
            d->spec.format = SDL_AUDIO_S16;
        } else if (d->bits_per_sample <= 32) {
            d->spec.format = SDL_AUDIO_S32;
        }

        SDL_SetAudioStreamFormat(stream, &d->spec, NULL);
        if (headerchannels == 3) {
            static const int chmap[] = { 0, 1, 2, -1, -1, -1 };
            SDL_SetAudioStreamInputChannelMap(stream, chmap, SDL_arraysize(chmap));
        } else {
            SDL_SetAudioStreamInputChannelMap(stream, NULL, 0);
        }
    }

    // !!! FIXME: this is a copy/paste from decoder_vorbis.c
    #define INTERLEAVE(typ, shift) { \
        typ samples[256]; \
        if (headerchannels== 3) { SDL_zeroa(samples); }  /* prevent valgrind complaints */ \
        const int max_frames = SDL_arraysize(samples) / sdlchannels;  /* most sample frames per chunk. */ \
        const int num_chunks = (int) (frame->header.blocksize / max_frames); \
        const int chunksize = max_frames * sdlchannels * sizeof (typ); \
        int iframe = 0; \
        for (int chunk = 0; chunk < num_chunks; chunk++) { \
            typ *fptr = samples; \
            for (int i = 0; i < max_frames; i++) { \
                for (int channel = 0; channel < headerchannels; channel++) { \
                    *(fptr++) = (typ) (buffer[channel][iframe] << shift); \
                } \
                iframe++; \
                fptr += (sdlchannels - headerchannels); \
            } \
            SDL_PutAudioStreamData(stream, samples, chunksize); \
        } \
        /* put the last bit. */ \
        SDL_assert(iframe <= frame->header.blocksize); \
        SDL_assert(((frame->header.blocksize - iframe) * sdlchannels) <= SDL_arraysize(samples)); \
        const int remaining = frame->header.blocksize - iframe; \
        if (remaining > 0) { \
            typ *fptr = samples; \
            for (int i = 0; i < remaining; i++) { \
                for (int channel = 0; channel < headerchannels; channel++) { \
                    *(fptr++) = (typ) (buffer[channel][iframe] << shift); \
                } \
                iframe++; \
                fptr += (sdlchannels - headerchannels); \
            } \
            SDL_PutAudioStreamData(stream, samples, remaining * sdlchannels * sizeof (typ)); \
        } \
        SDL_assert(iframe == frame->header.blocksize); \
    }

    const int shift = SDL_AUDIO_BITSIZE(d->spec.format) - d->bits_per_sample;

    switch (d->spec.format) {
        case SDL_AUDIO_S8: INTERLEAVE(Sint8, shift); break;
        case SDL_AUDIO_S16: INTERLEAVE(Sint16, shift); break;
        case SDL_AUDIO_S32: INTERLEAVE(Sint32, shift); break;
        default: SDL_assert(!"Unexpected audio data type"); return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }

    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void FLAC_IoMetadata(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *userdata)
{
    FLAC_UserData *d = (FLAC_UserData *) userdata;
    FLAC_AudioUserData *payload = (FLAC_AudioUserData *) d->payload;  // cast away constness here. This metadata callback is when we're initializing payload.

    if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
        d->spec.freq = metadata->data.stream_info.sample_rate;
        d->spec.channels = metadata->data.stream_info.channels;
        d->spec.format = SDL_AUDIO_S32;
        d->bits_per_sample = (int) metadata->data.stream_info.bits_per_sample;
    } else if (metadata->type == FLAC__METADATA_TYPE_VORBIS_COMMENT) {
        const FLAC__StreamMetadata_VorbisComment *vc = &metadata->data.vorbis_comment;
        const int num_comments = (int) vc->num_comments;
        char **comments = (char **) SDL_malloc(num_comments * sizeof (*comments));
        if (comments) {
            for (int i = 0; i < num_comments; i++) {
            	comments[i] = (char *) vc->comments[i].entry;
            }
            Mix_ParseOggComments(payload->props, d->spec.freq, (const char *) vc->vendor_string.entry, (const char * const *) comments, num_comments, &payload->loop_start, &payload->loop_end, &payload->loop_len);
            SDL_free(comments);
        }
    }
}

static void FLAC_IoError(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data)
{
    // (currently) don't care.
}

static void FLAC_IoMetadataNoOp(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *userdata)
{
    // don't care about metadata at this point.
}

static bool SDLCALL FLAC_init(void)
{
    if (!LoadModule_flac()) {
        return false;
    }
    return true;
}

static void SDLCALL FLAC_quit(void)
{
    UnloadModule_flac();
}

static bool SDLCALL FLAC_init_audio(SDL_IOStream *io, SDL_AudioSpec *spec, SDL_PropertiesID props, Sint64 *duration_frames, void **audio_userdata)
{
    // just load the bare minimum from the IOStream to verify it's a FLAC file (if it's an Ogg stream, we'll let libFLAC try to parse it out).
    bool is_ogg_stream = false;

    char magic[4];
    if (SDL_ReadIO(io, magic, 4) != 4) {
        return false;
    } else if (SDL_memcmp(magic, "OggS", 4) == 0) {
        is_ogg_stream = true;  // MAYBE flac, might be vorbis, etc.
    } else if (SDL_memcmp(magic, "fLaC", 4) != 0) {
        return SDL_SetError("Not a FLAC audio stream");
    }

    // rewind, let libFLAC process through the metadata, so we know it's definitely a FLAC file and we have the props. Then we'll load the whole thing into RAM.
    if (SDL_SeekIO(io, 0, SDL_IO_SEEK_SET) < 0) {
        return false;
    }

    FLAC_UserData d;
    SDL_zero(d);
    d.io = io;

    d.decoder = flac.FLAC__stream_decoder_new();
    if (!d.decoder) {
        return SDL_SetError("FLAC__stream_decoder_new() failed");
    }

    flac.FLAC__stream_decoder_set_metadata_respond(d.decoder, FLAC__METADATA_TYPE_VORBIS_COMMENT);

    FLAC_AudioUserData *payload = (FLAC_AudioUserData *) SDL_calloc(1, sizeof (*payload));
    if (!payload) {
        flac.FLAC__stream_decoder_delete(d.decoder);
        return false;
    }

    payload->is_ogg_stream = is_ogg_stream;
    payload->props = props;

    d.payload = payload;

    FLAC__StreamDecoderInitStatus ret;
    if (is_ogg_stream) {
        ret = flac.FLAC__stream_decoder_init_ogg_stream(d.decoder, FLAC_IoRead, FLAC_IoSeek, FLAC_IoTell, FLAC_IoLength, FLAC_IoEOF, FLAC_IoWriteNoOp, FLAC_IoMetadata, FLAC_IoError, &d);
    } else {
        ret = flac.FLAC__stream_decoder_init_stream(d.decoder, FLAC_IoRead, FLAC_IoSeek, FLAC_IoTell, FLAC_IoLength, FLAC_IoEOF, FLAC_IoWriteNoOp, FLAC_IoMetadata, FLAC_IoError, &d);
    }

    if (ret != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        flac.FLAC__stream_decoder_delete(d.decoder);
        return SDL_SetError("FLAC__stream_decoder_init_stream() failed");
    }

    const bool rc = !!flac.FLAC__stream_decoder_process_until_end_of_metadata(d.decoder);
    const Sint64 total_frames = rc ? (Sint64) flac.FLAC__stream_decoder_get_total_samples(d.decoder) : -1;

    // Dump this stream no matter what. Either we failed, or we know it's a real FLAC and have its metadata pushed to `props`.
    flac.FLAC__stream_decoder_finish(d.decoder);
    flac.FLAC__stream_decoder_delete(d.decoder);

    if (!rc) {
        SDL_SetError("FLAC__stream_decoder_process_until_end_of_metadata() failed");
    }

    // metadata processing should have filled in several things in `payload`.
    payload->props = 0;  // metadata callbacks needed this, but don't store this past this function.

    // now rewind, load the whole thing to memory, and use that buffer for future processing.
    if (SDL_SeekIO(io, 0, SDL_IO_SEEK_SET) < 0) {
        return false;
    }
    size_t datalen = 0;
    Uint8 *data = (Uint8 *) SDL_LoadFile_IO(io, &datalen, false);
    if (!data) {
        return false;
    }

    payload->data = data;
    payload->datalen = datalen;

    SDL_copyp(spec, &d.spec);
    *duration_frames = total_frames;
    *audio_userdata = payload;

    return true;
}

bool SDLCALL FLAC_init_track(void *audio_userdata, const SDL_AudioSpec *spec, SDL_PropertiesID props, void **userdata)
{
    FLAC_UserData *d = (FLAC_UserData *) SDL_calloc(1, sizeof (*d));
    if (!d) {
        return false;
    }

    const FLAC_AudioUserData *payload = (const FLAC_AudioUserData *) audio_userdata;
    d->payload = payload;
    d->io = SDL_IOFromConstMem(payload->data, payload->datalen);
    if (!d->io) {
        SDL_free(d);
        return false;
    }

    d->decoder = flac.FLAC__stream_decoder_new();
    if (!d->decoder) {
        SDL_CloseIO(d->io);
        SDL_free(d);
        return SDL_SetError("FLAC__stream_decoder_new() failed");
    }

    FLAC__StreamDecoderInitStatus ret;
    if (payload->is_ogg_stream) {
        ret = flac.FLAC__stream_decoder_init_ogg_stream(d->decoder, FLAC_IoRead, FLAC_IoSeek, FLAC_IoTell, FLAC_IoLength, FLAC_IoEOF, FLAC_IoWrite, FLAC_IoMetadataNoOp, FLAC_IoError, d);
    } else {
        ret = flac.FLAC__stream_decoder_init_stream(d->decoder, FLAC_IoRead, FLAC_IoSeek, FLAC_IoTell, FLAC_IoLength, FLAC_IoEOF, FLAC_IoWrite, FLAC_IoMetadataNoOp, FLAC_IoError, d);
    }

    if (ret != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        flac.FLAC__stream_decoder_delete(d->decoder);
        SDL_CloseIO(d->io);
        SDL_free(d);
        return SDL_SetError("FLAC__stream_decoder_init_stream() failed");
    }

    if (!flac.FLAC__stream_decoder_process_until_end_of_metadata(d->decoder)) {
        flac.FLAC__stream_decoder_finish(d->decoder);
        flac.FLAC__stream_decoder_delete(d->decoder);
        SDL_CloseIO(d->io);
        SDL_free(d);
        SDL_SetError("FLAC__stream_decoder_process_until_end_of_metadata() failed");
    }

    SDL_copyp(&d->spec, spec);

    *userdata = d;
    return true;
}

bool SDLCALL FLAC_decode(void *userdata, SDL_AudioStream *stream)
{
    FLAC_UserData *d = (FLAC_UserData *) userdata;
    d->stream = stream;
    return !!flac.FLAC__stream_decoder_process_single(d->decoder);  // write callback will fill in stream. Might fill 0 if it hit a metadata block, but the higher level loops to get what it needs.
}

bool SDLCALL FLAC_seek(void *userdata, Uint64 frame)
{
    FLAC_UserData *d = (FLAC_UserData *) userdata;
    if (!flac.FLAC__stream_decoder_seek_absolute(d->decoder, frame)) {
        if (flac.FLAC__stream_decoder_get_state(d->decoder) == FLAC__STREAM_DECODER_SEEK_ERROR) {
            flac.FLAC__stream_decoder_flush(d->decoder);
        }
        return SDL_SetError("Seeking of FLAC stream failed: libFLAC seek failed.");
    }
    return true;
}

void SDLCALL FLAC_quit_track(void *userdata)
{
    FLAC_UserData *d = (FLAC_UserData *) userdata;
    d->stream = NULL;
    flac.FLAC__stream_decoder_finish(d->decoder);
    flac.FLAC__stream_decoder_delete(d->decoder);
    SDL_CloseIO(d->io);
    SDL_free(d);
}

void SDLCALL FLAC_quit_audio(void *audio_userdata)
{
    FLAC_AudioUserData *d = (FLAC_AudioUserData *) audio_userdata;
    SDL_free((void *) d->data);
    SDL_free(d);
}

Mix_Decoder Mix_Decoder_FLAC = {
    "FLAC",
    FLAC_init,
    FLAC_init_audio,
    FLAC_init_track,
    FLAC_decode,
    FLAC_seek,
    FLAC_quit_track,
    FLAC_quit_audio,
    FLAC_quit
};

