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

#define DR_FLAC_IMPLEMENTATION
#if defined(__GNUC__) && (__GNUC__ >= 4) && \
  !(defined(_WIN32) || defined(__EMX__))
#define DRFLAC_API __attribute__((visibility("hidden")))
#elif defined(__APPLE__)
#define DRFLAC_API __private_extern__
#else
#define DRFLAC_API /* just in case.. */
#endif
#define DR_FLAC_NO_STDIO
#define DRFLAC_ASSERT(expression) SDL_assert((expression))
#define DRFLAC_COPY_MEMORY(dst, src, sz) SDL_memcpy((dst), (src), (sz))
#define DRFLAC_MOVE_MEMORY(dst, src, sz) SDL_memmove((dst), (src), (sz))
#define DRFLAC_ZERO_MEMORY(p, sz) SDL_memset((p), 0, (sz))
#define DRFLAC_MALLOC(sz) SDL_malloc((sz))
#define DRFLAC_REALLOC(p, sz) SDL_realloc((p), (sz))
#define DRFLAC_FREE(p) SDL_free((p))
#include "dr_libs/dr_flac.h"



typedef struct DRFLAC_AudioUserData
{
    void *buffer;
    size_t buflen;
    size_t framesize;
    bool loop;
    Sint64 loop_start;
    Sint64 loop_end;
    Sint64 loop_len;
} DRFLAC_AudioUserData;

typedef struct DRFLAC_UserData
{
    const DRFLAC_AudioUserData *payload;
    drflac *decoder;
} DRFLAC_UserData;


// the i/o callbacks are only used for initial open, so it can read as little
//  as possible to verify it's really a FLAC file. After we're sure it is, we
//  pull the whole thing into RAM.

static size_t DRFLAC_IoRead(void *context, void *buf, size_t size)
{
    return SDL_ReadIO((SDL_IOStream *) context, buf, size);
}

static drflac_bool32 DRFLAC_IoSeek(void *context, int offset, drflac_seek_origin origin)
{
    // SDL_IOWhence and drflac_seek_origin happen to match up.
    return (SDL_SeekIO((SDL_IOStream *) context, offset, (SDL_IOWhence) origin) < 0) ? DRFLAC_FALSE : DRFLAC_TRUE;
}


typedef struct DRFLAC_Metadata {
    char *vendor;
    char **comments;
    int num_comments;
} DRFLAC_Metadata;

static void FreeMetadata(DRFLAC_Metadata *metadata)
{
    for (int i = 0; i < metadata->num_comments; i++) {
        SDL_free(metadata->comments[i]);
    }
    SDL_free(metadata->comments);
    SDL_free(metadata->vendor);
}

static void DRFLAC_OnMetadata(void *context, drflac_metadata *pMetadata)
{
    if (pMetadata->type == DRFLAC_METADATA_BLOCK_TYPE_VORBIS_COMMENT) {
        DRFLAC_Metadata *metadata = (DRFLAC_Metadata *) context;

        if (!metadata->vendor) {
            metadata->vendor = (char *) SDL_malloc(pMetadata->data.vorbis_comment.vendorLength + 1);
            if (metadata->vendor) {
                SDL_memcpy(metadata->vendor, pMetadata->data.vorbis_comment.vendor, pMetadata->data.vorbis_comment.vendorLength);
                metadata->vendor[pMetadata->data.vorbis_comment.vendorLength] = '\0';
            }
        }

        const char *comment;
        drflac_uint32 commentlen = 0;
        drflac_vorbis_comment_iterator iter;
        drflac_init_vorbis_comment_iterator(&iter, pMetadata->data.vorbis_comment.commentCount, pMetadata->data.vorbis_comment.pComments);

        while ((comment = drflac_next_vorbis_comment(&iter, &commentlen)) != NULL) {
            void *ptr = SDL_realloc(metadata->comments, sizeof (char *) * (metadata->num_comments + 1));
            if (ptr) {
                metadata->comments = (char **) ptr;
                char *str = (char *) SDL_malloc(commentlen + 1);
                if (str) {
                    SDL_memcpy(str, comment, commentlen);
                    str[commentlen] = '\0';
                    metadata->comments[metadata->num_comments++] = str;
                }
            }
        }
    }
}

static bool SDLCALL DRFLAC_init_audio(SDL_IOStream *io, SDL_AudioSpec *spec, SDL_PropertiesID props, Sint64 *duration_frames, void **audio_userdata)
{
    // do an initial load from the IOStream directly, so it can determine if this is really a FLAC file without
    //  reading a lot of the stream into memory first.
    drflac *decoder = drflac_open(DRFLAC_IoRead, DRFLAC_IoSeek, io, NULL);
    if (!decoder) {
        return false;  // probably not a FLAC file.
    }
    drflac_close(decoder);

    // suck the whole thing into memory and work out of there from now on.
    if (SDL_SeekIO(io, SDL_IO_SEEK_SET, 0) == -1) {
        return false;
    }

    DRFLAC_AudioUserData *payload = (DRFLAC_AudioUserData *) SDL_calloc(1, sizeof(*payload));
    if (!payload) {
        return false;
    }

    payload->buffer = SDL_LoadFile_IO(io, &payload->buflen, false);
    if (!payload->buffer) {
        SDL_free(payload);
        return false;
    }

    DRFLAC_Metadata metadata;
    SDL_zero(metadata);

    decoder = drflac_open_memory_with_metadata(payload->buffer, payload->buflen, DRFLAC_OnMetadata, &metadata, NULL);
    if (!decoder) {
        FreeMetadata(&metadata);
        SDL_free(payload->buffer);
        SDL_free(payload);
        return false;
    }

    MIX_ParseOggComments(props, (int) decoder->sampleRate, metadata.vendor, (const char * const *) metadata.comments, metadata.num_comments, &payload->loop_start, &payload->loop_end, &payload->loop_len);
    FreeMetadata(&metadata);

    spec->format = SDL_AUDIO_F32;
    spec->channels = (int) decoder->channels;
    spec->freq = (int) decoder->sampleRate;

    *duration_frames = (decoder->totalPCMFrameCount == 0) ? MIX_DURATION_UNKNOWN : (Sint64) decoder->totalPCMFrameCount;

    payload->loop = ((payload->loop_end > 0) && (payload->loop_end <= *duration_frames) && (payload->loop_start < payload->loop_end));
    if (payload->loop) {
        *duration_frames = MIX_DURATION_INFINITE;  // if looping, stream is infinite.
    }

    drflac_close(decoder);

    payload->framesize = SDL_AUDIO_FRAMESIZE(*spec);

    *audio_userdata = payload;

    return true;
}

static bool SDLCALL DRFLAC_init_track(void *audio_userdata, const SDL_AudioSpec *spec, SDL_PropertiesID props, void **userdata)
{
    const DRFLAC_AudioUserData *payload = (const DRFLAC_AudioUserData *) audio_userdata;
    DRFLAC_UserData *d = (DRFLAC_UserData *) SDL_calloc(1, sizeof (*d));
    if (!d) {
        return false;
    }

    d->decoder = drflac_open_memory(payload->buffer, payload->buflen, NULL);
    if (!d->decoder) {
        SDL_free(d);
        return false;
    }

    d->payload = payload;
    *userdata = d;

    return true;
}

static bool SDLCALL DRFLAC_decode(void *userdata, SDL_AudioStream *stream)
{
    DRFLAC_UserData *d = (DRFLAC_UserData *) userdata;
    const int framesize = d->payload->framesize;
    float samples[256];
    const drflac_uint64 rc = drflac_read_pcm_frames_f32(d->decoder, sizeof (samples) / framesize, samples);
    if (!rc) {
        return false;  // done decoding.
    }
    SDL_PutAudioStreamData(stream, samples, rc * framesize);
    return true;
}

static bool SDLCALL DRFLAC_seek(void *userdata, Uint64 frame)
{
    DRFLAC_UserData *d = (DRFLAC_UserData *) userdata;
    return !!drflac_seek_to_pcm_frame(d->decoder, (drflac_uint64) frame);
}

static void SDLCALL DRFLAC_quit_track(void *userdata)
{
    DRFLAC_UserData *d = (DRFLAC_UserData *) userdata;
    drflac_close(d->decoder);
    SDL_free(d);
}

static void SDLCALL DRFLAC_quit_audio(void *audio_userdata)
{
    DRFLAC_AudioUserData *payload = (DRFLAC_AudioUserData *) audio_userdata;
    SDL_free(payload->buffer);
    SDL_free(payload);
}

MIX_Decoder MIX_Decoder_DRFLAC = {
    "DRFLAC",
    NULL,  // init
    DRFLAC_init_audio,
    DRFLAC_init_track,
    DRFLAC_decode,
    DRFLAC_seek,
    DRFLAC_quit_track,
    DRFLAC_quit_audio,
    NULL  // quit
};

